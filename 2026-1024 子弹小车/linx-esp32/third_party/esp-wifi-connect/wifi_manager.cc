/*
 * WiFi Manager Implementation
 */

#include "wifi_manager.h"
#include "wifi_station.h"
#include "wifi_configuration_ap.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "WifiManager"

WifiManager& WifiManager::GetInstance() {
    static WifiManager instance;
    return instance;
}

WifiManager::WifiManager() = default;

WifiManager::~WifiManager() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (station_active_ && station_) {
        station_->Stop();
    }
    if (config_mode_active_ && config_ap_) {
        config_ap_->Stop();
    }
    if (initialized_) {
        esp_wifi_deinit();
    }
}

void WifiManager::NotifyEvent(WifiEvent event) {
    // Copy callback under lock, invoke without lock to avoid deadlock
    std::function<void(WifiEvent)> callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = event_callback_;
    }
    if (callback) {
        callback(event);
    }
}

bool WifiManager::Initialize(const WifiManagerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    config_ = config;
    ESP_LOGI(TAG, "Initializing...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize netif
    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Netif init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Create event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Event loop create failed: %s", esp_err_to_name(ret));
        return false;
    }

    // Initialize WiFi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = false;
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return false;
    }

    station_ = std::make_unique<WifiStation>();
    config_ap_ = std::make_unique<WifiConfigurationAp>();

    initialized_ = true;
    ESP_LOGI(TAG, "Initialized");
    return true;
}

bool WifiManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

// ==================== Station Mode ====================

void WifiManager::StartStation() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return;
    }
    if (station_active_) {
        ESP_LOGW(TAG, "Station already active");
        return;
    }

    // Auto-stop config AP if active
    const bool keep_ap = config_mode_active_ && config_.activation_portal;
    if (config_mode_active_ && !keep_ap) {
        ESP_LOGI(TAG, "Stopping config AP before starting station");
        config_ap_->Stop();
        config_mode_active_ = false;
        // Notify outside lock
        mutex_.unlock();
        NotifyEvent(WifiEvent::ConfigModeExit);
        mutex_.lock();
    }

    ESP_LOGI(TAG, "Starting station");

    // Apply configuration
    station_->SetScanIntervalRange(config_.station_scan_min_interval_seconds,
                                   config_.station_scan_max_interval_seconds);

    // Setup callbacks
    station_->OnScanBegin([this]() {
        NotifyEvent(WifiEvent::Scanning);
    });
    station_->OnConnect([this](const std::string&) {
        NotifyEvent(WifiEvent::Connecting);
    });
    station_->OnConnected([this](const std::string&) {
        NotifyEvent(WifiEvent::Connected);
    });
    station_->OnDisconnected([this]() {
        NotifyEvent(WifiEvent::Disconnected);
    });

    // 配网页只做过临时试连；先停用临时 STA，再让原 WifiStation
    // 创建 netif 并接收新的 STA_START/IP 事件，热点始终保持运行。
    if (keep_ap) {
#if CONFIG_CAR_WEB_REMOTE
        config_ap_->SetStationManaged(true);
#endif
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    }
    station_->Start(keep_ap);
    station_active_ = true;
}

void WifiManager::StopStation() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!station_active_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping station");
    station_->Stop(config_mode_active_);
    ESP_LOGI(TAG, "Station stopped");
    station_active_ = false;
    
    mutex_.unlock();
    NotifyEvent(WifiEvent::Disconnected);
    mutex_.lock();
}

bool WifiManager::IsConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_active_ && station_ && station_->IsConnected();
}

std::string WifiManager::GetSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!station_active_ || !station_) return "";
    return station_->GetSsid();
}

std::string WifiManager::GetIpAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!station_active_ || !station_) return "";
    return station_->GetIpAddress();
}

int WifiManager::GetRssi() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!station_active_ || !station_ || !station_->IsConnected()) return 0;
    return station_->GetRssi();
}

int WifiManager::GetChannel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!station_active_ || !station_ || !station_->IsConnected()) return 0;
    return station_->GetChannel();
}

std::string WifiManager::GetMacAddress() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!mac_address_.empty()) {
        return mac_address_;
    }

    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char buf[18];
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        mac_address_ = buf;
    }
    return mac_address_;
}

// ==================== Config AP Mode ====================

void WifiManager::StartConfigAp() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return;
    }
    if (config_mode_active_) {
#if CONFIG_CAR_WEB_REMOTE
        if (!station_active_) config_ap_->SetStationManaged(false);
#endif
        ESP_LOGW(TAG, "Config AP already active");
        return;
    }

    // Auto-stop station if active
    if (station_active_) {
        ESP_LOGI(TAG, "Stopping station before starting config AP");
        station_->Stop();
        station_active_ = false;
        mutex_.unlock();
        NotifyEvent(WifiEvent::Disconnected);
        mutex_.lock();
    }

    ESP_LOGI(TAG, "Starting config AP");

    config_ap_->SetSsidPrefix(config_.ssid_prefix);
    config_ap_->SetLanguage(config_.language);
    config_ap_->SetActivationPortal(config_.activation_portal);
    config_ap_->OnActivationRequested([this]() { StartStation(); });
    ++portal_generation_;
    portal_close_scheduled_ = false;
    
    // Web handler calls this when user submits config
    config_ap_->OnExitRequested([this, generation = portal_generation_]() {
        ESP_LOGI(TAG, "Config exit requested from web");
        StopConfigAp(generation);
    });
    
    config_ap_->Start();
    config_mode_active_ = true;

    mutex_.unlock();
    NotifyEvent(WifiEvent::ConfigModeEnter);
    mutex_.lock();
}

void WifiManager::StopConfigAp(uint32_t expected_generation) {
#if CONFIG_CAR_WEB_REMOTE
    // 遥控使用常开热点，旧网页 /exit 和激活延迟任务均不能关闭它。
    return;
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!config_mode_active_) {
        return;
    }
    // 旧页面或延迟任务不能关闭用户后来重新开启的热点。
    if (expected_generation != 0 && expected_generation != portal_generation_) return;

    ESP_LOGI(TAG, "Stopping config AP");
    config_ap_->Stop(station_active_);
    config_mode_active_ = false;
    portal_close_scheduled_ = false;

    mutex_.unlock();
    NotifyEvent(WifiEvent::ConfigModeExit);
    mutex_.lock();
}

void WifiManager::SetActivationStatus(const std::string& state, const std::string& code, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_mode_active_ || !config_.activation_portal) return;
    config_ap_->SetActivationStatus(state, code, message);
    if (state != "ready" || portal_close_scheduled_) return;
#if CONFIG_CAR_WEB_REMOTE
    return;
#endif

    struct CloseRequest { WifiManager* manager; uint32_t generation; };
    auto* request = new (std::nothrow) CloseRequest{this, portal_generation_};
    if (!request) return;
    portal_close_scheduled_ = true;
    // 留出页面轮询和显示成功状态的时间；即使浏览器关闭，也会收起热点。
    if (xTaskCreate([](void* context) {
            auto* close = static_cast<CloseRequest*>(context);
            vTaskDelay(pdMS_TO_TICKS(5000));
            close->manager->StopConfigAp(close->generation);
            delete close;
            vTaskDelete(nullptr);
        }, "activation_ap_close", 4096, request, 3, nullptr) != pdPASS) {
        portal_close_scheduled_ = false;
        delete request;
    }
}

bool WifiManager::IsConfigMode() const {
    std::lock_guard<std::mutex> lock(mutex_);
#if CONFIG_CAR_WEB_REMOTE
    // 常开 AP 不代表正在配网，已有 STA 时允许平台及语音正常启动。
    return config_mode_active_ && !station_active_;
#endif
    return config_mode_active_;
}

std::string WifiManager::GetApSsid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_mode_active_ || !config_ap_) return "";
    return config_ap_->GetSsid();
}

std::string WifiManager::GetApWebUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!config_mode_active_ || !config_ap_) return "";
    return config_ap_->GetWebServerUrl();
}

// ==================== Power ====================

void WifiManager::SetPowerSaveLevel(WifiPowerSaveLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!station_active_ || !station_) {
        return;
    }
    station_->SetPowerSaveLevel(level);
}

// ==================== Event ====================

void WifiManager::SetEventCallback(std::function<void(WifiEvent)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = std::move(callback);
}
