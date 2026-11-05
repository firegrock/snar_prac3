#include "app.h"
#include "driver/uart.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    int id,fd; bool active,subscribed,pending,queued,text;
    uint32_t cursor,ack,replay_until;
    uint64_t stream;
    int64_t ack_us,pending_us;
    size_t len;
    uint8_t tx[LAB_PACKET_MAX];
    size_t follow_len;
    uint8_t follow[42+LAB_RECORD*LAB_BATCH]; /* GAP then this immutable prefix */
} endpoint;
static endpoint endpoints[LAB_ENDPOINTS];
static SemaphoreHandle_t ep_lock;
static httpd_handle_t server;
static bool uart_ready;
static uint8_t uart_encoded[LAB_PACKET_MAX+LAB_PACKET_MAX/254+2];
static uint8_t uart_rx[LAB_PACKET_MAX+LAB_PACKET_MAX/254+2],uart_decoded[LAB_PACKET_MAX];
static size_t uart_rx_n; static bool uart_discard;

int endpoint_count(void) { int n=0; xSemaphoreTake(ep_lock,portMAX_DELAY); for(int i=1;i<LAB_ENDPOINTS;i++) n+=endpoints[i].active; xSemaphoreGive(ep_lock); return n; }
int wifi_station_count(void) { wifi_sta_list_t list; return esp_wifi_ap_get_sta_list(&list)==ESP_OK?list.num:0; }
bool endpoint_send_json(int id,const char *text) {
    size_t n=strlen(text); if(id<0||id>=LAB_ENDPOINTS||n>LAB_JSON_MAX) return false;
    /* Один ограниченный pending на получателя; диспетчер не блокирует сенсор. */
    for(int attempt=0;attempt<200;attempt++) {
        xSemaphoreTake(ep_lock,portMAX_DELAY); endpoint *e=&endpoints[id];
        if(!e->active) { xSemaphoreGive(ep_lock); return false; }
        if(!e->pending) {
            if(id==0) { lab_snapshot s; app_snapshot(&s); e->len=lab_packet(e->tx,sizeof(e->tx),3,0,s.uid,s.boot,s.stream,s.config_id,(uint8_t*)text,n); e->text=false; }
            else { memcpy(e->tx,text,n); e->len=n; e->text=true; }
            e->pending=true; e->pending_us=esp_timer_get_time(); xSemaphoreGive(ep_lock); return true;
        }
        xSemaphoreGive(ep_lock); vTaskDelay(pdMS_TO_TICKS(1));
    }
    return false;
}
bool endpoint_subscribe(int id,uint64_t stream,uint32_t cursor) {
    xSemaphoreTake(ep_lock,portMAX_DELAY); endpoint *e=&endpoints[id]; e->stream=stream; e->cursor=cursor; e->subscribed=true; e->follow_len=0; e->ack_us=esp_timer_get_time();
    xSemaphoreTake(state_lock,portMAX_DELAY); e->replay_until=history.next; xSemaphoreGive(state_lock); xSemaphoreGive(ep_lock); return true;
}
void endpoint_unsubscribe(int id) { xSemaphoreTake(ep_lock,portMAX_DELAY); endpoints[id].subscribed=false; endpoints[id].follow_len=0; xSemaphoreGive(ep_lock); }
void endpoint_ack(int id,uint32_t seq) { xSemaphoreTake(ep_lock,portMAX_DELAY); endpoints[id].ack=seq; endpoints[id].ack_us=esp_timer_get_time(); xSemaphoreGive(ep_lock); }
/* Nonblocking send override: never wait seconds for one slow socket in HTTPD.
 * On EAGAIN/partial-frame failure httpd closes it; client resumes from flushed ACK.
 * Called only by the HTTP server task, not the sampler/network pump. */
static int nonblocking_send(httpd_handle_t h,int fd,const char *buf,size_t n,int flags) { return send(fd,buf,n,flags|MSG_DONTWAIT); }
static void send_work(void *arg) {
    endpoint *e=arg;
    xSemaphoreTake(ep_lock,portMAX_DELAY);
    if(!e->active) { e->queued=e->pending=false; xSemaphoreGive(ep_lock); return; }
    int fd=e->fd; httpd_ws_frame_t frame={.type=e->text?HTTPD_WS_TYPE_TEXT:HTTPD_WS_TYPE_BINARY,.payload=e->tx,.len=e->len};
    xSemaphoreGive(ep_lock);
    esp_err_t err=httpd_ws_send_frame_async(server,fd,&frame);
    if(err!=ESP_OK) httpd_sess_trigger_close(server,fd);
    xSemaphoreTake(ep_lock,portMAX_DELAY); e->pending=e->queued=false; if(err!=ESP_OK) e->subscribed=false; xSemaphoreGive(ep_lock);
    if(err!=ESP_OK) { xSemaphoreTake(state_lock,portMAX_DELAY); state.transport_drops++; app_event_locked("SLOW_CLIENT","Nonblocking WS send failed; resume required"); xSemaphoreGive(state_lock); }
}
static void close_socket(httpd_handle_t h,int fd) {
    xSemaphoreTake(ep_lock,portMAX_DELAY);
    for(int i=1;i<LAB_ENDPOINTS;i++) if(endpoints[i].fd==fd && endpoints[i].active) { endpoints[i].active=false; endpoints[i].subscribed=false; }
    xSemaphoreGive(ep_lock); close(fd);
}
static esp_err_t ws_handler(httpd_req_t *req) {
    int fd=httpd_req_to_sockfd(req),id=-1;
    /* This SDK completes the upgrade in httpd_uri() WITHOUT calling the URI
     * handler. The first invocation is a data/control frame (method may be 0),
     * not an HTTP_GET handshake callback. Register lazily after a real upgrade. */
    if(httpd_ws_get_fd_info(server,fd)!=HTTPD_WS_CLIENT_WEBSOCKET) return ESP_FAIL;
    bool created=false;
    xSemaphoreTake(ep_lock,portMAX_DELAY);
    for(int i=1;i<LAB_ENDPOINTS;i++) if(endpoints[i].active && endpoints[i].fd==fd) id=i;
    if(id<0) {
        for(int i=1;i<LAB_ENDPOINTS;i++) if(!endpoints[i].active && !endpoints[i].queued) { id=i; endpoints[i]=(endpoint){.id=i,.fd=fd,.active=true}; created=true; break; }
    }
    xSemaphoreGive(ep_lock);
    if(id<0) return ESP_FAIL;
    if(created && httpd_sess_set_send_override(server,fd,nonblocking_send)!=ESP_OK) return ESP_FAIL;
    httpd_ws_frame_t f={0}; if(httpd_ws_recv_frame(req,&f,0)!=ESP_OK) return ESP_FAIL;
    if(f.type==HTTPD_WS_TYPE_CLOSE) return ESP_FAIL;
    /* SDK also dispatches PONG to the data handler. Consume it, don't close a
     * healthy connection simply because it is not a JSON command. */
    if(f.type==HTTPD_WS_TYPE_PONG) {
        if(f.len>125) return ESP_FAIL;
        uint8_t control[125]; f.payload=control;
        return f.len?httpd_ws_recv_frame(req,&f,f.len):ESP_OK;
    }
    if(f.type!=HTTPD_WS_TYPE_TEXT || f.len>LAB_COMMAND_MAX || !f.len || !f.final) return ESP_FAIL;
    char *b=malloc(f.len+1); if(!b) return ESP_ERR_NO_MEM; f.payload=(uint8_t*)b;
    esp_err_t err=httpd_ws_recv_frame(req,&f,f.len);
    if(err==ESP_OK && !command_enqueue(id,b,f.len)) {
        const char *busy="{\"request_id\":\"\",\"ok\":false,\"error\":{\"code\":\"BUSY\",\"message\":\"bounded command queue full\"}}";
        httpd_ws_frame_t response={.type=HTTPD_WS_TYPE_TEXT,.payload=(uint8_t*)busy,.len=strlen(busy)};
        err=httpd_ws_send_frame(req,&response);
    }
    free(b); return err;
}
extern const char html_start[] asm("_binary_index_html_start");
extern const char js_start[] asm("_binary_app_js_start");
extern const char css_start[] asm("_binary_style_css_start");
static esp_err_t asset(httpd_req_t *r) {
    const char *text=html_start,*type="text/html; charset=utf-8";
    if(!strcmp(r->uri,"/app.js")) { text=js_start; type="application/javascript"; }
    if(!strcmp(r->uri,"/style.css")) { text=css_start; type="text/css"; }
    httpd_resp_set_type(r,type); httpd_resp_set_hdr(r,"Cache-Control","no-cache"); return httpd_resp_send(r,text,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t api(httpd_req_t *r) {
    lab_snapshot s; app_snapshot(&s); cJSON *j=!strcmp(r->uri,"/api/info")?model_info(&s):model_status(&s); char *text=cJSON_PrintUnformatted(j); cJSON_Delete(j);
    httpd_resp_set_type(r,"application/json"); esp_err_t e=text?httpd_resp_send(r,text,HTTPD_RESP_USE_STRLEN):ESP_ERR_NO_MEM; free(text); return e;
}
static void uart_receive(void) {
    if(!uart_ready) return;
    uint8_t b[256]; int n=uart_read_bytes(UART_NUM_0,b,sizeof(b),0);
    for(int i=0;i<n;i++) {
        if(b[i]==0) {
            if(!uart_discard && uart_rx_n) {
                size_t len=lab_cobs_decode(uart_rx,uart_rx_n,uart_decoded,sizeof(uart_decoded));
                if(lab_packet_valid(uart_decoded,len) && uart_decoded[3]==2 && len-40<=LAB_COMMAND_MAX) command_enqueue(0,(char*)uart_decoded+LAB_HEADER,len-40);
            }
            uart_rx_n=0; uart_discard=false;
        } else if(!uart_discard) { if(uart_rx_n<sizeof(uart_rx)) uart_rx[uart_rx_n++]=b[i]; else { uart_discard=true; uart_rx_n=0; } }
    }
}
static void pump(void *arg) {
    int64_t hello_us=0;
    for(;;) {
        uart_receive();
        for(int id=0;id<LAB_ENDPOINTS;id++) {
            xSemaphoreTake(ep_lock,portMAX_DELAY); endpoint *e=&endpoints[id];
            if(!e->active) { xSemaphoreGive(ep_lock); continue; }
            if(e->subscribed && esp_timer_get_time()-e->ack_us>2500000) { e->subscribed=false; if(id) httpd_sess_trigger_close(server,e->fd); }
            if(!e->pending && e->subscribed && e->follow_len) {
                /* Do NOT re-read ring before sending this prefix. Otherwise
                 * oldest can advance after every GAP and starve resume forever. */
                memcpy(e->tx,e->follow,e->follow_len); e->len=e->follow_len; e->follow_len=0;
                e->text=false; e->pending=true; e->pending_us=esp_timer_get_time();
            }
            if(!e->pending && e->subscribed) {
                lab_sample batch[LAB_BATCH]; bool gap=false; uint64_t uid,boot,stream; uint32_t config,next,oldest;
                xSemaphoreTake(state_lock,portMAX_DELAY); uid=state.uid; boot=state.boot; stream=state.stream; config=state.config_id; next=history.next; oldest=next-history.count;
                uint32_t old_cursor=e->cursor,effective=e->cursor<oldest?oldest:e->cursor;
                size_t cap=LAB_BATCH;
                if(e->replay_until>effective && e->replay_until-effective<cap) cap=e->replay_until-effective;
                size_t n=e->stream==stream?lab_ring_get(&history,&e->cursor,batch,cap,&gap):0;
                xSemaphoreGive(state_lock);
                if(e->stream!=stream || gap) {
                    char text[256]; snprintf(text,sizeof(text),"{\"event\":\"%s\",\"from_seq\":%lu,\"oldest_seq\":%lu,\"next_seq\":%lu,\"reason\":\"%s\"}",e->stream!=stream?"STREAM_BOUNDARY":"GAP",(unsigned long)old_cursor,(unsigned long)oldest,(unsigned long)next,e->stream!=stream?"configuration changed; resubscribe":"RAM history overwritten; old event detail may be unavailable");
                    if(id) { memcpy(e->tx,text,strlen(text)); e->len=strlen(text); e->text=true; }
                    else e->len=lab_packet(e->tx,sizeof(e->tx),4,0,uid,boot,stream,config,(uint8_t*)text,strlen(text));
                    if(e->stream!=stream) e->subscribed=false;
                    else {
                        e->replay_until=next;
                        /* Cursor already advances past the copied prefix. Keep
                         * that prefix bounded in RAM until GAP has been sent. */
                        uint8_t payload[2+LAB_RECORD*LAB_BATCH]; size_t bytes=lab_samples_payload(payload,batch,n);
                        e->follow_len=lab_packet(e->follow,sizeof(e->follow),1,LAB_REPLAY,uid,boot,stream,config,payload,bytes);
                    }
                    e->pending=true;
                } else if(n) {
                    uint8_t payload[2+LAB_RECORD*LAB_BATCH]; size_t len=lab_samples_payload(payload,batch,n);
                    e->len=lab_packet(e->tx,sizeof(e->tx),1,batch[0].seq<e->replay_until?LAB_REPLAY:0,uid,boot,stream,config,payload,len); e->pending=true; e->text=false;
                }
                if(e->pending) e->pending_us=esp_timer_get_time();
            }
            if(id==0 && uart_ready && !e->pending && !e->subscribed && esp_timer_get_time()-hello_us>1000000) {
                hello_us=esp_timer_get_time(); lab_snapshot s; app_snapshot(&s); cJSON *j=model_info(&s); char *text=cJSON_PrintUnformatted(j); cJSON_Delete(j);
                if(text) { e->len=lab_packet(e->tx,sizeof(e->tx),5,0,s.uid,s.boot,s.stream,s.config_id,(uint8_t*)text,strlen(text)); free(text); e->pending=true; e->pending_us=hello_us; }
            }
            if(e->pending) {
                if(!id && uart_ready) {
                    size_t len=lab_cobs_encode(e->tx,e->len,uart_encoded,sizeof(uart_encoded)); size_t free_bytes=0; uart_get_tx_buffer_free_size(UART_NUM_0,&free_bytes);
                    if(len && free_bytes>=len+1) { uart_encoded[len++]=0; uart_write_bytes(UART_NUM_0,uart_encoded,len); e->pending=false; }
                    else if(esp_timer_get_time()-e->pending_us>500000) { e->pending=false; e->subscribed=false; }
                } else if(id && server && !e->queued) { e->queued=true; if(httpd_queue_work(server,send_work,e)!=ESP_OK) { e->queued=false; e->pending=false; e->subscribed=false; } }
            }
            xSemaphoreGive(ep_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(5)); /* <=5ms normal batching, catch-up 1600 samples/s */
    }
}
/* Network startup failure is diagnostic, never an ESP_ERROR_CHECK reboot of
 * the independent sampler. UART remains usable if AP/HTTP startup failed. */
#define NET_TRY(call) do { esp_err_t e=(call); if(e!=ESP_OK) return e; } while(0)
static esp_err_t network_start(void) {
    NET_TRY(esp_netif_init()); NET_TRY(esp_event_loop_create_default());
    esp_netif_t *ap=esp_netif_create_default_wifi_ap();
    if(!ap) return ESP_ERR_NO_MEM;
    esp_netif_ip_info_t ip; IP4_ADDR(&ip.ip,192,168,4,1); IP4_ADDR(&ip.gw,192,168,4,1); IP4_ADDR(&ip.netmask,255,255,255,0);
    esp_netif_dhcps_stop(ap); NET_TRY(esp_netif_set_ip_info(ap,&ip)); esp_netif_dhcps_start(ap);
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT(); NET_TRY(esp_wifi_init(&init)); NET_TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wc={0}; snprintf((char*)wc.ap.ssid,sizeof(wc.ap.ssid),"SNAR_IMU_%02d",LAB_DEVICE_ID); wc.ap.ssid_len=strlen((char*)wc.ap.ssid); strcpy((char*)wc.ap.password,LAB_AP_PASSWORD);
    wc.ap.channel=LAB_WIFI_CHANNEL; wc.ap.max_connection=4; wc.ap.authmode=WIFI_AUTH_WPA2_PSK;
    NET_TRY(esp_wifi_set_mode(WIFI_MODE_AP)); NET_TRY(esp_wifi_set_config(WIFI_IF_AP,&wc)); NET_TRY(esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_BW20)); NET_TRY(esp_wifi_start());
    httpd_config_t hc=HTTPD_DEFAULT_CONFIG(); hc.max_open_sockets=10; hc.max_uri_handlers=8; hc.stack_size=6144; hc.send_wait_timeout=1; hc.recv_wait_timeout=1; hc.close_fn=close_socket; hc.lru_purge_enable=true;
    NET_TRY(httpd_start(&server,&hc));
    const char *paths[]={"/","/app.js","/style.css","/api/info","/api/status"};
    for(int i=0;i<5;i++) { httpd_uri_t u={.uri=paths[i],.method=HTTP_GET,.handler=i<3?asset:api}; NET_TRY(httpd_register_uri_handler(server,&u)); }
    httpd_uri_t ws={.uri="/ws",.method=HTTP_GET,.handler=ws_handler,.is_websocket=true}; NET_TRY(httpd_register_uri_handler(server,&ws));
    return ESP_OK;
}
void transport_start(void) {
    ep_lock=xSemaphoreCreateMutex(); for(int i=0;i<LAB_ENDPOINTS;i++) endpoints[i].id=i;
    uart_config_t uc={.baud_rate=LAB_UART_BAUD,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    esp_err_t e=uart_driver_install(UART_NUM_0,8192,8192,0,NULL,0);
    if(e==ESP_OK) e=uart_param_config(UART_NUM_0,&uc);
    if(e==ESP_OK) e=uart_set_pin(UART_NUM_0,LAB_UART_TX,LAB_UART_RX,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
    uart_ready=e==ESP_OK; endpoints[0].active=uart_ready;
    if(!uart_ready) { xSemaphoreTake(state_lock,portMAX_DELAY); app_event_locked("UART_ERROR",esp_err_to_name(e)); xSemaphoreGive(state_lock); }
    e=network_start();
    if(e!=ESP_OK) { xSemaphoreTake(state_lock,portMAX_DELAY); app_event_locked("NETWORK_ERROR",esp_err_to_name(e)); xSemaphoreGive(state_lock); }
    if(xTaskCreatePinnedToCore(pump,"transport",7168,NULL,5,&transport_handle,0)!=pdPASS) { xSemaphoreTake(state_lock,portMAX_DELAY); app_event_locked("TRANSPORT_ERROR","Task allocation failed; sampler continues"); xSemaphoreGive(state_lock); }
}
