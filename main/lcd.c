#include "app.h"
#include "driver/i2c_master.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static i2c_master_dev_handle_t lcd;
static bool bl=true;
static esp_err_t pcf(uint8_t value) {
    if(bl==!!LCD_BL_ACTIVE_HIGH) value|=1<<LCD_BL;
    return i2c_master_transmit(lcd,&value,1,20);
}
static esp_err_t nibble(uint8_t v,bool rs) {
    const int pins[]={LCD_D4,LCD_D5,LCD_D6,LCD_D7}; uint8_t p=rs?(1<<LCD_RS):0;
    for(int i=0;i<4;i++) if(v&(1<<i)) p|=1<<pins[i];
    esp_err_t e=pcf(p); if(e==ESP_OK) e=pcf(p|(1<<LCD_E)); if(e==ESP_OK) e=pcf(p); return e;
}
static esp_err_t byte(uint8_t v,bool rs) { esp_err_t e=nibble(v>>4,rs); return e==ESP_OK?nibble(v&15,rs):e; }
static bool init_lcd(void) {
    const int pins[]={LCD_RS,LCD_RW,LCD_E,LCD_BL,LCD_D4,LCD_D5,LCD_D6,LCD_D7}; unsigned mask=0;
    for(int i=0;i<8;i++) { if(pins[i]<0 || pins[i]>7 || (mask&(1<<pins[i]))) return false; mask|=1<<pins[i]; }
    vTaskDelay(pdMS_TO_TICKS(50));
    if(nibble(3,false)!=ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(5));
    if(nibble(3,false)!=ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(1));
    if(nibble(3,false)!=ESP_OK || nibble(2,false)!=ESP_OK) return false;
    if(byte(0x28,false)!=ESP_OK || byte(0x0c,false)!=ESP_OK || byte(0x06,false)!=ESP_OK || byte(0x01,false)!=ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(3)); return true;
}
static void pad(char dst[17],const char *src) { memset(dst,' ',16); size_t n=strlen(src); if(n>16) n=16; memcpy(dst,src,n); dst[16]=0; }
void lcd_task(void *arg) {
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bc={.i2c_port=I2C_NUM_1,.sda_io_num=LCD_SDA,.scl_io_num=LCD_SCL,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    esp_err_t e=i2c_new_master_bus(&bc,&bus); int found=0,address=0;
    if(e==ESP_OK) {
        for(int a=0x20;a<=0x3f;a++) if((a<=0x27 || a>=0x38) && i2c_master_probe(bus,a,20)==ESP_OK) { address=a; found++; }
        if(LCD_ADDRESS) address=LCD_ADDRESS; else if(found!=1) address=0;
        if(address) { i2c_device_config_t dc={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=address,.scl_speed_hz=LCD_BUS_HZ}; e=i2c_master_bus_add_device(bus,&dc,&lcd); } else e=ESP_ERR_NOT_FOUND;
    }
    bool ok=e==ESP_OK && init_lcd();
    xSemaphoreTake(state_lock,portMAX_DELAY); state.lcd_ok=ok; state.lcd_address=address; state.lcd_ack_count=found;
    app_event_locked(ok?"LCD_READY":"LCD_ERROR",ok?"ACK/init only; human must verify mapping and contrast":"Missing/ambiguous address or I2C error"); xSemaphoreGive(state_lock);
    char previous[2][17]={{0}};
    for(;;) {
        lab_snapshot s; app_snapshot(&s);
        if(ok) {
            char text[2][80],lines[2][17]; bl=s.backlight;
            if(s.lcd_page==4) { strcpy(text[0],"0123456789ABCDEF"); strcpy(text[1],"LCD TEST 16x2 OK?"); }
            else if(s.lcd_page==1) { snprintf(text[0],80,"KIT %02d WiFi %d",LAB_DEVICE_ID,wifi_station_count()); snprintf(text[1],80,"%3dHz F%lu I%lu",s.config.rate,(unsigned long)s.fifo_overflows,(unsigned long)s.irq_overflows); }
            else if(s.lcd_page==2) { snprintf(text[0],80,"SNAR_IMU_%02d",LAB_DEVICE_ID); strcpy(text[1],"192.168.4.1"); }
            else if(s.lcd_page==3) { snprintf(text[0],80,"CAL S%d FACE%d",s.wizard.state,s.wizard.face); snprintf(text[1],80,"%lu/%lu %s",(unsigned long)s.wizard.n,(unsigned long)s.wizard.target,s.wizard.state==4?"CAND":""); }
            else if(!s.sensor_ok) { snprintf(text[0],80,"KIT %02d MPU ERR",LAB_DEVICE_ID); strcpy(text[1],"CHECK MPU / INT"); }
            else if(!s.att.ready) { snprintf(text[0],80,"KIT %02d WAIT STILL",LAB_DEVICE_ID); strcpy(text[1],s.cal.valid?"CAL NO ATTITUDE":"UNCAL NO ATTITUDE"); }
            else { float r[3]; lab_euler(s.att.q,r); snprintf(text[0],80,"R%6.1f P%6.1f",r[0]*180/M_PI,r[1]*180/M_PI); snprintf(text[1],80,"Y%6.1f %s %02d",r[2]*180/M_PI,s.cal.valid?"CAL":"UNCAL",LAB_DEVICE_ID); }
            for(int row=0;row<2;row++) {
                pad(lines[row],text[row]);
                for(int col=0;col<16;col++) if(lines[row][col]!=previous[row][col]) {
                    if(byte(0x80+(row?0x40:0)+col,false)!=ESP_OK || byte(lines[row][col],true)!=ESP_OK) { ok=false; break; }
                    previous[row][col]=lines[row][col];
                }
            }
            /* Also applies backlight when characters haven't changed. */
            if(pcf(0)!=ESP_OK) ok=false;
            if(!ok) { xSemaphoreTake(state_lock,portMAX_DELAY); state.lcd_ok=false; app_event_locked("LCD_ERROR","I2C write; measurement continues"); xSemaphoreGive(state_lock); }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
