//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

#include "mpsc/RingBuffer.hpp"

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

class RingBufferTest : public ::testing::Test {
  protected:
    mpsc::RingBuffer<1024> rb;
};

} // namespace

TEST_F(RingBufferTest, Empty) {
    EXPECT_EQ(rb.slotCount(), 0);
    EXPECT_EQ(rb.emptySlots(), 0);
    EXPECT_EQ(rb.occupiedSlots(), 0);

    uint8_t d[1024];
    std::size_t sz;
    EXPECT_EQ(rb.read(d, 1024, sz), false);
    EXPECT_EQ(sz, 0);
    EXPECT_EQ(rb.write(d, 1024), false);
}
