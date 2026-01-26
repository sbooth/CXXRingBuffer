//
// SPDX-FileCopyrightText: 2025 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

#include "CXXRingBuffer/RingBuffer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t KB = 1024;
constexpr std::size_t MB = 1024 * KB;
constexpr std::size_t GB = 1024 * MB;

struct POD {
    uint32_t a;
    uint64_t b;
};

static_assert(std::is_trivially_copyable_v<POD>);

class RingBufferTest : public ::testing::Test {
  protected:
    CXXRingBuffer::RingBuffer rb;
};

// A type that is trivially copyable but might throw during default construction
struct ThrowingDefault {
    int value{0};
    static inline bool should_throw = false;

    // Must be default initializable
    ThrowingDefault() noexcept(false) {
        if (should_throw) {
            throw std::runtime_error("ctor failed");
        }
    }

    // Logic to keep it trivially copyable (C++17/20 requirements)
    ThrowingDefault(const ThrowingDefault&) = default;
    ThrowingDefault& operator=(const ThrowingDefault&) = default;
};

// Ensure test type meets RingBuffer concepts
static_assert(std::is_trivially_copyable_v<ThrowingDefault>);
static_assert(std::default_initializable<ThrowingDefault>);

class RingBufferExceptionTest : public ::testing::Test {
  protected:
    CXXRingBuffer::RingBuffer rb;
    void SetUp() override {
        rb.allocate(1 * KB);
        ThrowingDefault::should_throw = false;
    }
    void TearDown() override {
        ThrowingDefault::should_throw = false;
    }
};

// Structure to hold our test parameters
struct StressParams {
    std::size_t capacity;
    std::chrono::seconds duration;
};

class RingBufferStressTest : public ::testing::TestWithParam<StressParams> {
  public:
    // Helper to print nice parameter names in the test runner
    static std::string ParamNameGenerator(const ::testing::TestParamInfo<StressParams>& info) {
        const auto cap = info.param.capacity;
        if (cap >= MB) {
            return std::to_string(cap / MB) + "MB";
        }
        if (cap >= KB) {
            return std::to_string(cap / KB) + "KB";
        }
        return std::to_string(cap) + "Bytes";
    }

  protected:
    CXXRingBuffer::RingBuffer rb;
};

// A mixed-type structure to stress alignment and multi-value logic
struct PacketHeader {
    uint32_t sequence;
    uint8_t type;
    double timestamp;
};

} // namespace

TEST_F(RingBufferTest, Empty) {
    EXPECT_EQ(rb.capacity(), 0);
    EXPECT_EQ(rb.availableBytes(), 0);
    EXPECT_EQ(rb.freeSpace(), 0);

    uint8_t d[1024];
    EXPECT_EQ(rb.read(d, 1, 1024, true), 0);
    EXPECT_EQ(rb.write(d, 1, 1024, true), 0);
}

TEST_F(RingBufferTest, Capacity) {
    EXPECT_FALSE(rb.allocate(1));
    EXPECT_TRUE(rb.allocate(2));

    EXPECT_TRUE(rb.allocate(1024));
    EXPECT_EQ(rb.capacity(), 1024);
    EXPECT_EQ(rb.availableBytes(), 0);
    EXPECT_EQ(rb.freeSpace(), rb.capacity());
}

TEST_F(RingBufferTest, Functional) {
    ASSERT_TRUE(rb.allocate(128));

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(-100, 100);

    constexpr size_t size = 10;
    std::vector<int> vec(size);
    for (auto& v : vec) {
        v = dist(gen);
    }

    EXPECT_EQ(rb.write(vec.data(), sizeof(int), vec.size(), true), vec.size());
    EXPECT_EQ(rb.availableBytes(), vec.size() * sizeof(int));

    std::vector<int> read(size);
    EXPECT_EQ(rb.read(read.data(), sizeof(int), read.size(), true), vec.size());

    EXPECT_EQ(vec, read);
    EXPECT_EQ(rb.availableBytes(), 0);
}

TEST_F(RingBufferTest, ThroughputBenchmarkChunkedMultiThreaded) {
    constexpr std::size_t bufferSize = 1 * MB;          // 1MB buffer
    constexpr std::size_t totalDataToMove = 10ULL * GB; // 10GB
    constexpr std::size_t chunkSize = 4 * KB;           // 4KB chunks (page size)

    ASSERT_TRUE(rb.allocate(bufferSize));

    std::vector<uint8_t> data(chunkSize, 0xAA);
    std::vector<uint8_t> sink(chunkSize, 0x00);

    const auto start = std::chrono::high_resolution_clock::now();

    // Producer Thread
    std::thread producer([&]() {
        size_t sent = 0;
        while (sent < totalDataToMove) {
            size_t written = rb.write(std::span<const uint8_t>{data}, false);
            sent += written;
            if (written == 0) {
                std::this_thread::yield();
            }
        }
    });

    // Consumer Thread
    std::thread consumer([&]() {
        size_t received = 0;
        while (received < totalDataToMove) {
            size_t read = rb.read(std::span{sink}, false);
            received += read;
            if (read == 0) {
                std::this_thread::yield();
            }
        }
    });

    if (producer.joinable()) {
        producer.join();
    }
    if (consumer.joinable()) {
        consumer.join();
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> diff = end - start;

    const double gigabytes = static_cast<double>(totalDataToMove) / GB;
    const double throughput = gigabytes / diff.count();

    RecordProperty("TotalGB", std::to_string(gigabytes));
    RecordProperty("GB_per_sec", std::to_string(throughput));

    std::cout << "[ BENCH    ] Transferred " << gigabytes << " GB in " << diff.count() << "sec (" << throughput
              << " GB/sec)" << std::endl;
}

// MARK: -

TEST_F(RingBufferTest, DefaultConstructedIsInvalid) {
    EXPECT_FALSE(rb);
    EXPECT_EQ(rb.capacity(), 0);
}

TEST_F(RingBufferTest, AllocateRoundsToPowerOfTwo) {
    EXPECT_TRUE(rb.allocate(3));
    EXPECT_TRUE(rb);
    EXPECT_GE(rb.capacity(), 3);
    EXPECT_TRUE((rb.capacity() & (rb.capacity() - 1)) == 0);

    EXPECT_TRUE(rb.allocate(100));
    EXPECT_EQ(rb.capacity(), 128); // Next power of 2
    EXPECT_TRUE(rb);
}

TEST_F(RingBufferTest, AllocateMinimumCapacity) {
    EXPECT_TRUE(rb.allocate(CXXRingBuffer::RingBuffer::minCapacity));
    EXPECT_TRUE(rb);
    EXPECT_GE(rb.capacity(), CXXRingBuffer::RingBuffer::minCapacity);
}

TEST_F(RingBufferTest, DeallocateResetsState) {
    EXPECT_TRUE(rb.allocate(64));
    rb.deallocate();
    EXPECT_FALSE(rb);
    EXPECT_EQ(rb.capacity(), 0);
}

TEST_F(RingBufferTest, WriteAndReadSingleValue) {
    ASSERT_TRUE(rb.allocate(64));

    int value = 42;
    EXPECT_TRUE(rb.writeValue(value));

    int out = 0;
    EXPECT_TRUE(rb.readValue(out));
    EXPECT_EQ(out, 42);
    EXPECT_TRUE(rb.isEmpty());
}

TEST_F(RingBufferTest, WriteReadMultipleItems) {
    ASSERT_TRUE(rb.allocate(128));

    std::vector<int> input{1, 2, 3, 4, 5};
    std::vector<int> output(5);

    EXPECT_EQ(rb.write(std::span<const int>{input}), input.size());
    EXPECT_EQ(rb.read(std::span<int>{output}), output.size());
    EXPECT_EQ(input, output);
}

TEST_F(RingBufferTest, WriteFailsWhenNoPartialAllowed) {
    ASSERT_TRUE(rb.allocate(16)); // fits only 4 ints
    std::array<int, 5> data{};

    EXPECT_EQ(rb.write(data.data(), sizeof(int), 5, false), 0);
    EXPECT_TRUE(rb.isEmpty());
}

TEST_F(RingBufferTest, ReadFailsWhenNoPartialAllowed) {
    ASSERT_TRUE(rb.allocate(32));
    int x = 1;
    rb.writeValue(x);

    int out[2]{};
    EXPECT_EQ(rb.read(out, sizeof(int), 2, false), 0);
}

TEST_F(RingBufferTest, WrapAroundReadWrite) {
    ASSERT_TRUE(rb.allocate(16));
    // 16 bytes total
    // 4 ints max

    int a = 1, b = 2, c = 3, d = 4;
    rb.writeValue(a);
    rb.writeValue(b);

    int out = 0;
    rb.readValue(out);
    EXPECT_EQ(out, 1);

    rb.writeValue(c);
    rb.writeValue(d); // wrap

    int results[3];
    EXPECT_EQ(rb.read(results, sizeof(int), 3, false), 3);
    EXPECT_EQ(results[0], 2);
    EXPECT_EQ(results[1], 3);
    EXPECT_EQ(results[2], 4);
}

TEST_F(RingBufferTest, PeekDoesNotAdvance) {
    ASSERT_TRUE(rb.allocate(64));

    int x = 7;
    rb.writeValue(x);

    int peeked = 0;
    EXPECT_TRUE(rb.peekValue(peeked));
    EXPECT_EQ(peeked, 7);
    EXPECT_FALSE(rb.isEmpty());

    int read = 0;
    rb.readValue(read);
    EXPECT_EQ(read, 7);
}

TEST_F(RingBufferTest, SkipAndDrain) {
    ASSERT_TRUE(rb.allocate(64));

    std::array<int, 4> data{1, 2, 3, 4};
    rb.write(std::span<const int>{data});

    EXPECT_EQ(rb.skip(sizeof(int), 2), 2);
    EXPECT_EQ(rb.availableBytes(), 2 * sizeof(int));

    EXPECT_EQ(rb.drain(), 2 * sizeof(int));
    EXPECT_TRUE(rb.isEmpty());
}

TEST_F(RingBufferTest, PODWriteRead) {
    ASSERT_TRUE(rb.allocate(64));

    POD in{1, 2};
    EXPECT_TRUE(rb.writeValue(in));

    auto out = rb.readValue<POD>();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->a, 1);
    EXPECT_EQ(out->b, 2);
}

TEST_F(RingBufferTest, PeekOptionalFailsWhenInsufficientData) {
    EXPECT_TRUE(rb.allocate(64));
    auto val = rb.peekValue<int>();
    EXPECT_FALSE(val.has_value());
}

TEST_F(RingBufferTest, WriteAndReadValuesVariadic) {
    ASSERT_TRUE(rb.allocate(64));

    int a = 1;
    double b = 2.5;
    uint8_t c = 9;

    EXPECT_TRUE(rb.writeValues(a, b, c));

    int aa;
    double bb;
    uint8_t cc;

    EXPECT_TRUE(rb.readValues(aa, bb, cc));
    EXPECT_EQ(aa, 1);
    EXPECT_EQ(bb, 2.5);
    EXPECT_EQ(cc, 9);
}

TEST_F(RingBufferTest, PeekValuesTuple) {
    EXPECT_TRUE(rb.allocate(64));
    rb.writeValues(1, 2);

    auto tup = rb.peekValues<int, unsigned>();
    ASSERT_TRUE(tup.has_value());
    EXPECT_EQ(std::get<0>(*tup), 1);
    EXPECT_EQ(std::get<1>(*tup), 2);
}

TEST_F(RingBufferTest, WriteVectorAndCommit) {
    ASSERT_TRUE(rb.allocate(32));

    auto [front, back] = rb.writeVector();
    ASSERT_GT(front.size(), 0);

    std::memset(front.data(), 0xAB, front.size());
    rb.commitWrite(front.size());

    EXPECT_EQ(rb.availableBytes(), front.size());
}

TEST_F(RingBufferTest, ReadVectorAndCommit) {
    ASSERT_TRUE(rb.allocate(32));

    std::array<uint8_t, 8> data{};
    rb.write(std::span<const uint8_t>{data});

    auto [front, back] = rb.readVector();
    ASSERT_EQ(front.size(), data.size());

    rb.commitRead(front.size());
    EXPECT_TRUE(rb.isEmpty());
}

TEST_F(RingBufferTest, SPSCStressTestSequentialValues) {
    constexpr std::size_t iterations = 1'000'000;
    ASSERT_TRUE(rb.allocate(64 * KB));

    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        for (std::size_t i = 0; i < iterations;) {
            if (rb.writeValue(i)) {
                ++i;
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::size_t expected = 0;
        while (!producerDone.load(std::memory_order_acquire) || !rb.isEmpty()) {
            std::size_t value;
            if (rb.readValue(value)) {
                ASSERT_EQ(value, expected);
                ++expected;
            }
        }
        EXPECT_EQ(expected, iterations);
    });

    if (producer.joinable()) {
        producer.join();
    }
    if (consumer.joinable()) {
        consumer.join();
    }

    EXPECT_TRUE(rb.isEmpty());
}

TEST_F(RingBufferTest, ThroughputBenchmarkSingleThreaded) {
    constexpr std::size_t iterations = 10'000'000;
    ASSERT_TRUE(rb.allocate(1 * MB));

    const auto start = std::chrono::high_resolution_clock::now();

    for (std::size_t i = 0; i < iterations; ++i) {
        while (!rb.writeValue(i)) {}
        std::size_t out;
        while (!rb.readValue(out)) {}
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> elapsed = end - start;

    const double ops_per_sec = iterations / elapsed.count();

    RecordProperty("TotalOps", std::to_string(iterations));
    RecordProperty("Ops_per_sec", std::to_string(ops_per_sec));

    std::cout << "[ BENCH    ] Operations per second = " << ops_per_sec << "\n";
}

// MARK: -

TEST_F(RingBufferTest, BasicReadWrite) {
    ASSERT_TRUE(rb.allocate(64));
    int input = 42;
    int output = 0;

    EXPECT_TRUE(rb.writeValue(input));
    EXPECT_EQ(rb.availableBytes(), sizeof(int));
    EXPECT_TRUE(rb.readValue(output));
    EXPECT_EQ(output, 42);
    EXPECT_TRUE(rb.isEmpty());
}

TEST_F(RingBufferTest, WrapAroundBehavior) {
    ASSERT_TRUE(rb.allocate(16));
    std::vector<uint8_t> data(10, 0xA);

    // Write 10 bytes
    EXPECT_EQ(rb.write(std::span<const uint8_t>{data}), 10);
    // Read 5 bytes
    std::vector<uint8_t> sink(5);
    EXPECT_EQ(rb.read(std::span{sink}), 5);

    // Now write 10 more bytes (this will trigger wrap around)
    // Free space: 16 - (10 - 5) = 11
    EXPECT_EQ(rb.write(std::span<const uint8_t>{data}), 10);

    EXPECT_EQ(rb.availableBytes(), 15);

    // Drain and verify
    EXPECT_EQ(rb.drain(), 15);
    EXPECT_TRUE(rb.isEmpty());
}

TEST_F(RingBufferTest, VariadicValues) {
    ASSERT_TRUE(rb.allocate(1 * KB));
    struct Foo {
        int a;
        float b;
    };

    EXPECT_TRUE(rb.writeValues(10, 20.5f, Foo{1, 2.0f}));

    int out1;
    float out2;
    Foo out3;
    EXPECT_TRUE(rb.readValues(out1, out2, out3));

    EXPECT_EQ(out1, 10);
    EXPECT_EQ(out2, 20.5f);
    EXPECT_EQ(out3.a, 1);
}

TEST_F(RingBufferTest, SPSCStressTestWithYield) {
    constexpr std::size_t bufferSize = 4 * KB;
    constexpr std::size_t totalItems = 1'000'000;
    ASSERT_TRUE(rb.allocate(bufferSize));

    std::thread producer([&]() {
        for (size_t i = 0; i < totalItems; ++i) {
            while (!rb.writeValue(i)) {
                std::this_thread::yield(); // Buffer full
            }
        }
    });

    std::thread consumer([&]() {
        for (size_t i = 0; i < totalItems; ++i) {
            size_t val = 0;
            while (!rb.readValue(val)) {
                std::this_thread::yield(); // Buffer empty
            }
            ASSERT_EQ(val, i);
        }
    });

    if (producer.joinable()) {
        producer.join();
    }
    if (consumer.joinable()) {
        consumer.join();
    }

    EXPECT_TRUE(rb.isEmpty());
}

TEST_F(RingBufferTest, ThroughputBenchmarkMultiThreaded) {
    constexpr std::size_t capacity = 1 * MB; // 1MB buffer
    constexpr std::size_t dataSize = 1 * GB; // 1GB total transfer
    ASSERT_TRUE(rb.allocate(capacity));

    std::vector<uint8_t> batch(64 * KB, 0xFF); // 64KB chunks

    const auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&]() {
        size_t sent = 0;
        while (sent < dataSize) {
            sent += rb.write(std::span<const uint8_t>{batch}, true) * sizeof(uint8_t);
        }
    });

    std::thread consumer([&]() {
        std::vector<uint8_t> sink(64 * KB);
        size_t received = 0;
        while (received < dataSize) {
            received += rb.read(std::span{sink}, true) * sizeof(uint8_t);
        }
    });

    if (producer.joinable()) {
        producer.join();
    }
    if (consumer.joinable()) {
        consumer.join();
    }

    const auto end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> diff = end - start;

    const double gigabytes = static_cast<double>(dataSize) / GB;
    const double throughput = gigabytes / diff.count();

    RecordProperty("TotalGB", std::to_string(gigabytes));
    RecordProperty("GB_per_sec", std::to_string(throughput));

    std::cout << "[ BENCH    ] Transferred " << gigabytes << " GB in " << diff.count() << "sec (" << throughput
              << " GB/sec)" << std::endl;
}

TEST_F(RingBufferExceptionTest, ReadValueMaintainsStateOnException) {
    // 1. Prepare data
    ThrowingDefault item;
    item.value = 42;
    rb.writeValue(item);

    size_t availableBefore = rb.availableBytes();
    EXPECT_EQ(availableBefore, sizeof(ThrowingDefault));

    // 2. Enable "The Trap"
    ThrowingDefault::should_throw = true;

    // 3. Attempt to read. Your readValue<T>() calls T value{};
    // This will throw BEFORE the internal read() logic advances the pointer.
    EXPECT_THROW({ auto result = rb.readValue<ThrowingDefault>(); }, std::runtime_error);

    // 4. Verify Exception Safety: The read position must NOT have moved.
    ThrowingDefault::should_throw = false; // Disable so we can verify
    EXPECT_EQ(rb.availableBytes(), availableBefore);

    auto successfulRead = rb.readValue<ThrowingDefault>();
    ASSERT_TRUE(successfulRead.has_value());
    EXPECT_EQ(successfulRead->value, 42);
}

TEST_F(RingBufferExceptionTest, PeekValueMaintainsStateOnException) {
    ThrowingDefault item;
    item.value = 99;
    rb.writeValue(item);

    ThrowingDefault::should_throw = true;

    // peekValue() also default-constructs the return object
    EXPECT_THROW({ (void)rb.peekValue<ThrowingDefault>(); }, std::runtime_error);

    // Buffer should still contain the data
    EXPECT_EQ(rb.availableBytes(), sizeof(ThrowingDefault));
}

using namespace std::chrono_literals;

/**
 * Stress test for RingBuffer.
 * Logic: Producer writes a continuous stream of uint64_t values.
 * Consumer reads them and ensures the sequence is never broken.
 */
TEST_P(RingBufferStressTest, ProducerConsumerThroughput) {
    const auto& [bufferCapacity, duration] = GetParam();

    ASSERT_TRUE(rb.allocate(bufferCapacity));

    std::atomic<bool> keepRunning{true};
    std::atomic<uint64_t> totalBytesProcessed{0};

    // --- Producer Thread ---
    std::thread producer([&]() {
        uint64_t counter = 0;
        std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(1, 128);

        while (keepRunning.load(std::memory_order_relaxed)) {
            std::size_t itemsToWrite = dist(gen);
            std::vector<uint64_t> data(itemsToWrite);
            for (auto& val : data) {
                val = counter++;
            }

            // Using the template span-based write
            std::size_t written = rb.write<uint64_t>(std::span<const uint64_t>{data}, true);

            if (written == 0) {
                std::this_thread::yield();
                counter -= itemsToWrite;
            } else {
                // Adjust counter if write was partial
                counter -= (itemsToWrite - written);
                totalBytesProcessed.fetch_add(written * sizeof(uint64_t), std::memory_order_relaxed);
            }
        }
    });

    // --- Consumer Thread ---
    std::thread consumer([&]() {
        uint64_t expectedValue = 0;
        std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<std::size_t> dist(1, 128);

        while (keepRunning.load(std::memory_order_relaxed) || !rb.isEmpty()) {
            std::size_t itemsToRead = dist(gen);
            std::vector<uint64_t> readBuffer(itemsToRead);

            std::size_t readCount = rb.read<uint64_t>(std::span<uint64_t>(readBuffer), true);

            for (std::size_t i = 0; i < readCount; ++i) {
                if (readBuffer[i] != expectedValue) {
                    FAIL() << "Data Corruption! Expected: " << expectedValue << ", got: " << readBuffer[i];
                }
                expectedValue++;
            }

            if (readCount == 0) {
                std::this_thread::yield();
            }
        }
    });

    // Run for duration
    std::this_thread::sleep_for(duration);

    keepRunning = false;
    if (producer.joinable()) {
        producer.join();
    }
    if (consumer.joinable()) {
        consumer.join();
    }

    // Report performance
    double totalMB = totalBytesProcessed.load() / MB;
    double throughput = totalMB / duration.count();

    RecordProperty("TotalMB", std::to_string(totalMB));
    RecordProperty("MB_per_sec", std::to_string(throughput));

    SUCCEED();
}

TEST_P(RingBufferStressTest, MixedProducerConsumerThroughput) {
    const auto& [bufferCapacity, duration] = GetParam();
    ASSERT_TRUE(rb.allocate(bufferCapacity));

    std::atomic<bool> keepRunning{true};
    std::atomic<uint64_t> totalBytesProcessed{0};

    // --- Producer: Variadic Multi-Value Write ---
    std::thread producer([&]() {
        uint32_t seq = 0;
        while (keepRunning.load(std::memory_order_relaxed)) {
            PacketHeader header{seq, 0xAB, 1.234};
            uint64_t payload = static_cast<uint64_t>(seq) * 2;

            // Stress the writeValues variadic template
            if (rb.writeValues(header, payload)) {
                seq++;
                totalBytesProcessed.fetch_add(sizeof header + sizeof payload, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    });

    // --- Consumer: Zero-Copy Vector API ---
    std::thread consumer([&]() {
        uint32_t expectedSeq = 0;
        while (keepRunning.load(std::memory_order_relaxed) || !rb.isEmpty()) {
            // Use the Vector API to look at data without copying to a temp buffer first
            auto [front, back] = rb.readVector();
            size_t totalAvailable = front.size() + back.size();
            size_t packetSize = sizeof(PacketHeader) + sizeof(uint64_t);

            if (totalAvailable < packetSize) {
                std::this_thread::yield();
                continue;
            }

            // Verify logic via readValues to exercise the internal cursor/memcpy
            PacketHeader h;
            uint64_t p;
            if (rb.readValues(h, p)) {
                if (h.sequence != expectedSeq || p != (uint64_t)expectedSeq * 2) {
                    FAIL() << "Data Corruption! Expected: " << expectedSeq << ", got: " << h.sequence;
                }
                expectedSeq++;
            }
        }
    });

    // Run for duration
    std::this_thread::sleep_for(duration);

    keepRunning = false;
    if (producer.joinable()) {
        producer.join();
    }
    if (consumer.joinable()) {
        consumer.join();
    }

    // Report performance
    double totalMB = totalBytesProcessed.load() / MB;
    double throughput = totalMB / duration.count();

    RecordProperty("TotalMB", std::to_string(totalMB));
    RecordProperty("MB_per_sec", std::to_string(throughput));

    SUCCEED();
}

// Instantiate the test with different configurations
INSTANTIATE_TEST_SUITE_P(VariedCapacities, RingBufferStressTest,
                         ::testing::Values(StressParams{1 * KB, 2s}, // Small: Stress-test wrap-around logic
                                           StressParams{1 * MB, 5s}, // Large: Stress-test memory throughput
                                           StressParams{64 * KB, 3s} // Medium
                                           ),
                         RingBufferStressTest::ParamNameGenerator);
