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

	buffer_ = static_cast<std::byte *>(std::malloc(size));
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

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Write(const std::byte * const source, size_type count, bool allowPartial) noexcept
{
	if(!source || count == 0 || capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	size_type bytesAvailable;
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
		const auto bytesAfterWritePointer = capacity_ - writePosition;
		std::memcpy(buffer_ + writePosition,
					source,
					bytesAfterWritePointer);
		std::memcpy(buffer_,
					source + bytesAfterWritePointer,
					bytesToWrite - bytesAfterWritePointer);
	} else
		std::memcpy(buffer_ + writePosition,
					source,
					bytesToWrite);

	writePosition_.store((writePosition + bytesToWrite) & capacityMask_, std::memory_order_release);

	return bytesToWrite;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Read(std::byte * const destination, size_type count, bool allowPartial) noexcept
{
	if(!destination || count == 0 || capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	size_type bytesAvailable;
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
					buffer_ + readPosition,
					bytesAfterReadPointer);
		std::memcpy(destination + bytesAfterReadPointer,
					buffer_,
					bytesToRead - bytesAfterReadPointer);
	} else
		std::memcpy(destination,
					buffer_ + readPosition,
					bytesToRead);

	readPosition_.store((readPosition + bytesToRead) & capacityMask_, std::memory_order_release);

	return bytesToRead;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Peek(std::byte * const destination, size_type count, bool allowPartial) const noexcept
{
	if(!destination || count == 0 || capacity_ == 0)
		return 0;

	const auto writePosition = writePosition_.load(std::memory_order_acquire);
	const auto readPosition = readPosition_.load(std::memory_order_acquire);

	size_type bytesAvailable;
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
					buffer_ + readPosition,
					bytesAfterReadPointer);
		std::memcpy(destination + bytesAfterReadPointer,
					buffer_,
					bytesToRead - bytesAfterReadPointer);
	} else
		std::memcpy(destination,
					buffer_ + readPosition,
					bytesToRead);

	return bytesToRead;
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
			{ buffer_ + writePosition, capacity_ - writePosition },
			{ buffer_, endOfWrite & capacity_ }
		};
	else
		return {
			{ buffer_ + writePosition, bytesAvailable },
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
			{ buffer_ + readPosition, capacity_ - readPosition },
			{ buffer_, endOfRead & capacity_ }
		};
	else
		return {
			{ buffer_ + readPosition, bytesAvailable },
			{}
		};
}
