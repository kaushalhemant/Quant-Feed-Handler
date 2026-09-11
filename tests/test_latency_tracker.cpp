#include "feed_handler/latency_tracker.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

using namespace hft;

void testLatencyCalculations() {
    LatencyTracker tracker(1000);
    assert(tracker.count() == 0);
    assert(tracker.minNs() == 0.0);
    assert(tracker.maxNs() == 0.0);
    assert(tracker.meanNs() == 0.0);

    // Record sample values: 100ns, 200ns, 300ns, 400ns, 500ns
    for (int64_t v = 100; v <= 500; v += 100) {
        tracker.recordNs(v);
    }

    assert(tracker.count() == 5);
    assert(tracker.minNs() == 100.0);
    assert(tracker.maxNs() == 500.0);
    assert(tracker.meanNs() == 300.0);
    assert(tracker.percentile(50.0) == 300.0);
    assert(tracker.percentile(0.0) == 100.0);
    assert(tracker.percentile(100.0) == 500.0);

    tracker.reset();
    assert(tracker.count() == 0);

    std::printf("[PASS] testLatencyCalculations\n");
}

int main() {
    std::printf("--- Running Latency Tracker Unit Tests ---\n");
    testLatencyCalculations();
    std::printf("All Latency Tracker tests passed successfully!\n\n");
    return 0;
}
