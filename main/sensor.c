#include "app.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static i2c_master_dev_handle_t dev;
static QueueHandle_t irq_queue;
static volatile uint32_t irq_lost;
static char driver_reason[80];
static void IRAM_ATTR data_ready(void *arg) {
    int64_t t=esp_timer_get_time(); BaseType_t wake=pdFALSE;
    if(xQueueSendFromISR(irq_queue,&t,&wake)!=pdTRUE) irq_lost++;
    if(wake) portYIELD_FROM_ISR();
}
static esp_err_t rd(uint8_t reg,uint8_t *out,size_t n) { return i2c_master_transmit_receive(dev,&reg,1,out,n,20); }
static esp_err_t wr(uint8_t reg,uint8_t v) { uint8_t b[2]={reg,v}; return i2c_master_transmit(dev,b,2,20); }
static int ar(int g) { return g==2?0:g==4?1:g==8?2:3; }
static int gr(int g) { return g==250?0:g==500?1:g==1000?2:3; }
static esp_err_t resync(void) {
    gpio_intr_disable(MPU_INT);
    esp_err_t err=wr(0x38,0); /* INT_ENABLE */
    if(err==ESP_OK) err=wr(0x23,0); /* FIFO_EN */
    if(err==ESP_OK) err=wr(0x6a,0x04); /* FIFO_RESET with FIFO disabled */
    xQueueReset(irq_queue); irq_lost=0;
    uint8_t status;
    if(err==ESP_OK) err=rd(0x3a,&status,1);
    /* FIFO sample: accel XYZ, temperature, gyro XYZ (14 bytes). */
    gpio_intr_enable(MPU_INT);
    if(err==ESP_OK) err=wr(0x38,0x11); /* overflow + data-ready */
    /* Synchronize on a deliberately discarded DRDY while FIFO is disabled.
     * Enable the FIFO strictly inside the first half-period after that edge.
     * This avoids changing MPU power state and an ambiguous startup prefix. */
    int64_t reference=0;
    if(err==ESP_OK && xQueueReceive(irq_queue,&reference,pdMS_TO_TICKS(200))!=pdTRUE) {
        snprintf(driver_reason,sizeof(driver_reason),"NO_DATA_READY during FIFO alignment"); err=ESP_ERR_TIMEOUT;
    }
    if(err==ESP_OK) err=wr(0x6a,0x40);
    if(err==ESP_OK) err=wr(0x23,0xf8);
    if(err==ESP_OK) {
        if(uxQueueMessagesWaiting(irq_queue) || esp_timer_get_time()-reference>500000/state.config.rate) {
            snprintf(driver_reason,sizeof(driver_reason),"FIFO alignment missed half-period deadline"); err=ESP_ERR_INVALID_STATE;
        } else {
            xSemaphoreTake(state_lock,portMAX_DELAY); state.alignment_discards++; app_event_locked("SYNC_REFERENCE","Reference DRDY deliberately discarded; FIFO starts next sample"); xSemaphoreGive(state_lock);
        }
    }
    if(err==ESP_OK) {
        const uint8_t regs[]={0x23,0x6a,0x38,0x6b};
        const uint8_t values[]={0xf8,0x40,0x11,0x01};
        for(unsigned i=0;i<sizeof(regs);i++) {
            uint8_t v=0; err=rd(regs[i],&v,1); if(err!=ESP_OK) break;
            if(v!=values[i]) {
                snprintf(driver_reason,sizeof(driver_reason),"VERIFY reg=0x%02x got=0x%02x expected=0x%02x",regs[i],v,values[i]);
                err=ESP_ERR_INVALID_RESPONSE; break;
            }
        }
    }
    return err;
}
static esp_err_t configure(const lab_config *c) {
    driver_reason[0]=0;
    uint8_t who=0;
    vTaskDelay(pdMS_TO_TICKS(100)); /* cold power-up, before identity access */
    esp_err_t e=rd(0x75,&who,1); if(e!=ESP_OK) return e;
    xSemaphoreTake(state_lock,portMAX_DELAY); state.mpu_whoami=who; xSemaphoreGive(state_lock);
    if((who&0x7e)!=0x68) { snprintf(driver_reason,sizeof(driver_reason),"WHO_AM_I=0x%02x; expected 0x68/0x69 identity",who); return ESP_ERR_INVALID_RESPONSE; }
    if((e=wr(0x6b,0x80))!=ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(100));
    if((e=wr(0x6b,0x01))!=ESP_OK) return e; /* PLL gyro X */
    if((e=wr(0x6c,0x00))!=ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(50));
    const uint8_t regs[]={0x19,0x1a,0x1b,0x1c,0x37,0x6b,0x6c};
    const uint8_t vals[]={(uint8_t)(1000/c->rate-1),(uint8_t)c->dlpf,(uint8_t)(gr(c->gyro_range)<<3),(uint8_t)(ar(c->accel_range)<<3),0,1,0};
    for(unsigned i=0;i<sizeof(regs);i++) if((e=wr(regs[i],vals[i]))!=ESP_OK) return e;
    for(unsigned i=0;i<sizeof(regs);i++) { uint8_t v; if((e=rd(regs[i],&v,1))!=ESP_OK) return e; if(v!=vals[i]) { snprintf(driver_reason,sizeof(driver_reason),"VERIFY reg=0x%02x got=0x%02x expected=0x%02x",regs[i],v,vals[i]); return ESP_ERR_INVALID_RESPONSE; } }
    return resync();
}
static void fault(const char *why,bool overflow,bool read_error) {
    xSemaphoreTake(state_lock,portMAX_DELAY);
    if(overflow) state.fifo_overflows++; else if(read_error) state.read_errors++; else state.sync_errors++;
    state.irq_overflows+=irq_lost; state.resyncs++;
    lab_att_reset(&state.att); app_event_locked("SENSOR_GAP",why);
    if(state.wizard.state==1 || state.wizard.state==3) { state.wizard.state=-1; strcpy(state.wizard.reason,"SENSOR_GAP"); }
    xSemaphoreGive(state_lock);
    esp_err_t e=resync();
    if(e!=ESP_OK) { xSemaphoreTake(state_lock,portMAX_DELAY); state.sensor_ok=false; snprintf(state.sensor_error,sizeof(state.sensor_error),"%s",driver_reason[0]?driver_reason:esp_err_to_name(e)); xSemaphoreGive(state_lock); }
}
static void operation(lab_op *op) {
    bool reconfigure=false;
    xSemaphoreTake(state_lock,portMAX_DELAY); op_result=ESP_OK;
    if(state.experiment && op->kind!=OP_END) op_result=ESP_ERR_INVALID_STATE;
    else switch(op->kind) {
    case OP_CONFIG: state.config=op->config; app_boundary_locked(true); reconfigure=true; break;
    case OP_CAL: state.cal=op->cal; app_boundary_locked(true); break;
    case OP_RESET: app_boundary_locked(true); break;
    case OP_ZERO: if(!state.att.ready) op_result=ESP_ERR_INVALID_STATE; else { lab_zero_yaw(&state.att); app_boundary_locked(false); } break;
    case OP_START:
        if(!state.sensor_ok) { op_result=ESP_ERR_INVALID_STATE; break; }
        if(state.wizard.state>0 && state.wizard.state<4) { op_result=ESP_ERR_INVALID_STATE; break; }
        state.experiment=true; snprintf(state.owner,sizeof(state.owner),"%s",op->owner);
        state.initial=state.att; state.boundary_seq=history.next; state.start_us=esp_timer_get_time(); app_event_locked("EXPERIMENT_START",state.owner); break;
    case OP_END: state.experiment=false; state.owner[0]=0; app_event_locked("EXPERIMENT_END","explicit end"); break;
    case OP_GYRO: case OP_ACCEL:
        lab_wizard_start(&state.wizard,&state.config,&state.cal,op->kind==OP_GYRO,LAB_GYRO_SECONDS);
        app_event_locked("CALIBRATION",op->kind==OP_GYRO?"GYRO_START":"SIX_FACE_START"); break;
    case OP_FACE:
        if(!lab_wizard_face(&state.wizard,op->face,state.config.rate,LAB_FACE_SECONDS)) op_result=ESP_ERR_INVALID_STATE;
        else { char s[32]; snprintf(s,sizeof(s),"FACE_%d",op->face); app_event_locked("CALIBRATION",s); } break;
    case OP_CANCEL: state.wizard.state=0; app_event_locked("CALIBRATION","CANCEL"); break;
    }
    lab_config config=state.config; xSemaphoreGive(state_lock);
    if(reconfigure) {
        esp_err_t e=configure(&config);
        xSemaphoreTake(state_lock,portMAX_DELAY); state.sensor_ok=e==ESP_OK;
        snprintf(state.sensor_error,sizeof(state.sensor_error),"%s",e!=ESP_OK && driver_reason[0]?driver_reason:esp_err_to_name(e)); xSemaphoreGive(state_lock);
    }
    xSemaphoreGive(op_done);
}
void sensor_task(void *arg) {
    irq_queue=xQueueCreate(LAB_IRQ_DEPTH,sizeof(int64_t));
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bc={.i2c_port=I2C_NUM_0,.sda_io_num=MPU_SDA,.scl_io_num=MPU_SCL,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    esp_err_t e=i2c_new_master_bus(&bc,&bus);
    if(e==ESP_OK) { i2c_device_config_t dc={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=MPU_ADDRESS,.scl_speed_hz=MPU_BUS_HZ}; e=i2c_master_bus_add_device(bus,&dc,&dev); }
    gpio_config_t gc={.pin_bit_mask=1ULL<<MPU_INT,.mode=GPIO_MODE_INPUT,.intr_type=GPIO_INTR_POSEDGE}; gpio_config(&gc);
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM); gpio_isr_handler_add(MPU_INT,data_ready,NULL);
    if(e==ESP_OK) e=configure(&state.config);
    xSemaphoreTake(state_lock,portMAX_DELAY); state.sensor_ok=e==ESP_OK; snprintf(state.sensor_error,sizeof(state.sensor_error),"%s",e!=ESP_OK && driver_reason[0]?driver_reason:esp_err_to_name(e)); app_event_locked(e==ESP_OK?"SENSOR_READY":"SENSOR_ERROR",state.sensor_error); xSemaphoreGive(state_lock);
    bool pending_gap=true; int64_t last_seen=esp_timer_get_time();
    for(;;) {
        lab_op op; if(xQueueReceive(op_queue,&op,0)==pdTRUE) { operation(&op); if(op.kind==OP_CONFIG) pending_gap=true; }
        if(!state.sensor_ok) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        int64_t first;
        if(xQueuePeek(irq_queue,&first,pdMS_TO_TICKS(20))!=pdTRUE) {
            /* Нет фиктивных нулей. Отсутствующий INT также является ошибкой. */
            if(esp_timer_get_time()-last_seen>500000) {
                fault("DATA_READY_TIMEOUT",false,false);
                xSemaphoreTake(state_lock,portMAX_DELAY); state.sensor_ok=false; strcpy(state.sensor_error,"DATA_READY_TIMEOUT; check INT, then set_config"); xSemaphoreGive(state_lock);
                last_seen=esp_timer_get_time(); pending_gap=true;
            }
            continue;
        }
        last_seen=esp_timer_get_time();
        uint8_t status,b[2];
        if(rd(0x3a,&status,1)!=ESP_OK || rd(0x72,b,2)!=ESP_OK) { fault("I2C_READ",false,true); pending_gap=true; continue; }
        int bytes=(b[0]<<8)|b[1];
        if((status&0x10) || bytes>=1008) { fault("FIFO_OVERFLOW",true,false); pending_gap=true; continue; }
        if(irq_lost) { fault("IRQ_QUEUE_OVERFLOW",false,false); pending_gap=true; continue; }
        if(bytes%14) { vTaskDelay(pdMS_TO_TICKS(1)); if(rd(0x72,b,2)!=ESP_OK || (((b[0]<<8)|b[1])%14)) { fault("PARTIAL_FIFO_PACKET",false,false); pending_gap=true; continue; } bytes=(b[0]<<8)|b[1]; }
        int n=bytes/14; unsigned stamps=uxQueueMessagesWaiting(irq_queue);
        /* FIFO can trail the DRDY edge. Do not call an empty FIFO a lost sample
         * after only 1ms; bounded wait, same untouched FIFO/stamp prefix. */
        if(!n && esp_timer_get_time()-first<50000) { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
        if(!n || stamps!=(unsigned)n) {
            vTaskDelay(pdMS_TO_TICKS(1)); /* IRQ/FIFO read can straddle an update; recheck once */
            if(rd(0x72,b,2)!=ESP_OK) { fault("FIFO_COUNT_READ",false,true); pending_gap=true; continue; }
            bytes=(b[0]<<8)|b[1]; n=bytes/14; stamps=uxQueueMessagesWaiting(irq_queue);
            if(!n || bytes%14 || stamps!=(unsigned)n) {
                uint8_t ctrl=0,en=0,stat=0; rd(0x6a,&ctrl,1); rd(0x23,&en,1); rd(0x3a,&stat,1);
                char reason[96]; snprintf(reason,sizeof(reason),"FIFO_TIMESTAMP_MISMATCH bytes=%d irq=%u ctrl=%02x en=%02x int=%02x",bytes,stamps,ctrl,en,stat);
                fault(reason,false,false); pending_gap=true; continue;
            }
        }
        /* Prefix pairing: further IRQ/FIFO writes may append but never replace prefix. */
        for(int j=0;j<n;j++) {
            uint8_t raw[14]; int64_t t;
            if(xQueueReceive(irq_queue,&t,0)!=pdTRUE || rd(0x74,raw,14)!=ESP_OK) { fault("FIFO_OR_TIMESTAMP_READ",false,true); pending_gap=true; break; }
            lab_sample s={.t_us=t};
            const int offsets[7]={0,2,4,8,10,12,6};
            for(int i=0;i<7;i++) { int k=offsets[i]; s.raw[i]=(int16_t)(((uint16_t)raw[k]<<8)|raw[k+1]); }
            xSemaphoreTake(state_lock,portMAX_DELAY);
            if(history.next==UINT32_MAX) { state.experiment=false; app_boundary_locked(true); app_event_locked("SEQ_LIMIT","New stream before uint32 wrap; experiment ended"); }
            float a[3],w[3],temp; lab_correct(&state.config,&state.cal,s.raw,a,w,&temp);
            if(pending_gap) { s.flags|=LAB_TIME_GAP; pending_gap=false; }
            s.flags|=lab_att_step(&state.att,&state.config,a,w,t);
            if(!state.cal.valid) s.flags|=LAB_UNCALIBRATED;
            for(int i=0;i<6;i++) if(s.raw[i]>=32760 || s.raw[i]<=-32760) s.flags|=LAB_SATURATED;
            if(state.wizard.state==1 || state.wizard.state==2 || state.wizard.state==3) s.flags|=LAB_CALIBRATION;
            memcpy(s.q,state.att.q,sizeof(s.q));
            int old_wizard=state.wizard.state; lab_wizard_feed(&state.wizard,&state.config,&s);
            if(old_wizard!=state.wizard.state) app_event_locked("CALIBRATION",state.wizard.state==4?"CANDIDATE":state.wizard.state==-1?state.wizard.reason:"WAIT_FACE");
            lab_ring_put(&history,&s); state.last=s;
            xSemaphoreGive(state_lock);
        }
        vTaskDelay(1);
    }
}
