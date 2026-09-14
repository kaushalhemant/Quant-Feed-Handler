# QuantDesk: Ultra-Low Latency C++20 Market Data Feed Handler & Limit Order Book Engine

[![CI Build & Test](https://github.com/kaushalhemant/Quant-Feed-Handler/actions/workflows/ci.yml/badge.svg)](https://github.com/kaushalhemant/Quant-Feed-Handler/actions)
[![C++20](https://img.shields.io/badge/Standard-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/Build-CMake%203.20%2B-green.svg)](https://cmake.org/)
[![Sanitizers](https://img.shields.io/badge/Sanitizers-ASan%20%7C%20UBSan%20%7C%20TSan-brightgreen.svg)]()
[![Tests](https://img.shields.io/badge/Tests-100%25%20Passing%20(8%2F8%20CTest%2C%2011%2F11%20Pytest)-brightgreen.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)

QuantDesk is an institutional-grade, zero-allocation C++20 reference implementation of a high-frequency trading (HFT) market data feed handler and Level 2 Limit Order Book (LOB) engine. It features sub-microsecond deterministic wire-to-book processing, lock-free SPSC communication, an open-addressing Fibonacci hash table, a bounded array LOB, native Prometheus histogram observability, a hardened Python WebSocket bridge, and a responsive, accessible Web Dashboard.

---

## ⚠️ Scope & Compliance Disclaimer

> **IMPORTANT NOTICE:**  
> QuantDesk is an **educational and reference market data feed handler and Limit Order Book reconstruction engine**.  
> - **Not an Execution Venue / Matching Engine:** This system reconstructs book state from downstream market data feeds (ITCH / binary wire format); it does not match or cross resting orders.
> - **Not a Broker-Dealer Platform:** It does not provide direct market access (DMA), sponsored access, or live exchange order routing (FIX / OUCH / binary order entry).
> - **Not Regulated Trading Infrastructure:** It does not implement pre-trade risk controls (e.g., SEC Rule 15c3-5), regulatory transaction reporting (e.g., CAT, MiFID II RTS 25 clock synchronization), or clearing/settlement workflows.

---

## 🏛️ System Architecture

QuantDesk utilizes a multi-threaded producer-consumer architecture with physical CPU core pinning and lock-free cache-line-isolated IPC:

```
                      [ Dual UDP Market Feeds: Feed A & Feed B ]
                                          │
                                          ▼
                ┌────────────────────────────────────────────────────────┐
                │  Core 0: Network Ingestion & Framing (Producer)        │
                │  • Non-blocking UDP Socket (SO_RCVBUF 8MB)             │
                │  • DualFeedArbitrator (A/B Deduplication & Gap Track)  │
                │  • Length-Validated ITCH Packet Framing                │
                │  • WALJournal (Memory-Mapped Crash-Resilient Write-Log)│
                └─────────────────────────┬──────────────────────────────┘
                                          │
                                          ▼ (std::memory_order_release)
                ┌────────────────────────────────────────────────────────┐
                │  Lock-Free SPSCRingBuffer (alignas(64), Zero-Lock)     │
                └─────────────────────────┬──────────────────────────────┘
                                          │
                                          ▼ (std::memory_order_acquire)
                ┌────────────────────────────────────────────────────────┐
                │  Core 1: Order Book Calculation Worker (Consumer)      │
                │  • Open-Addressing Hash Table (Fibonacci Hash, O(1))   │
                │  • Contiguous Top-5 Level Bubbling (O(Depth))          │
                │  • Real-Time Analytics (BBO, Micro-Price, Spread)      │
                │  • Prometheus Metrics Exporter (Histograms + Health)   │
                └─────────────────────────┬──────────────────────────────┘
                                          │ (Stdout / Unix Pipe)
                                          ▼
                ┌────────────────────────────────────────────────────────┐
                │  Python Bridge (bridge.py)                             │
                │  • Token Bucket Rate Limiting (DoS Protection)         │
                │  • Strict Command Allowlisting & Sanitization          │
                │  • WebSocket Telemetry Broadcast (8765)                │
                │  • HTTP Dashboard Server with Security Headers (8080)  │
                └─────────────────────────┬──────────────────────────────┘
                                          │
                                          ▼
                ┌────────────────────────────────────────────────────────┐
                │  Web Dashboard (web/index.html & app.js)               │
                │  • Real-Time L2 Ladder & Depth Visualizer              │
                │  • High-Contrast Colorblind-Safe Theme                 │
                │  • Exponential Backoff WebSocket Auto-Reconnection     │
                │  • Full ARIA Screen-Reader Accessibility               │
                └────────────────────────────────────────────────────────┘
```

---

## 📐 Architecture Decision Records (ADRs)

Key architectural decisions are documented in detail:
- [ADR-001: Lock-Free Single-Producer Single-Consumer (SPSC) Queue](ADR/ADR-001-lock-free-spsc-queue.md)
- [ADR-002: Contiguous Bounded Array for Top-N Limit Order Book Depth](ADR/ADR-002-bounded-array-lob.md)
- [ADR-003: Fibonacci Multiplicative Hashing for Order Tracking](ADR/ADR-003-fibonacci-multiplicative-hashing.md)
- [ADR-004: Memory-Mapped Write-Ahead Logging (WAL) for Crash Resilience](ADR/ADR-004-memory-mapped-wal-persistence.md)

---

## ⚡ Core Low-Latency Design Principles

| Component | Technical Decision | Hardware / Latency Rationale |
|---|---|---|
| **Wire Protocol** | `#pragma pack(push, 1)` binary union structs | Eliminates field serialization overhead; messages parsed directly from contiguous socket buffers. |
| **Hot Path Memory** | Zero runtime heap allocations (`malloc`/`new`) | Pre-allocated static arrays eliminate allocator lock contention, page faults, and GC jitter. |
| **Concurrency** | Lock-Free SPSC Queue (`std::atomic`) | Thread handoff using acquire-release semantics without kernel mutexes or futex wakeups. |
| **Cache Line Isolation** | `alignas(64)` on Producer/Consumer pointers | Eliminates false sharing across CPU core cache lines. |
| **Order Lookup** | Open-Addressing Hash Map (Fibonacci Hash) | Avoids pointer chasing and node allocations of `std::unordered_map`; contiguous flat arrays maximize L1D prefetcher efficiency. |
| **Order Book Depth** | Contiguous Top-$N$ Sorted Arrays ($N \le 10$) | Bounded insertion-sort level shifts outperform red-black trees due to localized cache hits and branch predictability. |
| **OS Jitter Isolation** | Hard Thread Affinity (`SetThreadAffinityMask` / `pthread_setaffinity_np`) | Pins network ingestion and order book calculation to dedicated isolated physical cores. |

---

## 📊 Benchmark Latency Profile

> **Environment:** MinGW GCC 16.1.0 / x86-64 Native / Windows 11 / Linux 6.x  
> **Compilation Flags:** `-std=c++20 -O3 -march=native -flto`  
> **Workload:** 1,000,000 mixed ITCH messages (65% Add, 25% Cancel/Modify, 10% Execute)

```
=================================================================
               HFT BENCHMARK & LATENCY PROFILE                   
=================================================================
  Total Packets Ingested : 1,000,000
  Total Wall Time        : 383.358 ms
  Effective Throughput   : 2,608,526 msgs/sec
-----------------------------------------------------------------
  Latency Distribution   : (Nanoseconds per message)
    Min Latency          :     0.00 ns
    Mean Latency         :   302.27 ns (StdDev: 2292.47 ns)
    50.00th (Median)     :   300.00 ns
    90.00th Percentile   :   400.00 ns
    95.00th Percentile   :   500.00 ns
    99.00th Percentile   :   700.00 ns  [Tail Latency]
    99.90th Percentile   :  6400.00 ns
    99.99th Percentile   : 30600.02 ns
    Max Latency          : 995100.00 ns
=================================================================
```

---

## 🔨 Build & Testing

### Prerequisites
- C++20 compliant compiler (`GCC 12+`, `Clang 15+`, or `MSVC 2022+`)
- `CMake 3.20+`
- Python `3.10+` with dependencies: `pip install -r requirements.txt`

### Build with CMake Presets

```bash
# Configure and build Release preset
cmake --preset release
cmake --build --preset release

# Or standard CMake:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Sanitizer Builds (ASan / UBSan / TSan)

```bash
# ASan + UBSan
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan

# ThreadSanitizer
cmake --preset tsan
cmake --build --preset tsan
```

### Run Full Test Suite

```bash
# 1. Run all C++ Unit & Property Tests (8 test suites)
ctest --test-dir build --output-on-failure

# 2. Run Python Bridge Security & Integration Tests (11 tests)
pytest tests/test_bridge.py -v
```

---

## 🚀 Running QuantDesk

### Option A: Local Native Execution

```bash
# Start the Python WebSocket bridge and HTTP server
python bridge.py
```
Open **`http://localhost:8080`** in your browser to access the Web Dashboard.

### Option B: Docker Compose (Full Stack)

Spins up the C++ Engine, Python Bridge, Prometheus, and Grafana:

```bash
docker compose up -d
```
- **Web Dashboard:** `http://localhost:8080`
- **Prometheus UI:** `http://localhost:9091`
- **Grafana Dashboards:** `http://localhost:3000` (admin/admin)
- **Engine Metrics & Health:** `http://localhost:9090/metrics` / `http://localhost:9090/healthz`

### Option C: Kubernetes

```bash
kubectl apply -f k8s/configmap.yaml
kubectl apply -f k8s/deployment.yaml
kubectl apply -f k8s/service.yaml
```

---

## ⚙️ Configuration

QuantDesk can be configured via `config/quantdesk.json` or environment variable overrides:

| Parameter | Environment Variable | Default | Description |
|---|---|---|---|
| `http_port` | `QUANTDESK_HTTP_PORT` | `8080` | Web dashboard HTTP server port |
| `ws_port` | `QUANTDESK_WS_PORT` | `8765` | WebSocket live telemetry broadcast port |
| `metrics_port` | `QUANTDESK_METRICS_PORT` | `9090` | Prometheus metrics and health endpoint port |
| `ring_buffer_capacity` | `QUANTDESK_RING_CAPACITY` | `1048576` | SPSC ring buffer slot capacity |
| `lob_depth` | `QUANTDESK_LOB_DEPTH` | `5` | Active visible Limit Order Book depth levels |
| `rate_limit_capacity` | `QUANTDESK_RATE_LIMIT` | `200` | Token bucket maximum burst capacity |

See [`CONFIG.md`](CONFIG.md) for full configuration specifications and schemas.

---

## 📂 Repository Structure

```
├── .github/workflows/ci.yml    # GitHub Actions multi-platform matrix CI pipeline
├── ADR/                        # Architecture Decision Records (ADR-001 to ADR-004)
├── CMakeLists.txt              # Production build config with Sanitizers, IPO/LTO, C++20
├── CMakePresets.json           # Standardized presets: Release, Debug, ASan, TSan, Coverage
├── CONFIG.md                   # Configuration reference & environment variable schema
├── CONTRIBUTING.md             # Developer contribution guidelines & code standards
├── CODE_OF_CONDUCT.md         # Contributor Covenant Code of Conduct
├── Doxyfile                    # Doxygen API documentation configuration
├── LICENSE                     # MIT License
├── PRODUCTION_READINESS.md     # Production-readiness audit report across 12 phases
├── config/                     # Configuration files (quantdesk.json)
├── docker-compose.yml          # Multi-container orchestration (Engine, Bridge, Prom, Grafana)
├── Dockerfile.engine           # Multi-stage C++20 engine container build
├── Dockerfile.bridge           # Hardened Python WebSocket bridge container build
├── grafana/                    # Grafana telemetry dashboard & provisioning configs
├── k8s/                        # Kubernetes Deployment, Service, and ConfigMap manifests
├── prometheus/                 # Prometheus metrics scraper & SLA alert rules
├── systemd/                    # Systemd unit files for bare-metal Linux deployment
├── include/feed_handler/       # C++20 Zero-Allocation Header-Only Core Engine
│   ├── protocol.hpp            # Packed binary ITCH wire protocol & bounds checkers
│   ├── ring_buffer.hpp         # Cache-aligned lock-free SPSC ring buffer (alignas(64))
│   ├── order_table.hpp         # Open-addressing Fibonacci hash table with tombstone handling
│   ├── order_book.hpp          # Contiguous top-N depth Limit Order Book engine
│   ├── dual_feed_arbitrator.hpp# A/B line arbitration & sequence gap tracking
│   ├── wal_journal.hpp         # Memory-mapped crash-resilient Write-Ahead Log
│   ├── parser_validator.hpp    # Strict format parsing & injection defense
│   ├── metrics_exporter.hpp    # Zero-allocation Prometheus metrics & histogram server
│   ├── udp_receiver.hpp        # Non-blocking cross-platform UDP socket wrapper
│   ├── cpu_affinity.hpp        # Physical thread affinity & thread priority utilities
│   └── latency_tracker.hpp     # Nanosecond latency percentile & distribution tracker
├── src/                        # Executables & CLI drivers
│   ├── main.cpp                # High-speed benchmark & dual-mode engine driver
│   ├── stream_server.cpp       # Headless streaming daemon with Prometheus exporter
│   └── mock_exchange.cpp       # Dedicated UDP market data broadcaster tool
├── tests/                      # Automated Test Suite (CTest & Pytest)
│   ├── test_order_book.cpp     # Unit & Property-Based Invariant Tests
│   ├── test_ring_buffer.cpp    # Multi-threaded concurrent SPSC stress tests
│   ├── test_order_table.cpp    # Hash map collision & probe chain tests
│   ├── test_arbitrator.cpp     # Dual-feed packet arbitration & gap tests
│   ├── test_wal_journal.cpp    # WAL crash recovery & reconstruction tests
│   ├── test_latency_tracker.cpp# Nanosecond percentile calculation tests
│   ├── test_metrics_exporter.cpp# Prometheus server lifecycle tests
│   ├── fuzz_parser.cpp         # Fuzzing target for parser injection defense
│   └── test_bridge.py          # Python bridge security, rate limiting & WS tests
└── web/                        # Production Web Dashboard
    ├── index.html              # Accessible HTML5 UI with ARIA, live ladder & Features modal
    ├── features.html           # Dedicated Asymmetric Bento Box Feature Grid showcase
    ├── style.css               # Responsive styling with Colorblind High-Contrast mode
    ├── bento-grid.css          # Vanilla CSS compatibility module for Bento Grid
    ├── bento-grid.tailwind.css # Tailwind CSS v4 @theme input specification
    └── app.js                  # Auto-reconnecting WebSocket engine & canvas visualizer
```

---

## 📜 License

QuantDesk is open-sourced under the [MIT License](LICENSE).
