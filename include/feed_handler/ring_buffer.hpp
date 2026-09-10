#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace hft {

/**
 * @brief Lock-Free Single Producer Single Consumer (SPSC) Ring Buffer.
 * Designed for ultra-low latency thread-to-thread communication.
 * 
 * Hardware Optimizations:
 * - Cache-line aligned (alignas(64)) to eliminate false sharing between producer and consumer cores.
 * - Power-of-2 capacity with bitwise AND masking instead of modulo division.
 * - Acquire-Release memory ordering semantics to minimize CPU pipeline stalls.
 * - Zero dynamic memory allocations during runtime operation.
 * 
 * @tparam T Element type (must be trivially copyable for optimal performance)
 * @tparam Capacity Power-of-two buffer size
 */
template <typename T, size_t Capacity = 65536>
class SPSCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

    static constexpr size_t MASK = Capacity - 1;

public:
    SPSCRingBuffer() noexcept : head_(0), cachedTail_(0), tail_(0), cachedHead_(0) {}

    ~SPSCRingBuffer() = default;

    // Non-copyable and non-movable to ensure thread safety invariants
    SPSCRingBuffer(const SPSCRingBuffer&) = delete;
    SPSCRingBuffer& operator=(const SPSCRingBuffer&) = delete;

    /**
     * @brief Pushes an item into the queue (Producer thread only).
     * @param item Value to push
     * @return true if successfully enqueued, false if queue is full
     */
    [[nodiscard]] bool tryPush(const T& item) noexcept {
        const size_t currentHead = head_.load(std::memory_order_relaxed);

        // Check if queue has space using cached tail to avoid cache line bouncing
        if ((currentHead - cachedTail_) >= Capacity) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if ((currentHead - cachedTail_) >= Capacity) {
                return false; // Queue is genuinely full
            }
        }

        buffer_[currentHead & MASK] = item;
        head_.store(currentHead + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Emplaces an item in-place (Producer thread only).
     */
    template <typename... Args>
    [[nodiscard]] bool tryEmplace(Args&&... args) noexcept {
        const size_t currentHead = head_.load(std::memory_order_relaxed);

        if ((currentHead - cachedTail_) >= Capacity) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if ((currentHead - cachedTail_) >= Capacity) {
                return false;
            }
        }

        buffer_[currentHead & MASK] = T{std::forward<Args>(args)...};
        head_.store(currentHead + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pops an item from the queue (Consumer thread only).
     * @param item Output reference to store popped value
     * @return true if item was dequeued, false if queue is empty
     */
    [[nodiscard]] bool tryPop(T& item) noexcept {
        const size_t currentTail = tail_.load(std::memory_order_relaxed);

        // Check if queue has available elements using cached head
        if (currentTail == cachedHead_) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (currentTail == cachedHead_) {
                return false; // Queue is empty
            }
        }

        item = buffer_[currentTail & MASK];
        tail_.store(currentTail + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief Checks if queue is currently empty (approximate from consumer side).
     */
    [[nodiscard]] bool empty() const noexcept {
        return tail_.load(std::memory_order_relaxed) == head_.load(std::memory_order_relaxed);
    }

    /**
     * @brief Returns current approximate size.
     */
    [[nodiscard]] size_t size() const noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t t = tail_.load(std::memory_order_relaxed);
        return (h >= t) ? (h - t) : 0;
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity;
    }

private:
    // --- Producer cache line (Written by Producer, Read by Consumer) ---
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) size_t cachedTail_{0};

    // --- Consumer cache line (Written by Consumer, Read by Producer) ---
    alignas(64) std::atomic<size_t> tail_{0};
    alignas(64) size_t cachedHead_{0};

    // --- Storage Buffer (Separated on distinct cache line) ---
    alignas(64) T buffer_[Capacity];
};

} // namespace hft
