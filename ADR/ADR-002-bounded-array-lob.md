# ADR-002: Bounded-Array Limit Order Book (Top-N Level Bubbling)

## Status
Accepted

## Context
Standard textbook Limit Order Book implementations frequently employ self-balancing binary search trees (e.g., Red-Black Trees in `std::map`) or doubly linked lists attached to price nodes.

While asymptotically $O(\log M)$ for depth $M$, tree and pointer-based data structures incur significant memory fragmentation and pointer chasing. Every tree node traversal triggers an L1/L2 cache miss, adding 15–50 nanoseconds per level hop.

## Decision
For high-frequency market data consumption where trading decisions are dominated by top-of-book liquidity (BBO, spread, micro-price, and top-5 levels), we employ a **fixed-size, contiguous stack-allocated array (`std::array<PriceLevel, Depth>` where $Depth \le 10$)**:
1. **Contiguous Cache Locality:** All 5 price levels fit comfortably within a single 64-byte L1 cache line or two contiguous cache lines.
2. **Bounded Insertion-Sort Level Bubbling:** Inserting or updating a level requires at most $N \le 5$ register swaps, which unrolls into branchless CPU SIMD/register moves.
3. **Zero Heap Allocations:** Zero calls to `malloc`/`free` or node allocators on the order addition path.

## Consequences
- **Positive:** Wire-to-book update latency is reduced to under 300 nanoseconds; zero cache thrashing.
- **Trade-off:** Deep-book levels beyond the configured top-$N$ depth ($N=5$ or $N=10$) are evicted unless tracked in a separate off-hot-path deep-book structure.
