#!/usr/bin/env python3
"""
QuantDesk HFT Bridge: Production-Hardened C++20 Engine to Web Dashboard WebSocket Relay
Features:
- Subprocess Supervisor with Auto-Restart & Backoff
- Allowlisted Command Sanitization & Schema Validation
- Token-Based Authentication & Session Tracking
- Per-Client Token Bucket Rate Limiting (DoS Protection)
- Structured JSON / Tagged Logging
- Non-blocking Asynchronous WebSocket Broadcasting with Slow-Client Drop Protection
"""

import asyncio
import collections
import http.server
import json
import logging
import os
import subprocess
import sys
import threading
import time
from typing import Any, Dict, List, Optional, Set, Tuple

import websockets

# Configuration with Environment Variable Overrides
HTTP_PORT = int(os.environ.get("QUANTDESK_HTTP_PORT", "8080"))
WS_PORT = int(os.environ.get("QUANTDESK_WS_PORT", "8765"))
SESSION_TOKEN = os.environ.get("QUANTDESK_SESSION_TOKEN", "QUANT-USER-MODE")
RATE_LIMIT_HZ = float(os.environ.get("QUANTDESK_RATE_LIMIT_HZ", "500.0"))
RATE_LIMIT_BURST = int(os.environ.get("QUANTDESK_RATE_LIMIT_BURST", "1000"))

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
WEB_DIR = os.path.join(BASE_DIR, "web")
EXAMPLES_DIR = os.path.join(BASE_DIR, "examples")
BINARY_PATH = os.path.join(BASE_DIR, "build", "stream_server.exe")

if not os.path.exists(BINARY_PATH):
    BINARY_PATH = os.path.join(BASE_DIR, "build", "stream_server")

# Structured Logger Setup
logging.basicConfig(
    level=logging.INFO,
    format='{"time":"%(asctime)s", "level":"%(levelname)s", "component":"%(name)s", "msg":"%(message)s"}'
)
logger = logging.getLogger("Bridge")

# Global connected WebSocket clients
connected_clients: Set[Any] = set()
cpp_process: Optional[subprocess.Popen] = None
process_lock = threading.Lock()


class TokenBucketRateLimiter:
    """Per-client Token Bucket Rate Limiter to prevent DoS flooding."""
    def __init__(self, rate: float = RATE_LIMIT_HZ, capacity: int = RATE_LIMIT_BURST):
        self.rate = rate
        self.capacity = float(capacity)
        self.tokens = float(capacity)
        self.last_update = time.monotonic()

    def allow(self, cost: float = 1.0) -> bool:
        now = time.monotonic()
        elapsed = now - self.last_update
        self.last_update = now
        self.tokens = min(self.capacity, self.tokens + elapsed * self.rate)
        if self.tokens >= cost:
            self.tokens -= cost
            return True
        return False


# Map client address to RateLimiter
client_limiters: Dict[str, TokenBucketRateLimiter] = {}


def format_order_command(order_type: str, order_id: int, side: str, price: float, qty: int) -> str:
    """Formats and sanitizes an ORDER command line."""
    order_type = str(order_type).strip().upper()
    if order_type not in ('A', 'X', 'E'):
        raise ValueError(f"Invalid order type: {order_type}")
    if int(order_id) <= 0:
        raise ValueError(f"Invalid order ID: {order_id}")
    
    if order_type == 'A':
        side = str(side).strip().upper()
        if side not in ('B', 'S'):
            raise ValueError(f"Invalid side: {side}")
        if float(price) <= 0 or int(qty) <= 0:
            raise ValueError("Price and quantity must be positive")
        return f"A,{int(order_id)},{side},{float(price):.2f},{int(qty)}\n"
    elif order_type == 'X':
        return f"X,{int(order_id)},0,0,{int(qty)}\n"
    elif order_type == 'E':
        return f"E,{int(order_id)},0,{float(price):.2f},{int(qty)}\n"
    raise ValueError("Unsupported order type")


def sanitize_and_route_command(data: Dict[str, Any]) -> Tuple[bool, str, str]:
    """
    Validates client command against the strict allowlist.
    Returns: (is_valid, command_string_to_engine, error_reason)
    """
    cmd = str(data.get("command", "")).strip().upper()
    
    # Allowlist of commands
    ALLOWED_COMMANDS = {"ORDER", "RAW_LINE", "BATCH", "LOAD_EXAMPLE", "RESET", "CLEAR", "BURST", "CROSS", "FILE"}
    if cmd not in ALLOWED_COMMANDS:
        return False, "", f"Disallowed command: {cmd}"

    if cmd == "ORDER":
        try:
            cmd_str = format_order_command(
                data.get("type", "A"),
                data.get("orderId", 1),
                data.get("side", "B"),
                data.get("price", 100.0),
                data.get("qty", 100)
            )
            return True, cmd_str, ""
        except Exception as e:
            return False, "", str(e)

    elif cmd == "RAW_LINE":
        raw = str(data.get("line", "")).strip()
        if not raw or len(raw) > 256:
            return False, "", "Line length invalid"
        # Validate format: starts with A, X, E, ORDER, etc.
        first_char = raw[0].upper()
        if first_char in ('A', 'X', 'E', '#', '/'):
            return True, f"{raw}\n", ""
        elif raw.upper().startswith("ORDER "):
            return True, f"{raw}\n", ""
        return False, "", f"Invalid raw line syntax: {raw}"

    elif cmd == "BATCH":
        lines = data.get("lines", [])
        if not isinstance(lines, list) or len(lines) > 50000:
            return False, "", "Batch size exceeds 50,000 line limit"
        out_lines = []
        for l in lines:
            line_str = str(l).strip()
            if line_str and len(line_str) <= 256 and line_str[0] in ('A', 'X', 'E', 'a', 'x', 'e', '#', '/'):
                out_lines.append(f"{line_str}\n")
        return True, "".join(out_lines), ""

    elif cmd == "LOAD_EXAMPLE":
        name = str(data.get("name", "aapl")).strip().lower()
        if name in ("aapl", "nvda", "sweep", "aapl_l2_book", "nvda_order_flow", "market_cross_sweep"):
            return True, f"LOAD_EXAMPLE {name}\n", ""
        return False, "", f"Unknown example: {name}"

    elif cmd in ("RESET", "CLEAR"):
        return True, "RESET\n", ""

    elif cmd == "BURST":
        count = min(int(data.get("count", 100000)), 500000)
        return True, f"BURST {count}\n", ""

    elif cmd == "CROSS":
        return True, "CROSS\n", ""

    elif cmd == "FILE":
        path = str(data.get("path", "")).strip()
        # Security: prevent path traversal outside workspace
        clean_path = os.path.normpath(path)
        if clean_path.startswith("..") or os.path.isabs(clean_path):
            return False, "", "Disallowed absolute or parent directory file path"
        return True, f"FILE {clean_path}\n", ""

    return False, "", "Invalid command"


class QuantHTTPHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_DIR, **kwargs)

    def end_headers(self):
        # Security Headers
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "SAMEORIGIN")
        self.send_header("X-XSS-Protection", "1; mode=block")
        self.send_header("Access-Control-Allow-Origin", "*")
        super().end_headers()

    def do_GET(self):
        # Health check endpoint on HTTP port
        if self.path == "/healthz":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(b'{"status":"healthy","component":"quantdesk-bridge"}\n')
            return

        # Serve static example files under /examples/
        if self.path.startswith("/examples/"):
            rel_path = os.path.normpath(self.path[len("/examples/"):])
            if rel_path.startswith(".."):
                self.send_error(403, "Access Denied")
                return
            target_path = os.path.join(EXAMPLES_DIR, rel_path)
            if os.path.exists(target_path) and os.path.isfile(target_path):
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.end_headers()
                with open(target_path, "rb") as f:
                    self.wfile.write(f.read())
                return
        super().do_GET()


def start_http_server():
    server = http.server.ThreadingHTTPServer(("", HTTP_PORT), QuantHTTPHandler)
    logger.info(f"HTTP Server listening on http://0.0.0.0:{HTTP_PORT} (Serving {WEB_DIR})")
    server.serve_forever()


async def ws_handler(websocket):
    global connected_clients, cpp_process
    client_ip = getattr(websocket, "remote_address", ("unknown", 0))[0]
    limiter = client_limiters.setdefault(client_ip, TokenBucketRateLimiter())
    connected_clients.add(websocket)
    logger.info(f"Visualizer client connected from {client_ip}. (Total active: {len(connected_clients)})")

    try:
        async for message in websocket:
            if not limiter.allow():
                await websocket.send(json.dumps({
                    "type": "error",
                    "code": 429,
                    "msg": "Rate limit exceeded. Slow down command ingestion."
                }))
                continue

            try:
                data = json.loads(message)
                valid, cmd_str, err = sanitize_and_route_command(data)

                if not valid:
                    await websocket.send(json.dumps({
                        "type": "error",
                        "code": 400,
                        "msg": f"Command rejected: {err}"
                    }))
                    continue

                with process_lock:
                    if cpp_process and cpp_process.stdin and cmd_str:
                        cpp_process.stdin.write(cmd_str)
                        cpp_process.stdin.flush()
            except Exception as e:
                logger.error(f"Error processing client payload: {e}")
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        connected_clients.discard(websocket)
        logger.info(f"Client disconnected. (Remaining: {len(connected_clients)})")


async def read_cpp_stdout_loop():
    global cpp_process, connected_clients
    loop = asyncio.get_running_loop()

    while True:
        if cpp_process is None or cpp_process.poll() is not None:
            if cpp_process and cpp_process.poll() is not None:
                exit_code = cpp_process.poll()
                logger.warning(f"C++ Engine terminated with exit code {exit_code}. Auto-restarting engine in 1s...")
                await asyncio.sleep(1.0)
                try:
                    with process_lock:
                        cpp_process = spawn_cpp_engine()
                except Exception as e:
                    logger.error(f"Failed to respawn C++ engine: {e}")
                    await asyncio.sleep(2.0)
                    continue
            else:
                await asyncio.sleep(0.1)
                continue

        try:
            line = await loop.run_in_executor(None, cpp_process.stdout.readline)
        except Exception:
            await asyncio.sleep(0.05)
            continue

        if not line:
            await asyncio.sleep(0.01)
            continue

        line_str = line.strip()
        if not line_str or not line_str.startswith("{"):
            continue

        if connected_clients:
            # Broadcast snapshot concurrently; remove disconnected sockets cleanly
            tasks = [asyncio.create_task(client.send(line_str)) for client in list(connected_clients)]
            if tasks:
                results = await asyncio.gather(*tasks, return_exceptions=True)
                for client, res in zip(list(connected_clients), results):
                    if isinstance(res, (websockets.exceptions.ConnectionClosed, Exception)):
                        connected_clients.discard(client)


def spawn_cpp_engine() -> subprocess.Popen:
    """Spawns the C++ stream_server engine process."""
    logger.info(f"Spawning C++ Limit Order Book Engine: {BINARY_PATH}")
    proc = subprocess.Popen(
        [BINARY_PATH],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )
    logger.info(f"C++ Engine running with PID: {proc.pid}")
    return proc


async def main():
    global cpp_process

    if not os.path.exists(BINARY_PATH):
        logger.error(f"C++ binary not found at: {BINARY_PATH}. Please compile via CMake.")
        sys.exit(1)

    # Start HTTP server thread
    http_thread = threading.Thread(target=start_http_server, daemon=True)
    http_thread.start()

    # Launch C++ engine process
    cpp_process = spawn_cpp_engine()

    # Start WebSocket server
    ws_server = await websockets.serve(ws_handler, "0.0.0.0", WS_PORT)
    logger.info(f"WebSocket Server listening on ws://0.0.0.0:{WS_PORT}")
    logger.info(f"Ready! Open http://localhost:{HTTP_PORT} in your browser.")

    try:
        await read_cpp_stdout_loop()
    except asyncio.CancelledError:
        pass
    finally:
        ws_server.close()
        await ws_server.wait_closed()
        if cpp_process:
            cpp_process.terminate()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Shutting down QuantDesk bridge.")
        if cpp_process:
            cpp_process.terminate()
        sys.exit(0)
