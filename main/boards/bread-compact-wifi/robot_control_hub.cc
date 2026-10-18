#include "robot_control_hub.h"

#include "application.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <wifi_manager.h>

#include <algorithm>

#define TAG "RobotControlHub"

RobotControlHub::RobotControlHub(DualDcMotorController* wheel_motor, DualServoController* head_servo, Display* display)
    : wheel_motor_(wheel_motor), head_servo_(head_servo), display_(display) {
    esp_timer_create_args_t timer_args = {
        .callback = &RobotControlHub::VoiceMotionStopTimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "voice_motion_stop",
        .skip_unhandled_events = true,
    };

    auto err = esp_timer_create(&timer_args, &voice_motion_stop_timer_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Khong tao duoc timer stop: %s", esp_err_to_name(err));
        voice_motion_stop_timer_ = nullptr;
    }
}

RobotControlHub::~RobotControlHub() {
    if (voice_motion_stop_timer_ != nullptr) {
        esp_timer_stop(voice_motion_stop_timer_);
        esp_timer_delete(voice_motion_stop_timer_);
        voice_motion_stop_timer_ = nullptr;
    }
}

void RobotControlHub::VoiceMotionStopTimerCallback(void* arg) {
    auto* self = static_cast<RobotControlHub*>(arg);
    if (self == nullptr || self->wheel_motor_ == nullptr) {
        return;
    }
    self->wheel_motor_->Stop();
}

void RobotControlHub::ArmVoiceMotionStopTimer(int duration_ms) {
    if (voice_motion_stop_timer_ == nullptr || wheel_motor_ == nullptr) {
        return;
    }

    esp_timer_stop(voice_motion_stop_timer_);
    int clamped_ms = std::clamp(duration_ms, 80, 5000);
    esp_timer_start_once(voice_motion_stop_timer_, static_cast<uint64_t>(clamped_ms) * 1000ULL);
}

std::pair<std::string, std::string> RobotControlHub::GetWebUiAccessInfo() {
    auto& wifi = WifiManager::GetInstance();

    std::string url;
    std::string hint;

    if (wifi.IsConnected()) {
        std::string ip = wifi.GetIpAddress();
        if (!ip.empty()) {
            url = "http://" + ip + "/";
            hint = "WebUI (same Wi-Fi): " + url;
        }
    }

    if (url.empty() && wifi.IsConfigMode()) {
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
    }

    if (url.empty()) {
        hint = "WebUI chua san sang. Hay doi robot ket noi Wi-Fi.";
    }

    return {url, hint};
}

void RobotControlHub::HideQrOnDisplay() {
    if (display_ != nullptr) {
        display_->HideQRCode();
    }
}

void RobotControlHub::ShowWebUiQrOnDisplay(const std::string& url) {
    // QR do Display ve (chi QR, khong chu): tu an sau kWebUiQrDurationMs.
    if (display_ != nullptr) {
        display_->ShowQRCode(url.c_str(), kWebUiQrDurationMs);
    }
}

void RobotControlHub::ShowWifiSetupQrOnDisplay() {
    std::string ssid = WifiManager::GetInstance().GetApSsid();
    if (ssid.empty() || display_ == nullptr) {
        return;
    }
    // Chuan QR Wi-Fi: dien thoai quet bang camera la vao diem phat (mang mo, khong mat khau),
    // sau do trang cau hinh tu bat len (captive portal).
    std::string escaped;
    for (char c : ssid) {
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
            escaped += '\\';
        }
        escaped += c;
    }
    std::string payload = "WIFI:T:nopass;S:" + escaped + ";;";
    display_->ShowQRCode(payload.c_str(), 0);  // giu toi khi HideQrOnDisplay()
}

void RobotControlHub::ShowWebUiAccessOnDisplay() {
    auto [url, hint] = GetWebUiAccessInfo();
    if (!url.empty()) {
        ESP_LOGI(TAG, "WebUI access: %s", url.c_str());
        ShowWebUiQrOnDisplay(url);
    }

    if (display_ != nullptr) {
        if (url.empty()) {
            display_->ShowNotification(hint);
        } else {
            display_->ShowNotification("Da hien QR WebUI tren OLED", 2500);
        }
    }
}

void RobotControlHub::RegisterTools() {
    auto& mcp_server = McpServer::GetInstance();

    auto robot_move_tool = [this](const PropertyList& properties) -> ReturnValue {
        if (wheel_motor_ == nullptr) {
            return std::string("Wheel motor controller unavailable");
        }

        auto action = properties["action"].value<std::string>();
        int speed = properties["speed"].value<int>();
        int duration_ms = properties["duration_ms"].value<int>();

        speed = std::clamp(speed, 0, 100);
        duration_ms = std::clamp(duration_ms, 80, 5000);

        bool handled = false;
        if (action == "forward") {
            wheel_motor_->Forward(speed);
            ArmVoiceMotionStopTimer(duration_ms);
            handled = true;
        } else if (action == "backward") {
            wheel_motor_->Backward(speed);
            ArmVoiceMotionStopTimer(duration_ms);
            handled = true;
        } else if (action == "left") {
            wheel_motor_->TurnLeft(speed);
            ArmVoiceMotionStopTimer(duration_ms);
            handled = true;
        } else if (action == "right") {
            wheel_motor_->TurnRight(speed);
            ArmVoiceMotionStopTimer(duration_ms);
            handled = true;
        } else if (action == "stop") {
            wheel_motor_->Stop();
            if (voice_motion_stop_timer_ != nullptr) {
                esp_timer_stop(voice_motion_stop_timer_);
            }
            handled = true;
        } else if (head_servo_ != nullptr && action == "head_nod") {
            handled = head_servo_->nodYes();
        } else if (head_servo_ != nullptr && action == "head_shake") {
            handled = head_servo_->shakeNo();
        } else if (head_servo_ != nullptr && action == "head_curious") {
            handled = head_servo_->curiousHeadTilt();
        } else if (head_servo_ != nullptr && action == "head_center") {
            handled = head_servo_->RunAction("center");
        }

        if (!handled) {
            return std::string("Unsupported action: ") + action;
        }

        ESP_LOGI(TAG, "Robot move tool action=%s speed=%d duration=%d", action.c_str(), speed, duration_ms);
        return std::string("OK");
    };

    mcp_server.AddTool("self.robot.move",
        "High-level robot control for wheels/head. Always prefer this tool for movement commands from voice/server. "
        "Supported action: forward, backward, left, right, stop, head_nod, head_shake, head_curious, head_center. "
        "`speed` range 0..100 (default 60). `duration_ms` range 80..5000 (default 350).",
        PropertyList({
            Property("action", kPropertyTypeString),
            Property("speed", kPropertyTypeInteger, 60, 0, 100),
            Property("duration_ms", kPropertyTypeInteger, 350, 80, 5000)
        }),
        robot_move_tool);

    // Server xiaozhi gioi han 32 tool: khong dang ky alias trung lap (robot.control, webui.get_ip...).

    auto get_webui_access_info = [this](const PropertyList&) -> ReturnValue {
        auto [url, hint] = GetWebUiAccessInfo();

        if (display_ != nullptr) {
            if (!url.empty()) {
                ShowWebUiQrOnDisplay(url);
                display_->ShowNotification("Da hien QR WebUI tren OLED", 2500);
            } else {
                display_->ShowNotification(hint);
            }
        }

        if (url.empty()) {
            return std::string("WebUI chua san sang. Hay cho robot ket noi Wi-Fi roi hoi lai.");
        }

        return std::string("Dia chi WebUI la ") + url +
               " Minh da hien ma QR tren man hinh OLED de ban quet nhanh.";
    };

    mcp_server.AddTool("self.webui.get_access_info",
        "Get local WebUI access URL and show it on OLED screen. "
        "Use when user asks: web UI address, robot IP, login link, dia chi WebUI, IP WebUI, link dang nhap webUI, cach mo trang dieu khien local.",
        PropertyList(),
        get_webui_access_info);

}
