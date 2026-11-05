"use strict";
// No network dependency; identifiers are decimal strings, times/UID on wire BigInt.
function crc32(bytes) { let c=0xffffffff; for(const b of bytes) { c^=b; for(let i=0;i<8;i++) c=(c>>>1)^((c&1)?0xedb88320:0); } return (c^0xffffffff)>>>0; }
function decodePacket(buffer) {
  const b=new Uint8Array(buffer),d=new DataView(buffer);
  if(b.length<40||b.length>6184||b[0]!==83||b[1]!==73||b[2]!==1||d.getUint16(4,true)!==b.length||d.getUint32(b.length-4,true)!==crc32(b.subarray(0,-4))) throw Error("Неверный пакет/CRC");
  const p={type:b[3],delivery:d.getUint16(6,true),uid:d.getBigUint64(8,true).toString(),boot:d.getBigUint64(16,true).toString(),stream:d.getBigUint64(24,true).toString(),samples:[]};
  if(p.type<1||p.type>5) throw Error("Неизвестный тип пакета");
  if(p.type===1) {const n=d.getUint16(36,true);if(n<1||n>8||b.length!==42+n*46) throw Error("Неверная пачка");for(let i=0;i<n;i++){const o=38+46*i;p.samples.push({seq:d.getUint32(o,true),t_us:d.getBigUint64(o+4,true),raw:Array.from({length:7},(_,k)=>d.getInt16(o+12+2*k,true)),q:Array.from({length:4},(_,k)=>d.getFloat32(o+26+4*k,true)),flags:d.getUint32(o+42,true)});}}
  return p;
}
if(typeof module!=="undefined") module.exports={crc32,decodePacket};
if(typeof document!=="undefined") {
const $=id=>document.getElementById(id);let ws,info,verified=false,serial=0,busy=false;
const client="web-"+Math.random().toString(36).slice(2),pending=new Map();
function show(x){$("result").textContent=typeof x==="string"?x:JSON.stringify(x,null,2);}
function rejectPending(error){for(const p of pending.values()){clearTimeout(p.timer);p.reject(error);}pending.clear();}
function send(command,args={}) { return new Promise((resolve,reject)=>{if(!ws||ws.readyState!==1) return reject(Error("Нет соединения"));const request_id=client+"-"+(++serial); const m={request_id,client_id:client,command,...args};if(info){m.device_id=info.device_id;m.device_uid=info.device_uid;}const timer=setTimeout(()=>{pending.delete(request_id);reject(Error("Таймаут ответа"));},3000);pending.set(request_id,{resolve,reject,timer});ws.send(JSON.stringify(m));}); }
async function action(command,args={}){try{if(!verified)throw Error("Сначала сверить номер/UID");const r=await send(command,args);show(r);return r;}catch(e){show(e.message);}}
$("connect").onclick=()=>{
  const previous=ws;ws=null;verified=false;info=null;
  rejectPending(Error("Переподключение"));if(previous)previous.close();
  $("identity").textContent="Проверка устройства…";
  $("connection").textContent="Подключение к /ws…";$("connect").disabled=true;
  const socket=new WebSocket((location.protocol==="https:"?"wss://":"ws://")+location.host+"/ws");
  ws=socket;socket.binaryType="arraybuffer";let failure="";
  socket.onopen=async()=>{
    if(socket!==ws)return;
    try{
      const r=await send("get_info");if(socket!==ws)return;
      if(r.result.device_id!==Number($("expected").value))throw Error("НЕ ТОТ КОМПЛЕКТ: запись/команды запрещены");
      info=r.result;verified=true;$("identity").textContent="Комплект "+info.device_id+" · UID "+info.device_uid;
      $("connection").textContent="Соединено · "+info.sdk_version;$("connect").disabled=false;
    }catch(e){if(socket!==ws)return;failure=e.message;show(failure);socket.close();}
  };
  socket.onerror=()=>{
    if(socket!==ws)return;
    failure="Не удалось установить WebSocket /ws. Проверьте подключение к SNAR_IMU и адрес http://192.168.4.1/.";
    $("connection").textContent=failure;show(failure);
  };
  socket.onclose=e=>{
    if(socket!==ws)return;
    verified=false;$("connect").disabled=false;$("identity").textContent="Не подключено";
    const detail="WebSocket закрыт: код "+e.code+(e.reason?", "+e.reason:"");
    rejectPending(Error(failure||detail));
    $("connection").textContent=(failure?failure+" ":"")+detail+". Опыт на модуле НЕ завершён автоматически.";
  };
  socket.onmessage=e=>{
    if(socket!==ws)return;
    try{
      if(typeof e.data!=="string"){decodePacket(e.data);return;}
      const r=JSON.parse(e.data),p=pending.get(r.request_id);
      if(p){clearTimeout(p.timer);pending.delete(r.request_id);if(r.ok)p.resolve(r);else p.reject(Error(r.error?r.error.code+": "+r.error.message:"Ошибка команды"));}
      else show(r);
    }catch(error){show("Некорректный ответ: "+error.message);}
  };
};
setInterval(async()=>{
  if(!verified||busy)return;
  busy=true;
  try {
    const r=await send("get_status"),s=r.result,a=s.rpy_deg;
    ["roll","pitch","yaw"].forEach((id,i)=>$(id).textContent=a?a[i].toFixed(1)+"°":"—");
    const temperature=s.sensor_ok&&Number.isFinite(s.temperature_c)?s.temperature_c.toFixed(2):"—";
    $("status").textContent=`Сенсор: ${s.sensor_ok?"OK":s.sensor_error}; ориентация: ${s.orientation_ready?"готова":"НЕ ГОТОВА"}\nКалибровка: ${s.calibrated?"CAL":"UNCALIBRATED"}; температура: ${temperature} °C\nОпыт: ${s.experiment?"АКТИВЕН (файлы пишет регистратор ПК)":"нет"}; Wi-Fi станций: ${s.wifi_stations}; WS: ${s.ws_receivers}\nHeap: ${s.free_heap}; минимум: ${s.minimum_free_heap} байт\nFIFO: ${s.fifo_overflows}; IRQ: ${s.irq_overflows}; sync: ${s.sync_errors}; I2C: ${s.read_errors}\nLCD: ${s.lcd_ok?"init OK (проверьте изображение)":"ERR/отсутствует"}; NVS: ${s.nvs_status}\nПараметры: ${JSON.stringify(s.parameters)}`;
    $("wizard").textContent=JSON.stringify(s.wizard,null,2);
    document.querySelectorAll(".mutable,.mutableForm input,.mutableForm select,.mutableForm button").forEach(el=>el.disabled=s.experiment);
  }catch(e){show(e.message);}finally{busy=false;}
},200);
document.querySelectorAll("[data-cmd]").forEach(b=>b.onclick=()=>{if(b.classList.contains("danger")&&!confirm(b.textContent))return;action(b.dataset.cmd);});
$("end").onclick=()=>{if(confirm("Завершить опыт на модуле, включая опыт другого владельца? Файловую запись ПК завершите отдельно."))action("end_experiment",{confirm_owner:true});};
$("addmark").onclick=()=>action("mark",{text:$("mark").value});
$("config").onsubmit=e=>{e.preventDefault();if(!confirm("Новый поток, история будет очищена. Применить?"))return;const config={};for(const [key,v]of new FormData(e.target))if(key!=="use_gyro_bias")config[key]=Number(v);config.use_gyro_bias=e.target.elements.use_gyro_bias.checked;action("set_config",{config});};
["+X","-X","+Y","-Y","+Z","-Z"].forEach((name,face)=>{const b=document.createElement("button");b.textContent=name+" вверх — начать 5 с";b.onclick=()=>action("cal_face",{face});$("faces").append(b);});
async function download(source){const r=await action("cal_export",{source});if(!r)return;const a=document.createElement("a"),url=URL.createObjectURL(new Blob([JSON.stringify(r.result,null,2)],{type:"application/json"}));a.href=url;a.download="calibration_"+source+".json";a.click();URL.revokeObjectURL(url);}
$("export").onclick=()=>download("active");$("candidate").onclick=()=>download("candidate");
$("import").onchange=async e=>{try{const f=e.target.files[0];if(f.size>4096)throw Error("Слишком большой JSON");const calibration=JSON.parse(await f.text());let confirm_transfer=false;if(calibration.device_uid!==info.device_uid)confirm_transfer=confirm("Коэффициенты ДРУГОЙ платы. Явно разрешить перенос?");if(calibration.device_uid!==info.device_uid&&!confirm_transfer)return;await action("cal_import",{calibration,confirm_transfer});}catch(err){show(err.message);}};
$("lcd").onclick=()=>action("lcd",{page:Number($("lcdpage").value),backlight:$("backlight").checked});
}
