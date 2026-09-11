# ADR-004: Memory-Mapped Write-Ahead Log (WAL) for State Recovery

## Status
Accepted

## Context
High-frequency market data engines must maintain crash consistency and support audit replay of historical order streams. Traditional file I/O operations (`fwrite`, `write` system calls) transition through user/kernel space and block calling threads.

## Decision
We utilize a **Memory-Mapped Write-Ahead Log (`WALJournal`)**:
1. **Windows:** `CreateFileMappingA` and `MapViewOfFile`.
2. **POSIX:** `open` with `mmap` (`MAP_SHARED`) and asynchronous `msync(MS_ASYNC)`.
3. **Zero-Copy Disk Writes:** Appending a record is simply a memory copy (`memcpy`) into the mapped pointer offset.
4. **State Recovery:** During engine restart, `recover(LimitOrderBook&)` replays the binary sequence of packed wire messages to reconstruct the order book state deterministically.

## Consequences
- **Positive:** Microsecond disk persistence offloaded directly to the operating system's virtual memory page cache; zero user-space I/O buffering overhead.
- **Trade-off:** Pre-allocates fixed-size file chunks (e.g. 64 MB default) on disk.
