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

CXXRingBuffer::RingBuffer::RingBuffer(uint32_t size)
{
	if(size < 2 || size > 0x80000000)
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

bool CXXRingBuffer::RingBuffer::Allocate(uint32_t size) noexcept
{
	if(size < 2 || size > 0x80000000)
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

uint32_t CXXRingBuffer::RingBuffer::Capacity() const noexcept
{
	if(capacity_ == 0)
		return 0;
	return capacity_ - 1;
}

uint32_t CXXRingBuffer::RingBuffer::AvailableReadCount() const noexcept
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

uint32_t CXXRingBuffer::RingBuffer::AvailableWriteCount() const noexcept
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

// MARK: Reading and Writing Data

uint32_t CXXRingBuffer::RingBuffer::Read(void * const destination, uint32_t count, bool allowPartial) noexcept
{
	if(!destination || count == 0 || capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = writePosition - readPosition;
	else
		bytesAvailable = (writePosition - readPosition + capacity_) & capacityMask_;

	if(bytesAvailable == 0 || (bytesAvailable < count && !allowPartial))
		return 0;

	const auto bytesToRead = std::min(bytesAvailable, count);
	if(readPosition + bytesToRead > capacity_) {
		const auto bytesAfterReadPointer = capacity_ - readPosition;
		std::memcpy(destination,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition),
					bytesAfterReadPointer);
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(destination) + bytesAfterReadPointer),
					buffer_,
					bytesToRead - bytesAfterReadPointer);
	}
	else
		std::memcpy(destination, reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition), bytesToRead);

	readPosition_.store((readPosition + bytesToRead) & capacityMask_, std::memory_order_release);

	return bytesToRead;
}

uint32_t CXXRingBuffer::RingBuffer::Peek(void * const destination, uint32_t count, bool allowPartial) const noexcept
{
	if(!destination || count == 0 || capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = writePosition - readPosition;
	else
		bytesAvailable = (writePosition - readPosition + capacity_) & capacityMask_;

	if(bytesAvailable == 0 || (bytesAvailable < count && !allowPartial))
		return 0;

	const auto bytesToRead = std::min(bytesAvailable, count);
	if(readPosition + bytesToRead > capacity_) {
		auto bytesAfterReadPointer = capacity_ - readPosition;
		std::memcpy(destination,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition),
					bytesAfterReadPointer);
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(destination) + bytesAfterReadPointer),
					buffer_,
					bytesToRead - bytesAfterReadPointer);
	}
	else
		std::memcpy(destination, reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(buffer_) + readPosition), bytesToRead);

	return bytesToRead;
}

uint32_t CXXRingBuffer::RingBuffer::Write(const void * const source, uint32_t count, bool allowPartial) noexcept
{
	if(!source || count == 0 || capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = ((readPosition - writePosition + capacity_) & capacityMask_) - 1;
	else if(writePosition < readPosition)
		bytesAvailable = (readPosition - writePosition) - 1;
	else
		bytesAvailable = capacity_ - 1;

	if(bytesAvailable == 0 || (bytesAvailable < count && !allowPartial))
		return 0;

	const auto bytesToWrite = std::min(bytesAvailable, count);
	if(writePosition + bytesToWrite > capacity_) {
		auto bytesAfterWritePointer = capacity_ - writePosition;
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buffer_) + writePosition),
					source,
					bytesAfterWritePointer);
		std::memcpy(buffer_,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(source) + bytesAfterWritePointer),
					bytesToWrite - bytesAfterWritePointer);
	}
	else
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buffer_) + writePosition), source, bytesToWrite);

	writePosition_.store((writePosition + bytesToWrite) & capacityMask_, std::memory_order_release);

	return bytesToWrite;
}

// MARK: Advanced Reading and Writing

void CXXRingBuffer::RingBuffer::AdvanceReadPosition(uint32_t count) noexcept
{
	readPosition_.store((readPosition_.load(std::memory_order_acquire) + count) & capacityMask_, std::memory_order_release);
}

void CXXRingBuffer::RingBuffer::AdvanceWritePosition(uint32_t count) noexcept
{
	writePosition_.store((writePosition_.load(std::memory_order_acquire) + count) & capacityMask_, std::memory_order_release);
}

const CXXRingBuffer::RingBuffer::ReadBufferPair CXXRingBuffer::RingBuffer::GetReadVector() const noexcept
{
	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
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

const CXXRingBuffer::RingBuffer::WriteBufferPair CXXRingBuffer::RingBuffer::GetWriteVector() const noexcept
{
	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
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
