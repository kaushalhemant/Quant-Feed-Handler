cmake_minimum_required(VERSION 3.20)
project(UltraLowLatencyFeedHandler
    VERSION 1.0.0
    DESCRIPTION "High-Frequency Trading (HFT) Market Data Feed Handler & Limit Order Book Engine in C++20"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(include)

# Platform-specific optimization flags
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O3
        -march=native
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
    # Enable LTO if supported
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
elseif (MSVC)
    add_compile_options(
        /O2
        /Ot
        /GL
        /W4
        /permissive-
    )
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG")
endif()

# System libraries
set(SYSTEM_LIBS "")
if (WIN32)
    list(APPEND SYSTEM_LIBS ws2_32)
else()
    list(APPEND SYSTEM_LIBS pthread)
endif()

# ---------------------------------------------------------------------------
# Main Executable: Feed Handler & Order Book Engine
# ---------------------------------------------------------------------------
add_executable(feed_handler
    src/main.cpp
)
target_link_libraries(feed_handler PRIVATE ${SYSTEM_LIBS})

# ---------------------------------------------------------------------------
# Auxiliary Tool: Mock Exchange UDP Publisher
# ---------------------------------------------------------------------------
add_executable(mock_exchange
    src/mock_exchange.cpp
)
target_link_libraries(mock_exchange PRIVATE ${SYSTEM_LIBS})

# ---------------------------------------------------------------------------
# Continuous Telemetry & Streaming Server for Web Dashboard Bridge
# ---------------------------------------------------------------------------
add_executable(stream_server
    src/stream_server.cpp
)
target_link_libraries(stream_server PRIVATE ${SYSTEM_LIBS})

# ---------------------------------------------------------------------------
# Auxiliary Tool: PCAP Binary Market Data Replayer
# ---------------------------------------------------------------------------
add_executable(pcap_replayer
    src/pcap_replayer.cpp
)
target_link_libraries(pcap_replayer PRIVATE ${SYSTEM_LIBS})

# ---------------------------------------------------------------------------
# Test Suite
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE ${SYSTEM_LIBS})
add_test(NAME OrderBookTests COMMAND test_order_book)

add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
target_link_libraries(test_ring_buffer PRIVATE ${SYSTEM_LIBS})
add_test(NAME RingBufferTests COMMAND test_ring_buffer)

add_executable(test_order_table tests/test_order_table.cpp)
target_link_libraries(test_order_table PRIVATE ${SYSTEM_LIBS})
add_test(NAME OrderTableTests COMMAND test_order_table)

add_executable(fuzz_parser tests/fuzz_parser.cpp)
target_link_libraries(fuzz_parser PRIVATE ${SYSTEM_LIBS})
add_test(NAME FuzzParserTests COMMAND fuzz_parser)

add_executable(test_arbitrator tests/test_arbitrator.cpp)
target_link_libraries(test_arbitrator PRIVATE ${SYSTEM_LIBS})
add_test(NAME ArbitratorTests COMMAND test_arbitrator)

add_executable(test_wal_journal tests/test_wal_journal.cpp)
target_link_libraries(test_wal_journal PRIVATE ${SYSTEM_LIBS})
add_test(NAME WALJournalTests COMMAND test_wal_journal)

