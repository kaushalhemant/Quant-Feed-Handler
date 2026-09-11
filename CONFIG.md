# QuantDesk Configuration Reference

This document outlines all operational knobs, configuration parameters, and environment variable overrides for the **QuantDesk HFT Engine & WebSocket Bridge**.

---

## ⚙️ Configuration Schema

QuantDesk supports runtime configuration via `config/quantdesk.json` or through standard environment variables:

| Configuration Key | Environment Variable | Default Value | Description |
|---|---|---|---|
| `server.http_port` | `QUANTDESK_HTTP_PORT` | `8080` | TCP port for static dashboard web server. |
| `server.ws_port` | `QUANTDESK_WS_PORT` | `8765` | TCP port for the live WebSocket telemetry & command stream. |
| `server.metrics_port` | `QUANTDESK_METRICS_PORT`| `9090` | TCP port for the embedded Prometheus `/metrics` and `/healthz` endpoint. |
| `security.session_token` | `QUANTDESK_SESSION_TOKEN`| `QUANT-USER-MODE` | Shared secret token for authenticated client connections. |
| `security.rate_limit_hz` | `QUANTDESK_RATE_LIMIT_HZ`| `500.0` | Token bucket sustained ingestion rate per client IP (messages/sec). |
| `security.rate_limit_burst` | `QUANTDESK_RATE_LIMIT_BURST` | `1000` | Token bucket maximum burst capacity per client IP. |
| `engine.affinity.producer_core` | `QUANTDESK_CORE_PRODUCER` | `0` | Physical CPU core pinned to the network ingestion thread. |
| `engine.affinity.consumer_core` | `QUANTDESK_CORE_CONSUMER` | `1` | Physical CPU core pinned to the order book matching worker. |
| `engine.ring_buffer_capacity` | `QUANTDESK_RING_BUFFER_CAP` | `65536` | SPSC queue capacity (must be a power of two). |
| `engine.lob_depth` | `QUANTDESK_LOB_DEPTH` | `5` | Visible Level 2 price depth per side (Bids and Asks). |
| `engine.socket_rcvbuf_bytes` | `QUANTDESK_SO_RCVBUF` | `8388608` | UDP kernel receive buffer size (8 MB) to prevent packet loss. |

---

## 🚀 Environment Variable Examples

### Production Staging Run:
```bash
export QUANTDESK_HTTP_PORT=80
export QUANTDESK_WS_PORT=443
export QUANTDESK_SESSION_TOKEN="DESK-SECURE-PROD-2026"
export QUANTDESK_RATE_LIMIT_HZ=1000.0
python bridge.py
```

### High-Throughput Dedicated Bare-Metal Run:
```bash
export QUANTDESK_CORE_PRODUCER=2
export QUANTDESK_CORE_CONSUMER=3
./build/feed_handler --live 12345
```
