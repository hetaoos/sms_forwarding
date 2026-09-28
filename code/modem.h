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
bool sendSMS(const char* phoneNumber, const char* message);
// 模组串口是否被占用（发送短信/初始化中），HTTP 处理器据此快速失败
bool modemBusy();

#endif
