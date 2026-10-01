# 低成本短信转发器

> 本项目基于 [chenxuuu/sms_forwarding](https://github.com/chenxuuu/sms_forwarding) 的源码 Fork 修改而来。

> 当前分支为新方案，2022年的老方案请前往[luatos分支](https://github.com/chenxuuu/sms_forwarding/tree/old-luatos)。  
本项目**仅用于接收短信**与进行保号相关功能。  
多卡控制、通话、拨号、开放接口、自动化等功能，永远不会考虑支持，请勿提出相关需求。

## 本 Fork 新增功能

相比原版，本分支新增/增强以下内容：

- **硬件切换为 ML307R/C/A-DC 4G 模组**（运行出厂 AT 固件，无需改动）
- **SIM 卡热插拔检测**：插卡自动初始化、拔卡置未就绪
- **4G 网络时间同步**，并接入系统时间与 Web 界面展示
- **邮件增强**：新增 Mailgun 通道、HTML 富文本卡片正文、启动邮件附带模组/信号/号码信息、按事件类型开关并持久化至 NVS
- **收短信 LED 闪烁提示**（初始化后熄灭）
- **Web 界面深色主题**
- **WiFi 配置增强**：Web 扫描附近网络选择连接、失败时自动进入 AP 配网，配置存于 NVS
- **稳定性提升**：短信通知改为异步队列分片发送、统一网络超时上限、各通道与邮件重试、模组初始化有限重试与后台自动恢复
- **短信解析发送者名称与验证码**，增强各通道通知

[后台页面演示](https://sms.j2.cx/)

本项目旨在使用低成本的硬件设备，实现短信的自动转发功能，支持多种推送方式同时启用。

> 视频教程：[B站视频](https://www.bilibili.com/video/BV1cSmABYEiX)

<img src="assets/photo.png" width="200" />

## 功能

- 支持使用通用AT指令与模块进行通信
- 开启后支持通过WEB界面配置短信转发参数、查询当前状态
- **支持多达5个推送通道同时启用**，每个通道可独立配置
- 支持将收到的短信转发到指定的邮箱
- 支持通过WEB界面主动发送短信，以便消耗余额
- 支持通过WEB界面进行Ping测试，以极低的成本消耗余额
- 支持长短信自动合并（30秒超时）
- 支持管理员短信远程发送短信和重启设备
- 支持通过WEB界面配置WiFi（含扫描附近网络、选择并输入密码连接）
- WiFi 连接失败时自动启动配置 AP 热点，供手机/电脑连入网页重新设置 WiFi

## 推送通道支持

支持以下7种推送方式，可同时启用多个通道：

| 推送方式 | 说明 | 需要配置 |
|---------|------|---------|
| **POST JSON** | 通用HTTP POST | URL |
| **Bark** | iOS推送服务 | Bark服务器URL |
| **GET请求** | URL参数方式 | URL |
| **钉钉机器人** | 企业群通知 | Webhook URL，可选Secret加签 |
| **PushPlus** | 微信公众号推送 | Token |
| **Server酱** | 微信推送服务 | SendKey |
| **自定义模板** | 灵活的JSON模板 | URL + 请求体模板 |
| **飞书机器人** | 自定义通知 | Webhook URL |

### 推送格式说明

- **POST JSON**: `{"sender":"发送者号码","message":"短信内容","timestamp":"时间戳"}`
- **Bark**: `{"title":"发送者号码","body":"短信内容"}`
- **GET请求**: `URL?sender=xxx&message=xxx&timestamp=xxx`（自动URL编码）
- **钉钉机器人**: 文本消息格式，支持加签验证
- **PushPlus**: 使用Token推送，支持HTML格式
- **Server酱**: 使用SendKey推送，支持Markdown格式
- **自定义模板**: 使用`{sender}`、`{message}`、`{timestamp}`占位符
- **飞书机器人**: 文本消息格式，支持加签验证

|状态信息|主动ping|
|-|-|
|![](assets/status.png)|![](assets/ping.png)|

## 硬件搭配

若没有焊接能力，希望直接使用成品，可选直接购以下套件（我看过了，和自己做的成本一样）  
支持**移动/联通/电信卡**：

- [小蓝鲸WIFI短信宝](https://item.taobao.com/item.htm?id=1003711355912)（找客服问）
- [4G FPC天线](https://item.taobao.com/item.htm?id=1003711355912&skuId=6162872574943)，与开发板同购

如果希望自行焊接硬件，参考下面的硬件搭配，总成本约¥27.8（会有浮动，可按实际自行组合搭配）  
仅支持**移动/联通卡**：

- ESP32C3开发板，实测选用[ESP32C3 Super Mini](https://item.taobao.com/item.htm?id=852057780489&skuId=5813710390565)，¥9.5包邮
- ML307R-DC开发板，实测选用[小蓝鲸ML307R-DC核心板](https://item.taobao.com/item.htm?id=797466121802&skuId=5722077108045)，¥16.3包邮
- [4G FPC天线](https://item.taobao.com/item.htm?id=797466121802&skuId=5722077108045)，¥2，与核心板同购


## 支持的4G模块

- **ML307R-DC / ML307C-DC / ML307A-DC**：型号以 `-DC` 结尾，运行出厂 AT 固件，无需改动即可直接使用。
- 以 `-DL` 结尾的型号不支持；**ML307X 系列**已知与本项目不兼容，请勿选用。

## 硬件连接

ESP32C3 与 ML307R-DC 通过串口（UART）连接，接线如下：

```
┌───────────────────────────────────────────────┐
|                                               |
|   ESP32C3 Super Mini      ML307R-DC核心板     |
| ┌───────────────────┐    ┌─────────────────┐ |
└─┼─ GPIO5 (MODEM_EN) │    │                 │ |
  │       GPIO3 (TX) ─┼───►│ RX              │ |
  │                   │    │             EN ─┼─┘
  │       GPIO4 (RX) ◄┼────┤ TX              │ 
  │                   │    │                 │ 
  │              GND ─┼────┤ GND             │ 
  │                   │    │                 │ 
  │               5V ─┼────┤ VCC (5V)        |
  │                   │    │                 │
  └───────────────────┘    └─────────────────┘
                           │                 │
                           │  SIM卡槽        │
                           │  (插入Nano SIM) │
                           │                 │
                           │  天线接口       │
                           │  (连接4G天线)   │
                           └─────────────────┘
```

### 使用 ESP32-WROOM-32（经典 ESP32）

若改用经典 ESP32 / ESP32-WROOM-32 开发板，接线逻辑完全相同（串口交叉、EN 控制模组电源），仅引脚号不同。固件已通过条件编译自动适配，编译时板型选 `esp32:esp32:esp32` 即可，无需改代码：

```
┌───────────────────────────────────────────────┐
|                                               |
|   ESP32-WROOM-32          ML307R-DC核心板     |
| ┌───────────────────┐    ┌─────────────────┐ |
└─┼─ GPIO4 (MODEM_EN) │    │                 │ |
  │      GPIO17 (TX) ─┼───►│ RX              │ |
  │                   │    │             EN ─┼─┘
  │      GPIO16 (RX) ◄┼────┤ TX              │
  │                   │    │                 │
  │              GND ─┼────┤ GND             │
  │                   │    │                 │
  │               5V ─┼────┤ VCC (5V)        |
  │                   │    │                 │
  └───────────────────┘    └─────────────────┘
                           │                 │
                           │  SIM卡槽        │
                           │  (插入Nano SIM) │
                           │                 │
                           │  天线接口       │
                           │  (连接4G天线)   │
                           └─────────────────┘
```

> 引脚对照（固件自动适配，`code/globals.h` 中按 `CONFIG_IDF_TARGET_*` 条件编译）：
>
> | 信号 | ESP32-C3 | ESP32-WROOM-32 |
> |---|---|---|
> | 模组 TX（MCU 发） | GPIO3 | GPIO17 |
> | 模组 RX（MCU 收） | GPIO4 | GPIO16 |
> | 模组 EN | GPIO5 | GPIO4 |
> | LED | GPIO8 | GPIO2 |
>
> 注意：ESP32-WROOM-32 上模组 EN 接 GPIO4（属上电 strapping 脚），建议在 EN 与 3.3V 之间加 10kΩ 上拉，确保上电瞬间模组处于运行态；若模组不启动，可微调 `modemPowerCycle()` 的断电/上电时序。

可通过USB连接ESP32C3进行编程和供电，正常工作时，可通过网页与模组进行AT通信，方便调试。

## 刷机教程

> 板型：**MakerGO ESP32 C3 SuperMini**  
> FQBN：`esp32:esp32:makergo_c3_supermini:PartitionScheme=huge_app`  
> 串口波特率：115200（日志）/ 460800（烧录）
>
> **重要**：固件体积已超默认分区 ~1.3MB 上限，编译/烧录**必须**带上 `:PartitionScheme=huge_app`（Arduino IDE 中则是 **工具 → 分区方案 → Huge App**）。本项目仅用 NVS 存配置、Web 页面为字符串常量，不依赖 SPIFFS/LittleFS/OTA，切换安全。

### 方式一：命令行烧录（推荐，已验证）

使用 `arduino-cli` 编译并烧录（`arduino-cli` 随 Arduino IDE 自带，路径按机器实际情况调整；所有相关目录**不能含中文路径**，否则编译失败）。

```powershell
# 1. 准备环境（路径按本机实际情况修改）
$env:Path = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources;$env:Path"
$env:ARDUINO_DIRECTORIES_DATA = "D:\dev\arduino_pack"
$env:ARDUINO_DIRECTORIES_USER = "D:\dev\arduino_pack\user"

# 2. 编译（首次约 3-5 分钟，不要提前中断；必须带 huge_app 分区）
arduino-cli compile --fqbn esp32:esp32:makergo_c3_supermini:PartitionScheme=huge_app --build-path "D:\dev\arduino_pack\build" "项目路径\code"

# 3. 烧录（--port 按真实串口修改，如 COM4 / /dev/ttyUSB0）
arduino-cli upload --fqbn esp32:esp32:makergo_c3_supermini:PartitionScheme=huge_app --port COM4 --input-dir "D:\dev\arduino_pack\build" "项目路径\code"

# 4. 查看串口日志（115200）
arduino-cli monitor --port COM4 --config 115200
```

> 依赖库需在 Arduino IDE 中安装：**pdulib 0.5.11**、**ReadyMail 0.4.2**，ESP32 Core 版本 **3.3.10**。

### 方式二：图形化烧录（适合只烧预编译固件）

如果你拿到的是已编译好的 `sms_forwarding_full.bin`（含 bootloader + 分区表 + 固件），可使用浏览器免安装烧录：

1. 用 Chrome / Edge（88+）打开 [ESPConnect](https://thelastoutpostworkshop.github.io/ESPConnect/)
2. 波特率选择 `460800`，点击「连接」并选择 `USB JTAG/serial debug unit` 设备
3. 进入「闪存工具」→「烧录固件」，上传 `sms_forwarding_full.bin`，从地址 `0x0` 开始烧录即可

如需用命令行 `esptool` 烧录预编译固件：

```bash
pip install "esptool>=4.8"

# 全量烧录（推荐，地址 0x0）
esptool --chip esp32c3 --baud 460800 write_flash 0x0 sms_forwarding_full.bin
```

> 若自行用 arduino-cli 编译，构建产物中 `bootloader.bin` 对应 `0x1000`、`partitions.bin` 对应 `0x8000`、固件 `*.ino.bin` 对应 `0x10000`，可直接用上述 `esptool` 命令分地址写入。

### 烧录时 USB 反复闪断怎么办

如果插上 Type-C 后 USB 出现「连上又断了、连上又断了」反复闪断，导致无法正常烧录，可尝试以下两种方法：

- **方法一（手动进入下载模式）**：插上 Type-C 后，**先按住 `BOOT` 键不松手**，再**按一下 `RST` 键**松开，此时设备进入下载模式，即可正常烧录（烧录完成后再松开 `BOOT`）。
- **方法二（快速刷入）**：在设备 USB 刚连接上的瞬间，立刻点「连接 / 烧录」快速刷入，趁其还未断开时完成写入。

### 首次配置

烧录完成后，设备启动会尝试连接 `wifi_config.h` 中的默认 WiFi；若未配置或 20 秒连不上，将自动进入「配置 AP 模式」：

1. 连接热点 `SMS-Forwarding-Setup`（密码见 `wifi_config.h` 的 `AP_PASS`，默认 `12345678`）
2. 浏览器访问 `http://192.168.4.1`，默认账号/密码 `admin / admin123`
3. 进入 **📶 WiFi 设置**，扫描并填入路由器 SSID/密码，保存后设备自动重启联网
4. 联网后通过路由器分配的 IP 访问管理界面，添加并配置推送通道

详见下方「WiFi 配置」章节。

## WiFi 配置

设备启动时会使用保存在 NVS 中的 WiFi 凭据（首次启动为 `wifi_config.h` 里的 `DEFAULT_WIFI_SSID` / `DEFAULT_WIFI_PASS`）尝试连接，连接成功后即可通过局域网 IP 访问网页。

### 配置 AP 模式（首次配网 / 连不上时）

若 **未配置 WiFi** 或 **20 秒内连不上已保存的 WiFi**，设备会自动进入「配置 AP 模式」：

1. 设备广播一个名为 `SMS-Forwarding-Setup` 的热点；
2. 用手机或电脑连上该热点（密码见下方说明），浏览器打开 `http://192.168.4.1`；
3. 登录后进入左侧 **📶 WiFi 设置**，点「扫描附近网络」选一个 WiFi，输入密码后点「保存并连接」；
4. 连接成功后配置 AP 自动关闭，设备恢复正常联网；之后重启会直接使用保存的凭据连接。

> 配置热点密码由 `wifi_config.h` 的 `AP_PASS` 宏控制：`strlen(AP_PASS) >= 8` 时使用该密码，否则为开放网络（无密码）。默认 `AP_PASS "12345678"`，即带密码热点。网页登录认证（`admin/admin123`）独立于热点密码，即使开放网络也只暴露登录页。

### 修改已保存的 WiFi

在网页 **📶 WiFi 设置** 面板中可随时修改要连接的 WiFi 名称/密码，点「保存并连接」即生效；该面板也提供「扫描附近网络」功能，列出附近热点（含信号强度、加密方式），点选即可自动填入 SSID。

## 软件组成

- ESP32C3运行自己的`Arduino`固件，负责连接WiFi和接收ML307R-DC发送过来的短信数据，然后转发到指定HTTP接口或邮箱
- ML307R-DC运行默认的AT固件，不用动

需要在`Arduino IDE`中单独安装这些库：

- **ReadyMail** by Mobizt
- **pdulib** by David Henry

需要在`Arduino IDE`中安装ESP32开发板支持，参考[官方文档](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)，版型选`MakerGO ESP32 C3 SuperMini`。
