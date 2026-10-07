#pragma once

// ===== 이 파일을 config.h로 복사한 뒤 실제 값을 입력하세요 =====
static const char *RECEIVER_ID = "RX-01";                // 수신기 ID (RX-01 / RX-02)
static const int   RSSI_THRESHOLD = -75;                 // 수신 감도 필터
static const char *TARGET_MAC = "00:00:00:00:00:00";     // 송신기 시리얼에 출력된 BLE MAC (소문자)