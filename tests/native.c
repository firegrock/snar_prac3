#include "lab_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static void closef(float a,float b,float tol) { if(fabsf(a-b)>tol) { fprintf(stderr,"%.9g != %.9g\n",a,b); abort(); } }
static void scales(void) {
    lab_config c; lab_cal cal; lab_defaults(&c,&cal,123); assert(lab_config_valid(&c));
    int16_t raw[7]={-8192,4096,8192,-655,131,0,-340}; float a[3],w[3],t;
    lab_nominal(&c,raw,a,w,&t); closef(a[0],-LAB_G0,1e-6); closef(w[0],-10*M_PI/180,1e-6); closef(t,35.53,1e-5);
    int ranges[]={2,4,8,16}; for(int i=0;i<4;i++) { c.accel_range=ranges[i]; raw[0]=32768/ranges[i]; lab_nominal(&c,raw,a,w,&t); closef(a[0],LAB_G0,1e-6); }
    const int8_t r[]={0,-1,0,1,0,0,0,0,1}; memcpy(c.r_bs,r,9); assert(lab_config_valid(&c));
    raw[0]=100; raw[1]=200; float as[3],ws[3]; lab_nominal(&c,raw,as,ws,&t); lab_correct(&c,&cal,raw,a,w,&t); closef(a[0],-as[1],1e-6); closef(a[1],as[0],1e-6);
    c.r_bs[8]=-1; assert(!lab_config_valid(&c)); c.r_bs[8]=1; c.dlpf=7; assert(!lab_config_valid(&c)); c.dlpf=4; c.ki=NAN; assert(!lab_config_valid(&c));
    puts("PASS scales, signed raw, units, axes, det=+1, invalid config");
}
static void attitude(void) {
    lab_attitude s; lab_att_reset(&s); s.ready=true;
    float a[]={0,0,LAB_G0},w[]={0,0,0};
    for(int i=0;i<1000;i++) assert(lab_mahony(&s,a,w,2,0,.01)); closef(s.q[0],1,1e-6);
    s.q[0]=cosf(.2); s.q[1]=sinf(.2);
    float before=fabsf(s.q[1]); assert(lab_mahony(&s,a,w,2,0,.01)); assert(fabsf(s.q[1])<before); /* feedback sign */
    for(int i=0;i<1000;i++) lab_mahony(&s,a,w,2,0,.01); closef(s.q[1],0,1e-5);
    lab_att_reset(&s); w[2]=M_PI/2; for(int i=0;i<100;i++) lab_mahony(&s,a,w,2,0,.01);
    closef(s.q[0],sqrtf(.5f),2e-5); closef(s.q[3],sqrtf(.5f),2e-5); /* yaw not corrected */
    float rpy[3]; lab_euler(s.q,rpy); closef(rpy[2],M_PI/2,5e-5); lab_zero_yaw(&s); closef(s.q[3],0,1e-6);
    float neg[4],pos[3],minus[3]; for(int i=0;i<4;i++) neg[i]=-s.q[i]; lab_euler(s.q,pos);lab_euler(neg,minus); for(int i=0;i<3;i++) closef(pos[i],minus[i],1e-6);
    a[0]=a[1]=a[2]=0; assert(lab_mahony(&s,a,w,2,0,.01)); assert(!lab_mahony(&s,a,w,2,0,1)); assert(!lab_mahony(&s,a,w,2,0,0));
    s.integral[0]=.3; lab_mahony(&s,a,w,2,0,.01); closef(s.integral[0],0,0);
    lab_att_reset(&s); s.q[0]=cosf(.2);s.q[1]=sinf(.2); a[2]=LAB_G0; w[2]=0;
    lab_mahony(&s,a,w,2,.1,.01); assert(s.integral[0]<0);
    float n=0;for(int i=0;i<4;i++) n+=s.q[i]*s.q[i]; closef(n,1,1e-6);
    lab_config c;lab_cal cal;lab_defaults(&c,&cal,1); lab_att_reset(&s);
    for(int i=1;i<=100;i++) lab_att_step(&s,&c,a,w,10000LL*i);assert(s.ready);
    uint32_t f=lab_att_step(&s,&c,a,w,2000000);assert((f&LAB_TIME_GAP)&&(f&LAB_ORIENT_INVALID));
    a[2]=0;f=lab_att_step(&s,&c,a,w,2010000);assert(f&LAB_ACCEL_INVALID);
    for(int j=0;j<4;j++) {
        float roll=(j&1)?.4f:-.4f,pitch=(j&2)?.3f:-.3f;
        a[0]=-LAB_G0*sinf(pitch);a[1]=LAB_G0*sinf(roll)*cosf(pitch);a[2]=LAB_G0*cosf(roll)*cosf(pitch);
        lab_att_reset(&s);for(int i=1;i<=100;i++)lab_att_step(&s,&c,a,w,i*10000LL);
        assert(s.ready);lab_euler(s.q,rpy);closef(rpy[0],roll,1e-5);closef(rpy[1],pitch,1e-5);closef(rpy[2],0,1e-5);
    }
    puts("PASS Mahony rest, tilt sign, yaw, q/-q, zero accel, dt gap, Ki and normalization");
}
static void calibration(void) {
    lab_config c;lab_cal cal;lab_defaults(&c,&cal,123); const float bias[]={.2,-.1,.3},scale[]={1.02,.98,1.01};
    for(int f=0;f<6;f++) { cal.face_n[f]=500;for(int i=0;i<3;i++) cal.face_mean[f][i]=bias[i]+(i==f/2?(f%2?-LAB_G0:LAB_G0)/scale[i]:0); }
    assert(lab_six_solve(&cal,LAB_G0));for(int i=0;i<3;i++){closef(cal.accel_bias[i],bias[i],1e-6);closef(cal.accel_scale[i],scale[i],1e-6);}
    cal.face_mean[1][0]=LAB_G0;assert(!lab_six_solve(&cal,LAB_G0)); assert(!lab_cal_valid(&cal,999,false));assert(lab_cal_valid(&cal,999,true));
    cal.accel_scale[0]=-1;assert(!lab_cal_valid(&cal,123,false));cal.accel_scale[0]=NAN;assert(!lab_cal_valid(&cal,123,false));
    lab_defaults(&c,&cal,123);lab_wizard wiz;lab_wizard_start(&wiz,&c,&cal,true,1);
    for(int i=0;i<100;i++){lab_sample s={.t_us=(i+1)*10000,.raw={0,0,8192,65,-131,196,0}};lab_wizard_feed(&wiz,&c,&s);}assert(wiz.state==4);closef(wiz.candidate.gyro_bias[1],-2*M_PI/180,1e-6);
    lab_wizard_start(&wiz,&c,&cal,true,1);lab_sample moving={.t_us=10000,.raw={0,0,8192,10000,0,0,0}};lab_wizard_feed(&wiz,&c,&moving);assert(wiz.state==-1);
    lab_wizard_start(&wiz,&c,&cal,false,1);assert(lab_wizard_face(&wiz,0,100,1));lab_sample bad={.t_us=10000,.raw={0,0,8192,0,0,0,0}};lab_wizard_feed(&wiz,&c,&bad);assert(wiz.state==-1);
    puts("PASS calibration known bias/scale, UID, NaN, negative scale, movement and wrong face");
}
static lab_ring ring;static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static void *producer(void *arg) { for(int i=0;i<10000;i++){lab_sample s={.t_us=i*10000LL};pthread_mutex_lock(&lock);assert(lab_ring_put(&ring,&s));pthread_mutex_unlock(&lock);}return NULL; }
static void *consumer(void *arg) { uint32_t cursor=0;for(int i=0;i<3000;i++){lab_sample s[8];bool gap;pthread_mutex_lock(&lock);size_t n=lab_ring_get(&ring,&cursor,s,8,&gap);pthread_mutex_unlock(&lock);for(size_t k=1;k<n;k++)assert(s[k].seq==s[k-1].seq+1);}return NULL; }
static void rings(void) {
    lab_ring_clear(&ring); for(int i=0;i<LAB_RING_CAP;i++){lab_sample s={.t_us=i};assert(lab_ring_put(&ring,&s));}
    uint32_t cursors[]={0,400,800,0};lab_sample out[8];bool gap;
    for(int i=0;i<4;i++){size_t n=lab_ring_get(&ring,&cursors[i],out,8,&gap);assert(n==8&&!gap);}
    lab_sample s={0};lab_ring_put(&ring,&s);uint32_t p=0;lab_ring_get(&ring,&p,out,8,&gap);assert(gap&&out[0].seq==1);
    p=800;lab_ring_get(&ring,&p,out,8,&gap);assert(!gap&&out[0].seq==800); /* pause inside window */
    ring.next=UINT32_MAX;assert(!lab_ring_put(&ring,&s));lab_ring_clear(&ring);assert(ring.next==0);
    pthread_t prod,cons[4];pthread_create(&prod,NULL,producer,NULL);for(int i=0;i<4;i++)pthread_create(&cons[i],NULL,consumer,NULL);pthread_join(prod,NULL);for(int i=0;i<4;i++)pthread_join(cons[i],NULL);
    puts("PASS ring four cursors, overwrite GAP, resume, seq limit, concurrent writer/readers (caller lock)");
}
static void wire(void) {
    assert(lab_crc32((uint8_t*)"123456789",9)==0xcbf43926);
    uint8_t p[LAB_PACKET_MAX],c[LAB_PACKET_MAX+64],d[LAB_PACKET_MAX];
    for(int n=1;n<1024;n++) { for(int i=0;i<n;i++) p[i]=(i*17)%256;size_t m=lab_cobs_encode(p,n,c,sizeof(c));assert(m);size_t k=lab_cobs_decode(c,m,d,sizeof(d));assert(k==(size_t)n&&!memcmp(p,d,n)); }
    lab_sample s={.seq=0x12345678,.t_us=9007199254740993LL,.raw={-32768,32767,-1,1,-655,131,-340},.q={1,0,0,0},.flags=32};uint8_t payload[48];size_t n=lab_samples_payload(payload,&s,1);
    assert(!lab_samples_payload(payload,&s,0)); assert(!lab_samples_payload(payload,&s,9));
    size_t len=lab_packet(p,sizeof(p),1,1,0xfedcba9876543210ULL,0xffffffffffffffffULL,0x1020304050607080ULL,7,payload,n);assert(len==88&&lab_packet_valid(p,len));assert(!lab_packet_valid(p,len-1));p[40]^=1;assert(!lab_packet_valid(p,len));p[40]^=1;
    FILE *f=fopen("tests/out/vector.bin","wb");assert(f);fwrite(p,1,len,f);fclose(f);
    puts("PASS C CRC check, COBS roundtrip, packet explicit endian/CRC/64-bit; vector.bin emitted");
}
static void fixture(void) {
    FILE *f=fopen("tests/out/mahony_fixture.csv","w");assert(f);fprintf(f,"t_us,ax,ay,az,wx,wy,wz,qw,qx,qy,qz,ix,iy,iz\n");
    lab_attitude s;lab_att_reset(&s);s.ready=true;
    for(int i=0;i<1500;i++){float a[3]={.4f*sinf(i*.013f),.7f*cosf(i*.01f),LAB_G0},w[3]={.1f*sinf(i*.02f),.08f*cosf(i*.017f),.12f};lab_mahony(&s,a,w,2,.05,.01);fprintf(f,"%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",(i+1)*10000,a[0],a[1],a[2],w[0],w[1],w[2],s.q[0],s.q[1],s.q[2],s.q[3],s.integral[0],s.integral[1],s.integral[2]);}fclose(f);
}
static void policy_storage(void) {
    const char *mutations[]={"set_config","cal_save","cal_apply","cal_gyro","cal_accel","cal_import","cal_reset","reset_orientation","zero_yaw","start_experiment"};
    for(unsigned i=0;i<sizeof(mutations)/sizeof(*mutations);i++) assert(lab_mutation_policy(true,0,mutations[i])==1);
    assert(lab_mutation_policy(true,0,"mark")==0);assert(lab_mutation_policy(true,0,"end_experiment")==0);
    assert(lab_mutation_policy(false,1,"set_config")==2);assert(lab_mutation_policy(false,1,"cal_cancel")==0);
    lab_request_cache cache={0};const char *reply=NULL;
    assert(lab_request_lookup(&cache,"pc","1",42,&reply)==0);assert(lab_request_store(&cache,"pc","1",42,"{\"ok\":true}"));
    assert(lab_request_lookup(&cache,"pc","1",42,&reply)==1);assert(!strcmp(reply,"{\"ok\":true}"));assert(lab_request_lookup(&cache,"pc","1",43,&reply)==-1);
    lab_config c;lab_cal cal;lab_defaults(&c,&cal,123);cal.valid=true;
    lab_saved_cal blob={.version=1,.size=sizeof(lab_cal),.cal=cal};blob.crc=lab_crc32((uint8_t*)&blob,offsetof(lab_saved_cal,crc));
    assert(lab_saved_valid(&blob,sizeof(blob),123));assert(!lab_saved_valid(&blob,sizeof(blob)-1,123));assert(!lab_saved_valid(&blob,sizeof(blob),124));
    blob.version=2;blob.crc=lab_crc32((uint8_t*)&blob,offsetof(lab_saved_cal,crc));assert(!lab_saved_valid(&blob,sizeof(blob),123));
    blob.version=1;assert(!lab_saved_valid(&blob,sizeof(blob),123));
    puts("PASS production command policy, idempotency cache, NVS version/size/CRC/UID validation");
}
int main(void){scales();attitude();calibration();rings();wire();fixture();policy_storage();printf("sample bytes=%zu ring bytes=%zu\n",sizeof(lab_sample),sizeof(lab_ring));return 0;}
