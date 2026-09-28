#include "modem.h"
#include "web_handlers.h"

// 初始化重试上限（所有等待都必须有上界，避免模组异常时永久卡死/反复重启）
#define MODEM_AT_RETRY            8      // 开机握手重试次数
#define MODEM_AT_RETRY_BG         3      // 后台自动恢复时的握手重试次数
#define MODEM_CMD_RETRY           3      // 单条 AT 配置命令重试次数
#define MODEM_CMD_RETRY_BG        1      // 后台恢复时单条命令只试一次，尽量少阻塞主循环
#define MODEM_CEREG_RETRY         20     // 开机等待网络注册的次数（每次约 2 秒）
#define MODEM_CEREG_RETRY_BG      3      // 后台恢复时等待网络注册的次数
#define MODEM_REINIT_INTERVAL_MS  90000UL  // 模组未就绪时的自动重试间隔
#define MODEM_REINIT_MAX_INTERVAL_MS 300000UL // 连续失败后的最长重试间隔（5 分钟）

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
        }
        return resp;
      }
    }
    server.handleClient();
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

// 模组 AT 初始化流程（setup 中调用，resetModule 后也调用）
// background=true 表示后台自动恢复，使用更少的重试次数，尽量少阻塞主循环
// 返回 true 表示模组可用（已注册网络且短信参数配置成功）
bool modemInit(bool background) {
  // 防止重入：modemInit() 内部会调 server.handleClient()，
  // 浏览器超时重试或自动恢复都可能造成嵌套调用
  static bool inProgress = false;
  if (inProgress) {
    logCaptureLn(String("模组初始化正在进行，忽略本次调用"));
    return modemReady;
  }
  inProgress = true;
  lastModemInitAttempt = millis();
  modemReady = false;

  int atRetry = background ? MODEM_AT_RETRY_BG : MODEM_AT_RETRY;
  int cmdRetry = background ? MODEM_CMD_RETRY_BG : MODEM_CMD_RETRY;
  int ceregRetry = background ? MODEM_CEREG_RETRY_BG : MODEM_CEREG_RETRY;

  // 清掉上电噪声/残留
  while (Serial1.available()) Serial1.read();

  if (!modemWaitATReady(atRetry)) {
    logCaptureLn(String("⚠️ 模组AT无响应（未上电/串口异常），放弃本次初始化，稍后自动重试"));
    inProgress = false;
    return false;
  }
  logCaptureLn(String("模组AT响应正常"));

  //判断型号，做一些特定操作
  bool need_set_CGACT = true;
  String resp = sendATCommand("ATI", 2000);
  logCaptureLn(String("ATI响应: " + resp));
  if (resp.indexOf("OK") >= 0) {
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
  if (!modemReady) {
    logCaptureLn(String("⚠️ 模组初始化未完成，系统将定时自动重试（不影响网页访问）"));
  }
  inProgress = false;
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
  if (millis() - lastModemInitAttempt < reinitInterval) return;

  logCaptureLn(String("模组未就绪，尝试重新初始化..."));
  bool ok = modemInit(true);
  if (ok) {
    reinitInterval = MODEM_REINIT_INTERVAL_MS;
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
  }
  return false;
}

// 发送短信（PDU模式）
bool sendSMS(const char* phoneNumber, const char* message) {
  logCaptureLn(String("准备发送短信..."));
  logCapture(String("目标号码: ")); logCaptureLn(String(phoneNumber));
  logCapture(String("短信内容: ")); logCaptureLn(String(message));

  // 使用pdulib编码PDU
  pdu.setSCAnumber();  // 使用默认短信中心
  int pduLen = pdu.encodePDU(phoneNumber, message);
  
  if (pduLen < 0) {
    logCapture(String("PDU编码失败，错误码: "));
    logCaptureLn(String(pduLen));
    return false;
  }
  
  logCapture(String("PDU数据: ")); logCaptureLn(String(pdu.getSMS()));
  logCapture(String("PDU长度: ")); logCaptureLn(String(pduLen));
  
  // 发送AT+CMGS命令
  String cmgsCmd = "AT+CMGS=";
  cmgsCmd += pduLen;
  
  while (Serial1.available()) Serial1.read();
  Serial1.println(cmgsCmd);
  
  // 等待 > 提示符
  unsigned long start = millis();
  bool gotPrompt = false;
  while (millis() - start < 5000) {
    if (Serial1.available()) {
      char c = Serial1.read();
      logCapture(String(c));
      if (c == '>') {
        gotPrompt = true;
        break;
      }
    }
    server.handleClient();
  }
  
  if (!gotPrompt) {
    logCaptureLn(String("未收到>提示符"));
    return false;
  }
  
  // 发送PDU数据
  Serial1.print(pdu.getSMS());
  Serial1.write(0x1A);  // Ctrl+Z 结束
  
  // 等待响应
  start = millis();
  String resp = "";
  while (millis() - start < 30000) {
    while (Serial1.available()) {
      char c = Serial1.read();
      resp += c;
      logCapture(String(c));
      if (resp.indexOf("OK") >= 0) {
        logCaptureLn(String("\n短信发送成功"));
        return true;
      }
      if (resp.indexOf("ERROR") >= 0) {
        logCaptureLn(String("\n短信发送失败"));
        return false;
      }
    }
    server.handleClient();
  }
  logCaptureLn(String("短信发送超时"));
  return false;
}
