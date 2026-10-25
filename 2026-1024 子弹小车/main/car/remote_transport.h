#pragma once
#include <esp_http_server.h>
#include <esp_tls.h>
#include <atomic>

// 页面服务与控制服务分别运行；控制服务仅接受一条连接。
// 在 TLS 分配和握手之前拒绝额外连接，避免拖住已建立的控制流。
namespace RemoteTransport {
inline std::atomic<bool> occupied{false};
inline esp_tls_cfg_server_t config{};
inline void Close(void* context) {
    esp_tls_server_session_delete(static_cast<esp_tls_t*>(context));
    occupied = false;
}
inline esp_err_t Open(httpd_handle_t server, int fd) {
    if (occupied.exchange(true)) return ESP_FAIL;
    auto tls = esp_tls_init();
    if (!tls) { occupied = false; return ESP_ERR_NO_MEM; }
    if (esp_tls_server_session_create(&config, fd, tls) != 0) {
        esp_tls_server_session_delete(tls);
        occupied = false;
        return ESP_FAIL;
    }
    httpd_sess_set_transport_ctx(server, fd, tls, Close);
    httpd_sess_set_send_override(server, fd, [](httpd_handle_t h, int s, const char* b, size_t n, int) {
        return static_cast<int>(esp_tls_conn_write(static_cast<esp_tls_t*>(httpd_sess_get_transport_ctx(h,s)), b,n));
    });
    httpd_sess_set_recv_override(server, fd, [](httpd_handle_t h, int s, char* b, size_t n, int) {
        return static_cast<int>(esp_tls_conn_read(static_cast<esp_tls_t*>(httpd_sess_get_transport_ctx(h,s)), b,n));
    });
    httpd_sess_set_pending_override(server, fd, [](httpd_handle_t h, int s) {
        return esp_tls_get_bytes_avail(static_cast<esp_tls_t*>(httpd_sess_get_transport_ctx(h,s)));
    });
    return ESP_OK;
}
}
