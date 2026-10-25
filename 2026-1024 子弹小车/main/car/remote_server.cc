#include "remote_server.h"
#include "remote_wire.h"
#include "remote_http.h"
#include "remote_transport.h"
#include "remote_page_metadata.h"
#include <esp_https_server.h>
#include <esp_log.h>
#include <cJSON.h>
#include <atomic>
#include <mutex>
#include <map>
#include <memory>
#include <cstring>
#include <cmath>

extern const char page_start[] asm("_binary_remote_page_html_start");
extern const uint8_t gzip_start[] asm("_binary_remote_page_html_gz_start");
extern const uint8_t gzip_end[] asm("_binary_remote_page_html_gz_end");
extern const char capture_js_start[] asm("_binary_capture_js_start");
extern const uint8_t server_cert_start[] asm("_binary_server_cert_pem_start");
extern const uint8_t server_cert_end[] asm("_binary_server_cert_pem_end");
extern const uint8_t server_key_start[] asm("_binary_server_key_pem_start");
extern const uint8_t server_key_end[] asm("_binary_server_key_pem_end");
extern const char root_ca_start[] asm("_binary_root_ca_crt_start");
extern const uint8_t root_der_start[] asm("_binary_car_root_ca_cer_start");
extern const uint8_t root_der_end[] asm("_binary_car_root_ca_cer_end");

namespace {
struct Client { uint32_t token; int fd; RemoteWire wire; };
std::mutex clients_mutex;
std::map<uint32_t, Client*> clients;
std::atomic<uint32_t> next_token{0};
uint32_t Read32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
bool Number(cJSON* root, const char* key, uint32_t& value) {
    auto item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0 || item->valuedouble > UINT32_MAX ||
        std::floor(item->valuedouble) != item->valuedouble) return false;
    value = static_cast<uint32_t>(item->valuedouble);
    return true;
}
esp_err_t Asset(httpd_req_t* req) {
    const char* body = static_cast<const char*>(req->user_ctx);
    httpd_resp_set_type(req, strstr(req->uri, ".js") ? "text/javascript; charset=utf-8" :
        strstr(req->uri, ".crt") ? "application/x-x509-ca-cert" : "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Content-Security-Policy", kRemote_page_csp);
    const bool page = body == page_start;
    char accept[160] = {};
    httpd_req_get_hdr_value_str(req, "Accept-Encoding", accept, sizeof(accept));
    const bool gzip = page && RemoteAcceptsGzip(accept);
    const char* etag = page ? (gzip ? kRemote_gzip_etag : kRemote_page_etag) :
        body == capture_js_start ? kRemote_capture_etag : nullptr;
    if (page) httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    if (etag) {
        httpd_resp_set_hdr(req, "ETag", etag);
        char previous[80] = {};
        if (httpd_req_get_hdr_value_str(req, "If-None-Match", previous, sizeof(previous)) == ESP_OK && !strcmp(previous,etag)) {
            httpd_resp_set_status(req, "304 Not Modified");
            return httpd_resp_send(req, nullptr, 0);
        }
    }
    if (gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, reinterpret_cast<const char*>(gzip_start), gzip_end-gzip_start);
    }
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}
}

RemoteServer& RemoteServer::Get() { static RemoteServer instance; return instance; }

bool RemoteServer::IsClientConnected(uint32_t client) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    return clients.find(client) != clients.end();
}

bool RemoteServer::Start(std::function<bool(RemoteEvent&&)> handler) {
    if (server_) return true;
    handler_ = std::move(handler);
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.httpd.ctrl_port = 32769; // 与既有 HTTP 配网服务分开。
    config.httpd.max_open_sockets = 3;
    config.httpd.max_uri_handlers = 8;
    config.httpd.stack_size = 8192;
    config.httpd.lru_purge_enable = true;
    config.httpd.recv_wait_timeout = 5;
    config.httpd.send_wait_timeout = 5;
    // 复用原接收等待上限，空闲预连接不得无限占用握手任务。
    config.tls_handshake_timeout_ms = config.httpd.recv_wait_timeout * 1000;
    config.servercert = server_cert_start;
    config.servercert_len = server_cert_end - server_cert_start;
    config.prvtkey_pem = server_key_start;
    config.prvtkey_len = server_key_end - server_key_start;
    if (httpd_ssl_start(&server_, &config) != ESP_OK) { server_ = nullptr; return false; }
    const struct { const char* uri; const char* body; } assets[] = {
        {"/", page_start}, {"/remote", page_start},
        {"/capture.js", capture_js_start}, {"/root-ca.crt", root_ca_start}
    };
    for (const auto& asset : assets) {
        httpd_uri_t uri = {};
        uri.uri = asset.uri; uri.method = HTTP_GET; uri.handler = Asset; uri.user_ctx = const_cast<char*>(asset.body);
        ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &uri));
    }
    httpd_uri_t der = {};
    der.uri = "/car-root-ca.cer"; der.method = HTTP_GET;
    der.handler = [](httpd_req_t* req) {
        httpd_resp_set_type(req, "application/x-x509-ca-cert");
        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=car-root-ca.cer");
        return httpd_resp_send(req, reinterpret_cast<const char*>(root_der_start), root_der_end - root_der_start);
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server_, &der));
    RemoteTransport::config.servercert_buf = reinterpret_cast<const unsigned char*>(server_cert_start);
    RemoteTransport::config.servercert_bytes = server_cert_end - server_cert_start;
    RemoteTransport::config.serverkey_buf = reinterpret_cast<const unsigned char*>(server_key_start);
    RemoteTransport::config.serverkey_bytes = server_key_end - server_key_start;
    RemoteTransport::config.tls_handshake_timeout_ms = config.tls_handshake_timeout_ms;
    httpd_config_t control = HTTPD_DEFAULT_CONFIG();
    control.server_port = 8443;
    control.ctrl_port = 32770;
    control.max_open_sockets = 2;
    control.max_uri_handlers = 1;
    control.stack_size = 8192;
    control.lru_purge_enable = false;
    control.open_fn = RemoteTransport::Open;
    if (httpd_start(&control_server_, &control) != ESP_OK) {
        httpd_ssl_stop(server_); server_ = nullptr; return false;
    }
    httpd_uri_t ws = {};
    ws.uri = "/control"; ws.method = HTTP_GET; ws.handler = Socket; ws.is_websocket = true;
    ws.handle_ws_control_frames = false;
    ESP_ERROR_CHECK(httpd_register_uri_handler(control_server_, &ws));
    ESP_LOGI("WebRemote", "Ready: https://192.168.4.1 (trust the local CA before recording)");
    return true;
}

esp_err_t RemoteServer::Socket(httpd_req_t* req) {
    auto& self = Get();
    if (req->method == HTTP_GET) {
        char origin[96] = {};
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) != ESP_OK ||
            strcmp(origin, "https://192.168.4.1") != 0) return ESP_FAIL;
        auto client = new Client{++next_token, httpd_req_to_sockfd(req), {}};
        req->sess_ctx = client;
        req->free_ctx = [](void* context) {
            auto client = static_cast<Client*>(context);
            { std::lock_guard<std::mutex> lock(clients_mutex); clients.erase(client->token); }
            RemoteEvent event; event.client = client->token; event.op = "disconnect";
            if (Get().handler_) Get().handler_(std::move(event));
            delete client;
        };
        { std::lock_guard<std::mutex> lock(clients_mutex); clients[client->token] = client; }
        return ESP_OK;
    }
    auto client = static_cast<Client*>(req->sess_ctx);
    if (!client) return ESP_FAIL;
    httpd_ws_frame_t ws = {};
    if (httpd_ws_recv_frame(req, &ws, 0) != ESP_OK || ws.len > 1932) return ESP_FAIL;
    if (ws.type != HTTPD_WS_TYPE_TEXT && ws.type != HTTPD_WS_TYPE_BINARY && ws.type != HTTPD_WS_TYPE_CONTINUE) return ESP_FAIL;
    std::vector<uint8_t> data(ws.len + 1);
    ws.payload = data.data();
    if (httpd_ws_recv_frame(req, &ws, ws.len) != ESP_OK) return ESP_FAIL;
    const auto result = client->wire.Append(static_cast<uint8_t>(ws.type), ws.final, data.data(), ws.len);
    if (result == RemoteWire::Result::Invalid) return ESP_FAIL;
    if (result == RemoteWire::Result::More) return ESP_OK;
    data = client->wire.Take();
    RemoteEvent event; event.client = client->token;
    if (client->wire.Type() == HTTPD_WS_TYPE_BINARY) {
        if (data.size() < 14 || data.size() > 1932 || data.size() % 2) return ESP_FAIL;
        event.op = "pcm"; event.id = Read32(data.data()); event.recording = Read32(data.data() + 4);
        event.sequence = Read32(data.data() + 8);
        event.pcm.assign(data.begin() + 12, data.end());
    } else {
        if (data.empty() || data.size() > 384 || memchr(data.data(), 0, data.size())) return ESP_FAIL;
        data.push_back(0);
        auto root = cJSON_Parse(reinterpret_cast<char*>(data.data()));
        auto op = cJSON_GetObjectItemCaseSensitive(root, "op");
        const bool valid = root && cJSON_IsString(op) && Number(root, "id", event.id) && event.id != 0;
        if (valid) {
            event.op = op->valuestring;
            if (event.op == "move") { uint32_t value; if (!Number(root, "state", value) ||
                !(value == 0 || value == 1 || value == 2 || value == 4 || value == 8)) { cJSON_Delete(root); return ESP_FAIL; } event.state = value; }
            if (event.op == "end" && !Number(root, "recording", event.recording)) { cJSON_Delete(root); return ESP_FAIL; }
        }
        cJSON_Delete(root);
        if (!valid) return ESP_FAIL;
    }
    if (!client->wire.AcceptRequest(event.id)) return ESP_FAIL;
    if (!self.handler_ || !self.handler_(std::move(event))) return ESP_FAIL; // 超限关闭连接，不能无限排队。
    return ESP_OK;
}

void RemoteServer::Reply(const RemoteEvent& event, bool ok, const char* message, bool enabled,
                         bool recording, uint32_t owner, bool platform) {
    auto root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "id", event.id); cJSON_AddBoolToObject(root, "ok", ok);
    // 高频 PCM 成功确认只返回编号，避免重复序列化和渲染整份状态。
    if (event.op != "pcm" || !ok) {
    cJSON_AddStringToObject(root, "message", message); cJSON_AddBoolToObject(root, "enabled", enabled);
    cJSON_AddBoolToObject(root, "recording", recording); cJSON_AddBoolToObject(root, "mine", owner == event.client);
    cJSON_AddBoolToObject(root, "owned", owner != 0); cJSON_AddBoolToObject(root, "platform", platform);
    }
    char* json = cJSON_PrintUnformatted(root);
    struct Reply { httpd_handle_t server; uint32_t client; std::string json; };
    auto reply = new Reply{control_server_, event.client, json ? json : "{}"};
    cJSON_free(json); cJSON_Delete(root);
    if (httpd_queue_work(control_server_, [](void* context) {
        std::unique_ptr<Reply> reply(static_cast<Reply*>(context));
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto found = clients.find(reply->client);
        if (found == clients.end()) return; // token 隔离已复用的 socket 描述符。
        httpd_ws_frame_t frame = {}; frame.type = HTTPD_WS_TYPE_TEXT; frame.final = true;
        frame.payload = reinterpret_cast<uint8_t*>(&reply->json[0]); frame.len = reply->json.size();
        if (httpd_ws_send_frame_async(reply->server, found->second->fd, &frame) != ESP_OK)
            httpd_sess_trigger_close(reply->server, found->second->fd);
    }, reply) != ESP_OK) delete reply;
}
