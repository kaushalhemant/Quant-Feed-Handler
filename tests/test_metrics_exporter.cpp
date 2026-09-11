#include "feed_handler/metrics_exporter.hpp"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>

using namespace hft;

void testPrometheusExporterLifecycle() {
    PrometheusExporter exporter;
    bool started = exporter.start(19090);
    assert(started);
    (void)started;

    PrometheusExporter::MetricsData d{};
    d.totalIngested = 1000;
    d.totalProcessed = 999;
    d.sequenceGaps = 1;
    d.tradesExecuted = 50;
    d.p50Ns = 250.0;
    d.p99Ns = 750.0;
    d.throughput = 3500000.0;
    d.bucket_100ns = 100;
    d.bucket_250ns = 500;
    d.bucket_500ns = 800;
    d.bucket_inf = 999;
    d.latency_sum_ns = 250000.0;

    exporter.updateMetrics(d);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    exporter.stop();
    std::printf("[PASS] testPrometheusExporterLifecycle\n");
}

int main() {
    std::printf("--- Running Prometheus Exporter Unit Tests ---\n");
    testPrometheusExporterLifecycle();
    std::printf("All Prometheus Exporter tests passed successfully!\n\n");
    return 0;
}
