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
// 解析 ATI 响应，提取制造商/型号/固件版本。
// ML307 系列 ATI 行数不固定（可能仅一行型号，也可能带 "Revision:" 前缀的版本行），
// 不依赖固定行序，按关键字识别，确保 modemModel 取到真实型号。
void parseATI(const String& resp, String& manufacturer, String& model, String& version);
// 信号强度统一查询入口（RSRP/RSRQ/RSSI），所有展示信号的地方都调用它
bool getModemSignal(SignalInfo& info);
// 获取本机号码（SIM 卡 MSISDN，AT+CNUM），取不到时返回空串
String getModemOwnNumber();
// ---- SIM 卡热插拔检测 ----
// 运行中换卡不必重启设备：主循环每 20 秒轮询 AT+CPIN?，并用模组主动上报的 +CPIN URC 做快速通道。
// 插卡 → 自动重跑模组初始化（AT 握手 → CNMI → PDU → 等网络注册），失败再兜底断电重启一次；
// 拔卡 → 立刻置为未就绪，避免继续发短信、也免掉 modemAutoRecover() 无意义的反复断电重启。
SimStatus getSimStatus();    // 实时查询（占用一次串口收发，模组忙时不要调用）
bool isSimInserted();        // 最近一次检测结果：卡是否插着（不发 AT，供页面展示）
String simStatusText();      // 最近一次状态的展示文案：已插入/未插入/需解锁/未知
void simHotplugTick();       // 主循环调用：轮询状态 + 执行插卡后的自动初始化
void handleSimUrc(const String& params);  // URC 回调：处理 "+CPIN:" 上报（只置标记，不做初始化）
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
