# Shu-Clock

ESP32 BLE 비콘 기반 버스 도착/출발 감지 시스템.

## 구성
- **Transmitter**: 버스에 설치, BLE 광고 송출 (`MY-BEACON`)
- **Receiver**: 정류장에 설치, 비콘 RSSI로 ARRIVE / DEPART 판정

## 폴더 구조
```
Shu-Clock/
├── transmitter/   송신기 스케치
├── receiver/      수신기 스케치 (+ config.example.h)
└── docs/          문서
```

## 하드웨어 / 라이브러리
- ESP32 (송신기 1대, 수신기 N대)
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 2.x

## 판정 로직
| 항목 | 값 |
|---|---|
| RSSI 필터 | -75 dBm |
| 진입(ARRIVE) | 2초 내 3회 수신 |
| 미수신 보류(MISSING) | 3초 |
| 이탈(DEPART) | 10초 미수신 |

## 사용법
1. `transmitter/transmitter.ino`를 송신기 ESP32에 업로드
2. 시리얼 모니터(115200)에서 `BLE MAC: xx:xx:...` 확인
3. `receiver/config.example.h`를 `config.h`로 복사 후 MAC, ID 수정
4. `receiver/receiver.ino`를 수신기 ESP32에 업로드
5. 시리얼 모니터에서 동작 확인

### 테스트용 시리얼 명령 (수신기)
| 키 | 동작 |
|---|---|
| `r` | 재부팅 |
| `p` | 일시 정지 |
| `s` / `c` | 재개 |

## TODO
- [ ] 서버로 ARRIVE/DEPART 전송
- [ ] 수신기 2대 연동 (RX-01 / RX-02)
- [ ] 실외 RSSI 임계값 튜닝
- [ ] 테스트용 UUID를 고유 값으로 교체