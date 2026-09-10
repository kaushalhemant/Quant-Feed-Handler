#include "feed_handler/wal_journal.hpp"
#include "feed_handler/order_book.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>

using namespace hft;

void testWALPersistenceAndRecovery() {
    const std::string walPath = "test_feed_handler.wal";

    // Step 1: Open WAL file and append 500 binary market records
    {
        WALJournal wal;
        bool opened = wal.open(walPath, 1024 * 1024); // 1 MB test WAL
        assert(opened);

        for (uint64_t i = 1; i <= 500; ++i) {
            WireMessage msg{};
            msg.msgType = 'A';
            msg.add.msgType = 'A';
            msg.add.seqNo = i;
            msg.add.timestampNs = i * 100ULL;
            msg.add.orderId = 1000 + i;
            msg.add.side = (i % 2 == 0) ? 'B' : 'S';
            msg.add.price = 10000 + (i % 10);
            msg.add.quantity = 50;
            assert(wal.append(msg));
        }

        assert(wal.recordCount() == 500);
        wal.close();
    }

    // Step 2: Simulate process recovery by reopening WAL file and replaying into LimitOrderBook
    {
        WALJournal walRecovery;
        bool reopened = walRecovery.open(walPath, 1024 * 1024);
        assert(reopened);
        assert(walRecovery.recordCount() == 500);

        LimitOrderBook<5, 4096> recoveredBook;
        size_t replayed = walRecovery.recover(recoveredBook);
        assert(replayed == 500);
        assert(recoveredBook.totalMessagesProcessed() == 500);
        assert(recoveredBook.hasBids());
        assert(recoveredBook.hasAsks());

        walRecovery.close();
    }

    // Cleanup test WAL file
    std::remove(walPath.c_str());

    std::printf("[PASS] testWALPersistenceAndRecovery\n");
}

int main() {
    std::printf("--- Running Memory-Mapped WAL Journal Tests ---\n");
    testWALPersistenceAndRecovery();
    std::printf("All WAL Journal tests passed successfully!\n\n");
    return 0;
}
