#include <Arduino.h>
#include "BluetoothSerial.h" // 引入 ESP32 经典蓝牙串口库

// 健壮性检查：确保 PlatformIO 或 Arduino IDE 编译环境中开启了蓝牙宏
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please enable Bluetooth in menuconfig or build flags.
#endif

#define LED_PIN 12

BluetoothSerial SerialBT;     // 实例化蓝牙串口对象
volatile int currentMode = 2;  // 当前模式：0-灭，1-亮，2-闪烁（初始默认闪烁）

/**
 * 统一的指令处理核心函数
 * @param cmd 接收到的单字节字符
 * @param output 响应的目标通道（可以是 Serial 也可以是 SerialBT）
 */
void handleCommand(char cmd, Stream &output) {
  if (cmd >= '0' && cmd <= '2') {
    currentMode = cmd - '0';
    output.print("Switching to Mode: ");
    output.println(currentMode); // 附带 \n，完美对齐 Golang 接收端
  }
  else if (cmd == '9') {
    // 关键对齐：必须使用 println() 发送 \n 结束符，彻底解决 Go 端的阻塞和超时
    output.println(currentMode);
  }
}

void setup() {
  // 【核心修复 1】：强制锁定 160MHz 高性能主频
  // 经典蓝牙对射频时钟同步要求极高，绝对不允许降频到 80MHz 或开启轻度睡眠(Light Sleep)
  setCpuFrequencyMhz(160);

  // 初始化硬件物理串口（用于 USB 数据线调试）
  Serial.begin(115200);

  // 初始化 LED 引脚
  pinMode(LED_PIN, OUTPUT);

  // 初始化并启动经典蓝牙 SPP 协议栈
  if (!SerialBT.begin("ESP32-LED-Controller")) {
    Serial.println("Bluetooth initialization failed!");
  } else {
    Serial.println("Bluetooth Ready! Device Name: ESP32-LED-Controller");
  }
}

void loop() {
  // 【通道一】：监听物理硬件串口（USB 数据线）
  while (Serial.available() > 0) {
    char incomingChar = Serial.read();
    handleCommand(incomingChar, Serial);
  }

  // 【通道二】：监听无线蓝牙串口（Golang 客户端通过蓝牙发送的指令）
  while (SerialBT.available() > 0) {
    char incomingChar = SerialBT.read();

    // 【核心修复 2：调试镜像】：将蓝牙收到的数据原样实时打印到 USB 串口
    // 您可以一直打开 Mac 的串口监视器，如果这里有输出，说明 Go 发射信号成功；如果没输出，说明空中丢包
    Serial.print("[Debug BT Input] Received byte: '");
    Serial.print(incomingChar);
    Serial.print("' (ASCII: ");
    Serial.print((int)incomingChar);
    Serial.println(")");

    // 执行业务控制逻辑，并通过蓝牙回传结果给 Go
    handleCommand(incomingChar, SerialBT);
  }

  // 【LED 状态机】：采用毫秒级非阻塞时间戳调度，保证高频响应
  static unsigned long previousMillis = 0;
  static bool ledState = LOW;

  switch (currentMode) {
    case 0:
      digitalWrite(LED_PIN, LOW);
      break;

    case 1:
      digitalWrite(LED_PIN, HIGH);
      break;

    case 2: {
      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= 500) {
        previousMillis = currentMillis;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
      }
      break;
    }
  }

  // 【核心修复 3】：让出 10ms 时间片给 FreeRTOS 底层
  // 经典蓝牙的后台协议栈运行在高级别的内部任务中，此延迟能确保射频握手稳定、不掉线
  vTaskDelay(pdMS_TO_TICKS(10));
}