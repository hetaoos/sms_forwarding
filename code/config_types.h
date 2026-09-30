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
  PUSH_TYPE_TELEGRAM = 10, // Telegram Bot
  PUSH_TYPE_MAILGUN = 11   // Mailgun 邮件 API（url=基址, key1=API Key, key2=域名, key3=收件人,
                           // key4=发件人[可选], key5=标题模板[可选], customBody=正文 HTML 模板[可选]）
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
  String key3;            // 额外参数3（Mailgun：收件人，支持逗号/分号分隔多个）
  String key4;            // 额外参数4（Mailgun：发件人，留空自动派生为 SMS Notification <sms@域名>）
  String key5;            // 额外参数5（Mailgun：邮件标题模板，留空用默认标题）
  String customBody;      // 自定义请求体模板（自定义类型使用 {sender} {message} {timestamp} {sender_name} {verify_code} {sender_display} 占位符）
                          // Mailgun 类型下复用为「正文 HTML 模板」，留空用默认 HTML 邮件正文
};

// ---- 邮件通知信息类型（位掩码，存 Config::emailNotifyTypes）----
// 每个位代表一类"会发邮件"的事件；关闭后该事件的邮件不再发送（动作本身照常执行）。
// 新增类型时：在这里加一个位 → 入队处带上该位 → web_html.cpp 加一个复选框 → handleSave 解析。
#define EMAIL_NOTIFY_SMS      (1u << 0)  // 短信转发
#define EMAIL_NOTIFY_STARTUP  (1u << 1)  // 设备启动通知
#define EMAIL_NOTIFY_CONFIG   (1u << 2)  // 配置变更通知
#define EMAIL_NOTIFY_COMMAND  (1u << 3)  // 管理员命令（SMS 命令执行结果 / 命令格式错误）
#define EMAIL_NOTIFY_REBOOT   (1u << 4)  // RESET 重启通知

#define EMAIL_NOTIFY_ALL      0xFFFFFFFFu  // 有效位全集（用于归一化，扩展位数后需同步）
#define EMAIL_NOTIFY_DEFAULT  EMAIL_NOTIFY_ALL  // 老配置（NVS 无此键）升级后保持原有行为：全开

// 配置参数结构体
struct Config {
  String smtpServer;
  int smtpPort;
  String smtpUser;
  String smtpPass;
  String smtpSendTo;
  uint32_t emailNotifyTypes;  // 允许发送邮件的事件类型位掩码（见 EMAIL_NOTIFY_*）
  String adminPhone;
  PushChannel pushChannels[MAX_PUSH_CHANNELS];  // 多推送通道
  String webUser;      // Web管理账号
  String webPass;      // Web管理密码
  String numberBlackList;  // 号码黑名单（换行符分隔）
  String wifiSsid;     // 要连接的 WiFi 名称（持久化到 NVS，启动失败则进 AP 模式）
  String wifiPass;     // 要连接的 WiFi 密码
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

// ---- 板载蓝色 LED 指示 ----
// LED 为低电平点亮。启动/初始化完成后保持熄灭，只在收到短信时闪烁。
#define SMS_LED_BLINK_TIMES 2  // 收到短信时闪烁的次数
#define SMS_LED_BLINK_MS 200   // 单次亮/灭的时长（毫秒），闪烁由 ledTick() 推进

// 长短信合并相关定义
#define MAX_CONCAT_PARTS 10       // 最大支持的长短信分段数
#define CONCAT_TIMEOUT_MS 30000   // 长短信空闲超时: 距上一个分段的间隔超过该值就强制转发(毫秒)
#define CONCAT_MAX_WAIT_MS 180000 // 长短信总超时: 距第一个分段的上限，防止分段迟迟不来时一直占用槽位
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
  unsigned long lastPartTime;           // 最近一次收到分段的时间（用于空闲超时判定）
  SmsPart parts[MAX_CONCAT_PARTS];      // 各分段内容
};

#endif
