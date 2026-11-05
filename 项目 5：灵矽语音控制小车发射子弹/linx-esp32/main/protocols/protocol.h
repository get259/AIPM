#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cJSON.h>
#include <string>
#include <functional>
#include <chrono>
#include <vector>
#include <memory>
#include <cstdint>
#include <mutex>
#include <atomic>

struct AudioStreamPacket {
    int sample_rate = 0;
    int frame_duration = 0;
    uint32_t timestamp = 0;
    std::vector<uint8_t> payload;
};

struct BinaryProtocol2 {
    uint16_t version;
    uint16_t type;          // Message type (0: OPUS, 1: JSON)
    uint32_t reserved;      // Reserved for future use
    uint32_t timestamp;     // Timestamp in milliseconds (used for server-side AEC)
    uint32_t payload_size;  // Payload size in bytes
    uint8_t payload[];      // Payload data
} __attribute__((packed));

struct BinaryProtocol3 {
    uint8_t type;
    uint8_t reserved;
    uint16_t payload_size;
    uint8_t payload[];
} __attribute__((packed));

enum AbortReason {
    kAbortReasonNone,
    kAbortReasonWakeWordDetected
};

enum ListeningMode {
    kListeningModeAutoStop,
    kListeningModeManualStop,
    kListeningModeRealtime // 需要 AEC 支持
};

class Protocol {
public:
    virtual ~Protocol() = default;

    inline int server_sample_rate() const {
        return server_sample_rate_;
    }
    inline int server_frame_duration() const {
        return server_frame_duration_;
    }
    inline const std::string& session_id() const {
        return session_id_;
    }

    void OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback);
    void OnIncomingJson(std::function<void(const cJSON* root)> callback);
    void OnAudioChannelOpened(std::function<void()> callback);
    void OnAudioChannelClosed(std::function<void()> callback);
    void OnNetworkError(std::function<void(const std::string& message)> callback);
    void OnConnected(std::function<void()> callback);
    void OnDisconnected(std::function<void()> callback);

    // 后台音频发送持有会话锁并核对代际，旧音频不能进入新连接。
    uint32_t AudioEpoch() const { return audio_epoch_.load(); }
    bool SendAudioForEpoch(std::unique_ptr<AudioStreamPacket> packet, uint32_t epoch) {
        std::lock_guard<std::recursive_mutex> lock(audio_transport_mutex_);
        if (epoch != audio_epoch_) return true;
        return SendAudio(std::move(packet));
    }
    virtual bool Start() = 0;
    virtual bool OpenAudioChannel() = 0;
    virtual void CloseAudioChannel() = 0;
    // 控制来源切换时建立新的输入会话边界，不改变认证及原异常重试策略。
    virtual void ResetInputSession() { CloseAudioChannel(); }
    virtual bool IsAudioChannelOpened() const = 0;
    virtual bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) = 0;
    virtual void SendWakeWordDetected(const std::string& wake_word);
    bool SendUserText(const std::string& text);
    virtual void SendStartListening(ListeningMode mode);
    virtual void SendStopListening();
    virtual void SendAbortSpeaking(AbortReason reason);
    virtual void SendMcpMessage(const std::string& message);

protected:
    std::recursive_mutex audio_transport_mutex_;
    std::atomic<uint32_t> audio_epoch_{0};
    std::function<void(const cJSON* root)> on_incoming_json_;
    std::function<void(std::unique_ptr<AudioStreamPacket> packet)> on_incoming_audio_;
    std::function<void()> on_audio_channel_opened_;
    std::function<void()> on_audio_channel_closed_;
    std::function<void(const std::string& message)> on_network_error_;
    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;

    int server_sample_rate_ = 24000;
    int server_frame_duration_ = 60;
    bool error_occurred_ = false;
    std::string session_id_;
    std::chrono::time_point<std::chrono::steady_clock> last_incoming_time_;

    virtual bool SendText(const std::string& text) = 0;
    virtual void SetError(const std::string& message);
    virtual bool IsTimeout() const;
};

#endif // PROTOCOL_H

