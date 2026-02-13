#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// --- Network Setup ---
const char* ssid = "Trash-Transformer-V1";
const char* password = "wastecollection";

AsyncWebServer server(80);

// --- PWM & Speed Settings ---
int globalSpeed = 150; // Initial speed (0-255)
const int freq = 5000;
const int resolution = 8;

// --- Motor Structure ---
struct Motor {
    int in1;
    int in2;
    int pwm;
    int channel;
};

// --- Pin Definitions (Adjust based on your final wiring) ---
// Using motorFL, motorFR, etc. to avoid conflict with ESP32 internal "BR" register
Motor mFL = {13, 12, 14, 0}; // Front Left
Motor mFR = {27, 26, 25, 1}; // Front Right
Motor mBL = {33, 32, 23, 2}; // Back Left
Motor mBR = {19, 18, 5,  3}; // Back Right

// --- Motor Control Helper ---
void setMotor(Motor m, int d1, int d2) {
    digitalWrite(m.in1, d1);
    digitalWrite(m.in2, d2);
    ledcWrite(m.channel, globalSpeed);
}

void stopAll() {
    setMotor(mFL, 0, 0); setMotor(mFR, 0, 0);
    setMotor(mBL, 0, 0); setMotor(mBR, 0, 0);
}

// Master Movement function (FL, FR, BL, BR)
void move(int fl1, int fl2, int fr1, int fr2, int bl1, int bl2, int br1, int br2) {
    setMotor(mFL, fl1, fl2); setMotor(mFR, fr1, fr2);
    setMotor(mBL, bl1, bl2); setMotor(mBR, br1, br2);
}

// --- Web Interface (Responsive UI) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html>
<head>
    <title>Mecanum Control</title>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <style>
        body { font-family: sans-serif; text-align: center; background: #121212; color: #00ff00; margin: 0; padding: 10px; }
        .grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; max-width: 400px; margin: 20px auto; }
        button { background: #333; border: 2px solid #00ff00; color: white; padding: 25px 0; border-radius: 12px; font-weight: bold; font-size: 16px; cursor: pointer; user-select: none; touch-action: manipulation; }
        button:active { background: #00ff00; color: black; }
        .slider-container { margin: 30px auto; max-width: 400px; }
        .slider { width: 100%; height: 25px; accent-color: #00ff00; }
        .status { color: #888; font-size: 0.8em; }
    </style>
</head>
<body>
    <h1>TRASH TRANSFORMER</h1>
    
    <div class="slider-container">
        <p>SPEED: <span id="speedVal">150</span></p>
        <input type="range" min="0" max="255" value="150" class="slider" 
               oninput="document.getElementById('speedVal').innerText = this.value"
               onchange="fetch('/speed?val=' + this.value)">
    </div>
    
    <div class="grid">
        <button onmousedown="cmd('fl')" onmouseup="cmd('s')" ontouchstart="cmd('fl')" ontouchend="cmd('s')">◤</button>
        <button onmousedown="cmd('f')"  onmouseup="cmd('s')" ontouchstart="cmd('f')"  ontouchend="cmd('s')">FORWARD</button>
        <button onmousedown="cmd('fr')" onmouseup="cmd('s')" ontouchstart="cmd('fr')" ontouchend="cmd('s')">◥</button>
        
        <button onmousedown="cmd('cl')" onmouseup="cmd('s')" ontouchstart="cmd('cl')" ontouchend="cmd('s')">CRAWL L</button>
        <button onmousedown="cmd('s')"  style="background:#800; border-color:red">STOP</button>
        <button onmousedown="cmd('cr')" onmouseup="cmd('s')" ontouchstart="cmd('cr')" ontouchend="cmd('s')">CRAWL R</button>
        
        <button onmousedown="cmd('bl')" onmouseup="cmd('s')" ontouchstart="cmd('bl')" ontouchend="cmd('s')">◣</button>
        <button onmousedown="cmd('b')"  onmouseup="cmd('s')" ontouchstart="cmd('b')"  ontouchend="cmd('s')">BACK</button>
        <button onmousedown="cmd('br')" onmouseup="cmd('s')" ontouchstart="cmd('br')" ontouchend="cmd('s')">◢</button>
    </div>
    <p class="status">Connected to: 192.168.4.1</p>
    <script>function cmd(p) { fetch('/' + p); }</script>
</body>
</html>)rawliteral";

void setup() {
    Serial.begin(115200);

    // Initialize Motor Pins and PWM
    Motor motors[4] = {mFL, mFR, mBL, mBR};
    for(int i=0; i<4; i++) {
        pinMode(motors[i].in1, OUTPUT);
        pinMode(motors[i].in2, OUTPUT);
        ledcSetup(motors[i].channel, freq, resolution);
        ledcAttachPin(motors[i].pwm, motors[i].channel);
    }
    stopAll();

    // WiFi Access Point
    WiFi.softAP(ssid, password);
    Serial.println("\nTrash Transformer AP Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    // --- Web Server Routes ---
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", index_html);
    });

    // Basic & Mecanum Crawl Routes
    server.on("/f",  HTTP_GET, [](AsyncWebServerRequest *r){ move(1,0, 1,0, 1,0, 1,0); r->send(200); });
    server.on("/b",  HTTP_GET, [](AsyncWebServerRequest *r){ move(0,1, 0,1, 0,1, 0,1); r->send(200); });
    server.on("/cl", HTTP_GET, [](AsyncWebServerRequest *r){ move(0,1, 1,0, 1,0, 0,1); r->send(200); });
    server.on("/cr", HTTP_GET, [](AsyncWebServerRequest *r){ move(1,0, 0,1, 0,1, 1,0); r->send(200); });
    
    // Diagonals (Crawl Front-Left, etc.)
    server.on("/fl", HTTP_GET, [](AsyncWebServerRequest *r){ move(0,0, 1,0, 1,0, 0,0); r->send(200); }); 
    server.on("/fr", HTTP_GET, [](AsyncWebServerRequest *r){ move(1,0, 0,0, 0,0, 1,0); r->send(200); });
    server.on("/bl", HTTP_GET, [](AsyncWebServerRequest *r){ move(0,1, 0,0, 0,0, 0,1); r->send(200); });
    server.on("/br", HTTP_GET, [](AsyncWebServerRequest *r){ move(0,0, 0,1, 0,1, 0,0); r->send(200); });

    server.on("/s",  HTTP_GET, [](AsyncWebServerRequest *r){ stopAll(); r->send(200); });

    // Speed Control Route
    server.on("/speed", HTTP_GET, [](AsyncWebServerRequest *r){
        if(r->hasParam("val")) {
            globalSpeed = r->getParam("val")->value().toInt();
            Serial.printf("Speed set to: %d\n", globalSpeed);
        }
        r->send(200);
    });

    server.begin();
}

void loop() {
    // Handled by Async Server
}