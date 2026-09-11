# ADR-003: Fibonacci Multiplicative Hashing for Order Lookup

## Status
Accepted

## Context
Individual order cancellations (`'X'`) and order fill executions (`'E'`) reference orders by unique 64-bit `orderId` integers. The engine must locate the original order's price, quantity, and side in $O(1)$ time to adjust the corresponding price level in the limit order book.

Standard `std::unordered_map` relies on bucket arrays pointing to separately allocated linked-list nodes, causing cache misses and runtime heap allocations.

## Decision
We implement a **zero-allocation, open-addressing hash table (`OrderTable<Capacity>`)** with **64-bit Fibonacci Multiplicative Hashing**:
1. **Hash Formula:** $H(K) = (K \cdot 2^{64}/\phi) \gg (64 - \log_2(\text{Capacity}))$, using the golden ratio prime constant `0x9E3779B97F4A7C15ull`.
2. **Single-Instruction Avalanche:** In a single 64-bit hardware multiplication and bit shift, sequential or structured order IDs are scrambled uniformly across the entire power-of-two table.
3. **Linear Probing with Flat Layout:** Continuous array scanning maximizes hardware L1D prefetcher efficiency.

## Consequences
- **Positive:** Average lookup and update latency under 15 nanoseconds; zero heap allocation during trading hours.
- **Trade-off:** Sized statically at compile time (or startup); capacity must be chosen with sufficient headroom ($2^{21} \approx 2 \text{ million slots}$) to keep the load factor below 70%.
