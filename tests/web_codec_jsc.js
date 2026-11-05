// macOS built-in JavaScriptCore: same browser codec, no Node/npm dependency.
load('web/app.js');
const vectors=JSON.parse(readFile('protocol/test_vectors.json'));
function assertTest(ok,message){if(!ok)throw Error(message);}
assertTest(crc32(Uint8Array.from('123456789',x=>x.charCodeAt(0)))===0xcbf43926,'CRC check');
const hex=vectors.sample_packet_hex;
const bytes=new Uint8Array(hex.match(/../g).map(x=>parseInt(x,16)));
const p=decodePacket(bytes.buffer),e=vectors.expected;
assertTest(p.uid===e.device_uid && p.boot===e.boot_id && p.stream===e.stream_id,'64-bit ids');
assertTest(p.samples[0].t_us===BigInt(e.t_us),'BigInt time');
assertTest(JSON.stringify(p.samples[0].raw)===JSON.stringify(e.raw),'signed raw');
assertTest(p.samples[0].seq===e.seq,'endian seq');
bytes[40]^=1;let rejected=false;try{decodePacket(bytes.buffer);}catch(err){rejected=true;}
assertTest(rejected,'corruption rejection');
print('PASS JavaScriptCore: browser CRC / shared C vector / BigInt / signed raw / corruption');
