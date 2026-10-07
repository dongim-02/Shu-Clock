#include <NimBLEDevice.h>
#include "config.h"   // RECEIVER_ID, RSSI_THRESHOLD, TARGET_MAC 정의

// ===== [타이밍 및 상태 판정 기준 설정] =====
static const uint32_t MISSING_MS = 3000;          // 미수신 보류 시간 (3초)
static const uint32_t LOST_MS    = 10000;         // 이탈 확정 시간 (10초)
static const uint32_t ENTER_WINDOW_MS = 2000;     // 진입 판정 윈도우 (2초)
static const uint8_t  ENTER_COUNT = 3;            // 진입 필요 수신 횟수 (3회)

enum State { ABSENT, PRESENT };
static State state = ABSENT;

// ===== [상태 관리 변수] =====
static volatile uint32_t lastSeenMs = 0;
static volatile uint32_t windowStartMs = 0;
static volatile uint8_t  windowCount = 0;
static volatile int      lastRssi = -127;
static volatile bool     everSeen = false;

// ===== [테스트 제어 변수] =====
static bool isPaused = false; // 'p' 키로 일시 정지 상태인지 여부

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (dev->getAddress().toString() != TARGET_MAC) return;
    
    int rssi = dev->getRSSI();
    if (rssi < RSSI_THRESHOLD) return;

    uint32_t now = millis();
    lastRssi = rssi;
    lastSeenMs = now;
    everSeen = true;

    // 진입 윈도우 카운터 관리
    if (now - windowStartMs > ENTER_WINDOW_MS) {
      windowStartMs = now;
      windowCount = 0;
    }
    windowCount++;
  }

  void onScanEnd(const NimBLEScanResults &results, int reason) override {
    NimBLEDevice::getScan()->start(0, false, true);
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCallbacks(), false);
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(99);
  scan->setDuplicateFilter(false);
  scan->start(0, false, true);

  Serial.printf("[%s] ready\n", RECEIVER_ID);
}

void loop() {
  // ===== [테스트용 시리얼 명령어 처리] =====
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    if (cmd == 'r' || cmd == 'R') {
      Serial.println("\n[CMD] 강제 재부팅 수행 중...");
      delay(100);
      ESP.restart(); 
    }
    else if (cmd == 'p' || cmd == 'P') {
      isPaused = true;
      Serial.println("\n[CMD] 시스템 일시 정지");
    }
    else if (cmd == 's' || cmd == 'S' || cmd == 'c' || cmd == 'C') {
      isPaused = false;
      Serial.println("\n[CMD] 시스템 재개");
    }
  }

  // 일시 정지 상태인 경우 대기
  if (isPaused) {
    delay(100); 
    return;     
  }
  // ==========================================

  uint32_t now = millis();
  uint32_t age = everSeen ? (now - lastSeenMs) : 0xFFFFFFFF;

  if (state == ABSENT) {
    // 진입 판정 (ARRIVE)
    if (everSeen && age < ENTER_WINDOW_MS && windowCount >= ENTER_COUNT) {
      state = PRESENT;
      Serial.printf("[%s] EVENT=ARRIVE rssi=%d\n", RECEIVER_ID, lastRssi);
      // TODO: 서버로 ARRIVE 전송
    }
  } else {  // PRESENT
    // 이탈 판정 (DEPART)
    if (age > LOST_MS) {
      state = ABSENT;
      windowCount = 0;
      Serial.printf("[%s] EVENT=DEPART lastSeen=%lums\n", RECEIVER_ID, (unsigned long)age);
      // TODO: 서버로 DEPART 전송
    }
  }

  // 1초마다 상태 모니터링 로그 출력
  static uint32_t lastLog = 0;
  if (now - lastLog >= 1000) {
    lastLog = now;
    const char *label = (state == PRESENT) ? (age > MISSING_MS ? "MISSING" : "PRESENT") : "ABSENT";
    Serial.printf("[%s] state=%s lastSeen=%lums rssi=%d\n",
                  RECEIVER_ID, label, (unsigned long)(everSeen ? age : 0), lastRssi);
  }
  
  delay(10);
}