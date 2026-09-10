#pragma once

#include "protocol.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <cmath>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace hft {

/**
 * @brief Zero-allocation wire parsing validator.
 * Ensures packet memory bounds and struct invariants before memory casting or book mutation.
 */
class WireParserValidator {
public:
    enum class ValidationResult : uint8_t {
        Valid = 0,
        TruncatedHeader,
        UnknownMessageType,
        LengthMismatch,
        InvalidSide,
        InvalidPrice,
        InvalidQuantity,
        SequenceOutOfOrder
    };

    /**
     * @brief Validates raw buffer memory bounds and message payload.
     * @param buffer Raw buffer received from socket
     * @param bytesReceived Size of buffer in bytes
     * @param outMsg Output parsed WireMessage
     * @return ValidationResult status code
     */
    [[nodiscard]] static ValidationResult validateAndParse(const char* buffer, size_t bytesReceived, WireMessage& outMsg) noexcept {
        if (buffer == nullptr || bytesReceived < 1) [[unlikely]] {
            return ValidationResult::TruncatedHeader;
        }

        const char msgType = buffer[0];
        const size_t reqSize = WireMessage::expectedSize(msgType);

        if (reqSize == 0) [[unlikely]] {
            return ValidationResult::UnknownMessageType;
        }

        if (bytesReceived < reqSize) [[unlikely]] {
            return ValidationResult::LengthMismatch;
        }

        // Safe memory copy into output struct container
        std::memcpy(&outMsg, buffer, reqSize);

        // Field value invariant checks
        switch (msgType) {
            case 'A': {
                if (outMsg.add.side != 'B' && outMsg.add.side != 'S') [[unlikely]] {
                    return ValidationResult::InvalidSide;
                }
                if (outMsg.add.price <= 0) [[unlikely]] {
                    return ValidationResult::InvalidPrice;
                }
                if (outMsg.add.quantity <= 0) [[unlikely]] {
                    return ValidationResult::InvalidQuantity;
                }
                break;
            }
            case 'X': {
                if (outMsg.cancel.quantity < 0) [[unlikely]] {
                    return ValidationResult::InvalidQuantity;
                }
                break;
            }
            case 'E': {
                if (outMsg.exec.execQuantity <= 0) [[unlikely]] {
                    return ValidationResult::InvalidQuantity;
                }
                if (outMsg.exec.matchPrice <= 0) [[unlikely]] {
                    return ValidationResult::InvalidPrice;
                }
                break;
            }
            default:
                return ValidationResult::UnknownMessageType;
        }

        return ValidationResult::Valid;
    }

    [[nodiscard]] static const char* resultToString(ValidationResult result) noexcept {
        switch (result) {
            case ValidationResult::Valid: return "Valid";
            case ValidationResult::TruncatedHeader: return "TruncatedHeader";
            case ValidationResult::UnknownMessageType: return "UnknownMessageType";
            case ValidationResult::LengthMismatch: return "LengthMismatch";
            case ValidationResult::InvalidSide: return "InvalidSide";
            case ValidationResult::InvalidPrice: return "InvalidPrice";
            case ValidationResult::InvalidQuantity: return "InvalidQuantity";
            case ValidationResult::SequenceOutOfOrder: return "SequenceOutOfOrder";
            default: return "UnknownError";
        }
    }

    /**
     * @brief Parses a user-supplied text line (CSV or space-delimited) into a WireMessage.
     * Supports formats like:
     *   Add: "A,1001,B,224.95,500" or "A 1001 B 22495 500" or "ORDER A 1001 B 224.95 500"
     *   Cancel: "X,1001,0,0,0" or "X 1001 0" or "CANCEL 1001"
     *   Execute: "E,1006,0,225.00,500" or "E 1006 225.00 500"
     *
     * @param line Input text line
     * @param seqNo Sequence number to assign
     * @param outMsg Output parsed WireMessage
     * @return true if successfully parsed and valid, false otherwise.
     */
    static bool parseUserLine(const std::string& line, uint64_t seqNo, WireMessage& outMsg) noexcept {
        if (line.empty() || line[0] == '#' || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) {
            return false;
        }

        // Tokenize line by comma or whitespace
        std::vector<std::string> tokens;
        tokens.reserve(6);
        std::string current;
        for (char c : line) {
            if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }

        if (tokens.empty()) return false;

        size_t idx = 0;
        if (tokens[idx] == "ORDER" || tokens[idx] == "order") {
            idx++;
        }

        if (idx >= tokens.size()) return false;

        const std::string& typeStr = tokens[idx++];
        char type = static_cast<char>(std::toupper(static_cast<unsigned char>(typeStr[0])));

        auto parsePriceTicks = [](const std::string& s) -> int32_t {
            if (s.find('.') != std::string::npos) {
                double val = std::strtod(s.c_str(), nullptr);
                return static_cast<int32_t>(std::llround(val * 100.0));
            }
            return static_cast<int32_t>(std::strtol(s.c_str(), nullptr, 10));
        };

        const uint64_t nowNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count());

        std::memset(&outMsg, 0, sizeof(WireMessage));

        if (type == 'A') { // Add Order: A [orderId] [side] [price] [qty]
            if (tokens.size() - idx < 4) return false;
            uint64_t orderId = std::strtoull(tokens[idx++].c_str(), nullptr, 10);
            char side = static_cast<char>(std::toupper(static_cast<unsigned char>(tokens[idx++][0])));
            int32_t price = parsePriceTicks(tokens[idx++]);
            int32_t qty = static_cast<int32_t>(std::strtol(tokens[idx++].c_str(), nullptr, 10));

            if (side != 'B' && side != 'S') return false;
            if (price <= 0 || qty <= 0) return false;

            outMsg.msgType = 'A';
            outMsg.add.msgType = 'A';
            outMsg.add.seqNo = seqNo;
            outMsg.add.timestampNs = nowNs;
            outMsg.add.orderId = orderId;
            outMsg.add.side = side;
            outMsg.add.price = price;
            outMsg.add.quantity = qty;
            return true;
        } else if (type == 'X' || type == 'C') { // Cancel Order: X [orderId] [opt: side/0] [opt: 0] [qty/0]
            if (tokens.size() - idx < 1) return false;
            uint64_t orderId = std::strtoull(tokens[idx++].c_str(), nullptr, 10);
            int32_t newQty = 0;
            if (idx < tokens.size()) {
                // If there are dummy fields (e.g. side, price from 5-col CSV: X,1001,0,0,0)
                if (tokens.size() - idx >= 3) {
                    idx += 2; // skip side, price
                    newQty = static_cast<int32_t>(std::strtol(tokens[idx++].c_str(), nullptr, 10));
                } else {
                    newQty = static_cast<int32_t>(std::strtol(tokens[idx++].c_str(), nullptr, 10));
                }
            }

            outMsg.msgType = 'X';
            outMsg.cancel.msgType = 'X';
            outMsg.cancel.seqNo = seqNo;
            outMsg.cancel.timestampNs = nowNs;
            outMsg.cancel.orderId = orderId;
            outMsg.cancel.quantity = newQty;
            return true;
        } else if (type == 'E') { // Execute Trade: E [orderId] [opt: side/0] [matchPrice] [execQty]
            if (tokens.size() - idx < 2) return false;
            uint64_t orderId = std::strtoull(tokens[idx++].c_str(), nullptr, 10);
            int32_t matchPrice = 0;
            int32_t execQty = 0;

            if (tokens.size() - idx >= 3) {
                idx++; // skip side or dummy column
                matchPrice = parsePriceTicks(tokens[idx++]);
                execQty = static_cast<int32_t>(std::strtol(tokens[idx++].c_str(), nullptr, 10));
            } else if (tokens.size() - idx == 2) {
                matchPrice = parsePriceTicks(tokens[idx++]);
                execQty = static_cast<int32_t>(std::strtol(tokens[idx++].c_str(), nullptr, 10));
            } else {
                return false;
            }

            if (execQty <= 0) return false;

            outMsg.msgType = 'E';
            outMsg.exec.msgType = 'E';
            outMsg.exec.seqNo = seqNo;
            outMsg.exec.timestampNs = nowNs;
            outMsg.exec.orderId = orderId;
            outMsg.exec.matchPrice = matchPrice;
            outMsg.exec.execQuantity = execQty;
            return true;
        }

        return false;
    }
};

} // namespace hft

