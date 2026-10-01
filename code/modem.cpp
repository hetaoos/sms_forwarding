#include "modem.h"
#include "web_handlers.h"
#include <sys/time.h>

// 初始化重试上限（所有等待都必须有上界，避免模组异常时永久卡死/反复重启）
#define MODEM_AT_RETRY            8      // 开机握手重试次数
#define MODEM_AT_RETRY_BG         3      // 后台自动恢复时的握手重试次数
#define MODEM_CMD_RETRY           3      // 单条 AT 配置命令重试次数
#define MODEM_CMD_RETRY_BG        1      // 后台恢复时单条命令只试一次，尽量少阻塞主循环
#define MODEM_CEREG_RETRY         20     // 开机等待网络注册的次数（每次约 2 秒）
#define MODEM_CEREG_RETRY_BG      3      // 后台恢复时等待网络注册的次数
#define MODEM_REINIT_INTERVAL_MS  90000UL  // 模组未就绪时的自动重试间隔
#define MODEM_REINIT_MAX_INTERVAL_MS 300000UL // 连续失败后的最长重试间隔（5 分钟）
#define SMS_PROMPT_TIMEOUT_MS     3000UL  // 等待 ">" 提示符的上限
#define SMS_RESULT_TIMEOUT_MS     12000UL // 等待 +CMGS/OK 的上限

static bool trySyncModemTime();   // 定义在「模组时钟」小节：模组就绪后尝试同步一次网络时间
static bool simKnownAbsent();     // 定义在「SIM 卡热插拔检测」小节：确实没插卡时为 true

// 串口占用标记：短信发送/模组初始化期间独占 Serial1。
// HTTP 处理器据此快速失败，避免嵌套调用把对方的提示符和结果吞掉而双双卡到超时。
static bool smsInProgress = false;
static bool modemInitInProgress = false;

bool modemBusy() {
  return smsInProgress || modemInitInProgress;
}

// 发送AT命令并获取响应
String sendATCommand(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeout) {
    if (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0) {
        // 读取剩余数据（最多 50ms）
        unsigned long t = millis();
        while (millis() - t < 50) {
          if (Serial1.available()) resp += (char)Serial1.read();
          server.handleClient();
          delay(1);  // 让出 CPU，避免长时间忙等触发任务看门狗
        }
        return resp;
      }
    }
    server.handleClient();
    delay(1);  // 让出 CPU，避免长时间忙等触发任务看门狗
  }
  return resp;
}

// 等待指定毫秒，期间持续处理 HTTP 请求（避免断电重启时网页长时间无响应）
static void responsiveDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    server.handleClient();
    delay(1);
  }
}

// 新增"模组断电重启"函数
void modemPowerCycle() {
  pinMode(MODEM_EN_PIN, OUTPUT);

  logCaptureLn(String("EN 拉低：关闭模组"));
  digitalWrite(MODEM_EN_PIN, LOW);
  responsiveDelay(1200);  // 关机时间给够

  logCaptureLn(String("EN 拉高：开启模组"));
  digitalWrite(MODEM_EN_PIN, HIGH);
  responsiveDelay(6000);  // 等模组完全启动再发AT（关键）
}

// 重启模组（EN引脚断电重启 + 重新初始化）
void resetModule() {
  logCaptureLn(String("正在硬重启模组（EN 断电重启）..."));
  modemPowerCycle();
  if (!modemInit()) {
    logCaptureLn(String("⚠️ 硬重启后模组仍不可用，稍后将自动重试初始化"));
  }
}

// 与模组建立 AT 握手，最多尝试 maxAttempts 次；中途做一次断电重启尝试唤醒
// 返回 false 表示模组始终无响应（此时绝不能无限重试）
static bool modemWaitATReady(int maxAttempts) {
  for (int i = 0; i < maxAttempts; i++) {
    if (sendATandWaitOK("AT", 1000)) return true;
    logCaptureLn(String("AT未响应，重试 " + String(i + 1) + "/" + String(maxAttempts)));
    blink_short();
    // 连续无响应，做一次断电重启再试
    if (maxAttempts >= 2 && i + 1 == maxAttempts / 2) {
      logCaptureLn(String("AT持续无响应，对模组做一次断电重启"));
      modemPowerCycle();
    }
  }
  return false;
}

// 发送配置类 AT 命令并有限次重试
static bool sendATWithRetry(const char* cmd, unsigned long timeout, int maxAttempts) {
  for (int i = 0; i < maxAttempts; i++) {
    if (sendATandWaitOK(cmd, timeout)) return true;
    logCaptureLn(String(String(cmd) + " 失败，重试 " + String(i + 1) + "/" + String(maxAttempts)));
    blink_short();
  }
  return false;
}

// 解析 ATI 响应，提取制造商/型号/固件版本。
// 不依赖固定行序：先挑出以 "Revision"/"Rev:" 开头的版本行，剩余行按数量判断制造商与型号。
// 覆盖 ML307 常见格式：
//   单行型号：         "ML307C-DC-CN-MBRH0S00"                → 型号
//   制造商+型号：       "Mobiletek" / "ML307C-..."             → 制造商 / 型号
//   含版本行：         "... / ML307C-... / Revision: ML307C-..." → 制造商 / 型号 / 版本
void parseATI(const String& resp, String& outManufacturer, String& outModel, String& outVersion) {
  outManufacturer = "未知";
  outModel = "未知";
  outVersion = "未知";

  // 逐行收集有效内容（排除回显的 ATI 与结尾 OK）
  String lines[8];
  int nLines = 0;
  int lineStart = 0;
  for (int i = 0; i < resp.length() && nLines < 8; i++) {
    if (resp.charAt(i) == '\n' || i == resp.length() - 1) {
      String line = resp.substring(lineStart, i);
      line.trim();
      lineStart = i + 1;
      if (line.length() > 0 && line != "ATI" && line != "OK") {
        lines[nLines++] = line;
      }
    }
  }

  // 先挑出版本行（以 Revision / Rev: 开头，大小写不敏感）
  int versionIdx = -1;
  for (int i = 0; i < nLines; i++) {
    String low = lines[i];
    low.toLowerCase();
    if (low.startsWith("revision") || low.startsWith("rev:")) {
      String v = lines[i];
      int colon = v.indexOf(':');
      if (colon >= 0) v = v.substring(colon + 1);
      v.trim();
      outVersion = v.length() > 0 ? v : lines[i];
      versionIdx = i;
      break;
    }
  }

  // 去掉版本行后，按剩余行数判断制造商/型号
  String rest[8];
  int nRest = 0;
  for (int i = 0; i < nLines; i++) {
    if (i != versionIdx) rest[nRest++] = lines[i];
  }

  if (nRest == 1) {
    outModel = rest[0];
  } else if (nRest == 2) {
    outManufacturer = rest[0];
    outModel = rest[1];
  } else if (nRest >= 3) {
    outManufacturer = rest[0];
    // 若第 3 行以第 2 行开头（第 2 行是型号族、第 3 行是完整 Part Number，
    // 如 "ML307C" / "ML307C-DC-CN-MBRH0S00"），型号取完整串；
    // 否则按常规把第 2 行当型号、第 3 行当版本。
    if (rest[2].startsWith(rest[1])) {
      outModel = rest[2];
    } else {
      outModel = rest[1];
      if (outVersion == "未知") outVersion = rest[2];
    }
  }
}

// 模组 AT 初始化流程（setup 中调用，resetModule 后也调用）
// background=true 表示后台自动恢复，使用更少的重试次数，尽量少阻塞主循环
// 返回 true 表示模组可用（已注册网络且短信参数配置成功）
bool modemInit(bool background) {
  // 防止重入：modemInit() 内部会调 server.handleClient()，
  // 浏览器超时重试或自动恢复都可能造成嵌套调用
  if (modemInitInProgress) {
    logCaptureLn(String("模组初始化正在进行，忽略本次调用"));
    return modemReady;
  }
  modemInitInProgress = true;
  lastModemInitAttempt = millis();
  modemReady = false;

  int atRetry = background ? MODEM_AT_RETRY_BG : MODEM_AT_RETRY;
  int cmdRetry = background ? MODEM_CMD_RETRY_BG : MODEM_CMD_RETRY;
  int ceregRetry = background ? MODEM_CEREG_RETRY_BG : MODEM_CEREG_RETRY;

  // 清掉上电噪声/残留
  while (Serial1.available()) Serial1.read();

  if (!modemWaitATReady(atRetry)) {
    logCaptureLn(String("⚠️ 模组AT无响应（未上电/串口异常），放弃本次初始化，稍后自动重试"));
    modemInitInProgress = false;
    return false;
  }
  logCaptureLn(String("模组AT响应正常"));

  //判断型号，做一些特定操作
  bool need_set_CGACT = true;
  String resp = sendATCommand("ATI", 2000);
  logCaptureLn(String("ATI响应: " + resp));
  if (resp.indexOf("OK") >= 0) {
    // 解析ATI响应（按关键字识别，不依赖固定行序）
    String manufacturer = "未知";
    String model = "未知";
    String version = "未知";
    parseATI(resp, manufacturer, model, version);
    // 写入全局变量，供状态查询/启动邮件使用
    modemManufacturer = manufacturer;
    modemModel = model;
    modemVersion = version;
    //这个模组这条命令有bug
    if(model == "ML307Y") need_set_CGACT = false;
  }

  if(need_set_CGACT) {
    // 仅为省流量，失败不阻断短信功能，交由后续自动重试
    if (sendATWithRetry("AT+CGACT=0,1", 5000, cmdRetry)) {
      logCaptureLn(String("已禁用数据连接(AT+CGACT=0,1)，防止流量消耗"));
    } else {
      logCaptureLn(String("⚠️ 设置CGACT失败，跳过（可能导致流量消耗）"));
    }
  } else {
    logCaptureLn(String("该型号无法配置(AT+CGACT=0,1)，跳过该命令，会不会消耗流量？自求多福"));
  }

  bool cnmiOk = sendATWithRetry("AT+CNMI=2,2,0,0,0", 1000, cmdRetry);
  if (cnmiOk) {
    logCaptureLn(String("CNMI参数设置完成"));
  } else {
    logCaptureLn(String("⚠️ 设置CNMI失败，短信上报可能不可用"));
  }

  bool cmgfOk = sendATWithRetry("AT+CMGF=0", 1000, cmdRetry);
  if (cmgfOk) {
    logCaptureLn(String("PDU模式设置完成"));
  } else {
    logCaptureLn(String("⚠️ 设置PDU模式失败，短信收发不可用"));
  }

  int ceregCount = 0;
  while (!waitCEREG() && ceregCount < ceregRetry) {
    logCaptureLn(String("等待网络注册... " + String(ceregCount + 1) + "/" + String(ceregRetry)));
    ceregCount++;
    blink_short();
  }

  bool registered = (ceregCount < ceregRetry);
  if (registered) {
    logCaptureLn(String("网络已注册"));
  } else {
    logCaptureLn(String("⚠️ 网络注册超时（无SIM卡或信号差），模组功能不可用"));
  }

  modemReady = registered && cnmiOk && cmgfOk;
  if (modemReady) {
    // 模组就绪后立刻用基站网络时间校准系统时间（NTP 走 WiFi，这条路径无外网时也能校准）。
    // 基站时间下发可能有延迟，此刻失败则由 loop() 的 modemTimeSyncTick() 定时重试。
    trySyncModemTime();
  } else {
    logCaptureLn(String("⚠️ 模组初始化未完成，系统将定时自动重试（不影响网页访问）"));
  }
  modemInitInProgress = false;
  return modemReady;
}

// 模组未就绪时，在主循环中定时重试初始化（有上界，不会卡死）
// 连续失败时按 2 倍退避拉长间隔，最长 MODEM_REINIT_MAX_INTERVAL_MS
void modemAutoRecover() {
  static unsigned long reinitInterval = MODEM_REINIT_INTERVAL_MS;

  if (modemReady) {
    reinitInterval = MODEM_REINIT_INTERVAL_MS;
    return;
  }
  // SIM 卡确实不在时不必反复重试：重试过程里含断电重启，没卡时只会白折腾模组。
  // 插卡事件由 simHotplugTick() 捕获并立刻触发一次初始化。
  if (simKnownAbsent()) {
    lastModemInitAttempt = millis();
    return;
  }
  if (millis() - lastModemInitAttempt < reinitInterval) return;

  logCaptureLn(String("模组未就绪，尝试重新初始化..."));
  bool ok = modemInit(true);
  if (ok) {
    reinitInterval = MODEM_REINIT_INTERVAL_MS;
    ledOff();   // 后台重试成功，同样熄灭指示灯
  } else {
    reinitInterval = (reinitInterval * 2 > MODEM_REINIT_MAX_INTERVAL_MS)
                         ? MODEM_REINIT_MAX_INTERVAL_MS
                         : reinitInterval * 2;
    logCaptureLn(String("模组恢复失败，" + String(reinitInterval / 1000) + " 秒后再次尝试"));
  }
}

void blink_short(unsigned long gap_time) {
  digitalWrite(LED_BUILTIN, LOW);
  responsiveDelay(50);
  digitalWrite(LED_BUILTIN, HIGH);
  responsiveDelay(gap_time);  // 重试间隙同样保持 HTTP 响应
}

// ---- 蓝色 LED 指示（低电平点亮）----
// 初始化完成后保持熄灭；收到短信时按「亮-灭」交替闪烁若干次，由 ledTick() 在主循环里推进，
// 避免在 URC 回调中用 delay() 卡住主循环。
static int ledPhasesLeft = 0;             // 剩余的半周期数（亮、灭各算一段，最后一段是熄灭）
static unsigned long ledPhaseEnd = 0;     // 当前半周期的结束时刻（millis）
static unsigned long ledPhaseMs = SMS_LED_BLINK_MS;

void ledOff() {
  ledPhasesLeft = 0;
  ledPhaseEnd = 0;
  digitalWrite(LED_BUILTIN, HIGH);
}

// 触发 times 次闪烁：每段持续 duration 毫秒，亮灭交替，结束保持熄灭
void ledBlink(unsigned int times, unsigned long duration) {
  if (times == 0) {
    ledOff();
    return;
  }
  ledPhaseMs = duration;
  ledPhasesLeft = times * 2;   // 亮、灭 … 亮、灭，末尾的熄灭段负责收尾
  ledPhaseEnd = millis() + duration;
  digitalWrite(LED_BUILTIN, LOW);
}

void ledTick() {
  if (ledPhasesLeft == 0) return;
  if ((long)(millis() - ledPhaseEnd) < 0) return;

  ledPhasesLeft--;
  if (ledPhasesLeft == 0) {
    ledOff();
    return;
  }
  // 剩余段数为奇数 → 熄灭段；为偶数 → 点亮段
  bool on = (ledPhasesLeft % 2 == 0);
  digitalWrite(LED_BUILTIN, on ? LOW : HIGH);
  ledPhaseEnd = millis() + ledPhaseMs;
}

// ---- AP 模式：蓝灯慢闪（约 1 秒周期）表示「等待配置」 ----
// 与短信闪烁/模组不可用共用同一物理引脚，但用独立状态机，避免互相干扰。
static bool apLedOn = false;
static unsigned long apLedNext = 0;

void ledApEnter() {
  // 进入 AP 模式：先清零短信闪烁状态，再熄灭，等第一个周期再亮，避免一进 AP 就常亮
  ledPhasesLeft = 0;
  ledPhaseEnd = 0;
  apLedOn = false;
  apLedNext = 0;
  digitalWrite(LED_BUILTIN, HIGH);  // 先熄灭
}

void ledApTick() {
  if ((long)(millis() - apLedNext) < 0) return;
  apLedOn = !apLedOn;
  digitalWrite(LED_BUILTIN, apLedOn ? LOW : HIGH);  // LOW=亮
  apLedNext = millis() + 500;  // 亮 500ms + 灭 500ms ≈ 1 秒周期
}

void ledRestoreNormal() {
  // 离开 AP 模式：恢复常态。模组/SIM 不可用时保持常亮（与平时一致），否则熄灭。
  ledPhasesLeft = 0;
  ledPhaseEnd = 0;
  if (!modemReady || !isSimInserted()) {
    digitalWrite(LED_BUILTIN, LOW);
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
  }
}

bool sendATandWaitOK(const char* cmd, unsigned long timeout) {
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmd);
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeout) {
    if (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("OK") >= 0) return true;
      if (resp.indexOf("ERROR") >= 0) return false;
    }
    server.handleClient();
    delay(1);  // 让出 CPU，避免长时间忙等触发任务看门狗
  }
  return false;
}

// 检测网络注册状态（LTE/4G）
// CEREG状态: 1=已注册本地, 5=已注册漫游
bool waitCEREG() {
  Serial1.println("AT+CEREG?");
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < 2000) {
    if (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (resp.indexOf("+CEREG:") >= 0) {
        if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) return true;
        if (resp.indexOf(",0") >= 0 || resp.indexOf(",2") >= 0 || 
            resp.indexOf(",3") >= 0 || resp.indexOf(",4") >= 0) return false;
      }
    }
    server.handleClient();
    delay(1);  // 让出 CPU，避免长时间忙等触发任务看门狗
  }
  return false;
}

// 截取 URC 行的参数部分：从 "+CMD:" 之后到换行符为止
static String extractParams(const String& resp, const String& prefix) {
  int idx = resp.indexOf(prefix);
  if (idx < 0) return "";
  String params = resp.substring(idx + prefix.length());
  int end = params.indexOf('\r');
  if (end < 0) end = params.indexOf('\n');
  if (end > 0) params = params.substring(0, end);
  params.trim();
  return params;
}

// 按逗号切分参数，返回实际取到的段数
static int splitParams(const String& params, String* out, int max) {
  int count = 0;
  int start = 0;
  for (int i = 0; i <= params.length() && count < max; i++) {
    if (i == params.length() || params.charAt(i) == ',') {
      out[count] = params.substring(start, i);
      out[count].trim();
      count++;
      start = i + 1;
    }
  }
  return count;
}

// LTE RSRP 评级（dBm）
static String rateRsrp(int dbm) {
  if (dbm >= -80) return "极好";
  if (dbm >= -90) return "良好";
  if (dbm >= -100) return "一般";
  if (dbm >= -110) return "较弱";
  return "很差";
}

// CSQ 推算出的 RSSI 评级（dBm）
static String rateRssi(int dbm) {
  if (dbm >= -70) return "极好";
  if (dbm >= -80) return "良好";
  if (dbm >= -90) return "一般";
  if (dbm >= -100) return "较弱";
  return "很差";
}

// 信号强度统一查询入口。
// 网页的 /query?type=signal 与 /modem?action=signal 都必须走这里，
// 否则不同接口用不同 AT 指令、不同换算公式，页面上会出现互相矛盾的数字。
// 口径：优先 LTE 指标 AT+CESQ（RSRP/RSRQ）；取不到或不合法时回退 AT+CSQ（只有 RSSI，
// 且必须标为 RSSI，绝不能当作 RSRP 展示）。
bool getModemSignal(SignalInfo& info) {
  info = SignalInfo();

  // 1) LTE 指标：+CESQ: <rxlev>,<ber>,<rscp>,<ecno>,<rsrq>,<rsrp>
  String cesqParams = extractParams(sendATCommand("AT+CESQ", 2000), "+CESQ:");
  if (cesqParams.length() > 0) {
    String v[6];
    if (splitParams(cesqParams, v, 6) == 6) {
      int rsrpRaw = v[5].toInt();
      int rsrqRaw = v[4].toInt();
      // 0-97 为有效值，99/255 表示未知或不支持
      if (rsrpRaw >= 0 && rsrpRaw <= 97) {
        info.lte = true;
        info.rsrpDbm = -140 + rsrpRaw;
        info.rsrpText = String(info.rsrpDbm) + " dBm (" + rateRsrp(info.rsrpDbm) + ")";
        info.quality = rateRsrp(info.rsrpDbm);
      }
      // RSRQ: 0-34 映射到 -19.5 ~ -3 dB，99/255 为未知
      if (rsrqRaw >= 0 && rsrqRaw <= 34) {
        info.rsrqDb = -19.5f + rsrqRaw * 0.5f;
        info.rsrqText = String(info.rsrqDb, 1) + " dB";
      }
      info.raw = cesqParams;
      info.source = "AT+CESQ";
      info.valid = info.lte;  // 只有拿到 LTE 的 RSRP 才算有效信号查询
    }
  }

  // 2) CSQ 兜底：+CSQ: <rssi>,<ber>，rssi 0-31 映射到 -113 ~ -51 dBm，99 为未知
  String csqParams = extractParams(sendATCommand("AT+CSQ", 3000), "+CSQ:");
  if (csqParams.length() > 0) {
    String c[2];
    if (splitParams(csqParams, c, 2) == 2) {
      int rssiRaw = c[0].toInt();
      info.ber = c[1].toInt();
      if (rssiRaw >= 0 && rssiRaw <= 31) {
        info.rssiDbm = -113 + rssiRaw * 2;
        info.rssiText = String(info.rssiDbm) + " dBm (" + rateRssi(info.rssiDbm) + ")";
        if (info.lte) {
          info.source = "AT+CESQ + AT+CSQ";
        } else {
          // 没有 LTE 指标时只能用 RSSI 兜底，此时切勿把该值标成 RSRP
          info.valid = true;
          info.raw = csqParams;
          info.source = "AT+CSQ";
          info.quality = rateRssi(info.rssiDbm);
        }
      }
    }
  }

  if (!info.valid) info.quality = "未知";
  logCaptureLn(String("信号查询[" + info.source + "]: RSRP=" + info.rsrpText +
                      ", RSSI=" + info.rssiText + ", raw=" + info.raw));
  return info.valid;
}

// 获取本机号码（SIM 卡 MSISDN）：AT+CNUM 返回 +CNUM: <名称>,"+86138...",<类型>
// 取第二个被双引号包裹的字段（号码本身），取不到时返回空串
String getModemOwnNumber() {
  String resp = sendATCommand("AT+CNUM", 2000);
  int idx = resp.indexOf("+CNUM:");
  if (idx < 0) return "";
  // 收集所有被双引号包裹的字段，取第二个（号码）
  int pos = idx;
  int quoteCount = 0;
  int start = -1;
  for (int i = idx; i < resp.length(); i++) {
    if (resp.charAt(i) == '"') {
      if (start < 0) {
        start = i + 1;
      } else {
        quoteCount++;
        if (quoteCount == 2) {
          String num = resp.substring(start, i);
          num.trim();
          return num;
        }
        start = -1;
      }
    }
  }
  return "";
}

// ---- SIM 卡热插拔检测 ----
// 轮询 AT+CPIN?（主循环每 SIM_POLL_INTERVAL_MS 一次）+ 解析模组主动上报的 +CPIN URC。
// 所有耗时动作（初始化、断电重启）都只在 simHotplugTick() 里、且模组串口空闲时执行，
// URC 回调只置标记，绝不在回调里跑初始化。
#define SIM_POLL_INTERVAL_MS  20000UL  // SIM 状态轮询间隔
#define SIM_INSERT_SETTLE_MS  2000UL   // 检测到插卡后，留给模组完成 SIM 上电/识别的时间
#define SIM_REBOOT_DELAY_MS   15000UL  // 首次初始化未成功时，等待这么久再做一次断电重启兜底

static SimStatus s_simStatus = SIM_STATUS_UNKNOWN;  // 最近一次检测到的 SIM 状态
static unsigned long s_lastSimPoll = 0;             // 上次轮询时刻
static bool s_simInitPending = false;               // 已检测到插卡，等到稳定后再跑初始化
static unsigned long s_simInitAt = 0;               // 检测到插卡的时刻
static bool s_simRebootTried = false;               // 本次插卡是否已做过断电重启兜底
static unsigned long s_simRebootAt = 0;             // 兜底断电重启的预定时刻（0=未安排）

// 解析 +CPIN 的参数体：READY / SIM PIN / SIM PUK / NOT INSERTED ...
static SimStatus parseSimState(const String& body) {
  String p = body;
  p.toUpperCase();
  if (p.indexOf("READY") >= 0) return SIM_STATUS_READY;
  if (p.indexOf("NOT INSERTED") >= 0 || p.indexOf("NOT READY") >= 0 ||
      p.indexOf("REMOVED") >= 0) return SIM_STATUS_ABSENT;
  if (p.indexOf("PIN") >= 0 || p.indexOf("PUK") >= 0) return SIM_STATUS_LOCKED;
  return SIM_STATUS_UNKNOWN;
}

SimStatus getSimStatus() {
  String resp = sendATCommand("AT+CPIN?", 3000);
  if (resp.indexOf("+CPIN:") >= 0) {
    SimStatus st = parseSimState(extractParams(resp, "+CPIN:"));
    if (st != SIM_STATUS_UNKNOWN) return st;
  }
  // 完全无响应（模组掉电/串口异常）：不判定，交给 modemAutoRecover() 处理
  if (resp.length() == 0) return SIM_STATUS_UNKNOWN;
  // 有响应但没有可用的 +CPIN（典型为 "+CME ERROR: SIM not inserted"）→ 认为卡不在
  return SIM_STATUS_ABSENT;
}

bool isSimInserted() {
  return s_simStatus == SIM_STATUS_READY || s_simStatus == SIM_STATUS_LOCKED;
}

String simStatusText() {
  switch (s_simStatus) {
    case SIM_STATUS_READY:  return String("已插入");
    case SIM_STATUS_ABSENT: return String("未插入");
    case SIM_STATUS_LOCKED: return String("需 PIN/PUK 解锁");
    default:                return String("未知");
  }
}

// SIM 确实不在时为 true：modemAutoRecover() 据此跳过无意义的重试
static bool simKnownAbsent() {
  return s_simStatus == SIM_STATUS_ABSENT;
}

// 安排插卡后的自动初始化
static void scheduleSimInit(unsigned long now) {
  s_simInitPending = true;
  s_simInitAt = now;
  s_simRebootTried = false;
}

// 插卡后的自动初始化；失败时安排一次（且仅一次）断电重启兜底——
// 部分模组必须重启 SIM 接口才能真正识别新插入的卡
static void runSimInsertInit() {
  logCaptureLn(String("SIM 卡已就绪，开始自动初始化模组..."));
  if (modemInit(true)) {
    ledOff();
    logCaptureLn(String("✅ 插卡后模组初始化成功，短信收发已恢复"));
    return;
  }
  if (s_simRebootTried) {
    logCaptureLn(String("⚠️ 插卡后初始化仍未成功，交由自动恢复继续重试"));
    return;
  }
  s_simRebootTried = true;
  s_simRebootAt = millis();
  logCaptureLn(String("⚠️ 模组暂未识别新卡，" + String(SIM_REBOOT_DELAY_MS / 1000) +
                      " 秒后将断电重启一次（仅此一次）"));
}

// 兜底：断电重启模组后重新初始化（有界，每次插卡最多一次）
static void powerCycleForSim() {
  logCaptureLn(String("正在断电重启模组以重新识别 SIM 卡..."));
  modemPowerCycle();
  if (modemInit(true)) {
    ledOff();
    logCaptureLn(String("✅ 重启后模组已就绪"));
  } else {
    logCaptureLn(String("⚠️ 重启后模组仍未就绪，交由自动恢复继续重试"));
  }
}

// 记录一次检测结果并按需触发动作（轮询与 URC 共用）
static void applySimStatus(SimStatus st) {
  if (st == SIM_STATUS_UNKNOWN) return;
  SimStatus prev = s_simStatus;
  s_simStatus = st;

  if (st == SIM_STATUS_ABSENT) {
    s_simRebootTried = false;
    s_simRebootAt = 0;
    if (prev == SIM_STATUS_READY || prev == SIM_STATUS_LOCKED) {
      logCaptureLn(String("⚠️ 检测到 SIM 卡已拔出，短信收发不可用"));
    }
    if (modemReady) {
      modemReady = false;
      digitalWrite(LED_BUILTIN, LOW);   // 点亮指示灯表示模组不可用（与初始化失败一致）
      logCaptureLn(String("模组已置为未就绪，插入 SIM 卡后会自动初始化"));
    }
    return;
  }

  if (st == SIM_STATUS_LOCKED) {
    if (prev != SIM_STATUS_LOCKED) {
      logCaptureLn(String("⚠️ SIM 卡需要 PIN/PUK 解锁，无法收发短信"));
    }
    return;
  }

  // SIM_STATUS_READY
  if (prev == SIM_STATUS_ABSENT) logCaptureLn(String("检测到 SIM 卡插入"));
  else if (prev == SIM_STATUS_LOCKED) logCaptureLn(String("检测到 SIM 卡已解锁"));
  else if (prev == SIM_STATUS_UNKNOWN) logCaptureLn(String("SIM 卡状态: READY"));

  if (!modemReady && !s_simInitPending && s_simRebootAt == 0) {
    scheduleSimInit(millis());
    logCaptureLn(String("SIM 已就绪，" + String(SIM_INSERT_SETTLE_MS / 1000) + " 秒后自动初始化模组"));
  }
}

// URC 回调：模组主动上报 SIM 状态（+CPIN: READY / +CPIN: NOT INSERTED）
// 只更新状态与标记，真正的初始化留给 simHotplugTick()，避免在串口读取路径里长时间阻塞
void handleSimUrc(const String& params) {
  SimStatus st = parseSimState(params);
  if (st == SIM_STATUS_UNKNOWN) return;
  logCaptureLn(String("模组上报 SIM 状态: " + params));
  applySimStatus(st);
}

// 主循环调用：轮询 SIM 状态并执行插卡后的自动初始化
void simHotplugTick() {
  if (modemBusy()) return;   // 初始化/发短信期间独占串口，不插队查询

  unsigned long now = millis();

  // 1) 插卡后初始化失败的兜底断电重启
  if (s_simRebootAt != 0) {
    if (now - s_simRebootAt < SIM_REBOOT_DELAY_MS) return;
    s_simRebootAt = 0;
    if (!modemReady && getSimStatus() == SIM_STATUS_READY) powerCycleForSim();
    return;
  }

  // 2) 检测到插卡，等模组完成 SIM 识别后跑一次初始化
  if (s_simInitPending) {
    if (now - s_simInitAt < SIM_INSERT_SETTLE_MS) return;
    s_simInitPending = false;
    if (modemReady) return;   // 期间已由别处（如网页重启）初始化成功
    runSimInsertInit();
    return;
  }

  // 3) 常规轮询（插卡与拔卡都要能发现）
  if (now - s_lastSimPoll < SIM_POLL_INTERVAL_MS) return;
  s_lastSimPoll = now;
  applySimStatus(getSimStatus());
}

// ---- 模组时钟（AT+CCLK?）----
// 模组返回示例：+CCLK: "26/09/30,19:36:45+32"
// 年份两位或四位均可；时区是 15 分钟单位的有符号数（+32 = UTC+8）
#define MODEM_TIME_MIN_YEAR 2020   // 早于该年份视为模组时钟无效（尚未取到网络时间）

// ML307R 实测：AT+CCLK? 的时分秒是 UTC，末尾的 +zz 只是「网络时区指示」，并没有叠加到时分秒上
// （例：真实北京时间 2026-10-01 03:36，模组返回 "26/09/30,19:36:45+32"）。
// 所以这里直接把时分秒当 UTC 时间戳使用，本地时间 = UTC + 时区偏移。
// 若你的模组返回的是已带偏移的本地时间，把下面改成 0（此时 UTC = 时分秒 - 时区偏移）。
#define MODEM_CCLK_IS_UTC  1

// 公历日期 → 距 1970-01-01 的天数（自己算，不依赖 libc 的时区设置）
static long daysFromCivil(int y, int m, int d) {
  y -= (m <= 2) ? 1 : 0;
  long era = (y >= 0 ? y : y - 399) / 400;
  int yoe = (int)(y - era * 400);
  int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// 时间戳 → "YYYY-MM-DD HH:MM:SS"（按 offsetHours 偏移后的本地时间）
static String formatEpoch(time_t epochUtc, int offsetHours) {
  time_t t = epochUtc + (time_t)offsetHours * 3600;
  struct tm* tmVal = gmtime(&t);
  char buf[24];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           tmVal->tm_year + 1900, tmVal->tm_mon + 1, tmVal->tm_mday,
           tmVal->tm_hour, tmVal->tm_min, tmVal->tm_sec);
  return String(buf);
}

// 解析 +CCLK 的参数体（已去掉引号），如 26/10/01,20:13:45+32
static bool parseCclk(const String& body, ModemTimeInfo& info) {
  int comma = body.indexOf(',');
  if (comma < 0) return false;
  String datePart = body.substring(0, comma);
  String timePart = body.substring(comma + 1);
  datePart.trim();
  timePart.trim();

  // 时区：时间串末尾的 +/-NN（15 分钟单位）
  int tzQuarter = 0;
  int signPos = -1;
  for (int i = timePart.length() - 1; i >= 0; i--) {
    char c = timePart.charAt(i);
    if (c == '+' || c == '-') { signPos = i; break; }
  }
  if (signPos >= 0) {
    tzQuarter = timePart.substring(signPos).toInt();  // toInt 自带正负号
    timePart = timePart.substring(0, signPos);
  }

  int s1 = datePart.indexOf('/');
  int s2 = datePart.indexOf('/', s1 + 1);
  int c1 = timePart.indexOf(':');
  int c2 = timePart.indexOf(':', c1 + 1);
  if (s1 < 0 || s2 < 0 || c1 < 0 || c2 < 0) return false;

  int year = datePart.substring(0, s1).toInt();
  int mon  = datePart.substring(s1 + 1, s2).toInt();
  int day  = datePart.substring(s2 + 1).toInt();
  int hour = timePart.substring(0, c1).toInt();
  int min  = timePart.substring(c1 + 1, c2).toInt();
  int sec  = timePart.substring(c2 + 1).toInt();   // 可能带小数，toInt 只取整数部分
  if (year < 100) year += 2000;
  if (year < MODEM_TIME_MIN_YEAR || mon < 1 || mon > 12 || day < 1 || day > 31) return false;
  if (hour > 23 || min > 59 || sec > 59) return false;

  long days = daysFromCivil(year, mon, day);
  long secs = days * 86400L + hour * 3600L + min * 60L + sec;
  // 见 MODEM_CCLK_IS_UTC：默认时分秒即 UTC，不再额外减去时区偏移
  info.epochUtc = (time_t)(MODEM_CCLK_IS_UTC ? secs : secs - (long)tzQuarter * 900L);
  info.tzQuarterHours = tzQuarter;
  info.valid = true;
  return true;
}

String formatSystemTime(time_t epochUtc) {
  if (epochUtc < 100000) return String("未同步");
  return formatEpoch(epochUtc, DISPLAY_TZ_OFFSET_HOURS) +
         " (UTC+" + String(DISPLAY_TZ_OFFSET_HOURS) + ")";
}

// 时间同步策略：
// 1) 首次：基站网络时间（NITZ）下发有几秒到几十秒的延迟，模组刚就绪时 AT+CCLK? 可能是无效值，
//    所以就绪后由主循环每 60 秒重试，成功一次即进入定期校准；失败有次数上限，不再刷日志。
// 2) 之后：ESP32 的 RTC 会漂移，每 24 小时再用基站时间校准一次。
#define MODEM_TIME_SYNC_INTERVAL_MS   60000UL     // 首次同步未成功时的重试间隔
#define MODEM_TIME_SYNC_MAX_ATTEMPTS  10          // 首次同步的重试上限
#define MODEM_TIME_RESYNC_INTERVAL_MS 86400000UL  // 首次成功后的定期校准间隔（24 小时）
static bool modemTimeSynced = false;
static int modemTimeAttempts = 0;
static unsigned long lastModemTimeAttempt = 0;    // 最近一次尝试（成功时即最近一次校准）时刻

// 尝试同步一次；返回是否成功
static bool trySyncModemTime() {
  lastModemTimeAttempt = millis();
  ModemTimeInfo info;
  if (syncTimeFromModem(info)) return true;
  if (modemTimeSynced) return false;   // 之前成功过，24 小时后再试，不计入首轮重试次数
  modemTimeAttempts++;
  if (modemTimeAttempts >= MODEM_TIME_SYNC_MAX_ATTEMPTS) {
    logCaptureLn(String("⚠️ 模组时间同步已重试 " + String(modemTimeAttempts) +
                        " 次仍未取到有效网络时间，停止自动重试（可在「模组诊断」手动同步）"));
  }
  return false;
}

// 主循环调用：模组就绪后同步时间，成功之后每 24 小时再校准一次
void modemTimeSyncTick() {
  if (!modemReady) return;
  unsigned long now = millis();
  if (modemTimeSynced) {
    // 已同步成功：定期校准，抵消 RTC 漂移（millis() 回绕也能正确比较）
    if (now - lastModemTimeAttempt >= MODEM_TIME_RESYNC_INTERVAL_MS) trySyncModemTime();
    return;
  }
  if (modemTimeAttempts >= MODEM_TIME_SYNC_MAX_ATTEMPTS) return;
  if (now - lastModemTimeAttempt < MODEM_TIME_SYNC_INTERVAL_MS) return;
  trySyncModemTime();
}

bool getModemTime(ModemTimeInfo& info) {
  info = ModemTimeInfo();
  info.source = "AT+CCLK?";
  String params = extractParams(sendATCommand("AT+CCLK?", 2000), "+CCLK:");
  params.replace("\"", "");
  params.trim();
  info.raw = params;
  if (params.length() == 0) return false;
  if (!parseCclk(params, info)) return false;
  // 模组本地时间 = UTC 时间戳 + 模组时区偏移
  info.localText = formatEpoch(info.epochUtc + (time_t)info.tzQuarterHours * 900, 0);
  return true;
}

bool syncTimeFromModem(ModemTimeInfo& info) {
  if (!getModemTime(info)) {
    logCaptureLn(String("⚠️ 未能获取模组时间（AT+CCLK? 无有效响应或时钟未初始化）"));
    return false;
  }
  struct timeval tv;
  tv.tv_sec = info.epochUtc;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  timeSynced = true;
  modemTimeSynced = true;   // 成功一次即停止自动重试（网页手动同步同样走这里）
  logCaptureLn(String("系统时间已同步（来源: 4G 网络时间 " + info.source + "，原始 " + info.raw +
                      "，UTC " + formatEpoch(info.epochUtc, 0) +
                      "）: " + formatSystemTime(info.epochUtc)));
  return true;
}

// 取消模组可能残留的 CMGS 输入态：
// 否则模组会一直等待 PDU 输入，之后所有 AT 指令都被当成短信内容吞掉，
// 表现为后续每次发送都等不到 ">" 提示符
static void abortPendingInput() {
  Serial1.write(0x1B);  // ESC
  delay(200);
  while (Serial1.available()) Serial1.read();
}

static bool sendSMSInternal(const char* phoneNumber, const char* message);

// 发送短信（PDU模式）
// 整个过程独占 Serial1：期间用 smsInProgress 阻止其它请求再次进入，
// 每个等待循环都有上界并让出 CPU（否则会一直忙等触发任务看门狗复位）
bool sendSMS(const char* phoneNumber, const char* message) {
  if (smsInProgress) {
    logCaptureLn(String("⚠️ 上一次短信发送尚未结束，忽略本次请求"));
    return false;
  }
  if (!modemReady) {
    logCaptureLn(String("⚠️ 模组未就绪，无法发送短信"));
    return false;
  }

  smsInProgress = true;
  bool result = sendSMSInternal(phoneNumber, message);
  smsInProgress = false;
  return result;
}

static bool sendSMSInternal(const char* phoneNumber, const char* message) {
  logCaptureLn(String("准备发送短信..."));
  logCaptureLn(String("目标号码: " + String(phoneNumber)));
  logCaptureLn(String("短信内容: " + String(message)));

  // 使用pdulib编码PDU
  pdu.setSCAnumber();  // 使用默认短信中心
  int pduLen = pdu.encodePDU(phoneNumber, message);

  if (pduLen <= 0) {
    logCaptureLn(String("PDU编码失败，错误码: " + String(pduLen)));
    return false;
  }

  logCaptureLn(String("PDU数据: " + String(pdu.getSMS())));
  logCaptureLn(String("PDU长度: " + String(pduLen)));

  // 发送AT+CMGS命令
  String cmgsCmd = "AT+CMGS=";
  cmgsCmd += pduLen;

  while (Serial1.available()) Serial1.read();
  Serial1.println(cmgsCmd);

  // 等待 > 提示符
  unsigned long start = millis();
  bool gotPrompt = false;
  String echo = "";
  while (millis() - start < SMS_PROMPT_TIMEOUT_MS) {
    while (Serial1.available()) {
      char c = Serial1.read();
      echo += c;
      if (c == '>') {
        gotPrompt = true;
        break;
      }
    }
    if (gotPrompt) break;
    if (echo.indexOf("ERROR") >= 0) {
      logCaptureLn(String("AT+CMGS被拒绝: " + echo));
      return false;
    }
    server.handleClient();
    delay(1);
  }

  if (!gotPrompt) {
    logCaptureLn(String("未收到>提示符，取消本次发送"));
    abortPendingInput();
    return false;
  }

  // 发送PDU数据
  Serial1.print(pdu.getSMS());
  Serial1.write(0x1A);  // Ctrl+Z 结束

  // 等待响应（按整行输出日志，避免逐字符 Serial.print 把等待时间拖长）
  start = millis();
  String resp = "";
  String line = "";
  bool success = false;
  bool done = false;
  while (millis() - start < SMS_RESULT_TIMEOUT_MS) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      if (c == '\n') {
        line.trim();
        if (line.length() > 0) logCaptureLn(String("模组< " + line));
        line = "";
      } else {
        line += c;
      }
      if (resp.indexOf("OK") >= 0) { success = true; done = true; break; }
      if (resp.indexOf("ERROR") >= 0) { done = true; break; }
    }
    if (done) break;
    server.handleClient();
    delay(1);
  }

  line.trim();
  if (line.length() > 0) logCaptureLn(String("模组< " + line));

  if (!done) {
    logCaptureLn(String("短信发送超时，取消本次发送"));
    abortPendingInput();
    return false;
  }

  if (success) {
    logCaptureLn(String("短信发送成功"));
  } else {
    logCaptureLn(String("短信发送失败: " + resp));
  }
  return success;
}
