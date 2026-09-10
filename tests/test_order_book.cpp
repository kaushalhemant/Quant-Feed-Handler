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

int main() {
    std::printf("--- Running Order Book Unit Tests ---\n");
    testBasicAddAndBBO();
    testLevelAggregation();
    testCancelAndModify();
    testExecutionFill();
    testDepthSortingAndEviction();
    std::printf("All Order Book tests passed successfully!\n\n");
    return 0;
}
