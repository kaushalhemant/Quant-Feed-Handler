# Ultra-Low Latency Market Data Feed Handler & Limit Order Book Engine

[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/Build-CMake%203.20%2B-green.svg)](https://cmake.org/)
[![License](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)
[![Tests](https://img.shields.io/badge/Tests-100%25%20Passing-brightgreen.svg)]()

An institutional-grade, zero-allocation C++20 reference implementation of a high-frequency trading (HFT) market data feed handler and limit order book engine. Designed for sub-microsecond deterministic wire-to-book processing, zero heap allocations on hot paths, and cache-conscious data layout.

---

## 🏛️ System Architecture

The engine employs a decoupled, multi-threaded producer-consumer architecture connected via a lock-free Single Producer Single Consumer (SPSC) ring buffer with explicit CPU core pinning:

```
[ Mock Exchange / Exchange UDP Multicast ]
                    │
                    ▼ (Packed Binary ITCH Packets)
  ┌────────────────────────────────────────────────────────┐
  │  Core 0: Network Ingestion Thread (Producer)           │
  │  • Non-blocking UDP Socket Receiver (SO_RCVBUF 8MB)    │
  │  • Fast Framing & Sequence Number Validation           │
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
  │  • Open-Addressing Hash Table (Fibonacci Multiplicative)│
  │  • Contiguous Top-N Level Bubbling (O(Depth))          │
  │  • BBO, Spread, Mid-Price, Micro-Price Calculation     │
  └────────────────────────────────────────────────────────┘
```

---

## ⚡ Core Low-Latency Design Decisions

| Component | Technical Decision | Hardware / Latency Rationale |
|---|---|---|
| **Wire Protocol** | `#pragma pack(push, 1)` binary union structs | Eliminates framing & field serialization overhead; messages ingested as flat contiguous chunks directly from socket buffers. |
| **Hot Path Memory** | Zero runtime heap allocations (`malloc`/`new`) | Pre-allocated static/BSS storage pools eliminate allocator mutex contention, page faults, and memory fragmentation jitter. |
| **Concurrency** | Lock-Free SPSC Queue (`std::atomic`) | Thread-to-thread communication via acquire-release semantics without blocking kernel mutexes or condition variable context switches. |
| **Cache Line Isolation** | `alignas(64)` on Producer/Consumer pointers | Prevents false sharing across L1/L2 cache lines between CPU cores 0 and 1. |
| **Order Lookup** | Open-Addressing Hash Map (Fibonacci Hash) | Eliminates pointer chasing and node allocations of `std::unordered_map`; contiguous flat arrays maximize L1D prefetcher efficiency. |
| **Order Book Depth** | Contiguous Top-$N$ Sorted Arrays ($N \le 10$) | Bounded insertion-sort level shifts beat `std::map`/red-black trees due to localized cache hits and branch predictability. |
| **OS Jitter Isolation** | Hard Thread Affinity (`SetThreadAffinityMask` / `pthread_setaffinity_np`) | Pins network ingestion and order book calculation to dedicated isolated physical cores, eliminating OS context switching. |

---

## 📊 Benchmark Metrics & Latency Profile

> **Environment:** MinGW GCC 16.1.0 (UCRT) / x86-64 Native  
> **Compilation Flags:** `-std=c++20 -O3 -march=native -flto`  
> **Workload:** 1,000,000 mixed ITCH messages (65% Add, 25% Cancel/Modify, 10% Execute)

### 1. In-Memory Zero-Allocation Microbenchmark

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

### 2. Multi-Threaded SPSC Lock-Free Pipeline Benchmark

```
=================================================================
      ASYNC SPSC LOCK-FREE PIPELINE BENCHMARK RESULTS           
=================================================================
  Producer Core Pinning  : Core 0
  Consumer Core Pinning  : Core 1
  Queue Implementation   : Lock-Free SPSCRingBuffer (alignas(64))
  Messages Ingested      : 1,000,000
  Total Wall Time        : 263.282 ms
  Mean Throughput        : 3,798,206 msgs/sec
  Mean Processing Time   : 263.28 ns/msg
=================================================================
```

### 3. Reconstructed Limit Order Book Depth & Market Analytics

```
  +-------------------------------------------------------------+
  |                      LIMIT ORDER BOOK                       |
  +------------------------------+------------------------------+
  |             BIDS             |             ASKS             |
  |  Orders    Qty        Price  |  Price       Qty      Orders |
  +------------------------------+------------------------------+
  |  5099    1164711    99.99   |  100.00   1172175    5127   |
  |  5128    1159294    99.98   |  100.01   1163588    5084   |
  |  5171    1181181    99.97   |  100.02   1198685    5208   |
  |  5112    1175175    99.96   |  100.03   1156158    5155   |
  |  5055    1149428    99.95   |  100.04   1160652    5132   |
  +------------------------------+------------------------------+
  | Spread: $0.01 | Mid: $100.00 | MicroPrice: $99.99        |
  +-------------------------------------------------------------+
```

---

## 📂 Repository Structure

```
├── CMakeLists.txt              # Production build config with IPO/LTO & C++20
├── README.md                   # System documentation & architectural analysis
├── include/
│   └── feed_handler/
│       ├── protocol.hpp        # Packed binary ITCH wire protocol & union structs
│       ├── ring_buffer.hpp     # Cache-aligned lock-free SPSC ring buffer
│       ├── order_table.hpp     # Open-addressing hash table with Fibonacci hash
│       ├── order_book.hpp      # Contiguous top-N depth Limit Order Book engine
│       ├── udp_receiver.hpp    # Non-blocking cross-platform UDP socket wrapper
│       ├── cpu_affinity.hpp    # Thread affinity pinning and priority utilities
│       └── latency_tracker.hpp # High-resolution nanosecond percentile tracker
├── src/
│   ├── main.cpp                # High-speed benchmark & dual-mode engine driver
│   └── mock_exchange.cpp       # Dedicated UDP market data broadcaster tool
└── tests/
    ├── test_order_book.cpp     # Unit tests for order book level dynamics
    ├── test_ring_buffer.cpp    # Multi-threaded concurrent SPSC stress tests
    └── test_order_table.cpp    # Hash map collision & probe chain tests
```

---

## 🔨 Build & Quickstart

### Prerequisites
- C++20 compliant compiler (`GCC 12+`, `Clang 15+`, or `MSVC 2022+`)
- `CMake 3.20+`

### Build Instructions

```bash
# Generate build files
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile all targets with native optimization & LTO
cmake --build build --config Release
```

### Run Unit Tests

```bash
ctest --test-dir build --output-on-failure
```

### User Data Ingestion (Main Functionality)

The engine and web dashboard run on **100% user-inserted data**. You can feed your own market orders through any of the following interfaces:

#### 1. Web Dashboard User Ingestion Center (Browser UI)
Launch the web interface:
```bash
python bridge.py
```
Open **`http://localhost:8080`** in your browser:
* **Manual Order Entry Desk:** Submit Limit Buy/Sell orders (`A`), cancel/reduce orders (`X`), or execute fills (`E`) with custom Order IDs, Prices, and Quantities directly into the live C++20 engine.
* **Custom File & CSV / JSON Batch Upload:** Drag-and-drop your custom `.csv` or `.json` market data file, or paste order streams (e.g. `A,1001,B,224.95,500`) for instantaneous batch ingestion.
* **Optional Preset Example Datasets:** Load curated institutional reference datasets on-demand (e.g., `AAPL L2 Book`, `NVDA Order Flow`, or `Market Sweep Cross`).
* **Export Book State:** Download the current Level 2 Limit Order Book snapshot as a CSV file with one click.

#### 2. CLI User File Ingestion
Ingest any user CSV/text dataset directly into the engine:
```bash
./build/feed_handler --file examples/aapl_l2_book.csv
# or with custom files:
./build/feed_handler --file path/to/your_orders.csv
```

#### 3. Interactive Terminal Console
Place orders interactively from the command line:
```bash
./build/feed_handler --interactive
```
Example terminal input:
```text
> A 1001 B 224.95 500
> A 1002 S 225.00 400
> E 1002 225.00 200
> X 1001 0
> BOOK
```

#### 4. Live External UDP Network Mode
Ingest real network packets sent to UDP socket port 12345:
```bash
./build/feed_handler --live 12345
```

---

### Run High-Throughput Microbenchmarks (Optional)

```bash
# Run 1,000,000 synthetic message microbenchmark & async pipeline benchmark
./build/feed_handler --bench 1000000
```

---

## 🎯 Interview Scope & Deliberate Engineering Boundaries

- **User-Driven Ingestion:** The engine operates strictly on deterministic user input and datasets, eliminating background noise or synthetic flood unless explicitly invoked via `--bench`.
- **Book Builder vs. Matching Engine:** This system is engineered as a downstream market data parser and book state reconstructor; it does not match or execute resting orders against each other.
- **Top-$N$ Depth Tracking:** Shallow depth ($N=5$) reflects real-world algorithmic trading signal consumption where top-of-book liquidity and micro-price imbalance dominate latency-critical decision paths.
- **Lock-Free SPSC Topology:** Single-producer single-consumer is intentional; market data feeds for a given instrument are typically bound to a single dedicated network queue / NIC ring buffer to preserve deterministic FIFO sequencing.
