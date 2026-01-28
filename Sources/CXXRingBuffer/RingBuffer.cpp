//
// SPDX-FileCopyrightText: 2014 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

#include "ring/RingBuffer.hpp"

#include <bit>
#include <cstdlib>
#include <new>
#include <stdexcept>

// MARK: Construction and Destruction

ring::RingBuffer::RingBuffer(SizeType minCapacity) {
    if (minCapacity < RingBuffer::minCapacity || minCapacity > RingBuffer::maxCapacity) [[unlikely]] {
        throw std::invalid_argument("capacity out of range");
    }
    if (!allocate(minCapacity)) [[unlikely]] {
        throw std::bad_alloc();
    }
}

ring::RingBuffer::RingBuffer(RingBuffer &&other) noexcept
    : buffer_{std::exchange(other.buffer_, nullptr)}, capacity_{std::exchange(other.capacity_, 0)},
      capacityMask_{std::exchange(other.capacityMask_, 0)},
      writePosition_{other.writePosition_.exchange(0, std::memory_order_relaxed)},
      readPosition_{other.readPosition_.exchange(0, std::memory_order_relaxed)} {}

auto ring::RingBuffer::operator=(RingBuffer &&other) noexcept -> RingBuffer & {
    if (this != &other) [[likely]] {
        std::free(buffer_);

        buffer_ = std::exchange(other.buffer_, nullptr);
        capacity_ = std::exchange(other.capacity_, 0);
        capacityMask_ = std::exchange(other.capacityMask_, 0);

        writePosition_.store(other.writePosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
        readPosition_.store(other.readPosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

ring::RingBuffer::~RingBuffer() noexcept { std::free(buffer_); }

// MARK: Buffer Management

bool ring::RingBuffer::allocate(SizeType minCapacity) noexcept {
    if (minCapacity < RingBuffer::minCapacity || minCapacity > RingBuffer::maxCapacity) [[unlikely]] {
        return false;
    }

    deallocate();

    const auto capacity = std::bit_ceil(minCapacity);
    buffer_ = std::malloc(capacity);

    if (buffer_ == nullptr) [[unlikely]] {
        return false;
    }

    capacity_ = capacity;
    capacityMask_ = capacity - 1;

    writePosition_.store(0, std::memory_order_relaxed);
    readPosition_.store(0, std::memory_order_relaxed);

    return true;
}

void ring::RingBuffer::deallocate() noexcept {
    if (buffer_ != nullptr) [[likely]] {
        std::free(buffer_);

        buffer_ = nullptr;
        capacity_ = 0;
        capacityMask_ = 0;

        writePosition_.store(0, std::memory_order_relaxed);
        readPosition_.store(0, std::memory_order_relaxed);
    }
}
