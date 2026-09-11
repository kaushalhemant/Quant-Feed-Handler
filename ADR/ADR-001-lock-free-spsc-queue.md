# ADR-001: Lock-Free Single Producer Single Consumer (SPSC) Ring Buffer

## Status
Accepted (Architectural Invariant)

## Context
High-frequency trading feed handlers require deterministic, microsecond-level message passing between the network ingestion thread (handling incoming UDP socket datagrams) and the downstream order book calculation engine. 

Traditional synchronization mechanisms like `std::mutex` and `std::condition_variable` introduce non-deterministic kernel context switching, OS scheduler preemption, and priority inversion jitter. Multi-Producer Multi-Consumer (MPMC) lock-free queues (like Michael-Scott queues or `moodycamel::ConcurrentQueue`) introduce atomic CAS (Compare-And-Swap) contention and cache coherency traffic across multiple writing cores.

## Decision
We implement a dedicated, lock-free **Single Producer Single Consumer (SPSC)** ring buffer with the following hardware-level properties:
1. **Cache-Line Alignment (`alignas(64)`):** The producer's head index and the consumer's tail index are isolated on separate 64-byte cache lines to eliminate false sharing.
2. **Local Index Caching:** `cachedTail_` on the producer side and `cachedHead_` on the consumer side prevent continuous cache line invalidations across physical CPU cores.
3. **Acquire-Release Memory Ordering (`std::memory_order_acquire` / `std::memory_order_release`):** Ensures memory visibility of the payload without the overhead of `std::memory_order_seq_cst` full pipeline barriers.
4. **Power-of-Two Masking:** Eliminates runtime integer modulo division with branchless bitwise AND masking (`currentHead & (Capacity - 1)`).

## Consequences
- **Positive:** Sub-10 nanosecond queueing overhead; zero kernel mutex contention; deterministic tail latency.
- **Trade-off:** Strictly bound to single-producer single-consumer topologies. Multi-source feed arbitration is handled upstream in `DualFeedArbitrator` before pushing into the SPSC ring.
