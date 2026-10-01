#include "robot_control_hub.h"

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

void RobotControlHub::ShowWebUiAccessOnDisplay() {
    auto [url, hint] = GetWebUiAccessInfo();
    if (!url.empty()) {
        ESP_LOGI(TAG, "WebUI access: %s", url.c_str());
    }

    if (display_ != nullptr) {
        display_->ShowNotification(hint);
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

    mcp_server.AddTool("self.robot.control",
        "Alias of self.robot.move. Use this for robot control by voice.",
        PropertyList({
            Property("action", kPropertyTypeString),
            Property("speed", kPropertyTypeInteger, 60, 0, 100),
            Property("duration_ms", kPropertyTypeInteger, 350, 80, 5000)
        }),
        robot_move_tool);

    auto get_webui_access_info = [this](const PropertyList&) -> ReturnValue {
        auto [url, hint] = GetWebUiAccessInfo();

        if (display_ != nullptr) {
            display_->ShowNotification(hint);
        }

        if (url.empty()) {
            return std::string("WebUI chua san sang. Hay cho robot ket noi Wi-Fi roi hoi lai.");
        }

        return std::string("Dia chi WebUI la ") + url +
               " Minh da hien dia chi nay tren man hinh OLED.";
    };

    mcp_server.AddTool("self.webui.get_access_info",
        "Get local WebUI access URL and show it on OLED screen. "
        "Use when user asks: web UI address, robot IP, login link, dia chi WebUI, IP WebUI, link dang nhap webUI, cach mo trang dieu khien local.",
        PropertyList(),
        get_webui_access_info);

    mcp_server.AddTool("self.webui.get_ip",
        "Get the local IP/login URL for robot WebUI and display it on OLED.",
        PropertyList(),
        get_webui_access_info);

    mcp_server.AddTool("self.webui.get_login_link",
        "Get WebUI login link for local robot control and show on OLED.",
        PropertyList(),
        get_webui_access_info);
}
