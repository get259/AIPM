#include "dns_server.h"
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include "sdkconfig.h"

#define TAG "DnsServer"

DnsServer::DnsServer() {
}

DnsServer::~DnsServer() {
    Stop();
}

void DnsServer::Start(esp_ip4_addr_t gateway) {
    // If already running, stop first
    if (running_) {
        Stop();
    }

    ESP_LOGI(TAG, "Starting DNS server");
    gateway_ = gateway;

    fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd_ < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port_);

    if (bind(fd_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "failed to bind port %d", port_);
        close(fd_);
        fd_ = -1;
        return;
    }

    running_ = true;
    xTaskCreate([](void* arg) {
        DnsServer* dns_server = static_cast<DnsServer*>(arg);
        dns_server->Run();
        vTaskDelete(NULL);
    }, "DnsServerTask", 4096, this, 5, &task_handle_);
}

void DnsServer::Stop() {
    if (!running_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping DNS server");
    running_ = false;

    // Close socket to unblock recvfrom
    if (fd_ >= 0) {
        shutdown(fd_, SHUT_RDWR);
        close(fd_);
        fd_ = -1;
    }

    // Wait for task to finish
    if (task_handle_ != nullptr) {
        // Give the task some time to exit gracefully
        vTaskDelay(pdMS_TO_TICKS(100));
        task_handle_ = nullptr;
    }
}

void DnsServer::Run() {
    char buffer[512];
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int len = recvfrom(fd_, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_addr_len);
        if (len < 0) {
            if (!running_) {
                // Socket was closed during Stop(), exit gracefully
                break;
            }
            ESP_LOGE(TAG, "recvfrom failed, errno=%d", errno);
            continue;
        }

        if (!running_) {
            break;
        }

        // 忽略短包与响应包，预留传统配网回复所需的 16 字节。
        if (len < 12 || len > static_cast<int>(sizeof(buffer)) - 16 ||
            (static_cast<unsigned char>(buffer[2]) & 0x80)) continue;
#if CONFIG_CAR_WEB_REMOTE
        // 常开遥控热点仅通过 IP 访问，不劫持手机后台网站的域名。
        // 快速返回 NXDOMAIN，避免外站 HTTPS 被引向本机并反复握手失败。
        buffer[2] = static_cast<char>((static_cast<unsigned char>(buffer[2]) & 0x79) | 0x80);
        buffer[3] = 0x03;
        buffer[6] = buffer[7] = buffer[8] = buffer[9] = 0;
        sendto(fd_, buffer, len, 0, (struct sockaddr *)&client_addr, client_addr_len);
        continue;
#endif
        // 普通配网模式仍将域名指向设备，以保留自动配网入口。
        buffer[2] |= 0x80;  // Set response flag
        buffer[3] |= 0x80;  // Set Recursion Available
        buffer[7] = 1;      // Set answer count to 1

        // Add answer section
        memcpy(&buffer[len], "\xc0\x0c", 2);  // Name pointer
        len += 2;
        memcpy(&buffer[len], "\x00\x01\x00\x01\x00\x00\x00\x1c\x00\x04", 10);  // Type, class, TTL, data length
        len += 10;
        memcpy(&buffer[len], &gateway_.addr, 4);  // 192.168.4.1
        len += 4;
        ESP_LOGI(TAG, "Sending DNS response to %s", inet_ntoa(gateway_.addr));

        sendto(fd_, buffer, len, 0, (struct sockaddr *)&client_addr, client_addr_len);
    }

    task_handle_ = nullptr;
    ESP_LOGI(TAG, "DNS server task exiting");
}
