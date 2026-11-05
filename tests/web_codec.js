const assert=require('node:assert/strict'),fs=require('node:fs');
const {crc32,decodePacket}=require('../web/app.js');
assert.equal(crc32(Buffer.from('123456789')),0xcbf43926);
const data=fs.readFileSync('tests/out/vector.bin');
const buffer=data.buffer.slice(data.byteOffset,data.byteOffset+data.byteLength),p=decodePacket(buffer);
assert.equal(p.uid,0xfedcba9876543210n.toString());assert.equal(p.samples[0].t_us,9007199254740993n);
assert.equal(p.samples[0].raw[0],-32768);assert.equal(p.samples[0].seq,0x12345678);
new Uint8Array(buffer)[40]^=1;assert.throws(()=>decodePacket(buffer));
console.log('PASS JS CRC / C vector / BigInt / signed raw / corrupt packet');
