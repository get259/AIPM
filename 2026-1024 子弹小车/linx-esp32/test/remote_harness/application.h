#pragma once
#include "remote_server.h"
#include "remote_audio.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <memory>
#include <vector>
enum DeviceState { kDeviceStateIdle, kDeviceStateConnecting, kDeviceStateListening, kDeviceStateSpeaking };
enum ListeningMode { kListeningModeManualStop, kListeningModeRealtime };
constexpr int MAIN_EVENT_REMOTE = 1;
inline void xEventGroupSetBits(int, int) {}
struct FakeProtocol {
    bool open = false;
    bool open_result = true;
    int closes = 0, starts = 0, stops = 0;
    void CloseAudioChannel() { ++closes; open = false; }
    void ResetInputSession() { CloseAudioChannel(); }
    bool OpenAudioChannel() { return open = open_result; }
    bool IsAudioChannelOpened() { return open; }
    void SendStartListening(ListeningMode) { ++starts; }
    void SendStopListening() { ++stops; }
};
struct FakeAudio {
    bool external = false, accept = true, failed = false, drained = true;
    int clears = 0;
    std::vector<std::vector<int16_t>> frames;
    void EnableVoiceProcessing(bool) {}
    void EnableWakeWordDetection(bool) {}
    void SetExternalInput(bool value) { external = value; ++clears; frames.clear(); }
    bool SubmitExternalPcm(std::vector<int16_t>&& pcm) {
        if (!accept) return false;
        frames.push_back(std::move(pcm)); return true;
    }
    bool IsUplinkDrained() { return drained; }
    bool HasUplinkError() { return failed; }
};
struct FakeCar {
    std::vector<uint8_t> states;
    int resets = 0;
    void DiscardReply() { ++resets; }
    void NextInput() {}
    void ApplyRemoteState(uint8_t state) { states.push_back(state); }
    void FireRemote() { states.push_back((states.empty() ? 0 : states.back() & 0x0f) | 0x10); }
};
// 仅替换硬件/调度依赖，直接编译真实的 remote_application.cc。
class Application {
public:
    int remote_sender_task_ = 1;
    bool remote_sender_busy_ = false;
    uint32_t remote_audio_session_ = 0;
    std::mutex remote_mutex_;
    std::deque<RemoteEvent> remote_events_;
    std::atomic<bool> remote_enabled_{false}, remote_reply_allowed_{false}, remote_resetting_{false};
    std::atomic<uint32_t> remote_generation_{1};
    std::atomic<bool> remote_releasing_{false};
    uint32_t remote_owner_ = 0, remote_recording_id_ = 0;
    bool remote_recording_ = false, remote_ending_ = false, remote_platform_ready_ = true;
    RemoteEvent remote_end_event_;
    RemoteAudio remote_audio_;
    FakeAudio audio_service_;
    FakeCar car_;
    std::unique_ptr<FakeProtocol> protocol_{new FakeProtocol};
    DeviceState state = kDeviceStateIdle;
    ListeningMode listening_mode_ = kListeningModeManualStop;
    int event_group_ = 0, toggles = 0;
    DeviceState GetDeviceState() { return state; }
    void SetDeviceState(DeviceState value) { state = value; }
    void ToggleChatState() { ++toggles; }
    bool PostRemoteEvent(RemoteEvent&&);
    void HandleRemoteEvents();
    void ResetRemoteSession();
    void FinishRemoteAudio();
};
