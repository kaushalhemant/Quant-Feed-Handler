#pragma once

#include <cstdint>
#include <cstring>
#include <bit>

namespace hft {

#pragma pack(push, 1)

/**
 * @brief Add Order Message ('A')
 * Represents a new limit order submitted to the exchange book.
 */
struct AddOrderMsg {
    char     msgType;      // 'A'
    uint64_t seqNo;        // Monotonically increasing packet sequence number
    uint64_t timestampNs;  // Nanosecond-precision exchange timestamp
    uint64_t orderId;      // Unique 64-bit order identifier
    char     side;         // 'B' (Bid / Buy) or 'S' (Ask / Sell)
    int32_t  price;        // Fixed-point price in ticks (e.g., 10000 = $100.00)
    int32_t  quantity;     // Order quantity in lots / shares
};
static_assert(sizeof(AddOrderMsg) == 34, "AddOrderMsg must be 34 bytes packed");

/**
 * @brief Cancel / Modify Order Message ('X')
 * Cancels or reduces the remaining quantity of an active order.
 */
struct CancelOrderMsg {
    char     msgType;      // 'X'
    uint64_t seqNo;        // Sequence number
    uint64_t timestampNs;  // Nanosecond timestamp
    uint64_t orderId;      // Target order ID
    int32_t  quantity;     // New remaining quantity (0 denotes complete cancellation)
};
static_assert(sizeof(CancelOrderMsg) == 29, "CancelOrderMsg must be 29 bytes packed");

/**
 * @brief Order Execution / Fill Message ('E')
 * Informs the market that an existing order has been matched and filled.
 */
struct ExecuteOrderMsg {
    char     msgType;      // 'E'
    uint64_t seqNo;        // Sequence number
    uint64_t timestampNs;  // Nanosecond timestamp
    uint64_t orderId;      // Executed order ID
    int32_t  execQuantity; // Number of shares filled
    int32_t  matchPrice;   // Match execution price
};
static_assert(sizeof(ExecuteOrderMsg) == 33, "ExecuteOrderMsg must be 33 bytes packed");

/**
 * @brief MoldUDP64 Session Packet Header
 * Used by Nasdaq ITCH 5.0 feeds for sequence tracking and packet batching over UDP multicast.
 */
struct MoldHeader {
    char     session[10];     // 10-byte alphanumeric session identifier
    uint64_t sequenceNumber;  // Sequence number of first payload message in packet
    uint16_t messageCount;    // Number of downstream payload messages in this datagram
};
static_assert(sizeof(MoldHeader) == 20, "MoldHeader must be 20 bytes packed");

/**
 * @brief MoldUDP64 Individual Block Header
 * Prefixed to each payload message within a MoldUDP64 datagram.
 */
struct MoldBlockHeader {
    uint16_t messageLength;   // Length of payload message following this block header
};
static_assert(sizeof(MoldBlockHeader) == 2, "MoldBlockHeader must be 2 bytes packed");

/**
 * @brief Fixed-size wire container holding any supported message variant.
 * Avoids dynamic allocation and framing parsing, enabling flat array ingestion.
 */
union WireMessage {
    char            msgType;
    AddOrderMsg     add;
    CancelOrderMsg  cancel;
    ExecuteOrderMsg exec;

    [[nodiscard]] constexpr uint64_t seqNo() const noexcept {
        return add.seqNo; // seqNo offset is identical across all valid packet variants
    }

    [[nodiscard]] constexpr uint64_t timestampNs() const noexcept {
        return add.timestampNs;
    }

    [[nodiscard]] constexpr uint64_t orderId() const noexcept {
        return add.orderId;
    }

    /**
     * @brief Computes expected wire struct size based on msgType.
     * @return Expected size in bytes, or 0 if unknown type.
     */
    [[nodiscard]] constexpr static size_t expectedSize(char type) noexcept {
        switch (type) {
            case 'A': return sizeof(AddOrderMsg);
            case 'X': return sizeof(CancelOrderMsg);
            case 'E': return sizeof(ExecuteOrderMsg);
            default:  return 0;
        }
    }

    /**
     * @brief Validates if this wire message holds valid memory invariants.
     */
    [[nodiscard]] constexpr bool isValid() const noexcept {
        if (msgType == 'A') {
            return (add.side == 'B' || add.side == 'S') && (add.price > 0) && (add.quantity > 0);
        } else if (msgType == 'X') {
            return (cancel.quantity >= 0);
        } else if (msgType == 'E') {
            return (exec.execQuantity > 0) && (exec.matchPrice > 0);
        }
        return false;
    }
};
static_assert(sizeof(WireMessage) == sizeof(AddOrderMsg), "WireMessage size mismatch");

#pragma pack(pop)

} // namespace hft

