#ifndef CONFIG_H
#define CONFIG_H

#include "globals.h"

void saveConfig();
void loadConfig();
bool isPushChannelValid(const PushChannel& ch);
bool isConfigValid();
String getDeviceUrl();
// 当前设备 IP（AP 模式返回热点 IP，否则 STA IP）
String getDeviceIp();
// 开发板芯片类型（如 "ESP32-C3"）
String getChipModelName();
// 判断某类事件是否允许发邮件（mask 取 EMAIL_NOTIFY_*）
bool emailNotifyEnabled(uint32_t mask);

#endif
