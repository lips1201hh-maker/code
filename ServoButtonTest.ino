/*
 * ESP32-S3 N16R8 / SG90 单独测试
 * 信号 GPIO6，BOOT 按钮 GPIO0（按下为 LOW）。
 * BOOT 每按一次：45 -> 90 -> 135 -> 90 -> 45 ...
 * 上电不发送位置脉冲，按键后才开始控制；RST 仅复位。
 * Arduino ESP32 3.x，无需安装第三方库。
 */
#include <Arduino.h>

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32S3 Dev Module."
#endif
#if ESP_ARDUINO_VERSION_MAJOR < 3
#error "Install Arduino ESP32 core 3.x."
#endif

constexpr uint8_t SERVO_PIN = 6;
constexpr uint8_t TEST_BUTTON_PIN = 0;
constexpr uint8_t PWM_BITS = 14;
constexpr uint32_t PWM_HZ = 50;
constexpr uint32_t PWM_PERIOD_US = 1000000UL / PWM_HZ;
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t MOVE_WAIT_MS = 800;
constexpr uint8_t ANGLES[] = {45, 90, 135, 90};

bool pwmReady = false;
bool rawPressed = false;
bool stablePressed = false;
bool releasedSinceBoot = false;
bool hasCommand = false;
uint32_t rawChangedAt = 0;
uint32_t lastMoveAt = 0;
uint32_t lastStatusAt = 0;
uint8_t nextAngleIndex = 0;
int commandedAngle = -1;

void printStatus() {
  if (!pwmReady) {
    Serial.println("ERROR: PWM unavailable. Check board/core settings, then reset.");
  } else if (!hasCommand) {
    Serial.println("READY: press and release BOOT. No angle commanded yet.");
  } else {
    Serial.printf("READY: angle_cmd=%d; next=%u. Position is NOT measured.\n",
      commandedAngle, unsigned(ANGLES[nextAngleIndex]));
  }
}

void moveOnPress(uint32_t now) {
  if (!pwmReady) return;
  if (hasCommand && now - lastMoveAt < MOVE_WAIT_MS) {
    Serial.println("WAIT: allow 800ms between moves, then press BOOT again.");
    return;
  }
  const uint8_t angle = ANGLES[nextAngleIndex];
  // SG90 常见 500~2500us 对应 0~180 度；本测试只用中间范围。
  // 45/90/135 度分别发出 1000/1500/2000us 脉冲。
  const uint32_t pulseUs = 500UL + uint32_t(angle) * 2000UL / 180UL;
  const uint32_t duty = (pulseUs * ((1UL << PWM_BITS) - 1) + PWM_PERIOD_US / 2)
                        / PWM_PERIOD_US;
  if (!ledcWrite(SERVO_PIN, duty)) {
    pwmReady = false;
    Serial.println("ERROR: ledcWrite failed. Restart after checking configuration.");
    return;
  }
  commandedAngle = angle;
  hasCommand = true;
  lastMoveAt = now;
  nextAngleIndex = (nextAngleIndex + 1) % (sizeof(ANGLES) / sizeof(ANGLES[0]));
  Serial.printf("MOVE: GPIO6 angle_cmd=%u pulse=%luus frequency=50Hz\n",
    unsigned(angle), (unsigned long)pulseUs);
}

void setup() {
  Serial.begin(115200);
  pinMode(TEST_BUTTON_PIN, INPUT_PULLUP);
  rawPressed = stablePressed = (digitalRead(TEST_BUTTON_PIN) == LOW);
  rawChangedAt = millis();
  pwmReady = ledcAttach(SERVO_PIN, PWM_HZ, PWM_BITS);
  // 保持信号低电平，不在上电时直接转向未知的目标位置。
  if (pwmReady && !ledcWrite(SERVO_PIN, 0)) pwmReady = false;
  Serial.println("\nSG90 BUTTON TEST / BOOT=GPIO0 / SIGNAL=GPIO6");
  printStatus();
}

void loop() {
  const uint32_t now = millis();
  const bool pressed = (digitalRead(TEST_BUTTON_PIN) == LOW);
  if (pressed != rawPressed) {
    rawPressed = pressed;
    rawChangedAt = now;
  }
  if (now - rawChangedAt >= DEBOUNCE_MS) {
    if (!rawPressed) releasedSinceBoot = true;
    if (rawPressed != stablePressed) {
      stablePressed = rawPressed;
      if (stablePressed && releasedSinceBoot) moveOnPress(now);
    }
  }
  // 长按不会重复动作；定期输出让稍后打开串口的人也能看到状态。
  if (now - lastStatusAt >= 3000) {
    lastStatusAt = now;
    printStatus();
  }
  delay(1);
}
