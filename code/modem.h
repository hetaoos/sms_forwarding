#ifndef MODEM_H
#define MODEM_H

#include "globals.h"

String sendATCommand(const char* cmd, unsigned long timeout);
void modemPowerCycle();
void resetModule();
bool modemInit(bool background = false);
void modemAutoRecover();
bool sendATandWaitOK(const char* cmd, unsigned long timeout);
bool waitCEREG();
void blink_short(unsigned long gap_time = 500);
// 板载蓝色 LED（低电平点亮）：初始化完成后熄灭，收到短信时闪烁 SMS_LED_BLINK_TIMES 次
void ledOff();
void ledBlink(unsigned int times = SMS_LED_BLINK_TIMES, unsigned long duration = SMS_LED_BLINK_MS);
void ledTick();   // 主循环里调用，推进闪烁状态机并在结束后熄灭
bool sendSMS(const char* phoneNumber, const char* message);
// 模组串口是否被占用（发送短信/初始化中），HTTP 处理器据此快速失败
bool modemBusy();
// 信号强度统一查询入口（RSRP/RSRQ/RSSI），所有展示信号的地方都调用它
bool getModemSignal(SignalInfo& info);
// 获取本机号码（SIM 卡 MSISDN，AT+CNUM），取不到时返回空串
String getModemOwnNumber();
// ---- 模组时钟 / 系统时间 ----
// 读取模组的网络时间（AT+CCLK?），解析为 UTC 时间戳 + 本地时间文案
bool getModemTime(ModemTimeInfo& info);
// 读取模组时间并写入系统时间（settimeofday），成功时置 timeSynced=true
bool syncTimeFromModem(ModemTimeInfo& info);
// 把 UTC 时间戳格式化为展示用本地时间 "YYYY-MM-DD HH:MM:SS (UTC+8)"；未同步时返回 "未同步"
String formatSystemTime(time_t epochUtc);
// 主循环调用：模组已就绪但时间还没同步成功时，定期重试（基站时间下发有延迟）
void modemTimeSyncTick();

#endif
