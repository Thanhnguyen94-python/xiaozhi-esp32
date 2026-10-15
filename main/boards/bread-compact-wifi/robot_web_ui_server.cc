#include "robot_web_ui_server.h"
#include "rc_sbus_controller.h"
#include "ws_log_broadcaster.h"

#include "application.h"
#include "board.h"
#include "settings.h"
#include "robot_life_behavior.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstring>
#include <vector>
#include <dirent.h>       
#include <sys/stat.h>

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
        /* Virtual Joystick */
    .joystick-wrap {
      display: flex; justify-content: center;
      margin: 12px 0 8px 0;
      touch-action: none;              /* chặn scroll khi kéo joystick */
      user-select: none;
      -webkit-user-select: none;
    }
    #joyBase { touch-action: none; cursor: grab; }
    #joyBase:active { cursor: grabbing; }
    .joy-readout {
      display: grid; grid-template-columns: 1fr 1fr;
      gap: 2px 12px; font-family: monospace; font-size: 11px;
      color: var(--text-dim); padding: 8px 12px;
      background: #05080f; border-radius: 6px;
      margin-top: 6px;
    }
    .joy-readout b { color: var(--accent); }
    .joy-mode-toggle {
      display: flex; gap: 12px; font-size: 12px;
      color: var(--text-dim); margin-bottom: 10px;
      justify-content: center;
    }
    .joy-mode-toggle label { cursor: pointer; user-select: none; }
    .joy-mode-toggle input { margin-right: 3px; vertical-align: middle; }
    .hidden { display: none !important; }

        /* Macro */
    .macro-main-btn {
      display: block; width: 100%; padding: 14px;
      border-radius: 10px; border: 2px solid var(--border);
      background: #16223d; color: var(--text);
      font-weight: 700; font-size: 15px; cursor: pointer;
      transition: all 0.15s;
    }
    .macro-main-btn.rec {
      border-color: #ff5252; color: #ff5252;
    }
    .macro-main-btn.rec.active {
      background: #ff5252; color: #fff;
      box-shadow: 0 0 12px rgba(255,82,82,0.5);
      animation: macroPulse 1.2s ease-in-out infinite;
    }
    @keyframes macroPulse {
      0%,100% { box-shadow: 0 0 8px rgba(255,82,82,0.4); }
      50%     { box-shadow: 0 0 20px rgba(255,82,82,0.9); }
    }
    .macro-main-btn.play {
      border-color: #00e676; color: #00e676; margin-top: 8px;
    }
    .macro-main-btn.play.active {
      background: #00e676; color: #000;
      box-shadow: 0 0 12px rgba(0,230,118,0.6);
    }
    .macro-main-btn:disabled {
      opacity: 0.35; cursor: not-allowed; filter: grayscale(1);
    }
    .macro-status {
      font-family: monospace; font-size: 14px; font-weight: 600;
      text-align: center; padding: 10px; margin: 12px 0;
      background: #05080f; border-radius: 6px;
      color: #8a99ad; transition: color 0.2s;
    }
    .macro-status.rec  { color: #ff5252; }
    .macro-status.play { color: #00e676; }
    .macro-clear-btn {
      display: block; width: 100%; margin-top: 8px;
      padding: 8px; border-radius: 8px;
      border: 1px solid var(--border);
      background: transparent; color: var(--text-dim);
      font-size: 12px; cursor: pointer;
    }
    
    .macro-input-row {
      display: flex; gap: 6px; margin-top: 8px; align-items: center;
    }
    #macroNameInput {
      flex: 1; background: #111a2e; border: 1px solid var(--border);
      color: var(--text); padding: 8px 10px; border-radius: 6px;
      font-size: 13px; margin-bottom: 0;
    }
    .macro-mini-btn {
      padding: 8px 12px; border-radius: 6px; border: 1px solid var(--border);
      background: #16223d; color: var(--text); cursor: pointer;
      font-size: 12px; font-weight: 600;
    }
    .macro-mini-btn.save   { border-color: #00d2ff; color: #00d2ff; }
    .macro-mini-btn.load   { border-color: #00e676; color: #00e676; }
    .macro-mini-btn.del    { border-color: #ff5252; color: #ff5252; }
    #macroSavedList {
      width: 100%; margin-top: 8px; padding: 8px;
      background: #111a2e; border: 1px solid var(--border);
      color: var(--text); border-radius: 6px; font-size: 12px;
    }
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
     
        <!-- Card: Macro Record & Playback -->
        <div class="card" id="macroCard">
        <div class="card-title">🎬 Macro Record & Playback</div>

        <button class="macro-main-btn rec" id="btnMacroRec">● Bắt đầu ghi</button>

        <div class="macro-status" id="macroStatus">Chưa có macro</div>

        <button class="macro-main-btn play" id="btnMacroPlay" disabled>▶ Phát lại</button>

        <div class="macro-input-row">
            <input type="text" id="macroNameInput" placeholder="Tên macro (VD: demo_1)" maxlength="32" />
            <button class="macro-mini-btn save" id="btnMacroSave">💾</button>
        </div>
        <select id="macroSavedList">
            <option value="">-- Chưa có macro nào --</option>
        </select>
        <div class="macro-input-row">
            <button class="macro-mini-btn load" id="btnMacroLoad" style="flex:1">📂 Tải</button>
            <button class="macro-mini-btn del"  id="btnMacroDelete" style="flex:1">🗑 Xoá file</button>
        </div>

        <button class="macro-clear-btn" id="btnMacroClear">🗑 Xoá macro</button>
        </div>
      <div class="joy-mode-toggle">
        <label><input type="radio" name="joyMode" value="dpad"> D-pad</label>
        <label><input type="radio" name="joyMode" value="joystick"> Joystick</label>
        <label><input type="radio" name="joyMode" value="both" checked> Cả hai</label>
      </div>
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
            <!-- Virtual Joystick -->
      <div class="joystick-wrap">
        <svg id="joyBase" viewBox="-100 -100 200 200" width="200" height="200">
          <circle cx="0" cy="0" r="90" fill="#0d1424"
                  stroke="#1e2c4d" stroke-width="2"/>
          <line x1="-90" y1="0" x2="90" y2="0"
                stroke="#1e2c4d" stroke-width="1" opacity="0.45"/>
          <line x1="0" y1="-90" x2="0" y2="90"
                stroke="#1e2c4d" stroke-width="1" opacity="0.45"/>
          <circle cx="0" cy="0" r="2" fill="#8a99ad"/>
          <circle id="joyKnob" cx="0" cy="0" r="28"
                  fill="rgba(0,210,255,0.35)"
                  stroke="#00d2ff" stroke-width="2"/>
        </svg>
      </div>
      <div class="joy-readout">
        <div>x: <b><span id="joyX">0.00</span></b></div>
        <div>y: <b><span id="joyY">0.00</span></b></div>
        <div>L: <b><span id="joyL">0</span></b></div>
        <div>R: <b><span id="joyR">0</span></b></div>
      </div>
        
    </div>

    <!-- Card: Gọi robot & Hệ thống -->
    <div class="card" id="sysCard">
      <div class="card-title">🤖 Gọi robot & Hệ thống</div>
      <div class="btn-grid">
        <button class="btn-action" onclick="sendCtrl('robot_call')">📣 Gọi / Ngắt robot</button>
        <button class="btn-action" style="border-color: var(--danger); color: var(--danger);" onclick="confirmReboot()">🔄 Khởi động lại</button>
      </div>
      <div style="font-size:12px; color: var(--text-dim); margin-top:8px; line-height:1.45;">
        <b>Gọi / Ngắt robot</b>: giống nhấn nút BOOT hoặc chạm 2 lần trên robot. Robot đang rảnh thì bắt đầu nghe, đang nghe hoặc đang nói thì ngắt.<br>
        <b>Khởi động lại</b>: robot tắt rồi bật lại trong vài giây. Trang web sẽ tự kết nối lại khi robot lên.
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
        <!-- Card 5: Hành vi "có sự sống" -->
    <div class="card" id="lifeCard">
    <style>
        #lifeCard .lf-hint { font-size: 12px; color: var(--text-dim); margin: 3px 0 10px 0; line-height: 1.45; }
        #lifeCard .lf-warn { color: #ffb74d; }
        #lifeCard input[type=number] { width: 100%; background: #111a2e; border: 1px solid var(--border); color: var(--text); padding: 8px 12px; border-radius: 8px; }
        #lifeCard .lf-row { display: flex; gap: 8px; }
        #lifeCard .lf-row > div { flex: 1; }
        #lifeCard .lf-sep { border-top: 1px solid var(--border); margin: 12px 0; }
        
        /* Cấu trúc lại các dòng chứa label và checkbox */
        #lifeCard .lf-checkbox-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px; }
        #lifeCard label.lf-chk { display: flex; gap: 8px; align-items: center; cursor: pointer; font-size: 14px; flex: 1; }
        #lifeCard .control-label { display: flex; justify-content: space-between; align-items: center; }

        /* Style cho nút dấu hỏi */
        #lifeCard .help-btn {
        display: inline-flex; align-items: center; justify-content: center;
        width: 18px; height: 18px; border-radius: 50%;
        background: #1e2c4d; color: #8a99ad;
        font-size: 12px; font-weight: bold;
        cursor: pointer; border: 1px solid #1e2c4d;
        transition: all 0.2s; user-select: none; flex-shrink: 0;
        }
        #lifeCard .help-btn:hover { background: #00d2ff; color: #000; border-color: #00d2ff; }
        
        /* Class ẩn chú thích */
        #lifeCard .lf-hint-hidden { display: none !important; }
    </style>

    <div class="card-title">
        🌱 Hành vi "có sự sống"
        <span class="help-btn" onclick="toggleHintById('hint-main')">?</span>
    </div>
    <div class="lf-hint lf-hint-hidden" id="hint-main">Robot tự gật/lắc đầu và nhích nhẹ khi trò chuyện để trông sinh động hơn. Chỉnh xong nhớ bấm "Lưu". Giá trị được nhớ cả khi tắt nguồn.</div>

    <!-- Checkbox 1 -->
    <div class="lf-checkbox-row">
        <label class="lf-chk"><input type="checkbox" id="lfHead"> Cử động đầu khi robot đang nói</label>
        <span class="help-btn" onclick="toggleHintById('hint-head')">?</span>
    </div>
    <div class="lf-hint lf-hint-hidden" id="hint-head">Đầu gật/lắc ngẫu nhiên nhẹ trong lúc robot trả lời. Tắt nếu thấy tiếng servo làm phiền.</div>

    <!-- Checkbox 2 -->
    <div class="lf-checkbox-row">
        <label class="lf-chk"><input type="checkbox" id="lfListen"> Gật đầu khi nghe bạn nói</label>
        <span class="help-btn" onclick="toggleHintById('hint-listen')">?</span>
    </div>
    <div class="lf-hint lf-hint-hidden" id="hint-listen">Khi bạn nói liên tục hơn khoảng 1,5 giây, robot gật đầu để thể hiện đang lắng nghe. Nếu robot gật khi không ai nói (micro bắt nhiễu), hãy tắt mục này.</div>

    <!-- Checkbox 3 -->
    <div class="lf-checkbox-row">
        <label class="lf-chk"><input type="checkbox" id="lfIdle"> Cử động nhỏ khi rảnh</label>
        <span class="help-btn" onclick="toggleHintById('hint-idle')">?</span>
    </div>
    <div class="lf-hint lf-hint-hidden" id="hint-idle">Thỉnh thoảng nghiêng đầu tò mò khi không trò chuyện.</div>

    <div class="lf-sep"></div>
    
    <!-- Checkbox 4 (Warning) -->
    <div class="lf-checkbox-row">
        <label class="lf-chk"><input type="checkbox" id="lfWheel"> ⚠️ Cho phép nhích bánh xe khi robot nói</label>
        <span class="help-btn" onclick="toggleHintById('hint-wheel')">?</span>
    </div>
    <div class="lf-hint lf-warn lf-hint-hidden" id="hint-wheel">Robot sẽ tiến/lùi nhẹ khi đang nói. Chỉ bật sau khi bạn đã bấm "Thử nhích" và đo quãng đường bên dưới. Đặt robot ở giữa bàn, cách mép bàn xa hơn "Giới hạn lệch" ít nhất 20 cm. Lệnh dừng từ web, giọng nói hoặc tay cầm RC luôn được ưu tiên.</div>

    <!-- Input 1 -->
    <div class="control-label">
        <span>Tốc độ khi nhích (30–100)</span>
        <span class="help-btn" onclick="toggleHintById('hint-speed')">?</span>
    </div>
    <input type="number" id="lfSpeed" min="30" max="100" step="5">
    <div class="lf-hint lf-hint-hidden" id="hint-speed">Công suất động cơ mỗi lần nhích. Số lớn thì nhích dứt khoát, số nhỏ thì êm nhưng bánh có thể không quay. Gợi ý bắt đầu 80–90; nếu bánh không nhúc nhích thì tăng lên.</div>

    <!-- Input 2 -->
    <div class="control-label">
        <span>Thời gian cấp điện mỗi lần nhích (ms) (40–500)</span>
        <span class="help-btn" onclick="toggleHintById('hint-ms')">?</span>
    </div>
    <input type="number" id="lfMs" min="40" max="500" step="10">
    <div class="lf-hint lf-hint-hidden" id="hint-ms">Càng lớn thì robot đi càng xa mỗi lần. Nếu bấm "Thử nhích" mà bánh không nhúc nhích thì tăng thêm 20–40 ms rồi thử lại. Robot lớn/nặng cần số lớn hơn robot nhỏ/nhẹ.</div>

    <!-- Input 3 -->
    <div class="control-label">
        <span>Quãng đường THỰC TẾ mỗi lần nhích (cm) (1–25)</span>
        <span class="help-btn" onclick="toggleHintById('hint-cm')">?</span>
    </div>
    <input type="number" id="lfCm" min="1" max="25" step="1">
    <div class="lf-hint lf-hint-hidden" id="hint-cm">Cách làm: đặt robot trên bàn, bấm "Thử nhích tiến", dùng thước đo robot vừa đi bao nhiêu cm (đo 3 lần lấy trung bình, làm tròn), rồi nhập vào đây. Robot dùng số này để tính mình đã lệch bao xa so với chỗ đặt. Nhập thấp hơn thực tế sẽ làm giới hạn bên dưới kém an toàn.</div>

    <!-- Input 4 -->
    <div class="control-label">
        <span>Giới hạn lệch tối đa so với chỗ đặt (cm) (5–25)</span>
        <span class="help-btn" onclick="toggleHintById('hint-drift')">?</span>
    </div>
    <input type="number" id="lfDrift" min="5" max="25" step="1">
    <div class="lf-hint lf-hint-hidden" id="hint-drift">Robot không bao giờ đi xa hơn khoảng này so với chỗ bạn đặt. Hãy đặt nhỏ hơn khoảng cách từ robot đến mép bàn ít nhất 10 cm. Số này phải lớn hơn hoặc bằng "quãng đường mỗi lần nhích", nếu không robot sẽ không nhích. Robot lệch dần do bánh trượt hoặc 2 động cơ không đều, nên thỉnh thoảng hãy đặt lại về giữa bàn.</div>

    <!-- Input 5 -->
    <div class="control-label">
        <span>Nghỉ giữa 2 lần nhích (giây): thấp nhất / cao nhất</span>
        <span class="help-btn" onclick="toggleHintById('hint-gap')">?</span>
    </div>
    <div class="lf-row">
        <div><input type="number" id="lfGapMin" min="2" max="120"></div>
        <div><input type="number" id="lfGapMax" min="2" max="300"></div>
    </div>
    <div class="lf-hint lf-hint-hidden" id="hint-gap">Khi robot đang nói, mỗi lần nhích cách nhau ngẫu nhiên trong khoảng này. Muốn nhích ít hơn thì tăng cả hai số.</div>

    <!-- Input 6 -->
    <div class="control-label">
        <span>Nghỉ giữa 2 cử động khi rảnh (giây): thấp nhất / cao nhất</span>
        <span class="help-btn" onclick="toggleHintById('hint-idle-gap')">?</span>
    </div>
    <div class="lf-row">
        <div><input type="number" id="lfIdleMin" min="5" max="600"></div>
        <div><input type="number" id="lfIdleMax" min="5" max="1200"></div>
    </div>
    <div class="lf-hint lf-hint-hidden" id="hint-idle-gap">Chỉ áp dụng khi robot đang rảnh và bật "Cử động nhỏ khi rảnh".</div>

    <!-- Nút thử nghiệm -->
    <div class="control-label" style="margin-top: 10px;">
        <span>Thử nghiệm</span>
        <span class="help-btn" onclick="toggleHintById('hint-test')">?</span>
    </div>
    <div class="btn-grid" style="margin-bottom: 8px;">
        <button class="btn-action" onclick="testNudge('forward')">⬆️ Thử nhích tiến</button>
        <button class="btn-action" onclick="testNudge('backward')">⬇️ Thử nhích lùi</button>
    </div>
    <div class="lf-hint lf-hint-hidden" id="hint-test">Nút "Thử" dùng đúng các giá trị đang nhập ở trên (tự lưu luôn) và hoạt động cả khi chưa tick bật nhích bánh xe.</div>

    <button class="btn-action" style="width: 100%; background: var(--ok); color: #000; font-weight: bold;" onclick="saveLife()">💾 Lưu hành vi</button>
    </div>

        <!-- Card: Chat & Lệnh -->
    <div class="card" id="chatCard">
      <div class="card-title">💬 Chat & Lệnh</div>
      <div id="chatMessages">
        <div class="chat-msg-sys">Nhập lệnh: "đi thẳng 2 giây", "dừng", "gật đầu", "phát nhạc", "dừng nhạc"...</div>
      </div>
      <div id="chatInputRow">
        <input type="text" id="chatInput" placeholder="Nhập lệnh (vd: phát nhạc, đi thẳng 2 giây)" />
        <button id="btnChatSend">Gửi</button>
      </div>
    </div>
  </div>

  <div class="log-box" id="logBox">System ready.</div>

    <script>
    // ===== Helpers =====
    const $ = id => document.getElementById(id);
    let macroState = { recording: false, playing: false, count: 0 };
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
    function toggleHintById(id) {
        const el = document.getElementById(id);
        if (el) {
            el.classList.toggle('lf-hint-hidden');
        }
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
        // ===== Hành vi "có sự sống" =====
    const LIFE_NUM = {
      life_nudge_speed: 'lfSpeed', life_nudge_ms: 'lfMs', life_nudge_cm: 'lfCm',
      life_max_drift_cm: 'lfDrift', life_nudge_gap_min_s: 'lfGapMin', life_nudge_gap_max_s: 'lfGapMax',
      life_idle_gap_min_s: 'lfIdleMin', life_idle_gap_max_s: 'lfIdleMax'
    };
    const LIFE_CHK = { life_head_talk: 'lfHead', life_listen_nod: 'lfListen', life_idle: 'lfIdle', life_wheel: 'lfWheel' };

    function loadLife(s) {
      if (!s || s.life_nudge_ms === undefined) { $('lifeCard').style.display = 'none'; return; }
      for (const k in LIFE_NUM) $(LIFE_NUM[k]).value = s[k];
      for (const k in LIFE_CHK) $(LIFE_CHK[k]).checked = !!s[k];
    }

    function lifePayload() {
      const p = {};
      for (const k in LIFE_NUM) p[k] = Number($(LIFE_NUM[k]).value);
      for (const k in LIFE_CHK) p[k] = $(LIFE_CHK[k]).checked ? 1 : 0;
      return p;
    }

    async function saveLife() {
      await apiPost('/api/settings', lifePayload());
    }

    async function testNudge(dir) {
      await apiPost('/api/settings', { ...lifePayload(), life_test: dir });
    }
    function confirmReboot() {
      if (confirm('Khởi động lại robot? Robot sẽ tạm thời mất kết nối trong vài giây.')) {
        sendCtrl('robot_reboot');
      }
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
          loadLife(s);
        }
      } catch (e) { log('INIT SETTINGS ERROR: ' + e.message); }

      try {
        refreshStatus();
        
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
          ws.send(JSON.stringify({ type: 'macro', cmd: 'list' }));
          refreshStatus();
          fetchMacroStatusOnce();
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
          } else if (msg.type === 'status') {
            // Badge "Trạng thái" — 3 màu: Sẵn sàng / Đang chạy / Lỗi
            if (typeof msg.wheel_ready === 'boolean') {
                const el = $('stWheel');
                const moving = (msg.left_speed !== 0 || msg.right_speed !== 0);

                if (!msg.wheel_ready) {
                    el.innerText = 'Lỗi';
                    el.style.color = '#ff5252';
                } else if (moving) {
                    el.innerText = 'Đang chạy';
                    el.style.color = '#00e676';
                } else {
                    el.innerText = 'Sẵn sàng';
                    el.style.color = '#00d2ff';
                }
            }

            if (typeof msg.left_speed === 'number' && typeof msg.right_speed === 'number') {
                $('stSpeed').innerText = `${msg.left_speed}/${msg.right_speed}`;
                if (!joystick || !joystick.active) {
                    const lEl = document.getElementById('joyL');
                    const rEl = document.getElementById('joyR');
                    if (lEl) lEl.innerText = msg.left_speed;
                    if (rEl) rEl.innerText = msg.right_speed;
                }
            }

            if (typeof msg.volume === 'number') {
                $('stVol').innerText = `${msg.volume}%`;
            }
          } else if (msg.type === 'motor') {
            // Cập nhật readout + speed bar (silent — không log)
            const lEl = document.getElementById('joyL');
            const rEl = document.getElementById('joyR');
            if (lEl) lEl.innerText = msg.left;
            if (rEl) rEl.innerText = msg.right;
          } else if (msg.type === 'macro_status') {
            macroState.recording = !!msg.recording;
            macroState.playing   = !!msg.playing;
            macroState.count     = msg.count || 0;
            renderMacroStatus();
          
          } else if (msg.type === 'macro_list') {
            macroSavedList = msg.items || [];
            updateMacroDropdown();
          } else if (msg.type === 'macro_reply') {
            log('macro reply: ' + msg.msg + (msg.ok ? ' OK' : ' FAIL'));
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
        // ===== Virtual Joystick =====
    function joyClamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

    class VirtualJoystick {
      constructor() {
        this.svg   = document.getElementById('joyBase');
        this.knob  = document.getElementById('joyKnob');
        if (!this.svg || !this.knob) return;

        this.radius    = 62;     
        this.deadzone  = 0.08;
        this.active    = false;
        this.pointerId = null;
        this.x = 0; this.y = 0;
        this.lastSendMs = 0;
        this.sendInterval = 50; 
        this.autoSendTimer = null;  

        this.bind();
      }

      bind() {
        this.svg.addEventListener('pointerdown',   e => this.onDown(e));
        this.svg.addEventListener('pointermove',   e => this.onMove(e));
        this.svg.addEventListener('pointerup',     e => this.onUp(e));
        this.svg.addEventListener('pointercancel', e => this.onUp(e));
      }

      toLocal(e) {
        const r = this.svg.getBoundingClientRect();
        const cx = r.left + r.width  / 2;
        const cy = r.top  + r.height / 2;
        const scale = 200 / r.width;      // viewBox = 200x200
        return { x: (e.clientX - cx) * scale,
                 y: (e.clientY - cy) * scale };
      }

      onDown(e) {
        if (this.active) return;
        this.active    = true;
        this.pointerId = e.pointerId;
        try { this.svg.setPointerCapture(e.pointerId); } catch(_) {}
        this.update(e);
        this.startAutoSend();  
        e.preventDefault();
      }

      onMove(e) {
        if (!this.active || e.pointerId !== this.pointerId) return;
        this.update(e);
        e.preventDefault();
      }

      onUp(e) {
        if (!this.active || e.pointerId !== this.pointerId) return;
        this.active    = false;
        this.pointerId = null;
        try { this.svg.releasePointerCapture(e.pointerId); } catch(_) {}
        this.stopAutoSend();
        this.reset();
      }

      update(e) {
        let p = this.toLocal(e);
        const mag = Math.hypot(p.x, p.y);
        if (mag > this.radius) {
          p.x = p.x / mag * this.radius;
          p.y = p.y / mag * this.radius;
        }
        // knob visual
        this.knob.setAttribute('cx', p.x);
        this.knob.setAttribute('cy', p.y);

        // normalize [-1, 1]
        let nx = p.x / this.radius;
        let ny = p.y / this.radius;

        // deadzone
        if (Math.abs(nx) < this.deadzone) nx = 0;
        if (Math.abs(ny) < this.deadzone) ny = 0;

        // y đảo dấu: kéo lên trên màn hình = tiến = y dương
        this.x =  nx;
        this.y = -ny;

        this.render();

        // throttle send
        const now = performance.now();
        if (now - this.lastSendMs >= this.sendInterval) {
          this.lastSendMs = now;
          this.send();
        }
      }

      reset() {
        this.x = 0; this.y = 0;
        this.knob.setAttribute('cx', 0);
        this.knob.setAttribute('cy', 0);
        this.render();
        this.send();   // gửi (0,0) ngay lập tức
      }
      startAutoSend() {
        if (this.autoSendTimer) return;
        this.autoSendTimer = setInterval(() => {
          if (this.active) this.send();
        }, 150);   // 10Hz — đủ để reset watchdog 300ms
      }

      stopAutoSend() {
        if (this.autoSendTimer) {
          clearInterval(this.autoSendTimer);
          this.autoSendTimer = null;
        }
      }

      send() {
        if (!ws || ws.readyState !== WebSocket.OPEN) return;
        ws.send(JSON.stringify({ type: 'vector', x: this.x, y: this.y }));
      }

      render() {
        const l = Math.round(joyClamp(this.y + this.x, -1, 1) * 100);
        const r = Math.round(joyClamp(this.y - this.x, -1, 1) * 100);
        const xEl = document.getElementById('joyX');
        const yEl = document.getElementById('joyY');
        const lEl = document.getElementById('joyL');
        const rEl = document.getElementById('joyR');
        if (xEl) xEl.innerText = this.x.toFixed(2);
        if (yEl) yEl.innerText = this.y.toFixed(2);
        if (lEl) lEl.innerText = l;
        if (rEl) rEl.innerText = r;
      }
    }

    let joystick = null;

    let macroPoller = null;

    function fetchMacroStatusOnce() {
      fetch('/api/macro/status')
        .then(r => r.json())
        .then(msg => {
          macroState.recording = !!msg.recording;
          macroState.playing   = !!msg.playing;
          macroState.count     = msg.count || 0;
          renderMacroStatus();
        })
        .catch(() => {});
    }

    function startMacroPolling() {
      if (macroPoller) return;
      macroPoller = setInterval(fetchMacroStatusOnce, 1000);
      fetchMacroStatusOnce();   // fetch ngay lập tức
    }

    function sendMacroCmd(cmd) {
      if (!ws || ws.readyState !== WebSocket.OPEN) {
        log('WS chưa sẵn sàng');
        return;
      }
      ws.send(JSON.stringify({ type: 'macro', cmd: cmd }));
      log('macro → ' + cmd);

      // Fetch state sau 150ms (server đã xử lý xong)
      setTimeout(fetchMacroStatusOnce, 150);

      // Bật polling khi vào chế độ active
      if (cmd === 'start_rec' || cmd === 'play') {
        startMacroPolling();
      } else if (cmd === 'stop_rec' || cmd === 'stop_play' || cmd === 'clear') {
        // Fetch vài lần để chắc chắn lấy state cuối, rồi tắt poll sau 2s
        setTimeout(fetchMacroStatusOnce, 500);
        setTimeout(() => {
          fetchMacroStatusOnce();
          if (macroPoller) { clearInterval(macroPoller); macroPoller = null; }
        }, 1500);
      }
    }

    function renderMacroStatus() {
      const st      = document.getElementById('macroStatus');
      const btnRec  = document.getElementById('btnMacroRec');
      const btnPlay = document.getElementById('btnMacroPlay');
      if (!st || !btnRec || !btnPlay) return;

      const { recording: rec, playing: play, count: n } = macroState;

      // Nút Record / Stop toggle
      if (rec) {
        btnRec.textContent = '■ Dừng ghi';
        btnRec.classList.add('active');
      } else {
        btnRec.textContent = '● Bắt đầu ghi';
        btnRec.classList.remove('active');
      }

      // Nút Play
      btnPlay.textContent = play ? '■ Dừng phát' : '▶ Phát lại';
      btnPlay.classList.toggle('active', play);
      // Disable Play khi đang record hoặc chưa có macro
      btnPlay.disabled = rec || (!play && n === 0);

      // Status
      if (rec) {
        st.textContent = '● Đang ghi... (' + n + ' bước)';
        st.className = 'macro-status rec';
      } else if (play) {
        st.textContent = '▶ Đang phát... (' + n + ' bước)';
        st.className = 'macro-status play';
      } else if (n > 0) {
        st.textContent = '✓ Đã lưu ' + n + ' bước';
        st.className = 'macro-status';
      } else {
        st.textContent = 'Chưa có macro';
        st.className = 'macro-status';
      }
    }
    
    let macroSavedList = [];

    function updateMacroDropdown() {
      const sel = document.getElementById('macroSavedList');
      if (!sel) return;
      const cur = sel.value;
      sel.innerHTML = '';
      if (macroSavedList.length === 0) {
        const o = document.createElement('option');
        o.value = ''; o.textContent = '-- Chưa có macro nào --';
        sel.appendChild(o);
        return;
      }
      macroSavedList.forEach(n => {
        const o = document.createElement('option');
        o.value = n; o.textContent = n;
        sel.appendChild(o);
      });
      if (macroSavedList.includes(cur)) sel.value = cur;
    }

    function sendMacroSave() {
      const inp = document.getElementById('macroNameInput');
      const name = (inp.value || '').trim();
      if (!name) { log('Nhập tên macro trước'); return; }
      if (!/^[a-zA-Z0-9_-]+$/.test(name)) {
        log('Tên chỉ cho phép chữ, số, _, -'); return;
      }
      ws.send(JSON.stringify({ type: 'macro', cmd: 'save', name }));
      log('macro → save ' + name);
    }

    function sendMacroLoad() {
      const sel = document.getElementById('macroSavedList');
      const name = sel.value;
      if (!name) { log('Chọn macro để tải'); return; }
      ws.send(JSON.stringify({ type: 'macro', cmd: 'load', name }));
      log('macro → load ' + name);
    }

    function sendMacroDelete() {
      const sel = document.getElementById('macroSavedList');
      const name = sel.value;
      if (!name) { log('Chọn macro để xoá'); return; }
      if (!confirm('Xoá file macro "' + name + '"?')) return;
      ws.send(JSON.stringify({ type: 'macro', cmd: 'delete', name }));
      log('macro → delete ' + name);
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
      try {
        joystick = new VirtualJoystick();
        log('VirtualJoystick ready');
      } catch (e) {
        log('Joystick init error: ' + e.message);
      }
      // Mode toggle D-pad / Joystick
      const dpadEl   = document.querySelector('.dpad-container');
      const joyEl    = document.querySelector('.joystick-wrap');
      const readEl   = document.querySelector('.joy-readout');

      function applyMode(mode) {
        if (!dpadEl || !joyEl) return;
        dpadEl.classList.toggle('hidden', mode === 'joystick');
        joyEl.classList.toggle('hidden',  mode === 'dpad');
        if (readEl) readEl.classList.toggle('hidden', mode === 'dpad');
        localStorage.setItem('joyMode', mode);
      }

      document.querySelectorAll('input[name="joyMode"]').forEach(r => {
        r.addEventListener('change', e => applyMode(e.target.value));
      });

      const savedMode = localStorage.getItem('joyMode') || 'both';
      applyMode(savedMode);
      const savedRadio = document.querySelector(`input[name="joyMode"][value="${savedMode}"]`);
      if (savedRadio) savedRadio.checked = true;
            // Macro buttons
      const btnRec   = document.getElementById('btnMacroRec');
      const btnPlay  = document.getElementById('btnMacroPlay');
      const btnClear = document.getElementById('btnMacroClear');

      if (btnRec) btnRec.addEventListener('click', () => {
        if (macroState.recording) sendMacroCmd('stop_rec');
        else                       sendMacroCmd('start_rec');
      });

      if (btnPlay) btnPlay.addEventListener('click', () => {
        if (macroState.playing) sendMacroCmd('stop_play');
        else                     sendMacroCmd('play');
      });

      if (btnClear) btnClear.addEventListener('click', () => sendMacroCmd('clear'));

      renderMacroStatus();

      const btnMSave = document.getElementById('btnMacroSave');
      const btnMLoad = document.getElementById('btnMacroLoad');
      const btnMDel  = document.getElementById('btnMacroDelete');
      if (btnMSave) btnMSave.addEventListener('click', sendMacroSave);
      if (btnMLoad) btnMLoad.addEventListener('click', sendMacroLoad);
      if (btnMDel)  btnMDel.addEventListener('click',  sendMacroDelete);
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
  self->current_left_speed_  = 0;
  self->current_right_speed_ = 0;
  self->BroadcastStatusNow();
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
    httpd_uri_t macro_status_uri = {
        .uri      = "/api/macro/status",
        .method   = HTTP_GET,
        .handler  = &RobotWebUiServer::ApiMacroStatusHandler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(server_handle_, &macro_status_uri);

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
        // === PHASE 3: status broadcast task ===
    BaseType_t st_ok = xTaskCreatePinnedToCore(
        &RobotWebUiServer::StatusBroadcastTaskEntry,
        "ws_status",
        4096,
        this,
        tskIDLE_PRIORITY + 1,
        &status_broadcast_task_,
        0);
    if (st_ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create ws_status task");
        status_broadcast_task_ = nullptr;
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

void RobotWebUiServer::BuildStatusJson(std::string& out, bool include_type) {
    int left_speed  = current_left_speed_;
    int right_speed = current_right_speed_;
    bool wheel_ready = (wheel_motor_ != nullptr) && wheel_motor_->IsReady();

    int move_speed, turn_speed, step_ms;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        move_speed = config_.move_speed;
        turn_speed = config_.turn_speed;
        step_ms    = config_.step_ms;
    }

    int volume = 0;
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec != nullptr) {
        volume = codec->output_volume();
    }

    cJSON* root = cJSON_CreateObject();
    if (include_type) {
        cJSON_AddStringToObject(root, "type", "status");
    }
    cJSON_AddBoolToObject(root,   "ok",           true);
    cJSON_AddBoolToObject(root,   "wheel_ready",  wheel_ready);
    cJSON_AddNumberToObject(root, "left_speed",   left_speed);
    cJSON_AddNumberToObject(root, "right_speed",  right_speed);
    cJSON_AddNumberToObject(root, "move_speed",   move_speed);
    cJSON_AddNumberToObject(root, "turn_speed",   turn_speed);
    cJSON_AddNumberToObject(root, "step_ms",      step_ms);
    cJSON_AddNumberToObject(root, "volume",       volume);

    char* json_text = cJSON_PrintUnformatted(root);
    out = json_text ? json_text : "{}";
    if (json_text != nullptr) {
        cJSON_free(json_text);
    }
    cJSON_Delete(root);
}

esp_err_t RobotWebUiServer::HandleStatus(httpd_req_t* req) {
    std::string payload;
    BuildStatusJson(payload, /*include_type=*/false);
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
    if (life_behavior_ != nullptr) {
        life_behavior_->AppendConfigToJson(root);
    }

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
    if (life_behavior_ != nullptr) {
        life_behavior_->ApplyConfigFromJson(root);
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
        // === He thong: goi/ngat tro chuyen va khoi dong lai (xu ly som, khong ghi vao macro) ===
    if (action == "robot_call") {
        cJSON_Delete(root);
        Application::GetInstance().ToggleChatState();
        SendJson(req, 200, "{\"ok\":true,\"message\":\"Da gui lenh goi/ngat robot\"}");
        return ESP_OK;
    }
    if (action == "robot_reboot") {
        cJSON_Delete(root);
        SendJson(req, 200, "{\"ok\":true,\"message\":\"Robot dang khoi dong lai\"}");
        ESP_LOGW(TAG, "Reboot requested from WebUI");
        Application::GetInstance().Schedule([]() {
            vTaskDelay(pdMS_TO_TICKS(500));
            Application::GetInstance().Reboot();
        });
        return ESP_OK;
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

    auto* duration_json = cJSON_GetObjectItem(root, "duration_ms");
    int duration_ms =
        cJSON_IsNumber(duration_json) ? Clamp(duration_json->valueint, 80, 5000) : step_ms;

    bool handled = false;

        if (wheel_motor_ != nullptr) {
        if (action == "forward") {
            wheel_motor_->Forward(move_speed);
            current_left_speed_  = move_speed;
            current_right_speed_ = move_speed;
            handled = true;
        } else if (action == "backward") {
            wheel_motor_->Backward(move_speed);
            current_left_speed_  = -move_speed;
            current_right_speed_ = -move_speed;
            handled = true;
        } else if (action == "left") {
            wheel_motor_->TurnLeft(turn_speed);
            current_left_speed_  = -turn_speed;
            current_right_speed_ = turn_speed;
            handled = true;
        } else if (action == "right") {
            wheel_motor_->TurnRight(turn_speed);
            current_left_speed_  = turn_speed;
            current_right_speed_ = -turn_speed;
            handled = true;
        } else if (action == "stop") {
            wheel_motor_->Stop();
            current_left_speed_  = 0;
            current_right_speed_ = 0;
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

    // MACRO record
    if (handled) {
        std::lock_guard<std::mutex> lock(macro_mutex_);
        if (macro_recording_) {
            uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            uint32_t delay = now - macro_last_step_ms_;
            if (recorded_macro_.size() < kMacroMaxSteps &&
                (now - macro_start_ms_) < kMacroMaxDurationMs) {
                MacroStep s;
                s.action = action;
                s.p1 = (action == "forward" || action == "backward") ? move_speed : turn_speed;
                s.p2 = duration_ms;
                s.delay_ms = delay;
                recorded_macro_.push_back(std::move(s));
                macro_last_step_ms_ = now;
            }
        }
    }

    cJSON_Delete(root);

    if (!handled) {
        SendJson(req, 400,
                 "{\"ok\":false,\"message\":\"Unknown action or unavailable controller\"}");
        return ESP_OK;
    }
    if (handled) {
        BroadcastStatusNow();
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
        last_left_speed_  = -999;
        last_right_speed_ = -999;
        last_volume_      = -999;
        last_wheel_ready_ = false;
        WsSendTextToReq(req, "{\"type\":\"hello\",\"server\":\"robot-webui\",\"proto\":1}");
        BroadcastMacroStatus();
        BroadcastMacroList();
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
    } else if (t == "vector") {
        HandleVectorCommand(fd, tmp, len);
    } else if (t == "macro") {
        HandleMacroCommand(fd, tmp, len);
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
            // WsRemoveClient(fd);
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
// PHASE 3: Status broadcast — poll 500ms, chỉ push khi thay đổi
// ============================================================

void RobotWebUiServer::StatusBroadcastTaskEntry(void* arg) {
    static_cast<RobotWebUiServer*>(arg)->StatusBroadcastLoop();
}

void RobotWebUiServer::StatusBroadcastLoop() {
    const TickType_t period = pdMS_TO_TICKS(kStatusPollMs);
    TickType_t last_wake = xTaskGetTickCount();
    std::string json;

    while (true) {
        vTaskDelayUntil(&last_wake, period);

        if (GetWsClientCount() == 0) continue;

        int left  = current_left_speed_;
        int right = current_right_speed_;
        bool ready = (wheel_motor_ != nullptr) && wheel_motor_->IsReady();

        int vol = 0;
        auto codec = Board::GetInstance().GetAudioCodec();
        if (codec != nullptr) {
            vol = codec->output_volume();
        }

        // Chỉ broadcast khi có thay đổi
        if (left  == last_left_speed_  &&
            right == last_right_speed_ &&
            vol   == last_volume_      &&
            ready == last_wheel_ready_) {
            continue;
        }

        last_left_speed_  = left;
        last_right_speed_ = right;
        last_volume_      = vol;
        last_wheel_ready_ = ready;

        json.clear();
        BuildStatusJson(json, /*include_type=*/true);
        BroadcastJson(json);

        // ─── Macro status dirty check ───
        bool rec, play;
        size_t mcount;
        {
            std::lock_guard<std::mutex> lock(macro_mutex_);
            rec = macro_recording_;
            play = macro_playing_;
            mcount = recorded_macro_.size();
        }
        if ((int)mcount != last_macro_count_ ||
            rec          != last_macro_rec_   ||
            play         != last_macro_play_) {
            last_macro_count_ = (int)mcount;
            last_macro_rec_   = rec;
            last_macro_play_  = play;

            char mbuf[128];
            snprintf(mbuf, sizeof(mbuf),
                     "{\"type\":\"macro_status\",\"recording\":%s,"
                     "\"playing\":%s,\"count\":%u}",
                     rec  ? "true" : "false",
                     play ? "true" : "false",
                     (unsigned)mcount);
            BroadcastJson(mbuf);
        }
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
    // ====== STOP NHAC (phai dat TRUOC nhanh STOP chung, vi "dung nhac" cung chua chu "dung") ======
    if (Contains(lower, "dung nhac") || Contains(lower, "dừng nhạc") ||
        Contains(lower, "ngung nhac") || Contains(lower, "ngừng nhạc") ||
        Contains(lower, "stop nhac") || Contains(lower, "tắt nhạc") ||
        Contains(lower, "tat nhac")) {
        auto& app = Application::GetInstance();
        bool ok = app.StopSdCardMusic();
        ESP_LOGI(TAG, "stop music (chat) StopSdCardMusic=%d", ok);
        tool = "self.webui.music";
        reply = ok ? "Đã dừng nhạc" : "Không dừng được";
        handled = true;
    }

    // ====== STOP (ưu tiên cao) ======
    else if (Contains(lower, "dung") || Contains(lower, "dừng") || Contains(lower, "stop") ||
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
    {
        std::lock_guard<std::mutex> lock(macro_mutex_);
        if (macro_recording_) {
            uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            uint32_t delay = now - macro_last_step_ms_;
            if (recorded_macro_.size() < kMacroMaxSteps &&
                (now - macro_start_ms_) < kMacroMaxDurationMs) {
                MacroStep s;
                s.action = "chat";
                s.text   = text;
                s.delay_ms = delay;
                recorded_macro_.push_back(std::move(s));
                macro_last_step_ms_ = now;
            }
        }
    }
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
void RobotWebUiServer::BroadcastStatusNow() {
    if (GetWsClientCount() == 0) return;
    std::string json;
    BuildStatusJson(json, /*include_type=*/true);
    BroadcastJson(json);
}

void RobotWebUiServer::SetSpeedsAndBroadcast(int l, int r) {
    current_left_speed_  = l;
    current_right_speed_ = r;
    BroadcastStatusNow();
}

// ============================================================
// JOYSTICK: vector (x, y) → differential drive
// ============================================================

void RobotWebUiServer::HandleVectorCommand(int fd, const char* json_text, size_t len) {
    cJSON* root = cJSON_Parse(json_text);
    if (root == nullptr) return;

    cJSON* x_item = cJSON_GetObjectItem(root, "x");
    cJSON* y_item = cJSON_GetObjectItem(root, "y");
    if (!cJSON_IsNumber(x_item) || !cJSON_IsNumber(y_item)) {
        cJSON_Delete(root);
        return;
    }

    float x = static_cast<float>(x_item->valuedouble);
    float y = static_cast<float>(y_item->valuedouble);
    cJSON_Delete(root);

    // Clamp về [-1, 1]
    x = std::max(-1.0f, std::min(1.0f, x));
    y = std::max(-1.0f, std::min(1.0f, y));

    // RC override → từ chối (rate-limited warning 2s/lần)
    if (rc_sbus::IsOverrideActive()) {
        static uint32_t s_last_warn_ms = 0;
        uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        if (now_ms - s_last_warn_ms > 2000) {
            s_last_warn_ms = now_ms;
            WsSendTextToFd(fd,
                "{\"type\":\"warning\",\"msg\":\"RC override active\"}");
        }
        return;
    }

    // Differential drive: left = y + x, right = y - x
    float lf = y + x;
    float rf = y - x;
    lf = std::max(-1.0f, std::min(1.0f, lf));
    rf = std::max(-1.0f, std::min(1.0f, rf));

    int l100 = static_cast<int>(lf * 100.0f);
    int r100 = static_cast<int>(rf * 100.0f);

    if (wheel_motor_ != nullptr) {
        wheel_motor_->SetSpeed(l100, r100);

        // Cập nhật software-tracked speeds (để BuildStatusJson phản ánh đúng)
        current_left_speed_  = l100;
        current_right_speed_ = r100;

        // Watchdog: nếu 300ms không có vector mới → auto-stop
        ArmMotionStopTimer(800);
        BroadcastStatusNow();
    }

    // Broadcast cho mọi client (multi-tab sync + hiển thị speed bar)
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"motor\",\"left\":%d,\"right\":%d,\"source\":\"joystick\"}",
             l100, r100);
    BroadcastJson(buf);
    {
        static uint32_t s_last_sample_ms = 0;
        static float    s_last_x = 999.0f, s_last_y = 999.0f;

        std::lock_guard<std::mutex> lock(macro_mutex_);
        if (macro_recording_) {
            uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            bool big_change = (std::fabs(x - s_last_x) > 0.1f) ||
                              (std::fabs(y - s_last_y) > 0.1f);
            bool time_due   = (now - s_last_sample_ms) >= 200;

            if ((big_change || time_due) &&
                recorded_macro_.size() < kMacroMaxSteps &&
                (now - macro_start_ms_) < kMacroMaxDurationMs) {
                MacroStep s;
                s.action = "vector";
                s.p1 = static_cast<int>(x * 100.0f);
                s.p2 = static_cast<int>(y * 100.0f);
                s.delay_ms = now - macro_last_step_ms_;
                recorded_macro_.push_back(std::move(s));
                macro_last_step_ms_ = now;
                s_last_sample_ms = now;
                s_last_x = x;
                s_last_y = y;
            }
        }
    }
}
void RobotWebUiServer::HandleMacroCommand(int fd, const char* json_text, size_t len) {
    cJSON* root = cJSON_Parse(json_text);
    if (root == nullptr) return;

    cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd) || cmd->valuestring == nullptr) {
        cJSON_Delete(root);
        return;
    }

    std::string c = cmd->valuestring;
    cJSON_Delete(root);

    if (c == "start_rec") {
        bool did_start = false;
        {
            std::lock_guard<std::mutex> lock(macro_mutex_);
            if (macro_recording_) return;
            recorded_macro_.clear();
            macro_recording_ = true;
            macro_start_ms_  = static_cast<uint32_t>(esp_timer_get_time() / 1000);
            macro_last_step_ms_ = macro_start_ms_;
            did_start = true;
        }
        if (did_start) {
            ESP_LOGI(TAG, "Macro recording START");
            // BroadcastMacroStatus();
        }

    } else if (c == "stop_rec") {
        size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(macro_mutex_);
            macro_recording_ = false;
            count = recorded_macro_.size();
        }
        // Force stop motor (đảm bảo robot dừng khi ngừng ghi)
        if (wheel_motor_ != nullptr) {
            wheel_motor_->Stop();
            current_left_speed_ = 0;
            current_right_speed_ = 0;
            BroadcastStatusNow();
        }
        ESP_LOGI(TAG, "Macro recording STOP (%u steps)", (unsigned)count);
        // BroadcastMacroStatus();

    } else if (c == "play") {
        bool can_play = false;
        {
            std::lock_guard<std::mutex> lock(macro_mutex_);
            if (!macro_playing_ && !macro_recording_ && !recorded_macro_.empty()) {
                macro_playing_ = true;
                macro_abort_   = false;
                can_play = true;
            }
        }
        if (can_play && macro_player_task_ == nullptr) {
            BaseType_t ok = xTaskCreatePinnedToCore(
                &RobotWebUiServer::MacroPlayerTaskEntry,
                "macro_play",
                4096,
                this,
                tskIDLE_PRIORITY + 2,
                &macro_player_task_,
                0);
            if (ok != pdPASS) {
                std::lock_guard<std::mutex> lock(macro_mutex_);
                macro_playing_ = false;
                macro_player_task_ = nullptr;
                ESP_LOGE(TAG, "Failed to create macro_play task");
            } else {
                ESP_LOGI(TAG, "Macro playback START");
                // BroadcastMacroStatus();
            }
        }

    } else if (c == "stop_play") {
        {
            std::lock_guard<std::mutex> lock(macro_mutex_);
            macro_abort_   = true;
            macro_playing_ = false;   // ★ reset ngay, không chờ task cleanup
        }
        if (wheel_motor_ != nullptr) {
            wheel_motor_->Stop();
            current_left_speed_ = 0;
            current_right_speed_ = 0;
            BroadcastStatusNow();
        }
        ESP_LOGI(TAG, "Macro playback ABORT requested");

    } else if (c == "clear") {
        bool did_clear = false;
        {
            std::lock_guard<std::mutex> lock(macro_mutex_);
            if (!macro_playing_ && !macro_recording_) {
                recorded_macro_.clear();
                did_clear = true;
            }
        }
        if (did_clear) {
            // BroadcastMacroStatus();
        }
    } else if (c == "save" || c == "load" || c == "delete") {
        // Parse lại JSON để lấy name (đã bị xoá ở trên)
        cJSON* root2 = cJSON_Parse(json_text);
        if (!root2) return;
        if      (c == "save")   HandleMacroSave(fd, root2);
        else if (c == "load")   HandleMacroLoad(fd, root2);
        else if (c == "delete") HandleMacroDelete(fd, root2);
        cJSON_Delete(root2);
    } else if (c == "list") {
        HandleMacroList(fd);
    
    }
}

void RobotWebUiServer::RecordMacroStep(const char* action, int p1, int p2,
                                       uint32_t delay_ms, const char* text) {
    std::lock_guard<std::mutex> lock(macro_mutex_);
    if (!macro_recording_) return;

    uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (recorded_macro_.size() >= kMacroMaxSteps) return;
    if ((now - macro_start_ms_) >= kMacroMaxDurationMs) return;

    MacroStep s;
    s.action   = action ? action : "";
    s.p1       = p1;
    s.p2       = p2;
    s.delay_ms = delay_ms;
    s.text     = text ? text : "";
    recorded_macro_.push_back(std::move(s));
    macro_last_step_ms_ = now;
}

void RobotWebUiServer::BroadcastMacroStatus() {
    if (GetWsClientCount() == 0) return;

    bool rec, play;
    size_t count;
    {
        std::lock_guard<std::mutex> lock(macro_mutex_);
        rec = macro_recording_;
        play = macro_playing_;
        count = recorded_macro_.size();
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"type\":\"macro_status\",\"recording\":%s,\"playing\":%s,\"count\":%u}",
             rec ? "true" : "false",
             play ? "true" : "false",
             (unsigned)count);
    BroadcastJson(buf);
}

void RobotWebUiServer::MacroPlayerTaskEntry(void* arg) {
    static_cast<RobotWebUiServer*>(arg)->MacroPlayerTaskEntryWrapper();
}

void RobotWebUiServer::MacroPlayerTaskEntryWrapper() {
    // Snapshot để không giữ lock suốt quá trình playback
    std::vector<MacroStep> steps;
    {
        std::lock_guard<std::mutex> lock(macro_mutex_);
        steps = recorded_macro_;
    }

    ESP_LOGI(TAG, "Macro playback: %u steps", (unsigned)steps.size());

    for (size_t i = 0; i < steps.size(); ++i) {
        // Delay
        if (steps[i].delay_ms > 0) {
            uint32_t remain = steps[i].delay_ms;
            while (remain > 0) {
                uint32_t chunk = remain > 50 ? 50 : remain;
                vTaskDelay(pdMS_TO_TICKS(chunk));
                remain -= chunk;
                if (macro_abort_) break;
            }
        }

        bool abort = false;
        {
            std::lock_guard<std::mutex> lock(macro_mutex_);
            abort = macro_abort_;
        }
        if (abort) break;

        ExecuteMacroStep(steps[i]);
    }

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(macro_mutex_);
        macro_playing_      = false;
        macro_abort_        = false;
        macro_player_task_  = nullptr;   // ★ QUAN TRỌNG: cho phép play lần sau
    }

    if (wheel_motor_ != nullptr) {
        wheel_motor_->Stop();
        current_left_speed_  = 0;
        current_right_speed_ = 0;
        BroadcastStatusNow();
    }

    ESP_LOGI(TAG, "Macro playback DONE");
    BroadcastMacroStatus();

    vTaskDelete(nullptr);
    // Không reset handle ở đây vì đã là task tự xoá
}

void RobotWebUiServer::ExecuteMacroStep(const MacroStep& s) {
    if (s.action == "forward" && wheel_motor_) {
        wheel_motor_->Forward(s.p1 > 0 ? s.p1 : 60);
        ArmMotionStopTimer(s.p2 > 0 ? s.p2 : 350);
        current_left_speed_  = s.p1;
        current_right_speed_ = s.p1;
    } else if (s.action == "backward" && wheel_motor_) {
        wheel_motor_->Backward(s.p1 > 0 ? s.p1 : 60);
        ArmMotionStopTimer(s.p2 > 0 ? s.p2 : 350);
        current_left_speed_  = -s.p1;
        current_right_speed_ = -s.p1;
    } else if (s.action == "left" && wheel_motor_) {
        wheel_motor_->TurnLeft(s.p1 > 0 ? s.p1 : 55);
        ArmMotionStopTimer(s.p2 > 0 ? s.p2 : 350);
    } else if (s.action == "right" && wheel_motor_) {
        wheel_motor_->TurnRight(s.p1 > 0 ? s.p1 : 55);
        ArmMotionStopTimer(s.p2 > 0 ? s.p2 : 350);
    } else if (s.action == "stop" && wheel_motor_) {
        wheel_motor_->Stop();
        current_left_speed_  = 0;
        current_right_speed_ = 0;
    } else if (s.action == "head_nod" && head_servo_) {
        head_servo_->nodYes();
    } else if (s.action == "head_shake" && head_servo_) {
        head_servo_->shakeNo();
    } else if (s.action == "head_curious" && head_servo_) {
        head_servo_->curiousHeadTilt();
    } else if (s.action == "head_center" && head_servo_) {
        head_servo_->RunAction("center");
    } else if (s.action == "vector" && wheel_motor_) {
        int l = s.p2 + s.p1;
        int r = s.p2 - s.p1;
        l = std::max(-100, std::min(100, l));
        r = std::max(-100, std::min(100, r));
        wheel_motor_->SetSpeed(l, r);
        current_left_speed_  = l;
        current_right_speed_ = r;
        ArmMotionStopTimer(300);
    } else if (s.action == "music_vuive") {
        Application::GetInstance().PlaySdCardMusicByMood("vuive");
    } else if (s.action == "music_buon") {
        Application::GetInstance().PlaySdCardMusicByMood("buon");
    } else if (s.action == "music_stop") {
        Application::GetInstance().StopSdCardMusic();
    } else if (s.action == "music_next") {
        Application::GetInstance().PlayNextSdCardMusic();
    } else if (s.action == "chat") {
        // Replay chat command bằng cách gọi HandleChat internal
        // Cần tạo JSON tạm
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "{\"text\":\"%s\"}", s.text.c_str());
        HandleChat(0, tmp, strlen(tmp));
    }

    BroadcastStatusNow();
}
esp_err_t RobotWebUiServer::ApiMacroStatusHandler(httpd_req_t* req) {
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleMacroStatus(req);
}

esp_err_t RobotWebUiServer::HandleMacroStatus(httpd_req_t* req) {
    bool rec, play;
    size_t count;
    {
        std::lock_guard<std::mutex> lock(macro_mutex_);
        rec   = macro_recording_;
        play  = macro_playing_;
        count = recorded_macro_.size();
    }
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"recording\":%s,\"playing\":%s,\"count\":%u}",
             rec  ? "true" : "false",
             play ? "true" : "false",
             (unsigned)count);
    SendJson(req, 200, buf);
    return ESP_OK;
}
// ============================================================
// PHASE 6c: Persistence SD — save/load/delete/list macros
// ============================================================

bool RobotWebUiServer::IsValidMacroName(const std::string& name) {
    if (name.empty() || name.size() > kMacroMaxNameLen) return false;
    for (char c : name) {
        // Chỉ cho phép chữ, số, gạch dưới, gạch ngang
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

std::string RobotWebUiServer::MacroFilePath(const std::string& name) {
    return std::string(kMacroDir) + "/" + name + ".json";
}

bool RobotWebUiServer::SaveMacroToSd(const std::string& name) {
    if (!IsValidMacroName(name)) return false;

    std::vector<MacroStep> steps;
    {
        std::lock_guard<std::mutex> lock(macro_mutex_);
        if (recorded_macro_.empty()) return false;
        steps = recorded_macro_;
    }

    // Đảm bảo thư mục tồn tại
    mkdir(kMacroDir, 0755);   // EEXIST OK

    std::string path = MacroFilePath(name);
    FILE* fp = fopen(path.c_str(), "w");
    if (!fp) {
        ESP_LOGW(TAG, "Cannot open for write: %s", path.c_str());
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name.c_str());
    cJSON_AddNumberToObject(root, "created_ms",
        static_cast<double>(esp_timer_get_time() / 1000));

    cJSON* arr = cJSON_AddArrayToObject(root, "steps");
    for (const auto& s : steps) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "action", s.action.c_str());
        cJSON_AddNumberToObject(o, "p1", s.p1);
        cJSON_AddNumberToObject(o, "p2", s.p2);
        cJSON_AddNumberToObject(o, "delay_ms", s.delay_ms);
        if (!s.text.empty()) {
            cJSON_AddStringToObject(o, "text", s.text.c_str());
        }
        cJSON_AddItemToArray(arr, o);
    }

    char* txt = cJSON_PrintUnformatted(root);
    bool ok = false;
    if (txt != nullptr) {
        size_t len = strlen(txt);
        size_t w = fwrite(txt, 1, len, fp);
        ok = (w == len);
        cJSON_free(txt);
    }
    cJSON_Delete(root);
    fclose(fp);

    ESP_LOGI(TAG, "Macro save '%s': %s (%u steps)",
             name.c_str(), ok ? "OK" : "FAIL", (unsigned)steps.size());
    return ok;
}

bool RobotWebUiServer::LoadMacroFromSd(const std::string& name) {
    if (!IsValidMacroName(name)) {
        ESP_LOGW(TAG, "Load '%s': invalid name", name.c_str());
        return false;
    }

    std::string path = MacroFilePath(name);
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) {
        ESP_LOGW(TAG, "Load '%s': cannot open file", path.c_str());
        return false;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz <= 0 || sz > 64 * 1024) {
        ESP_LOGW(TAG, "Load '%s': invalid size %ld", name.c_str(), sz);
        fclose(fp);
        return false;
    }
    fseek(fp, 0, SEEK_SET);
    std::vector<char> buf(sz + 1);
    size_t r = fread(buf.data(), 1, sz, fp);
    fclose(fp);
    buf[r] = '\0';

    cJSON* root = cJSON_Parse(buf.data());
    if (!root) {
        ESP_LOGW(TAG, "Load '%s': JSON parse fail (len=%u)", name.c_str(), (unsigned)r);
        return false;
    }

    cJSON* arr = cJSON_GetObjectItem(root, "steps");
    if (!cJSON_IsArray(arr)) {
        ESP_LOGW(TAG, "Load '%s': no steps array", name.c_str());
        cJSON_Delete(root);
        return false;
    }

    std::vector<MacroStep> new_steps;
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && new_steps.size() < kMacroMaxSteps; ++i) {
        cJSON* o = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(o)) continue;

        MacroStep s;
        cJSON* a = cJSON_GetObjectItem(o, "action");
        cJSON* p1 = cJSON_GetObjectItem(o, "p1");
        cJSON* p2 = cJSON_GetObjectItem(o, "p2");
        cJSON* d  = cJSON_GetObjectItem(o, "delay_ms");
        cJSON* t  = cJSON_GetObjectItem(o, "text");

        s.action   = cJSON_IsString(a) ? a->valuestring : "";
        s.p1       = cJSON_IsNumber(p1) ? p1->valueint : 0;
        s.p2       = cJSON_IsNumber(p2) ? p2->valueint : 0;
        s.delay_ms = cJSON_IsNumber(d)  ? (uint32_t)d->valueint : 0;
        s.text     = cJSON_IsString(t)  ? t->valuestring : "";
        new_steps.push_back(std::move(s));
    }
    cJSON_Delete(root);

    size_t loaded = new_steps.size();
    {
        std::lock_guard<std::mutex> lock(macro_mutex_);
        if (macro_recording_) {
            ESP_LOGW(TAG, "Load '%s': busy (recording)", name.c_str());
            return false;
        }
        if (macro_playing_) {
            ESP_LOGW(TAG, "Load '%s': busy (playing)", name.c_str());
            return false;
        }
        recorded_macro_   = std::move(new_steps);
        last_macro_count_ = -1;
    }

    ESP_LOGI(TAG, "Macro load '%s': %u steps", name.c_str(), (unsigned)loaded);
    return true;
}

bool RobotWebUiServer::DeleteMacroFromSd(const std::string& name) {
    if (!IsValidMacroName(name)) return false;
    std::string path = MacroFilePath(name);
    int ret = remove(path.c_str());
    ESP_LOGI(TAG, "Macro delete '%s': %s",
             name.c_str(), ret == 0 ? "OK" : "FAIL");
    return ret == 0;
}

std::vector<std::string> RobotWebUiServer::ListMacrosOnSd() {
    std::vector<std::string> out;
    DIR* d = opendir(kMacroDir);
    if (d == nullptr) return out;

    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        std::string n = e->d_name;
        if (n.size() > 5 && n.substr(n.size() - 5) == ".json") {
            out.push_back(n.substr(0, n.size() - 5));
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

void RobotWebUiServer::HandleMacroSave(int fd, cJSON* root) {
    cJSON* name = cJSON_GetObjectItem(root, "name");
    if (!cJSON_IsString(name) || name->valuestring == nullptr) return;

    bool ok = SaveMacroToSd(name->valuestring);

    char reply[128];
    snprintf(reply, sizeof(reply),
             "{\"type\":\"macro_reply\",\"ok\":%s,\"msg\":\"save %s\"}",
             ok ? "true" : "false", name->valuestring);
    WsSendTextToFd(fd, reply);

    if (ok) BroadcastMacroList();
}

void RobotWebUiServer::HandleMacroLoad(int fd, cJSON* root) {
    cJSON* name = cJSON_GetObjectItem(root, "name");
    if (!cJSON_IsString(name) || name->valuestring == nullptr) return;

    bool ok = LoadMacroFromSd(name->valuestring);

    char reply[128];
    snprintf(reply, sizeof(reply),
             "{\"type\":\"macro_reply\",\"ok\":%s,\"msg\":\"load %s\"}",
             ok ? "true" : "false", name->valuestring);
    WsSendTextToFd(fd, reply);

    BroadcastMacroStatus();
}

void RobotWebUiServer::HandleMacroDelete(int fd, cJSON* root) {
    cJSON* name = cJSON_GetObjectItem(root, "name");
    if (!cJSON_IsString(name) || name->valuestring == nullptr) return;

    bool ok = DeleteMacroFromSd(name->valuestring);

    char reply[128];
    snprintf(reply, sizeof(reply),
             "{\"type\":\"macro_reply\",\"ok\":%s,\"msg\":\"delete %s\"}",
             ok ? "true" : "false", name->valuestring);
    WsSendTextToFd(fd, reply);

    if (ok) BroadcastMacroList();
}

void RobotWebUiServer::HandleMacroList(int fd) {
    auto names = ListMacrosOnSd();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "macro_list");
    cJSON* arr = cJSON_AddArrayToObject(root, "items");
    for (const auto& n : names) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(n.c_str()));
    }
    char* txt = cJSON_PrintUnformatted(root);
    if (txt) {
        WsSendTextToFd(fd, txt);
        cJSON_free(txt);
    }
    cJSON_Delete(root);
}

void RobotWebUiServer::BroadcastMacroList() {
    if (GetWsClientCount() == 0) return;
    auto names = ListMacrosOnSd();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "macro_list");
    cJSON* arr = cJSON_AddArrayToObject(root, "items");
    for (const auto& n : names) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(n.c_str()));
    }
    char* txt = cJSON_PrintUnformatted(root);
    if (txt) {
        BroadcastJson(txt);
        cJSON_free(txt);
    }
    cJSON_Delete(root);
}