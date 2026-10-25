# Shu-Clock

ESP32 BLE 비콘 기반 버스 도착/출발 감지 시스템과 데이터 수집 서버입니다.

## 구성

- **Transmitter**: 버스에 설치하며, BLE 광고 신호를 송출합니다.
- **Receiver**: 정류장에 설치하며, 비콘의 RSSI로 도착(ARRIVE)/출발(DEPART)을 판정해 서버로 전송합니다. 정류장마다 `RX-01`, `RX-02` 스케치를 따로 업로드합니다.
- **Server**: FastAPI 기반 수집 서버로, 방문/이탈 기록과 체류 시간을 MongoDB에 저장합니다.

```text
[Transmitter] --BLE--> [Receiver] --HTTP(S)--> [FastAPI] --> [MongoDB]
```

## 폴더 구조

```text
Shu-Clock/
├── transmitter/
│   ├── transmitter.ino
│   └── secrets.example.h
├── receiver/
│   ├── receiver_common.h      수신기 공통 코드
│   ├── secrets.example.h      수신기 설정 예시 (RX-01, RX-02 공용)
│   ├── RX-01/RX-01.ino        1번 수신기 스케치 (+ secrets.h)
│   └── RX-02/RX-02.ino        2번 수신기 스케치 (+ secrets.h)
├── server/
│   ├── main.py
│   ├── requirements.txt
│   └── .env.example
└── docs/
    └── protocol.md
```

## 설정

비밀 정보 파일(`secrets.h`, `.env`)은 저장소에 포함되지 않습니다. 예시 파일을 복사해 값을 채워 주세요.

| 예시 파일 | 복사 위치 |
|---|---|
| `server/.env.example` | `server/.env` |
| `transmitter/secrets.example.h` | `transmitter/secrets.h` |
| `receiver/secrets.example.h` | `receiver/RX-01/secrets.h` |
| `receiver/secrets.example.h` | `receiver/RX-02/secrets.h` |

- `RX-01/secrets.h`는 `SECRET_RECEIVER_ID`를 `"RX-01"`, `RX-02/secrets.h`는 `"RX-02"`로 설정합니다.
- 서버의 `API_KEY`와 수신기의 `SECRET_API_KEY`는 같은 값이어야 합니다.
- `SECRET_TARGET_MAC`은 송신기를 부팅했을 때 시리얼에 출력되는 BLE MAC(소문자)입니다.
- `ALLOWED_RECEIVERS`에는 사용하는 수신기 ID를 모두 넣어 주세요.
- `API_KEY` 생성: `python -c "import secrets; print(secrets.token_urlsafe(32))"`

## 실행

### 서버

```powershell
cd server
python -m venv .venv
.venv\Scripts\Activate.ps1        # macOS/Linux: source .venv/bin/activate
pip install -r requirements.txt
python -m uvicorn main:app --host 0.0.0.0 --port 8000
```

### 보드 업로드

Arduino IDE에서 각 스케치를 해당 보드에 업로드합니다.

- 송신기: `transmitter/transmitter.ino`
- 수신기 1: `receiver/RX-01/RX-01.ino`
- 수신기 2: `receiver/RX-02/RX-02.ino`

## 보안 주의

- `secrets.h`, `.env`는 절대 커밋하지 마세요. `.gitignore`에 등록되어 있습니다.
- `**/config.h`도 무시되므로, 비밀이 아닌 일반 설정 헤더는 다른 이름을 쓰세요.
- 커밋 전에 `git ls-files`로 비밀 파일이 포함되지 않았는지 확인하세요.
- 운영 환경에서는 `APP_ENV=production`, HTTPS 서버 주소, `SECRET_ROOT_CA` 설정을 권장합니다.

## 문서

통신 규격과 데이터 구조는 [`docs/protocol.md`](docs/protocol.md)를 참고하세요.