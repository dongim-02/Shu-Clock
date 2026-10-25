#pragma once
// 이 파일을 secrets.h 로 복사한 뒤 실제 값을 입력하세요. secrets.h 는 git 에 올리지 않습니다.

#define SECRET_RECEIVER_ID  "RX-01"                 // 수신기 ID (RX-01 / RX-02)
#define SECRET_TARGET_MAC   "00:00:00:00:00:00"     // 송신기 시리얼에 출력된 BLE MAC (소문자)

#define SECRET_WIFI_SSID    "your-ssid"
#define SECRET_WIFI_PASS    "your-password"

// 서버 주소. 운영에서는 https:// 를 권장합니다.
#define SECRET_SERVER_URL   "https://your-server.example.com/api/events
#define SECRET_API_KEY      "server/.env 의 API_KEY 와 동일한 값"

// (선택) HTTPS 서버 인증서 검증용 루트 CA (PEM). 정의하지 않으면 검증 없이 접속합니다(테스트 전용).
// #define SECRET_ROOT_CA "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n"