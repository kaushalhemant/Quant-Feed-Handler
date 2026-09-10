#pragma once

#include <cstdint>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

namespace hft {

/**
 * @brief Thread affinity and priority utilities to eliminate OS scheduler jitter.
 */
class ThreadAffinity {
public:
    /**
     * @brief Pins the calling thread to a specific CPU core index.
     * @param coreId Core index (0, 1, 2, ...)
     * @return true if successfully pinned, false otherwise
     */
    static bool pinCurrentThread(uint32_t coreId) noexcept {
#if defined(_WIN32) || defined(_WIN64)
        const DWORD_PTR mask = static_cast<DWORD_PTR>(1ULL << (coreId % 64));
        const DWORD_PTR result = SetThreadAffinityMask(GetCurrentThread(), mask);
        return (result != 0);
#else
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(coreId, &cpuset);
        const int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        return (result == 0);
#endif
    }

    /**
     * @brief Sets thread priority to real-time / highest priority to prevent preemption.
     */
    static bool setHighestPriority() noexcept {
#if defined(_WIN32) || defined(_WIN64)
        return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST) != 0;
#else
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        return pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0;
#endif
    }
};

} // namespace hft
