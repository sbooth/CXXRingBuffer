//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

#include "mpsc/RingBuffer.hpp"

#include <stdexcept>
#include <utility>

// MARK: Construction and Destruction

template <std::size_t N>
    requires mpsc::ValidPowerOfTwo<N>
mpsc::RingBuffer<N>::RingBuffer(SizeType minSlots) {
    if (minSlots < RingBuffer::minSlots || minSlots > RingBuffer::maxSlots) [[unlikely]] {
        throw std::invalid_argument("slot count out of range");
    }
    if (!allocate(minSlots)) [[unlikely]] {
        throw std::bad_alloc();
    }
}

template <std::size_t N>
    requires mpsc::ValidPowerOfTwo<N>
mpsc::RingBuffer<N>::RingBuffer(RingBuffer &&other) noexcept
    : slots_{std::exchange(other.slots_, nullptr)}, slotCount_{std::exchange(other.slotCount_, 0)},
      slotCountMask_{std::exchange(other.slotCountMask_, 0)},
      writePosition_{other.writePosition_.exchange(0, std::memory_order_relaxed)},
      readPosition_{other.readPosition_.exchange(0, std::memory_order_relaxed)} {}

template <std::size_t N>
    requires mpsc::ValidPowerOfTwo<N>
auto mpsc::RingBuffer<N>::operator=(RingBuffer &&other) noexcept -> RingBuffer & {
    if (this != &other) [[likely]] {
        slots_ = std::exchange(other.slots_, nullptr);
        slotCount_ = std::exchange(other.slotCount_, 0);
        slotCountMask_ = std::exchange(other.slotCountMask_, 0);

        writePosition_.store(other.writePosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
        readPosition_.store(other.readPosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

// MARK: Buffer Management

template <std::size_t N>
    requires mpsc::ValidPowerOfTwo<N>
bool mpsc::RingBuffer<N>::allocate(SizeType minSlots) noexcept {
    if (minSlots < RingBuffer::minSlots || minSlots > RingBuffer::maxSlots) [[unlikely]] {
        return false;
    }

    deallocate();

    const auto slotCount = std::bit_ceil(minSlots);

    try {
        slots_ = std::make_unique_for_overwrite<Slot[]>(slotCount);
    } catch (const std::exception &e) {
        return false;
    }

    for (SizeType i = 0; i < slotCount; ++i) {
        slots_[i].sequence_ = i;
        slots_[i].dataSize_ = 0;
    }

    slotCount_ = slotCount;
    slotCountMask_ = slotCount - 1;

    writePosition_.store(0, std::memory_order_relaxed);
    readPosition_.store(0, std::memory_order_relaxed);

    return true;
}

template <std::size_t N>
    requires mpsc::ValidPowerOfTwo<N>
void mpsc::RingBuffer<N>::deallocate() noexcept {
    if (!slots_) [[likely]] {
        slots_.reset();

        slotCount_ = 0;
        slotCountMask_ = 0;

        writePosition_.store(0, std::memory_order_relaxed);
        readPosition_.store(0, std::memory_order_relaxed);
    }
}
