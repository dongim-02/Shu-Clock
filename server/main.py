import os
import re
import secrets
from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Literal, Optional

from dotenv import load_dotenv
from fastapi import Depends, FastAPI, Header, HTTPException, Query
from motor.motor_asyncio import AsyncIOMotorClient
from pydantic import BaseModel, Field, field_validator
from pymongo import ASCENDING, DESCENDING
from pymongo.errors import DuplicateKeyError

# 실행 위치와 상관없이 server/.env 를 읽는다
load_dotenv(Path(__file__).with_name(".env"))


def _csv_env(name: str, transform) -> set:
    return {transform(x.strip()) for x in os.getenv(name, "").split(",") if x.strip()}


MONGO_URI = os.getenv("MONGO_URI", "mongodb://localhost:27017")
DB_NAME = os.getenv("DB_NAME", "bus_tracking")
API_KEY = os.getenv("API_KEY", "")
APP_ENV = os.getenv("APP_ENV", "development")
ALLOWED_RECEIVERS = _csv_env("ALLOWED_RECEIVERS", str.upper)
ALLOWED_BUS_MACS = _csv_env("ALLOWED_BUS_MACS", str.lower)

# 키가 없거나 약하면 서버를 아예 시작하지 않는다 (기본값으로 열리는 사고 방지)
if len(API_KEY) < 16 or API_KEY == "change-me":
    raise RuntimeError(
        "API_KEY 가 설정되지 않았거나 너무 짧습니다 (16자 이상). server/.env 를 확인하세요."
    )

KST = timezone(timedelta(hours=9))
MIN_VALID_EPOCH = 1_700_000_000          # 이보다 작으면 NTP 미동기화로 간주
MAX_FUTURE_SKEW = timedelta(minutes=5)   # 미래 시각 허용 오차
MAC_RE = re.compile(r"^([0-9a-f]{2}:){5}[0-9a-f]{2}$")


# ---------- 입력 스키마 ----------
class BeaconEvent(BaseModel):
    event_id: str = Field(min_length=3, max_length=64)  # 재전송 시 중복 방지용
    receiver_id: str = Field(min_length=1, max_length=32)
    event_type: Literal["ARRIVE", "DEPART"]
    event_ts: int = Field(ge=0)          # epoch(초). 0이면 기기 시간 미동기화
    bus_mac: str
    rssi: Optional[int] = Field(default=None, ge=-127, le=0)

    @field_validator("bus_mac")
    @classmethod
    def normalize_mac(cls, v: str) -> str:
        v = v.strip().lower().replace("-", ":")
        if not MAC_RE.match(v):
            raise ValueError("invalid MAC address")
        return v

    @field_validator("receiver_id")
    @classmethod
    def normalize_rx(cls, v: str) -> str:
        return v.strip().upper()


# ---------- 전처리 ----------
def preprocess(ev: BeaconEvent, received_at: datetime) -> dict:
    """원본 이벤트를 저장용 문서로 정제한다."""
    time_source = "device"
    event_time = None

    if ev.event_ts >= MIN_VALID_EPOCH:
        event_time = datetime.fromtimestamp(ev.event_ts, tz=timezone.utc)
        if event_time - received_at > MAX_FUTURE_SKEW:   # 비정상적인 미래 시각
            event_time = None

    if event_time is None:
        event_time = received_at
        time_source = "server"

    return {
        "event_id": ev.event_id,
        "receiver_id": ev.receiver_id,
        "event_type": ev.event_type,
        "bus_mac": ev.bus_mac,
        "rssi": ev.rssi,
        "event_time": event_time,               # Mongo에는 UTC로 저장됨
        "time_source": time_source,             # device | server
        "received_at": received_at,
        "latency_sec": round((received_at - event_time).total_seconds(), 1),
    }


# ---------- 앱 / DB ----------
@asynccontextmanager
async def lifespan(app: FastAPI):
    client = AsyncIOMotorClient(MONGO_URI, tz_aware=True)
    db = client[DB_NAME]
    await db.raw_events.create_index("event_id", unique=True)
    await db.raw_events.create_index([("event_time", DESCENDING)])
    await db.visits.create_index(
        [("receiver_id", ASCENDING), ("bus_mac", ASCENDING), ("status", ASCENDING)]
    )
    await db.visits.create_index([("arrive_time", DESCENDING)])
    app.state.client = client
    app.state.db = db
    yield
    client.close()


_is_prod = APP_ENV == "production"
app = FastAPI(
    title="Bus Beacon Collector",
    lifespan=lifespan,
    docs_url=None if _is_prod else "/docs",
    redoc_url=None,
    openapi_url=None if _is_prod else "/openapi.json",
)


async def verify_key(x_api_key: str = Header(default="")):
    # bytes 로 비교: 비ASCII 문자가 들어와도 500 이 아니라 401 이 되도록
    if not secrets.compare_digest(x_api_key.encode(), API_KEY.encode()):
        raise HTTPException(status_code=401, detail="invalid api key")


def to_kst(doc: dict) -> dict:
    """응답용: _id 제거, datetime 을 KST ISO 문자열로 변환."""
    out = {}
    for k, v in doc.items():
        if k == "_id":
            continue
        out[k] = v.astimezone(KST).isoformat() if isinstance(v, datetime) else v
    return out


async def update_visits(db, doc: dict):
    """ARRIVE/DEPART 를 방문(visit) 단위로 짝짓는다. 같은 이벤트로 여러 번 불려도 안전(멱등)."""
    key = {"receiver_id": doc["receiver_id"], "bus_mac": doc["bus_mac"]}
    t = doc["event_time"]
    eid = doc["event_id"]

    if doc["event_type"] == "ARRIVE":
        if await db.visits.find_one({"arrive_event_id": eid}, {"_id": 1}):
            return  # 이미 반영됨

        # 순서가 뒤바뀌어 DEPART 가 먼저 도착했던 경우: 그 건과 합친다
        orphan = await db.visits.find_one(
            {**key, "status": "depart_only", "depart_time": {"$gte": t}},
            sort=[("depart_time", ASCENDING)],
        )
        if orphan:
            await db.visits.update_one(
                {"_id": orphan["_id"]},
                {"$set": {
                    "arrive_time": t,
                    "duration_sec": int((orphan["depart_time"] - t).total_seconds()),
                    "status": "closed",
                    "arrive_event_id": eid,
                }},
            )
            return

        # 이전 방문이 DEPART 없이 열려 있으면 '미종료' 처리
        await db.visits.update_many(
            {**key, "status": "open"}, {"$set": {"status": "unclosed"}}
        )
        await db.visits.insert_one({
            **key,
            "arrive_time": t,
            "depart_time": None,
            "duration_sec": None,
            "status": "open",
            "arrive_event_id": eid,
            "depart_event_id": None,
        })
    else:  # DEPART
        if await db.visits.find_one({"depart_event_id": eid}, {"_id": 1}):
            return  # 이미 반영됨

        visit = await db.visits.find_one(
            {**key, "status": "open", "arrive_time": {"$lte": t}},
            sort=[("arrive_time", DESCENDING)],
        )
        if visit:
            await db.visits.update_one(
                {"_id": visit["_id"]},
                {"$set": {
                    "depart_time": t,
                    "duration_sec": int((t - visit["arrive_time"]).total_seconds()),
                    "status": "closed",
                    "depart_event_id": eid,
                }},
            )
        else:  # 짝이 되는 ARRIVE 를 아직 못 받은 경우
            await db.visits.insert_one({
                **key,
                "arrive_time": None,
                "depart_time": t,
                "duration_sec": None,
                "status": "depart_only",
                "arrive_event_id": None,
                "depart_event_id": eid,
            })


# ---------- 엔드포인트 ----------
@app.get("/health")
async def health():
    return {"ok": True}


@app.post("/api/events", dependencies=[Depends(verify_key)])
async def receive_event(ev: BeaconEvent):
    if ALLOWED_RECEIVERS and ev.receiver_id not in ALLOWED_RECEIVERS:
        raise HTTPException(status_code=403, detail="receiver not allowed")
    if ALLOWED_BUS_MACS and ev.bus_mac not in ALLOWED_BUS_MACS:
        raise HTTPException(status_code=403, detail="bus not allowed")

    received_at = datetime.now(timezone.utc)
    doc = preprocess(ev, received_at)
    db = app.state.db

    try:
        await db.raw_events.insert_one(dict(doc))
        status = "ok"
    except DuplicateKeyError:
        # 재전송된 중복 이벤트. 이전에 visits 반영 도중 실패했을 수 있으니 다시 보정한다
        stored = await db.raw_events.find_one({"event_id": ev.event_id})
        doc = stored or doc
        status = "duplicate"

    await update_visits(db, doc)
    return {"status": status, "event_id": ev.event_id, "time_source": doc["time_source"]}


@app.get("/api/events", dependencies=[Depends(verify_key)])
async def list_events(limit: int = Query(50, ge=1, le=500), receiver_id: Optional[str] = None):
    q = {"receiver_id": receiver_id.upper()} if receiver_id else {}
    cur = app.state.db.raw_events.find(q).sort("event_time", DESCENDING).limit(limit)
    return [to_kst(d) async for d in cur]


@app.get("/api/visits", dependencies=[Depends(verify_key)])
async def list_visits(limit: int = Query(50, ge=1, le=500), status: Optional[str] = None):
    q = {"status": status} if status else {}
    cur = app.state.db.visits.find(q).sort("arrive_time", DESCENDING).limit(limit)
    return [to_kst(d) async for d in cur]