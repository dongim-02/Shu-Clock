#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ===== [BLE 필터] ===== 
static const int RSSI_THRESHOLD = -75;

// ===== [시간: 한국 표준시] =====
static const long GMT_OFFSET_SEC = 9L * 3600;
static const int DAYLIGHT_OFFSET_SEC = 0;
static const time_t MIN_VALID_EPOCH = 1700000000;   // 이보다 작으면 NTP 미동기화

// ===== [Wi-Fi / 전송] =====
static const uint32_t WIFI_CHECK_MS = 10000;
static const uint32_t WIFI_BOOT_TIMEOUT_MS = 15000;
static const uint32_t HTTP_TIMEOUT_MS = 5000;
static const uint32_t RETRY_INTERVAL_MS = 3000;

// ===== [타이밍 및 상태 판정] =====
static const uint32_t MISSING_MS = 3000;
static const uint32_t LOST_MS = 10000;
static const uint32_t ENTER_WINDOW_MS = 2000;
static const uint8_t ENTER_COUNT = 3;

enum State { ABSENT, PRESENT };
static State state = ABSENT;

// ===== [상태 관리 변수] =====
static volatile uint32_t lastSeenMs = 0;
static volatile uint32_t windowStartMs = 0;
static volatile uint8_t windowCount = 0;
static volatile int lastRssi = -127;
static volatile bool everSeen = false;

// ===== [시스템 제어 변수] =====
static bool isPaused = false;
static uint32_t lastWifiCheck = 0;
static bool ntpStarted = false;

// ===== [전송 대기 큐: 네트워크가 끊겨도 이벤트 보존] =====
struct PendingEvent {
  char id[48];     // 재전송해도 동일 → 서버가 중복 제거
  char type[8];    // "ARRIVE" / "DEPART"
  uint32_t ts;     // epoch(초), 시간 미동기화면 0
  int rssi;
};
static const uint8_t QUEUE_SIZE = 20;
static PendingEvent queueBuf[QUEUE_SIZE];
static uint8_t qHead = 0, qCount = 0;
static uint32_t lastSendTry = 0;

void enqueueEvent(const char *type, uint32_t ageMs, int rssi) {
  time_t nowT = time(nullptr);
  uint32_t ts = 0;
  if (nowT >= MIN_VALID_EPOCH) {
    // DEPART 는 판정이 LOST_MS 만큼 늦으므로 '마지막 수신 시각'을 출발 시각으로 쓴다
    ts = (uint32_t)nowT - (ageMs / 1000);
  }

  if (qCount == QUEUE_SIZE) {
    qHead = (qHead + 1) % QUEUE_SIZE;
    qCount--;
    Serial.println("[QUEUE] Full, dropped oldest");
  }

  PendingEvent &e = queueBuf[(qHead + qCount) % QUEUE_SIZE];
  if (ts > 0) {
    snprintf(e.id, sizeof(e.id), "%s-%s-%lu", SECRET_RECEIVER_ID, type, (unsigned long)ts);
  } else {
    // 시간 미동기화: 재부팅해도 겹치지 않도록 하드웨어 난수 사용
    snprintf(e.id, sizeof(e.id), "%s-%s-u%08lx", SECRET_RECEIVER_ID, type, (unsigned long)esp_random());
  }
  strncpy(e.type, type, sizeof(e.type) - 1);
  e.type[sizeof(e.type) - 1] = '\0';
  e.ts = ts;
  e.rssi = rssi;
  qCount++;
  Serial.printf("[QUEUE] +%s (pending=%u)\n", e.id, qCount);
}

// 반환: true = 큐에서 제거해도 됨
bool postEvent(const PendingEvent &e) {
  char body[256];
  snprintf(body, sizeof(body),
           "{\"event_id\":\"%s\",\"receiver_id\":\"%s\",\"event_type\":\"%s\","
           "\"event_ts\":%lu,\"bus_mac\":\"%s\",\"rssi\":%d}",
           e.id, SECRET_RECEIVER_ID, e.type, (unsigned long)e.ts, SECRET_TARGET_MAC, e.rssi);

  String url = SECRET_SERVER_URL;
  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);

  bool began;
  if (url.startsWith("https://")) {
#ifdef SECRET_ROOT_CA
    secure.setCACert(SECRET_ROOT_CA);
#else
    secure.setInsecure();   // 테스트 전용: 서버 인증서를 검증하지 않음
#endif
    began = http.begin(secure, url);
  } else {
    began = http.begin(plain, url);
  }
  if (!began) return false;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", SECRET_API_KEY);

  int code = http.POST((uint8_t *)body, strlen(body));
  http.end();
  Serial.printf("[HTTP] %s -> %d\n", e.id, code);

  if (code >= 200 && code < 300) return true;   // 성공 / 중복 모두 제거
  if (code == 422 || code == 403) return true;  // 재시도해도 소용없는 거절은 폐기
  return false;                                 // 네트워크 / 인증 / 서버 오류는 재시도
}

void flushQueue() {
  if (qCount == 0 || WiFi.status() != WL_CONNECTED) return;
  if (lastSendTry != 0 && millis() - lastSendTry < RETRY_INTERVAL_MS) return;
  lastSendTry = millis();

  if (postEvent(queueBuf[qHead])) {
    qHead = (qHead + 1) % QUEUE_SIZE;
    qCount--;
    lastSendTry = 0;   // 성공하면 다음 건을 바로 전송
  }
}

// ===== [로그용 시각 문자열] =====
String getTimestamp() {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10)) return "TIME_NOT_SYNCED";
  char buffer[24];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo);
  return String(buffer);
}

// ===== [Wi-Fi] =====
void startNtpIfNeeded() {
  if (!ntpStarted && WiFi.status() == WL_CONNECTED) {
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.google.com");
    ntpStarted = true;
    Serial.println("[NTP] Sync started");
  }
}

void connectWiFiAtBoot() {
  Serial.printf("[WiFi] Connecting to %s...\n", SECRET_WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SECRET_WIFI_SSID, SECRET_WIFI_PASS);

  uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < WIFI_BOOT_TIMEOUT_MS) {
    delay(250);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected, IP=%s\n", WiFi.localIP().toString().c_str());
    startNtpIfNeeded();
  } else {
    Serial.println("[WiFi] Boot connect failed (retrying in background)");
  }
}

// ===== [BLE 스캔 콜백] =====
class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    if (dev->getAddress().toString() != SECRET_TARGET_MAC) return;

    int rssi = dev->getRSSI();
    if (rssi < RSSI_THRESHOLD) return;

    uint32_t now = millis();
    lastRssi = rssi;
    lastSeenMs = now;
    everSeen = true;

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
  Serial.printf("Wi-Fi MAC: %s\n", WiFi.macAddress().c_str());

  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCallbacks(), false);
  scan->setActiveScan(false);
  scan->setInterval(100);
  scan->setWindow(60);              // Wi-Fi 와 무선부를 공유하므로 99 → 60
  scan->setDuplicateFilter(false);
  scan->start(0, false, true);
  Serial.printf("[%s] BLE receiver ready\n", SECRET_RECEIVER_ID);

  connectWiFiAtBoot();
}

void loop() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'r' || cmd == 'R') {
      Serial.println("[CMD] Restarting...");
      delay(100);
      ESP.restart();
    } else if (cmd == 'p' || cmd == 'P') {
      isPaused = true;
      Serial.println("[CMD] Paused");
    } else if (cmd == 's' || cmd == 'S' || cmd == 'c' || cmd == 'C') {
      isPaused = false;
      Serial.println("[CMD] Resumed");
    }
  }

  uint32_t now = millis();

  // Wi-Fi 점검: 블로킹 없이 재연결만 요청 (BLE 판정이 늦어지지 않도록)
  if (now - lastWifiCheck >= WIFI_CHECK_MS) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Disconnected. Reconnecting...");
      WiFi.reconnect();
    }
  }
  startNtpIfNeeded();

  if (isPaused) {
    delay(100);
    return;
  }

  now = millis();
  uint32_t age = everSeen ? (now - lastSeenMs) : 0xFFFFFFFF;

  if (state == ABSENT) {
    if (everSeen && age < ENTER_WINDOW_MS && windowCount >= ENTER_COUNT) {
      state = PRESENT;
      Serial.printf("[%s] EVENT=ARRIVE time=%s rssi=%d\n",
                    SECRET_RECEIVER_ID, getTimestamp().c_str(), (int)lastRssi);
      enqueueEvent("ARRIVE", 0, (int)lastRssi);
    }
  } else {
    if (age > LOST_MS) {
      state = ABSENT;
      windowCount = 0;
      Serial.printf("[%s] EVENT=DEPART time=%s lastSeen=%lums\n",
                    SECRET_RECEIVER_ID, getTimestamp().c_str(), (unsigned long)age);
      enqueueEvent("DEPART", age, (int)lastRssi);
    }
  }

  flushQueue();

  static uint32_t lastLog = 0;
  if (now - lastLog >= 1000) {
    lastLog = now;
    const char *label = (state == PRESENT) ? (age > MISSING_MS ? "MISSING" : "PRESENT") : "ABSENT";
    Serial.printf("[%s] time=%s state=%s lastSeen=%lums rssi=%d wifi=%s queue=%u\n",
                  SECRET_RECEIVER_ID, getTimestamp().c_str(), label,
                  (unsigned long)(everSeen ? age : 0), (int)lastRssi,
                  WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED", qCount);
  }

  delay(10);
}