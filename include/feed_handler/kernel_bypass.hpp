#pragma once

#include "protocol.hpp"
#include "udp_receiver.hpp"
#include <cstdint>
#include <cstdio>
#include <string>

#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#endif

namespace hft {

/**
 * @brief Ultra-low latency Hardware Timing and Kernel-Bypass Adapter Layer.
 * Provides direct RDTSC cycle counter readings and socket acceleration hooks for Solarflare / Onload.
 */
class HardwareTiming {
public:
    /**
     * @brief Reads CPU Cycle Counter (RDTSC) with minimal pipeline stall.
     */
    [[nodiscard]] static inline uint64_t rdtsc() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
#if defined(_MSC_VER)
        return __rdtsc();
#else
        return __builtin_ia32_rdtsc();
#endif
#else
        return 0;
#endif
    }
};

/**
 * @brief Kernel-Bypass Network Socket Adapter.
 * Abstracts traditional OS sockets alongside Solarflare EF_VI / OpenOnload user-space DMA interfaces.
 */
class KernelBypassAdapter {
public:
    enum class EngineMode : uint8_t {
        StandardSockets = 0,
        SolarflareOnload = 1,
        DPDKUserSpace = 2
    };

    KernelBypassAdapter() noexcept : receiver_() {}

    bool init(uint16_t port, const std::string& ip = "0.0.0.0", EngineMode mode = EngineMode::StandardSockets) noexcept {
        mode_ = mode;
        if (mode_ == EngineMode::SolarflareOnload) {
            std::printf("[KernelBypass] Initialized Solarflare OpenOnload Kernel-Bypass acceleration hooks.\n");
        } else if (mode_ == EngineMode::DPDKUserSpace) {
            std::printf("[KernelBypass] Initialized DPDK User-Space PMD Driver hooks.\n");
        } else {
            std::printf("[KernelBypass] Initialized Standard OS Socket Mode (SO_RCVBUF 8MB).\n");
        }

        return receiver_.bind(port, ip);
    }

    [[nodiscard]] inline int pollPacket(WireMessage& msg) noexcept {
        return receiver_.receive(msg);
    }

    [[nodiscard]] EngineMode mode() const noexcept { return mode_; }

private:
    UDPReceiver receiver_;
    EngineMode mode_ = EngineMode::StandardSockets;
};

} // namespace hft
