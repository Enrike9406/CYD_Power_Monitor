#pragma once
/*
 * device_pages.h
 * Pagina web /devices (administrador de dispositivos) + endpoints API JSON.
 * Requiere que device_manager.h ya este incluido antes de este archivo.
 */

#include <WebServer.h>

extern WebServer server; // declarado en el .ino principal

// ============================================
// PAGINA HTML - /devices
// ============================================
// La pagina /devices NO contiene datos de dispositivos en el HTML inicial
// (se rellena via fetch('/api/devices') desde el JS). Eso permite servirla
// desde una cache en RAM, evitando regenerar ~11KB de String concatenado en
// CADA peticion - antes eso bloqueaba al nucleo principal ~50-100ms por cada
// click en el menu, dando la sensacion de "se abre muy lento".
String getDevicesPage() {
    static String cached;
    static bool ready = false;
    if (ready) return cached;

    String page;
    page.reserve(11000);

    page = "<!DOCTYPE html><html lang='es'><head>";
    page += "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
    page += "<title>Dispositivos - CYD Power Monitor</title>";
    page += "<style>";
    page += ":root{--bg:#0a0e17;--card:#111827;--border:#1f2937;--text:#e5e7eb;--muted:#9ca3af;--accent:#00d4ff;--accent2:#3b82f6;--green:#10b981;--orange:#f59e0b;--red:#ef4444;--purple:#8b5cf6}";
    page += "*{margin:0;padding:0;box-sizing:border-box}";
    page += "body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);line-height:1.5}";
    page += ".topbar{background:linear-gradient(135deg,#0f172a 0%,#1e3a5f 100%);padding:0;border-bottom:2px solid var(--accent);position:sticky;top:0;z-index:100}";
    page += ".topbar-inner{max-width:1400px;margin:0 auto;padding:12px 20px;display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px}";
    page += ".logo{display:flex;align-items:center;gap:12px;min-width:0}";
    page += ".logo-icon{flex-shrink:0;width:36px;height:36px;background:linear-gradient(135deg,var(--accent),var(--accent2));border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:1.2em;font-weight:bold;color:#000}";
    page += ".logo-text{min-width:0}";
    page += ".logo-text h1{color:var(--accent);font-size:1.3em;letter-spacing:-0.5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}";
    page += ".logo-text span{color:var(--muted);font-size:0.75em}";
    page += ".nav{display:flex;gap:6px;flex-wrap:wrap}";
    page += ".nav a{color:var(--muted);text-decoration:none;padding:8px 14px;border-radius:6px;font-size:0.85em;font-weight:500;transition:all 0.2s;border:1px solid transparent;white-space:nowrap}";
    page += ".nav a:hover,.nav a.active{color:var(--accent);background:rgba(0,212,255,0.1);border-color:rgba(0,212,255,0.2)}";
    page += ".container{max-width:1400px;margin:0 auto;padding:20px}";
    page += ".card{background:var(--card);border:1px solid var(--border);border-radius:12px;overflow:hidden;margin-bottom:16px}";
    page += ".card-header{padding:14px 18px;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px}";
    page += ".card-header h2{font-size:0.95em;color:var(--accent);display:flex;align-items:center;gap:8px;font-weight:600}";
    page += ".card-header h2::before{content:'';width:3px;height:16px;background:var(--accent);border-radius:2px;flex-shrink:0}";
    page += ".card-body{padding:18px}";
    page += ".row{display:flex;gap:10px;flex-wrap:wrap;align-items:center;margin:8px 0}";
    page += "label{font-size:0.75em;color:var(--muted);display:block;margin-bottom:4px;text-transform:uppercase;letter-spacing:1px}";
    page += "input[type=text],input[type=number],select{background:#0f172a;border:1px solid var(--border);color:var(--text);padding:8px;border-radius:6px;font-size:0.9em;max-width:100%}";
    page += "input[type=time]{background:#0f172a;border:1px solid var(--border);color:var(--text);padding:7px;border-radius:6px}";
    page += "input[type=checkbox]{width:18px;height:18px;vertical-align:middle;flex-shrink:0}";
    page += "button{background:var(--accent);color:#000;border:none;padding:9px 18px;border-radius:6px;font-weight:bold;cursor:pointer;font-size:0.85em}";
    page += "button.secondary{background:var(--border);color:var(--text)}";
    page += "button.danger{background:var(--red);color:#fff}";
    page += "button.small{padding:5px 10px;font-size:0.75em}";
    page += ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:14px}";
    page += ".dcard{background:linear-gradient(135deg,#0f172a 0%,#1e293b 100%);border:1px solid var(--border);border-radius:10px;padding:14px;transition:transform 0.15s,border-color 0.15s;cursor:pointer;overflow:hidden;min-width:0}";
    page += ".dcard:hover{transform:translateY(-2px);border-color:var(--accent)}";
    page += ".dcard .top{display:flex;justify-content:space-between;align-items:flex-start;gap:8px;margin-bottom:6px}";
    page += ".dcard .who{display:flex;align-items:center;gap:8px;min-width:0}";
    page += ".dcard .icon{font-size:1.5em;flex-shrink:0}";
    page += ".dcard .name{font-weight:bold;font-size:0.95em;color:var(--text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}";
    page += ".dcard .ip{color:var(--muted);font-size:0.72em;word-break:break-all}";
    page += ".badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:0.62em;font-weight:bold;text-transform:uppercase;white-space:nowrap}";
    page += ".badge.tasmota{background:rgba(0,212,255,0.15);color:var(--accent)}";
    page += ".badge.openbeken{background:rgba(139,92,246,0.15);color:var(--purple)}";
    page += ".badge.ats{background:rgba(245,158,11,0.15);color:var(--orange)}";
    page += ".badges{display:flex;gap:4px;flex-wrap:wrap;margin:6px 0}";
    page += ".toggle{width:46px;height:24px;border-radius:12px;background:var(--border);position:relative;cursor:pointer;border:none;flex-shrink:0}";
    page += ".toggle.on{background:var(--green)}";
    page += ".toggle .dot{width:18px;height:18px;background:#fff;border-radius:50%;position:absolute;top:3px;left:3px;transition:.2s}";
    page += ".toggle.on .dot{left:25px}";
    page += ".metrics3{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-top:10px}";
    page += ".metrics3 .m{background:#0a0e17;border:1px solid var(--border);border-radius:6px;padding:6px 4px;text-align:center}";
    page += ".metrics3 .m .lbl{font-size:0.6em;color:var(--muted);text-transform:uppercase}";
    page += ".metrics3 .m .val{font-size:1em;font-weight:bold;color:var(--accent);margin-top:2px}";
    page += ".metrics3 .m .val.stale{color:var(--muted)}";
    page += ".no-metrics{font-size:0.72em;color:var(--muted);margin-top:10px;font-style:italic}";
    page += ".meta{font-size:0.72em;color:var(--muted);margin:4px 0}";
    page += "input[type=range]{width:100%}";
    page += ".modal-bg{display:none;position:fixed;inset:0;background:rgba(0,0,0,0.6);z-index:200;align-items:center;justify-content:center;padding:16px}";
    page += ".modal-bg.show{display:flex}";
    page += ".modal{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:20px;max-width:420px;width:100%;max-height:88vh;overflow-y:auto}";
    page += ".modal h3{color:var(--accent);margin-bottom:12px}";
    page += ".field{margin-bottom:10px}";
    page += ".field label{margin-bottom:4px}";
    page += ".field input,.field select{width:100%}";
    page += ".two-col{display:grid;grid-template-columns:1fr 1fr;gap:10px}";
    page += ".footer{text-align:center;padding:24px;color:var(--muted);font-size:0.8em;border-top:1px solid var(--border);margin-top:20px}";
    page += ".empty{color:var(--muted);text-align:center;padding:30px}";
    page += "@media (max-width:480px){.logo-text h1{font-size:1.05em}.nav a{padding:6px 10px;font-size:0.78em}.grid{grid-template-columns:1fr}}";
    page += "</style></head><body>";

    page += "<div class='topbar'><div class='topbar-inner'>";
    page += "<div class='logo'><div class='logo-icon'>⚡</div><div class='logo-text'>";
    page += "<h1>CYD Power Monitor</h1><span>Administrador de Dispositivos</span></div></div>";
    page += "<div class='nav'><a href='/'>Dashboard</a><a href='/history'>Histórico ATS</a>";
    page += "<a href='/devices' class='active'>Dispositivos</a><a href='/ota'>Actualización</a></div>";
    page += "</div></div><div class='container'>";

    // ---- Configuracion de hora (necesaria para que los horarios disparen bien) ----
    page += "<div class='card'><div class='card-header'><h2>Configuración de Hora y Modo Nocturno</h2></div><div class='card-body'>";
    page += "<div class='row'>";
    page += "<div><label>Zona horaria (UTC)</label><input type='number' id='utcOffset' style='width:80px' step='1'></div>";
    page += "<div><label>Modo nocturno: inicio</label><input type='time' id='nightStart'></div>";
    page += "<div><label>Modo nocturno: fin</label><input type='time' id='nightEnd'></div>";
    page += "<div style='align-self:flex-end'><button onclick='saveConfig()'>Guardar</button></div>";
    page += "<div style='align-self:flex-end'><button class='secondary' onclick='syncPhoneTime()'>Sincronizar con este teléfono</button></div>";
    page += "<div class='meta' id='ntpStatus' style='flex-basis:100%'>Estado hora: cargando...</div>";
    page += "<div class='meta' style='flex-basis:100%'>La hora se sincroniza automáticamente con tu teléfono cada vez que abres esta página (no depende de internet). También se intenta por NTP en segundo plano.</div>";
    page += "</div></div></div>";

    // ---- Panel Dispositivos ----
    page += "<div class='card'><div class='card-header'><h2>Dispositivos</h2><button onclick=\"openAddModal()\">+ Añadir Dispositivo</button></div>";
    page += "<div class='card-body'><div class='grid' id='deviceGrid'><div class='empty'>Cargando...</div></div></div>";
    page += "</div>";

    page += "<div class='card'><div class='card-header'><h2>Actividad Reciente</h2></div>";
    page += "<div class='card-body'><div id='eventsList'><div class='empty'>Cargando...</div></div></div>";
    page += "</div>";

    page += "<div class='footer'>CYD Power Monitor v";
    page += FIRMWARE_VERSION;
    page += "</div></div>";

    // ---- Modal Añadir/Editar ----
    page += "<div class='modal-bg' id='modalBg'><div class='modal'>";
    page += "<h3 id='modalTitle'>Añadir Dispositivo</h3>";
    page += "<input type='hidden' id='fId'>";
    page += "<div class='field'><label>Nombre</label><input type='text' id='fName' placeholder='Refrigerador'></div>";
    page += "<div class='field'><label>Dirección IP</label><input type='text' id='fIp' placeholder='192.168.1.50'></div>";
    page += "<div class='field'><label>Firmware</label><select id='fType'>";
    page += "<option value='tasmota'>Tasmota</option><option value='openbeken'>OpenBeken</option></select></div>";
    page += "<div class='field'><label>Categoría</label><select id='fCategory'>";
    page += "<option value='cocina'>🍳 Cocina</option><option value='luz'>💡 Luz</option>";
    page += "<option value='bomba'>🚰 Bomba</option><option value='exterior'>🌳 Exterior</option>";
    page += "<option value='refrigerador'>🧊 Refrigerador</option><option value='entretenimiento'>📺 Entretenimiento</option>";
    page += "<option value='otro'>🔌 Otro</option></select></div>";
    page += "<div class='field'><label><input type='checkbox' id='fDimmable'> Regulable (brillo/dimmer)</label></div>";
    page += "<div class='field'><label><input type='checkbox' id='fEnergyMon'> Tiene medición de energía (voltaje/corriente/potencia)</label></div>";
    page += "<div class='field'><label><input type='checkbox' id='fScheduleEnabled' onchange='onScheduleToggle()'> Horario activo</label></div>";
    page += "<div id='scheduleFields' style='display:none'>";
    page += "<div class='two-col'><div><label>Encender a las</label><input type='time' id='fOnTime'></div>";
    page += "<div><label>Apagar a las</label><input type='time' id='fOffTime'></div></div>";
    page += "</div>";
    page += "<div class='field'><label>Automatización según fuente ATS</label><select id='fAtsMode'>";
    page += "<option value='0'>Ninguna</option>";
    page += "<option value='1'>Apagar automáticamente en generador</option>";
    page += "<option value='2'>Encender en red / apagar en generador</option>";
    page += "<option value='3'>Modo nocturno: solo de noche Y con red eléctrica</option>";
    page += "</select></div>";
    page += "<div class='row' style='justify-content:flex-end;margin-top:15px'>";
    page += "<button class='secondary' onclick='closeModal()'>Cancelar</button>";
    page += "<button onclick='saveDevice()'>Guardar</button>";
    page += "</div></div></div>";

    // ---- JavaScript ----
    page += "<script>";
    page += "const ICONS={cocina:'🍳',luz:'💡',bomba:'🚰',exterior:'🌳',refrigerador:'🧊',entretenimiento:'📺',otro:'🔌'};";
    page += "const ATS_LABEL={1:'Apaga en generador',2:'Solo en red',3:'Modo nocturno'};";
    page += "let devicesCache=[];";

    page += "function loadConfig(){fetch('/api/devconfig').then(r=>r.json()).then(d=>{";
    page += "document.getElementById('utcOffset').value=d.utcOffset;";
    page += "document.getElementById('nightStart').value=pad(d.nightStartHour)+':'+pad(d.nightStartMin);";
    page += "document.getElementById('nightEnd').value=pad(d.nightEndHour)+':'+pad(d.nightEndMin);";
    page += "document.getElementById('ntpStatus').textContent='Estado hora: '+(d.ntpSynced?('sincronizada ('+d.currentTime+')'):'sin sincronizar todavia');";
    page += "});}";

    page += "function saveConfig(){";
    page += "const [nsh,nsm]=document.getElementById('nightStart').value.split(':').map(Number);";
    page += "const [neh,nem]=document.getElementById('nightEnd').value.split(':').map(Number);";
    page += "const body={utcOffset:parseInt(document.getElementById('utcOffset').value)||-5,";
    page += "nightStartHour:nsh||0,nightStartMin:nsm||0,nightEndHour:neh||0,nightEndMin:nem||0};";
    page += "fetch('/api/devconfig',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(()=>loadConfig());";
    page += "}";

    page += "function pad(n){return String(n).padStart(2,'0');}";

    page += "function loadDevices(){fetch('/api/devices').then(r=>r.json()).then(d=>{devicesCache=d;renderDevices(d);});}";

    page += "function renderDevices(list){";
    page += "const grid=document.getElementById('deviceGrid');";
    page += "if(list.length===0){grid.innerHTML=\"<div class='empty'>No hay dispositivos añadidos todavia</div>\";return;}";
    page += "grid.innerHTML=list.map(d=>{";
    page += "const icon=ICONS[d.category]||'🔌';";
    page += "let html=\"<div class='dcard' onclick='openEditModal(\\\"\"+d.id+\"\\\")'><div class='top'>\";";
    page += "html+=\"<div class='who'><span class='icon'>\"+icon+\"</span><div style='min-width:0'><div class='name'>\"+d.name+\"</div><div class='ip'>\"+d.ip+\"</div></div></div>\";";
    page += "html+=\"<button class='toggle\"+(d.state?' on':'')+\"' onclick='event.stopPropagation();toggleDevice(\\\"\"+d.id+\"\\\")'><span class='dot'></span></button></div>\";";
    page += "html+=\"<div class='badges'><span class='badge \"+d.type+\"'>\"+d.type+\"</span>\";";
    page += "if(!d.online){html+=\"<span class='badge' style='background:rgba(239,68,68,.15);color:var(--red)'>⚠️ Sin conexión</span>\";}";
    page += "if(d.scheduleEnabled){html+=\"<span class='badge' style='background:rgba(59,130,246,.15);color:var(--accent2)'>⏰ \"+pad(d.onHour)+':'+pad(d.onMin)+'-'+pad(d.offHour)+':'+pad(d.offMin)+\"</span>\";}";
    page += "if(d.atsMode>0){html+=\"<span class='badge ats'>⚡ \"+ATS_LABEL[d.atsMode]+\"</span>\";}";
    page += "html+=\"</div>\";";
    page += "if(d.hasEnergyMonitoring){";
    page += "  const stale=(d.metricsAgeSec>30||!d.metricsValid);";
    page += "  const cls=stale?'val stale':'val';";
    page += "  html+=\"<div class='metrics3'>\";";
    page += "  html+=\"<div class='m'><div class='lbl'>Voltaje</div><div class='\"+cls+\"'>\"+(d.metricsValid?d.voltage.toFixed(0):'--')+\"<small>V</small></div></div>\";";
    page += "  html+=\"<div class='m'><div class='lbl'>Corriente</div><div class='\"+cls+\"'>\"+(d.metricsValid?d.current.toFixed(2):'--')+\"<small>A</small></div></div>\";";
    page += "  html+=\"<div class='m'><div class='lbl'>Potencia</div><div class='\"+cls+\"'>\"+(d.metricsValid?d.power.toFixed(0):'--')+\"<small>W</small></div></div>\";";
    page += "  html+=\"</div>\";";
    page += "}else{";
    page += "  html+=\"<div class='no-metrics'>Este modelo no reporta consumo (solo ON/OFF)</div>\";";
    page += "}";
    page += "if(d.dimmable){html+=\"<div class='meta' onclick='event.stopPropagation()'>Brillo: <input type='range' min='0' max='100' value='\"+d.brightness+\"' onchange='setBrightness(\\\"\"+d.id+\"\\\",this.value)'></div>\";}";
    page += "html+=\"</div>\";";
    page += "return html;";
    page += "}).join('');";
    page += "}";

    page += "function toggleDevice(id){fetch('/api/devices/toggle?id='+encodeURIComponent(id),{method:'POST'}).then(()=>loadDevices());}";
    page += "function setBrightness(id,val){fetch('/api/devices/brightness?id='+encodeURIComponent(id)+'&value='+val,{method:'POST'});}";
    page += "function deleteDevice(id){if(!confirm('¿Eliminar este dispositivo?'))return;fetch('/api/devices/delete?id='+encodeURIComponent(id),{method:'POST'}).then(()=>{closeModal();loadDevices();});}";

    page += "function onScheduleToggle(){document.getElementById('scheduleFields').style.display=document.getElementById('fScheduleEnabled').checked?'block':'none';}";

    page += "function openAddModal(){";
    page += "document.getElementById('modalTitle').textContent='Añadir Dispositivo';";
    page += "['fId','fName','fIp'].forEach(id=>document.getElementById(id).value='');";
    page += "document.getElementById('fType').value='tasmota';document.getElementById('fCategory').value='otro';";
    page += "document.getElementById('fDimmable').checked=false;document.getElementById('fEnergyMon').checked=false;";
    page += "document.getElementById('fScheduleEnabled').checked=false;document.getElementById('fAtsMode').value='0';";
    page += "document.getElementById('fOnTime').value='06:00';document.getElementById('fOffTime').value='22:00';";
    page += "onScheduleToggle();";
    page += "document.getElementById('modalBg').classList.add('show');";
    page += "}";

    page += "function openEditModal(id){";
    page += "const d=devicesCache.find(x=>x.id===id);if(!d)return;";
    page += "document.getElementById('modalTitle').innerHTML='Editar Dispositivo <button class=\\'small danger\\' style=\\'float:right\\' onclick=\\'deleteDevice(\"'+id+'\")\\'>Eliminar</button>';";
    page += "document.getElementById('fId').value=d.id;document.getElementById('fName').value=d.name;document.getElementById('fIp').value=d.ip;";
    page += "document.getElementById('fType').value=d.type;document.getElementById('fCategory').value=d.category;";
    page += "document.getElementById('fDimmable').checked=d.dimmable;document.getElementById('fEnergyMon').checked=d.hasEnergyMonitoring;";
    page += "document.getElementById('fScheduleEnabled').checked=d.scheduleEnabled;";
    page += "document.getElementById('fOnTime').value=pad(d.onHour)+':'+pad(d.onMin);";
    page += "document.getElementById('fOffTime').value=pad(d.offHour)+':'+pad(d.offMin);";
    page += "document.getElementById('fAtsMode').value=String(d.atsMode);";
    page += "onScheduleToggle();";
    page += "document.getElementById('modalBg').classList.add('show');";
    page += "}";

    page += "function closeModal(){document.getElementById('modalBg').classList.remove('show');}";

    page += "function saveDevice(){";
    page += "const [oh,om]=document.getElementById('fOnTime').value.split(':').map(Number);";
    page += "const [fh,fm]=document.getElementById('fOffTime').value.split(':').map(Number);";
    page += "const body={id:document.getElementById('fId').value,name:document.getElementById('fName').value,ip:document.getElementById('fIp').value,";
    page += "type:document.getElementById('fType').value,category:document.getElementById('fCategory').value,";
    page += "dimmable:document.getElementById('fDimmable').checked,hasEnergyMonitoring:document.getElementById('fEnergyMon').checked,";
    page += "scheduleEnabled:document.getElementById('fScheduleEnabled').checked,";
    page += "onHour:oh||0,onMin:om||0,offHour:fh||0,offMin:fm||0,atsMode:parseInt(document.getElementById('fAtsMode').value)};";
    page += "if(!body.name||!body.ip){alert('Nombre e IP son obligatorios');return;}";
    page += "const url=body.id?'/api/devices/update':'/api/devices/add';";
    page += "fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(()=>{closeModal();loadDevices();});";
    page += "}";

    page += "function syncPhoneTime(){";
    page += "const epoch=Math.floor(Date.now()/1000);";
    page += "fetch('/api/settime',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({epoch:epoch})}).then(()=>loadConfig());";
    page += "}";

    page += "function loadEvents(){fetch('/api/deviceevents').then(r=>r.json()).then(list=>{";
    page += "const el=document.getElementById('eventsList');";
    page += "if(list.length===0){el.innerHTML=\"<div class='empty'>Sin actividad registrada todavia</div>\";return;}";
    page += "el.innerHTML=list.slice().reverse().map(e=>{";
    page += "const icon=e.turnedOn?'🟢':'🔴';";
    page += "const action=e.turnedOn?'encendido':'apagado';";
    page += "return \"<div class='meta' style='padding:6px 0;border-bottom:1px solid var(--border)'>\"+icon+\" <b style='color:var(--text)'>\"+e.deviceName+\"</b> \"+action+\" — \"+e.source+\" · \"+e.timestamp+\"</div>\";";
    page += "}).join('');";
    page += "});}";

    page += "loadConfig();loadDevices();loadEvents();syncPhoneTime();setInterval(loadDevices,4000);setInterval(loadConfig,20000);setInterval(loadEvents,10000);";
    page += "</script></body></html>";
    return page;
}

// ============================================
// API HANDLERS
// ============================================
void apiDevicesList() {
    DynamicJsonDocument doc(7168);
    JsonArray arr = doc.to<JsonArray>();
    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    for (int i = 0; i < deviceCount; i++) {
        JsonObject o = arr.createNestedObject();
        o["id"] = devices[i].id;
        o["name"] = devices[i].name;
        o["ip"] = devices[i].ip;
        o["type"] = deviceTypeToStr(devices[i].type);
        o["category"] = devices[i].category;
        o["dimmable"] = devices[i].dimmable;
        o["brightness"] = devices[i].brightness;
        o["state"] = devices[i].state;
        o["scheduleEnabled"] = devices[i].scheduleEnabled;
        o["onHour"] = devices[i].onHour;
        o["onMin"] = devices[i].onMin;
        o["offHour"] = devices[i].offHour;
        o["offMin"] = devices[i].offMin;
        o["atsMode"] = (int)devices[i].atsMode;
        o["hasEnergyMonitoring"] = devices[i].hasEnergyMonitoring;
        o["voltage"] = devices[i].lastVoltage;
        o["current"] = devices[i].lastCurrent;
        o["power"] = devices[i].lastPower;
        o["metricsValid"] = devices[i].metricsValid;
        o["metricsAgeSec"] = devices[i].lastMetricsUpdate == 0 ? 9999 : (millis() - devices[i].lastMetricsUpdate) / 1000;
        o["online"] = devices[i].pollFailures < 3;
    }
    xSemaphoreGive(devicesMutex);
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void apiDeviceAdd() {
    if (deviceCount >= MAX_DEVICES) {
        server.send(400, "application/json", "{\"error\":\"Limite de dispositivos alcanzado\"}");
        return;
    }
    String body = server.arg("plain");
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, body)) {
        server.send(400, "application/json", "{\"error\":\"JSON invalido\"}");
        return;
    }
    SmartDevice d;
    d.id = makeDeviceId();
    d.name = doc["name"].as<String>();
    d.ip = doc["ip"].as<String>();
    d.type = deviceTypeFromStr(doc["type"].as<String>());
    d.category = doc["category"].as<String>();
    d.dimmable = doc["dimmable"] | false;
    d.hasEnergyMonitoring = doc["hasEnergyMonitoring"] | false;
    d.scheduleEnabled = doc["scheduleEnabled"] | false;
    d.onHour = doc["onHour"] | 6;
    d.onMin = doc["onMin"] | 0;
    d.offHour = doc["offHour"] | 22;
    d.offMin = doc["offMin"] | 0;
    d.atsMode = (AtsAutoMode)(int)(doc["atsMode"] | 0);
    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    devices[deviceCount++] = d;
    xSemaphoreGive(devicesMutex);
    devicesSave();
    server.send(200, "application/json", "{\"ok\":true,\"id\":\"" + d.id + "\"}");
}

void apiDeviceUpdate() {
    String body = server.arg("plain");
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, body)) {
        server.send(400, "application/json", "{\"error\":\"JSON invalido\"}");
        return;
    }
    String id = doc["id"].as<String>();
    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    int idx = findDeviceIndexById(id);
    if (idx < 0) {
        xSemaphoreGive(devicesMutex);
        server.send(404, "application/json", "{\"error\":\"Dispositivo no encontrado\"}");
        return;
    }
    SmartDevice& d = devices[idx];
    d.name = doc["name"].as<String>();
    d.ip = doc["ip"].as<String>();
    d.type = deviceTypeFromStr(doc["type"].as<String>());
    d.category = doc["category"].as<String>();
    d.dimmable = doc["dimmable"] | false;
    d.hasEnergyMonitoring = doc["hasEnergyMonitoring"] | false;
    d.scheduleEnabled = doc["scheduleEnabled"] | false;
    d.onHour = doc["onHour"] | 6;
    d.onMin = doc["onMin"] | 0;
    d.offHour = doc["offHour"] | 22;
    d.offMin = doc["offMin"] | 0;
    d.atsMode = (AtsAutoMode)(int)(doc["atsMode"] | 0);
    d.lastFiredMinuteOn = -1;
    d.lastFiredMinuteOff = -1;
    xSemaphoreGive(devicesMutex);
    devicesSave();
    server.send(200, "application/json", "{\"ok\":true}");
}

void apiDeviceDelete() {
    if (!server.hasArg("id")) { server.send(400, "application/json", "{\"error\":\"falta id\"}"); return; }
    String id = server.arg("id");
    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    int idx = findDeviceIndexById(id);
    if (idx < 0) {
        xSemaphoreGive(devicesMutex);
        server.send(404, "application/json", "{\"error\":\"no encontrado\"}");
        return;
    }
    for (int i = idx; i < deviceCount - 1; i++) devices[i] = devices[i + 1];
    deviceCount--;
    xSemaphoreGive(devicesMutex);
    devicesSave();
    server.send(200, "application/json", "{\"ok\":true}");
}

void apiDeviceToggle() {
    if (!server.hasArg("id")) { server.send(400, "application/json", "{\"error\":\"falta id\"}"); return; }
    String id = server.arg("id");

    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    int idx = findDeviceIndexById(id);
    if (idx < 0) {
        xSemaphoreGive(devicesMutex);
        server.send(404, "application/json", "{\"error\":\"no encontrado\"}");
        return;
    }
    String ip = devices[idx].ip;
    bool newState = !devices[idx].state;
    String name = devices[idx].name;
    xSemaphoreGive(devicesMutex);

    bool ok = httpCmndPower(ip, newState);

    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    idx = findDeviceIndexById(id);
    if (idx >= 0 && ok) {
        logDeviceEvent(name, newState, "Manual");
        devices[idx].state = newState;
    }
    xSemaphoreGive(devicesMutex);

    devicesSave();
    server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"fallo de comunicacion\"}");
}

void apiDeviceBrightness() {
    if (!server.hasArg("id") || !server.hasArg("value")) { server.send(400, "application/json", "{\"error\":\"faltan parametros\"}"); return; }
    String id = server.arg("id");
    int value = server.arg("value").toInt();

    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    int idx = findDeviceIndexById(id);
    if (idx < 0) {
        xSemaphoreGive(devicesMutex);
        server.send(404, "application/json", "{\"error\":\"no encontrado\"}");
        return;
    }
    String ip = devices[idx].ip;
    bool dimmable = devices[idx].dimmable;
    xSemaphoreGive(devicesMutex);

    bool ok = dimmable && httpCmndDimmer(ip, value);

    xSemaphoreTake(devicesMutex, portMAX_DELAY);
    idx = findDeviceIndexById(id);
    if (idx >= 0 && ok) {
        devices[idx].brightness = constrain(value, 0, 100);
        if (value > 0) devices[idx].state = true;
    }
    xSemaphoreGive(devicesMutex);

    devicesSave();
    server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void apiDevConfigGet() {
    StaticJsonDocument<300> doc;
    doc["utcOffset"] = utcOffsetHours;
    doc["ntpSynced"] = ntpSynced;
    doc["nightStartHour"] = nightStartHour;
    doc["nightStartMin"] = nightStartMin;
    doc["nightEndHour"] = nightEndHour;
    doc["nightEndMin"] = nightEndMin;
    struct tm ti;
    if (getLocalTimeInfo(ti)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
        doc["currentTime"] = buf;
    } else {
        doc["currentTime"] = "--:--";
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void apiDevConfigSet() {
    String body = server.arg("plain");
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, body)) {
        server.send(400, "application/json", "{\"error\":\"JSON invalido\"}");
        return;
    }
    int newOffset = doc["utcOffset"] | utcOffsetHours;
    bool changed = (newOffset != utcOffsetHours);
    utcOffsetHours = newOffset;
    nightStartHour = doc["nightStartHour"] | nightStartHour;
    nightStartMin = doc["nightStartMin"] | nightStartMin;
    nightEndHour = doc["nightEndHour"] | nightEndHour;
    nightEndMin = doc["nightEndMin"] | nightEndMin;
    utcOffsetSave();
    if (changed) ntpTimeBegin();
    server.send(200, "application/json", "{\"ok\":true}");
}

void apiSetTime() {
    String body = server.arg("plain");
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, body) || !doc.containsKey("epoch")) {
        server.send(400, "application/json", "{\"error\":\"JSON invalido\"}");
        return;
    }
    long epoch = doc["epoch"];
    applyManualTime(epoch);
    server.send(200, "application/json", "{\"ok\":true}");
}

void apiDeviceEvents() {
    DynamicJsonDocument doc(3072);
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < deviceEventCount; i++) {
        int idx = (deviceEventCount < MAX_DEVICE_EVENTS) ? i : (deviceEventIndex + i) % MAX_DEVICE_EVENTS;
        JsonObject o = arr.createNestedObject();
        o["timestamp"] = formatRealTimestamp(deviceEvents[idx].timestamp);
        o["deviceName"] = deviceEvents[idx].deviceName;
        o["turnedOn"] = deviceEvents[idx].turnedOn;
        o["source"] = deviceEvents[idx].source;
    }
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void deviceManagerWebBegin() {
    server.on("/devices", []() {
        // La pagina de /devices NO depende de los dispositivos concretos - el
        // JS hace fetch a /api/devices y rellena la grid. Asi que la cacheamos
        // una sola vez en RAM y la enviamos directamente. Antes, cada GET
        // reconstruia ~11KB de HTML via concatenacion, que ademas reventaba
        // varios String temporales y fragmentaba la heap.
        static String cachedPage;
        static bool cached = false;
        if (!cached) {
            cachedPage = getDevicesPage();
            cached = true;
        }
        server.send(200, "text/html", cachedPage);
    });
    server.on("/api/devices", HTTP_GET, apiDevicesList);
    server.on("/api/devices/add", HTTP_POST, apiDeviceAdd);
    server.on("/api/devices/update", HTTP_POST, apiDeviceUpdate);
    server.on("/api/devices/delete", HTTP_POST, apiDeviceDelete);
    server.on("/api/devices/toggle", HTTP_POST, apiDeviceToggle);
    server.on("/api/devices/brightness", HTTP_POST, apiDeviceBrightness);
    server.on("/api/devconfig", HTTP_GET, apiDevConfigGet);
    server.on("/api/devconfig", HTTP_POST, apiDevConfigSet);
    server.on("/api/settime", HTTP_POST, apiSetTime);
    server.on("/api/deviceevents", HTTP_GET, apiDeviceEvents);
}

