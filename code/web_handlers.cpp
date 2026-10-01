#include "web_handlers.h"
#include "web_html.h"
#include "config.h"
#include "modem.h"
#include "push.h"
#include "wifi_config.h"
#include "wifi_manager.h"

// ---- 日志环形缓冲区 ----
String logBuffer[LOG_BUF_SIZE];
int logBufIdx = 0;
int logBufCount = 0;
static String _logLine;  // 行缓冲：logCapture 写入这里，logCaptureLn 提交整行

static void _logAppend(const String& line) {
  logBuffer[logBufIdx] = line;
  logBufIdx = (logBufIdx + 1) % LOG_BUF_SIZE;
  if (logBufCount < LOG_BUF_SIZE) logBufCount++;
}

static void _logCommit() {
  if (_logLine.length() > 0) {
    _logAppend(_logLine);
    _logLine = "";
  }
}

void logCapture(const String& msg) {
  Serial.print(msg);
  _logLine += msg;
}

void logCapture(const char* msg) {
  Serial.print(msg);
  _logLine += msg;
}

void logCaptureF(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
  _logLine += buf;
  // 如果格式化字符串以 \n 结尾，则提交此行
  size_t len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n') {
    _logLine.trim();  // 去掉尾部空格和可能多余的 \n
    _logCommit();
  }
}

void logCaptureLn(const String& msg) {
  Serial.println(msg);
  _logLine += msg;
  _logCommit();
}

void logCaptureLn(const char* msg) {
  Serial.println(msg);
  _logLine += msg;
  _logCommit();
}

// 检查HTTP Basic认证
bool checkAuth() {
  if (!server.authenticate(config.webUser.c_str(), config.webPass.c_str())) {
    server.requestAuthentication(BASIC_AUTH, "SMS Forwarding", "请输入管理员账号密码");
    return false;
  }
  return true;
}

// 生成单个推送通道的表单 HTML。
// 整页模板约 45KB，若把 5 个通道拼成一个大字符串再 replace("%PUSH_CHANNELS%")，
// 需要同时持有旧页与新页（峰值内存翻倍），堆紧张时分配失败会留下 %PUSH_CHANNELS% 占位符；
// 因此这里逐通道生成，由 handleRoot() 用分块传输依次发送。
static String buildChannelForm(int i) {
  String channelsHtml = "";
  String idx = String(i);
  String enabledClass = config.pushChannels[i].enabled ? " enabled" : "";
  String checked = config.pushChannels[i].enabled ? " checked" : "";

  channelsHtml += "<div class=\"push-channel" + enabledClass + "\" id=\"channel" + idx + "\">";
  channelsHtml += "<div class=\"push-channel-header\">";
  channelsHtml += "<input type=\"checkbox\" name=\"push" + idx + "en\" id=\"push" + idx + "en\" onchange=\"toggleChannel(" + idx + ")\"" + checked + ">";
  channelsHtml += "<label for=\"push" + idx + "en\" class=\"label-inline\">启用推送通道 " + String(i + 1) + "</label>";
  channelsHtml += "</div>";
  channelsHtml += "<div class=\"push-channel-body\">";

  // 通道名称
  channelsHtml += "<div class=\"form-group\">";
  channelsHtml += "<label>通道名称</label>";
  channelsHtml += "<input type=\"text\" name=\"push" + idx + "name\" value=\"" + config.pushChannels[i].name + "\" placeholder=\"自定义名称\">";
  channelsHtml += "</div>";

  // 推送类型
  channelsHtml += "<div class=\"form-group\">";
  channelsHtml += "<label>推送方式</label>";
  channelsHtml += "<select name=\"push" + idx + "type\" id=\"push" + idx + "type\" onchange=\"updateTypeHint(" + idx + ")\">";
  channelsHtml += "<option value=\"1\"" + String(config.pushChannels[i].type == PUSH_TYPE_POST_JSON ? " selected" : "") + ">POST JSON（通用格式）</option>";
  channelsHtml += "<option value=\"2\"" + String(config.pushChannels[i].type == PUSH_TYPE_BARK ? " selected" : "") + ">Bark（iOS推送）</option>";
  channelsHtml += "<option value=\"3\"" + String(config.pushChannels[i].type == PUSH_TYPE_GET ? " selected" : "") + ">GET请求（参数在URL中）</option>";
  channelsHtml += "<option value=\"4\"" + String(config.pushChannels[i].type == PUSH_TYPE_DINGTALK ? " selected" : "") + ">钉钉机器人</option>";
  channelsHtml += "<option value=\"5\"" + String(config.pushChannels[i].type == PUSH_TYPE_PUSHPLUS ? " selected" : "") + ">PushPlus</option>";
  channelsHtml += "<option value=\"6\"" + String(config.pushChannels[i].type == PUSH_TYPE_SERVERCHAN ? " selected" : "") + ">Server酱</option>";
  channelsHtml += "<option value=\"7\"" + String(config.pushChannels[i].type == PUSH_TYPE_CUSTOM ? " selected" : "") + ">自定义模板</option>";
  channelsHtml += "<option value=\"8\"" + String(config.pushChannels[i].type == PUSH_TYPE_FEISHU ? " selected" : "") + ">飞书机器人</option>";
  channelsHtml += "<option value=\"9\"" + String(config.pushChannels[i].type == PUSH_TYPE_GOTIFY ? " selected" : "") + ">Gotify</option>";
  channelsHtml += "<option value=\"10\"" + String(config.pushChannels[i].type == PUSH_TYPE_TELEGRAM ? " selected" : "") + ">Telegram Bot</option>";
  channelsHtml += "<option value=\"11\"" + String(config.pushChannels[i].type == PUSH_TYPE_MAILGUN ? " selected" : "") + ">Mailgun（邮件 API）</option>";
  channelsHtml += "</select>";
  channelsHtml += "<div class=\"push-type-hint\" id=\"hint" + idx + "\"></div>";
  channelsHtml += "</div>";

  // URL
  channelsHtml += "<div class=\"form-group\">";
  channelsHtml += "<label>推送URL/Webhook</label>";
  channelsHtml += "<input type=\"text\" name=\"push" + idx + "url\" id=\"url" + idx + "\" value=\"" + config.pushChannels[i].url + "\" placeholder=\"http://your-server.com/api 或 webhook地址\">";
  channelsHtml += "</div>";

  // 额外参数区域（钉钉/PushPlus/Server酱/Mailgun 等需要）
  channelsHtml += "<div id=\"extra" + idx + "\" style=\"display:none;\">";
  channelsHtml += "<div class=\"form-group\">";
  channelsHtml += "<label id=\"key1label" + idx + "\">参数1</label>";
  channelsHtml += "<input type=\"text\" name=\"push" + idx + "key1\" id=\"key1" + idx + "\" value=\"" + config.pushChannels[i].key1 + "\">";
  channelsHtml += "</div>";
  channelsHtml += "<div class=\"form-group\" id=\"key2group" + idx + "\">";
  channelsHtml += "<label id=\"key2label" + idx + "\">参数2</label>";
  channelsHtml += "<input type=\"text\" name=\"push" + idx + "key2\" id=\"key2" + idx + "\" value=\"" + config.pushChannels[i].key2 + "\">";
  channelsHtml += "</div>";
  channelsHtml += "<div class=\"form-group\" id=\"key3group" + idx + "\" style=\"display:none;\">";
  channelsHtml += "<label id=\"key3label" + idx + "\">参数3</label>";
  channelsHtml += "<input type=\"text\" name=\"push" + idx + "key3\" id=\"key3" + idx + "\" value=\"" + config.pushChannels[i].key3 + "\">";
  channelsHtml += "</div>";
  channelsHtml += "<div class=\"form-group\" id=\"key4group" + idx + "\" style=\"display:none;\">";
  channelsHtml += "<label id=\"key4label" + idx + "\">参数4</label>";
  channelsHtml += "<input type=\"text\" name=\"push" + idx + "key4\" id=\"key4" + idx + "\" value=\"" + config.pushChannels[i].key4 + "\">";
  channelsHtml += "</div>";
  channelsHtml += "<div class=\"form-group\" id=\"key5group" + idx + "\" style=\"display:none;\">";
  channelsHtml += "<label id=\"key5label" + idx + "\">参数5</label>";
  channelsHtml += "<input type=\"text\" name=\"push" + idx + "key5\" id=\"key5" + idx + "\" value=\"" + config.pushChannels[i].key5 + "\">";
  channelsHtml += "</div>";
  channelsHtml += "</div>";

  // 自定义模板区域（Mailgun 下复用为正文 HTML 模板）
  channelsHtml += "<div id=\"custom" + idx + "\" style=\"display:none;\">";
  channelsHtml += "<div class=\"form-group\">";
  channelsHtml += "<label id=\"bodylabel" + idx + "\">请求体模板（使用 {sender} {message} {timestamp} {sender_name} {verify_code} {sender_display} 占位符）</label>";
  channelsHtml += "<textarea name=\"push" + idx + "body\" id=\"body" + idx + "\" rows=\"6\" style=\"width:100%;font-family:monospace;\">" + config.pushChannels[i].customBody + "</textarea>";
  channelsHtml += "</div>";
  channelsHtml += "</div>";

  channelsHtml += "</div></div>";
  return channelsHtml;
}

// 处理配置页面请求
void handleRoot() {
  if (!checkAuth()) return;
  
  String html = String(htmlPage);
  if (apMode) {
    html.replace("%IP%", WiFi.softAPIP().toString());
    html.replace("%WIFI_MODE%", "配置 AP 模式");
    html.replace("%AP_WARN_DISPLAY%", "block");
    html.replace("%WIFI_SSID%", config.wifiSsid);
  } else {
    html.replace("%IP%", WiFi.localIP().toString());
    html.replace("%WIFI_MODE%", WiFi.isConnected() ? "已连接 WiFi" : "未连接");
    html.replace("%AP_WARN_DISPLAY%", "none");
    html.replace("%WIFI_SSID%", String(WiFi.SSID()));
  }
  html.replace("%AP_SSID%", String(AP_SSID));
  // 配置 AP 密码提示：AP_PASS 长度 >= 8 为密码，否则开放网络
  html.replace("%AP_PASS%", strlen(AP_PASS) >= 8 ? String(AP_PASS) : "(开放网络，无需密码)");
  html.replace("%FREE_HEAP%", String(ESP.getFreeHeap() / 1024) + " KB");
  long uptimeSec = millis() / 1000;
  char uptimeBuf[16];
  snprintf(uptimeBuf, sizeof(uptimeBuf), "%ld:%02ld:%02ld", uptimeSec / 3600, (uptimeSec % 3600) / 60, uptimeSec % 60);
  html.replace("%UPTIME%", String(uptimeBuf));
  // 系统时间：展示用本地时间（UTC+8），同时把 UTC 时间戳交给 JS 做秒级自增
  time_t sysNow = time(nullptr);
  html.replace("%SYSTIME%", timeSynced ? formatSystemTime(sysNow) : String("未同步"));
  html.replace("%SYSTIME_EPOCH%", String(timeSynced ? (unsigned long)sysNow : 0UL));
  html.replace("%TZ_OFFSET%", String(DISPLAY_TZ_OFFSET_HOURS));
  html.replace("%WEB_USER%", config.webUser);
  html.replace("%WEB_PASS%", config.webPass);
  html.replace("%SMTP_SERVER%", config.smtpServer);
  html.replace("%SMTP_PORT%", String(config.smtpPort));
  html.replace("%SMTP_USER%", config.smtpUser);
  html.replace("%SMTP_PASS%", config.smtpPass);
  html.replace("%SMTP_SEND_TO%", config.smtpSendTo);
  html.replace("%MT_SMS_CHECKED%", emailNotifyEnabled(EMAIL_NOTIFY_SMS) ? "checked" : "");
  html.replace("%MT_STARTUP_CHECKED%", emailNotifyEnabled(EMAIL_NOTIFY_STARTUP) ? "checked" : "");
  html.replace("%MT_CONFIG_CHECKED%", emailNotifyEnabled(EMAIL_NOTIFY_CONFIG) ? "checked" : "");
  html.replace("%MT_CMD_CHECKED%", emailNotifyEnabled(EMAIL_NOTIFY_COMMAND) ? "checked" : "");
  html.replace("%MT_REBOOT_CHECKED%", emailNotifyEnabled(EMAIL_NOTIFY_REBOOT) ? "checked" : "");
  html.replace("%ADMIN_PHONE%", config.adminPhone);
  html.replace("%NUMBER_BLACK_LIST%", config.numberBlackList);

  // 概览页面的配置状态
  bool emailOk = config.smtpServer.length() > 0 && config.smtpUser.length() > 0 &&
                 config.smtpPass.length() > 0 && config.smtpSendTo.length() > 0;
  html.replace("%SMTP_CHECK%", emailOk ? "已配置" : "未配置");
  // 模组状态附带 SIM 卡状态，便于直接看出「没插卡」还是「插了卡没注册上网络」
  html.replace("%MODEM_CHECK%", modemReady ? String("已就绪") : "未就绪（SIM " + simStatusText() + "）");
  int pushCount = 0;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (config.pushChannels[i].enabled) pushCount++;
  }
  html.replace("%PUSH_COUNT%", String(pushCount));
  
  // 推送通道表单：逐通道分块发送（见 buildChannelForm 说明）
  // 整页模板约 45KB，若拼成整块再 replace("%PUSH_CHANNELS%") 需要同时持有旧页与新页，
  // 堆紧张时会分配失败，页面上就会残留未替换的占位符。
  int pushPos = html.indexOf("%PUSH_CHANNELS%");

  // 禁用缓存：固件更新后确保浏览器拉取新版页面，避免旧版 UI 与新固件不匹配
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");

  if (pushPos >= 0) {
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/html", "");
    server.sendContent(html.c_str(), (size_t)pushPos);   // 前半段：直接引用原缓冲，不额外拷贝
    for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
      server.sendContent(buildChannelForm(i));
    }
    // 后半段：跳过 "%PUSH_CHANNELS%"（15 字符）
    server.sendContent(html.c_str() + pushPos + 15, (size_t)(html.length() - pushPos - 15));
    server.sendContent("");                             // 结束分块传输
    return;
  }

  // 模板缺少占位符（不应发生）：整页发送，避免页面出现未替换的占位符
  logCaptureLn(String("页面模板缺少 PUSH_CHANNELS 占位符"));
  server.send(200, "text/html", html);
}

// 处理工具箱页面请求 — 已整合到主页，直接返回主页
void handleToolsPage() {
  handleRoot();
}

// 模组串口被占用（发短信/初始化）时快速失败：
// 多个请求同时操作 Serial1 会互相吞掉对方的响应，导致双方都卡到超时
static bool rejectIfModemBusy() {
  if (!modemBusy()) return false;
  server.send(429, "application/json", "{\"success\":false,\"message\":\"模组正忙，请稍后重试\"}");
  return true;
}

// 处理飞行模式控制请求
void handleFlightMode() {
  if (!checkAuth()) return;
  if (rejectIfModemBusy()) return;
  
  String action = server.arg("action");
  String json = "{";
  bool success = false;
  String message = "";
  
  if (action == "query") {
    // 查询当前功能模式
    logCaptureLn(String("网页端查询飞行模式: AT+CFUN?"));
    String resp = sendATCommand("AT+CFUN?", 2000);
    logCaptureLn(String("CFUN查询响应: " + resp));
    
    if (resp.indexOf("+CFUN:") >= 0) {
      success = true;
      int idx = resp.indexOf("+CFUN:");
      int mode = resp.substring(idx + 6).toInt();
      
      String modeStr;
      String statusIcon;
      if (mode == 0) {
        modeStr = "最小功能模式（关机）";
        statusIcon = "🔴";
      } else if (mode == 1) {
        modeStr = "全功能模式（正常）";
        statusIcon = "🟢";
      } else if (mode == 4) {
        modeStr = "飞行模式（射频关闭）";
        statusIcon = "✈️";
      } else {
        modeStr = "未知模式 (" + String(mode) + ")";
        statusIcon = "❓";
      }
      
      message = "<table class='info-table'>";
      message += "<tr><td>当前状态</td><td>" + statusIcon + " " + modeStr + "</td></tr>";
      message += "<tr><td>CFUN值</td><td>" + String(mode) + "</td></tr>";
      message += "</table>";
    } else {
      message = "查询失败";
    }
  }
  else if (action == "toggle") {
    // 先查询当前状态
    String resp = sendATCommand("AT+CFUN?", 2000);
    logCaptureLn(String("CFUN查询响应: " + resp));
    
    if (resp.indexOf("+CFUN:") >= 0) {
      int idx = resp.indexOf("+CFUN:");
      int currentMode = resp.substring(idx + 6).toInt();
      
      // 切换模式：1(正常) <-> 4(飞行模式)
      int newMode = (currentMode == 1) ? 4 : 1;
      String cmd = "AT+CFUN=" + String(newMode);
      
      logCaptureLn(String("切换飞行模式: " + cmd));
      String setResp = sendATCommand(cmd.c_str(), 5000);
      logCaptureLn(String("CFUN设置响应: " + setResp));
      
      if (setResp.indexOf("OK") >= 0) {
        success = true;
        if (newMode == 4) {
          message = "已开启飞行模式 ✈️<br>模组射频已关闭，无法收发短信";
        } else {
          message = "已关闭飞行模式 🟢<br>模组恢复正常工作";
        }
      } else {
        message = "切换失败: " + setResp;
      }
    } else {
      message = "无法获取当前状态";
    }
  }
  else if (action == "on") {
    // 强制开启飞行模式
    logCaptureLn(String("网页端强制开启飞行模式: AT+CFUN=4"));
    String resp = sendATCommand("AT+CFUN=4", 5000);
    if (resp.indexOf("OK") >= 0) {
      success = true;
      message = "已开启飞行模式 ✈️";
    } else {
      message = "开启失败: " + resp;
    }
  }
  else if (action == "off") {
    // 强制关闭飞行模式
    logCaptureLn(String("网页端关闭飞行模式: AT+CFUN=1"));
    String resp = sendATCommand("AT+CFUN=1", 5000);
    if (resp.indexOf("OK") >= 0) {
      success = true;
      message = "已关闭飞行模式 🟢";
    } else {
      message = "关闭失败: " + resp;
    }
  }
  else {
    message = "未知操作";
  }
  
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + message + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

// 处理AT指令测试请求
void handleATCommand() {
  if (!checkAuth()) return;
  if (rejectIfModemBusy()) return;
  
  String cmd = server.arg("cmd");
  bool success = false;
  String message = "";
  
  if (cmd.length() == 0) {
    message = "错误：指令不能为空";
  } else {
    logCaptureLn(String("网页端发送AT指令: " + cmd));
    String resp = sendATCommand(cmd.c_str(), 5000);
    logCaptureLn(String("模组响应: " + resp));
    
    if (resp.length() > 0) {
      success = true;
      message = resp;
    } else {
      message = "超时或无响应";
    }
  }
  
  String json = "{";
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

// 处理模组信息查询请求
void handleQuery() {
  if (!checkAuth()) return;
  if (rejectIfModemBusy()) return;
  
  String type = server.arg("type");
  String json = "{";
  bool success = false;
  String message = "";
  
  if (type == "ati") {
    // 固件信息查询
    String resp = sendATCommand("ATI", 2000);
    logCaptureLn(String("ATI响应: " + resp));
    
    if (resp.indexOf("OK") >= 0) {
      success = true;
      // 解析ATI响应
      String manufacturer = "未知";
      String model = "未知";
      String version = "未知";
      
      // 按行解析
      int lineStart = 0;
      int lineNum = 0;
      for (int i = 0; i < resp.length(); i++) {
        if (resp.charAt(i) == '\n' || i == resp.length() - 1) {
          String line = resp.substring(lineStart, i);
          line.trim();
          if (line.length() > 0 && line != "ATI" && line != "OK") {
            lineNum++;
            if (lineNum == 1) manufacturer = line;
            else if (lineNum == 2) model = line;
            else if (lineNum == 3) version = line;
          }
          lineStart = i + 1;
        }
      }
      
      message = "<table class='info-table'>";
      message += "<tr><td>制造商</td><td>" + manufacturer + "</td></tr>";
      message += "<tr><td>模组型号</td><td>" + model + "</td></tr>";
      message += "<tr><td>固件版本</td><td>" + version + "</td></tr>";
      message += "</table>";
    } else {
      message = "查询失败";
    }
  }
  else if (type == "signal") {
    // 信号质量查询（与 /modem?action=signal 共用 getModemSignal()，口径一致）
    SignalInfo sig;
    if (getModemSignal(sig)) {
      success = true;
      message = "<table class='info-table'>";
      if (sig.lte) {
        message += "<tr><td>信号强度 (RSRP)</td><td>" + sig.rsrpText + "</td></tr>";
        message += "<tr><td>信号质量 (RSRQ)</td><td>" + sig.rsrqText + "</td></tr>";
      } else {
        // 未取到 LTE 指标时只展示 CSQ 的 RSSI，避免把 RSSI 当 RSRP 上报
        message += "<tr><td>信号强度 (RSRP)</td><td>未知</td></tr>";
      }
      message += "<tr><td>接收电平 (RSSI)</td><td>" + (sig.rssiText.length() > 0 ? sig.rssiText : String("未知")) + "</td></tr>";
      message += "<tr><td>误码率 (BER)</td><td>" + String(sig.ber) + "</td></tr>";
      message += "<tr><td>数据来源</td><td>" + sig.source + "</td></tr>";
      message += "<tr><td>原始数据</td><td>" + sig.raw + "</td></tr>";
      message += "</table>";
    } else {
      message = "查询失败";
    }
  }
  else if (type == "siminfo") {
    // SIM卡信息查询
    success = true;
    message = "<table class='info-table'>";
    
    // SIM 卡状态（热插拔检测的最近一次结果，不占用串口）
    message += "<tr><td>SIM 卡状态</td><td>" + simStatusText() + "</td></tr>";

    // 查询IMSI
    String resp = sendATCommand("AT+CIMI", 2000);
    String imsi = "未知";
    if (resp.indexOf("OK") >= 0) {
      int start = resp.indexOf('\n');
      if (start >= 0) {
        int end = resp.indexOf('\n', start + 1);
        if (end < 0) end = resp.indexOf('\r', start + 1);
        if (end > start) {
          imsi = resp.substring(start + 1, end);
          imsi.trim();
          if (imsi == "OK" || imsi.length() < 10) imsi = "未知";
        }
      }
    }
    message += "<tr><td>IMSI</td><td>" + imsi + "</td></tr>";
    
    // 查询ICCID
    resp = sendATCommand("AT+ICCID", 2000);
    String iccid = "未知";
    if (resp.indexOf("+ICCID:") >= 0) {
      int idx = resp.indexOf("+ICCID:");
      String tmp = resp.substring(idx + 7);
      int endIdx = tmp.indexOf('\r');
      if (endIdx < 0) endIdx = tmp.indexOf('\n');
      if (endIdx > 0) iccid = tmp.substring(0, endIdx);
      iccid.trim();
    }
    message += "<tr><td>ICCID</td><td>" + iccid + "</td></tr>";
    
    // 查询本机号码 (如果SIM卡支持)
    resp = sendATCommand("AT+CNUM", 2000);
    String phoneNum = "未存储或不支持";
    if (resp.indexOf("+CNUM:") >= 0) {
      int idx = resp.indexOf(",\"");
      if (idx >= 0) {
        int endIdx = resp.indexOf("\"", idx + 2);
        if (endIdx > idx) {
          phoneNum = resp.substring(idx + 2, endIdx);
        }
      }
    }
    message += "<tr><td>本机号码</td><td>" + phoneNum + "</td></tr>";
    
    message += "</table>";
  }
  else if (type == "network") {
    // 网络状态查询
    success = true;
    message = "<table class='info-table'>";
    
    // 查询网络注册状态
    String resp = sendATCommand("AT+CEREG?", 2000);
    String regStatus = "未知";
    if (resp.indexOf("+CEREG:") >= 0) {
      int idx = resp.indexOf("+CEREG:");
      String tmp = resp.substring(idx + 7);
      int commaIdx = tmp.indexOf(',');
      if (commaIdx >= 0) {
        String stat = tmp.substring(commaIdx + 1, commaIdx + 2);
        int s = stat.toInt();
        switch(s) {
          case 0: regStatus = "未注册，未搜索"; break;
          case 1: regStatus = "已注册，本地网络"; break;
          case 2: regStatus = "未注册，正在搜索"; break;
          case 3: regStatus = "注册被拒绝"; break;
          case 4: regStatus = "未知"; break;
          case 5: regStatus = "已注册，漫游"; break;
          default: regStatus = "状态码: " + stat;
        }
      }
    }
    message += "<tr><td>网络注册</td><td>" + regStatus + "</td></tr>";
    
    // 查询运营商
    resp = sendATCommand("AT+COPS?", 2000);
    String oper = "未知";
    if (resp.indexOf("+COPS:") >= 0) {
      int idx = resp.indexOf(",\"");
      if (idx >= 0) {
        int endIdx = resp.indexOf("\"", idx + 2);
        if (endIdx > idx) {
          oper = resp.substring(idx + 2, endIdx);
        }
      }
    }
    message += "<tr><td>运营商</td><td>" + oper + "</td></tr>";
    
    // 查询PDP上下文激活状态
    resp = sendATCommand("AT+CGACT?", 2000);
    String pdpStatus = "未激活";
    if (resp.indexOf("+CGACT: 1,1") >= 0) {
      pdpStatus = "已激活";
    } else if (resp.indexOf("+CGACT:") >= 0) {
      pdpStatus = "未激活";
    }
    message += "<tr><td>数据连接</td><td>" + pdpStatus + "</td></tr>";
    
    // 查询APN
    resp = sendATCommand("AT+CGDCONT?", 2000);
    String apn = "未知";
    if (resp.indexOf("+CGDCONT:") >= 0) {
      int idx = resp.indexOf(",\"");
      if (idx >= 0) {
        idx = resp.indexOf(",\"", idx + 2);  // 跳过PDP类型
        if (idx >= 0) {
          int endIdx = resp.indexOf("\"", idx + 2);
          if (endIdx > idx) {
            apn = resp.substring(idx + 2, endIdx);
            if (apn.length() == 0) apn = "(自动)";
          }
        }
      }
    }
    message += "<tr><td>APN</td><td>" + apn + "</td></tr>";
    
    message += "</table>";
  }
  else if (type == "wifi") {
    // WiFi状态查询
    success = true;
    message = "<table class='info-table'>";
    
    // WiFi连接状态
    String wifiStatus = WiFi.isConnected() ? "已连接" : "未连接";
    message += "<tr><td>连接状态</td><td>" + wifiStatus + "</td></tr>";
    
    // SSID
    String ssid = WiFi.SSID();
    if (ssid.length() == 0) ssid = "未知";
    message += "<tr><td>当前SSID</td><td>" + ssid + "</td></tr>";
    
    // 信号强度 RSSI
    int rssi = WiFi.RSSI();
    String rssiStr = String(rssi) + " dBm";
    if (rssi >= -50) rssiStr += " (信号极好)";
    else if (rssi >= -60) rssiStr += " (信号很好)";
    else if (rssi >= -70) rssiStr += " (信号良好)";
    else if (rssi >= -80) rssiStr += " (信号一般)";
    else if (rssi >= -90) rssiStr += " (信号较弱)";
    else rssiStr += " (信号很差)";
    message += "<tr><td>信号强度 (RSSI)</td><td>" + rssiStr + "</td></tr>";
    
    // IP地址
    message += "<tr><td>IP地址</td><td>" + WiFi.localIP().toString() + "</td></tr>";
    
    // 网关
    message += "<tr><td>网关</td><td>" + WiFi.gatewayIP().toString() + "</td></tr>";
    
    // 子网掩码
    message += "<tr><td>子网掩码</td><td>" + WiFi.subnetMask().toString() + "</td></tr>";
    
    // DNS
    message += "<tr><td>DNS服务器</td><td>" + WiFi.dnsIP().toString() + "</td></tr>";
    
    // MAC地址
    message += "<tr><td>MAC地址</td><td>" + WiFi.macAddress() + "</td></tr>";
    
    // BSSID (路由器MAC)
    message += "<tr><td>路由器BSSID</td><td>" + WiFi.BSSIDstr() + "</td></tr>";
    
    // 信道
    message += "<tr><td>WiFi信道</td><td>" + String(WiFi.channel()) + "</td></tr>";
    
    message += "</table>";
  }
  else {
    message = "未知的查询类型";
  }
  
  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + message + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

// 处理发送短信请求
void handleSendSms() {
  if (!checkAuth()) return;
  
  String phone = server.arg("phone");
  String content = server.arg("content");
  
  phone.trim();
  content.trim();
  
  bool success = false;
  String resultMsg = "";
  
  if (phone.length() == 0) {
    resultMsg = "错误：请输入目标号码";
  } else if (content.length() == 0) {
    resultMsg = "错误：请输入短信内容";
  } else if (modemBusy()) {
    resultMsg = "错误：模组正忙（上一次操作尚未结束），请稍后重试";
  } else {
    logCaptureLn(String("网页端发送短信请求"));
    logCaptureLn(String("目标号码: " + phone));
    logCaptureLn(String("短信内容: " + content));
    
    success = sendSMS(phone.c_str(), content.c_str());
    resultMsg = success ? "短信发送成功！" : "短信发送失败，请检查模组状态与日志";
  }
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta http-equiv="refresh" content="3;url=/sms">
  <meta name="theme-color" content="#0f0f0f">
  <title>发送结果</title>
  <style>
    :root { color-scheme: dark; }
    body { font-family: Arial, sans-serif; text-align: center; padding-top: 100px; background: #0f0f0f; color: #ededed; }
    .result { padding: 20px; border-radius: 10px; display: inline-block; }
    .success { background: #10241a; color: #63d68e; box-shadow: 0 0 0 1px #1f4a30; }
    .error { background: #2a1416; color: #ff7b7f; box-shadow: 0 0 0 1px #4d2226; }
  </style>
</head>
<body>
  <div class="result %CLASS%">
    <h2>%ICON% %MSG%</h2>
    <p>3秒后返回发送页面...</p>
  </div>
</body>
</html>
)rawliteral";
  
  html.replace("%CLASS%", success ? "success" : "error");
  html.replace("%ICON%", success ? "✅" : "❌");
  html.replace("%MSG%", resultMsg);
  
  server.send(200, "text/html", html);
}

// 处理Ping请求
void handlePing() {
  if (!checkAuth()) return;
  if (rejectIfModemBusy()) return;
  
  logCaptureLn(String("网页端发起Ping请求"));
  
  // 清空串口缓冲区
  while (Serial1.available()) Serial1.read();
  
  // 激活PDP上下文（数据连接）
  logCaptureLn(String("激活数据连接(CGACT)..."));
  String activateResp = sendATCommand("AT+CGACT=1,1", 10000);
  logCaptureLn(String("CGACT响应: " + activateResp));
  
  // 检查激活是否成功（OK或已激活的情况）
  bool networkActivated = (activateResp.indexOf("OK") >= 0);
  if (!networkActivated) {
    logCaptureLn(String("数据连接激活失败，尝试继续执行..."));
  }
  
  // 清空串口缓冲区
  while (Serial1.available()) Serial1.read();
  delay(500);  // 等待网络稳定
  
  // 发送MPING命令，ping 8.8.8.8，超时30秒，ping 1次
  Serial1.println("AT+MPING=\"8.8.8.8\",30,1");
  
  // 等待响应
  unsigned long start = millis();
  String resp = "";
  bool gotOK = false;
  bool gotError = false;
  bool gotPingResult = false;
  String pingResultMsg = "";
  
  // 等待最多35秒（30秒超时 + 5秒余量）
  while (millis() - start < 35000) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      logCapture(String(c));  // 调试输出
      
      // 检查是否收到OK
      if (resp.indexOf("OK") >= 0 && !gotOK) {
        gotOK = true;
      }
      
      // 检查是否收到ERROR
      if (resp.indexOf("+CME ERROR") >= 0 || resp.indexOf("ERROR") >= 0) {
        gotError = true;
        pingResultMsg = "模组返回错误";
        break;
      }
      
      // 检查是否收到Ping结果URC
      // 成功格式: +MPING: 1,8.8.8.8,32,xxx,xxx
      // 失败格式: +MPING: 2 或其他
      int mpingIdx = resp.indexOf("+MPING:");
      if (mpingIdx >= 0) {
        // 找到换行符确定完整的一行
        int lineEnd = resp.indexOf('\n', mpingIdx);
        if (lineEnd >= 0) {
          String mpingLine = resp.substring(mpingIdx, lineEnd);
          mpingLine.trim();
          logCaptureLn(String("收到MPING结果: " + mpingLine));
          
          // 解析结果
          // +MPING: <result>[,<ip>,<packet_len>,<time>,<ttl>]
          int colonIdx = mpingLine.indexOf(':');
          if (colonIdx >= 0) {
            String params = mpingLine.substring(colonIdx + 1);
            params.trim();
            
            // 获取第一个参数（result）
            int commaIdx = params.indexOf(',');
            String resultStr;
            if (commaIdx >= 0) {
              resultStr = params.substring(0, commaIdx);
            } else {
              resultStr = params;
            }
            resultStr.trim();
            int result = resultStr.toInt();
            
            gotPingResult = true;
            
            // result=0或1都表示成功（不同模组可能返回不同值）
            // 如果有完整的响应参数（IP、时间等），也视为成功
            bool pingSuccess = (result == 0 || result == 1) || (params.indexOf(',') >= 0 && params.length() > 5);
            
            if (pingSuccess) {
              // 成功，解析详细信息
              // 格式: 0/1,"8.8.8.8",16,时间,TTL
              int idx1 = params.indexOf(',');
              if (idx1 >= 0) {
                String rest = params.substring(idx1 + 1);
                // 处理IP地址（可能带引号）
                String ip;
                int idx2;
                if (rest.startsWith("\"")) {
                  // 带引号的IP
                  int quoteEnd = rest.indexOf('\"', 1);
                  if (quoteEnd >= 0) {
                    ip = rest.substring(1, quoteEnd);
                    idx2 = rest.indexOf(',', quoteEnd);
                  } else {
                    idx2 = rest.indexOf(',');
                    ip = rest.substring(0, idx2);
                  }
                } else {
                  idx2 = rest.indexOf(',');
                  ip = rest.substring(0, idx2);
                }
                
                if (idx2 >= 0) {
                  rest = rest.substring(idx2 + 1);
                  int idx3 = rest.indexOf(',');  // packet_len后
                  if (idx3 >= 0) {
                    rest = rest.substring(idx3 + 1);
                    int idx4 = rest.indexOf(',');  // time后
                    String timeStr, ttlStr;
                    if (idx4 >= 0) {
                      timeStr = rest.substring(0, idx4);
                      ttlStr = rest.substring(idx4 + 1);
                    } else {
                      timeStr = rest;
                      ttlStr = "N/A";
                    }
                    timeStr.trim();
                    ttlStr.trim();
                    pingResultMsg = "目标: " + ip + ", 延迟: " + timeStr + "ms, TTL: " + ttlStr;
                  }
                }
              }
              if (pingResultMsg.length() == 0) {
                pingResultMsg = "Ping成功";
              }
            } else {
              // 失败
              pingResultMsg = "Ping超时或目标不可达 (错误码: " + String(result) + ")";
            }
            break;
          }
        }
      }
    }
    
    if (gotError || gotPingResult) break;
    server.handleClient();
    delay(1);  // 让出 CPU，避免长时间忙等触发任务看门狗
  }
  
  logCaptureLn(String("\nPing操作完成"));
  
  // 关闭数据连接以节省流量
  logCaptureLn(String("关闭PDP上下文(CGACT=0)..."));
  String deactivateResp = sendATCommand("AT+CGACT=0,1", 5000);
  logCaptureLn(String("CGACT关闭响应: " + deactivateResp));
  
  // 构建JSON响应
  String json = "{";
  if (gotPingResult && pingResultMsg.indexOf("延迟") >= 0) {
    json += "\"success\":true,";
    json += "\"message\":\"" + pingResultMsg + "\"";
  } else if (gotError) {
    json += "\"success\":false,";
    json += "\"message\":\"" + pingResultMsg + "\"";
  } else if (gotPingResult) {
    json += "\"success\":false,";
    json += "\"message\":\"" + pingResultMsg + "\"";
  } else {
    json += "\"success\":false,";
    json += "\"message\":\"操作超时，未收到Ping结果\"";
  }
  json += "}";
  
  server.send(200, "application/json", json);
}

// 处理保存配置请求
void handleSave() {
  if (!checkAuth()) return;

  // 账号管理表单：只在字段存在时更新
  if (server.hasArg("webUser")) {
    String newWebUser = server.arg("webUser");
    if (newWebUser.length() == 0) newWebUser = DEFAULT_WEB_USER;
    config.webUser = newWebUser;
  }
  if (server.hasArg("webPass")) {
    String newWebPass = server.arg("webPass");
    if (newWebPass.length() == 0) newWebPass = DEFAULT_WEB_PASS;
    config.webPass = newWebPass;
  }

  // WiFi 配置：只在字段存在时更新（通过「WiFi 设置」面板的保存也会走这里）
  if (server.hasArg("wifiSsid")) {
    config.wifiSsid = server.arg("wifiSsid");
  }
  if (server.hasArg("wifiPass")) {
    config.wifiPass = server.arg("wifiPass");
  }

  // 邮件通知表单：只在字段存在时更新
  if (server.hasArg("smtpServer")) {
    config.smtpServer = server.arg("smtpServer");
  }
  if (server.hasArg("smtpPort")) {
    config.smtpPort = server.arg("smtpPort").toInt();
    if (config.smtpPort == 0) config.smtpPort = 465;
  }
  if (server.hasArg("smtpUser")) {
    config.smtpUser = server.arg("smtpUser");
  }
  if (server.hasArg("smtpPass")) {
    config.smtpPass = server.arg("smtpPass");
  }
  if (server.hasArg("smtpSendTo")) {
    config.smtpSendTo = server.arg("smtpSendTo");
  }
  // 邮件通知类型（位掩码）：只有「邮件通知」页会提交 smtpServer，据此判断是否需要更新；
  // 复选框未勾选时不会出现在请求里，对应位即为 0（与推送通道 enabled 的处理方式一致）。
  if (server.hasArg("smtpServer")) {
    uint32_t types = 0;
    if (server.hasArg("mtSms"))     types |= EMAIL_NOTIFY_SMS;
    if (server.hasArg("mtStartup")) types |= EMAIL_NOTIFY_STARTUP;
    if (server.hasArg("mtConfig"))  types |= EMAIL_NOTIFY_CONFIG;
    if (server.hasArg("mtCmd"))     types |= EMAIL_NOTIFY_COMMAND;
    if (server.hasArg("mtReboot"))  types |= EMAIL_NOTIFY_REBOOT;
    config.emailNotifyTypes = types;
  }

  // 管理员 & 黑名单表单：只在字段存在时更新
  if (server.hasArg("adminPhone")) {
    config.adminPhone = server.arg("adminPhone");
  }
  if (server.hasArg("numberBlackList")) {
    config.numberBlackList = server.arg("numberBlackList");
  }

  // 推送通道配置：只在对应通道的字段存在时更新
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    String idx = String(i);
    String enKey = "push" + idx + "en";
    String typeKey = "push" + idx + "type";
    String urlKey = "push" + idx + "url";
    String nameKey = "push" + idx + "name";
    String k1Key = "push" + idx + "key1";
    String k2Key = "push" + idx + "key2";
    String k3Key = "push" + idx + "key3";
    String k4Key = "push" + idx + "key4";
    String k5Key = "push" + idx + "key5";
    String bodyKey = "push" + idx + "body";
    // 只要该通道的任一字段存在，就更新整个通道
    if (server.hasArg(enKey) || server.hasArg(typeKey) || server.hasArg(urlKey) ||
        server.hasArg(nameKey) || server.hasArg(k1Key) || server.hasArg(k2Key) ||
        server.hasArg(k3Key) || server.hasArg(k4Key) || server.hasArg(k5Key) ||
        server.hasArg(bodyKey)) {
      config.pushChannels[i].enabled = server.arg(enKey) == "on";
      config.pushChannels[i].type = (PushType)server.arg(typeKey).toInt();
      config.pushChannels[i].url = server.arg(urlKey);
      config.pushChannels[i].name = server.arg(nameKey);
      config.pushChannels[i].key1 = server.arg(k1Key);
      config.pushChannels[i].key2 = server.arg(k2Key);
      config.pushChannels[i].key3 = server.arg(k3Key);
      config.pushChannels[i].key4 = server.arg(k4Key);
      config.pushChannels[i].key5 = server.arg(k5Key);
      config.pushChannels[i].customBody = server.arg(bodyKey);
      if (config.pushChannels[i].name.length() == 0) {
        config.pushChannels[i].name = "通道" + String(i + 1);
      }
    }
  }
  
  saveConfig();
  configValid = isConfigValid();
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta http-equiv="refresh" content="3;url=/">
  <meta name="theme-color" content="#0f0f0f">
  <title>保存成功</title>
  <style>
    :root { color-scheme: dark; }
    body { font-family: Arial, sans-serif; text-align: center; padding-top: 100px; background: #0f0f0f; color: #ededed; }
    .success { background: #10241a; color: #63d68e; box-shadow: 0 0 0 1px #1f4a30; padding: 20px; border-radius: 10px; display: inline-block; }
  </style>
</head>
<body>
  <div class="success">
    <h2>✅ 配置保存成功！</h2>
    <p>3秒后返回配置页面...</p>
    <p>如果修改了账号密码，请使用新的账号密码登录</p>
  </div>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
  
  // 如果配置有效，发送启动通知（走异步队列，避免保存配置时卡住 HTTP 响应）
  if (configValid) {
    logCaptureLn(String("配置有效，发送启动通知..."));
    String subject = "短信转发器配置已更新";
    String inner = buildMailTable(buildMailRow("设备地址", getDeviceUrl()) +
                                  buildMailRow("IP地址", WiFi.localIP().toString()));
    notifyQueueEmail(subject.c_str(), buildMailHtml("⚙️ 配置已更新", inner).c_str(), MAIL_BODY_HTML);
  }
}

// 处理日志查询请求 — 返回环形缓冲区中的日志行
void handleLog() {
  if (!checkAuth()) return;

  String json = "[";
  int total = logBufCount;
  int start = total < LOG_BUF_SIZE ? 0 : logBufIdx;
  for (int i = 0; i < total; i++) {
    int pos = (start + i) % LOG_BUF_SIZE;
    if (i > 0) json += ",";
    json += "\"" + jsonEscape(logBuffer[pos]) + "\"";
  }
  json += "]";
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "application/json", json);
}

// 模组控制命令
void handleModem() {
  if (!checkAuth()) return;
  if (rejectIfModemBusy()) return;

  // 防止重入：modemInit() 内部会调 server.handleClient()，
  // 若浏览器超时重试会导致嵌套调用，最终拖垮 WiFi
  static bool busy = false;
  if (busy) {
    server.send(429, "application/json", "{\"success\":false,\"message\":\"模组正忙，请稍后重试\"}");
    return;
  }
  busy = true;

  String action = server.arg("action");
  String json = "{";
  bool success = false;
  String message = "";

  if (action == "restart") {
    // AT 软重启 — 先响应浏览器再初始化，防止浏览器超时重试
    logCaptureLn(String("网页端请求软重启模组..."));
    server.send(200, "application/json", "{\"success\":true,\"message\":\"正在软重启模组，请等待约 15 秒后刷新页面\"}");
    String resp = sendATCommand("AT+CFUN=1,1", 15000);
    success = (resp.indexOf("OK") >= 0);
    message = success ? "模组软重启成功" : "软重启失败";
    logCaptureLn(String(message + ": " + resp));
    if (success) modemInit();
    busy = false;
    return;
  }
  else if (action == "hardreset") {
    // EN 引脚断电重启（内部已调用 modemInit()）
    logCaptureLn(String("网页端请求硬重启模组..."));
    server.send(200, "application/json", "{\"success\":true,\"message\":\"正在硬重启模组，请等待约 15 秒后刷新页面\"}");
    busy = false;  // 先释放，否则 resetModule() 内部初始化失败后 busy 永远为真
    resetModule();
    return;
  }
  else if (action == "signal") {
    logCaptureLn(String("网页端查询信号"));
    // 与 /query?type=signal 共用同一实现，避免两个入口给出不同的数值
    SignalInfo sig;
    if (getModemSignal(sig)) {
      String rsrp = sig.lte ? sig.rsrpText : String("未知");
      String rssi = sig.rssiText.length() > 0 ? sig.rssiText : String("未知");
      message = "RSRP: " + rsrp + ", RSSI: " + rssi + ", BER: " + String(sig.ber) +
                ", 来源: " + sig.source;
      success = true;
    } else {
      message = "无法获取信号（模组无响应）";
    }
  }
  else if (action == "operator") {
    logCaptureLn(String("网页端查询运营商: AT+COPS?"));
    String resp = sendATCommand("AT+COPS?", 5000);
    int copsIdx = resp.indexOf("+COPS:");
    if (copsIdx >= 0) {
      String copsLine = resp.substring(copsIdx);
      copsLine = copsLine.substring(0, copsLine.indexOf('\n'));
      copsLine.trim();
      int q1 = copsLine.indexOf('"');
      int q2 = copsLine.indexOf('"', q1 + 1);
      if (q1 >= 0 && q2 >= 0) {
        message = copsLine.substring(q1 + 1, q2);
        success = true;
      } else {
        message = copsLine;
        success = true;
      }
    }
    if (!success) message = "无法获取运营商: " + resp;
  }
  else if (action == "modemtime") {
    // 读取 4G 网络时间并写入系统时间（网页「模组诊断」页面的同步按钮）
    logCaptureLn(String("网页端请求同步模组时间"));
    ModemTimeInfo info;
    String before = timeSynced ? formatSystemTime(time(nullptr)) : String("未同步");
    if (syncTimeFromModem(info)) {
      success = true;
      message = "模组时间(本地): " + info.localText + "<br>" +
                "模组原始值: " + info.raw + "（" + info.source + "，时分秒为 UTC）<br>" +
                "同步前系统时间: " + before + "<br>" +
                "已设置为系统时间: " + formatSystemTime(time(nullptr));
    } else {
      message = "未取到有效的模组时间（模组无响应或尚未获取网络时间），当前系统时间: " + before;
    }
  }
  else if (action == "imei") {
    logCaptureLn(String("网页端查询IMEI: AT+GSN"));
    String resp = sendATCommand("AT+GSN", 3000);
    resp.trim();
    int okIdx = resp.lastIndexOf("OK");
    if (okIdx > 0) resp = resp.substring(0, okIdx);
    int gsnIdx = resp.indexOf("AT+GSN");
    if (gsnIdx >= 0) resp = resp.substring(gsnIdx + 6);
    resp.trim();
    if (resp.length() > 0) {
      message = resp;
      success = true;
    } else {
      message = "无法获取 IMEI";
    }
  }
  else {
    message = "未知操作: " + action;
  }

  json += "\"success\":" + String(success ? "true" : "false") + ",";
  json += "\"message\":\"" + jsonEscape(message) + "\"";
  json += "}";
  busy = false;
  server.send(200, "application/json", json);
}

// WiFi 管理：扫描 / 保存并连接 / 重启
void handleWifi() {
  if (!checkAuth()) return;

  static bool busy = false;
  if (busy) {
    server.send(429, "application/json", "{\"success\":false,\"message\":\"WiFi正忙，请稍后重试\"}");
    return;
  }
  busy = true;

  String action = server.arg("action");
  if (action == "scan") {
    // 扫描附近 WiFi（同步扫描，约 2~4 秒）
    logCaptureLn(String("网页端请求扫描WiFi..."));
    int n = WiFi.scanNetworks();
    logCaptureLn(String("扫描到 ") + String(n) + " 个网络");
    String json = "{\"success\":true,\"count\":" + String(n) + ",\"networks\":[";
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      String ssid = WiFi.SSID(i);
      json += "{\"ssid\":\"" + jsonEscape(ssid) + "\"";
      json += ",\"rssi\":" + String(WiFi.RSSI(i));
      json += ",\"enc\":" + String((int)WiFi.encryptionType(i));
      json += ",\"chan\":" + String(WiFi.channel(i)) + "}";
    }
    json += "]}";
    server.send(200, "application/json", json);
    busy = false;
  }
  else if (action == "save") {
    // 保存 WiFi 凭据并尝试连接；成功则关闭 AP，失败保持/回退 AP 模式
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    if (ssid.length() == 0) {
      server.send(200, "application/json", "{\"success\":false,\"message\":\"WiFi 名称 (SSID) 不能为空\"}");
      busy = false;
      return;
    }
    config.wifiSsid = ssid;
    config.wifiPass = pass;
    saveConfig();
    logCaptureLn(String("网页端保存WiFi配置: ") + ssid);
    // 先响应浏览器（避免在长连接过程中浏览器超时），再做实际连接
    server.send(200, "application/json", "{\"success\":true,\"message\":\"已保存，正在连接 WiFi: " + jsonEscape(ssid) + "...\"}");
    delay(300);  // 等待响应真正发出，再断开 AP 接口
    bool ok = connectWiFiAndSettle(ssid, pass, 20000);
    if (ok) {
      logCaptureLn(String("网页端WiFi连接成功"));
    } else {
      logCaptureLn(String("网页端WiFi连接失败，已恢复AP模式"));
    }
    busy = false;
  }
  else if (action == "restart") {
    logCaptureLn(String("网页端请求重启WiFi..."));
    server.send(200, "application/json", "{\"success\":true,\"message\":\"WiFi 正在重启，请等待约 5 秒后刷新页面\"}");
    delay(300);
    connectWiFiAndSettle(config.wifiSsid, config.wifiPass, 15000);
    busy = false;
  } else {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"未知操作\"}");
    busy = false;
  }
}

// 系统控制命令
void handleSystem() {
  if (!checkAuth()) return;

  String action = server.arg("action");
  if (action == "restart") {
    // 整机重启 — 先响应浏览器再重启，防止浏览器超时重试
    logCaptureLn(String("网页端请求重启系统..."));
    server.send(200, "application/json", "{\"success\":true,\"message\":\"系统正在重启，请等待约 30 秒后刷新页面\"}");
    delay(500);  // 确保响应完整发出
    logCaptureLn(String("系统重启中..."));
    ESP.restart();
  } else {
    server.send(200, "application/json", "{\"success\":false,\"message\":\"未知操作\"}");
  }
}
