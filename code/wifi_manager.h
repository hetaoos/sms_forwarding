#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "globals.h"

// 启动配置 AP（WIFI_AP_STA 模式），供用户连入并配置 WiFi。
void startAPMode();

// 以给定凭据连接 STA；成功则切换为纯 STA 并关闭配置 AP，失败则保持/回退到 AP 模式。
// 注意：调用前需先 server.send 完 HTTP 响应，因为内部会断开 AP 接口。
// 返回是否成功连上 STA。
bool connectWiFiAndSettle(const String& ssid, const String& pass, unsigned long timeoutMs);

#endif
