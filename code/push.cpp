#include "push.h"
#include "web_handlers.h"
#include "config.h"
#include "web_handlers.h"
#include "modem.h"
#include "task_types.h"
#include <HTTPClient.h>
#include <mbedtls/md.h>
#include <base64.h>
#include <sys/time.h>

// ---- 邮件 HTML 统一样式表（class 版）----
// 之前每个 <div>/<td> 都内联同一串 CSS，正文里大量重复；这里改为集中定义一次，
// 各构建函数只输出 class，体积更小、改风格只需动这一处。
// 兼容性说明（这是本次改用 class 的代价，需以实际收到的邮件为准）：
//   - Gmail 网页/iOS、Apple Mail、多数手机原生客户端：支持 <style> + class
//   - Outlook 桌面版（Word 排版引擎）：认 class，但不支持渐变等部分属性（标题栏会退化成纯色）
//   - 少数客户端会剥掉 <style>：此时正文退化为无样式表格（仍可读）
// 因此仅把「动态值」（卡片最大宽度、状态徽标配色）以内联方式保留。
static const char MAIL_CSS[] =
  ".mw{background:#f4f6f8;padding:16px;font-family:-apple-system,Segoe UI,Helvetica,Arial,sans-serif}"
  ".card{margin:0 auto;background:#ffffff;border:1px solid #e3e8ee;border-radius:10px;overflow:hidden}"
  ".hd{background:linear-gradient(135deg,#4f8cff,#2f6fed);color:#ffffff;padding:14px 20px}"
  ".hd span{font-size:17px;font-weight:600}"
  ".blk{padding:16px 20px 4px}"                     /* 带小标题的区块 */
  ".sub{padding:0 20px 4px}"                        /* 次级分组（模组信息/信号/号码） */
  ".rows{padding:16px 20px}"                        /* 只有表格的区块 */
  ".cap{font-size:13px;font-weight:600;color:#2f6fed;margin-bottom:6px}"
  ".tb{width:100%;border-collapse:collapse;font-size:14px;color:#333;line-height:1.5}"
  ".k{padding:8px 0;width:96px;color:#888;vertical-align:top;white-space:nowrap}"
  ".v{padding:8px 0;border-bottom:1px solid #f0f0f0;word-break:break-all}"
  ".kn{padding:8px 0;width:64px;color:#888;vertical-align:top}"
  ".vn{padding:8px 0;border-bottom:1px solid #f0f0f0}"
  ".vl{padding:8px 0;word-break:break-word}"
  ".info{padding:12px 20px 0;border-top:1px solid #f0f0f0;margin-top:12px}"
  ".info .tb{font-size:13px;color:#666}"
  ".ft{padding:10px 20px;background:#fafbfc;color:#aaa;font-size:12px;border-top:1px solid #f0f0f0;margin-top:12px}"
  ".badge{margin:16px 20px 0;border-radius:8px;padding:12px 16px}"
  ".b1{font-size:14px;font-weight:600}"
  ".b2{font-size:13px;color:#666}"
  ".note{padding:16px 20px;font-size:14px;color:#333;line-height:1.6}"
  ".kbd{background:#f4f6f8;padding:2px 6px;border-radius:4px}"
  ".code{margin:16px 20px 0;background:#fff4f4;border:1px solid #ffd0d0;border-radius:8px;padding:12px 16px;text-align:center}"
  ".code1{font-size:12px;color:#c0392b;letter-spacing:2px}"
  ".code2{font-size:28px;font-weight:700;color:#d00;letter-spacing:5px;margin-top:2px}"
  ".hl{color:#2f6fed}";

// 邮件配置是否完整
static bool emailConfigured() {
  return config.smtpServer.length() > 0 && config.smtpUser.length() > 0 &&
         config.smtpPass.length() > 0 && config.smtpSendTo.length() > 0;
}

// ---- 网络超时 ----
// 取值原则：网络差时要足够宽容（宁可多等一会也不要动不动失败），
// 但必须有硬上界，否则一次阻塞就能把主循环冻死。
#define HTTP_TIMEOUT_MS          8000   // 单次 HTTP 请求（连接 + 读写）上限
#define SMTP_SOCKET_TIMEOUT_MS  15000   // SMTP 单次 socket 读写上限
                                        // ReadyMail 默认 read 超时高达 120 秒，服务器不响应时会
                                        // 把整个主循环卡死两分钟；库未暴露该选项，
                                        // 只能在它重设之后再次覆盖底层 socket 超时。
#define NOTIFY_JOB_DEADLINE_MS  180000UL // 单条通知的整体时限，超时丢弃，避免弱网时长期占住队列
#define MAILGUN_TIMEOUT_MS      15000  // Mailgun：走 TLS 且要上传 HTML 正文，单次请求上限比普通通道放宽
#define MAILGUN_HTML_MAX_CHARS  6000   // Mailgun：HTML 正文上限，超出只发纯文本（urlEncode 后体积会膨胀数倍）

// 覆盖底层 socket 超时（ReadyMail 在认证后会把超时重设为 120 秒，需要再次压回来）
static void applySmtpSocketTimeout() {
  ssl_client.setTimeout(SMTP_SOCKET_TIMEOUT_MS);
}

// 单次邮件发送尝试（不含重试与退避）。
// 同步发送（sendEmailNotification）与异步队列共用它，队列靠返回值决定出队还是退避重试。
static NotifyStep emailAttemptOnce(const char* subject, const char* body, MailBodyType bodyType) {
  auto statusCallback = [](SMTPStatus status) {
    logCaptureLn(String(status.text));
  };

  // 清理上一次尝试的残留连接状态，确保每次从干净状态开始
  smtp.stop();
  applySmtpSocketTimeout();   // 连接与 TLS 握手阶段的读写上限

  if (!smtp.connect(config.smtpServer.c_str(), config.smtpPort, statusCallback)) {
    logCaptureLn(String("邮件服务器连接失败"));
    return STEP_RETRY;
  }

  if (!smtp.authenticate(config.smtpUser.c_str(), config.smtpPass.c_str(), readymail_auth_password)) {
    // 认证失败属配置错误，重试无意义
    logCaptureLn(String("邮件认证失败（请检查账号与SMTP授权码），停止重试"));
    smtp.stop();
    return STEP_FAILED;
  }

  // 认证完成后库会把 socket 超时改成它自己的 120 秒，这里压回我们的上限
  applySmtpSocketTimeout();

  SMTPMessage msg;
  String from = "SMS Notification <"; from += config.smtpUser; from += ">";
  msg.headers.add(rfc822_from, from.c_str());
  String to = "your_email <"; to += config.smtpSendTo; to += ">";
  msg.headers.add(rfc822_to, to.c_str());
  msg.headers.add(rfc822_subject, subject);
  // 按 bodyType 只写入一种正文：纯 HTML 时邮件为 text/html 单部分
  if (bodyType == MAIL_BODY_HTML) {
    msg.html.body(body);
  } else {
    msg.text.body(body);
  }
  msg.timestamp = time(nullptr);

  if (smtp.send(msg)) {
    logCaptureLn(String("[邮件] 发送成功"));
    smtp.stop();
    return STEP_OK;
  }
  logCaptureLn(String("邮件发送失败"));
  smtp.stop();
  return STEP_RETRY;
}

// 发送邮件通知函数（带重试，同步阻塞）
// 注意：本函数会一直占用主循环，仅用于「必须立刻发出」的场景（如重启前的通知）。
// 短信转发等常规通知必须走 notifyQueueSms()/notifyQueueEmail()，由主循环分片执行。
void sendEmailNotification(const char* subject, const char* body, MailBodyType bodyType) {
  if (!emailConfigured()) {
    logCaptureLn(String("邮件配置不完整，跳过发送"));
    return;
  }

  if (!body || strlen(body) == 0) {
    logCaptureLn(String("邮件正文为空，跳过发送"));
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    logCaptureLn(String("WiFi未连接，跳过邮件发送"));
    return;
  }

  const int MAX_ATTEMPTS = 3; // 总尝试次数（含首次）
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    if (attempt > 1) {
      delay(1000 * (attempt - 1)); // 退避：第2次前等1秒，第3次前等2秒
      logCaptureF("[邮件] 重试 (%d/%d)...\n", attempt, MAX_ATTEMPTS);
    }

    NotifyStep r = emailAttemptOnce(subject, body, bodyType);
    if (r == STEP_OK || r == STEP_FAILED) return;
  }

  logCaptureLn(String("邮件多次重试后仍失败，本次通知已丢弃"));
  smtp.stop();
}

// 带小标题的表格块（启动邮件各分组使用）
static String mailSection(const String& caption, const String& rows) {
  return "<div class=\"sub\"><div class=\"cap\">" + htmlEscape(caption) + "</div>"
         "<table class=\"tb\">" + rows + "</table></div>";
}

// 发送"设备已启动"通知邮件：在模组初始化完成后调用，
// 收集设备/模组/信号/号码等信息，以 HTML 富文本发送（不再额外生成纯文本正文）
void sendStartupEmail() {
  if (config.smtpServer.length() == 0 || config.smtpUser.length() == 0 ||
      config.smtpPass.length() == 0 || config.smtpSendTo.length() == 0) {
    logCaptureLn(String("邮件配置不完整，跳过启动通知"));
    return;
  }
  if (!emailNotifyEnabled(EMAIL_NOTIFY_STARTUP)) {
    logCaptureLn(String("「设备启动」邮件通知已关闭，跳过启动通知"));
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    logCaptureLn(String("WiFi未连接，跳过启动通知"));
    return;
  }

  // ---- 收集设备/模组/信号信息 ----
  String deviceUrl = getDeviceUrl();
  String ip = WiFi.localIP().toString();
  int wifiRssi = WiFi.RSSI();

  // 系统时间由 buildMailHtml() 的公共信息块统一附加，此处不再重复
  // 模组初始化状态
  String initStatus = modemReady ? "已就绪" : "未就绪";
  String netStatus  = modemReady ? "已注册网络" : "未注册（无SIM卡或信号差）";

  // 模组信息（全局变量，由 modemInit 解析 ATI 写入）
  // 注意：不再展示模组固件版本——这批 4G 模组没有可靠的固件版本查询命令（ATI 只有厂商/型号），
  // 展示出来的值要么恒为「未知」要么是误取到的型号串，反而误导。
  String manufacturer = modemManufacturer;
  String model = modemModel;

  // 信号
  SignalInfo sig;
  bool sigOk = getModemSignal(sig);
  String sigQuality = sig.quality;
  String rsrpStr = sig.lte ? sig.rsrpText : "—";
  String rsrqStr = sig.lte ? sig.rsrqText : "—";
  String rssiStr = sig.rssiText;
  String sigSource = sigOk ? sig.source : "未取到";

  // 本机号码由 buildMailHtml() 的公共信息块统一附加（取不到时回退设备 IP）
  String adminNumber = config.adminPhone.length() > 0 ? config.adminPhone : "未设置";

  // 推送通道统计
  int enabledChannels = 0;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) enabledChannels++;
  }
  String channelSummary = String(enabledChannels) + " 个已启用 / 共 " + String(MAX_PUSH_CHANNELS) + " 个";

  // ---- HTML 富文本正文（与其他通知邮件同一套卡片风格）----
  // 模组状态徽标
  String badgeBg = modemReady ? "#e6f7ec" : "#fff4e6";
  String badgeBorder = modemReady ? "#bfe9cd" : "#ffd9a8";
  String badgeColor = modemReady ? "#1a8a4a" : "#c97a00";
  // 徽标配色是动态值（就绪/未就绪），仍用内联
  String inner = "<div class=\"badge\" style=\"background:" + badgeBg + ";border:1px solid " + badgeBorder + ";\">";
  inner += "<span class=\"b1\" style=\"color:" + badgeColor + ";\">" + htmlEscape(initStatus) + "</span>";
  inner += " <span class=\"b2\">" + htmlEscape(netStatus) + "</span>";
  inner += "</div>";

  // 设备信息
  inner += "<div class=\"blk\"><div class=\"cap\">📡 设备信息</div><table class=\"tb\">";
  // 按「标识 → 型号/固件 → 网络 → 配置」排列，与网页概览的「📡 设备信息」顺序保持一致
  inner += buildMailRow("设备地址", deviceUrl);
  inner += buildMailRow("IP地址", ip);
  inner += buildMailRow("MAC地址", WiFi.macAddress());
  inner += buildMailRow("芯片型号", getChipModelName());
  inner += buildMailRow("固件版本", String(FW_BUILD_STAMP));
  inner += buildMailRow("WiFi信号", String(wifiRssi) + " dBm");
  inner += buildMailRow("推送通道", channelSummary);
  inner += "</table></div>";

  // 模组信息 / 信号状态 / 号码信息
  inner += mailSection("🔧 模组信息", buildMailRow("制造商", manufacturer) +
                                     buildMailRow("型号", model));
  inner += mailSection("📶 信号状态", buildMailRow("评级", sigQuality) +
                                     buildMailRow("RSRP", rsrpStr) +
                                     buildMailRow("RSRQ", rsrqStr) +
                                     buildMailRow("RSSI", rssiStr) +
                                     buildMailRow("数据来源", sigSource));
  inner += mailSection("📱 号码信息", buildMailRow("管理员号码", adminNumber));

  String html = buildMailHtml("🚀 短信转发器已启动", inner, 600);
  sendEmailNotification("短信转发器已启动", html.c_str(), MAIL_BODY_HTML);
}

// URL编码辅助函数
String urlEncode(const String& str) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encoded += '+';
    } else if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

// 钉钉签名函数（时间戳为UTC毫秒级）
String dingtalkSign(const String& secret, int64_t timestamp) {
  String stringToSign = String(timestamp) + "\n" + secret;
  
  uint8_t hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, (const unsigned char*)secret.c_str(), secret.length());
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);
  
  String base64Encoded = base64::encode(hmacResult, 32);
  return urlEncode(base64Encoded);
}

// 获取当前UTC毫秒级时间戳（用于钉钉签名）
int64_t getUtcMillis() {
  struct timeval tv;
  if (gettimeofday(&tv, NULL) == 0) {
    return (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000;
  }
  // 如果获取失败，使用time()函数
  return (int64_t)time(nullptr) * 1000LL;
}

// JSON转义函数
String jsonEscape(const String& str) {
  String result = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == '"') result += "\\\"";
    else if (c == '\\') result += "\\\\";
    else if (c == '\n') result += "\\n";
    else if (c == '\r') result += "\\r";
    else if (c == '\t') result += "\\t";
    else result += c;
  }
  return result;
}

// HTML转义函数（同时把换行符转为 <br>），用于邮件富文本正文
String htmlEscape(const String& str) {
  String result = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == '&') result += "&amp;";
    else if (c == '<') result += "&lt;";
    else if (c == '>') result += "&gt;";
    else if (c == '"') result += "&quot;";
    else if (c == '\n') result += "<br>";
    else if (c == '\r') { /* 忽略，避免与 \n 重复 */ }
    else result += c;
  }
  return result;
}

// 表格行：左侧灰色字段名 + 右侧值（键值均已转义）
String buildMailRow(const String& key, const String& val) {
  return "<tr><td class=\"k\">" + htmlEscape(key) +
         "</td><td class=\"v\">" + htmlEscape(val) + "</td></tr>";
}

// 把若干表格行包成带内边距的表格块
String buildMailTable(const String& rows) {
  return "<div class=\"blk\"><table class=\"tb\">" + rows + "</table></div>";
}

// 所有通知邮件共用的一段「设备标识信息」：系统时间 + 本机号码（取不到号码时回退设备 IP）。
// 每条通知都应能看出「什么时候、哪台设备」，因此统一在 buildMailHtml() 里追加，
// 各邮件不必各自拼装，新增邮件类型也自动带上（只读缓存，不碰串口）。
static String buildMailInfoBlock() {
  String rows = buildMailRow("系统时间", timeSynced ? formatSystemTime(time(nullptr)) : String("未同步"));
  if (modemOwnNumber.length() > 0) {
    rows += buildMailRow("本机号码", modemOwnNumber);
  } else {
    rows += buildMailRow("设备IP", getDeviceIp());   // 无号码时用 IP 标识设备
  }
  return "<div class=\"info\"><table class=\"tb\">" + rows + "</table></div>";
}

// 完整 HTML 邮件正文：外层灰底 + 白色卡片 + 渐变标题栏 + 卡片主体 + 设备标识信息 + 底部标识
String buildMailHtml(const String& title, const String& inner, int maxWidth) {
  // 样式表随每封邮件一起发出：邮件正文是片段，没有 <head> 可放 <style>，只能贴在正文开头
  String html = "<style>" + String(MAIL_CSS) + "</style>";
  html += "<div class=\"mw\">";
  // 卡片宽度按邮件类型可变，动态值保留内联
  html += "<div class=\"card\" style=\"max-width:" + String(maxWidth) + "px\">";
  html += "<div class=\"hd\"><span>" + title + "</span></div>";
  html += inner;
  html += buildMailInfoBlock();
  html += "<div class=\"ft\">SMS Forwarder · 短信转发通知</div>";
  html += "</div></div>";
  return html;
}

// 扫描某一类括号的全部出现，按"到最近边缘的距离"更新最优候选
// open/close 为括号字符串（多字节需注意字节长度 openLen/closeLen），其余参数为输入输出的最优候选
static void scanBracket(const String& msg, const char* open, int openLen, const char* close, int closeLen,
                        int& bestDist, int& bestSegStart, int& bestCStart, int& bestCEnd) {
  int n = msg.length();
  int pos = 0;
  while (pos < n) {
    int o = msg.indexOf(open, pos);
    if (o < 0) break;
    int c = msg.indexOf(close, o + openLen);
    if (c < 0) break;
    int contentStart = o + openLen;   // 括号内纯内容起点（跳过整个开括号）
    int contentEnd = c;               // 闭括号起点（不含闭括号）
    if (contentEnd > contentStart) {
      int segStart = o;
      int segEnd = c + closeLen;
      // 到最近边缘（开头或结尾）的距离，越小越优先
      int dist = (segStart < (n - segEnd)) ? segStart : (n - segEnd);
      if (dist < bestDist || (dist == bestDist && segStart < bestSegStart)) {
        bestDist = dist;
        bestSegStart = segStart;
        bestCStart = contentStart;
        bestCEnd = contentEnd;
      }
    }
    pos = c + closeLen;
  }
}

// 把短信 PDU 时间戳格式化为可读形式。
// pdulib 的 getTimeStamp() 返回紧凑串「YYMMDDHHMMSS」+ 两位时区（单位 15 分钟），
// 例如 "26100123500032" 表示 2026-10-01 23:50:00、时区 32×15 分钟 = UTC+8。
// 原样透出到邮件/推送里完全看不懂，因此在时间进入通知前统一转成
// "2026-10-01 23:50:00 (UTC+8)"。
// 长度不足或含非数字时认为它已是可读串（或异常），原样返回，避免把内容改坏。
String formatSmsTimestamp(const char* raw) {
  String s = raw ? String(raw) : String("");
  s.trim();
  if (s.length() < 12) return s;
  for (int i = 0; i < 12; i++) {
    if (!isdigit((unsigned char)s.charAt(i))) return s;
  }

  int year  = 2000 + (s.charAt(0) - '0') * 10 + (s.charAt(1) - '0');   // PDU 只带两位年份
  int mon   = (s.charAt(2) - '0') * 10 + (s.charAt(3) - '0');
  int day   = (s.charAt(4) - '0') * 10 + (s.charAt(5) - '0');
  int hour  = (s.charAt(6) - '0') * 10 + (s.charAt(7) - '0');
  int min   = (s.charAt(8) - '0') * 10 + (s.charAt(9) - '0');
  int sec   = (s.charAt(10) - '0') * 10 + (s.charAt(11) - '0');

  // 时区：第 13~14 位为 15 分钟的倍数；缺失时按展示时区兜底
  String tzText;
  if (s.length() >= 14 && isdigit((unsigned char)s.charAt(12)) && isdigit((unsigned char)s.charAt(13))) {
    int totalMin = ((s.charAt(12) - '0') * 10 + (s.charAt(13) - '0')) * 15;
    int tzHour = totalMin / 60;
    int tzMin  = totalMin % 60;
    if (tzHour == 0 && tzMin == 0) {
      tzText = " (UTC)";
    } else if (tzMin == 0) {
      tzText = " (UTC+" + String(tzHour) + ")";
    } else {
      tzText = " (UTC+" + String(tzHour) + ":" + (tzMin < 10 ? "0" : "") + String(tzMin) + ")";
    }
  } else {
    tzText = " (UTC+" + String(DISPLAY_TZ_OFFSET_HOURS) + ")";
  }

  char buf[24];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", year, mon, day, hour, min, sec);
  return String(buf) + tzText;
}

// 从短信正文解析发送者名称与验证码
// 发送者名称：扫描全部【...】/[[...]]/[...]，优先选取最靠近开头或结尾的括号内容；无则空串
// 验证码：取首个长度 4~6 的连续数字串（等价于"前后不为其他数字"），无则空串
void parseSmsMeta(const String& message, String& senderName, String& verifyCode) {
  senderName = "";
  verifyCode = "";

  // 1) 发送者名称：优先最靠近边缘的中括号内容
  int n = message.length();
  int bestDist = 0x7FFFFFFF;
  int bestSegStart = 0x7FFFFFFF;
  int bestCStart = -1, bestCEnd = -1;
  scanBracket(message, "【", 3, "】", 3, bestDist, bestSegStart, bestCStart, bestCEnd);
  scanBracket(message, "[[", 2, "]]", 2, bestDist, bestSegStart, bestCStart, bestCEnd);
  scanBracket(message, "[", 1, "]", 1, bestDist, bestSegStart, bestCStart, bestCEnd);
  if (bestCStart >= 0 && bestCEnd > bestCStart) {
    senderName = message.substring(bestCStart, bestCEnd);
    senderName.trim();
  }

  // 2) 验证码：首个长度 4~6 的连续数字串
  int i = 0;
  while (i < n) {
    if (isdigit((unsigned char)message.charAt(i))) {
      int j = i;
      while (j < n && isdigit((unsigned char)message.charAt(j))) j++;
      int len = j - i;
      if (len >= 4 && len <= 6) {
        verifyCode = message.substring(i, j);
        break;
      }
      i = j;
    } else {
      i++;
    }
  }
}

// 判断业务响应体是否表示成功
// 部分平台无论成败 HTTP 都返回 200，错误信息在响应体中，需要额外校验
static bool isBodySuccess(const PushChannel& channel, const String& resp) {
  switch (channel.type) {
    case PUSH_TYPE_DINGTALK:   return resp.indexOf("\"errcode\":0") >= 0;
    case PUSH_TYPE_FEISHU:     return resp.indexOf("\"code\":0") >= 0;
    case PUSH_TYPE_PUSHPLUS:   return resp.indexOf("\"code\":200") >= 0;
    case PUSH_TYPE_SERVERCHAN: return resp.indexOf("\"code\":0") >= 0;
    case PUSH_TYPE_TELEGRAM:   return resp.indexOf("\"ok\":true") >= 0;
    case PUSH_TYPE_MAILGUN:    return resp.indexOf("Queued") >= 0; // 成功返回 {"id":"...","message":"Queued. Thank you."}
    default:                   return true; // 其余平台以 HTTP 状态码为准
  }
}

// 执行单个通道的 HTTP 请求。
// maxAttempts=1 时只发一次请求、不做退避 delay，供异步队列在主循环里分片调用：
// 每次 processNotifyQueue() 只推进一次网络请求，退避交给队列的时间戳调度完成。
// authUser/authPass 非空时附加 HTTP Basic 认证（Mailgun 用 api:API_KEY）；timeoutMs 可覆盖默认超时
static NotifyStep executeChannelRequest(const PushChannel& channel, const String& url,
                                        bool isGet, const String& contentType, const String& body,
                                        const String& channelName, int maxAttempts,
                                        const String& authUser = "", const String& authPass = "",
                                        int timeoutMs = HTTP_TIMEOUT_MS) {
  for (int attempt = 1; attempt <= maxAttempts; attempt++) {
    if (attempt > 1) {
      delay(500 * (attempt - 1)); // 退避：第2次前等0.5秒，第3次前等1秒
      logCaptureF("[%s] 重试 (%d/%d)...\n", channelName.c_str(), attempt, maxAttempts);
    }

    HTTPClient http;
    http.begin(url);
    // 显式设置超时：默认行为在网络差时可能长时间不返回，把主循环整个拖住
    http.setTimeout(timeoutMs);
    if (authUser.length() > 0) {
      // HTTP Basic 认证：Authorization: Basic base64(user:pass)
      String cred = authUser + ":" + authPass;
      http.addHeader("Authorization", "Basic " + base64::encode((const uint8_t*)cred.c_str(), cred.length()));
    }
    if (!isGet) http.addHeader("Content-Type", contentType);
    int httpCode = isGet ? http.GET() : http.POST(body);

    if (httpCode <= 0) {
      // 连接失败/超时等传输层错误，可重试
      logCaptureF("[%s] HTTP请求失败: %s\n", channelName.c_str(), http.errorToString(httpCode).c_str());
    } else {
      logCaptureF("[%s] 响应码: %d\n", channelName.c_str(), httpCode);
      bool httpOk = (httpCode >= 200 && httpCode < 300);
      String resp = "";
      if (httpOk) resp = http.getString();

      if (httpOk && isBodySuccess(channel, resp)) {
        logCaptureF("[%s] 推送成功（第 %d/%d 次尝试）\n", channelName.c_str(), attempt, maxAttempts);
        if (resp.length() > 0) logCaptureLn(String("响应: " + resp));
        http.end();
        return STEP_OK;
      }

      if (resp.length() > 0) logCaptureLn(String("响应: " + resp));

      // 明确的 4xx 客户端错误（除 429 限流）多为配置问题，重试无意义
      if (httpCode >= 400 && httpCode < 500 && httpCode != 429) {
        logCaptureF("[%s] 客户端错误 %d，跳过重试\n", channelName.c_str(), httpCode);
        http.end();
        return STEP_FAILED;
      }
      logCaptureLn(String("[" + channelName + "] 本次推送未确认成功"));
    }
    http.end();
  }

  logCaptureLn(String("[" + channelName + "] 多次重试后仍失败"));
  return STEP_RETRY;
}

// 短信邮件正文构建（定义在本文件后段，供 Mailgun 等通道取默认标题/正文）
static void buildSmsMail(const char* sender, const char* message, const char* timestamp,
                         String& subject, String& html);

// 模板占位符替换：{sender} {sender_name} {verify_code} {timestamp} {message} {sender_display}
// escapeValues=true 时对填入值做 HTML 转义（正文是 HTML 上下文，短信内容可能含 < & 等字符）
static String applySmsTemplate(const String& tpl, const char* sender, const char* message,
                               const char* timestamp, const String& senderName,
                               const String& verifyCode, const String& displayName,
                               bool escapeValues) {
  String s(sender), m(message), t(timestamp);
  String sn = senderName, vc = verifyCode, dn = displayName;
  if (escapeValues) {
    s = htmlEscape(s);
    m = htmlEscape(m);
    t = htmlEscape(t);
    sn = htmlEscape(sn);
    vc = htmlEscape(vc);
    dn = htmlEscape(dn);
  }
  String out = tpl;
  out.replace("{sender}", s);
  out.replace("{sender_name}", sn);
  out.replace("{verify_code}", vc);
  out.replace("{timestamp}", t);
  out.replace("{message}", m);
  out.replace("{sender_display}", dn);
  return out;
}

// 发送单个推送通道（统一构建请求参数，由 executeChannelRequest 执行）
// 返回本次结果，便于异步队列判断是否需要退避重试。
NotifyStep sendToChannel(const PushChannel& channel, const char* sender, const char* message, const char* timestamp,
                         const char* senderName, const char* verifyCode, int maxAttempts) {
  if (!channel.enabled) return STEP_OK;   // 未启用：直接算作这一步完成

  // 时间戳统一格式化（PDU 紧凑串 → 可读形式），已是格式化结果时原样返回。
  // 放在这里兜底：无论调用方传进来的是原始 PDU 时间戳还是已处理过的，推送出去的都是可读串。
  String tsFormatted = formatSmsTimestamp(timestamp);
  timestamp = tsFormatted.c_str();

  // 对于某些推送方式，URL可以为空（使用默认URL）
  bool needUrl = (channel.type == PUSH_TYPE_POST_JSON || channel.type == PUSH_TYPE_BARK ||
                  channel.type == PUSH_TYPE_GET || channel.type == PUSH_TYPE_DINGTALK ||
                  channel.type == PUSH_TYPE_CUSTOM);
  if (needUrl && channel.url.length() == 0) return STEP_OK;

  String channelName = channel.name.length() > 0 ? channel.name : ("通道" + String(channel.type));
  logCaptureLn(String("发送到推送通道: " + channelName));

  String senderEscaped = jsonEscape(String(sender));
  String messageEscaped = jsonEscape(String(message));
  String timestampEscaped = jsonEscape(String(timestamp));
  String senderNameEscaped = jsonEscape(String(senderName));
  String verifyCodeEscaped = jsonEscape(String(verifyCode));
  String senderNameStr = String(senderName);
  String verifyCodeStr = String(verifyCode);
  // 优化后的发送者名称：优先 sender_name，为空时回退到 sender
  String displayNameStr = senderNameStr.length() > 0 ? senderNameStr : String(sender);
  String displayNameEscaped = jsonEscape(displayNameStr);

  // 统一的富文本（含发送者名称与验证码），供文本类通道使用
  String notifyText = "📱短信通知\n发送者: " + String(sender);
  if (senderNameStr.length() > 0) notifyText += " (" + senderNameStr + ")";
  notifyText += "\n内容: " + String(message);
  if (verifyCodeStr.length() > 0) notifyText += "\n验证码: " + verifyCodeStr;
  notifyText += "\n时间: " + String(timestamp);
  String notifyTextEscaped = jsonEscape(notifyText);

  // 统一标题（参考 send_notification.sh：验证码优先入标题）
  String titleText;
  if (verifyCodeStr.length() > 0) {
    titleText = "验证码: " + verifyCodeStr + " 来自 " + (senderNameStr.length() > 0 ? senderNameStr : String(sender));
  } else if (senderNameStr.length() > 0) {
    titleText = "来自 " + senderNameStr + " 的短信";
  } else {
    titleText = "来自 " + String(sender) + " 的短信";
  }
  String titleEscaped = jsonEscape(titleText);

  String reqUrl = "";
  String reqBody = "";
  String reqContentType = "application/json";
  bool useGet = false;
  String authUser = "";         // HTTP Basic 认证用户名（Mailgun 固定为 api）
  String authPass = "";         // HTTP Basic 认证密码（Mailgun 为 API Key）
  int timeoutMs = HTTP_TIMEOUT_MS;

  switch (channel.type) {
    case PUSH_TYPE_POST_JSON: {
      // 标准POST JSON格式
      reqUrl = channel.url;
      reqBody = "{";
      reqBody += "\"sender\":\"" + senderEscaped + "\",";
      reqBody += "\"message\":\"" + messageEscaped + "\",";
      reqBody += "\"timestamp\":\"" + timestampEscaped + "\",";
      reqBody += "\"sender_name\":\"" + senderNameEscaped + "\",";
      reqBody += "\"verify_code\":\"" + verifyCodeEscaped + "\",";
      reqBody += "\"sender_display\":\"" + displayNameEscaped + "\"";
      reqBody += "}";
      logCaptureLn(String("POST JSON: " + reqBody));
      break;
    }

    case PUSH_TYPE_BARK: {
      // Bark推送格式
      reqUrl = channel.url;
      reqBody = "{";
      reqBody += "\"title\":\"" + titleEscaped + "\",";
      reqBody += "\"body\":\"" + messageEscaped;
      if (senderNameStr.length() > 0) reqBody += "\\n发件人: " + senderNameEscaped;
      if (verifyCodeStr.length() > 0) reqBody += "\\n验证码: " + verifyCodeEscaped;
      reqBody += "\"}";
      logCaptureLn(String("BARK JSON: " + reqBody));
      break;
    }

    case PUSH_TYPE_GET: {
      // GET请求，参数放URL里
      String getUrl = channel.url;
      if (getUrl.indexOf('?') == -1) {
        getUrl += "?";
      } else {
        getUrl += "&";
      }
      getUrl += "sender=" + urlEncode(String(sender));
      getUrl += "&message=" + urlEncode(String(message));
      getUrl += "&timestamp=" + urlEncode(String(timestamp));
      getUrl += "&sender_name=" + urlEncode(senderNameStr);
      getUrl += "&verify_code=" + urlEncode(verifyCodeStr);
      getUrl += "&sender_display=" + urlEncode(displayNameStr);
      logCaptureLn(String("GET URL: " + getUrl));
      reqUrl = getUrl;
      useGet = true;
      break;
    }

    case PUSH_TYPE_DINGTALK: {
      // 钉钉机器人（如果配置了secret，需要添加签名）
      String webhookUrl = channel.url;
      if (channel.key1.length() > 0) {
        // 获取UTC毫秒级时间戳（钉钉要求）
        int64_t ts = getUtcMillis();
        String sign = dingtalkSign(channel.key1, ts);
        if (webhookUrl.indexOf('?') == -1) {
          webhookUrl += "?";
        } else {
          webhookUrl += "&";
        }
        // 使用字符串拼接避免int64_t转换问题
        char tsBuf[21];
        snprintf(tsBuf, sizeof(tsBuf), "%lld", ts);
        webhookUrl += "timestamp=" + String(tsBuf) + "&sign=" + sign;
      }
      reqUrl = webhookUrl;
      reqBody = "{\"msgtype\":\"text\",\"text\":{\"content\":\"";
      reqBody += notifyTextEscaped;
      reqBody += "\"}}";
      logCaptureLn(String("钉钉: " + reqBody));
      break;
    }

    case PUSH_TYPE_PUSHPLUS: {
      // PushPlus
      reqUrl = channel.url.length() > 0 ? channel.url : "http://www.pushplus.plus/send";
      // 发送渠道
      String channelValue = "wechat";
      if (channel.key2.length() > 0) {
          // 仅支持微信公众号（wechat）、浏览器插件（extension）和 PushPlus App（app）三种渠道
          if (channel.key2 == "wechat" || channel.key2 == "extension" || channel.key2 == "app") {
              channelValue = channel.key2;
          } else {
              logCaptureLn(String("Invalid PushPlus channel '" + channel.key2 + "'. Using default 'wechat'."));
          }
      }
      reqBody = "{";
      reqBody += "\"token\":\"" + channel.key1 + "\",";
      reqBody += "\"title\":\"" + titleEscaped + "\",";
      reqBody += "\"content\":\"<b>发送者:</b> " + senderEscaped + (senderNameStr.length() > 0 ? " (<b>" + senderNameEscaped + "</b>)" : "") + "<br><b>时间:</b> " + timestampEscaped + (verifyCodeStr.length() > 0 ? "<br><b>验证码:</b> " + verifyCodeEscaped : "") + "<br><b>内容:</b><br>" + messageEscaped + "\",";
      reqBody += "\"channel\":\"" + channelValue + "\"";
      reqBody += "}";
      logCaptureLn(String("PushPlus: " + reqBody));
      break;
    }

    case PUSH_TYPE_SERVERCHAN: {
      // Server酱
      reqUrl = channel.url.length() > 0 ? channel.url : ("https://sctapi.ftqq.com/" + channel.key1 + ".send");
      reqContentType = "application/x-www-form-urlencoded";
      reqBody = "title=" + urlEncode(titleText);
      reqBody += "&desp=" + urlEncode("**发送者:** " + String(sender) + (senderNameStr.length() > 0 ? " (" + senderNameStr + ")" : "") + "\n\n**时间:** " + String(timestamp) + (verifyCodeStr.length() > 0 ? "\n\n**验证码:** " + verifyCodeStr : "") + "\n\n**内容:**\n\n" + String(message));
      logCaptureLn(String("Server酱: " + reqBody));
      break;
    }

    case PUSH_TYPE_CUSTOM: {
      // 自定义模板
      if (channel.customBody.length() == 0) {
        logCaptureLn(String("自定义模板为空，跳过"));
        return STEP_OK;
      }
      reqUrl = channel.url;
      reqBody = channel.customBody;
      reqBody.replace("{sender}", senderEscaped);
      reqBody.replace("{message}", messageEscaped);
      reqBody.replace("{timestamp}", timestampEscaped);
      reqBody.replace("{sender_name}", senderNameEscaped);
      reqBody.replace("{verify_code}", verifyCodeEscaped);
      reqBody.replace("{sender_display}", displayNameEscaped);
      logCaptureLn(String("自定义: " + reqBody));
      break;
    }

    case PUSH_TYPE_FEISHU: {
      // 飞书机器人
      String jsonData = "{";

      // 如果配置了secret，需要添加签名（飞书使用秒级时间戳）
      if (channel.key1.length() > 0) {
        int64_t ts = time(nullptr);
        // 飞书签名: base64(HMAC-SHA256(key=timestamp + "\n" + secret, msg=""))
        String stringToSign = String(ts) + "\n" + channel.key1;
        uint8_t hmacResult[32];
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
        mbedtls_md_hmac_starts(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
        mbedtls_md_hmac_finish(&ctx, hmacResult);
        mbedtls_md_free(&ctx);
        String sign = base64::encode(hmacResult, 32);

        jsonData += "\"timestamp\":\"" + String(ts) + "\",";
        jsonData += "\"sign\":\"" + sign + "\",";
      }

      // 飞书消息体
      jsonData += "\"msg_type\":\"text\",";
      jsonData += "\"content\":{\"text\":\"";
      jsonData += notifyTextEscaped;
      jsonData += "\"}}";

      reqUrl = channel.url;
      reqBody = jsonData;
      logCaptureLn(String("飞书: " + reqBody));
      break;
    }

    case PUSH_TYPE_GOTIFY: {
      // Gotify 推送
      String gotifyUrl = channel.url;
      // 确保URL以/结尾
      if (!gotifyUrl.endsWith("/")) gotifyUrl += "/";
      gotifyUrl += "message?token=" + channel.key1;

      reqUrl = gotifyUrl;
      reqBody = "{";
      reqBody += "\"title\":\"" + titleEscaped + "\",";
      reqBody += "\"message\":\"" + messageEscaped;
      if (senderNameStr.length() > 0) reqBody += "\\n\\n发件人: " + senderNameEscaped;
      if (verifyCodeStr.length() > 0) reqBody += "\\n\\n验证码: " + verifyCodeEscaped;
      reqBody += "\",";
      reqBody += "\"priority\":5";
      reqBody += "}";
      logCaptureLn(String("Gotify: " + reqBody));
      break;
    }

    case PUSH_TYPE_TELEGRAM: {
      // Telegram Bot 推送（key1: Chat ID, key2: Bot Token）
      String tgBaseUrl = channel.url.length() > 0 ? channel.url : "https://api.telegram.org";
      if (tgBaseUrl.endsWith("/")) tgBaseUrl.remove(tgBaseUrl.length() - 1);

      reqUrl = tgBaseUrl + "/bot" + channel.key2 + "/sendMessage";

      String text = notifyTextEscaped;
      reqBody = "{";
      reqBody += "\"chat_id\":\"" + channel.key1 + "\",";
      reqBody += "\"text\":\"" + text + "\"";
      reqBody += "}";

      logCaptureLn(String("Telegram: " + reqBody));
      break;
    }

    case PUSH_TYPE_MAILGUN: {
      // Mailgun 邮件 API（curl 形态：POST <基址>/v3/<域名>/messages，Basic 认证 api:API_KEY）
      // key1=API Key, key2=域名, key3=收件人, key4=发件人(可选), key5=标题模板(可选), customBody=正文 HTML 模板(可选)
      if (channel.key1.length() == 0 || channel.key2.length() == 0 || channel.key3.length() == 0) {
        logCaptureLn(String("[Mailgun] 配置不完整（需 API Key / 域名 / 收件人），跳过"));
        return STEP_OK;
      }

      // 基址：留空用官方美国节点；EU 区域需填 https://api.eu.mailgun.net
      String base = channel.url;
      base.trim();
      if (base.length() == 0) base = "https://api.mailgun.net";
      if (!base.startsWith("http://") && !base.startsWith("https://")) base = "https://" + base;
      while (base.endsWith("/")) base.remove(base.length() - 1);

      String domain = channel.key2;
      domain.trim();
      reqUrl = base + "/v3/" + domain + "/messages";

      // 发件人：留空时按官方示例派生为 SMS Notification <sms@域名>
      String from = channel.key4;
      from.trim();
      if (from.length() == 0) from = "SMS Notification <sms@" + domain + ">";

      // 标题与正文：模板留空时回退到与邮件通知相同的默认内容
      String defSubject, defHtml;
      if (channel.key5.length() == 0 || channel.customBody.length() == 0) {
        buildSmsMail(sender, message, timestamp, defSubject, defHtml);
      }
      String subject = channel.key5.length() > 0
        ? applySmsTemplate(channel.key5, sender, message, timestamp, senderNameStr, verifyCodeStr, displayNameStr, false)
        : defSubject;
      // 正文是 HTML 上下文：模板里的占位符值需要转义，避免短信内容中的 < & 破坏结构
      String htmlBody = channel.customBody.length() > 0
        ? applySmsTemplate(channel.customBody, sender, message, timestamp, senderNameStr, verifyCodeStr, displayNameStr, true)
        : defHtml;

      reqContentType = "application/x-www-form-urlencoded; charset=utf-8";
      reqBody = "from=" + urlEncode(from);

      // 收件人支持逗号/分号分隔，拆成多个 to 参数
      String tos = channel.key3;
      tos.replace(";", ",");
      int pos = 0;
      while (pos <= tos.length()) {
        int next = tos.indexOf(',', pos);
        if (next < 0) next = tos.length();
        String one = tos.substring(pos, next);
        one.trim();
        if (one.length() > 0) reqBody += "&to=" + urlEncode(one);
        pos = next + 1;
      }

      reqBody += "&subject=" + urlEncode(subject);
      if (htmlBody.length() <= MAILGUN_HTML_MAX_CHARS) {
        reqBody += "&html=" + urlEncode(htmlBody);
      } else {
        logCaptureLn(String("[Mailgun] HTML 正文过长，本次仅发送纯文本"));
      }
      reqBody += "&text=" + urlEncode(notifyText);   // 纯文本兜底，供不支持 HTML 的客户端

      authUser = "api";
      authPass = channel.key1;
      timeoutMs = MAILGUN_TIMEOUT_MS;

      // 不要把 API Key（Basic 认证头）和完整请求体写进日志：Web 端日志对所有人可见
      logCaptureLn(String("Mailgun: " + domain + " 主题=" + subject));
      break;
    }

    default:
      logCaptureLn(String("未知推送类型"));
      return STEP_OK;
  }

  return executeChannelRequest(channel, reqUrl, useGet, reqContentType, reqBody, channelName, maxAttempts,
                               authUser, authPass, timeoutMs);
}

// ===================== 异步通知队列 =====================
// 短信到达时（URC 回调）只把通知排进队列，真正耗时的推送/发邮件由 loop() 分片推进。
// 这样一条短信最多让主循环停顿一次网络请求的时长，而不是把 Web、URC、看门狗全部冻住。

static NotifyJob notifyQueue[NOTIFY_QUEUE_SIZE];
static uint8_t notifyHead = 0;
static uint8_t notifyTail = 0;
static uint8_t notifyCount = 0;

#define NOTIFY_ATTEMPT_MAX      3        // 单个步骤（单个通道/单封邮件）的最大尝试次数
#define NOTIFY_MAX_WAIT_MS      120000UL // WiFi 断开时，通知最多等待重连的时间，超时丢弃

// 安全拷贝字符串到定长字段（保证以 '\0' 结尾，超长部分截断）
static void copyField(char* dst, const char* src, size_t size) {
  if (size == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

// 入队一条"短信转发"通知：先推送所有启用通道，再发邮件
bool notifyQueueSms(const char* sender, const char* message, const char* timestamp) {
  if (WiFi.status() != WL_CONNECTED) {
    // 仍然入队：队列会等待 WiFi 恢复（最多 NOTIFY_MAX_WAIT_MS）再发送
    logCaptureLn(String("WiFi未连接，通知先入队，待恢复连接后发送"));
  }
  // 先判断是否有可用的出口（推送通道或邮件），都没有就没必要占用队列
  // 邮件还要看"短信转发"这一事件类型是否被允许，关掉后仅靠推送通道也要能工作
  bool hasChannel = false;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) { hasChannel = true; break; }
  }
  bool mailWanted = emailConfigured() && emailNotifyEnabled(EMAIL_NOTIFY_SMS);
  if (!hasChannel && !mailWanted) {
    logCaptureLn(String("没有启用的推送通道且邮件未配置（或短信转发邮件已关闭），跳过本次通知"));
    return false;
  }

  size_t msgLen = message ? strlen(message) : 0;
  if (msgLen == 0) {
    logCaptureLn(String("短信内容为空，跳过本次通知"));
    return false;
  }

  // 先占位检查队列余量，避免截断后才发现队列已满
  if (notifyCount >= NOTIFY_QUEUE_SIZE) {
    logCaptureLn(String("⚠️ 通知队列已满，本次短信未入队"));
    return false;
  }

  NotifyJob& job = notifyQueue[notifyTail];
  memset(&job, 0, sizeof(job));
  job.type = NOTIFY_JOB_SMS_PUSH;
  job.stage = NOTIFY_STAGE_PUSH;
  job.mailBit = EMAIL_NOTIFY_SMS;
  job.enqueuedAt = millis();
  copyField(job.sender, sender, NOTIFY_SENDER_SIZE);
  copyField(job.timestamp, timestamp, NOTIFY_TIMESTAMP_SIZE);
  copyField(job.message, message, NOTIFY_MESSAGE_SIZE);
  if (msgLen >= NOTIFY_MESSAGE_SIZE) {
    logCaptureLn(String("⚠️ 短信内容过长，已截断到 " + String(NOTIFY_MESSAGE_SIZE - 1) + " 字节"));
  }

  notifyTail = (notifyTail + 1) % NOTIFY_QUEUE_SIZE;
  notifyCount++;
  logCaptureF("已入队短信转发通知（队列 %d/%d）\n", notifyCount, NOTIFY_QUEUE_SIZE);
  return true;
}

// 入队一封邮件（正文已构建好）
bool notifyQueueEmail(const char* subject, const char* body, MailBodyType bodyType, uint32_t typeBit) {
  if (!emailConfigured()) {
    logCaptureLn(String("邮件配置不完整，跳过邮件入队"));
    return false;
  }
  if (!emailNotifyEnabled(typeBit)) {
    logCaptureLn(String("该类邮件通知已在配置中关闭，跳过邮件入队"));
    return false;
  }
  if (!body || strlen(body) == 0) {
    logCaptureLn(String("邮件正文为空，跳过邮件入队"));
    return false;
  }
  if (notifyCount >= NOTIFY_QUEUE_SIZE) {
    logCaptureLn(String("⚠️ 通知队列已满，本次邮件未入队"));
    return false;
  }

  NotifyJob& job = notifyQueue[notifyTail];
  memset(&job, 0, sizeof(job));
  job.type = NOTIFY_JOB_EMAIL;
  job.stage = NOTIFY_STAGE_EMAIL;   // 邮件任务直接进入发信阶段
  job.bodyType = (uint8_t)bodyType;
  job.mailBit = typeBit;
  job.enqueuedAt = millis();
  copyField(job.subject, subject, NOTIFY_SUBJECT_SIZE);
  copyField(job.body, body, NOTIFY_BODY_SIZE);
  if (strlen(body) >= NOTIFY_BODY_SIZE) {
    logCaptureLn(String("⚠️ 邮件正文过长，已截断到 " + String(NOTIFY_BODY_SIZE - 1) + " 字节"));
  }

  notifyTail = (notifyTail + 1) % NOTIFY_QUEUE_SIZE;
  notifyCount++;
  logCaptureF("已入队邮件通知（队列 %d/%d）\n", notifyCount, NOTIFY_QUEUE_SIZE);
  return true;
}

// 入队"管理员 SMS 命令"：先经模组发短信，再把执行结果发邮件
bool notifyQueueAdminSms(const char* targetPhone, const char* content, const char* cmdText) {
  if (!targetPhone || strlen(targetPhone) == 0 || !content || strlen(content) == 0) {
    logCaptureLn(String("管理员短信命令参数不完整，跳过入队"));
    return false;
  }
  if (notifyCount >= NOTIFY_QUEUE_SIZE) {
    logCaptureLn(String("⚠️ 通知队列已满，管理员短信命令未入队"));
    return false;
  }

  NotifyJob& job = notifyQueue[notifyTail];
  memset(&job, 0, sizeof(job));
  job.type = NOTIFY_JOB_SMS_COMMAND;
  job.stage = NOTIFY_STAGE_SMS;
  job.mailBit = EMAIL_NOTIFY_COMMAND;   // 结果邮件；关闭时短信照发，只是不发邮件
  job.enqueuedAt = millis();
  copyField(job.sender, targetPhone, NOTIFY_SENDER_SIZE);      // 复用 sender 存目标号码
  copyField(job.message, content, NOTIFY_MESSAGE_SIZE);        // 短信正文
  // 复用 body 存原始命令文本，结果邮件的 HTML 在发送时（含执行结果）才构建
  copyField(job.body, cmdText ? cmdText : "", NOTIFY_BODY_SIZE);

  notifyTail = (notifyTail + 1) % NOTIFY_QUEUE_SIZE;
  notifyCount++;
  logCaptureF("已入队管理员短信命令（队列 %d/%d）\n", notifyCount, NOTIFY_QUEUE_SIZE);
  return true;
}

// 入队"管理员 RESET 命令"：先发通知邮件，再重启模组与 ESP32
bool notifyQueueReboot() {
  if (notifyCount >= NOTIFY_QUEUE_SIZE) {
    logCaptureLn(String("⚠️ 通知队列已满，重启命令未入队"));
    return false;
  }

  NotifyJob& job = notifyQueue[notifyTail];
  memset(&job, 0, sizeof(job));
  job.type = NOTIFY_JOB_REBOOT;
  job.stage = NOTIFY_STAGE_EMAIL;      // 先发邮件，发完（或超时）再重启
  job.bodyType = (uint8_t)MAIL_BODY_HTML;
  job.mailBit = EMAIL_NOTIFY_REBOOT;   // 通知邮件；关闭时直接重启，不发邮件
  job.enqueuedAt = millis();
  copyField(job.subject, "重启命令已执行", NOTIFY_SUBJECT_SIZE);
  String inner = "<div class=\"note\">收到 RESET 命令，即将重启模组与 ESP32。</div>";
  copyField(job.body, buildMailHtml("🔄 重启命令已执行", inner).c_str(), NOTIFY_BODY_SIZE);

  notifyTail = (notifyTail + 1) % NOTIFY_QUEUE_SIZE;
  notifyCount++;
  logCaptureLn(String("已入队重启命令，通知邮件发出后将重启设备"));
  return true;
}

int notifyQueueCount() {
  return notifyCount;
}

static void notifyDequeue() {
  notifyHead = (notifyHead + 1) % NOTIFY_QUEUE_SIZE;
  notifyCount--;
}

// 构建短信转发邮件的主题与 HTML 正文（发送时才构建，避免占用队列内存）
static void buildSmsMail(const char* sender, const char* message, const char* timestamp,
                        String& subject, String& html) {
  String senderName, verifyCode;
  parseSmsMeta(String(message), senderName, verifyCode);
  // 兜底再格式化一次：Mailgun 等通道也会调这里，确保邮件里的时间一定是可读串
  String tsFormatted = formatSmsTimestamp(timestamp);

  // 标题（验证码优先，参考 send_notification.sh）
  if (verifyCode.length() > 0) {
    subject = "验证码: " + verifyCode + " 来自 " + (senderName.length() > 0 ? senderName : String(sender));
  } else if (senderName.length() > 0) {
    subject = "来自 " + senderName + " 的短信";
  } else {
    subject = "来自 " + String(sender) + " 的短信";
  }

  // 富文本（HTML）正文：卡片式布局，渐变标题栏 + 验证码高亮块 + 表格字段
  String inner;

  // 验证码高亮块
  if (verifyCode.length() > 0) {
    inner += "<div class=\"code\"><div class=\"code1\">验证码</div>";
    inner += "<div class=\"code2\">" + htmlEscape(verifyCode) + "</div></div>";
  }

  // 信息字段（表格）
  inner += "<div class=\"rows\"><table class=\"tb\">";
  inner += "<tr><td class=\"kn\">发件人</td><td class=\"vn\">" + htmlEscape(String(sender));
  if (senderName.length() > 0) inner += " <span class=\"hl\">(" + htmlEscape(senderName) + ")</span>";
  inner += "</td></tr>";
  inner += "<tr><td class=\"kn\">时间</td><td class=\"vn\">" + htmlEscape(tsFormatted) + "</td></tr>";
  inner += "<tr><td class=\"kn\">内容</td><td class=\"vl\">" + htmlEscape(String(message)) + "</td></tr>";
  inner += "</table></div>";

  html = buildMailHtml("📱 " + htmlEscape(subject), inner);
}

// 推进队首任务的一小步：发出一次网络请求后立刻返回
void processNotifyQueue() {
  if (notifyCount == 0) return;

  NotifyJob& job = notifyQueue[notifyHead];

  // 邮件已处理完，执行重启（此分支不返回）
  if (job.type == NOTIFY_JOB_REBOOT && job.stage == NOTIFY_STAGE_REBOOT) {
    logCaptureLn(String("正在硬重启模组..."));
    resetModule();
    logCaptureLn(String("正在重启ESP32..."));
    delay(1000);
    ESP.restart();
    return;   // 正常不会走到这里
  }

  // 整体时限：网络状况差时不再无限重试，超时丢弃，保证后续短信还能进队列
  if (millis() - job.enqueuedAt >= NOTIFY_JOB_DEADLINE_MS) {
    if (job.type == NOTIFY_JOB_REBOOT) {
      // 重启命令即使邮件发不出去也要执行重启，只是不再等邮件
      logCaptureLn(String("⚠️ 重启通知邮件超时未发出，直接执行重启"));
      job.stage = NOTIFY_STAGE_REBOOT;
    } else {
      logCaptureLn(String("⚠️ 通知超时未发出（网络状况差），已丢弃"));
      notifyDequeue();
    }
    return;
  }

  // 只有推送与邮件阶段需要网络；模组发短信、重启阶段不受 WiFi 状态影响
  bool needsWifi = (job.stage == NOTIFY_STAGE_PUSH || job.stage == NOTIFY_STAGE_EMAIL);

  // WiFi 断开时保留队列等待重连，超时后才丢弃（避免短暂断网就丢通知）
  if (needsWifi && WiFi.status() != WL_CONNECTED) {
    if (millis() - job.enqueuedAt >= NOTIFY_MAX_WAIT_MS) {
      logCaptureLn(String("⚠️ WiFi长时间未连接，丢弃 1 条待发通知"));
      notifyDequeue();
    }
    return;
  }

  // 退避等待：还没到重试时刻就直接返回，把时间让给主循环
  if (job.nextAttemptAt != 0 && (long)(millis() - job.nextAttemptAt) < 0) return;

  // ---- 管理员短信命令：先用模组发短信（不占用网络） ----
  if (job.type == NOTIFY_JOB_SMS_COMMAND && job.stage == NOTIFY_STAGE_SMS) {
    if (!modemReady) {
      // 模组未就绪时等待恢复，等太久就按失败处理并发出结果邮件
      if (millis() - job.enqueuedAt >= NOTIFY_MAX_WAIT_MS) {
        logCaptureLn(String("⚠️ 模组长时间未就绪，管理员短信按失败处理"));
        job.smsOk = 0;
        job.stage = NOTIFY_STAGE_EMAIL;
        job.attempt = 0;
        job.nextAttemptAt = 0;
      }
      return;
    }
    if (modemBusy()) return;   // 模组正在初始化/发上一条短信，下一轮再试

    logCaptureLn(String("队列执行管理员短信命令"));
    bool ok = sendSMS(job.sender, job.message);
    job.smsOk = ok ? 1 : 0;
    job.stage = NOTIFY_STAGE_EMAIL;
    job.attempt = 0;
    job.nextAttemptAt = 0;
    logCaptureLn(String(ok ? "管理员短信发送成功" : "管理员短信发送失败"));
    return;   // 结果邮件下一轮再发，避免一次调用里连续做两件慢事
  }

  NotifyStep result = STEP_RETRY;

  if (job.type == NOTIFY_JOB_SMS_PUSH && job.stage == NOTIFY_STAGE_PUSH) {
    // 跳过未启用/配置无效的通道
    while (job.channelIdx < MAX_PUSH_CHANNELS &&
           !isPushChannelValid(config.pushChannels[job.channelIdx])) {
      job.channelIdx++;
    }
    if (job.channelIdx >= MAX_PUSH_CHANNELS) {
      // 推送阶段结束，下一轮进入邮件阶段
      job.stage = NOTIFY_STAGE_EMAIL;
      job.attempt = 0;
      job.nextAttemptAt = 0;
      return;
    }

    const PushChannel& ch = config.pushChannels[job.channelIdx];
    String senderName, verifyCode;
    parseSmsMeta(String(job.message), senderName, verifyCode);
    result = sendToChannel(ch, job.sender, job.message, job.timestamp,
                           senderName.c_str(), verifyCode.c_str(), 1);

    job.attempt++;
    if (result == STEP_OK || result == STEP_FAILED || job.attempt >= NOTIFY_ATTEMPT_MAX) {
      job.channelIdx++;      // 该通道结束（成功 / 无需重试 / 重试次数用尽）
      job.attempt = 0;
      job.nextAttemptAt = 0;
    } else {
      // 退避后重试同一通道：0.5s、1s
      job.nextAttemptAt = millis() + 500UL * job.attempt;
    }
    return;
  }

  // ---- 邮件阶段 ----
  // 事件类型开关：在发送时才判定，保证保存配置后队列里积压的旧任务也立刻生效。
  // 命令/重启类只跳过邮件，动作本身（发短信、重启）照常执行。
  if (job.mailBit == 0 || !emailNotifyEnabled(job.mailBit)) {
    if (job.type == NOTIFY_JOB_REBOOT) {
      job.stage = NOTIFY_STAGE_REBOOT;
      return;
    }
    logCaptureLn(String("该类邮件通知已关闭，跳过邮件发送"));
    notifyDequeue();
    return;
  }

  if (job.type == NOTIFY_JOB_SMS_PUSH) {
    if (!emailConfigured()) {   // 未配置邮件：短信只推送即可
      notifyDequeue();
      return;
    }
    String subject, html;
    buildSmsMail(job.sender, job.message, job.timestamp, subject, html);
    result = emailAttemptOnce(subject.c_str(), html.c_str(), MAIL_BODY_HTML);
  } else if (job.type == NOTIFY_JOB_SMS_COMMAND) {
    if (!emailConfigured()) {   // 未配置邮件：短信已发出，无结果邮件可发
      notifyDequeue();
      return;
    }
    bool ok = job.smsOk;
    String subject = ok ? "短信发送成功" : "短信发送失败";
    // job.body 存原始命令文本，job.sender 为目标号码，job.message 为短信内容
    String inner = buildMailTable(buildMailRow("命令", String(job.body)) +
                                  buildMailRow("目标号码", String(job.sender)) +
                                  buildMailRow("短信内容", String(job.message)) +
                                  buildMailRow("执行结果", ok ? "成功" : "失败"));
    String body = buildMailHtml(ok ? "✅ 短信发送成功" : "❌ 短信发送失败", inner);
    result = emailAttemptOnce(subject.c_str(), body.c_str(), MAIL_BODY_HTML);
  } else {
    if (!emailConfigured()) {
      // 重启命令不依赖邮件，直接进入重启阶段
      if (job.type == NOTIFY_JOB_REBOOT) {
        job.stage = NOTIFY_STAGE_REBOOT;
        return;
      }
      notifyDequeue();
      return;
    }
    result = emailAttemptOnce(job.subject, job.body, (MailBodyType)job.bodyType);
  }

  job.attempt++;
  bool mailDone = (result == STEP_OK || result == STEP_FAILED || job.attempt >= NOTIFY_ATTEMPT_MAX);

  if (job.type == NOTIFY_JOB_REBOOT) {
    // 重启命令：邮件处理完（成功或放弃）后转入重启阶段
    if (mailDone) {
      job.stage = NOTIFY_STAGE_REBOOT;
      job.attempt = 0;
      job.nextAttemptAt = 0;
    } else {
      job.nextAttemptAt = millis() + 1000UL * job.attempt;   // 退避后重试：1s、2s
    }
    return;
  }

  if (result == STEP_OK) {
    notifyDequeue();
    return;
  }
  if (mailDone) {
    logCaptureLn(String("⚠️ 通知多次重试后仍失败，已丢弃"));
    notifyDequeue();
    return;
  }
  // 退避后重试：1s、2s
  job.nextAttemptAt = millis() + 1000UL * job.attempt;
}
