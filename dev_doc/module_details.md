# 模块详解 — 逐文件源码指南

---

## code.ino — 主入口

**文件**: `code/code.ino` (约 105 行)  
**角色**: Arduino 工程入口点，仅包含 `setup()` 和 `loop()`。

### setup() 初始化顺序

```
1. GPIO 初始化
   ├── pinMode(LED_BUILTIN, OUTPUT); digitalWrite(HIGH)   // LED 灭
   └── (MODEM_EN_PIN 在 modemPowerCycle 内设置)

2. 串口初始化
   ├── Serial.begin(115200); delay(1500)                   // USB CDC 稳定
   └── Serial1.begin(115200, SERIAL_8N1, RXD, TXD)        // 模组 UART

3. 模组上电
   ├── Serial1.read() 清噪声
   ├── modemPowerCycle()                                     // EN 引脚时序
   └── Serial1.read() 清启动噪声

4. 数据加载
   ├── initConcatBuffer()                                    // 长短信缓存清零
   ├── loadConfig()                                          // NVS → config
   └── configValid = isConfigValid()                        // 校验

5. 模组初始化 modemInit() (每步失败有限次重试+LED闪烁，绝不无限等待)
   ├── modemWaitATReady()   "AT" ×8                          // 握手，中途失败会断电重启一次
   ├── sendATWithRetry("AT+CGACT=0,1", 5000) ×3             // 禁数据(省流量)，失败仅告警
   ├── sendATWithRetry("AT+CNMI=2,2,0,0,0", 1000) ×3        // 短信URC上报
   ├── sendATWithRetry("AT+CMGF=0", 1000) ×3                // PDU模式
   └── waitCEREG() ×20                                       // 等网络注册
   → 判定 modemReady 后 syncTimeFromModem() (AT+CCLK?) 把基站网络时间写入系统时间；
     失败（NITZ 下发有延迟）时由 loop() 的 modemTimeSyncTick() 每 60 秒重试，最多 10 次；
     首次成功后 modemTimeSyncTick() 每 24 小时再校准一次（抵消 RTC 漂移）
   → 任一步失败则返回 false，modemReady=false，由 loop() 中的
     modemAutoRecover() 每 90 秒用更少的重试次数(background=true)自动重试

6. WiFi 连接
   ├── WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN)             // 扫描全部信道
   └── WiFi.begin(SSID, PASS, 0, nullptr, true)              // 支持隐藏SSID

7. NTP 时间同步
   ├── configTime(0, 0, "ntp.ntsc.ac.cn", ...)              // UTC时区
   └── 等待 time() > 100000 (最多10秒)

8. HTTP 服务器
   ├── server.on("/", handleRoot)
   ├── server.on("/save", HTTP_POST, handleSave)
   ├── server.on("/tools", handleRoot)                       # 兼容旧链接
   ├── server.on("/sms", handleRoot)                         # 兼容旧链接
   ├── server.on("/sendsms", HTTP_POST, handleSendSms)
   ├── server.on("/ping", HTTP_POST, handlePing)
   ├── server.on("/query", handleQuery)
   ├── server.on("/flight", handleFlightMode)
   ├── server.on("/at", handleATCommand)
   ├── server.on("/log", handleLog)                          # 系统日志 JSON
   ├── server.on("/modem", handleModem)
   ├── server.on("/wifi", handleWifi)
   ├── server.on("/system", handleSystem)                    # 整机重启
   └── server.begin()

9. 启动通知
   └── if(configValid) sendEmailNotification("短信转发器已启动", ...)
```

### loop() 执行顺序（每帧执行）

```cpp
server.handleClient();        // 1. 处理HTTP请求
if(!configValid) { ... }      // 2. 配置无效时每秒打印IP
checkConcatTimeout();          // 3. 长短信超时合并转发
simHotplugTick();              // 4. SIM 热插拔检测（插卡自动初始化/拔卡置未就绪）
modemAutoRecover();            // 5. 模组未就绪时按退避间隔重试初始化
modemTimeSyncTick();           // 6. 用 4G 网络时间校准系统时间
if(Serial.available())         // 7. USB->模组透传(单字节)
    Serial1.write(Serial.read());
checkSerial1URC();             // 8. 模组URC解析(短信/SIM 上报)
processNotifyQueue();          // 9. 分片发送推送与邮件（每轮最多一次网络请求）
```

### 修改指南

- **新增 HTTP 路由**: 在 `web_handlers.h/.cpp` 添加处理函数，然后在 `setup()` 中 `server.on("/path", handler)`
- **调整初始化顺序**: 直接修改 `setup()` 中的代码顺序即可，注意模组初始化必须在 WiFi 之前
- **禁用某功能**: 注释掉对应的 `server.on()` 行即可

---

## config_types.h — 类型定义

**依赖**: 仅 `<Arduino.h>`  
**被依赖**: 所有其他模块

### PushType 枚举 (0-10)

表示推送目标平台。值 0 为"未启用"，1-10 对应 10 种推送方式。

**添加新推送类型**:
1. 在 `PushType` 枚举末尾添加新值（如 `PUSH_TYPE_NEW = 11`）
2. 在 `isPushChannelValid()` 添加对应的必填字段校验
3. 在 `sendToChannel()` 添加 `case PUSH_TYPE_NEW:` 实现
4. 在 `handleRoot()` 的 HTML 生成中添加 option
5. 在 HTML JavaScript `updateTypeHint()` 中添加类型提示

### PushChannel 结构体

通用推送通道配置。字段含义:
- `enabled` — 开关
- `type` — PushType 枚举值
- `name` — WebUI 中显示的名称
- `url` — Webhook URL（GET 类型则为目标 URL）
- `key1` — 通用参数 1（token/secret/chat_id 等）
- `key2` — 通用参数 2（channel/bot_token/域名 等）
- `key3` — 通用参数 3（Mailgun 收件人等）
- `key4` — 通用参数 4（Mailgun 发件人等）
- `key5` — 通用参数 5（Mailgun 标题模板等）
- `customBody` — 自定义模板的 HTTP body；Mailgun 下复用为正文 HTML 模板

### Config 结构体

所有可持久化配置的容器，通过 `saveConfig()/loadConfig()` 与 NVS 同步。

### 常量

| 常量 | 值 | 说明 |
|---|---|---|
| `MAX_PUSH_CHANNELS` | 5 | 推送通道数量上限 |
| `MAX_CONCAT_PARTS` | 10 | 长短信最大分段数 |
| `CONCAT_TIMEOUT_MS` | 30000 | 长短信空闲超时：距上一个分段超过该值即强制转发(ms) |
| `CONCAT_MAX_WAIT_MS` | 180000 | 长短信总超时：距第一个分段的上限(ms) |
| `MAX_CONCAT_MESSAGES` | 5 | 同时缓存的长短信组数 |
| `MODEM_RX_BUFFER_SIZE` | 16384 | 模组串口接收环形缓冲（必须在 `Serial1.begin()` 之前设置） |
| `SERIAL_BUFFER_SIZE` | 1024 | 串口单行缓冲（需大于一条 +CMT URC 的长度） |
| `DEFAULT_WEB_USER` | `"admin"` | 默认 Web 账号 |
| `DEFAULT_WEB_PASS` | `"admin123"` | 默认 Web 密码 |

---

## globals.h / globals.cpp — 全局变量

### 全局变量清单

| 变量 | 类型 | 初始化 | 用途 |
|---|---|---|---|
| `config` | `Config` | 默认构造 | 系统全部配置（运行时+持久化） |
| `preferences` | `Preferences` | 默认构造 | NVS 存储接口 |
| `pdu` | `PDU` | `PDU(4096)` | PDU 编解码器（4KB 缓冲区） |
| `ssl_client` | `WiFiClientSecure` | 默认构造 | TLS 客户端（SMTP/HTTPS） |
| `smtp` | `SMTPClient` | `SMTPClient(ssl_client)` | SMTP 邮件客户端 |
| `server` | `WebServer` | `WebServer(80)` | HTTP 服务器（端口 80） |
| `configValid` | `bool` | `false` | 配置是否有效标志 |
| `timeSynced` | `bool` | `false` | NTP 是否已同步 |
| `lastPrintTime` | `unsigned long` | `0` | 上次打印 IP 的 millis |
| `concatBuffer` | `ConcatSms[5]` | 默认构造 | 长短信合并缓存 |

### 功能说明

- **添加新的全局变量**: 在 `globals.h` 添加 `extern` 声明，在 `globals.cpp` 添加定义
- **添加新的库依赖**: 在 `globals.h` 添加 `#include`，所有模块自动获得该依赖

---

## config.h / config.cpp — 配置管理

### NVS 存储结构

```
Namespace: "sms_config"
├── smtpServer  (String)
├── smtpPort    (Int, 默认 465)
├── smtpUser    (String)
├── smtpPass    (String)
├── smtpSendTo  (String)
├── adminPhone  (String)
├── webUser     (String, 默认 "admin")
├── webPass     (String, 默认 "admin123")
├── numBlkList  (String, 换行分隔)
├── push0en     (Bool)
├── push0type   (UChar)
├── push0url    (String)
├── push0name   (String)
├── push0k1     (String)
├── push0k2     (String)
├── push0body   (String)
├── push1en ... (同上 × 4)
└── ...
```

### 兼容迁移机制

`loadConfig()` 末尾检查旧 key `httpUrl` 和 `barkMode`，若存在则自动迁移到 `pushChannels[0]`。这是旧版单通道配置 → 新版多通道配置的迁移路径。

### 修改指南

- **添加新配置项**: 在 `Config` 结构体加字段 → `loadConfig()` 加读取（设默认值） → `saveConfig()` 加写入 → `handleSave()` 加表单解析
- **修改校验逻辑**: 编辑 `isPushChannelValid()` 和 `isConfigValid()`

---

## modem.h / modem.cpp — 模组控制

### 串口通信模型

ESP32-C3（默认）：

```
ESP32-C3                   4G 模组
  Serial1 (UART) ──────────── AT 端口
  GPIO 5     ──────────── EN 引脚
```

ESP32-WROOM-32 / ESP32-S3（经典 ESP32 与 S3，板型选 `esp32:esp32:esp32` / `esp32:esp32:esp32s3`）：

```
ESP32-WROOM-32 / S3        4G 模组
  GPIO 17 (TX1) ───────────► RX (AT端口)
  GPIO 16 (RX1) ◄─────────── TX (AT端口)
  GPIO 4  (EN)  ───────────► EN 引脚
```

引脚由 `globals.h` 中 `CONFIG_IDF_TARGET_*` 条件编译自动选择，切换板型无需改代码。

### AT 指令函数对比

| 函数 | 返回类型 | 延时处理 | 适用场景 |
|---|---|---|---|
| `sendATCommand()` | String (完整响应) | 收到 OK/ERROR 后 `delay(50)` | 需要解析响应内容 |
| `sendATandWaitOK()` | bool | 无额外 delay | 快速幂等检查 |

### 模组电源控制

```
modemPowerCycle():
  EN=HIGH (默认)  模组运行
      ↓
  EN=LOW (1200ms) 模组断电
      ↓
  EN=HIGH (6000ms) 模组上电启动
```

6000ms 是关键时间，太短模组初始化未完成会导致 AT 无响应。

### sendSMS() 时序图

```
ESP32                         模组
  │                            │
  ├─ AT+CMGS=<len>\r\n ──────►│
  │                            │
  │◄───────────────────── >  │  (提示符)
  │                            │
  ├─ PDU hex data + 0x1A ────►│
  │                            │
  │◄──────────────── +CMGS:   │  (成功响应)
  │◄──────────────── OK       │
```

### SIM 卡热插拔检测

```
检测入口（两条）:
  loop() → simHotplugTick()            每 20 秒 AT+CPIN? 轮询
  checkSerial1URC() → handleSimUrc()   模组主动上报的 +CPIN: READY / NOT INSERTED

状态机（modem.cpp 内部静态变量）:
  SIM_STATUS_UNKNOWN ──检测到卡──► READY ──2 秒后──► modemInit(true)
        ▲                            │
        │                            └──初始化失败──► 15 秒后断电重启一次（仅一次）
        └────── 拔出（ABSENT）────────┘
```

- 插卡：`READY` 且 `!modemReady` → 等 2 秒 → `modemInit(true)`；仍失败则断电重启兜底一次，再不行交给 `modemAutoRecover()`
- 拔卡：立刻 `modemReady=false` 并点亮 LED（LED 亮 = 模组不可用），同时让 `modemAutoRecover()` 暂停重试（没卡时重试只会白白断电重启）
- 需要 PIN/PUK 的卡判为 `LOCKED`，只告警不初始化
- 所有耗时动作都在 `simHotplugTick()` 里执行，URC 回调只置标记；`modemBusy()` 为真时跳过轮询

### 修改指南

- **更换模组型号**: 修改 `modemPowerCycle()` 的时序参数，调整 AT 指令序列
- **添加新 AT 功能**: 实现新函数，使用 `sendATCommand()` 或直接操作 `Serial1`
- **调波特率**: 修改 `Serial1.begin()` 参数 + 模组 AT 配置
- **调 SIM 检测节奏**: 改 `SIM_POLL_INTERVAL_MS` / `SIM_INSERT_SETTLE_MS` / `SIM_REBOOT_DELAY_MS`

---

## push.h / push.cpp — 推送服务

### 推送通道执行流程

```
notifyQueueSms(sender, message, timestamp)     ← 短信 URC 回调里只做这一步（入队）
  │
  ▼ 队列内由 processNotifyQueue() 分片推进（每轮主循环最多一次网络请求）
NOTIFY_STAGE_PUSH:
  └─ for each valid channel（每轮推进一个通道）:
       └─ sendToChannel(channel, sender, message, timestamp, senderName, verifyCode, maxAttempts=1)
            │  ← senderName/verifyCode 由 push.cpp 内部调用 parseSmsMeta() 解析而来
            │
            ├─ jsonEscape() 转义 sender/message/timestamp/senderName/verifyCode
            ├─ 构建统一富文本 notifyText 与统一标题 titleText（验证码优先入标题）
            │
            └─ switch(channel.type):
                 ├─ POST_JSON  → POST {sender, message, timestamp, sender_name, verify_code}
                 ├─ BARK       → POST {title, body(含发件人/验证码)}
                 ├─ GET        → GET ?sender=&message=&timestamp=&sender_name=&verify_code=
                 ├─ DINGTALK   → POST {msgtype:"text", text:{content: notifyText}}
                 │               ├─ 有secret → dingtalkSign() 追加URL参数
                 ├─ PUSHPLUS   → POST {token, title: titleText, content(含发件人/验证码), channel}
                 │               └─ 默认URL: http://www.pushplus.plus/send
                 ├─ SERVERCHAN → POST title=titleText&desp=(含发件人/验证码) (form-urlencoded)
                 │               └─ 默认URL: https://sctapi.ftqq.com/{key1}.send
                 ├─ CUSTOM     → POST (customBody 模板替换，支持 5 个占位符)
                 ├─ FEISHU     → POST {timestamp?, sign?, msg_type, content: notifyText}
                 │               └─ 有secret → HMAC-SHA256签名
                 ├─ GOTIFY     → POST {title: titleText, message(含发件人/验证码), priority}
                 │               └─ URL: {url}/message?token={key1}
                 └─ TELEGRAM   → POST {chat_id, text: notifyText}
                     └─ 默认URL: https://api.telegram.org/bot{key2}/sendMessage
                 ├─ MAILGUN    → POST form: from/to/subject/html/text（Basic 认证 api:{key1}）
                 │   └─ URL: {url 或 https://api.mailgun.net}/v3/{key2}/messages
                 │   └─ 收件人 {key3}（逗号/分号分隔 → 多个 to 参数）；发件人 {key4} 留空为 SMS Notification <sms@{key2}>
                 │   └─ 标题模板 {key5} / 正文 HTML 模板 customBody，留空回退 buildSmsMail()
```

### 短信元信息解析（parseSmsMeta）

发送阶段（队列处理到推送/邮件步骤时）会调用 `parseSmsMeta(const String& message, String& senderName, String& verifyCode)` 自动提取两项元信息：

- **发送者名称 `senderName`**：扫描正文中全部 `【...】` / `[[...]]` / `[...]` 片段，**优先选取最靠近开头或结尾（即离任一边缘最近）的括号内容**；结果为括号之间的纯文本，不包含任何括号字符（`【】`、`[]`、`[[]]` 均被丢弃）；无则空串。
  - 当开头与结尾各有括号时，按"到最近边缘的距离"比较，距离相同则取更靠近开头的。
  - 注意 `【`/`】` 为 3 字节 UTF-8，匹配按字节索引跳过整个字符，不会残留半个括号字节。
- **验证码 `verifyCode`**：取正文中首个长度 4~6 的连续数字串（等价于"前后不为其他数字"）；无则空串。

两者解析结果会随 `sendToChannel` 一起传入所有通道，并用于增强通知标题与正文（参考 `dev_doc/send_notification.sh` 的标题逻辑：验证码优先进入标题）。

### 通知占位符

除内置通道已自动使用外，自定义模板（`PUSH_TYPE_CUSTOM` 的 `customBody`）支持以下占位符替换：

| 占位符 | 含义 | 来源 |
|---|---|---|
| `{sender}` | 短信发送者号码 | `sender` |
| `{message}` | 短信原始内容 | `message` |
| `{timestamp}` | 接收时间 | `timestamp` |
| `{sender_name}` | 解析出的发送者名称（如【阿里云】） | `parseSmsMeta()` |
| `{verify_code}` | 解析出的短信验证码（4~6 位数字） | `parseSmsMeta()` |
| `{sender_display}` | 优化后的发送者名称：有 `sender_name` 时取其值，否则回退到 `sender` | `parseSmsMeta()` + 回退 |

> 替换发生在 JSON 转义之后，模板中写入的即为已转义内容；无对应值时替换为空串。

### HMAC 签名实现

钉钉和飞书都使用 `mbedtls_md` 库（ESP32 内置）:
```cpp
mbedtls_md_context_t ctx;
mbedtls_md_init(&ctx);
mbedtls_md_setup(&ctx, MBEDTLS_MD_SHA256, 1);
mbedtls_md_hmac_starts(&ctx, key, keyLen);
mbedtls_md_hmac_update(&ctx, data, dataLen);
mbedtls_md_hmac_finish(&ctx, hmacResult);
mbedtls_md_free(&ctx);
// hmacResult → base64::encode() → urlEncode()
```

### 邮件发送

使用 ReadyMail 库的 SMTP 客户端。短信通知与启动通知只生成 HTML 富文本，配置更新/管理员命令结果等简短通知用纯文本；`sendEmailNotification(subject, body, bodyType)` 的 `bodyType`（`MAIL_BODY_TEXT` / `MAIL_BODY_HTML`）决定写入 `msg.text` 还是 `msg.html`，另一部分不再生成，因此纯 HTML 邮件为 `text/html` 单部分；正文为空时直接跳过发送。

```cpp
smtp.connect(server, port, callback);
smtp.authenticate(user, pass, readymail_auth_password);
SMTPMessage msg;
msg.headers.add(rfc822_from, from);
msg.headers.add(rfc822_to, to);
msg.headers.add(rfc822_subject, subject);
if (bodyType == MAIL_BODY_HTML) msg.html.body(body);   // HTML 富文本（由 htmlEscape() 转义后拼接）
else                            msg.text.body(body);   // 纯文本
msg.timestamp = time(nullptr);
smtp.send(msg);
```

> 动态内容（发送者、名称、内容、验证码）需经 `htmlEscape()` 转义后再拼入 HTML，避免 `<`/`>`/`&` 破坏版式。

### 网络超时（网络差时的行为）

所有网络操作都有显式上界，避免一次阻塞把主循环冻死：

| 常量 | 值 | 作用 |
|---|---|---|
| `HTTP_TIMEOUT_MS` | 8000 | 单次 HTTP 请求（连接 + 读写）上限，通过 `http.setTimeout()` 设置 |
| `SMTP_SOCKET_TIMEOUT_MS` | 15000 | SMTP 单次 socket 读写上限 |
| `NOTIFY_ATTEMPT_MAX` | 3 | 单步（单通道 / 单封邮件）最多尝试次数 |
| `NOTIFY_MAX_WAIT_MS` | 120000 | WiFi 断开（或模组未就绪）时等待恢复的最长时间 |
| `NOTIFY_JOB_DEADLINE_MS` | 180000 | 单条通知的整体时限，超时丢弃 |

**SMTP 超时特别说明**：ReadyMail 库默认的读取超时高达 **120 秒**（`smtp_timeout.read`），且未暴露设置接口；
服务器不响应时会把主循环整个卡住两分钟。库在认证完成后会把底层 socket 超时重设成这个值，
因此 `emailAttemptOnce()` 在连接前和认证后各调用一次 `ssl_client.setTimeout(SMTP_SOCKET_TIMEOUT_MS)` 把它压回来。

**弱网下的取舍**：超时取值偏宽容（HTTP 8s / SMTP 15s），宁可多等也不要动不动失败；
同时用「总时限 180s + 队列深度 3」兜底——网络长时间不可用时旧通知会被丢弃，
保证新短信仍能入队，而不是让队列被卡死的任务占满。

### 修改指南

- **添加新推送通道**: 在 `PushType` 加枚举 → `isPushChannelValid()` 加校验 → `sendToChannel()` 加 case → Web UI 加选项
- **修改钉钉/飞书签名逻辑**: 编辑 `dingtalkSign()` 或 `sendToChannel()` 中 FEISHU case
- **更换 SMTP 库**: 只需修改 `emailAttemptOnce()`（单次尝试）与 `sendEmailNotification()`（同步重试封装）
- **新增异步通知类型**: 在 `task_types.h` 的 `NotifyJobType` / `NotifyStage` 加枚举 → `push.cpp` 加入队函数与 `processNotifyQueue()` 的阶段分支

---

## sms_process.h / sms_process.cpp — 短信处理

### 长短信合并状态机

```
┌─────────────────────────────────────────────────────┐
│                  concatBuffer[0..4]                  │
│  ┌─────────┐  ┌─────────┐      ┌─────────┐         │
│  │ Slot 0  │  │ Slot 1  │ ...  │ Slot 4  │         │
│  │ inUse   │  │ inUse   │      │ inUse   │         │
│  │ refNum  │  │ refNum  │      │ refNum  │         │
│  │ total   │  │ total   │      │ total   │         │
│  │ recv'd  │  │ recv'd  │      │ recv'd  │         │
│  │ time    │  │ time    │      │ time    │         │
│  │ parts[] │  │ parts[] │      │ parts[] │         │
│  └─────────┘  └─────────┘      └─────────┘         │
└─────────────────────────────────────────────────────┘
```

### PDU 解析流程

```
checkSerial1URC() 循环:
  ┌─ IDLE 状态 ──────────────────────────────────────┐
  │  逐行读取 Serial1                                 │
  │  检测 "+CMT:" → 转入 WAIT_PDU                    │
  └──────────────────────────────────────────────────┘
                       │
  ┌─ WAIT_PDU 状态 ──────────────────────────────────┐
  │  读取下一行                                       │
  │  isHexString()?                                   │
  │    ├─ 是 → pdu.decodePDU(line)                   │
  │    │       ├─ 失败 → 打印错误, 回 IDLE            │
  │    │       └─ 成功 → 提取 sender/text/timestamp   │
  │    │                → 检查 concatInfo              │
  │    │                  ├─ 长短信 → 缓存分段         │
  │    │                  │   └─ 收齐? → 合并→process │
  │    │                  └─ 普通短信 → process        │
  │    └─ 否 → 回 IDLE                               │
  └──────────────────────────────────────────────────┘
```

### 黑名单匹配算法

```
原始发送者号码: "+8613800138000"
  │
  ├─ 提取纯号码: "13800138000" (去+86)
  │
  └─ 逐行对比黑名单:
       黑名单行 "13800138000" → 匹配 ✓
       黑名单行 "13900139000" → 不匹配
       黑名单行 "+8613800138000" → 匹配 ✓ (用原始号码对比)
```

### 管理员命令解析

```
短信内容: "SMS:13800138000:你好世界"
  │
  ├─ 第一个 ':'  → "SMS"
  ├─ 第二个 ':'  → "13800138000"  (目标号码)
  └─ 剩余内容    → "你好世界"      (短信内容)
  │
  └─ notifyQueueAdminSms("13800138000", "你好世界", cmd)   ← 入队，立即返回
       │
       ▼ 队列内
     sendSMS("13800138000", "你好世界")  →  结果写入 job.smsOk
       │
       ▼
     邮件通知执行结果

短信内容: "RESET"
  └─ notifyQueueReboot()   ← 入队，立即返回
       │
       ▼ 队列内
     邮件"重启命令已执行"  →  resetModule() + ESP.restart()
```

### 修改指南

- **调整长短信超时**: 修改 `CONCAT_TIMEOUT_MS` 宏（config_types.h）
- **修改黑名单匹配逻辑**: 编辑 `isInNumberBlackList()`
- **添加新管理员命令**: 在 `processAdminCommand()` 中添加 `else if` 分支
- **更换短信库**: 修改 `checkSerial1URC()` 中的 `pdu.decodePDU()` 调用

---

## web_handlers.h / web_handlers.cpp — HTTP 处理 + 日志系统

### 路由表

| 方法 | 路径 | 处理函数 | Auth | 说明 |
|---|---|---|---|---|
| GET | `/` | `handleRoot()` | ✓ | SPA 主页（含 10 个面板） |
| GET | `/tools` | `handleRoot()` | ✓ | 旧链接兼容，返回同一 SPA 页面 |
| GET | `/sms` | `handleRoot()` | ✓ | 旧链接兼容，返回同一 SPA 页面 |
| POST | `/save` | `handleSave()` | ✓ | 保存配置 |
| POST | `/sendsms` | `handleSendSms()` | ✓ | 网页发送短信 |
| POST | `/ping` | `handlePing()` | ✓ | Ping 测试 |
| GET | `/query` | `handleQuery()` | ✓ | 模组/WiFi 信息查询 |
| GET | `/flight` | `handleFlightMode()` | ✓ | 飞行模式控制 |
| GET | `/at` | `handleATCommand()` | ✓ | AT 指令调试 |
| GET | `/log` | `handleLog()` | ✓ | 系统日志（JSON 数组） |

### 日志环形缓冲区

```
容量: LOG_BUF_SIZE = 120 行
结构: String logBuffer[120] + 行缓冲 _logLine
写入:
  logCapture(msg)      → Serial.print() + 追加到 _logLine
  logCaptureLn(msg)    → Serial.println() + 提交 _logLine 到环形缓冲区
  logCaptureF(fmt,...) → Serial.print() + 追加到 _logLine (fmt 以 \n 结尾时自动提交)
读取:
  handlerLog()         → JSON ["行1","行2",...] (按插入顺序)
```

**行缓冲设计目的**: 避免 `logCapture("A:"); logCaptureLn(B);` 在环形缓冲区中产生两个独立条目。实际只产生一行 `"A: B"`。

### 模板变量替换

SPA 页面中的 `%PLACEHOLDER%` 在 `handleRoot()` 中通过 `html.replace()` 替换:

| 占位符 | 数据来源 |
|---|---|
| `%IP%` | `WiFi.localIP().toString()` |
| `%WEB_USER%` / `%WEB_PASS%` | `config.webUser` / `config.webPass` |
| `%SMTP_SERVER%` ~ `%SMTP_SEND_TO%` | `config.smtp*` |
| `%ADMIN_PHONE%` | `config.adminPhone` |
| `%NUMBER_BLACK_LIST%` | `config.numberBlackList` |
| `%SYSTIME%` | `formatSystemTime(time(nullptr))`（UTC+8 展示），未同步时为「未同步」 |
| `%SYSTIME_EPOCH%` / `%TZ_OFFSET%` | 系统时间 UTC 时间戳 / 展示时区偏移，供 JS 秒级自增 |
| `%PUSH_CHANNELS%` | 循环生成 5 个通道的 HTML 表单 |

### 响应格式

| 端点 | Content-Type | 返回格式 |
|---|---|---|
| `/` `/tools` `/sms` `/save` | `text/html` | HTML 页面 |
| `/query` `/flight` `/at` `/ping` | `application/json` | `{"success":bool, "message":"..."}` |
| `/log` | `application/json` | `["行1", "行2", ...]` |

### 修改指南

- **添加新页面**: 在 `web_handlers.cpp` 添加处理函数 → `setup()` 注册路由
- **修改页面样式**: 编辑 `web_html.cpp` 中的 HTML 模板
- **添加查询类型**: 在 `handleQuery()` 的 if-else 链中添加新 type

---

## web_html.h / web_html.cpp — HTML 模板

### 内容说明

**单页应用 (SPA)**：整个项目仅一个 HTML 常量 `htmlPage`（约 500 行），包含 10 个面板：

| 面板 ID | 名称 | 功能 |
|---|---|---|
| `panel-overview` | 系统概览 | 显示 IP/信号/配置状态 |
| `panel-account` | 账号管理 | 修改 Web 登录密码 |
| `panel-email` | 邮件通知 | SMTP 邮件配置 |
| `panel-push` | 推送通道 | 5 个推送通道配置 |
| `panel-admin` | 管理员 & 黑名单 | 管理员号码 + 号码黑名单 |
| `panel-sendsms` | 发送短信 | Web 端发送短信 |
| `panel-diagnose` | 模组诊断 | ATI/信号/SIM/网络查询 |
| `panel-network` | 网络测试 | Ping 测试 |
| `panel-modem` | 模组控制 | 飞行模式开关/模组重启 |
| `panel-atterm` | AT 终端 | AT 指令交互调试 |
| `panel-log` | 系统日志 | 实时日志查看（终端风格） |

**侧边栏**分为"配置"和"工具"两个分组，底部有"修改密码"快捷按钮。JS 通过 `switchPanel(name)` 控制面板显示/隐藏，日志面板激活时自动开始轮询，切换离开后停止。

### 修改指南

- 直接编辑 `web_html.cpp` 中的 `R"rawliteral(...)rawliteral"` 块
- HTML 中的 `"` 在 `R"rawliteral"` 中不需要转义
- 需要动态内容的地方使用 `%PLACEHOLDER%`，在对应 handler 中 replace

---

## wifi_config.h — WiFi 凭据

```cpp
#define WIFI_SSID "liuwifi"
#define WIFI_PASS "Bairuiqin"
```

**修改**: 直接编辑此文件填入实际 WiFi 信息。  
**注意**: 此文件包含明文密码，请勿提交到公开仓库。
