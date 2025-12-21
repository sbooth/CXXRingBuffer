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

CXXRingBuffer::RingBuffer::RingBuffer(size_type minCapacity)
{
	if(minCapacity < min_capacity || minCapacity > max_capacity) [[unlikely]]
		throw std::invalid_argument("capacity out of range");
	if(!Allocate(minCapacity)) [[unlikely]]
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

bool CXXRingBuffer::RingBuffer::Allocate(size_type minCapacity) noexcept
{
	if(minCapacity < min_capacity || minCapacity > max_capacity) [[unlikely]]
		return false;

	Deallocate();

	const auto capacity = std::bit_ceil(minCapacity);

#if false
	// Use aligned_alloc for cache-line alignment (64 bytes)
	// Note: size must be a multiple of alignment for aligned_alloc
	if(__builtin_available(macOS 10.15, iOS 13.0, tvOS 13.0, watchOS 6.0, *))
		buffer_ = std::aligned_alloc(64, size);
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

void CXXRingBuffer::RingBuffer::Deallocate() noexcept
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

void CXXRingBuffer::RingBuffer::Reset() noexcept
{
	writePosition_.store(0, std::memory_order_relaxed);
	readPosition_.store(0, std::memory_order_relaxed);
}

// MARK: Buffer Information

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Capacity() const noexcept
{
	return capacity_;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::FreeSpace() const noexcept
{
	const auto writePos = writePosition_.load(std::memory_order_relaxed);
	const auto readPos = readPosition_.load(std::memory_order_acquire);
	return capacity_ - (writePos - readPos);
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::AvailableBytes() const noexcept
{
	const auto writePos = writePosition_.load(std::memory_order_acquire);
	const auto readPos = readPosition_.load(std::memory_order_relaxed);
	return writePos - readPos;
}

bool CXXRingBuffer::RingBuffer::IsEmpty() const noexcept
{
	const auto writePos = writePosition_.load(std::memory_order_relaxed);
	const auto readPos = readPosition_.load(std::memory_order_relaxed);
	return writePos == readPos;
}

bool CXXRingBuffer::RingBuffer::IsFull() const noexcept
{
	const auto writePos = writePosition_.load(std::memory_order_relaxed);
	const auto readPos = readPosition_.load(std::memory_order_relaxed);
	return (writePos - readPos) == capacity_;
}

// MARK: Writing and Reading Data

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Write(const void * const ptr, size_type itemSize, size_type itemCount, bool allowPartial) noexcept
{
	if(!ptr || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
		return 0;

	const auto writePos = writePosition_.load(std::memory_order_relaxed);
	const auto readPos = readPosition_.load(std::memory_order_acquire);

	const auto bytesUsed = writePos - readPos;
	const auto bytesFree = capacity_ - bytesUsed;
	const auto slotsFree = bytesFree / itemSize;
	if(slotsFree == 0 || (slotsFree < itemCount && !allowPartial))
		return 0;

	const auto itemsToWrite = std::min(slotsFree, itemCount);
	const auto bytesToWrite = itemsToWrite * itemSize;

	auto dst = static_cast<uint8_t *>(buffer_);
	const auto src = static_cast<const uint8_t *>(ptr);

	const auto writeIndex = writePos & capacityMask_;
	const auto spaceToEnd = capacity_ - writeIndex;
	if(bytesToWrite <= spaceToEnd) [[likely]]
		std::memcpy(dst + writeIndex, src, bytesToWrite);
	else [[unlikely]] {
		std::memcpy(dst + writeIndex, src, spaceToEnd);
		std::memcpy(dst, src + spaceToEnd, bytesToWrite - spaceToEnd);
	}

	writePosition_.store(writePos + bytesToWrite, std::memory_order_release);

	return itemsToWrite;
}

CXXRingBuffer::RingBuffer::size_type CXXRingBuffer::RingBuffer::Read(void * const ptr, size_type itemSize, size_type itemCount, bool allowPartial) noexcept
{
	if(!ptr || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
		return 0;

	const auto writePos = writePosition_.load(std::memory_order_acquire);
	const auto readPos = readPosition_.load(std::memory_order_relaxed);

	const auto availableBytes = writePos - readPos;
	const auto availableItems = availableBytes / itemSize;
	if(availableItems == 0 || (availableItems < itemCount && !allowPartial))
		return 0;

	const auto itemsToRead = std::min(availableItems, itemCount);
	const auto bytesToRead = itemsToRead * itemSize;

	auto dst = static_cast<uint8_t *>(ptr);
	const auto src = static_cast<const uint8_t *>(buffer_);

	const auto readIndex = readPos & capacityMask_;
	const auto spaceToEnd = capacity_ - readIndex;
	if(bytesToRead <= spaceToEnd) [[likely]]
		std::memcpy(dst, src + readIndex, bytesToRead);
	else [[unlikely]] {
		std::memcpy(dst, src + readIndex, spaceToEnd);
		std::memcpy(dst + spaceToEnd, src, bytesToRead - spaceToEnd);
	}

	readPosition_.store(readPos + bytesToRead, std::memory_order_release);

	return itemsToRead;
}

bool CXXRingBuffer::RingBuffer::Peek(void * const ptr, size_type itemSize, size_type itemCount) const noexcept
{
	if(!ptr || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
		return false;

	const auto writePos = writePosition_.load(std::memory_order_acquire);
	const auto readPos = readPosition_.load(std::memory_order_relaxed);

	const auto availableBytes = writePos - readPos;
	const auto availableItems = availableBytes / itemSize;
	if(availableItems < itemCount)
		return false;

	const auto bytesToPeek = itemCount * itemSize;

	auto dst = static_cast<uint8_t *>(ptr);
	const auto src = static_cast<const uint8_t *>(buffer_);

	const auto readIndex = readPos & capacityMask_;
	const auto spaceToEnd = capacity_ - readIndex;
	if(bytesToPeek <= spaceToEnd) [[likely]]
		std::memcpy(dst, src + readIndex, bytesToPeek);
	else [[unlikely]] {
		std::memcpy(dst, src + readIndex, spaceToEnd);
		std::memcpy(dst + spaceToEnd, src, bytesToPeek - spaceToEnd);
	}

	return true;
}

// MARK: Advanced Writing and Reading

CXXRingBuffer::RingBuffer::write_vector CXXRingBuffer::RingBuffer::GetWriteVector() const noexcept
{
	const auto writePos = writePosition_.load(std::memory_order_relaxed);
	const auto readPos = readPosition_.load(std::memory_order_acquire);

	const auto usedBytes = writePos - readPos;
	const auto freeBytes = capacity_ - usedBytes;
	if(freeBytes == 0) [[unlikely]]
		return {};

	auto dst = static_cast<uint8_t *>(buffer_);

	const auto writeIndex = writePos & capacityMask_;
	const auto spaceToEnd = capacity_ - writeIndex;
	if(freeBytes <= spaceToEnd) [[likely]]
		return {{dst + writeIndex, freeBytes}, {}};
	else [[unlikely]]
		return {{dst + writeIndex, spaceToEnd}, {dst, freeBytes - spaceToEnd}};
}

CXXRingBuffer::RingBuffer::read_vector CXXRingBuffer::RingBuffer::GetReadVector() const noexcept
{
	const auto writePos = writePosition_.load(std::memory_order_acquire);
	const auto readPos = readPosition_.load(std::memory_order_relaxed);

	const auto availableBytes = writePos - readPos;
	if(availableBytes == 0) [[unlikely]]
		return {};

	const auto src = static_cast<const uint8_t *>(buffer_);

	const auto readIndex = readPos & capacityMask_;
	const auto spaceToEnd = capacity_ - readIndex;
	if(availableBytes <= spaceToEnd) [[likely]]
		return {{src + readIndex, availableBytes}, {}};
	else [[unlikely]]
		return {{src + readIndex, spaceToEnd}, {src, availableBytes - spaceToEnd}};
}

void CXXRingBuffer::RingBuffer::CommitWrite(size_type count) noexcept
{
	assert(count <= FreeSpace() && "Logic error: Write committing more than available free space");
	const auto writePos = writePosition_.load(std::memory_order_relaxed);
	writePosition_.store(writePos + count, std::memory_order_release);
}

void CXXRingBuffer::RingBuffer::CommitRead(size_type count) noexcept
{
	assert(count <= AvailableBytes() && "Logic error: Read committing more than available data");
	const auto readPos = readPosition_.load(std::memory_order_relaxed);
	readPosition_.store(readPos + count, std::memory_order_release);
}
