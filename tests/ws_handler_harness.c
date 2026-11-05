/* The included function is extracted verbatim from main/transport.c by the
 * Python test. SDK calls are mocks; this is NOT an RF/HTTPD integration test. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define HTTP_GET 1
#define HTTPD_WS_CLIENT_HTTP 1
#define HTTPD_WS_CLIENT_WEBSOCKET 2
#define HTTPD_WS_TYPE_TEXT 1
#define HTTPD_WS_TYPE_CLOSE 8
#define HTTPD_WS_TYPE_PONG 10
#define LAB_ENDPOINTS 6
#define LAB_COMMAND_MAX 4096
#define portMAX_DELAY 0
typedef int esp_err_t;
typedef int httpd_handle_t;
typedef struct { int method,fd; } httpd_req_t;
typedef struct { int type; bool final; uint8_t *payload; size_t len; } httpd_ws_frame_t;
typedef struct { int id,fd; bool active,queued; } endpoint;
static endpoint endpoints[LAB_ENDPOINTS];
static int ep_lock,server,depth,overrides,queued,header_reads,body_reads,busy_replies;
static int client_info=HTTPD_WS_CLIENT_WEBSOCKET,frame_type=HTTPD_WS_TYPE_TEXT;
static bool final=true,enqueue_ok=true;
static const char command[]="{\"request_id\":\"browser-1\",\"client_id\":\"browser\",\"command\":\"get_info\"}";
static size_t frame_len=sizeof(command)-1;
static int xSemaphoreTake(int lock,int timeout){(void)lock;(void)timeout;assert(depth++==0);return 1;}
static int xSemaphoreGive(int lock){(void)lock;assert(--depth==0);return 1;}
static int httpd_req_to_sockfd(httpd_req_t *r){return r->fd;}
static int httpd_ws_get_fd_info(int h,int fd){(void)h;(void)fd;return client_info;}
static int nonblocking_send(int h,int fd,const char *b,size_t n,int flags){(void)h;(void)fd;(void)b;(void)flags;return n;}
static int httpd_sess_set_send_override(int h,int fd,int(*send_fn)(int,int,const char*,size_t,int)){
    (void)h;(void)fd;assert(send_fn==nonblocking_send);overrides++;return ESP_OK;
}
static int httpd_ws_recv_frame(httpd_req_t *req,httpd_ws_frame_t *f,size_t max){
    (void)req;
    if(!max){header_reads++;f->type=frame_type;f->final=final;f->len=frame_len;return ESP_OK;}
    body_reads++;assert(max==frame_len && f->payload);
    if(frame_type==HTTPD_WS_TYPE_TEXT){assert(frame_len==sizeof(command)-1);memcpy(f->payload,command,frame_len);}
    else memset(f->payload,0,frame_len);
    return ESP_OK;
}
static bool command_enqueue(int id,const char *text,size_t n){assert(id>0 && id<LAB_ENDPOINTS);assert(n==sizeof(command)-1 && !memcmp(text,command,n));queued++;return enqueue_ok;}
static int httpd_ws_send_frame(httpd_req_t *req,httpd_ws_frame_t *frame){(void)req;assert(frame->type==HTTPD_WS_TYPE_TEXT);assert(strstr((char*)frame->payload,"BUSY"));busy_replies++;return ESP_OK;}
#include "ws_handler_under_test.inc"
int main(void){
    /* ESP-IDF initializes method=0 for WS frames and skips the URI handler on
     * HTTP Upgrade. No earlier handshake invocation exists to reserve a slot. */
    httpd_req_t req={.method=0,.fd=42};
    assert(ws_handler(&req)==ESP_OK);assert(queued==1 && overrides==1 && header_reads==1 && body_reads==1);
    assert(endpoints[1].active && endpoints[1].fd==42);
    assert(ws_handler(&req)==ESP_OK);assert(queued==2 && overrides==1);
    frame_type=HTTPD_WS_TYPE_PONG;frame_len=3;assert(ws_handler(&req)==ESP_OK);assert(queued==2);
    frame_len=0;assert(ws_handler(&req)==ESP_OK);assert(queued==2);
    frame_type=HTTPD_WS_TYPE_TEXT;frame_len=sizeof(command)-1;
    final=false;assert(ws_handler(&req)==ESP_FAIL);final=true;
    frame_len=LAB_COMMAND_MAX+1;assert(ws_handler(&req)==ESP_FAIL);frame_len=sizeof(command)-1;
    enqueue_ok=false;assert(ws_handler(&req)==ESP_OK);assert(busy_replies==1);enqueue_ok=true;
    for(int fd=43;fd<47;fd++){req.fd=fd;assert(ws_handler(&req)==ESP_OK);}
    req.fd=47;assert(ws_handler(&req)==ESP_FAIL);assert(overrides==5);
    client_info=HTTPD_WS_CLIENT_HTTP;req.method=HTTP_GET;assert(ws_handler(&req)==ESP_FAIL);
    assert(depth==0);
    puts("PASS production WS handler: SDK skips handshake callback, first JSON, slot reuse/limit, PONG, bounds, BUSY");
    return 0;
}
