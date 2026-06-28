#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ModbusMaster.h>

// ============================================================
//  Hardware Config
// ============================================================
#define MAX485_DE  4
#define RX2_PIN   16
#define TX2_PIN   17
#define BUZZER_PIN 14

// ============================================================
//  Timing constants  
// ============================================================
constexpr uint32_t READ_INTERVAL_FAST        = 1000;   
constexpr uint32_t READ_INTERVAL_SLOW        = 3000;   
constexpr uint32_t READ_INTERVAL_NPK_LOCKED  = 15000;  
constexpr uint32_t CLIENT_TIMEOUT_MS         = 30000;  
constexpr uint32_t BEEP_ON_MS               = 100;
constexpr uint32_t BEEP_OFF_MS              = 100;

// ============================================================
//  Sensor / stability constants
// ============================================================
constexpr uint8_t  STABILITY_COUNT_THRESHOLD = 5;
constexpr float    MOISTURE_TOLERANCE        = 3.0f;
constexpr float    EC_TOLERANCE              = 15.0f;
constexpr uint8_t  ERROR_THRESHOLD           = 3;
constexpr uint8_t  MEDIAN_WINDOW             = 3;     

// ============================================================
//  Crop enum  (แทน int 0-4)
// ============================================================
enum Crop : uint8_t { STANDARD=0, RICE, SUGARCANE, CASSAVA, CORN };

// ============================================================
//  WiFi / Network
// ============================================================
const char* ssid      = "Soil_Sensor_WiFi";
const char* wifi_pass = "12345678";
const char* mdns_name = "soil";          // http://soil.local

constexpr byte  DNS_PORT = 53;
IPAddress       apIP(192, 168, 4, 1);
DNSServer       dnsServer;
WebServer       server(80);

// ============================================================
//  Modbus
// ============================================================
ModbusMaster node;
void preTransmission()  { digitalWrite(MAX485_DE, HIGH); }
void postTransmission() { digitalWrite(MAX485_DE, LOW);  }

// ============================================================
//  Sensor values 
// ============================================================
uint16_t raw_moisture = 0, raw_temp = 0, raw_ph = 0;
uint16_t raw_ec = 0, raw_n = 0, raw_p = 0, raw_k = 0;

uint16_t disp_moisture = 0, disp_temp = 0, disp_ph = 0;
uint16_t disp_ec = 0, disp_n = 0, disp_p = 0, disp_k = 0;

uint16_t med_moisture[MEDIAN_WINDOW] = {};
uint16_t med_ec[MEDIAN_WINDOW]       = {};
uint8_t  med_idx = 0;

uint16_t prev_moisture = 0, prev_ec = 0;

// ============================================================
//  State
// ============================================================
Crop     selectedCrop      = STANDARD;
int8_t   stable_count      = 0;
uint8_t  error_count       = 0;
bool     is_locked         = false;

unsigned long last_read_time     = 0;
unsigned long last_npk_read_time = 0;
unsigned long last_client_time   = 0; 

// ============================================================
//  Buzzer (non-blocking)
// ============================================================
int           beep_toggles  = 0;
unsigned long last_beep_time = 0;
bool          buzzer_state   = false;

// ============================================================
//  HTML  (PROGMEM)
// ============================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="th">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ระบบวิเคราะห์ดินดิจิทัล</title>
    <style>
        body { font-family: 'Segoe UI', Roboto, Helvetica, sans-serif; background: #f4f7fb; margin: 0; padding: 15px; color: #334155; }
        .top-bar { display: flex; justify-content: center; align-items: center; position: relative; font-size: 18px; font-weight: 700; margin-bottom: 20px; color: #1e293b; }
        .menu-btn { position: absolute; left: 0; font-size: 24px; cursor: pointer; color: #94a3b8; }
        .status-badge { background: linear-gradient(135deg, #6ee7b7, #34d399); color: white; padding: 10px 30px; border-radius: 20px; text-align: center; font-weight: bold; margin: 0 auto 20px; width: fit-content; box-shadow: 0 4px 10px rgba(52,211,153,0.3); cursor: pointer; border: none; font-size: 16px; display: block; }
        .status-badge:disabled { background: #cbd5e1; box-shadow: none; cursor: not-allowed; }
        .npk-container { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-bottom: 10px; }
        .npk-card { padding: 15px; border-radius: 12px; position: relative; box-shadow: 0 2px 8px rgba(0,0,0,0.04); }
        .n-card { background: #dcfce7; color: #166534; border: 1px solid #bbf7d0; }
        .p-card { background: #e0f2fe; color: #075985; border: 1px solid #bae6fd; }
        .k-card { background: #ffedd5; color: #9a3412; border: 1px solid #fed7aa; }
        .card-title { font-size: 14px; font-weight: 600; margin-bottom: 8px; }
        .card-value { font-size: 36px; font-weight: 900; line-height: 1; margin-bottom: 5px; }
        .card-unit { font-size: 12px; opacity: 0.8; }
        .mini-chart { position: absolute; bottom: 15px; right: 15px; display: flex; gap: 3px; align-items: flex-end; height: 18px; opacity: 0.4; }
        .bar { width: 6px; background: currentColor; border-radius: 2px; }
        .advice-box { background: white; padding: 15px 20px; border-radius: 12px; margin-bottom: 20px; font-size: 14px; line-height: 1.8; display: none; box-shadow: 0 2px 8px rgba(0,0,0,0.04); }
        .action-btns { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-bottom: 20px; }
        .btn-action { padding: 12px; border-radius: 8px; border: none; color: white; font-weight: 600; font-size: 14px; cursor: pointer; display: flex; justify-content: center; align-items: center; gap: 8px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }
        .btn-blue { background: #2563eb; }
        .btn-red  { background: #dc2626; }
        .section-title { text-align: center; font-weight: 700; font-size: 16px; margin: 25px 0 15px; color: #334155; }
        .env-container { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; }
        .env-card { background: white; padding: 12px; border-radius: 12px; display: flex; align-items: center; gap: 10px; box-shadow: 0 2px 8px rgba(0,0,0,0.04); }
        .env-icon { width: 36px; height: 36px; border-radius: 50%; display: flex; justify-content: center; align-items: center; font-weight: 800; font-size: 14px; flex-shrink: 0; }
        .env-data { display: flex; flex-direction: column; }
        .env-label { font-size: 11px; color: #64748b; margin-bottom: 2px; }
        .env-value { font-size: 16px; font-weight: 800; display: flex; align-items: baseline; gap: 2px; }
        .env-unit { font-size: 10px; font-weight: normal; color: #64748b; }
        @media (max-width: 600px) {
            .env-container { grid-template-columns: repeat(2, 1fr); }
            .npk-container { grid-template-columns: 1fr; }
        }
        #sideMenu { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(15,23,42,0.8); z-index: 200; backdrop-filter: blur(4px); }
        .menu-content { background: white; padding: 25px; border-radius: 0 0 20px 20px; text-align: center; }
        select { width: 100%; padding: 12px; border-radius: 8px; border: 1px solid #cbd5e1; margin: 15px 0; font-size: 16px; }
    </style>
</head>
<body>
    <div class="top-bar">
        <div class="menu-btn" onclick="toggleMenu()">≡</div>
        <div>📄 ระบบวิเคราะห์ดินดิจิทัล</div>
    </div>

    <button id="saveBtn" class="status-badge" onclick="saveToHistory()" disabled>บันทึกค่า</button>

    <div class="npk-container">
        <div class="npk-card n-card">
            <div class="card-title">Nitrogen (N)</div>
            <div class="card-value" id="n">-</div>
            <div class="card-unit">mg/kg</div>
            <div class="mini-chart">
                <div class="bar" style="height:30%"></div><div class="bar" style="height:50%"></div>
                <div class="bar" style="height:80%"></div><div class="bar" style="height:60%"></div>
                <div class="bar" style="height:40%"></div>
            </div>
        </div>
        <div class="npk-card p-card">
            <div class="card-title">Phosphorus (P)</div>
            <div class="card-value" id="p">-</div>
            <div class="card-unit">mg/kg</div>
            <div class="mini-chart">
                <div class="bar" style="height:20%"></div><div class="bar" style="height:40%"></div>
                <div class="bar" style="height:90%"></div><div class="bar" style="height:70%"></div>
                <div class="bar" style="height:50%"></div>
            </div>
        </div>
        <div class="npk-card k-card">
            <div class="card-title">Potassium (K)</div>
            <div class="card-value" id="k">-</div>
            <div class="card-unit">mg/kg</div>
            <div class="mini-chart">
                <div class="bar" style="height:40%"></div><div class="bar" style="height:60%"></div>
                <div class="bar" style="height:50%"></div><div class="bar" style="height:80%"></div>
                <div class="bar" style="height:30%"></div>
            </div>
        </div>
    </div>

    <div id="adv" class="advice-box"></div>

    <div class="action-btns">
        <button class="btn-action btn-blue" onclick="downloadCSV()">☁️ ดาวน์โหลด CSV</button>
        <button class="btn-action btn-red"  onclick="resetData()">🗑️ ล้างประวัติ</button>
    </div>

    <div class="section-title">ข้อมูลสภาพดินเพิ่มเติม</div>

    <div class="env-container">
        <div class="env-card">
            <div class="env-icon" style="background:#dcfce7; color:#166534;">pH</div>
            <div class="env-data">
                <div class="env-label">pH</div>
                <div class="env-value"><span id="ph">-</span></div>
            </div>
        </div>
        <div class="env-card">
            <div class="env-icon" style="background:#e2e8f0; color:#475569;">EC</div>
            <div class="env-data">
                <div class="env-label">Conductivity</div>
                <div class="env-value"><span id="ec">-</span> <span class="env-unit">µS/cm</span></div>
            </div>
        </div>
        <div class="env-card">
            <div class="env-icon" style="background:#dcfce7; font-size:20px;">🌡️</div>
            <div class="env-data">
                <div class="env-label">Temp</div>
                <div class="env-value"><span id="t">-</span> <span class="env-unit">°C</span></div>
            </div>
        </div>
        <div class="env-card">
            <div class="env-icon" style="background:#dcfce7; font-size:18px;">💧</div>
            <div class="env-data">
                <div class="env-label">Moisture</div>
                <div class="env-value"><span id="m">-</span> <span class="env-unit">%</span></div>
            </div>
        </div>
    </div>

    <div id="sideMenu">
        <div class="menu-content">
            <h3>ตั้งค่าชนิดพืช</h3>
            <select id="cropSel">
                <option value="0">📊 มาตรฐาน</option>
                <option value="1">🌾 ข้าว</option>
                <option value="2">🎋 อ้อย</option>
                <option value="3">🥔 มันสำปะหลัง</option>
                <option value="4">🌽 ข้าวโพด</option>
            </select>
            <button class="status-badge" onclick="setCrop()">บันทึกการตั้งค่า</button>
            <div style="margin-top:15px;color:#ef4444;cursor:pointer;" onclick="toggleMenu()">[ ปิดหน้าต่าง ]</div>
        </div>
    </div>

    <script>
        // ── Cache ค่าก่อนหน้า เพื่อ skip DOM update ถ้าไม่เปลี่ยน ──
        const prev = { n:'', p:'', k:'', ph:'', ec:'', t:'', m:'', locked:null, c:-1 };

        function setIfChanged(id, val) {
            const el = document.getElementById(id);
            if (el && el.innerText !== String(val)) el.innerText = val;
        }

        let activeCrop = 0;
        const cropConfig = {
            0:{n:[50,150],  p:[30,80],   k:[80,200]},
            1:{n:[80,150],  p:[25,60],   k:[60,120]},
            2:{n:[100,200], p:[40,100],  k:[120,250]},
            3:{n:[60,120],  p:[30,70],   k:[150,300]},
            4:{n:[120,200], p:[50,100],  k:[80,150]}
        };

        function saveToHistory() {
            fetch('/beep');
            let d = JSON.parse(localStorage.getItem('soilData') || '[]');
            d.unshift({
                time: new Date().toLocaleTimeString(),
                n: document.getElementById('n').innerText,
                p: document.getElementById('p').innerText,
                k: document.getElementById('k').innerText,
                ph: document.getElementById('ph').innerText,
                ec: document.getElementById('ec').innerText,
                t: document.getElementById('t').innerText,
                m: document.getElementById('m').innerText
            });
            if (d.length > 5000) d.pop();
            localStorage.setItem('soilData', JSON.stringify(d));
            alert('บันทึกลงประวัติเรียบร้อย');
        }

        function downloadCSV() {
            let d = JSON.parse(localStorage.getItem('soilData') || '[]');
            if (!d.length) { alert('ไม่มีข้อมูลให้ดาวน์โหลด'); return; }
            let csv = 'Time,Nitrogen,Phosphorus,Potassium,pH,EC,Temp,Moisture\n';
            d.forEach(r => { csv += `${r.time},${r.n},${r.p},${r.k},${r.ph},${r.ec},${r.t},${r.m}\n`; });
            const a = document.createElement('a');
            a.href = URL.createObjectURL(new Blob([csv], {type:'text/csv;charset=utf-8;'}));
            a.download = 'soil_report.csv';
            a.click();
        }

        function resetData() {
            if (confirm('⚠️ คุณต้องการล้างประวัติทั้งหมดใช่หรือไม่?')) {
                localStorage.removeItem('soilData');
                alert('ล้างประวัติเรียบร้อย');
            }
        }

        function toggleMenu() {
            const m = document.getElementById('sideMenu');
            m.style.display = (m.style.display === 'block') ? 'none' : 'block';
        }

        function setCrop() {
            const c = document.getElementById('cropSel').value;
            fetch('/setCrop?v=' + c).then(() => { alert('ตั้งค่าพืชเรียบร้อย'); toggleMenu(); });
        }

        // ── Polling ทุก 1 วิ — อัปเดต DOM เฉพาะค่าที่เปลี่ยน ──
        setInterval(function () {
            fetch('/data').then(r => r.json()).then(d => {
                // crop changed?
                if (prev.c !== d.c) {
                    activeCrop = d.c;
                    document.getElementById('cropSel').value = activeCrop;
                    prev.c = d.c;
                }

                // อัปเดต DOM เฉพาะค่าที่ต่างจากเดิม
                setIfChanged('n',  d.n);
                setIfChanged('p',  d.p);
                setIfChanged('k',  d.k);
                setIfChanged('ph', d.ph);
                setIfChanged('ec', d.ec);
                setIfChanged('t',  d.t);
                setIfChanged('m',  d.m);

                // advice box — อัปเดตเฉพาะเมื่อ locked state เปลี่ยน หรือค่าเปลี่ยน
                const advBox = document.getElementById('adv');
                const btn    = document.getElementById('saveBtn');

                if (d.locked) {
                    btn.disabled = false;
                    advBox.style.display = 'block';
                    const conf = cropConfig[activeCrop];
                    let advText = '';
                    if      (d.ph < 5.5) advText += '<div>⚠️ ดินเปรี้ยว: ควรโรยปูนขาวปรับสภาพดิน</div>';
                    else if (d.ph > 8.0) advText += '<div>⚠️ ดินด่าง: ควรเติมอินทรียวัตถุหรือกำมะถันผง</div>';
                    if (d.n < conf.n[0] && d.p < conf.p[0] && d.k < conf.k[0]) {
                        advText += '<div>⚠️ ขาดธาตุหลัก: แนะนำปุ๋ยสูตรเสมอ 15-15-15</div>';
                    } else {
                        if (d.n < conf.n[0]) advText += '<div>⚠️ ขาด N: แนะนำปุ๋ยยูเรีย 46-0-0 (เร่งใบ)</div>';
                        if (d.p < conf.p[0]) advText += '<div>⚠️ ขาด P: แนะนำปุ๋ยแดป 18-46-0 (เร่งราก)</div>';
                        if (d.k < conf.k[0]) advText += '<div>⚠️ ขาด K: แนะนำปุ๋ย 0-0-60 (เร่งผล)</div>';
                    }
                    advBox.innerHTML = advText || '<div>✅ สภาพดินดีเยี่ยม ไม่ต้องเติมปุ๋ยเพิ่ม</div>';
                } else {
                    btn.disabled = true;
                    advBox.style.display = 'none';
                }
            }).catch(() => {});
        }, 1000);
    </script>
</body>
</html>
)rawliteral";

// ============================================================
//  Median filter  (size 3 — sort 3 ค่า หา median)
// ============================================================
uint16_t median3(uint16_t a, uint16_t b, uint16_t c) {
  if (a > b) { uint16_t t=a; a=b; b=t; }
  if (b > c) { uint16_t t=b; b=c; c=t; }
  if (a > b) { uint16_t t=a; a=b; b=t; }
  return b; // middle value
}

// ============================================================
//  Buzzer (non-blocking)
// ============================================================
void triggerBeep(int times) {
  beep_toggles  = times * 2;
  buzzer_state  = true;
  digitalWrite(BUZZER_PIN, HIGH);
  last_beep_time = millis();
  beep_toggles--;
}

void handleBuzzer(unsigned long now) {
  if (beep_toggles > 0) {
    uint32_t interval = buzzer_state ? BEEP_ON_MS : BEEP_OFF_MS;
    if (now - last_beep_time >= interval) {
      last_beep_time = now;
      buzzer_state   = !buzzer_state;
      digitalWrite(BUZZER_PIN, buzzer_state ? HIGH : LOW);
      beep_toggles--;
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ============================================================
//  Read sensor 
//  readEnv()  : Moisture, Temp, EC, pH  (reg 0-3)
//  readNPK()  : N, P, K                 (reg 4-6)
// ============================================================
bool readEnv() {
  uint8_t result = node.readHoldingRegisters(0x0000, 4);
  if (result != node.ku8MBSuccess) return false;
  raw_moisture = node.getResponseBuffer(0);
  raw_temp     = node.getResponseBuffer(1);
  raw_ec       = node.getResponseBuffer(2);
  raw_ph       = node.getResponseBuffer(3);
  return true;
}

bool readNPK() {
  uint8_t result = node.readHoldingRegisters(0x0004, 3);
  if (result != node.ku8MBSuccess) return false;
  raw_n = node.getResponseBuffer(0);
  raw_p = node.getResponseBuffer(1);
  raw_k = node.getResponseBuffer(2);
  return true;
}

// ============================================================
//  readSensorData — loop
// ============================================================
void readSensorData(unsigned long now) {
  bool env_ok = readEnv();

  if (!env_ok) {
    error_count++;
    if (error_count >= ERROR_THRESHOLD) {
      is_locked    = false;
      stable_count = 0;
      error_count  = ERROR_THRESHOLD;
      disp_moisture = disp_temp = disp_ph = disp_ec = 0;
      disp_n = disp_p = disp_k = 0;
      prev_moisture = prev_ec = 0;
      med_idx = 0;
      memset(med_moisture, 0, sizeof(med_moisture));
      memset(med_ec,       0, sizeof(med_ec));
    }
    return;
  }

  error_count = 0;

  // Median filter Moisture & EC
  med_moisture[med_idx] = raw_moisture;
  med_ec[med_idx]       = raw_ec;
  med_idx = (med_idx + 1) % MEDIAN_WINDOW;

  uint16_t filt_moisture = median3(med_moisture[0], med_moisture[1], med_moisture[2]);
  uint16_t filt_ec       = median3(med_ec[0],       med_ec[1],       med_ec[2]);

  disp_moisture = filt_moisture;
  disp_temp     = raw_temp;
  disp_ph       = raw_ph;
  disp_ec       = filt_ec;

  bool need_npk = !is_locked || (now - last_npk_read_time >= READ_INTERVAL_NPK_LOCKED);
  if (need_npk) {
    if (readNPK()) {
      last_npk_read_time = now;
      if (!is_locked) {
        disp_n = raw_n;
        disp_p = raw_p;
        disp_k = raw_k;
      }
    }
  }
}

// ============================================================
//  checkStability
// ============================================================
void checkStability() {
  if (error_count > 0) return;

  uint16_t filt_moisture = median3(med_moisture[0], med_moisture[1], med_moisture[2]);
  uint16_t filt_ec       = median3(med_ec[0],       med_ec[1],       med_ec[2]);

  bool moisture_changed = abs((int)filt_moisture - (int)prev_moisture) > (int)(MOISTURE_TOLERANCE * 10);
  bool ec_changed       = abs((int)filt_ec       - (int)prev_ec)       > (int)EC_TOLERANCE;

  if (moisture_changed || ec_changed) {
    stable_count = 0;
    if (is_locked) {
      is_locked = false;
      triggerBeep(1);
    }
  } else if (!is_locked && filt_moisture > 20) { // 20 unit = 2.0%
    stable_count++;
    if (stable_count >= STABILITY_COUNT_THRESHOLD) {
      is_locked = true;
      disp_n = raw_n;
      disp_p = raw_p;
      disp_k = raw_k;
      triggerBeep(1);
    }
  }

  prev_moisture = filt_moisture;
  prev_ec       = filt_ec;
}

// ============================================================
//  Setup
// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(MAX485_DE,  OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, wifi_pass);

  // mDNS — http://soil.local
  if (MDNS.begin(mdns_name)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS: http://soil.local");
  }

  dnsServer.start(DNS_PORT, "*", apIP);

  // ── Routes ──
  server.on("/", []() {
    server.send_P(200, "text/html", index_html);
  });

  server.on("/setCrop", []() {
    selectedCrop = (Crop)constrain(server.arg("v").toInt(), 0, 4);
    server.send(200, "text/plain", "OK");
  });

  server.on("/beep", []() {
    triggerBeep(1);
    server.send(200, "text/plain", "OK");
  });

  server.on("/data", []() {
    last_client_time = millis();

    char buf[320];
    snprintf(buf, sizeof(buf),
      "{\"n\":%u,\"p\":%u,\"k\":%u"
      ",\"ph\":%.1f,\"ec\":%u,\"t\":%.1f,\"m\":%.1f"
      ",\"locked\":%s,\"c\":%d}",
      disp_n, disp_p, disp_k,
      disp_ph  * 0.1f,
      disp_ec,
      disp_temp * 0.1f,
      disp_moisture * 0.1f,
      is_locked ? "true" : "false",
      (int)selectedCrop
    );
    server.send(200, "application/json", buf);
  });

  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();

  // Modbus
  Serial2.begin(4800, SERIAL_8N1, RX2_PIN, TX2_PIN);
  node.begin(1, Serial2);
  Serial2.setTimeout(300);  
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  last_client_time = millis(); 
  triggerBeep(2);
}

// ============================================================
//  Loop
// ============================================================
void loop() {
  unsigned long now = millis();

  dnsServer.processNextRequest();
  server.handleClient();
  handleBuzzer(now);

  bool client_active = (now - last_client_time) < CLIENT_TIMEOUT_MS;

  uint32_t current_interval = is_locked ? READ_INTERVAL_SLOW : READ_INTERVAL_FAST;

  if (client_active && (now - last_read_time) >= current_interval) {
    last_read_time = now;
    readSensorData(now);
    checkStability();
  }
}
