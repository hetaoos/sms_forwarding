#include "config_types.h"

const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="theme-color" content="#0f0f0f">
  <title>SMS Forwarding</title>
  <style>
    :root {
      color-scheme: dark;
      --ink: #ededed;
      --body: #a8a8a8;
      --mute: #767676;
      --canvas: #181818;
      --canvas-soft: #0f0f0f;
      --canvas-soft-2: #212121;
      --hairline: #2b2b2b;
      --hairline-strong: #4d4d4d;
      --sidebar-bg: #101010;
      --link: #4c8dff;
      --error: #e5484d;
      --accent: #ededed;
      --accent-fg: #0f0f0f;
      --warning-soft: #33280f;
      --warning-text: #e8b545;
      --sidebar-w: 220px;
      --radius-sm: 6px;
      --radius-md: 8px;
      --radius-pill: 100px;
      --shadow-card: 0 0 0 1px rgba(255,255,255,0.07), 0 1px 2px rgba(0,0,0,0.4);
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    ::-webkit-scrollbar { width: 10px; height: 10px; }
    ::-webkit-scrollbar-track { background: transparent; }
    ::-webkit-scrollbar-thumb { background: #3a3a3a; border-radius: 100px; border: 2px solid var(--canvas); }
    ::-webkit-scrollbar-thumb:hover { background: #4d4d4d; }
    input:-webkit-autofill, input:-webkit-autofill:focus, textarea:-webkit-autofill {
      -webkit-text-fill-color: var(--ink);
      -webkit-box-shadow: 0 0 0 1000px var(--canvas) inset;
      caret-color: var(--ink);
    }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
      font-size: 14px; font-weight: 400; line-height: 1.5;
      color: var(--ink); background: var(--canvas-soft);
      display: flex; min-height: 100vh;
    }

    /* Sidebar */
    .sidebar {
      position: fixed; top: 0; left: 0; bottom: 0; width: var(--sidebar-w);
      background: var(--sidebar-bg); border-right: 1px solid var(--hairline);
      display: flex; flex-direction: column;
      z-index: 100; overflow-y: auto;
    }
    .sidebar-brand { padding: 22px 18px 16px; border-bottom: 1px solid rgba(255,255,255,0.08); }
    .sidebar-brand h2 { font-size: 17px; font-weight: 600; color: #fff; letter-spacing: -0.4px; }
    .sidebar-brand span { font-size: 10px; color: rgba(255,255,255,0.4); display: block; margin-top: 1px; font-family: 'SF Mono','Cascadia Code','JetBrains Mono','Consolas',monospace; }
    .sidebar-nav { flex: 1; padding: 10px; }
    .sidebar-nav a {
      display: flex; align-items: center; gap: 10px; padding: 9px 12px;
      border-radius: var(--radius-sm); color: rgba(255,255,255,0.6);
      font-size: 13px; font-weight: 500; text-decoration: none;
      transition: all 0.12s; margin-bottom: 1px; cursor: pointer;
    }
    .sidebar-nav a:hover { background: rgba(255,255,255,0.07); color: rgba(255,255,255,0.85); }
    .sidebar-nav a.active { background: rgba(255,255,255,0.12); color: #fff; }
    .sidebar-nav a .ico { font-size: 15px; width: 20px; text-align: center; flex-shrink: 0; }
    .sidebar-divider { height: 1px; background: rgba(255,255,255,0.08); margin: 8px 12px; }
    .sidebar-section-label { font-size: 10px; color: rgba(255,255,255,0.3); padding: 4px 16px 6px; text-transform: uppercase; letter-spacing: 0.6px; font-family: 'SF Mono','Cascadia Code','JetBrains Mono','Consolas',monospace; }
    .sidebar-footer { padding: 12px 16px; border-top: 1px solid rgba(255,255,255,0.08); }
    .sidebar-footer .btn { width: 100%; }

    /* Main */
    .main {
      margin-left: var(--sidebar-w); flex: 1; padding: 32px;
      max-width: 780px; width: 100%;
    }
    .page-title { font-size: 22px; font-weight: 600; color: var(--ink); letter-spacing: -0.5px; margin-bottom: 6px; }
    .page-subtitle { font-size: 13px; color: var(--mute); margin-bottom: 24px; }

    /* Card */
    .card { background: var(--canvas); border-radius: var(--radius-md); box-shadow: var(--shadow-card); margin-bottom: 18px; }
    .card-header { padding: 16px 22px 0; font-size: 14px; font-weight: 600; color: var(--ink); letter-spacing: -0.2px; display: flex; align-items: center; gap: 8px; }
    .card-body { padding: 16px 22px 22px; }
    .card-header + .card-body { padding-top: 12px; }

    /* Panel hide/show */
    .panel { display: none; }
    .panel.active { display: block; }

    /* Form */
    .form-group { margin-bottom: 14px; }
    .form-group:last-child { margin-bottom: 0; }
    .form-label { display: block; font-size: 12px; font-weight: 500; color: var(--body); margin-bottom: 4px; letter-spacing: -0.1px; }
    .mail-type-list { display: flex; flex-wrap: wrap; gap: 10px 22px; }
    .mail-type-list .form-label { display: flex; align-items: center; gap: 6px; margin-bottom: 0; font-size: 13px; cursor: pointer; }
    .mail-type-list input[type="checkbox"] { width: 15px; height: 15px; accent-color: var(--ink); }
    .form-input, .form-select, .form-textarea {
      width: 100%; padding: 7px 11px; font-size: 13px; font-family: inherit;
      border: 1px solid var(--hairline); border-radius: var(--radius-sm);
      background: var(--canvas); color: var(--ink);
      transition: border-color 0.15s, box-shadow 0.15s; outline: none;
    }
    .form-input:focus, .form-select:focus, .form-textarea:focus { border-color: var(--ink); box-shadow: 0 0 0 1px var(--ink); }
    .form-select { cursor: pointer; }
    .form-textarea { resize: vertical; min-height: 70px; line-height: 1.5; }
    .form-hint { font-size: 11px; color: var(--mute); margin-top: 3px; line-height: 1.4; }
    .form-warning { font-size: 11px; color: var(--warning-text); background: var(--warning-soft); padding: 9px 12px; border-radius: var(--radius-sm); margin-bottom: 14px; line-height: 1.5; }
    .form-row { display: flex; gap: 14px; }
    .form-row .form-group { flex: 1; }

    /* Buttons */
    .btn {
      display: inline-flex; align-items: center; justify-content: center; gap: 6px;
      padding: 7px 14px; font-size: 13px; font-weight: 500; font-family: inherit;
      border-radius: var(--radius-pill); border: none; cursor: pointer;
      transition: all 0.15s; line-height: 1.4; white-space: nowrap;
    }
    .btn:disabled { opacity: 0.5; cursor: not-allowed; }
    .btn-primary { background: var(--accent); color: var(--accent-fg); }
    .btn-primary:hover { background: #ffffff; }
    .btn-secondary { background: var(--canvas); color: var(--ink); box-shadow: 0 0 0 1px var(--hairline); }
    .btn-secondary:hover { background: var(--canvas-soft-2); }
    .btn-danger { background: var(--error); color: #fff; }
    .btn-danger:hover { background: #c9252d; }
    .btn-sm { padding: 4px 10px; font-size: 12px; border-radius: var(--radius-sm); }
    .btn-white { background: var(--canvas-soft-2); color: var(--ink); box-shadow: 0 0 0 1px var(--hairline); }
    .btn-white:hover { background: var(--hairline); }
    .btn-block { width: 100%; justify-content: center; }
    .btn-save { padding: 10px 20px; font-size: 14px; margin-top: 4px; }

    /* Push Channel */
    .push-channel { border: 1px solid var(--hairline); border-radius: var(--radius-md); padding: 14px; margin-bottom: 10px; background: var(--canvas-soft-2); transition: border-color 0.15s; }
    .push-channel:hover { border-color: var(--hairline-strong); }
    .push-channel-header { display: flex; align-items: center; gap: 8px; margin-bottom: 10px; }
    .push-channel-header label { font-size: 13px; font-weight: 600; color: var(--ink); cursor: pointer; }
    .push-channel-header input[type="checkbox"] { width: 15px; height: 15px; accent-color: var(--ink); }
    .push-channel-body { display: none; }
    .push-channel.enabled .push-channel-body { display: block; }
    .push-channel.enabled { border-color: var(--hairline-strong); background: var(--canvas); }
    .push-channel-body .form-group { margin-bottom: 12px; }
    .push-channel-body .form-group:last-child { margin-bottom: 0; }
    .push-channel-body label { display: block; font-size: 12px; font-weight: 500; color: var(--body); margin-bottom: 4px; letter-spacing: -0.1px; }
    .push-channel-body input[type="text"], .push-channel-body input[type="password"], .push-channel-body select, .push-channel-body textarea {
      width: 100%; padding: 7px 11px; font-size: 13px; font-family: inherit;
      border: 1px solid var(--hairline); border-radius: var(--radius-sm);
      background: var(--canvas); color: var(--ink);
      transition: border-color 0.15s, box-shadow 0.15s; outline: none;
    }
    .push-channel-body input:focus, .push-channel-body select:focus, .push-channel-body textarea:focus { border-color: var(--ink); box-shadow: 0 0 0 1px var(--ink); }
    .push-channel-body select { cursor: pointer; }
    .push-channel-body textarea { resize: vertical; min-height: 60px; line-height: 1.5; }
    .push-type-hint { font-size: 11px; color: var(--body); margin-top: 4px; padding: 8px 12px; background: #2a2a2a; border-radius: var(--radius-sm); font-family: 'SF Mono','Cascadia Code','JetBrains Mono','Consolas',monospace; line-height: 1.5; }

    /* Result Boxes */
    .result-box { margin-top: 12px; padding: 10px 14px; border-radius: var(--radius-sm); display: none; font-size: 12px; line-height: 1.5; }
    .result-success { background: #10241a; color: #63d68e; display: block; }
    .result-error { background: #2a1416; color: #ff7b7f; display: block; }
    .result-loading { background: #2a2113; color: #f5c163; display: block; }
    .result-info { background: #13253a; color: #77b3ff; display: block; }
    .info-table { width: 100%; border-collapse: collapse; margin-top: 4px; font-size: 12px; }
    .info-table td { padding: 5px 8px; border-bottom: 1px solid var(--hairline); }
    .info-table td:first-child { font-weight: 500; color: var(--body); width: 40%; }

    /* Overview */
    .overview-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 14px; }
    .overview-item { background: var(--canvas-soft-2); border-radius: var(--radius-sm); padding: 14px; }
    .overview-item .label { font-size: 10px; color: var(--mute); text-transform: uppercase; letter-spacing: 0.4px; font-family: 'SF Mono','Cascadia Code','JetBrains Mono','Consolas',monospace; margin-bottom: 4px; }
    .overview-item .value { font-size: 14px; font-weight: 600; color: var(--ink); }

    /* Tools */
    .btn-row { display: flex; gap: 8px; flex-wrap: wrap; }
    .btn-row .btn { flex: 1; min-width: 90px; }
    .btn-row + .btn-row { margin-top: 8px; }
    #atLog {
      background: #0a0a0a; color: #50e3c2; font-family: 'SF Mono','Cascadia Code','JetBrains Mono','Consolas',monospace;
      min-height: 130px; max-height: 260px; overflow-y: auto; padding: 12px 14px;
      border-radius: var(--radius-sm); margin-bottom: 10px; font-size: 12px;
      white-space: pre-wrap; word-break: break-all; line-height: 1.5;
    }
    .at-bar { display: flex; gap: 6px; }
    .at-bar input { flex: 1; font-family: 'SF Mono','Cascadia Code','JetBrains Mono','Consolas',monospace; }
    .at-bar .btn { min-width: 60px; }

    /* Responsive */
    @media (max-width: 700px) {
      .sidebar { width: 50px; }
      .sidebar-brand h2 { font-size: 0; }
      .sidebar-brand h2::first-letter { font-size: 16px; }
      .sidebar-brand span, .sidebar-section-label { display: none; }
      .sidebar-nav a { padding: 10px; justify-content: center; }
      .sidebar-nav a span:not(.ico) { display: none; }
      .sidebar-nav a .ico { font-size: 16px; }
      .sidebar-divider { margin: 6px 8px; }
      .sidebar-footer { padding: 8px; }
      .sidebar-footer .btn span { display: none; }
      .sidebar-footer .btn { padding: 6px; font-size: 11px; }
      .main { margin-left: 50px; padding: 18px 14px; }
      :root { --sidebar-w: 50px; }
      .overview-grid { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <aside class="sidebar">
    <div class="sidebar-brand">
      <h2>SMS Forwarding</h2>
      <span>短信转发器</span>
    </div>
    <nav class="sidebar-nav">
      <div class="sidebar-section-label">配置</div>
      <a data-panel="overview" class="active"><span class="ico">🏠</span> <span>系统概览</span></a>
      <a data-panel="account"><span class="ico">🔐</span> <span>账号管理</span></a>
      <a data-panel="wifi"><span class="ico">📶</span> <span>WiFi 设置</span></a>
      <a data-panel="email"><span class="ico">📧</span> <span>邮件通知</span></a>
      <a data-panel="push"><span class="ico">🔗</span> <span>推送通道</span></a>
      <a data-panel="admin"><span class="ico">👤</span> <span>管理员 &amp; 黑名单</span></a>
      <div class="sidebar-divider"></div>
      <div class="sidebar-section-label">工具</div>
      <a data-panel="sendsms"><span class="ico">📤</span> <span>发送短信</span></a>
      <a data-panel="diagnose"><span class="ico">📊</span> <span>模组诊断</span></a>
      <a data-panel="network"><span class="ico">🌐</span> <span>网络测试</span></a>
      <a data-panel="modem"><span class="ico">✈</span> <span>模组控制</span></a>
      <a data-panel="atterm"><span class="ico">💻</span> <span>AT 终端</span></a>
      <a data-panel="log"><span class="ico">📋</span> <span>系统日志</span></a>
    </nav>
    <div class="sidebar-footer">
      <button class="btn btn-white btn-sm btn-block" onclick="switchPanel('account')"><span>修改密码</span> 🔑</button>
    </div>
  </aside>

  <main class="main">

    <!-- ===== Overview ===== -->
    <div class="panel active" id="panel-overview">
      <h1 class="page-title">系统概览</h1>
      <p class="page-subtitle">设备状态与基本信息</p>
      <div class="card">
        <div class="card-header">📡 设备信息</div>
        <div class="card-body">
          <div class="overview-grid">
            <div class="overview-item"><div class="label">Device IP</div><div class="value" id="ovIp">%IP%</div></div>
            <div class="overview-item"><div class="label">WiFi SSID</div><div class="value" id="ovSsid">%WIFI_SSID%</div></div>
            <div class="overview-item"><div class="label">Free Heap</div><div class="value" id="ovHeap">%FREE_HEAP%</div></div>
            <div class="overview-item"><div class="label">Uptime</div><div class="value" id="ovUptime">%UPTIME%</div></div>
          </div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">⚙ 配置状态</div>
        <div class="card-body">
          <table class="info-table">
            <tr><td>模组状态</td><td id="cfgModem">%MODEM_CHECK%</td></tr>
            <tr><td>邮件通知</td><td id="cfgEmail">%SMTP_CHECK%</td></tr>
            <tr><td>推送通道</td><td id="cfgPush">%PUSH_COUNT% 个已启用</td></tr>
            <tr><td>管理员号码</td><td>%ADMIN_PHONE%</td></tr>
          </table>
        </div>
      </div>
      <div class="card">
        <div class="card-header">🔄 系统控制</div>
        <div class="card-body">
          <button class="btn btn-danger" onclick="systemRestart()">重启系统</button>
          <p class="form-hint">重启整个设备（ESP32 + 模组），期间无法接收短信和访问网页，约需 1 分钟恢复</p>
          <div class="result-box" id="sysRestartResult"></div>
        </div>
      </div>
    </div>

    <!-- ===== Account ===== -->
    <div class="panel" id="panel-account">
      <h1 class="page-title">账号管理</h1>
      <p class="page-subtitle">修改 Web 管理界面的登录凭据</p>
      <form action="/save" method="POST" id="mainForm">
      <div class="card">
        <div class="card-header">🔐 登录凭据</div>
        <div class="card-body">
          <div class="form-warning">首次使用请立即修改默认密码！默认: )rawliteral" DEFAULT_WEB_USER " / " DEFAULT_WEB_PASS R"rawliteral(</div>
          <div class="form-row">
            <div class="form-group"><label class="form-label">管理账号</label><input class="form-input" type="text" name="webUser" value="%WEB_USER%" placeholder="admin"></div>
            <div class="form-group"><label class="form-label">管理密码</label><input class="form-input" type="password" name="webPass" value="%WEB_PASS%" placeholder="设置复杂密码"></div>
          </div>
        </div>
      </div>
      <button type="submit" class="btn btn-primary btn-block btn-save">保存配置</button>
      </form>
    </div>

    <!-- ===== WiFi Settings ===== -->
    <div class="panel" id="panel-wifi">
      <h1 class="page-title">WiFi 设置</h1>
      <p class="page-subtitle">配置设备要连接的 WiFi 网络（连接失败时会自动进入配置 AP 模式）</p>
      <div class="card">
        <div class="card-header">📶 当前状态</div>
        <div class="card-body">
          <table class="info-table">
            <tr><td>连接模式</td><td>%WIFI_MODE%</td></tr>
            <tr><td>当前 SSID</td><td>%WIFI_SSID%</td></tr>
            <tr><td>设备地址</td><td id="ovIp2">%IP%</td></tr>
          </table>
          <div class="form-warning" style="display:%AP_WARN_DISPLAY%;">当前处于配置 AP 模式：请用手机/电脑连接热点 <b>%AP_SSID%</b>（密码 <b>%AP_PASS%</b>），再访问设备地址配置 WiFi。</div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">🔑 连接 WiFi</div>
        <div class="card-body">
          <div class="form-group"><label class="form-label">WiFi 名称 (SSID)</label><input class="form-input" type="text" id="wifiSsid" value="%WIFI_SSID%" placeholder="WiFi 名称"></div>
          <div class="form-group"><label class="form-label">WiFi 密码</label><input class="form-input" type="password" id="wifiPass" value="" placeholder="留空表示开放网络"></div>
          <div class="btn-row">
            <button class="btn btn-secondary" onclick="scanWifi()">扫描附近网络</button>
            <button class="btn btn-primary" onclick="saveWifi()">保存并连接</button>
          </div>
          <div class="result-box" id="wifiSaveResult"></div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">📡 可用网络</div>
        <div class="card-body">
          <div id="wifiScanList"><p class="form-hint">点击「扫描附近网络」查看可连接的 WiFi，选择一个即可自动填入名称。</p></div>
        </div>
      </div>
    </div>

    <!-- ===== Email ===== -->
    <div class="panel" id="panel-email">
      <h1 class="page-title">邮件通知</h1>
      <p class="page-subtitle">配置 SMTP 服务器以接收短信邮件通知</p>
      <form action="/save" method="POST" id="mainForm2">
      <div class="card">
        <div class="card-header">📧 SMTP 设置</div>
        <div class="card-body">
          <div class="form-row">
            <div class="form-group"><label class="form-label">SMTP 服务器</label><input class="form-input" type="text" name="smtpServer" value="%SMTP_SERVER%" placeholder="smtp.qq.com"></div>
            <div class="form-group"><label class="form-label">SMTP 端口</label><input class="form-input" type="number" name="smtpPort" value="%SMTP_PORT%" placeholder="465"></div>
          </div>
          <div class="form-row">
            <div class="form-group"><label class="form-label">邮箱账号</label><input class="form-input" type="text" name="smtpUser" value="%SMTP_USER%" placeholder="your@qq.com"></div>
            <div class="form-group"><label class="form-label">密码 / 授权码</label><input class="form-input" type="password" name="smtpPass" value="%SMTP_PASS%" placeholder="授权码"></div>
          </div>
          <div class="form-group"><label class="form-label">接收邮件地址</label><input class="form-input" type="text" name="smtpSendTo" value="%SMTP_SEND_TO%" placeholder="receiver@example.com"></div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">📬 通知类型</div>
        <div class="card-body">
          <p class="form-hint" style="margin-bottom:10px;">勾选允许通过邮件发送的通知类型；未勾选的类型仍会正常执行（转发推送、发送短信、重启设备），只是不发邮件。</p>
          <div class="mail-type-list">
            <label class="form-label"><input type="checkbox" name="mtSms" %MT_SMS_CHECKED%> 短信转发</label>
            <label class="form-label"><input type="checkbox" name="mtStartup" %MT_STARTUP_CHECKED%> 设备启动</label>
            <label class="form-label"><input type="checkbox" name="mtConfig" %MT_CONFIG_CHECKED%> 配置变更</label>
            <label class="form-label"><input type="checkbox" name="mtCmd" %MT_CMD_CHECKED%> 管理员命令结果</label>
            <label class="form-label"><input type="checkbox" name="mtReboot" %MT_REBOOT_CHECKED%> 重启通知</label>
          </div>
        </div>
      </div>
      <button type="submit" class="btn btn-primary btn-block btn-save">保存配置</button>
      </form>
    </div>

    <!-- ===== Push Channels ===== -->
    <div class="panel" id="panel-push">
      <h1 class="page-title">推送通道</h1>
      <p class="page-subtitle">最多 5 个独立推送通道，支持 POST JSON、Bark、钉钉、飞书、PushPlus、Server酱、Gotify、Telegram</p>
      <div class="card">
        <div class="card-header">📌 可用占位符</div>
        <div class="card-body">
          <p class="form-hint" style="margin-bottom:10px;">自定义模板与 GET 请求支持以下占位符，短信到达时自动替换为实际内容：</p>
          <table class="info-table">
            <tr><td>{sender}</td><td>发送者号码（如 +8613800138000）</td></tr>
            <tr><td>{message}</td><td>短信正文内容</td></tr>
            <tr><td>{timestamp}</td><td>短信接收时间（如 2026-01-01 12:00:00）</td></tr>
            <tr><td>{sender_name}</td><td>发送者名称（自动从短信【】/[] 中解析，无则空）</td></tr>
            <tr><td>{verify_code}</td><td>验证码（自动提取短信中首个 4~6 位数字，无则空）</td></tr>
            <tr><td>{sender_display}</td><td>显示名称（优先 sender_name，否则回退到 sender）</td></tr>
          </table>
        </div>
      </div>
      <form action="/save" method="POST" id="mainForm3">
      <div class="card">
        <div class="card-header">🔗 通道配置</div>
        <div class="card-body">
          %PUSH_CHANNELS%
        </div>
      </div>
      <button type="submit" class="btn btn-primary btn-block btn-save">保存配置</button>
      </form>
    </div>

    <!-- ===== Admin & Blacklist ===== -->
    <div class="panel" id="panel-admin">
      <h1 class="page-title">管理员 &amp; 黑名单</h1>
      <p class="page-subtitle">远程控制权限与短信过滤</p>
      <form action="/save" method="POST" id="mainForm4">
      <div class="card">
        <div class="card-header">👤 管理员手机号</div>
        <div class="card-body">
          <div class="form-group">
            <input class="form-input" type="text" name="adminPhone" value="%ADMIN_PHONE%" placeholder="13800138000">
            <p class="form-hint">此号码可通过短信发送远程指令（SMS:号码:内容 发短信、RESET 重启）</p>
          </div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">🚫 号码黑名单</div>
        <div class="card-body">
          <div class="form-group">
            <textarea class="form-textarea" name="numberBlackList" rows="5" placeholder="每行一个号码">%NUMBER_BLACK_LIST%</textarea>
            <p class="form-hint">黑名单号码发来的短信将被自动忽略</p>
          </div>
        </div>
      </div>
      <button type="submit" class="btn btn-primary btn-block btn-save">保存配置</button>
      </form>
    </div>

    <!-- ===== Send SMS ===== -->
    <div class="panel" id="panel-sendsms">
      <h1 class="page-title">发送短信</h1>
      <p class="page-subtitle">通过模组直接发送短信</p>
      <div class="card">
        <div class="card-header">📤 新建短信</div>
        <div class="card-body">
          <form action="/sendsms" method="POST" target="_self">
            <div class="form-group"><label class="form-label">目标号码</label><input class="form-input" type="text" name="phone" placeholder="13800138000" required></div>
            <div class="form-group"><label class="form-label">短信内容</label><textarea class="form-textarea" name="content" placeholder="输入短信内容..." required oninput="updateCount(this)"></textarea><p class="form-hint">已输入 <span id="charCount">0</span> 字符</p></div>
            <button type="submit" class="btn btn-primary" style="padding:9px 18px;">发送短信</button>
          </form>
        </div>
      </div>
    </div>

    <!-- ===== Diagnostics ===== -->
    <div class="panel" id="panel-diagnose">
      <h1 class="page-title">模组诊断</h1>
      <p class="page-subtitle">查询模组状态、SIM 卡与网络信息</p>
      <div class="card">
        <div class="card-header">📊 查询</div>
        <div class="card-body">
          <div class="btn-row"><button class="btn btn-secondary" onclick="queryInfo('ati')">固件信息</button><button class="btn btn-secondary" onclick="queryInfo('signal')">信号质量</button></div>
          <div class="btn-row"><button class="btn btn-secondary" onclick="queryInfo('siminfo')">SIM 卡信息</button><button class="btn btn-secondary" onclick="queryInfo('network')">网络状态</button><button class="btn btn-secondary" onclick="queryInfo('wifi')">WiFi 状态</button></div>
          <div class="result-box" id="queryResult"></div>
        </div>
      </div>
    </div>

    <!-- ===== Network Test ===== -->
    <div class="panel" id="panel-network">
      <h1 class="page-title">网络测试</h1>
      <p class="page-subtitle">通过模组数据连接测试网络连通性</p>
      <div class="card">
        <div class="card-header">🌐 Ping</div>
        <div class="card-body">
          <button class="btn btn-secondary" id="pingBtn" onclick="confirmPing()">Ping 8.8.8.8</button>
          <p class="form-hint">通过模组执行 Ping，消耗极少流量</p>
          <div class="result-box" id="pingResult"></div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">📡 WiFi 控制</div>
        <div class="card-body">
          <button class="btn btn-danger" onclick="wifiRestart()">重启 WiFi</button>
          <p class="form-hint">断开当前 WiFi 连接并重新连接</p>
          <div class="result-box" id="wifiResult"></div>
        </div>
      </div>
    </div>

    <!-- ===== Modem Control ===== -->
    <div class="panel" id="panel-modem">
      <h1 class="page-title">模组控制</h1>
      <p class="page-subtitle">模组重启、飞行模式、信号查询等操作</p>
      <div class="card">
        <div class="card-header">🔄 模组重启</div>
        <div class="card-body">
          <div class="btn-row"><button class="btn btn-danger" onclick="modemAction('restart')">软重启 (AT+CFUN)</button><button class="btn btn-danger" onclick="modemAction('hardreset')">硬重启 (EN引脚)</button></div>
          <p class="form-hint">软重启发送 AT+CFUN=1,1 指令（15s 超时）；硬重启通过 EN 引脚断电后重新上电</p>
          <div class="result-box" id="modemRstResult"></div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">📶 信号查询</div>
        <div class="card-body">
          <div class="btn-row"><button class="btn btn-primary" onclick="modemAction('signal')">查询信号强度</button><button class="btn btn-primary" onclick="modemAction('operator')">查询运营商</button><button class="btn btn-primary" onclick="modemAction('imei')">查询 IMEI</button></div>
          <div class="result-box" id="modemQueryResult"></div>
        </div>
      </div>
      <div class="card">
        <div class="card-header">✈ 飞行模式</div>
        <div class="card-body">
          <div class="btn-row"><button class="btn btn-danger" id="flightBtn" onclick="toggleFlightMode()">切换飞行模式</button><button class="btn btn-secondary" onclick="queryFlightMode()">查询状态</button></div>
          <p class="form-hint">飞行模式开启后模组射频关闭，无法收发短信</p>
          <div class="result-box" id="flightResult"></div>
        </div>
      </div>
    </div>

    <!-- ===== AT Terminal ===== -->
    <div class="panel" id="panel-atterm">
      <h1 class="page-title">AT 指令终端</h1>
      <p class="page-subtitle">直接向模组发送 AT 指令并接收响应</p>
      <div class="card">
        <div class="card-header">💻 终端</div>
        <div class="card-body">
          <div id="atLog">就绪 — 输入 AT 指令开始调试</div>
          <div class="at-bar"><input class="form-input" type="text" id="atCmd" placeholder="AT+CSQ"><button class="btn btn-primary btn-sm" onclick="sendAT()" id="atBtn">发送</button></div>
          <div class="btn-row" style="margin-top:8px;"><button class="btn btn-secondary btn-sm" onclick="clearATLog()">清空日志</button></div>
          <p class="form-hint">直接向模组串口发送指令并接收响应，请谨慎操作</p>
        </div>
      </div>
    </div>

    <!-- ===== System Log ===== -->
    <div class="panel" id="panel-log">
      <h1 class="page-title">系统日志</h1>
      <p class="page-subtitle">实时查看设备串口日志输出 <span id="logStatus" style="color:#4CAF50;">● 自动刷新中</span></p>
      <div class="card">
        <div class="card-header">📋 日志输出</div>
        <div class="card-body">
          <div id="logView" style="background:#101010;color:#c9c9c9;padding:12px;border-radius:8px;font-family:'Cascadia Code','Fira Code',Consolas,monospace;font-size:12px;line-height:1.6;max-height:60vh;overflow-y:auto;white-space:pre-wrap;word-break:break-all;min-height:300px;">加载中...</div>
          <div class="btn-row" style="margin-top:8px;">
            <button class="btn btn-secondary btn-sm" onclick="clearLogUI()">清空显示</button>
            <button class="btn btn-secondary btn-sm" onclick="refreshLog()">手动刷新</button>
            <label style="margin-left:8px;font-size:13px;cursor:pointer;"><input type="checkbox" id="logAuto" checked onchange="toggleLogAuto()"> 自动刷新</label>
          </div>
          <p class="form-hint">显示设备运行时输出的日志信息，每2秒自动刷新。日志最多保留最近120条。</p>
        </div>
      </div>
    </div>

  </main>

  <script>
    // ---- Panel switching ----
    function switchPanel(name) {
      document.querySelectorAll('.panel').forEach(function(p) { p.classList.remove('active'); });
      document.getElementById('panel-' + name).classList.add('active');
      document.querySelectorAll('.sidebar-nav a').forEach(function(a) { a.classList.remove('active'); });
      document.querySelector('.sidebar-nav a[data-panel="' + name + '"]').classList.add('active');
    }
    document.querySelectorAll('.sidebar-nav a').forEach(function(a) {
      a.addEventListener('click', function() { switchPanel(this.dataset.panel); });
    });

    // ---- Push Channel JS ----
    function toggleChannel(idx) {
      var ch = document.getElementById('channel' + idx);
      var cb = document.getElementById('push' + idx + 'en');
      if (cb.checked) ch.classList.add('enabled'); else ch.classList.remove('enabled');
    }
    function updateTypeHint(idx) {
      var sel = document.getElementById('push' + idx + 'type');
      var hint = document.getElementById('hint' + idx);
      var extra = document.getElementById('extra' + idx);
      var custom = document.getElementById('custom' + idx);
      var type = parseInt(sel.value);
      // 按平台预填官方默认接口地址
      // 替换规则：字段为空、或当前值恰好是某平台默认地址时才更新；用户手输的自定义地址不动
      var urlInput = document.getElementById('url' + idx);
      if (urlInput) {
        var defUrls = {4:'https://oapi.dingtalk.com/robot/send',5:'http://www.pushplus.plus/send',8:'https://open.feishu.cn/open-apis/bot/v2/hook/',10:'https://api.telegram.org'};
        var urlPhs = {1:'http://your-server.com/api',2:'https://api.day.app/你的Key（或自建服务器地址）',3:'http://your-server.com/api',6:'留空将自动用 SendKey 拼接官方接口',7:'http://your-server.com/api',9:'https://你的Gotify服务器地址',11:'留空使用 https://api.mailgun.net（EU 区域填 https://api.eu.mailgun.net）'};
        var cur = urlInput.value;
        var isDefaultVal = false;
        for (var k in defUrls) { if (defUrls[k] === cur) { isDefaultVal = true; break; } }
        if ((!cur || isDefaultVal) && defUrls[type]) urlInput.value = defUrls[type];
        urlInput.placeholder = urlPhs[type] || 'http://your-server.com/api 或 webhook地址';
      }
      extra.style.display = 'none'; custom.style.display = 'none';
      document.getElementById('key1label' + idx).innerText = '参数 1';
      document.getElementById('key2label' + idx).innerText = '参数 2';
      document.getElementById('key1' + idx).placeholder = '';
      document.getElementById('key2' + idx).placeholder = '';
      var kg = document.getElementById('key2group' + idx);
      if (kg) kg.style.display = 'none';
      ['key3','key4','key5'].forEach(function(k) {
        var g = document.getElementById(k + 'group' + idx);
        if (g) g.style.display = 'none';
      });
      var bl = document.getElementById('bodylabel' + idx);
      if (bl) bl.innerText = '请求体模板（使用 {sender} {message} {timestamp} {sender_name} {verify_code} {sender_display} 占位符）';
      if (type == 1) hint.innerHTML = 'POST JSON<br>{<br>&nbsp;"sender":"+8613800138000",<br>&nbsp;"message":"短信内容",<br>&nbsp;"timestamp":"2026-01-01 12:00:00",<br>&nbsp;"sender_name":"发送者名称",<br>&nbsp;"verify_code":"123456",<br>&nbsp;"sender_display":"显示名称"<br>}';
      else if (type == 2) hint.innerHTML = 'Bark (iOS)<br>POST {"title":"标题或验证码","body":"短信内容（含发送者/验证码）"}';
      else if (type == 3) hint.innerHTML = 'GET 请求<br>URL?sender=xxx&message=xxx&timestamp=xxx&sender_name=xxx&verify_code=xxx&sender_display=xxx';
      else if (type == 4) { hint.innerHTML = '钉钉机器人<br>填写 Webhook 地址，加签需填 Secret（详见上方占位符说明）'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Secret（加签密钥，可选）'; document.getElementById('key1'+idx).placeholder='SEC...'; }
      else if (type == 5) { hint.innerHTML = 'PushPlus<br>填写 Token，URL 留空使用默认'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Token'; document.getElementById('key1'+idx).placeholder='pushplus token'; if(kg)kg.style.display='block'; document.getElementById('key2label'+idx).innerText='发送渠道'; document.getElementById('key2'+idx).placeholder='wechat / extension / app'; }
      else if (type == 6) { hint.innerHTML = 'Server酱<br>填写 SendKey，URL 留空使用默认（标题含验证码/发送者，正文含内容）'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='SendKey'; document.getElementById('key1'+idx).placeholder='SCT...'; }
      else if (type == 7) { hint.innerHTML = '自定义模板<br>可使用占位符：{sender} {message} {timestamp} {sender_name} {verify_code} {sender_display}'; custom.style.display='block'; }
      else if (type == 8) { hint.innerHTML = '飞书机器人<br>已预填官方地址，需在其末尾拼接你的 Hook Token；签名验证另填 Secret（详见上方占位符说明）'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Secret（签名密钥，可选）'; document.getElementById('key1'+idx).placeholder='飞书签名密钥'; }
      else if (type == 9) { hint.innerHTML = 'Gotify<br>填写服务器地址 + 应用 Token（标题含验证码/发送者，正文含内容）'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Token（应用 Token）'; document.getElementById('key1'+idx).placeholder='A...'; }
      else if (type == 10) { hint.innerHTML = 'Telegram Bot<br>Chat ID（参数1）+ Bot Token（参数2），消息含发送者/验证码/内容'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='Chat ID'; document.getElementById('key1'+idx).placeholder='123456789'; if(kg)kg.style.display='block'; document.getElementById('key2label'+idx).innerText='Bot Token'; document.getElementById('key2'+idx).placeholder='12345678:ABC...'; }
      else if (type == 11) { hint.innerHTML = 'Mailgun 邮件 API<br>POST &lt;基址&gt;/v3/&lt;域名&gt;/messages，Basic 认证 api:API Key<br>URL 留空用 https://api.mailgun.net，EU 区域填 https://api.eu.mailgun.net<br>参数1=API Key，参数2=域名，参数3=收件人（多个用逗号或分号分隔）<br>参数4=发件人（留空为 SMS Notification &lt;sms@域名&gt;）<br>参数5=标题模板（留空用默认标题）<br>正文模板为完整 HTML（留空用默认 HTML 邮件正文）<br>占位符：{sender} {sender_name} {verify_code} {timestamp} {message} {sender_display}'; extra.style.display='block'; document.getElementById('key1label'+idx).innerText='API Key'; document.getElementById('key1'+idx).placeholder='key-xxxxxxxx'; if(kg)kg.style.display='block'; document.getElementById('key2label'+idx).innerText='域名'; document.getElementById('key2'+idx).placeholder='mg.example.com'; var g3=document.getElementById('key3group'+idx); if(g3)g3.style.display='block'; document.getElementById('key3label'+idx).innerText='收件人'; document.getElementById('key3'+idx).placeholder='you@example.com（多个用逗号分隔）'; var g4=document.getElementById('key4group'+idx); if(g4)g4.style.display='block'; document.getElementById('key4label'+idx).innerText='发件人（可选）'; document.getElementById('key4'+idx).placeholder='留空用 SMS Notification <sms@域名>'; var g5=document.getElementById('key5group'+idx); if(g5)g5.style.display='block'; document.getElementById('key5label'+idx).innerText='标题模板（可选）'; document.getElementById('key5'+idx).placeholder='留空用默认标题，可用 {sender_name} {verify_code}'; custom.style.display='block'; bl.innerText='正文模板（完整 HTML，留空用默认 HTML 邮件正文；占位符 {sender} {sender_name} {verify_code} {timestamp} {message} {sender_display}）'; }
    }
    document.addEventListener('DOMContentLoaded', function() {
      for (var i = 0; i < 5; i++) { toggleChannel(i); updateTypeHint(i); }
    });

    // ---- Send SMS ----
    function updateCount(el) { document.getElementById('charCount').textContent = el.value.length; }

    // ---- Query ----
    function queryInfo(type) {
      var r = document.getElementById('queryResult');
      r.className = 'result-box result-loading'; r.textContent = '查询中...';
      fetch('/query?type=' + type).then(function(rr){return rr.json()}).then(function(d){
        if(d.success){r.className='result-box result-info';r.innerHTML=d.message;}
        else{r.className='result-box result-error';r.innerHTML='查询失败: '+d.message;}
      }).catch(function(e){r.className='result-box result-error';r.textContent='请求失败: '+e;});
    }

    // ---- Ping ----
    function confirmPing(){if(confirm('确定要执行 Ping 吗？将消耗少量流量。'))doPing();}
    function doPing(){
      var b=document.getElementById('pingBtn'),r=document.getElementById('pingResult');
      b.disabled=true;b.textContent='Pinging...';
      r.className='result-box result-loading';r.textContent='正在 Ping 8.8.8.8（最长 30 秒）...';
      fetch('/ping',{method:'POST'}).then(function(rr){return rr.json()}).then(function(d){
        b.disabled=false;b.textContent='Ping 8.8.8.8';
        if(d.success){r.className='result-box result-success';r.innerHTML='Ping 成功 — '+d.message;}
        else{r.className='result-box result-error';r.innerHTML='Ping 失败 — '+d.message;}
      }).catch(function(e){b.disabled=false;b.textContent='Ping 8.8.8.8';r.className='result-box result-error';r.textContent='请求失败: '+e;});
    }

    // ---- System Control ----
    var sysReloadTimer = null;
    function scheduleSysReload(){
      if (sysReloadTimer) return;
      var left = 30, r = document.getElementById('sysRestartResult');
      r.className = 'result-box result-success';
      r.textContent = '系统重启中，' + left + ' 秒后自动刷新页面...';
      sysReloadTimer = setInterval(function(){
        left--;
        if(left <= 0){ clearInterval(sysReloadTimer); location.reload(); }
        else r.textContent = '系统重启中，' + left + ' 秒后自动刷新页面...';
      }, 1000);
    }
    function systemRestart(){
      if(!confirm('确定要重启整个系统吗？重启期间将无法接收短信和访问网页。'))return;
      var r=document.getElementById('sysRestartResult');
      r.className='result-box result-loading';r.textContent='正在发送重启请求...';
      fetch('/system?action=restart').then(function(rr){return rr.json()}).then(function(d){
        scheduleSysReload();
      }).catch(function(e){
        // 重启导致连接中断属正常现象
        scheduleSysReload();
      });
    }

    // ---- WiFi Control ----
    function wifiRestart(){
      if(!confirm('确定要重启WiFi吗？网页将暂时不可用。'))return;
      var r=document.getElementById('wifiResult');
      r.className='result-box result-loading';r.textContent='WiFi 重启中（约5秒）...';
      fetch('/wifi?action=restart').then(function(rr){return rr.json()}).then(function(d){
        r.className=d.success?'result-box result-success':'result-box result-error';
        r.textContent=d.message;
      }).catch(function(e){r.className='result-box result-error';r.textContent='请求失败: '+e;});
    }

    // ---- WiFi 扫描与连接 ----
    function escapeHtml(s){return String(s).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});}
    function scanWifi(){
      var r=document.getElementById('wifiScanList');
      r.innerHTML='<p class="form-hint">扫描中...</p>';
      fetch('/wifi?action=scan').then(function(rr){return rr.json()}).then(function(d){
        if(!d.success){r.innerHTML='<p class="form-hint">扫描失败</p>';return;}
        if(d.count===0){r.innerHTML='<p class="form-hint">未发现任何网络，请确认设备附近有 WiFi 信号</p>';return;}
        var html='<table class="info-table">';
        for(var i=0;i<d.networks.length;i++){
          var n=d.networks[i];
          var lock=n.enc!==0?'🔒':'🔓';
          var sig=n.rssi>=-50?'极强':n.rssi>=-60?'很好':n.rssi>=-70?'良好':n.rssi>=-80?'一般':'较弱';
          var ssid=n.ssid; if(!ssid)ssid='(隐藏网络)';
          html+='<tr style="cursor:pointer" onclick="pickWifi(\''+escapeJsAttr(ssid)+'\')"><td>'+lock+' '+escapeHtml(ssid)+'</td><td>信道 '+n.chan+'</td><td>'+n.rssi+' dBm ('+sig+')</td></tr>';
        }
        html+='</table>';
        r.innerHTML=html;
      }).catch(function(e){r.innerHTML='<p class="form-hint">请求失败: '+e+'</p>';});
    }
    function escapeJsAttr(s){return String(s).replace(/\\/g,'\\\\').replace(/'/g,"\\'").replace(/"/g,'\\"');}
    function pickWifi(ssid){
      document.getElementById('wifiSsid').value=ssid;
      document.getElementById('wifiPass').focus();
      window.scrollTo({top:0,behavior:'smooth'});
    }
    function saveWifi(){
      var ssid=document.getElementById('wifiSsid').value.trim();
      var pass=document.getElementById('wifiPass').value;
      var r=document.getElementById('wifiSaveResult');
      if(!ssid){r.className='result-box result-error';r.textContent='请输入 WiFi 名称 (SSID)';return;}
      r.className='result-box result-loading';r.textContent='正在保存并连接 '+ssid+' ...';
      var fd=new URLSearchParams();
      fd.append('action','save');
      fd.append('ssid',ssid);
      fd.append('pass',pass);
      fetch('/wifi',{method:'POST',body:fd}).then(function(rr){return rr.json()}).then(function(d){
        r.className=d.success?'result-box result-success':'result-box result-error';
        r.textContent=d.message+(d.success?' 若成功，请连接回你的 WiFi 并刷新页面查看新地址':'');
      }).catch(function(e){
        // 连接成功会导致 AP 断开、页面失联，属正常现象
        r.className='result-box result-info';
        r.textContent='已提交，正在重连 WiFi；若成功请连接回你的 WiFi 并刷新页面查看新地址';
      });
    }

    // ---- Flight Mode ----
    function queryFlightMode(){
      var r=document.getElementById('flightResult');
      r.className='result-box result-loading';r.textContent='查询中...';
      fetch('/flight?action=query').then(function(rr){return rr.json()}).then(function(d){
        if(d.success){r.className='result-box result-info';r.innerHTML=d.message;}
        else{r.className='result-box result-error';r.innerHTML='查询失败: '+d.message;}
      }).catch(function(e){r.className='result-box result-error';r.textContent='请求失败: '+e;});
    }
    function toggleFlightMode(){
      if(!confirm('确定要切换飞行模式吗？'))return;
      var b=document.getElementById('flightBtn'),r=document.getElementById('flightResult');
      b.disabled=true;r.className='result-box result-loading';r.textContent='切换中...';
      fetch('/flight?action=toggle').then(function(rr){return rr.json()}).then(function(d){
        b.disabled=false;
        if(d.success){r.className='result-box result-success';r.innerHTML=d.message;}
        else{r.className='result-box result-error';r.innerHTML='切换失败: '+d.message;}
      }).catch(function(e){b.disabled=false;r.className='result-box result-error';r.textContent='请求失败: '+e;});
    }

    // ---- Modem Control ----
    function modemAction(action){
      var names={'restart':'软重启','hardreset':'硬重启','signal':'信号查询','operator':'运营商查询','imei':'IMEI查询'};
      var name=names[action]||action;
      var resultEl=null;
      if(action==='restart'||action==='hardreset') resultEl=document.getElementById('modemRstResult');
      else resultEl=document.getElementById('modemQueryResult');
      if(action==='hardreset'){
        if(!confirm('硬重启将断电重启模组，确定继续？'))return;
        resultEl.className='result-box result-loading';resultEl.textContent='硬重启中（约10秒）...';
        fetch('/modem?action=hardreset').then(function(rr){return rr.json()}).then(function(d){
          resultEl.className='result-box result-success';resultEl.textContent=d.message+' — 稍后请手动查询信号确认恢复';
        }).catch(function(e){resultEl.className='result-box result-error';resultEl.textContent='请求失败: '+e;});
        return;
      }
      resultEl.className='result-box result-loading';resultEl.textContent=name+'中...';
      fetch('/modem?action='+action).then(function(rr){return rr.json()}).then(function(d){
        if(d.success){resultEl.className='result-box result-success';resultEl.innerHTML=name+'成功: '+d.message;}
        else{resultEl.className='result-box result-error';resultEl.innerHTML=name+'失败: '+d.message;}
      }).catch(function(e){resultEl.className='result-box result-error';resultEl.textContent='请求失败: '+e;});
    }

    // ---- AT Terminal ----
    function addLog(msg,type){
      type=type||'resp';var log=document.getElementById('atLog'),div=document.createElement('div'),b=document.createElement('b');
      if(type==='user'){b.style.color='#fff';b.textContent='> ';}
      else if(type==='error'){b.style.color='#f44336';b.textContent='! ';}
      else{b.style.color='#50e3c2';b.textContent='';}
      div.appendChild(b);div.appendChild(document.createTextNode(msg));
      log.appendChild(div);log.scrollTop=log.scrollHeight;
    }
    function sendAT(){
      var inp=document.getElementById('atCmd'),cmd=inp.value.trim();if(!cmd)return;
      var btn=document.getElementById('atBtn');btn.disabled=true;btn.textContent='...';
      addLog(cmd,'user');inp.value='';
      fetch('/at?cmd='+encodeURIComponent(cmd)).then(function(rr){return rr.json()}).then(function(d){
        addLog(d.message,d.success?'resp':'error');
      }).catch(function(e){addLog('网络错误: '+e,'error')}).finally(function(){btn.disabled=false;btn.textContent='发送';});
    }
    function clearATLog(){var l=document.getElementById('atLog');l.innerHTML='';addLog('日志已清空','resp');}
    document.getElementById('atCmd').addEventListener('keydown',function(e){if(e.key==='Enter')sendAT();});

    // ---- Log Viewer ----
    var logTimer = null;
    function startLogPoll() {
      if (logTimer) return;
      logTimer = setInterval(refreshLog, 2000);
    }
    function stopLogPoll() {
      if (logTimer) { clearInterval(logTimer); logTimer = null; }
    }
    function toggleLogAuto() {
      if (document.getElementById('logAuto').checked) startLogPoll();
      else stopLogPoll();
    }
    function clearLogUI() { document.getElementById('logView').textContent = ''; }
    function refreshLog() {
      var el = document.getElementById('logView');
      fetch('/log').then(function(r) { return r.json(); }).then(function(lines) {
        if (!Array.isArray(lines)) return;
        el.textContent = lines.join('\n');
        el.scrollTop = el.scrollHeight;
      }).catch(function() { if (el.textContent === '加载中...') el.textContent = '无法获取日志'; });
    }
    var _origSwitchPanel = switchPanel;
    switchPanel = function(name) {
      _origSwitchPanel(name);
      if (name === 'log') { refreshLog(); startLogPoll(); }
      else stopLogPoll();
    };
    document.addEventListener('DOMContentLoaded', function() { refreshLog(); startLogPoll(); });
  </script>
</body>
</html>
)rawliteral";
