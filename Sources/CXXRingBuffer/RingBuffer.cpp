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
	if(capacity_ == 0)
		return 0;
	return capacity_ - 1;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::FreeSpace() const noexcept
{
	if(capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	if(writePosition > readPosition)
		return ((readPosition - writePosition + capacity_) & capacityMask_) - 1;
	else if(writePosition < readPosition)
		return (readPosition - writePosition) - 1;
	else
		return capacity_ - 1;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::AvailableBytes() const noexcept
{
	if(capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	if(writePosition > readPosition)
		return writePosition - readPosition;
	else
		return (writePosition - readPosition + capacity_) & capacityMask_;
}

// MARK: Writing and Reading Data

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Write(const void * const src, size_type size, size_type count, bool allowPartial) noexcept
{
	if(!src || size == 0 || count == 0 || capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	size_type freeSpace;
	if(writePosition > readPosition)
		freeSpace = ((readPosition - writePosition + capacity_) & capacityMask_) - 1;
	else if(writePosition < readPosition)
		freeSpace = (readPosition - writePosition) - 1;
	else
		freeSpace = capacity_ - 1;

	if(freeSpace == 0)
		return 0;

	const auto freeSlots = freeSpace / size;
	if(freeSlots < count && !allowPartial)
		return 0;

	const auto itemCountToWrite = std::min(freeSlots, count);
	const auto byteCountToWrite = itemCountToWrite * size;
	if(writePosition + byteCountToWrite > capacity_) {
		const auto byteCountAfterWritePointer = capacity_ - writePosition;
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buffer_) + writePosition),
					src,
					byteCountAfterWritePointer);
		std::memcpy(buffer_,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(src) + byteCountAfterWritePointer),
					byteCountToWrite - byteCountAfterWritePointer);
	} else
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buffer_) + writePosition),
					src,
					byteCountToWrite);

	writePosition_.store((writePosition + byteCountToWrite) & capacityMask_, std::memory_order_release);

	return itemCountToWrite;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Read(void * const dst, size_type size, size_type count, bool allowPartial) noexcept
{
	if(!dst || size == 0 || count == 0 || capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	size_type availableBytes;
	if(writePosition > readPosition)
		availableBytes = writePosition - readPosition;
	else
		availableBytes = (writePosition - readPosition + capacity_) & capacityMask_;

	if(availableBytes == 0)
		return 0;

	const auto availableItems = availableBytes / size;
	if(availableItems < count && !allowPartial)
		return 0;

	const auto itemCountToRead = std::min(availableItems, count);
	const auto byteCountToRead = itemCountToRead * size;
	if(readPosition + byteCountToRead > capacity_) {
		const auto byteCountAfterReadPointer = capacity_ - readPosition;
		std::memcpy(dst,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition),
					byteCountAfterReadPointer);
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(dst) + byteCountAfterReadPointer),
					buffer_,
					byteCountToRead - byteCountAfterReadPointer);
	} else
		std::memcpy(dst,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition),
					byteCountToRead);

	readPosition_.store((readPosition + byteCountToRead) & capacityMask_, std::memory_order_release);

	return itemCountToRead;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Peek(void * const dst, size_type size, size_type count, bool allowPartial) const noexcept
{
	if(!dst || size == 0 || count == 0 || capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	size_type availableBytes;
	if(writePosition > readPosition)
		availableBytes = writePosition - readPosition;
	else
		availableBytes = (writePosition - readPosition + capacity_) & capacityMask_;

	if(availableBytes == 0)
		return 0;

	const auto availableItems = availableBytes / size;
	if(availableItems < count && !allowPartial)
		return 0;

	const auto itemCountToRead = std::min(availableItems, count);
	const auto byteCountToRead = itemCountToRead * size;
	if(readPosition + byteCountToRead > capacity_) {
		auto bytesAfterReadPointer = capacity_ - readPosition;
		std::memcpy(dst,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition),
					bytesAfterReadPointer);
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(dst) + bytesAfterReadPointer),
					buffer_,
					byteCountToRead - bytesAfterReadPointer);
	} else
		std::memcpy(dst,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition),
					byteCountToRead);

	return itemCountToRead;
}

// MARK: Advanced Writing and Reading

void CXXRingBuffer::RingBuffer::AdvanceWritePosition(size_type count) noexcept
{
	writePosition_.store((writePosition_.load(std::memory_order_acquire) + count) & capacityMask_, std::memory_order_release);
}

void CXXRingBuffer::RingBuffer::AdvanceReadPosition(size_type count) noexcept
{
	readPosition_.store((readPosition_.load(std::memory_order_acquire) + count) & capacityMask_, std::memory_order_release);
}

const CXXRingBuffer::RingBuffer::WriteBufferPair CXXRingBuffer::RingBuffer::GetWriteVector() const noexcept
{
	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	size_type bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = ((readPosition - writePosition + capacity_) & capacityMask_) - 1;
	else if(writePosition < readPosition)
		bytesAvailable = (readPosition - writePosition) - 1;
	else
		bytesAvailable = capacity_ - 1;

	const auto endOfWrite = writePosition + bytesAvailable;

	if(endOfWrite > capacity_)
		return {
			{ reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buffer_) + writePosition), capacity_ - writePosition },
			{ buffer_, endOfWrite & capacity_ }
		};
	else
		return {
			{ reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buffer_) + writePosition), bytesAvailable },
			{}
		};
}

const CXXRingBuffer::RingBuffer::ReadBufferPair CXXRingBuffer::RingBuffer::GetReadVector() const noexcept
{
	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	size_type bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = writePosition - readPosition;
	else
		bytesAvailable = (writePosition - readPosition + capacity_) & capacityMask_;

	const auto endOfRead = readPosition + bytesAvailable;

	if(endOfRead > capacity_)
		return {
			{ reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition), capacity_ - readPosition },
			{ buffer_, endOfRead & capacity_ }
		};
	else
		return {
			{ reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition), bytesAvailable },
			{}
		};
}
