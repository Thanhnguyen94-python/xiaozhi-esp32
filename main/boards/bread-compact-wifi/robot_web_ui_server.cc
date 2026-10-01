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
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>Robot Local UI</title>
  <style>
    :root { --bg:#0b1020; --card:#141b31; --muted:#9db0d8; --fg:#eef3ff; --pri:#4da3ff; --ok:#35c48b; }
    * { box-sizing: border-box; font-family: Inter, system-ui, Arial, sans-serif; }
    body { margin:0; background:linear-gradient(180deg,#091022,#0b1020); color:var(--fg); }
    header { position:sticky; top:0; background:#0b1228ee; backdrop-filter: blur(8px); padding:10px; border-bottom:1px solid #223; }
    .tabs { display:flex; gap:8px; flex-wrap:wrap; }
    .tab { border:none; color:var(--fg); background:#1a2442; border-radius:10px; padding:10px 14px; cursor:pointer; }
    .tab.active { background:var(--pri); color:#001737; font-weight:700; }
    main { padding:14px; max-width:980px; margin:auto; }
    .page { display:none; }
    .page.active { display:block; }
    .card { background:var(--card); border:1px solid #233359; border-radius:14px; padding:14px; margin:10px 0; }
    .row { display:flex; gap:10px; flex-wrap:wrap; align-items:center; }
    .muted { color:var(--muted); font-size:14px; }
    button { border:none; border-radius:10px; padding:10px 12px; cursor:pointer; background:#22335f; color:var(--fg); }
    button.primary { background:var(--pri); color:#001737; font-weight:700; }
    button.ok { background:var(--ok); color:#032616; font-weight:700; }
    input[type=range] { width:220px; }
    input, select { background:#111a33; color:var(--fg); border:1px solid #2f4271; border-radius:8px; padding:8px; }
    .grid3 { display:grid; grid-template-columns:repeat(3, minmax(70px, 90px)); gap:8px; justify-content:center; }
    .dpad button { height:46px; }
    .status { padding:8px 10px; border-radius:9px; background:#101b35; border:1px solid #233865; margin-top:6px; }
  </style>
</head>
<body>
  <header>
    <div class="tabs" id="tabs">
      <button class="tab active" data-page="home">Trang chính</button>
      <button class="tab" data-page="settings">Cài đặt</button>
      <button class="tab" data-page="music">Nhạc</button>
      <button class="tab" data-page="control">Bảng điều khiển robot</button>
    </div>
  </header>

  <main>
    <section class="page active" id="page-home">
      <div class="card">
        <h3>Robot local WebUI</h3>
        <div class="muted">Điều khiển robot trực tiếp trên ESP32-S3, không cần cloud.</div>
        <div class="status" id="homeStatus">Đang tải trạng thái...</div>
      </div>
    </section>

    <section class="page" id="page-settings">
      <div class="card">
        <h3>Cài đặt chuyển động</h3>
        <div class="row">
          <label>Tốc độ tiến/lùi: <span id="moveSpeedVal">60</span></label>
          <input id="moveSpeed" type="range" min="0" max="100" value="60" />
        </div>
        <div class="row">
          <label>Tốc độ xoay: <span id="turnSpeedVal">55</span></label>
          <input id="turnSpeed" type="range" min="0" max="100" value="55" />
        </div>
        <div class="row">
          <label>Bước di chuyển (ms): <span id="stepMsVal">350</span></label>
          <input id="stepMs" type="range" min="80" max="2000" value="350" />
        </div>
        <div class="row">
          <label>Âm lượng: <span id="volumeVal">60</span></label>
          <input id="volume" type="range" min="0" max="100" value="60" />
        </div>
        <button class="ok" id="btnSaveSettings">Lưu cài đặt</button>
        <div class="status" id="settingsStatus"></div>
      </div>
    </section>

    <section class="page" id="page-music">
      <div class="card">
        <h3>Nhạc từ thẻ SD</h3>
        <div class="row">
          <button id="btnPlayHappy">Phát nhạc vui</button>
          <button id="btnPlaySad">Phát nhạc buồn</button>
          <button id="btnNextSong">Bài tiếp theo</button>
          <button id="btnStopSong">Dừng nhạc</button>
        </div>
        <div class="row">
          <input id="musicQuery" placeholder="Tên bài cần tìm" />
          <button id="btnSearchMusic" class="primary">Tìm & phát</button>
        </div>
        <div class="status" id="musicStatus"></div>
      </div>
    </section>

    <section class="page" id="page-control">
      <div class="card dpad">
        <h3>Điều hướng robot</h3>
        <div class="grid3">
          <div></div>
          <button data-act="forward">▲</button>
          <div></div>
          <button data-act="left">◀</button>
          <button data-act="stop" class="primary">■</button>
          <button data-act="right">▶</button>
          <div></div>
          <button data-act="backward">▼</button>
          <div></div>
        </div>
        <div class="muted">Mỗi lần nhấn sẽ đi theo thời gian "Bước di chuyển".</div>
      </div>

      <div class="card">
        <h3>Điều khiển đầu robot</h3>
        <div class="row">
          <button data-act="head_center">Center</button>
          <button data-act="head_nod">Nod</button>
          <button data-act="head_shake">Shake</button>
          <button data-act="head_curious">Curious</button>
        </div>
      </div>

      <div class="status" id="controlStatus"></div>
    </section>
  </main>

  <script>
    const qs = (s) => document.querySelector(s);
    const qsa = (s) => document.querySelectorAll(s);

    function setTab(page) {
      qsa('.tab').forEach(b => b.classList.toggle('active', b.dataset.page === page));
      qsa('.page').forEach(p => p.classList.toggle('active', p.id === `page-${page}`));
    }

    qsa('.tab').forEach(btn => btn.onclick = () => setTab(btn.dataset.page));

    function syncLabels() {
      qs('#moveSpeedVal').textContent = qs('#moveSpeed').value;
      qs('#turnSpeedVal').textContent = qs('#turnSpeed').value;
      qs('#stepMsVal').textContent = qs('#stepMs').value;
      qs('#volumeVal').textContent = qs('#volume').value;
    }

    qsa('input[type=range]').forEach(r => r.addEventListener('input', syncLabels));

    async function getJson(url) {
      const r = await fetch(url);
      return await r.json();
    }

    async function postJson(url, body) {
      const r = await fetch(url, {
        method: 'POST',
        headers: {'Content-Type':'application/json'},
        body: JSON.stringify(body || {})
      });
      return await r.json();
    }

    async function refreshStatus() {
      const st = await getJson('/api/status');
      qs('#homeStatus').textContent = `IP: ${location.host} | WheelReady: ${st.wheel_ready} | L/R speed: ${st.left_speed}/${st.right_speed} | Volume: ${st.volume}`;
      qs('#controlStatus').textContent = `Move=${st.move_speed}, Turn=${st.turn_speed}, Step=${st.step_ms}ms`;
    }

    async function loadSettings() {
      const s = await getJson('/api/settings');
      qs('#moveSpeed').value = s.move_speed;
      qs('#turnSpeed').value = s.turn_speed;
      qs('#stepMs').value = s.step_ms;
      qs('#volume').value = s.volume;
      syncLabels();
    }

    qs('#btnSaveSettings').onclick = async () => {
      const payload = {
        move_speed: Number(qs('#moveSpeed').value),
        turn_speed: Number(qs('#turnSpeed').value),
        step_ms: Number(qs('#stepMs').value),
        volume: Number(qs('#volume').value)
      };
      const r = await postJson('/api/settings', payload);
      qs('#settingsStatus').textContent = r.message || 'Đã lưu.';
      refreshStatus();
    };

    async function move(action) {
      const ms = Number(qs('#stepMs').value);
      await postJson('/api/control', {action, duration_ms: ms});
      qs('#controlStatus').textContent = `Đã gửi lệnh: ${action}`;
      setTimeout(refreshStatus, 120);
    }

    qsa('[data-act]').forEach(btn => {
      btn.onclick = async () => {
        const action = btn.dataset.act;
        if (['forward','backward','left','right'].includes(action)) {
          await move(action);
        } else {
          const r = await postJson('/api/control', {action});
          qs('#controlStatus').textContent = r.message || action;
          setTimeout(refreshStatus, 120);
        }
      };
    });

    async function music(action, extra) {
      const r = await postJson('/api/music', {action, ...(extra||{})});
      qs('#musicStatus').textContent = r.message || 'OK';
    }

    qs('#btnPlayHappy').onclick = () => music('play_mood', {mood: 'vuive'});
    qs('#btnPlaySad').onclick = () => music('play_mood', {mood: 'buon'});
    qs('#btnNextSong').onclick = () => music('next');
    qs('#btnStopSong').onclick = () => music('stop');
    qs('#btnSearchMusic').onclick = () => {
      const query = qs('#musicQuery').value.trim();
      if (!query) return;
      music('search', {query});
    };

    (async () => {
      await loadSettings();
      await refreshStatus();
      setInterval(refreshStatus, 3000);
    })();
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
