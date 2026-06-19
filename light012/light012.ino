 #define LED_PIN 12  // 已修改引脚号为 12

int currentMode = 2; // 默认上电运行模式 2
unsigned long previousMillis = 0;
const long interval = 500; // 0.5 秒 = 500 毫秒
bool ledState = LOW;

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200); // 波特率设置为 115200
  
  // 打印启动信息
  Serial.println("ESP32 Ready. Default Mode: 2");
}

void loop() {
  // 1. 检查串口是否有数据
  if (Serial.available() > 0) {
    char incomingChar = Serial.read();
    
    // 过滤并处理指令
    if (incomingChar == '0' || incomingChar == '1' || incomingChar == '2') {
      currentMode = incomingChar - '0'; // 字符转为数字 0, 1, 2
      Serial.print("Switching to Mode: ");
      Serial.println(currentMode);
    } 
    else if (incomingChar == '9') {
      // 收到问号，返回当前模式号
      Serial.print(currentMode);
    }
  }

  // 2. 根据当前模式执行对应的 LED 逻辑
  switch (currentMode) {
    case 0:
      // 模式 0：熄灭 LED
      digitalWrite(LED_PIN, LOW);
      break;
      
    case 1:
      // 模式 1：点亮并保持长亮
      digitalWrite(LED_PIN, HIGH);
      break;
      
    case 2:
      // 模式 2：以 0.5 秒间隔闪烁（非阻塞）
      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        ledState = !ledState; // 反转 LED 状态
        digitalWrite(LED_PIN, ledState);
      }
      break;
  }
}