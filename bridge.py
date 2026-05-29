#!/usr/bin/env python3
"""
bridge.py — HTTP + WebSocket bridge to the ESP32 BLE keyboard emulator.

Runs a small web service on the host (e.g. a Raspberry Pi) that is connected to
the ESP32 over USB serial, so any authorized device on the network can send
keystrokes and watch connection events live.

Install:
  pip install -r requirements-bridge.txt

Run:
  python bridge.py                          # binds 127.0.0.1:8000 (localhost only)
  python bridge.py --net-host 0.0.0.0       # expose on the LAN (token REQUIRED)
  python bridge.py --serial-port COM5       # override auto-detected serial port

Auth:
  If the HARDLOOP_TOKEN environment variable is set, every HTTP request must
  send  "Authorization: Bearer <token>"  and the WebSocket must pass ?token=...
  Binding to a non-localhost address WITHOUT a token is refused.

Endpoints:
  GET  /health              liveness probe (no auth)
  GET  /status              current BLE state
  POST /type     {"text": "...", "enter": false}
  POST /key      {"name": "ENTER"}
  POST /mod      {"combo": "CTRL+C"}
  POST /media    {"name": "PLAY"}
  POST /command  {"raw": "STATUS"}
  WS   /events              live EVENT/state stream
  GET  /docs                interactive OpenAPI docs (FastAPI)
"""

import argparse
import asyncio
import json
import os
import sys
from contextlib import asynccontextmanager

import serial
from fastapi import Depends, FastAPI, Header, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.concurrency import run_in_threadpool
from pydantic import BaseModel
import uvicorn

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from hardloop_daemon import HardloopDaemon
from port_detect import find_esp32_port

LOCALHOSTS = {"127.0.0.1", "localhost", "::1"}

# Set in main() before the server starts.
daemon: HardloopDaemon | None = None
AUTH_TOKEN: str | None = None


# ── Request models ────────────────────────────────────────────────────────────
class TypeBody(BaseModel):
    text: str
    enter: bool = False


class KeyBody(BaseModel):
    name: str


class ModBody(BaseModel):
    combo: str


class MediaBody(BaseModel):
    name: str


class CommandBody(BaseModel):
    raw: str


# ── WebSocket fan-out ───────────────────────────────────────────────────────────
class ConnectionManager:
    def __init__(self):
        self.active: set[WebSocket] = set()
        self.queue: asyncio.Queue | None = None

    async def connect(self, ws: WebSocket):
        await ws.accept()
        self.active.add(ws)

    def disconnect(self, ws: WebSocket):
        self.active.discard(ws)

    async def broadcast(self, message: str):
        dead = []
        for ws in list(self.active):
            try:
                await ws.send_text(message)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.active.discard(ws)


manager = ConnectionManager()


# ── Auth ──────────────────────────────────────────────────────────────────────
def require_auth(authorization: str | None = Header(default=None)):
    if AUTH_TOKEN is None:
        return
    if authorization != f"Bearer {AUTH_TOKEN}":
        raise HTTPException(status_code=401, detail="invalid or missing token")


# ── App / lifecycle ─────────────────────────────────────────────────────────────
async def _broadcaster():
    assert manager.queue is not None
    while True:
        message = await manager.queue.get()
        await manager.broadcast(message)


@asynccontextmanager
async def lifespan(app: FastAPI):
    loop = asyncio.get_running_loop()
    manager.queue = asyncio.Queue()
    broadcaster = asyncio.create_task(_broadcaster())

    # Push each ESP32 event onto the asyncio queue from the daemon's thread.
    def push(event: str):
        payload = json.dumps({"event": event, "state": daemon.state})
        loop.call_soon_threadsafe(manager.queue.put_nowait, payload)

    daemon.on_event = push
    try:
        daemon.connect()
        print(f"[bridge] connected to {daemon.port} — state: {daemon.state}")
    except serial.SerialException as e:
        print(f"[bridge] FATAL: cannot open serial port {daemon.port}: {e}")
        broadcaster.cancel()
        raise

    try:
        yield
    finally:
        daemon.disconnect()
        broadcaster.cancel()


app = FastAPI(title="ESP32 BLE Keyboard Bridge", version="1.0.0", lifespan=lifespan)


# ── HTTP endpoints ──────────────────────────────────────────────────────────────
@app.get("/health")
def health():
    return {"ok": True}


@app.get("/status", dependencies=[Depends(require_auth)])
async def status():
    resp = await run_in_threadpool(daemon.status)
    return {"state": daemon.state, "raw": resp}


@app.post("/type", dependencies=[Depends(require_auth)])
async def do_type(body: TypeBody):
    fn = daemon.type_enter if body.enter else daemon.type_text
    resp = await run_in_threadpool(fn, body.text)
    return {"response": resp}


@app.post("/key", dependencies=[Depends(require_auth)])
async def do_key(body: KeyBody):
    resp = await run_in_threadpool(daemon.key, body.name)
    return {"response": resp}


@app.post("/mod", dependencies=[Depends(require_auth)])
async def do_mod(body: ModBody):
    resp = await run_in_threadpool(daemon.mod, body.combo)
    return {"response": resp}


@app.post("/media", dependencies=[Depends(require_auth)])
async def do_media(body: MediaBody):
    resp = await run_in_threadpool(daemon.media, body.name)
    return {"response": resp}


@app.post("/command", dependencies=[Depends(require_auth)])
async def do_command(body: CommandBody):
    resp = await run_in_threadpool(daemon.raw, body.raw)
    return {"response": resp}


@app.websocket("/events")
async def events(ws: WebSocket, token: str | None = None):
    if AUTH_TOKEN is not None and token != AUTH_TOKEN:
        await ws.close(code=1008)   # policy violation
        return
    await manager.connect(ws)
    # Send a snapshot of current state on connect.
    await ws.send_text(json.dumps({"event": "SNAPSHOT", "state": daemon.state}))
    try:
        while True:
            await ws.receive_text()   # we ignore inbound; keeps the socket open
    except WebSocketDisconnect:
        manager.disconnect(ws)


# ── Entry point ──────────────────────────────────────────────────────────────────
def main():
    global daemon, AUTH_TOKEN

    parser = argparse.ArgumentParser(
        description="HTTP + WebSocket bridge to the ESP32 BLE keyboard emulator",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--net-host", default="127.0.0.1",
                        help="Network interface to bind (default: 127.0.0.1, localhost only)")
    parser.add_argument("--net-port", type=int, default=8000,
                        help="Network port to listen on (default: 8000)")
    parser.add_argument("--serial-port", "-p", default=None,
                        help="ESP32 serial port (auto-detected if omitted)")
    parser.add_argument("--baud", "-b", type=int, default=115200)
    args = parser.parse_args()

    AUTH_TOKEN = os.environ.get("HARDLOOP_TOKEN")

    if args.net_host not in LOCALHOSTS and not AUTH_TOKEN:
        print("Refusing to bind to a non-localhost address without HARDLOOP_TOKEN set.")
        print("Set a token first, e.g.:")
        print("  Linux/macOS:  export HARDLOOP_TOKEN=your-secret")
        print("  Windows PS:   $env:HARDLOOP_TOKEN = 'your-secret'")
        sys.exit(1)

    if AUTH_TOKEN is None:
        print("[bridge] WARNING: no HARDLOOP_TOKEN set — running without auth (localhost only).")

    serial_port = args.serial_port or find_esp32_port()
    daemon = HardloopDaemon(serial_port, args.baud)

    uvicorn.run(app, host=args.net_host, port=args.net_port)


if __name__ == "__main__":
    main()
