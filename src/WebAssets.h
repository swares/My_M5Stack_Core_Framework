#pragma once
// ============================================================
//  WebAssets.h  -  Embedded dashboard / setup / settings pages
//
//  The dashboard, first-boot setup portal, and settings editor
//  are self-contained HTML+CSS+JS pages stored in PROGMEM.  They
//  were moved out of WebAPI.cpp (which is now about the server
//  logic) by tools/apply_refactors.py.  Included once, by
//  WebAPI.cpp, inside its #if OUT_WEB block.
// ============================================================
#include <Arduino.h>  // PROGMEM

static const char DASH_HTML[] PROGMEM = R"==(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5Stack I2C Framework</title>
<style>
:root {
  --bg:#0d1117; --surface:#161b22; --surface-2:#1c222b; --border:#30363d;
  --text:#e6edf3; --dim:#8b949e; --accent:#58a6ff;
  --green:#3fb950; --orange:#d29922; --red:#f85149;
  --teal:#39d353; --radius:10px; --row-border:#1e2430;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh}

/* ── Header ── */
.topbar{background:linear-gradient(135deg,#0d2137,#0a1628);
  border-bottom:1px solid var(--border);padding:14px 20px;
  display:flex;align-items:center;gap:14px}
.logo{width:36px;height:36px;background:var(--accent);border-radius:8px;
  display:flex;align-items:center;justify-content:center;font-size:18px;flex-shrink:0}
.topbar h1{font-size:1.1rem;font-weight:700;letter-spacing:.02em}
.topbar p{font-size:.75rem;color:var(--dim)}
.topbar .right{margin-left:auto;display:flex;gap:10px;align-items:center}
.pill{font-size:.7rem;padding:3px 10px;border-radius:20px;border:1px solid var(--border);color:var(--dim)}
.pill.ok{border-color:var(--green);color:var(--green)}
.pill.err{border-color:var(--red);color:var(--red)}

/* ── Toolbar ── */
.toolbar{padding:12px 20px;display:flex;gap:10px;align-items:center;
  border-bottom:1px solid var(--border);flex-wrap:wrap}
button{background:var(--accent);color:#04111f;border:none;border-radius:6px;
  padding:7px 16px;cursor:pointer;font-size:.82rem;font-weight:600;
  transition:.15s;font-family:inherit}
button:hover{opacity:.85}
button.ghost{background:transparent;color:var(--dim);border:1px solid var(--border)}
button.ghost:hover{color:var(--text);border-color:var(--dim)}
#ts{color:var(--dim);font-size:.75rem;margin-left:6px}

/* ── Endpoint list ── */
.ep{font-size:.72rem;color:var(--dim);padding:0 20px 8px;
  display:flex;flex-wrap:wrap;gap:6px;align-items:center}
.ep .label{color:var(--dim);margin-right:4px}
.ep a, .ep span.chip{background:var(--surface);border:1px solid var(--border);
  border-radius:4px;padding:2px 8px;font-family:ui-monospace,Menlo,monospace;
  color:var(--text);text-decoration:none;transition:.15s;font-size:.72rem}
.ep a:hover{border-color:var(--accent);color:var(--accent)}
.ep span.chip{color:var(--dim);cursor:default}
.ep .method{color:var(--accent);font-weight:600;margin-right:4px}

/* ── Section headers ── */
.section-head{padding:14px 20px 4px;display:flex;align-items:baseline;gap:10px}
.section-head h2{font-size:.76rem;font-weight:700;color:var(--dim);
  letter-spacing:.12em;text-transform:uppercase}
.section-head .count{font-size:.7rem;color:var(--dim);background:var(--surface);
  border:1px solid var(--border);border-radius:20px;padding:1px 8px;
  font-family:ui-monospace,monospace}
.section-head .hint{margin-left:auto;font-size:.7rem;color:var(--dim)}

/* ── Card grids ── */
.grid{display:grid;gap:14px;padding:6px 20px 8px}
.grid.controls{grid-template-columns:repeat(auto-fill,minmax(330px,1fr))}
.grid.sensors{grid-template-columns:repeat(auto-fill,minmax(230px,1fr))}
.empty-note{color:var(--dim);font-size:.78rem;padding:6px 20px 14px}
.card{background:var(--surface);border:1px solid var(--border);
  border-radius:var(--radius);overflow:hidden;transition:.2s;position:relative}
.card.controllable{border-color:#3b4253}
.card-head{padding:10px 14px;display:flex;align-items:center;gap:8px;
  border-bottom:1px solid var(--border);cursor:pointer;flex-wrap:wrap}
.card-head:hover h2{color:var(--accent)}
.card-head h2{font-size:.8rem;font-weight:700;letter-spacing:.04em;
  text-transform:uppercase;flex:1;min-width:0}
.dot{width:8px;height:8px;border-radius:50%;background:var(--dim);flex-shrink:0}
.dot.on{background:var(--green);box-shadow:0 0 6px var(--green)}
.addr{font-size:.65rem;color:var(--dim);background:#0d1117;
  padding:1px 6px;border-radius:4px;font-family:ui-monospace,monospace}
.bus-tag{font-size:.6rem;color:var(--accent);
  background:#1a2a3a;padding:1px 5px;border-radius:3px}
.mount-tag{font-size:.6rem;color:var(--dim);
  background:#21262d;padding:1px 5px;border-radius:3px;letter-spacing:.03em}
.ctrl-tag{font-size:.6rem;color:#04111f;background:var(--orange);
  padding:1px 5px;border-radius:3px;font-weight:700}
.card-body{padding:10px 14px}

/* ── Reading rows ── */
.row{display:flex;justify-content:space-between;align-items:baseline;
  padding:5px 0;border-bottom:1px solid var(--row-border)}
.row:last-child{border:none}
.rk{color:var(--dim);font-size:.78rem}
.rv{font-weight:600;font-size:1rem;font-variant-numeric:tabular-nums}
.ru{font-size:.7rem;color:var(--orange);margin-left:3px}
.readings:empty{display:none}
.readings:not(:empty)+.controls{margin-top:8px;padding-top:8px;
  border-top:1px solid var(--row-border)}

/* ── Control widgets ── */
.ctrl-group-label{font-size:.64rem;color:var(--dim);text-transform:uppercase;
  letter-spacing:.09em;font-weight:700;margin:9px 0 1px}
.ctrl-group-label:first-child{margin-top:1px}
.ctrl-row{display:flex;align-items:center;gap:10px;padding:5px 0}
.ctrl-label{font-size:.78rem;color:var(--text);flex:1;min-width:0}
.slider-row .ctrl-label{flex:0 0 86px}
/* toggle */
.toggle{appearance:none;-webkit-appearance:none;width:38px;height:22px;
  background:#0d1117;border:1px solid var(--border);border-radius:99px;
  position:relative;cursor:pointer;transition:.15s;flex-shrink:0}
.toggle::after{content:"";position:absolute;left:2px;top:1px;
  width:16px;height:16px;border-radius:50%;background:var(--dim);
  transition:.18s cubic-bezier(.4,1.4,.6,1)}
.toggle:checked{background:#1e3a1f;border-color:#2e5e34}
.toggle:checked::after{transform:translateX(16px);background:var(--green);
  box-shadow:0 0 8px var(--green)}
/* slider */
.slider{appearance:none;-webkit-appearance:none;height:4px;flex:1;min-width:0;
  background:#0d1117;border-radius:99px;outline:none;cursor:pointer;
  border:1px solid var(--border)}
.slider::-webkit-slider-thumb{appearance:none;-webkit-appearance:none;
  width:14px;height:14px;background:var(--accent);border-radius:50%;
  border:2px solid #04111f;cursor:pointer}
.slider::-moz-range-thumb{width:14px;height:14px;background:var(--accent);
  border-radius:50%;border:2px solid #04111f;cursor:pointer}
.readout{font-family:ui-monospace,Menlo,monospace;font-variant-numeric:tabular-nums;
  font-size:.8rem;color:var(--text);background:#0d1117;
  border:1px solid var(--border);border-radius:4px;padding:1px 6px;
  min-width:62px;text-align:right;flex-shrink:0}
.readout .u{color:var(--dim);font-size:.68rem;margin-left:2px}
/* colour */
.cpick{width:34px;height:24px;border:1px solid var(--border);
  background:none;cursor:pointer;padding:0;border-radius:4px;flex-shrink:0}
.hex{font-family:ui-monospace,Menlo,monospace;font-size:.74rem;color:var(--dim);
  min-width:66px;text-align:right}
/* text */
.tinput{flex:1;background:#0d1117;border:1px solid var(--border);
  border-radius:4px;color:var(--text);padding:4px 8px;font-size:.78rem;
  font-family:ui-monospace,Menlo,monospace;min-width:0}
.ctrl-send{padding:4px 12px;font-size:.74rem}
/* quick-action button strip */
.qab{display:flex;gap:6px;margin-top:10px;flex-wrap:wrap}
.qab-btn{padding:4px 10px;font-size:.72rem;background:var(--surface-2);
  color:var(--text);border:1px solid var(--border);font-weight:500}
.qab-btn:hover{border-color:var(--accent);color:var(--accent);opacity:1}

/* ── Request log ── */
.reqlog{margin:8px 20px 16px;background:var(--surface);
  border:1px solid var(--border);border-radius:var(--radius);overflow:hidden}
.reqlog-head{padding:8px 14px;border-bottom:1px solid var(--border);
  display:flex;align-items:center;gap:10px;color:var(--dim);
  font-size:.72rem;letter-spacing:.08em;text-transform:uppercase}
.reqlog-head .count{margin-left:auto;text-transform:none;letter-spacing:0;
  font-family:ui-monospace,monospace}
.reqlog-body{max-height:170px;overflow-y:auto;
  font-family:ui-monospace,Menlo,monospace;font-size:.72rem}
.reqlog-row{display:grid;grid-template-columns:78px 40px 1fr 52px;gap:10px;
  padding:4px 14px;border-bottom:1px solid var(--row-border);align-items:baseline}
.reqlog-row:last-child{border:none}
.reqlog-row .t{color:var(--dim)}
.reqlog-row .m{color:var(--accent);font-weight:600}
.reqlog-row .u{color:var(--text);word-break:break-all}
.reqlog-row .s{text-align:right;font-weight:600}
.reqlog-row .s.ok{color:var(--green)}
.reqlog-row .s.bad{color:var(--red)}
.reqlog-row .s.pend{color:var(--dim)}
.reqlog-empty{padding:16px;text-align:center;color:var(--dim);font-size:.78rem}

/* ── Stats bar ── */
.stats{padding:8px 20px;border-top:1px solid var(--border);
  display:flex;gap:20px;flex-wrap:wrap}
.stat{font-size:.72rem;color:var(--dim)}
.stat span{color:var(--text);font-weight:600}

footer{text-align:center;padding:20px;color:var(--dim);font-size:.72rem}
[hidden]{display:none!important}

/* LLM chat panel */
.llm{background:var(--surface);border:1px solid var(--border);border-radius:10px;
  margin:14px 0;overflow:hidden}
.llm-head{display:flex;align-items:center;gap:8px;padding:10px 14px;
  border-bottom:1px solid var(--border);font-weight:600;font-size:.85rem}
.llm-convo{max-height:340px;overflow-y:auto;padding:12px 14px;
  display:flex;flex-direction:column;gap:8px}
.llm-hint{color:var(--dim);font-size:.8rem;text-align:center;padding:16px 0}
.llm-msg{padding:8px 11px;border-radius:8px;font-size:.82rem;line-height:1.45;
  white-space:pre-wrap;word-break:break-word;max-width:85%}
.llm-user{align-self:flex-end;background:var(--accent);color:#06121f}
.llm-bot{align-self:flex-start;background:var(--surface-2);color:var(--text);
  border:1px solid var(--border)}
.llm-input{display:flex;gap:8px;padding:10px 14px;border-top:1px solid var(--border)}
.llm-input input{flex:1;background:var(--bg);border:1px solid var(--border);
  color:var(--text);border-radius:7px;padding:8px 10px;font-size:.82rem}
.llm-input button{background:var(--accent);color:#06121f;border:0;border-radius:7px;
  padding:8px 16px;font-weight:600;cursor:pointer;font-size:.82rem}
.llm-input button:disabled{opacity:.5;cursor:default}

/* ── Geiger card extensions (from Geiger Dashboard Card.html) ── */
.card.geiger{grid-column:1 / -1;max-width:560px}
.card.geiger.alarming{border-color:var(--red);box-shadow:0 0 0 1px var(--red),0 0 22px -6px var(--red)}
@media (prefers-reduced-motion: no-preference){
  .card.geiger.alarming{animation:geigerPulse 1.15s ease-in-out infinite}
}
@keyframes geigerPulse{50%{box-shadow:0 0 0 1px var(--red),0 0 30px -2px var(--red)}}
.alarm-tag{font-size:.6rem;color:#fff;background:var(--red);padding:1px 6px;border-radius:3px;font-weight:700;letter-spacing:.04em;display:none}
.card.geiger.alarming .alarm-tag{display:inline-block}
.g-hero{display:flex;align-items:flex-end;gap:12px;padding:4px 0 12px}
.g-big{font-size:2.9rem;line-height:.92;font-weight:700;font-variant-numeric:tabular-nums;letter-spacing:-.01em}
.g-unit{font-size:.9rem;color:var(--dim);padding-bottom:7px}
.g-sec{margin-left:auto;text-align:right;padding-bottom:6px}
.g-sec .v{font-size:1.05rem;font-weight:600;font-variant-numeric:tabular-nums}
.g-sec .u{font-size:.68rem;color:var(--dim);display:block;letter-spacing:.04em}
.g-statuswrap{display:flex;align-items:center;gap:10px;margin:2px 0 4px}
.g-status{font-size:.72rem;font-weight:700;letter-spacing:.09em}
.g-settling{font-size:.6rem;color:var(--dim);border:1px solid var(--border);border-radius:20px;padding:1px 7px}
.g-bar{position:relative;height:8px;border-radius:6px;margin:8px 0 2px;
  background:linear-gradient(90deg,var(--green) 0 33%,var(--orange) 33% 66%,var(--red) 66% 100%);opacity:.85}
.g-mark{position:absolute;top:-3px;width:3px;height:14px;border-radius:2px;background:var(--text);
  box-shadow:0 0 0 2px var(--bg);transition:left .4s ease;transform:translateX(-50%)}
.g-scale{display:flex;justify-content:space-between;font-size:.6rem;color:var(--dim);margin-top:3px}
.g-spark-wrap{margin:12px 0 4px}
.g-spark{width:100%;height:60px;display:block}
.g-spark-cap{font-size:.62rem;color:var(--dim);margin-top:3px;display:flex;justify-content:space-between}
.g-detail{margin-top:8px;padding-top:6px;border-top:1px solid var(--row-border)}
</style>
</head>
<body>

<div class="topbar">
  <div class="logo">&#128204;</div>
  <div>
    <h1 id="board-title">M5Stack  I2C Framework</h1>
    <p id="ip">connecting...</p>
  </div>
  <div class="right">
    <span class="pill" id="wifi-pill">WiFi</span>
    <span class="pill" id="uptime-pill">--:--:--</span>
  </div>
</div>

<div class="toolbar">
  <button onclick="refresh()">&#8635; Refresh</button>
  <button class="ghost" onclick="openJson('all')">&#128196; JSON</button>
  <button class="ghost" onclick="window.open('/api/scan')">&#128270; Scan</button>
  <button class="ghost" onclick="window.open('/api/config')">&#9881; Config</button>
  <button class="ghost" onclick="window.open('/api/mqtt')">&#128231; MQTT</button>
  <button class="ghost" onclick="window.open('/api/sdcard')">&#128190; SD</button>
  <button class="ghost" onclick="location.href='/settings'">&#128273; Settings</button>
  <button class="ghost" onclick="if(confirm('Re-run boot scan?'))doRescan()">&#8635; Rescan</button>
  <span id="ts"></span>
</div>

<div class="ep" id="ep-list">
  <span class="label">Endpoints:</span>
  <span class="chip">loading…</span>
</div>

<div class="llm" id="llm-panel" hidden>
  <div class="llm-head"><span class="dot" id="llm-dot"></span>LLM Chat</div>
  <div class="llm-convo" id="llm-convo">
    <div class="llm-hint">Ask the Module LLM something&hellip;</div>
  </div>
  <div class="llm-input">
    <input type="text" id="llm-prompt" autocomplete="off"
           placeholder="Type a prompt and press Enter">
    <button id="llm-send">Send</button>
  </div>
</div>

<div class="llm" id="lora-panel" hidden>
  <div class="llm-head"><span class="dot" id="lora-dot"></span>LoRa P2P Chat
    <span id="lora-meta" style="opacity:.65;font-weight:400;font-size:.85em"></span></div>
  <div class="llm-convo" id="lora-convo">
    <div class="llm-hint">Messages to/from the peer node appear here&hellip;</div>
  </div>
  <div class="llm-input">
    <input type="text" id="lora-prompt" autocomplete="off"
           placeholder="Type a message and press Enter">
    <button id="lora-send">Send</button>
    <button id="lora-clear" class="ghost">Clear</button>
  </div>
</div>

<div class="llm" id="alarms-panel" hidden>
  <div class="llm-head"><span class="dot" id="alarms-dot"></span>&#9888; Alarms
    <span id="alarms-meta" style="opacity:.65;font-weight:400;font-size:.85em"></span>
    <button id="rule-toggle" class="ghost" style="margin-left:auto">&#9881; Rules</button>
    <button id="alarms-ack" class="ghost">Ack all</button></div>
  <div class="llm-convo" id="alarms-convo">
    <div class="llm-hint">No alerts yet &mdash; rules evaluate every poll.</div>
  </div>
  <div id="rules-box" hidden style="border-top:1px solid var(--border);padding:10px 14px">
    <div style="display:flex;align-items:center;gap:8px;margin-bottom:6px">
      <b style="font-size:.78rem">Rules</b>
      <button id="rule-add" class="ghost" style="margin-left:auto">+ Add</button>
      <button id="rule-reset" class="ghost">Reset</button>
    </div>
    <div id="rule-list"></div>
    <form id="rule-form" hidden style="margin-top:8px;display:grid;grid-template-columns:auto 1fr;gap:5px 8px;font-size:.74rem;align-items:center">
      <input type="hidden" name="id">
      <label>Slug</label><input name="slug" list="rule-slugs" placeholder="geiger" autocomplete="off">
      <label>Key</label><input name="key" list="rule-keys" placeholder="usv_per_h" autocomplete="off">
      <label>Kind</label><select name="kind"><option value="0">threshold</option><option value="1">event</option></select>
      <label>Op</label><select name="op"><option value="0">&ge;</option><option value="1">&le;</option><option value="2">&gt;</option><option value="3">&lt;</option></select>
      <label>Threshold</label><input name="thr" type="number" step="any">
      <label>Gate key</label><input name="gk" list="rule-keys" placeholder="(optional) distance_km" autocomplete="off">
      <label>Gate op</label><select name="gop"><option value="0">&ge;</option><option value="1">&le;</option><option value="2">&gt;</option><option value="3">&lt;</option></select>
      <label>Gate val</label><input name="gv" type="number" step="any">
      <label>Severity</label><select name="sev"><option value="0">info</option><option value="1">warn</option><option value="2">critical</option></select>
      <label>Channels</label><span id="rule-ch"></span>
      <label>Latch</label><input name="latch" type="checkbox">
      <label>Debounce</label><input name="deb" type="number" min="1">
      <label>Hysteresis %</label><input name="hyst" type="number" step="any">
      <label>Cooldown s</label><input name="cd" type="number" min="0">
      <span></span><span><button type="submit">Save</button> <button type="button" id="rule-cancel" class="ghost">Cancel</button></span>
    </form>
    <datalist id="rule-slugs"><option value="geiger"><option value="lightning"><option value="pm25"><option value="env3"><option value="env4"><option value="co2"><option value="light"><option value="tvoc"><option value="multigas"><option value="modem"><option value="lora"></datalist>
    <datalist id="rule-keys"><option value="usv_per_h"><option value="cpm"><option value="strikes"><option value="distance_km"><option value="energy"><option value="pm2_5"><option value="pm10"><option value="pm1_0"><option value="temp"><option value="humidity"><option value="pressure"><option value="co2"><option value="lux"><option value="tvoc"><option value="rssi_dbm"><option value="signal_pct"></datalist>
  </div>
</div>

<div class="section-head" id="ctrl-head" hidden>
  <h2>Controllable Devices</h2>
  <span class="count" id="ctrl-count">0</span>
  <span class="hint">Widgets POST /api/&lt;slug&gt;/set</span>
</div>
<div class="grid controls" id="ctrl-grid"></div>

<div class="section-head" id="sensor-head">
  <h2>Read-only Sensors</h2>
  <span class="count" id="sensor-count">0</span>
  <span class="hint">Polled live &middot; click a card header for JSON</span>
</div>
<div class="grid sensors" id="sensor-grid">
  <p class="empty-note">Loading sensors&hellip;</p>
</div>

<div class="reqlog">
  <div class="reqlog-head">
    <span class="dot on"></span>API Request Log
    <span class="count" id="reqlog-count">0 requests</span>
  </div>
  <div class="reqlog-body" id="reqlog-body">
    <div class="reqlog-empty">No control requests yet &mdash; flip a toggle or move a slider.</div>
  </div>
</div>

<div class="stats" id="stats"></div>
<footer id="foot">M5Stack I2C Framework &bull; auto-refresh every 5 s</footer>

<script>
// ── small helpers ───────────────────────────────────────────
function esc(s){return String(s).replace(/[&<>"]/g,function(c){
  return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c];});}
function nowHMS(){var d=new Date();return [d.getHours(),d.getMinutes(),d.getSeconds()]
  .map(function(n){return String(n).padStart(2,'0');}).join(':');}
function fmtUptime(s){
  return String(Math.floor(s/3600)).padStart(2,'0')+':'+
         String(Math.floor((s%3600)/60)).padStart(2,'0')+':'+
         String(s%60).padStart(2,'0');}
function openJson(slug){window.open('/api/'+slug,'_blank','noopener');}
function doRescan(){fetch('/api/rescan').then(function(){refresh();}).catch(function(){});}
function busAbbrev(s){
  if(typeof s.bus==='string'&&s.bus.indexOf('hub')===0)
    return 'HUB'+(s.channel!=null?s.channel:'');
  if(s.bus==='internal')return 'INT';
  if(s.bus==='external')return 'EXT';
  if(s.bus==='shared')  return 'SHR';
  return String(s.bus||'?').toUpperCase();
}

// ── API request log (client-side only) ──────────────────────
//  Records every /api/<slug>/set call a widget issues, with its
//  HTTP status, so it's easy to see exactly what each widget sends.
var logRows=[],logSeq=0;
function logReq(m,u){
  var row={id:++logSeq,t:nowHMS(),m:m,u:u,s:null};
  logRows.unshift(row);
  while(logRows.length>60)logRows.pop();
  renderLog();
  return row.id;
}
function logDone(id,status){
  for(var i=0;i<logRows.length;i++)
    if(logRows[i].id===id){logRows[i].s=status;break;}
  renderLog();
}
function renderLog(){
  document.getElementById('reqlog-count').textContent=
    logRows.length+' request'+(logRows.length===1?'':'s');
  var b=document.getElementById('reqlog-body');
  if(!logRows.length){
    b.innerHTML='<div class="reqlog-empty">No control requests yet &mdash; '+
                'flip a toggle or move a slider.</div>';
    return;
  }
  b.innerHTML=logRows.map(function(r){
    var cls=r.s==null?'pend':(r.s>0&&r.s<400?'ok':'bad');
    var st =r.s==null?'···':r.s;
    return '<div class="reqlog-row"><span class="t">'+r.t+'</span>'+
           '<span class="m">'+r.m+'</span><span class="u">'+esc(r.u)+'</span>'+
           '<span class="s '+cls+'">'+st+'</span></div>';
  }).join('');
}

// ── issue one control command ───────────────────────────────
//  POST the param(s) in the body (a /set call changes physical state,
//  so it shouldn't ride a cacheable/prefetchable GET).  `query` is
//  already a url-encoded "k=v[&k=v]" string — send it verbatim as the
//  body.  The device also still accepts the old GET ?query form.
function sendCmd(slug,query){
  var url='/api/'+slug+'/set';
  var id=logReq('POST',url+'  '+query);
  fetch(url,{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded',
             'X-Requested-With':'M5Dashboard'},
    body:query})
    .then(function(r){logDone(id,r.status);})
    .catch(function(){logDone(id,0);});
}

// ── reading rows ────────────────────────────────────────────
//  `skip` is a Set of keys to omit (used on control cards to hide
//  readings that a widget already represents).
function readingRows(readings,skip){
  if(!readings)return '';
  // Friendly labels for the Claude/Router conversation fields so the
  // control card reads as a chat status, not raw toJson keys.
  var CONVO={turns:'Turns',stop_reason:'Stop reason',awaiting:'Awaiting reply'};
  var html='';
  Object.keys(readings).filter(function(k){return !/_unit$/.test(k);})
    .forEach(function(k){
      if(skip&&skip.has(k))return;
      var v=readings[k];
      var u=readings[k+'_unit']||'';
      var label=CONVO[k]||k,fv;
      if(k==='awaiting'){
        fv=(v===1||v===true)?'yes — reply to continue':'no';
      }else if(k==='stop_reason'){
        fv=v?(String(v)==='max_tokens'?'max_tokens (truncated)':String(v)):'—';
      }else{
        fv=(typeof v==='number')?(Number.isInteger(v)?v:v.toFixed(3)):v;
      }
      html+='<div class="row"><span class="rk">'+esc(label)+'</span>'+
            '<span><span class="rv">'+esc(fv)+'</span>'+
            (u?'<span class="ru">'+esc(u)+'</span>':'')+'</span></div>';
    });
  return html;
}

// ── card header (shared by both card types) ─────────────────
function cardHead(s){
  var ctrl=s.controllable?'<span class="ctrl-tag">CTRL</span>':'';
  var addr='0x'+Number(s.addr||0).toString(16).toUpperCase().padStart(2,'0');
  return '<div class="card-head" data-slug="'+esc(s.slug)+'" '+
         'title="Open /api/'+esc(s.slug)+'">'+
         '<div class="dot on"></div><h2>'+esc(s.name)+'</h2>'+ctrl+
         '<span class="mount-tag">'+esc(String(s.mount||'pluggable').toUpperCase())+'</span>'+
         '<span class="bus-tag">'+esc(busAbbrev(s))+'</span>'+
         '<span class="addr">'+addr+'</span></div>';
}

// ── one control widget, rendered from a schema entry ────────
function widgetHTML(slug,c){
  var id=c.id||'',lbl=esc(c.label||id),sl=esc(slug),si=esc(id);
  if(c.type==='toggle'){
    var on=(c.value===1||c.value===true)?' checked':'';
    return '<div class="ctrl-row"><span class="ctrl-label">'+lbl+'</span>'+
      '<input type="checkbox" class="toggle" data-type="toggle" '+
      'data-slug="'+sl+'" data-id="'+si+'"'+on+'></div>';
  }
  if(c.type==='slider'){
    var mn=(c.min!=null)?c.min:0,mx=(c.max!=null)?c.max:100,
        st=(c.step!=null)?c.step:1,val=(c.value!=null)?c.value:mn;
    var u=c.unit?'<span class="u">'+esc(c.unit)+'</span>':'';
    return '<div class="ctrl-row slider-row"><span class="ctrl-label">'+lbl+'</span>'+
      '<input type="range" class="slider" min="'+mn+'" max="'+mx+'" step="'+st+
      '" value="'+val+'" data-type="slider" data-slug="'+sl+'" data-id="'+si+
      '" data-unit="'+esc(c.unit||'')+'">'+
      '<span class="readout">'+val+u+'</span></div>';
  }
  if(c.type==='color'){
    var hex='#'+String(c.value||'000000').replace(/^#/,'');
    if(!/^#[0-9a-fA-F]{6}$/.test(hex))hex='#000000';
    return '<div class="ctrl-row"><span class="ctrl-label">'+lbl+'</span>'+
      '<input type="color" class="cpick" value="'+hex+'" data-type="color" '+
      'data-slug="'+sl+'" data-id="'+si+'">'+
      '<span class="hex">'+hex.toUpperCase()+'</span></div>';
  }
  if(c.type==='text'){
    return '<div class="ctrl-row"><span class="ctrl-label">'+lbl+'</span>'+
      '<input type="text" class="tinput" placeholder="'+esc(c.placeholder||'')+
      '" data-id="'+si+'">'+
      '<button class="ctrl-send" data-act="send-text" data-slug="'+sl+
      '" data-id="'+si+'">Send</button></div>';
  }
  return '';   // "button" is collected into the .qab strip instead
}

// ── build a controllable-device card ────────────────────────
function buildControlCard(s){
  var card=document.createElement('div');
  card.className='card controllable';
  var controls=s.controls||[];
  var ctrlIds=new Set();
  controls.forEach(function(c){if(c.id)ctrlIds.add(c.id);});
  var inner='',curGroup=null,buttons='';
  controls.forEach(function(c){
    if(c.type==='button'){
      var q=c.query||((c.id||'')+'='+(c.send!=null?c.send:''));
      buttons+='<button class="qab-btn" data-slug="'+esc(s.slug)+
               '" data-query="'+esc(q)+'">'+esc(c.label||'action')+'</button>';
      return;
    }
    var g=c.group||'';
    if(g!==curGroup){
      curGroup=g;
      if(g)inner+='<div class="ctrl-group-label">'+esc(g)+'</div>';
    }
    inner+=widgetHTML(s.slug,c);
  });
  var body='<div class="readings" data-readings="'+esc(s.slug)+'">'+
           readingRows(s.readings,ctrlIds)+'</div>'+
           '<div class="controls">'+inner+
           (buttons?'<div class="qab">'+buttons+'</div>':'')+'</div>';
  card.innerHTML=cardHead(s)+'<div class="card-body">'+body+'</div>';
  return card;
}

// ── build a read-only sensor card ───────────────────────────
function buildSensorCard(s){
  var card=document.createElement('div');
  card.className='card';
  card.innerHTML=cardHead(s)+'<div class="card-body">'+
                 readingRows(s.readings,null)+'</div>';
  return card;
}

// ── Geiger dashboard card (from "Geiger Dashboard Card.html") ──
//  Special-cased renderer for slug "geiger": big µSv/h hero, CPM, a
//  zone bar, an optional counts/sec sparkline (only when toJson emits
//  trace[]), and a detail block.  `s` is the merged toJson object
//  (readings + name/slug).  drawSparks() paints the canvases after the
//  cards are in the DOM.  Reuses the page's esc() and the shared card
//  CSS vars; helpers are g-prefixed to avoid clashing with the grid.
var G_SCALE_MAX=6.0;  // µSv/h full-scale on the zone bar
function gStatusColor(st){return st==='DANGER'?'var(--red)':st==='ELEVATED'?'var(--orange)':'var(--green)';}
function gRow(k,v,u){return '<div class="row"><span class="rk">'+esc(k)+'</span>'+
  '<span><span class="rv">'+esc(v)+'</span>'+(u?'<span class="ru">'+esc(u)+'</span>':'')+'</span></div>';}
function gRowStatus(k,v,c){return '<div class="row"><span class="rk">'+esc(k)+'</span>'+
  '<span class="rv" style="color:'+c+'">'+esc(v)+'</span></div>';}
function geigerCard(s){
  var usv=+s.usv_per_h,cpm=+s.cpm;
  var frac=Math.max(Math.min(usv/G_SCALE_MAX,1),0);
  var danger=s.status==='DANGER'&&(s.alarm===1||s.alarm===true);
  var col=gStatusColor(s.status);
  var hasTrace=Array.isArray(s.trace)&&s.trace.length>0;
  var detail=
    gRow('Total counts',(+s.total_counts).toLocaleString('en-US'))+
    gRow('Cumulative dose',(+s.dose_uSv).toFixed(2),'µSv')+
    gRow('Tube factor',Math.round(+s.tube_factor),'CPM/µSv·h')+
    gRow('Signal pin','GPIO'+s.pin)+
    gRowStatus('Status',s.status,col);
  var spark=hasTrace
    ? '<div class="g-spark-wrap"><canvas class="g-spark" data-trace=\''+
        esc(JSON.stringify(s.trace))+'\' data-factor="'+(+s.tube_factor)+'"></canvas>'+
        '<div class="g-spark-cap"><span>counts/sec · last '+(s.trace_secs||s.trace.length)+' s</span>'+
        '<span>– – alarm</span></div></div>'
    : '';
  return '<div class="card geiger'+(danger?' alarming':'')+'" data-slug="geiger">'+
    '<div class="card-head" title="Open /api/geiger">'+
      '<div class="dot on"></div><h2>'+esc(s.name||'Geiger Counter')+'</h2>'+
      '<span class="alarm-tag">⚠ ALARM</span>'+
      '<span class="mount-tag">PIN</span>'+
      '<span class="bus-tag">GPIO</span>'+
    '</div>'+
    '<div class="card-body">'+
      '<div class="g-hero">'+
        '<span class="g-big" style="color:'+col+'">'+usv.toFixed(2)+'</span>'+
        '<span class="g-unit">µSv/h</span>'+
        '<span class="g-sec"><span class="v">'+cpm.toLocaleString('en-US')+'</span>'+
          '<span class="u">CPM</span></span>'+
      '</div>'+
      '<div class="g-statuswrap">'+
        '<span class="g-status" style="color:'+col+'">'+esc(s.status)+'</span>'+
        (s.settling?'<span class="g-settling">settling… 60 s</span>':'')+
      '</div>'+
      '<div class="g-bar"><div class="g-mark" style="left:'+(frac*100).toFixed(1)+'%"></div></div>'+
      '<div class="g-scale"><span>0</span><span>1</span><span>5</span><span>6+ µSv/h</span></div>'+
      spark+
      '<div class="g-detail">'+detail+'</div>'+
    '</div></div>';
}
function drawSparks(){
  document.querySelectorAll('.g-spark').forEach(function(cv){
    var trace;try{trace=JSON.parse(cv.dataset.trace);}catch(e){return;}
    var factor=+cv.dataset.factor||154;
    var dpr=window.devicePixelRatio||1;
    cv.width=cv.clientWidth*dpr;cv.height=cv.clientHeight*dpr;
    var ctx=cv.getContext('2d'),w=cv.width,h=cv.height;
    ctx.clearRect(0,0,w,h);
    var peak=Math.max.apply(null,trace);
    var thrCps=5.0*factor/60;                 // alarm 5 µSv/h → counts/sec
    var scaleMax=Math.max(peak,thrCps)*1.15||1;
    var pad=4*dpr;
    function Y(v){return h-pad-(v/scaleMax)*(h-2*pad);}
    function X(i){return (i/(trace.length-1))*w;}
    var accent=getComputedStyle(document.body).getPropertyValue('--accent').trim();
    var red=getComputedStyle(document.body).getPropertyValue('--red').trim();
    ctx.strokeStyle=red;ctx.globalAlpha=.6;ctx.lineWidth=1*dpr;
    ctx.setLineDash([4*dpr,4*dpr]);ctx.beginPath();
    ctx.moveTo(0,Y(thrCps));ctx.lineTo(w,Y(thrCps));ctx.stroke();
    ctx.setLineDash([]);ctx.globalAlpha=1;
    ctx.beginPath();ctx.moveTo(0,h);
    trace.forEach(function(v,i){ctx.lineTo(X(i),Y(v));});
    ctx.lineTo(w,h);ctx.closePath();
    ctx.fillStyle=accent;ctx.globalAlpha=.13;ctx.fill();ctx.globalAlpha=1;
    ctx.beginPath();
    trace.forEach(function(v,i){i?ctx.lineTo(X(i),Y(v)):ctx.moveTo(X(i),Y(v));});
    ctx.strokeStyle=accent;ctx.lineWidth=1.8*dpr;ctx.stroke();
  });
}

// ── main poll ───────────────────────────────────────────────
var ctrlSig=null;
async function refresh(){
  document.getElementById('ts').textContent='updating…';
  try{
    var r=await fetch('/api/all');
    if(!r.ok)throw new Error(r.status);
    var d=await r.json();

    if(d.board&&d.board.long_name){
      var title=d.board.long_name+'  I2C Framework';
      document.title=title;
      document.getElementById('board-title').textContent=title;
      document.getElementById('foot').innerHTML=
        esc(d.board.long_name)+' I2C Framework &bull; auto-refresh every 5 s';
    }
    document.getElementById('ip').textContent=
      window.location.protocol+'//'+(d.ip||'?');
    document.getElementById('uptime-pill').textContent=fmtUptime(d.uptime_s||0);
    document.getElementById('wifi-pill').className='pill ok';
    document.getElementById('wifi-pill').textContent='WiFi OK';

    var all=(d.sensors||[]).filter(function(x){return x.active;});
    // The Module LLM is controllable but has its own chat panel, so
    // it is kept out of the generic control grid.
    var ctrl=all.filter(function(x){return x.controllable&&x.slug!=='llm'&&x.slug!=='lora';});
    var sensors=all.filter(function(x){return !x.controllable;});
    llmRefresh(all.filter(function(x){return x.slug==='llm';})[0]);
    loraRefresh(all.filter(function(x){return x.slug==='lora';})[0]);

    // Controllable devices — rebuild the grid only when the SET of
    // devices changes, so live widget state isn't clobbered on
    // every 5 s poll.  Otherwise just refresh the reading rows.
    var sig=JSON.stringify(ctrl.map(function(x){return x.slug;}));
    var cg=document.getElementById('ctrl-grid');
    if(sig!==ctrlSig){
      ctrlSig=sig;
      cg.innerHTML='';
      ctrl.forEach(function(s){cg.appendChild(buildControlCard(s));});
    }else{
      ctrl.forEach(function(s){
        var rc=cg.querySelector('[data-readings="'+s.slug+'"]');
        if(rc){
          var ids=new Set();
          (s.controls||[]).forEach(function(c){if(c.id)ids.add(c.id);});
          rc.innerHTML=readingRows(s.readings,ids);
        }
      });
    }
    document.getElementById('ctrl-head').hidden=(ctrl.length===0);
    document.getElementById('ctrl-count').textContent=ctrl.length;

    // Read-only sensors — rebuilt every poll (no widget state to keep).
    var sg=document.getElementById('sensor-grid');
    if(!sensors.length){
      sg.innerHTML='<p class="empty-note">No read-only sensors bound.</p>';
    }else{
      sg.innerHTML='';
      sensors.forEach(function(s){
        if(s.slug==='geiger'){
          // Special-cased look; data still comes straight from toJson.
          var t=document.createElement('div');
          t.innerHTML=geigerCard(Object.assign({name:s.name,slug:s.slug},s.readings||{}));
          if(t.firstElementChild)sg.appendChild(t.firstElementChild);
        }else{
          sg.appendChild(buildSensorCard(s));
        }
      });
      drawSparks();
    }
    document.getElementById('sensor-count').textContent=sensors.length;

    var boardLbl=(d.board&&d.board.short_name)||'?';
    document.getElementById('stats').innerHTML=
      '<div class="stat">Board: <span>'+esc(boardLbl)+'</span></div>'+
      '<div class="stat">Devices online: <span>'+all.length+'/'+
        (d.sensors||[]).length+'</span></div>'+
      '<div class="stat">Controllable: <span>'+ctrl.length+'</span></div>'+
      '<div class="stat">Free heap: <span>'+
        Number(d.free_heap||0).toLocaleString()+' B</span></div>'+
      '<div class="stat">CPU: <span>'+(d.cpu_mhz||'?')+' MHz</span></div>';

    document.getElementById('ts').textContent=
      'Updated '+new Date().toLocaleTimeString();
  }catch(e){
    document.getElementById('ts').textContent='Error: '+e.message;
    document.getElementById('wifi-pill').className='pill err';
    document.getElementById('wifi-pill').textContent='Offline';
  }
}

// ── widget event wiring (delegated on the control grid) ─────
(function(){
  var cg=document.getElementById('ctrl-grid');
  cg.addEventListener('click',function(e){
    var h=e.target.closest('.card-head');
    if(h&&h.dataset.slug){openJson(h.dataset.slug);return;}
    var t=e.target;
    if(t.dataset&&t.dataset.act==='send-text'){
      var inp=t.parentElement.querySelector('input.tinput');
      var val=inp?inp.value.trim():'';
      if(val)sendCmd(t.dataset.slug,t.dataset.id+'='+encodeURIComponent(val));
      return;
    }
    if(t.classList&&t.classList.contains('qab-btn'))
      sendCmd(t.dataset.slug,t.dataset.query);
  });
  cg.addEventListener('input',function(e){
    var t=e.target,ty=t.dataset&&t.dataset.type;
    if(ty==='slider'){
      var ro=t.parentElement.querySelector('.readout');
      if(ro)ro.innerHTML=t.value+(t.dataset.unit?
        '<span class="u">'+esc(t.dataset.unit)+'</span>':'');
    }else if(ty==='color'){
      var hx=t.parentElement.querySelector('.hex');
      if(hx)hx.textContent=t.value.toUpperCase();
    }
  });
  cg.addEventListener('change',function(e){
    var t=e.target,ty=t.dataset&&t.dataset.type;
    if(!ty)return;
    var slug=t.dataset.slug,id=t.dataset.id;
    if(ty==='toggle')      sendCmd(slug,id+'='+(t.checked?1:0));
    else if(ty==='slider') sendCmd(slug,id+'='+t.value);
    else if(ty==='color')  sendCmd(slug,id+'='+t.value.slice(1).toUpperCase());
  });
  document.getElementById('sensor-grid').addEventListener('click',function(e){
    var h=e.target.closest('.card-head');
    if(h&&h.dataset.slug)openJson(h.dataset.slug);
  });
})();

// Populate the endpoint chip strip from /api/endpoints.  Fetched
// once at page load — the list doesn't change without a reflash.
// Slugs with a placeholder (e.g. /api/{slug}) can't be opened, so
// they render as dimmed chips; everything else is a clickable link.
async function loadEndpoints(){
  var host=document.getElementById('ep-list');
  try{
    var r=await fetch('/api/endpoints');
    if(!r.ok)throw new Error(r.status);
    var d=await r.json();
    host.innerHTML='<span class="label">Endpoints:</span>';
    (d.endpoints||[]).forEach(function(e){
      var ph=(e.url||'').indexOf('{')>=0;
      var el=document.createElement(ph?'span':'a');
      if(ph){el.className='chip';}
      else{el.href=e.url;el.target='_blank';el.rel='noopener';}
      el.title=e.description||'';
      el.innerHTML='<span class="method">'+esc(e.method)+'</span>'+esc(e.url);
      host.appendChild(el);
    });
  }catch(err){
    host.innerHTML='<span class="label">Endpoints:</span>'+
      '<span class="chip">unavailable</span>';
  }
}

// ── LLM chat panel ──────────────────────────────────────────
//  Shown only when an "llm" device is bound.  Submits a prompt via
//  /api/llm/set?ask=, then polls /api/llm until the streamed reply
//  finishes.  Inference runs asynchronously on the module, so the
//  rest of the dashboard keeps refreshing while a reply streams in.
var llmBusy=false;
function llmRefresh(dev){
  var p=document.getElementById('llm-panel');
  if(!dev){p.hidden=true;return;}
  p.hidden=false;
  document.getElementById('llm-dot').className=
    'dot'+((dev.readings&&dev.readings.connected)?' on':'');
}
function llmAdd(role,text){
  var c=document.getElementById('llm-convo');
  var h=c.querySelector('.llm-hint');if(h)h.remove();
  var d=document.createElement('div');
  d.className='llm-msg llm-'+role;
  d.textContent=text;
  c.appendChild(d);c.scrollTop=c.scrollHeight;
  return d;
}
function llmEnd(){
  llmBusy=false;
  document.getElementById('llm-send').disabled=false;
}
function llmPoll(bot){
  fetch('/api/llm').then(function(r){return r.json();}).then(function(d){
    var rd=(d&&d.readings)||{};
    if(rd.answer)bot.textContent=rd.answer;
    document.getElementById('llm-convo').scrollTop=9e9;
    if(rd.busy){setTimeout(function(){llmPoll(bot);},700);}
    else{
      if(rd.timed_out)bot.textContent=(rd.answer||'')+'  [timed out]';
      else if(!rd.answer)bot.textContent='[no reply]';
      llmEnd();
    }
  }).catch(function(){setTimeout(function(){llmPoll(bot);},1200);});
}
function llmSend(){
  if(llmBusy)return;
  var inp=document.getElementById('llm-prompt');
  var q=inp.value.trim();if(!q)return;
  inp.value='';
  llmAdd('user',q);
  var bot=llmAdd('bot','…');
  llmBusy=true;
  document.getElementById('llm-send').disabled=true;
  var u='/api/llm/set';
  var body='ask='+encodeURIComponent(q);
  var id=logReq('POST',u);
  fetch(u,{method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded',
             'X-Requested-With':'M5Dashboard'},
    body:body}).then(function(r){
    logDone(id,r.status);
    if(!r.ok){bot.textContent='[error '+r.status+']';llmEnd();return;}
    llmPoll(bot);
  }).catch(function(){bot.textContent='[network error]';llmEnd();});
}
(function(){
  var b=document.getElementById('llm-send');
  if(b)b.addEventListener('click',llmSend);
  var i=document.getElementById('llm-prompt');
  if(i)i.addEventListener('keydown',function(e){
    if(e.key==='Enter'){e.preventDefault();llmSend();}
  });
})();

// ── LoRa P2P chat panel ─────────────────────────────────────
//  Shown only when a "lora" device is bound.  Polls /api/lora on its
//  own ~1.5s cadence (independent of the 5s dashboard refresh) so
//  incoming peer messages appear promptly; appends only log entries
//  newer than the highest id already shown.
var loraSeen=0,loraPolling=false;
function loraRefresh(dev){
  var p=document.getElementById('lora-panel');
  if(!dev){p.hidden=true;loraPolling=false;return;}
  p.hidden=false;
  if(!loraPolling){loraPolling=true;loraPoll();}
}
function loraAppend(m){
  var c=document.getElementById('lora-convo');
  var h=c.querySelector('.llm-hint');if(h)h.remove();
  var d=document.createElement('div');
  d.className='llm-msg '+(m.dir==='tx'?'llm-user':'llm-bot');
  var who=m.from?(esc(m.from)+': '):'';
  var meta=(m.dir==='rx'&&m.rssi!=null)?('  ('+m.rssi+' dBm)'):'';
  d.textContent=who+m.text+meta;
  c.appendChild(d);c.scrollTop=c.scrollHeight;
}
function loraPoll(){
  if(document.getElementById('lora-panel').hidden){loraPolling=false;return;}
  fetch('/api/lora').then(function(r){return r.json();}).then(function(d){
    var rd=(d&&d.readings)||{};
    document.getElementById('lora-dot').className='dot'+(rd.connected?' on':'');
    document.getElementById('lora-meta').textContent=
      (rd.node?('['+rd.node+'] '):'')+'rx '+(rd.rx_count||0)+' · tx '+(rd.tx_count||0);
    (rd.log||[]).forEach(function(m){if(m.id>loraSeen){loraAppend(m);loraSeen=m.id;}});
  }).catch(function(){});
  setTimeout(loraPoll,1500);
}
function loraSend(){
  var inp=document.getElementById('lora-prompt');
  var q=inp.value.trim();if(!q)return;
  inp.value='';
  sendCmd('lora','send='+encodeURIComponent(q));
}
(function(){
  var b=document.getElementById('lora-send');
  if(b)b.addEventListener('click',loraSend);
  var cl=document.getElementById('lora-clear');
  if(cl)cl.addEventListener('click',function(){
    sendCmd('lora','clear=1');
    document.getElementById('lora-convo').innerHTML='';loraSeen=0;
  });
  var i=document.getElementById('lora-prompt');
  if(i)i.addEventListener('keydown',function(e){
    if(e.key==='Enter'){e.preventDefault();loraSend();}
  });
})();

// ── Alarms panel ───────────────────────────────────────────
//  Polls /api/alerts on its own cadence and shows itself only when
//  the alarm engine is enabled.  Re-renders the event ring each poll
//  (small list), newest first, severity-coloured.  "Ack all" releases
//  every latched rule.
function alarmsPoll(){
  var next=3000;
  fetch('/api/alerts').then(function(r){return r.json();}).then(function(d){
    var p=document.getElementById('alarms-panel');
    if(!d||!d.enabled){p.hidden=true;next=5000;return;}
    p.hidden=false;
    document.getElementById('alarms-dot').className='dot'+((d.active|0)>0?' on':'');
    document.getElementById('alarms-meta').textContent=
      (d.active|0)+' active · '+(d.rules|0)+' rules';
    var evs=d.events||[];
    if(evs.length){
      var c=document.getElementById('alarms-convo');
      c.innerHTML='';
      evs.slice().reverse().forEach(function(e){
        var sev=e.sev||'info';
        var col=sev==='critical'?'var(--red)':sev==='warn'?'var(--orange)':'var(--green)';
        var el=document.createElement('div');
        el.className='llm-msg';el.style.borderLeft='3px solid '+col;
        el.textContent='['+sev.toUpperCase()+'] '+esc(e.slug)+'.'+esc(e.key)+' = '+
          (+e.value).toFixed(2)+'  '+esc(e.type)+'  ('+Math.round((e.age_ms||0)/1000)+'s)';
        c.appendChild(el);
      });
    }
  }).catch(function(){}).finally(function(){setTimeout(alarmsPoll,next);});
}
(function(){
  var b=document.getElementById('alarms-ack');
  if(b)b.addEventListener('click',function(){
    fetch('/api/alerts/ack',{method:'POST',headers:{'X-Requested-With':'M5Dashboard'}}).then(function(){alarmsPoll();}).catch(function(){});
  });
})();

// ── Rule editor (milestone 3b) ──────────────────────────────
//  Channel bits MUST match AlertManager::Channel.
var RULE_CH=[['buzzer',1],['lcd',2],['mqtt',4],['sd',8],['dash',16],
             ['lora',32],['email',64],['webhook',128],['sms',256]];
var RULE_OP=['≥','≤','>','<'];
function ruleChBoxes(){
  var host=document.getElementById('rule-ch');if(!host)return;host.innerHTML='';
  RULE_CH.forEach(function(c){
    var l=document.createElement('label');
    l.style.cssText='font-size:.7rem;margin-right:7px;color:var(--text)';
    l.innerHTML='<input type="checkbox" data-bit="'+c[1]+'"> '+c[0];
    host.appendChild(l);
  });
}
function rulesLoad(){
  fetch('/api/alerts/rules').then(function(r){return r.json();}).then(function(d){
    var L=document.getElementById('rule-list');L.innerHTML='';
    (d.rules||[]).forEach(function(r){
      var div=document.createElement('div');
      div.style.cssText='display:flex;gap:6px;align-items:center;padding:3px 0;font-size:.74rem;border-bottom:1px solid var(--row-border)';
      var summ=(r.kind==1?'event':(RULE_OP[r.op]||'?')+' '+r.thr);
      var sev=['info','warn','crit'][r.sev]||'';
      var sp=document.createElement('span');sp.style.flex='1';
      sp.textContent=(r.en?'':'○ ')+r.slug+'.'+r.key+' '+summ+'  ['+sev+']';
      var e=document.createElement('button');e.className='ghost';e.textContent='Edit';
      e.onclick=function(){ruleEdit(r);};
      var x=document.createElement('button');x.className='ghost';x.textContent='Del';
      x.onclick=function(){if(confirm('Delete rule '+r.id+'?'))
        fetch('/api/alerts/rules/delete?id='+r.id,{method:'POST',headers:{'X-Requested-With':'M5Dashboard'}}).then(rulesLoad);};
      div.appendChild(sp);div.appendChild(e);div.appendChild(x);L.appendChild(div);
    });
  }).catch(function(){});
}
function ruleEdit(r){
  r=r||{};var f=document.getElementById('rule-form');f.hidden=false;
  f.id.value=r.id||0;f.slug.value=r.slug||'';f.key.value=r.key||'';
  f.kind.value=r.kind||0;f.op.value=r.op||0;f.thr.value=(r.thr!=null?r.thr:0);
  f.gk.value=r.gk||'';f.gop.value=(r.gop!=null?r.gop:1);f.gv.value=(r.gv!=null?r.gv:0);
  f.sev.value=(r.sev!=null?r.sev:1);f.latch.checked=!!r.latch;
  f.deb.value=(r.deb!=null?r.deb:2);f.hyst.value=(r.hyst!=null?r.hyst:10);f.cd.value=(r.cd!=null?r.cd:60);
  var ch=r.ch||0;
  document.querySelectorAll('#rule-ch input').forEach(function(b){b.checked=(ch&(+b.dataset.bit))!==0;});
}
function ruleSave(ev){
  ev.preventDefault();var f=document.getElementById('rule-form');
  if(!f.slug.value.trim()||!f.key.value.trim()){alert('Slug and key are required.');return;}
  var ch=0;document.querySelectorAll('#rule-ch input').forEach(function(b){if(b.checked)ch|=+b.dataset.bit;});
  var r={id:+f.id.value||0,en:1,slug:f.slug.value.trim(),key:f.key.value.trim(),
    kind:+f.kind.value,op:+f.op.value,thr:+f.thr.value,
    gate:f.gk.value.trim()?1:0,gk:f.gk.value.trim(),gop:+f.gop.value,gv:+f.gv.value,
    sev:+f.sev.value,ch:ch,latch:f.latch.checked?1:0,
    deb:+f.deb.value,hyst:+f.hyst.value,cd:+f.cd.value};
  fetch('/api/alerts/rules/save',{method:'POST',
    headers:{'Content-Type':'application/json','X-Requested-With':'M5Dashboard'},body:JSON.stringify(r)})
    .then(function(res){if(res.ok){f.hidden=true;rulesLoad();}else alert('Save rejected.');})
    .catch(function(){alert('Save failed.');});
}
(function(){
  ruleChBoxes();
  var t=document.getElementById('rule-toggle');
  if(t)t.addEventListener('click',function(){
    var bx=document.getElementById('rules-box');bx.hidden=!bx.hidden;if(!bx.hidden)rulesLoad();});
  var a=document.getElementById('rule-add');if(a)a.addEventListener('click',function(){ruleEdit({});});
  var c=document.getElementById('rule-cancel');if(c)c.addEventListener('click',function(){document.getElementById('rule-form').hidden=true;});
  var rs=document.getElementById('rule-reset');if(rs)rs.addEventListener('click',function(){
    if(confirm('Reset to default (seed) rules?'))fetch('/api/alerts/rules/reset',{method:'POST',headers:{'X-Requested-With':'M5Dashboard'}}).then(rulesLoad);});
  var fm=document.getElementById('rule-form');if(fm)fm.addEventListener('submit',ruleSave);
})();
alarmsPoll();

loadEndpoints();
refresh();
setInterval(refresh,5000);
</script>
</body>
</html>
)==";

// ============================================================
//  First-boot setup portal (approach B).  Served at "/" when the
//  device is unprovisioned (no WiFi in NVS or Secrets.h).  Self-
//  contained; submits the form to /api/setup as a GET (the request
//  travels inside the TLS tunnel, and fetch keeps it out of browser
//  history), which saves the values to NVS and reboots.
// ============================================================
static const char SETUP_HTML[] PROGMEM = R"==(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Device Setup</title>
<style>
:root{color-scheme:dark}
body{margin:0;background:#0d1b2a;color:#e8eef5;font:16px/1.5 system-ui,sans-serif;
 display:flex;min-height:100vh;align-items:center;justify-content:center}
.card{background:#16263a;max-width:420px;width:92%;padding:26px 24px;border-radius:14px;
 box-shadow:0 10px 30px rgba(0,0,0,.4)}
h1{margin:0 0 6px;font-size:20px}
p.sub{margin:0 0 18px;color:#9fb2c8;font-size:14px}
label{display:block;margin:12px 0 4px;font-size:13px;color:#b9c8da}
input{width:100%;box-sizing:border-box;padding:10px 12px;
 border:1px solid #2c4258;border-radius:8px;background:#0f1f31;color:#e8eef5;font-size:15px}
hr{border:none;border-top:1px solid #243a52;margin:20px 0}
button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:8px;
 background:#fd9720;color:#1a1207;font-size:16px;font-weight:600;cursor:pointer}
button:disabled{opacity:.6;cursor:default}
#msg{margin-top:14px;font-size:14px;min-height:20px}
.hint{color:#7f93a8;font-size:12px;margin-top:2px}
</style>
</head>
<body>
<div class="card">
<h1>Device setup</h1>
<p class="sub">This unit isn&#39;t configured yet. Enter your Wi-Fi and a dashboard login. It saves them on the device and reboots to join your network.</p>
<form id="f">
<label style="display:flex;align-items:center;gap:8px;margin-bottom:6px">
<input type="checkbox" name="aponly" value="1" id="ap" style="width:auto;margin:0">
Run as a standalone access point (no Wi-Fi network)</label>
<div class="hint">Check this to serve the dashboard on the device's own Wi-Fi at its AP address (192.168.4.1) without joining a router.</div>
<div id="wifi">
<label>Wi-Fi network (SSID)</label>
<input name="ssid" autocomplete="off" required>
<label>Wi-Fi password</label>
<input name="wpass" type="password" autocomplete="off">
</div>
<hr>
<label>Dashboard username</label>
<input name="user" autocomplete="off" placeholder="blank = keep current">
<div class="hint">Leave the login fields blank to keep the password already set on the device.</div>
<label>Dashboard password</label>
<input name="upass" type="password" autocomplete="off">
<details style="margin-top:14px">
<summary style="cursor:pointer;color:#9fb2c8">Advanced (optional) — MQTT &amp; Claude</summary>
<div class="hint">Leave blank to skip. (TLS on/off is set in Config.h.)</div>
<label>MQTT broker host</label>
<input name="mqtthost" autocomplete="off" placeholder="e.g. 192.168.1.10 — blank = MQTT off">
<label>MQTT port</label>
<input name="mqttport" type="number" min="1" max="65535" placeholder="1883 plain / 8883 TLS">
<label>MQTT username</label>
<input name="mqttuser" autocomplete="off">
<label>MQTT password</label>
<input name="mqttpass" type="password" autocomplete="off">
<label>Claude API key</label>
<input name="claude" type="password" autocomplete="off">
</details>
<button id="b" type="submit">Save &amp; reboot</button>
</form>
<p id="msg"></p>
</div>
<script>
var f=document.getElementById('f'),m=document.getElementById('msg'),b=document.getElementById('b');
var ap=document.getElementById('ap'),wifi=document.getElementById('wifi');
function syncAp(){
 var on=ap.checked;
 wifi.style.display=on?'none':'';
 f.ssid.required=!on;            // don't block submit when AP-only
}
ap.addEventListener('change',syncAp);syncAp();
f.addEventListener('submit',function(e){
 e.preventDefault();
 var body=new URLSearchParams(new FormData(f)).toString();
 b.disabled=true;m.textContent='Saving...';
 // POST (not GET) so the password is never written to the device's
 // request-line log; the body is url-encoded and decoded on-device.
 fetch('/api/setup',{method:'POST',
   headers:{'Content-Type':'application/x-www-form-urlencoded'},
   body:body}).then(function(r){return r.json();}).then(function(d){
  if(d&&d.ok){m.textContent=d.msg||'Saved. Rebooting...';}
  else{b.disabled=false;m.textContent='Error: '+((d&&d.error)||'unknown');}
 }).catch(function(){m.textContent='Saved. The device is rebooting — reconnect to your Wi-Fi.';});
});
</script>
</body>
</html>
)==";

// ============================================================
//  Settings page (approach B, full).  Auth-gated editor served at
//  /settings on a provisioned device, so credentials can be changed
//  over HTTPS without reflashing or factory-reset.  Non-secret values
//  (SSID, usernames) are pre-filled from /api/settings; password and
//  key fields are blank and "blank = keep current".  Secrets are
//  never sent back to the browser — only set/unset badges.
// ============================================================
static const char SETTINGS_HTML[] PROGMEM = R"==(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Settings</title>
<style>
:root{color-scheme:dark}
body{margin:0;background:#0d1b2a;color:#e8eef5;font:16px/1.5 system-ui,sans-serif;
 display:flex;min-height:100vh;align-items:center;justify-content:center}
.card{background:#16263a;max-width:440px;width:92%;padding:26px 24px;border-radius:14px;
 box-shadow:0 10px 30px rgba(0,0,0,.4)}
h1{margin:0 0 6px;font-size:20px}
p.sub{margin:0 0 16px;color:#9fb2c8;font-size:14px}
label{display:block;margin:12px 0 4px;font-size:13px;color:#b9c8da}
input{width:100%;box-sizing:border-box;padding:10px 12px;
 border:1px solid #2c4258;border-radius:8px;background:#0f1f31;color:#e8eef5;font-size:15px}
hr{border:none;border-top:1px solid #243a52;margin:18px 0}
button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:8px;
 background:#fd9720;color:#1a1207;font-size:16px;font-weight:600;cursor:pointer}
button:disabled{opacity:.6;cursor:default}
#msg{margin-top:14px;font-size:14px;min-height:20px}
.hint{color:#7f93a8;font-size:12px;margin-top:2px}
.badge{font-size:11px;padding:2px 7px;border-radius:10px;margin-left:6px}
.set{background:#1f8a5b;color:#fff}.unset{background:#553;color:#fdd}
a.back{color:#9fb2c8;font-size:13px;text-decoration:none}
</style>
</head>
<body>
<div class="card">
<h1>Settings</h1>
<p class="sub">Change stored credentials. Leave a password/key blank to keep the current one. Saving reboots the device.</p>
<form id="f">
<label style="display:flex;align-items:center;gap:8px;margin-bottom:6px">
<input type="checkbox" name="aponly" value="1" id="ap" style="width:auto;margin:0">
Run as a standalone access point (no Wi-Fi network)</label>
<div class="hint">Serves the dashboard on the device's own Wi-Fi at 192.168.4.1, with no router.</div>
<div id="wifi">
<label>Wi-Fi network (SSID)</label>
<input name="ssid" autocomplete="off">
<label>Wi-Fi password <span class="hint">(blank = keep)</span></label>
<input name="wpass" type="password" autocomplete="off">
</div>
<hr>
<label>Dashboard username</label>
<input name="user" autocomplete="off">
<label>Dashboard password <span class="hint">(blank = keep)</span></label>
<input name="upass" type="password" autocomplete="off">
<hr>
<label>MQTT broker host <span class="hint">(blank = keep)</span></label>
<input name="mqtthost" autocomplete="off">
<label>MQTT port</label>
<input name="mqttport" type="number" min="1" max="65535">
<label>MQTT username <span id="mb" class="badge unset">unset</span></label>
<input name="mqttuser" autocomplete="off">
<label>MQTT password <span class="hint">(blank = keep)</span></label>
<input name="mqttpass" type="password" autocomplete="off">
<label>Claude API key <span id="cb" class="badge unset">unset</span> <span class="hint">(blank = keep)</span></label>
<input name="claude" type="password" autocomplete="off">
<button id="b" type="submit">Save &amp; reboot</button>
</form>
<p id="msg"></p>
<p><a class="back" href="/">&larr; back to dashboard</a></p>
</div>
<script>
var f=document.getElementById('f'),m=document.getElementById('msg'),b=document.getElementById('b');
var ap=document.getElementById('ap'),wifi=document.getElementById('wifi');
function syncAp(){wifi.style.display=ap.checked?'none':'';}
ap.addEventListener('change',syncAp);
fetch('/api/settings').then(function(r){return r.json();}).then(function(d){
 ap.checked=!!d.ap_only;syncAp();
 f.ssid.value=d.ap_only?'':(d.wifi_ssid||'');
 f.user.value=d.web_user||'';
 f.mqtthost.value=d.mqtt_host||'';
 if(d.mqtt_port)f.mqttport.value=d.mqtt_port;
 f.mqttuser.value=d.mqtt_user||'';
 if(d.mqtt_set){var e=document.getElementById('mb');e.textContent='set';e.className='badge set';}
 if(d.claude_set){var c=document.getElementById('cb');c.textContent='set';c.className='badge set';}
}).catch(function(){});
f.addEventListener('submit',function(e){
 e.preventDefault();
 var body=new URLSearchParams(new FormData(f)).toString();
 b.disabled=true;m.textContent='Saving...';
 fetch('/api/settings/save',{method:'POST',
   headers:{'Content-Type':'application/x-www-form-urlencoded',
            'X-Requested-With':'M5Dashboard'},
   body:body}).then(function(r){return r.json();}).then(function(d){
  if(d&&d.ok){m.textContent=d.msg||'Saved. Rebooting...';}
  else{b.disabled=false;m.textContent='Error: '+((d&&d.error)||'unknown');}
 }).catch(function(){m.textContent='Saved. The device is rebooting...';});
});
</script>
</body>
</html>
)==";
