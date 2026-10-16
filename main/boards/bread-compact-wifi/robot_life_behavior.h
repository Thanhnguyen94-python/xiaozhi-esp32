#ifndef BREAD_COMPACT_WIFI_ROBOT_LIFE_BEHAVIOR_H
#define BREAD_COMPACT_WIFI_ROBOT_LIFE_BEHAVIOR_H

// Hanh vi "co su song": gat/lac dau nhe, nhich banh xe nhe khi robot noi/nghe.
// File header-only, dat trong boards/bread-compact-wifi/ (khong can sua CMakeLists).
// Chi DOC trang thai tu Application (GetDeviceState / IsVoiceDetected).
// Thong so duoc luu trong NVS (namespace "robot_life") va chinh duoc tu WebUI.

#include "application.h"
#include "dual_dc_motor_controller.h"
#include "dual_servo_controller.h"
#include "rc_sbus_controller.h"
#include "robot_sensor_hub.h"
#include "robot_hardware.h"
#include "settings.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Cac thong so chinh duoc tu WebUI (xem chu thich o the "Hanh vi co su song").
struct LifeConfig {
    int enable_head_talk  = 1;   // 1 = cu dong dau khi robot dang noi
    int enable_listen_nod = 1;   // 1 = gat dau khi nghe nguoi dung noi
    int enable_idle       = 1;   // 1 = cu dong nho khi ranh
    int enable_wheel      = 0;   // 1 = cho phep nhich banh xe (MAC DINH TAT de an toan)
    int nudge_speed       = 90;  // toc do banh khi nhich, 30..100
    int nudge_ms          = 160; // thoi gian cap dien moi lan nhich, 40..500 ms
    int nudge_cm          = 4;   // quang duong THUC TE moi lan nhich (do tay), 1..25 cm
    int max_drift_cm      = 10;  // khong lech qua muc nay so voi cho dat, 5..25 cm
    int nudge_gap_min_s   = 7;   // nghi giua 2 lan nhich (giay)
    int nudge_gap_max_s   = 14;
    int idle_gap_min_s    = 20;  // nghi giua 2 cu dong khi ranh (giay)
    int idle_gap_max_s    = 45;
};

class RobotLifeBehavior {
public:
    // Hang so co dinh (khong phu thuoc kich thuoc robot)
    static constexpr uint32_t kFirstTalkDelayMs  = 1500;  // cho action theo emotion chay xong
    static constexpr uint32_t kTalkMinGapMs      = 1800;
    static constexpr uint32_t kTalkMaxGapMs      = 3200;
    static constexpr uint32_t kListenVoiceHoldMs = 1500;  // nghe lien tuc >= 1.5s moi gat
    static constexpr uint32_t kListenNodGapMs    = 4500;  // toi thieu giua 2 lan gat

    RobotLifeBehavior(DualDcMotorController* wheel_motor, DualServoController* head_servo)
        : wheel_motor_(wheel_motor), head_servo_(head_servo) {
        LoadConfig();
    }

    ~RobotLifeBehavior() {
        if (task_ != nullptr) {
            vTaskDelete(task_);
            task_ = nullptr;
        }
    }

    void Start() {
        if (task_ != nullptr || head_servo_ == nullptr) {
            return;
        }
        BaseType_t ok = xTaskCreate(&RobotLifeBehavior::TaskEntry, "robot_life", 4096, this, 3, &task_);
        if (ok != pdPASS) {
            task_ = nullptr;
            ESP_LOGE(TAG, "Khong tao duoc task robot_life");
            return;
        }
        ESP_LOGI(TAG, "RobotLifeBehavior started");
    }

    // Cam bien an toan (tuy chon): khong nhich ve phia dang bi chan / mep ban.
    void SetSensorHub(RobotSensorHub* sensors) { sensors_ = sensors; }

    // Phan cung bi tat tren WebUI thi khong cu dong phan do.
    void SetHardware(RobotHardware* hw) { hw_ = hw; }

    // Tam dung/tiep tuc (vi du khi macro dang phat). Mac dinh bat.
    void SetEnabled(bool enabled) { enabled_.store(enabled); }

    LifeConfig GetConfig() {
        std::lock_guard<std::mutex> lock(config_mutex_);
        return config_;
    }

    void SetConfig(LifeConfig c) {
        Sanitize(c);
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            config_ = c;
        }
        SaveConfig(c);
    }

    // ---- Cau noi voi WebUI ----
    void AppendConfigToJson(cJSON* root) {
        LifeConfig c = GetConfig();
        cJSON_AddNumberToObject(root, "life_head_talk", c.enable_head_talk);
        cJSON_AddNumberToObject(root, "life_listen_nod", c.enable_listen_nod);
        cJSON_AddNumberToObject(root, "life_idle", c.enable_idle);
        cJSON_AddNumberToObject(root, "life_wheel", c.enable_wheel);
        cJSON_AddNumberToObject(root, "life_nudge_speed", c.nudge_speed);
        cJSON_AddNumberToObject(root, "life_nudge_ms", c.nudge_ms);
        cJSON_AddNumberToObject(root, "life_nudge_cm", c.nudge_cm);
        cJSON_AddNumberToObject(root, "life_max_drift_cm", c.max_drift_cm);
        cJSON_AddNumberToObject(root, "life_nudge_gap_min_s", c.nudge_gap_min_s);
        cJSON_AddNumberToObject(root, "life_nudge_gap_max_s", c.nudge_gap_max_s);
        cJSON_AddNumberToObject(root, "life_idle_gap_min_s", c.idle_gap_min_s);
        cJSON_AddNumberToObject(root, "life_idle_gap_max_s", c.idle_gap_max_s);
    }

    // Doc cac truong "life_*" trong JSON (neu co), ap dung + luu. Neu co "life_test"
    // ("forward"/"backward") thi nhich thu 1 lan sau khi ap dung.
    void ApplyConfigFromJson(const cJSON* root) {
        if (root == nullptr) {
            return;
        }
        LifeConfig c = GetConfig();
        bool changed = false;
        auto get = [&](const char* key, int& dst) {
            const cJSON* it = cJSON_GetObjectItem(root, key);
            if (cJSON_IsNumber(it)) {
                dst = it->valueint;
                changed = true;
            }
        };
        get("life_head_talk", c.enable_head_talk);
        get("life_listen_nod", c.enable_listen_nod);
        get("life_idle", c.enable_idle);
        get("life_wheel", c.enable_wheel);
        get("life_nudge_speed", c.nudge_speed);
        get("life_nudge_ms", c.nudge_ms);
        get("life_nudge_cm", c.nudge_cm);
        get("life_max_drift_cm", c.max_drift_cm);
        get("life_nudge_gap_min_s", c.nudge_gap_min_s);
        get("life_nudge_gap_max_s", c.nudge_gap_max_s);
        get("life_idle_gap_min_s", c.idle_gap_min_s);
        get("life_idle_gap_max_s", c.idle_gap_max_s);
        if (changed) {
            SetConfig(c);
        }

        const cJSON* test = cJSON_GetObjectItem(root, "life_test");
        if (cJSON_IsString(test) && test->valuestring != nullptr) {
            TestNudge(std::strcmp(test->valuestring, "forward") == 0);
        }
    }

    // Nhich thu 1 lan (de do quang duong). Bo qua cong tac enable_wheel.
    bool TestNudge(bool forward) {
        LifeConfig c = GetConfig();
        if (!WheelsFree()) {
            ESP_LOGW(TAG, "Test nudge bi bo qua (RC dang lai hoac banh dang chay)");
            return false;
        }
        bool ok = RunNudge(forward, c);
        drift_cm_.store(0.0f);  // vi tri moi lam moc moi
        ESP_LOGI(TAG, "Test nudge %s %dms speed=%d: %s", forward ? "fwd" : "back",
                 c.nudge_ms, c.nudge_speed, ok ? "OK" : "FAIL");
        return ok;
    }

private:
    static constexpr const char* TAG = "RobotLife";

    DualDcMotorController* wheel_motor_ = nullptr;
    DualServoController* head_servo_ = nullptr;
    RobotSensorHub* sensors_ = nullptr;
    RobotHardware* hw_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> enabled_{true};

    std::mutex config_mutex_;
    LifeConfig config_;

    DeviceState last_state_ = kDeviceStateUnknown;
    uint32_t next_talk_ms_ = 0;
    uint32_t next_nudge_ms_ = 0;
    uint32_t next_idle_ms_ = 0;
    uint32_t voice_since_ms_ = 0;
    uint32_t last_listen_nod_ms_ = 0;
    std::atomic<float> drift_cm_{0.0f};  // uoc tinh vi tri so voi cho dat (+ la phia truoc)

    static void Sanitize(LifeConfig& c) {
        c.enable_head_talk  = c.enable_head_talk ? 1 : 0;
        c.enable_listen_nod = c.enable_listen_nod ? 1 : 0;
        c.enable_idle       = c.enable_idle ? 1 : 0;
        c.enable_wheel      = c.enable_wheel ? 1 : 0;
        c.nudge_speed       = std::clamp(c.nudge_speed, 30, 100);
        c.nudge_ms          = std::clamp(c.nudge_ms, 40, 500);
        c.nudge_cm          = std::clamp(c.nudge_cm, 1, 25);
        c.max_drift_cm      = std::clamp(c.max_drift_cm, 5, 25);
        c.nudge_gap_min_s   = std::clamp(c.nudge_gap_min_s, 2, 120);
        c.nudge_gap_max_s   = std::clamp(c.nudge_gap_max_s, 2, 300);
        if (c.nudge_gap_max_s < c.nudge_gap_min_s) c.nudge_gap_max_s = c.nudge_gap_min_s;
        c.idle_gap_min_s    = std::clamp(c.idle_gap_min_s, 5, 600);
        c.idle_gap_max_s    = std::clamp(c.idle_gap_max_s, 5, 1200);
        if (c.idle_gap_max_s < c.idle_gap_min_s) c.idle_gap_max_s = c.idle_gap_min_s;
    }

    void LoadConfig() {
        LifeConfig c;
        {
            Settings s("robot_life", false);
            c.enable_head_talk  = s.GetInt("head_talk", c.enable_head_talk);
            c.enable_listen_nod = s.GetInt("listen_nod", c.enable_listen_nod);
            c.enable_idle       = s.GetInt("idle", c.enable_idle);
            c.enable_wheel      = s.GetInt("wheel", c.enable_wheel);
            c.nudge_speed       = s.GetInt("n_speed", c.nudge_speed);
            c.nudge_ms          = s.GetInt("n_ms", c.nudge_ms);
            c.nudge_cm          = s.GetInt("n_cm", c.nudge_cm);
            c.max_drift_cm      = s.GetInt("max_drift", c.max_drift_cm);
            c.nudge_gap_min_s   = s.GetInt("n_gap_min", c.nudge_gap_min_s);
            c.nudge_gap_max_s   = s.GetInt("n_gap_max", c.nudge_gap_max_s);
            c.idle_gap_min_s    = s.GetInt("i_gap_min", c.idle_gap_min_s);
            c.idle_gap_max_s    = s.GetInt("i_gap_max", c.idle_gap_max_s);
        }
        Sanitize(c);
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_ = c;
    }

    void SaveConfig(const LifeConfig& c) {
        Settings s("robot_life", true);
        s.SetInt("head_talk", c.enable_head_talk);
        s.SetInt("listen_nod", c.enable_listen_nod);
        s.SetInt("idle", c.enable_idle);
        s.SetInt("wheel", c.enable_wheel);
        s.SetInt("n_speed", c.nudge_speed);
        s.SetInt("n_ms", c.nudge_ms);
        s.SetInt("n_cm", c.nudge_cm);
        s.SetInt("max_drift", c.max_drift_cm);
        s.SetInt("n_gap_min", c.nudge_gap_min_s);
        s.SetInt("n_gap_max", c.nudge_gap_max_s);
        s.SetInt("i_gap_min", c.idle_gap_min_s);
        s.SetInt("i_gap_max", c.idle_gap_max_s);
    }

    static uint32_t NowMs() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    }

    static uint32_t RandRange(uint32_t lo, uint32_t hi) {
        if (hi <= lo) {
            return lo;
        }
        return lo + (esp_random() % (hi - lo + 1));
    }

    static void TaskEntry(void* arg) {
        static_cast<RobotLifeBehavior*>(arg)->Loop();
    }

    bool RcIsDriving() const {
        return rc_sbus::IsOverrideActive();
    }

    // Banh san sang, RC khong lai, va khong co lenh nao khac dang chay banh.
    bool WheelsFree() {
        if (wheel_motor_ == nullptr || !wheel_motor_->IsReady() || RcIsDriving()) {
            return false;
        }
        int l = 0, r = 0;
        wheel_motor_->GetSpeed(l, r);
        return l == 0 && r == 0;
    }

    // Cap dien nudge_ms roi dung. Chi dung neu banh van o trang thai do minh dat.
    bool RunNudge(bool forward, const LifeConfig& c) {
        int expect = forward ? c.nudge_speed : -c.nudge_speed;
        if (forward) {
            wheel_motor_->Forward(c.nudge_speed);
        } else {
            wheel_motor_->Backward(c.nudge_speed);
        }
        vTaskDelay(pdMS_TO_TICKS(c.nudge_ms));

        int l = 0, r = 0;
        wheel_motor_->GetSpeed(l, r);
        if (l == expect && r == expect) {
            wheel_motor_->Stop();
            return true;
        }
        return false;  // co lenh khac chen vao, khong dung banh cua ho
    }

    bool HeadOn() const { return hw_ == nullptr || hw_->HeadEnabled(); }

    void Nudge(const LifeConfig& c) {
        if (!c.enable_wheel || (hw_ != nullptr && !hw_->WheelsEnabled())) {
            return;
        }
        if (!WheelsFree()) {
            drift_cm_.store(0.0f);  // vi tri da bi nguoi dung/lenh khac thay doi
            return;
        }

        float drift = drift_cm_.load();
        float leg = static_cast<float>(c.nudge_cm);
        float limit = static_cast<float>(c.max_drift_cm);
        bool can_fwd = (drift + leg) <= limit;
        bool can_back = (drift - leg) >= -limit;
        if (sensors_ != nullptr) {
            can_fwd = can_fwd && !sensors_->FrontBlocked();
            can_back = can_back && !sensors_->BackBlocked();
        }
        if (!can_fwd && !can_back) {
            return;
        }
        bool forward = (can_fwd && can_back) ? ((esp_random() & 1U) != 0) : can_fwd;

        if (RunNudge(forward, c)) {
            drift += forward ? leg : -leg;
            drift_cm_.store(drift);
            ESP_LOGI(TAG, "Nudge %s %dms, drift=%.1fcm", forward ? "fwd" : "back", c.nudge_ms, drift);
        } else {
            drift_cm_.store(0.0f);
        }
    }

    void OnEnterState(DeviceState from, DeviceState to, uint32_t now, const LifeConfig& c) {
        if (from == kDeviceStateSpeaking && head_servo_ != nullptr && HeadOn()) {
            head_servo_->RunAction("center");  // tra dau ve giua khi noi xong
        }
        voice_since_ms_ = 0;
        if (to == kDeviceStateSpeaking) {
            next_talk_ms_ = now + kFirstTalkDelayMs;
            next_nudge_ms_ = now + RandRange(c.nudge_gap_min_s * 1000u, c.nudge_gap_max_s * 1000u);
        } else if (to == kDeviceStateIdle) {
            next_idle_ms_ = now + RandRange(c.idle_gap_min_s * 1000u, c.idle_gap_max_s * 1000u);
        }
    }

    void Loop() {
        auto& app = Application::GetInstance();
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(100));

            if (!enabled_.load() || RcIsDriving()) {
                continue;
            }

            LifeConfig c = GetConfig();
            uint32_t now = NowMs();
            DeviceState state = app.GetDeviceState();
            if (state != last_state_) {
                OnEnterState(last_state_, state, now, c);
                last_state_ = state;
            }

            switch (state) {
                case kDeviceStateSpeaking:
                    if (c.enable_head_talk && HeadOn() && now >= next_talk_ms_) {
                        head_servo_->talkingMotion();
                        next_talk_ms_ = now + RandRange(kTalkMinGapMs, kTalkMaxGapMs);
                    }
                    if (now >= next_nudge_ms_) {
                        Nudge(c);
                        next_nudge_ms_ = NowMs() + RandRange(c.nudge_gap_min_s * 1000u, c.nudge_gap_max_s * 1000u);
                    }
                    break;

                case kDeviceStateListening:
                    if (c.enable_listen_nod && HeadOn() && app.IsVoiceDetected()) {
                        if (voice_since_ms_ == 0) {
                            voice_since_ms_ = now;
                        }
                        if ((now - voice_since_ms_) >= kListenVoiceHoldMs &&
                            (now - last_listen_nod_ms_) >= kListenNodGapMs) {
                            head_servo_->nodYes();
                            last_listen_nod_ms_ = now;
                            voice_since_ms_ = now;
                        }
                    } else {
                        voice_since_ms_ = 0;
                    }
                    break;

                case kDeviceStateIdle:
                    if (c.enable_idle && HeadOn() && now >= next_idle_ms_) {
                        head_servo_->curiousHeadTilt();
                        next_idle_ms_ = now + RandRange(c.idle_gap_min_s * 1000u, c.idle_gap_max_s * 1000u);
                    }
                    break;

                default:
                    break;
            }
        }
    }
};

#endif  // BREAD_COMPACT_WIFI_ROBOT_LIFE_BEHAVIOR_H