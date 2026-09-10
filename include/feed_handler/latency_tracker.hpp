#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace hft {

/**
 * @brief High-precision nanosecond latency metrics and percentile tracker.
 */
class LatencyTracker {
public:
    explicit LatencyTracker(size_t reserveCount = 1'000'000) {
        samplesNs_.reserve(reserveCount);
    }

    void reset() noexcept {
        samplesNs_.clear();
    }

    inline void recordNs(int64_t nanoseconds) noexcept {
        samplesNs_.push_back(nanoseconds);
    }

    [[nodiscard]] size_t count() const noexcept {
        return samplesNs_.size();
    }

    [[nodiscard]] double minNs() const noexcept {
        if (samplesNs_.empty()) return 0.0;
        return static_cast<double>(*std::min_element(samplesNs_.begin(), samplesNs_.end()));
    }

    [[nodiscard]] double maxNs() const noexcept {
        if (samplesNs_.empty()) return 0.0;
        return static_cast<double>(*std::max_element(samplesNs_.begin(), samplesNs_.end()));
    }

    [[nodiscard]] double meanNs() const noexcept {
        if (samplesNs_.empty()) return 0.0;
        double sum = 0.0;
        for (const auto v : samplesNs_) sum += v;
        return sum / static_cast<double>(samplesNs_.size());
    }

    [[nodiscard]] double stddevNs() const noexcept {
        if (samplesNs_.size() < 2) return 0.0;
        const double avg = meanNs();
        double varianceSum = 0.0;
        for (const auto v : samplesNs_) {
            const double diff = static_cast<double>(v) - avg;
            varianceSum += diff * diff;
        }
        return std::sqrt(varianceSum / static_cast<double>(samplesNs_.size() - 1));
    }

    /**
     * @brief Computes percentile (0.0 to 100.0) after sorting.
     */
    [[nodiscard]] double percentile(double p) {
        if (samplesNs_.empty()) return 0.0;
        if (!sorted_) {
            std::sort(samplesNs_.begin(), samplesNs_.end());
            sorted_ = true;
        }
        if (p <= 0.0) return static_cast<double>(samplesNs_.front());
        if (p >= 100.0) return static_cast<double>(samplesNs_.back());

        const double rank = (p / 100.0) * (static_cast<double>(samplesNs_.size()) - 1);
        const size_t lower = static_cast<size_t>(std::floor(rank));
        const size_t upper = static_cast<size_t>(std::ceil(rank));
        const double weight = rank - static_cast<double>(lower);

        return static_cast<double>(samplesNs_[lower]) * (1.0 - weight) +
               static_cast<double>(samplesNs_[upper]) * weight;
    }

    void printSummary(double totalWallTimeMs, FILE* out = stdout) {
        if (samplesNs_.empty()) {
            std::fprintf(out, "No latency samples recorded.\n");
            return;
        }

        const double p50 = percentile(50.0);
        const double p90 = percentile(90.0);
        const double p95 = percentile(95.0);
        const double p99 = percentile(99.0);
        const double p999 = percentile(99.9);
        const double p9999 = percentile(99.99);
        const double throughput = (static_cast<double>(samplesNs_.size()) / (totalWallTimeMs / 1000.0));

        std::fprintf(out, "\n=================================================================\n");
        std::fprintf(out, "               HFT BENCHMARK & LATENCY PROFILE                   \n");
        std::fprintf(out, "=================================================================\n");
        std::fprintf(out, "  Total Packets Ingested : %'zu\n", samplesNs_.size());
        std::fprintf(out, "  Total Wall Time        : %.3f ms\n", totalWallTimeMs);
        std::fprintf(out, "  Effective Throughput   : %'.0f msgs/sec\n", throughput);
        std::fprintf(out, "-----------------------------------------------------------------\n");
        std::fprintf(out, "  Latency Distribution   : (Nanoseconds per message)\n");
        std::fprintf(out, "    Min Latency          : %8.2f ns\n", minNs());
        std::fprintf(out, "    Mean Latency         : %8.2f ns (StdDev: %.2f ns)\n", meanNs(), stddevNs());
        std::fprintf(out, "    50.00th (Median)     : %8.2f ns\n", p50);
        std::fprintf(out, "    90.00th Percentile   : %8.2f ns\n", p90);
        std::fprintf(out, "    95.00th Percentile   : %8.2f ns\n", p95);
        std::fprintf(out, "    99.00th Percentile   : %8.2f ns  [Tail Latency]\n", p99);
        std::fprintf(out, "    99.90th Percentile   : %8.2f ns\n", p999);
        std::fprintf(out, "    99.99th Percentile   : %8.2f ns\n", p9999);
        std::fprintf(out, "    Max Latency          : %8.2f ns\n", maxNs());
        std::fprintf(out, "=================================================================\n\n");
    }

private:
    std::vector<int64_t> samplesNs_;
    bool sorted_ = false;
};

} // namespace hft
