#include "robot_web_ui_server.h"

#include "application.h"
#include "board.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

#define TAG "RobotWebUI"

namespace {

constexpr const char* kWebUiHtml = R"HTML(
<!doctype html>
<html lang="vi">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Xiaozhi Robot Dashboard</title>
  <style>
    :root {
      --bg: #090d16;
      --card-bg: rgba(20, 29, 51, 0.7);
      --accent: #00d2ff;
      --accent-glow: rgba(0, 210, 255, 0.3);
      --ok: #00e676;
      --danger: #ff5252;
      --text: #e0e6ed;
      --text-dim: #8a99ad;
      --border: #1e2c4d;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Segoe UI', system-ui, sans-serif; }
    body { background: var(--bg); color: var(--text); min-height: 100vh; padding: 15px; }

    /* Top Bar Status */
    header {
      display: flex; justify-content: space-between; align-items: center;
      background: var(--card-bg); border: 1px solid var(--border);
      padding: 12px 20px; border-radius: 12px; backdrop-filter: blur(10px); margin-bottom: 15px;
    }
    .brand { font-size: 18px; font-weight: 700; color: var(--accent); display: flex; align-items: center; gap: 8px; }
    .status-badges { display: flex; gap: 10px; flex-wrap: wrap; }
    .badge { background: #111a2e; border: 1px solid var(--border); padding: 4px 10px; border-radius: 20px; font-size: 12px; color: var(--text-dim); }
    .badge span { color: var(--accent); font-weight: bold; }

    /* Main Grid Layout */
    .dashboard-grid {
      display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 15px;
    }
    .card {
      background: var(--card-bg); border: 1px solid var(--border); border-radius: 14px;
      padding: 16px; backdrop-filter: blur(8px);
    }
    .card-title { font-size: 15px; font-weight: 600; margin-bottom: 12px; color: var(--accent); display: flex; justify-content: space-between; }

    /* D-Pad Controls */
    .dpad-container { display: grid; grid-template-columns: repeat(3, 60px); gap: 8px; justify-content: center; margin: 15px 0; }
    .btn-cmd {
      background: #182544; border: 1px solid var(--border); color: var(--text);
      border-radius: 10px; padding: 12px; font-weight: bold; cursor: pointer; transition: 0.15s;
    }
    .btn-cmd:active { background: var(--accent); color: #000; transform: scale(0.95); }
    .btn-cmd.stop { background: rgba(255, 82, 82, 0.2); border-color: var(--danger); color: var(--danger); }

    /* Quick Action Buttons */
    .btn-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
    .btn-action { background: #16223d; border: 1px solid var(--border); color: var(--text); padding: 10px; border-radius: 8px; cursor: pointer; font-size: 13px; }
    .btn-action:hover { border-color: var(--accent); }

    /* Range Sliders */
    .control-group { margin-bottom: 12px; }
    .control-label { display: flex; justify-content: space-between; font-size: 13px; color: var(--text-dim); margin-bottom: 4px; }
    input[type=range] { width: 100%; accent-color: var(--accent); background: #111a2e; height: 6px; border-radius: 3px; }
    input[type=text] { width: 100%; background: #111a2e; border: 1px solid var(--border); color: var(--text); padding: 8px 12px; border-radius: 8px; margin-bottom: 8px; }

    /* Status Footer Log */
    .log-box { font-family: monospace; font-size: 11px; color: var(--text-dim); background: #05080f; border-radius: 6px; padding: 8px; height: 40px; overflow: hidden; margin-top: 10px; }
  </style>
</head>
<body>

  <header>
    <div class="brand">🤖 Xiaozhi UI</div>
    <div class="status-badges">
      <div class="badge">Bánh xe: <span id="stWheel">--</span></div>
      <div class="badge">Tốc độ L/R: <span id="stSpeed">0/0</span></div>
      <div class="badge">Âm lượng: <span id="stVol">0%</span></div>
    </div>
  </header>

  <div class="dashboard-grid">
    <!-- Card 1: Điều khiển chuyển động -->
    <div class="card">
      <div class="card-title">🎮 Điều khiển Bánh Xe</div>
      <div class="dpad-container">
        <div></div>
        <button class="btn-cmd" onclick="sendCtrl('forward')">▲</button>
        <div></div>
        <button class="btn-cmd" onclick="sendCtrl('left')">◀</button>
        <button class="btn-cmd stop" onclick="sendCtrl('stop')">■</button>
        <button class="btn-cmd" onclick="sendCtrl('right')">▶</button>
        <div></div>
        <button class="btn-cmd" onclick="sendCtrl('backward')">▼</button>
        <div></div>
      </div>
    </div>

    <!-- Card 2: Hành động Cổ/Đầu -->
    <div class="card">
      <div class="card-title">🗣️ Hành động Servo Cổ</div>
      <div class="btn-grid">
        <button class="btn-action" onclick="sendCtrl('head_center')">🎯 Cân bằng (Center)</button>
        <button class="btn-action" onclick="sendCtrl('head_nod')">👍 Gật đầu (Nod)</button>
        <button class="btn-action" onclick="sendCtrl('head_shake')">👎 Lắc đầu (Shake)</button>
        <button class="btn-action" onclick="sendCtrl('head_curious')">🤔 Tò mò (Curious)</button>
      </div>
    </div>

    <!-- Card 3: Trình phát nhạc -->
    <div class="card">
      <div class="card-title">🎵 Âm nhạc & SD Card</div>
      <div class="btn-grid" style="margin-bottom: 10px;">
        <button class="btn-action" onclick="sendMusic('play_mood', {mood:'vuive'})">😊 Nhạc vui</button>
        <button class="btn-action" onclick="sendMusic('play_mood', {mood:'buon'})">😢 Nhạc buồn</button>
        <button class="btn-action" onclick="sendMusic('next')">⏭️ Bài tiếp</button>
        <button class="btn-action" onclick="sendMusic('stop')">⏹️ Dừng nhạc</button>
      </div>
      <input type="text" id="musicQuery" placeholder="Nhập tên bài hát..." />
      <button class="btn-action" style="width: 100%; background: var(--accent); color: #000; font-weight: bold;" onclick="searchMusic()">🔍 Tìm & Phát</button>
    </div>

    <!-- Card 4: Cài đặt thông số -->
    <div class="card">
      <div class="card-title">⚙️ Cấu hình Hệ thống</div>
      <div class="control-group">
        <div class="control-label">Tốc độ tiến: <span id="vMove">60</span></div>
        <input type="range" id="moveSpeed" min="0" max="100" value="60" oninput="syncVal('vMove', this.value)">
      </div>
      <div class="control-group">
        <div class="control-label">Tốc độ xoay: <span id="vTurn">55</span></div>
        <input type="range" id="turnSpeed" min="0" max="100" value="55" oninput="syncVal('vTurn', this.value)">
      </div>
      <div class="control-group">
        <div class="control-label">Bước di chuyển (ms): <span id="vStep">350</span></div>
        <input type="range" id="stepMs" min="80" max="2000" value="350" oninput="syncVal('vStep', this.value)">
      </div>
      <div class="control-group">
        <div class="control-label">Âm lượng loa: <span id="vVol">60</span></div>
        <input type="range" id="volume" min="0" max="100" value="60" oninput="syncVal('vVol', this.value)">
      </div>
      <button class="btn-action" style="width: 100%; background: var(--ok); color: #000; font-weight: bold;" onclick="saveSettings()">💾 Lưu Cài Đặt</button>
    </div>
  </div>

  <div class="log-box" id="logBox">System ready.</div>

  <script>
    const $ = id => document.getElementById(id);
    const log = msg => { $('logBox').innerText = `[${new Date().toLocaleTimeString()}] ${msg}`; };

    function syncVal(labelId, val) { $(labelId).innerText = val; }

    async function apiGet(url) {
      try { const r = await fetch(url); return await r.json(); } 
      catch(e) { log('Lỗi kết nối API'); }
    }

    async function apiPost(url, body) {
      try {
        const r = await fetch(url, { method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify(body) });
        const res = await r.json();
        log(res.message || 'Thành công');
        return res;
      } catch(e) { log('Lỗi gửi lệnh'); }
    }

    async function refreshStatus() {
      const st = await apiGet('/api/status');
      if(st) {
        $('stWheel').innerText = st.wheel_ready ? 'Sẵn sàng' : 'Chưa kết nối';
        $('stSpeed').innerText = `${st.left_speed}/${st.right_speed}`;
        $('stVol').innerText = `${st.volume}%`;
      }
    }

    async function sendCtrl(action) {
      const ms = Number($('stepMs').value);
      const isMove = ['forward','backward','left','right'].includes(action);
      await apiPost('/api/control', isMove ? {action, duration_ms: ms} : {action});
      setTimeout(refreshStatus, 150);
    }

    async function sendMusic(action, extra = {}) {
      await apiPost('/api/music', {action, ...extra});
    }

    function searchMusic() {
      const q = $('musicQuery').value.trim();
      if(q) sendMusic('search', {query: q});
    }

    async function saveSettings() {
      await apiPost('/api/settings', {
        move_speed: Number($('moveSpeed').value),
        turn_speed: Number($('turnSpeed').value),
        step_ms: Number($('stepMs').value),
        volume: Number($('volume').value)
      });
      refreshStatus();
    }

    async function init() {
      const s = await apiGet('/api/settings');
      if(s) {
        $('moveSpeed').value = s.move_speed; syncVal('vMove', s.move_speed);$('turnSpeed').value = s.turn_speed; syncVal('vTurn', s.turn_speed);
        $('stepMs').value = s.step_ms; syncVal('vStep', s.step_ms);$('volume').value = s.volume; syncVal('vVol', s.volume);
      }
      refreshStatus();
      setInterval(refreshStatus, 3000);
    }

    init();
  </script>
</body>
</html>
)HTML";

}  // namespace

RobotWebUiServer::RobotWebUiServer(DualDcMotorController* wheel_motor, DualServoController* head_servo)
    : wheel_motor_(wheel_motor), head_servo_(head_servo) {
    LoadConfig();

  esp_timer_create_args_t timer_args = {
    .callback = &RobotWebUiServer::MotionStopTimerCallback,
    .arg = this,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "webui_motion_stop",
    .skip_unhandled_events = true,
  };
  auto err = esp_timer_create(&timer_args, &motion_stop_timer_);
  if (err != ESP_OK) {
    motion_stop_timer_ = nullptr;
    ESP_LOGW(TAG, "Failed to create motion stop timer: %s", esp_err_to_name(err));
  }
}

RobotWebUiServer::~RobotWebUiServer() {
  if (motion_stop_timer_ != nullptr) {
    esp_timer_stop(motion_stop_timer_);
    esp_timer_delete(motion_stop_timer_);
    motion_stop_timer_ = nullptr;
  }
    Stop();
}

void RobotWebUiServer::MotionStopTimerCallback(void* arg) {
  auto* self = static_cast<RobotWebUiServer*>(arg);
  if (self == nullptr || self->wheel_motor_ == nullptr) {
    return;
  }
  self->wheel_motor_->Stop();
}

void RobotWebUiServer::ArmMotionStopTimer(int duration_ms) {
  if (motion_stop_timer_ == nullptr || wheel_motor_ == nullptr) {
    return;
  }

  esp_timer_stop(motion_stop_timer_);
  int clamped_ms = Clamp(duration_ms, 80, 5000);
  esp_timer_start_once(motion_stop_timer_, static_cast<uint64_t>(clamped_ms) * 1000ULL);
}

bool RobotWebUiServer::Start(int port) {
    if (server_handle_ != nullptr) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_open_sockets = 7;

    if (httpd_start(&server_handle_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web UI server on port %d", port);
        return false;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = &RobotWebUiServer::IndexHandler,
        .user_ctx = this,
    };

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = &RobotWebUiServer::ApiStatusHandler,
        .user_ctx = this,
    };

    httpd_uri_t settings_get_uri = {
        .uri = "/api/settings",
        .method = HTTP_GET,
        .handler = &RobotWebUiServer::ApiSettingsGetHandler,
        .user_ctx = this,
    };

    httpd_uri_t settings_post_uri = {
        .uri = "/api/settings",
        .method = HTTP_POST,
        .handler = &RobotWebUiServer::ApiSettingsPostHandler,
        .user_ctx = this,
    };

    httpd_uri_t control_uri = {
        .uri = "/api/control",
        .method = HTTP_POST,
        .handler = &RobotWebUiServer::ApiControlHandler,
        .user_ctx = this,
    };

    httpd_uri_t music_uri = {
        .uri = "/api/music",
        .method = HTTP_POST,
        .handler = &RobotWebUiServer::ApiMusicHandler,
        .user_ctx = this,
    };

    httpd_register_uri_handler(server_handle_, &index_uri);
    httpd_register_uri_handler(server_handle_, &status_uri);
    httpd_register_uri_handler(server_handle_, &settings_get_uri);
    httpd_register_uri_handler(server_handle_, &settings_post_uri);
    httpd_register_uri_handler(server_handle_, &control_uri);
    httpd_register_uri_handler(server_handle_, &music_uri);

    ESP_LOGI(TAG, "Robot Web UI started on http://<device-ip>:%d/", port);
    return true;
}

void RobotWebUiServer::Stop() {
    if (server_handle_ != nullptr) {
        httpd_stop(server_handle_);
        server_handle_ = nullptr;
    }
}

void RobotWebUiServer::LoadConfig() {
    Settings settings("robot_webui", false);
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.move_speed = Clamp(settings.GetInt("move_speed", 60), 0, 100);
    config_.turn_speed = Clamp(settings.GetInt("turn_speed", 55), 0, 100);
    config_.step_ms = Clamp(settings.GetInt("step_ms", 350), 80, 5000);
}

void RobotWebUiServer::SaveConfig() {
    Settings settings("robot_webui", true);
    std::lock_guard<std::mutex> lock(config_mutex_);
    settings.SetInt("move_speed", config_.move_speed);
    settings.SetInt("turn_speed", config_.turn_speed);
    settings.SetInt("step_ms", config_.step_ms);
}

esp_err_t RobotWebUiServer::IndexHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, kWebUiHtml, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t RobotWebUiServer::ApiStatusHandler(httpd_req_t* req) {
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleStatus(req);
}

esp_err_t RobotWebUiServer::ApiSettingsGetHandler(httpd_req_t* req) {
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleSettingsGet(req);
}

esp_err_t RobotWebUiServer::ApiSettingsPostHandler(httpd_req_t* req) {
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleSettingsPost(req);
}

esp_err_t RobotWebUiServer::ApiControlHandler(httpd_req_t* req) {
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleControl(req);
}

esp_err_t RobotWebUiServer::ApiMusicHandler(httpd_req_t* req) {
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleMusic(req);
}

esp_err_t RobotWebUiServer::HandleStatus(httpd_req_t* req) {
    int left_speed = 0;
    int right_speed = 0;
    if (wheel_motor_ != nullptr) {
        wheel_motor_->GetSpeed(left_speed, right_speed);
    }

    int move_speed;
    int turn_speed;
    int step_ms;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        move_speed = config_.move_speed;
        turn_speed = config_.turn_speed;
        step_ms = config_.step_ms;
    }

    int volume = 0;
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr) {
        volume = codec->output_volume();
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "wheel_ready", wheel_motor_ != nullptr && wheel_motor_->IsReady());
    cJSON_AddNumberToObject(root, "left_speed", left_speed);
    cJSON_AddNumberToObject(root, "right_speed", right_speed);
    cJSON_AddNumberToObject(root, "move_speed", move_speed);
    cJSON_AddNumberToObject(root, "turn_speed", turn_speed);
    cJSON_AddNumberToObject(root, "step_ms", step_ms);
    cJSON_AddNumberToObject(root, "volume", volume);

    char* json_text = cJSON_PrintUnformatted(root);
    std::string payload = json_text ? json_text : "{}";
    if (json_text != nullptr) {
        cJSON_free(json_text);
    }
    cJSON_Delete(root);

    SendJson(req, 200, payload);
    return ESP_OK;
}

esp_err_t RobotWebUiServer::HandleSettingsGet(httpd_req_t* req) {
    int move_speed;
    int turn_speed;
    int step_ms;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        move_speed = config_.move_speed;
        turn_speed = config_.turn_speed;
        step_ms = config_.step_ms;
    }

    int volume = 0;
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr) {
        volume = codec->output_volume();
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "move_speed", move_speed);
    cJSON_AddNumberToObject(root, "turn_speed", turn_speed);
    cJSON_AddNumberToObject(root, "step_ms", step_ms);
    cJSON_AddNumberToObject(root, "volume", volume);

    char* json_text = cJSON_PrintUnformatted(root);
    std::string payload = json_text ? json_text : "{}";
    if (json_text != nullptr) {
        cJSON_free(json_text);
    }
    cJSON_Delete(root);

    SendJson(req, 200, payload);
    return ESP_OK;
}

esp_err_t RobotWebUiServer::HandleSettingsPost(httpd_req_t* req) {
    std::string body;
    if (!ReadRequestBody(req, body)) {
        SendJson(req, 400, "{\"ok\":false,\"message\":\"Invalid request body\"}");
        return ESP_OK;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) {
            cJSON_Delete(root);
        }
        SendJson(req, 400, "{\"ok\":false,\"message\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    {
        std::lock_guard<std::mutex> lock(config_mutex_);

        auto* move_speed = cJSON_GetObjectItem(root, "move_speed");
        if (cJSON_IsNumber(move_speed)) {
            config_.move_speed = Clamp(move_speed->valueint, 0, 100);
        }

        auto* turn_speed = cJSON_GetObjectItem(root, "turn_speed");
        if (cJSON_IsNumber(turn_speed)) {
            config_.turn_speed = Clamp(turn_speed->valueint, 0, 100);
        }

        auto* step_ms = cJSON_GetObjectItem(root, "step_ms");
        if (cJSON_IsNumber(step_ms)) {
            config_.step_ms = Clamp(step_ms->valueint, 80, 5000);
        }
    }

    auto* volume = cJSON_GetObjectItem(root, "volume");
    if (cJSON_IsNumber(volume)) {
        auto codec = Board::GetInstance().GetAudioCodec();
        if (codec != nullptr) {
            codec->SetOutputVolume(Clamp(volume->valueint, 0, 100));
        }
    }

    cJSON_Delete(root);
    SaveConfig();

    SendJson(req, 200, "{\"ok\":true,\"message\":\"Saved settings\"}");
    return ESP_OK;
}

esp_err_t RobotWebUiServer::HandleControl(httpd_req_t* req) {
    std::string body;
    if (!ReadRequestBody(req, body)) {
        SendJson(req, 400, "{\"ok\":false,\"message\":\"Invalid request body\"}");
        return ESP_OK;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) {
            cJSON_Delete(root);
        }
        SendJson(req, 400, "{\"ok\":false,\"message\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    auto* action_json = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action_json) || action_json->valuestring == nullptr) {
        cJSON_Delete(root);
        SendJson(req, 400, "{\"ok\":false,\"message\":\"Missing action\"}");
        return ESP_OK;
    }

    std::string action = action_json->valuestring;

    int move_speed;
    int turn_speed;
    int step_ms;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        move_speed = config_.move_speed;
        turn_speed = config_.turn_speed;
        step_ms = config_.step_ms;
    }

    auto* duration_json = cJSON_GetObjectItem(root, "duration_ms");
    int duration_ms = cJSON_IsNumber(duration_json) ? Clamp(duration_json->valueint, 80, 5000) : step_ms;

    bool handled = false;

    if (wheel_motor_ != nullptr) {
        if (action == "forward") {
            wheel_motor_->Forward(move_speed);
            handled = true;
        } else if (action == "backward") {
            wheel_motor_->Backward(move_speed);
            handled = true;
        } else if (action == "left") {
            wheel_motor_->TurnLeft(turn_speed);
            handled = true;
        } else if (action == "right") {
            wheel_motor_->TurnRight(turn_speed);
            handled = true;
        } else if (action == "stop") {
            wheel_motor_->Stop();
            handled = true;
        }
    }

    if (!handled && head_servo_ != nullptr) {
        if (action == "head_nod") {
            handled = head_servo_->nodYes();
        } else if (action == "head_shake") {
            handled = head_servo_->shakeNo();
        } else if (action == "head_curious") {
            handled = head_servo_->curiousHeadTilt();
        } else if (action == "head_center") {
            handled = head_servo_->RunAction("center");
        }
    }

    if (handled && wheel_motor_ != nullptr &&
        (action == "forward" || action == "backward" || action == "left" || action == "right")) {
      ArmMotionStopTimer(duration_ms);
    }

    cJSON_Delete(root);

    if (!handled) {
        SendJson(req, 400, "{\"ok\":false,\"message\":\"Unknown action or unavailable controller\"}");
        return ESP_OK;
    }

    SendJson(req, 200, "{\"ok\":true,\"message\":\"Command accepted\"}");
    return ESP_OK;
}

esp_err_t RobotWebUiServer::HandleMusic(httpd_req_t* req) {
    std::string body;
    if (!ReadRequestBody(req, body)) {
        SendJson(req, 400, "{\"ok\":false,\"message\":\"Invalid request body\"}");
        return ESP_OK;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr || !cJSON_IsObject(root)) {
        if (root != nullptr) {
            cJSON_Delete(root);
        }
        SendJson(req, 400, "{\"ok\":false,\"message\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    auto* action_json = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action_json) || action_json->valuestring == nullptr) {
        cJSON_Delete(root);
        SendJson(req, 400, "{\"ok\":false,\"message\":\"Missing action\"}");
        return ESP_OK;
    }

    std::string action = action_json->valuestring;
    std::string message = "OK";
    bool ok = false;

    if (action == "play_mood") {
        auto* mood_json = cJSON_GetObjectItem(root, "mood");
        std::string mood = cJSON_IsString(mood_json) && mood_json->valuestring != nullptr ? mood_json->valuestring : "vuive";
        ok = Application::GetInstance().PlaySdCardMusicByMood(mood);
        message = ok ? "Playing mood music" : "Play mood failed";
    } else if (action == "search") {
        auto* query_json = cJSON_GetObjectItem(root, "query");
        std::string query = cJSON_IsString(query_json) && query_json->valuestring != nullptr ? query_json->valuestring : "";
        ok = !query.empty() && Application::GetInstance().PlaySdCardMusicByQuery(query);
        message = ok ? "Playing matched song" : "Not found / failed";
    } else if (action == "stop") {
        ok = Application::GetInstance().StopSdCardMusic();
        message = ok ? "Stopped" : "Stop failed";
    } else if (action == "next") {
        ok = Application::GetInstance().PlayNextSdCardMusic();
        message = ok ? "Next song" : "Next failed";
    }

    cJSON_Delete(root);

    if (!ok) {
        SendJson(req, 400, std::string("{\"ok\":false,\"message\":\"") + message + "\"}");
        return ESP_OK;
    }

    SendJson(req, 200, std::string("{\"ok\":true,\"message\":\"") + message + "\"}");
    return ESP_OK;
}

bool RobotWebUiServer::ReadRequestBody(httpd_req_t* req, std::string& out_body, size_t max_len) {
    out_body.clear();
    if (req->content_len <= 0 || static_cast<size_t>(req->content_len) > max_len) {
        return false;
    }

    out_body.resize(static_cast<size_t>(req->content_len));
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, out_body.data() + received, req->content_len - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            out_body.clear();
            return false;
        }
        received += ret;
    }
    return true;
}

void RobotWebUiServer::SendJson(httpd_req_t* req, int status_code, const std::string& json) {
    if (status_code >= 400) {
        httpd_resp_set_status(req, "400 Bad Request");
    } else {
        httpd_resp_set_status(req, "200 OK");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, json.c_str(), HTTPD_RESP_USE_STRLEN);
}

int RobotWebUiServer::Clamp(int value, int min_value, int max_value) {
    return std::max(min_value, std::min(max_value, value));
}
