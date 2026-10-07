# Shu-Clock

ESP32 BLE 비콘 기반 버스 도착/출발 감지 시스템과 데이터 수집 서버입니다.

## 구성

- **Transmitter**: 버스에 설치하며, BLE 광고 신호를 송출합니다.
- **Receiver**: 정류장에 설치하며, 비콘의 RSSI로 도착(ARRIVE)/출발(DEPART)을 판정해 서버로 전송합니다.
- **Server**: FastAPI 기반 수집 서버로, 방문/이탈 기록과 체류 시간을 MongoDB에 저장합니다.

```text
[Transmitter] --BLE--> [Receiver] --HTTP(S)--> [FastAPI] --> [MongoDB]
```

## 폴더 구조

```text
Shu-Clock/
├── transmitter/   송신기 스케치 (+ secrets.h)
├── receiver/      수신기 스케치 (+ secrets.h)
├── server/        FastAPI + MongoDB 서버 (+ .env)
└── docs/          문서
```

## 설정 및 실행

비밀 정보 파일(`secrets.h`, `.env`)은 저장소에 포함되지 않습니다. 예시 파일을 복사해 값을 채워 주세요.

1. **설정 파일 준비**
   - `receiver/secrets.example.h` → `receiver/secrets.h`
   - `transmitter/secrets.example.h` → `transmitter/secrets.h`
   - `server/.env.example` → `server/.env`
   - 서버의 `API_KEY`와 수신기의 `SECRET_API_KEY`는 같은 값이어야 합니다.
2. **서버 실행**
```bash
   cd server
   pip install -r requirements.txt
   python -m uvicorn main:app --host 0.0.0.0 --port 8000
```
3. **보드 업로드**: Arduino IDE에서 `transmitter.ino`와 `receiver.ino`를 각 보드에 업로드합니다.

## 문서

통신 규격과 데이터 구조는 [`docs/protocol.md`](docs/protocol.md)를 참고하세요.