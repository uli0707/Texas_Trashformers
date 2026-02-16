#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// =============================================================================
// TRASH TRANSFORMER — MOTOR TEST FIRMWARE
// Tests each motor individually: forward and reverse, one at a time.
// Use this to verify wiring and direction before running the main firmware.
//
// ┌───────────────────────────┬─────────────────────────────────────────────┐
// │  LEFT DRIVER              │  RIGHT DRIVER                               │
// │  STBY → GPIO 15           │  STBY → GPIO 2                              │
// ├─────────────┬─────────────┼─────────────┬───────────────────────────────┤
// │ TB6612 pin  │ ESP32 GPIO  │ TB6612 pin  │ ESP32 GPIO                    │
// ├─────────────┼─────────────┼─────────────┼───────────────────────────────┤
// │ PWMA        │ 18          │ PWMA        │ 22                            │
// │ AIN1        │ 16          │ AIN1        │ 19                            │
// │ AIN2        │ 17          │ AIN2        │ 21                            │
// │ AO1+AO2     │ FL motor    │ AO1+AO2     │ FR motor                     │
// ├─────────────┼─────────────┼─────────────┼───────────────────────────────┤
// │ PWMB        │ 13          │ PWMB        │ 26                            │
// │ BIN1        │ 4           │ BIN1        │ 14                            │
// │ BIN2        │ 5           │ BIN2        │ 27                            │
// │ BO1+BO2     │ BL motor    │ BO1+BO2     │ BR motor                     │
// └─────────────┴─────────────┴─────────────┴───────────────────────────────┘
// =============================================================================

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
const char* ssid     = "Trash-Transformer-V1";
const char* password = "wastecollection";

AsyncWebServer server(80);

// ---------------------------------------------------------------------------
// PWM — core 2.x API
// ---------------------------------------------------------------------------
const int PWM_FREQ       = 5000;
const int PWM_RESOLUTION = 8;
const int TEST_SPEED     = 180;   // 0–255, used for all test movements

// ---------------------------------------------------------------------------
// STBY pins
// ---------------------------------------------------------------------------
const int STBY_LEFT  = 15;
const int STBY_RIGHT =  2;

// ---------------------------------------------------------------------------
// Motor descriptor
// ---------------------------------------------------------------------------
struct Motor {
    const char* name;
    int  in1;
    int  in2;
    int  pwmPin;
    int  channel;
};

Motor mFL = { "FL",  16,  17,  18,  0 };  // Left  driver ch A
Motor mFR = { "FR",  19,  21,  22,  1 };  // Right driver ch A
Motor mBL = { "BL",   4,   5,  13,  2 };  // Left  driver ch B
Motor mBR = { "BR",  14,  27,  26,  3 };  // Right driver ch B

Motor* allMotors[4] = { &mFL, &mFR, &mBL, &mBR };

// ---------------------------------------------------------------------------
// Motor control  —  dir: +1 forward, -1 reverse, 0 stop
// ---------------------------------------------------------------------------
void setMotor(const Motor& m, int dir) {
    if (dir > 0) {
        digitalWrite(m.in1, HIGH);
        digitalWrite(m.in2, LOW);
        ledcWrite(m.channel, TEST_SPEED);
    } else if (dir < 0) {
        digitalWrite(m.in1, LOW);
        digitalWrite(m.in2, HIGH);
        ledcWrite(m.channel, TEST_SPEED);
    } else {
        digitalWrite(m.in1, LOW);
        digitalWrite(m.in2, LOW);
        ledcWrite(m.channel, 0);
    }
}

void stopAll() {
    for (int i = 0; i < 4; i++) setMotor(*allMotors[i], 0);
}

// ---------------------------------------------------------------------------
// Web UI — motor test panel
// ---------------------------------------------------------------------------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Motor Test</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@700;900&display=swap');

  :root {
    --acid:   #b5ff2b;
    --bg:     #0a0d04;
    --panel:  #111406;
    --border: #2a3008;
    --danger: #ff3c28;
    --warn:   #ffaa00;
    --muted:  #4a5520;
    --text:   #c8d88a;
    --fwd:    #00e5ff;
    --bck:    #ff6b35;
  }

  *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'Share Tech Mono', monospace;
    min-height: 100dvh;
    display: flex;
    flex-direction: column;
    align-items: center;
    padding: 16px 12px 32px;
  }

  /* scanlines */
  body::before {
    content: '';
    pointer-events: none;
    position: fixed;
    inset: 0;
    background: repeating-linear-gradient(
      180deg, transparent 0px, transparent 3px,
      rgba(0,0,0,.07) 3px, rgba(0,0,0,.07) 4px
    );
    z-index: 9999;
  }

  header {
    width: 100%;
    max-width: 480px;
    border: 1px solid var(--border);
    border-top: 3px solid var(--warn);
    background: var(--panel);
    padding: 12px 16px;
    margin-bottom: 6px;
    display: flex;
    justify-content: space-between;
    align-items: center;
  }

  h1 {
    font-family: 'Orbitron', sans-serif;
    font-size: clamp(12px, 4.5vw, 18px);
    color: var(--warn);
    letter-spacing: .1em;
  }

  .subtitle {
    width: 100%;
    max-width: 480px;
    text-align: center;
    font-size: 11px;
    color: var(--muted);
    margin-bottom: 20px;
    letter-spacing: .05em;
  }

  /* Motor card grid — 2×2 */
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    width: 100%;
    max-width: 480px;
    margin-bottom: 14px;
  }

  .card {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 14px 10px 12px;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 10px;
  }

  .card-label {
    font-family: 'Orbitron', sans-serif;
    font-size: clamp(13px, 4vw, 18px);
    font-weight: 700;
    letter-spacing: .1em;
    color: var(--acid);
  }

  .card-sub {
    font-size: 10px;
    color: var(--muted);
    letter-spacing: .06em;
    margin-top: -6px;
  }

  .btn-row {
    display: flex;
    gap: 8px;
    width: 100%;
  }

  .btn {
    flex: 1;
    border: 1px solid var(--border);
    background: #0d1205;
    border-radius: 4px;
    padding: 14px 4px;
    font-family: 'Orbitron', sans-serif;
    font-size: clamp(8px, 2.5vw, 11px);
    font-weight: 700;
    letter-spacing: .05em;
    cursor: pointer;
    user-select: none;
    touch-action: manipulation;
    -webkit-tap-highlight-color: transparent;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 5px;
    transition: background .06s, border-color .06s, color .06s;
    color: var(--text);
  }

  .btn .arrow { font-size: clamp(18px, 5.5vw, 24px); line-height: 1; }

  .btn.fwd { border-color: #004455; color: var(--fwd); }
  .btn.fwd:active, .btn.fwd.active {
    background: var(--fwd);
    border-color: var(--fwd);
    color: #000;
  }

  .btn.bck { border-color: #4a1a00; color: var(--bck); }
  .btn.bck:active, .btn.bck.active {
    background: var(--bck);
    border-color: var(--bck);
    color: #000;
  }

  /* Stop all */
  .stop-btn {
    width: 100%;
    max-width: 480px;
    padding: 16px;
    background: #1a0600;
    border: 1px solid #6a1a00;
    border-radius: 6px;
    color: var(--danger);
    font-family: 'Orbitron', sans-serif;
    font-size: clamp(12px, 4vw, 16px);
    font-weight: 700;
    letter-spacing: .1em;
    cursor: pointer;
    touch-action: manipulation;
    -webkit-tap-highlight-color: transparent;
    margin-bottom: 14px;
    transition: background .06s;
  }
  .stop-btn:active { background: var(--danger); color: #fff; border-color: var(--danger); }

  /* Status bar */
  footer {
    width: 100%;
    max-width: 480px;
    border: 1px solid var(--border);
    background: var(--panel);
    padding: 8px 14px;
    font-size: 11px;
    color: var(--muted);
    display: flex;
    justify-content: space-between;
  }
  .ok  { color: var(--acid); }
  .err { color: var(--danger); }
</style>
</head>
<body>

<header>
  <h1>⚡ MOTOR TEST</h1>
  <span style="font-size:11px;color:var(--muted)">192.168.4.1</span>
</header>
<p class="subtitle">HOLD a button to run that motor — RELEASE to stop</p>

<div class="grid">

  <!-- FL -->
  <div class="card">
    <div class="card-label">FL</div>
    <div class="card-sub">FRONT LEFT</div>
    <div class="btn-row">
      <button class="btn fwd" data-motor="fl" data-dir="fwd">
        <span class="arrow">↑</span>FWD
      </button>
      <button class="btn bck" data-motor="fl" data-dir="bck">
        <span class="arrow">↓</span>BCK
      </button>
    </div>
  </div>

  <!-- FR -->
  <div class="card">
    <div class="card-label">FR</div>
    <div class="card-sub">FRONT RIGHT</div>
    <div class="btn-row">
      <button class="btn fwd" data-motor="fr" data-dir="fwd">
        <span class="arrow">↑</span>FWD
      </button>
      <button class="btn bck" data-motor="fr" data-dir="bck">
        <span class="arrow">↓</span>BCK
      </button>
    </div>
  </div>

  <!-- BL -->
  <div class="card">
    <div class="card-label">BL</div>
    <div class="card-sub">BACK LEFT</div>
    <div class="btn-row">
      <button class="btn fwd" data-motor="bl" data-dir="fwd">
        <span class="arrow">↑</span>FWD
      </button>
      <button class="btn bck" data-motor="bl" data-dir="bck">
        <span class="arrow">↓</span>BCK
      </button>
    </div>
  </div>

  <!-- BR -->
  <div class="card">
    <div class="card-label">BR</div>
    <div class="card-sub">BACK RIGHT</div>
    <div class="btn-row">
      <button class="btn fwd" data-motor="br" data-dir="fwd">
        <span class="arrow">↑</span>FWD
      </button>
      <button class="btn bck" data-motor="br" data-dir="bck">
        <span class="arrow">↓</span>BCK
      </button>
    </div>
  </div>

</div>

<button class="stop-btn" id="stopAll">⏹ STOP ALL</button>

<footer>
  <span id="statusMotor">—</span>
  <span id="statusResult" class="ok">READY</span>
</footer>

<script>
  let activeBtn = null;

  function send(path, label) {
    fetch(path)
      .then(() => {
        document.getElementById('statusResult').className = 'ok';
        document.getElementById('statusResult').textContent = 'OK';
        document.getElementById('statusMotor').textContent = label;
      })
      .catch(() => {
        document.getElementById('statusResult').className = 'err';
        document.getElementById('statusResult').textContent = 'ERR — check connection';
      });
  }

  function activate(btn) {
    if (activeBtn && activeBtn !== btn) {
      activeBtn.classList.remove('active');
    }
    activeBtn = btn;
    btn.classList.add('active');
    const motor = btn.dataset.motor.toUpperCase();
    const dir   = btn.dataset.dir;
    send('/test?motor=' + btn.dataset.motor + '&dir=' + dir,
         motor + ' ' + (dir === 'fwd' ? '↑ FWD' : '↓ BCK'));
  }

  function release() {
    if (activeBtn) {
      activeBtn.classList.remove('active');
      activeBtn = null;
    }
    send('/stop', 'STOPPED');
  }

  // Attach hold-to-run / release-to-stop to every motor button
  document.querySelectorAll('.btn[data-motor]').forEach(btn => {
    btn.addEventListener('mousedown',  e => { e.preventDefault(); activate(btn); });
    btn.addEventListener('mouseup',    ()  => release());
    btn.addEventListener('mouseleave', ()  => { if (activeBtn === btn) release(); });
    btn.addEventListener('touchstart', e => { e.preventDefault(); activate(btn); }, { passive: false });
    btn.addEventListener('touchend',   e => { e.preventDefault(); release(); },     { passive: false });
    btn.addEventListener('touchcancel',()  => release());
  });

  // Stop All button
  const stopBtn = document.getElementById('stopAll');
  stopBtn.addEventListener('mousedown',  e => { e.preventDefault(); release(); });
  stopBtn.addEventListener('touchstart', e => { e.preventDefault(); release(); }, { passive: false });
</script>
</body>
</html>
)rawliteral";

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Trash Transformer — Motor Test ===");

    // STBY HIGH — both drivers enabled
    pinMode(STBY_LEFT,  OUTPUT); digitalWrite(STBY_LEFT,  HIGH);
    pinMode(STBY_RIGHT, OUTPUT); digitalWrite(STBY_RIGHT, HIGH);
    Serial.println("STBY pins HIGH — drivers enabled");

    // Motor GPIO + LEDC (core 2.x)
    for (int i = 0; i < 4; i++) {
        Motor& m = *allMotors[i];
        pinMode(m.in1, OUTPUT);
        pinMode(m.in2, OUTPUT);
        ledcSetup(m.channel, PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(m.pwmPin, m.channel);
        Serial.printf("  Motor %s  IN1=GPIO%-2d  IN2=GPIO%-2d  PWM=GPIO%-2d  CH=%d\n",
            m.name, m.in1, m.in2, m.pwmPin, m.channel);
    }
    stopAll();

    // WiFi AP
    WiFi.softAP(ssid, password);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    // --- Routes ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* r){
        r->send_P(200, "text/html", index_html);
    });

    // /test?motor=fl&dir=fwd  (or bck)
    // motor: fl, fr, bl, br
    // dir:   fwd, bck
    server.on("/test", HTTP_GET, [](AsyncWebServerRequest* r) {
        if (!r->hasParam("motor") || !r->hasParam("dir")) {
            r->send(400); return;
        }

        String motor = r->getParam("motor")->value();
        String dir   = r->getParam("dir")->value();
        int dirVal   = (dir == "fwd") ? 1 : -1;

        stopAll();  // always stop everything else first

        Motor* target = nullptr;
        if      (motor == "fl") target = &mFL;
        else if (motor == "fr") target = &mFR;
        else if (motor == "bl") target = &mBL;
        else if (motor == "br") target = &mBR;

        if (target) {
            setMotor(*target, dirVal);
            Serial.printf("TEST  %s  %s\n", target->name, dirVal > 0 ? "FWD" : "BCK");
        }

        r->send(200);
    });

    server.on("/stop", HTTP_GET, [](AsyncWebServerRequest* r){
        stopAll();
        Serial.println("STOP ALL");
        r->send(200);
    });

    server.begin();
    Serial.println("Server ready — connect to " + String(ssid));
    Serial.println("Open http://192.168.4.1 on your phone or laptop");
}

// =============================================================================
// LOOP
// =============================================================================
void loop() {}