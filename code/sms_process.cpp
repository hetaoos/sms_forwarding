#include "sms_process.h"
#include "web_handlers.h"
#include "modem.h"
#include "web_handlers.h"
#include "push.h"
#include "web_handlers.h"

// 初始化长短信缓存
void initConcatBuffer() {
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    concatBuffer[i].inUse = false;
    concatBuffer[i].receivedParts = 0;
    concatBuffer[i].firstPartTime = 0;
    concatBuffer[i].lastPartTime = 0;
    for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
      concatBuffer[i].parts[j].valid = false;
      concatBuffer[i].parts[j].text = "";
    }
  }
}

// 查找或创建长短信缓存槽位
int findOrCreateConcatSlot(int refNumber, const char* sender, int totalParts) {
  // 先查找是否已存在
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].inUse && 
        concatBuffer[i].refNumber == refNumber &&
        concatBuffer[i].sender.equals(sender)) {
      return i;
    }
  }
  
  // 查找空闲槽位
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (!concatBuffer[i].inUse) {
      concatBuffer[i].inUse = true;
      concatBuffer[i].refNumber = refNumber;
      concatBuffer[i].sender = String(sender);
      concatBuffer[i].totalParts = totalParts;
      concatBuffer[i].receivedParts = 0;
      concatBuffer[i].firstPartTime = millis();
      concatBuffer[i].lastPartTime = millis();
      for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
        concatBuffer[i].parts[j].valid = false;
        concatBuffer[i].parts[j].text = "";
      }
      return i;
    }
  }
  
  // 没有空闲槽位，查找最老的槽位覆盖
  int oldestSlot = 0;
  unsigned long oldestTime = concatBuffer[0].firstPartTime;
  for (int i = 1; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].firstPartTime < oldestTime) {
      oldestTime = concatBuffer[i].firstPartTime;
      oldestSlot = i;
    }
  }
  
  // 覆盖最老的槽位
  logCaptureLn(String("⚠️ 长短信缓存已满，覆盖最老的槽位"));
  concatBuffer[oldestSlot].inUse = true;
  concatBuffer[oldestSlot].refNumber = refNumber;
  concatBuffer[oldestSlot].sender = String(sender);
  concatBuffer[oldestSlot].totalParts = totalParts;
  concatBuffer[oldestSlot].receivedParts = 0;
  concatBuffer[oldestSlot].firstPartTime = millis();
  concatBuffer[oldestSlot].lastPartTime = millis();
  for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
    concatBuffer[oldestSlot].parts[j].valid = false;
    concatBuffer[oldestSlot].parts[j].text = "";
  }
  return oldestSlot;
}

// 合并长短信各分段
String assembleConcatSms(int slot) {
  String result = "";
  String missing = "";
  // 分段数超过缓存上限时只合并能装下的部分，避免越界读取
  int total = concatBuffer[slot].totalParts;
  if (total > MAX_CONCAT_PARTS) total = MAX_CONCAT_PARTS;
  for (int i = 0; i < total; i++) {
    if (concatBuffer[slot].parts[i].valid) {
      result += concatBuffer[slot].parts[i].text;
    } else {
      result += "[缺失分段" + String(i + 1) + "]";
      if (missing.length() > 0) missing += ",";
      missing += String(i + 1);
    }
  }
  if (missing.length() > 0) {
    logCaptureF("⚠️ 长短信缺少分段: %s（参考号 %d，共 %d 段）\n",
                missing.c_str(), concatBuffer[slot].refNumber, concatBuffer[slot].totalParts);
  }
  return result;
}

// 清空长短信槽位
void clearConcatSlot(int slot) {
  concatBuffer[slot].inUse = false;
  concatBuffer[slot].receivedParts = 0;
  concatBuffer[slot].lastPartTime = 0;
  concatBuffer[slot].sender = "";
  concatBuffer[slot].timestamp = "";
  for (int j = 0; j < MAX_CONCAT_PARTS; j++) {
    concatBuffer[slot].parts[j].valid = false;
    concatBuffer[slot].parts[j].text = "";
  }
}

// 检查长短信超时并转发
void checkConcatTimeout() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_CONCAT_MESSAGES; i++) {
    if (concatBuffer[i].inUse) {
      // 空闲超时（分段迟迟不来）或总超时（分段慢慢续到，防止槽位被长期占用）
      bool idleTimeout = (now - concatBuffer[i].lastPartTime >= CONCAT_TIMEOUT_MS);
      bool hardTimeout = (now - concatBuffer[i].firstPartTime >= CONCAT_MAX_WAIT_MS);
      if (idleTimeout || hardTimeout) {
        logCaptureLn(String("⏰ 长短信超时，强制转发不完整消息"));
        logCaptureF("  参考号: %d, 已收到: %d/%d\n", 
                      concatBuffer[i].refNumber,
                      concatBuffer[i].receivedParts,
                      concatBuffer[i].totalParts);
        
        // 合并已收到的分段
        String fullText = assembleConcatSms(i);
        
        // 处理短信内容
        processSmsContent(concatBuffer[i].sender.c_str(), 
                         fullText.c_str(), 
                         concatBuffer[i].timestamp.c_str());
        
        // 清空槽位
        clearConcatSlot(i);
      }
    }
  }
}

// 读取串口一行（含回车换行），返回行字符串，无新行时返回空
String readSerialLine(HardwareSerial& port) {
  static char lineBuf[SERIAL_BUFFER_SIZE];
  static int linePos = 0;

  while (port.available()) {
    char c = port.read();
    if (c == '\n') {
      lineBuf[linePos] = 0;
      String res = String(lineBuf);
      linePos = 0;
      return res;
    } else if (c != '\r') {  // 跳过\r
      if (linePos < SERIAL_BUFFER_SIZE - 1)
        lineBuf[linePos++] = c;
      else
        linePos = 0;  //超长报错保护，重头计
    }
  }
  return "";
}

// 检查字符串是否为有效的十六进制PDU数据
bool isHexString(const String& str) {
  if (str.length() == 0) return false;
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
      return false;
    }
  }
  return true;
}

// 检查发送者是否在号码黑名单中
bool isInNumberBlackList(const char* sender) {
  if (config.numberBlackList.length() == 0) return false;

  String originalSender = String(sender);
  bool has86 = originalSender.startsWith("+86");
  String strippedSender = has86 ? originalSender.substring(3) : "";

  int listLen = (int)config.numberBlackList.length();

  int start = 0;
  while (start <= listLen) {
    int end = config.numberBlackList.indexOf('\n', start);
    if (end == -1) end = listLen;

    String line = config.numberBlackList.substring(start, end);
    line.trim();

    if (line.length() > 0 && (line.equals(originalSender) || (has86 && line.equals(strippedSender)))) {
      return true;
    }

    start = end + 1;
  }

  return false;
}

// 检查发送者是否为管理员
bool isAdmin(const char* sender) {
  if (config.adminPhone.length() == 0) return false;
  
  // 去除可能的国际区号前缀进行比较
  String senderStr = String(sender);
  String adminStr = config.adminPhone;
  
  // 去除+86前缀
  if (senderStr.startsWith("+86")) {
    senderStr = senderStr.substring(3);
  }
  if (adminStr.startsWith("+86")) {
    adminStr = adminStr.substring(3);
  }
  
  return senderStr.equals(adminStr);
}

// 处理管理员命令
void processAdminCommand(const char* sender, const char* text) {
  String cmd = String(text);
  cmd.trim();
  
  logCaptureLn(String("处理管理员命令: " + cmd));
  
  // 处理 SMS:号码:内容 命令
  if (cmd.startsWith("SMS:")) {
    int firstColon = cmd.indexOf(':');
    int secondColon = cmd.indexOf(':', firstColon + 1);
    
    if (secondColon > firstColon + 1) {
      String targetPhone = cmd.substring(firstColon + 1, secondColon);
      String smsContent = cmd.substring(secondColon + 1);
      
      targetPhone.trim();
      smsContent.trim();
      
      logCaptureLn(String("目标号码: " + targetPhone));
      logCaptureLn(String("短信内容: " + smsContent));

      // 发短信（最长十几秒）与结果邮件都走异步队列：
      // 本函数在 URC 回调中执行，必须立刻返回，否则会一直占着串口读取
      if (!notifyQueueAdminSms(targetPhone.c_str(), smsContent.c_str(), cmd.c_str())) {
        logCaptureLn(String("⚠️ 管理员短信命令未进入队列（队列已满）"));
      }
    } else {
      logCaptureLn(String("SMS命令格式错误"));
      String inner = "<div style=\"padding:16px 20px;font-size:14px;color:#333;line-height:1.6;\">SMS命令格式错误，正确格式: <code style=\"background:#f4f6f8;padding:2px 6px;border-radius:4px;\">SMS:号码:内容</code></div>";
      notifyQueueEmail("命令执行失败", buildMailHtml("❌ 命令执行失败", inner).c_str(), MAIL_BODY_HTML);
    }
  }
  // 处理 RESET 命令
  else if (cmd.equals("RESET")) {
    logCaptureLn(String("执行RESET命令"));

    // 重启同样交给队列：先发通知邮件（发不出也会在超时后继续），再重启模组与 ESP32
    if (!notifyQueueReboot()) {
      logCaptureLn(String("⚠️ 重启命令未进入队列，改为立即重启"));
      resetModule();
      delay(1000);
      ESP.restart();
    }
  }
  else {
    logCaptureLn(String("未知命令: " + cmd));
  }
}

// 处理最终的短信内容（管理员命令检查和转发）
void processSmsContent(const char* sender, const char* text, const char* timestamp) {
  logCaptureLn(String("=== 处理短信内容 ==="));
  logCaptureLn(String("发送者: " + String(sender)));
  logCaptureLn(String("时间戳: " + String(timestamp)));
  logCaptureLn(String("内容: " + String(text)));
  logCaptureLn(String("===================="));

  // 检查是否在号码黑名单中
  if (isInNumberBlackList(sender)) {
    logCaptureLn(String("发送者在号码黑名单中，忽略该短信"));
    return;
  }

  // 检查是否为管理员命令
  if (isAdmin(sender)) {
    logCaptureLn(String("收到管理员短信，检查命令..."));
    String smsText = String(text);
    smsText.trim();
    
    // 检查是否为命令格式
    if (smsText.startsWith("SMS:") || smsText.equals("RESET")) {
      processAdminCommand(sender, text);
      // 命令已处理，不再发送普通通知邮件
      return;
    }
  }

  // 推送与邮件都是网络慢操作（单条最坏可达数十秒），这里只入队、立即返回，
  // 真正的发送由 loop() 里的 processNotifyQueue() 分片推进；
  // 邮件标题/HTML 正文（含验证码、发件人名称）在发送阶段由 push.cpp 构建。
  if (!notifyQueueSms(sender, text, timestamp)) {
    logCaptureLn(String("⚠️ 本次短信未进入通知队列（队列已满或未配置任何出口）"));
  }
}

// URC 接收状态机（跨调用保持）
static enum { IDLE,
              WAIT_PDU } urcState = IDLE;

// 处理从模组收到的一行
static void handleModemLine(const String& line) {
  // 打印到调试串口
  logCaptureLn(String("Debug> " + line));

  if (urcState == IDLE) {
    // 检测到短信上报URC头
    if (line.startsWith("+CMT:")) {
      logCaptureLn(String("检测到+CMT，等待PDU数据..."));
      urcState = WAIT_PDU;
    }
  } else if (urcState == WAIT_PDU) {
    // 又来一个新的 +CMT，说明上一条的 PDU 没收到（串口丢数据），继续等下一条
    if (line.startsWith("+CMT:")) {
      logCaptureLn(String("⚠️ 上一条 +CMT 的 PDU 未收到，继续等待下一条"));
      return;
    }

    // 如果是十六进制字符串，认为是PDU数据
    if (isHexString(line)) {
      logCaptureLn(String("收到PDU数据: " + line));
      logCaptureLn(String("PDU长度: " + String(line.length()) + " 字符"));
      
      // 解析PDU
      if (!pdu.decodePDU(line.c_str())) {
        logCaptureLn(String("❌ PDU解析失败！"));
      } else {
        logCaptureLn(String("✓ PDU解析成功"));
        logCaptureLn(String("=== 短信内容 ==="));
        logCaptureLn(String("发送者: " + String(pdu.getSender())));
        logCaptureLn(String("时间戳: " + String(pdu.getTimeStamp())));
        logCaptureLn(String("内容: " + String(pdu.getText())));
        
        // 获取长短信信息
        int* concatInfo = pdu.getConcatInfo();
        int refNumber = concatInfo[0];
        int partNumber = concatInfo[1];
        int totalParts = concatInfo[2];
        
        logCaptureF("长短信信息: 参考号=%d, 当前=%d, 总计=%d\n", refNumber, partNumber, totalParts);
        logCaptureLn(String("==============="));

        // 判断是否为长短信
        if (totalParts > 1 && partNumber > 0) {
          // 这是长短信的一部分
          logCaptureF("📧 收到长短信分段 %d/%d\n", partNumber, totalParts);

          // 分段号/总段数异常（PDU 损坏时会出现）时不要污染缓存，按普通短信处理
          if (partNumber > totalParts || totalParts > MAX_CONCAT_PARTS || partNumber > MAX_CONCAT_PARTS) {
            logCaptureF("⚠️ 分段号异常(%d/%d，上限%d)，按普通短信处理\n",
                        partNumber, totalParts, MAX_CONCAT_PARTS);
            processSmsContent(pdu.getSender(), pdu.getText(), pdu.getTimeStamp());
          } else {
            // 查找或创建缓存槽位
            int slot = findOrCreateConcatSlot(refNumber, pdu.getSender(), totalParts);

            // 存储该分段（partNumber从1开始，数组从0开始）
            int partIndex = partNumber - 1;
            if (partIndex >= 0 && partIndex < MAX_CONCAT_PARTS) {
              if (!concatBuffer[slot].parts[partIndex].valid) {
                concatBuffer[slot].parts[partIndex].valid = true;
                concatBuffer[slot].parts[partIndex].text = String(pdu.getText());
                concatBuffer[slot].receivedParts++;
                // 收到新分段就刷新空闲计时，避免后到的分段还没到就被超时转发
                concatBuffer[slot].lastPartTime = millis();

                // 如果是第一个收到的分段，保存时间戳
                if (concatBuffer[slot].receivedParts == 1) {
                  concatBuffer[slot].timestamp = String(pdu.getTimeStamp());
                }

                logCaptureF("  已缓存分段 %d，当前已收到 %d/%d\n",
                             partNumber,
                             concatBuffer[slot].receivedParts,
                             totalParts);
              } else {
                logCaptureF("  ⚠️ 分段 %d 已存在，跳过\n", partNumber);
              }
            }

            // 检查是否已收齐所有分段
            if (concatBuffer[slot].receivedParts >= totalParts) {
              logCaptureLn(String("✅ 长短信已收齐，开始合并转发"));

              // 合并所有分段
              String fullText = assembleConcatSms(slot);

              // 处理完整短信
              processSmsContent(concatBuffer[slot].sender.c_str(),
                               fullText.c_str(),
                               concatBuffer[slot].timestamp.c_str());

              // 清空槽位
              clearConcatSlot(slot);
            }
          }
        } else {
          // 普通短信，直接处理
          processSmsContent(pdu.getSender(), pdu.getText(), pdu.getTimeStamp());
        }
      }
      
      // 返回IDLE状态
      urcState = IDLE;
    } 
    // 如果是其他内容（OK、ERROR等），也返回IDLE
    else {
      logCaptureLn(String("收到非PDU数据，返回IDLE状态"));
      urcState = IDLE;
    }
  }
}

// 处理URC和PDU
void checkSerial1URC() {
  // 一次主循环里把模组串口中积压的行全部读完。
  // 原来每次只读一行，多条 +CMT 连续下发时后续的会一直堆在串口缓冲区里，
  // 缓冲区溢出后整段 PDU 被丢弃，长短信就出现「缺失分段」。
  while (true) {
    String line = readSerialLine(Serial1);
    if (line.length() == 0) return;
    handleModemLine(line);
  }
}
