// НОВИЙ АСИНХРОННИЙ ВЕБ-СЕРВЕР - НЕБЛОКУЮЧИЙ!
#include "web_server.h"
#include "mqtt_handler.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Preferences.h>
#include <Update.h>

WebServerManager::WebServerManager(BluettiDevice* device, SystemStatus* sharedStatus)
    : server(80), bluetti(device), status(sharedStatus) {}

void WebServerManager::begin() {
    // Головна сторінка
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "text/html", buildHtml());
    });
    
    // Toggle Bluetti
    server.on("/toggle", HTTP_GET, [this](AsyncWebServerRequest *request) {
        status->bluettiEnabled = !status->bluettiEnabled;
        Serial.printf("Bluetti enabled set to: %s\n", status->bluettiEnabled ? "true" : "false");
        if (!status->bluettiEnabled && bluetti && bluetti->isConnected()) {
            bluetti->disconnect();
        }
        request->redirect("/");
    });
    
    // Status JSON
    server.on("/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["wifi_connected"] = status->wifiConnected;
        doc["wifi_ip"] = status->wifiConnected ? status->wifiIp.toString() : "0.0.0.0";
        doc["wifi_rssi"] = status->wifiRssi;
        doc["mqtt_connected"] = status->mqttConnected;
        doc["battery_level"] = status->batteryLevel;
        doc["ac_power"] = status->acPower;
        doc["dc_power"] = status->dcPower;
        doc["input_power"] = status->inputPower;
        doc["ac_input_power"] = status->acInputPower;
        doc["dc_input_power"] = status->dcInputPower;
        doc["ac_state"] = status->acOutputState;
        doc["dc_state"] = status->dcOutputState;
        doc["charging_speed"] = status->chargingSpeed; // 0=Standard, 1=Silent, 2=Turbo
        doc["eco_mode"] = status->ecoMode;
        doc["power_lifting"] = status->powerLifting;
        doc["led_mode"] = status->ledMode; // 1=Low, 2=High, 3=SOS, 4=Off
        doc["eco_shutdown"] = status->ecoShutdown; // 1-4 години
        doc["battery_voltage"] = status->esp32UsbPowered ? status->esp32BatteryVoltage : status->esp32Voltage;
        doc["battery_percent"] = status->esp32BatteryPercent;
        doc["usb_powered"] = status->esp32UsbPowered;
        doc["uptime"] = status->uptime;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["total_heap"] = ESP.getHeapSize();
        doc["max_heap"] = ESP.getMaxAllocHeap();
        doc["cpu_freq"] = ESP.getCpuFreqMHz();
        doc["bluetti_connected"] = status->bluettiConnected;
        doc["bluetti_enabled"] = status->bluettiEnabled;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Restart
    server.on("/restart", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Restarting...");
        delay(200);
        ESP.restart();
    });
    
    // Config page
    server.on("/config", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "text/html", buildConfigHtml());
    });
    
    // Save config
    server.on("/save_config", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSaveConfig(request);
    });
    
    // OTA redirect
    server.on("/ota", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->redirect("/update");
    });
    
    // Update page
    server.on("/update", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "text/html", buildUpdateHtml());
    });
    
    // Update POST
    server.on("/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!Update.isFinished()) {
            request->send(500, "text/plain", "Update not finished");
            return;
        }
        if (Update.hasError()) {
            String error = "Update failed: ";
            error += Update.errorString();
            request->send(500, "text/plain", error);
            Update.abort();
            return;
        }
        request->send(200, "text/plain", "OK");
        request->onDisconnect([]() {
            delay(1000);
            ESP.restart();
        });
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        handleUpdateProgress(request, filename, index, data, len, final);
    });
    
    // AC/DC output control
    server.on("/ac_output", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSetACOutput(request);
    });
    
    server.on("/dc_output", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSetDCOutput(request);
    });
    
    // Charging speed control
    server.on("/charging_speed", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSetChargingSpeed(request);
    });
    
    // ECO mode control
    server.on("/eco_mode", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSetEcoMode(request);
    });
    
    // Power Lifting control
    server.on("/power_lifting", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSetPowerLifting(request);
    });
    
    // LED mode control
    server.on("/led_mode", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSetLedMode(request);
    });
    
    // ECO Shutdown control
    server.on("/eco_shutdown", HTTP_POST, [this](AsyncWebServerRequest *request) {
        handleSetEcoShutdown(request);
    });
    
    // Power Off
    server.on("/power_off", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (bluetti) {
            bluetti->powerOff();
            request->send(200, "text/plain", "Power Off command sent");
        } else {
            request->send(400, "text/plain", "Bluetti not available");
        }
    });
    
    // Republish MQTT Discovery
    server.on("/republish_discovery", HTTP_GET, [this](AsyncWebServerRequest *request) {
        extern MQTTHandler mqtt;
        mqtt.republishDiscovery();
        request->send(200, "text/plain", "MQTT Discovery republished");
    });
    
    server.begin();
    Serial.println("Async Web server started on port 80");
}

void WebServerManager::handleClient() {
    // Асинхронний сервер не потребує handleClient()
}

bool WebServerManager::isBluettiEnabled() const {
    return status->bluettiEnabled;
}

void WebServerManager::setBluettiEnabled(bool enabled) {
    status->bluettiEnabled = enabled;
    if (!enabled && bluetti && bluetti->isConnected()) {
        bluetti->disconnect();
    }
}

String WebServerManager::buildHtml() {
    // Розділяю на частини через обмеження розміру F() макросу
    String html;
    html.reserve(12000);
    
    // Частина 1: Header + CSS
    html += F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP32 Monitor</title>");
    html += F("<style>body{font-family:Arial;margin:0;padding:12px;background:#f5f5f5;}.c{max-width:800px;margin:0 auto;}");
    html += F(".menu{display:flex;gap:8px;flex-wrap:wrap;margin:0 0 12px 0;}");
    html += F(".menu a{background:#007bff;color:#fff;text-decoration:none;padding:6px 10px;border-radius:6px;font-size:13px;font-weight:bold;}");
    html += F(".menu a:hover{opacity:0.9;}");
    html += F(".card{background:#fff;padding:16px;border-radius:8px;margin-bottom:12px;box-shadow:0 2px 4px rgba(0,0,0,0.1);}");
    html += F("h1{font-size:20px;margin:0 0 12px 0;color:#333;}h2{font-size:16px;margin:0 0 8px 0;color:#555;border-bottom:1px solid #eee;padding-bottom:6px;}");
    html += F(".g{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:8px;margin-top:8px;}");
    html += F(".i{display:flex;justify-content:space-between;padding:6px;background:#f9f9f9;border-radius:4px;align-items:center;}");
    html += F(".l{font-weight:bold;color:#666;font-size:13px;}.v{color:#333;font-size:13px;}");
    html += F(".b{display:inline-block;padding:3px 10px;border-radius:10px;font-size:11px;font-weight:bold;}");
    html += F(".bs{background:#d4edda;color:#155724;}.bd{background:#f8d7da;color:#721c24;}.bw{background:#fff3cd;color:#856404;}");
    html += F("button{width:100%;padding:10px;border:none;border-radius:6px;font-size:14px;margin-top:8px;cursor:pointer;font-weight:bold;}");
    html += F(".btn-s{background:#28a745;color:#fff;}.btn-d{background:#dc3545;color:#fff;}");
    html += F(".btn-sm{background:#2196F3;color:#fff;padding:4px 8px;font-size:11px;width:auto;margin:0 0 0 8px;}");
    html += F(".sel{background:#fff;border:1px solid #ddd;border-radius:4px;padding:4px 8px;font-size:12px;cursor:pointer;margin:0 0 0 8px;color:#333;}");
    html += F(".btn-w{background:#ffc107;color:#000;}.btn-eco{background:#4CAF50;color:#fff;padding:8px;margin-top:4px;}");
    html += F(".btn-power{background:#F44336;color:#fff;padding:8px;margin-top:4px;}button:hover{opacity:0.9;}");
    html += F("select{padding:4px 8px;font-size:11px;border-radius:4px;margin-left:8px;}</style>");
    
    // Частина 2: JavaScript (початок)
    html += F("<script>function u(){fetch('/status').then(r=>r.json()).then(d=>{");
    html += F("document.getElementById('ws').textContent=d.wifi_connected?'Підключено':'Відключено';");
    html += F("document.getElementById('wi').textContent=d.wifi_ip||'0.0.0.0';document.getElementById('wr').textContent=d.wifi_rssi+' dBm';");
    html += F("document.getElementById('ms').textContent=d.mqtt_connected?'Підключено':'Очікування';");
    html += F("var h=Math.floor(d.uptime/3600),m=Math.floor((d.uptime%3600)/60),s=d.uptime%60;");
    html += F("document.getElementById('ut').textContent=h+':'+(m<10?'0':'')+m+':'+(s<10?'0':'')+s;");
    html += F("var v=d.battery_voltage||0,p=d.battery_percent||0;if(d.usb_powered){");
    html += F("if(v>0.1)document.getElementById('eb').innerHTML=v.toFixed(2)+' V ('+p+'%) <span style=\"color:#4CAF50;\">USB</span>';");
    html += F("else document.getElementById('eb').innerHTML='USB Powered';}");
    html += F("else if(v<0.05)document.getElementById('eb').textContent=v.toFixed(2)+' V';");
    html += F("else if(v>=2.5)document.getElementById('eb').textContent=v.toFixed(2)+' V ('+p+'%)';");
    html += F("else document.getElementById('eb').textContent=v.toFixed(2)+' V';");
    html += F("document.getElementById('bl').textContent=d.battery_level+'%';");
    
    // Частина 3: AC/DC/Input Power
    html += F("var acText=(d.ac_state?'ON':'OFF')+' '+d.ac_power+'W';");
    html += F("if(d.ac_input_power>5)acText+=' (⚡'+d.ac_input_power+'W)';");
    html += F("document.getElementById('ac').textContent=acText;");
    html += F("var dcText=(d.dc_state?'ON':'OFF')+' '+d.dc_power+'W';");
    html += F("if(d.dc_input_power>d.dc_power+5)dcText+=' (⚡'+d.dc_input_power+'W)';");
    html += F("document.getElementById('dc').textContent=dcText;");
    html += F("var inputText=d.input_power+'W';");
    html += F("document.getElementById('ip').textContent=inputText;");
    
    // Частина 4: New Features Display
    html += F("document.getElementById('cs-text').textContent=d.charging_speed==0?'STANDARD':(d.charging_speed==1?'SILENT':'TURBO');");
    html += F("document.getElementById('eco-text').textContent=d.eco_mode?'ON':'OFF';");
    html += F("document.getElementById('pl-text').textContent=d.power_lifting?'ON':'OFF';");
    html += F("var ledText=['','Low','High','SOS','Off'];document.getElementById('led-text').textContent=ledText[d.led_mode]||'Off';");
    html += F("document.getElementById('ecs-text').textContent=d.eco_shutdown+'h';");
    
    // Частина 5: ESP32 Memory
    html += F("document.getElementById('fh').textContent=Math.floor(d.free_heap/1024)+' KB';");
    html += F("document.getElementById('th').textContent=Math.floor(d.total_heap/1024)+' KB';");
    html += F("document.getElementById('mh').textContent=Math.floor(d.max_heap/1024)+' KB';");
    html += F("document.getElementById('cf').textContent=d.cpu_freq+' MHz';");
    
    // Частина 6: Dynamic Buttons (AC) - зберігаємо стан у data-атрибуті
    html += F("var acC=document.getElementById('acb-container');if(acC){if(d.bluetti_connected){");
    html += F("if(!document.getElementById('acb')){var acB=document.createElement('button');acB.id='acb';acB.className='btn-sm';acC.appendChild(acB);}");
    html += F("var ac=document.getElementById('acb');if(ac){ac.textContent=d.ac_state?'🔴 Вимк':'🟢 Увімк';ac.dataset.state=d.ac_state?'on':'off';");
    html += F("ac.onclick=function(){var cur=this.dataset.state==='on';fetch('/ac_output',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},");
    html += F("body:'state='+(cur?'off':'on')}).then(()=>setTimeout(u,1000));};}}else{acC.innerHTML='';}}");
    
    // Частина 7: Dynamic Buttons (DC) - зберігаємо стан у data-атрибуті
    html += F("var dcC=document.getElementById('dcb-container');if(dcC){if(d.bluetti_connected){");
    html += F("if(!document.getElementById('dcb')){var dcB=document.createElement('button');dcB.id='dcb';dcB.className='btn-sm';dcC.appendChild(dcB);}");
    html += F("var dc=document.getElementById('dcb');if(dc){dc.textContent=d.dc_state?'🔴 Вимк':'🟢 Увімк';dc.dataset.state=d.dc_state?'on':'off';");
    html += F("dc.onclick=function(){var cur=this.dataset.state==='on';fetch('/dc_output',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},");
    html += F("body:'state='+(cur?'off':'on')}).then(()=>setTimeout(u,1000));};}}else{dcC.innerHTML='';}}");
    
    // Частина 8: Dynamic Dropdown (Charging Speed)
    html += F("var csC=document.getElementById('cs-container');if(csC){if(d.bluetti_connected){");
    html += F("if(!document.getElementById('cs')){var csS=document.createElement('select');csS.id='cs';csS.className='sel';");
    html += F("csS.innerHTML='<option value=\"0\">📊 STANDARD</option><option value=\"1\">🔇 SILENT</option><option value=\"2\">⚡ TURBO</option>';");
    html += F("csS.onchange=function(){fetch('/charging_speed',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},");
    html += F("body:'speed='+this.value}).then(()=>setTimeout(u,500));};csC.appendChild(csS);}");
    html += F("var cs=document.getElementById('cs');if(cs)cs.value=d.charging_speed;");
    html += F("}else{csC.innerHTML='';}}");
    
    // Частина 9: Toggle Button
    html += F("var btn=document.getElementById('toggleBtn');if(btn){");
    html += F("btn.textContent=d.bluetti_enabled?'🔴 Вимкнути':'🟢 Увімкнути';");
    html += F("btn.className=d.bluetti_enabled?'btn-d':'btn-s';}");
    html += F("var bs=document.getElementById('bs');if(bs)bs.textContent=d.bluetti_connected?'Підключено':(d.bluetti_enabled?'Підключення...':'Вимкнено');");
    
    // Частина 10: New Feature Buttons
    html += F("var ecoB=document.getElementById('eco-btn'),plB=document.getElementById('pl-btn');");
    html += F("var ledS=document.getElementById('led-select'),ecsS=document.getElementById('ecs-select'),pwrB=document.getElementById('power-btn');");
    html += F("if(d.bluetti_connected){");
    // ECO button - зберігаємо стан в data-атрибуті і читаємо його при кліку
    html += F("if(ecoB){ecoB.style.display='block';ecoB.textContent=d.eco_mode?'🌿 ECO: ON':'🌿 ECO: OFF';");
    html += F("ecoB.dataset.state=d.eco_mode?'on':'off';");
    html += F("ecoB.onclick=function(){var cur=this.dataset.state==='on';fetch('/eco_mode',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},");
    html += F("body:'state='+(cur?'off':'on')}).then(()=>setTimeout(u,1000));};}");
    // Power Lifting button
    html += F("if(plB){plB.style.display='block';plB.textContent=d.power_lifting?'⚡ Power Lifting: ON':'⚡ Power Lifting: OFF';");
    html += F("plB.dataset.state=d.power_lifting?'on':'off';");
    html += F("plB.onclick=function(){var cur=this.dataset.state==='on';fetch('/power_lifting',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},");
    html += F("body:'state='+(cur?'off':'on')}).then(()=>setTimeout(u,1000));};}");
    html += F("if(ledS){ledS.style.display='inline-block';ledS.value=d.led_mode||4;");
    html += F("ledS.onchange=function(){fetch('/led_mode',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},");
    html += F("body:'mode='+ledS.value}).then(()=>setTimeout(u,1000));};}");
    html += F("if(ecsS){ecsS.style.display='inline-block';ecsS.value=d.eco_shutdown||1;");
    html += F("ecsS.onchange=function(){fetch('/eco_shutdown',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},");
    html += F("body:'hours='+ecsS.value}).then(()=>setTimeout(u,1000));};}");
    html += F("if(pwrB)pwrB.style.display='block';");
    html += F("}else{if(ecoB)ecoB.style.display='none';if(plB)plB.style.display='none';");
    html += F("if(ledS)ledS.style.display='none';if(ecsS)ecsS.style.display='none';if(pwrB)pwrB.style.display='none';}");
    html += F("}).catch(e=>console.error('E:',e));}");
    html += F("function startUpdates(){u();setInterval(u,3000);}");
    html += F("if(document.readyState==='complete'||document.readyState==='interactive'){startUpdates();}else{document.addEventListener('DOMContentLoaded',startUpdates);}");
    html += F("</script></head>");
    
    // Частина 11: HTML Body
    html += F("<body><div class='c'><div class='card'><h1>🔋 ESP32 Monitor</h1><div class='menu'>");
    html += F("<a href='/'>Головна</a>");
    html += F("<a href='/config'>Налаштування</a>");
    html += F("<a href='/update'>Прошивка</a>");
    html += F("<a href='/restart' onclick=\"return confirm('Перезапустити ESP32?');\">Перезапуск</a>");
    html += F("</div></div>");
    html += F("<div class='card'><h2>📡 Система</h2><div class='g'>");
    html += F("<div class='i'><span class='l'>WiFi:</span><span class='b bs'><span id='ws'>...</span></span></div>");
    html += F("<div class='i'><span class='l'>IP:</span><span class='v' id='wi'>...</span></div>");
    html += F("<div class='i'><span class='l'>RSSI:</span><span class='v' id='wr'>...</span></div>");
    html += F("<div class='i'><span class='l'>MQTT:</span><span class='b bw'><span id='ms'>...</span></span></div>");
    html += F("<div class='i'><span class='l'>Uptime:</span><span class='v' id='ut'>...</span></div>");
    html += F("<div class='i'><span class='l'>Батарея:</span><span class='v' id='eb'>...</span></div>");
    html += F("</div></div>");
    
    html += F("<div class='card'><h2>💻 ESP32</h2><div class='g'>");
    html += F("<div class='i'><span class='l'>Вільна:</span><span class='v' id='fh'>...</span></div>");
    html += F("<div class='i'><span class='l'>Загальна:</span><span class='v' id='th'>...</span></div>");
    html += F("<div class='i'><span class='l'>Макс:</span><span class='v' id='mh'>...</span></div>");
    html += F("<div class='i'><span class='l'>CPU:</span><span class='v' id='cf'>...</span></div>");
    html += F("</div></div>");
    
    html += F("<div class='card'><h2>🔋 Bluetti</h2>");
    html += F("<div class='i'><span class='l'>Статус:</span><span class='b bw'><span id='bs'>...</span></span></div>");
    html += F("<div class='g'>");
    html += F("<div class='i'><span class='l'>Батарея:</span><span class='v' id='bl'>...</span></div>");
    html += F("<div class='i'><span class='l'>AC:</span><span class='v' id='ac'>...</span><span id='acb-container'></span></div>");
    html += F("<div class='i'><span class='l'>DC:</span><span class='v' id='dc'>...</span><span id='dcb-container'></span></div>");
    html += F("<div class='i'><span class='l'>Вхід:</span><span class='v' id='ip'>...</span></div>");
    html += F("<div class='i'><span class='l'>Зарядка:</span><span class='v' id='cs-text'>...</span><span id='cs-container'></span></div>");
    html += F("<div class='i'><span class='l'>ECO:</span><span class='v' id='eco-text'>...</span></div>");
    html += F("<div class='i'><span class='l'>Power Lift:</span><span class='v' id='pl-text'>...</span></div>");
    html += F("<div class='i'><span class='l'>LED:</span><span class='v' id='led-text'>...</span>");
    html += F("<select id='led-select' style='display:none;'><option value='1'>Low</option><option value='2'>High</option>");
    html += F("<option value='3'>SOS</option><option value='4'>Off</option></select></div>");
    html += F("<div class='i'><span class='l'>ECO Таймер:</span><span class='v' id='ecs-text'>...</span>");
    html += F("<select id='ecs-select' style='display:none;'><option value='1'>1h</option><option value='2'>2h</option>");
    html += F("<option value='3'>3h</option><option value='4'>4h</option></select></div>");
    html += F("</div>");
    html += F("<button id='eco-btn' class='btn-eco' style='display:none;'>🌿 ECO Mode</button>");
    html += F("<button id='pl-btn' class='btn-eco' style='display:none;'>⚡ Power Lifting</button>");
    html += F("<button id='power-btn' class='btn-power' style='display:none;' onclick=\"if(confirm('Вимкнути Bluetti?'))");
    html += F("fetch('/power_off',{method:'POST'}).then(()=>alert('Bluetti вимкнено'));\">🔴 Power Off</button>");
    html += F("<button id='toggleBtn' class='btn-s' onclick=\"location.href='/toggle'\">Увімкнути</button>");
    html += F("</div></div></body></html>");
    
    return html;
}

String WebServerManager::buildConfigHtml() {
    extern char mqttServer[64];
    extern char bluettiMac[18];
    extern char wifiSsid[64];
    extern char wifiPassword[64];
    extern BluettiDevice bluetti;
    
    String html;
    html.reserve(2000);
    html += F("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>ESP32 Configuration</title>");
    html += F("<style>body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;}");
    html += F("input[type=text],input[type=password],input[type=number]{width:100%;padding:8px;margin:5px 0;box-sizing:border-box;}");
    html += F("button{background:#4CAF50;color:white;padding:10px 20px;border:none;cursor:pointer;margin:5px;}");
    html += F("button:hover{background:#45a049;} .back{background:#2196F3;}");
    html += F("h2{font-size:16px;margin-top:20px;color:#555;border-bottom:1px solid #ddd;padding-bottom:5px;}");
    html += F(".hint{font-size:12px;color:#777;margin-top:2px;}</style></head><body>");
    html += F("<h1>⚙️ Configuration</h1>");
    html += F("<form method='POST' action='/save_config'>");
    html += F("<h2>WiFi Settings</h2>");
    html += F("<label>WiFi SSID:</label>");
    html += F("<input type='text' name='wifi_ssid' value='");
    html += wifiSsid;
    html += F("' placeholder='Your WiFi Network'><br>");
    html += F("<label>WiFi Password:</label>");
    html += F("<input type='password' name='wifi_password' value='");
    html += wifiPassword;
    html += F("' placeholder='WiFi Password'><br>");
    html += F("<h2>MQTT Settings</h2>");
    html += F("<label>MQTT Server IP:</label>");
    html += F("<input type='text' name='mqtt_server' value='");
    html += mqttServer;
    html += F("' placeholder='192.168.1.100'><br>");
    html += F("<h2>Bluetti Settings</h2>");
    html += F("<label>Bluetti MAC Address:</label>");
    html += F("<input type='text' name='bluetti_mac' value='");
    html += bluettiMac;
    html += F("' placeholder='D1:4C:11:6B:6A:3D'><br>");
    html += F("<label>Інтервал опитування (секунди):</label>");
    html += F("<input type='number' name='update_interval' value='");
    html += String(bluetti.getUpdateInterval() / 1000);
    html += F("' min='5' max='300' placeholder='20'>");
    html += F("<div class='hint'>Рекомендовано: 20-30 сек (економить батарею)</div>");
    html += F("<button type='submit'>💾 Save</button>");
    html += F("<a href='/'><button type='button' class='back'>← Back</button></a>");
    html += F("</form></body></html>");
    return html;
}

String WebServerManager::buildUpdateHtml() {
    String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>Завантажити прошивку</title>");
    html += F("<style>body{font-family:Arial;max-width:600px;margin:20px auto;padding:20px;}");
    html += F("input[type=file]{width:100%;padding:8px;margin:5px 0;box-sizing:border-box;}");
    html += F("button{background:#4CAF50;color:white;padding:10px 20px;border:none;cursor:pointer;margin:5px;width:100%;}");
    html += F("button:hover{background:#45a049;} .back{background:#2196F3;}");
    html += F("#progress{display:none;margin:20px 0;}");
    html += F("#progressBar{width:100%;height:30px;background:#f0f0f0;border-radius:15px;overflow:hidden;}");
    html += F("#progressFill{height:100%;background:#4CAF50;width:0%;transition:width 0.3s;}");
    html += F("#status{margin:10px 0;font-weight:bold;}</style>");
    html += F("<script>");
    html += F("function uploadFile() {");
    html += F("var form=document.getElementById('uploadForm');");
    html += F("var file=document.getElementById('firmware').files[0];");
    html += F("if(!file){alert('Виберіть файл!');return;}");
    html += F("var xhr=new XMLHttpRequest();");
    html += F("var formData=new FormData();");
    html += F("formData.append('firmware',file);");
    html += F("xhr.upload.addEventListener('progress',function(e){");
    html += F("if(e.lengthComputable){");
    html += F("var percent=Math.round((e.loaded/e.total)*100);");
    html += F("document.getElementById('progressFill').style.width=percent+'%';");
    html += F("document.getElementById('status').textContent='Завантаження: '+percent+'%';");
    html += F("}});");
    html += F("xhr.addEventListener('load',function(){");
    html += F("if(xhr.status==200){");
    html += F("document.getElementById('status').textContent='✅ Завантаження завершено! Встановлення...';");
    html += F("document.getElementById('status').style.color='#4CAF50';");
    html += F("setTimeout(function(){location.href='/';},5000);");
    html += F("}else{");
    html += F("var errorMsg=xhr.responseText||xhr.statusText||'Невідома помилка';");
    html += F("document.getElementById('status').textContent='❌ Помилка: '+errorMsg+' (код: '+xhr.status+')';");
    html += F("document.getElementById('status').style.color='#f44336';");
    html += F("}});");
    html += F("xhr.addEventListener('error',function(){");
    html += F("document.getElementById('status').textContent='❌ Помилка завантаження (перевірте підключення)';");
    html += F("document.getElementById('status').style.color='#f44336';});");
    html += F("xhr.addEventListener('abort',function(){");
    html += F("document.getElementById('status').textContent='❌ Завантаження перервано';");
    html += F("document.getElementById('status').style.color='#f44336';});");
    html += F("xhr.addEventListener('timeout',function(){");
    html += F("document.getElementById('status').textContent='❌ Таймаут завантаження';");
    html += F("document.getElementById('status').style.color='#f44336';});");
    html += F("xhr.timeout=300000;");
    html += F("document.getElementById('progress').style.display='block';");
    html += F("document.getElementById('status').textContent='Початок завантаження...';");
    html += F("xhr.open('POST','/update');");
    html += F("xhr.send(formData);");
    html += F("return false;}");
    html += F("</script></head><body>");
    html += F("<h1>📤 Завантажити прошивку</h1>");
    html += F("<form id='uploadForm' onsubmit='return uploadFile();'>");
    html += F("<label>Виберіть файл прошивки (.bin):</label>");
    html += F("<input type='file' id='firmware' name='firmware' accept='.bin' required>");
    html += F("<button type='submit'>📤 Завантажити та встановити</button>");
    html += F("</form>");
    html += F("<div id='progress'>");
    html += F("<div id='progressBar'><div id='progressFill'></div></div>");
    html += F("<div id='status'></div>");
    html += F("</div>");
    html += F("<p><small>Файл прошивки: <code>.pio/build/lilygo-t-display/firmware.bin</code></small></p>");
    html += F("<a href='/'><button type='button' class='back'>← Back</button></a>");
    html += F("</body></html>");
    return html;
}

void WebServerManager::handleSaveConfig(AsyncWebServerRequest *request) {
    extern char mqttServer[64];
    extern char bluettiMac[18];
    extern char wifiSsid[64];
    extern char wifiPassword[64];
    extern BluettiDevice bluetti;
    
    bool changed = false;
    String newMqtt, newMac, newSsid, newPassword;
    
    if (request->hasParam("wifi_ssid", true)) {
        newSsid = request->getParam("wifi_ssid", true)->value();
        newSsid.trim();
        if (newSsid.length() > 0 && newSsid != String(wifiSsid)) {
            newSsid.toCharArray(wifiSsid, sizeof(wifiSsid));
            changed = true;
        }
    }
    
    if (request->hasParam("wifi_password", true)) {
        newPassword = request->getParam("wifi_password", true)->value();
        newPassword.trim();
        if (newPassword != String(wifiPassword)) {
            newPassword.toCharArray(wifiPassword, sizeof(wifiPassword));
            changed = true;
        }
    }
    
    if (request->hasParam("mqtt_server", true)) {
        newMqtt = request->getParam("mqtt_server", true)->value();
        newMqtt.trim();
        if (newMqtt.length() > 0 && newMqtt != String(mqttServer)) {
            newMqtt.toCharArray(mqttServer, sizeof(mqttServer));
            changed = true;
        }
    }
    
    if (request->hasParam("bluetti_mac", true)) {
        newMac = request->getParam("bluetti_mac", true)->value();
        newMac.trim();
        if (newMac.length() > 0 && newMac != String(bluettiMac)) {
            newMac.toCharArray(bluettiMac, sizeof(bluettiMac));
            changed = true;
        }
    }
    
    // Зберігаємо інтервал опитування
    if (request->hasParam("update_interval", true)) {
        int intervalSec = request->getParam("update_interval", true)->value().toInt();
        if (intervalSec >= 5 && intervalSec <= 300) {
            unsigned long intervalMs = intervalSec * 1000;
            bluetti.setUpdateInterval(intervalMs);
            Preferences prefs;
            prefs.begin("config", false);
            prefs.putULong("update_interval", intervalMs);
            prefs.end();
            changed = true;
        }
    }
    
    if (changed) {
        Preferences prefs;
        prefs.begin("config", false);
        if (newSsid.length() > 0) prefs.putString("wifi_ssid", wifiSsid);
        if (newPassword.length() >= 0) prefs.putString("wifi_password", wifiPassword);
        if (newMqtt.length() > 0) prefs.putString("mqtt_server", mqttServer);
        if (newMac.length() > 0) prefs.putString("bluetti_mac", bluettiMac);
        prefs.end();
        
        Serial.printf("Configuration saved - WiFi: %s, MQTT: %s, MAC: %s\n", wifiSsid, mqttServer, bluettiMac);
        
        if (newSsid.length() > 0 || newPassword.length() >= 0) {
            request->send(200, "text/html", 
                "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                "<meta http-equiv='refresh' content='3;url=/'>"
                "<title>Configuration Saved</title></head><body>"
                "<h1>✅ Configuration Saved</h1>"
                "<p>ESP32 will restart in 3 seconds to apply WiFi changes...</p>"
                "</body></html>");
            delay(1000);
            ESP.restart();
        } else {
            request->redirect("/config");
        }
    } else {
        request->redirect("/config");
    }
}

void WebServerManager::handleUpdateProgress(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static bool updateStarted = false;
    static size_t totalSize = 0;
    
    if (index == 0) {
        Serial.printf("\n=== Update Started ===\nFile: %s\n", filename.c_str());
        updateStarted = false;
        totalSize = 0;
        
        if (Update.isRunning()) {
            Update.abort();
        }
        
        // Перевірка розміру файлу
        // Custom OTA partition має 2MB (0x200000 = 2097152 bytes) на партицію
        // Залишаємо невеликий запас для безпеки
        const size_t MAX_OTA_SIZE = 2000000; // ~1.9MB
        if (request->contentLength() > 0) {
            totalSize = request->contentLength();
            if (totalSize > MAX_OTA_SIZE) {
                Serial.printf("ERROR: File too large! Max: %u bytes, Got: %u\n", MAX_OTA_SIZE, totalSize);
                return;
            }
        }
        
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Serial.println("ERROR: Update.begin() failed!");
            Update.printError(Serial);
            return;
        }
        updateStarted = true;
        Serial.println("Update.begin() OK");
    }
    
    if (updateStarted && len > 0) {
        size_t written = Update.write(data, len);
        if (written != len) {
            Serial.printf("ERROR: Update.write() failed! Written: %u, Expected: %u\n", written, len);
            Update.printError(Serial);
            Update.abort();
            updateStarted = false;
            return;
        }
        
        // Логуємо прогрес кожні 10%
        static unsigned long lastProgress = 0;
        if (totalSize > 0) {
            unsigned long progress = ((index + len) * 100) / totalSize;
            if (progress >= lastProgress + 10) {
                Serial.printf("Update progress: %lu%% (%u / %u bytes)\n", progress, index + len, totalSize);
                lastProgress = progress;
            }
        }
    }
    
    if (final) {
        if (updateStarted) {
            if (Update.end(true)) {
                Serial.printf("Update Success: %u bytes\n", Update.size());
            } else {
                Serial.println("Update.end() failed!");
                Update.printError(Serial);
                Update.abort();
            }
        } else {
            Serial.println("Update failed - not started!");
            Update.abort();
        }
    }
}

void WebServerManager::handleSetACOutput(AsyncWebServerRequest *request) {
    if (!bluetti || !bluetti->isConnected()) {
        request->send(400, "text/plain", "Bluetti not connected");
        return;
    }
    
    if (request->hasParam("state", true)) {
        String state = request->getParam("state", true)->value();
        bool newState = (state == "on");
        bool success = bluetti->setACOutput(newState);
        
        if (success) {
            Serial.printf("AC output set to: %s\n", newState ? "ON" : "OFF");
            request->redirect("/");
        } else {
            request->send(500, "text/plain", "Failed to set AC output");
        }
    } else {
        request->send(400, "text/plain", "Missing 'state' parameter");
    }
}

void WebServerManager::handleSetDCOutput(AsyncWebServerRequest *request) {
    if (!bluetti || !bluetti->isConnected()) {
        request->send(400, "text/plain", "Bluetti not connected");
        return;
    }
    
    if (request->hasParam("state", true)) {
        String state = request->getParam("state", true)->value();
        bool newState = (state == "on");
        bool success = bluetti->setDCOutput(newState);
        
        if (success) {
            Serial.printf("DC output set to: %s\n", newState ? "ON" : "OFF");
            request->redirect("/");
        } else {
            request->send(500, "text/plain", "Failed to set DC output");
        }
    } else {
        request->send(400, "text/plain", "Missing 'state' parameter");
    }
}

void WebServerManager::handleSetChargingSpeed(AsyncWebServerRequest *request) {
    if (!bluetti || !bluetti->isConnected()) {
        request->send(400, "text/plain", "Bluetti not connected");
        return;
    }
    
    if (request->hasParam("speed", true)) {
        String speedStr = request->getParam("speed", true)->value();
        uint8_t speed = 0;
        
        if (speedStr == "standard" || speedStr == "0") {
            speed = 0;
        } else if (speedStr == "silent" || speedStr == "1") {
            speed = 1;
        } else if (speedStr == "turbo" || speedStr == "2") {
            speed = 2;
        } else {
            request->send(400, "text/plain", "Invalid speed. Use 'standard' (0), 'silent' (1), or 'turbo' (2)");
            return;
        }
        
        bool success = bluetti->setChargingSpeed(speed);
        
        if (success) {
            const char* modeNames[] = {"STANDARD", "SILENT", "TURBO"};
            Serial.printf("Charging speed set to: %s\n", modeNames[speed]);
            request->redirect("/");
        } else {
            request->send(500, "text/plain", "Failed to set charging speed");
        }
    } else {
        request->send(400, "text/plain", "Missing 'speed' parameter");
    }
}

void WebServerManager::handleSetEcoMode(AsyncWebServerRequest *request) {
    if (bluetti) {
        String stateStr = "";
        if (request->hasParam("state", true)) {
            stateStr = request->getParam("state", true)->value();
        }
        bool state = (stateStr == "on" || stateStr == "ON" || stateStr == "1" || stateStr == "true");
        bool success = bluetti->setEcoMode(state);
        if (success) {
            request->send(200, "text/plain", state ? "ECO Mode ON" : "ECO Mode OFF");
        } else {
            request->send(500, "text/plain", "Failed to set ECO mode");
        }
    } else {
        request->send(400, "text/plain", "Bluetti not available");
    }
}

void WebServerManager::handleSetPowerLifting(AsyncWebServerRequest *request) {
    if (bluetti) {
        String stateStr = "";
        if (request->hasParam("state", true)) {
            stateStr = request->getParam("state", true)->value();
        }
        bool state = (stateStr == "on" || stateStr == "ON" || stateStr == "1" || stateStr == "true");
        bool success = bluetti->setPowerLifting(state);
        if (success) {
            request->send(200, "text/plain", state ? "Power Lifting ON" : "Power Lifting OFF");
        } else {
            request->send(500, "text/plain", "Failed to set Power Lifting");
        }
    } else {
        request->send(400, "text/plain", "Bluetti not available");
    }
}

void WebServerManager::handleSetLedMode(AsyncWebServerRequest *request) {
    if (bluetti) {
        String modeStr = "";
        if (request->hasParam("mode", true)) {
            modeStr = request->getParam("mode", true)->value();
        }
        uint8_t mode = 4;
        if (modeStr == "low" || modeStr == "Low" || modeStr == "1") { mode = 1; }
        else if (modeStr == "high" || modeStr == "High" || modeStr == "2") { mode = 2; }
        else if (modeStr == "sos" || modeStr == "SOS" || modeStr == "3") { mode = 3; }
        else if (modeStr == "off" || modeStr == "Off" || modeStr == "4") { mode = 4; }
        else if (modeStr == "on" || modeStr == "On") { mode = 2; } // convenience ON=High
        else { request->send(400, "text/plain", "Invalid mode"); return; }
        bool success = bluetti->setLedMode(mode);
        if (success) {
            const char* modeNames[] = {"", "Low", "High", "SOS", "Off"};
            request->send(200, "text/plain", String("LED Mode: ") + modeNames[mode]);
        } else {
            request->send(500, "text/plain", "Failed to set LED mode");
        }
    } else {
        request->send(400, "text/plain", "Bluetti not available");
    }
}

void WebServerManager::handleSetEcoShutdown(AsyncWebServerRequest *request) {
    if (bluetti) {
        String hoursStr = "";
        if (request->hasParam("hours", true)) {
            hoursStr = request->getParam("hours", true)->value();
        }
        uint8_t hours = hoursStr.toInt();
        if (hours < 1 || hours > 4) {
            request->send(400, "text/plain", "Invalid hours. Use 1-4");
            return;
        }
        bool success = bluetti->setEcoShutdown(hours);
        if (success) {
            request->send(200, "text/plain", String("ECO Shutdown: ") + hours + "h");
        } else {
            request->send(500, "text/plain", "Failed to set ECO shutdown");
        }
    } else {
        request->send(400, "text/plain", "Bluetti not available");
    }
}
