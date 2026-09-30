#ifndef TASK_TYPES_H
#define TASK_TYPES_H

#include <Arduino.h>

#define MODEM_JOB_ARG1_SIZE 96
#define MODEM_JOB_ARG2_SIZE 1024
#define MODEM_RESULT_MESSAGE_SIZE 1536
#define NOTIFY_SENDER_SIZE 32
#define NOTIFY_TIMESTAMP_SIZE 32
#define NOTIFY_SUBJECT_SIZE 192
#define NOTIFY_BODY_SIZE 2048
#define NOTIFY_MESSAGE_SIZE 2048   // 短信正文（长短信合并后的整条内容）

// 通知队列深度：推送/邮件都是慢操作，积压过多说明网络异常，超出的直接丢弃
#define NOTIFY_QUEUE_SIZE 3

enum ModemJobType {
  MODEM_JOB_INIT = 0,
  MODEM_JOB_AT,
  MODEM_JOB_SEND_SMS,
  MODEM_JOB_PING,
  MODEM_JOB_SOFT_RESET,
  MODEM_JOB_HARD_RESET
};

struct ModemJob {
  uint32_t id;
  ModemJobType type;
  unsigned long timeoutMs;
  bool wantsReply;
  char arg1[MODEM_JOB_ARG1_SIZE];
  char arg2[MODEM_JOB_ARG2_SIZE];
};

struct ModemResult {
  uint32_t id;
  bool success;
  char message[MODEM_RESULT_MESSAGE_SIZE];
};

enum NotifyJobType {
  NOTIFY_JOB_SMS_PUSH = 0,   // 转发一条短信：先推送所有通道，再发邮件
  NOTIFY_JOB_EMAIL,          // 只发一封邮件（subject/body 已构建好）
  NOTIFY_JOB_SMS_COMMAND,    // 管理员 SMS 命令：先发一条短信，再把执行结果发邮件
  NOTIFY_JOB_REBOOT          // 管理员 RESET 命令：先发通知邮件，再重启模组与 ESP32
};

// 通知任务的执行阶段（用于在主循环里分片推进）
enum NotifyStage {
  NOTIFY_STAGE_PUSH = 0,     // 正在逐通道推送
  NOTIFY_STAGE_EMAIL,        // 正在发邮件
  NOTIFY_STAGE_SMS,          // 正在通过模组发短信（NOTIFY_JOB_SMS_COMMAND 的首个阶段）
  NOTIFY_STAGE_REBOOT        // 邮件已处理完，正在执行重启（NOTIFY_JOB_REBOOT 的末个阶段）
};

struct NotifyJob {
  NotifyJobType type;
  NotifyStage stage;         // 当前阶段
  uint8_t channelIdx;        // 当前处理到第几个推送通道
  uint8_t attempt;           // 当前步骤已尝试次数
  uint8_t bodyType;          // 邮件正文类型（见 push.h 的 MailBodyType），避免本文件反向依赖 push.h
  uint8_t smsOk;             // 管理员 SMS 命令的发送结果（1=成功，0=失败）
  uint32_t mailBit;          // 该任务对应邮件所属的事件类型位（EMAIL_NOTIFY_*），0=不发邮件
  uint32_t enqueuedAt;       // 入队时刻（WiFi 断开时的等待时限基准）
  uint32_t nextAttemptAt;    // 退避到该时刻后才重试（0=立即）
  char sender[NOTIFY_SENDER_SIZE];
  char timestamp[NOTIFY_TIMESTAMP_SIZE];
  char subject[NOTIFY_SUBJECT_SIZE];
  char body[NOTIFY_BODY_SIZE];        // 邮件正文（NOTIFY_JOB_EMAIL 使用）
  char message[NOTIFY_MESSAGE_SIZE];  // 短信正文（NOTIFY_JOB_SMS_PUSH 使用，邮件正文在发送前构建）
};

#endif
