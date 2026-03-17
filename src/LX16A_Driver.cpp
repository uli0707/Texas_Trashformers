#include <Arduino.h>
#include <WiFiS3.h>
#include <LX16A-bus.h>
#include <Servo.h>

// ─── Config ──────────────────────────────────────────────────────────────────
const char* ssid     = "Servo-Tester";
const char* password = "password123";

const int SERVO_MIN   = 0;
const int SERVO_MAX   = 240;
const int MOVE_SPEED  = 500;   // ms to reach target position
const int HOME_S1     = 120;
const int HOME_S2     = 120;

// Miuzei MS62 gripper — standard PWM servo on pin 9
// Tune GRIPPER_MIN (fully open) and GRIPPER_MAX (fully closed) to your mechanism
const int GRIPPER_PIN  = 9;
const int GRIPPER_MIN  = 0;    // fully open
const int GRIPPER_MAX  = 90;   // fully closed
const int GRIPPER_HOME = 0;    // start open on boot
const int GRIPPER_STEP = 20;   // degrees per keypress

// ─── Hardware ────────────────────────────────────────────────────────────────
LX16A servo1(1, Serial1);
LX16A servo2(2, Serial1);
Servo gripper;
WiFiServer server(80);

// ─── Helpers ─────────────────────────────────────────────────────────────────
int clampServo(int val) {
    return val < SERVO_MIN ? SERVO_MIN : (val > SERVO_MAX ? SERVO_MAX : val);
}

// Safely extract an integer query parameter from a URL string.
// Returns -1 if the key is not found.
int parseParam(const String& req, const char* key) {
    String k = String(key) + "=";
    int idx = req.indexOf(k);
    if (idx < 0) return -1;
    idx += k.length();
    int endAmp   = req.indexOf('&', idx);
    int endSpace = req.indexOf(' ', idx);
    int end = (endAmp >= 0 && endAmp < endSpace) ? endAmp : endSpace;
    if (end < 0) end = req.length();
    return req.substring(idx, end).toInt();
}

// ─── UI (HTML/CSS/JS) ────────────────────────────────────────────────────────
const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"/><title>ARM CTRL</title>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Oswald:wght@300;600&display=swap" rel="stylesheet"/>
  <style>
    *,*::before,*::after{box-sizing:border-box;margin:0;padding:0}
    :root{
      --bg:#0d0f14;--panel:#131720;--border:#1e2535;--accent:#00e5ff;
      --warn:#ff6b35;--text:#c8d6e5;--dim:#4a5568;--green:#39ff6e;
      --mono:'Share Tech Mono',monospace;--head:'Oswald',sans-serif;
    }
    html,body{height:100%;background:var(--bg);color:var(--text);font-family:var(--mono);overflow-x:hidden}
    body::before{content:'';position:fixed;inset:0;background:repeating-linear-gradient(0deg,transparent,transparent 2px,rgba(0,0,0,.15) 2px,rgba(0,0,0,.15) 4px);pointer-events:none;z-index:999}
    body::after{content:'';position:fixed;top:-200px;left:50%;transform:translateX(-50%);width:600px;height:400px;background:radial-gradient(ellipse,rgba(0,229,255,.06) 0%,transparent 70%);pointer-events:none;z-index:0}
    .wrapper{position:relative;z-index:1;max-width:480px;margin:0 auto;padding:24px 16px 48px}
    header{display:flex;align-items:baseline;gap:12px;margin-bottom:32px;border-bottom:1px solid var(--border);padding-bottom:16px}
    header h1{font-family:var(--head);font-weight:600;font-size:28px;letter-spacing:4px;color:var(--accent);text-shadow:0 0 20px rgba(0,229,255,.4)}
    .subtitle{font-size:11px;letter-spacing:2px;color:var(--dim);text-transform:uppercase}
    .servo-card{background:var(--panel);border:1px solid var(--border);border-radius:4px;padding:20px;margin-bottom:14px;position:relative;overflow:hidden;transition:border-color .2s}
    .servo-card::before{content:'';position:absolute;top:0;left:0;width:3px;height:100%;background:var(--accent);opacity:.5}
    .servo-card:last-of-type::before{background:var(--warn)}
    .servo-card.active{border-color:var(--accent)}
    .servo-card:last-of-type.active{border-color:var(--warn)}
    .card-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}
    .servo-label{font-family:var(--head);font-weight:300;font-size:13px;letter-spacing:3px;color:var(--dim);text-transform:uppercase}
    .key-hint{font-size:11px;color:var(--dim)}
    .key-hint kbd{background:var(--border);border:1px solid #2a3245;border-radius:3px;padding:1px 6px;font-family:var(--mono);font-size:11px;color:var(--text)}
    .angle-row{display:flex;align-items:center;gap:10px;margin-bottom:14px}
    .angle-value{font-family:var(--head);font-weight:600;font-size:48px;letter-spacing:-1px;color:var(--accent);line-height:1;min-width:110px;text-shadow:0 0 30px rgba(0,229,255,.3);transition:color .2s}
    .servo-card:last-of-type .angle-value{color:var(--warn);text-shadow:0 0 30px rgba(255,107,53,.3)}
    .bar-track{flex:1;height:6px;background:var(--border);border-radius:3px;overflow:hidden}
    .bar-fill{height:100%;background:var(--accent);border-radius:3px;transition:width .3s cubic-bezier(.25,.46,.45,.94);box-shadow:0 0 8px rgba(0,229,255,.5)}
    .servo-card:last-of-type .bar-fill{background:var(--warn);box-shadow:0 0 8px rgba(255,107,53,.5)}
    .step-btns{display:grid;grid-template-columns:1fr 1fr;gap:8px}
    .btn{background:transparent;border:1px solid var(--border);color:var(--text);font-family:var(--mono);font-size:13px;padding:10px;border-radius:3px;cursor:pointer;transition:background .15s,border-color .15s,color .15s;letter-spacing:1px}
    .btn:hover{background:rgba(255,255,255,.05);border-color:var(--dim)}
    .btn:active{background:rgba(255,255,255,.1)}
    .speed-row{background:var(--panel);border:1px solid var(--border);border-radius:4px;padding:16px 20px;margin-bottom:14px;display:flex;align-items:center;gap:14px}
    .speed-label{font-size:11px;letter-spacing:2px;color:var(--dim);text-transform:uppercase;white-space:nowrap}
    input[type=range]{flex:1;accent-color:var(--accent);height:4px}
    #speedVal{font-size:13px;color:var(--accent);min-width:40px;text-align:right}
    .actions{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:14px}
    .btn-action{background:transparent;border:1px solid var(--border);color:var(--text);font-family:var(--head);font-weight:300;font-size:13px;letter-spacing:3px;padding:14px 10px;border-radius:3px;cursor:pointer;text-transform:uppercase;transition:all .15s}
    .btn-action.home:hover{border-color:var(--green);color:var(--green);box-shadow:inset 0 0 20px rgba(57,255,110,.05)}
    .btn-action.target:hover{border-color:var(--warn);color:var(--warn);box-shadow:inset 0 0 20px rgba(255,107,53,.05)}
    .btn-action.dump-front:hover{border-color:var(--warn);color:var(--warn);box-shadow:inset 0 0 20px rgba(255,107,53,.05)}
    .btn-action.dump-back:hover{border-color:var(--accent);color:var(--accent);box-shadow:inset 0 0 20px rgba(0,229,255,.05)}
    .goto-panel{background:var(--panel);border:1px solid var(--border);border-radius:4px;padding:18px 20px;margin-bottom:14px}
    .goto-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}
    .goto-inputs{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-bottom:10px}
    .goto-field{display:flex;flex-direction:column;gap:4px}
    .goto-field label{font-size:10px;letter-spacing:2px;color:var(--dim);text-transform:uppercase}
    .goto-field input{background:var(--bg);border:1px solid var(--border);color:var(--accent);font-family:var(--mono);font-size:16px;padding:8px 10px;border-radius:3px;width:100%;text-align:center;outline:none;transition:border-color .15s}
    .goto-field input:focus{border-color:var(--accent)}
    .goto-field input::placeholder{color:var(--dim);font-size:11px}
    .goto-field input::-webkit-outer-spin-button,.goto-field input::-webkit-inner-spin-button{-webkit-appearance:none}
    .goto-field input[type=number]{-moz-appearance:textfield}
    .goto-actions{display:grid;grid-template-columns:1fr auto;gap:8px;margin-bottom:4px}
    .btn-goto{background:transparent;font-family:var(--head);font-weight:300;font-size:12px;letter-spacing:3px;padding:10px 14px;border-radius:3px;cursor:pointer;text-transform:uppercase;transition:all .15s}
    .btn-goto.exec{border:1px solid var(--accent);color:var(--accent)}
    .btn-goto.exec:hover{background:rgba(0,229,255,.08)}
    .btn-goto.save{border:1px solid var(--border);color:var(--dim);white-space:nowrap}
    .btn-goto.save:hover{border-color:var(--green);color:var(--green)}
    #savedList{margin-top:10px;display:flex;flex-direction:column;gap:6px}
    .saved-item{display:flex;align-items:center;gap:8px;background:var(--bg);border:1px solid var(--border);border-radius:3px;padding:7px 10px;cursor:pointer;transition:border-color .15s}
    .saved-item:hover{border-color:var(--dim)}
    .saved-item:hover .saved-go{opacity:1}
    .saved-name{flex:1;font-size:12px;letter-spacing:1px;color:var(--text)}
    .saved-name input{background:transparent;border:none;color:var(--text);font-family:var(--mono);font-size:12px;letter-spacing:1px;width:100%;outline:none}
    .saved-coords{font-size:11px;color:var(--dim);white-space:nowrap}
    .saved-go{font-size:11px;color:var(--accent);opacity:0;transition:opacity .15s;padding:0 4px}
    .saved-del{font-size:14px;color:var(--dim);cursor:pointer;padding:0 2px;line-height:1;transition:color .15s}
    .saved-del:hover{color:var(--warn)}
    .status-bar{display:flex;justify-content:space-between;font-size:11px;color:var(--dim);letter-spacing:1px;padding-top:12px;border-top:1px solid var(--border)}
    .status-dot{display:inline-block;width:6px;height:6px;border-radius:50%;background:var(--green);margin-right:6px;animation:pulse 2s infinite;vertical-align:middle}
    @keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
    #toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%);background:var(--panel);border:1px solid var(--border);color:var(--dim);font-size:12px;letter-spacing:1px;padding:8px 16px;border-radius:3px;opacity:0;transition:opacity .3s;pointer-events:none;white-space:nowrap}
    #toast.show{opacity:1}
  </style>
</head>
<body>
<div class="wrapper">
  <header>
    <h1>ARM CTRL</h1>
    <span class="subtitle">2-Axis · LX16A Bus</span>
  </header>

  <div class="servo-card" id="cardG">
    <div class="card-header">
      <span class="servo-label">Gripper · MS62</span>
      <span class="key-hint"><kbd>▲</kbd> <kbd>▼</kbd></span>
    </div>
    <div class="angle-row">
      <div class="angle-value" id="dispG">0°</div>
      <div class="bar-track">
        <div class="bar-fill" id="barG" style="width:0%;background:var(--green);box-shadow:0 0 8px rgba(57,255,110,.5)"></div>
      </div>
    </div>
    <div class="step-btns">
      <button class="btn" onclick="stepGripper(-5)">− 5°</button>
      <button class="btn" onclick="stepGripper(+5)">+ 5°</button>
    </div>
  </div>

  <div id="servos"></div>

  <div class="speed-row">
    <span class="speed-label">Speed</span>
    <input type="range" id="speedSlider" min="100" max="2000" value="500" step="100"
           oninput="speed=+this.value;document.getElementById('speedVal').textContent=speed+'ms'"/>
    <span id="speedVal">500ms</span>
  </div>

  <div class="actions" style="grid-template-columns:1fr 1fr 1fr">
    <button class="btn-action home" onclick="goHome()">⌂ Home</button>
    <button class="btn-action dump-front" onclick="dumpSequence(0,170)">⬇ Dump Front</button>
    <button class="btn-action dump-back" onclick="dumpSequence(60,120)">⬆ Dump Back</button>
  </div>

  <div class="goto-panel">
    <div class="goto-header">
      <span class="servo-label">Go-To Position</span>
      <span class="key-hint" style="font-size:10px;color:var(--dim)">Enter angles · Enter to execute</span>
    </div>
    <div class="goto-inputs" id="gotoInputs"></div>
    <div class="goto-actions">
      <button class="btn-goto exec" onclick="executeGoto()">▶ EXECUTE</button>
      <button class="btn-goto save" onclick="savePosition()">+ SAVE</button>
    </div>
    <div id="savedList"></div>
  </div>

  <div class="status-bar">
    <span><span class="status-dot"></span>CONNECTED</span>
    <span>ARM 0–240° · GRIPPER 0–90° · STEP ±10°/20°</span>
  </div>
</div>

<div id="toast"></div>

<script>
  const MIN=0,MAX=240,GMIN=0,GMAX=90,GSTEP=20,H1=120,H2=120;
  let s1=H1,s2=H2,sg=GMIN,speed=500,sendTimer=null,savedPositions=[];

  // Generate servo cards
  [['1','◀','▶'],['2','A','D']].forEach(([n,k1,k2])=>{
    document.getElementById('servos').insertAdjacentHTML('beforeend',`
      <div class="servo-card" id="card${n}">
        <div class="card-header">
          <span class="servo-label">Servo 0${n}</span>
          <span class="key-hint"><kbd>${k1}</kbd> <kbd>${k2}</kbd></span>
        </div>
        <div class="angle-row">
          <div class="angle-value" id="disp${n}">120°</div>
          <div class="bar-track"><div class="bar-fill" id="bar${n}" style="width:50%"></div></div>
        </div>
        <div class="step-btns">
          <button class="btn" onclick="step(${n},-10)">− 10°</button>
          <button class="btn" onclick="step(${n},+10)">+ 10°</button>
        </div>
      </div>`);
  });

  // Generate goto inputs
  [['S1','gotoS1',0,240],['S2','gotoS2',0,240],['GRP','gotoSG',0,90]].forEach(([l,id,mn,mx])=>{
    document.getElementById('gotoInputs').insertAdjacentHTML('beforeend',
      `<div class="goto-field"><label>${l}</label><input type="number" id="${id}" min="${mn}" max="${mx}" placeholder="${mn}–${mx}"/></div>`);
  });

  document.addEventListener('keydown',e=>{
    if(e.target.tagName==='INPUT'){if(e.key==='Enter')executeGoto();return;}
    const k=e.key.toLowerCase();
    if(k==='arrowleft'){e.preventDefault();step(1,-10);}
    if(k==='arrowright'){e.preventDefault();step(1,+10);}
    if(k==='arrowup'){e.preventDefault();stepGripper(-GSTEP);}
    if(k==='arrowdown'){e.preventDefault();stepGripper(+GSTEP);}
    if(k==='a')step(2,-10);
    if(k==='d')step(2,+10);
    if(k==='f')dumpSequence(0,170);
    if(k==='b')dumpSequence(60,120);
    if(e.code==='Space'){e.preventDefault();goHome();}
  });

  function clamp(v){return Math.max(MIN,Math.min(MAX,v));}
  function clampG(v){return Math.max(GMIN,Math.min(GMAX,v));}

  function step(servo,delta){
    if(servo===1)s1=clamp(s1+delta);else s2=clamp(s2+delta);
    updateUI();scheduleSend();
  }

  function goHome(){s1=H1;s2=H2;updateUI();scheduleSend();}

  function dumpSequence(targetS1,targetS2){
    const SAFE1=180,SAFE2=120;
    // Phase 1: move to safe waypoint
    s1=SAFE1;s2=SAFE2;updateUI();sendCommand();
    showToast('MOVING TO SAFE POINT');
    setTimeout(()=>{
      // Phase 2: move to dump target
      s1=targetS1;s2=targetS2;updateUI();sendCommand();
      showToast('MOVING TO DUMP');
      setTimeout(()=>{
        // Phase 3: open gripper to release
        sg=0;updateUI();
        fetch('/grip?angle=0').catch(()=>showToast('SEND FAILED'));
        showToast('RELEASING');
        setTimeout(()=>{
          // Return to safe waypoint
          s1=SAFE1;s2=SAFE2;updateUI();sendCommand();
        },500);
      },speed);
    },speed);
  }

  function goTo(p1,p2,pg){
    s1=clamp(p1);s2=clamp(p2);sg=clampG(pg);
    updateUI();scheduleSend();
    fetch(`/grip?angle=${sg}`).catch(()=>showToast('SEND FAILED'));
  }

  function executeGoto(){
    const v1=parseInt(document.getElementById('gotoS1').value);
    const v2=parseInt(document.getElementById('gotoS2').value);
    const vg=parseInt(document.getElementById('gotoSG').value);
    const p1=isNaN(v1)?s1:v1,p2=isNaN(v2)?s2:v2,pg=isNaN(vg)?sg:vg;
    goTo(p1,p2,pg);
    showToast(`GOTO  S1:${clamp(p1)}°  S2:${clamp(p2)}°  G:${clampG(pg)}°`);
  }

  function savePosition(){
    const v1=parseInt(document.getElementById('gotoS1').value);
    const v2=parseInt(document.getElementById('gotoS2').value);
    const vg=parseInt(document.getElementById('gotoSG').value);
    const p1=isNaN(v1)?s1:clamp(v1),p2=isNaN(v2)?s2:clamp(v2),pg=isNaN(vg)?sg:clampG(vg);
    savedPositions.push({name:`Position ${savedPositions.length+1}`,s1:p1,s2:p2,sg:pg});
    renderSaved();showToast('POSITION SAVED');
  }

  function renderSaved(){
    document.getElementById('savedList').innerHTML=savedPositions.map((pos,i)=>`
      <div class="saved-item" onclick="goTo(${pos.s1},${pos.s2},${pos.sg});showToast('▶ '+savedPositions[${i}].name)">
        <div class="saved-name"><input value="${pos.name}" onchange="savedPositions[${i}].name=this.value" onclick="event.stopPropagation()"/></div>
        <span class="saved-coords">S1:${pos.s1}° S2:${pos.s2}° G:${pos.sg}°</span>
        <span class="saved-go">▶</span>
        <span class="saved-del" onclick="event.stopPropagation();deletePos(${i})">×</span>
      </div>`).join('');
  }

  function deletePos(i){savedPositions.splice(i,1);renderSaved();}

  function stepGripper(delta){
    sg=clampG(sg+delta);
    document.getElementById('dispG').textContent=sg+'°';
    document.getElementById('barG').style.width=((sg/GMAX)*100).toFixed(1)+'%';
    fetch(`/grip?angle=${sg}`).catch(()=>showToast('SEND FAILED'));
  }

  function updateUI(){
    document.getElementById('disp1').textContent=s1+'°';
    document.getElementById('disp2').textContent=s2+'°';
    document.getElementById('dispG').textContent=sg+'°';
    document.getElementById('bar1').style.width=((s1/MAX)*100).toFixed(1)+'%';
    document.getElementById('bar2').style.width=((s2/MAX)*100).toFixed(1)+'%';
    document.getElementById('barG').style.width=((sg/GMAX)*100).toFixed(1)+'%';
  }

  function scheduleSend(){clearTimeout(sendTimer);sendTimer=setTimeout(sendCommand,150);}
  function sendCommand(){fetch(`/move?s1=${s1}&s2=${s2}&spd=${speed}`).catch(()=>showToast('SEND FAILED'));}

  function showToast(msg){
    const t=document.getElementById('toast');
    t.textContent=msg;t.classList.add('show');
    setTimeout(()=>t.classList.remove('show'),2500);
  }
</script>
</body>
</html>
)rawliteral";

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial1.begin(115200);

    servo1.initialize(); servo1.enableTorque();
    servo2.initialize(); servo2.enableTorque();

    // Move to Home on boot with smooth ramp
    servo1.move(HOME_S1, MOVE_SPEED);
    servo2.move(HOME_S2, MOVE_SPEED);

    // Gripper starts open
    gripper.attach(GRIPPER_PIN);
    gripper.write(GRIPPER_HOME);

    WiFi.beginAP(ssid, password);
    server.begin();

    Serial.println("AP started. Server ready.");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
    WiFiClient client = server.available();
    if (client) {
        String req = client.readStringUntil('\r');

        if (req.indexOf("GET /move?") >= 0) {
            int v1  = parseParam(req, "s1");
            int v2  = parseParam(req, "s2");
            int spd = parseParam(req, "spd");

            if (v1 >= 0) v1 = clampServo(v1);
            if (v2 >= 0) v2 = clampServo(v2);
            spd = (spd > 0 && spd <= 5000) ? spd : MOVE_SPEED;

            if (v1 >= 0 && v2 >= 0) {
                servo1.move(v1, spd);
                servo2.move(v2, spd);
                Serial.print("S1: "); Serial.print(v1);
                Serial.print(" | S2: "); Serial.print(v2);
                Serial.print(" | SPD: "); Serial.println(spd);
            }
        } else if (req.indexOf("GET /grip?") >= 0) {
            int angle = parseParam(req, "angle");
            if (angle >= 0) {
                angle = angle < GRIPPER_MIN ? GRIPPER_MIN : (angle > GRIPPER_MAX ? GRIPPER_MAX : angle);
                gripper.write(angle);
                Serial.print("Gripper: "); Serial.println(angle);
            }
        }

        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
        client.print(index_html);
        client.stop();
    }
}
