#include "lab_core.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float norm3(const float *v) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
static bool finite3(const float *v) { return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]); }
static float clamp(float x,float a,float b) { return fminf(b,fmaxf(a,x)); }
int lab_request_lookup(lab_request_cache *cache,const char *client,const char *id,uint32_t hash,const char **reply) {
    for(int i=0;i<16;i++) if(!strcmp(cache->entries[i].client,client) && !strcmp(cache->entries[i].id,id)) {
        if(cache->entries[i].hash!=hash) return -1;
        *reply=cache->entries[i].reply; return 1;
    }
    return 0;
}
bool lab_request_store(lab_request_cache *cache,const char *client,const char *id,uint32_t hash,const char *reply) {
    if(!*client || !*id || strlen(client)>63 || strlen(id)>63 || strlen(reply)>511) return false;
    lab_cached_request *c=&cache->entries[cache->next++%16];
    strcpy(c->client,client); strcpy(c->id,id); strcpy(c->reply,reply); c->hash=hash; return true;
}
int lab_mutation_policy(bool experiment,int wizard,const char *command) {
    if(experiment && strcmp(command,"end_experiment") && strcmp(command,"mark") && strcmp(command,"lcd")) return 1;
    if(wizard>0 && wizard<4 && strcmp(command,"cal_face") && strcmp(command,"cal_cancel") && strcmp(command,"mark") && strcmp(command,"lcd")) return 2;
    return 0;
}
bool lab_saved_valid(const lab_saved_cal *b,size_t length,uint64_t uid) {
    return length==sizeof(*b) && b->version==1 && b->size==sizeof(lab_cal) && b->crc==lab_crc32((const uint8_t*)b,offsetof(lab_saved_cal,crc)) && b->cal.valid && lab_cal_valid(&b->cal,uid,false);
}
void lab_defaults(lab_config *c,lab_cal *cal,uint64_t uid) {
    *c=(lab_config){.rate=100,.accel_range=4,.gyro_range=500,.dlpf=4,.kp=2,.ki=0,.g0=LAB_G0,.r_bs={1,0,0,0,1,0,0,0,1},.use_bias=true};
    memset(cal,0,sizeof(*cal)); cal->version=1; cal->uid=uid;
    for(int i=0;i<3;i++) cal->accel_scale[i]=1;
    strcpy(cal->method,"DEFAULT");
}
bool lab_config_valid(const lab_config *c) {
    if(c->rate!=50 && c->rate!=100 && c->rate!=200) return false;
    if(c->accel_range!=2 && c->accel_range!=4 && c->accel_range!=8 && c->accel_range!=16) return false;
    if(c->gyro_range!=250 && c->gyro_range!=500 && c->gyro_range!=1000 && c->gyro_range!=2000) return false;
    if(c->dlpf<1 || c->dlpf>6 || !isfinite(c->kp) || !isfinite(c->ki) || c->kp<0 || c->kp>20 || c->ki<0 || c->ki>10 || !isfinite(c->g0) || c->g0<9 || c->g0>10) return false;
    const int8_t *r=c->r_bs;
    for(int i=0;i<3;i++) for(int j=0;j<3;j++) {
        int sum=0; for(int k=0;k<3;k++) { if(r[3*i+k]<-1 || r[3*i+k]>1) return false; sum+=r[3*i+k]*r[3*j+k]; }
        if(sum!=(i==j)) return false;
    }
    int d=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
    return d==1;
}
bool lab_cal_valid(const lab_cal *c,uint64_t uid,bool transfer) {
    if(c->version!=1 || (!transfer && c->uid!=uid) || !finite3(c->gyro_bias) || !finite3(c->accel_bias) || !finite3(c->accel_scale) || !isfinite(c->temperature)) return false;
    for(int i=0;i<3;i++) if(fabsf(c->gyro_bias[i])>LAB_CAL_GYRO_BIAS_MAX_RAD_S || fabsf(c->accel_bias[i])>LAB_CAL_ACCEL_BIAS_MAX_M_S2 || c->accel_scale[i]<LAB_CAL_SCALE_MIN || c->accel_scale[i]>LAB_CAL_SCALE_MAX || !isfinite(c->gyro_std[i]) || c->gyro_std[i]<0) return false;
    for(int j=0;j<6;j++) for(int i=0;i<3;i++) if(!isfinite(c->face_mean[j][i]) || !isfinite(c->accel_std[j][i]) || c->accel_std[j][i]<0) return false;
    return true;
}
void lab_nominal(const lab_config *c,const int16_t raw[7],float a[3],float w[3],float *t) {
    const float as=32768.0f/c->accel_range;
    const float gs=c->gyro_range==250?131.0f:c->gyro_range==500?65.5f:c->gyro_range==1000?32.8f:16.4f;
    for(int i=0;i<3;i++) { a[i]=raw[i]/as*c->g0; w[i]=raw[i+3]/gs*(3.14159265358979323846f/180); }
    *t=raw[6]/340.0f+36.53f;
}
void lab_correct(const lab_config *c,const lab_cal *cal,const int16_t raw[7],float a[3],float w[3],float *t) {
    float u[3],v[3]; lab_nominal(c,raw,u,v,t);
    for(int i=0;i<3;i++) { u[i]=cal->accel_scale[i]*(u[i]-cal->accel_bias[i]); if(c->use_bias) v[i]-=cal->gyro_bias[i]; }
    for(int i=0;i<3;i++) { a[i]=w[i]=0; for(int j=0;j<3;j++) { a[i]+=c->r_bs[3*i+j]*u[j]; w[i]+=c->r_bs[3*i+j]*v[j]; } }
}
void lab_att_reset(lab_attitude *s) { memset(s,0,sizeof(*s)); s->q[0]=1; }
bool lab_mahony(lab_attitude *s,const float a[3],const float w[3],float kp,float ki,float dt) {
    if(!isfinite(dt) || dt<=0 || dt>0.05f || !finite3(w)) return false;
    float n=norm3(a), e[3]={0}, *q=s->q;
    bool correction=finite3(a) && isfinite(n) && n>1e-6f;
    if(correction) {
        float v[3]={2*(q[1]*q[3]-q[0]*q[2]),2*(q[2]*q[3]+q[0]*q[1]),1-2*(q[1]*q[1]+q[2]*q[2])};
        float u[3]={a[0]/n,a[1]/n,a[2]/n};
        e[0]=u[1]*v[2]-u[2]*v[1]; e[1]=u[2]*v[0]-u[0]*v[2]; e[2]=u[0]*v[1]-u[1]*v[0];
    }
    float omega[3];
    for(int i=0;i<3;i++) {
        s->integral[i]=ki==0?0:clamp(s->integral[i]+ki*e[i]*dt,-0.5f,0.5f);
        omega[i]=w[i]+kp*e[i]+s->integral[i];
    }
    float h=0.5f*dt, p[4]={q[0]+h*(-q[1]*omega[0]-q[2]*omega[1]-q[3]*omega[2]),
        q[1]+h*(q[0]*omega[0]+q[2]*omega[2]-q[3]*omega[1]),
        q[2]+h*(q[0]*omega[1]-q[1]*omega[2]+q[3]*omega[0]),
        q[3]+h*(q[0]*omega[2]+q[1]*omega[1]-q[2]*omega[0])};
    n=sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]+p[3]*p[3]);
    if(!isfinite(n) || n<1e-6f) { lab_att_reset(s); return false; }
    for(int i=0;i<4;i++) q[i]=p[i]/n;
    return true;
}
void lab_euler(const float q[4],float rpy[3]) {
    rpy[0]=atan2f(2*(q[0]*q[1]+q[2]*q[3]),1-2*(q[1]*q[1]+q[2]*q[2]));
    rpy[1]=asinf(clamp(2*(q[0]*q[2]-q[3]*q[1]),-1,1));
    rpy[2]=atan2f(2*(q[0]*q[3]+q[1]*q[2]),1-2*(q[2]*q[2]+q[3]*q[3]));
}
void lab_zero_yaw(lab_attitude *s) {
    float rpy[3]; lab_euler(s->q,rpy); float c=cosf(-rpy[2]/2), z=sinf(-rpy[2]/2), *q=s->q;
    float p[4]={c*q[0]-z*q[3],c*q[1]-z*q[2],c*q[2]+z*q[1],c*q[3]+z*q[0]}; memcpy(q,p,sizeof(p));
}
uint32_t lab_att_step(lab_attitude *s,const lab_config *c,const float a[3],const float w[3],int64_t t) {
    uint32_t flags=0; float n=norm3(a);
    if(!finite3(a) || !isfinite(n) || n<1e-6f) flags|=LAB_ACCEL_INVALID;
    else if(fabsf(n-c->g0)>0.2f*c->g0) flags|=LAB_ACCEL_NORM;
    float dt=s->prev_us?(t-s->prev_us)*1e-6f:0;
    if(s->prev_us && (dt<=0 || dt>2.5f/c->rate)) { lab_att_reset(s); flags|=LAB_TIME_GAP; }
    bool had_time=s->prev_us!=0; s->prev_us=t;
    if(!s->ready) {
        bool still=!(flags&(LAB_ACCEL_INVALID|LAB_ACCEL_NORM)) && finite3(w) && norm3(w)<0.08f;
        if(!still) { s->still_n=0; memset(s->still_mean,0,sizeof(s->still_mean)); memset(s->still_m2,0,sizeof(s->still_m2)); }
        else {
            ++s->still_n;
            for(int i=0;i<3;i++) { float d=a[i]-s->still_mean[i]; s->still_mean[i]+=d/s->still_n; s->still_m2[i]+=d*(a[i]-s->still_mean[i]); }
            if(s->still_n>=(uint32_t)c->rate) {
                bool ok=true; for(int i=0;i<3;i++) if(s->still_m2[i]/(s->still_n-1)>0.01f) ok=false;
                if(ok) {
                    float r=atan2f(s->still_mean[1],s->still_mean[2])/2, p=atan2f(-s->still_mean[0],hypotf(s->still_mean[1],s->still_mean[2]))/2;
                    s->q[0]=cosf(r)*cosf(p); s->q[1]=sinf(r)*cosf(p); s->q[2]=cosf(r)*sinf(p); s->q[3]=-sinf(r)*sinf(p); s->ready=true;
                } else { s->still_n=0; memset(s->still_mean,0,sizeof(s->still_mean)); memset(s->still_m2,0,sizeof(s->still_m2)); }
            }
        }
    } else if(had_time && !lab_mahony(s,a,w,c->kp,c->ki,dt)) { lab_att_reset(s); s->prev_us=t; flags|=LAB_TIME_GAP; }
    if(!s->ready) flags|=LAB_ORIENT_INVALID;
    return flags;
}
void lab_ring_clear(lab_ring *r) { r->next=0; r->count=0; }
bool lab_ring_put(lab_ring *r,lab_sample *s) {
    if(r->next==UINT32_MAX) return false;
    s->seq=r->next; r->data[r->next%LAB_RING_CAP]=*s; r->next++; if(r->count<LAB_RING_CAP) r->count++; return true;
}
size_t lab_ring_get(const lab_ring *r,uint32_t *cursor,lab_sample *out,size_t cap,bool *gap) {
    uint32_t oldest=r->next-r->count; *gap=*cursor<oldest;
    if(*gap) *cursor=oldest;
    if(*cursor>r->next) return 0;
    size_t n=r->next-*cursor; if(n>cap) n=cap;
    for(size_t i=0;i<n;i++) out[i]=r->data[((*cursor)++)%LAB_RING_CAP];
    return n;
}
bool lab_six_solve(lab_cal *cal,float g0) {
    for(int f=0;f<6;f++) {
        int ax=f/2; float sign=(f%2)?-1:1;
        if(!cal->face_n[f] || !finite3(cal->face_mean[f])) return false;
        for(int k=0;k<3;k++) if(k==ax ? sign*cal->face_mean[f][k]<LAB_CAL_SOLVE_PRIMARY_G*g0 : fabsf(cal->face_mean[f][k])>LAB_CAL_SOLVE_CROSS_G*g0) return false;
    }
    for(int i=0;i<3;i++) { float p=cal->face_mean[2*i][i],m=cal->face_mean[2*i+1][i]; cal->accel_bias[i]=(p+m)/2; cal->accel_scale[i]=2*g0/(p-m); }
    return lab_cal_valid(cal,cal->uid,false);
}
void lab_wizard_start(lab_wizard *w,const lab_config *c,const lab_cal *cal,bool gyro,unsigned seconds) {
    memset(w,0,sizeof(*w)); w->candidate=*cal; w->candidate.accel_range=c->accel_range; w->candidate.gyro_range=c->gyro_range;
    w->state=gyro?1:2; w->target=(seconds?seconds:30)*c->rate; strcpy(w->candidate.method,"ON_DEVICE");
}
bool lab_wizard_face(lab_wizard *w,int face,int rate,unsigned seconds) {
    if(w->state!=2 || face<0 || face>5 || (w->faces&(1<<face))) return false;
    w->state=3; w->face=face; w->n=0; w->first_us=w->last_us=0; w->target=(seconds?seconds:5)*rate;
    memset(w->mean,0,sizeof(w->mean)); memset(w->m2,0,sizeof(w->m2)); return true;
}
static void reject(lab_wizard *w,const char *why) { w->state=-1; snprintf(w->reason,sizeof(w->reason),"%s",why); }
void lab_wizard_feed(lab_wizard *w,const lab_config *c,const lab_sample *s) {
    if(w->state!=1 && w->state!=3) return;
    float a[3],g[3],t; lab_nominal(c,s->raw,a,g,&t);
    float n=norm3(a);
    if((s->flags&(LAB_SATURATED|LAB_TIME_GAP)) || (w->last_us && (s->t_us-w->last_us<=0 || s->t_us-w->last_us>2500000/c->rate))) { reject(w,"SAMPLE_GAP_OR_SATURATION"); return; }
    if(!finite3(a) || !finite3(g) || fabsf(n-c->g0)>LAB_CAL_ACCEL_NORM_TOL_G*c->g0 || norm3(g)>LAB_CAL_GYRO_MOTION_RAD_S) { reject(w,"MOTION_OR_NORM"); return; }
    if(w->state==3) {
        int ax=w->face/2; float sign=w->face%2?-1:1;
        if(sign*a[ax]<LAB_CAL_FACE_PRIMARY_G*c->g0) { reject(w,"WRONG_FACE"); return; }
        for(int i=0;i<3;i++) if(i!=ax && fabsf(a[i])>LAB_CAL_FACE_CROSS_G*c->g0) { reject(w,"WRONG_FACE"); return; }
    }
    if(!w->n) w->first_us=s->t_us;
    w->last_us=s->t_us; ++w->n;
    float values[7]={a[0],a[1],a[2],g[0],g[1],g[2],t};
    for(int i=0;i<7;i++) { float d=values[i]-w->mean[i]; w->mean[i]+=d/w->n; w->m2[i]+=d*(values[i]-w->mean[i]); }
    if(w->n<w->target) return;
    for(int i=0;i<6;i++) if(sqrtf(w->m2[i]/(w->n-1))>(i<3?LAB_CAL_ACCEL_STD_M_S2:LAB_CAL_GYRO_STD_RAD_S)) { reject(w,"STD_MOTION"); return; }
    w->candidate.temperature=w->mean[6];
    if(w->state==1) {
        for(int i=0;i<3;i++) { w->candidate.gyro_bias[i]=w->mean[i+3]; w->candidate.gyro_std[i]=sqrtf(w->m2[i+3]/(w->n-1)); }
        w->candidate.gyro_n=w->n; w->state=4;
    } else {
        int f=w->face; for(int i=0;i<3;i++) { w->candidate.face_mean[f][i]=w->mean[i]; w->candidate.accel_std[f][i]=sqrtf(w->m2[i]/(w->n-1)); }
        w->candidate.face_n[f]=w->n; w->faces|=1<<f; w->state=2;
        if(w->faces==63) { if(lab_six_solve(&w->candidate,c->g0)) w->state=4; else reject(w,"SIX_SOLVE_REJECTED"); }
    }
    if(w->state==4) w->candidate.valid=true;
}
