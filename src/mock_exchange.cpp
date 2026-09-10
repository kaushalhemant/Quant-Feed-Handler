#include "feed_handler/protocol.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
using socket_t = int;
#endif

int main(int argc, char* argv[]) {
    std::string targetIp = "127.0.0.1";
    uint16_t targetPort = 12345;
    size_t totalPackets = 500'000;

    if (argc > 1) totalPackets = std::strtoull(argv[1], nullptr, 10);
    if (argc > 2) targetPort = static_cast<uint16_t>(std::atoi(argv[2]));

    std::printf("=================================================================\n");
    std::printf("      MOCK EXCHANGE MARKET DATA PUBLISHER (UDP BROADCASTER)     \n");
    std::printf("=================================================================\n");
    std::printf("  Target Host  : %s:%u\n", targetIp.c_str(), targetPort);
    std::printf("  Total Packets: %llu\n", static_cast<unsigned long long>(totalPackets));
    std::printf("=================================================================\n\n");

#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    socket_t sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#if defined(_WIN32) || defined(_WIN64)
    if (sock == INVALID_SOCKET) {
#else
    if (sock < 0) {
#endif
        std::fprintf(stderr, "Failed to create UDP socket\n");
        return 1;
    }

    // Set high send buffer size
    int sndBufSize = 8 * 1024 * 1024;
    ::setsockopt(sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&sndBufSize), sizeof(sndBufSize));

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(targetPort);
    inet_pton(AF_INET, targetIp.c_str(), &destAddr.sin_addr);

    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<int32_t> bidPriceDist(9950, 9999);
    std::uniform_int_distribution<int32_t> askPriceDist(10000, 10049);
    std::uniform_int_distribution<int32_t> qtyDist(10, 500);
    std::uniform_int_distribution<int> sideDist(0, 1);
    std::uniform_int_distribution<int> actionDist(0, 99); // 60% Add, 25% Cancel, 15% Exec

    std::vector<uint64_t> activeOrders;
    activeOrders.reserve(100'000);

    uint64_t nextOrderId = 1;
    uint64_t seqNo = 1;

    std::printf("[Publisher] Streaming %'zu market data packets...\n", totalPackets);
    const auto startTime = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < totalPackets; ++i) {
        hft::WireMessage msg{};
        const uint64_t nowNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count());

        const int action = actionDist(rng);

        if (action < 60 || activeOrders.empty()) {
            // Add Order
            msg.msgType = 'A';
            msg.add.msgType = 'A';
            msg.add.seqNo = seqNo++;
            msg.add.timestampNs = nowNs;
            msg.add.orderId = nextOrderId++;
            msg.add.side = (sideDist(rng) == 0) ? 'B' : 'S';
            msg.add.price = (msg.add.side == 'B') ? bidPriceDist(rng) : askPriceDist(rng);
            msg.add.quantity = qtyDist(rng);

            activeOrders.push_back(msg.add.orderId);
        } else if (action < 85) {
            // Cancel / Modify Order
            std::uniform_int_distribution<size_t> pick(0, activeOrders.size() - 1);
            size_t idx = pick(rng);
            uint64_t targetId = activeOrders[idx];

            msg.msgType = 'X';
            msg.cancel.msgType = 'X';
            msg.cancel.seqNo = seqNo++;
            msg.cancel.timestampNs = nowNs;
            msg.cancel.orderId = targetId;
            msg.cancel.quantity = (action % 3 == 0) ? 0 : qtyDist(rng) / 2;

            if (msg.cancel.quantity == 0) {
                activeOrders[idx] = activeOrders.back();
                activeOrders.pop_back();
            }
        } else {
            // Execute Order
            std::uniform_int_distribution<size_t> pick(0, activeOrders.size() - 1);
            size_t idx = pick(rng);
            uint64_t targetId = activeOrders[idx];

            msg.msgType = 'E';
            msg.exec.msgType = 'E';
            msg.exec.seqNo = seqNo++;
            msg.exec.timestampNs = nowNs;
            msg.exec.orderId = targetId;
            msg.exec.execQuantity = qtyDist(rng) / 4;
            msg.exec.matchPrice = 10000;
        }

        ::sendto(sock, reinterpret_cast<const char*>(&msg), sizeof(hft::WireMessage), 0,
                 reinterpret_cast<sockaddr*>(&destAddr), sizeof(destAddr));
    }

    const auto endTime = std::chrono::high_resolution_clock::now();
    const double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    const double rate = static_cast<double>(totalPackets) / (elapsedMs / 1000.0);

    std::printf("[Publisher] Broadcast complete!\n");
    std::printf("  Elapsed Time : %.2f ms\n", elapsedMs);
    std::printf("  Transmit Rate: %'.0f packets/sec\n\n", rate);

#if defined(_WIN32) || defined(_WIN64)
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif
    return 0;
}
