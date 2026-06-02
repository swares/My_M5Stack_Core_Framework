// ============================================================
//  WebAPI.cpp  –  HTTPS dashboard + REST API
// ============================================================
#include "Config.h"        // OUT_WEB — must precede the #if
#if OUT_WEB
#include "WebAPI.h"
#include "Framework.h"
#include "BoardInfo.h"
#include "Settings.h"  // runtime WiFi + dashboard login (approach B)
#include <WiFiUdp.h>   // captive-portal DNS responder (lwIP sockets — safe)
#include <SD.h>        // sdcard_type_t enum (CARD_NONE/MMC/SD/SDHC) for /api SD status
#include <esp_log.h>   // esp_log_level_set() — quiets the esp-tls log tag
#include <cstdio>      // snprintf

// Embedded self-signed TLS certificate + private key (DER).  See
// https_cert.h for why the cert is baked in rather than generated
// on the device.
#include "https_cert.h"

// ============================================================
//  Embedded HTML dashboard – stored in Flash (PROGMEM)
//  The page title and topbar headline are filled in by JavaScript
//  using values from /api/all (board.long_name), so the same
//  binary works for any supported board without a recompile.
//
//  The dashboard is a single self-contained vanilla-JS page — no
//  external scripts, no internet needed.  It polls /api/all and
//  splits devices into two sections:
//    • Controllable Devices — cards with live interactive widgets
//      (toggle / slider / colour / text / button).  The widgets
//      are built generically from the "controls" array each
//      controllable device emits via IDevice::controlSchema();
//      changing a widget issues POST /api/<slug>/set (body
//      <id>=<value>); the old GET ?<id>=<value> form still works.
//    • Read-only Sensors — reading cards, as before.
//  An API request log panel records every /set call + its status.
// ============================================================
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

<div class="section-head" id="ctrl-head" hidden>
  <h2>Controllable Devices</h2>
  <span class="count" id="ctrl-count">0</span>
  <span class="hint">Widgets call GET /api/&lt;slug&gt;/set</span>
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
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
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
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
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
   headers:{'Content-Type':'application/x-www-form-urlencoded'},
   body:body}).then(function(r){return r.json();}).then(function(d){
  if(d&&d.ok){m.textContent=d.msg||'Saved. Rebooting...';}
  else{b.disabled=false;m.textContent='Error: '+((d&&d.error)||'unknown');}
 }).catch(function(){m.textContent='Saved. The device is rebooting...';});
});
</script>
</body>
</html>
)==";

// ============================================================

// Forward declaration — the shared setup-submit handler is defined
// with the other setup helpers further below; begin() references it
// for the captive (plain-HTTP) server.
template <class Srv> static void serveSetupSubmit(Srv* srv);

// ── Plain-HTTP-AP response helpers (templated on server type) ─
//  Mirror _cors / _json / _requireAuth / _reqPath but operate on any
//  server, so the standalone-AP routes can reuse the JSON the HTTPS
//  routes build.  Same pattern as serveSetupSubmit<Srv>.
template <class Srv> static void apCors(Srv* s) {
  s->sendHeader("Access-Control-Allow-Origin",  "*");
  s->sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  s->sendHeader("Access-Control-Allow-Headers", "Content-Type");
  s->sendHeader("Cache-Control",                "no-cache");
}
template <class Srv> static void apSendJson(Srv* s, JsonDocument& doc, int code = 200) {
  apCors(s);
  String out;
  serializeJson(doc, out);
  s->send(code, "application/json", out);
}
template <class Srv> static bool apAuth(Srv* s) {
  String user = Settings::webUser();
  if (user.isEmpty()) return true;                 // auth disabled
  if (s->authenticate(user.c_str(), Settings::webPass().c_str())) return true;
  s->requestAuthentication();
  return false;
}
template <class Srv> static String apPath(Srv* s) {
  String u = s->uri();
  int q = u.indexOf('?');
  return (q >= 0) ? u.substring(0, q) : u;
}

void WebAPI::begin(Framework* fw) {
  _fw = fw;
  if (!enabled) return;

  // ── Quiet the TLS handshake-abort log spam ───────────────────
  //  Browsers (Chrome especially) routinely open speculative /
  //  pre-connect TLS sockets and abandon the ones they end up not
  //  using.  Each abandoned socket makes ESP-IDF's esp-tls layer
  //  print a red "mbedtls_ssl_handshake returned -0x7780" line.
  //  -0x7780 is MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE — the peer
  //  closed the handshake — which is normal background noise on
  //  any LAN web server, not a device fault.  Silence just that
  //  one log tag so the serial console stays readable.
  //
  //  NOTE: this tag is shared with the MQTT-over-TLS client.  If
  //  you ever need to debug an MQTTS handshake, comment this line
  //  out temporarily to see the esp-tls diagnostics again.
  esp_log_level_set("esp-tls-mbedtls", ESP_LOG_NONE);

  // ── Standalone-AP plain-HTTP mode ─────────────────────────
  //  When WEB_AP_PLAIN_HTTP is set and we're a provisioned standalone
  //  access point, serve the dashboard + API over plain HTTP on port
  //  80 and DON'T start the TLS server at all (no self-signed-cert
  //  warning over our own WPA2 AP, and the TLS RAM stays free).  Setup
  //  mode (captive portal) and station mode are unaffected and fall
  //  through to the HTTPS path below.
  _plainAp = WEB_AP_PLAIN_HTTP && !_setupMode() && Settings::apOnlyMode();
  if (_plainAp) {
    _httpRedirect = new ESPWebServer(WEB_HTTP_REDIRECT_PORT);
    _wirePlainAp(_httpRedirect);
    _httpRedirect->begin();
    Serial.printf("[WebAPI] STANDALONE AP — dashboard over PLAIN HTTP on "
                  "port %d (HTTPS disabled via WEB_AP_PLAIN_HTTP)\n",
                  WEB_HTTP_REDIRECT_PORT);
    return;
  }

  // ── HTTPS server (TLS) ────────────────────────────────────
  //  ESPWebServerSecure mirrors the standard Arduino WebServer
  //  API, so the route registration below is unchanged from the
  //  old plain-HTTP server — only the transport is now TLS.  The
  //  embedded self-signed certificate must be set before begin().
  _srv = new ESPWebServerSecure(WEB_HTTPS_PORT);
  _srv->setServerKeyAndCert(HTTPS_KEY_DER,  HTTPS_KEY_DER_LEN,
                            HTTPS_CERT_DER, HTTPS_CERT_DER_LEN);

  // Every route handler first passes through _requireAuth().  When
  // WEB_AUTH_USER is empty this is a no-op; otherwise the request
  // is rejected with 401 unless it carries valid HTTP Basic Auth.
  _srv->on("/",           [this](){
    // In setup mode (unprovisioned, or recovery AP after a failed
    // join) "/" serves the setup portal with NO auth — there may be
    // no usable password to enter yet.  Otherwise it's the dashboard
    // behind the usual auth.
    if (!_setupMode() && !_requireAuth()) return;
    _route_root();
  });
  // Provisioning submit.  Open in setup mode; auth-gated otherwise so
  // a normally-running unit can't be re-provisioned by anyone who can
  // reach it.
  _srv->on("/api/setup",  [this](){
    if (!_setupMode() && !_requireAuth()) return;
    _route_setup();
  });
  // Auth-gated credential editor (provisioned device, over HTTPS).
  _srv->on("/settings", [this](){
    if (!_requireAuth()) return;
    _cors();
    _srv->send_P(200, "text/html; charset=utf-8", SETTINGS_HTML);
  });
  _srv->on("/api/settings", [this](){
    if (!_requireAuth()) return;
    _route_settingsGet();
  });
  _srv->on("/api/settings/save", [this](){
    if (!_requireAuth()) return;
    _route_settingsSave();
  });
  _srv->on("/api/all",    [this](){
    if (!_requireAuth()) return;
    _route_all();
  });
  _srv->on("/api/scan",   [this](){
    if (!_requireAuth()) return;
    _route_scan();
  });
  _srv->on("/api/config", [this](){
    if (!_requireAuth()) return;
    _route_config();
  });
  _srv->on("/api/rescan", [this](){
    if (!_requireAuth()) return;
    // GET /api/rescan  → re-run the boot-time scan-and-bind pass.
    // Useful after plugging in a sensor (the device must already
    // have been powered when the call is made — the framework does
    // not hot-plug, it just rebinds what's currently on the bus).
    _fw->rescanAll();
    _route_all();   // reply with the fresh sensor map
  });
  _srv->on("/api/mqtt", [this](){
    if (!_requireAuth()) return;
    _route_mqtt();
  });
  _srv->on("/api/endpoints", [this](){
    if (!_requireAuth()) return;
    _route_endpoints();
  });
  _srv->on("/api/sdcard", [this](){
    if (!_requireAuth()) return;
    _route_sdcard();
  });
  _srv->on("/api/sdcard/flush", [this](){
    if (!_requireAuth()) return;
    bool ok = _fw->sdlog.flush();
    JsonDocument doc;
    _buildSdStatus(doc);
    doc["flushed"] = ok;
    _json(doc);
  });
  _srv->on("/api/sdcard/eject", [this](){
    if (!_requireAuth()) return;
    // Cleanly close the log file + unmount.  After this the card
    // is safe to physically remove; logging resumes only on reboot.
    bool ok = _fw->sdlog.eject();
    JsonDocument doc;
    _buildSdStatus(doc);
    doc["ejected"] = ok;
    _json(doc);
  });
  _srv->on("/api/mqtt/publish", [this](){
    if (!_requireAuth()) return;
    // GET /api/mqtt/publish  → force one immediate publish cycle.
    // Returns the same status doc as /api/mqtt plus a "publish_now"
    // boolean indicating whether the publish actually fired.
    bool fired = _fw->mqtt.publishNow();
    JsonDocument doc;
    _buildMqttStatus(doc);
    doc["publish_now"] = fired;
    _json(doc);
  });
  _srv->onNotFound([this](){
    String uri = _reqPath();          // path only — query stripped
    // Catch /api/setup here too, in case the server's on() only
    // registered it for GET and the portal submits via POST.  Same
    // gating as the dedicated route: open in setup mode, else auth.
    if (uri == "/api/setup") {
      if (!_setupMode() && !_requireAuth()) return;
      _route_setup();
      return;
    }
    if (!_requireAuth()) return;
    // The settings editor submits the save via POST.  Like /api/setup,
    // the on() registration above only matches GET, so the POST lands
    // here — catch it explicitly before the /api/<slug> plugin lookup,
    // otherwise "settings/save" is mistaken for a plugin slug and 404s
    // with "plugin not found: settings/save".
    if (uri == "/api/settings/save") {
      _route_settingsSave();
      return;
    }
    if (uri == "/api/settings") {
      _route_settingsGet();
      return;
    }
    if (!uri.startsWith("/api/")) {
      _route_404();
      return;
    }
    // /api/<slug>/set  → control endpoint for a controllable
    // plugin.  /api/<slug>  → that plugin's readings.
    if (uri.endsWith("/set")) {
      String slug = uri.substring(5, uri.length() - 4);
      _route_control(slug);
    } else {
      _route_plugin();
    }
  });
  _srv->begin();

  // ── HTTP server (port 80) ─────────────────────────────────
  _httpRedirect = new ESPWebServer(WEB_HTTP_REDIRECT_PORT);
  if (_setupMode()) {
    // Captive-portal mode: serve the setup page (and accept the
    // submit) over plain HTTP so a phone's captive-portal probe opens
    // it automatically — and without a self-signed-cert warning.  The
    // link is already WPA2-encrypted on our own AP.  Paired with the
    // DNS hijack below, any URL the device requests lands here.
    _httpRedirect->onNotFound([this]() {
      // Handle the submit by path (covers POST even if on() is
      // GET-only); serve the portal for every other request so the
      // OS captive check opens it.
      if (_httpRedirect->uri() == "/api/setup") {
        serveSetupSubmit(_httpRedirect);
        return;
      }
      _httpRedirect->send_P(200, "text/html; charset=utf-8", SETUP_HTML);
    });
    _httpRedirect->begin();
#if CAPTIVE_DNS_ENABLED
    // Point every hostname at us (resolve *.* → our AP IP) so the OS
    // captive check is redirected to the portal.  WiFiUDP uses the lwIP
    // SOCKETS layer (socket/bind/recvfrom) — thread-safe, unlike the
    // raw udp_new path DNSServer / AsyncUDP take, which asserts here.
    // Serviced (polled) in update().
    _dnsUdp = new WiFiUDP();
    if (_dnsUdp->begin(53)) {
      Serial.printf("[WebAPI] Captive portal active — DNS → %s\n",
                    WiFi.softAPIP().toString().c_str());
    } else {
      Serial.println(F("[WebAPI] Captive DNS failed to bind :53 — portal "
                       "still at the AP IP."));
      delete _dnsUdp;
      _dnsUdp = nullptr;
    }
#else
    Serial.printf("[WebAPI] Setup portal at http://%s/  (captive DNS off — "
                  "browse there manually)\n",
                  WiFi.softAPIP().toString().c_str());
#endif
  } else {
    // Normal mode: serve no content; 301 every request to the HTTPS
    // URL so an old http:// bookmark still lands on the secure page.
    _httpRedirect->onNotFound([this]() { _sendRedirect(); });
    _httpRedirect->begin();
  }

  if (_setupMode()) {
    Serial.printf("[WebAPI] HTTPS server on port %d — SETUP MODE, "
                  "serving setup portal at /\n", WEB_HTTPS_PORT);
  } else if (Settings::webUser().length() > 0) {
    Serial.printf("[WebAPI] HTTPS server on port %d (auth: user=%s)\n",
                  WEB_HTTPS_PORT, Settings::webUser().c_str());
  } else {
    Serial.printf("[WebAPI] HTTPS server on port %d (no auth)\n",
                  WEB_HTTPS_PORT);
  }
  Serial.printf("[WebAPI] HTTP port %d redirects -> HTTPS\n",
                WEB_HTTP_REDIRECT_PORT);
}

void WebAPI::update() {
  if (!enabled) return;
  if (_dnsUdp)       _serviceCaptiveDns();         // captive-portal DNS
  if (_srv)          _srv->handleClient();
  if (_httpRedirect) _httpRedirect->handleClient();
}

// ── _serviceCaptiveDns ───────────────────────────────────────
//  Answer one pending DNS query (if any) with an A record pointing at
//  the AP IP, so every hostname resolves to us and the OS opens the
//  setup portal.  Echoes the question, appends one answer, drops any
//  additional/EDNS records.  WiFiUDP = lwIP sockets = thread-safe.
void WebAPI::_serviceCaptiveDns() {
  int avail = _dnsUdp->parsePacket();
  if (avail <= 0) return;

  uint8_t q[320];
  int qlen = _dnsUdp->read(q, sizeof(q));
  if (qlen < 12) return;                  // too short to be a DNS query

  // Walk the first question's QNAME (length-prefixed labels, 0-term).
  int pos = 12;
  while (pos < qlen && q[pos] != 0) {
    pos += q[pos] + 1;
    if (pos >= qlen) return;              // malformed
  }
  pos += 1 + 4;                           // 0 terminator + QTYPE + QCLASS
  if (pos > qlen) return;
  const int qend = pos;                  // header + one question

  uint8_t resp[320];
  if (qend + 16 > (int)sizeof(resp)) return;
  memcpy(resp, q, qend);                  // copy header + question verbatim
  resp[2] = 0x81;                         // QR=1, AA=1
  resp[3] = 0x80;                         // RA=1, RCODE=0
  resp[4] = 0x00; resp[5] = 0x01;         // QDCOUNT = 1
  resp[6] = 0x00; resp[7] = 0x01;         // ANCOUNT = 1
  resp[8] = 0x00; resp[9] = 0x00;         // NSCOUNT = 0
  resp[10] = 0x00; resp[11] = 0x00;       // ARCOUNT = 0 (drop EDNS OPT)

  IPAddress apIP = WiFi.softAPIP();
  int i = qend;
  resp[i++] = 0xC0; resp[i++] = 0x0C;     // NAME → pointer to the question
  resp[i++] = 0x00; resp[i++] = 0x01;     // TYPE  A
  resp[i++] = 0x00; resp[i++] = 0x01;     // CLASS IN
  resp[i++] = 0x00; resp[i++] = 0x00;
  resp[i++] = 0x00; resp[i++] = 0x3C;     // TTL = 60 s
  resp[i++] = 0x00; resp[i++] = 0x04;     // RDLENGTH = 4
  resp[i++] = apIP[0]; resp[i++] = apIP[1];
  resp[i++] = apIP[2]; resp[i++] = apIP[3];

  _dnsUdp->beginPacket(_dnsUdp->remoteIP(), _dnsUdp->remotePort());
  _dnsUdp->write(resp, i);
  _dnsUdp->endPacket();
}

// ── _sendRedirect ─────────────────────────────────────────────
//  Handler for the plain-HTTP port-80 server: 301 every request to
//  the same path on HTTPS.  The target host comes from the request
//  Host header (port stripped); if absent, the device IP is used.
void WebAPI::_sendRedirect() {
  String host = _httpRedirect->hostHeader();
  int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);
  if (host.length() == 0) host = WiFi.localIP().toString();

  String loc = "https://" + host;
  if (WEB_HTTPS_PORT != 443) loc += ":" + String(WEB_HTTPS_PORT);
  loc += _httpRedirect->uri();

  _httpRedirect->sendHeader("Location", loc);
  _httpRedirect->send(301, "text/plain", "Redirecting to HTTPS");
}

// ── _reqPath ──────────────────────────────────────────────────
//  ESPWebServerSecure::uri() returns the FULL request target,
//  query string included (e.g. "/api/pps/set?vset=5") — unlike the
//  standard Arduino WebServer, whose uri() is path-only.  Routing
//  that inspects the path (does it end in "/set"? what's the slug?)
//  must therefore strip the "?query" first, or a controllable
//  device's /set endpoint never matches.  Query parameters are
//  still read normally via _srv->arg()/args(), which parse them
//  independently of uri().
String WebAPI::_reqPath() {
  String u = _srv->uri();
  int q = u.indexOf('?');
  return (q >= 0) ? u.substring(0, q) : u;
}

// ── helpers ───────────────────────────────────────────────────
bool WebAPI::_requireAuth() {
  // Effective dashboard login: NVS override (set via the setup portal)
  // first, else the compiled Secrets.h value.
  String user = Settings::webUser();
  // Empty user disables authentication — every route serves freely.
  if (user.isEmpty()) return true;
  // authenticate() returns true if the Authorization header matches.
  // The credentials travel inside the TLS tunnel, encrypted on the wire.
  if (_srv->authenticate(user.c_str(), Settings::webPass().c_str())) return true;
  // Send 401 + WWW-Authenticate so the browser prompts.
  _srv->requestAuthentication();
  return false;
}

void WebAPI::_cors() {
  _srv->sendHeader("Access-Control-Allow-Origin",  "*");
  _srv->sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  _srv->sendHeader("Access-Control-Allow-Headers", "Content-Type");
  _srv->sendHeader("Cache-Control",                "no-cache");
}

void WebAPI::_json(JsonDocument& doc, int code) {
  _cors();
  String out;
  serializeJsonPretty(doc, out);
  _srv->send(code, "application/json", out);
}

void WebAPI::_buildSensorObj(JsonObject& obj, IDevice* p) {
  obj["name"]   = p->name();
  obj["slug"]   = p->slug();
  obj["active"] = p->active;
  obj["addr"]   = p->addr;
  // How the device is physically attached:
  //   builtin   – soldered on the Core / power base
  //   stackable – an M-Bus stacking module (internal bus)
  //   pluggable – a cabled Grove / Port-A unit
  switch (p->mount()) {
    case MountType::Builtin:   obj["mount"] = "builtin";   break;
    case MountType::Stackable: obj["mount"] = "stackable"; break;
    default:                   obj["mount"] = "pluggable"; break;
  }
  // True for output devices that accept commands via
  // POST /api/<slug>/set (body <param>=<value>; GET ?query also works).
  obj["controllable"] = p->controllable();
  // Label "shared" on Core1 (single physical bus); otherwise route
  // through BoardInfo's bus pointers so the label matches reality
  // regardless of which TwoWire instance the board happens to use.
  // For plugins bound behind an I2C hub, emit a compact descriptor
  // including the hub address and channel.
  const auto& bi = BoardInfo::detect();
  if (p->muxAddr != 0) {
    char buf[24];
    snprintf(buf, sizeof(buf), "hub 0x%02X ch%u", p->muxAddr, p->muxChannel);
    obj["bus"]     = buf;
    obj["hub"]     = p->muxAddr;
    obj["channel"] = p->muxChannel;
  } else if (bi.sharedBus()) {
    obj["bus"] = "shared";
  } else if (p->bus == bi.intBus) {
    obj["bus"] = "internal";
  } else if (p->bus == bi.extBus) {
    obj["bus"] = "external";
  } else {
    obj["bus"] = "?";
  }
  if (p->active) {
    JsonObject rd = obj["readings"].to<JsonObject>();
    p->toJson(rd);
  }
  // Controllable devices additionally describe their widgets via
  // controlSchema() so the dashboard can render interactive
  // controls generically.  Only emitted for active devices — an
  // unbound device has no live state to seed the widget values.
  if (p->active && p->controllable()) {
    JsonArray ctl = obj["controls"].to<JsonArray>();
    p->controlSchema(ctl);
  }
}

// ── routes ────────────────────────────────────────────────────
void WebAPI::_route_root() {
  _cors();
  // Setup mode → the setup portal instead of the dashboard (which
  // would have no network data to show anyway).
  if (_setupMode()) {
    _srv->send_P(200, "text/html; charset=utf-8", SETUP_HTML);
    return;
  }
  _srv->send_P(200, "text/html; charset=utf-8", DASH_HTML);
}

// ── _setupMode ───────────────────────────────────────────────
bool WebAPI::_setupMode() {
  return !Settings::isProvisioned() || (_fw && _fw->forceSetupPortal());
}

// ── URL-decode helpers (approach B setup form) ───────────────
//  We decode the submitted fields ourselves so any password
//  character round-trips exactly, independent of how the HTTPS
//  compat layer parses request bodies.
static String _urlDecode(const String& s) {
  auto hex = [](char h) -> int {
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
    return -1;
  };
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < s.length()) {
      int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
      if (hi >= 0 && lo >= 0) { out += char((hi << 4) | lo); i += 2; }
      else out += c;
    } else {
      out += c;
    }
  }
  return out;
}

//  Extract one field from an application/x-www-form-urlencoded blob
//  ("a=1&b=2"), URL-decoded.  Returns "" when the key is absent.
//  Safe for values containing & = etc. because URLSearchParams
//  percent-encodes those inside values.
static String _formField(const String& body, const String& key) {
  int i = 0;
  const int n = body.length();
  while (i < n) {
    int amp = body.indexOf('&', i);
    if (amp < 0) amp = n;
    int eq = body.indexOf('=', i);
    if (eq >= 0 && eq < amp) {
      if (body.substring(i, eq) == key) return _urlDecode(body.substring(eq + 1, amp));
    }
    i = amp + 1;
  }
  return "";
}

//  Read one setup field: our own decode of the raw body/query first,
//  then the server's arg parser as a fallback.  Templated on the
//  server type so it works for both the HTTPS (_srv) and the plain
//  HTTP captive (_httpRedirect) servers.
template <class Srv>
static String _setupField(Srv* srv, const String& src, const char* key) {
  String v = _formField(src, key);
  if (v.isEmpty()) v = srv->arg(key);
  return v;
}

// ── serveSetupSubmit ─────────────────────────────────────────
//  Shared handler for POST /api/setup, used by both the HTTPS server
//  and the plain-HTTP captive server.  Saves Wi-Fi + the optional
//  dashboard / MQTT / Claude credentials to NVS, marks the device
//  provisioned, replies, then reboots.  POST keeps the secrets out of
//  the request-line log; we decode the fields ourselves so any
//  character round-trips exactly.
template <class Srv>
static void serveSetupSubmit(Srv* srv) {
  String src = srv->arg("plain");          // raw POST body, or ""
  if (src.isEmpty()) {                      // fall back to the query
    String full = srv->uri();
    int qm = full.indexOf('?');
    if (qm >= 0) src = full.substring(qm + 1);
  }

  String ssid   = _setupField(srv, src, "ssid");
  String wpass  = _setupField(srv, src, "wpass");
  String user   = _setupField(srv, src, "user");
  String upass  = _setupField(srv, src, "upass");
  String mqttH  = _setupField(srv, src, "mqtthost");
  String mqttPt = _setupField(srv, src, "mqttport");
  String mqttU  = _setupField(srv, src, "mqttuser");
  String mqttP  = _setupField(srv, src, "mqttpass");
  String claude = _setupField(srv, src, "claude");
  // Standalone-AP checkbox: present (any non-empty value) → run as our
  // own access point serving the dashboard, with no upstream Wi-Fi.
  bool apOnly   = _setupField(srv, src, "aponly").length() > 0;

  if (apOnly) {
    // No network to join — clear any stored STA creds so _connectWiFi()
    // takes the access-point path, and record the deliberate choice.
    Settings::setWifi("", "");
    Settings::setApOnly(true);
  } else {
    if (ssid.isEmpty()) {
      srv->send(400, "application/json", "{\"error\":\"wifi ssid required\"}");
      return;
    }
    Settings::setWifi(ssid, wpass);
    Settings::setApOnly(false);
  }
  // Optional sections: only overwrite when the user supplied a value,
  // otherwise keep whatever is already set (NVS or compiled).
  if (user.length())   Settings::setWebLogin(user, upass);
  if (mqttH.length()) {
    long p = mqttPt.toInt();                 // 0/invalid → keep current
    Settings::setMqttServer(mqttH,
        (p > 0 && p < 65536) ? (uint16_t)p : Settings::mqttPort());
  }
  if (mqttU.length())  Settings::setMqtt(mqttU, mqttP);
  if (claude.length()) Settings::setClaudeKey(claude);
  Settings::markProvisioned();

  // Static JSON (no field interpolation → no quoting hazards).
  if (apOnly) {
    srv->send(200, "application/json",
              "{\"ok\":true,\"msg\":\"Saved. Rebooting as a standalone access "
              "point — reconnect to the device's Wi-Fi and open its IP.\"}");
    Serial.println(F("[WebAPI] Provisioned via setup portal — standalone AP "
                     "(dashboard) mode. Rebooting."));
  } else {
    srv->send(200, "application/json",
              "{\"ok\":true,\"msg\":\"Saved. Rebooting to join your Wi-Fi...\"}");
    Serial.printf("[WebAPI] Provisioned via setup portal — SSID '%s'. "
                  "Rebooting.\n", ssid.c_str());
  }
  delay(1200);
  ESP.restart();
}

// ── _route_setup ─────────────────────────────────────────────
//  HTTPS entry point for the provisioning submit (see route table).
void WebAPI::_route_setup() { serveSetupSubmit(_srv); }

// ── _route_settingsGet ───────────────────────────────────────
//  GET /api/settings — current NON-SECRET state for the editor.
//  Returns SSID + usernames (so the form can pre-fill) and set/unset
//  booleans for the secrets.  Never returns a password or the key.
void WebAPI::_route_settingsGet() {
  JsonDocument doc;
  doc["provisioned"] = Settings::isProvisioned();
  doc["ap_only"]     = Settings::apOnlyMode();
  doc["wifi_ssid"]   = Settings::wifiSsid();
  doc["web_user"]    = Settings::webUser();
  doc["mqtt_host"]   = Settings::mqttHost();
  doc["mqtt_port"]   = Settings::mqttPort();
  doc["mqtt_user"]   = Settings::mqttUser();
  doc["mqtt_set"]    = Settings::mqttConfigured();
  doc["claude_set"]  = Settings::claudeConfigured();
  _json(doc);
}

// ── _route_settingsSave ──────────────────────────────────────
//  POST /api/settings/save — update any supplied credential, keeping
//  the existing value where a field is left blank, then reboot so the
//  changes take effect (re-inits WiFi / MQTT / Claude cleanly).
void WebAPI::_route_settingsSave() {
  String src = _srv->arg("plain");
  if (src.isEmpty()) {
    String full = _srv->uri();
    int qm = full.indexOf('?');
    if (qm >= 0) src = full.substring(qm + 1);
  }
  String ssid   = _setupField(_srv, src, "ssid");
  String wpass  = _setupField(_srv, src, "wpass");
  String user   = _setupField(_srv, src, "user");
  String upass  = _setupField(_srv, src, "upass");
  String mqttH  = _setupField(_srv, src, "mqtthost");
  String mqttPt = _setupField(_srv, src, "mqttport");
  String mqttU  = _setupField(_srv, src, "mqttuser");
  String mqttP  = _setupField(_srv, src, "mqttpass");
  String claude = _setupField(_srv, src, "claude");
  bool   apOnly = _setupField(_srv, src, "aponly").length() > 0;

  // Standalone-AP toggle takes precedence: when on, clear the stored STA
  // credentials so the next boot comes up as an access point serving the
  // dashboard.  When off, fall through to the normal SSID handling (and
  // make sure any earlier AP-only choice is cleared).
  if (apOnly) {
    Settings::setWifi("", "");
    Settings::setApOnly(true);
  } else {
    Settings::setApOnly(false);
    // For each section: a blank password/key means "keep current", so we
    // merge against the present effective value before saving the pair.
    if (ssid.length()) {
      Settings::setWifi(ssid, wpass.length() ? wpass : Settings::wifiPass());
    }
  }
  if (user.length()) {
    Settings::setWebLogin(user, upass.length() ? upass : Settings::webPass());
  }
  if (mqttH.length()) {
    long p = mqttPt.toInt();
    Settings::setMqttServer(mqttH,
        (p > 0 && p < 65536) ? (uint16_t)p : Settings::mqttPort());
  }
  if (mqttU.length()) {
    Settings::setMqtt(mqttU, mqttP.length() ? mqttP : Settings::mqttPass());
  }
  if (claude.length()) Settings::setClaudeKey(claude);

  _srv->send(200, "application/json",
             "{\"ok\":true,\"msg\":\"Saved. Rebooting...\"}");
  Serial.println(F("[WebAPI] Settings updated via dashboard — rebooting."));
  delay(1200);
  ESP.restart();
}

void WebAPI::_route_all() {
  JsonDocument doc;
  doc["uptime_s"]  = millis() / 1000;
  doc["ip"]        = _fw->apMode() ? WiFi.softAPIP().toString()
                                   : WiFi.localIP().toString();
  doc["port"]      = WEB_HTTPS_PORT;
  doc["scheme"]    = "https";
  doc["free_heap"] = ESP.getFreeHeap();
  doc["cpu_mhz"]   = ESP.getCpuFreqMHz();
  doc["flash_mb"]  = ESP.getFlashChipSize() / (1024 * 1024);

  // Wall-clock from the ESP32 RTC.  time_synced=true means the
  // value came from NTP; false means it's a fallback from the
  // RTC's post-reset epoch (i.e. seconds since boot mapped onto
  // 1970-01-01) and shouldn't be trusted as real time.
  char ts[24];
  bool synced = _fw->nowIso8601(ts, sizeof(ts));
  doc["datetime"]     = ts;
  doc["time_synced"]  = synced;

  // Board identity
  const auto& bi = _fw->board();
  JsonObject bobj = doc["board"].to<JsonObject>();
  bobj["short_name"] = bi.shortName;
  bobj["long_name"]  = bi.longName;
  bobj["i2c_int_sda"]= bi.i2cIntSda;
  bobj["i2c_int_scl"]= bi.i2cIntScl;
  bobj["i2c_ext_sda"]= bi.i2cExtSda;
  bobj["i2c_ext_scl"]= bi.i2cExtScl;

  JsonArray arr = doc["sensors"].to<JsonArray>();
  for (auto* p : _fw->plugins()) {
    JsonObject obj = arr.add<JsonObject>();
    _buildSensorObj(obj, p);
  }
  _json(doc);
}

void WebAPI::_route_plugin() {
  String slug = _reqPath().substring(5);    // strip "/api/" prefix
  for (auto* p : _fw->plugins()) {
    if (String(p->slug()) == slug) {
      JsonDocument doc;
      JsonObject obj = doc.to<JsonObject>();
      _buildSensorObj(obj, p);
      _json(doc);
      return;
    }
  }
  JsonDocument err;
  err["error"] = "plugin not found: " + slug;
  _json(err, 404);
}

// ── _route_control ───────────────────────────────────────────
//  POST /api/<slug>/set   (body: <param>=<value>[&...])   — preferred
//  GET  /api/<slug>/set?<param>=<value>[&...]              — still works
//  Drive a controllable output device (relays, servos, LEDs).
//
//  POST is preferred because a /set call changes physical state (and
//  may bill an LLM/cloud token): a GET can be fired speculatively by a
//  browser prefetch / link probe and its values land in URL logs.  Both
//  verbs are accepted so existing GET bookmarks and scripts keep
//  working — params are read from the query string AND a url-encoded
//  POST body, merged.  (The secure server doesn't fold a POST body into
//  arg()/args() reliably, so the body is decoded here, the same way the
//  setup/settings handlers do via _formField.)
//
//  Each parameter is handed to the plugin's command(), which validates
//  it.  Valid commands are applied; anything the plugin rejects
//  (unknown param, malformed or out-of-range value) is listed under
//  "rejected" and the hardware is left untouched for that parameter.
//  This is the ONLY control path — there is no serial/display control
//  for output devices.
//
//  Controllable modules are all on the root (internal) bus, so
//  no mux channel selection is needed here.
void WebAPI::_route_control(const String& slug) {
  IDevice* target = nullptr;
  for (auto* p : _fw->plugins()) {
    if (String(p->slug()) == slug) {
      target = p;
      break;
    }
  }

  JsonDocument doc;
  doc["slug"] = slug;

  if (!target) {
    doc["error"] = "plugin not found";
    _json(doc, 404);
    return;
  }
  if (!target->active) {
    doc["error"] = "plugin not active";
    _json(doc, 409);
    return;
  }
  if (!target->controllable()) {
    doc["error"] = "plugin is not controllable";
    _json(doc, 400);
    return;
  }

  JsonArray applied  = doc["applied"].to<JsonArray>();
  JsonArray rejected = doc["rejected"].to<JsonArray>();

  // Apply one param→value pair through the plugin, recording the result.
  auto apply = [&](const String& k, const String& v) {
    if (k.isEmpty()) return;
    bool ok = target->command(k, v);
    JsonObject e = (ok ? applied : rejected).add<JsonObject>();
    e["param"] = k;
    e["value"] = v;
  };

  // 1) Query-string params (the GET path, and any ?query on a POST).
  //    Skip "plain" — that's the server's name for the raw POST body,
  //    which we decode ourselves below, not a real command parameter.
  int nArgs = _srv->args();
  for (int i = 0; i < nArgs; i++) {
    String k = _srv->argName(i);
    if (k == "plain") continue;
    apply(k, _srv->arg(i));
  }

  // 2) url-encoded POST body ("a=1&b=2"), decoded the same way the
  //    setup/settings handlers do — the secure server doesn't reliably
  //    expose body fields through arg()/args().
  String body = _srv->arg("plain");
  int start = 0;
  const int n = body.length();
  while (start < n) {
    int amp = body.indexOf('&', start);
    if (amp < 0) amp = n;
    int eq = body.indexOf('=', start);
    if (eq >= 0 && eq < amp) {
      String k = body.substring(start, eq);
      String v = _urlDecode(body.substring(eq + 1, amp));
      k.trim();
      apply(k, v);
    }
    start = amp + 1;
  }
  // ok = at least one command applied and nothing was rejected.
  doc["ok"] = (rejected.size() == 0 && applied.size() > 0);
  // Echo the device's resulting state so a client sees the effect.
  JsonObject st = doc["state"].to<JsonObject>();
  target->toJson(st);
  _json(doc, rejected.size() ? 400 : 200);
}

void WebAPI::_route_scan() {
  // Delegate to Framework::scanReport() — it knows the board's bus
  // topology (shared vs separate) AND walks every registered hub's
  // 8 channels.  Doing the scan in WebAPI directly meant the old
  // implementation hardcoded Wire/Wire1 and ignored the muxes,
  // which on a Core1+PaHUB setup made everything behind the hub
  // invisible to the API.
  JsonDocument doc;
  _fw->scanReport(doc);
  _json(doc);
}

void WebAPI::_route_config() {
  JsonDocument doc;
  const auto& bi = _fw->board();
  doc["board_short_name"] = bi.shortName;
  doc["board_long_name"]  = bi.longName;
  doc["i2c_int_sda"]      = bi.i2cIntSda;
  doc["i2c_int_scl"]      = bi.i2cIntScl;
  doc["i2c_ext_sda"]      = bi.i2cExtSda;
  doc["i2c_ext_scl"]      = bi.i2cExtScl;
  doc["wifi_ssid"]        = Settings::wifiSsid();   // effective (NVS or compiled)
  doc["wifi_mode"]        = _fw->apMode() ? "ap" : "station";
  doc["ap_only"]          = Settings::apOnlyMode();
  doc["provisioned"]      = Settings::isProvisioned();
  doc["poll_ms"]          = POLL_MS;
  doc["i2c_int_freq"]     = I2C_INT_FREQ;
  doc["i2c_ext_freq"]     = I2C_EXT_FREQ;
  doc["out_web"]          = OUT_WEB;
  doc["out_serial"]       = OUT_SERIAL;
  doc["out_display"]      = OUT_DISPLAY;
  doc["display_scroll"]   = DISPLAY_SCROLL;
  doc["display_cycle_ms"] = DISPLAY_CYCLE_MS;
  _json(doc);
}

void WebAPI::_route_404() {
  JsonDocument doc;
  doc["error"] = "not found";
  doc["uri"]   = _srv->uri();
  _json(doc, 404);
}

void WebAPI::_route_mqtt() {
  JsonDocument doc;
  _buildMqttStatus(doc);
  _json(doc);
}

void WebAPI::_route_sdcard() {
  JsonDocument doc;
  _buildSdStatus(doc);
  _json(doc);
}

// Shared between /api/sdcard and /api/sdcard/flush so the JSON
// shape stays stable for dashboards polling either URL.
void WebAPI::_buildSdStatus(JsonDocument& doc) {
  const auto& st = _fw->sdlog.stats();
  const auto& bi = _fw->board();

  doc["enabled"]      = _fw->sdlog.enabled;
  doc["supported"]    = bi.hasSdCard;
  doc["present"]      = st.present;
  doc["active"]       = st.active;
  doc["log_interval_ms"] = SD_LOG_INTERVAL_MS;
  doc["spi_hz"]       = static_cast<uint32_t>(SD_SPI_HZ);
  // self_test: "pass", "fail", or "not-run".  A "fail" here is the
  // smoking gun for a card whose writes don't persist — see the
  // boot serial log for the suggested fix (lower SD_SPI_HZ).
  const char* selfTestStr = "not-run";
  if (st.selfTest == 1)
    selfTestStr = "pass";
  else if (st.selfTest == 0)
    selfTestStr = "fail";
  doc["self_test"]    = selfTestStr;

  const char* tname = "?";
  switch (st.cardType) {
    case CARD_NONE: tname = "none";   break;
    case CARD_MMC:  tname = "MMC";    break;
    case CARD_SD:   tname = "SDSC";   break;
    case CARD_SDHC: tname = "SDHC";   break;
  }
  doc["card_type"]      = tname;
  doc["card_size_mb"]   =
      static_cast<uint32_t>(st.cardSizeBytes / (1024ULL * 1024ULL));
  doc["boot_number"]    = st.bootNumber;
  doc["filename"]       = st.filename;
  doc["rows_written"]   = st.rowsWritten;
  doc["bytes_written"]  = st.bytesWritten;
  doc["write_failures"] = st.writeFailures;
  if (st.lastWriteMs) {
    doc["since_last_write_s"] = (millis() - st.lastWriteMs) / 1000;
  }
}

// ── _route_endpoints ─────────────────────────────────────────
//  Self-describing endpoint list.  Hand-maintained — the HTTPS
//  server library doesn't expose its registered-handler table.
//  Keep this in sync when adding new routes; one place to update
//  beats hunting through README and dashboard separately.
void WebAPI::_route_endpoints() {
  struct Ep { const char* method; const char* url; const char* desc; };
  static const Ep eps[] = {
    { "GET", "/",                 "Live HTML dashboard (auto-refresh ~5s)" },
    { "GET", "/api/all",
      "All plugins + readings + board + system info" },
    { "GET", "/api/{slug}",
      "One plugin's readings (e.g. /api/env4, /api/heart)" },
    { "POST", "/api/{slug}/set",
      "Control an output device (body relay1=1; GET ?relay1=1 also works)" },
    { "GET", "/api/scan",
      "Live I2C scan: root bus(es) + every hub channel" },
    { "GET", "/api/config",       "Framework configuration + board info" },
    { "GET", "/api/rescan",
      "Re-run boot-time scan-and-bind; returns fresh /api/all" },
    { "GET", "/api/mqtt",
      "MQTT runtime status (connected/state/counts/timings)" },
    { "GET", "/api/mqtt/publish",
      "Force one immediate publish cycle; returns status" },
    { "GET", "/api/sdcard",
      "SD card + log file status (boot/filename/rows/bytes)" },
    { "GET", "/api/sdcard/flush",
      "Commit (close+reopen) buffered log writes to the card now" },
    { "GET", "/api/sdcard/eject",
      "Cleanly close the file + unmount; card safe to remove" },
    { "GET", "/api/endpoints",    "This list" },
  };

  JsonDocument doc;
  doc["uptime_s"] = millis() / 1000;
  doc["ip"]       = _fw->apMode() ? WiFi.softAPIP().toString()
                                  : WiFi.localIP().toString();
  doc["port"]     = WEB_HTTPS_PORT;
  doc["scheme"]   = "https";
  doc["auth"]     = Settings::webUser().length() > 0 ? "basic" : "none";

  JsonArray arr = doc["endpoints"].to<JsonArray>();
  for (const auto& e : eps) {
    JsonObject o = arr.add<JsonObject>();
    o["method"]      = e.method;
    o["url"]         = e.url;
    o["description"] = e.desc;
  }
  _json(doc);
}

// Shared between /api/mqtt and /api/mqtt/publish so both endpoints
// emit the same status fields.  Keeps the JSON shape stable for
// dashboards that poll either URL.
void WebAPI::_buildMqttStatus(JsonDocument& doc) {
  const auto& st  = _fw->mqtt.stats();
  bool        wifiUp    = (WiFi.status() == WL_CONNECTED);
  bool        connected = _fw->mqtt.connected();
  uint32_t    now       = millis();

  doc["enabled"]        = _fw->mqtt.enabled;
  doc["configured"]     = Settings::mqttHost().length() > 0;
  doc["host"]           = Settings::mqttHost();
  doc["port"]           = Settings::mqttPort();
  doc["transport"]      = MQTTOut::transport();   // "plain" or "tls"
  doc["tls"]            = MQTTOut::tls();
  doc["tls_mutual"]     = MQTTOut::tlsMutual();    // X.509 client-cert auth
  doc["tls_verified"]   = MQTTOut::tlsVerified();  // broker checked vs CA cert
  doc["client_id"]      = MQTT_CLIENT_ID;
  doc["base_topic"]     = MQTT_BASE_TOPIC;
  // HA discovery is suppressed at runtime only for a mutual-TLS
  // broker (e.g. AWS IoT) that rejects the homeassistant/# tree, so
  // report the effective value.
  doc["ha_discovery"]   = (MQTT_HA_DISCOVERY && !MQTTOut::tlsMutual());
  doc["wifi_connected"] = wifiUp;
  doc["connected"]      = connected;
  doc["last_state"]     = st.lastState;
  doc["last_state_text"] = MQTTOut::stateText(st.lastState);
  doc["connect_attempts"]  = st.connectAttempts;
  doc["connect_successes"] = st.connectSuccesses;
  doc["publish_count"]     = st.publishCount;
  doc["publish_failures"]  = st.publishFailures;
  doc["publish_interval_ms"] = MQTT_PUBLISH_MS;
  // "seconds since" fields default to null when never set, otherwise
  // an integer.  Lets dashboards distinguish "never happened" from
  // "happened just now".
  if (st.lastConnectMs) {
    doc["since_last_connect_s"] = (now - st.lastConnectMs) / 1000;
  }
  if (st.lastDisconnectMs) {
    doc["since_last_disconnect_s"] = (now - st.lastDisconnectMs) / 1000;
  }
  if (st.lastPublishMs) {
    doc["since_last_publish_s"] = (now - st.lastPublishMs) / 1000;
  }
}

// ── _wirePlainAp ─────────────────────────────────────────────
//  Standalone-AP dashboard over plain HTTP.  Intentionally mirrors the
//  HTTPS route table in begin() — if you add or rename a route there,
//  mirror it here.  Reuses the same server-independent builders
//  (_buildSensorObj / _buildMqttStatus / _buildSdStatus /
//  Framework::scanReport) so the JSON shapes stay identical; only the
//  transport and the /api/all scheme+port differ.  (/api/setup and
//  /api/endpoints are intentionally omitted: this path only runs when
//  already provisioned, and the dashboard degrades gracefully without
//  the endpoint chips.)
template <class Srv>
void WebAPI::_wirePlainAp(Srv* s) {
  // ── HTML pages ────────────────────────────────────────────
  s->on("/", [this, s]() {
    if (!apAuth(s)) return;
    apCors(s);
    s->send_P(200, "text/html; charset=utf-8", DASH_HTML);
  });
  s->on("/settings", [this, s]() {
    if (!apAuth(s)) return;
    apCors(s);
    s->send_P(200, "text/html; charset=utf-8", SETTINGS_HTML);
  });

  // /api/all — same doc as _route_all(), but reports the plain-HTTP
  // scheme/port and always the AP IP.
  auto buildAll = [this](JsonDocument& doc) {
    doc["uptime_s"]  = millis() / 1000;
    doc["ip"]        = WiFi.softAPIP().toString();
    doc["port"]      = WEB_HTTP_REDIRECT_PORT;
    doc["scheme"]    = "http";
    doc["free_heap"] = ESP.getFreeHeap();
    doc["cpu_mhz"]   = ESP.getCpuFreqMHz();
    doc["flash_mb"]  = ESP.getFlashChipSize() / (1024 * 1024);
    char ts[24];
    bool synced = _fw->nowIso8601(ts, sizeof(ts));
    doc["datetime"]    = ts;
    doc["time_synced"] = synced;
    const auto& bi = _fw->board();
    JsonObject bobj = doc["board"].to<JsonObject>();
    bobj["short_name"] = bi.shortName;
    bobj["long_name"]  = bi.longName;
    bobj["i2c_int_sda"]= bi.i2cIntSda;
    bobj["i2c_int_scl"]= bi.i2cIntScl;
    bobj["i2c_ext_sda"]= bi.i2cExtSda;
    bobj["i2c_ext_scl"]= bi.i2cExtScl;
    JsonArray arr = doc["sensors"].to<JsonArray>();
    for (auto* p : _fw->plugins()) {
      JsonObject obj = arr.add<JsonObject>();
      _buildSensorObj(obj, p);
    }
  };
  s->on("/api/all", [this, s, buildAll]() {
    if (!apAuth(s)) return;
    JsonDocument doc; buildAll(doc); apSendJson(s, doc);
  });
  s->on("/api/rescan", [this, s, buildAll]() {
    if (!apAuth(s)) return;
    _fw->rescanAll();
    JsonDocument doc; buildAll(doc); apSendJson(s, doc);
  });
  s->on("/api/scan", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc; _fw->scanReport(doc); apSendJson(s, doc);
  });
  s->on("/api/config", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc;
    const auto& bi = _fw->board();
    doc["board_short_name"] = bi.shortName;
    doc["board_long_name"]  = bi.longName;
    doc["i2c_int_sda"]      = bi.i2cIntSda;
    doc["i2c_int_scl"]      = bi.i2cIntScl;
    doc["i2c_ext_sda"]      = bi.i2cExtSda;
    doc["i2c_ext_scl"]      = bi.i2cExtScl;
    doc["wifi_ssid"]        = Settings::wifiSsid();
    doc["wifi_mode"]        = _fw->apMode() ? "ap" : "station";
    doc["ap_only"]          = Settings::apOnlyMode();
    doc["provisioned"]      = Settings::isProvisioned();
    doc["poll_ms"]          = POLL_MS;
    doc["i2c_int_freq"]     = I2C_INT_FREQ;
    doc["i2c_ext_freq"]     = I2C_EXT_FREQ;
    doc["out_web"]          = OUT_WEB;
    doc["out_serial"]       = OUT_SERIAL;
    doc["out_display"]      = OUT_DISPLAY;
    doc["display_scroll"]   = DISPLAY_SCROLL;
    doc["display_cycle_ms"] = DISPLAY_CYCLE_MS;
    doc["scheme"]           = "http";
    apSendJson(s, doc);
  });
  s->on("/api/mqtt", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc; _buildMqttStatus(doc); apSendJson(s, doc);
  });
  s->on("/api/mqtt/publish", [this, s]() {
    if (!apAuth(s)) return;
    bool fired = _fw->mqtt.publishNow();
    JsonDocument doc; _buildMqttStatus(doc); doc["publish_now"] = fired;
    apSendJson(s, doc);
  });
  s->on("/api/sdcard", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc; _buildSdStatus(doc); apSendJson(s, doc);
  });
  s->on("/api/sdcard/flush", [this, s]() {
    if (!apAuth(s)) return;
    bool ok = _fw->sdlog.flush();
    JsonDocument doc; _buildSdStatus(doc); doc["flushed"] = ok;
    apSendJson(s, doc);
  });
  s->on("/api/sdcard/eject", [this, s]() {
    if (!apAuth(s)) return;
    bool ok = _fw->sdlog.eject();
    JsonDocument doc; _buildSdStatus(doc); doc["ejected"] = ok;
    apSendJson(s, doc);
  });
  s->on("/api/settings", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc;
    doc["provisioned"] = Settings::isProvisioned();
    doc["ap_only"]     = Settings::apOnlyMode();
    doc["wifi_ssid"]   = Settings::wifiSsid();
    doc["web_user"]    = Settings::webUser();
    doc["mqtt_host"]   = Settings::mqttHost();
    doc["mqtt_port"]   = Settings::mqttPort();
    doc["mqtt_user"]   = Settings::mqttUser();
    doc["mqtt_set"]    = Settings::mqttConfigured();
    doc["claude_set"]  = Settings::claudeConfigured();
    apSendJson(s, doc);
  });

  // Slug reads, control /set, and POST /api/settings/save land here.
  s->onNotFound([this, s]() {
    if (!apAuth(s)) return;
    String path = apPath(s);

    // POST /api/settings/save — mirror _route_settingsSave().
    if (path == "/api/settings/save") {
      String src = s->arg("plain");
      if (src.isEmpty()) {
        String full = s->uri();
        int qm = full.indexOf('?');
        if (qm >= 0) src = full.substring(qm + 1);
      }
      String ssid   = _setupField(s, src, "ssid");
      String wpass  = _setupField(s, src, "wpass");
      String user   = _setupField(s, src, "user");
      String upass  = _setupField(s, src, "upass");
      String mqttH  = _setupField(s, src, "mqtthost");
      String mqttPt = _setupField(s, src, "mqttport");
      String mqttU  = _setupField(s, src, "mqttuser");
      String mqttP  = _setupField(s, src, "mqttpass");
      String claude = _setupField(s, src, "claude");
      bool   apOnly = _setupField(s, src, "aponly").length() > 0;
      if (apOnly) {
        Settings::setWifi("", "");
        Settings::setApOnly(true);
      } else {
        Settings::setApOnly(false);
        if (ssid.length())
          Settings::setWifi(ssid, wpass.length() ? wpass : Settings::wifiPass());
      }
      if (user.length())
        Settings::setWebLogin(user, upass.length() ? upass : Settings::webPass());
      if (mqttH.length()) {
        long p = mqttPt.toInt();
        Settings::setMqttServer(mqttH,
            (p > 0 && p < 65536) ? (uint16_t)p : Settings::mqttPort());
      }
      if (mqttU.length())
        Settings::setMqtt(mqttU, mqttP.length() ? mqttP : Settings::mqttPass());
      if (claude.length()) Settings::setClaudeKey(claude);
      s->send(200, "application/json",
              "{\"ok\":true,\"msg\":\"Saved. Rebooting...\"}");
      Serial.println(F("[WebAPI] Settings updated via AP dashboard — rebooting."));
      delay(1200);
      ESP.restart();
      return;
    }

    if (!path.startsWith("/api/")) {
      JsonDocument doc;
      doc["error"] = "not found";
      doc["uri"]   = s->uri();
      apSendJson(s, doc, 404);
      return;
    }

    // /api/<slug>/set — control a device.  Mirror _route_control().
    if (path.endsWith("/set")) {
      String slug = path.substring(5, path.length() - 4);
      IDevice* target = nullptr;
      for (auto* p : _fw->plugins()) {
        if (String(p->slug()) == slug) { target = p; break; }
      }
      JsonDocument doc;
      doc["slug"] = slug;
      if (!target) {
        doc["error"] = "plugin not found: " + slug;
        apSendJson(s, doc, 404);
        return;
      }
      if (!target->active) {
        doc["error"] = "plugin not active";
        apSendJson(s, doc, 409);
        return;
      }
      if (!target->controllable()) {
        doc["error"] = "plugin is not controllable";
        apSendJson(s, doc, 400);
        return;
      }
      JsonArray applied  = doc["applied"].to<JsonArray>();
      JsonArray rejected = doc["rejected"].to<JsonArray>();
      auto apply = [&](const String& k, const String& v) {
        if (k.isEmpty()) return;
        bool ok = target->command(k, v);
        JsonObject e = (ok ? applied : rejected).add<JsonObject>();
        e["param"] = k;
        e["value"] = v;
      };
      int nArgs = s->args();
      for (int i = 0; i < nArgs; i++) {
        String k = s->argName(i);
        if (k == "plain") continue;
        apply(k, s->arg(i));
      }
      String body = s->arg("plain");
      int start = 0;
      const int n = body.length();
      while (start < n) {
        int amp = body.indexOf('&', start);
        if (amp < 0) amp = n;
        int eq = body.indexOf('=', start);
        if (eq >= 0 && eq < amp) {
          String k = body.substring(start, eq);
          String v = _urlDecode(body.substring(eq + 1, amp));
          k.trim();
          apply(k, v);
        }
        start = amp + 1;
      }
      doc["ok"] = (rejected.size() == 0 && applied.size() > 0);
      JsonObject st = doc["state"].to<JsonObject>();
      target->toJson(st);
      apSendJson(s, doc, rejected.size() ? 400 : 200);
      return;
    }

    // /api/<slug> — that plugin's readings.
    String slug = path.substring(5);
    for (auto* p : _fw->plugins()) {
      if (String(p->slug()) == slug) {
        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        _buildSensorObj(obj, p);
        apSendJson(s, doc);
        return;
      }
    }
    JsonDocument doc;
    doc["error"] = "plugin not found: " + slug;
    apSendJson(s, doc, 404);
  });
}
#endif  // OUT_WEB

