#include "robot_web_ui_server.h"
#include "rc_sbus_controller.h"
#include "ws_log_broadcaster.h"
#include "web_auth.h"

#include "application.h"
#include "board.h"
#include "settings.h"
#include "robot_life_behavior.h"
#include "robot_sensor_hub.h"
#include "robot_hardware.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_image_format.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
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
<html lang="vi" data-theme="dark" data-theme-pref="auto">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover" />
  <meta name="color-scheme" content="dark light" />
  <meta name="theme-color" content="#0a0f1a" />
  <title>Xiaozhi Robot</title>
  <script>
    // Áp theme trước khi vẽ trang để không bị nháy màu
    (function () {
      try {
        var p = localStorage.getItem('theme') || 'auto';
        var d = p === 'auto' ? (matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark') : p;
        document.documentElement.setAttribute('data-theme', d);
        document.documentElement.setAttribute('data-theme-pref', p);
      } catch (e) {}
    })();
  </script>
  <style>
    /* ===== Theme tokens ===== */
    :root, [data-theme=dark] {
      --bg:#0a0f1a; --surface:#121a2b; --surface-2:#0e1524; --surface-3:#1a2540; --inset:#060a12;
      --border:#22304f; --border-strong:#31446f;
      --text:#e3e9f2; --text-dim:#8a99ad; --text-faint:#5b6a80;
      --accent:#22c8f0; --accent-ink:#00141c; --accent-soft:rgba(34,200,240,.14);
      --ok:#1fd67a; --ok-ink:#03150b; --ok-soft:rgba(31,214,122,.13);
      --warn:#ffb547; --warn-soft:rgba(255,181,71,.13);
      --danger:#ff5d5d; --danger-soft:rgba(255,93,93,.13);
      --shadow:0 1px 2px rgba(0,0,0,.35);
      --toast-shadow:0 10px 30px rgba(0,0,0,.45);
    }
    [data-theme=light] {
      --bg:#f1f4f8; --surface:#ffffff; --surface-2:#f6f8fb; --surface-3:#eaeff6; --inset:#f3f6fa;
      --border:#d9e1ec; --border-strong:#c1cddc;
      --text:#142033; --text-dim:#55647a; --text-faint:#8794a6;
      --accent:#0a8bbd; --accent-ink:#ffffff; --accent-soft:rgba(10,139,189,.10);
      --ok:#108a4b; --ok-ink:#ffffff; --ok-soft:rgba(16,138,75,.10);
      --warn:#a96400; --warn-soft:rgba(169,100,0,.10);
      --danger:#d23838; --danger-soft:rgba(210,56,56,.09);
      --shadow:0 1px 3px rgba(20,32,51,.07);
      --toast-shadow:0 10px 30px rgba(20,32,51,.18);
    }

    * { box-sizing:border-box; margin:0; padding:0; }
    html { -webkit-text-size-adjust:100%; }
    body {
      font:14px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
      background:var(--bg); color:var(--text); min-height:100vh; padding-bottom:100px;
      transition:background-color .2s,color .2s;
    }
    button, input, select { font:inherit; color:inherit; }
    button { cursor:pointer; }
    code { font:12px ui-monospace,Menlo,Consolas,monospace; background:var(--surface-3); padding:1px 5px; border-radius:4px; }
    .wrap { max-width:1200px; margin:0 auto; padding:0 14px; }
    .hidden { display:none !important; }
    .muted { color:var(--text-dim); font-size:12px; }

    /* ===== Top bar ===== */
    .topbar { position:sticky; top:0; z-index:20; background:var(--bg); border-bottom:1px solid var(--border); }
    .topbar-row { display:flex; align-items:center; gap:10px; flex-wrap:wrap; padding:10px 0 8px; }
    .brand { font-weight:700; font-size:17px; display:flex; align-items:center; gap:8px; }
    .conn { display:inline-flex; align-items:center; gap:6px; font-size:12px; color:var(--text-dim);
      padding:3px 10px; border:1px solid var(--border); border-radius:99px; background:var(--surface); }
    .conn .dot { width:8px; height:8px; border-radius:50%; background:var(--warn); }
    .conn.ok .dot { background:var(--ok); box-shadow:0 0 0 3px var(--ok-soft); }
    .conn.err .dot { background:var(--danger); }
    .conn.wait .dot { animation:blink 1s infinite; }
    @keyframes blink { 50% { opacity:.25; } }
    .spacer { flex:1; }
    .stats { display:flex; gap:6px; flex-wrap:wrap; }
    .stat { font-size:12px; padding:3px 10px; border-radius:99px; background:var(--surface);
      border:1px solid var(--border); color:var(--text-dim); white-space:nowrap; }
    .stat b { color:var(--text); font-weight:600; margin-left:3px; font-variant-numeric:tabular-nums; }
    .stat b.st-ok { color:var(--accent); } .stat b.st-run { color:var(--ok); } .stat b.st-err { color:var(--danger); }
    .icon-btn { display:inline-flex; align-items:center; gap:6px; height:32px; padding:0 10px; border-radius:8px;
      border:1px solid var(--border); background:var(--surface); font-size:13px; }
    .icon-btn:hover { border-color:var(--accent); }
    .icon-btn svg { width:16px; height:16px; display:none; }
    [data-theme-pref=auto] .ti-auto, [data-theme-pref=light] .ti-light, [data-theme-pref=dark] .ti-dark { display:block; }
    @media (max-width:640px) {
      .stats { order:5; width:100%; }
      .icon-btn span { display:none; }
    }

    /* ===== Tabs ===== */
    .tabs { display:flex; gap:2px; overflow-x:auto; scrollbar-width:none; margin-bottom:-1px; }
    .tabs::-webkit-scrollbar { display:none; }
    .tab { background:none; border:none; border-bottom:2px solid transparent; padding:9px 12px;
      color:var(--text-dim); font-weight:600; white-space:nowrap; display:inline-flex; align-items:center; gap:6px; }
    .tab:hover { color:var(--text); }
    .tab[aria-selected=true] { color:var(--accent); border-bottom-color:var(--accent); }
    .count { font-style:normal; font-size:10px; min-width:16px; height:16px; padding:0 4px; border-radius:8px;
      background:var(--danger); color:#fff; display:inline-flex; align-items:center; justify-content:center; }
    main { padding-top:14px; }
    .panel { display:none; }
    .panel.active { display:block; animation:fadein .15s ease-out; }
    @keyframes fadein { from { opacity:0; } }

    /* ===== Grid & cards ===== */
    .grid { display:grid; gap:14px; grid-template-columns:repeat(auto-fill,minmax(320px,1fr)); align-items:start; }
    .span-2 { grid-column:span 2; }
    @media (max-width:700px) { .span-2 { grid-column:auto; } .grid { grid-template-columns:1fr; } }
    .card { background:var(--surface); border:1px solid var(--border); border-radius:14px; padding:16px; box-shadow:var(--shadow); }
    .card-h { display:flex; align-items:center; gap:8px; margin-bottom:12px; }
    .card-h h2 { font-size:15px; font-weight:650; flex:1; }
    .pill { font-size:11px; padding:2px 8px; border-radius:99px; background:var(--warn-soft); color:var(--warn); font-weight:600; }
    .sep { border-top:1px solid var(--border); margin:14px 0; }
    .sec-title { font-size:11px; text-transform:uppercase; letter-spacing:.07em; color:var(--text-faint); font-weight:700; margin:2px 0 8px; }

    /* ===== Buttons ===== */
    .btn { display:inline-flex; align-items:center; justify-content:center; gap:6px; padding:10px 12px; border-radius:9px;
      border:1px solid var(--border); background:var(--surface-3); font-size:13px; font-weight:500;
      transition:border-color .15s,background-color .15s,transform .05s; }
    .btn:hover { border-color:var(--accent); }
    .btn:active { transform:scale(.97); }
    .btn:disabled { opacity:.4; cursor:not-allowed; transform:none; filter:grayscale(1); }
    .btn.primary { background:var(--accent); border-color:var(--accent); color:var(--accent-ink); font-weight:650; }
    .btn.success { background:var(--ok); border-color:var(--ok); color:var(--ok-ink); font-weight:650; }
    .btn.danger { background:var(--danger-soft); border-color:var(--danger); color:var(--danger); }
    .btn.ghost { background:transparent; color:var(--text-dim); }
    .btn.block { width:100%; }
    .btn.sm { padding:6px 10px; font-size:12px; }
    .btn-grid { display:grid; grid-template-columns:1fr 1fr; gap:8px; }

    /* ===== Inputs ===== */
    .input { width:100%; background:var(--surface-2); border:1px solid var(--border); border-radius:9px;
      padding:9px 12px; outline:none; transition:border-color .15s,box-shadow .15s; }
    .input:focus { border-color:var(--accent); box-shadow:0 0 0 3px var(--accent-soft); }
    .input.bad { border-color:var(--danger); box-shadow:0 0 0 3px var(--danger-soft); }
    .field { margin-bottom:12px; }
    .lbl { display:flex; justify-content:space-between; align-items:center; gap:8px; font-size:13px; color:var(--text-dim); margin-bottom:5px; }
    .lbl output { color:var(--text); font-weight:600; font-variant-numeric:tabular-nums; }
    input[type=range] { width:100%; accent-color:var(--accent); }
    .row { display:flex; gap:8px; align-items:center; }
    .row > .input { flex:1; }
    .two { display:grid; grid-template-columns:1fr 1fr; gap:8px; }
    .two .muted { display:block; margin-bottom:3px; }
    .err-box { color:var(--danger); font-size:12px; background:var(--danger-soft); border-radius:8px; padding:8px 10px; margin-bottom:10px; }

    /* Segmented control */
    .seg { display:flex; background:var(--surface-2); border:1px solid var(--border); border-radius:10px; padding:3px; gap:2px; }
    .seg label { flex:1; text-align:center; font-size:12px; font-weight:600; padding:6px 8px; border-radius:7px; cursor:pointer; color:var(--text-dim); user-select:none; }
    .seg input { position:absolute; opacity:0; pointer-events:none; }
    .seg label.on { background:var(--surface-3); color:var(--text); box-shadow:var(--shadow); }

    /* Switch */
    .sw-row { display:flex; align-items:center; gap:10px; padding:6px 0; }
    .sns-live { display:grid; grid-template-columns:repeat(3,1fr); gap:8px; margin-bottom:14px; }
    .sns-chip { border:1px solid var(--border); border-radius:10px; padding:8px 10px; background:var(--surface-2); }
    .sns-chip span { display:block; font-size:11px; color:var(--text-dim); }
    .sns-chip b { font-size:15px; }
    .sns-chip.on { border-color:var(--danger); } .sns-chip.on b { color:var(--danger); }
    .sns-chip.ok b { color:var(--ok); } .sns-chip.off b { color:var(--text-faint); }
    .fw-bar { height:8px; border-radius:99px; background:var(--surface-2); border:1px solid var(--border); overflow:hidden; margin-top:8px; }
    .fw-bar > div { height:100%; width:0; background:var(--accent); transition:width .2s; }
    .sw { flex:1; display:flex; align-items:center; gap:10px; cursor:pointer; font-size:14px; }
    .sw input { appearance:none; -webkit-appearance:none; width:38px; height:22px; border-radius:99px; flex-shrink:0;
      background:var(--surface-3); border:1px solid var(--border-strong); position:relative; cursor:pointer; transition:background-color .15s; }
    .sw input::after { content:""; position:absolute; top:2px; left:2px; width:16px; height:16px; border-radius:50%;
      background:var(--text-dim); transition:transform .15s,background-color .15s; }
    .sw input:checked { background:var(--accent); border-color:var(--accent); }
    .sw input:checked::after { transform:translateX(16px); background:#fff; }
    .sw input:focus-visible { outline:2px solid var(--accent); outline-offset:2px; }

    /* Help hints */
    .help { width:20px; height:20px; border-radius:50%; border:1px solid var(--border); background:var(--surface-2);
      color:var(--text-dim); font-size:12px; font-weight:700; flex-shrink:0; line-height:1; }
    .help:hover, .help[aria-expanded=true] { background:var(--accent); color:var(--accent-ink); border-color:var(--accent); }
    .hint { font-size:12px; color:var(--text-dim); background:var(--inset); border-left:3px solid var(--accent);
      padding:8px 10px; border-radius:0 8px 8px 0; margin:2px 0 10px; line-height:1.5; }
    .hint.warn { border-left-color:var(--warn); color:var(--warn); }
    .alert { font-size:12px; line-height:1.5; color:var(--warn); background:var(--warn-soft); border:1px solid var(--warn);
      border-radius:10px; padding:10px 12px; margin-bottom:12px; }

    /* ===== Drive ===== */
    .drive { display:flex; flex-wrap:wrap; justify-content:center; align-items:center; gap:22px; margin:16px 0 8px; }
    .dpad { display:grid; grid-template-columns:repeat(3,58px); grid-template-rows:repeat(3,58px); gap:6px; }
    .dpad button { border-radius:12px; border:1px solid var(--border); background:var(--surface-3); font-size:18px;
      transition:background-color .1s,transform .05s; user-select:none; -webkit-user-select:none; }
    .dpad button:hover { border-color:var(--accent); }
    .dpad button:active, .dpad button.pressed { background:var(--accent); color:var(--accent-ink); border-color:var(--accent); transform:scale(.95); }
    .dpad button.stop { background:var(--danger-soft); border-color:var(--danger); color:var(--danger); }
    .dpad button.stop:active, .dpad button.stop.pressed { background:var(--danger); color:#fff; }
    .joy { touch-action:none; user-select:none; -webkit-user-select:none; }
    #joyBase { display:block; cursor:grab; touch-action:none; }
    #joyBase.active { cursor:grabbing; }
    .joy-ring { fill:var(--surface-2); stroke:var(--border-strong); stroke-width:2; }
    .joy-axis { stroke:var(--border); stroke-width:1; }
    .joy-center { fill:var(--text-faint); }
    #joyKnob { fill:var(--accent-soft); stroke:var(--accent); stroke-width:2.5; }
    #joyBase.active #joyKnob { fill:var(--accent); fill-opacity:.45; }
    .readout { display:grid; grid-template-columns:repeat(4,1fr); gap:6px; font:12px ui-monospace,Menlo,Consolas,monospace; }
    .readout div { background:var(--inset); border-radius:7px; padding:6px 8px; text-align:center; color:var(--text-dim); }
    .readout b { color:var(--accent); }
    .kbd { font:11px ui-monospace,Menlo,Consolas,monospace; padding:1px 5px; border:1px solid var(--border);
      border-bottom-width:2px; border-radius:4px; background:var(--surface-2); }
    .keys { margin-top:10px; text-align:center; }

    /* ===== Macro ===== */
    .macro-main-btn { display:block; width:100%; padding:13px; border-radius:10px; border:2px solid var(--border);
      background:var(--surface-3); font-weight:700; font-size:15px; transition:all .15s; }
    .macro-main-btn.rec { border-color:var(--danger); color:var(--danger); }
    .macro-main-btn.rec.active { background:var(--danger); color:#fff; animation:macroPulse 1.2s ease-in-out infinite; }
    @keyframes macroPulse { 0%,100% { box-shadow:0 0 6px var(--danger-soft); } 50% { box-shadow:0 0 18px var(--danger); } }
    .macro-main-btn.play { border-color:var(--ok); color:var(--ok); }
    .macro-main-btn.play.active { background:var(--ok); color:var(--ok-ink); }
    .macro-main-btn:disabled { opacity:.35; cursor:not-allowed; filter:grayscale(1); }
    .macro-status { font:600 14px ui-monospace,Menlo,Consolas,monospace; text-align:center; padding:10px; margin:10px 0;
      background:var(--inset); border-radius:8px; color:var(--text-dim); }
    .macro-status.rec { color:var(--danger); } .macro-status.play { color:var(--ok); }

    /* ===== Chat ===== */
    #chatMessages { display:flex; flex-direction:column; gap:8px; height:min(52vh,440px); overflow-y:auto;
      background:var(--inset); border:1px solid var(--border); border-radius:10px; padding:12px; margin-bottom:10px; }
    .msg { max-width:82%; padding:7px 11px; border-radius:12px; overflow-wrap:anywhere; }
    .msg .meta { font-size:10.5px; color:var(--text-faint); margin-bottom:2px; }
    .msg.user { align-self:flex-end; background:var(--accent-soft); border-bottom-right-radius:4px; }
    .msg.user .meta b { color:var(--accent); }
    .msg.robot { align-self:flex-start; background:var(--ok-soft); border-bottom-left-radius:4px; }
    .msg.robot .meta b { color:var(--ok); }
    .msg.sys { align-self:center; text-align:center; color:var(--text-dim); font-style:italic; font-size:12px; background:none; }
    .chips { display:flex; gap:6px; flex-wrap:wrap; margin-bottom:10px; }
    .chip { font-size:12px; padding:4px 10px; border-radius:99px; border:1px solid var(--border); background:var(--surface-2); }
    .chip:hover { border-color:var(--accent); color:var(--accent); }

    /* ===== Log ===== */
    .log-tools { display:flex; gap:8px; flex-wrap:wrap; align-items:center; margin-bottom:10px; }
    .log-tools select { width:auto; padding:6px 10px; }
    .log-tools label { font-size:12px; color:var(--text-dim); display:flex; gap:6px; align-items:center; cursor:pointer; }
    .log-box { font:12px/1.55 ui-monospace,Menlo,Consolas,monospace; background:var(--inset); border:1px solid var(--border);
      border-radius:10px; padding:10px; height:min(62vh,560px); overflow-y:auto; white-space:pre-wrap; word-break:break-word; }
    .log-info { color:var(--text-dim); } .log-warn { color:var(--warn); } .log-error { color:var(--danger); }
    .log-debug { color:var(--text-faint); } .log-sys { color:var(--accent); }

    /* ===== Password ===== */
    .pw { position:relative; }
    .pw .input { padding-right:46px; }
    .pw-eye { position:absolute; right:4px; top:50%; transform:translateY(-50%); width:38px; height:32px; border:none;
      background:none; color:var(--text-dim); border-radius:7px; display:flex; align-items:center; justify-content:center; }
    .pw-eye:hover { color:var(--accent); background:var(--accent-soft); }
    .pw-eye svg { width:18px; height:18px; }
    .pw-eye .ic-off { display:none; }
    .pw-eye[aria-pressed=true] .ic-on { display:none; }
    .pw-eye[aria-pressed=true] .ic-off { display:block; }
    .meter { height:4px; border-radius:2px; background:var(--surface-3); margin-top:7px; overflow:hidden; }
    .meter i { display:block; height:100%; width:0; background:var(--danger); transition:width .2s,background-color .2s; }
    .pw-info { display:flex; justify-content:space-between; font-size:11.5px; color:var(--text-dim); margin-top:4px; }
    .match { font-size:12px; margin-top:5px; min-height:18px; }
    .match.ok { color:var(--ok); } .match.bad { color:var(--danger); }

    /* ===== E-stop & toasts ===== */
    .estop { position:fixed; right:16px; bottom:calc(16px + env(safe-area-inset-bottom)); z-index:30; width:66px; height:66px;
      border-radius:50%; border:3px solid rgba(255,255,255,.35); background:#e53935; color:#fff; font-weight:800; font-size:12px;
      letter-spacing:.03em; box-shadow:0 6px 18px rgba(229,57,53,.45); display:flex; flex-direction:column; align-items:center; justify-content:center; gap:1px; }
    .estop span { font-size:18px; line-height:1; }
    .estop:active { transform:scale(.93); }
    #toasts { position:fixed; left:50%; top:76px; transform:translateX(-50%); z-index:40; display:flex; flex-direction:column;
      gap:8px; align-items:center; pointer-events:none; width:min(92vw,420px); }
    .toast { width:100%; background:var(--surface); border:1px solid var(--border); border-left:4px solid var(--accent);
      padding:10px 14px; border-radius:10px; box-shadow:var(--toast-shadow); font-size:13px; animation:tin .2s ease-out; }
    .toast.ok { border-left-color:var(--ok); } .toast.err { border-left-color:var(--danger); } .toast.warn { border-left-color:var(--warn); }
    .toast.out { opacity:0; transform:translateY(-6px); transition:opacity .25s,transform .25s; }
    @keyframes tin { from { opacity:0; transform:translateY(-8px); } }
    @media (prefers-reduced-motion:reduce) { *, *::before, *::after { animation:none !important; transition:none !important; } }
  </style>
</head>
<body>

<header class="topbar">
  <div class="wrap">
    <div class="topbar-row">
      <div class="brand">🤖 Xiaozhi</div>
      <div class="conn wait" id="conn" role="status"><i class="dot"></i><span id="connText">Đang kết nối…</span></div>
      <div class="spacer"></div>
      <div class="stats">
        <div class="stat">Bánh xe<b id="stWheel">--</b></div>
        <div class="stat">L/R<b id="stSpeed">0/0</b></div>
        <div class="stat">Âm lượng<b id="stVol">0%</b></div>
      </div>
      <button class="icon-btn" id="themeBtn" type="button" title="Đổi giao diện">
        <svg class="ti-auto" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><path d="M12 3a9 9 0 0 1 0 18z" fill="currentColor"/></svg>
        <svg class="ti-light" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>
        <svg class="ti-dark" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>
        <span>Tự động</span>
      </button>
    </div>
    <nav class="tabs" role="tablist">
      <button class="tab" role="tab" data-tab="control">🎮 Điều khiển</button>
      <button class="tab" role="tab" data-tab="chat">💬 Chat & Nhạc</button>
      <button class="tab" role="tab" data-tab="life">🌱 Hành vi</button>
      <button class="tab" role="tab" data-tab="sensor">🛡️ Cảm biến</button>
      <button class="tab" role="tab" data-tab="settings">⚙️ Cài đặt <i class="count hidden" id="setBadge">!</i></button>
      <button class="tab" role="tab" data-tab="log">📜 Nhật ký <i class="count hidden" id="logBadge">0</i></button>
    </nav>
  </div>
</header>

<main class="wrap">

  <!-- ================= TAB: ĐIỀU KHIỂN ================= -->
  <section class="panel" data-panel="control">
    <div class="grid">

      <div class="card span-2" id="wheelCard">
        <div class="card-h"><h2>🎮 Điều khiển bánh xe</h2></div>
        <div class="seg" id="joyModeSeg">
          <label><input type="radio" name="joyMode" value="dpad"> D-pad</label>
          <label><input type="radio" name="joyMode" value="joystick"> Joystick</label>
          <label><input type="radio" name="joyMode" value="both" checked> Cả hai</label>
        </div>
        <div class="drive">
          <div class="dpad">
            <span></span>
            <button data-cmd="forward" aria-label="Tiến">▲</button>
            <span></span>
            <button data-cmd="left" aria-label="Trái">◀</button>
            <button data-cmd="stop" class="stop" aria-label="Dừng">■</button>
            <button data-cmd="right" aria-label="Phải">▶</button>
            <span></span>
            <button data-cmd="backward" aria-label="Lùi">▼</button>
            <span></span>
          </div>
          <div class="joy">
            <svg id="joyBase" viewBox="-100 -100 200 200" width="200" height="200">
              <circle class="joy-ring" cx="0" cy="0" r="90"/>
              <line class="joy-axis" x1="-90" y1="0" x2="90" y2="0"/>
              <line class="joy-axis" x1="0" y1="-90" x2="0" y2="90"/>
              <circle class="joy-center" cx="0" cy="0" r="2"/>
              <circle id="joyKnob" cx="0" cy="0" r="28"/>
            </svg>
          </div>
        </div>
        <div class="readout">
          <div>x <b id="joyX">0.00</b></div>
          <div>y <b id="joyY">0.00</b></div>
          <div>L <b id="joyL">0</b></div>
          <div>R <b id="joyR">0</b></div>
        </div>
        <div class="keys muted">
          Phím tắt: <span class="kbd">↑</span><span class="kbd">↓</span><span class="kbd">←</span><span class="kbd">→</span> hoặc
          <span class="kbd">W</span><span class="kbd">A</span><span class="kbd">S</span><span class="kbd">D</span> ·
          <span class="kbd">Space</span> dừng · <span class="kbd">Esc</span> dừng khẩn cấp
        </div>
      </div>

      <div class="card" id="macroCard">
        <div class="card-h"><h2>🎬 Macro ghi & phát</h2></div>
        <button class="macro-main-btn rec" id="btnMacroRec">● Bắt đầu ghi</button>
        <div class="macro-status" id="macroStatus">Chưa có macro</div>
        <button class="macro-main-btn play" id="btnMacroPlay" disabled>▶ Phát lại</button>
        <div class="sep"></div>
        <div class="sec-title">Lưu vào bộ nhớ</div>
        <div class="row" style="margin-bottom:8px">
          <input type="text" class="input" id="macroNameInput" placeholder="Tên macro (vd: demo_1)" maxlength="32" />
          <button class="btn" id="btnMacroSave" title="Lưu macro hiện tại">💾 Lưu</button>
        </div>
        <select class="input" id="macroSavedList"><option value="">-- Chưa có macro nào --</option></select>
        <div class="btn-grid" style="margin-top:8px">
          <button class="btn" id="btnMacroLoad">📂 Tải</button>
          <button class="btn danger" id="btnMacroDelete">🗑 Xoá file</button>
        </div>
        <button class="btn ghost block sm" id="btnMacroClear" style="margin-top:8px">Xoá macro đang có trong bộ nhớ tạm</button>
      </div>

      <div class="card" id="headCard">
        <div class="card-h"><h2>🗣️ Servo cổ</h2></div>
        <div class="btn-grid">
          <button class="btn" data-cmd="head_center">🎯 Cân bằng</button>
          <button class="btn" data-cmd="head_nod">👍 Gật đầu</button>
          <button class="btn" data-cmd="head_shake">👎 Lắc đầu</button>
          <button class="btn" data-cmd="head_curious">🤔 Tò mò</button>
        </div>
      </div>

      <div class="card" id="sysCard">
        <div class="card-h"><h2>🤖 Gọi robot & hệ thống</h2></div>
        <div class="btn-grid">
          <button class="btn primary" data-cmd="robot_call">📣 Gọi / Ngắt</button>
          <button class="btn danger" id="btnReboot">🔄 Khởi động lại</button>
        </div>
        <p class="muted" style="margin-top:10px; line-height:1.5">
          <b>Gọi / Ngắt</b>: giống nhấn nút BOOT hoặc chạm 2 lần trên robot. Đang rảnh thì bắt đầu nghe, đang nghe/nói thì ngắt.<br>
          <b>Khởi động lại</b>: robot tắt rồi bật lại trong vài giây, trang sẽ tự kết nối lại.
        </p>
      </div>

    </div>
  </section>

  <!-- ================= TAB: CHAT & NHẠC ================= -->
  <section class="panel" data-panel="chat">
    <div class="grid">
      <div class="card span-2" id="chatCard">
        <div class="card-h"><h2>💬 Chat & lệnh</h2></div>
        <div id="chatMessages">
          <div class="msg sys">Nhập lệnh như "đi thẳng 2 giây", "dừng", "gật đầu", "phát nhạc"…</div>
        </div>
        <div class="chips" id="chatChips">
          <button class="chip" data-say="đi thẳng 2 giây">đi thẳng 2 giây</button>
          <button class="chip" data-say="dừng">dừng</button>
          <button class="chip" data-say="gật đầu">gật đầu</button>
          <button class="chip" data-say="phát nhạc">phát nhạc</button>
          <button class="chip" data-say="dừng nhạc">dừng nhạc</button>
        </div>
        <div class="row">
          <input type="text" class="input" id="chatInput" placeholder="Nhập lệnh rồi nhấn Enter" autocomplete="off" />
          <button class="btn primary" id="btnChatSend">Gửi</button>
        </div>
      </div>

      <div class="card" id="musicCard">
        <div class="card-h"><h2>🎵 Âm nhạc & thẻ SD</h2></div>
        <div class="alert hidden" id="sdWarn" style="margin-bottom:10px">⚠️ Robot không thấy thẻ SD: phát nhạc từ thẻ và lưu/tải macro sẽ không dùng được. Cắm thẻ (FAT32) vào, robot sẽ tự nhận lại sau vài giây.</div>
        <div class="btn-grid" style="margin-bottom:12px">
          <button class="btn" data-music="play_mood" data-mood="vuive">😊 Nhạc vui</button>
          <button class="btn" data-music="play_mood" data-mood="buon">😢 Nhạc buồn</button>
          <button class="btn" data-music="next">⏭️ Bài tiếp</button>
          <button class="btn" data-music="stop">⏹️ Dừng nhạc</button>
        </div>
        <div class="row">
          <input type="text" class="input" id="musicQuery" placeholder="Tên bài hát…" />
          <button class="btn primary" id="btnMusicSearch">🔍 Phát</button>
        </div>
      </div>
    </div>
  </section>

  <!-- ================= TAB: HÀNH VI ================= -->
  <section class="panel" data-panel="life">
    <div class="card hidden" id="lifeUnsupported">
      <div class="card-h"><h2>🌱 Hành vi "có sự sống"</h2></div>
      <p class="muted" style="line-height:1.5">Firmware đang chạy chưa hỗ trợ tính năng này (<code>/api/settings</code> không có các trường <code>life_…</code>). Hãy nạp firmware mới hơn rồi tải lại trang.</p>
    </div>
      <div class="card" id="lifeCard">
        <div class="card-h">
          <h2>🌱 Hành vi "có sự sống"</h2>
          <button class="help" data-hint="hint-main" aria-expanded="false">?</button>
        </div>
        <div class="hint hidden" id="hint-main">Robot tự gật/lắc đầu và nhích nhẹ khi trò chuyện để trông sinh động hơn. Chỉnh xong nhớ bấm "Lưu". Giá trị được nhớ cả khi tắt nguồn.</div>

        <div class="grid" style="gap:18px">
          <div id="lifeHeadCol">
            <div class="sec-title">Cử động đầu</div>
            <div class="sw-row"><label class="sw"><input type="checkbox" id="lfHead"> Cử động đầu khi robot đang nói</label><button class="help" data-hint="hint-head">?</button></div>
            <div class="hint hidden" id="hint-head">Đầu gật/lắc ngẫu nhiên nhẹ trong lúc robot trả lời. Tắt nếu thấy tiếng servo làm phiền.</div>
            <div class="sw-row"><label class="sw"><input type="checkbox" id="lfListen"> Gật đầu khi nghe bạn nói</label><button class="help" data-hint="hint-listen">?</button></div>
            <div class="hint hidden" id="hint-listen">Khi bạn nói liên tục hơn khoảng 1,5 giây, robot gật đầu để thể hiện đang lắng nghe. Nếu robot gật khi không ai nói (micro bắt nhiễu), hãy tắt mục này.</div>
            <div class="sw-row"><label class="sw"><input type="checkbox" id="lfIdle"> Cử động nhỏ khi rảnh</label><button class="help" data-hint="hint-idle">?</button></div>
            <div class="hint hidden" id="hint-idle">Thỉnh thoảng nghiêng đầu tò mò khi không trò chuyện.</div>

            <div class="field" style="margin-top:10px">
              <div class="lbl"><span>Nghỉ giữa 2 cử động khi rảnh (giây)</span><button class="help" data-hint="hint-idle-gap">?</button></div>
              <div class="two">
                <div><span class="muted">Thấp nhất</span><input type="number" class="input" id="lfIdleMin" min="5" max="600" inputmode="numeric"></div>
                <div><span class="muted">Cao nhất</span><input type="number" class="input" id="lfIdleMax" min="5" max="1200" inputmode="numeric"></div>
              </div>
              <div class="hint hidden" id="hint-idle-gap" style="margin-top:8px">Chỉ áp dụng khi robot đang rảnh và bật "Cử động nhỏ khi rảnh".</div>
            </div>
          </div>

          <div id="lifeWheelCol">
            <div class="sec-title">Nhích bánh xe</div>
            <div class="sw-row"><label class="sw"><input type="checkbox" id="lfWheel"> ⚠️ Cho phép nhích bánh xe khi robot nói</label><button class="help" data-hint="hint-wheel">?</button></div>
            <div class="hint warn hidden" id="hint-wheel">Robot sẽ tiến/lùi nhẹ khi đang nói. Chỉ bật sau khi bạn đã bấm "Thử nhích" và đo quãng đường bên dưới. Đặt robot ở giữa bàn, cách mép bàn xa hơn "Giới hạn lệch" ít nhất 20 cm. Lệnh dừng từ web, giọng nói hoặc tay cầm RC luôn được ưu tiên.</div>

            <div class="two" style="margin-top:8px">
              <div class="field">
                <div class="lbl"><span>Tốc độ (30–100)</span><button class="help" data-hint="hint-speed">?</button></div>
                <input type="number" class="input" id="lfSpeed" min="30" max="100" step="5" inputmode="numeric">
              </div>
              <div class="field">
                <div class="lbl"><span>Cấp điện (ms, 40–500)</span><button class="help" data-hint="hint-ms">?</button></div>
                <input type="number" class="input" id="lfMs" min="40" max="500" step="10" inputmode="numeric">
              </div>
            </div>
            <div class="hint hidden" id="hint-speed">Công suất động cơ mỗi lần nhích. Số lớn thì nhích dứt khoát, số nhỏ thì êm nhưng bánh có thể không quay. Gợi ý bắt đầu 80–90; nếu bánh không nhúc nhích thì tăng lên.</div>
            <div class="hint hidden" id="hint-ms">Càng lớn thì robot đi càng xa mỗi lần. Nếu bấm "Thử nhích" mà bánh không nhúc nhích thì tăng thêm 20–40 ms rồi thử lại. Robot lớn/nặng cần số lớn hơn robot nhỏ/nhẹ.</div>

            <div class="two">
              <div class="field">
                <div class="lbl"><span>Quãng đường THỰC TẾ (cm, 1–25)</span><button class="help" data-hint="hint-cm">?</button></div>
                <input type="number" class="input" id="lfCm" min="1" max="25" step="1" inputmode="numeric">
              </div>
              <div class="field">
                <div class="lbl"><span>Giới hạn lệch (cm, 5–25)</span><button class="help" data-hint="hint-drift">?</button></div>
                <input type="number" class="input" id="lfDrift" min="5" max="25" step="1" inputmode="numeric">
              </div>
            </div>
            <div class="hint hidden" id="hint-cm">Cách làm: đặt robot trên bàn, bấm "Thử nhích tiến", dùng thước đo robot vừa đi bao nhiêu cm (đo 3 lần lấy trung bình, làm tròn), rồi nhập vào đây. Robot dùng số này để tính mình đã lệch bao xa so với chỗ đặt. Nhập thấp hơn thực tế sẽ làm giới hạn bên dưới kém an toàn.</div>
            <div class="hint hidden" id="hint-drift">Robot không bao giờ đi xa hơn khoảng này so với chỗ bạn đặt. Hãy đặt nhỏ hơn khoảng cách từ robot đến mép bàn ít nhất 10 cm. Số này phải lớn hơn hoặc bằng "quãng đường mỗi lần nhích", nếu không robot sẽ không nhích. Robot lệch dần do bánh trượt hoặc 2 động cơ không đều, nên thỉnh thoảng hãy đặt lại về giữa bàn.</div>

            <div class="field">
              <div class="lbl"><span>Nghỉ giữa 2 lần nhích (giây)</span><button class="help" data-hint="hint-gap">?</button></div>
              <div class="two">
                <div><span class="muted">Thấp nhất</span><input type="number" class="input" id="lfGapMin" min="2" max="120" inputmode="numeric"></div>
                <div><span class="muted">Cao nhất</span><input type="number" class="input" id="lfGapMax" min="2" max="300" inputmode="numeric"></div>
              </div>
              <div class="hint hidden" id="hint-gap" style="margin-top:8px">Khi robot đang nói, mỗi lần nhích cách nhau ngẫu nhiên trong khoảng này. Muốn nhích ít hơn thì tăng cả hai số.</div>
            </div>

            <div class="lbl"><span>Thử nghiệm</span><button class="help" data-hint="hint-test">?</button></div>
            <div class="btn-grid">
              <button class="btn" data-nudge="forward">⬆️ Thử nhích tiến</button>
              <button class="btn" data-nudge="backward">⬇️ Thử nhích lùi</button>
            </div>
            <div class="hint hidden" id="hint-test" style="margin-top:8px">Nút "Thử" dùng đúng các giá trị đang nhập ở trên (tự lưu luôn) và hoạt động cả khi chưa bật nhích bánh xe.</div>
          </div>
        </div>

        <div class="sep"></div>
        <div class="err-box hidden" id="lifeErr"></div>
        <button class="btn success block" id="btnSaveLife">💾 Lưu hành vi</button>
      </div>
  </section>

  <!-- ================= TAB: CẢM BIẾN ================= -->
  <section class="panel" data-panel="sensor">
    <div class="card hidden" id="snsUnsupported">
      <div class="card-h"><h2>🛡️ Cảm biến an toàn</h2></div>
      <p class="muted" style="line-height:1.5">Firmware đang chạy chưa hỗ trợ cảm biến (<code>/api/settings</code> không có các trường <code>sns_…</code>). Hãy nạp firmware mới hơn rồi tải lại trang.</p>
    </div>
    <div class="card" id="snsCard">
      <div class="card-h">
        <h2>🛡️ Cảm biến an toàn</h2>
        <button class="help" data-hint="hint-sns-main" aria-expanded="false">?</button>
      </div>
      <div class="hint hidden" id="hint-sns-main">Khi cảm biến phát hiện vật cản hoặc mép bàn, robot dừng bánh và không nhận lệnh đi tiếp về hướng đó (từ web, giọng nói, macro, "có sự sống" và cả tay cầm RC). Đi theo hướng ngược lại hoặc quay tại chỗ vẫn được. Đổi chân và lưu là áp dụng ngay, không cần khởi động lại.</div>

      <div class="sec-title">Trạng thái trực tiếp</div>
      <div class="sns-live">
        <div class="sns-chip off" id="snsLiveFront"><span>Phía trước</span><b>—</b></div>
        <div class="sns-chip off" id="snsLiveBack"><span>Phía sau</span><b>—</b></div>
        <div class="sns-chip off" id="snsLiveTof"><span>Khoảng cách ToF</span><b>—</b></div>
      </div>

      <div class="grid" style="gap:18px">
        <div>
          <div class="sec-title">Cảm biến va chạm / vật cản (LM393, công tắc)</div>
          <div class="sw-row"><label class="sw"><input type="checkbox" id="snsFrontEn"> Cảm biến phía trước</label><button class="help" data-hint="hint-sns-bump">?</button></div>
          <div class="two">
            <div><span class="muted">Chân GPIO</span><select class="input" id="snsFrontGpio"></select></div>
            <div><span class="muted">Mức báo</span><select class="input" id="snsFrontLow"><option value="1">LOW = có vật (LM393)</option><option value="0">HIGH = có vật</option></select></div>
          </div>
          <div class="sw-row" style="margin-top:8px"><label class="sw"><input type="checkbox" id="snsBackEn"> Cảm biến phía sau</label></div>
          <div class="two">
            <div><span class="muted">Chân GPIO</span><select class="input" id="snsBackGpio"></select></div>
            <div><span class="muted">Mức báo</span><select class="input" id="snsBackLow"><option value="1">LOW = có vật (LM393)</option><option value="0">HIGH = có vật</option></select></div>
          </div>
          <div class="hint hidden" id="hint-sns-bump" style="margin-top:8px">Nối OUT của module vào chân GPIO đã chọn, VCC vào <b>3.3V</b> (không dùng 5V), GND chung. Module LM393 thường báo LOW khi có vật; nếu ô "Trạng thái trực tiếp" báo ngược thì đổi "Mức báo". Công tắc hành trình: một chân vào GPIO, chân kia xuống GND, chọn LOW. Chỉ nên dùng các chân trong danh sách (đã kiểm tra là trống trên board).</div>
        </div>

        <div>
          <div class="sec-title">Cảm biến khoảng cách ToF (VL6180X / TOF050C)</div>
          <div class="sw-row"><label class="sw"><input type="checkbox" id="snsTofEn"> Bật cảm biến ToF</label><button class="help" data-hint="hint-sns-tof">?</button></div>
          <div class="two">
            <div><span class="muted">Cách gắn</span><select class="input" id="snsTofMode"><option value="0">Nhìn thẳng: chặn vật cản</option><option value="1">Chúc xuống: chống rơi mép bàn</option></select></div>
            <div><span class="muted">Ngưỡng (mm, 10–200)</span><input type="number" class="input" id="snsTofMm" min="10" max="200" inputmode="numeric"></div>
          </div>
          <div class="hint hidden" id="hint-sns-tof" style="margin-top:8px">Nối SDA→GPIO41, SCL→GPIO42 (chung với OLED chính), VCC 3.3V, GND. Tầm đo thực tế khoảng 10–20 cm.<br>• <b>Nhìn thẳng</b>: dừng khi vật gần hơn ngưỡng.<br>• <b>Chúc xuống</b>: gắn ở mũi robot, nhìn xuống mặt bàn; xem số đo trực tiếp khi đặt trên bàn (vd 30 mm) rồi đặt ngưỡng lớn hơn khoảng 20–30 mm. Khi mũi robot ra khỏi mép bàn, số đo vọt lên hoặc mất → robot dừng.<br>Nếu bật mà không đọc được cảm biến, robot sẽ chặn đi tiến cho an toàn.</div>
        </div>

        <div>
          <div class="sec-title">Phản ứng</div>
          <div class="two">
            <div><span class="muted">Khi phát hiện lúc đang chạy</span><select class="input" id="snsAction"><option value="1">Dừng + lùi nhẹ</option><option value="0">Chỉ dừng</option></select></div>
            <div><span class="muted">Lùi trong (ms, 100–1000)</span><input type="number" class="input" id="snsBackoffMs" min="100" max="1000" step="50" inputmode="numeric"></div>
          </div>
          <div class="two" style="margin-top:8px">
            <div><span class="muted">Tốc độ lùi (30–100)</span><input type="number" class="input" id="snsBackoffSpeed" min="30" max="100" step="5" inputmode="numeric"></div>
            <div></div>
          </div>
          <div class="sw-row" style="margin-top:8px"><label class="sw"><input type="checkbox" id="snsFace"> Đổi mặt "giật mình" khi va chạm</label></div>
          <div class="sw-row" id="snsRcRow"><label class="sw"><input type="checkbox" id="snsRcBypass"> ⚠️ Cho tay cầm RC vượt qua cảm biến</label><button class="help" data-hint="hint-sns-rc">?</button></div>
          <div class="hint warn hidden" id="hint-sns-rc">Khi bật, lúc bạn đang lái bằng tay cầm RC robot sẽ không bị cảm biến chặn (bạn tự chịu trách nhiệm quan sát). Các nguồn lệnh khác vẫn bị chặn.</div>
        </div>
      </div>

      <div class="sep"></div>
      <div class="err-box hidden" id="snsErr"></div>
      <button class="btn success block" id="btnSaveSns">💾 Lưu cảm biến</button>
    </div>
  </section>

  <!-- ================= TAB: CÀI ĐẶT ================= -->
  <section class="panel" data-panel="settings">
    <div class="grid">

      <div class="card" id="setCard">
        <div class="card-h"><h2>⚙️ Thông số chuyển động & âm thanh</h2><span class="pill hidden" id="setDirty">Chưa lưu</span></div>
        <div id="setMotion">
        <div class="field">
          <div class="lbl">Tốc độ tiến <output id="vMove">60</output></div>
          <input type="range" id="moveSpeed" min="0" max="100" value="60" data-out="vMove">
        </div>
        <div class="field">
          <div class="lbl">Tốc độ xoay <output id="vTurn">55</output></div>
          <input type="range" id="turnSpeed" min="0" max="100" value="55" data-out="vTurn">
        </div>
        <div class="field">
          <div class="lbl">Bước di chuyển (ms) <output id="vStep">350</output></div>
          <input type="range" id="stepMs" min="80" max="2000" value="350" data-out="vStep">
        </div>
        </div>
        <div class="field">
          <div class="lbl">Âm lượng loa <output id="vVol">60</output></div>
          <input type="range" id="volume" min="0" max="100" value="60" data-out="vVol">
        </div>
        <button class="btn success block" id="btnSaveSettings">💾 Lưu cài đặt</button>
      </div>

      <div class="card hidden" id="hwCard">
        <div class="card-h">
          <h2>🧩 Phần cứng</h2>
          <button class="help" data-hint="hint-hw" aria-expanded="false">?</button>
        </div>
        <div class="hint hidden" id="hint-hw">Tắt các bộ phận robot của bạn không có, giao diện sẽ ẩn phần điều khiển tương ứng và robot không cử động phần đó. Thẻ SD được theo dõi liên tục (rút/cắm tự nhận); màn phụ được phát hiện khi khởi động.</div>
        <div class="sec-title">Bạn tự chọn</div>
        <div class="sw-row"><label class="sw"><input type="checkbox" id="hwWheels"> Bánh xe (2 động cơ DC)</label></div>
        <div class="sw-row"><label class="sw"><input type="checkbox" id="hwHead"> Servo cổ (pan-tilt)</label></div>
        <div class="sw-row"><label class="sw"><input type="checkbox" id="hwRc"> Tay cầm RC (bộ thu SBUS)</label></div>
        <div class="alert hidden" id="hwRcReboot" style="margin:6px 0">Thay đổi tay cầm RC có hiệu lực sau khi khởi động lại robot.</div>
        <div class="sec-title" style="margin-top:12px">Robot tự phát hiện</div>
        <div class="sns-live" style="grid-template-columns:repeat(2,1fr)">
          <div class="sns-chip off" id="hwSdChip"><span>Thẻ SD</span><b>—</b></div>
          <div class="sns-chip off" id="hwOled2Chip"><span>Màn phụ 0.91"</span><b>—</b></div>
        </div>
        <button class="btn success block" id="btnSaveHw">💾 Lưu phần cứng</button>
      </div>

      <div class="card hidden" id="fwCard">
        <div class="card-h">
          <h2>⬆️ Cập nhật firmware</h2>
          <button class="help" data-hint="hint-fw" aria-expanded="false">?</button>
        </div>
        <div class="hint hidden" id="hint-fw">Chọn file <b>.bin của app</b> (vd <code>build/xiaozhi.bin</code>), không dùng file gộp (merged) hay bootloader. Robot kiểm tra file trước khi cài, cài xong tự khởi động lại. Nếu firmware mới lỗi không chạy lên được, robot tự quay về bản cũ. Các cài đặt (Wi-Fi, mật khẩu, cảm biến…) được giữ nguyên.</div>
        <div class="sns-live" style="grid-template-columns:repeat(2,1fr)">
          <div class="sns-chip ok" id="fwVerChip"><span>Phiên bản đang chạy</span><b>—</b></div>
          <div class="sns-chip ok" id="fwPartChip"><span>Phân vùng</span><b>—</b></div>
        </div>
        <div class="sec-title">Tải file lên từ máy</div>
        <input type="file" class="input" id="fwFile" accept=".bin">
        <div class="fw-bar hidden" id="fwBarWrap"><div id="fwBar"></div></div>
        <div class="muted" id="fwMsg" style="margin:6px 0; min-height:16px"></div>
        <button class="btn primary block" id="btnFwUpload">⬆️ Tải lên & cài đặt</button>
        <div class="sec-title" style="margin-top:14px">Hoặc cài từ đường link</div>
        <div class="row">
          <input type="url" class="input" id="fwUrl" placeholder="https://…/xiaozhi.bin" autocomplete="off">
          <button class="btn" id="btnFwUrl">Cài</button>
        </div>
      </div>

      <div class="card" id="authCard">
        <div class="card-h"><h2>🔒 Bảo mật WebUI</h2></div>
        <div class="alert hidden" id="authWarn">⚠️ Bạn đang dùng mật khẩu mặc định ("admin"). Hãy đổi ngay để người khác trong mạng Wi-Fi không điều khiển được robot.</div>
        <div class="field">
          <div class="lbl"><label for="authNew">Mật khẩu mới</label><span id="pwLen">0/32</span></div>
          <div class="pw">
            <input type="password" class="input" id="authNew" maxlength="32" autocomplete="new-password" />
            <button type="button" class="pw-eye" data-target="authNew" aria-pressed="false" aria-label="Hiện mật khẩu">
              <svg class="ic-on" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></svg>
              <svg class="ic-off" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 3l18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.5 0 10 7 10 7a17.6 17.6 0 0 1-3.2 4.2M6.6 6.6C3.9 8.3 2 12 2 12s3.5 7 10 7c1.8 0 3.4-.5 4.8-1.3"/><path d="M9.9 9.9a3 3 0 0 0 4.2 4.2"/></svg>
            </button>
          </div>
          <div class="meter"><i id="pwBar"></i></div>
          <div class="pw-info"><span id="pwHint">6–32 ký tự</span><span id="pwStrength"></span></div>
        </div>
        <div class="field">
          <div class="lbl"><label for="authNew2">Nhập lại mật khẩu mới</label></div>
          <div class="pw">
            <input type="password" class="input" id="authNew2" maxlength="32" autocomplete="new-password" />
            <button type="button" class="pw-eye" data-target="authNew2" aria-pressed="false" aria-label="Hiện mật khẩu">
              <svg class="ic-on" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></svg>
              <svg class="ic-off" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 3l18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.5 0 10 7 10 7a17.6 17.6 0 0 1-3.2 4.2M6.6 6.6C3.9 8.3 2 12 2 12s3.5 7 10 7c1.8 0 3.4-.5 4.8-1.3"/><path d="M9.9 9.9a3 3 0 0 0 4.2 4.2"/></svg>
            </button>
          </div>
          <div class="match" id="pwMatch"></div>
        </div>
        <button class="btn primary block" id="btnPw" disabled>🔑 Đổi mật khẩu</button>
        <p class="muted" style="margin-top:10px; line-height:1.5">
          Tên đăng nhập luôn là <b>admin</b>. Đổi xong trang sẽ tải lại và trình duyệt hỏi đăng nhập bằng mật khẩu mới.
          Quên mật khẩu thì nạp lại firmware với <code>kResetPasswordOnBoot = true</code> trong <code>web_auth.h</code> một lần.
        </p>
      </div>

    </div>
  </section>

  <!-- ================= TAB: NHẬT KÝ ================= -->
  <section class="panel" data-panel="log">
    <div class="card">
      <div class="card-h"><h2>📜 Nhật ký hệ thống</h2></div>
      <div class="log-tools">
        <select class="input" id="logFilter">
          <option value="all">Tất cả</option>
          <option value="log-error">Lỗi (E)</option>
          <option value="log-warn">Cảnh báo (W)</option>
          <option value="log-info">Thông tin (I)</option>
          <option value="log-debug">Debug (D)</option>
          <option value="log-sys">Trang web (SYS)</option>
        </select>
        <label><input type="checkbox" id="logAuto" checked> Tự cuộn</label>
        <div class="spacer"></div>
        <button class="btn sm" id="btnLogSave">⬇️ Tải về</button>
        <button class="btn sm ghost" id="btnLogClear">Xoá</button>
      </div>
      <div class="log-box" id="logBox"></div>
    </div>
  </section>

</main>

<button class="estop" id="estop" title="Dừng khẩn cấp (Esc)"><span>■</span>DỪNG</button>
<div id="toasts" aria-live="polite"></div>

<script>
  // ===== Helpers =====
  const $ = id => document.getElementById(id);
  const LS = {
    get(k, d) { try { const v = localStorage.getItem(k); return v === null ? d : v; } catch (_) { return d; } },
    set(k, v) { try { localStorage.setItem(k, v); } catch (_) {} }
  };
  let macroState = { recording: false, playing: false, count: 0 };
  let ws = null;
  let joystick = null;
  let currentTab = 'control';

  function escapeHtml(s) {
    return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  }
  function wsOpen() { return ws && ws.readyState === WebSocket.OPEN; }
  function wsSend(obj) {
    if (!wsOpen()) { toast('Chưa kết nối được với robot', 'err'); log('WS chưa sẵn sàng'); return false; }
    ws.send(JSON.stringify(obj));
    return true;
  }

  // ===== Theme =====
  const THEME_ORDER = ['auto', 'light', 'dark'];
  const THEME_LABEL = { auto: 'Tự động', light: 'Sáng', dark: 'Tối' };
  const themeMq = window.matchMedia ? matchMedia('(prefers-color-scheme: light)') : null;
  function applyTheme(pref) {
    const eff = pref === 'auto' ? (themeMq && themeMq.matches ? 'light' : 'dark') : pref;
    const root = document.documentElement;
    root.setAttribute('data-theme', eff);
    root.setAttribute('data-theme-pref', pref);
    const btn = $('themeBtn');
    if (btn) {
      btn.querySelector('span').textContent = THEME_LABEL[pref];
      btn.title = 'Giao diện: ' + THEME_LABEL[pref] + ' (bấm để đổi)';
    }
    const meta = document.querySelector('meta[name=theme-color]');
    if (meta) meta.content = eff === 'light' ? '#f1f4f8' : '#0a0f1a';
  }
  function cycleTheme() {
    const cur = LS.get('theme', 'auto');
    const next = THEME_ORDER[(THEME_ORDER.indexOf(cur) + 1) % THEME_ORDER.length];
    LS.set('theme', next);
    applyTheme(next);
    toast('Giao diện: ' + THEME_LABEL[next]);
  }
  if (themeMq) {
    const onSys = () => { if (LS.get('theme', 'auto') === 'auto') applyTheme('auto'); };
    if (themeMq.addEventListener) themeMq.addEventListener('change', onSys); else if (themeMq.addListener) themeMq.addListener(onSys);
  }

  // ===== Toast =====
  function toast(msg, type = 'info', ms = 2600) {
    const box = $('toasts');
    if (!box) return;
    const t = document.createElement('div');
    t.className = 'toast ' + type;
    t.textContent = msg;
    box.appendChild(t);
    while (box.children.length > 3) box.removeChild(box.firstChild);
    setTimeout(() => { t.classList.add('out'); setTimeout(() => t.remove(), 300); }, ms);
  }

  // ===== Log =====
  let logFilter = 'all';
  let logAuto = true;
  let unseenErr = 0;
  function updateErrBadge() {
    const b = $('logBadge');
    if (!b) return;
    b.textContent = unseenErr > 99 ? '99+' : unseenErr;
    b.classList.toggle('hidden', unseenErr === 0);
  }
  function appendLogLine(level, msg, cls) {
    const box = $('logBox');
    if (!box) return;
    const t = new Date().toLocaleTimeString();
    const line = document.createElement('div');
    line.className = cls || 'log-info';
    line.textContent = `[${t}] ${level} ${msg}`;
    if (logFilter !== 'all' && line.className !== logFilter) line.hidden = true;
    box.appendChild(line);
    while (box.childNodes.length > 300) box.removeChild(box.firstChild);
    if (logAuto) box.scrollTop = box.scrollHeight;
    if (cls === 'log-error' && currentTab !== 'log') { unseenErr++; updateErrBadge(); }
  }
  function log(msg) { appendLogLine('SYS', msg, 'log-sys'); }
  function applyLogFilter() {
    for (const el of $('logBox').children) el.hidden = logFilter !== 'all' && el.className !== logFilter;
  }
  function downloadLog() {
    const text = Array.from($('logBox').children).map(d => d.textContent).join('\n');
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: 'text/plain' }));
    a.download = 'xiaozhi-log-' + new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-') + '.txt';
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  }

  // ===== Tabs =====
  function showTab(name) {
    const tabBtn = document.querySelector(`.tab[data-tab="${name}"]`);
    if (!document.querySelector(`.panel[data-panel="${name}"]`) || (tabBtn && tabBtn.classList.contains('hidden'))) name = 'control';
    document.querySelectorAll('.tab').forEach(t => t.setAttribute('aria-selected', t.dataset.tab === name ? 'true' : 'false'));
    document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.dataset.panel === name));
    currentTab = name;
    LS.set('tab', name);
    if (name === 'log') { unseenErr = 0; updateErrBadge(); const b = $('logBox'); b.scrollTop = b.scrollHeight; }
    if (name === 'chat') { const c = $('chatMessages'); c.scrollTop = c.scrollHeight; }
  }

  // ===== Connection indicator =====
  function setConn(state, text) {
    const c = $('conn');
    c.className = 'conn ' + state;
    $('connText').textContent = text;
  }

  // ===== Chat =====
  function addChatMsg(who, text) {
    const box = $('chatMessages');
    if (!box) return;
    const t = new Date().toLocaleTimeString();
    const div = document.createElement('div');
    div.className = 'msg ' + (who === 'user' ? 'user' : 'robot');
    const label = who === 'user' ? '👤 Bạn' : '🤖 Robot';
    div.innerHTML = `<div class="meta"><b>${label}</b> · ${t}</div><div>${escapeHtml(text)}</div>`;
    box.appendChild(div);
    while (box.children.length > 200) box.removeChild(box.firstChild);
    box.scrollTop = box.scrollHeight;
  }
  function sendChat(textOverride) {
    const input = $('chatInput');
    const text = (textOverride != null ? textOverride : input.value).trim();
    if (!text) return;
    if (!wsOpen()) { addChatMsg('robot', '(Chưa kết nối WebSocket)'); return; }
    addChatMsg('user', text);
    ws.send(JSON.stringify({ type: 'chat', text: text }));
    if (textOverride == null) input.value = '';
  }

  // ===== REST =====
  function syncVal(labelId, val) { $(labelId).textContent = val; }

  async function apiGet(url) {
    try {
      const r = await fetch(url, { cache: 'no-store' });
      if (!r.ok) { log(`Lỗi API ${url} (HTTP ${r.status})`); return null; }
      return await r.json();
    } catch (e) { log('Lỗi kết nối API'); return null; }
  }

  async function apiPost(url, body, opts = {}) {
    try {
      const r = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
      let res = {};
      try { res = await r.json(); } catch (_) {}
      if (!r.ok && res.ok === undefined) res.ok = false;
      const msg = res.message || (res.ok === false ? `Thất bại (HTTP ${r.status})` : 'Thành công');
      log(msg);
      if (res.ok === false) toast(msg, 'err');
      else if (opts.toast) toast(typeof opts.toast === 'string' ? opts.toast : msg, 'ok');
      return res;
    } catch (e) {
      log('Lỗi gửi lệnh');
      toast('Không gửi được lệnh, kiểm tra kết nối', 'err');
      return null;
    }
  }

  function renderWheel(ready, l, r) {
    const el = $('stWheel');
    if (!ready) { el.textContent = 'Chưa kết nối'; el.className = 'st-err'; }
    else if (l !== 0 || r !== 0) { el.textContent = 'Đang chạy'; el.className = 'st-run'; }
    else { el.textContent = 'Sẵn sàng'; el.className = 'st-ok'; }
  }

  async function refreshStatus() {
    const st = await apiGet('/api/status');
    if (st) {
      renderWheel(!!st.wheel_ready, st.left_speed || 0, st.right_speed || 0);
      $('stSpeed').textContent = `${st.left_speed}/${st.right_speed}`;
      $('stVol').textContent = `${st.volume}%`;
      renderSensors(st);
      renderSdcard(st);
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
    else $('musicQuery').focus();
  }

  async function saveSettings() {
    const res = await apiPost('/api/settings', {
      move_speed: Number($('moveSpeed').value),
      turn_speed: Number($('turnSpeed').value),
      step_ms: Number($('stepMs').value),
      volume: Number($('volume').value)
    }, { toast: 'Đã lưu cài đặt' });
    if (res && res.ok !== false) $('setDirty').classList.add('hidden');
    refreshStatus();
  }

  // ===== Hành vi "có sự sống" =====
  const LIFE_NUM = {
    life_nudge_speed: 'lfSpeed', life_nudge_ms: 'lfMs', life_nudge_cm: 'lfCm',
    life_max_drift_cm: 'lfDrift', life_nudge_gap_min_s: 'lfGapMin', life_nudge_gap_max_s: 'lfGapMax',
    life_idle_gap_min_s: 'lfIdleMin', life_idle_gap_max_s: 'lfIdleMax'
  };
  const LIFE_LABEL = {
    lfSpeed: 'Tốc độ khi nhích', lfMs: 'Thời gian cấp điện', lfCm: 'Quãng đường mỗi lần nhích',
    lfDrift: 'Giới hạn lệch', lfGapMin: 'Nghỉ giữa 2 lần nhích (thấp nhất)', lfGapMax: 'Nghỉ giữa 2 lần nhích (cao nhất)',
    lfIdleMin: 'Nghỉ khi rảnh (thấp nhất)', lfIdleMax: 'Nghỉ khi rảnh (cao nhất)'
  };
  const LIFE_CHK = { life_head_talk: 'lfHead', life_listen_nod: 'lfListen', life_idle: 'lfIdle', life_wheel: 'lfWheel' };

  function loadLife(s) {
    const ok = !!s && s.life_nudge_ms !== undefined;
    $('lifeCard').classList.toggle('hidden', !ok);
    $('lifeUnsupported').classList.toggle('hidden', ok);
    if (!ok) return;
    for (const k in LIFE_NUM) $(LIFE_NUM[k]).value = s[k];
    for (const k in LIFE_CHK) $(LIFE_CHK[k]).checked = !!s[k];
  }
  function lifePayload() {
    const p = {};
    for (const k in LIFE_NUM) p[k] = Number($(LIFE_NUM[k]).value);
    for (const k in LIFE_CHK) p[k] = $(LIFE_CHK[k]).checked ? 1 : 0;
    return p;
  }
  function validateLife() {
    const errs = [];
    for (const k in LIFE_NUM) {
      const el = $(LIFE_NUM[k]);
      const v = Number(el.value), mn = Number(el.min), mx = Number(el.max);
      const bad = el.value === '' || !isFinite(v) || v < mn || v > mx;
      el.classList.toggle('bad', bad);
      if (bad) errs.push(`${LIFE_LABEL[el.id]} phải từ ${mn} đến ${mx}`);
    }
    const n = id => Number($(id).value);
    const pair = (a, b, label) => {
      if (n(a) > n(b)) { $(a).classList.add('bad'); $(b).classList.add('bad'); errs.push(label + ': số "thấp nhất" đang lớn hơn số "cao nhất"'); }
    };
    pair('lfGapMin', 'lfGapMax', 'Nghỉ giữa 2 lần nhích');
    pair('lfIdleMin', 'lfIdleMax', 'Nghỉ khi rảnh');
    if (n('lfDrift') < n('lfCm')) {
      $('lfDrift').classList.add('bad');
      errs.push('Giới hạn lệch phải ≥ quãng đường mỗi lần nhích, nếu không robot sẽ không nhích');
    }
    const box = $('lifeErr');
    box.innerHTML = errs.map(e => '• ' + escapeHtml(e)).join('<br>');
    box.classList.toggle('hidden', errs.length === 0);
    if (errs.length) toast('Có ' + errs.length + ' giá trị chưa hợp lệ', 'err');
    return errs.length === 0;
  }
  async function saveLife() {
    if (!validateLife()) return;
    await apiPost('/api/settings', lifePayload(), { toast: 'Đã lưu hành vi' });
  }
  async function testNudge(dir) {
    if (!validateLife()) return;
    await apiPost('/api/settings', { ...lifePayload(), life_test: dir });
  }

  // ===== Cảm biến an toàn =====
  const SNS_NUM = { sns_tof_mm: 'snsTofMm', sns_backoff_ms: 'snsBackoffMs', sns_backoff_speed: 'snsBackoffSpeed' };
  const SNS_SEL = { sns_front_gpio: 'snsFrontGpio', sns_front_low: 'snsFrontLow', sns_back_gpio: 'snsBackGpio',
                    sns_back_low: 'snsBackLow', sns_tof_mode: 'snsTofMode', sns_action: 'snsAction' };
  const SNS_CHK = { sns_front_en: 'snsFrontEn', sns_back_en: 'snsBackEn', sns_tof_en: 'snsTofEn',
                    sns_rc_bypass: 'snsRcBypass', sns_react_face: 'snsFace' };
  const SNS_LABEL = { snsTofMm: 'Ngưỡng ToF', snsBackoffMs: 'Thời gian lùi', snsBackoffSpeed: 'Tốc độ lùi' };

  function loadSensors(s) {
    const ok = !!s && s.sns_tof_mm !== undefined;
    $('snsCard').classList.toggle('hidden', !ok);
    $('snsUnsupported').classList.toggle('hidden', ok);
    if (!ok) return;
    const pins = Array.isArray(s.sns_gpio_choices) ? s.sns_gpio_choices : [];
    for (const id of ['snsFrontGpio', 'snsBackGpio']) {
      $(id).innerHTML = pins.map(g => `<option value="${g}">GPIO${g}</option>`).join('');
    }
    for (const k in SNS_NUM) $(SNS_NUM[k]).value = s[k];
    for (const k in SNS_SEL) $(SNS_SEL[k]).value = String(s[k]);
    for (const k in SNS_CHK) $(SNS_CHK[k]).checked = !!s[k];
  }
  function sensorPayload() {
    const p = {};
    for (const k in SNS_NUM) p[k] = Number($(SNS_NUM[k]).value);
    for (const k in SNS_SEL) p[k] = Number($(SNS_SEL[k]).value);
    for (const k in SNS_CHK) p[k] = $(SNS_CHK[k]).checked ? 1 : 0;
    return p;
  }
  function validateSensors() {
    const errs = [];
    for (const k in SNS_NUM) {
      const el = $(SNS_NUM[k]);
      const v = Number(el.value), mn = Number(el.min), mx = Number(el.max);
      const bad = el.value === '' || !isFinite(v) || v < mn || v > mx;
      el.classList.toggle('bad', bad);
      if (bad) errs.push(`${SNS_LABEL[el.id]} phải từ ${mn} đến ${mx}`);
    }
    if ($('snsFrontEn').checked && $('snsBackEn').checked && $('snsFrontGpio').value === $('snsBackGpio').value) {
      errs.push('Cảm biến trước và sau đang dùng chung một chân GPIO');
    }
    const box = $('snsErr');
    box.innerHTML = errs.map(e => '• ' + escapeHtml(e)).join('<br>');
    box.classList.toggle('hidden', errs.length === 0);
    if (errs.length) toast('Có ' + errs.length + ' giá trị chưa hợp lệ', 'err');
    return errs.length === 0;
  }
  async function saveSensors() {
    if (!validateSensors()) return;
    await apiPost('/api/settings', sensorPayload(), { toast: 'Đã lưu cảm biến' });
    setTimeout(refreshStatus, 300);
  }
  function setChip(id, cls, text) {
    const el = $(id);
    el.classList.remove('on', 'ok', 'off');
    el.classList.add(cls);
    el.querySelector('b').textContent = text;
  }
  function renderSensors(m) {
    if (typeof m.sns_front !== 'number') return;
    const tofEn = $('snsTofEn').checked;
    const side = (on, blocked, hit) => {
      if (!on) return ['off', 'Tắt'];
      return blocked ? ['on', hit ? '⛔ Va chạm' : '⛔ Bị chặn'] : ['ok', '✅ Thông'];
    };
    const f = side($('snsFrontEn').checked || tofEn, !!m.sns_front, !!m.sns_front_hit);
    setChip('snsLiveFront', f[0], f[1]);
    const b = side($('snsBackEn').checked, !!m.sns_back, !!m.sns_back_hit);
    setChip('snsLiveBack', b[0], b[1]);
    if (!tofEn) setChip('snsLiveTof', 'off', 'Tắt');
    else if (!m.sns_tof_ok) setChip('snsLiveTof', 'on', 'Không thấy');
    else setChip('snsLiveTof', 'ok', m.sns_tof_mm_now < 0 ? 'Xa / trống' : `${m.sns_tof_mm_now} mm`);
  }

  // ===== Phần cứng: ẩn phần giao diện của bộ phận không có =====
  let hwState = null;
  function applyHardware(s) {
    const ok = !!s && s.hw_wheels !== undefined;
    $('hwCard').classList.toggle('hidden', !ok);
    if (!ok) return;
    hwState = s;
    $('hwWheels').checked = !!s.hw_wheels;
    $('hwHead').checked = !!s.hw_head;
    $('hwRc').checked = !!s.hw_rc;
    const wheels = !!s.hw_wheels, head = !!s.hw_head;
    const hide = (id, h) => { const el = $(id); if (el) el.classList.toggle('hidden', h); };
    hide('wheelCard', !wheels);
    hide('setMotion', !wheels);
    hide('lifeWheelCol', !wheels);
    hide('headCard', !head);
    hide('lifeHeadCol', !head);
    hide('snsRcRow', !s.hw_rc);
    hide('sdWarn', !!s.hw_sdcard);
    const tab = (name, h) => {
      const t = document.querySelector(`.tab[data-tab="${name}"]`);
      if (t) t.classList.toggle('hidden', h);
    };
    tab('sensor', !wheels);
    tab('life', !wheels && !head);
    $('hwRcReboot').classList.toggle('hidden', !!s.hw_rc === !!s.hw_rc_running);
    setChip('hwSdChip', s.hw_sdcard ? 'ok' : 'on', s.hw_sdcard ? '✅ Có' : '❌ Không thấy');
    setChip('hwOled2Chip', s.hw_oled2 ? 'ok' : 'off', s.hw_oled2 ? '✅ Có' : 'Không có');
    if (currentTab) showTab(currentTab);
  }
  function renderSdcard(m) {
    if (typeof m.hw_sdcard !== 'number' || !hwState) return;
    hwState.hw_sdcard = m.hw_sdcard;
    $('sdWarn').classList.toggle('hidden', !!m.hw_sdcard);
    setChip('hwSdChip', m.hw_sdcard ? 'ok' : 'on', m.hw_sdcard ? '✅ Có' : '❌ Không thấy');
  }
  async function saveHardware() {
    const body = { hw_wheels: $('hwWheels').checked ? 1 : 0, hw_head: $('hwHead').checked ? 1 : 0, hw_rc: $('hwRc').checked ? 1 : 0 };
    const res = await apiPost('/api/settings', body, { toast: 'Đã lưu phần cứng' });
    if (res && res.ok !== false && hwState) applyHardware({ ...hwState, ...body });
  }

  // ===== Cập nhật firmware =====
  let fwMax = 0;
  function loadFirmware(s) {
    const ok = !!s && typeof s.fw_version === 'string';
    $('fwCard').classList.toggle('hidden', !ok);
    if (!ok) return;
    fwMax = s.fw_max_size || 0;
    setChip('fwVerChip', 'ok', s.fw_version);
    setChip('fwPartChip', 'ok', s.fw_partition);
    $('fwVerChip').title = 'Build: ' + (s.fw_built || '');
  }
  function fwSetMsg(t, err) {
    const m = $('fwMsg');
    m.textContent = t;
    m.style.color = err ? 'var(--danger)' : '';
  }
  function uploadFirmware() {
    const f = $('fwFile').files[0];
    if (!f) { toast('Chọn file .bin trước', 'err'); return; }
    if (!/\.bin$/i.test(f.name)) { toast('File phải có đuôi .bin', 'err'); return; }
    if (fwMax && f.size > fwMax) { toast('File lớn hơn phân vùng firmware của robot', 'err'); return; }
    if (!confirm(`Cài firmware "${f.name}" (${Math.round(f.size / 1024)} KB)? Robot sẽ tự khởi động lại.`)) return;
    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/api/ota');
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    $('fwBarWrap').classList.remove('hidden');
    $('fwBar').style.width = '0%';
    $('btnFwUpload').disabled = true;
    fwSetMsg('Đang tải lên… 0%');
    xhr.upload.onprogress = e => {
      if (!e.lengthComputable) return;
      const p = Math.round(e.loaded * 100 / e.total);
      $('fwBar').style.width = p + '%';
      fwSetMsg(p < 100 ? `Đang tải lên… ${p}%` : 'Đang kiểm tra và ghi firmware…');
    };
    xhr.onload = () => {
      let res = {};
      try { res = JSON.parse(xhr.responseText); } catch (_) {}
      $('btnFwUpload').disabled = false;
      if (xhr.status === 200 && res.ok) {
        fwSetMsg(res.message || 'Đã cài xong, robot đang khởi động lại…');
        toast('Cài firmware thành công', 'ok');
      } else {
        fwSetMsg(res.message || `Thất bại (HTTP ${xhr.status})`, true);
        toast('Cài firmware thất bại', 'err');
      }
    };
    xhr.onerror = () => {
      $('btnFwUpload').disabled = false;
      fwSetMsg('Mất kết nối khi đang tải lên', true);
    };
    xhr.send(f);
  }
  async function installFirmwareUrl() {
    const url = $('fwUrl').value.trim();
    if (!/^https?:\/\//i.test(url)) { toast('Link phải bắt đầu bằng http:// hoặc https://', 'err'); return; }
    if (!confirm('Robot sẽ tải firmware từ link này rồi tự khởi động lại. Tiếp tục?')) return;
    const res = await apiPost('/api/ota/url', { url }, { toast: true });
    if (res && res.ok !== false) fwSetMsg(res.message || 'Robot đang tải firmware, xem tiến độ trên màn hình robot');
  }

  function confirmReboot() {
    if (confirm('Khởi động lại robot? Robot sẽ tạm thời mất kết nối trong vài giây.')) sendCtrl('robot_reboot');
  }

  // ===== Mật khẩu =====
  function loadAuth(s) {
    const isDefault = !!(s && s.auth_default);
    $('authWarn').classList.toggle('hidden', !isDefault);
    $('setBadge').classList.toggle('hidden', !isDefault);
  }
  function pwScore(p) {
    if (p.length < 6) return p.length ? 1 : 0;
    let s = 1;
    if (p.length >= 10) s++;
    if (/[a-z]/.test(p) && /[A-Z]/.test(p)) s++;
    if (/\d/.test(p)) s++;
    if (/[^A-Za-z0-9]/.test(p)) s++;
    return Math.min(s, 4);
  }
  function updatePw() {
    const a = $('authNew').value, b = $('authNew2').value;
    $('pwLen').textContent = a.length + '/32';
    const sc = pwScore(a);
    const bar = $('pwBar');
    bar.style.width = (sc * 25) + '%';
    bar.style.background = ['var(--danger)', 'var(--danger)', 'var(--warn)', 'var(--ok)', 'var(--ok)'][sc];
    $('pwStrength').textContent = ['', 'Yếu', 'Trung bình', 'Khá', 'Mạnh'][sc];
    $('pwHint').textContent = a.length && a.length < 6 ? `Cần thêm ${6 - a.length} ký tự` : '6–32 ký tự';
    const m = $('pwMatch');
    if (!b) { m.textContent = ''; m.className = 'match'; }
    else if (a === b) { m.textContent = '✓ Hai mật khẩu khớp nhau'; m.className = 'match ok'; }
    else { m.textContent = '✗ Hai mật khẩu chưa khớp'; m.className = 'match bad'; }
    $('authNew2').classList.toggle('bad', !!b && a !== b);
    $('btnPw').disabled = !(a.length >= 6 && a.length <= 32 && a === b);
  }
  function togglePwEye(btn) {
    const inp = $(btn.dataset.target);
    const show = inp.type === 'password';
    inp.type = show ? 'text' : 'password';
    btn.setAttribute('aria-pressed', show ? 'true' : 'false');
    btn.setAttribute('aria-label', show ? 'Ẩn mật khẩu' : 'Hiện mật khẩu');
    inp.focus();
    const len = inp.value.length;
    try { inp.setSelectionRange(len, len); } catch (_) {}
  }
  async function changePassword() {
    const a = $('authNew').value, b = $('authNew2').value;
    if (a.length < 6 || a.length > 32) { toast('Mật khẩu phải từ 6 đến 32 ký tự', 'err'); return; }
    if (a !== b) { toast('Hai mật khẩu không khớp', 'err'); return; }
    $('btnPw').disabled = true;
    const res = await apiPost('/api/settings', { new_password: a });
    if (!res || res.ok === false) { updatePw(); return; }
    toast('Đã đổi mật khẩu. Trang sẽ tải lại, hãy đăng nhập bằng mật khẩu mới.', 'ok', 4000);
    setTimeout(() => location.reload(), 1800);
  }

  // ===== Init =====
  async function init() {
    try { initWs(); } catch (e) { log('INIT WS ERROR: ' + e.message); console.error(e); }
    try {
      const s = await apiGet('/api/settings');
      if (s) {
        const setRange = (id, out, v) => { if (typeof v === 'number') $(id).value = v; syncVal(out, $(id).value); };
        setRange('moveSpeed', 'vMove', s.move_speed);
        setRange('turnSpeed', 'vTurn', s.turn_speed);
        setRange('stepMs', 'vStep', s.step_ms);
        setRange('volume', 'vVol', s.volume);
        loadLife(s);
        loadSensors(s);
        applyHardware(s);
        loadFirmware(s);
        loadAuth(s);
      }
    } catch (e) { log('INIT SETTINGS ERROR: ' + e.message); }
    try { refreshStatus(); } catch (e) { log('INIT STATUS ERROR: ' + e.message); }
  }

  // ===== WebSocket =====
  let wsBackoff = 1000;
  let wsManualClose = false;

  function initWs() {
    try {
      const url = `ws://${location.host}/ws`;
      log(`WS connecting → ${url}`);
      setConn('wait', 'Đang kết nối…');
      ws = new WebSocket(url);

      ws.onopen = () => {
        wsBackoff = 1000;
        log('WS connected');
        setConn('ok', 'Đã kết nối');
        ws.send(JSON.stringify({ type: 'ping', t: Date.now() }));
        ws.send(JSON.stringify({ type: 'macro', cmd: 'list' }));
        refreshStatus();
        fetchMacroStatusOnce();
      };

      ws.onmessage = (ev) => {
        let msg = null;
        try { msg = JSON.parse(ev.data); } catch (_) {}
        if (!msg) { log('WS recv (raw): ' + ev.data); return; }

        if (msg.type === 'log') {
          const cls = msg.level === 'E' ? 'log-error' : msg.level === 'W' ? 'log-warn' : msg.level === 'D' ? 'log-debug' : 'log-info';
          appendLogLine(msg.level, msg.msg, cls);
        } else if (msg.type === 'chat_reply') {
          addChatMsg('robot', msg.text);
        } else if (msg.type === 'status') {
          if (typeof msg.wheel_ready === 'boolean') renderWheel(msg.wheel_ready, msg.left_speed || 0, msg.right_speed || 0);
          if (typeof msg.left_speed === 'number' && typeof msg.right_speed === 'number') {
            $('stSpeed').textContent = `${msg.left_speed}/${msg.right_speed}`;
            if (!joystick || !joystick.active) { $('joyL').textContent = msg.left_speed; $('joyR').textContent = msg.right_speed; }
          }
          if (typeof msg.volume === 'number') $('stVol').textContent = `${msg.volume}%`;
          renderSensors(msg);
          renderSdcard(msg);
        } else if (msg.type === 'motor') {
          $('joyL').textContent = msg.left;
          $('joyR').textContent = msg.right;
        } else if (msg.type === 'macro_status') {
          macroState.recording = !!msg.recording;
          macroState.playing = !!msg.playing;
          macroState.count = msg.count || 0;
          renderMacroStatus();
        } else if (msg.type === 'macro_list') {
          macroSavedList = msg.items || [];
          updateMacroDropdown();
        } else if (msg.type === 'macro_reply') {
          log('macro reply: ' + msg.msg + (msg.ok ? ' OK' : ' FAIL'));
          toast('Macro: ' + msg.msg, msg.ok ? 'ok' : 'err');
        } else if (msg.type === 'heartbeat' || msg.type === 'hello' || msg.type === 'pong') {
          /* silent */
        } else {
          log('WS recv: ' + ev.data);
        }
      };

      ws.onclose = (e) => {
        log(`WS closed code=${e.code}`);
        if (joystick && joystick.active) joystick.forceStop();
        if (wsManualClose) { wsManualClose = false; setConn('err', 'Đã ngắt'); return; }
        setConn('err', `Mất kết nối · thử lại sau ${Math.round(wsBackoff / 1000)}s`);
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
      this.svg = $('joyBase');
      this.knob = $('joyKnob');
      if (!this.svg || !this.knob) return;
      this.radius = 62;
      this.deadzone = 0.08;
      this.active = false;
      this.pointerId = null;
      this.x = 0; this.y = 0;
      this.lastSendMs = 0;
      this.sendInterval = 50;
      this.autoSendTimer = null;
      this.bind();
    }
    bind() {
      this.svg.addEventListener('pointerdown', e => this.onDown(e));
      this.svg.addEventListener('pointermove', e => this.onMove(e));
      this.svg.addEventListener('pointerup', e => this.onUp(e));
      this.svg.addEventListener('pointercancel', e => this.onUp(e));
      this.svg.addEventListener('lostpointercapture', e => this.onUp(e));
    }
    toLocal(e) {
      const r = this.svg.getBoundingClientRect();
      const cx = r.left + r.width / 2, cy = r.top + r.height / 2;
      const scale = 200 / r.width;
      return { x: (e.clientX - cx) * scale, y: (e.clientY - cy) * scale };
    }
    onDown(e) {
      if (this.active) return;
      this.active = true;
      this.pointerId = e.pointerId;
      this.svg.classList.add('active');
      try { this.svg.setPointerCapture(e.pointerId); } catch (_) {}
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
      try { this.svg.releasePointerCapture(e.pointerId); } catch (_) {}
      this.forceStop();
    }
    forceStop() {
      this.active = false;
      this.pointerId = null;
      this.svg.classList.remove('active');
      this.stopAutoSend();
      this.reset();
    }
    update(e) {
      const p = this.toLocal(e);
      const mag = Math.hypot(p.x, p.y);
      if (mag > this.radius) { p.x = p.x / mag * this.radius; p.y = p.y / mag * this.radius; }
      this.knob.setAttribute('cx', p.x);
      this.knob.setAttribute('cy', p.y);
      let nx = p.x / this.radius, ny = p.y / this.radius;
      if (Math.abs(nx) < this.deadzone) nx = 0;
      if (Math.abs(ny) < this.deadzone) ny = 0;
      this.x = nx;
      this.y = -ny;   // kéo lên = tiến = y dương
      this.render();
      const now = performance.now();
      if (now - this.lastSendMs >= this.sendInterval) { this.lastSendMs = now; this.send(); }
    }
    reset() {
      this.x = 0; this.y = 0;
      this.knob.setAttribute('cx', 0);
      this.knob.setAttribute('cy', 0);
      this.render();
      this.send();
    }
    startAutoSend() {
      if (this.autoSendTimer) return;
      this.autoSendTimer = setInterval(() => { if (this.active) this.send(); }, 150); // giữ watchdog 300ms
    }
    stopAutoSend() {
      if (this.autoSendTimer) { clearInterval(this.autoSendTimer); this.autoSendTimer = null; }
    }
    send() {
      if (!wsOpen()) return;
      ws.send(JSON.stringify({ type: 'vector', x: this.x, y: this.y }));
    }
    render() {
      const l = Math.round(joyClamp(this.y + this.x, -1, 1) * 100);
      const r = Math.round(joyClamp(this.y - this.x, -1, 1) * 100);
      $('joyX').textContent = this.x.toFixed(2);
      $('joyY').textContent = this.y.toFixed(2);
      $('joyL').textContent = l;
      $('joyR').textContent = r;
    }
  }

  // ===== Macro =====
  let macroPoller = null;
  let macroPollStart = 0;
  let macroSavedList = [];

  function fetchMacroStatusOnce() {
    fetch('/api/macro/status', { cache: 'no-store' })
      .then(r => r.json())
      .then(msg => {
        macroState.recording = !!msg.recording;
        macroState.playing = !!msg.playing;
        macroState.count = msg.count || 0;
        renderMacroStatus();
        // Tự tắt polling khi đã hết ghi/phát (tránh poll mãi sau khi macro phát xong)
        if (macroPoller && !macroState.recording && !macroState.playing && Date.now() - macroPollStart > 2500) stopMacroPolling();
      })
      .catch(() => {});
  }
  function startMacroPolling() {
    macroPollStart = Date.now();
    if (macroPoller) return;
    macroPoller = setInterval(fetchMacroStatusOnce, 1000);
    fetchMacroStatusOnce();
  }
  function stopMacroPolling() {
    if (macroPoller) { clearInterval(macroPoller); macroPoller = null; }
  }
  function sendMacroCmd(cmd) {
    if (!wsSend({ type: 'macro', cmd: cmd })) return;
    log('macro → ' + cmd);
    setTimeout(fetchMacroStatusOnce, 150);
    if (cmd === 'start_rec' || cmd === 'play') startMacroPolling();
    else if (cmd === 'stop_rec' || cmd === 'stop_play' || cmd === 'clear') {
      setTimeout(fetchMacroStatusOnce, 500);
      setTimeout(fetchMacroStatusOnce, 1500);
    }
  }
  function renderMacroStatus() {
    const st = $('macroStatus'), btnRec = $('btnMacroRec'), btnPlay = $('btnMacroPlay');
    if (!st || !btnRec || !btnPlay) return;
    const { recording: rec, playing: play, count: n } = macroState;
    btnRec.textContent = rec ? '■ Dừng ghi' : '● Bắt đầu ghi';
    btnRec.classList.toggle('active', rec);
    btnPlay.textContent = play ? '■ Dừng phát' : '▶ Phát lại';
    btnPlay.classList.toggle('active', play);
    btnPlay.disabled = rec || (!play && n === 0);
    $('btnMacroClear').disabled = rec || play || n === 0;
    if (rec) { st.textContent = '● Đang ghi… (' + n + ' bước)'; st.className = 'macro-status rec'; }
    else if (play) { st.textContent = '▶ Đang phát… (' + n + ' bước)'; st.className = 'macro-status play'; }
    else if (n > 0) { st.textContent = '✓ Có ' + n + ' bước trong bộ nhớ'; st.className = 'macro-status'; }
    else { st.textContent = 'Chưa có macro'; st.className = 'macro-status'; }
  }
  function updateMacroDropdown() {
    const sel = $('macroSavedList');
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
    const inp = $('macroNameInput');
    const name = (inp.value || '').trim();
    if (!name) { toast('Nhập tên macro trước', 'warn'); inp.focus(); return; }
    if (!/^[a-zA-Z0-9_-]+$/.test(name)) { toast('Tên chỉ được dùng chữ không dấu, số, _ và -', 'err'); inp.classList.add('bad'); return; }
    inp.classList.remove('bad');
    if (macroSavedList.includes(name) && !confirm('Đã có macro "' + name + '". Ghi đè?')) return;
    if (wsSend({ type: 'macro', cmd: 'save', name })) log('macro → save ' + name);
  }
  function sendMacroLoad() {
    const name = $('macroSavedList').value;
    if (!name) { toast('Chọn macro để tải', 'warn'); return; }
    if (wsSend({ type: 'macro', cmd: 'load', name })) { log('macro → load ' + name); setTimeout(fetchMacroStatusOnce, 300); }
  }
  function sendMacroDelete() {
    const name = $('macroSavedList').value;
    if (!name) { toast('Chọn macro để xoá', 'warn'); return; }
    if (!confirm('Xoá file macro "' + name + '"?')) return;
    if (wsSend({ type: 'macro', cmd: 'delete', name })) log('macro → delete ' + name);
  }

  // ===== Dừng khẩn cấp =====
  function emergencyStop() {
    if (joystick && joystick.active) joystick.forceStop();
    else if (wsOpen()) ws.send(JSON.stringify({ type: 'vector', x: 0, y: 0 }));
    if (macroState.playing && wsOpen()) ws.send(JSON.stringify({ type: 'macro', cmd: 'stop_play' }));
    sendCtrl('stop');
    toast('Đã gửi lệnh DỪNG', 'warn', 1500);
  }

  // ===== Bàn phím =====
  const KEYMAP = {
    ArrowUp: 'forward', KeyW: 'forward', ArrowDown: 'backward', KeyS: 'backward',
    ArrowLeft: 'left', KeyA: 'left', ArrowRight: 'right', KeyD: 'right', Space: 'stop'
  };
  let lastKeySend = 0;
  function flashBtn(cmd) {
    const b = document.querySelector(`.dpad [data-cmd="${cmd}"]`);
    if (!b) return;
    b.classList.add('pressed');
    setTimeout(() => b.classList.remove('pressed'), 160);
  }
  function onKey(e) {
    if (e.key === 'Escape') { emergencyStop(); return; }
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    if (e.target.closest && e.target.closest('input, textarea, select, [contenteditable]')) return;
    if (currentTab !== 'control') return;
    const cmd = KEYMAP[e.code];
    if (!cmd) return;
    e.preventDefault();
    const now = Date.now();
    const gap = cmd === 'stop' ? 0 : Math.max(120, Number($('stepMs').value) * 0.8);
    if (e.repeat && now - lastKeySend < gap) return;
    lastKeySend = now;
    flashBtn(cmd);
    sendCtrl(cmd);
  }

  // ===== Boot =====
  window.addEventListener('load', () => {
    applyTheme(LS.get('theme', 'auto'));
    $('themeBtn').addEventListener('click', cycleTheme);

    document.querySelectorAll('.tab').forEach(t => t.addEventListener('click', () => showTab(t.dataset.tab)));
    showTab(LS.get('tab', 'control'));

    log('page loaded');
    init();

    // Điều khiển + servo + gọi robot (data-cmd)
    document.addEventListener('click', e => {
      const c = e.target.closest('[data-cmd]');
      if (c) { sendCtrl(c.dataset.cmd); return; }
      const m = e.target.closest('[data-music]');
      if (m) { sendMusic(m.dataset.music, m.dataset.mood ? { mood: m.dataset.mood } : {}); return; }
      const h = e.target.closest('[data-hint]');
      if (h) {
        const el = $(h.dataset.hint);
        if (el) { const open = el.classList.toggle('hidden') === false; h.setAttribute('aria-expanded', open ? 'true' : 'false'); }
        return;
      }
      const n = e.target.closest('[data-nudge]');
      if (n) { testNudge(n.dataset.nudge); return; }
      const say = e.target.closest('[data-say]');
      if (say) { sendChat(say.dataset.say); return; }
      const eye = e.target.closest('.pw-eye');
      if (eye) togglePwEye(eye);
    });
    document.addEventListener('keydown', onKey);
    window.addEventListener('blur', () => { if (joystick && joystick.active) joystick.forceStop(); });

    $('btnReboot').addEventListener('click', confirmReboot);
    $('estop').addEventListener('click', emergencyStop);

    // Chat & nhạc
    $('btnChatSend').addEventListener('click', () => sendChat());
    $('chatInput').addEventListener('keydown', e => { if (e.key === 'Enter') sendChat(); });
    $('btnMusicSearch').addEventListener('click', searchMusic);
    $('musicQuery').addEventListener('keydown', e => { if (e.key === 'Enter') searchMusic(); });

    // Cài đặt
    document.querySelectorAll('#setCard input[type=range]').forEach(r => r.addEventListener('input', () => {
      syncVal(r.dataset.out, r.value);
      $('setDirty').classList.remove('hidden');
    }));
    $('btnSaveSettings').addEventListener('click', saveSettings);
    $('btnSaveLife').addEventListener('click', saveLife);
    $('btnSaveSns').addEventListener('click', saveSensors);
    $('btnSaveHw').addEventListener('click', saveHardware);
    $('btnFwUpload').addEventListener('click', uploadFirmware);
    $('btnFwUrl').addEventListener('click', installFirmwareUrl);
    document.querySelectorAll('#snsCard input[type=number]').forEach(i => i.addEventListener('input', () => i.classList.remove('bad')));
    document.querySelectorAll('#lifeCard input[type=number]').forEach(i => i.addEventListener('input', () => i.classList.remove('bad')));

    // Mật khẩu
    $('authNew').addEventListener('input', updatePw);
    $('authNew2').addEventListener('input', updatePw);
    $('authNew2').addEventListener('keydown', e => { if (e.key === 'Enter' && !$('btnPw').disabled) changePassword(); });
    $('btnPw').addEventListener('click', changePassword);

    // Nhật ký
    $('logFilter').addEventListener('change', e => { logFilter = e.target.value; applyLogFilter(); });
    $('logAuto').addEventListener('change', e => { logAuto = e.target.checked; });
    $('btnLogClear').addEventListener('click', () => { $('logBox').innerHTML = ''; unseenErr = 0; updateErrBadge(); });
    $('btnLogSave').addEventListener('click', downloadLog);

    // Joystick
    try { joystick = new VirtualJoystick(); log('VirtualJoystick ready'); }
    catch (e) { log('Joystick init error: ' + e.message); }

    // Chế độ D-pad / Joystick
    const dpadEl = document.querySelector('.dpad');
    const joyEl = document.querySelector('.joy');
    const readEl = document.querySelector('.readout');
    function applyMode(mode) {
      dpadEl.classList.toggle('hidden', mode === 'joystick');
      joyEl.classList.toggle('hidden', mode === 'dpad');
      readEl.classList.toggle('hidden', mode === 'dpad');
      document.querySelectorAll('#joyModeSeg label').forEach(l => l.classList.toggle('on', l.querySelector('input').value === mode));
      LS.set('joyMode', mode);
    }
    document.querySelectorAll('input[name="joyMode"]').forEach(r => r.addEventListener('change', e => applyMode(e.target.value)));
    const savedMode = LS.get('joyMode', 'both');
    const savedRadio = document.querySelector(`input[name="joyMode"][value="${savedMode}"]`);
    if (savedRadio) savedRadio.checked = true;
    applyMode(savedRadio ? savedMode : 'both');

    // Macro
    $('btnMacroRec').addEventListener('click', () => sendMacroCmd(macroState.recording ? 'stop_rec' : 'start_rec'));
    $('btnMacroPlay').addEventListener('click', () => sendMacroCmd(macroState.playing ? 'stop_play' : 'play'));
    $('btnMacroClear').addEventListener('click', () => { if (confirm('Xoá macro đang có trong bộ nhớ tạm?')) sendMacroCmd('clear'); });
    $('btnMacroSave').addEventListener('click', sendMacroSave);
    $('macroNameInput').addEventListener('keydown', e => { if (e.key === 'Enter') sendMacroSave(); });
    $('btnMacroLoad').addEventListener('click', sendMacroLoad);
    $('btnMacroDelete').addEventListener('click', sendMacroDelete);
    document.querySelectorAll('#setCard input[type=range]').forEach(r => syncVal(r.dataset.out, r.value));
    renderMacroStatus();
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
    webauth::Init();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_open_sockets = 10;
    config.lru_purge_enable = true;
    config.stack_size = 8192;
    config.max_uri_handlers = 16;  // mac dinh 8 da dung het

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

    httpd_uri_t ota_upload_uri = {
        .uri = "/api/ota",
        .method = HTTP_POST,
        .handler = &RobotWebUiServer::ApiOtaUploadHandler,
        .user_ctx = this,
    };
    httpd_uri_t ota_url_uri = {
        .uri = "/api/ota/url",
        .method = HTTP_POST,
        .handler = &RobotWebUiServer::ApiOtaUrlHandler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(server_handle_, &ota_upload_uri);
    httpd_register_uri_handler(server_handle_, &ota_url_uri);

    // WebUI da chay -> firmware moi (neu vua cap nhat) duoc coi la tot, huy rollback.
    MarkFirmwareValidIfPending();
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
    if (!webauth::Check(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, kWebUiHtml, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t RobotWebUiServer::ApiStatusHandler(httpd_req_t* req) {
    if (!webauth::Check(req)) {
        return ESP_OK;
    }
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleStatus(req);
}

esp_err_t RobotWebUiServer::ApiSettingsGetHandler(httpd_req_t* req) {
    if (!webauth::Check(req)) {
        return ESP_OK;
    }
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleSettingsGet(req);
}

esp_err_t RobotWebUiServer::ApiSettingsPostHandler(httpd_req_t* req) {
    if (!webauth::Check(req)) {
        return ESP_OK;
    }
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleSettingsPost(req);
}

esp_err_t RobotWebUiServer::ApiControlHandler(httpd_req_t* req) {
    if (!webauth::Check(req)) {
        return ESP_OK;
    }
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleControl(req);
}

// ===================== Cap nhat firmware qua WebUI =====================

void RobotWebUiServer::AppendFirmwareInfo(cJSON* root) {
    const esp_app_desc_t* desc = esp_app_get_description();
    cJSON_AddStringToObject(root, "fw_version", desc->version);
    cJSON_AddStringToObject(root, "fw_project", desc->project_name);
    std::string built = std::string(desc->date) + " " + desc->time;
    cJSON_AddStringToObject(root, "fw_built", built.c_str());
    const esp_partition_t* running = esp_ota_get_running_partition();
    cJSON_AddStringToObject(root, "fw_partition", running != nullptr ? running->label : "?");
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    cJSON_AddNumberToObject(root, "fw_max_size", next != nullptr ? next->size : 0);
}

void RobotWebUiServer::MarkFirmwareValidIfPending() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "Firmware moi chay tot (WebUI da len) -> huy rollback");
    }
}

static void SendOtaResult(httpd_req_t* req, int status, bool ok, const std::string& message) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", ok);
    cJSON_AddStringToObject(root, "message", message.c_str());
    char* text = cJSON_PrintUnformatted(root);
    std::string payload = text ? text : "{}";
    if (text != nullptr) {
        cJSON_free(text);
    }
    cJSON_Delete(root);
    httpd_resp_set_status(req, status == 200 ? "200 OK" : (status == 409 ? "409 Conflict" : "400 Bad Request"));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, payload.c_str(), payload.size());
}

static void RestartSoon() {
    static esp_timer_handle_t timer = nullptr;
    if (timer == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = [](void*) { esp_restart(); };
        args.name = "ota_restart";
        esp_timer_create(&args, &timer);
    }
    esp_timer_start_once(timer, 1500 * 1000);
}

// Kiem tra goi dau file: dung la app image ESP32-S3 cua cung du an.
static bool ValidateAppImage(const uint8_t* data, size_t len, std::string& err, std::string& version) {
    constexpr size_t kDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (len < kDescOffset + sizeof(esp_app_desc_t)) {
        err = "File qua nho hoac khong phai firmware";
        return false;
    }
    const auto* header = reinterpret_cast<const esp_image_header_t*>(data);
    if (header->magic != ESP_IMAGE_HEADER_MAGIC) {
        err = "Khong phai file firmware ESP32 (.bin cua app)";
        return false;
    }
    if (header->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
        err = "Firmware danh cho chip khac, khong phai ESP32-S3";
        return false;
    }
    const auto* desc = reinterpret_cast<const esp_app_desc_t*>(data + kDescOffset);
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        err = "File khong co thong tin app (co the la file merged/bootloader)";
        return false;
    }
    const esp_app_desc_t* running = esp_app_get_description();
    if (strncmp(desc->project_name, running->project_name, sizeof(desc->project_name)) != 0) {
        err = std::string("Firmware cua du an khac: ") + desc->project_name;
        return false;
    }
    version.assign(desc->version, strnlen(desc->version, sizeof(desc->version)));
    return true;
}

esp_err_t RobotWebUiServer::ApiOtaUploadHandler(httpd_req_t* req) {
    // Giong webauth::Check nhung body la file nhi phan (application/octet-stream).
    if (esp_timer_get_time() < webauth::detail::g_locked_until_us.load()) {
        webauth::detail::SendLocked(req);
        return ESP_OK;
    }
    if (!webauth::IsAuthorized(req)) {
        webauth::detail::SendChallenge(req);
        return ESP_OK;
    }
    std::string ct;
    if (!webauth::detail::GetHeader(req, "Content-Type", ct, 128) ||
        ct.compare(0, 24, "application/octet-stream") != 0) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_send(req, "Yeu cau khong hop le", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleOtaUpload(req);
}

esp_err_t RobotWebUiServer::HandleOtaUpload(httpd_req_t* req) {
    if (ota_busy_.exchange(true)) {
        SendOtaResult(req, 409, false, "Robot dang cap nhat firmware, vui long cho");
        return ESP_OK;
    }
    struct BusyReset {
        std::atomic<bool>& flag;
        ~BusyReset() { flag.store(false); }
    } busy_reset{ota_busy_};

    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    const size_t total = req->content_len;
    if (part == nullptr) {
        SendOtaResult(req, 400, false, "Khong tim thay phan vung OTA");
        return ESP_OK;
    }
    if (total < 64 * 1024 || total > part->size) {
        SendOtaResult(req, 400, false, "Kich thuoc file khong hop le (toi da " +
                                           std::to_string(part->size / 1024) + " KB)");
        return ESP_OK;
    }
    if (wheel_motor_ != nullptr) {
        wheel_motor_->Stop();
    }
    ESP_LOGW(TAG, "OTA qua WebUI: %u bytes -> phan vung %s", static_cast<unsigned>(total), part->label);

    constexpr size_t kChunk = 4096;
    uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kChunk, MALLOC_CAP_SPIRAM));
    if (buf == nullptr) {
        buf = static_cast<uint8_t*>(malloc(kChunk));
    }
    if (buf == nullptr) {
        SendOtaResult(req, 400, false, "Khong du bo nho de cap nhat");
        return ESP_OK;
    }

    esp_ota_handle_t handle = 0;
    bool begun = false;
    size_t received = 0;
    int timeouts = 0;
    std::string err;
    std::string new_version;
    int last_pct = -1;

    while (received < total) {
        int r = httpd_req_recv(req, reinterpret_cast<char*>(buf), std::min(kChunk, total - received));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > 5) {
                err = "Het thoi gian cho du lieu";
                break;
            }
            continue;
        }
        if (r <= 0) {
            err = "Mat ket noi khi dang tai len";
            break;
        }
        timeouts = 0;
        if (!begun) {
            if (!ValidateAppImage(buf, r, err, new_version)) {
                break;
            }
            // Ghi tuan tu: xoa flash dan theo tung khoi, khong treo vai giay luc bat dau.
            if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
                err = "Khong mo duoc phan vung OTA";
                break;
            }
            begun = true;
        }
        if (esp_ota_write(handle, buf, r) != ESP_OK) {
            err = "Ghi flash that bai";
            break;
        }
        received += r;
        int pct = static_cast<int>(received * 100 / total);
        if (pct / 10 != last_pct / 10) {
            last_pct = pct;
            ESP_LOGI(TAG, "OTA %d%%", pct);
        }
    }
    heap_caps_free(buf);

    if (!err.empty()) {
        if (begun) {
            esp_ota_abort(handle);
        }
        ESP_LOGE(TAG, "OTA that bai: %s", err.c_str());
        SendOtaResult(req, 400, false, err);
        return ESP_OK;
    }
    esp_err_t end_err = esp_ota_end(handle);
    if (end_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(end_err));
        SendOtaResult(req, 400, false, end_err == ESP_ERR_OTA_VALIDATE_FAILED
                                           ? "File firmware bi loi (kiem tra checksum that bai)"
                                           : "Khong hoan tat duoc cap nhat");
        return ESP_OK;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        SendOtaResult(req, 400, false, "Khong dat duoc phan vung khoi dong");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "OTA xong: phien ban %s, khoi dong lai", new_version.c_str());
    SendOtaResult(req, 200, true, "Da cai firmware " + new_version + ". Robot dang khoi dong lai...");
    RestartSoon();
    return ESP_OK;
}

esp_err_t RobotWebUiServer::ApiOtaUrlHandler(httpd_req_t* req) {
    if (!webauth::Check(req)) {
        return ESP_OK;
    }
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleOtaUrl(req);
}

esp_err_t RobotWebUiServer::HandleOtaUrl(httpd_req_t* req) {
    std::string body;
    if (!ReadRequestBody(req, body, 1024)) {
        SendOtaResult(req, 400, false, "Du lieu khong hop le");
        return ESP_OK;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    const cJSON* url_json = root ? cJSON_GetObjectItem(root, "url") : nullptr;
    std::string url = (cJSON_IsString(url_json) && url_json->valuestring) ? url_json->valuestring : "";
    cJSON_Delete(root);
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        SendOtaResult(req, 400, false, "Link phai bat dau bang http:// hoac https://");
        return ESP_OK;
    }
    if (ota_busy_.load()) {
        SendOtaResult(req, 409, false, "Robot dang cap nhat firmware, vui long cho");
        return ESP_OK;
    }
    if (wheel_motor_ != nullptr) {
        wheel_motor_->Stop();
    }
    ESP_LOGW(TAG, "OTA tu link: %s", url.c_str());
    // Chay tren task chinh giong luong cap nhat goc (hien tien do tren man hinh, xong tu reboot).
    Application::GetInstance().Schedule([url]() {
        Application::GetInstance().UpgradeFirmware(url);
    });
    SendOtaResult(req, 200, true, "Robot dang tai firmware, xem tien do tren man hinh robot");
    return ESP_OK;
}

esp_err_t RobotWebUiServer::ApiMusicHandler(httpd_req_t* req) {
    if (!webauth::Check(req)) {
        return ESP_OK;
    }
    auto* self = static_cast<RobotWebUiServer*>(req->user_ctx);
    return self->HandleMusic(req);
}

void RobotWebUiServer::BuildStatusJson(std::string& out, bool include_type) {
    int left_speed  = current_left_speed_;
    int right_speed = current_right_speed_;
    bool wheel_ready = (wheel_motor_ != nullptr) && wheel_motor_->IsReady();
    if (wheel_ready) {
        wheel_motor_->GetSpeed(left_speed, right_speed);  // toc do thuc (sau bo loc cam bien)
    }

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
    if (sensor_hub_ != nullptr) {
        sensor_hub_->AppendStatusToJson(root);
    }
    if (hardware_ != nullptr) {
        hardware_->AppendStatusToJson(root);
    }

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
    if (sensor_hub_ != nullptr) {
        sensor_hub_->AppendConfigToJson(root);
    }
    if (hardware_ != nullptr) {
        hardware_->AppendToJson(root);
    }
    cJSON_AddNumberToObject(root, "auth_default", webauth::IsDefaultPassword() ? 1 : 0);
    AppendFirmwareInfo(root);

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
    if (sensor_hub_ != nullptr) {
        sensor_hub_->ApplyConfigFromJson(root);
    }
    if (hardware_ != nullptr) {
        hardware_->ApplyFromJson(root);
    }
    auto* new_pw = cJSON_GetObjectItem(root, "new_password");
    if (cJSON_IsString(new_pw) && new_pw->valuestring != nullptr) {
        if (!webauth::SetPassword(new_pw->valuestring)) {
            cJSON_Delete(root);
            SendJson(req, 400, "{\"ok\":false,\"message\":\"Mat khau phai tu 6 den 32 ky tu\"}");
            return ESP_OK;
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
    if (req->method == HTTP_GET && !(webauth::OriginOk(req) && webauth::IsAuthorized(req))) {
        ESP_LOGW(TAG, "WS handshake bi tu choi (chua dang nhap hoac sai Origin)");
        httpd_sess_trigger_close(self->server_handle_, httpd_req_to_sockfd(req));
        return ESP_FAIL;
    }
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
        if (ready) {
            wheel_motor_->GetSpeed(left, right);
        }
        uint32_t sensor_stamp = (sensor_hub_ != nullptr) ? sensor_hub_->StatusStamp() : 0;
        if (hardware_ != nullptr) {
            sensor_stamp += hardware_->StatusStamp() * 7919u;  // gop 2 bo dem thay doi
        }

        int vol = 0;
        auto codec = Board::GetInstance().GetAudioCodec();
        if (codec != nullptr) {
            vol = codec->output_volume();
        }

        // Chỉ broadcast khi có thay đổi
        if (left  == last_left_speed_  &&
            right == last_right_speed_ &&
            vol   == last_volume_      &&
            ready == last_wheel_ready_ &&
            sensor_stamp == last_sensor_stamp_) {
            continue;
        }
        last_sensor_stamp_ = sensor_stamp;

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
    if (!webauth::Check(req)) {
        return ESP_OK;
    }
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