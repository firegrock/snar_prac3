// Minimal browser boundary mocks; actual web/app.js and its event handlers run.
const nodes=new Map(),timers=new Map(),intervals=[];let nextTimer=1;
function node(id){if(!nodes.has(id))nodes.set(id,{value:id==='expected'?'1':'',textContent:'',disabled:false,append(){}});return nodes.get(id);}
globalThis.document={getElementById:node,querySelectorAll:()=>[],createElement:()=>({})};
globalThis.location={protocol:'http:',host:'192.168.4.1'};
globalThis.setTimeout=fn=>{const id=nextTimer++;timers.set(id,fn);return id;};
globalThis.clearTimeout=id=>timers.delete(id);
globalThis.setInterval=fn=>intervals.push(fn);
const sockets=[];
const status={sensor_ok:true,orientation_ready:true,calibrated:true,rpy_deg:[1,2,3],temperature_c:27.5,experiment:false,wifi_stations:1,ws_receivers:1,free_heap:40000,minimum_free_heap:35000,fifo_overflows:0,irq_overflows:0,sync_errors:0,read_errors:0,lcd_ok:false,nvs_status:'LOADED',parameters:{sample_rate_hz:100},wizard:{state:0}};
class FakeWebSocket{
  constructor(url){this.url=url;this.readyState=0;this.requests=[];sockets.push(this);}
  send(text){const request=JSON.parse(text);this.requests.push(request);if(this.silent)return;const result=request.command==='get_info'?{device_id:1,device_uid:'49189103694417',sdk_version:'TEST SDK'}:status;this.onmessage({data:JSON.stringify({request_id:request.request_id,ok:true,result})});}
  close(){this.readyState=3;if(this.onclose)this.onclose({code:1000,reason:''});}
}
globalThis.WebSocket=FakeWebSocket;
load('web/app.js');
function check(ok,message){if(!ok)throw Error(message);}
async function run(){
  node('connect').onclick();const first=sockets[0];first.readyState=1;await first.onopen();
  check(first.url==='ws://192.168.4.1/ws','WS URL');
  check(node('identity').textContent.includes('49189103694417'),'handshake identity');
  await intervals[0]();check(node('roll').textContent==='1.0°','display roll');
  check(node('status').textContent.includes('27.50'),'display temperature');
  node('connect').onclick();const second=sockets[1];
  first.onclose({code:1006,reason:'stale socket'});
  check(node('connection').textContent.includes('Подключение'),'stale close cannot disable new connection');
  second.readyState=1;await second.onopen();await intervals[0]();
  second.silent=true;const waiting=intervals[0]();
  second.onclose({code:1006,reason:'link lost'});await waiting;
  check(timers.size===0,'close rejects pending RPCs and clears timers');
  check(node('connection').textContent.includes('1006')&&node('connection').textContent.includes('link lost'),'close diagnostics');
  check(node('connect').disabled===false,'retry button enabled');
  print('PASS browser connection: verify identity, render MPU status, ignore stale socket, report close code, reject pending RPC');
}
run().catch(error=>{print('FAIL '+error.stack);quit(1);});
