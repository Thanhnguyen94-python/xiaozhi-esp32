#ifndef BREAD_COMPACT_WIFI_ROBOT_LIFE_BEHAVIOR_H
#define BREAD_COMPACT_WIFI_ROBOT_LIFE_BEHAVIOR_H

// Hanh vi "co su song": gat/lac dau nhe, nhich banh xe nhe khi robot noi/nghe.
// File header-only, dat trong boards/bread-compact-wifi/ (khong can sua CMakeLists).
// Chi DOC trang thai tu Application (GetDeviceState / IsVoiceDetected),
// khong sua gi trong code loi cua xiaozhi.

#include "application.h"
#include "dual_dc_motor_controller.h"
#include "dual_servo_controller.h"
#include "rc_sbus_controller.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class RobotLifeBehavior {
public:
    // ===================== THONG SO CAN CHINH =====================
    // Nhich banh xe (robot dung tren BAN). Chi chay khi kCmPerSec > 0.
    // kCmPerSec = so cm robot di duoc moi giay o toc do kNudgeSpeed, tren chinh mat ban do.
    // Cach do: xem huong dan trong chat (ghi nhan quang duong khi chay ~200ms roi chia 0.2).
    static constexpr bool  kEnableWheelNudge = true;
    static constexpr int   kNudgeSpeed       = 90;    // 80..100
    static constexpr float kCmPerSec         = 50.0f;  // <-- DIEN SAU KHI DO. 0 = tat nhich
    static constexpr float kNudgeLegCm       = 4.0f;  // moi lan nhich di khoang bao nhieu cm
    static constexpr float kMaxDriftCm       = 10.0f; // khong bao gio lech qua muc nay so voi cho dat (10..25)
    static constexpr int   kNudgeMinMs       = 60;    // chan duoi thoi gian cap dien
    static constexpr int   kNudgeMaxMs       = 400;   // chan tren thoi gian cap dien
    static constexpr uint32_t kNudgeMinGapMs = 2500;
    static constexpr uint32_t kNudgeMaxGapMs = 4000;

    // Dau khi robot dang noi (talkingMotion cua DualServoController)
    static constexpr uint32_t kFirstTalkDelayMs = 1500;  // cho action theo emotion chay xong
    static constexpr uint32_t kTalkMinGapMs     = 1800;
    static constexpr uint32_t kTalkMaxGapMs     = 3200;

    // Khi dang nghe nguoi dung noi: gat dau
    static constexpr uint32_t kListenVoiceHoldMs = 1500;  // nghe lien tuc >= 1.5s moi gat
    static constexpr uint32_t kListenNodGapMs    = 4500;  // toi thieu giua 2 lan gat

    // Khi ranh (Idle): cu dong nho ngau nhien
    static constexpr bool     kEnableIdleFidget = true;
    static constexpr uint32_t kIdleMinGapMs     = 20000;
    static constexpr uint32_t kIdleMaxGapMs     = 45000;
    // ===============================================================

    RobotLifeBehavior(DualDcMotorController* wheel_motor, DualServoController* head_servo)
        : wheel_motor_(wheel_motor), head_servo_(head_servo) {}

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
        ESP_LOGI(TAG, "RobotLifeBehavior started (wheel_nudge=%d)", kEnableWheelNudge ? 1 : 0);
    }

    // Tam dung/tiep tuc (vi du khi macro dang phat). Mac dinh bat.
    void SetEnabled(bool enabled) { enabled_.store(enabled); }

private:
    static constexpr const char* TAG = "RobotLife";

    DualDcMotorController* wheel_motor_ = nullptr;
    DualServoController* head_servo_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> enabled_{true};

    DeviceState last_state_ = kDeviceStateUnknown;
    uint32_t next_talk_ms_ = 0;
    uint32_t next_nudge_ms_ = 0;
    uint32_t next_idle_ms_ = 0;
    uint32_t voice_since_ms_ = 0;
    uint32_t last_listen_nod_ms_ = 0;
    float drift_cm_ = 0.0f;  // uoc tinh vi tri hien tai so voi cho dat (+ la phia truoc)

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

    void OnEnterState(DeviceState from, DeviceState to, uint32_t now) {
        if (from == kDeviceStateSpeaking && head_servo_ != nullptr) {
            head_servo_->RunAction("center");  // tra dau ve giua khi noi xong
        }
        voice_since_ms_ = 0;
        if (to == kDeviceStateSpeaking) {
            next_talk_ms_ = now + kFirstTalkDelayMs;
            next_nudge_ms_ = now + RandRange(kNudgeMinGapMs, kNudgeMaxGapMs);
        } else if (to == kDeviceStateIdle) {
            next_idle_ms_ = now + RandRange(kIdleMinGapMs, kIdleMaxGapMs);
        }
    }

    bool RcIsDriving() const {
        return rc_sbus::IsOverrideActive();
    }

    void Nudge() {
        if (!kEnableWheelNudge || wheel_motor_ == nullptr || !wheel_motor_->IsReady()) {
            return;
        }
        if (kCmPerSec <= 0.0f) {
            return;  // chua hieu chuan => khong nhich
        }
        if (RcIsDriving()) {
            drift_cm_ = 0.0f;  // vi tri da bi nguoi dung thay doi
            return;
        }
        int l = 0, r = 0;
        wheel_motor_->GetSpeed(l, r);
        if (l != 0 || r != 0) {
            drift_cm_ = 0.0f;  // banh dang chay do lenh khac (voice/web), khong chen vao
            return;
        }

        bool can_fwd = (drift_cm_ + kNudgeLegCm) <= kMaxDriftCm;
        bool can_back = (drift_cm_ - kNudgeLegCm) >= -kMaxDriftCm;
        if (!can_fwd && !can_back) {
            return;
        }
        bool forward = (can_fwd && can_back) ? ((esp_random() & 1U) != 0) : can_fwd;

        int duration_ms = static_cast<int>(kNudgeLegCm / kCmPerSec * 1000.0f);
        duration_ms = std::clamp(duration_ms, kNudgeMinMs, kNudgeMaxMs);
        int expect = forward ? kNudgeSpeed : -kNudgeSpeed;

        if (forward) {
            wheel_motor_->Forward(kNudgeSpeed);
        } else {
            wheel_motor_->Backward(kNudgeSpeed);
        }
        vTaskDelay(pdMS_TO_TICKS(duration_ms));

        // Chi dung neu banh van dang o trang thai do minh dat.
        wheel_motor_->GetSpeed(l, r);
        if (l == expect && r == expect) {
            wheel_motor_->Stop();
            float moved = static_cast<float>(duration_ms) / 1000.0f * kCmPerSec;
            drift_cm_ += forward ? moved : -moved;
            ESP_LOGI(TAG, "Nudge %s %dms, drift=%.1fcm", forward ? "fwd" : "back", duration_ms, drift_cm_);
        } else {
            drift_cm_ = 0.0f;
        }
    }

    void Loop() {
        auto& app = Application::GetInstance();
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(100));

            if (!enabled_.load() || RcIsDriving()) {
                continue;
            }

            uint32_t now = NowMs();
            DeviceState state = app.GetDeviceState();
            if (state != last_state_) {
                OnEnterState(last_state_, state, now);
                last_state_ = state;
            }

            switch (state) {
                case kDeviceStateSpeaking:
                    if (now >= next_talk_ms_) {
                        head_servo_->talkingMotion();
                        next_talk_ms_ = now + RandRange(kTalkMinGapMs, kTalkMaxGapMs);
                    }
                    if (now >= next_nudge_ms_) {
                        Nudge();
                        next_nudge_ms_ = NowMs() + RandRange(kNudgeMinGapMs, kNudgeMaxGapMs);
                    }
                    break;

                case kDeviceStateListening:
                    if (app.IsVoiceDetected()) {
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
                    if (kEnableIdleFidget && now >= next_idle_ms_) {
                        head_servo_->curiousHeadTilt();
                        next_idle_ms_ = now + RandRange(kIdleMinGapMs, kIdleMaxGapMs);
                    }
                    break;

                default:
                    break;
            }
        }
    }
};

#endif  // BREAD_COMPACT_WIFI_ROBOT_LIFE_BEHAVIOR_H