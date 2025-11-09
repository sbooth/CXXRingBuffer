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

#import "SFBRingBuffer.hpp"

SFB::RingBuffer::RingBuffer(uint32_t size)
{
	if(size < 2 || size > 0x80000000)
		throw std::invalid_argument("capacity out of range");
	if(!Allocate(size))
		throw std::bad_alloc();
}

SFB::RingBuffer::RingBuffer(RingBuffer&& other) noexcept
: buffer_{other.buffer_}, capacity_{other.capacity_}, capacityMask_{other.capacityMask_}, writePosition_{other.writePosition_.load(std::memory_order_acquire)}, readPosition_{other.readPosition_.load(std::memory_order_acquire)}
{
	other.buffer_ = nullptr;

	other.capacity_ = 0;
	other.capacityMask_ = 0;

	other.writePosition_ = 0;
	other.readPosition_ = 0;
}

SFB::RingBuffer& SFB::RingBuffer::operator=(RingBuffer&& other) noexcept
{
	if(this == &other)
		return *this;

	std::free(buffer_);

	buffer_ = other.buffer_;

	capacity_ = other.capacity_;
	capacityMask_ = other.capacityMask_;

	writePosition_.store(other.writePosition_.load(std::memory_order_acquire), std::memory_order_release);
	readPosition_.store(other.readPosition_.load(std::memory_order_acquire), std::memory_order_release);

	other.buffer_ = nullptr;

	other.capacity_ = 0;
	other.capacityMask_ = 0;

	other.writePosition_ = 0;
	other.readPosition_ = 0;

	return *this;
}

SFB::RingBuffer::~RingBuffer() noexcept
{
	std::free(buffer_);
}

#pragma mark Buffer Management

bool SFB::RingBuffer::Allocate(uint32_t size) noexcept
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

void SFB::RingBuffer::Deallocate() noexcept
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

void SFB::RingBuffer::Reset() noexcept
{
	writePosition_ = 0;
	readPosition_ = 0;
}

#pragma mark Buffer Information

uint32_t SFB::RingBuffer::CapacityBytes() const noexcept
{
	if(capacity_ == 0)
		return 0;
	return capacity_ - 1;
}

uint32_t SFB::RingBuffer::BytesAvailableToRead() const noexcept
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

uint32_t SFB::RingBuffer::BytesAvailableToWrite() const noexcept
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

#pragma mark Reading and Writing Data

uint32_t SFB::RingBuffer::Read(void * const destination, uint32_t count, bool allowPartial) noexcept
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

uint32_t SFB::RingBuffer::Peek(void * const destination, uint32_t count, bool allowPartial) const noexcept
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

uint32_t SFB::RingBuffer::Write(const void * const source, uint32_t count, bool allowPartial) noexcept
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

#pragma mark Advanced Reading and Writing

void SFB::RingBuffer::AdvanceReadPosition(uint32_t count) noexcept
{
	readPosition_.store((readPosition_.load(std::memory_order_acquire) + count) & capacityMask_, std::memory_order_release);
}

void SFB::RingBuffer::AdvanceWritePosition(uint32_t count) noexcept
{
	writePosition_.store((writePosition_.load(std::memory_order_acquire) + count) & capacityMask_, std::memory_order_release);
}

const SFB::RingBuffer::ReadBufferPair SFB::RingBuffer::GetReadVector() const noexcept
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

const SFB::RingBuffer::WriteBufferPair SFB::RingBuffer::GetWriteVector() const noexcept
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
