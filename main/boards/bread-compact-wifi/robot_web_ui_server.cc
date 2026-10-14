#include "robot_web_ui_server.h"
#include "rc_sbus_controller.h"
#include "ws_log_broadcaster.h"

#include "application.h"
#include "board.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstring>
#include <vector>

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
    .log-box { font-family: monospace; font-size: 11px; color: var(--text-dim); background: #05080f; border-radius: 6px; padding: 8px; height: 120px; overflow-y: auto; margin-top: 10px; }
     
    /* Chat box */
    #chatCard { grid-column: 1 / -1 !important; }
    #chatMessages {
      display: flex;
      flex-direction: column;
      height: 220px;
      overflow-y: auto;
      background: #05080f;
      border-radius: 6px;
      padding: 10px;
      margin-bottom: 10px;
      font-size: 13px;
    }
    .chat-msg-user,
    .chat-msg-robot,
    .chat-msg-sys {
      max-width: 80%;
      padding: 6px 10px;
      border-radius: 10px;
      margin-bottom: 8px;
      word-wrap: break-word;
      line-height: 1.4;
    }
    .chat-msg-user {
      align-self: flex-end;
      background: rgba(0, 210, 255, 0.15);
      border: 1px solid rgba(0, 210, 255, 0.4);
      color: #00d2ff;
      text-align: right;
    }
    .chat-msg-robot {
      align-self: flex-start;
      background: rgba(0, 230, 118, 0.10);
      border: 1px solid rgba(0, 230, 118, 0.3);
      color: #00e676;
      text-align: left;
    }
    .chat-msg-sys {
      align-self: center;
      color: #888;
      font-style: italic;
      font-size: 11px;
      background: transparent;
      border: none;
    }
    .chat-msg-time { color: #555; font-size: 10px; }
    #chatInputRow   { display: flex; gap: 8px; }
    #chatInput      { flex: 1; margin-bottom: 0; }
    #btnChatSend    { background: var(--accent); color: #000; font-weight: bold;
                      padding: 8px 18px; border-radius: 8px; border: none; cursor: pointer; }

    /* Log levels */
    .log-info  { color: #8a99ad; }
    .log-warn  { color: #ffb74d; }
    .log-error { color: #ff5252; }
    .log-debug { color: #666;    }
    .log-sys   { color: #00d2ff; }

    /* Log box to hơn */
    .log-box { height: 200px !important; overflow-y: auto !important; }
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
        <!-- Card 5: Chat với Robot -->
        <div class="card" id="chatCard">
        <div class="card-title">💬 Chat với Robot</div>
        <div id="chatMessages">
            <div class="chat-msg-sys">Nhập lệnh: "đi thẳng 2 giây", "dừng", "gật đầu", ...</div>
        </div>
        <div id="chatInputRow">
            <input type="text" id="chatInput"
                placeholder="Nhập lệnh (vd: đi thẳng 2 giây)" />
            <button id="btnChatSend">Gửi</button>
        </div>
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
    // ===== Helpers =====
    const $ = id => document.getElementById(id);

    function escapeHtml(s) {
      return String(s).replace(/[&<>"']/g, c => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
      }[c]));
    }

    // ===== Log box =====
    function appendLogLine(level, msg, cls) {
      const box = document.getElementById('logBox');
      if (!box) return;
      const t = new Date().toLocaleTimeString();
      const line = document.createElement('div');
      line.className = cls || 'log-info';
      line.textContent = `[${t}] ${level} ${msg}`;
      box.appendChild(line);
      while (box.childNodes.length > 200) box.removeChild(box.firstChild);
      box.scrollTop = box.scrollHeight;
    }

    function log(msg) {
      appendLogLine('SYS', msg, 'log-sys');
    }

    // ===== Chat box =====
    function addChatMsg(who, text) {
        const box = document.getElementById('chatMessages');
        if (!box) return;
        const t = new Date().toLocaleTimeString();
        const div = document.createElement('div');
        const cls = who === 'user' ? 'chat-msg-user' : 'chat-msg-robot';
        const label = who === 'user' ? '👤 Bạn' : '🤖 Robot';
        div.className = cls;
        div.innerHTML = `<div><span class="chat-msg-time">[${t}]</span> <b>${label}</b></div>
                        <div>${escapeHtml(text)}</div>`;
        box.appendChild(div);
        box.scrollTop = box.scrollHeight;
    }   

    function sendChat() {
      const input = document.getElementById('chatInput');
      const text = input.value.trim();
      if (!text) return;
      if (!ws || ws.readyState !== WebSocket.OPEN) {
        addChatMsg('robot', '(Chưa kết nối WebSocket)');
        return;
      }
      addChatMsg('user', text);
      ws.send(JSON.stringify({ type: 'chat', text: text }));
      input.value = '';
    }

    // ===== REST helpers =====
    function syncVal(labelId, val) { $(labelId).innerText = val; }

    async function apiGet(url) {
      try { const r = await fetch(url); return await r.json(); }
      catch (e) { log('Lỗi kết nối API'); }
    }

    async function apiPost(url, body) {
      try {
        const r = await fetch(url, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(body)
        });
        const res = await r.json();
        log(res.message || 'Thành công');
        return res;
      } catch (e) { log('Lỗi gửi lệnh'); }
    }

    async function refreshStatus() {
      const st = await apiGet('/api/status');
      if (st) {
        $('stWheel').innerText = st.wheel_ready ? 'Sẵn sàng' : 'Chưa kết nối';
        $('stSpeed').innerText = `${st.left_speed}/${st.right_speed}`;
        $('stVol').innerText = `${st.volume}%`;
      }
    }

    async function sendCtrl(action) {
      const ms = Number($('stepMs').value);
      const isMove = ['forward', 'backward', 'left', 'right'].includes(action);
      await apiPost('/api/control', isMove ? { action, duration_ms: ms } : { action });
      setTimeout(refreshStatus, 150);
    }

    async function sendMusic(action, extra = {}) {
      await apiPost('/api/music', { action, ...extra });
    }

    function searchMusic() {
      const q = $('musicQuery').value.trim();
      if (q) sendMusic('search', { query: q });
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

    // ===== Init =====
    async function init() {
      try { initWs(); }
      catch (e) { log('INIT WS ERROR: ' + e.message); console.error(e); }

      try {
        const s = await apiGet('/api/settings');
        if (s) {
          $('moveSpeed').value = s.move_speed; syncVal('vMove', s.move_speed);
          $('turnSpeed').value = s.turn_speed; syncVal('vTurn', s.turn_speed);
          $('stepMs').value = s.step_ms;       syncVal('vStep', s.step_ms);
          $('volume').value = s.volume;        syncVal('vVol', s.volume);
        }
      } catch (e) { log('INIT SETTINGS ERROR: ' + e.message); }

      try {
        refreshStatus();
        setInterval(refreshStatus, 3000);
      } catch (e) { log('INIT STATUS ERROR: ' + e.message); }
    }

    // ===== WebSocket =====
    let ws = null;
    let wsBackoff = 1000;
    let wsManualClose = false;

    function initWs() {
      log('initWs() called');
      try {
        const url = `ws://${location.host}/ws`;
        log(`WS connecting → ${url}`);
        ws = new WebSocket(url);

        ws.onopen = () => {
          wsBackoff = 1000;
          log('WS connected');
          ws.send(JSON.stringify({ type: 'ping', t: Date.now() }));
        };

        ws.onmessage = (ev) => {
          let msg = null;
          try { msg = JSON.parse(ev.data); } catch (_) { }

          if (!msg) {
            log('WS recv (raw): ' + ev.data);
            return;
          }

          if (msg.type === 'log') {
            const cls = msg.level === 'E' ? 'log-error'
              : msg.level === 'W' ? 'log-warn'
                : msg.level === 'D' ? 'log-debug'
                  : 'log-info';
            appendLogLine(msg.level, msg.msg, cls);
          } else if (msg.type === 'chat_reply') {
            addChatMsg('robot', msg.text);
          } else if (msg.type === 'heartbeat') {
            /* silent */
          } else if (msg.type === 'hello' || msg.type === 'pong') {
            /* silent */
          } else {
            log('WS recv: ' + ev.data);
          }
        };

        ws.onclose = (e) => {
          log(`WS closed code=${e.code}`);
          if (wsManualClose) { wsManualClose = false; return; }
          setTimeout(initWs, wsBackoff);
          wsBackoff = Math.min(wsBackoff * 2, 30000);
        };

        ws.onerror = () => log('WS error');
      } catch (e) {
        log('initWs EXCEPTION: ' + e.message);
        console.error(e);
      }
    }

    // ===== Boot =====
    window.addEventListener('load', () => {
      log('page loaded');
      init();
      const btn = document.getElementById('btnChatSend');
      if (btn) btn.addEventListener('click', sendChat);
      const inp = document.getElementById('chatInput');
      if (inp) inp.addEventListener('keydown', e => {
        if (e.key === 'Enter') sendChat();
      });
    });
  </script>

</body>
</html>
)HTML";

}  // namespace

RobotWebUiServer::RobotWebUiServer(DualDcMotorController* wheel_motor,
                                   DualServoController* head_servo)
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
    config.max_open_sockets = 10;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

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
    // ============ PHASE 1: WebSocket /ws ============
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = &RobotWebUiServer::WsHandlerEntry,
        .user_ctx = this,
        .is_websocket = true,
    };
    esp_err_t ws_err = httpd_register_uri_handler(server_handle_, &ws_uri);
    if (ws_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /ws: %s", esp_err_to_name(ws_err));
    } else {
        // Heartbeat task: 4KB stack, priority thấp hơn httpd task (thường là prio 5)
        BaseType_t ok =
            xTaskCreatePinnedToCore(&RobotWebUiServer::WsHeartbeatTaskEntry, "ws_heartbeat", 4096,
                                    this, tskIDLE_PRIORITY + 2, &ws_heartbeat_task_, 0);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create ws_heartbeat task");
            ws_heartbeat_task_ = nullptr;
        } else {
            ESP_LOGI(TAG, "WebSocket ready at ws://<device-ip>:%d/ws", port);
        }
    }
    // === PHASE 2: hook log ESP-IDF → WS ===
    WsLogBroadcaster::GetInstance().Init([this](const char* json) { this->BroadcastJson(json); });
    // ================================================
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
    int duration_ms =
        cJSON_IsNumber(duration_json) ? Clamp(duration_json->valueint, 80, 5000) : step_ms;

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
        SendJson(req, 400,
                 "{\"ok\":false,\"message\":\"Unknown action or unavailable controller\"}");
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
        std::string mood = cJSON_IsString(mood_json) && mood_json->valuestring != nullptr
                               ? mood_json->valuestring
                               : "vuive";
        ok = Application::GetInstance().PlaySdCardMusicByMood(mood);
        message = ok ? "Playing mood music" : "Play mood failed";
    } else if (action == "search") {
        auto* query_json = cJSON_GetObjectItem(root, "query");
        std::string query = cJSON_IsString(query_json) && query_json->valuestring != nullptr
                                ? query_json->valuestring
                                : "";
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

// PHASE 1: WebSocket — handler, broadcast, heartbeat
// ============================================================

esp_err_t RobotWebUiServer::WsHandlerEntry(httpd_req_t* req) {
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    if (self == nullptr)
        return ESP_FAIL;
    return self->HandleWs(req);
}

esp_err_t RobotWebUiServer::HandleWs(httpd_req_t* req) {
    const int fd = httpd_req_to_sockfd(req);

    // Idempotent: đảm bảo fd có trong danh sách (không quan tâm đây là
    // handshake hay data — cả 2 đều cần fd này được track).
    WsAddClient(fd);

    // Thử đọc frame. Nếu đây là handshake-only call thì recv sẽ trả
    // ESP_ERR_INVALID_STATE (hoặc timeout với len=0) → không phải lỗi.
    uint8_t buf[kWsRxBufSize];
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = buf;

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, sizeof(buf));

    // Handshake-only: không có frame để đọc
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "WS handshake done fd=%d (clients=%u)", fd,
                 static_cast<unsigned>(GetWsClientCount()));
        WsSendTextToReq(req, "{\"type\":\"hello\",\"server\":\"robot-webui\",\"proto\":1}");
        return ESP_OK;
    }

    if (ret != ESP_OK) {
        printf("[RobotWebUI] recv_frame fd=%d err=%d\n", fd, ret);
        WsRemoveClient(fd);
        return ret;
    }

    // frame.len == 0 cũng coi như handshake-only
    if (frame.len == 0) {
        return ESP_OK;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WS close fd=%d", fd);
        WsRemoveClient(fd);
        return ESP_OK;
    }

    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        HandleWsText(fd, frame.payload, frame.len);
    }
    return ESP_OK;
}

void RobotWebUiServer::HandleWsText(int fd, const uint8_t* data, size_t len) {
    if (len == 0 || len >= 512)
        return;

    char tmp[512];
    memcpy(tmp, data, len);
    tmp[len] = '\0';

    cJSON* root = cJSON_Parse(tmp);
    if (root == nullptr)
        return;

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type) || type->valuestring == nullptr) {
        cJSON_Delete(root);
        return;
    }

    std::string t = type->valuestring;

    if (t == "ping") {
        char reply[96];
        snprintf(reply, sizeof(reply), "{\"type\":\"pong\",\"uptime_ms\":%lu}",
                 static_cast<unsigned long>(esp_timer_get_time() / 1000ULL));
        WsSendTextToFd(fd, reply);
    } else if (t == "chat") {
        HandleChat(fd, tmp, len);
    }

    cJSON_Delete(root);
}

// ---------- Client list ----------

bool RobotWebUiServer::WsIsRegistered(int fd) {
    std::lock_guard<std::mutex> lock(ws_mutex_);
    for (int c : ws_clients_) {
        if (c == fd)
            return true;
    }
    return false;
}

void RobotWebUiServer::WsAddClient(int fd) {
    std::lock_guard<std::mutex> lock(ws_mutex_);
    // Bỏ qua nếu đã có (idempotent)
    for (int c : ws_clients_)
        if (c == fd)
            return;
    if (ws_clients_.size() >= kMaxWsClients) {
        ESP_LOGW(TAG, "WS client limit reached, fd=%d rejected", fd);
        return;
    }
    ws_clients_.push_back(fd);
}

void RobotWebUiServer::WsRemoveClient(int fd) {
    std::lock_guard<std::mutex> lock(ws_mutex_);
    for (auto it = ws_clients_.begin(); it != ws_clients_.end(); ++it) {
        if (*it == fd) {
            ws_clients_.erase(it);
            break;
        }
    }
}

size_t RobotWebUiServer::GetWsClientCount() {
    std::lock_guard<std::mutex> lock(ws_mutex_);
    return ws_clients_.size();
}

// ---------- Send ----------

void RobotWebUiServer::WsSendTextToFd(int fd, const char* text) {
    if (server_handle_ == nullptr || text == nullptr)
        return;
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(text));
    frame.len = strlen(text);
    // send_frame_async copy payload → stack buffer an toàn
    esp_err_t err = httpd_ws_send_frame_async(server_handle_, fd, &frame);
    if (err != ESP_OK) {
        printf("[RobotWebUI] send fd=%d err=%d\n", fd, err);
        WsRemoveClient(fd);
    }
}

void RobotWebUiServer::WsSendTextToReq(httpd_req_t* req, const char* text) {
    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(text));
    frame.len = strlen(text);
    esp_err_t err = httpd_ws_send_frame(req, &frame);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "send(handshake) err=%d", err);
    }
}

// ---------- Broadcast ----------

void RobotWebUiServer::BroadcastJson(const char* json) {
    if (server_handle_ == nullptr || json == nullptr)
        return;

    // Snapshot để giữ critical section ngắn (send có thể block)
    std::vector<int> snapshot;
    {
        std::lock_guard<std::mutex> lock(ws_mutex_);
        snapshot = ws_clients_;
    }
    if (snapshot.empty())
        return;

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(json));
    frame.len = strlen(json);

    for (int fd : snapshot) {
        esp_err_t err = httpd_ws_send_frame_async(server_handle_, fd, &frame);
        if (err != ESP_OK) {
            printf("[RobotWebUI] broadcast fd=%d err=%d\n", fd, err);
            WsRemoveClient(fd);
        }
    }
}

// ---------- Heartbeat ----------

void RobotWebUiServer::WsHeartbeatTaskEntry(void* arg) {
    static_cast<RobotWebUiServer*>(arg)->WsHeartbeatLoop();
}

void RobotWebUiServer::WsHeartbeatLoop() {
    const TickType_t period = pdMS_TO_TICKS(kWsHeartbeatMs);
    TickType_t last = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last, period);

        if (GetWsClientCount() == 0)
            continue;

        char buf[128];
        snprintf(buf, sizeof(buf), "{\"type\":\"heartbeat\",\"uptime_ms\":%lu,\"clients\":%u}",
                 static_cast<unsigned long>(esp_timer_get_time() / 1000ULL),
                 static_cast<unsigned>(GetWsClientCount()));
        BroadcastJson(buf);
    }
}

// ============================================================
// PHASE 4: Chat — parse keyword local, không gọi LLM
// ============================================================

namespace {

std::string ToLowerAscii(const std::string& s) {
    std::string r = s;
    for (auto& c : r) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return r;
}

bool Contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

// Tìm số kèm đơn vị trong text: "2 giây", "2s", "1500ms"
// Trả về ms, hoặc default_ms nếu không tìm thấy.
int ExtractDurationMs(const std::string& lower, int default_ms) {
    for (size_t i = 0; i < lower.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(lower[i])))
            continue;

        size_t j = i;
        while (j < lower.size() && std::isdigit(static_cast<unsigned char>(lower[j])))
            j++;

        int val = 0;
        try {
            val = std::stoi(lower.substr(i, j - i));
        } catch (...) {
            return default_ms;
        }

        size_t k = j;
        while (k < lower.size() && lower[k] == ' ')
            k++;

        std::string unit;
        while (k < lower.size() && std::isalpha(static_cast<unsigned char>(lower[k]))) {
            unit += lower[k++];
        }

        int ms;
        if (unit == "ms") {
            ms = val;
        } else if (unit == "s" || unit.rfind("sec", 0) == 0 || Contains(unit, "gi")) {
            ms = val * 1000;
        } else if (unit.empty()) {
            ms = val * 1000;  // "đi thẳng 2" → mặc định 2 giây
        } else {
            i = j;  // không hiểu đơn vị, thử số tiếp theo
            continue;
        }

        if (ms >= 100 && ms <= 5000)
            return ms;
        i = j;
    }
    return default_ms;
}

}  // namespace

void RobotWebUiServer::HandleChat(int fd, const char* json_text, size_t len) {
    cJSON* root = cJSON_Parse(json_text);
    if (root == nullptr)
        return;

    cJSON* text_item = cJSON_GetObjectItem(root, "text");
    if (!cJSON_IsString(text_item) || text_item->valuestring == nullptr) {
        cJSON_Delete(root);
        SendChatReply(fd, "Lệnh không hợp lệ", "");
        return;
    }

    std::string text = text_item->valuestring;
    cJSON_Delete(root);

    if (text.empty() || text.size() > 200) {
        SendChatReply(fd, "Lệnh rỗng hoặc quá dài (max 200 ký tự)", "");
        return;
    }

    // Rule: RC đang override → từ chối
    if (rc_sbus::IsOverrideActive()) {
        SendChatReply(fd, "Tay điều khiển RC đang hoạt động, không nhận lệnh chat", "");
        return;
    }

    std::string lower = ToLowerAscii(text);

    int move_speed, turn_speed;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        move_speed = config_.move_speed;
        turn_speed = config_.turn_speed;
    }

    int duration_ms = ExtractDurationMs(lower, 2000);

    std::string tool;
    std::string reply;
    bool handled = false;

    // ====== STOP (ưu tiên cao) ======
    if (Contains(lower, "dung") || Contains(lower, "dừng") || Contains(lower, "stop") ||
        Contains(lower, "đứng")) {
        if (wheel_motor_) {
            wheel_motor_->Stop();
            if (motion_stop_timer_)
                esp_timer_stop(motion_stop_timer_);
            tool = "self.robot.move";
            reply = "Đã dừng";
            handled = true;
        }
    }
    // ====== WHEEL ======
    else if (Contains(lower, "tien") || Contains(lower, "tiến") || Contains(lower, "thang") ||
             Contains(lower, "thẳng") || Contains(lower, "forward")) {
        if (wheel_motor_) {
            wheel_motor_->Forward(move_speed);
            ArmMotionStopTimer(duration_ms);
            tool = "self.robot.move";
            reply = "OK, đã tiến " + std::to_string(duration_ms) +
                    "ms (speed=" + std::to_string(move_speed) + ")";
            handled = true;
        }
    } else if (Contains(lower, "lui") || Contains(lower, "lùi") || Contains(lower, "backward")) {
        if (wheel_motor_) {
            wheel_motor_->Backward(move_speed);
            ArmMotionStopTimer(duration_ms);
            tool = "self.robot.move";
            reply = "OK, đã lùi " + std::to_string(duration_ms) + "ms";
            handled = true;
        }
    } else if (Contains(lower, "trai") || Contains(lower, "trái")) {
        if (wheel_motor_) {
            wheel_motor_->TurnLeft(turn_speed);
            ArmMotionStopTimer(duration_ms);
            tool = "self.robot.move";
            reply = "OK, đã qua trái";
            handled = true;
        }
    } else if (Contains(lower, "phai") || Contains(lower, "phải")) {
        if (wheel_motor_) {
            wheel_motor_->TurnRight(turn_speed);
            ArmMotionStopTimer(duration_ms);
            tool = "self.robot.move";
            reply = "OK, đã qua phải";
            handled = true;
        }
    }
    // ====== HEAD ======
    else if (Contains(lower, "gat") || Contains(lower, "gật") || Contains(lower, "nod")) {
        if (head_servo_ && head_servo_->nodYes()) {
            tool = "self.robot.move";
            reply = "Đã gật đầu";
            handled = true;
        }
    } else if (Contains(lower, "lac") || Contains(lower, "lắc") || Contains(lower, "shake")) {
        if (head_servo_ && head_servo_->shakeNo()) {
            tool = "self.robot.move";
            reply = "Đã lắc đầu";
            handled = true;
        }
    } else if (Contains(lower, "to mo") || Contains(lower, "tò mò") || Contains(lower, "curious")) {
        if (head_servo_ && head_servo_->curiousHeadTilt()) {
            tool = "self.robot.move";
            reply = "Đã nghiêng đầu tò mò";
            handled = true;
        }
    } else if (Contains(lower, "can bang") || Contains(lower, "cân bằng") ||
               Contains(lower, "center") || Contains(lower, "giua") || Contains(lower, "giữa")) {
        if (head_servo_ && head_servo_->RunAction("center")) {
            tool = "self.robot.move";
            reply = "Đã về vị trí cân bằng";
            handled = true;
        }
    }
    // ====== MUSIC ======
        else if (Contains(lower, "dung nhac") || Contains(lower, "dừng nhạc") ||
                Contains(lower, "stop nhac") || Contains(lower, "tắt nhạc") ||
                Contains(lower, "tat nhac")) {
            auto& app = Application::GetInstance();
            bool ok = app.StopSdCardMusic();
            ESP_LOGI(TAG, "stop music called, StopSdCardMusic=%d", ok);

            tool  = "self.webui.music";
            reply = ok ? "Đã dừng nhạc" : "Không dừng được";
            handled = true;
    } else if (Contains(lower, "nhac vui") || Contains(lower, "nhạc vui") ||
               Contains(lower, "vuive") || Contains(lower, "vui vẻ") || Contains(lower, "vui ve") ||
               (Contains(lower, "nhac") && Contains(lower, "vui")) ||
               (Contains(lower, "nhạc") && Contains(lower, "vui"))) {
        if (Application::GetInstance().PlaySdCardMusicByMood("vuive")) {
            tool = "self.webui.music";
            reply = "Đang phát nhạc vui 🎵";
            handled = true;
        } else {
            tool = "self.webui.music";
            reply = "Không tìm thấy nhạc vui trong SD card";
            handled = true;
        }
    } else if (Contains(lower, "nhac buon") || Contains(lower, "nhạc buồn") ||
               Contains(lower, "buon") || Contains(lower, "buồn") ||
               (Contains(lower, "nhac") && Contains(lower, "buon")) ||
               (Contains(lower, "nhạc") && Contains(lower, "buồn"))) {
        if (Application::GetInstance().PlaySdCardMusicByMood("buon")) {
            tool = "self.webui.music";
            reply = "Đang phát nhạc buồn 🎵";
            handled = true;
        } else {
            tool = "self.webui.music";
            reply = "Không tìm thấy nhạc buồn trong SD card";
            handled = true;
        }
    } else if (Contains(lower, "bai tiep") || Contains(lower, "bài tiếp") ||
               Contains(lower, "next") || Contains(lower, "chuyen bai") ||
               Contains(lower, "chuyển bài") || Contains(lower, "bai khac") ||
               Contains(lower, "bài khác")) {
        if (Application::GetInstance().PlayNextSdCardMusic()) {
            tool = "self.webui.music";
            reply = "Đã chuyển bài tiếp";
            handled = true;
        } else {
            tool = "self.webui.music";
            reply = "Không chuyển bài được";
            handled = true;
        }
    } else if (Contains(lower, "mo nhac") || Contains(lower, "mở nhạc") ||
               Contains(lower, "phat nhac") || Contains(lower, "phát nhạc") ||
               Contains(lower, "cho nhac") || Contains(lower, "chơi nhạc")) {
        // "mở nhạc" chung chung → phát mood vui default
        if (Application::GetInstance().PlaySdCardMusicByMood("vuive")) {
            tool = "self.webui.music";
            reply = "Đang phát nhạc 🎵";
            handled = true;
        } else {
            tool = "self.webui.music";
            reply = "Không phát được nhạc";
            handled = true;
        }
    } else if (Contains(lower, "tim bai") || Contains(lower, "tìm bài") ||
               Contains(lower, "search nhac") || Contains(lower, "tìm nhạc")) {
        // "tìm bài <tên>" → extract sau keyword
        size_t pos = lower.find("bài");
        if (pos == std::string::npos)
            pos = lower.find("nhạc");
        if (pos == std::string::npos)
            pos = lower.find("bai");
        if (pos == std::string::npos)
            pos = lower.find("nhac");

        std::string query;
        if (pos != std::string::npos) {
            query = text.substr(pos + 3);  // giữ nguyên dấu gốc
            while (!query.empty() && query.front() == ' ')
                query.erase(0, 1);
        }

        if (!query.empty() && Application::GetInstance().PlaySdCardMusicByQuery(query)) {
            tool = "self.webui.music";
            reply = "Đang phát: " + query;
            handled = true;
        } else {
            tool = "self.webui.music";
            reply = "Không tìm thấy bài: " + (query.empty() ? "(thiếu tên)" : query);
            handled = true;
        }
    }

    if (!handled) {
        reply =
            "Không hiểu lệnh. Thử: 'đi thẳng 2 giây', 'dừng', "
            "'qua trái', 'gật đầu', 'lắc đầu', 'về center'";
    }

    ESP_LOGI(TAG, "chat fd=%d text=\"%s\" → %s",
         fd, text.c_str(), handled ? reply.c_str() : "unhandled");

    SendChatReply(fd, reply, tool);
}

void RobotWebUiServer::SendChatReply(int fd, const std::string& text, const std::string& tool) {
    cJSON* r = cJSON_CreateObject();
    cJSON_AddStringToObject(r, "type", "chat_reply");
    cJSON_AddStringToObject(r, "text", text.c_str());
    cJSON_AddStringToObject(r, "tool", tool.c_str());

    char* out = cJSON_PrintUnformatted(r);
    if (out != nullptr) {
        WsSendTextToFd(fd, out);
        cJSON_free(out);
    }
    cJSON_Delete(r);
}