#include "application.h"
#include <esp_log.h>

void Application::StartRemoteSender() {
    if (remote_sender_task_) return;
    // 仅网络发送放在独立任务，手机 PCM 整理与原 Opus 编码可同时运行。
    if (xTaskCreate([](void* context) {
        auto app = static_cast<Application*>(context);
        while (true) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            auto protocol = app->remote_sender_protocol_;
            auto packet = std::move(app->remote_sender_packet_);
            const bool ok = protocol && protocol->SendAudioForEpoch(
                std::move(packet), app->remote_sender_epoch_);
            app->remote_sender_protocol_.reset();
            app->remote_sender_failed_ = !ok;
            // release/acquire 发布结果；主任务只在空闲时填写下一帧。
            app->remote_sender_busy_.store(false);
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_SEND_AUDIO);
        }
    }, "remote_uplink", 6144, this, 3, &remote_sender_task_) != pdPASS) {
        remote_sender_task_ = nullptr;
        ESP_LOGE("WebRemote", "Unable to create cloud audio sender");
    }
}

void Application::KickRemoteSender() {
    if (!remote_sender_task_ || remote_sender_busy_ || !protocol_ ||
        (!remote_recording_ && !remote_ending_)) return;
    auto packet = audio_service_.PopPacketFromSendQueue();
    if (!packet) return;
    remote_sender_protocol_ = protocol_;
    remote_sender_packet_ = std::move(packet);
    remote_sender_epoch_ = protocol_->AudioEpoch();
    remote_sender_session_ = remote_audio_session_;
    remote_sender_busy_ = true;
    xTaskNotifyGive(remote_sender_task_);
}
