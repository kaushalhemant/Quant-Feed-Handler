#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>

namespace hft {

/**
 * @brief Represents an individual active order stored in the hash map.
 */
struct alignas(32) OrderRecord {
    uint64_t orderId   = 0;     // 8 bytes
    char     side      = 0;     // 1 byte ('B' or 'S')
    bool     occupied  = false; // 1 byte
    bool     cancelled = false; // 1 byte
    uint8_t  padding[5]= {0};   // Explicit padding
    int32_t  price     = 0;     // 4 bytes
    int32_t  quantity  = 0;     // 4 bytes
    uint64_t timestamp = 0;     // 8 bytes
};

/**
 * @brief High-speed, zero-allocation open-addressing hash table.
 * Sized statically at compile time with a power-of-two capacity for fast bitwise masking.
 * Uses a branchless 64-bit Fibonacci multiplicative hash with optimal avalanche characteristics.
 *
 * @tparam Capacity Power-of-two capacity
 */
template <size_t Capacity = (1u << 16)>
class OrderTable {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static constexpr size_t MASK = Capacity - 1;

public:
    OrderTable() noexcept {
        clear();
    }

    void clear() noexcept {
        table_.fill(OrderRecord{});
        occupiedCount_ = 0;
        activeCount_ = 0;
    }

    /**
     * @brief 64-bit Fibonacci Multiplicative Hash.
     * Maps 64-bit Order ID uniformly across the power-of-two table.
     */
    [[nodiscard]] static constexpr size_t hash(uint64_t orderId) noexcept {
        constexpr uint64_t FIBONACCI_PRIME = 0x9E3779B97F4A7C15ull;
        uint64_t h = orderId * FIBONACCI_PRIME;
        return static_cast<size_t>(h >> 43) & MASK;
    }

    /**
     * @brief Inserts or updates an order record in O(1) average time.
     * @return Pointer to inserted OrderRecord, or nullptr if table is exhausted.
     */
    [[nodiscard]] OrderRecord* insert(uint64_t orderId, char side, int32_t price, int32_t quantity, uint64_t ts = 0) noexcept {
        const size_t startIdx = hash(orderId);

        for (size_t probe = 0; probe < Capacity; ++probe) {
            OrderRecord& slot = table_[(startIdx + probe) & MASK];
            if (!slot.occupied) {
                slot.orderId   = orderId;
                slot.side      = side;
                slot.price     = price;
                slot.quantity  = quantity;
                slot.timestamp = ts;
                slot.occupied  = true;
                slot.cancelled = false;
                ++occupiedCount_;
                ++activeCount_;
                return &slot;
            } else if (slot.orderId == orderId) {
                // Re-activate or overwrite existing ID
                if (slot.cancelled) {
                    slot.cancelled = false;
                    ++activeCount_;
                }
                slot.side      = side;
                slot.price     = price;
                slot.quantity  = quantity;
                slot.timestamp = ts;
                return &slot;
            }
        }
        return nullptr; // Table capacity saturated
    }

    /**
     * @brief Looks up an order record by Order ID.
     * @return Pointer to OrderRecord if found, nullptr otherwise.
     */
    [[nodiscard]] OrderRecord* lookup(uint64_t orderId) noexcept {
        const size_t startIdx = hash(orderId);

        for (size_t probe = 0; probe < Capacity; ++probe) {
            OrderRecord& slot = table_[(startIdx + probe) & MASK];
            if (!slot.occupied) {
                return nullptr; // Terminate probe on first unoccupied slot
            }
            if (slot.orderId == orderId) {
                return &slot;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const OrderRecord* lookup(uint64_t orderId) const noexcept {
        const size_t startIdx = hash(orderId);

        for (size_t probe = 0; probe < Capacity; ++probe) {
            const OrderRecord& slot = table_[(startIdx + probe) & MASK];
            if (!slot.occupied) {
                return nullptr;
            }
            if (slot.orderId == orderId) {
                return &slot;
            }
        }
        return nullptr;
    }

    /**
     * @brief Marks an order as cancelled / tombstoned.
     */
    bool erase(uint64_t orderId) noexcept {
        OrderRecord* rec = lookup(orderId);
        if (rec != nullptr && !rec->cancelled) {
            rec->cancelled = true;
            if (activeCount_ > 0) --activeCount_;
            return true;
        }
        return false;
    }

    [[nodiscard]] size_t size() const noexcept {
        return activeCount_;
    }

    [[nodiscard]] size_t occupiedSlots() const noexcept {
        return occupiedCount_;
    }

    [[nodiscard]] double loadFactor() const noexcept {
        return static_cast<double>(occupiedCount_) / static_cast<double>(Capacity);
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity;
    }

private:
    alignas(64) std::array<OrderRecord, Capacity> table_{};
    size_t occupiedCount_ = 0;
    size_t activeCount_ = 0;
};

} // namespace hft
