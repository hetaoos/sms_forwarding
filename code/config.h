#ifndef CONFIG_H
#define CONFIG_H

#include "globals.h"

void saveConfig();
void loadConfig();
bool isPushChannelValid(const PushChannel& ch);
bool isConfigValid();
String getDeviceUrl();
// 判断某类事件是否允许发邮件（mask 取 EMAIL_NOTIFY_*）
bool emailNotifyEnabled(uint32_t mask);

#endif
