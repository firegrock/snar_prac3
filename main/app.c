#include "app.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

SemaphoreHandle_t state_lock,op_done;
QueueHandle_t op_queue;
int op_result;
lab_snapshot state;
lab_ring history;
TaskHandle_t sensor_handle,lcd_handle,transport_handle,command_handle;
static lab_event events[LAB_EVENTS];
static uint64_t random64(void) { return ((uint64_t)esp_random()<<32)|esp_random(); }
void app_snapshot(lab_snapshot *s) {
    xSemaphoreTake(state_lock,portMAX_DELAY); *s=state; s->oldest=history.next-history.count; s->next=history.next; xSemaphoreGive(state_lock);
}
void app_event_locked(const char *type,const char *text) {
    lab_event *e=&events[state.event_next%LAB_EVENTS];
    *e=(lab_event){.id=state.event_next++,.seq=history.next,.config_id=state.config_id,.t_us=esp_timer_get_time(),.stream=state.stream};
    snprintf(e->type,sizeof(e->type),"%s",type); snprintf(e->text,sizeof(e->text),"%s",text);
}
size_t app_events(uint32_t after,lab_event *out,size_t cap,bool *lost) {
    xSemaphoreTake(state_lock,portMAX_DELAY); uint32_t first=state.event_next>LAB_EVENTS?state.event_next-LAB_EVENTS:0;
    *lost=after<first; if(after<first) after=first; size_t n=state.event_next-after; if(n>cap) n=cap;
    for(size_t i=0;i<n;i++) out[i]=events[(after+i)%LAB_EVENTS];
    xSemaphoreGive(state_lock); return n;
}
void app_boundary_locked(bool reset) {
    state.stream=random64(); ++state.config_id; lab_ring_clear(&history);
    if(reset) lab_att_reset(&state.att);
    state.last.flags|=LAB_ORIENT_INVALID;
    state.initial=state.att; state.boundary_seq=0;
    app_event_locked("STREAM_BOUNDARY","History cleared; fetch metadata before subscribing");
}
esp_err_t app_operation(const lab_op *op) {
    if(xQueueSend(op_queue,op,pdMS_TO_TICKS(50))!=pdTRUE) return ESP_ERR_TIMEOUT;
    /* Единственный диспетчер. Драйвер I2C имеет конечные timeout. */
    xSemaphoreTake(op_done,portMAX_DELAY); return op_result;
}
bool calibration_save(const lab_cal *cal) {
    lab_saved_cal blob={.version=1,.size=sizeof(lab_cal),.cal=*cal}; blob.crc=lab_crc32((uint8_t*)&blob,offsetof(lab_saved_cal,crc));
    nvs_handle_t h; if(nvs_open("snar",NVS_READWRITE,&h)!=ESP_OK) return false;
    bool ok=nvs_set_blob(h,"cal",&blob,sizeof(blob))==ESP_OK && nvs_commit(h)==ESP_OK; nvs_close(h); return ok;
}
static void load_cal(void) {
    esp_err_t err=nvs_flash_init();
    if(err!=ESP_OK) { snprintf(state.nvs_status,sizeof(state.nvs_status),"NVS_INIT_%s (not erased)",esp_err_to_name(err)); return; }
    state.nvs_ok=true; strcpy(state.nvs_status,"UNCALIBRATED: no saved coefficients");
    nvs_handle_t h; if(nvs_open("snar",NVS_READONLY,&h)!=ESP_OK) return;
    lab_saved_cal blob; size_t n=sizeof(blob); err=nvs_get_blob(h,"cal",&blob,&n); nvs_close(h);
    if(err==ESP_ERR_NVS_NOT_FOUND) return;
    if(err!=ESP_OK || !lab_saved_valid(&blob,n,state.uid)) {
        strcpy(state.nvs_status,"UNCALIBRATED: invalid version/CRC/UID/coefficients"); return;
    }
    state.cal=blob.cal; strcpy(state.nvs_status,"LOADED");
}
void app_main(void) {
    esp_log_level_set("*",ESP_LOG_NONE);
    state_lock=xSemaphoreCreateMutex(); op_done=xSemaphoreCreateBinary(); op_queue=xQueueCreate(1,sizeof(lab_op));
    uint8_t mac[6]; ESP_ERROR_CHECK(esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP));
    for(int i=0;i<6;i++) state.uid=(state.uid<<8)|mac[i];
    state.boot=random64(); state.backlight=true;
    lab_defaults(&state.config,&state.cal,state.uid); lab_att_reset(&state.att); load_cal(); app_boundary_locked(false);
    commands_start();
    xTaskCreatePinnedToCore(sensor_task,"sensor",6144,NULL,20,&sensor_handle,1);
    xTaskCreatePinnedToCore(lcd_task,"lcd",4096,NULL,2,&lcd_handle,0);
    transport_start();
}
