cmake_minimum_required(VERSION 3.20)
project(QuantDesk
    VERSION 1.1.0
    DESCRIPTION "Institutional High-Frequency Trading Market Data Feed Handler & Limit Order Book Engine in C++20"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build configuration options
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

# Include directories
include_directories(include)

# Platform-specific optimization & sanitizer flags
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -O3
        -march=native
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )

    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
        add_compile_options(-Wno-error=constraints -fconcepts-diagnostics-depth=1)
    endif()

    if (ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
        add_link_options(-fsanitize=address,undefined)
    endif()

    if (ENABLE_TSAN)
        add_compile_options(-fsanitize=thread -g)
        add_link_options(-fsanitize=thread)
    endif()

    if (ENABLE_COVERAGE)
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    endif()

    # Enable LTO if supported (when sanitizers are off)
    if (NOT ENABLE_SANITIZERS AND NOT ENABLE_TSAN AND NOT ENABLE_COVERAGE)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
        if(ipo_supported)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        endif()
    endif()
elseif (MSVC)
    add_compile_options(
        /O2
        /Ot
        /GL
        /W4
        /permissive-
    )
    if (NOT ENABLE_SANITIZERS)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LTCG")
    endif()
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
add_executable(feed_handler src/main.cpp)
target_link_libraries(feed_handler PRIVATE ${SYSTEM_LIBS})

# ---------------------------------------------------------------------------
# Auxiliary Tool: Mock Exchange UDP Publisher
# ---------------------------------------------------------------------------
add_executable(mock_exchange src/mock_exchange.cpp)
target_link_libraries(mock_exchange PRIVATE ${SYSTEM_LIBS})

# ---------------------------------------------------------------------------
# Continuous Telemetry & Streaming Server for Web Dashboard Bridge
# ---------------------------------------------------------------------------
add_executable(stream_server src/stream_server.cpp)
target_link_libraries(stream_server PRIVATE ${SYSTEM_LIBS})

# ---------------------------------------------------------------------------
# Auxiliary Tool: PCAP Binary Market Data Replayer
# ---------------------------------------------------------------------------
add_executable(pcap_replayer src/pcap_replayer.cpp)
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

add_executable(test_latency_tracker tests/test_latency_tracker.cpp)
target_link_libraries(test_latency_tracker PRIVATE ${SYSTEM_LIBS})
add_test(NAME LatencyTrackerTests COMMAND test_latency_tracker)

add_executable(test_metrics_exporter tests/test_metrics_exporter.cpp)
target_link_libraries(test_metrics_exporter PRIVATE ${SYSTEM_LIBS})
add_test(NAME MetricsExporterTests COMMAND test_metrics_exporter)

# ---------------------------------------------------------------------------
# Install Targets
# ---------------------------------------------------------------------------
install(TARGETS feed_handler stream_server mock_exchange pcap_replayer
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/feed_handler
    DESTINATION include
)
