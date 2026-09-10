#pragma once

#include "protocol.hpp"
#include "order_book.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace hft {

/**
 * @brief Zero-copy Memory-Mapped Write-Ahead Log (WAL) Journal.
 * High-speed binary disk persistence and process crash recovery engine.
 */
class WALJournal {
public:
    static constexpr size_t DEFAULT_WAL_BYTES = 64 * 1024 * 1024; // 64 MB default file size

    WALJournal() noexcept = default;

    ~WALJournal() {
        close();
    }

    // Non-copyable
    WALJournal(const WALJournal&) = delete;
    WALJournal& operator=(const WALJournal&) = delete;

    /**
     * @brief Opens or creates a memory-mapped WAL file.
     * @param filepath Path to .wal file on disk
     * @param maxSizeBytes File size allocation (default 64MB)
     * @return true on successful memory mapping, false on I/O error
     */
    bool open(const std::string& filepath, size_t maxSizeBytes = DEFAULT_WAL_BYTES) noexcept {
        close();
        filepath_ = filepath;
        maxSizeBytes_ = maxSizeBytes;

#if defined(_WIN32) || defined(_WIN64)
        hFile_ = CreateFileA(filepath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile_ == INVALID_HANDLE_VALUE) {
            std::fprintf(stderr, "[WALJournal] CreateFile failed for %s\n", filepath.c_str());
            return false;
        }

        LARGE_INTEGER sizeLi;
        sizeLi.QuadPart = static_cast<LONGLONG>(maxSizeBytes);
        hMapping_ = CreateFileMappingA(hFile_, NULL, PAGE_READWRITE, sizeLi.HighPart, sizeLi.LowPart, NULL);
        if (hMapping_ == NULL) {
            std::fprintf(stderr, "[WALJournal] CreateFileMapping failed\n");
            CloseHandle(hFile_);
            hFile_ = INVALID_HANDLE_VALUE;
            return false;
        }

        mappedPtr_ = static_cast<char*>(MapViewOfFile(hMapping_, FILE_MAP_ALL_ACCESS, 0, 0, maxSizeBytes));
        if (mappedPtr_ == nullptr) {
            std::fprintf(stderr, "[WALJournal] MapViewOfFile failed\n");
            CloseHandle(hMapping_);
            CloseHandle(hFile_);
            hMapping_ = NULL;
            hFile_ = INVALID_HANDLE_VALUE;
            return false;
        }
#else
        fd_ = ::open(filepath.c_str(), O_CREAT | O_RDWR, 0644);
        if (fd_ < 0) {
            std::fprintf(stderr, "[WALJournal] open failed for %s\n", filepath.c_str());
            return false;
        }

        if (ftruncate(fd_, maxSizeBytes) != 0) {
            std::fprintf(stderr, "[WALJournal] ftruncate failed\n");
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        mappedPtr_ = static_cast<char*>(mmap(NULL, maxSizeBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
        if (mappedPtr_ == MAP_FAILED) {
            std::fprintf(stderr, "[WALJournal] mmap failed\n");
            ::close(fd_);
            fd_ = -1;
            mappedPtr_ = nullptr;
            return false;
        }
#endif

        // Inspect header to find current append offset
        const size_t recordsInFile = maxSizeBytes / sizeof(WireMessage);
        const WireMessage* records = reinterpret_cast<const WireMessage*>(mappedPtr_);
        writeOffsetBytes_ = 0;

        for (size_t i = 0; i < recordsInFile; ++i) {
            if (records[i].msgType == 0) {
                writeOffsetBytes_ = i * sizeof(WireMessage);
                break;
            }
        }

        std::printf("[WALJournal] Mapped %s (%zu MB), Active offset: %zu KB (%zu records)\n",
                    filepath.c_str(), maxSizeBytes / (1024 * 1024), writeOffsetBytes_ / 1024, recordCount());
        return true;
    }

    /**
     * @brief Appends a WireMessage record to the memory-mapped file buffer.
     * @param msg WireMessage struct to append
     * @return true if appended, false if WAL file capacity is reached.
     */
    bool append(const WireMessage& msg) noexcept {
        if (mappedPtr_ == nullptr || (writeOffsetBytes_ + sizeof(WireMessage)) > maxSizeBytes_) {
            return false;
        }

        std::memcpy(mappedPtr_ + writeOffsetBytes_, &msg, sizeof(WireMessage));
        writeOffsetBytes_ += sizeof(WireMessage);
        return true;
    }

    /**
     * @brief Replays all recorded WAL market messages into the target Limit Order Book.
     * @tparam Depth LOB depth
     * @tparam TableCapacity OrderTable capacity
     * @param book LimitOrderBook reference to populate
     * @return Number of records replayed into the order book
     */
    template <size_t Depth, size_t TableCapacity>
    size_t recover(LimitOrderBook<Depth, TableCapacity>& book) noexcept {
        if (mappedPtr_ == nullptr) return 0;

        const size_t count = recordCount();
        const WireMessage* records = reinterpret_cast<const WireMessage*>(mappedPtr_);
        size_t replayed = 0;

        for (size_t i = 0; i < count; ++i) {
            if (records[i].msgType == 'A' || records[i].msgType == 'X' || records[i].msgType == 'E') {
                book.processMessage(records[i]);
                ++replayed;
            }
        }

        std::printf("[WALJournal] Replayed %zu binary records into LimitOrderBook state\n", replayed);
        return replayed;
    }

    /**
     * @brief Flushes pending memory writes to disk asynchronously.
     */
    void flushAsync() noexcept {
        if (mappedPtr_ == nullptr) return;
#if defined(_WIN32) || defined(_WIN64)
        FlushViewOfFile(mappedPtr_, writeOffsetBytes_);
#else
        msync(mappedPtr_, writeOffsetBytes_, MS_ASYNC);
#endif
    }

    void close() noexcept {
        if (mappedPtr_ != nullptr) {
            flushAsync();
#if defined(_WIN32) || defined(_WIN64)
            UnmapViewOfFile(mappedPtr_);
            CloseHandle(hMapping_);
            CloseHandle(hFile_);
            hMapping_ = NULL;
            hFile_ = INVALID_HANDLE_VALUE;
#else
            munmap(mappedPtr_, maxSizeBytes_);
            ::close(fd_);
            fd_ = -1;
#endif
            mappedPtr_ = nullptr;
        }
    }

    [[nodiscard]] size_t recordCount() const noexcept {
        return writeOffsetBytes_ / sizeof(WireMessage);
    }

private:
    std::string filepath_;
    size_t maxSizeBytes_ = 0;
    size_t writeOffsetBytes_ = 0;
    char* mappedPtr_ = nullptr;

#if defined(_WIN32) || defined(_WIN64)
    HANDLE hFile_ = INVALID_HANDLE_VALUE;
    HANDLE hMapping_ = NULL;
#else
    int fd_ = -1;
#endif
};

} // namespace hft
