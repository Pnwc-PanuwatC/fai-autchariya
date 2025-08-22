#include <WiFi.h>
#include <WebServer.h>
#include <TM1637Display.h>
  #include <ESPmDNS.h>


// ---------- Wi-Fi AP ----------
const char* AP_SSID = "FaiAutchariya";
const char* AP_PASS = "12345678";   // ต้อง >= 8 ตัว

// ---------- Pins ----------
#define LED_PIN      25   // LED หรือ MOSFET gate
#define PIR_PIN      14   // PIR OUT
#define TM_CLK_PIN   23   // TM1637 CLK
#define TM_DIO_PIN   22   // TM1637 DIO
#define BUTTON_PIN   27   // ปุ่ม toggle "สวิตช์หลัก" (ต่อกับ GND)

// ---------- Globals ----------
WebServer server(80);
TM1637Display display(TM_CLK_PIN, TM_DIO_PIN);

unsigned long countdownSeconds = 10;
unsigned long countdownStartMillis = 0;
bool countdownRunning = false;
bool lightOn = false;
bool systemEnabled = true;  // true=เปิดระบบ, false=ปิดระบบ

// ปุ่ม (debounce)
bool lastBtn = HIGH;
unsigned long lastDebounce = 0;
const unsigned long DEBOUNCE_MS = 40;

// ---------- Helpers ----------
void applySystem(bool enable) {
  systemEnabled = enable;
  if (!systemEnabled) {
    // ปิดทุกอย่างเหมือนเบรกเกอร์
    lightOn = false;
    countdownRunning = false;
    digitalWrite(LED_PIN, LOW);
    pinMode(LED_PIN, INPUT);           // High-Z ลด leakage ถ้าขับ MOSFET/รีเลย์
    display.showNumberDec(countdownSeconds, true);
  } else {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
  }
}

void setLight(bool on) {
  if (!systemEnabled) on = false;      // ถ้าปิดระบบ บังคับปิดเสมอ
  lightOn = on;
  pinMode(LED_PIN, OUTPUT);            // เผื่อเคย High-Z
  digitalWrite(LED_PIN, on ? HIGH : LOW);
  if (on) {
    countdownStartMillis = millis();
    countdownRunning = true;
  } else {
    countdownRunning = false;
    display.showNumberDec(countdownSeconds, true);
  }
}

// ---------- Web UI ----------
void handleRoot() {
  String html = R"HTML(
<!doctype html><html lang="th"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ไฟตรวจจับการเคลื่อนไหว (ESP32)</title>
<style>
:root{--bg:#0b1220;--card:#131c2b;--edge:#23314a;--txt:#e8ecf1}
body{margin:0;background:var(--bg);color:var(--txt);font-family:-apple-system,Segoe UI,Roboto,Arial,sans-serif}
.wrap{max-width:720px;margin:0 auto;padding:20px}
.card{background:var(--card);border:1px solid var(--edge);border-radius:16px;padding:20px}
h1{margin:0 0 12px;font-size:22px}
.row{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
.pill{padding:6px 12px;border-radius:999px;border:1px solid var(--edge);background:#1a2640;font-weight:600}
.state{background:#0f2a1d;border-color:#2a5d41}
.state.off{background:#2a0f0f;border-color:#5d2a2a}
#cd{font-size:40px;font-weight:800;font-family:ui-monospace,Consolas,monospace;margin:8px 0 14px}
input[type=number]{background:#0e1626;color:var(--txt);border:1px solid var(--edge);border-radius:10px;padding:10px;width:140px}
button{background:#2a3b5f;border:1px solid #40598f;color:var(--txt);padding:10px 16px;border-radius:12px;font-weight:700;cursor:pointer}
button:disabled{opacity:.5;cursor:not-allowed}
.muted{opacity:.8}
.footer{opacity:.6;font-size:13px;margin-top:10px}
.switch{position:relative;display:inline-block;width:64px;height:36px}
.switch input{display:none}
.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background:#5b677e;border-radius:999px;transition:.2s}
.slider:before{position:absolute;content:"";height:28px;width:28px;left:4px;top:4px;background:white;border-radius:50%;transition:.2s}
input:checked + .slider{background:#4cd964}
input:checked + .slider:before{transform:translateX(28px)}
</style>
</head><body>
<div class="wrap">
  <div class="card">
    <h1>ไฟตรวจจับการเคลื่อนไหว (ESP32)</h1>

    <div class="row">
      <div id="sys" class="pill">สวิตช์หลัก: --</div>
      <div id="state" class="pill state off">สถานะไฟ: ปิด</div>
      <div id="delaypill" class="pill">เวลานับถอยหลัง: -- วินาที</div>
    </div>

    <div style="margin:12px 0;">
      <div class="muted">เวลาที่เหลือก่อนปิดอัตโนมัติ (ไม่มีการเคลื่อนไหว):</div>
      <div id="cd">--</div>
    </div>

    <div class="row" style="gap:12px;margin:8px 0 14px;">
      <label class="row" style="gap:10px;align-items:center">
        <span>สวิตช์หลัก</span>
        <label class="switch">
          <input id="mainSwitch" type="checkbox">
          <span class="slider"></span>
        </label>
        <span>เปิด/ปิด</span>
      </label>

      <label class="row" style="gap:10px;align-items:center">
        <span>สวิตช์ไฟ</span>
        <label class="switch">
          <input id="lightSwitch" type="checkbox">
          <span class="slider"></span>
        </label>
        <span>เปิด/ปิด</span>
      </label>

      <form id="f" class="row" onsubmit="return false;">
        <label for="sec">เวลานับถอยหลัง (วินาที)</label>
        <input id="sec" type="number" min="1" max="86400" step="1">
        <button id="save" type="button">บันทึก</button>
      </form>
    </div>

    <div class="footer">หมายเหตุ: เมื่อ <b>ปิดสวิตช์หลัก</b> ระบบจะไม่ตอบสนองต่อการเคลื่อนไหวและไฟจะถูกปิด</div>
  </div>
</div>

<script>
async function getStatus(){ const r = await fetch('/api/status', {cache:'no-store'}); return r.json(); }
async function setDelay(sec){ await fetch('/api/setDelay?sec='+encodeURIComponent(sec)); }
async function setLight(on){ await fetch('/api/light?' + (on ? 'on=1' : 'off=1')); }
async function setSystem(on){ await fetch('/api/system?' + (on ? 'on=1' : 'off=1')); }

const elSys  = document.getElementById('sys');
const elState= document.getElementById('state');
const elDelay= document.getElementById('delaypill');
const elCd   = document.getElementById('cd');
const elMain = document.getElementById('mainSwitch');
const elSw   = document.getElementById('lightSwitch');
const elSec  = document.getElementById('sec');
const elSave = document.getElementById('save');

function fmt(n){ return Math.max(1, Math.floor(Number(n)||1)); }
function applyDisabled(dis){
  if (elSw)  elSw.disabled  = dis;
  if (elSec) elSec.disabled = dis;
  if (elSave)elSave.disabled= dis;
}

async function refresh(){
  try{
    const j = await getStatus();
    if (elMain) { elMain.checked = !!j.enabled; }
    if (elSys)  { elSys.textContent = 'สวิตช์หลัก: ' + (j.enabled ? 'เปิดระบบ' : 'ปิดระบบ'); }
    if (elSw)   { elSw.checked = !!j.light; }
    if (elState){ elState.textContent = 'สถานะไฟ: ' + (j.light ? 'เปิด' : 'ปิด');
                  elState.className = 'pill state ' + (j.light ? '' : 'off'); }
    if (elDelay){ elDelay.textContent = 'เวลานับถอยหลัง: ' + j.delay + ' วินาที'; }
    if (elSec && document.activeElement !== elSec) elSec.value = j.delay; // อย่าเขียนทับตอนกำลังพิมพ์
    if (elCd)   { elCd.textContent = j.remaining + ' s'; }
    applyDisabled(!j.enabled);
  }catch(e){
    // กัน JS หยุดทำงาน
  }
}

if (elMain) elMain.addEventListener('change', async () => { await setSystem(elMain.checked); refresh(); });
if (elSw)   elSw.addEventListener('change',  async () => { await setLight(elSw.checked);   refresh(); });
if (elSave) elSave.addEventListener('click', async () => { const v = fmt(elSec.value);     await setDelay(v); refresh(); });

refresh();
setInterval(refresh, 1000);
</script>
</body></html>
)HTML";

  server.send(200, "text/html; charset=utf-8", html);
}

// ---------- REST ----------
void handleStatus() {
  unsigned long now = millis();
  unsigned long remaining = 0;
  if (systemEnabled && lightOn && countdownRunning) {
    unsigned long elapsed = (now - countdownStartMillis) / 1000UL;
    if (elapsed < countdownSeconds) remaining = countdownSeconds - elapsed;
  }
  String json = "{";
  json += "\"enabled\":";   json += (systemEnabled ? "true" : "false"); json += ",";
  json += "\"light\":";     json += (lightOn ? "true" : "false");        json += ",";
  json += "\"delay\":";     json += countdownSeconds;                    json += ",";
  json += "\"remaining\":"; json += remaining;
  json += "}";

  server.sendHeader("Cache-Control", "no-store");  // กัน cache
  server.send(200, "application/json; charset=utf-8", json);
}

void handleSetDelay() {
  if (!systemEnabled) { server.send(200, "text/plain", "DISABLED"); return; }
  if (server.hasArg("sec")) {
    long v = server.arg("sec").toInt();
    if (v < 1) v = 1;
    if (v > 86400) v = 86400;
    countdownSeconds = (unsigned long)v;
    if (lightOn && countdownRunning) {
      unsigned long elapsed = (millis() - countdownStartMillis) / 1000UL;
      if (elapsed >= countdownSeconds) setLight(false);
    }
  }
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleLight() {
  if (!systemEnabled) { server.send(200, "text/plain; charset=utf-8", "DISABLED"); return; }
  if (server.hasArg("on"))  setLight(true);
  if (server.hasArg("off")) setLight(false);
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleSystem() {
  if (server.hasArg("on"))  applySystem(true);
  if (server.hasArg("off")) applySystem(false);
  server.send(200, "text/plain; charset=utf-8", "OK");
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(PIR_PIN, INPUT);            // HC-SR501 ขับระดับลอจิก
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // ปุ่มต่อกับ GND

  display.setBrightness(0x0f);
  display.showNumberDec(countdownSeconds, true);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/",                handleRoot);
  server.on("/api/status",      handleStatus);
  server.on("/api/setDelay",    handleSetDelay);
  server.on("/api/light",       handleLight);
  server.on("/api/system",      handleSystem);
  server.begin();

  if (!MDNS.begin("wayu")) {   // ชื่อเว็บไซต์
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started: http://wayu.local/");
  }
}


// ---------- Loop ----------
void loop() {
  server.handleClient();

  // ปุ่มจริง: toggle "สวิตช์หลัก" (debounce + edge)
  bool btn = digitalRead(BUTTON_PIN);          // LOW = กด
  if (btn != lastBtn) { lastDebounce = millis(); lastBtn = btn; }
  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    static bool lastStable = HIGH;
    if (btn != lastStable) {
      lastStable = btn;
      if (btn == LOW) {                        // กดลง
        applySystem(!systemEnabled);
        Serial.printf("System %s\n", systemEnabled ? "ENABLED" : "DISABLED");
      }
    }
  }

  // PIR ทำงานเมื่อเปิดระบบเท่านั้น
  if (systemEnabled && digitalRead(PIR_PIN) == HIGH) {
    if (!lightOn) setLight(true);
    countdownStartMillis = millis();
    countdownRunning = true;
  }

  // นับถอยหลัง + อัปเดต TM1637
  if (systemEnabled && countdownRunning) {
    unsigned long now = millis();
    unsigned long elapsed = (now - countdownStartMillis) / 1000UL;
    if (elapsed < countdownSeconds) {
      static unsigned long lastDisp = 0;
      if (now - lastDisp > 200) {
        lastDisp = now;
        display.showNumberDec(countdownSeconds - elapsed, true);
      }
    } else {
      setLight(false);
    }
  }
}