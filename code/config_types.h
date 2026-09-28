#ifndef CONFIG_TYPES_H
#define CONFIG_TYPES_H

#include <Arduino.h>

// 推送通道类型
enum PushType {
  PUSH_TYPE_NONE = 0,      // 未启用
  PUSH_TYPE_POST_JSON = 1, // POST JSON格式 {"sender":"xxx","message":"xxx","timestamp":"xxx"}
  PUSH_TYPE_BARK = 2,      // Bark格式 POST {"title":"xxx","body":"xxx"}
  PUSH_TYPE_GET = 3,       // GET请求，参数放URL中
  PUSH_TYPE_DINGTALK = 4,  // 钉钉机器人
  PUSH_TYPE_PUSHPLUS = 5,  // PushPlus
  PUSH_TYPE_SERVERCHAN = 6,// Server酱
  PUSH_TYPE_CUSTOM = 7,    // 自定义模板
  PUSH_TYPE_FEISHU = 8,    // 飞书机器人
  PUSH_TYPE_GOTIFY = 9,    // Gotify
  PUSH_TYPE_TELEGRAM = 10  // Telegram Bot
};

// 最大推送通道数
#define MAX_PUSH_CHANNELS 5

// 推送通道配置（通用设计，支持多种推送方式）
struct PushChannel {
  bool enabled;           // 是否启用
  PushType type;          // 推送类型
  String name;            // 通道名称（用于显示）
  String url;             // 推送URL（webhook地址）
  String key1;            // 额外参数1（如：钉钉secret、pushplus token等）
  String key2;            // 额外参数2（备用）
  String customBody;      // 自定义请求体模板（使用 {sender} {message} {timestamp} 占位符）
};

// 配置参数结构体
struct Config {
  String smtpServer;
  int smtpPort;
  String smtpUser;
  String smtpPass;
  String smtpSendTo;
  String adminPhone;
  PushChannel pushChannels[MAX_PUSH_CHANNELS];  // 多推送通道
  String webUser;      // Web管理账号
  String webPass;      // Web管理密码
  String numberBlackList;  // 号码黑名单（换行符分隔）
};

// 默认Web管理账号密码
#define DEFAULT_WEB_USER "admin"
#define DEFAULT_WEB_PASS "admin123"

// ---- 模组信号信息 ----
// 所有信号查询必须统一走 modem.cpp 的 getModemSignal() 得到本结构体，
// 避免不同入口各算各的导致页面上出现互相矛盾的数值。
// LTE 指标（RSRP/RSRQ）优先取自 AT+CESQ，取不到时回退到 AT+CSQ（只给 RSSI）。
struct SignalInfo {
  bool valid;       // 是否取到了有效信号值
  bool lte;         // true=数据源为 AT+CESQ（LTE 指标有效），false=仅有 CSQ 的 RSSI
  int rsrpDbm;      // LTE RSRP (dBm)，lte=false 时无意义（不要展示）
  float rsrqDb;     // LTE RSRQ (dB)，lte=false 时无意义（不要展示）
  int rssiDbm;      // CSQ 推算的接收电平 (dBm)
  int ber;          // 误码率 0-7，99 表示未知
  String source;    // 数据来源描述，如 "AT+CESQ" / "AT+CESQ + AT+CSQ"
  String raw;       // 模组返回的原始参数串
  String rsrpText;  // RSRP 展示文案（含单位与评级），未知时为 "未知"
  String rsrqText;  // RSRQ 展示文案（含单位），未知时为 "未知"
  String rssiText;  // RSSI 展示文案（含单位与评级）
  String quality;   // 统一评级：极好 / 良好 / 一般 / 较弱 / 很差，未知时为 "未知"

  SignalInfo()
    : valid(false), lte(false), rsrpDbm(0), rsrqDb(0.0f), rssiDbm(0), ber(99),
      quality("未知") {}
};

// 长短信合并相关定义
#define MAX_CONCAT_PARTS 10       // 最大支持的长短信分段数
#define CONCAT_TIMEOUT_MS 30000   // 长短信等待超时时间(毫秒)
#define MAX_CONCAT_MESSAGES 5     // 最多同时缓存的长短信组数

// 长短信分段结构
struct SmsPart {
  bool valid;           // 该分段是否有效
  String text;          // 分段内容
};

// 长短信缓存结构
struct ConcatSms {
  bool inUse;                           // 是否正在使用
  int refNumber;                        // 参考号
  String sender;                        // 发送者
  String timestamp;                     // 时间戳（使用第一个收到的分段的时间戳）
  int totalParts;                       // 总分段数
  int receivedParts;                    // 已收到的分段数
  unsigned long firstPartTime;          // 收到第一个分段的时间
  SmsPart parts[MAX_CONCAT_PARTS];      // 各分段内容
};

#endif
