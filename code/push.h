#ifndef PUSH_H
#define PUSH_H

#include "globals.h"

// 邮件正文类型：纯文本或 HTML 富文本
enum MailBodyType {
  MAIL_BODY_TEXT = 0,
  MAIL_BODY_HTML = 1
};

// 发送邮件通知（带重试，同步阻塞）。body 为正文内容，bodyType 指明它是纯文本还是 HTML。
// 仅用于「必须立刻发出」的场景（如重启前的通知）；短信转发等场景请走下面的异步队列。
void sendEmailNotification(const char* subject, const char* body, MailBodyType bodyType = MAIL_BODY_TEXT);

// 发送"设备已启动"通知邮件（含模组初始化状态/模组信息/信号/号码等，HTML 富文本）
void sendStartupEmail();

// 从短信正文解析发送者名称（【】/[[ ]]/[ ] 包裹）与验证码（4~6 位纯数字串）
void parseSmsMeta(const String& message, String& senderName, String& verifyCode);

// ---- 异步通知队列 ----
// 推送与发邮件都是网络慢操作（单条最坏可达数十秒），若直接在短信 URC 的回调里同步执行，
// 会把主循环整个冻住：Web 无响应、URC 停止读取、看门狗可能复位。
// 因此这里只把通知排入队列，由 loop() 调用 processNotifyQueue() 分片推进：
// 每次调用最多发起一次网络请求，重试间隔靠时间戳退避，绝不 delay 等待。

// 单次网络尝试的结果（异步队列据此决定出队还是退避重试）
enum NotifyStep { STEP_OK, STEP_FAILED, STEP_RETRY };

// 入队一条"短信转发"通知：先推送所有启用通道，再发邮件（邮件标题/正文在发送时才构建）
bool notifyQueueSms(const char* sender, const char* message, const char* timestamp);

// 入队一封邮件（正文已构建好，最长 NOTIFY_BODY_SIZE-1 字节，超出部分截断）
bool notifyQueueEmail(const char* subject, const char* body, MailBodyType bodyType = MAIL_BODY_TEXT);

// 入队"管理员 SMS 命令"：先经模组把 content 发给 targetPhone，再把执行结果发邮件通知。
// cmdText 为原始命令（仅用于回显到邮件正文）。
bool notifyQueueAdminSms(const char* targetPhone, const char* content, const char* cmdText);

// 入队"管理员 RESET 命令"：先发出重启通知邮件，再重启模组与 ESP32
bool notifyQueueReboot();

// 主循环调用：最多发起一次网络请求后立刻返回，其余时间交还主循环
void processNotifyQueue();

// 当前待发通知条数（供状态展示/日志用）
int notifyQueueCount();

// maxAttempts=1 时只发一次请求且不做退避 delay，供异步队列分片调用
NotifyStep sendToChannel(const PushChannel& channel, const char* sender, const char* message, const char* timestamp,
                         const char* senderName = "", const char* verifyCode = "", int maxAttempts = 3);
String urlEncode(const String& str);
String jsonEscape(const String& str);
String htmlEscape(const String& str);
String dingtalkSign(const String& secret, int64_t timestamp);
int64_t getUtcMillis();

#endif
