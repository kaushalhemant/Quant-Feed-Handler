#pragma once

#include "protocol.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hft {

/**
 * @brief Dual A/B Feed Arbitrator & Sequence Recovery Engine.
 * Provides zero-allocation lock-free primary/secondary feed deduplication,
 * out-of-order packet buffering, and sequence gap retransmission tracking.
 * 
 * @tparam MaxBufferedGaps Maximum number of out-of-order packets stored during feed gaps
 */
template <size_t MaxBufferedGaps = 1024>
class DualFeedArbitrator {
public:
    enum class FeedSource : uint8_t { FeedA = 0, FeedB = 1 };

    struct GapBufferEntry {
        WireMessage msg;
        bool        active = false;
    };

    DualFeedArbitrator() noexcept {
        reset();
    }

    void reset() noexcept {
        expectedSeqNo_ = 1;
        duplicatesDropped_ = 0;
        sequenceGapsDetected_ = 0;
        retransmitRequests_ = 0;
        gapBuffer_.fill(GapBufferEntry{});
    }

    /**
     * @brief Ingests a message from either Feed A or Feed B.
     * @param source Originating feed source
     * @param msg ITCH WireMessage payload
     * @param outMsg Output message ready for immediate book processing
     * @return true if outMsg contains a valid in-sequence packet to process, false if duplicate or buffered gap.
     */
    [[nodiscard]] bool ingest(FeedSource source, const WireMessage& msg, WireMessage& outMsg) noexcept {
        (void)source;
        const uint64_t seq = msg.seqNo();

        if (seq < expectedSeqNo_) {
            // Duplicate packet (already processed from the alternate feed path)
            ++duplicatesDropped_;
            return false;
        }

        if (seq == expectedSeqNo_) {
            // In-sequence packet
            outMsg = msg;
            ++expectedSeqNo_;
            return true;
        }

        // Out-of-order packet (seq > expectedSeqNo_): buffer for re-ordering & request fill
        const uint64_t gap = seq - expectedSeqNo_;
        sequenceGapsDetected_.fetch_add(gap, std::memory_order_relaxed);
        ++retransmitRequests_;

        const size_t slot = seq % MaxBufferedGaps;
        gapBuffer_[slot] = GapBufferEntry{msg, true};

        return false;
    }

    /**
     * @brief Checks if previously buffered out-of-order messages can now be drained.
     * @param outMsg Output next sequence message
     * @return true if a buffered gap message was drained, false if still waiting on missing packets.
     */
    [[nodiscard]] bool tryDrainBuffered(WireMessage& outMsg) noexcept {
        const size_t slot = expectedSeqNo_ % MaxBufferedGaps;
        if (gapBuffer_[slot].active && gapBuffer_[slot].msg.seqNo() == expectedSeqNo_) {
            outMsg = gapBuffer_[slot].msg;
            gapBuffer_[slot].active = false;
            ++expectedSeqNo_;
            return true;
        }
        return false;
    }

    [[nodiscard]] uint64_t expectedSeqNo() const noexcept { return expectedSeqNo_; }
    [[nodiscard]] uint64_t duplicatesDropped() const noexcept { return duplicatesDropped_; }
    [[nodiscard]] uint64_t sequenceGapsDetected() const noexcept { return sequenceGapsDetected_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t retransmitRequests() const noexcept { return retransmitRequests_; }

private:
    uint64_t expectedSeqNo_ = 1;
    uint64_t duplicatesDropped_ = 0;
    std::atomic<uint64_t> sequenceGapsDetected_{0};
    uint64_t retransmitRequests_ = 0;

    alignas(64) std::array<GapBufferEntry, MaxBufferedGaps> gapBuffer_{};
};

} // namespace hft
