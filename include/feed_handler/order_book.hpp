#pragma once

#include "protocol.hpp"
#include "order_table.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace hft {

/**
 * @brief Represents an aggregated price level in the order book.
 */
struct alignas(16) PriceLevel {
    int32_t price      = 0;     // Fixed-point price (ticks)
    int32_t quantity   = 0;     // Total aggregate quantity at this level
    int32_t orderCount = 0;     // Number of resting orders at this level
    bool    active     = false; // Whether this level currently holds volume
};

/**
 * @brief High-performance, zero-allocation Limit Order Book Engine.
 * 
 * Hardware & Latency Optimizations:
 * - Fixed-depth top-N contiguous array layout (depth <= 10) for maximum L1/L2 cache locality.
 * - Insertion-sort level bubbling bounded to N steps (faster than balanced tree/map pointer hops).
 * - Multi-threaded or single-threaded hot path marked `noexcept` with zero heap allocations.
 * - Integrated sequence number verification for packet drop / out-of-order detection.
 * 
 * @tparam Depth Number of visible price levels per side (default: 5)
 * @tparam TableCapacity Power-of-2 capacity for underlying OrderTable
 */
template <size_t Depth = 5, size_t TableCapacity = (1u << 16)>
class LimitOrderBook {
public:
    static constexpr size_t BOOK_DEPTH = Depth;

    LimitOrderBook() noexcept {
        reset();
    }

    void reset() noexcept {
        orderTable_.clear();
        bids_.fill(PriceLevel{});
        asks_.fill(PriceLevel{});
        totalMessagesProcessed_ = 0;
        expectedSeqNo_ = 1;
        sequenceGapsDetected_ = 0;
        tradesExecutedCount_ = 0;
    }

    /**
     * @brief Hot-path message dispatch handler.
     */
    void processMessage(const WireMessage& msg) noexcept {
        ++totalMessagesProcessed_;

        // Sequence number check
        if (expectedSeqNo_ > 0 && msg.seqNo() != expectedSeqNo_) {
            if (msg.seqNo() > expectedSeqNo_) {
                sequenceGapsDetected_ += (msg.seqNo() - expectedSeqNo_);
            }
            expectedSeqNo_ = msg.seqNo() + 1;
        } else {
            ++expectedSeqNo_;
        }

        switch (msg.msgType) {
            case 'A': [[likely]]
                processAdd(msg.add);
                break;
            case 'X': [[likely]]
                processCancel(msg.cancel);
                break;
            case 'E':
                processExecute(msg.exec);
                break;
            default: [[unlikely]]
                // Drop malformed/unknown packet variant
                break;
        }
    }

    /**
     * @brief Ingests an 'A' Add Order packet.
     */
    void processAdd(const AddOrderMsg& msg) noexcept {
        OrderRecord* rec = orderTable_.insert(msg.orderId, msg.side, msg.price, msg.quantity, msg.timestampNs);
        if (rec == nullptr) [[unlikely]] {
            return;
        }

        if (msg.side == 'B') {
            applyDelta(bids_, msg.price, msg.quantity, +1, /*ascending=*/false);
        } else {
            applyDelta(asks_, msg.price, msg.quantity, +1, /*ascending=*/true);
        }
    }

    /**
     * @brief Ingests an 'X' Cancel/Reduce Order packet.
     */
    void processCancel(const CancelOrderMsg& msg) noexcept {
        OrderRecord* rec = orderTable_.lookup(msg.orderId);
        if (rec == nullptr || rec->cancelled) [[unlikely]] {
            return;
        }

        const int32_t qtyDelta = msg.quantity - rec->quantity;
        const int32_t orderDelta = (msg.quantity <= 0) ? -1 : 0;

        if (rec->side == 'B') {
            applyDelta(bids_, rec->price, qtyDelta, orderDelta, /*ascending=*/false);
        } else {
            applyDelta(asks_, rec->price, qtyDelta, orderDelta, /*ascending=*/true);
        }

        rec->quantity = msg.quantity;
        if (msg.quantity <= 0) {
            rec->cancelled = true;
        }
    }

    /**
     * @brief Ingests an 'E' Execute Fill packet.
     */
    void processExecute(const ExecuteOrderMsg& msg) noexcept {
        ++tradesExecutedCount_;
        OrderRecord* rec = orderTable_.lookup(msg.orderId);
        if (rec == nullptr || rec->cancelled) [[unlikely]] {
            return;
        }

        const int32_t newQty = (rec->quantity > msg.execQuantity) ? (rec->quantity - msg.execQuantity) : 0;
        const int32_t qtyDelta = -msg.execQuantity;
        const int32_t orderDelta = (newQty == 0) ? -1 : 0;

        if (rec->side == 'B') {
            applyDelta(bids_, rec->price, qtyDelta, orderDelta, /*ascending=*/false);
        } else {
            applyDelta(asks_, rec->price, qtyDelta, orderDelta, /*ascending=*/true);
        }

        rec->quantity = newQty;
        if (newQty == 0) {
            rec->cancelled = true;
        }
    }

    // --- Market Analytics Getters ---

    [[nodiscard]] const std::array<PriceLevel, Depth>& bids() const noexcept { return bids_; }
    [[nodiscard]] const std::array<PriceLevel, Depth>& asks() const noexcept { return asks_; }

    [[nodiscard]] bool hasBids() const noexcept { return bids_[0].active; }
    [[nodiscard]] bool hasAsks() const noexcept { return asks_[0].active; }

    [[nodiscard]] int32_t bestBidPrice() const noexcept { return bids_[0].active ? bids_[0].price : 0; }
    [[nodiscard]] int32_t bestAskPrice() const noexcept { return asks_[0].active ? asks_[0].price : 0; }
    [[nodiscard]] int32_t bestBidQuantity() const noexcept { return bids_[0].active ? bids_[0].quantity : 0; }
    [[nodiscard]] int32_t bestAskQuantity() const noexcept { return asks_[0].active ? asks_[0].quantity : 0; }

    [[nodiscard]] int32_t spread() const noexcept {
        if (hasBids() && hasAsks()) {
            return asks_[0].price - bids_[0].price;
        }
        return 0;
    }

    [[nodiscard]] double midPrice() const noexcept {
        if (hasBids() && hasAsks()) {
            return 0.5 * (static_cast<double>(bids_[0].price) + static_cast<double>(asks_[0].price));
        }
        return 0.0;
    }

    /**
     * @brief Volume-Weighted Micro-Price (Order-Flow / Imbalance adjusted).
     */
    [[nodiscard]] double microPrice() const noexcept {
        if (!hasBids() || !hasAsks()) return 0.0;
        const double qBid = static_cast<double>(bids_[0].quantity);
        const double qAsk = static_cast<double>(asks_[0].quantity);
        const double totalVol = qBid + qAsk;
        if (totalVol <= 0.0) return midPrice();
        return (static_cast<double>(bids_[0].price) * qAsk + static_cast<double>(asks_[0].price) * qBid) / totalVol;
    }

    [[nodiscard]] uint64_t totalMessagesProcessed() const noexcept { return totalMessagesProcessed_; }
    [[nodiscard]] uint64_t sequenceGapsDetected() const noexcept { return sequenceGapsDetected_; }
    [[nodiscard]] uint64_t tradesExecuted() const noexcept { return tradesExecutedCount_; }

    /**
     * @brief Prints ASCII Top-of-Book depth snapshot.
     */
    void printBook(FILE* out = stdout) const noexcept {
        std::fprintf(out, "\n  +-------------------------------------------------------------+\n");
        std::fprintf(out, "  |                      LIMIT ORDER BOOK                       |\n");
        std::fprintf(out, "  +------------------------------+------------------------------+\n");
        std::fprintf(out, "  |             BIDS             |             ASKS             |\n");
        std::fprintf(out, "  |  Orders    Qty        Price  |  Price       Qty      Orders |\n");
        std::fprintf(out, "  +------------------------------+------------------------------+\n");

        for (size_t i = 0; i < Depth; ++i) {
            const auto& b = bids_[i];
            const auto& a = asks_[i];

            char bOrd[8] = "-", bQty[10] = "-", bPx[10] = "-";
            char aPx[10] = "-", aQty[10] = "-", aOrd[8] = "-";

            if (b.active) {
                std::snprintf(bOrd, sizeof(bOrd), "%d", b.orderCount);
                std::snprintf(bQty, sizeof(bQty), "%d", b.quantity);
                std::snprintf(bPx, sizeof(bPx), "%.2f", b.price / 100.0);
            }
            if (a.active) {
                std::snprintf(aPx, sizeof(aPx), "%.2f", a.price / 100.0);
                std::snprintf(aQty, sizeof(aQty), "%d", a.quantity);
                std::snprintf(aOrd, sizeof(aOrd), "%d", a.orderCount);
            }

            std::fprintf(out, "  |  %-6s  %-9s  %-7s |  %-7s  %-9s  %-6s |\n",
                         bOrd, bQty, bPx, aPx, aQty, aOrd);
        }
        std::fprintf(out, "  +------------------------------+------------------------------+\n");
        if (hasBids() && hasAsks()) {
            std::fprintf(out, "  | Spread: $%.2f | Mid: $%.2f | MicroPrice: $%.2f        |\n",
                         spread() / 100.0, midPrice() / 100.0, microPrice() / 100.0);
            std::fprintf(out, "  +-------------------------------------------------------------+\n\n");
        }
    }

private:
    alignas(64) OrderTable<TableCapacity> orderTable_;
    alignas(64) std::array<PriceLevel, Depth> bids_{};
    alignas(64) std::array<PriceLevel, Depth> asks_{};

    uint64_t totalMessagesProcessed_ = 0;
    uint64_t expectedSeqNo_ = 1;
    uint64_t sequenceGapsDetected_ = 0;
    uint64_t tradesExecutedCount_ = 0;

    /**
     * @brief Applies signed quantity & order count deltas to the price depth array.
     */
    static void applyDelta(std::array<PriceLevel, Depth>& levels,
                           int32_t price, int32_t qtyDelta, int32_t orderDelta, bool ascending) noexcept {
        // Step 1: Check if the price level already exists in the top-N book
        for (size_t i = 0; i < Depth; ++i) {
            if (levels[i].active && levels[i].price == price) {
                levels[i].quantity += qtyDelta;
                levels[i].orderCount += orderDelta;
                if (levels[i].quantity <= 0 || levels[i].orderCount <= 0) {
                    removeLevel(levels, i);
                }
                return;
            }
        }

        // If nothing is being added, no insertion is needed
        if (qtyDelta <= 0) return;

        // Step 2: Locate candidate insertion slot
        size_t insertPos = Depth;
        for (size_t i = 0; i < Depth; ++i) {
            if (!levels[i].active) {
                insertPos = i;
                break;
            }
        }

        // If the top-N array is full, check if the new price beats the worst level
        if (insertPos == Depth) {
            const bool improves = ascending ? (price < levels[Depth - 1].price)
                                            : (price > levels[Depth - 1].price);
            if (!improves) return; // Does not make the visible top-N depth cut
            insertPos = Depth - 1;
        }

        levels[insertPos] = PriceLevel{price, qtyDelta, (orderDelta > 0 ? orderDelta : 1), true};

        // Step 3: Bubble into sorted order (Bounded insertion sort, N <= 10)
        while (insertPos > 0) {
            const bool outOfOrder = ascending
                ? (levels[insertPos].price < levels[insertPos - 1].price)
                : (levels[insertPos].price > levels[insertPos - 1].price);
            if (!outOfOrder) break;
            std::swap(levels[insertPos], levels[insertPos - 1]);
            --insertPos;
        }
    }

    static void removeLevel(std::array<PriceLevel, Depth>& levels, size_t idx) noexcept {
        for (size_t i = idx; i + 1 < Depth; ++i) {
            levels[i] = levels[i + 1];
        }
        levels[Depth - 1] = PriceLevel{};
    }
};

} // namespace hft
