#pragma once

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace hft {

/**
 * @brief Lightweight, zero-dependency Prometheus HTTP Metrics Exporter.
 * Exposes /metrics endpoint on port 9090 for Grafana telemetry & SLA alert integration.
 */
class PrometheusExporter {
public:
    struct MetricsData {
        uint64_t totalIngested = 0;
        uint64_t totalProcessed = 0;
        uint64_t sequenceGaps = 0;
        uint64_t tradesExecuted = 0;
        double   p50Ns = 0.0;
        double   p90Ns = 0.0;
        double   p99Ns = 0.0;
        double   p999Ns = 0.0;
        double   throughput = 0.0;
        // Histogram bucket counts (100ns, 250ns, 500ns, 1us, 2.5us, 5us, 10us, 25us, 50us, 100us, +Inf)
        uint64_t bucket_100ns = 0;
        uint64_t bucket_250ns = 0;
        uint64_t bucket_500ns = 0;
        uint64_t bucket_1us = 0;
        uint64_t bucket_2_5us = 0;
        uint64_t bucket_5us = 0;
        uint64_t bucket_10us = 0;
        uint64_t bucket_25us = 0;
        uint64_t bucket_50us = 0;
        uint64_t bucket_100us = 0;
        uint64_t bucket_inf = 0;
        double   latency_sum_ns = 0.0;
    };

    PrometheusExporter() noexcept = default;

    ~PrometheusExporter() {
        stop();
    }

    bool start(uint16_t port = 9090) noexcept {
        port_ = port;
        running_.store(true);

        serverThread_ = std::thread([this]() {
#if defined(_WIN32) || defined(_WIN64)
            SOCKET listenSock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (listenSock == INVALID_SOCKET) return;
            int opt = 1;
            ::setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port_);
            addr.sin_addr.s_addr = htonl(INADDR_ANY);

            if (::bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listenSock, 5) != 0) {
                ::closesocket(listenSock);
                return;
            }
            std::printf("[PrometheusExporter] Telemetry & Health server listening on http://0.0.0.0:%u (/metrics, /healthz, /readyz)\n", port_);

            while (running_.load(std::memory_order_relaxed)) {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(listenSock, &readfds);

                timeval tv{1, 0}; // 1s timeout
                int sel = ::select(0, &readfds, nullptr, nullptr, &tv);
                if (sel > 0 && FD_ISSET(listenSock, &readfds)) {
                    SOCKET clientSock = ::accept(listenSock, nullptr, nullptr);
                    if (clientSock != INVALID_SOCKET) {
                        char buf[1024] = {0};
                        ::recv(clientSock, buf, sizeof(buf) - 1, 0);

                        std::string req(buf);
                        std::string response;

                        if (req.find("GET /healthz") != std::string::npos) {
                            std::string body = "{\"status\":\"healthy\",\"engine\":\"QuantDesk C++20 LOB\"}\n";
                            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                        } else if (req.find("GET /readyz") != std::string::npos) {
                            std::string body = "{\"status\":\"ready\",\"ready\":true}\n";
                            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                        } else {
                            std::string metricsBody = buildMetricsPayload();
                            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; version=0.0.4\r\nContent-Length: " +
                                       std::to_string(metricsBody.size()) + "\r\nConnection: close\r\n\r\n" + metricsBody;
                        }

                        ::send(clientSock, response.c_str(), static_cast<int>(response.size()), 0);
                        ::closesocket(clientSock);
                    }
                }
            }
            ::closesocket(listenSock);
#else
            int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (listenFd < 0) return;
            int opt = 1;
            ::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port_);
            addr.sin_addr.s_addr = htonl(INADDR_ANY);

            if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(listenFd, 5) != 0) {
                ::close(listenFd);
                return;
            }
            std::printf("[PrometheusExporter] Telemetry & Health server listening on http://0.0.0.0:%u (/metrics, /healthz, /readyz)\n", port_);

            while (running_.load(std::memory_order_relaxed)) {
                fd_set readfds;
                FD_ZERO(&readfds);
                FD_SET(listenFd, &readfds);

                timeval tv{1, 0};
                int sel = ::select(listenFd + 1, &readfds, nullptr, nullptr, &tv);
                if (sel > 0 && FD_ISSET(listenFd, &readfds)) {
                    int clientFd = ::accept(listenFd, nullptr, nullptr);
                    if (clientFd >= 0) {
                        char buf[1024] = {0};
                        ::recv(clientFd, buf, sizeof(buf) - 1, 0);

                        std::string req(buf);
                        std::string response;

                        if (req.find("GET /healthz") != std::string::npos) {
                            std::string body = "{\"status\":\"healthy\",\"engine\":\"QuantDesk C++20 LOB\"}\n";
                            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                        } else if (req.find("GET /readyz") != std::string::npos) {
                            std::string body = "{\"status\":\"ready\",\"ready\":true}\n";
                            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                        } else {
                            std::string metricsBody = buildMetricsPayload();
                            response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; version=0.0.4\r\nContent-Length: " +
                                       std::to_string(metricsBody.size()) + "\r\nConnection: close\r\n\r\n" + metricsBody;
                        }

                        ::send(clientFd, response.c_str(), response.size(), 0);
                        ::close(clientFd);
                    }
                }
            }
            ::close(listenFd);
#endif
        });

        return true;
    }

    void updateMetrics(const MetricsData& data) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        data_ = data;
    }

    void stop() noexcept {
        if (running_.load()) {
            running_.store(false);
            if (serverThread_.joinable()) {
                serverThread_.join();
            }
        }
    }

private:
    uint16_t port_ = 9090;
    std::atomic<bool> running_{false};
    std::thread serverThread_;
    mutable std::mutex mutex_;
    MetricsData data_{};

    std::string buildMetricsPayload() const {
        MetricsData d{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            d = data_;
        }

        std::string body;
        body += "# HELP feed_handler_messages_total Total packets ingested by feed handler\n";
        body += "# TYPE feed_handler_messages_total counter\n";
        body += "feed_handler_messages_total " + std::to_string(d.totalIngested) + "\n\n";

        body += "# HELP feed_handler_messages_processed_total Total messages processed into order book\n";
        body += "# TYPE feed_handler_messages_processed_total counter\n";
        body += "feed_handler_messages_processed_total " + std::to_string(d.totalProcessed) + "\n\n";

        body += "# HELP feed_handler_sequence_gaps_total Total sequence gaps detected\n";
        body += "# TYPE feed_handler_sequence_gaps_total counter\n";
        body += "feed_handler_sequence_gaps_total " + std::to_string(d.sequenceGaps) + "\n\n";

        body += "# HELP feed_handler_trades_executed_total Total fill trades executed\n";
        body += "# TYPE feed_handler_trades_executed_total counter\n";
        body += "feed_handler_trades_executed_total " + std::to_string(d.tradesExecuted) + "\n\n";

        body += "# HELP feed_handler_throughput_msgs_per_sec Current processing throughput\n";
        body += "# TYPE feed_handler_throughput_msgs_per_sec gauge\n";
        body += "feed_handler_throughput_msgs_per_sec " + std::to_string(d.throughput) + "\n\n";

        body += "# HELP feed_handler_latency_nanoseconds_p50 Median processing latency\n";
        body += "# TYPE feed_handler_latency_nanoseconds_p50 gauge\n";
        body += "feed_handler_latency_nanoseconds_p50 " + std::to_string(d.p50Ns) + "\n\n";

        body += "# HELP feed_handler_latency_nanoseconds_p99 99th percentile processing tail latency\n";
        body += "# TYPE feed_handler_latency_nanoseconds_p99 gauge\n";
        body += "feed_handler_latency_nanoseconds_p99 " + std::to_string(d.p99Ns) + "\n\n";

        body += "# HELP feed_handler_latency_nanoseconds_p999 99.9th percentile processing tail latency\n";
        body += "# TYPE feed_handler_latency_nanoseconds_p999 gauge\n";
        body += "feed_handler_latency_nanoseconds_p999 " + std::to_string(d.p999Ns) + "\n\n";

        // Latency Histogram
        body += "# HELP feed_handler_latency_nanoseconds Wire-to-book latency in nanoseconds\n";
        body += "# TYPE feed_handler_latency_nanoseconds histogram\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"100\"} " + std::to_string(d.bucket_100ns) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"250\"} " + std::to_string(d.bucket_250ns) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"500\"} " + std::to_string(d.bucket_500ns) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"1000\"} " + std::to_string(d.bucket_1us) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"2500\"} " + std::to_string(d.bucket_2_5us) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"5000\"} " + std::to_string(d.bucket_5us) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"10000\"} " + std::to_string(d.bucket_10us) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"25000\"} " + std::to_string(d.bucket_25us) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"50000\"} " + std::to_string(d.bucket_50us) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"100000\"} " + std::to_string(d.bucket_100us) + "\n";
        body += "feed_handler_latency_nanoseconds_bucket{le=\"+Inf\"} " + std::to_string(d.bucket_inf) + "\n";
        body += "feed_handler_latency_nanoseconds_sum " + std::to_string(d.latency_sum_ns) + "\n";
        body += "feed_handler_latency_nanoseconds_count " + std::to_string(d.totalProcessed) + "\n";

        return body;
    }
};

} // namespace hft
