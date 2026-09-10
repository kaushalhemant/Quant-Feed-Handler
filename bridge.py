#!/usr/bin/env python3
"""
QuantDesk HFT Bridge: C++20 Engine to Web Dashboard WebSocket Relay
Spawns stream_server.exe, reads real JSON telemetry lines from stdout,
broadcasts them over WebSocket (ws://localhost:8765), and relays UI commands to stdin.
Also serves the web dashboard files on http://localhost:8080.
"""

import asyncio
import functools
import http.server
import json
import os
import subprocess
import sys
import threading
import websockets

HTTP_PORT = 8080
WS_PORT = 8765
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
WEB_DIR = os.path.join(BASE_DIR, "web")
EXAMPLES_DIR = os.path.join(BASE_DIR, "examples")
BINARY_PATH = os.path.join(BASE_DIR, "build", "stream_server.exe")

if not os.path.exists(BINARY_PATH):
    BINARY_PATH = os.path.join(BASE_DIR, "build", "stream_server")

# Global connected WebSocket clients
connected_clients = set()
cpp_process = None

class QuantHTTPHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=WEB_DIR, **kwargs)

    def do_GET(self):
        # Allow serving example files if requested under /examples/
        if self.path.startswith("/examples/"):
            rel_path = self.path[len("/examples/"):]
            target_path = os.path.join(EXAMPLES_DIR, rel_path)
            if os.path.exists(target_path) and os.path.isfile(target_path):
                self.send_response(200)
                self.send_header("Content-Type", "text/plain")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.end_headers()
                with open(target_path, "rb") as f:
                    self.wfile.write(f.read())
                return
        super().do_GET()

def start_http_server():
    server = http.server.ThreadingHTTPServer(("", HTTP_PORT), QuantHTTPHandler)
    print(f"[Bridge] HTTP Server serving {WEB_DIR} on http://localhost:{HTTP_PORT}")
    server.serve_forever()

async def ws_handler(websocket):
    global connected_clients, cpp_process
    connected_clients.add(websocket)
    print(f"[Bridge] Web visualizer client connected. (Total clients: {len(connected_clients)})")

    try:
        async for message in websocket:
            try:
                data = json.loads(message)
                cmd = data.get("command", "")

                if cmd == "ORDER":
                    # User manual order placement
                    order_type = data.get("type", "A")
                    order_id = data.get("orderId", 1)
                    side = data.get("side", "B")
                    price = data.get("price", 100.0)
                    qty = data.get("qty", 100)
                    cmd_str = f"ORDER {order_type} {order_id} {side} {price} {qty}\n"
                elif cmd == "RAW_LINE":
                    raw = data.get("line", "").strip()
                    cmd_str = f"{raw}\n"
                elif cmd == "BATCH":
                    # User batch injection
                    lines = data.get("lines", [])
                    cmd_str = ""
                    for line in lines:
                        l = line.strip()
                        if l:
                            cmd_str += f"{l}\n"
                elif cmd == "LOAD_EXAMPLE":
                    name = data.get("name", "aapl")
                    cmd_str = f"LOAD_EXAMPLE {name}\n"
                elif cmd == "RESET" or cmd == "CLEAR":
                    cmd_str = "RESET\n"
                elif cmd == "BURST":
                    count = data.get("count", 100000)
                    cmd_str = f"BURST {count}\n"
                elif cmd == "CROSS":
                    cmd_str = "CROSS\n"
                elif cmd == "FILE":
                    path = data.get("path", "")
                    cmd_str = f"FILE {path}\n"
                else:
                    cmd_str = f"{cmd}\n"

                if cpp_process and cpp_process.stdin and cmd_str:
                    cpp_process.stdin.write(cmd_str)
                    cpp_process.stdin.flush()
            except Exception as e:
                print(f"[Bridge] Error processing client command: {e}")
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        connected_clients.remove(websocket)
        print(f"[Bridge] Client disconnected. (Remaining: {len(connected_clients)})")

async def read_cpp_stdout_loop():
    global cpp_process, connected_clients
    loop = asyncio.get_running_loop()

    while True:
        if cpp_process is None or cpp_process.poll() is not None:
            await asyncio.sleep(0.1)
            continue

        line = await loop.run_in_executor(None, cpp_process.stdout.readline)
        if not line:
            await asyncio.sleep(0.01)
            continue

        line_str = line.strip()
        if not line_str or not line_str.startswith("{"):
            continue

        if connected_clients:
            # Broadcast JSON snapshot to all connected browser tabs
            websockets_tasks = [client.send(line_str) for client in list(connected_clients)]
            if websockets_tasks:
                await asyncio.gather(*websockets_tasks, return_exceptions=True)

async def main():
    global cpp_process

    if not os.path.exists(BINARY_PATH):
        print(f"[Error] C++ stream_server binary not found at: {BINARY_PATH}")
        print("Please build it first using: cmake --build build --config Release")
        sys.exit(1)

    print("=================================================================")
    print("       QUANTDESK HFT BRIDGE: C++20 ENGINE TO WEB DASHBOARD       ")
    print("=================================================================")
    print(f"  HTTP Web Dashboard  : http://localhost:{HTTP_PORT}")
    print(f"  WebSocket Stream    : ws://localhost:{WS_PORT}")
    print(f"  C++ Engine Binary   : {BINARY_PATH}")
    print("=================================================================\n")

    # Start background HTTP server
    http_thread = threading.Thread(target=start_http_server, daemon=True)
    http_thread.start()

    # Launch C++ stream_server process with piped stdin/stdout
    print("[Bridge] Spawning C++20 Limit Order Book Engine process...")
    cpp_process = subprocess.Popen(
        [BINARY_PATH],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )
    print(f"[Bridge] C++ Engine running (PID: {cpp_process.pid})")

    # Start WebSocket server
    ws_server = await websockets.serve(ws_handler, "0.0.0.0", WS_PORT)
    print(f"[Bridge] WebSocket server listening on ws://0.0.0.0:{WS_PORT}")
    print("[Bridge] Ready! Open http://localhost:8080 in your browser to view LIVE C++ data.")

    # Run stdout ingestion loop
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
        print("\n[Bridge] Shutting down.")
        if cpp_process:
            cpp_process.terminate()
        sys.exit(0)
