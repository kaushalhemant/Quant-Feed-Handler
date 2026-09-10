#include "feed_handler/ring_buffer.hpp"
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

using namespace hft;

void testSingleThreadedBasics() {
    SPSCRingBuffer<int, 4> queue;
    assert(queue.empty());

    assert(queue.tryPush(10));
    assert(queue.tryPush(20));
    assert(queue.tryPush(30));
    assert(queue.tryPush(40));
    assert(!queue.tryPush(50)); // Full

    int val = 0;
    assert(queue.tryPop(val) && val == 10);
    assert(queue.tryPop(val) && val == 20);
    assert(queue.tryPush(50)); // Now has space

    assert(queue.tryPop(val) && val == 30);
    assert(queue.tryPop(val) && val == 40);
    assert(queue.tryPop(val) && val == 50);
    assert(!queue.tryPop(val)); // Empty
    (void)val;

    std::printf("[PASS] testSingleThreadedBasics\n");
}

void testMultiThreadedStress() {
    constexpr size_t NUM_ITEMS = 2'000'000;
    static SPSCRingBuffer<uint64_t, 65536> queue;

    std::vector<uint64_t> received;
    received.reserve(NUM_ITEMS);

    std::thread consumer([&]() {
        uint64_t val = 0;
        while (received.size() < NUM_ITEMS) {
            if (queue.tryPop(val)) {
                received.push_back(val);
            }
        }
    });

    std::thread producer([&]() {
        for (uint64_t i = 1; i <= NUM_ITEMS; ++i) {
            while (!queue.tryPush(i)) {
#if defined(__x86_64__) || defined(_M_X64)
                #if defined(_MSC_VER)
                _mm_pause();
                #else
                __builtin_ia32_pause();
                #endif
#endif
            }
        }
    });

    producer.join();
    consumer.join();

    assert(received.size() == NUM_ITEMS);
    for (size_t i = 0; i < NUM_ITEMS; ++i) {
        assert(received[i] == (i + 1));
    }

    std::printf("[PASS] testMultiThreadedStress (%llu items verified with zero drops)\n", static_cast<unsigned long long>(NUM_ITEMS));
}

int main() {
    std::printf("--- Running Lock-Free SPSC Ring Buffer Tests ---\n");
    testSingleThreadedBasics();
    testMultiThreadedStress();
    std::printf("All Ring Buffer tests passed successfully!\n\n");
    return 0;
}
