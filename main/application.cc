#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "assets.h"
#include "settings.h"
#include "dual_servo_controller.h"
#include <wifi_manager.h>

#include <cstring>
#include <cctype>
#include <algorithm>
#include <cstdio>
#include <new>
#include <array>
#include <utility>
#include <vector>
#include <dirent.h>
#include <esp_log.h>
#include <esp_random.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application"

namespace {

std::string ToLowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

void ReplaceAll(std::string& s, const char* from, const char* to) {
    if (from == nullptr || to == nullptr || *from == '\0') {
        return;
    }
    size_t pos = 0;
    size_t from_len = std::strlen(from);
    size_t to_len = std::strlen(to);
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from_len, to);
        pos += to_len;
    }
}

std::string RemoveAudioExtension(const std::string& file_name) {
    if (file_name.size() > 4) {
        auto lower = ToLowerCopy(file_name);
        if (lower.compare(lower.size() - 4, 4, ".mp3") == 0 ||
            lower.compare(lower.size() - 4, 4, ".ogg") == 0) {
            return file_name.substr(0, file_name.size() - 4);
        }
    }
    return file_name;
}

std::string NormalizeVietnameseSearchText(std::string s) {
    s = ToLowerCopy(s);

    static const std::array<std::pair<const char*, const char*>, 67> kVietnameseMap{{
        {"à", "a"}, {"á", "a"}, {"ạ", "a"}, {"ả", "a"}, {"ã", "a"},
        {"â", "a"}, {"ầ", "a"}, {"ấ", "a"}, {"ậ", "a"}, {"ẩ", "a"}, {"ẫ", "a"},
        {"ă", "a"}, {"ằ", "a"}, {"ắ", "a"}, {"ặ", "a"}, {"ẳ", "a"}, {"ẵ", "a"},
        {"è", "e"}, {"é", "e"}, {"ẹ", "e"}, {"ẻ", "e"}, {"ẽ", "e"},
        {"ê", "e"}, {"ề", "e"}, {"ế", "e"}, {"ệ", "e"}, {"ể", "e"}, {"ễ", "e"},
        {"ì", "i"}, {"í", "i"}, {"ị", "i"}, {"ỉ", "i"}, {"ĩ", "i"},
        {"ò", "o"}, {"ó", "o"}, {"ọ", "o"}, {"ỏ", "o"}, {"õ", "o"},
        {"ô", "o"}, {"ồ", "o"}, {"ố", "o"}, {"ộ", "o"}, {"ổ", "o"}, {"ỗ", "o"},
        {"ơ", "o"}, {"ờ", "o"}, {"ớ", "o"}, {"ợ", "o"}, {"ở", "o"}, {"ỡ", "o"},
        {"ù", "u"}, {"ú", "u"}, {"ụ", "u"}, {"ủ", "u"}, {"ũ", "u"},
        {"ư", "u"}, {"ừ", "u"}, {"ứ", "u"}, {"ự", "u"}, {"ử", "u"}, {"ữ", "u"},
        {"ỳ", "y"}, {"ý", "y"}, {"ỵ", "y"}, {"ỷ", "y"}, {"ỹ", "y"},
        {"đ", "d"}
    }};

    for (const auto& kv : kVietnameseMap) {
        ReplaceAll(s, kv.first, kv.second);
    }

    for (char& c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            c = ' ';
        }
    }

    std::string compact;
    compact.reserve(s.size());
    bool last_space = true;
    for (char c : s) {
        if (c == ' ') {
            if (!last_space) {
                compact.push_back(' ');
            }
            last_space = true;
        } else {
            compact.push_back(c);
            last_space = false;
        }
    }
    if (!compact.empty() && compact.back() == ' ') {
        compact.pop_back();
    }
    return compact;
}

std::vector<std::string> ExtractMeaningfulTokens(const std::string& normalized) {
    static const std::array<const char*, 24> kStopwords{{
        "mo", "phat", "nhac", "bai", "hat", "giup", "toi", "cho", "di", "nhe", "nha",
        "oi", "va", "la", "mot", "baihat", "baih", "play", "music", "please", "hay", "duoc", "khong", "vo"
    }};

    std::vector<std::string> tokens;
    size_t start = 0;
    while (start < normalized.size()) {
        size_t end = normalized.find(' ', start);
        std::string token = (end == std::string::npos)
            ? normalized.substr(start)
            : normalized.substr(start, end - start);
        if (!token.empty()) {
            bool skip = token.size() <= 1;
            if (!skip) {
                for (const auto* sw : kStopwords) {
                    if (token == sw) {
                        skip = true;
                        break;
                    }
                }
            }
            if (!skip) {
                tokens.push_back(token);
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return tokens;
}

bool ContainsAnyPhrase(const std::string& s, const std::initializer_list<const char*>& phrases) {
    for (const auto* phrase : phrases) {
        if (phrase != nullptr && s.find(phrase) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool IsWebUiAddressQuestion(const std::string& text) {
    auto normalized = NormalizeVietnameseSearchText(text);
    if (normalized.empty()) {
        return false;
    }

    bool asks_webui = ContainsAnyPhrase(normalized, {
        "web ui", "webui", "ui robot", "trang dieu khien", "dieu khien robot"
    });
    bool asks_address = ContainsAnyPhrase(normalized, {
        "ip", "dia chi", "link", "dang nhap", "o dau", "doc to", "doc dia chi", "mo trang"
    });

    return asks_webui && asks_address;
}

void BuildWebUiAccessInfo(std::string& url, std::string& hint) {
    auto& wifi = WifiManager::GetInstance();
    url.clear();
    hint.clear();

    if (wifi.IsConnected()) {
        std::string ip = wifi.GetIpAddress();
        if (!ip.empty()) {
            url = "http://" + ip + "/";
            hint = "WebUI (same Wi-Fi): " + url;
            return;
        }
    }

    if (wifi.IsConfigMode()) {
        std::string ap_url = wifi.GetApWebUrl();
        if (!ap_url.empty()) {
            if (ap_url.rfind("http://", 0) == 0 || ap_url.rfind("https://", 0) == 0) {
                url = ap_url;
            } else {
                url = "http://" + ap_url;
            }
        } else {
            url = "http://192.168.4.1/";
        }
        hint = "WebUI (AP mode): " + url;
        return;
    }

    hint = "WebUI chua san sang. Hay doi robot ket noi Wi-Fi.";
}

bool HasOggExtension(const std::string& file_name) {
    auto lower = ToLowerCopy(file_name);
    return lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".ogg") == 0;
}

bool HasMp3Extension(const std::string& file_name) {
    auto lower = ToLowerCopy(file_name);
    return lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".mp3") == 0;
}

std::string ResolveSdMusicFolder(const std::string& mood_or_emotion) {
    auto key = ToLowerCopy(mood_or_emotion);

    if (key == "vuive" || key == "happy" || key == "laughing" || key == "joy" || key == "excited") {
        return "/sdcard/music/vuive";
    }
    if (key == "buon" || key == "sad" || key == "crying" || key == "confused" || key == "angry") {
        return "/sdcard/music/buon";
    }
    return "";
}

const char* kSdMusicHappyFolder = "/sdcard/music/vuive";
const char* kSdMusicSadFolder = "/sdcard/music/buon";

std::string BaseNameFromPath(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::vector<std::string> ResolveSdMusicSearchFolders(const std::string& mood_hint) {
    std::vector<std::string> folders;
    std::string single = ResolveSdMusicFolder(mood_hint);
    if (!single.empty()) {
        folders.emplace_back(std::move(single));
        return folders;
    }
    folders.emplace_back(kSdMusicHappyFolder);
    folders.emplace_back(kSdMusicSadFolder);
    return folders;
}

void CollectAudioCandidatesFromFolder(const std::string& folder, std::vector<std::string>& out) {
    DIR* dir = opendir(folder.c_str());
    if (dir == nullptr) {
        return;
    }
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        std::string name(entry->d_name);
        if (HasOggExtension(name) || HasMp3Extension(name)) {
            out.emplace_back(folder + "/" + name);
        }
    }
    closedir(dir);
}

int MatchScore(const std::string& file_name_lower, const std::string& query_lower) {
    if (query_lower.empty()) {
        return 0;
    }
    int score = 0;
    auto full_pos = file_name_lower.find(query_lower);
    if (full_pos != std::string::npos) {
        score += 100;
        if (full_pos == 0) {
            score += 20;
        }
    }

    size_t start = 0;
    while (start < query_lower.size()) {
        while (start < query_lower.size() && query_lower[start] == ' ') {
            ++start;
        }
        if (start >= query_lower.size()) {
            break;
        }
        size_t end = query_lower.find(' ', start);
        std::string token = (end == std::string::npos)
            ? query_lower.substr(start)
            : query_lower.substr(start, end - start);
        if (!token.empty() && file_name_lower.find(token) != std::string::npos) {
            score += 15;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return score;
}

int MatchScoreAdvanced(const std::string& file_name, const std::string& query) {
    std::string base_name = RemoveAudioExtension(file_name);
    std::string file_norm = NormalizeVietnameseSearchText(base_name);
    std::string query_norm = NormalizeVietnameseSearchText(query);
    if (query_norm.empty()) {
        return 0;
    }

    int score = 0;
    if (file_norm == query_norm) {
        score += 500;
    }

    if (file_norm.find(query_norm) != std::string::npos) {
        score += 140;
    }

    std::string file_nospace = file_norm;
    file_nospace.erase(std::remove(file_nospace.begin(), file_nospace.end(), ' '), file_nospace.end());
    std::string query_nospace = query_norm;
    query_nospace.erase(std::remove(query_nospace.begin(), query_nospace.end(), ' '), query_nospace.end());
    if (!query_nospace.empty() && file_nospace.find(query_nospace) != std::string::npos) {
        score += 120;
    }

    auto tokens = ExtractMeaningfulTokens(query_norm);
    for (const auto& token : tokens) {
        if (file_norm.find(token) != std::string::npos) {
            score += 35;
            if (file_norm.rfind(token, 0) == 0) {
                score += 10;
            }
        }
    }

    score += MatchScore(ToLowerCopy(base_name), ToLowerCopy(query));
    return score;
}

} // namespace


Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

bool Application::SetDeviceState(DeviceState state) {
    return state_machine_.TransitionTo(state);
}

void Application::Initialize() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    // Setup the display
    auto display = board.GetDisplay();
    display->SetupUI();
    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    // Setup the audio service
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Add state change listeners
    state_machine_.AddStateChangeListener([this](DeviceState old_state, DeviceState new_state) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
    });

    // Start the clock timer to update the status bar
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    // Add MCP common tools (only once during initialization)
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    // Set network event callback for UI updates and network state handling
    board.SetNetworkEventCallback([this](NetworkEvent event, const std::string& data) {
        auto display = Board::GetInstance().GetDisplay();
        
        switch (event) {
            case NetworkEvent::Scanning:
                display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::Connecting: {
                if (data.empty()) {
                    // Cellular network - registering without carrier info yet
                    display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                } else {
                    // WiFi or cellular with carrier info
                    std::string msg = Lang::Strings::CONNECT_TO;
                    msg += data;
                    msg += "...";
                    display->ShowNotification(msg.c_str(), 30000);
                }
                break;
            }
            case NetworkEvent::Connected: {
                std::string msg = Lang::Strings::CONNECTED_TO;
                msg += data;
                display->ShowNotification(msg.c_str(), 30000);
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_CONNECTED);
                break;
            }
            case NetworkEvent::Disconnected:
                xEventGroupSetBits(event_group_, MAIN_EVENT_NETWORK_DISCONNECTED);
                break;
            case NetworkEvent::WifiConfigModeEnter:
                // WiFi config mode enter is handled by WifiBoard internally
                break;
            case NetworkEvent::WifiConfigModeExit:
                // WiFi config mode exit is handled by WifiBoard internally
                break;
            // Cellular modem specific events
            case NetworkEvent::ModemDetecting:
                display->SetStatus(Lang::Strings::DETECTING_MODULE);
                break;
            case NetworkEvent::ModemErrorNoSim:
                Alert(Lang::Strings::ERROR, Lang::Strings::PIN_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_PIN);
                break;
            case NetworkEvent::ModemErrorRegDenied:
                Alert(Lang::Strings::ERROR, Lang::Strings::REG_ERROR, "triangle_exclamation", Lang::Sounds::OGG_ERR_REG);
                break;
            case NetworkEvent::ModemErrorInitFailed:
                Alert(Lang::Strings::ERROR, Lang::Strings::MODEM_INIT_ERROR, "triangle_exclamation", Lang::Sounds::OGG_EXCLAMATION);
                break;
            case NetworkEvent::ModemErrorTimeout:
                display->SetStatus(Lang::Strings::REGISTERING_NETWORK);
                break;
        }
    });

    // Start network asynchronously
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);
}

void Application::Run() {
    // Set the priority of the main task to 10
    vTaskPrioritySet(nullptr, 10);

    const EventBits_t ALL_EVENTS = 
        MAIN_EVENT_SCHEDULE |
        MAIN_EVENT_SEND_AUDIO |
        MAIN_EVENT_WAKE_WORD_DETECTED |
        MAIN_EVENT_VAD_CHANGE |
        MAIN_EVENT_CLOCK_TICK |
        MAIN_EVENT_ERROR |
        MAIN_EVENT_NETWORK_CONNECTED |
        MAIN_EVENT_NETWORK_DISCONNECTED |
        MAIN_EVENT_TOGGLE_CHAT |
        MAIN_EVENT_START_LISTENING |
        MAIN_EVENT_STOP_LISTENING |
        MAIN_EVENT_ACTIVATION_DONE |
        MAIN_EVENT_STATE_CHANGED;

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, ALL_EVENTS, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_NETWORK_CONNECTED) {
            HandleNetworkConnectedEvent();
        }

        if (bits & MAIN_EVENT_NETWORK_DISCONNECTED) {
            HandleNetworkDisconnectedEvent();
        }

        if (bits & MAIN_EVENT_ACTIVATION_DONE) {
            HandleActivationDoneEvent();
        }

        if (bits & MAIN_EVENT_STATE_CHANGED) {
            HandleStateChangedEvent();
        }

        if (bits & MAIN_EVENT_TOGGLE_CHAT) {
            HandleToggleChatEvent();
        }

        if (bits & MAIN_EVENT_START_LISTENING) {
            HandleStartListeningEvent();
        }

        if (bits & MAIN_EVENT_STOP_LISTENING) {
            HandleStopListeningEvent();
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            HandleWakeWordDetectedEvent();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (GetDeviceState() == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            // Print debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::HandleNetworkConnectedEvent() {
    ESP_LOGI(TAG, "Network connected");
    auto state = GetDeviceState();

    if (state == kDeviceStateStarting || state == kDeviceStateWifiConfiguring) {
        // Network is ready, start activation
        SetDeviceState(kDeviceStateActivating);
        if (activation_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Activation task already running");
            return;
        }

        xTaskCreate([](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->ActivationTask();
            app->activation_task_handle_ = nullptr;
            vTaskDelete(NULL);
        }, "activation", 4096 * 2, this, 2, &activation_task_handle_);
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleNetworkDisconnectedEvent() {
    // Close current conversation when network disconnected
    auto state = GetDeviceState();
    if (state == kDeviceStateConnecting || state == kDeviceStateListening || state == kDeviceStateSpeaking) {
        ESP_LOGI(TAG, "Closing audio channel due to network disconnection");
        protocol_->CloseAudioChannel();
    }

    // Update the status bar immediately to show the network state
    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar(true);
}

void Application::HandleActivationDoneEvent() {
    ESP_LOGI(TAG, "Activation done");

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota_->HasServerTime();

    auto display = Board::GetInstance().GetDisplay();
    std::string message = std::string(Lang::Strings::VERSION) + ota_->GetCurrentVersion();
    display->ShowNotification(message.c_str());
    display->SetChatMessage("system", "");

    // Release OTA object after activation is complete
    ota_.reset();
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);

    Schedule([this]() {
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    });
}

void Application::ActivationTask() {
    // Create OTA object for activation process
    ota_ = std::make_unique<Ota>();

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version
    CheckNewVersion();

    // Initialize the protocol
    InitializeProtocol();

    // Signal completion to main loop
    xEventGroupSetBits(event_group_, MAIN_EVENT_ACTIVATION_DONE);
}

void Application::CheckAssetsVersion() {
    // Only allow CheckAssetsVersion to be called once
    if (assets_version_checked_) {
        return;
    }
    assets_version_checked_ = true;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [this, display](int progress, size_t speed) -> void {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            Schedule([display, message = std::string(buffer)]() {
                display->SetChatMessage("system", message.c_str());
            });
        });

        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            SetDeviceState(kDeviceStateActivating);
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // Initial retry delay in seconds

    auto& board = Board::GetInstance();
    while (true) {
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        esp_err_t err = ota_->CheckVersion();
        if (err != ESP_OK) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char error_message[128];
            snprintf(error_message, sizeof(error_message), "code=%d, url=%s", err, ota_->GetCheckVersionUrl().c_str());
            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, error_message);
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (GetDeviceState() == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // Double the retry delay
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // Reset retry delay

        if (ota_->HasNewVersion()) {
            if (UpgradeFirmware(ota_->GetFirmwareUrl(), ota_->GetFirmwareVersion())) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation
        }

        // No new version, mark the current version as valid
        ota_->MarkCurrentVersionValid();
        if (!ota_->HasActivationCode() && !ota_->HasActivationChallenge()) {
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_->HasActivationCode()) {
            ShowActivationCode(ota_->GetActivationCode(), ota_->GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_->Activate();
            if (err == ESP_OK) {
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (GetDeviceState() == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::InitializeProtocol() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto codec = board.GetAudioCodec();

    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    Settings websocket_settings("websocket", false);
    const std::string ws_url = websocket_settings.GetString("url");
    const bool force_websocket = websocket_settings.GetInt("force", 0) == 1;
    const bool has_local_websocket_url = !ws_url.empty();

    if (force_websocket || has_local_websocket_url || ota_->HasWebsocketConfig()) {
        if (force_websocket) {
            ESP_LOGI(TAG, "Using WebSocket protocol (forced by websocket.force)");
        } else if (has_local_websocket_url) {
            ESP_LOGI(TAG, "Using WebSocket protocol (local websocket.url configured)");
        } else {
            ESP_LOGI(TAG, "Using WebSocket protocol (OTA websocket config)");
        }
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else if (ota_->HasMqttConfig()) {
        ESP_LOGI(TAG, "Using MQTT protocol (OTA mqtt config)");
        protocol_ = std::make_unique<MqttProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in OTA/local settings, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (GetDeviceState() == kDeviceStateSpeaking) {
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    SetDeviceState(kDeviceStateSpeaking);
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (GetDeviceState() == kDeviceStateSpeaking) {
                        if (sd_music_active_.load()) {
                            SetDeviceState(kDeviceStateIdle);
                        } else if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                std::string user_text = text->valuestring;
                Schedule([display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });

                if (IsWebUiAddressQuestion(user_text)) {
                    Schedule([display]() {
                        std::string url;
                        std::string hint;
                        BuildWebUiAccessInfo(url, hint);

                        if (display != nullptr) {
                            display->ShowNotification(hint, 9000);
                            if (!url.empty()) {
                                std::string local_answer = "Địa chỉ WebUI: " + url;
                                display->SetChatMessage("assistant", local_answer.c_str());
                            }
                        }

                        if (!url.empty()) {
                            ESP_LOGI(TAG, "Local WebUI access info: %s", url.c_str());
                        } else {
                            ESP_LOGW(TAG, "Local WebUI access info unavailable: %s", hint.c_str());
                        }
                    });
                }
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                DualServoController::NotifyEmotion(emotion->valuestring);
                Schedule([display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    
    protocol_->Start();
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (GetDeviceState() == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_TOGGLE_CHAT);
}

void Application::StartListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_START_LISTENING);
}

void Application::StopListening() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STOP_LISTENING);
}

void Application::HandleToggleChatEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (state == kDeviceStateIdle) {
        ListeningMode mode = GetDefaultListeningMode();
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, mode]() {
                ContinueOpenAudioChannel(mode);
            });
            return;
        }
        SetListeningMode(mode);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
    } else if (state == kDeviceStateListening) {
        protocol_->CloseAudioChannel();
    }
}

void Application::ContinueOpenAudioChannel(ListeningMode mode) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            return;
        }
    }

    SetListeningMode(mode);
}

void Application::HandleStartListeningEvent() {
    if (sd_music_active_.load()) {
        StopSdCardMusic();
        vTaskDelay(pdMS_TO_TICKS(60));
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (state == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (state == kDeviceStateIdle) {
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this]() {
                ContinueOpenAudioChannel(kListeningModeManualStop);
            });
            return;
        }
        SetListeningMode(kListeningModeManualStop);
    } else if (state == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonNone);
        SetListeningMode(kListeningModeManualStop);
    }
}

void Application::HandleStopListeningEvent() {
    auto state = GetDeviceState();
    
    if (state == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    } else if (state == kDeviceStateListening) {
        if (protocol_) {
            protocol_->SendStopListening();
        }
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::HandleWakeWordDetectedEvent() {
    if (!protocol_) {
        return;
    }

    if (sd_music_active_.load()) {
        ESP_LOGI(TAG, "Wake word detected while SD music is playing, stopping music first");
        StopSdCardMusic();
        vTaskDelay(pdMS_TO_TICKS(60));
    }

    auto state = GetDeviceState();
    auto wake_word = audio_service_.GetLastWakeWord();
    ESP_LOGI(TAG, "Wake word detected: %s (state: %d)", wake_word.c_str(), (int)state);

    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();
        auto wake_word = audio_service_.GetLastWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update),
            // then continue with OpenAudioChannel which may block for ~1 second
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking || state == kDeviceStateListening) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
        // Clear send queue to avoid sending residues to server
        while (audio_service_.PopPacketFromSendQueue());

        if (state == kDeviceStateListening) {
            protocol_->SendStartListening(GetDefaultListeningMode());
            audio_service_.ResetDecoder();
            audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            // Re-enable wake word detection as it was stopped by the detection itself
            audio_service_.EnableWakeWordDetection(true);
        } else {
            // Play popup sound and start listening again
            play_popup_on_listening_ = true;
            SetListeningMode(GetDefaultListeningMode());
        }
    } else if (state == kDeviceStateActivating) {
        // Restart the activation check if the wake word is detected during activation
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::ContinueWakeWordInvoke(const std::string& wake_word) {
    // Check state again in case it was changed during scheduling
    if (GetDeviceState() != kDeviceStateConnecting) {
        return;
    }

    // Switch to performance mode before connecting to reduce latency
    auto& board = Board::GetInstance();
    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);

    if (!protocol_->IsAudioChannelOpened()) {
        if (!protocol_->OpenAudioChannel()) {
            audio_service_.EnableWakeWordDetection(true);
            return;
        }
    }

    ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
    // Encode and send the wake word data to the server
    while (auto packet = audio_service_.PopWakeWordPacket()) {
        protocol_->SendAudio(std::move(packet));
    }
    // Set the chat state to wake word detected
    protocol_->SendWakeWordDetected(wake_word);
    SetListeningMode(GetDefaultListeningMode());
#else
    // Set flag to play popup sound after state changes to listening
    // (PlaySound here would be cleared by ResetDecoder in EnableVoiceProcessing)
    play_popup_on_listening_ = true;
    SetListeningMode(GetDefaultListeningMode());
#endif
}

void Application::HandleStateChangedEvent() {
    DeviceState new_state = state_machine_.GetState();
    clock_ticks_ = 0;

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    switch (new_state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->ClearChatMessages();  // Clear messages first
            display->SetEmotion("neutral"); // Then set emotion (wechat mode checks child count)
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            if (play_popup_on_listening_ || !audio_service_.IsAudioProcessorRunning()) {
                // For auto mode, wait for playback queue to be empty before enabling voice processing
                // This prevents audio truncation when STOP arrives late due to network jitter
                if (listening_mode_ == kListeningModeAutoStop) {
                    audio_service_.WaitForPlaybackQueueEmpty();
                }
                
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                audio_service_.EnableVoiceProcessing(true);
            }

#ifdef CONFIG_WAKE_WORD_DETECTION_IN_LISTENING
            // Enable wake word detection in listening mode (configured via Kconfig)
            audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
#else
            // Disable wake word detection in listening mode
            audio_service_.EnableWakeWordDetection(false);
#endif
            
            // Play popup sound after ResetDecoder (in EnableVoiceProcessing) has been called
            if (play_popup_on_listening_) {
                play_popup_on_listening_ = false;
                audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_service_.EnableVoiceProcessing(false);
                // Only AFE wake word can be detected in speaking mode
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            audio_service_.ResetDecoder();
            break;
        case kDeviceStateWifiConfiguring:
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(false);
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Schedule(std::function<void()>&& callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

ListeningMode Application::GetDefaultListeningMode() const {
    return aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime;
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(const std::string& url, const std::string& version) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();

    std::string upgrade_url = url;
    std::string version_info = version.empty() ? "(Manual upgrade)" : version;

    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());

    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);

    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = Ota::Upgrade(upgrade_url, [this, display](int progress, size_t speed) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
        Schedule([display, message = std::string(buffer)]() {
            display->SetChatMessage("system", message.c_str());
        });
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER); // Restore power save level
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    if (!protocol_) {
        return;
    }

    auto state = GetDeviceState();
    
    if (state == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            // Schedule to let the state change be processed first (UI update)
            Schedule([this, wake_word]() {
                ContinueWakeWordInvoke(wake_word);
            });
            return;
        }
        // Channel already opened, continue directly
        ContinueWakeWordInvoke(wake_word);
    } else if (state == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (state == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (GetDeviceState() != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::RegisterMcpBroadcastCallback(std::function<void(const std::string&)> callback) {
    mcp_broadcast_callback_ = std::move(callback);
}

void Application::SendMcpMessage(const std::string& payload) {
    // Always schedule to run in main task for thread safety
    Schedule([this, payload](){ 
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
        if (mcp_broadcast_callback_) {
            mcp_broadcast_callback_(payload);
        }
    });
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}

bool Application::PlaySdCardMusicByMood(const std::string& mood_or_emotion) {
    std::string folder = ResolveSdMusicFolder(mood_or_emotion);
    if (folder.empty()) {
        return false;
    }

    // Prevent too-frequent re-trigger when server sends many same emotions continuously.
    static std::string s_last_folder;
    static int64_t s_last_play_us = 0;
    int64_t now_us = esp_timer_get_time();
    if (s_last_folder == folder && (now_us - s_last_play_us) < 5000000) {
        return false;
    }

    std::vector<std::string> candidates;
    CollectAudioCandidatesFromFolder(folder, candidates);

    if (candidates.empty()) {
        ESP_LOGW(TAG, "No audio files in %s", folder.c_str());
        return false;
    }

    size_t index = esp_random() % candidates.size();

    ESP_LOGI(TAG, "Play SD music: %s", candidates[index].c_str());

    {
        std::lock_guard<std::mutex> lock(sd_music_mutex_);
        sd_music_playlist_ = candidates;
        sd_music_index_ = static_cast<int>(index);
        sd_music_autoplay_next_ = true;
        sd_music_shuffle_mode_ = true;
    }
    if (!StartSdMusicPlaybackPath(candidates[index])) {
        return false;
    }

    s_last_folder = folder;
    s_last_play_us = now_us;
    return true;
}

bool Application::PlaySdCardMusicByQuery(const std::string& query, const std::string& mood_hint) {
    auto folders = ResolveSdMusicSearchFolders(mood_hint);
    std::vector<std::string> candidates;
    for (const auto& folder : folders) {
        CollectAudioCandidatesFromFolder(folder, candidates);
    }
    if (candidates.empty()) {
        ESP_LOGW(TAG, "No SD music candidates found");
        return false;
    }

    int best_score = -1;
    std::string best_path;
    for (const auto& path : candidates) {
        std::string name = BaseNameFromPath(path);
        int score = MatchScoreAdvanced(name, query);
        if (score > best_score) {
            best_score = score;
            best_path = path;
        }
    }

    if (best_score <= 0 || best_path.empty()) {
        ESP_LOGW(TAG, "No SD music match for query: %s", query.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Play SD music by query '%s': %s", query.c_str(), best_path.c_str());

    {
        std::lock_guard<std::mutex> lock(sd_music_mutex_);
        sd_music_playlist_ = candidates;
        sd_music_index_ = -1;
        sd_music_autoplay_next_ = false;
        sd_music_shuffle_mode_ = false;
        for (size_t i = 0; i < sd_music_playlist_.size(); ++i) {
            if (sd_music_playlist_[i] == best_path) {
                sd_music_index_ = static_cast<int>(i);
                break;
            }
        }
    }
    return StartSdMusicPlaybackPath(best_path);
}

bool Application::StartSdMusicPlaybackLocked(const std::string& path) {
    // Stop current playback first
    sd_music_stop_requested_ = true;
    audio_service_.RequestPlaybackStop();

    if (sd_music_task_handle_ != nullptr) {
        // Existing task is stopping; continue and start a new one anyway.
        // Decoder stop flag ensures old task exits soon.
    }

    sd_music_stop_requested_ = false;
    audio_service_.ClearPlaybackStopRequest();
    sd_music_active_.store(true);

    struct SdMusicTaskArg {
        std::string path;
        uint32_t generation;
    };

    uint32_t generation = ++sd_music_generation_;
    auto* task_arg = new (std::nothrow) SdMusicTaskArg{path, generation};
    if (task_arg == nullptr) {
        ESP_LOGE(TAG, "No memory for SD music task arg");
        return false;
    }

    BaseType_t ok = xTaskCreate([](void* arg) {
        std::unique_ptr<SdMusicTaskArg> task_arg_ptr(static_cast<SdMusicTaskArg*>(arg));
        auto& app = Application::GetInstance();

        bool played = false;
        uint32_t generation = 0;
        if (task_arg_ptr) {
            generation = task_arg_ptr->generation;
        }
        if (task_arg_ptr && !task_arg_ptr->path.empty()) {
            if (HasMp3Extension(task_arg_ptr->path)) {
                played = app.GetAudioService().PlayMp3File(task_arg_ptr->path.c_str());
            } else {
                played = app.GetAudioService().PlayOggFile(task_arg_ptr->path.c_str());
            }
        }

        app.Schedule([played, generation]() {
            auto& self = Application::GetInstance();
            bool stopped = false;
            bool is_current = false;
            bool should_autoplay_next = false;
            std::string next_path;
            {
                std::lock_guard<std::mutex> lock(self.sd_music_mutex_);
                stopped = self.sd_music_stop_requested_;
                is_current = (generation == self.sd_music_generation_);
                if (is_current) {
                    self.sd_music_task_handle_ = nullptr;
                    self.sd_music_active_.store(false);

                    if (!stopped && played && self.sd_music_autoplay_next_ && !self.sd_music_playlist_.empty()) {
                        if (self.sd_music_shuffle_mode_) {
                            self.sd_music_index_ = static_cast<int>(esp_random() % self.sd_music_playlist_.size());
                        } else {
                            if (self.sd_music_index_ < 0) {
                                self.sd_music_index_ = 0;
                            } else {
                                self.sd_music_index_ = (self.sd_music_index_ + 1) % static_cast<int>(self.sd_music_playlist_.size());
                            }
                        }
                        next_path = self.sd_music_playlist_[self.sd_music_index_];
                        should_autoplay_next = true;
                    }
                }
            }

            if (is_current && should_autoplay_next) {
                ESP_LOGI(TAG, "SD autoplay next: %s", next_path.c_str());
                if (!self.StartSdMusicPlaybackPath(next_path)) {
                    auto display = Board::GetInstance().GetDisplay();
                    display->SetChatMessage("assistant", "Mình chưa chuyển được bài tiếp theo. Bạn thử yêu cầu lại nhé.");
                }
                return;
            }

            if (is_current && !stopped) {
                auto display = Board::GetInstance().GetDisplay();
                if (played) {
                    display->SetChatMessage("assistant", "Mình đã phát xong bài này. Bạn muốn nghe bài khác không?");
                } else {
                    display->SetChatMessage("assistant", "Mình chưa phát được bài này. Bạn muốn chọn bài khác không?");
                }
            }
        });

        vTaskDelete(nullptr);
    }, "sd_music", 4096 * 3, task_arg, 3, &sd_music_task_handle_);

    if (ok != pdPASS) {
        delete task_arg;
        sd_music_task_handle_ = nullptr;
        sd_music_active_.store(false);
        ESP_LOGE(TAG, "Failed to create SD music task");
        return false;
    }
    return true;
}

bool Application::StartSdMusicPlaybackPath(const std::string& path) {
    std::lock_guard<std::mutex> lock(sd_music_mutex_);
    return StartSdMusicPlaybackLocked(path);
}

bool Application::StopSdCardMusic() {
    std::lock_guard<std::mutex> lock(sd_music_mutex_);
    sd_music_stop_requested_ = true;
    sd_music_autoplay_next_ = false;
    sd_music_active_.store(false);
    audio_service_.RequestPlaybackStop();
    return true;
}

bool Application::PlayNextSdCardMusic() {
    std::lock_guard<std::mutex> lock(sd_music_mutex_);
    if (sd_music_playlist_.empty()) {
        return false;
    }
    if (sd_music_index_ < 0) {
        sd_music_index_ = 0;
    } else {
        sd_music_index_ = (sd_music_index_ + 1) % static_cast<int>(sd_music_playlist_.size());
    }
    return StartSdMusicPlaybackLocked(sd_music_playlist_[sd_music_index_]);
}

void Application::ResetProtocol() {
    Schedule([this]() {
        // Close audio channel if opened
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
        // Reset protocol
        protocol_.reset();
    });
}

void Application::ReloadProtocolConfig() {
    Schedule([this]() {
        const bool was_opened = protocol_ && protocol_->IsAudioChannelOpened();
        const ListeningMode mode = listening_mode_;

        if (protocol_ && was_opened) {
            protocol_->CloseAudioChannel(false);
        }
        protocol_.reset();

        InitializeProtocol();

        if (was_opened && protocol_ != nullptr) {
            SetDeviceState(kDeviceStateConnecting);
            Schedule([this, mode]() {
                ContinueOpenAudioChannel(mode);
            });
        }
    });
}

