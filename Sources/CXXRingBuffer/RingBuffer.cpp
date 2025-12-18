//
// Copyright © 2014-2025 Stephen F. Booth
// Part of https://github.com/sbooth/CXXRingBuffer
// MIT license
//

#import <bit>
#import <cassert>
#import <cstdlib>
#import <limits>
#import <new>
#import <stdexcept>

#import "RingBuffer.hpp"

// MARK: Creation and Destruction

CXXRingBuffer::RingBuffer::RingBuffer(size_type size)
{
	if(size < min_buffer_size || size > max_buffer_size)
		throw std::invalid_argument("capacity out of range");
	if(!Allocate(size))
		throw std::bad_alloc();
}

CXXRingBuffer::RingBuffer::RingBuffer(RingBuffer&& other) noexcept
: buffer_{std::exchange(other.buffer_, nullptr)}, capacity_{std::exchange(other.capacity_, 0)}, capacityMask_{std::exchange(other.capacityMask_, 0)}, writePosition_{std::atomic_exchange(&other.writePosition_, 0)}, readPosition_{std::atomic_exchange(&other.readPosition_, 0)}
{}

CXXRingBuffer::RingBuffer& CXXRingBuffer::RingBuffer::operator=(RingBuffer&& other) noexcept
{
	if(this != &other) {
		std::free(buffer_);
		buffer_ = std::exchange(other.buffer_, nullptr);
		capacity_ = std::exchange(other.capacity_, 0);
		capacityMask_ = std::exchange(other.capacityMask_, 0);
		writePosition_ = std::atomic_exchange(&other.writePosition_, 0);
		readPosition_ = std::atomic_exchange(&other.readPosition_, 0);
	}
	return *this;
}

CXXRingBuffer::RingBuffer::~RingBuffer() noexcept
{
	std::free(buffer_);
}

// MARK: Buffer Management

bool CXXRingBuffer::RingBuffer::Allocate(size_type size) noexcept
{
	if(size < min_buffer_size || size > max_buffer_size)
		return false;

	Deallocate();

	size = std::bit_ceil(size);

	buffer_ = std::malloc(size);
	if(!buffer_)
		return false;

	capacity_ = size;
	capacityMask_ = size - 1;

	writePosition_ = 0;
	readPosition_ = 0;

	return true;
}

void CXXRingBuffer::RingBuffer::Deallocate() noexcept
{
	if(buffer_) {
		std::free(buffer_);
		buffer_ = nullptr;

		capacity_ = 0;
		capacityMask_ = 0;

		writePosition_ = 0;
		readPosition_ = 0;
	}
}

void CXXRingBuffer::RingBuffer::Reset() noexcept
{
	writePosition_ = 0;
	readPosition_ = 0;
}

// MARK: Buffer Information

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Capacity() const noexcept
{
	if(capacity_ == 0) [[unlikely]]
		return 0;
	return capacity_ - 1;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::FreeSpace() const noexcept
{
	if(capacity_ == 0) [[unlikely]]
		return 0;
	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);
	return (readPosition - writePosition - 1) & capacityMask_;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::AvailableBytes() const noexcept
{
	if(capacity_ == 0) [[unlikely]]
		return 0;
	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);
	return (writePosition - readPosition) & capacityMask_;
}

// MARK: Writing and Reading Data

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Write(const void * const ptr, size_type itemSize, size_type itemCount, bool allowPartial) noexcept
{
	if(!ptr || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
		return 0;

	const auto writePos = writePosition_.load(std::memory_order_relaxed);
	const auto readPos = readPosition_.load(std::memory_order_acquire);

	const auto freeBytes = (readPos - writePos - 1) & capacityMask_;
	const auto freeSlots = freeBytes / itemSize;
	if(freeSlots == 0 || (freeSlots < itemCount && !allowPartial)) [[unlikely]]
		return 0;

	const auto itemsToWrite = std::min(freeSlots, itemCount);
	const auto bytesToWrite = itemsToWrite * itemSize;

	auto dst = static_cast<uint8_t *>(buffer_);
	const auto src = static_cast<const uint8_t *>(ptr);

	const auto spaceToEnd = capacity_ - writePos;
	if(bytesToWrite <= spaceToEnd) [[likely]]
		std::memcpy(dst + writePos, src, bytesToWrite);
	else [[unlikely]] {
		std::memcpy(dst + writePos, src, spaceToEnd);
		std::memcpy(dst, src + spaceToEnd, bytesToWrite - spaceToEnd);
	}

	writePosition_.store((writePos + bytesToWrite) & capacityMask_, std::memory_order_release);

	return itemsToWrite;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Read(void * const ptr, size_type itemSize, size_type itemCount, bool allowPartial) noexcept
{
	if(!ptr || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
		return 0;

	const auto writePos = writePosition_.load(std::memory_order_acquire);
	const auto readPos = readPosition_.load(std::memory_order_relaxed);

	const auto availableBytes = (writePos - readPos) & capacityMask_;
	const auto availableItems = availableBytes / itemSize;
	if(availableItems == 0 || (availableItems < itemCount && !allowPartial)) [[unlikely]]
		return 0;

	const auto itemsToRead = std::min(availableItems, itemCount);
	const auto bytesToRead = itemsToRead * itemSize;

	auto dst = static_cast<uint8_t *>(ptr);
	const auto src = static_cast<const uint8_t *>(buffer_);

	const auto spaceToEnd = capacity_ - readPos;
	if(bytesToRead <= spaceToEnd) [[likely]]
		std::memcpy(dst, src + readPos, bytesToRead);
	else [[unlikely]] {
		std::memcpy(dst, src + readPos, spaceToEnd);
		std::memcpy(dst + spaceToEnd, src, bytesToRead - spaceToEnd);
	}

	readPosition_.store((readPos + bytesToRead) & capacityMask_, std::memory_order_release);

	return itemsToRead;
}

bool CXXRingBuffer::RingBuffer::Peek(void * const ptr, size_type itemSize, size_type itemCount) const noexcept
{
	if(!ptr || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
		return 0;

	const auto writePos = writePosition_.load(std::memory_order_acquire);
	const auto readPos = readPosition_.load(std::memory_order_relaxed);

	const auto availableBytes = (writePos - readPos) & capacityMask_;
	const auto availableItems = availableBytes / itemSize;
	if(availableItems < itemCount) [[unlikely]]
		return 0;

	const auto bytesToPeek = itemCount * itemSize;

	auto dst = static_cast<uint8_t *>(ptr);
	const auto src = static_cast<const uint8_t *>(buffer_);

	const auto spaceToEnd = capacity_ - readPos;
	if(bytesToPeek <= spaceToEnd) [[likely]]
		std::memcpy(dst, src + readPos, bytesToPeek);
	else [[unlikely]] {
		std::memcpy(dst, src + readPos, spaceToEnd);
		std::memcpy(dst + spaceToEnd, src, bytesToPeek - spaceToEnd);
	}

	return itemCount;
}

// MARK: Advanced Writing and Reading

void CXXRingBuffer::RingBuffer::AdvanceWritePosition(size_type count) noexcept
{
	const auto writePos = writePosition_.load(std::memory_order_relaxed);
	writePosition_.store((writePos + count) & capacityMask_, std::memory_order_release);
}

void CXXRingBuffer::RingBuffer::AdvanceReadPosition(size_type count) noexcept
{
	const auto readPos = readPosition_.load(std::memory_order_relaxed);
	readPosition_.store((readPos + count) & capacityMask_, std::memory_order_release);
}

CXXRingBuffer::RingBuffer::write_vector CXXRingBuffer::RingBuffer::GetWriteVector() const noexcept
{
	const auto writePos = writePosition_.load(std::memory_order_relaxed);
	const auto readPos = readPosition_.load(std::memory_order_acquire);

	const auto freeBytes = (readPos - writePos - 1) & capacityMask_;
	if(freeBytes == 0) [[unlikely]]
		return {};

	auto dst = static_cast<uint8_t *>(buffer_);

	const auto spaceToEnd = capacity_ - writePos;
	if(freeBytes <= spaceToEnd) [[likely]]
		return {
			{ dst + writePos, freeBytes },
			{}
		};
	else [[unlikely]]
		return {
			{ dst + writePos, spaceToEnd },
			{ dst, freeBytes - spaceToEnd }
		};
}

CXXRingBuffer::RingBuffer::read_vector CXXRingBuffer::RingBuffer::GetReadVector() const noexcept
{
	const auto writePos = writePosition_.load(std::memory_order_acquire);
	const auto readPos = readPosition_.load(std::memory_order_relaxed);

	const auto availableBytes = (writePos - readPos) & capacityMask_;
	if(availableBytes == 0) [[unlikely]]
		return {};

	const auto src = static_cast<const uint8_t *>(buffer_);

	const auto spaceToEnd = capacity_ - readPos;
	if(availableBytes <= spaceToEnd) [[likely]]
		return {
			{ src + readPos, availableBytes },
			{}
		};
	else [[unlikely]]
		return {
			{ src + readPos, spaceToEnd },
			{ src, availableBytes - spaceToEnd }
		};
}
