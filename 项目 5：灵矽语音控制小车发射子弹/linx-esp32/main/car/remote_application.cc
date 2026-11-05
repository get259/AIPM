#include "application.h"
#include "board.h"
#include "wifi_manager.h"
#include <esp_log.h>

bool Application::PostRemoteEvent(RemoteEvent&& event) {
    // 控制帧直接下发，不进入可能正在建立云端 TLS 的应用任务。
    if ((event.op == "move" || event.op == "fire" || event.op == "disable") && remote_enabled_ &&
        !remote_releasing_ && event.client == remote_owner_ &&
        RemoteServer::Get().IsClientConnected(event.client)) {
        if (event.op == "disable") remote_releasing_ = true;
        ++remote_generation_; // 丢弃手动操作前排队、尚未执行的 AI 回调。
        if (event.op == "fire") car_.FireRemote();
        else car_.ApplyRemoteState(event.op == "disable" ? 0 : event.state);
        if (event.op == "move" || event.op == "fire") {
            RemoteServer::Get().Reply(event, true, event.op == "fire" ? "单次发射指令已发送" : event.state ? "方向指令已发送" : "停止指令已发送",
                true, remote_recording_, remote_owner_, remote_platform_ready_);
            return true;
        }
    }
    if (event.op == "disconnect") {
        // 连接表已先移除身份；唤醒主任务检查，即使普通队列满也不会遗失释放通知。
        xEventGroupSetBits(event_group_, MAIN_EVENT_REMOTE);
        return true;
    }
    std::lock_guard<std::mutex> lock(remote_mutex_);
    // 总开关关闭保留一个独立名额，音频积压不能吞掉交还请求。
    if (remote_events_.size() >= 8u && event.op != "disable") return false;
    if (remote_events_.size() >= 9u) return false;
    remote_events_.push_back(std::move(event));
    xEventGroupSetBits(event_group_, MAIN_EVENT_REMOTE);
    return true;
}

void Application::ResetRemoteSession() {
    ++remote_audio_session_;
    if (remote_ending_) {
        RemoteServer::Get().Reply(remote_end_event_, false, "录音已取消", remote_enabled_, false,
            remote_owner_, remote_platform_ready_ && WifiManager::GetInstance().IsConnected());
    }
    ++remote_generation_;
    remote_reply_allowed_ = false;
    remote_recording_ = false;
    remote_ending_ = false;
    remote_audio_.Reset();
    car_.DiscardReply();
    audio_service_.EnableVoiceProcessing(false);
    audio_service_.EnableWakeWordDetection(false);
    // 先隔离来源，正在编码的任务返回时也会被代际校验丢弃。
    audio_service_.SetExternalInput(remote_enabled_);
    remote_resetting_ = true;
    if (protocol_) protocol_->ResetInputSession();
    remote_resetting_ = false;
    const auto state = GetDeviceState();
    if (state == kDeviceStateListening || state == kDeviceStateSpeaking || state == kDeviceStateConnecting)
        SetDeviceState(kDeviceStateIdle);
}

void Application::FinishRemoteAudio() {
    if (!remote_ending_ || remote_sender_busy_ || !audio_service_.IsUplinkDrained()) return;
    remote_ending_ = false;
    const bool valid = !audio_service_.HasUplinkError() && protocol_ && protocol_->IsAudioChannelOpened();
    if (valid) protocol_->SendStopListening();
    ResetRemoteSession();
    RemoteServer::Get().Reply(remote_end_event_, valid, valid ? "收听已关闭，网页继续独占" : "音频编码或平台连接异常，本轮已取消",
        remote_enabled_, false, remote_owner_, remote_platform_ready_ && WifiManager::GetInstance().IsConnected());
}

void Application::HandleRemoteEvents() {
    if (remote_owner_ && !RemoteServer::Get().IsClientConnected(remote_owner_)) {
        ResetRemoteSession();
        remote_owner_ = 0;
    }
    std::deque<RemoteEvent> events;
    { std::lock_guard<std::mutex> lock(remote_mutex_); events.swap(remote_events_); }
    for (auto& event : events) {
        // 已关闭连接排队的 enable/PCM 不能在稍后重新认领或影响新来源。
        if (!RemoteServer::Get().IsClientConnected(event.client)) continue;
        const bool platform = remote_platform_ready_ && WifiManager::GetInstance().IsConnected();
        bool ok = true;
        const char* message = "就绪";
        const bool owner = event.client == remote_owner_;
        if (event.op == "status") {
            message = remote_enabled_ ? "网页独占控制" : "INMP441 控制";
        } else if (event.op == "enable") {
            if (remote_owner_ && !owner) { ok = false; message = "另一台设备正在遥控"; }
            else if (!remote_enabled_ || !owner) {
                remote_enabled_ = true; remote_owner_ = event.client;
                remote_releasing_ = false;
                ResetRemoteSession(); message = "网页已独占接管";
            }
        } else if (!remote_enabled_ || !owner) {
            ok = false; message = "请先打开遥控开关并取得控制权";
        } else if (event.op == "disable") {
            car_.ApplyRemoteState(0);
            remote_enabled_ = false;
            ResetRemoteSession(); remote_owner_ = 0;
            remote_releasing_ = false;
            car_.NextInput();
            if (platform) ToggleChatState();
            message = "已停止，恢复 INMP441";
        } else if (event.op == "move") {
            // 按键永远优先于尚未返回的网页语音；释放按钮同样使该轮语音失效。
            car_.ApplyRemoteState(event.state);
            message = event.state ? "方向指令已发送" : "停止指令已发送";
        } else if (event.op == "fire") {
            car_.FireRemote();
            message = "单次发射指令已发送";
        } else if (event.op == "start") {
            if (!remote_sender_task_) { ok = false; message = "音频发送任务未启动，方向按钮仍可使用"; }
            else if (!platform || !protocol_) { ok = false; message = "平台尚未就绪，方向按钮仍可使用"; }
            else if (remote_recording_ || remote_ending_) { ok = false; message = "上一句语音正在提交"; }
            else {
                ResetRemoteSession();
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    ResetRemoteSession(); ok = false; message = "无法连接语音平台";
                } else {
                    listening_mode_ = kListeningModeRealtime;
                    remote_reply_allowed_ = true;
                    remote_recording_ = true;
                    remote_recording_id_ = event.id;
                    remote_audio_.Reset(event.id);
                    protocol_->SendStartListening(kListeningModeRealtime);
                    SetDeviceState(kDeviceStateListening);
                    message = "录音中";
                }
            }
        } else if (event.op == "pcm") {
            std::vector<int16_t> frame;
            if (!remote_recording_ || !protocol_ || !protocol_->IsAudioChannelOpened() ||
                !remote_audio_.Append(event.recording, event.sequence, event.pcm.data(), event.pcm.size(), frame) ||
                (!frame.empty() && !audio_service_.SubmitExternalPcm(std::move(frame)))) {
                ESP_LOGW("WebRemote", "PCM rejected: recording=%d, session=%lu, sequence=%lu, bytes=%u",
                    static_cast<int>(remote_recording_), static_cast<unsigned long>(event.recording),
                    static_cast<unsigned long>(event.sequence), static_cast<unsigned>(event.pcm.size()));
                ResetRemoteSession(); ok = false; message = "音频格式、顺序或缓冲异常，本轮已取消";
            }
        } else if (event.op == "end") {
            if (!remote_recording_) { ok = false; message = "当前没有录音"; }
            else {
                // 空 PCM 不允许伪造数据；结束标记也必须属于当前录音。
                // 使用独立的录音编号检查，编号绑定 start 的请求 id。
                if (event.recording != remote_recording_id_) {
                    ResetRemoteSession(); ok = false; message = "录音编号已失效";
                } else {
                    auto tail = remote_audio_.Finish();
                    if (!tail.empty() && !audio_service_.SubmitExternalPcm(std::move(tail))) {
                        ResetRemoteSession(); ok = false; message = "音频缓冲已满，本轮已取消";
                    } else {
                        remote_recording_ = false; remote_ending_ = true; remote_end_event_ = event;
                        FinishRemoteAudio();
                        continue; // 等队列实际发送完成后确认结束。
                    }
                }
            }
        } else if (event.op == "cancel") {
            ResetRemoteSession(); message = "本轮语音已取消";
        } else { ok = false; message = "不支持的请求"; }
        RemoteServer::Get().Reply(event, ok, message, remote_enabled_, remote_recording_, remote_owner_, platform);
    }
}
