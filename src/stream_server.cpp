#include "feed_handler/protocol.hpp"
#include "feed_handler/ring_buffer.hpp"
#include "feed_handler/order_book.hpp"
#include "feed_handler/latency_tracker.hpp"
#include "feed_handler/cpu_affinity.hpp"
#include "feed_handler/parser_validator.hpp"
#include "feed_handler/feed_arbitrator.hpp"
#include "feed_handler/wal_journal.hpp"
#include "feed_handler/metrics_exporter.hpp"
#include "feed_handler/udp_receiver.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <array>

using namespace hft;

// Global SPSC Ring Buffer, Order Book and Prometheus Exporter
static SPSCRingBuffer<WireMessage, 65536> g_ringBuffer;
static LimitOrderBook<5, (1u << 21)> g_orderBook;
static PrometheusExporter g_prometheusExporter;

// Control state
static std::atomic<bool> g_running{true};
static std::atomic<uint64_t> g_burstPending{0};
static std::atomic<uint64_t> g_userSeqNo{1};

// Performance metrics
static std::atomic<uint64_t> g_totalIngested{0};
static std::atomic<uint64_t> g_totalProcessed{0};

// Circular buffer of recent packets for live tape visualization
struct PacketRecord {
    char     type = 0;
    uint64_t seqNo = 0;
    uint64_t timestampNs = 0;
    uint64_t orderId = 0;
    char     side = 0;
    int32_t  price = 0;
    int32_t  quantity = 0;
};

static constexpr size_t RECENT_TAPE_SIZE = 30;
static std::array<PacketRecord, RECENT_TAPE_SIZE> g_recentTape{};
static std::atomic<size_t> g_tapeHead{0};
static std::mutex g_tapeMutex;

// Rolling latency samples (last 200,000 samples for live percentiles)
static constexpr size_t LATENCY_WINDOW = 200000;
static std::vector<int64_t> g_latencySamples;
static std::mutex g_latencyMutex;

/**
 * @brief Helper: Injects an explicit WireMessage into the SPSC ring buffer.
 */
static void pushWireMessage(const WireMessage& msg) {
    while (!g_ringBuffer.tryPush(msg)) {
#if defined(__x86_64__) || defined(_M_X64)
        #if defined(_MSC_VER)
        _mm_pause();
        #else
        __builtin_ia32_pause();
        #endif
#endif
    }
    g_totalIngested.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief Helper: Loads a user CSV file or example dataset and pushes orders into the engine.
 */
static void loadFileIntoEngine(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "[stream_server] Could not open dataset file: " << filepath << "\n";
        return;
    }

    std::string line;
    uint64_t loaded = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) {
            continue;
        }
        WireMessage msg{};
        if (WireParserValidator::parseUserLine(line, g_userSeqNo.fetch_add(1, std::memory_order_relaxed), msg)) {
            pushWireMessage(msg);
            ++loaded;
        }
    }
    std::cerr << "[stream_server] Loaded " << loaded << " messages from " << filepath << "\n";
}

/**
 * @brief Thread 1: Ingestion & On-Demand Generator / UDP Ingest (Producer)
 */
void producerThreadFunc() {
    ThreadAffinity::pinCurrentThread(0);
    ThreadAffinity::setHighestPriority();

    while (g_running.load(std::memory_order_relaxed)) {
        const uint64_t burst = g_burstPending.exchange(0, std::memory_order_relaxed);

        if (burst > 0) {
            std::mt19937_64 rng(42);
            std::uniform_int_distribution<int32_t> qtyDist(10, 500);
            std::uniform_int_distribution<int> sideDist(0, 1);
            std::uniform_int_distribution<int> actionDist(0, 99);

            std::vector<uint64_t> activeOrders;
            activeOrders.reserve(50000);
            int32_t basePrice = 10000;

            for (uint64_t k = 0; k < burst; ++k) {
                WireMessage msg{};
                const uint64_t nowNs = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::high_resolution_clock::now().time_since_epoch()).count());

                const int action = actionDist(rng);
                std::uniform_int_distribution<int32_t> bidSpreadDist(basePrice - 5, basePrice - 1);
                std::uniform_int_distribution<int32_t> askSpreadDist(basePrice, basePrice + 5);

                if (action < 65 || activeOrders.empty()) {
                    // Add Order
                    uint64_t orderId = g_userSeqNo.load(std::memory_order_relaxed);
                    msg.msgType = 'A';
                    msg.add.msgType = 'A';
                    msg.add.seqNo = g_userSeqNo.fetch_add(1, std::memory_order_relaxed);
                    msg.add.timestampNs = nowNs;
                    msg.add.orderId = orderId;
                    msg.add.side = (sideDist(rng) == 0) ? 'B' : 'S';
                    msg.add.price = (msg.add.side == 'B') ? bidSpreadDist(rng) : askSpreadDist(rng);
                    msg.add.quantity = qtyDist(rng);
                    activeOrders.push_back(orderId);
                } else if (action < 90) {
                    // Cancel / Modify
                    size_t idx = std::uniform_int_distribution<size_t>(0, activeOrders.size() - 1)(rng);
                    uint64_t targetId = activeOrders[idx];
                    msg.msgType = 'X';
                    msg.cancel.msgType = 'X';
                    msg.cancel.seqNo = g_userSeqNo.fetch_add(1, std::memory_order_relaxed);
                    msg.cancel.timestampNs = nowNs;
                    msg.cancel.orderId = targetId;
                    msg.cancel.quantity = (action % 2 == 0) ? 0 : qtyDist(rng) / 2;
                    if (msg.cancel.quantity == 0) {
                        activeOrders[idx] = activeOrders.back();
                        activeOrders.pop_back();
                    }
                } else {
                    // Execute Fill
                    size_t idx = std::uniform_int_distribution<size_t>(0, activeOrders.size() - 1)(rng);
                    uint64_t targetId = activeOrders[idx];
                    msg.msgType = 'E';
                    msg.exec.msgType = 'E';
                    msg.exec.seqNo = g_userSeqNo.fetch_add(1, std::memory_order_relaxed);
                    msg.exec.timestampNs = nowNs;
                    msg.exec.orderId = targetId;
                    msg.exec.execQuantity = qtyDist(rng) / 3;
                    msg.exec.matchPrice = basePrice;
                }

                pushWireMessage(msg);
            }
        }

        // Idle wait for user events
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

/**
 * @brief Thread 2: Order Book Engine (Consumer)
 */
void consumerThreadFunc() {
    ThreadAffinity::pinCurrentThread(1);
    ThreadAffinity::setHighestPriority();

    std::vector<int64_t> localLatencyBuffer;
    localLatencyBuffer.reserve(10000);

    WireMessage msg{};

    while (g_running.load(std::memory_order_relaxed)) {
        if (g_ringBuffer.tryPop(msg)) {
            const auto t0 = std::chrono::high_resolution_clock::now();
            g_orderBook.processMessage(msg);
            const auto t1 = std::chrono::high_resolution_clock::now();

            const int64_t elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            localLatencyBuffer.push_back(elapsedNs);

            // Record to recent tape
            PacketRecord pkt{};
            pkt.type = msg.msgType;
            pkt.seqNo = msg.seqNo();
            pkt.timestampNs = msg.timestampNs();

            if (msg.msgType == 'A') {
                pkt.orderId = msg.add.orderId;
                pkt.side = msg.add.side;
                pkt.price = msg.add.price;
                pkt.quantity = msg.add.quantity;
            } else if (msg.msgType == 'X') {
                pkt.orderId = msg.cancel.orderId;
                pkt.side = 'X';
                pkt.price = 0;
                pkt.quantity = msg.cancel.quantity;
            } else if (msg.msgType == 'E') {
                pkt.orderId = msg.exec.orderId;
                pkt.side = 'E';
                pkt.price = msg.exec.matchPrice;
                pkt.quantity = msg.exec.execQuantity;
            }

            {
                std::lock_guard<std::mutex> lock(g_tapeMutex);
                size_t head = g_tapeHead.load(std::memory_order_relaxed);
                g_recentTape[head % RECENT_TAPE_SIZE] = pkt;
                g_tapeHead.store(head + 1, std::memory_order_relaxed);
            }

            g_totalProcessed.fetch_add(1, std::memory_order_relaxed);

            if (localLatencyBuffer.size() >= 1000) {
                std::lock_guard<std::mutex> lock(g_latencyMutex);
                if (g_latencySamples.size() > LATENCY_WINDOW) {
                    g_latencySamples.erase(g_latencySamples.begin(), g_latencySamples.begin() + localLatencyBuffer.size());
                }
                g_latencySamples.insert(g_latencySamples.end(), localLatencyBuffer.begin(), localLatencyBuffer.end());
                localLatencyBuffer.clear();
            }
        } else {
#if defined(__x86_64__) || defined(_M_X64)
            #if defined(_MSC_VER)
            _mm_pause();
            #else
            __builtin_ia32_pause();
            #endif
#endif
            std::this_thread::yield();
        }
    }
}

/**
 * @brief Thread 3: Periodic Telemetry Snapshot JSON Emitter (Output to stdout for Python Bridge)
 */
void snapshotThreadFunc() {
    auto lastSnapshotTime = std::chrono::steady_clock::now();
    uint64_t lastProcessedCount = 0;

    g_latencySamples.reserve(LATENCY_WINDOW + 10000);

    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // ~20 FPS updates

        const auto now = std::chrono::steady_clock::now();
        const double dtSec = std::chrono::duration<double>(now - lastSnapshotTime).count();
        lastSnapshotTime = now;

        const uint64_t currentProcessed = g_totalProcessed.load(std::memory_order_relaxed);
        const uint64_t deltaProcessed = (currentProcessed >= lastProcessedCount) ? (currentProcessed - lastProcessedCount) : 0;
        lastProcessedCount = currentProcessed;

        const double currentThroughput = (dtSec > 0.0) ? (static_cast<double>(deltaProcessed) / dtSec) : 0.0;

        // Calculate latency percentiles
        double minNs = 0.0, meanNs = 0.0, p50 = 0.0, p90 = 0.0, p95 = 0.0, p99 = 0.0, p999 = 0.0, maxNs = 0.0;
        std::vector<int64_t> latCopy;
        PrometheusExporter::MetricsData metrics{};

        {
            std::lock_guard<std::mutex> lock(g_latencyMutex);
            if (!g_latencySamples.empty()) {
                latCopy = g_latencySamples;
            }
        }

        if (!latCopy.empty()) {
            std::sort(latCopy.begin(), latCopy.end());
            minNs = static_cast<double>(latCopy.front());
            maxNs = static_cast<double>(latCopy.back());

            double sum = 0;
            for (auto v : latCopy) sum += v;
            meanNs = sum / latCopy.size();

            auto getPct = [&](double p) -> double {
                size_t idx = static_cast<size_t>((p / 100.0) * (latCopy.size() - 1));
                return static_cast<double>(latCopy[idx]);
            };

            p50 = getPct(50.0);
            p90 = getPct(90.0);
            p95 = getPct(95.0);
            p99 = getPct(99.0);
            p999 = getPct(99.9);
            // Compute histogram buckets
            for (auto ns : latCopy) {
                if (ns <= 100) metrics.bucket_100ns++;
                if (ns <= 250) metrics.bucket_250ns++;
                if (ns <= 500) metrics.bucket_500ns++;
                if (ns <= 1000) metrics.bucket_1us++;
                if (ns <= 2500) metrics.bucket_2_5us++;
                if (ns <= 5000) metrics.bucket_5us++;
                if (ns <= 10000) metrics.bucket_10us++;
                if (ns <= 25000) metrics.bucket_25us++;
                if (ns <= 50000) metrics.bucket_50us++;
                if (ns <= 100000) metrics.bucket_100us++;
                metrics.bucket_inf++;
                metrics.latency_sum_ns += static_cast<double>(ns);
            }
        }

        metrics.totalIngested = g_totalIngested.load(std::memory_order_relaxed);
        metrics.totalProcessed = currentProcessed;
        metrics.sequenceGaps = g_orderBook.sequenceGapsDetected();
        metrics.tradesExecuted = g_orderBook.tradesExecuted();
        metrics.p50Ns = p50;
        metrics.p90Ns = p90;
        metrics.p99Ns = p99;
        metrics.p999Ns = p999;
        metrics.throughput = currentThroughput;
        g_prometheusExporter.updateMetrics(metrics);

        // Fetch Order Book Depth
        const auto& bids = g_orderBook.bids();
        const auto& asks = g_orderBook.asks();

        // Build JSON output line
        std::string json = "{\"type\":\"snapshot\",";
        json += "\"totalIngested\":" + std::to_string(g_totalIngested.load(std::memory_order_relaxed)) + ",";
        json += "\"totalProcessed\":" + std::to_string(currentProcessed) + ",";
        json += "\"throughput\":" + std::to_string(currentThroughput) + ",";
        json += "\"isStreaming\":true,";
        json += "\"targetRate\":0,";

        // Latency section
        json += "\"latency\":{";
        json += "\"min\":" + std::to_string(minNs) + ",";
        json += "\"mean\":" + std::to_string(meanNs) + ",";
        json += "\"p50\":" + std::to_string(p50) + ",";
        json += "\"p90\":" + std::to_string(p90) + ",";
        json += "\"p95\":" + std::to_string(p95) + ",";
        json += "\"p99\":" + std::to_string(p99) + ",";
        json += "\"p999\":" + std::to_string(p999) + ",";
        json += "\"max\":" + std::to_string(maxNs);
        json += "},";

        // Order Book Bids
        json += "\"bids\":[";
        for (size_t i = 0; i < bids.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"price\":" + std::to_string(bids[i].price) + ",";
            json += "\"qty\":" + std::to_string(bids[i].quantity) + ",";
            json += "\"orders\":" + std::to_string(bids[i].orderCount) + ",";
            json += "\"active\":" + std::string(bids[i].active ? "true" : "false") + "}";
        }
        json += "],";

        // Order Book Asks
        json += "\"asks\":[";
        for (size_t i = 0; i < asks.size(); ++i) {
            if (i > 0) json += ",";
            json += "{\"price\":" + std::to_string(asks[i].price) + ",";
            json += "\"qty\":" + std::to_string(asks[i].quantity) + ",";
            json += "\"orders\":" + std::to_string(asks[i].orderCount) + ",";
            json += "\"active\":" + std::string(asks[i].active ? "true" : "false") + "}";
        }
        json += "],";

        // Analytics
        json += "\"analytics\":{";
        json += "\"spread\":" + std::to_string(g_orderBook.spread()) + ",";
        json += "\"mid\":" + std::to_string(g_orderBook.midPrice()) + ",";
        json += "\"micro\":" + std::to_string(g_orderBook.microPrice()) + ",";
        json += "\"trades\":" + std::to_string(g_orderBook.tradesExecuted()) + ",";
        json += "\"gaps\":" + std::to_string(g_orderBook.sequenceGapsDetected()) + ",";
        json += "\"hasBids\":" + std::string(g_orderBook.hasBids() ? "true" : "false") + ",";
        json += "\"hasAsks\":" + std::string(g_orderBook.hasAsks() ? "true" : "false");
        json += "},";

        // Recent Tape Packets
        json += "\"tape\":[";
        {
            std::lock_guard<std::mutex> lock(g_tapeMutex);
            size_t head = g_tapeHead.load(std::memory_order_relaxed);
            size_t count = (head < RECENT_TAPE_SIZE) ? head : RECENT_TAPE_SIZE;
            for (size_t i = 0; i < count; ++i) {
                if (i > 0) json += ",";
                size_t idx = (head - 1 - i) % RECENT_TAPE_SIZE;
                const auto& p = g_recentTape[idx];
                json += "{\"type\":\"" + std::string(1, p.type) + "\",";
                json += "\"seqNo\":" + std::to_string(p.seqNo) + ",";
                json += "\"ts\":" + std::to_string(p.timestampNs) + ",";
                json += "\"orderId\":" + std::to_string(p.orderId) + ",";
                json += "\"side\":\"" + std::string(1, p.side) + "\",";
                json += "\"price\":" + std::to_string(p.price) + ",";
                json += "\"qty\":" + std::to_string(p.quantity) + "}";
            }
        }
        json += "]}";

        // Output JSON line to stdout with explicit flush
        std::cout << json << "\n";
        std::cout.flush();
    }
}

/**
 * @brief Thread 4: Command Dispatcher from Stdin (Control Loop from Python Bridge / User CLI)
 */
void commandThreadFunc() {
    std::string line;
    while (g_running.load(std::memory_order_relaxed) && std::getline(std::cin, line)) {
        if (line.empty()) continue;

        if (line.rfind("ORDER ", 0) == 0 || line.rfind("order ", 0) == 0 ||
            line.rfind("A,", 0) == 0 || line.rfind("X,", 0) == 0 || line.rfind("E,", 0) == 0 ||
            line.rfind("A ", 0) == 0 || line.rfind("X ", 0) == 0 || line.rfind("E ", 0) == 0) {
            
            WireMessage msg{};
            if (WireParserValidator::parseUserLine(line, g_userSeqNo.fetch_add(1, std::memory_order_relaxed), msg)) {
                pushWireMessage(msg);
            }
        } else if (line.rfind("FILE ", 0) == 0) {
            std::string path = line.substr(5);
            // Trim whitespace
            while (!path.empty() && (path.front() == ' ' || path.front() == '\t')) path.erase(0, 1);
            while (!path.empty() && (path.back() == ' ' || path.back() == '\r' || path.back() == '\n')) path.pop_back();
            loadFileIntoEngine(path);
        } else if (line.rfind("LOAD_EXAMPLE ", 0) == 0) {
            std::string name = line.substr(13);
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(0, 1);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\r' || name.back() == '\n')) name.pop_back();

            if (name == "aapl" || name == "aapl_l2_book") {
                loadFileIntoEngine("examples/aapl_l2_book.csv");
            } else if (name == "nvda" || name == "nvda_order_flow") {
                loadFileIntoEngine("examples/nvda_order_flow.csv");
            } else if (name == "sweep" || name == "market_cross_sweep") {
                loadFileIntoEngine("examples/market_cross_sweep.csv");
            } else {
                loadFileIntoEngine(name);
            }
        } else if (line.rfind("BURST", 0) == 0) {
            uint64_t burstAmount = 100000;
            if (line.size() > 6) {
                burstAmount = std::strtoull(line.c_str() + 6, nullptr, 10);
            }
            g_burstPending.store(burstAmount, std::memory_order_relaxed);
        } else if (line == "RESET" || line == "CLEAR") {
            g_orderBook.reset();
            {
                std::lock_guard<std::mutex> lock(g_tapeMutex);
                g_tapeHead.store(0, std::memory_order_relaxed);
                g_recentTape.fill(PacketRecord{});
            }
            {
                std::lock_guard<std::mutex> lock(g_latencyMutex);
                g_latencySamples.clear();
            }
            g_totalIngested.store(0, std::memory_order_relaxed);
            g_totalProcessed.store(0, std::memory_order_relaxed);
            g_userSeqNo.store(1, std::memory_order_relaxed);
        } else if (line == "CROSS") {
            // Inject a crossed user order that sweeps top of book
            WireMessage msg{};
            msg.msgType = 'A';
            msg.add.msgType = 'A';
            msg.add.seqNo = g_userSeqNo.fetch_add(1, std::memory_order_relaxed);
            msg.add.timestampNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now().time_since_epoch()).count());
            msg.add.orderId = 9999999;
            msg.add.side = 'B';
            msg.add.price = g_orderBook.hasAsks() ? g_orderBook.bestAskPrice() : 10000;
            msg.add.quantity = 500;
            pushWireMessage(msg);
        } else if (line == "QUIT" || line == "EXIT") {
            g_running.store(false, std::memory_order_relaxed);
            break;
        }
    }
}

int main() {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    g_orderBook.reset();
    g_prometheusExporter.start(9090);

    std::thread producer(producerThreadFunc);
    std::thread consumer(consumerThreadFunc);
    std::thread snapshotter(snapshotThreadFunc);
    std::thread commander(commandThreadFunc);

    commander.join();
    g_running.store(false, std::memory_order_relaxed);

    if (producer.joinable()) producer.join();
    if (consumer.joinable()) consumer.join();
    if (snapshotter.joinable()) snapshotter.join();

    g_prometheusExporter.stop();

    return 0;
}
