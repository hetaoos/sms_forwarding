#include "wifi_manager.h"
#include "web_handlers.h"
#include "config.h"
#include "wifi_config.h"
#include "modem.h"  // LED 指示：ledApEnter / ledRestoreNormal

// 启动配置 AP：WiFi 连接失败时让用户的手机/电脑能连上来配置 WiFi。
// 使用 WIFI_AP_STA，这样即使处于 AP 模式，STA 也可以在后台尝试连接。
void startAPMode() {
  WiFi.mode(WIFI_AP_STA);
  // 配置热点密码：AP_PASS 长度 >= 8 时启用密码，否则为开放网络（无密码）
  if (strlen(AP_PASS) >= 8) {
    WiFi.softAP(AP_SSID, AP_PASS);
    logCaptureLn(String("已启动配置AP热点: ") + String(AP_SSID) + " (密码: " + String(AP_PASS) + ")");
  } else {
    WiFi.softAP(AP_SSID);
    logCaptureLn(String("已启动配置AP热点: ") + String(AP_SSID) + " (开放网络，无密码)");
  }
  apMode = true;
  ledApEnter();  // 进入 AP 模式：蓝灯改慢闪表示「等待配置」
  logCapture(String("AP地址: "));
  logCaptureLn(WiFi.softAPIP().toString());
  logCaptureLn(String("请用手机/电脑连接该热点，再访问 http://") +
               WiFi.softAPIP().toString() + " 配置WiFi");
}

// 尝试以 ssid/pass 连接 STA。
// 成功 → 关闭配置 AP，仅保留 STA（设备恢复正常联网）。
// 失败 → 若当前不在 AP 模式则启动 AP，让用户重新配置。
bool connectWiFiAndSettle(const String& ssid, const String& pass, unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.setScanMethod(WIFI_FAST_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(ssid.c_str(), pass.c_str());
  logCaptureLn(String("正在连接WiFi: ") + ssid);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(100);
    server.handleClient();  // 保持网页响应（AP 模式下），避免浏览器超时
  }

  if (WiFi.status() == WL_CONNECTED) {
    logCaptureLn(String("WiFi已连接, IP: ") + WiFi.localIP().toString());
    if (apMode) {
      // 连接成功，关闭配置 AP（保留 STA 接口）
      WiFi.softAPdisconnect(true);
      apMode = false;
      ledRestoreNormal();  // 离开 AP 模式：恢复常态 LED（不可用常亮/正常熄灭）
      logCaptureLn(String("配置AP已关闭，设备恢复正常联网"));
    }
    return true;
  }

  logCaptureLn(String("⚠️ WiFi连接失败"));
  if (!apMode) {
    startAPMode();  // 回退到 AP 模式，等待用户重新配置
  }
  return false;
}
