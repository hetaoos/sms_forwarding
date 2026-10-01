#include "config.h"
#include "web_handlers.h"
#include "wifi_config.h"

// 保存配置到NVS
void saveConfig() {
  preferences.begin("sms_config", false);
  preferences.putString("smtpServer", config.smtpServer);
  preferences.putInt("smtpPort", config.smtpPort);
  preferences.putString("smtpUser", config.smtpUser);
  preferences.putString("smtpPass", config.smtpPass);
  preferences.putString("smtpSendTo", config.smtpSendTo);
  preferences.putUInt("mailTypes", config.emailNotifyTypes);
  preferences.putString("adminPhone", config.adminPhone);
  preferences.putString("webUser", config.webUser);
  preferences.putString("webPass", config.webPass);
  preferences.putString("numBlkList", config.numberBlackList);
  preferences.putString("wifiSsid", config.wifiSsid);
  preferences.putString("wifiPass", config.wifiPass);
  
  // 保存推送通道配置
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    String prefix = "push" + String(i);
    preferences.putBool((prefix + "en").c_str(), config.pushChannels[i].enabled);
    preferences.putUChar((prefix + "type").c_str(), (uint8_t)config.pushChannels[i].type);
    preferences.putString((prefix + "url").c_str(), config.pushChannels[i].url);
    preferences.putString((prefix + "name").c_str(), config.pushChannels[i].name);
    preferences.putString((prefix + "k1").c_str(), config.pushChannels[i].key1);
    preferences.putString((prefix + "k2").c_str(), config.pushChannels[i].key2);
    preferences.putString((prefix + "k3").c_str(), config.pushChannels[i].key3);
    preferences.putString((prefix + "k4").c_str(), config.pushChannels[i].key4);
    preferences.putString((prefix + "k5").c_str(), config.pushChannels[i].key5);
    preferences.putString((prefix + "body").c_str(), config.pushChannels[i].customBody);
  }
  
  preferences.end();
  logCaptureLn(String("配置已保存"));
}

// 从NVS加载配置
void loadConfig() {
  preferences.begin("sms_config", true);
  config.smtpServer = preferences.getString("smtpServer", "");
  config.smtpPort = preferences.getInt("smtpPort", 465);
  config.smtpUser = preferences.getString("smtpUser", "");
  config.smtpPass = preferences.getString("smtpPass", "");
  config.smtpSendTo = preferences.getString("smtpSendTo", "");
  // 老固件升级时 NVS 无此键，取默认值（全开），行为与升级前一致
  config.emailNotifyTypes = preferences.getUInt("mailTypes", EMAIL_NOTIFY_DEFAULT) & EMAIL_NOTIFY_ALL;
  config.adminPhone = preferences.getString("adminPhone", "");
  config.webUser = preferences.getString("webUser", DEFAULT_WEB_USER);
  config.webPass = preferences.getString("webPass", DEFAULT_WEB_PASS);
  config.numberBlackList = preferences.getString("numBlkList", "");
  config.wifiSsid = preferences.getString("wifiSsid", DEFAULT_WIFI_SSID);
  config.wifiPass = preferences.getString("wifiPass", DEFAULT_WIFI_PASS);
  
  // 加载推送通道配置
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    String prefix = "push" + String(i);
    config.pushChannels[i].enabled = preferences.getBool((prefix + "en").c_str(), false);
    config.pushChannels[i].type = (PushType)preferences.getUChar((prefix + "type").c_str(), PUSH_TYPE_POST_JSON);
    config.pushChannels[i].url = preferences.getString((prefix + "url").c_str(), "");
    config.pushChannels[i].name = preferences.getString((prefix + "name").c_str(), "通道" + String(i + 1));
    config.pushChannels[i].key1 = preferences.getString((prefix + "k1").c_str(), "");
    config.pushChannels[i].key2 = preferences.getString((prefix + "k2").c_str(), "");
    config.pushChannels[i].key3 = preferences.getString((prefix + "k3").c_str(), "");
    config.pushChannels[i].key4 = preferences.getString((prefix + "k4").c_str(), "");
    config.pushChannels[i].key5 = preferences.getString((prefix + "k5").c_str(), "");
    config.pushChannels[i].customBody = preferences.getString((prefix + "body").c_str(), "");
  }
  
  // 兼容旧配置：如果有旧的httpUrl配置，迁移到第一个通道
  String oldHttpUrl = preferences.getString("httpUrl", "");
  if (oldHttpUrl.length() > 0 && !config.pushChannels[0].enabled) {
    config.pushChannels[0].enabled = true;
    config.pushChannels[0].url = oldHttpUrl;
    config.pushChannels[0].type = preferences.getUChar("barkMode", 0) != 0 ? PUSH_TYPE_BARK : PUSH_TYPE_POST_JSON;
    config.pushChannels[0].name = "迁移通道";
    logCaptureLn(String("已迁移旧HTTP配置到推送通道1"));
  }
  
  preferences.end();
  logCaptureLn(String("配置已加载"));
}

// 检查推送通道是否有效配置
bool isPushChannelValid(const PushChannel& ch) {
  if (!ch.enabled) return false;
  
  switch (ch.type) {
    case PUSH_TYPE_POST_JSON:
    case PUSH_TYPE_BARK:
    case PUSH_TYPE_GET:
    case PUSH_TYPE_DINGTALK:
    case PUSH_TYPE_FEISHU:
    case PUSH_TYPE_CUSTOM:
      return ch.url.length() > 0;
    case PUSH_TYPE_PUSHPLUS:
    case PUSH_TYPE_SERVERCHAN:
      return ch.key1.length() > 0;  // 这两个主要靠key1（token/sendkey）
    case PUSH_TYPE_GOTIFY:
      return ch.url.length() > 0 && ch.key1.length() > 0;  // 需要URL和Token
    case PUSH_TYPE_TELEGRAM:
      return ch.key1.length() > 0 && ch.key2.length() > 0; // 需要Chat ID和Token
    case PUSH_TYPE_MAILGUN:
      return ch.key1.length() > 0 && ch.key2.length() > 0 && ch.key3.length() > 0; // 需要API Key、域名、收件人
    default:
      return false;
  }
}

// 检查配置是否有效（至少配置了邮件或任一推送通道）
bool isConfigValid() {
  bool emailValid = config.smtpServer.length() > 0 && 
                    config.smtpUser.length() > 0 && 
                    config.smtpPass.length() > 0 && 
                    config.smtpSendTo.length() > 0;
  
  bool pushValid = false;
  for (int i = 0; i < MAX_PUSH_CHANNELS; i++) {
    if (isPushChannelValid(config.pushChannels[i])) {
      pushValid = true;
      break;
    }
  }
  
  return emailValid || pushValid;
}

// 判断某类事件是否允许发邮件（mask 取 EMAIL_NOTIFY_*）
bool emailNotifyEnabled(uint32_t mask) {
  return (config.emailNotifyTypes & mask) != 0;
}

// 获取当前设备URL（AP 模式下返回配置热点地址，否则返回 STA 地址）
String getDeviceUrl() {
  if (apMode && WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
    return "http://" + WiFi.softAPIP().toString() + "/";
  }
  return "http://" + WiFi.localIP().toString() + "/";
}

// 开发板芯片类型（如 "ESP32-C3"）：概览页与启动邮件共用同一处的取值逻辑。
// 只用运行时系统 API，不依赖任何编译期宏、也不需要额外的编译参数：
// ESP.getChipModel() 内部走 IDF 的 esp_chip_info()（ESP32 经典款则读 eFuse 封装型号）。
// 注：Arduino 的板型名（如 FQBN 里的 makergo_c3_supermini）只存在于编译期，
// 运行时没有任何系统 API 能取到，因此这里不展示板名。
String getChipModelName() {
  String m = String(ESP.getChipModel());
  if (m.length() == 0 || m == "UNKNOWN" || m == "Unknown") m = "未知";
  return m;
}

// 获取当前设备 IP（AP 模式下返回配置热点 IP，否则返回 STA IP）
// 取不到本机号码时，通知邮件用它作为设备标识
String getDeviceIp() {
  if (apMode && WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
    return WiFi.softAPIP().toString();
  }
  return WiFi.localIP().toString();
}
