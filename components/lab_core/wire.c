#include "lab_core.h"
#include <string.h>

void lab_put16(uint8_t *p,uint16_t v) { p[0]=v; p[1]=v>>8; }
void lab_put32(uint8_t *p,uint32_t v) { for(int i=0;i<4;i++) p[i]=v>>(8*i); }
void lab_put64(uint8_t *p,uint64_t v) { for(int i=0;i<8;i++) p[i]=v>>(8*i); }
uint16_t lab_get16(const uint8_t *p) { return p[0]|((uint16_t)p[1]<<8); }
uint32_t lab_get32(const uint8_t *p) { uint32_t v=0; for(int i=0;i<4;i++) v|=(uint32_t)p[i]<<(8*i); return v; }
uint64_t lab_get64(const uint8_t *p) { uint64_t v=0; for(int i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v; }
uint32_t lab_crc32(const uint8_t *p,size_t n) {
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<n;i++) { crc^=p[i]; for(int b=0;b<8;b++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1))); }
    return ~crc;
}
size_t lab_cobs_encode(const uint8_t *in,size_t n,uint8_t *out,size_t cap) {
    if(cap<n+n/254+2) return 0;
    size_t code_i=0,pos=1; uint8_t code=1;
    for(size_t i=0;i<n;i++) { if(!in[i]) { out[code_i]=code; code_i=pos++; code=1; } else { out[pos++]=in[i]; if(++code==255) { out[code_i]=code; code_i=pos++; code=1; } } }
    out[code_i]=code; return pos;
}
size_t lab_cobs_decode(const uint8_t *in,size_t n,uint8_t *out,size_t cap) {
    size_t i=0,p=0;
    while(i<n) { uint8_t c=in[i++]; if(!c || i+c-1>n) return 0; for(int k=1;k<c;k++) { if(p>=cap || !in[i]) return 0; out[p++]=in[i++]; } if(c!=255 && i<n) { if(p>=cap) return 0; out[p++]=0; } }
    return p;
}
size_t lab_packet(uint8_t *out,size_t cap,uint8_t type,uint16_t delivery,uint64_t uid,uint64_t boot,uint64_t stream,uint32_t config,const uint8_t *payload,size_t n) {
    size_t total=LAB_HEADER+n+4; if(total>cap || total>LAB_PACKET_MAX) return 0;
    out[0]='S'; out[1]='I'; out[2]=1; out[3]=type; lab_put16(out+4,total); lab_put16(out+6,delivery);
    lab_put64(out+8,uid); lab_put64(out+16,boot); lab_put64(out+24,stream); lab_put32(out+32,config);
    if(n) memcpy(out+LAB_HEADER,payload,n);
    lab_put32(out+total-4,lab_crc32(out,total-4)); return total;
}
bool lab_packet_valid(const uint8_t *p,size_t n) { return n>=40 && n<=LAB_PACKET_MAX && p[0]=='S' && p[1]=='I' && p[2]==1 && p[3]>=1 && p[3]<=5 && lab_get16(p+4)==n && lab_get32(p+n-4)==lab_crc32(p,n-4); }
size_t lab_samples_payload(uint8_t *out,const lab_sample *s,size_t n) {
    if(!n || n>LAB_BATCH) return 0;
    lab_put16(out,n); size_t p=2;
    for(size_t j=0;j<n;j++) {
        lab_put32(out+p,s[j].seq); p+=4; lab_put64(out+p,s[j].t_us); p+=8;
        for(int i=0;i<7;i++) { lab_put16(out+p,(uint16_t)s[j].raw[i]); p+=2; }
        for(int i=0;i<4;i++) { uint32_t f; memcpy(&f,&s[j].q[i],4); lab_put32(out+p,f); p+=4; }
        lab_put32(out+p,s[j].flags); p+=4;
    }
    return p;
}
