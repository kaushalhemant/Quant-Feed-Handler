#include "feed_handler/protocol.hpp"
#include "feed_handler/parser_validator.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
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
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace hft;

// Simplified PCAP Global Header struct
#pragma pack(push, 1)
struct PcapGlobalHeader {
    uint32_t magicNumber;   // 0xa1b2c3d4
    uint16_t versionMajor;  // 2
    uint16_t versionMinor;  // 4
    int32_t  thisZone;
    uint32_t sigFigs;
    uint32_t snapLen;
    uint32_t network;      // 1 = Ethernet
};

struct PcapPacketHeader {
    uint32_t tsSec;
    uint32_t tsUsec;
    uint32_t inclLen;
    uint32_t origLen;
};
#pragma pack(pop)

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::printf("Usage: %s <pcap_file|synthetic_batch> <destination_port> [target_ip]\n", argv[0]);
        std::printf("  Example: %s market_feed.pcap 12345 127.0.0.1\n", argv[0]);
        return 1;
    }

    const std::string filename = argv[1];
    const uint16_t targetPort = static_cast<uint16_t>(std::atoi(argv[2]));
    const std::string targetIp = (argc >= 4) ? argv[3] : "127.0.0.1";

#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    int sendSock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sendSock < 0) {
        std::fprintf(stderr, "[PcapReplayer] Error creating UDP socket\n");
        return 1;
    }

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(targetPort);
    inet_pton(AF_INET, targetIp.c_str(), &destAddr.sin_addr);

    std::printf("[PcapReplayer] Starting market data replay to %s:%u...\n", targetIp.c_str(), targetPort);

    std::ifstream pcapFile(filename, std::ios::binary);
    std::vector<WireMessage> messagesToReplay;

    if (pcapFile.is_open()) {
        PcapGlobalHeader globalHeader{};
        pcapFile.read(reinterpret_cast<char*>(&globalHeader), sizeof(globalHeader));

        if (pcapFile.gcount() == sizeof(globalHeader) &&
            (globalHeader.magicNumber == 0xa1b2c3d4 || globalHeader.magicNumber == 0xd4c3b2a1)) {
            std::printf("[PcapReplayer] Valid PCAP capture file detected (%s). Extracting ITCH frames...\n", filename.c_str());

            while (pcapFile) {
                PcapPacketHeader pktHeader{};
                pcapFile.read(reinterpret_cast<char*>(&pktHeader), sizeof(pktHeader));
                if (pcapFile.gcount() < static_cast<std::streamsize>(sizeof(pktHeader))) break;

                std::vector<char> pktData(pktHeader.inclLen);
                pcapFile.read(pktData.data(), pktHeader.inclLen);
                if (pcapFile.gcount() < static_cast<std::streamsize>(pktHeader.inclLen)) break;

                // Search for valid WireMessage payload inside packet offset
                if (pktHeader.inclLen >= sizeof(WireMessage)) {
                    for (size_t offset = 0; offset + sizeof(AddOrderMsg) <= pktHeader.inclLen; ++offset) {
                        WireMessage parsedMsg{};
                        auto res = WireParserValidator::validateAndParse(pktData.data() + offset, pktHeader.inclLen - offset, parsedMsg);
                        if (res == WireParserValidator::ValidationResult::Valid) {
                            messagesToReplay.push_back(parsedMsg);
                            break;
                        }
                    }
                }
            }
        }
        pcapFile.close();
    }

    // Fallback: Generate synthetic ITCH stream if pcap file is not present or empty
    if (messagesToReplay.empty()) {
        std::printf("[PcapReplayer] PCAP file not found or empty. Generating 50,000 synthetic market packets for replay...\n");
        uint64_t seqNo = 1;
        uint64_t orderId = 1000;
        for (int i = 0; i < 50000; ++i) {
            WireMessage msg{};
            if (i % 10 < 7) {
                msg.msgType = 'A';
                msg.add.msgType = 'A';
                msg.add.seqNo = seqNo++;
                msg.add.timestampNs = i * 1000ULL;
                msg.add.orderId = orderId++;
                msg.add.side = (i % 2 == 0) ? 'B' : 'S';
                msg.add.price = 10000 + (i % 20);
                msg.add.quantity = 100 + (i % 50) * 10;
            } else if (i % 10 < 9) {
                msg.msgType = 'X';
                msg.cancel.msgType = 'X';
                msg.cancel.seqNo = seqNo++;
                msg.cancel.timestampNs = i * 1000ULL;
                msg.cancel.orderId = (orderId > 1010) ? (orderId - 5) : 1000;
                msg.cancel.quantity = 0;
            } else {
                msg.msgType = 'E';
                msg.exec.msgType = 'E';
                msg.exec.seqNo = seqNo++;
                msg.exec.timestampNs = i * 1000ULL;
                msg.exec.orderId = (orderId > 1010) ? (orderId - 3) : 1000;
                msg.exec.execQuantity = 50;
                msg.exec.matchPrice = 10005;
            }
            messagesToReplay.push_back(msg);
        }
    }

    std::printf("[PcapReplayer] Replaying %zu market packets...\n", messagesToReplay.size());
    size_t sent = 0;

    for (const auto& msg : messagesToReplay) {
        size_t sizeToSend = WireMessage::expectedSize(msg.msgType);
        ::sendto(sendSock, reinterpret_cast<const char*>(&msg), static_cast<int>(sizeToSend), 0,
                 reinterpret_cast<const sockaddr*>(&destAddr), sizeof(destAddr));
        ++sent;

        if (sent % 5000 == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    std::printf("[PcapReplayer] Successfully replayed %zu market packets to %s:%u\n", sent, targetIp.c_str(), targetPort);

#if defined(_WIN32) || defined(_WIN64)
    closesocket(sendSock);
    WSACleanup();
#else
    close(sendSock);
#endif
    return 0;
}
