#include "feed_handler/order_book.hpp"
#include <cassert>
#include <cstdio>

using namespace hft;

void testBasicAddAndBBO() {
    LimitOrderBook<5, 1024> book;
    
    // Add Bid 1: 100.00 x 50
    AddOrderMsg a1{'A', 1, 1000, 101, 'B', 10000, 50};
    book.processAdd(a1);

    assert(book.hasBids());
    assert(!book.hasAsks());
    assert(book.bestBidPrice() == 10000);
    assert(book.bestBidQuantity() == 50);

    // Add Ask 1: 100.05 x 30
    AddOrderMsg a2{'A', 2, 1001, 102, 'S', 10005, 30};
    book.processAdd(a2);

    assert(book.hasAsks());
    assert(book.bestAskPrice() == 10005);
    assert(book.bestAskQuantity() == 30);
    assert(book.spread() == 5);
    assert(book.midPrice() == 10002.5);

    std::printf("[PASS] testBasicAddAndBBO\n");
}

void testLevelAggregation() {
    LimitOrderBook<5, 1024> book;

    // Two orders at same price: 100.00 (qty 20 and qty 30)
    AddOrderMsg a1{'A', 1, 1000, 101, 'B', 10000, 20};
    AddOrderMsg a2{'A', 2, 1001, 102, 'B', 10000, 30};
    book.processAdd(a1);
    book.processAdd(a2);

    assert(book.bids()[0].active);
    assert(book.bids()[0].price == 10000);
    assert(book.bids()[0].quantity == 50);
    assert(book.bids()[0].orderCount == 2);
    assert(!book.bids()[1].active); // Only 1 price level

    std::printf("[PASS] testLevelAggregation\n");
}

void testCancelAndModify() {
    LimitOrderBook<5, 1024> book;

    AddOrderMsg a1{'A', 1, 1000, 101, 'B', 10000, 50};
    book.processAdd(a1);

    // Partial cancel: reduce from 50 to 20
    CancelOrderMsg c1{'X', 2, 1001, 101, 20};
    book.processCancel(c1);

    assert(book.bestBidQuantity() == 20);

    // Full cancel: reduce to 0
    CancelOrderMsg c2{'X', 3, 1002, 101, 0};
    book.processCancel(c2);

    assert(!book.hasBids());
    assert(book.bids()[0].quantity == 0);

    std::printf("[PASS] testCancelAndModify\n");
}

void testExecutionFill() {
    LimitOrderBook<5, 1024> book;

    AddOrderMsg a1{'A', 1, 1000, 101, 'S', 10010, 100};
    book.processAdd(a1);

    // Fill 40 shares
    ExecuteOrderMsg e1{'E', 2, 1001, 101, 40, 10010};
    book.processExecute(e1);

    assert(book.bestAskQuantity() == 60);
    assert(book.tradesExecuted() == 1);

    // Fill remaining 60 shares
    ExecuteOrderMsg e2{'E', 3, 1002, 101, 60, 10010};
    book.processExecute(e2);

    assert(!book.hasAsks());
    assert(book.tradesExecuted() == 2);

    std::printf("[PASS] testExecutionFill\n");
}

void testDepthSortingAndEviction() {
    LimitOrderBook<3, 1024> book; // Depth = 3

    // Add 4 bids in non-sorted order: 99.00, 100.00, 99.50, 98.00
    book.processAdd(AddOrderMsg{'A', 1, 100, 1, 'B', 9900, 10});
    book.processAdd(AddOrderMsg{'A', 2, 101, 2, 'B', 10000, 20});
    book.processAdd(AddOrderMsg{'A', 3, 102, 3, 'B', 9950, 30});
    book.processAdd(AddOrderMsg{'A', 4, 103, 4, 'B', 9800, 40});

    // Top 3 bids must be: 10000, 9950, 9900 (9800 evicted)
    assert(book.bids()[0].price == 10000 && book.bids()[0].quantity == 20);
    assert(book.bids()[1].price == 9950 && book.bids()[1].quantity == 30);
    assert(book.bids()[2].price == 9900 && book.bids()[2].quantity == 10);

    std::printf("[PASS] testDepthSortingAndEviction\n");
}

void testOrderBookInvariantsPropertyTest() {
    LimitOrderBook<5, 4096> book;
    
    // Invariant validation helper
    auto assertInvariants = [&book]() {
        const auto& bids = book.bids();
        const auto& asks = book.asks();

        // Check Bids strictly descending
        for (size_t i = 0; i + 1 < bids.size(); ++i) {
            if (bids[i].active && bids[i + 1].active) {
                assert(bids[i].price > bids[i + 1].price);
                assert(bids[i].quantity > 0);
                assert(bids[i].orderCount > 0);
            }
        }

        // Check Asks strictly ascending
        for (size_t i = 0; i + 1 < asks.size(); ++i) {
            if (asks[i].active && asks[i + 1].active) {
                assert(asks[i].price < asks[i + 1].price);
                assert(asks[i].quantity > 0);
                assert(asks[i].orderCount > 0);
            }
        }

        // Check spread logic
        if (book.hasBids() && book.hasAsks()) {
            assert(book.spread() == (book.bestAskPrice() - book.bestBidPrice()));
            assert(book.midPrice() == (0.5 * (book.bestBidPrice() + book.bestAskPrice())));
        }
    };

    // Feed 50,000 randomized operations
    uint64_t nextId = 1000;
    for (uint64_t i = 1; i <= 50000; ++i) {
        WireMessage msg{};
        if (i % 3 == 0) {
            // Add Bid
            msg.msgType = 'A';
            msg.add.msgType = 'A';
            msg.add.seqNo = i;
            msg.add.timestampNs = i * 10;
            msg.add.orderId = nextId++;
            msg.add.side = 'B';
            msg.add.price = 9900 + static_cast<int32_t>(i % 50);
            msg.add.quantity = 10 + static_cast<int32_t>(i % 100);
            book.processMessage(msg);
        } else if (i % 3 == 1) {
            // Add Ask
            msg.msgType = 'A';
            msg.add.msgType = 'A';
            msg.add.seqNo = i;
            msg.add.timestampNs = i * 10;
            msg.add.orderId = nextId++;
            msg.add.side = 'S';
            msg.add.price = 10000 + static_cast<int32_t>(i % 50);
            msg.add.quantity = 10 + static_cast<int32_t>(i % 100);
            book.processMessage(msg);
        } else {
            // Cancel random recent order
            msg.msgType = 'X';
            msg.cancel.msgType = 'X';
            msg.cancel.seqNo = i;
            msg.cancel.timestampNs = i * 10;
            msg.cancel.orderId = (nextId > 1020) ? (nextId - 10) : 1000;
            msg.cancel.quantity = 0;
            book.processMessage(msg);
        }

        assertInvariants();
    }

    std::printf("[PASS] testOrderBookInvariantsPropertyTest (50,000 randomized state changes asserted)\n");
}

int main() {
    std::printf("--- Running Order Book Unit Tests ---\n");
    testBasicAddAndBBO();
    testLevelAggregation();
    testCancelAndModify();
    testExecutionFill();
    testDepthSortingAndEviction();
    testOrderBookInvariantsPropertyTest();
    std::printf("All Order Book tests passed successfully!\n\n");
    return 0;
}
