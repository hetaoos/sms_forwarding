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

// 串口/控制引脚映射（按芯片自动选择：ESP32-C3 / ESP32-S3 / 经典 ESP32/WROOM-32 条件编译）
// arduino-esp32 3.x 在编译期自动定义 CONFIG_IDF_TARGET_*，据此切换引脚，无需手动改。
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  // ESP32-C3 系列（如 MakerGO C3 SuperMini、各类 C3-WROOM 开发板）
  #define TXD 3
  #define RXD 4
  #define MODEM_EN_PIN 5
  #define DEFAULT_LED_BUILTIN 8
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  // ESP32-S3 系列（Xtensa 双核）。需避开：Flash/PSRAM 脚(26-32/33)、strapping 脚
  // (GPIO0/3/45/46) 及仅输出脚(GPIO33-37 部分)、仅输入脚(部分高编号)。GPIO17/16/4 为通用安全脚。
  #define TXD 17
  #define RXD 16
  #define MODEM_EN_PIN 4
  #define DEFAULT_LED_BUILTIN 2
#elif defined(CONFIG_IDF_TARGET_ESP32)
  // 经典 ESP32 / ESP32-WROOM-32（Xtensa）。需避开：Flash 脚(6/7/8/9/10/11)、
  // 仅输入脚(34-39)、以及易造成启动冲突的 strapping 脚(GPIO0/2/5/12 谨慎)。
  #define TXD 17
  #define RXD 16
  #define MODEM_EN_PIN 4
  #define DEFAULT_LED_BUILTIN 2
#else
  // 未知目标回退到 C3 配置
  #define TXD 3
  #define RXD 4
  #define MODEM_EN_PIN 5
  #define DEFAULT_LED_BUILTIN 8
#endif

// LED引脚定义（用于通过CI验证，给个假的）
#ifndef LED_BUILTIN
#define LED_BUILTIN DEFAULT_LED_BUILTIN
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
extern String modemOwnNumber;     // 本机号码（SIM MSISDN）：模组初始化完成后查询一次并缓存，空串=未取到
extern unsigned long lastModemInitAttempt;
extern unsigned long lastPrintTime;
extern ConcatSms concatBuffer[MAX_CONCAT_MESSAGES];

#endif
