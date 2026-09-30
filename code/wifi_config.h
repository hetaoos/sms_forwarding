// WIFI - 默认 WiFi（首次启动、NVS 中无配置时使用）。
// 后续可在 Web 界面「WiFi 设置」中修改，并持久化到 NVS，不再依赖此处宏。
#define DEFAULT_WIFI_SSID ""
#define DEFAULT_WIFI_PASS ""

// 配置 AP：WiFi 连接失败时启动的临时热点，供用户连入并配置 WiFi。
// 名称尽量唯一，避免与用户自家 WiFi 冲突。
#define AP_SSID "SMS-Forwarding-Setup"
#define AP_PASS "12345678"
