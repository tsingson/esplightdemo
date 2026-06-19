#include <Arduino.h>
#include <esp_wifi.h>
#include "BluetoothSerial.h" // 引入 ESP32 经典蓝牙串口库

// 检查固件编译配置中是否开启了蓝牙宏
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to enable it
#endif

#define LED_PIN 12

BluetoothSerial SerialBT;    // 声明蓝牙串口对象
volatile int currentMode = 2; // 当前模式：0-灭，1-亮，2-闪烁

// 核心重构：利用 Stream 引用，完美复用物理串口与蓝牙串口的输出逻辑
void handleCommand(char cmd, Stream &output) {
  if (cmd >= '0' && cmd <= '2') {
    currentMode = cmd - '0';
    output.print("Switching to Mode: ");
    output.println(currentMode);
  }
  else if (cmd == '9') {
    // 收到查询指令，直接向来源渠道回写当前模式号
    output.print(currentMode);
  }
}

void setup() {
  // 1. 仍然关闭 Wi-Fi 模块以节约非必要能耗
  esp_wifi_stop();

  // 2. 初始化硬件物理串口
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // 3. 初始化并启动经典蓝牙
  // 此时底层会自动配置好 Bluedroid 协议栈，无需手动调用 esp_bt_controller_enable
  if (!SerialBT.begin("ESP32-LED-Controller")) {
    Serial.println("Bluetooth initialization failed!");
  } else {
    Serial.println("Bluetooth Ready! Device Name: ESP32-LED-Controller");
  }
}

void loop() {
  // 渠道一：监听物理硬件串口
  while (Serial.available() > 0) {
    char incomingChar = Serial.read();
    handleCommand(incomingChar, Serial);
  }

  // 渠道二：监听无线蓝牙串口（逻辑完全一致）
  while (SerialBT.available() > 0) {
    char incomingChar = SerialBT.read();
    handleCommand(incomingChar, SerialBT);
  }

  // LED 非阻塞时间戳调度状态机
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

  // 维持 FreeRTOS 时间片轮转，防止看门狗超时，同时给蓝牙协议栈留出处理时间
  vTaskDelay(pdMS_TO_TICKS(10));
}