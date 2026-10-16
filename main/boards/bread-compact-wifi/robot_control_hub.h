#ifndef BREAD_COMPACT_WIFI_ROBOT_CONTROL_HUB_H
#define BREAD_COMPACT_WIFI_ROBOT_CONTROL_HUB_H

#include "display.h"
#include "dual_dc_motor_controller.h"
#include "dual_servo_controller.h"

#include <esp_timer.h>
#include <lvgl.h>

#include <string>
#include <utility>

class RobotControlHub {
public:
    RobotControlHub(DualDcMotorController* wheel_motor, DualServoController* head_servo, Display* display);
    ~RobotControlHub();

    void RegisterTools();
    std::pair<std::string, std::string> GetWebUiAccessInfo();
    void ShowWebUiAccessOnDisplay();
    // Che do cau hinh Wi-Fi: QR de dien thoai quet la vao diem phat cua robot (giu toi khi an).
    void ShowWifiSetupQrOnDisplay();
    void HideQrOnDisplay() { HideWebUiQrOnDisplay(); }

private:
    DualDcMotorController* wheel_motor_ = nullptr;
    DualServoController* head_servo_ = nullptr;
    Display* display_ = nullptr;
    esp_timer_handle_t voice_motion_stop_timer_ = nullptr;
    esp_timer_handle_t qr_hide_timer_ = nullptr;
    lv_obj_t* qr_popup_ = nullptr;

    void ShowWebUiQrOnDisplay(const std::string& url);
    // hide_after_s = 0: khong tu an.
    void ShowQrPopup(const std::string& payload, const char* label, const char* hint, int hide_after_s);
    void HideWebUiQrOnDisplay();
    static void QrHideTimerCallback(void* arg);

    static void VoiceMotionStopTimerCallback(void* arg);
    void ArmVoiceMotionStopTimer(int duration_ms);
};

#endif // BREAD_COMPACT_WIFI_ROBOT_CONTROL_HUB_H
