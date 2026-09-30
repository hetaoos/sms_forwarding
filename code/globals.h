#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <pdulib.h>
#define ENABLE_SMTP
#define ENABLE_DEBUG
#include <ReadyMail.h>
#include "config_types.h"

// 串口映射
#define TXD 3
#define RXD 4
#define MODEM_EN_PIN 5

// LED引脚定义（用于通过CI验证，给个假的）
#ifndef LED_BUILTIN
#define LED_BUILTIN 8
#endif

#define SERIAL_BUFFER_SIZE 1024      // 串口单行缓冲（必须大于一条 +CMT URC 的长度）
// 模组串口接收环形缓冲。一条中文长短信分段的 +CMT URC 约 330~350 字节，
// 分段会连续下发，缓冲太小会在主循环被邮件/推送阻塞期间溢出丢字节（表现为「缺失分段」）。
// 注意：必须在 Serial1.begin() 之前调用 setRxBufferSize()，否则设置无效（回落到默认 256 字节）。
#define MODEM_RX_BUFFER_SIZE 16384
#define MAX_PDU_LENGTH 300

// 全局变量声明
extern Config config;
extern Preferences preferences;
extern PDU pdu;
extern WiFiClientSecure ssl_client;
extern SMTPClient smtp;
extern WebServer server;
extern bool configValid;
extern bool timeSynced;
extern bool modemReady;
extern bool apMode;        // true=当前运行在配置 AP 模式（WiFi 连接失败/未配置）
extern String modemManufacturer;  // 模组厂商（ATI 解析）
extern String modemModel;         // 模组型号（ATI 解析）
extern String modemVersion;       // 模组固件版本（ATI 解析）
extern unsigned long lastModemInitAttempt;
extern unsigned long lastPrintTime;
extern ConcatSms concatBuffer[MAX_CONCAT_MESSAGES];

#endif
