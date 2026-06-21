#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ModbusMaster.h>

// --- Hardware Config ---
#define MAX485_DE      4      
#define RX2_PIN        16     
#define TX2_PIN        17     
#define BUZZER_PIN     14     

const char* ssid = "Soil_Sensor_WiFi";
const char* wifi_pass = "12345678"; 
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer server(80);

ModbusMaster node;
float n_disp, p_disp, k_disp, ph_disp, ec_disp, t_disp, m_disp;
float moisture, temp, ec, ph, n_val, p_val, k_val;
float prev_moisture = 0.0, prev_ec = 0.0;
int stable_count = 0;
bool is_locked = false;
unsigned long last_read_time = 0;
int selectedCrop = 0; 

// --- โค้ดหน้าเว็บทั้งหมดถูกฝังลง Flash Memory (ROM) ด้วย PROGMEM ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>
  <style>
    body{font-family:'Segoe UI', Roboto, Helvetica, sans-serif; text-align:center; background:#f8fafc; margin:0; padding-bottom:30px; color:#334155;}
    .header{background:linear-gradient(135deg, #4f46e5, #3b82f6); color:white; padding:18px; position:relative; display:flex; align-items:center; justify-content:center; box-shadow:0 4px 12px rgba(79,70,229,0.3);}
    .menu-btn{position:absolute; left:15px; font-size:28px; cursor:pointer;}
    .card{background:white; padding:20px; margin:15px; border-radius:16px; border:none; transition:0.3s;}
    .val{font-size:55px; font-weight:900; color:#0f172a; margin:5px 0;}
    .none{box-shadow: 0 0 0 3px #ef4444 inset !important;} 
    .low{box-shadow: 0 0 0 3px #f59e0b inset !important;} 
    .opt{box-shadow: 0 0 0 3px #10b981 inset !important;}
    .sub-container{display:flex; flex-wrap:wrap; padding:5px; justify-content:center; gap:10px;}
    .sub-card{background:white; flex:1 1 28%; padding:20px 5px; border-radius:16px; box-sizing:border-box; border:none; box-shadow:0 4px 15px rgba(0,0,0,0.04); transition:0.3s;}
    .advice-box{display:none; background:linear-gradient(135deg, #1e293b, #0f172a); color:#f8fafc; padding:20px; margin:15px; border-radius:16px; text-align:left; line-height:1.6; box-shadow:0 8px 20px rgba(0,0,0,0.15);}
    .btn-save{background:linear-gradient(135deg, #10b981, #059669); color:white; border:none; padding:16px; font-size:20px; font-weight:bold; border-radius:50px; cursor:pointer; width:90%; margin:15px 0; box-shadow:0 4px 15px rgba(16,185,129,0.3); transition:0.2s;}
    .btn-save:disabled{background:#e2e8f0; box-shadow:none; color:#94a3b8;}
    .btn-group{margin:10px 15px; display:flex; justify-content:center; gap:12px;}
    .btn-action{flex:1; background:#3b82f6; color:white; padding:15px 5px; border-radius:12px; text-decoration:none; font-weight:bold; border:none; font-size:14px; cursor:pointer; box-shadow:0 4px 10px rgba(59,130,246,0.2);}
    .btn-reset{background:#ef4444; box-shadow:0 4px 10px rgba(239,68,68,0.2);}
    #sideMenu{display:none; position:fixed; top:0; left:0; width:100%; height:100%; background:rgba(15,23,42,0.8); z-index:200; color:white; padding-top:60px; backdrop-filter:blur(6px);}
    .menu-content{padding:25px; text-align:left;} select{width:100%; padding:15px; font-size:16px; border-radius:12px; margin-top:10px; background:white; border:none; box-shadow:0 2px 8px rgba(0,0,0,0.1); color:#0f172a; outline:none;}
    table{width:92%; margin:10px auto; background:white; border-collapse:collapse; border-radius:16px; overflow:hidden; font-size:12px; box-shadow:0 4px 15px rgba(0,0,0,0.04);} th,td{padding:12px 6px; border-bottom:1px solid #f1f5f9;} th{background:#f8fafc; color:#475569; font-weight:bold; text-transform:uppercase; letter-spacing:0.5px;}
  </style>
  <script>
    let activeCrop = 0;
    const cropConfig = { 0:{n:[50,150],p:[30,80],k:[80,200]}, 1:{n:[80,150],p:[25,60],k:[60,120]}, 2:{n:[100,200],p:[40,100],k:[120,250]}, 3:{n:[60,120],p:[30,70],k:[150,300]}, 4:{n:[120,200],p:[50,100],k:[80,150]} };
    
    function saveToHistory(){ 
      fetch('/beep'); 
      let d = JSON.parse(localStorage.getItem('soilData') || '[]'); 
      let now = new Date(); let time = now.toLocaleTimeString();
      let entry = { time:time, n:document.getElementById('n').innerText, p:document.getElementById('p').innerText, k:document.getElementById('k').innerText, ph:document.getElementById('ph').innerText, ec:document.getElementById('ec').innerText, t:document.getElementById('t').innerText, m:document.getElementById('m').innerText };
      d.unshift(entry); if(d.length > 5000) d.pop(); localStorage.setItem('soilData', JSON.stringify(d)); 
      alert('บันทึกลงประวัติเรียบร้อย'); updateTable();
    }

    function updateTable(){ 
      let d = JSON.parse(localStorage.getItem('soilData') || '[]'); 
      let hStr='<tr><th>N</th><th>P</th><th>K</th><th>pH</th><th>EC</th><th>Temp</th><th>Moist</th></tr>';
      d.slice(0,5).forEach(r=>{ 
        hStr+=`<tr><td>${r.n}</td><td>${r.p}</td><td>${r.k}</td><td>${r.ph}</td><td>${r.ec}</td><td>${r.t}</td><td>${r.m}</td></tr>`; 
      });
      document.getElementById('hist').innerHTML = hStr;
    }

    function downloadCSV(){ 
      let d = JSON.parse(localStorage.getItem('soilData') || '[]'); 
      if(d.length==0){ alert('ไม่มีข้อมูลให้ดาวน์โหลด'); return; }
      let csv = 'Time,Nitrogen,Phosphorus,Potassium,pH,EC,Temp,Moisture\\n';
      d.forEach(r=>{ csv += `${r.time},${r.n},${r.p},${r.k},${r.ph},${r.ec},${r.t},${r.m}\\n`; });
      let blob = new Blob([csv], {type: 'text/csv;charset=utf-8;'}); let url = window.URL.createObjectURL(blob);
      let a = document.createElement('a'); a.href = url; a.download = 'soil_report.csv'; a.click();
    }

    function resetData(){ 
      if(confirm('⚠️ คุณต้องการล้างประวัติทั้งหมดใช่หรือไม่?')){ 
        localStorage.removeItem('soilData'); 
        updateTable(); 
      } 
    }

    function toggleMenu(){ let m=document.getElementById('sideMenu'); m.style.display=(m.style.display==='block'?'none':'block'); }
    function setCrop(){ let c=document.getElementById('cropSel').value; fetch('/setCrop?v='+c).then(()=>{ alert('ตั้งค่าพืชเรียบร้อย'); toggleMenu(); }); }

    setInterval(function(){ 
      fetch('/data').then(r=>r.json()).then(d=>{
        if(activeCrop !== d.c) {
          activeCrop = d.c;
          document.getElementById('cropSel').value = activeCrop;
        }
        document.getElementById('n').innerText=d.n; document.getElementById('p').innerText=d.p; document.getElementById('k').innerText=d.k;
        document.getElementById('ph').innerText=d.ph; document.getElementById('ec').innerText=d.ec;
        document.getElementById('t').innerText=d.t; document.getElementById('m').innerText=d.m;
        
        let conf = cropConfig[activeCrop];
        const upd=(type,v,l,h)=>{ let el=document.getElementById('card-'+type); if(!el) return; el.classList.remove('low','opt','none'); if(v<=0) el.classList.add('none'); else if(v<l) el.classList.add('low'); else el.classList.add('opt'); };
        upd('n', d.n, conf.n[0], conf.n[1]); upd('p', d.p, conf.p[0], conf.p[1]); upd('k', d.k, conf.k[0], conf.k[1]);

        let advBox = document.getElementById('adv'); let btn = document.getElementById('saveBtn');
        if(d.locked){ 
          advBox.style.display='block'; btn.disabled=false; let advText=''; 
          if(d.ph < 5.5) advText += '<b style=\"color:#f59e0b;\">⚠️ ดินเปรี้ยว:</b> ควรโรยปูนขาวปรับสภาพดิน<br>'; 
          else if(d.ph > 8.0) advText += '<b style=\"color:#f59e0b;\">⚠️ ดินด่าง:</b> ควรเติมอินทรียวัตถุหรือกำมะถันผง<br>'; 
          if(d.n < conf.n[0] && d.p < conf.p[0] && d.k < conf.k[0]) { advText += '• <b>ขาดธาตุหลัก:</b> แนะนำปุ๋ยสูตรเสมอ 15-15-15<br>'; } 
          else { 
            if(d.n < conf.n[0]) advText += '• <b>ขาด N:</b> แนะนำปุ๋ยยูเรีย 46-0-0 (เร่งใบ)<br>'; 
            if(d.p < conf.p[0]) advText += '• <b>ขาด P:</b> แนะนำปุ๋ยแดป 18-46-0 (เร่งราก)<br>'; 
            if(d.k < conf.k[0]) advText += '• <b>ขาด K:</b> แนะนำปุ๋ย 0-0-60 (เร่งผล)<br>'; 
          } 
          advBox.innerHTML = advText || '<span style=\"color:#10b981;\">✅ สภาพดินดีเยี่ยม ไม่ต้องเติมปุ๋ยเพิ่ม</span>'; 
        } else { 
          advBox.style.display='none'; btn.disabled=true; 
        }
      });
    }, 1000); 
    
    window.onload = updateTable;
  </script>
</head>
<body>
  <div class='header'><span class='menu-btn' onclick='toggleMenu()'>☰</span><b style='font-size:20px; letter-spacing:1px;'>ระบบวิเคราะห์ดินดิจิทัล</b></div>
  
  <div id='sideMenu'><div class='menu-content'><h2>ตั้งค่าชนิดพืช</h2>
  <select id='cropSel'><option value='0'>📊 มาตรฐาน</option><option value='1'>🌾 ข้าว</option><option value='2'>🎋 อ้อย</option><option value='3'>🥔 มันสำปะหลัง</option><option value='4'>🌽 ข้าวโพด</option></select>
  <button class='btn-save' style='width:100%; margin-top:25px;' onclick='setCrop()'>บันทึกการตั้งค่า</button>
  <p style='text-align:center; color:#94a3b8; margin-top:40px; cursor:pointer; font-weight:bold;' onclick='toggleMenu()'>[ ปิดหน้าต่าง ]</p></div></div>

  <button id='saveBtn' class='btn-save' onclick='saveToHistory()' disabled style='margin-top:20px;'>บันทึกค่า</button>
  
  <div class='sub-container'>
    <div class='sub-card' id='card-n'><div style='color:#64748b; font-weight:bold;'>Nitrogen (N)</div><div class='val' id='n'>-</div><div style='font-size:12px; color:#94a3b8;'>mg/kg</div></div>
    <div class='sub-card' id='card-p'><div style='color:#64748b; font-weight:bold;'>Phosphorus (P)</div><div class='val' id='p'>-</div><div style='font-size:12px; color:#94a3b8;'>mg/kg</div></div>
    <div class='sub-card' id='card-k'><div style='color:#64748b; font-weight:bold;'>Potassium (K)</div><div class='val' id='k'>-</div><div style='font-size:12px; color:#94a3b8;'>mg/kg</div></div>
  </div>
  
  <div class='advice-box' id='adv'></div>
  
  <div class='btn-group'>
  <button class='btn-action' onclick='downloadCSV()'>📥 ดาวน์โหลดไฟล์ CSV</button>
  <button class='btn-action btn-reset' onclick='resetData()'>🔄 ล้างประวัติ</button>
  </div>

  <h3 style='color:#475569; margin-top:25px;'>ข้อมูลสภาพดินเพิ่มเติม</h3><div class='sub-container'>
    <div class='sub-card'><div style='color:#64748b; font-weight:bold;'>pH</div><div class='val' style='font-size:35px;' id='ph'>-</div></div>
    <div class='sub-card'><div style='color:#64748b; font-weight:bold;'>EC</div><div class='val' style='font-size:35px;' id='ec'>-</div><div style='font-size:12px; color:#94a3b8;'>us/cm</div></div>
    <div class='sub-card'><div style='color:#64748b; font-weight:bold;'>Temp</div><div class='val' style='font-size:35px;' id='t'>-</div><div style='font-size:12px; color:#94a3b8;'>°C</div></div>
    <div class='sub-card'><div style='color:#64748b; font-weight:bold;'>Moisture</div><div class='val' style='font-size:35px;' id='m'>-</div><div style='font-size:12px; color:#94a3b8;'>%</div></div>
  </div>
  
  <h3 style='color:#475569; margin-top:25px;'>ประวัติล่าสุด</h3><table id='hist'></table>
</body>
</html>
)rawliteral";

void preTransmission() { digitalWrite(MAX485_DE, HIGH); }
void postTransmission() { digitalWrite(MAX485_DE, LOW); }

void beep(int times, int duration) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(duration); digitalWrite(BUZZER_PIN, LOW);
    if (times > 1) delay(100); 
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(MAX485_DE, OUTPUT); pinMode(BUZZER_PIN, OUTPUT);
  
  WiFi.mode(WIFI_AP); WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)); WiFi.softAP(ssid, wifi_pass);
  dnsServer.start(DNS_PORT, "*", apIP);
  
  // โหลดหน้าเว็บจาก PROGMEM (คำสั่ง send_P)
  server.on("/", []() { server.send_P(200, "text/html", index_html); });
  server.on("/setCrop", []() { selectedCrop = server.arg("v").toInt(); server.send(200, "text/plain", "OK"); });
  server.on("/beep", []() { beep(1, 100); server.send(200, "text/plain", "OK"); });
  
  // แนบค่า selectedCrop (ตัวแปร c) ลงไปใน JSON ด้วย
  server.on("/data", []() { 
    String json = "{\"n\":"+String(n_disp,0)+",\"p\":"+String(p_disp,0)+",\"k\":"+String(k_disp,0)+",\"ph\":"+String(ph_disp,1)+",\"ec\":"+String(ec_disp,0)+",\"t\":"+String(t_disp,1)+",\"m\":"+String(m_disp,1)+",\"locked\":"+(is_locked?"true":"false")+",\"c\":"+String(selectedCrop)+"}";
    server.send(200, "application/json", json);
  });
  
  server.onNotFound([]() { server.sendHeader("Location", "/", true); server.send(302, "text/plain", ""); });
  server.begin();
  
  Serial2.begin(4800, SERIAL_8N1, RX2_PIN, TX2_PIN);
  node.begin(1, Serial2);
  node.preTransmission(preTransmission); node.postTransmission(postTransmission);
  beep(2, 100);
}

void loop() {
  dnsServer.processNextRequest(); server.handleClient();
  if (millis() - last_read_time > 1000) { last_read_time = millis(); if (readSensorData()) checkStability(); }
}

bool readSensorData() {
  uint8_t result = node.readHoldingRegisters(0x0000, 7);
  if (result == node.ku8MBSuccess) {
    moisture = node.getResponseBuffer(0) * 0.1; temp = node.getResponseBuffer(1) * 0.1;
    ec = node.getResponseBuffer(2); ph = node.getResponseBuffer(3) * 0.1;
    n_val = node.getResponseBuffer(4); p_val = node.getResponseBuffer(5); k_val = node.getResponseBuffer(6);
    if (!is_locked) { n_disp = n_val; p_disp = p_val; k_disp = k_val; ph_disp = ph; ec_disp = ec; t_disp = temp; m_disp = moisture; }
    return true;
  }
  return false;
}

void checkStability() {
  float diff = abs(moisture - prev_moisture) + abs(ec - prev_ec);
  if (diff > 5.0) { stable_count = 0; if (is_locked) { is_locked = false; beep(1, 50); } }
  else if (!is_locked && moisture > 2.0) { stable_count++; if (stable_count >= 5) { is_locked = true; beep(1, 800); } }
  prev_moisture = moisture; prev_ec = ec;
}