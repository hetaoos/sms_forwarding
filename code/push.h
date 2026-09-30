#ifndef PUSH_H
#define PUSH_H

#include "globals.h"

void sendEmailNotification(const char* subject, const char* body, const char* html = nullptr);

// 从短信正文解析发送者名称（【】/[[ ]]/[ ] 包裹）与验证码（4~6 位纯数字串）
void parseSmsMeta(const String& message, String& senderName, String& verifyCode);

void sendSMSToServer(const char* sender, const char* message, const char* timestamp,
                     const char* senderName = "", const char* verifyCode = "");
void sendToChannel(const PushChannel& channel, const char* sender, const char* message, const char* timestamp,
                   const char* senderName = "", const char* verifyCode = "");
String urlEncode(const String& str);
String jsonEscape(const String& str);
String htmlEscape(const String& str);
String dingtalkSign(const String& secret, int64_t timestamp);
int64_t getUtcMillis();

#endif
