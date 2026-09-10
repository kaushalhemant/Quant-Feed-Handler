#include "feed_handler/order_table.hpp"
#include <cassert>
#include <cstdio>

using namespace hft;

void testTableOperations() {
    OrderTable<1024> table;

    // Test Insert
    OrderRecord* r1 = table.insert(1001, 'B', 5000, 10, 12345);
    assert(r1 != nullptr);
    assert(r1->orderId == 1001);
    assert(r1->price == 5000);
    assert(r1->quantity == 10);
    assert(r1->occupied);

    // Test Lookup
    OrderRecord* found = table.lookup(1001);
    assert(found == r1);

    // Test non-existent lookup
    assert(table.lookup(9999) == nullptr);

    // Test overwrite / update on same ID
    OrderRecord* r1_updated = table.insert(1001, 'B', 5000, 25, 12399);
    assert(r1_updated == r1);
    assert(r1_updated->quantity == 25);

    // Test inserting multiple records with potential hash collisions
    for (uint64_t i = 1; i <= 500; ++i) {
        OrderRecord* rec = table.insert(i * 1024 + 7, 'S', static_cast<int32_t>(i), 100);
        assert(rec != nullptr);
        (void)rec;
    }

    for (uint64_t i = 1; i <= 500; ++i) {
        OrderRecord* rec = table.lookup(i * 1024 + 7);
        assert(rec != nullptr);
        assert(rec->side == 'S');
        assert(rec->price == static_cast<int32_t>(i));
        (void)rec;
    }

    (void)r1;
    (void)found;
    (void)r1_updated;

    std::printf("[PASS] testTableOperations (500+ probed insertions & lookups verified)\n");
}

int main() {
    std::printf("--- Running Order Table Hash Map Tests ---\n");
    testTableOperations();
    std::printf("All Order Table tests passed successfully!\n\n");
    return 0;
}
