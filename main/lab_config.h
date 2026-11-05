#pragma once
#ifndef LAB_DEVICE_ID
#define LAB_DEVICE_ID 1
#endif
#if LAB_DEVICE_ID < 1 || LAB_DEVICE_ID > 4
#error LAB_DEVICE_ID must be 1..4
#endif
/* Изменяемая аппаратная конфигурация. GPIO ESP32: только 3.3 V! */
#define MPU_SDA 21
#define MPU_SCL 22
#define MPU_INT 27
#define MPU_ADDRESS 0x68 /* AD0=GND: 0x68; AD0=3V3: 0x69 */
#define MPU_BUS_HZ 400000
#define LCD_SDA 25
#define LCD_SCL 26
#define LCD_BUS_HZ 100000
#define LCD_ADDRESS 0 /* 0: только единственный ACK в разрешённых диапазонах */
#define LCD_RS 0
#define LCD_RW 1
#define LCD_E 2
#define LCD_BL 3
#define LCD_D4 4
#define LCD_D5 5
#define LCD_D6 6
#define LCD_D7 7
#define LCD_BL_ACTIVE_HIGH 1
#define LAB_UART_BAUD 460800
#define LAB_UART_TX 1
#define LAB_UART_RX 3
#define LAB_WIFI_CHANNEL ((LAB_DEVICE_ID==2)?6:(LAB_DEVICE_ID==3)?11:1)
#define LAB_AP_PASSWORD "12345678"
#define LAB_WS_CLIENTS 5 /* четыре полных потока + веб-управление */
#define LAB_ENDPOINTS (LAB_WS_CLIENTS + 1)
#define LAB_COMMAND_MAX 4096
#define LAB_IRQ_DEPTH 96
#define LAB_EVENTS 64
#define LAB_GYRO_SECONDS 30
#define LAB_FACE_SECONDS 5
