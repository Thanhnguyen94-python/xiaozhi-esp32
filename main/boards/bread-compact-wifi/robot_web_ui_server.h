#ifndef ROBOT_WEB_UI_SERVER_H
#define ROBOT_WEB_UI_SERVER_H

#include "dual_dc_motor_controller.h"
#include "dual_servo_controller.h"

#include <esp_http_server.h>
#include <esp_timer.h>

#include <mutex>
#include <string>

class RobotWebUiServer {
public:
    RobotWebUiServer(DualDcMotorController* wheel_motor, DualServoController* head_servo);
    ~RobotWebUiServer();

    bool Start(int port = 80);
    void Stop();

private:
    struct MotionConfig {
        int move_speed = 60;
        int turn_speed = 55;
        int step_ms = 350;
    };

    httpd_handle_t server_handle_ = nullptr;
    DualDcMotorController* wheel_motor_ = nullptr;
    DualServoController* head_servo_ = nullptr;

    MotionConfig config_;
    std::mutex config_mutex_;
    esp_timer_handle_t motion_stop_timer_ = nullptr;

    void LoadConfig();
    void SaveConfig();
    void ArmMotionStopTimer(int duration_ms);
    static void MotionStopTimerCallback(void* arg);

    static esp_err_t IndexHandler(httpd_req_t* req);
    static esp_err_t ApiStatusHandler(httpd_req_t* req);
    static esp_err_t ApiSettingsGetHandler(httpd_req_t* req);
    static esp_err_t ApiSettingsPostHandler(httpd_req_t* req);
    static esp_err_t ApiControlHandler(httpd_req_t* req);
    static esp_err_t ApiMusicHandler(httpd_req_t* req);

    esp_err_t HandleStatus(httpd_req_t* req);
    esp_err_t HandleSettingsGet(httpd_req_t* req);
    esp_err_t HandleSettingsPost(httpd_req_t* req);
    esp_err_t HandleControl(httpd_req_t* req);
    esp_err_t HandleMusic(httpd_req_t* req);

    static bool ReadRequestBody(httpd_req_t* req, std::string& out_body, size_t max_len = 2048);
    static void SendJson(httpd_req_t* req, int status_code, const std::string& json);
    static int Clamp(int value, int min_value, int max_value);
};

#endif // ROBOT_WEB_UI_SERVER_H
