#pragma once
#include "lab_config.h"
#include "lab_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "cJSON.h"
#include "esp_err.h"

typedef struct { uint32_t id,seq,config_id; int64_t t_us; uint64_t stream; char type[24],text[100]; } lab_event;
typedef struct {
    uint64_t uid,boot,stream;
    uint32_t config_id;
    lab_config config;
    lab_cal cal;
    lab_attitude att;
    lab_sample last;
    bool sensor_ok,lcd_ok,nvs_ok,experiment;
    char sensor_error[80],nvs_status[64],owner[64];
    int lcd_page; bool backlight;
    int lcd_address, lcd_ack_count;
    int mpu_whoami;
    uint32_t fifo_overflows,irq_overflows,sync_errors,read_errors,resyncs,transport_drops,alignment_discards;
    uint32_t oldest,next,event_next;
    lab_wizard wizard;
    lab_attitude initial;
    uint32_t boundary_seq;
    int64_t start_us;
} lab_snapshot;
typedef enum { OP_CONFIG,OP_CAL,OP_RESET,OP_ZERO,OP_START,OP_END,OP_GYRO,OP_ACCEL,OP_FACE,OP_CANCEL } lab_op_kind;
typedef struct { lab_op_kind kind; lab_config config; lab_cal cal; int face; char owner[64]; } lab_op;

extern SemaphoreHandle_t state_lock;
extern lab_snapshot state;
extern lab_ring history;
extern QueueHandle_t op_queue;
extern SemaphoreHandle_t op_done;
extern int op_result;
extern TaskHandle_t sensor_handle,lcd_handle,transport_handle,command_handle;

void app_snapshot(lab_snapshot *s);
void app_event_locked(const char *type,const char *text);
size_t app_events(uint32_t after,lab_event *out,size_t cap,bool *lost);
void app_boundary_locked(bool reset);
esp_err_t app_operation(const lab_op *op);
void sensor_task(void *arg);
void lcd_task(void *arg);
void transport_start(void);
void commands_start(void);
bool command_enqueue(int endpoint,const char *json,size_t n);
bool endpoint_send_json(int ep,const char *text);
bool endpoint_subscribe(int ep,uint64_t stream,uint32_t cursor);
void endpoint_unsubscribe(int ep);
int endpoint_count(void);
int wifi_station_count(void);
void endpoint_ack(int ep,uint32_t seq);
cJSON *model_info(const lab_snapshot *s);
cJSON *model_status(const lab_snapshot *s);
cJSON *model_metadata(const lab_snapshot *s);
cJSON *model_cal(const lab_cal *c);
bool model_parse_cal(const cJSON *j,lab_cal *c,uint64_t uid,bool transfer);
bool model_parse_config(const cJSON *j,lab_config *c);
bool model_u64(const cJSON *j,uint64_t *v);
void model_u64_add(cJSON *j,const char *key,uint64_t v);
bool calibration_save(const lab_cal *cal);
