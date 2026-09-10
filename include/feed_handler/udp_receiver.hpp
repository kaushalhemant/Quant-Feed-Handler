#pragma once

#include "protocol.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
#endif

namespace hft {

/**
 * @brief High-performance, non-blocking UDP Socket Receiver.
 * Optimized for low packet loss and minimal kernel-to-user copy latency.
 */
class UDPReceiver {
public:
    UDPReceiver() noexcept : sock_(INVALID_SOCK) {
        initSocketSubsystem();
    }

    ~UDPReceiver() {
        closeSocket();
        cleanupSocketSubsystem();
    }

    /**
     * @brief Initializes and binds a UDP listening socket.
     * @param port Port number to bind
     * @param ip Local interface IP address (default "0.0.0.0" for any)
     * @param rcvBufSizeBytes Socket receive buffer size (default 8 MB)
     * @return true on success, false on failure
     */
    bool bind(uint16_t port, const std::string& ip = "0.0.0.0", int rcvBufSizeBytes = 8 * 1024 * 1024) noexcept {
        sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCK) {
            std::fprintf(stderr, "[UDPReceiver] Error creating socket\n");
            return false;
        }

        // Enable SO_REUSEADDR
        int reuse = 1;
        ::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        // Enlarge kernel receive buffer to prevent packet dropping during throughput spikes
        if (rcvBufSizeBytes > 0) {
            ::setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBufSizeBytes), sizeof(rcvBufSizeBytes));
        }

        // Set non-blocking mode
        setNonBlocking(true);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (ip == "0.0.0.0" || ip.empty()) {
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
        } else {
            inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        }

        if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            std::fprintf(stderr, "[UDPReceiver] Failed to bind to %s:%u\n", ip.c_str(), port);
            closeSocket();
            return false;
        }

        std::printf("[UDPReceiver] Listening on %s:%u (Kernel RCVBUF: %d MB)\n",
                    ip.c_str(), port, rcvBufSizeBytes / (1024 * 1024));
        return true;
    }

    /**
     * @brief Receives a single packet non-blockingly.
     * @param msg Output WireMessage buffer
     * @return Number of bytes received (>0), 0 if no packet waiting (EWOULDBLOCK), or -1 on fatal error.
     */
    [[nodiscard]] int receive(WireMessage& msg) noexcept {
        if (sock_ == INVALID_SOCK) return -1;

#if defined(_WIN32) || defined(_WIN64)
        int len = ::recv(sock_, reinterpret_cast<char*>(&msg), sizeof(WireMessage), 0);
        if (len == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) return 0;
            return -1;
        }
        return len;
#else
        ssize_t len = ::recv(sock_, reinterpret_cast<char*>(&msg), sizeof(WireMessage), 0);
        if (len < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) return 0;
            return -1;
        }
        return static_cast<int>(len);
#endif
    }

    void closeSocket() noexcept {
        if (sock_ != INVALID_SOCK) {
#if defined(_WIN32) || defined(_WIN64)
            ::closesocket(sock_);
#else
            ::close(sock_);
#endif
            sock_ = INVALID_SOCK;
        }
    }

private:
    socket_t sock_;

    void setNonBlocking(bool nonBlocking) noexcept {
        if (sock_ == INVALID_SOCK) return;
#if defined(_WIN32) || defined(_WIN64)
        u_long mode = nonBlocking ? 1 : 0;
        ::ioctlsocket(sock_, FIONBIO, &mode);
#else
        int flags = ::fcntl(sock_, F_GETFL, 0);
        if (flags != -1) {
            ::fcntl(sock_, F_SETFL, nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
        }
#endif
    }

    static void initSocketSubsystem() noexcept {
#if defined(_WIN32) || defined(_WIN64)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    static void cleanupSocketSubsystem() noexcept {
#if defined(_WIN32) || defined(_WIN64)
        WSACleanup();
#endif
    }
};

} // namespace hft
