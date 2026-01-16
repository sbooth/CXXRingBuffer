//
// SPDX-FileCopyrightText: 2014 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

#import <bit>
#import <cstdlib>
#import <limits>
#import <new>
#import <stdexcept>

#import "RingBuffer.hpp"

// MARK: Creation and Destruction

CXXRingBuffer::RingBuffer::RingBuffer(size_type minCapacity)
{
	if(minCapacity < RingBuffer::minCapacity || minCapacity > RingBuffer::maxCapacity) [[unlikely]]
		throw std::invalid_argument("capacity out of range");
	if(!allocate(minCapacity)) [[unlikely]]
		throw std::bad_alloc();
}

CXXRingBuffer::RingBuffer::RingBuffer(RingBuffer&& other) noexcept
: buffer_{std::exchange(other.buffer_, nullptr)}, capacity_{std::exchange(other.capacity_, 0)}, capacityMask_{std::exchange(other.capacityMask_, 0)}, writePosition_{other.writePosition_.exchange(0, std::memory_order_relaxed)}, readPosition_{other.readPosition_.exchange(0, std::memory_order_relaxed)}
{}

CXXRingBuffer::RingBuffer& CXXRingBuffer::RingBuffer::operator=(RingBuffer&& other) noexcept
{
	if(this != &other) [[likely]] {
		std::free(buffer_);
		buffer_ = std::exchange(other.buffer_, nullptr);

		capacity_ = std::exchange(other.capacity_, 0);
		capacityMask_ = std::exchange(other.capacityMask_, 0);

		writePosition_.store(other.writePosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
		readPosition_.store(other.readPosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
	}
	return *this;
}

CXXRingBuffer::RingBuffer::~RingBuffer() noexcept
{
	std::free(buffer_);
}

// MARK: Buffer Management

bool CXXRingBuffer::RingBuffer::allocate(size_type minCapacity) noexcept
{
	if(minCapacity < RingBuffer::minCapacity || minCapacity > RingBuffer::maxCapacity) [[unlikely]]
		return false;

	deallocate();

	const auto capacity = std::bit_ceil(minCapacity);

#if false
	// Use aligned_alloc for cache-line alignment (64 bytes)
	// Note: capacity must be a multiple of alignment for aligned_alloc
	if(__builtin_available(macOS 10.15, iOS 13.0, tvOS 13.0, watchOS 6.0, *))
		buffer_ = std::aligned_alloc(64, capacity);
	else
		buffer_ = std::malloc(capacity);
#endif

	buffer_ = std::malloc(capacity);
	if(!buffer_) [[unlikely]]
		return false;

	capacity_ = capacity;
	capacityMask_ = capacity - 1;

	writePosition_.store(0, std::memory_order_relaxed);
	readPosition_.store(0, std::memory_order_relaxed);

	return true;
}

void CXXRingBuffer::RingBuffer::deallocate() noexcept
{
	if(buffer_) [[likely]] {
		std::free(buffer_);
		buffer_ = nullptr;

		capacity_ = 0;
		capacityMask_ = 0;

		writePosition_.store(0, std::memory_order_relaxed);
		readPosition_.store(0, std::memory_order_relaxed);
	}
}
