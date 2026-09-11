#include "feed_handler/feed_arbitrator.hpp"
#include <cassert>
#include <cstdio>

using namespace hft;

void testArbitratorDeduplication() {
    DualFeedArbitrator<256> arbitrator;
    assert(arbitrator.expectedSeqNo() == 1);

    AddOrderMsg a1{'A', 1, 1000, 101, 'B', 10000, 50};
    WireMessage m1{};
    m1.add = a1;

    WireMessage outMsg{};
    // First arrival from Feed A -> Should be accepted
    const bool ok1 = arbitrator.ingest(DualFeedArbitrator<256>::FeedSource::FeedA, m1, outMsg);
    assert(ok1);
    assert(outMsg.seqNo() == 1);
    assert(arbitrator.expectedSeqNo() == 2);

    // Duplicate arrival from Feed B -> Should be dropped as duplicate
    const bool ok2 = arbitrator.ingest(DualFeedArbitrator<256>::FeedSource::FeedB, m1, outMsg);
    assert(!ok2);
    assert(arbitrator.duplicatesDropped() == 1);

    (void)ok1;
    (void)ok2;
    std::printf("[PASS] testArbitratorDeduplication\n");
}

void testArbitratorOutofOrderGapRecovery() {
    DualFeedArbitrator<256> arbitrator;

    AddOrderMsg a1{'A', 1, 1000, 101, 'B', 10000, 50};
    AddOrderMsg a2{'A', 2, 1001, 102, 'B', 10005, 30};
    AddOrderMsg a3{'A', 3, 1002, 103, 'B', 10010, 40};

    WireMessage m1{}, m2{}, m3{}, outMsg{};
    m1.add = a1;
    m2.add = a2;
    m3.add = a3;

    // Sequence 1 arrives
    assert(arbitrator.ingest(DualFeedArbitrator<256>::FeedSource::FeedA, m1, outMsg));

    // Sequence 3 arrives early (Sequence 2 missing -> Gap detected)
    const bool ok3 = arbitrator.ingest(DualFeedArbitrator<256>::FeedSource::FeedA, m3, outMsg);
    assert(!ok3); // Buffered
    assert(arbitrator.sequenceGapsDetected() >= 1);
    assert(arbitrator.retransmitRequests() == 1);

    // Sequence 2 arrives later
    const bool ok2 = arbitrator.ingest(DualFeedArbitrator<256>::FeedSource::FeedB, m2, outMsg);
    assert(ok2);
    assert(outMsg.seqNo() == 2);

    // Now drain buffered sequence 3
    const bool drained3 = arbitrator.tryDrainBuffered(outMsg);
    assert(drained3);
    assert(outMsg.seqNo() == 3);
    assert(arbitrator.expectedSeqNo() == 4);

    (void)ok3;
    (void)ok2;
    (void)drained3;
    std::printf("[PASS] testArbitratorOutofOrderGapRecovery\n");
}

int main() {
    std::printf("--- Running Dual Feed Arbitrator Tests ---\n");
    testArbitratorDeduplication();
    testArbitratorOutofOrderGapRecovery();
    std::printf("All Arbitrator tests passed successfully!\n\n");
    return 0;
}
