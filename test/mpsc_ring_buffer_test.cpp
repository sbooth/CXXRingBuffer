//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

#include "mpsc/RingBuffer.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

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

    std::array<unsigned char, 1024> a;
    std::size_t sz;
    EXPECT_EQ(rb.read(a, sz), false);
    EXPECT_EQ(sz, 0);
    EXPECT_EQ(rb.write(a), false);
}
