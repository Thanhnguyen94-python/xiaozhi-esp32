#ifndef BREAD_COMPACT_WIFI_ROBOT_HARDWARE_H
#define BREAD_COMPACT_WIFI_ROBOT_HARDWARE_H

// Danh sach phan cung thuc te cua robot, de WebUI tu an cac the khong dung.
// - Tu phat hien luc khoi dong: the SD, man phu 0.91".
// - Nguoi dung tu bat/tat (khong tu do duoc vi la ngo ra PWM/UART): banh xe, servo co, tay cam RC.
//   Luu NVS (namespace "robot_hw"). Tat banh xe/servo: Robot Life khong dung toi.
//   Tat RC: khong khoi dong bo thu SBUS o lan boot sau (giai phong UART1/GPIO19).
// File header-only, dat trong boards/bread-compact-wifi/ (khong can sua CMakeLists).

#include "settings.h"

#include <atomic>

#include <cJSON.h>

class RobotHardware {
public:
    RobotHardware() {
        Settings s("robot_hw", false);
        wheels_.store(s.GetInt("wheels", 1) != 0);
        head_.store(s.GetInt("head", 1) != 0);
        rc_.store(s.GetInt("rc", 1) != 0);
        rc_at_boot_ = rc_.load();
    }

    // Board goi sau khi khoi tao phan cung.
    void SetDetected(bool sdcard, bool oled2) {
        sdcard_.store(sdcard);
        oled2_ = oled2;
    }

    // The SD rut/cam luc dang chay (board theo doi va cap nhat).
    void SetSdcardPresent(bool present) {
        if (sdcard_.exchange(present) != present) {
            stamp_.fetch_add(1);
        }
    }

    // Trang thai song cho WebUI (gui qua WebSocket "status").
    void AppendStatusToJson(cJSON* root) {
        cJSON_AddNumberToObject(root, "hw_sdcard", sdcard_.load() ? 1 : 0);
    }

    // Thay doi khi trang thai doi -> WebUI chi broadcast khi can.
    uint32_t StatusStamp() const { return stamp_.load(); }

    bool WheelsEnabled() const { return wheels_.load(); }
    bool HeadEnabled() const { return head_.load(); }
    bool RcEnabled() const { return rc_.load(); }

    void AppendToJson(cJSON* root) {
        cJSON_AddNumberToObject(root, "hw_wheels", wheels_.load() ? 1 : 0);
        cJSON_AddNumberToObject(root, "hw_head", head_.load() ? 1 : 0);
        cJSON_AddNumberToObject(root, "hw_rc", rc_.load() ? 1 : 0);
        cJSON_AddNumberToObject(root, "hw_rc_running", rc_at_boot_ ? 1 : 0);
        cJSON_AddNumberToObject(root, "hw_sdcard", sdcard_.load() ? 1 : 0);
        cJSON_AddNumberToObject(root, "hw_oled2", oled2_ ? 1 : 0);
    }

    void ApplyFromJson(const cJSON* root) {
        if (root == nullptr) {
            return;
        }
        bool changed = false;
        auto get = [&](const char* key, std::atomic<bool>& dst) {
            const cJSON* it = cJSON_GetObjectItem(root, key);
            if (cJSON_IsNumber(it)) {
                dst.store(it->valueint != 0);
                changed = true;
            }
        };
        get("hw_wheels", wheels_);
        get("hw_head", head_);
        get("hw_rc", rc_);
        if (changed) {
            Settings s("robot_hw", true);
            s.SetInt("wheels", wheels_.load() ? 1 : 0);
            s.SetInt("head", head_.load() ? 1 : 0);
            s.SetInt("rc", rc_.load() ? 1 : 0);
        }
    }

private:
    std::atomic<bool> wheels_{true};
    std::atomic<bool> head_{true};
    std::atomic<bool> rc_{true};
    bool rc_at_boot_ = true;  // RC chi bat/tat duoc khi khoi dong
    std::atomic<bool> sdcard_{false};
    std::atomic<uint32_t> stamp_{0};
    bool oled2_ = false;
};

#endif  // BREAD_COMPACT_WIFI_ROBOT_HARDWARE_H
