#ifndef ROBOT_WEB_UI_SERVER_H
#define ROBOT_WEB_UI_SERVER_H

#include "dual_dc_motor_controller.h"
#include "dual_servo_controller.h"

#include <esp_http_server.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>  
#include <freertos/task.h>  
#include <mutex>
#include <string>
#include <vector>
#include <cJSON.h>

class RobotWebUiServer {
public:
    RobotWebUiServer(DualDcMotorController* wheel_motor, DualServoController* head_servo);
    ~RobotWebUiServer();

    bool Start(int port = 80);
    void Stop();
    void BroadcastJson(const char* json);
    void BroadcastJson(const std::string& json) { BroadcastJson(json.c_str()); }
    size_t GetWsClientCount();

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
    // === PHASE 1: WebSocket state ===
    static constexpr size_t kMaxWsClients  = 8;
    static constexpr size_t kWsHeartbeatMs = 5000;
    static constexpr size_t kWsRxBufSize   = 512;

    std::mutex       ws_mutex_;               // bảo vệ ws_clients_
    std::vector<int> ws_clients_;             // danh sách fd đang kết nối
    TaskHandle_t     ws_heartbeat_task_ = nullptr;

    // Entry points cho httpd
    static esp_err_t WsHandlerEntry(httpd_req_t* req);
    static void WsHeartbeatTaskEntry(void* arg);

    // Implementations
    esp_err_t HandleWs(httpd_req_t* req);
    void HandleWsText(int fd, const uint8_t* data, size_t len);
    void HandleChat(int fd, const char* json_text, size_t len);
    void SendChatReply(int fd, const std::string& text, const std::string& tool);
    void WsHeartbeatLoop();
    static constexpr size_t kStatusPollMs = 500;

    TaskHandle_t status_broadcast_task_ = nullptr;

    // Cache để skip broadcast khi không đổi
    int current_left_speed_  = 0;
    int current_right_speed_ = 0;
    int  last_left_speed_  = -999;
    int  last_right_speed_ = -999;
    int  last_macro_count_ = -1;
    bool last_macro_rec_   = false;
    bool last_macro_play_  = false;
    int  last_volume_      = -999;
    bool last_wheel_ready_ = false;

    static void StatusBroadcastTaskEntry(void* arg);
    void StatusBroadcastLoop();
    void BuildStatusJson(std::string& out, bool include_type);
    static constexpr const char* kMacroDir         = "/sdcard/macros";
    static constexpr size_t      kMacroMaxNameLen  = 32;

    bool SaveMacroToSd(const std::string& name);
    bool LoadMacroFromSd(const std::string& name);
    bool DeleteMacroFromSd(const std::string& name);
    std::vector<std::string> ListMacrosOnSd();

    static bool IsValidMacroName(const std::string& name);
    static std::string MacroFilePath(const std::string& name);

    void HandleMacroSave(int fd, cJSON* root);
    void HandleMacroLoad(int fd, cJSON* root);
    void HandleMacroDelete(int fd, cJSON* root);
    void HandleMacroList(int fd);
    void BroadcastMacroList();
    void HandleVectorCommand(int fd, const char* json_text, size_t len);
    void BroadcastStatusNow();
    void SetSpeedsAndBroadcast(int l, int r);

    // Helpers
    bool WsIsRegistered(int fd);
    void WsAddClient(int fd);
    void WsRemoveClient(int fd);
    void WsSendTextToFd(int fd, const char* text);
    void WsSendTextToReq(httpd_req_t* req, const char* text);

    static esp_err_t IndexHandler(httpd_req_t* req);
    static esp_err_t ApiStatusHandler(httpd_req_t* req);
    static esp_err_t ApiSettingsGetHandler(httpd_req_t* req);
    static esp_err_t ApiSettingsPostHandler(httpd_req_t* req);
    static esp_err_t ApiControlHandler(httpd_req_t* req);
    static esp_err_t ApiMusicHandler(httpd_req_t* req);
    static esp_err_t ApiMacroStatusHandler(httpd_req_t* req);

    esp_err_t HandleStatus(httpd_req_t* req);
    esp_err_t HandleSettingsGet(httpd_req_t* req);
    esp_err_t HandleSettingsPost(httpd_req_t* req);
    esp_err_t HandleControl(httpd_req_t* req);
    esp_err_t HandleMusic(httpd_req_t* req);
    esp_err_t HandleMacroStatus(httpd_req_t* req);

    static bool ReadRequestBody(httpd_req_t* req, std::string& out_body, size_t max_len = 2048);
    static void SendJson(httpd_req_t* req, int status_code, const std::string& json);
    static int Clamp(int value, int min_value, int max_value);
    public:   // tạm để HTML handler gọi được — hoặc để private nếu chỉ internal
    struct MacroStep {
        std::string action;      // "forward"|"backward"|"left"|"right"|"stop"
                                 // "head_nod"|"head_shake"|"head_curious"|"head_center"
                                 // "vector"|"music_vuive"|"music_buon"|"music_stop"|"music_next"
                                 // "chat"
        int         p1 = 0;      // speed | vx*100 | 0
        int         p2 = 0;      // duration_ms | vy*100 | 0
        uint32_t    delay_ms = 0;
        std::string text;        // cho chat
    };

    private:
        static constexpr size_t kMacroMaxSteps = 100;
        static constexpr uint32_t kMacroMaxDurationMs = 60000;

        std::mutex           macro_mutex_;
        std::vector<MacroStep> recorded_macro_;
        bool                 macro_recording_ = false;
        bool                 macro_playing_   = false;
        bool                 macro_abort_     = false;
        uint32_t             macro_start_ms_  = 0;
        uint32_t             macro_last_step_ms_ = 0;
        TaskHandle_t         macro_player_task_ = nullptr;

        // Internal
        void HandleMacroCommand(int fd, const char* json_text, size_t len);
        void RecordMacroStep(const char* action, int p1, int p2,
                            uint32_t delay_ms, const char* text = nullptr);
        void MacroPlayerTaskEntryWrapper();
        static void MacroPlayerTaskEntry(void* arg);
        void ExecuteMacroStep(const MacroStep& step);
        void BroadcastMacroStatus();
};

#endif // ROBOT_WEB_UI_SERVER_H
