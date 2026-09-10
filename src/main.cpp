#include "feed_handler/protocol.hpp"
#include "feed_handler/ring_buffer.hpp"
#include "feed_handler/order_book.hpp"
#include "feed_handler/latency_tracker.hpp"
#include "feed_handler/cpu_affinity.hpp"
#include "feed_handler/udp_receiver.hpp"
#include "feed_handler/parser_validator.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace hft;

// Global pre-allocated benchmark buffer in static/BSS storage (zero runtime heap allocation)
inline constexpr size_t MAX_BENCH_MESSAGES = 2'000'000;
static std::array<WireMessage, MAX_BENCH_MESSAGES> g_benchFeed{};

/**
 * @brief Processes a user-supplied CSV/text market data file.
 */
static void runUserFile(const std::string& filePath) {
    std::printf("\n=================================================================\n");
    std::printf("       USER DATA INGESTION: LIMIT ORDER BOOK ENGINE              \n");
    std::printf("=================================================================\n");
    std::printf("  Reading User Data File: %s\n", filePath.c_str());
    std::printf("=================================================================\n\n");

    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::fprintf(stderr, "Error: Unable to open user data file '%s'\n", filePath.c_str());
        return;
    }

    static LimitOrderBook<5, (1u << 21)> book;
    book.reset();

    std::vector<WireMessage> messages;
    messages.reserve(100000);

    std::string line;
    uint64_t seqNo = 1;
    while (std::getline(file, line)) {
        WireMessage msg{};
        if (WireParserValidator::parseUserLine(line, seqNo++, msg)) {
            messages.push_back(msg);
        }
    }

    if (messages.empty()) {
        std::printf("No valid order messages parsed from file '%s'.\n", filePath.c_str());
        std::printf("Expected format: Type,OrderId,Side,Price,Quantity (e.g. A,1001,B,224.95,500)\n");
        return;
    }

    std::printf("[Ingestion] Parsed %zu user messages. Processing through engine...\n\n", messages.size());

    LatencyTracker tracker(messages.size());
    const auto startTotal = std::chrono::high_resolution_clock::now();

    for (const auto& msg : messages) {
        const auto t0 = std::chrono::high_resolution_clock::now();
        book.processMessage(msg);
        const auto t1 = std::chrono::high_resolution_clock::now();

        const int64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        tracker.recordNs(elapsedNs);
    }

    const auto endTotal = std::chrono::high_resolution_clock::now();
    const double totalWallMs = std::chrono::duration<double, std::milli>(endTotal - startTotal).count();

    tracker.printSummary(totalWallMs);
    book.printBook();
}

/**
 * @brief Interactive CLI Terminal Order Ingestion.
 */
static void runInteractiveCLI() {
    std::printf("\n=================================================================\n");
    std::printf("      INTERACTIVE ORDER INGESTION CONSOLE (C++20 ENGINE)         \n");
    std::printf("=================================================================\n");
    std::printf("  Enter order messages directly into the Limit Order Book:\n");
    std::printf("    Add Order    : A <orderId> <B/S> <price> <qty>  (e.g. A 101 B 125.50 500)\n");
    std::printf("    Cancel Order : X <orderId> [newQty/0]          (e.g. X 101 0)\n");
    std::printf("    Execute Fill : E <orderId> <price> <execQty>    (e.g. E 101 125.50 200)\n");
    std::printf("    Commands     : RESET, BOOK, QUIT\n");
    std::printf("=================================================================\n\n");

    static LimitOrderBook<5, (1u << 21)> book;
    book.reset();

    std::string line;
    uint64_t seqNo = 1;

    std::cout << "> ";
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            std::cout << "> ";
            continue;
        }

        if (line == "QUIT" || line == "quit" || line == "exit") {
            break;
        } else if (line == "RESET" || line == "reset") {
            book.reset();
            std::printf("[Engine] Limit Order Book reset.\n");
            book.printBook();
        } else if (line == "BOOK" || line == "book") {
            book.printBook();
        } else {
            WireMessage msg{};
            if (WireParserValidator::parseUserLine(line, seqNo++, msg)) {
                const auto t0 = std::chrono::high_resolution_clock::now();
                book.processMessage(msg);
                const auto t1 = std::chrono::high_resolution_clock::now();
                const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

                std::printf(">>> Ingested '%c' Order #%llu | Wire-to-Book Latency: %lld ns\n",
                            msg.msgType, static_cast<unsigned long long>(msg.add.orderId), static_cast<long long>(ns));
                book.printBook();
            } else {
                std::printf("Invalid format. Examples:\n  A 1001 B 100.50 500\n  X 1001 0\n  E 1001 100.50 200\n");
            }
        }
        std::cout << "> ";
    }
}

/**
 * @brief Benchmark helper for realistic mock generation.
 */
static void generateMockFeed(size_t messageCount) {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int32_t> bidPriceDist(9950, 9999);
    std::uniform_int_distribution<int32_t> askPriceDist(10000, 10049);
    std::uniform_int_distribution<int32_t> qtyDist(10, 500);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> typeDist(0, 99);

    std::vector<uint64_t> liveOrderIds;
    liveOrderIds.reserve(messageCount / 2);

    uint64_t nextOrderId = 1;
    uint64_t seqNo = 1;

    for (size_t i = 0; i < messageCount; ++i) {
        WireMessage msg{};
        const int typeRoll = typeDist(rng);

        if ((typeRoll >= 65 && typeRoll < 90) && !liveOrderIds.empty()) {
            size_t idx = std::uniform_int_distribution<size_t>(0, liveOrderIds.size() - 1)(rng);
            uint64_t targetId = liveOrderIds[idx];
            msg.msgType = 'X';
            msg.cancel.msgType = 'X';
            msg.cancel.seqNo = seqNo++;
            msg.cancel.timestampNs = i * 100;
            msg.cancel.orderId = targetId;
            msg.cancel.quantity = (typeRoll % 2 == 0) ? 0 : qtyDist(rng) / 2;
            if (msg.cancel.quantity == 0) {
                liveOrderIds[idx] = liveOrderIds.back();
                liveOrderIds.pop_back();
            }
        } else if (typeRoll >= 90 && !liveOrderIds.empty()) {
            size_t idx = std::uniform_int_distribution<size_t>(0, liveOrderIds.size() - 1)(rng);
            uint64_t targetId = liveOrderIds[idx];
            msg.msgType = 'E';
            msg.exec.msgType = 'E';
            msg.exec.seqNo = seqNo++;
            msg.exec.timestampNs = i * 100;
            msg.exec.orderId = targetId;
            msg.exec.execQuantity = qtyDist(rng) / 3;
            msg.exec.matchPrice = 10000;
        } else {
            msg.msgType = 'A';
            msg.add.msgType = 'A';
            msg.add.seqNo = seqNo++;
            msg.add.timestampNs = i * 100;
            msg.add.orderId = nextOrderId++;
            msg.add.side = (sideDist(rng) == 0) ? 'B' : 'S';
            msg.add.price = (msg.add.side == 'B') ? bidPriceDist(rng) : askPriceDist(rng);
            msg.add.quantity = qtyDist(rng);
            liveOrderIds.push_back(msg.add.orderId);
        }
        g_benchFeed[i] = msg;
    }
}

/**
 * @brief Benchmark 1: Direct In-Memory Microsecond Hot-Path Benchmark.
 */
static void runDirectBenchmark(size_t messageCount) {
    std::printf("\n>>> Running Direct Zero-Allocation Microbenchmark (%zu messages)...\n", messageCount);

    static LimitOrderBook<5, (1u << 21)> book;
    book.reset();
    LatencyTracker tracker(messageCount);

    const auto startTotal = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < messageCount; ++i) {
        const auto t0 = std::chrono::high_resolution_clock::now();
        book.processMessage(g_benchFeed[i]);
        const auto t1 = std::chrono::high_resolution_clock::now();

        const int64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        tracker.recordNs(elapsedNs);
    }

    const auto endTotal = std::chrono::high_resolution_clock::now();
    const double totalWallMs = std::chrono::duration<double, std::milli>(endTotal - startTotal).count();

    tracker.printSummary(totalWallMs);
    book.printBook();
}

/**
 * @brief Benchmark 2: Multi-Threaded Asynchronous Lock-Free SPSC Pipeline.
 */
static void runSPSCBenchmark(size_t messageCount) {
    std::printf("\n>>> Running Multi-Threaded SPSC Lock-Free Pipeline Benchmark (%zu messages)...\n", messageCount);

    static SPSCRingBuffer<WireMessage, 65536> ringBuffer;
    static LimitOrderBook<5, (1u << 21)> book;
    book.reset();

    std::atomic<bool> producerFinished{false};
    std::atomic<bool> startFlag{false};

    std::thread consumerThread([&]() {
        ThreadAffinity::pinCurrentThread(1);
        ThreadAffinity::setHighestPriority();

        while (!startFlag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        WireMessage msg{};
        while (!producerFinished.load(std::memory_order_relaxed) || !ringBuffer.empty()) {
            if (ringBuffer.tryPop(msg)) {
                book.processMessage(msg);
            }
        }
    });

    ThreadAffinity::pinCurrentThread(0);
    ThreadAffinity::setHighestPriority();

    const auto startTotal = std::chrono::high_resolution_clock::now();
    startFlag.store(true, std::memory_order_release);

    for (size_t i = 0; i < messageCount; ++i) {
        while (!ringBuffer.tryPush(g_benchFeed[i])) {
#if defined(__x86_64__) || defined(_M_X64)
            #if defined(_MSC_VER)
            _mm_pause();
            #else
            __builtin_ia32_pause();
            #endif
#endif
        }
    }

    producerFinished.store(true, std::memory_order_release);
    consumerThread.join();

    const auto endTotal = std::chrono::high_resolution_clock::now();
    const double totalWallMs = std::chrono::duration<double, std::milli>(endTotal - startTotal).count();
    const double throughput = static_cast<double>(messageCount) / (totalWallMs / 1000.0);

    std::printf("\n=================================================================\n");
    std::printf("      ASYNC SPSC LOCK-FREE PIPELINE BENCHMARK RESULTS           \n");
    std::printf("=================================================================\n");
    std::printf("  Producer Core Pinning  : Core 0\n");
    std::printf("  Consumer Core Pinning  : Core 1\n");
    std::printf("  Queue Implementation   : Lock-Free SPSCRingBuffer (alignas(64))\n");
    std::printf("  Messages Ingested      : %zu\n", messageCount);
    std::printf("  Total Wall Time        : %.3f ms\n", totalWallMs);
    std::printf("  Mean Throughput        : %.0f msgs/sec\n", throughput);
    std::printf("  Mean Processing Time   : %.2f ns/msg\n", (totalWallMs * 1e6) / static_cast<double>(messageCount));
    std::printf("=================================================================\n\n");

    book.printBook();
}

/**
 * @brief Live UDP Network Feed Handler Receiver Mode.
 */
static void runLiveUDP(uint16_t port) {
    std::printf("\n=================================================================\n");
    std::printf("        LIVE UDP FEED HANDLER & LIMIT ORDER BOOK ENGINE          \n");
    std::printf("=================================================================\n");
    std::printf("  Listening on UDP port: %u\n", port);
    std::printf("  Ingestion Architecture: UDP Socket -> SPSC Ring Buffer -> OrderBook\n");
    std::printf("  Press Ctrl+C to terminate.\n");
    std::printf("=================================================================\n\n");

    static UDPReceiver receiver;
    if (!receiver.bind(port)) {
        std::fprintf(stderr, "Failed to start UDP receiver\n");
        return;
    }

    static SPSCRingBuffer<WireMessage, 65536> ringBuffer;
    static LimitOrderBook<5, (1u << 21)> book;
    book.reset();

    std::atomic<bool> running{true};
    std::atomic<uint64_t> packetCount{0};

    std::thread worker([&]() {
        ThreadAffinity::pinCurrentThread(1);
        WireMessage msg{};
        auto lastPrint = std::chrono::steady_clock::now();

        while (running.load(std::memory_order_relaxed)) {
            if (ringBuffer.tryPop(msg)) {
                book.processMessage(msg);
                packetCount.fetch_add(1, std::memory_order_relaxed);
            }

            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPrint).count() >= 1000) {
                if (packetCount.load(std::memory_order_relaxed) > 0) {
                    book.printBook();
                    std::printf("  [Stats] Packets: %llu | Gaps: %llu | Trades: %llu\n\n",
                                static_cast<unsigned long long>(book.totalMessagesProcessed()),
                                static_cast<unsigned long long>(book.sequenceGapsDetected()),
                                static_cast<unsigned long long>(book.tradesExecuted()));
                }
                lastPrint = now;
            }
        }
    });

    ThreadAffinity::pinCurrentThread(0);
    WireMessage packet{};

    while (running.load(std::memory_order_relaxed)) {
        int bytes = receiver.receive(packet);
        if (bytes > 0) {
            while (!ringBuffer.tryPush(packet)) {
#if defined(__x86_64__) || defined(_M_X64)
                #if defined(_MSC_VER)
                _mm_pause();
                #else
                __builtin_ia32_pause();
                #endif
#endif
            }
        }
    }

    running.store(false);
    worker.join();
}

int main(int argc, char* argv[]) {
    std::string userFilePath = "";
    bool interactiveMode = false;
    bool liveMode = false;
    uint16_t livePort = 12345;
    bool benchMode = false;
    size_t benchCount = 1'000'000;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("Usage: %s [options]\n", argv[0]);
            std::printf("Options for User Ingestion (Main Functionality):\n");
            std::printf("  --file <path.csv>   Ingest user data CSV file into Limit Order Book\n");
            std::printf("  --interactive       Launch interactive terminal order placement console\n");
            std::printf("  --live [port]       Listen on live UDP network socket for user packets (default port: 12345)\n\n");
            std::printf("Optional Example & Benchmark Modes:\n");
            std::printf("  --example [name]    Load pre-built example dataset (aapl, nvda, sweep)\n");
            std::printf("  --bench [count]     Run synthetic microbenchmarks (default count: 1000000)\n");
            return 0;
        } else if (std::strcmp(argv[i], "--file") == 0 || std::strcmp(argv[i], "-f") == 0) {
            if (i + 1 < argc) userFilePath = argv[++i];
        } else if (std::strcmp(argv[i], "--interactive") == 0 || std::strcmp(argv[i], "-i") == 0) {
            interactiveMode = true;
        } else if (std::strcmp(argv[i], "--example") == 0) {
            std::string ex = "aapl";
            if (i + 1 < argc && argv[i + 1][0] != '-') ex = argv[++i];
            if (ex == "aapl") userFilePath = "examples/aapl_l2_book.csv";
            else if (ex == "nvda") userFilePath = "examples/nvda_order_flow.csv";
            else if (ex == "sweep") userFilePath = "examples/market_cross_sweep.csv";
            else userFilePath = ex;
        } else if (std::strcmp(argv[i], "--live") == 0) {
            liveMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                livePort = static_cast<uint16_t>(std::atoi(argv[++i]));
            }
        } else if (std::strcmp(argv[i], "--bench") == 0) {
            benchMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                benchCount = std::strtoull(argv[++i], nullptr, 10);
                if (benchCount > MAX_BENCH_MESSAGES) benchCount = MAX_BENCH_MESSAGES;
            }
        }
    }

    if (!userFilePath.empty()) {
        runUserFile(userFilePath);
        return 0;
    }

    if (interactiveMode) {
        runInteractiveCLI();
        return 0;
    }

    if (liveMode) {
        runLiveUDP(livePort);
        return 0;
    }

    if (benchMode) {
        std::printf("=================================================================\n");
        std::printf("  ULTRA-LOW LATENCY MARKET DATA FEED HANDLER & ORDER BOOK ENGINE \n");
        std::printf("  C++20 Institutional Benchmark Suite                            \n");
        std::printf("=================================================================\n");
        std::printf("[Setup] Generating %zu realistic ITCH benchmark messages...\n", benchCount);
        generateMockFeed(benchCount);
        runDirectBenchmark(benchCount);
        runSPSCBenchmark(benchCount);
        return 0;
    }

    // Default when no arguments passed: check if examples/aapl_l2_book.csv exists, otherwise interactive
    std::printf("=================================================================\n");
    std::printf("  ULTRA-LOW LATENCY MARKET DATA FEED HANDLER & ORDER BOOK ENGINE \n");
    std::printf("  100%% User Data Ingestion Engine (C++20 Zero-Allocation LOB)   \n");
    std::printf("=================================================================\n");
    std::printf("No file specified. Starting interactive console mode...\n");
    std::printf("Tip: Run with --file <path.csv> to ingest your custom dataset.\n");
    std::printf("     Run with --live <port> to ingest live UDP packets.\n");
    std::printf("     Run with --help for all available options.\n");
    
    runInteractiveCLI();
    return 0;
}
