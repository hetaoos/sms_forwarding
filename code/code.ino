#include "globals.h"
#include "wifi_config.h"
#include "config.h"
#include "web_handlers.h"
#include "web_handlers.h"
#include "modem.h"
#include "web_handlers.h"
#include "push.h"
#include "web_handlers.h"
#include "sms_process.h"
#include "web_handlers.h"
#include "wifi_manager.h"

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  Serial.begin(115200);
  // 缩短初始化延时，WiFi连接会处理自己的超时
  delay(200);
  // 必须在 begin() 之前设置，否则无效（默认只有 256 字节，长短信分段会被挤掉）
  Serial1.setRxBufferSize(MODEM_RX_BUFFER_SIZE);
  Serial1.begin(115200, SERIAL_8N1, RXD, TXD);
  while (Serial1.available()) Serial1.read();
  modemPowerCycle();
  while (Serial1.available()) Serial1.read();
  initConcatBuffer();
  loadConfig();
  configValid = isConfigValid();

  // ---- WiFi 连接优化 ----
  // 已通过 NVS 加载配置，使用配置中的 WiFi 凭据（缺省为 DEFAULT_WIFI_*）
  // 连接失败时进入 AP 配置模式，供用户连入 Web 重新设置 WiFi。
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                    // 关闭 Modem Sleep，提高连接响应速度
  WiFi.setAutoReconnect(true);             // 断线后自动重连
  // 使用快速扫描而非全信道扫描（全信道扫描在空信道上等待超时极慢）
  // 首次连接成功后 ESP32 会自动记住信道，下次启动更快
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  if (config.wifiSsid.length() > 0) {
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPass.c_str());
    logCaptureLn(String("连接wifi: ") + config.wifiSsid);
  } else {
    logCaptureLn(String("⚠️ 未配置WiFi，直接进入AP配置模式"));
  }

  // 带超时的等待连接，失败则进入 AP 模式让用户配置
  unsigned long wifiStart = millis();
  const unsigned long WIFI_TIMEOUT = 20000; // 20秒超时
  while (config.wifiSsid.length() > 0 && WiFi.status() != WL_CONNECTED && millis() - wifiStart < WIFI_TIMEOUT) {
    blink_short(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    logCaptureLn(String("wifi已连接"));
    logCapture(String("IP地址: "));
    logCaptureLn(WiFi.localIP().toString());
    logCapture(String("信号强度(RSSI): "));
    logCaptureLn(String(WiFi.RSSI()) + " dBm");
  } else {
    logCaptureLn(String("⚠️ WiFi连接失败/未配置，启动配置AP模式"));
    startAPMode();
  }

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/tools", handleRoot);
  server.on("/sms", handleRoot);
  server.on("/sendsms", HTTP_POST, handleSendSms);
  server.on("/ping", HTTP_POST, handlePing);
  server.on("/query", handleQuery);
  server.on("/flight", handleFlightMode);
  server.on("/at", handleATCommand);
  server.on("/log", handleLog);
  server.on("/modem", handleModem);
  server.on("/wifi", handleWifi);
  server.on("/system", handleSystem);
  server.begin();
  logCaptureLn(String("HTTP服务器已启动"));

  // ---- NTP 时间同步 ----
  logCaptureLn(String("正在同步NTP时间..."));
  configTime(0, 0, "ntp.ntsc.ac.cn", "ntp.aliyun.com", "pool.ntp.org");
  int ntpRetry = 0;
  while (time(nullptr) < 100000 && ntpRetry < 100) {
    delay(1);
    server.handleClient();
    ntpRetry++;
  }
  if (time(nullptr) >= 100000) {
    timeSynced = true;
    logCaptureLn(String("NTP时间同步成功"));
    time_t now = time(nullptr);
    logCapture(String("当前UTC时间戳: "));
    logCaptureLn(String(now));
  } else {
    logCaptureLn(String("NTP时间同步失败，将使用设备时间"));
  }

  ssl_client.setInsecure();

  // ---- 模组初始化（较慢，但网页已可访问） ----
  modemInit();
  // 初始化成功后熄灭蓝色 LED；失败则保持点亮，提示模组不可用
  if (modemReady) ledOff();

  // ---- 启动通知（模组初始化完成后发送，可附带模组/信号/号码等信息） ----
  if (configValid) {
    logCaptureLn(String("配置有效，发送启动通知..."));
    sendStartupEmail();
  }
}

void loop() {
  if (apMode) {
    ledApTick();   // AP 模式：蓝灯慢闪（约 1 秒周期）表示「等待配置」
  } else {
    ledTick();     // 短信指示灯到时熄灭（非阻塞）
  }
  server.handleClient();
  if (!configValid) {
    if (millis() - lastPrintTime >= 1000) {
      lastPrintTime = millis();
      logCaptureLn(String("⚠️ 请访问 " + getDeviceUrl() + " 配置系统参数"));
    }
  }
  checkConcatTimeout();
  simHotplugTick();      // SIM 热插拔检测：插卡自动初始化 / 拔卡置未就绪
  modemAutoRecover();
  modemTimeSyncTick();   // 模组就绪后用 4G 网络时间校准系统时间，之后每 24 小时再校准一次
  if (Serial.available()) Serial1.write(Serial.read());
  checkSerial1URC();
  // 推送/邮件的实际发送放在这里分片执行（每次最多一次网络请求），
  // 保证 URC 与 HTTP 始终有机会被处理
  processNotifyQueue();
}
