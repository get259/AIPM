#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <esp_http_server.h>

struct RemoteEvent {
    uint32_t client = 0;
    uint32_t id = 0;
    uint32_t recording = 0;
    uint32_t sequence = 0;
    std::string op;
    uint8_t state = 0;
    std::vector<uint8_t> pcm;
};

// HTTPS 服务不依赖 Application；回调只提交事件，实际动作由主任务执行。
class RemoteServer {
public:
    static RemoteServer& Get();
    bool Start(std::function<bool(RemoteEvent&&)> handler);
    bool IsClientConnected(uint32_t client);
    void Reply(const RemoteEvent& event, bool ok, const char* message, bool enabled,
               bool recording, uint32_t owner, bool platform);
private:
    httpd_handle_t server_ = nullptr;
    httpd_handle_t control_server_ = nullptr;
    std::function<bool(RemoteEvent&&)> handler_;
    static esp_err_t Socket(httpd_req_t* req);
};
