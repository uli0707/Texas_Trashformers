#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// =============================================================================
// TRASH TRANSFORMER V3 — TB6612FNG dual-driver mecanum robot
// ESP32 DevKit, Arduino core 2.x (ledcSetup + ledcAttachPin)
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │                    POWER  (both drivers)                                │
// ├──────────────────────────────────┬──────────────────────────────────────┤
// │ TB6612 VM                        │ Motor battery + (4.5V–13.5V)         │
// │ TB6612 PGND (all PGND pins)      │ Battery −  AND  ESP32 GND            │
// │ TB6612 VCC                       │ ESP32 3.3V                           │
// │ TB6612 GND (signal GND)          │ ESP32 GND                            │
// └──────────────────────────────────┴──────────────────────────────────────┘
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │                    STANDBY PINS                                         │
// │  Left  driver STBY  ──── GPIO 15   (controls FL + BL)                  │
// │  Right driver STBY  ──── GPIO 2    (controls FR + BR)                  │
// │                                                                         │
// │  ⚠ STBY must be HIGH or the driver is completely frozen.               │
// │    Simplest option: tie BOTH driver STBY pins to one GPIO               │
// │    and set both STBY_LEFT and STBY_RIGHT to that same pin below.        │
// └─────────────────────────────────────────────────────────────────────────┘
//
// ┌───────────────────────────┬─────────────────────────────────────────────┐
// │  LEFT DRIVER              │  RIGHT DRIVER                               │
// │  (FL = ch A, BL = ch B)   │  (FR = ch A, BR = ch B)                    │
// ├─────────────┬─────────────┼─────────────┬───────────────────────────────┤
// │ TB6612 pin  │ ESP32 GPIO  │ TB6612 pin  │ ESP32 GPIO                    │
// ├─────────────┼─────────────┼─────────────┼───────────────────────────────┤
// │ PWMA        │ 18          │ PWMA        │ 22                            │
// │ AIN1        │ 16          │ AIN1        │ 19                            │
// │ AIN2        │ 17          │ AIN2        │ 21                            │
// │ AO1 + AO2   │ FL motor    │ AO1 + AO2   │ FR motor                     │
// ├─────────────┼─────────────┼─────────────┼───────────────────────────────┤
// │ PWMB        │ 13          │ PWMB        │ 26                            │
// │ BIN1        │ 4           │ BIN1        │ 14                            │
// │ BIN2        │ 5           │ BIN2        │ 27                            │
// │ BO1 + BO2   │ BL motor    │ BO1 + BO2   │ BR motor                     │
// └─────────────┴─────────────┴─────────────┴───────────────────────────────┘
//
// IF A WHEEL SPINS THE WRONG WAY:
//   Set that motor's `invert` flag to true in the table below.
//   No rewiring needed.
//
// MECANUM LOGIC  (rollers-inward, viewed from above)
//   FL ╲  ╱ FR       + = forward,  − = reverse,  0 = coast
//      ╲╱
//      ╱╲            Command   | FL | FR | BL | BR
//   BL ╱  ╲ BR       ----------+----+----+----+----
//                    Forward   |  + |  + |  + |  +
//                    Reverse   |  − |  − |  − |  −
//                    Strafe L  |  − |  + |  + |  −
//                    Strafe R  |  + |  − |  − |  +
//                    Diag FL   |  0 |  + |  + |  0
//                    Diag FR   |  + |  0 |  0 |  +
//                    Diag BL   |  − |  0 |  0 |  −
//                    Diag BR   |  0 |  − |  − |  0
//                    Rot CW    |  + |  − |  + |  −
//                    Rot CCW   |  − |  + |  − |  +
// =============================================================================

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
const char* ssid     = "Trash-Transformer-V1";
const char* password = "wastecollection";

AsyncWebServer server(80);

// ---------------------------------------------------------------------------
// Speed  (0–255)
// ---------------------------------------------------------------------------
int globalSpeed = 150;

// ---------------------------------------------------------------------------
// PWM (LEDC) — core 2.x API: ledcSetup + ledcAttachPin
// ---------------------------------------------------------------------------
const int PWM_FREQ       = 5000;
const int PWM_RESOLUTION = 8;

// ---------------------------------------------------------------------------
// STBY pins — MUST be driven HIGH or the TB6612FNG outputs nothing
// ---------------------------------------------------------------------------
const int STBY_LEFT  = 15;   // enables FL + BL
const int STBY_RIGHT =  2;   // enables FR + BR

// ---------------------------------------------------------------------------
// Motor descriptor
// ---------------------------------------------------------------------------
struct Motor {
    const char* name;
    int  in1;     // GPIO → AIN1 or BIN1
    int  in2;     // GPIO → AIN2 or BIN2
    int  pwmPin;  // GPIO → PWMA or PWMB
    int  channel; // LEDC channel (0–15, unique per motor)
    bool invert;  // true = this wheel spins the wrong way — flip in software
};

//         name   IN1  IN2  PWM  CH  invert
Motor mFL = { "FL",  16,  17,  18,  0, false };  // Left  driver ch A
Motor mFR = { "FR",  19,  21,  22,  1, false };  // Right driver ch A
Motor mBL = { "BL",   4,   5,  13,  2, false };  // Left  driver ch B
Motor mBR = { "BR",  14,  27,  26,  3, false };  // Right driver ch B

// ---------------------------------------------------------------------------
// Motor control  —  dir: +1 forward, -1 reverse, 0 coast
// ---------------------------------------------------------------------------
void setMotor(const Motor& m, int dir) {
    if (m.invert) dir = -dir;

    if (dir > 0) {
        digitalWrite(m.in1, HIGH);
        digitalWrite(m.in2, LOW);
        ledcWrite(m.channel, globalSpeed);
    } else if (dir < 0) {
        digitalWrite(m.in1, LOW);
        digitalWrite(m.in2, HIGH);
        ledcWrite(m.channel, globalSpeed);
    } else {
        // Coast: IN1=IN2=LOW, PWM=0
        // For hard electrical brake instead: set IN1=IN2=HIGH
        digitalWrite(m.in1, LOW);
        digitalWrite(m.in2, LOW);
        ledcWrite(m.channel, 0);
    }
}

void stopAll() {
    setMotor(mFL, 0); setMotor(mFR, 0);
    setMotor(mBL, 0); setMotor(mBR, 0);
}

void move(int fl, int fr, int bl, int br) {
    setMotor(mFL, fl); setMotor(mFR, fr);
    setMotor(mBL, bl); setMotor(mBR, br);
}

void driveForward()  { move( 1,  1,  1,  1); }
void driveBack()     { move(-1, -1, -1, -1); }
void strafeLeft()    { move(-1,  1,  1, -1); }
void strafeRight()   { move( 1, -1, -1,  1); }
void diagFwdLeft()   { move( 0,  1,  1,  0); }
void diagFwdRight()  { move( 1,  0,  0,  1); }
void diagBckLeft()   { move(-1,  0,  0, -1); }
void diagBckRight()  { move( 0, -1, -1,  0); }
void rotateCW()      { move( 1, -1,  1, -1); }
void rotateCCW()     { move(-1,  1, -1,  1); }

// =============================================================================
// Web UI
// =============================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Trash Transformer</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@700;900&display=swap');
  :root{--acid:#b5ff2b;--dim:#607000;--bg:#0a0d04;--panel:#111406;--border:#2a3008;--danger:#ff3c28;--muted:#4a5520;--text:#c8d88a}
  *,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
  body{background:var(--bg);color:var(--text);font-family:'Share Tech Mono',monospace;min-height:100dvh;display:flex;flex-direction:column;align-items:center;padding:16px 12px 24px;overflow-x:hidden}
  body::before{content:'';pointer-events:none;position:fixed;inset:0;background:repeating-linear-gradient(180deg,transparent 0px,transparent 3px,rgba(0,0,0,.08) 3px,rgba(0,0,0,.08) 4px);z-index:9999}
  header{width:100%;max-width:420px;border:1px solid var(--border);border-top:3px solid var(--acid);background:var(--panel);padding:12px 16px 10px;margin-bottom:16px;display:flex;justify-content:space-between;align-items:center}
  h1{font-family:'Orbitron',sans-serif;font-weight:900;font-size:clamp(14px,5vw,20px);color:var(--acid);letter-spacing:.08em;text-shadow:0 0 12px rgba(181,255,43,.5)}
  .pip{width:10px;height:10px;border-radius:50%;background:var(--acid);box-shadow:0 0 8px var(--acid);animation:blink 1.4s ease-in-out infinite}
  @keyframes blink{0%,100%{opacity:1}50%{opacity:.2}}
  .speed-panel{width:100%;max-width:420px;background:var(--panel);border:1px solid var(--border);padding:12px 16px;margin-bottom:14px}
  .speed-row{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:8px;font-size:12px;text-transform:uppercase;letter-spacing:.1em;color:var(--muted)}
  .speed-row span{color:var(--acid);font-size:18px;font-family:'Orbitron',sans-serif}
  input[type=range]{-webkit-appearance:none;width:100%;height:6px;background:linear-gradient(90deg,var(--acid) var(--pct,59%),var(--border) var(--pct,59%));border-radius:3px;outline:none;cursor:pointer}
  input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:var(--acid);box-shadow:0 0 8px rgba(181,255,43,.6);cursor:pointer}
  .dpad{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;width:100%;max-width:420px;margin-bottom:8px}
  .btn{background:var(--panel);border:1px solid var(--border);color:var(--text);font-family:'Orbitron',sans-serif;font-size:clamp(9px,2.8vw,12px);font-weight:700;letter-spacing:.06em;padding:0;height:clamp(60px,18vw,80px);border-radius:4px;cursor:pointer;user-select:none;touch-action:manipulation;-webkit-tap-highlight-color:transparent;transition:background .05s,border-color .05s,color .05s;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px}
  .btn .icon{font-size:clamp(16px,5vw,22px);line-height:1}
  .btn .label{font-size:clamp(7px,2vw,10px);opacity:.7}
  .btn:active,.btn.active{background:var(--acid);border-color:var(--acid);color:#000}
  .btn.diag{border-color:var(--dim);color:var(--muted)}
  .btn.diag:active,.btn.diag.active{background:var(--dim);border-color:var(--acid);color:var(--acid)}
  .btn.stop{background:#1a0600;border:1px solid #5a1800;color:var(--danger)}
  .btn.stop:active,.btn.stop.active{background:var(--danger);border-color:var(--danger);color:#fff}
  .rotate-row{display:grid;grid-template-columns:1fr 1fr;gap:8px;width:100%;max-width:420px;margin-bottom:14px}
  .btn.rotate{border-color:var(--dim);color:var(--muted)}
  .btn.rotate:active,.btn.rotate.active{background:var(--dim);border-color:var(--acid);color:var(--acid)}
  footer{width:100%;max-width:420px;border:1px solid var(--border);background:var(--panel);padding:8px 12px;font-size:11px;color:var(--muted);display:flex;justify-content:space-between}
  footer .ok{color:var(--acid)}
</style>
</head>
<body>
<header><h1>TRASH TRANSFORMER</h1><div class="pip" id="pip"></div></header>
<div class="speed-panel">
  <div class="speed-row">THROTTLE &nbsp;<span id="speedVal">150</span><span style="font-size:11px;color:var(--muted)">/255</span></div>
  <input type="range" id="slider" min="0" max="255" value="150">
</div>
<div class="dpad">
  <button class="btn diag" data-cmd="fl"><div class="icon">↖</div><div class="label">DIAG FL</div></button>
  <button class="btn"      data-cmd="f" ><div class="icon">↑</div><div class="label">FORWARD</div></button>
  <button class="btn diag" data-cmd="fr"><div class="icon">↗</div><div class="label">DIAG FR</div></button>
  <button class="btn"      data-cmd="sl"><div class="icon">←</div><div class="label">STRAFE L</div></button>
  <button class="btn stop" data-cmd="s" ><div class="icon">⏹</div><div class="label">STOP</div></button>
  <button class="btn"      data-cmd="sr"><div class="icon">→</div><div class="label">STRAFE R</div></button>
  <button class="btn diag" data-cmd="bl"><div class="icon">↙</div><div class="label">DIAG BL</div></button>
  <button class="btn"      data-cmd="b" ><div class="icon">↓</div><div class="label">BACK</div></button>
  <button class="btn diag" data-cmd="br"><div class="icon">↘</div><div class="label">DIAG BR</div></button>
</div>
<div class="rotate-row">
  <button class="btn rotate" data-cmd="rcw" ><div class="icon">↻</div><div class="label">ROTATE CW</div></button>
  <button class="btn rotate" data-cmd="rccw"><div class="icon">↺</div><div class="label">ROTATE CCW</div></button>
</div>
<footer><span>192.168.4.1</span><span class="ok" id="cmdStatus">READY</span></footer>
<script>
  const slider=document.getElementById('slider'),speedVal=document.getElementById('speedVal');
  slider.addEventListener('input',()=>{const v=slider.value;speedVal.textContent=v;slider.style.setProperty('--pct',(v/255*100).toFixed(1)+'%');});
  slider.addEventListener('change',()=>send('/speed?val='+slider.value));
  slider.style.setProperty('--pct',(150/255*100).toFixed(1)+'%');
  const statusEl=document.getElementById('cmdStatus'),pipEl=document.getElementById('pip');
  let activeBtn=null,stopTimer=null;
  function send(path){fetch(path).then(()=>{statusEl.textContent=path.split('/')[1].split('?')[0].toUpperCase()||'STOP';pipEl.style.background='#b5ff2b';}).catch(()=>{statusEl.textContent='ERR';pipEl.style.background='#ff3c28';});}
  function activate(btn,cmd){clearTimeout(stopTimer);if(activeBtn&&activeBtn!==btn)activeBtn.classList.remove('active');activeBtn=btn;btn.classList.add('active');send('/'+cmd);}
  function release(){if(activeBtn){activeBtn.classList.remove('active');activeBtn=null;}stopTimer=setTimeout(()=>send('/s'),80);}
  document.querySelectorAll('.btn[data-cmd]').forEach(btn=>{
    const cmd=btn.dataset.cmd;
    if(cmd==='s'){
      btn.addEventListener('mousedown',e=>{e.preventDefault();send('/s');});
      btn.addEventListener('touchstart',e=>{e.preventDefault();send('/s');},{passive:false});
      return;
    }
    btn.addEventListener('mousedown',e=>{e.preventDefault();activate(btn,cmd);});
    btn.addEventListener('mouseup',()=>release());
    btn.addEventListener('mouseleave',()=>{if(activeBtn===btn)release();});
    btn.addEventListener('touchstart',e=>{e.preventDefault();activate(btn,cmd);},{passive:false});
    btn.addEventListener('touchend',e=>{e.preventDefault();release();},{passive:false});
    btn.addEventListener('touchcancel',()=>release());
  });
  // Keyboard: arrows=cardinal, Q/E/Z/C=diagonals, A/D=rotate, Space=stop
  const keyMap={'ArrowUp':'f','ArrowDown':'b','ArrowLeft':'sl','ArrowRight':'sr',
                'q':'fl','e':'fr','z':'bl','c':'br','a':'rcw','d':'rccw',' ':'s'};
  const heldKeys=new Set();
  document.addEventListener('keydown',ev=>{const cmd=keyMap[ev.key];if(!cmd||heldKeys.has(ev.key))return;ev.preventDefault();heldKeys.add(ev.key);send('/'+cmd);});
  document.addEventListener('keyup',ev=>{const cmd=keyMap[ev.key];if(!cmd)return;heldKeys.delete(ev.key);if(heldKeys.size===0)send('/s');});
</script>
</body>
</html>
)rawliteral";

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Trash Transformer V3 ===");

    // --- STBY HIGH first — drivers are frozen until these go HIGH ---
    pinMode(STBY_LEFT,  OUTPUT); digitalWrite(STBY_LEFT,  HIGH);
    pinMode(STBY_RIGHT, OUTPUT); digitalWrite(STBY_RIGHT, HIGH);
    Serial.println("STBY pins HIGH — drivers enabled");

    // --- Motor GPIO + LEDC (core 2.x) ---
    Motor motors[4] = { mFL, mFR, mBL, mBR };
    for (int i = 0; i < 4; i++) {
        pinMode(motors[i].in1, OUTPUT);
        pinMode(motors[i].in2, OUTPUT);
        ledcSetup(motors[i].channel, PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(motors[i].pwmPin, motors[i].channel);
        Serial.printf("  Motor %s  IN1=GPIO%-2d  IN2=GPIO%-2d  PWM=GPIO%-2d  CH=%d  inv=%d\n",
            motors[i].name,
            motors[i].in1, motors[i].in2, motors[i].pwmPin,
            motors[i].channel, (int)motors[i].invert);
    }
    stopAll();

    // --- WiFi AP ---
    WiFi.softAP(ssid, password);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    // --- Routes ---
    server.on("/",     HTTP_GET, [](AsyncWebServerRequest* r){ r->send_P(200, "text/html", index_html); });
    server.on("/f",    HTTP_GET, [](AsyncWebServerRequest* r){ driveForward();  r->send(200); });
    server.on("/b",    HTTP_GET, [](AsyncWebServerRequest* r){ driveBack();     r->send(200); });
    server.on("/sl",   HTTP_GET, [](AsyncWebServerRequest* r){ strafeLeft();    r->send(200); });
    server.on("/sr",   HTTP_GET, [](AsyncWebServerRequest* r){ strafeRight();   r->send(200); });
    server.on("/fl",   HTTP_GET, [](AsyncWebServerRequest* r){ diagFwdLeft();   r->send(200); });
    server.on("/fr",   HTTP_GET, [](AsyncWebServerRequest* r){ diagFwdRight();  r->send(200); });
    server.on("/bl",   HTTP_GET, [](AsyncWebServerRequest* r){ diagBckLeft();   r->send(200); });
    server.on("/br",   HTTP_GET, [](AsyncWebServerRequest* r){ diagBckRight();  r->send(200); });
    server.on("/rcw",  HTTP_GET, [](AsyncWebServerRequest* r){ rotateCW();      r->send(200); });
    server.on("/rccw", HTTP_GET, [](AsyncWebServerRequest* r){ rotateCCW();     r->send(200); });
    server.on("/s",    HTTP_GET, [](AsyncWebServerRequest* r){ stopAll();       r->send(200); });
    server.on("/speed", HTTP_GET, [](AsyncWebServerRequest* r){
        if (r->hasParam("val"))
            globalSpeed = constrain(r->getParam("val")->value().toInt(), 0, 255);
        r->send(200);
    });

    server.begin();
    Serial.println("Web server started.");
}

// =============================================================================
// LOOP — ESPAsyncWebServer is task-driven, nothing needed here
// =============================================================================
void loop() {}