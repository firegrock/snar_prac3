#include "app.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

void model_u64_add(cJSON *j,const char *key,uint64_t v) { char b[24]; snprintf(b,sizeof(b),"%" PRIu64,v); cJSON_AddStringToObject(j,key,b); }
bool model_u64(const cJSON *j,uint64_t *v) {
    if(!cJSON_IsString(j) || !j->valuestring[0] || strlen(j->valuestring)>20) return false;
    for(const char *p=j->valuestring;*p;p++) if(*p<'0' || *p>'9') return false;
    errno=0; char *end; unsigned long long x=strtoull(j->valuestring,&end,10); if(errno || *end) return false; *v=x; return true;
}
static cJSON *floats(const float *v,int n) { cJSON *a=cJSON_CreateArray(); for(int i=0;i<n;i++) cJSON_AddItemToArray(a,cJSON_CreateNumber(v[i])); return a; }
static bool read_floats(const cJSON *j,float *v,int n) { if(!cJSON_IsArray(j) || cJSON_GetArraySize(j)!=n) return false; for(int i=0;i<n;i++) { cJSON *x=cJSON_GetArrayItem(j,i); if(!cJSON_IsNumber(x) || !isfinite(x->valuedouble)) return false; v[i]=x->valuedouble; } return true; }
static bool equal(const cJSON *j,const char *s) { return cJSON_IsString(j) && !strcmp(j->valuestring,s); }
cJSON *model_cal(const lab_cal *c) {
    cJSON *j=cJSON_CreateObject(); cJSON_AddNumberToObject(j,"schema_version",c->version); model_u64_add(j,"device_uid",c->uid);
    cJSON_AddStringToObject(j,"method",c->method); cJSON_AddStringToObject(j,"axes","S"); cJSON_AddStringToObject(j,"gyro_units","rad/s"); cJSON_AddStringToObject(j,"accel_units","m/s^2");
    cJSON_AddStringToObject(j,"firmware_version",LAB_FW); cJSON_AddBoolToObject(j,"valid",c->valid);
    cJSON_AddItemToObject(j,"gyro_bias",floats(c->gyro_bias,3)); cJSON_AddItemToObject(j,"accel_bias",floats(c->accel_bias,3)); cJSON_AddItemToObject(j,"accel_scale",floats(c->accel_scale,3));
    cJSON_AddNumberToObject(j,"temperature_c",c->temperature); cJSON_AddItemToObject(j,"gyro_std",floats(c->gyro_std,3)); cJSON_AddNumberToObject(j,"gyro_n",c->gyro_n);
    cJSON_AddNumberToObject(j,"accel_range_g",c->accel_range); cJSON_AddNumberToObject(j,"gyro_range_dps",c->gyro_range);
    cJSON *means=cJSON_AddArrayToObject(j,"face_mean"),*std=cJSON_AddArrayToObject(j,"face_std"),*ns=cJSON_AddArrayToObject(j,"face_n");
    for(int f=0;f<6;f++) { cJSON_AddItemToArray(means,floats(c->face_mean[f],3)); cJSON_AddItemToArray(std,floats(c->accel_std[f],3)); cJSON_AddItemToArray(ns,cJSON_CreateNumber(c->face_n[f])); }
    return j;
}
bool model_parse_cal(const cJSON *j,lab_cal *c,uint64_t uid,bool transfer) {
    if(!cJSON_IsObject(j) || !equal(cJSON_GetObjectItem(j,"axes"),"S") || !equal(cJSON_GetObjectItem(j,"gyro_units"),"rad/s") || !equal(cJSON_GetObjectItem(j,"accel_units"),"m/s^2")) return false;
    cJSON *version=cJSON_GetObjectItem(j,"schema_version"),*method=cJSON_GetObjectItem(j,"method");
    if(!cJSON_IsNumber(version) || version->valuedouble!=1 || !cJSON_IsString(method) || strlen(method->valuestring)>=sizeof(c->method)) return false;
    memset(c,0,sizeof(*c)); c->version=1; strcpy(c->method,method->valuestring);
    if(!model_u64(cJSON_GetObjectItem(j,"device_uid"),&c->uid)) return false;
    if(!read_floats(cJSON_GetObjectItem(j,"gyro_bias"),c->gyro_bias,3) || !read_floats(cJSON_GetObjectItem(j,"accel_bias"),c->accel_bias,3) || !read_floats(cJSON_GetObjectItem(j,"accel_scale"),c->accel_scale,3)) return false;
    cJSON *t=cJSON_GetObjectItem(j,"temperature_c"); if(!cJSON_IsNumber(t) || !isfinite(t->valuedouble)) return false; c->temperature=t->valuedouble;
    cJSON *std=cJSON_GetObjectItem(j,"gyro_std"); if(!read_floats(std,c->gyro_std,3)) return false;
    const char *names[]={"gyro_n","accel_range_g","gyro_range_dps"}; int *ranges[]={NULL,&c->accel_range,&c->gyro_range};
    for(int i=0;i<3;i++) { cJSON *v=cJSON_GetObjectItem(j,names[i]); if(!cJSON_IsNumber(v) || v->valuedouble<0 || v->valuedouble>100000000 || floor(v->valuedouble)!=v->valuedouble) return false; if(i==0) c->gyro_n=v->valuedouble; else *ranges[i]=v->valueint; }
    cJSON *means=cJSON_GetObjectItem(j,"face_mean"),*ss=cJSON_GetObjectItem(j,"face_std"),*ns=cJSON_GetObjectItem(j,"face_n");
    if(!cJSON_IsArray(means)||!cJSON_IsArray(ss)||!cJSON_IsArray(ns)||cJSON_GetArraySize(means)!=6||cJSON_GetArraySize(ss)!=6||cJSON_GetArraySize(ns)!=6) return false;
    for(int f=0;f<6;f++) {
        if(!read_floats(cJSON_GetArrayItem(means,f),c->face_mean[f],3)||!read_floats(cJSON_GetArrayItem(ss,f),c->accel_std[f],3)) return false;
        cJSON *n=cJSON_GetArrayItem(ns,f); if(!cJSON_IsNumber(n)||n->valuedouble<0||n->valuedouble>100000000||floor(n->valuedouble)!=n->valuedouble) return false; c->face_n[f]=n->valuedouble;
    }
    if(!lab_cal_valid(c,uid,transfer)) return false;
    c->uid=uid; c->valid=true; return true;
}
bool model_parse_config(const cJSON *j,lab_config *c) {
    if(!cJSON_IsObject(j)) return false;
    const char *names[]={"sample_rate_hz","accel_range_g","gyro_range_dps","dlpf","Kp","Ki","g0"};
    for(int i=0;i<7;i++) { cJSON *v=cJSON_GetObjectItem(j,names[i]); if(!v) continue; if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)) return false;
        if(i<4 && floor(v->valuedouble)!=v->valuedouble) return false;
        switch(i) { case 0:c->rate=v->valueint;break;case 1:c->accel_range=v->valueint;break;case 2:c->gyro_range=v->valueint;break;case 3:c->dlpf=v->valueint;break;case 4:c->kp=v->valuedouble;break;case 5:c->ki=v->valuedouble;break;case 6:c->g0=v->valuedouble;break; }
    }
    cJSON *r=cJSON_GetObjectItem(j,"R_BS"); if(r) { float v[9]; if(!read_floats(r,v,9)) return false; for(int i=0;i<9;i++) { if(v[i]<-1||v[i]>1||floorf(v[i])!=v[i]) return false; c->r_bs[i]=v[i]; } }
    cJSON *b=cJSON_GetObjectItem(j,"use_gyro_bias"); if(b) { if(!cJSON_IsBool(b)) return false; c->use_bias=cJSON_IsTrue(b); }
    return lab_config_valid(c);
}
cJSON *model_info(const lab_snapshot *s) {
    cJSON *j=cJSON_CreateObject(); cJSON_AddNumberToObject(j,"device_id",LAB_DEVICE_ID); model_u64_add(j,"device_uid",s->uid); model_u64_add(j,"boot_id",s->boot); model_u64_add(j,"stream_id",s->stream);
    cJSON_AddNumberToObject(j,"config_id",s->config_id); cJSON_AddStringToObject(j,"firmware_version",LAB_FW); cJSON_AddStringToObject(j,"sdk_version",esp_get_idf_version());
    char ssid[24]; snprintf(ssid,sizeof(ssid),"SNAR_IMU_%02d",LAB_DEVICE_ID); cJSON_AddStringToObject(j,"ssid",ssid); cJSON_AddStringToObject(j,"ip","192.168.4.1");
    cJSON_AddNumberToObject(j,"protocol_version",1); cJSON_AddNumberToObject(j,"ring_capacity",LAB_RING_CAP); cJSON_AddNumberToObject(j,"command_max",LAB_COMMAND_MAX); return j;
}
cJSON *model_status(const lab_snapshot *s) {
    cJSON *j=model_info(s); cJSON_AddBoolToObject(j,"sensor_ok",s->sensor_ok); cJSON_AddStringToObject(j,"sensor_error",s->sensor_error);
    cJSON_AddBoolToObject(j,"orientation_ready",s->att.ready); cJSON_AddBoolToObject(j,"calibrated",s->cal.valid); cJSON_AddBoolToObject(j,"lcd_ok",s->lcd_ok);
    cJSON_AddNumberToObject(j,"mpu_whoami",s->mpu_whoami);
    cJSON_AddNumberToObject(j,"alignment_discards",s->alignment_discards);
    cJSON_AddNumberToObject(j,"lcd_address",s->lcd_address); cJSON_AddNumberToObject(j,"lcd_ack_count",s->lcd_ack_count);
    cJSON_AddBoolToObject(j,"experiment",s->experiment); cJSON_AddStringToObject(j,"owner",s->owner); cJSON_AddStringToObject(j,"nvs_status",s->nvs_status);
    cJSON *params=cJSON_AddObjectToObject(j,"parameters");
    cJSON_AddNumberToObject(params,"sample_rate_hz",s->config.rate); cJSON_AddNumberToObject(params,"accel_range_g",s->config.accel_range); cJSON_AddNumberToObject(params,"gyro_range_dps",s->config.gyro_range); cJSON_AddNumberToObject(params,"dlpf",s->config.dlpf); cJSON_AddNumberToObject(params,"Kp",s->config.kp); cJSON_AddNumberToObject(params,"Ki",s->config.ki); cJSON_AddBoolToObject(params,"use_gyro_bias",s->config.use_bias);
    float rpy[3]; lab_euler(s->att.q,rpy); for(int i=0;i<3;i++) rpy[i]*=180.0f/M_PI;
    cJSON_AddItemToObject(j,"rpy_deg",s->att.ready?floats(rpy,3):cJSON_CreateNull()); cJSON_AddItemToObject(j,"q",floats(s->att.q,4));
    if(s->sensor_ok && s->last.t_us) cJSON_AddNumberToObject(j,"temperature_c",s->last.raw[6]/340.0f+36.53f); else cJSON_AddNullToObject(j,"temperature_c");
    cJSON_AddNumberToObject(j,"oldest_seq",s->oldest); cJSON_AddNumberToObject(j,"next_seq",s->next); model_u64_add(j,"t_us",s->last.t_us);
    cJSON_AddNumberToObject(j,"free_heap",esp_get_free_heap_size()); cJSON_AddNumberToObject(j,"minimum_free_heap",esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(j,"fifo_overflows",s->fifo_overflows); cJSON_AddNumberToObject(j,"irq_overflows",s->irq_overflows); cJSON_AddNumberToObject(j,"sync_errors",s->sync_errors); cJSON_AddNumberToObject(j,"read_errors",s->read_errors); cJSON_AddNumberToObject(j,"resyncs",s->resyncs); cJSON_AddNumberToObject(j,"transport_drops",s->transport_drops);
    wifi_sta_list_t stations; int station_n=esp_wifi_ap_get_sta_list(&stations)==ESP_OK?stations.num:0;
    cJSON_AddNumberToObject(j,"wifi_stations",station_n); cJSON_AddNumberToObject(j,"ws_receivers",endpoint_count());
    cJSON *stack=cJSON_AddObjectToObject(j,"stack_high_water_bytes");
    if(sensor_handle) cJSON_AddNumberToObject(stack,"sensor",uxTaskGetStackHighWaterMark(sensor_handle));
    if(lcd_handle) cJSON_AddNumberToObject(stack,"lcd",uxTaskGetStackHighWaterMark(lcd_handle));
    if(transport_handle) cJSON_AddNumberToObject(stack,"transport",uxTaskGetStackHighWaterMark(transport_handle));
    if(command_handle) cJSON_AddNumberToObject(stack,"commands",uxTaskGetStackHighWaterMark(command_handle));
    cJSON *w=cJSON_AddObjectToObject(j,"wizard"); cJSON_AddNumberToObject(w,"state",s->wizard.state); cJSON_AddNumberToObject(w,"face",s->wizard.face); cJSON_AddNumberToObject(w,"faces",s->wizard.faces); cJSON_AddNumberToObject(w,"n",s->wizard.n); cJSON_AddNumberToObject(w,"target",s->wizard.target); cJSON_AddStringToObject(w,"reason",s->wizard.reason);
    return j;
}
cJSON *model_metadata(const lab_snapshot *s) {
    cJSON *j=model_info(s),*c=cJSON_AddObjectToObject(j,"config");
    cJSON_AddNumberToObject(c,"sample_rate_hz",s->config.rate); cJSON_AddNumberToObject(c,"accel_range_g",s->config.accel_range); cJSON_AddNumberToObject(c,"gyro_range_dps",s->config.gyro_range); cJSON_AddNumberToObject(c,"dlpf",s->config.dlpf); cJSON_AddNumberToObject(c,"smplrt_div",1000/s->config.rate-1);
    cJSON_AddNumberToObject(c,"accel_lsb_per_g",32768.0f/s->config.accel_range); cJSON_AddNumberToObject(c,"gyro_lsb_per_dps",s->config.gyro_range==250?131:s->config.gyro_range==500?65.5:s->config.gyro_range==1000?32.8:16.4);
    cJSON_AddNumberToObject(c,"Kp",s->config.kp); cJSON_AddNumberToObject(c,"Ki",s->config.ki); cJSON_AddNumberToObject(c,"g0",s->config.g0); cJSON_AddBoolToObject(c,"use_gyro_bias",s->config.use_bias);
    cJSON *r=cJSON_AddArrayToObject(c,"R_BS"); for(int i=0;i<9;i++) cJSON_AddItemToArray(r,cJSON_CreateNumber(s->config.r_bs[i]));
    cJSON_AddItemToObject(j,"calibration",model_cal(&s->cal)); cJSON_AddStringToObject(j,"axes","raw/calibration=S; f,w=R_BS*S; W right-handed Z up; initial relative X, NOT north");
    cJSON_AddStringToObject(j,"quaternion","Hamilton qw,qx,qy,qz; B->W; AFTER sample; Rz(yaw)Ry(pitch)Rx(roll)");
    cJSON_AddStringToObject(j,"time","monotonic esp_timer IRQ availability us, includes DLPF delay; NOT host receive time");
    cJSON_AddStringToObject(j,"mahony","e=cross(normalize(f),R(q)'*[0,0,1]); I=clamp(I+Ki*e*dt,+/-0.5); Euler qdot=0.5*q*[0,w+Kp*e+I], then normalize");
    cJSON *initial=cJSON_AddObjectToObject(j,"initial_state");
    cJSON_AddNumberToObject(initial,"before_seq",s->boundary_seq); cJSON_AddItemToObject(initial,"q",floats(s->initial.q,4)); cJSON_AddItemToObject(initial,"integral",floats(s->initial.integral,3)); model_u64_add(initial,"previous_t_us",s->initial.prev_us); cJSON_AddBoolToObject(initial,"ready",s->initial.ready);
    cJSON_AddNumberToObject(initial,"still_n",s->initial.still_n); cJSON_AddItemToObject(initial,"still_mean",floats(s->initial.still_mean,3)); cJSON_AddItemToObject(initial,"still_m2",floats(s->initial.still_m2,3));
    cJSON_AddBoolToObject(j,"experiment",s->experiment); model_u64_add(j,"experiment_start_us",s->start_us);
    cJSON_AddNumberToObject(j,"irq_queue_capacity",LAB_IRQ_DEPTH); cJSON_AddNumberToObject(j,"mpu_address",MPU_ADDRESS); cJSON_AddNumberToObject(j,"mpu_bus_hz",MPU_BUS_HZ); cJSON_AddNumberToObject(j,"uart_baud",LAB_UART_BAUD);
    return j;
}
