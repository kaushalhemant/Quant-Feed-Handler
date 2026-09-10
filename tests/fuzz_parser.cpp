#include "feed_handler/protocol.hpp"
#include "feed_handler/parser_validator.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace hft;

// Direct test function for LLVM libFuzzer or standalone random bytes harness
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || data == nullptr) return 0;

    WireMessage msg{};
    WireParserValidator::ValidationResult res = WireParserValidator::validateAndParse(
        reinterpret_cast<const char*>(data), size, msg);

    if (res == WireParserValidator::ValidationResult::Valid) {
        // Confirm invariants hold if valid
        assert(msg.msgType == 'A' || msg.msgType == 'X' || msg.msgType == 'E');
        if (msg.msgType == 'A') {
            assert(msg.add.side == 'B' || msg.add.side == 'S');
            assert(msg.add.price > 0);
            assert(msg.add.quantity > 0);
        }
    }
    return 0;
}

void testFuzzWithRandomBytes() {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<size_t> lenDist(0, 100);

    size_t validCount = 0;
    size_t invalidCount = 0;

    for (int iteration = 0; iteration < 100000; ++iteration) {
        size_t len = lenDist(rng);
        std::vector<uint8_t> randomBuf(len);
        for (size_t i = 0; i < len; ++i) {
            randomBuf[i] = static_cast<uint8_t>(byteDist(rng));
        }

        WireMessage msg{};
        auto res = WireParserValidator::validateAndParse(reinterpret_cast<const char*>(randomBuf.data()), randomBuf.size(), msg);
        if (res == WireParserValidator::ValidationResult::Valid) {
            ++validCount;
        } else {
            ++invalidCount;
        }
    }

    std::printf("[PASS] testFuzzWithRandomBytes (Fuzzed 100,000 random byte sequences cleanly, Valid: %zu, Invalid: %zu)\n",
                validCount, invalidCount);
}

void testValidMsgParsing() {
    AddOrderMsg validAdd{'A', 100, 1234567, 9999, 'B', 10500, 50};
    WireMessage msg{};
    auto res = WireParserValidator::validateAndParse(reinterpret_cast<const char*>(&validAdd), sizeof(validAdd), msg);
    assert(res == WireParserValidator::ValidationResult::Valid);
    assert(msg.add.orderId == 9999);
    assert(msg.add.price == 10500);

    // Test truncated packet
    auto truncatedRes = WireParserValidator::validateAndParse(reinterpret_cast<const char*>(&validAdd), sizeof(validAdd) - 5, msg);
    assert(truncatedRes == WireParserValidator::ValidationResult::LengthMismatch);

    // Test invalid side
    validAdd.side = 'Z';
    auto invalidSideRes = WireParserValidator::validateAndParse(reinterpret_cast<const char*>(&validAdd), sizeof(validAdd), msg);
    assert(invalidSideRes == WireParserValidator::ValidationResult::InvalidSide);

    std::printf("[PASS] testValidMsgParsing\n");
}

int main() {
    std::printf("--- Running Wire Parser Fuzz & Safety Tests ---\n");
    testValidMsgParsing();
    testFuzzWithRandomBytes();
    std::printf("All Parser Fuzzing tests passed successfully!\n\n");
    return 0;
}
