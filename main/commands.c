#include "app.h"
#include "esp_timer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { int ep; char *text; } command;
static QueueHandle_t commands;
static lab_request_cache cache;
static const char *str(const cJSON *j,const char *key) { cJSON *v=cJSON_GetObjectItem(j,key); return cJSON_IsString(v)?v->valuestring:""; }
static bool num(const cJSON *j,const char *key,uint32_t *n) { cJSON *v=cJSON_GetObjectItem(j,key); if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>UINT32_MAX||v->valuedouble!=(uint32_t)v->valuedouble) return false; *n=v->valuedouble; return true; }
static void error(cJSON *r,const char *code,const char *message) { cJSON_AddBoolToObject(r,"ok",false); cJSON *e=cJSON_AddObjectToObject(r,"error"); cJSON_AddStringToObject(e,"code",code); cJSON_AddStringToObject(e,"message",message); }
static bool is_read(const char *s) { return !strcmp(s,"get_info")||!strcmp(s,"get_status")||!strcmp(s,"get_config")||!strcmp(s,"cal_export")||!strcmp(s,"get_events")||!strcmp(s,"subscribe")||!strcmp(s,"resume")||!strcmp(s,"unsubscribe")||!strcmp(s,"ack"); }
bool command_enqueue(int ep,const char *text,size_t n) {
    if(!n || n>LAB_COMMAND_MAX || memchr(text,0,n)) return false;
    char *s=malloc(n+1); if(!s) return false; memcpy(s,text,n); s[n]=0; command c={ep,s};
    if(strstr(s,"\\u0000") || xQueueSend(commands,&c,0)!=pdTRUE) { free(s); return false; } return true;
}
static void handle(command *cmd) {
    const char *end=NULL; cJSON *j=cJSON_ParseWithOpts(cmd->text,&end,true),*r=cJSON_CreateObject();
    const char *id=str(j,"request_id"),*client=str(j,"client_id"),*name=str(j,"command");
    cJSON_AddStringToObject(r,"request_id",id); const char *err=NULL,*why="Invalid parameters";
    lab_snapshot s; app_snapshot(&s); bool mutation=!is_read(name); bool subscribe=false; uint32_t cursor=0;
    uint32_t hash=lab_crc32((const uint8_t*)cmd->text,strlen(cmd->text));
    if(!j || !cJSON_IsObject(j) || !id[0] || strlen(id)>63 || !client[0] || strlen(client)>63 || !name[0]) { err="BAD_COMMAND"; goto done; }
    if(mutation) {
        uint64_t uid; uint32_t device;
        if(!model_u64(cJSON_GetObjectItem(j,"device_uid"),&uid)||uid!=s.uid||!num(j,"device_id",&device)||device!=LAB_DEVICE_ID) { err="IDENTITY_MISMATCH"; goto done; }
        const char *cached_reply=NULL; int found=lab_request_lookup(&cache,client,id,hash,&cached_reply);
        if(found<0) { err="REQUEST_ID_REUSED"; goto done; }
        if(found>0) { endpoint_send_json(cmd->ep,cached_reply); cJSON_Delete(j); cJSON_Delete(r); return; }
        int policy=lab_mutation_policy(s.experiment,s.wizard.state,name);
        if(policy==1) { err="INVALID_STATE"; why="Experiment owns immutable configuration"; goto done; }
        if(policy==2) { err="BUSY"; why="Calibration in progress; cancel first"; goto done; }
    }
    if(!strcmp(name,"get_info")) cJSON_AddItemToObject(r,"result",model_info(&s));
    else if(!strcmp(name,"get_status")) cJSON_AddItemToObject(r,"result",model_status(&s));
    else if(!strcmp(name,"get_config")) cJSON_AddItemToObject(r,"result",model_metadata(&s));
    else if(!strcmp(name,"cal_export")) {
        bool candidate=!strcmp(str(j,"source"),"candidate");
        if(candidate && s.wizard.state!=4) { err="INVALID_STATE"; why="No candidate available"; goto done; }
        cJSON_AddItemToObject(r,"result",model_cal(candidate?&s.wizard.candidate:&s.cal));
    }
    else if(!strcmp(name,"get_events")) {
        uint32_t after=0; if(cJSON_GetObjectItem(j,"after") && !num(j,"after",&after)) { err="BAD_COMMAND"; goto done; }
        lab_event e[16]; bool lost; size_t n=app_events(after,e,16,&lost);
        cJSON *a=cJSON_AddArrayToObject(r,"events"); cJSON_AddBoolToObject(r,"detail_unavailable",lost);
        model_u64_add(r,"device_uid",s.uid); model_u64_add(r,"boot_id",s.boot);
        for(size_t i=0;i<n;i++) { cJSON *x=cJSON_CreateObject(); cJSON_AddNumberToObject(x,"id",e[i].id); cJSON_AddNumberToObject(x,"seq",e[i].seq); model_u64_add(x,"stream_id",e[i].stream); cJSON_AddNumberToObject(x,"config_id",e[i].config_id); model_u64_add(x,"t_us",e[i].t_us); cJSON_AddStringToObject(x,"type",e[i].type); cJSON_AddStringToObject(x,"text",e[i].text); cJSON_AddItemToArray(a,x); }
        cJSON_AddNumberToObject(r,"next_event",n?e[n-1].id+1:after);
    }
    else if(!strcmp(name,"subscribe") || !strcmp(name,"resume")) {
        uint64_t uid,boot,stream;
        if(!model_u64(cJSON_GetObjectItem(j,"device_uid"),&uid)||!model_u64(cJSON_GetObjectItem(j,"boot_id"),&boot)||!model_u64(cJSON_GetObjectItem(j,"stream_id"),&stream)||uid!=s.uid||boot!=s.boot||stream!=s.stream) { err="STREAM_BOUNDARY"; goto done; }
        if(!strcmp(name,"resume")) { uint32_t last; if(!num(j,"last_seq",&last)||last==UINT32_MAX||last>=s.next) { err="INVALID_CURSOR"; goto done; } cursor=last+1; }
        else {
            cursor=!strcmp(str(j,"mode"),"history")?s.oldest:s.next;
            if(cJSON_GetObjectItem(j,"from_seq") && (!num(j,"from_seq",&cursor)||cursor>s.next)) { err="INVALID_CURSOR"; goto done; }
        }
        cJSON_AddNumberToObject(r,"requested_seq",cursor); cJSON_AddNumberToObject(r,"oldest_seq",s.oldest); cJSON_AddNumberToObject(r,"next_seq",s.next); cJSON_AddBoolToObject(r,"gap",cursor<s.oldest);
        cJSON_AddItemToObject(r,"metadata",model_metadata(&s)); subscribe=true;
    }
    else if(!strcmp(name,"unsubscribe")) endpoint_unsubscribe(cmd->ep);
    else if(!strcmp(name,"ack")) { uint32_t seq; if(!num(j,"seq",&seq)) { err="BAD_COMMAND"; goto done; } endpoint_ack(cmd->ep,seq); }
    else if(!strcmp(name,"mark")) {
        const char *text=str(j,"text"); if(!text[0] || strlen(text)>96) { err="BAD_COMMAND"; goto done; }
        xSemaphoreTake(state_lock,portMAX_DELAY); app_event_locked("MARK",text); xSemaphoreGive(state_lock);
    }
    else if(!strcmp(name,"lcd")) {
        uint32_t page; if(!num(j,"page",&page)||page>4) { err="BAD_COMMAND"; goto done; }
        xSemaphoreTake(state_lock,portMAX_DELAY); state.lcd_page=page; state.backlight=!cJSON_IsFalse(cJSON_GetObjectItem(j,"backlight")); xSemaphoreGive(state_lock);
    }
    else if(!strcmp(name,"cal_save")) {
        if(!s.cal.valid) { err="INVALID_STATE"; goto done; }
        bool ok=calibration_save(&s.cal);
        xSemaphoreTake(state_lock,portMAX_DELAY); snprintf(state.nvs_status,sizeof(state.nvs_status),"%s",ok?"SAVED":"SAVE_ERROR"); app_event_locked("NVS",state.nvs_status); xSemaphoreGive(state_lock);
        if(!ok) { err="NVS_ERROR"; goto done; }
    }
    else {
        lab_op op={0}; bool known=true;
        if(!strcmp(name,"set_config")) { op.kind=OP_CONFIG; op.config=s.config; if(!model_parse_config(cJSON_GetObjectItem(j,"config"),&op.config)) { err="INVALID_CONFIG"; goto done; } }
        else if(!strcmp(name,"cal_import")) {
            lab_cal cal;
            if(!model_parse_cal(cJSON_GetObjectItem(j,"calibration"),&cal,s.uid,cJSON_IsTrue(cJSON_GetObjectItem(j,"confirm_transfer")))) { err="INVALID_CALIBRATION"; goto done; }
            xSemaphoreTake(state_lock,portMAX_DELAY); state.wizard.candidate=cal; state.wizard.state=4; app_event_locked("CALIBRATION","IMPORTED_CANDIDATE"); xSemaphoreGive(state_lock); goto done;
        }
        else if(!strcmp(name,"cal_apply")) { if(s.wizard.state!=4) { err="INVALID_STATE"; goto done; } op.kind=OP_CAL; op.cal=s.wizard.candidate; }
        else if(!strcmp(name,"cal_reset")) { lab_config c; lab_defaults(&c,&op.cal,s.uid); op.kind=OP_CAL; }
        else if(!strcmp(name,"cal_gyro")) op.kind=OP_GYRO;
        else if(!strcmp(name,"cal_accel")) op.kind=OP_ACCEL;
        else if(!strcmp(name,"cal_face")) { uint32_t face; if(!num(j,"face",&face)||face>5) { err="BAD_COMMAND"; goto done; } op.kind=OP_FACE; op.face=face; }
        else if(!strcmp(name,"cal_cancel")) op.kind=OP_CANCEL;
        else if(!strcmp(name,"reset_orientation")) op.kind=OP_RESET;
        else if(!strcmp(name,"zero_yaw")) op.kind=OP_ZERO;
        else if(!strcmp(name,"start_experiment")) { op.kind=OP_START; snprintf(op.owner,sizeof(op.owner),"%s",client); if(s.experiment) { if(strcmp(s.owner,client)) { err="BUSY"; goto done; } goto done; } }
        else if(!strcmp(name,"end_experiment")) { op.kind=OP_END; if(s.experiment && strcmp(s.owner,client) && !cJSON_IsTrue(cJSON_GetObjectItem(j,"confirm_owner"))) { err="BUSY"; why="Other owner; explicit confirm_owner required"; goto done; } }
        else known=false;
        if(!known) { err="UNKNOWN_COMMAND"; goto done; }
        esp_err_t e=app_operation(&op); if(e!=ESP_OK) { err=e==ESP_ERR_INVALID_STATE?"INVALID_STATE":"BUSY"; goto done; }
    }
done:
    if(err) error(r,err,why); else cJSON_AddBoolToObject(r,"ok",true);
    char *text=cJSON_PrintUnformatted(r);
    if(text) {
        if(mutation) lab_request_store(&cache,client,id,hash,text);
        bool sent=endpoint_send_json(cmd->ep,text);
        if(sent && subscribe && !err) endpoint_subscribe(cmd->ep,s.stream,cursor);
        free(text);
    }
    cJSON_Delete(j); cJSON_Delete(r);
}
static void command_task(void *arg) { command c; for(;;) if(xQueueReceive(commands,&c,portMAX_DELAY)==pdTRUE) { handle(&c); free(c.text); } }
void commands_start(void) { commands=xQueueCreate(4,sizeof(command)); xTaskCreatePinnedToCore(command_task,"commands",10240,NULL,4,&command_handle,0); }
