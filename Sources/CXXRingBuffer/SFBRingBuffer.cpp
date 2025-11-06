//
// Copyright © 2014-2025 Stephen F. Booth
// Part of https://github.com/sbooth/CXXRingBuffer
// MIT license
//

#import <cassert>
#import <cstdlib>
#import <limits>

#import "SFBRingBuffer.hpp"

namespace {

/// Returns the smallest power of two value greater than @c x
/// @param x A value in the range [2..2147483648]
/// @return The smallest power of two greater than @c x
constexpr uint32_t NextPowerOfTwo(uint32_t x) noexcept
{
	assert(x > 1);
	assert(x <= ((std::numeric_limits<uint32_t>::max() / 2) + 1));
	return static_cast<uint32_t>(1 << (32 - __builtin_clz(x - 1)));
}

} /* namespace */

SFB::RingBuffer::RingBuffer(RingBuffer&& other) noexcept
: mBuffer{other.mBuffer}, mCapacityBytes{other.mCapacityBytes}, mCapacityBytesMask{other.mCapacityBytesMask}, mWritePosition{other.mWritePosition.load(std::memory_order_acquire)}, mReadPosition{other.mReadPosition.load(std::memory_order_acquire)}
{
	other.mBuffer = nullptr;

	other.mCapacityBytes = 0;
	other.mCapacityBytesMask = 0;

	other.mReadPosition = 0;
	other.mWritePosition = 0;
}

SFB::RingBuffer& SFB::RingBuffer::operator=(RingBuffer&& other) noexcept
{
	if(this == &other)
		return *this;

	std::free(mBuffer);

	mBuffer = other.mBuffer;

	mCapacityBytes = other.mCapacityBytes;
	mCapacityBytesMask = other.mCapacityBytesMask;

	mReadPosition.store(other.mReadPosition.load(std::memory_order_acquire), std::memory_order_release);
	mWritePosition.store(other.mWritePosition.load(std::memory_order_acquire), std::memory_order_release);

	other.mBuffer = nullptr;

	other.mCapacityBytes = 0;
	other.mCapacityBytesMask = 0;

	other.mReadPosition = 0;
	other.mWritePosition = 0;

	return *this;
}

SFB::RingBuffer::~RingBuffer() noexcept
{
	std::free(mBuffer);
}

#pragma mark Buffer Management

bool SFB::RingBuffer::Allocate(uint32_t capacityBytes) noexcept
{
	if(capacityBytes < 2 || capacityBytes > 0x80000000)
		return false;

	Deallocate();

	// Round up to the next power of two
	capacityBytes = NextPowerOfTwo(capacityBytes);

	mBuffer = std::malloc(capacityBytes);
	if(!mBuffer)
		return false;

	mCapacityBytes = capacityBytes;
	mCapacityBytesMask = capacityBytes - 1;

	mReadPosition = 0;
	mWritePosition = 0;

	return true;
}

void SFB::RingBuffer::Deallocate() noexcept
{
	if(mBuffer) {
		std::free(mBuffer);
		mBuffer = nullptr;

		mCapacityBytes = 0;
		mCapacityBytesMask = 0;

		mReadPosition = 0;
		mWritePosition = 0;
	}
}

void SFB::RingBuffer::Reset() noexcept
{
	mReadPosition = 0;
	mWritePosition = 0;
}

#pragma mark Buffer Information

uint32_t SFB::RingBuffer::CapacityBytes() const noexcept
{
	return mCapacityBytes;
}

uint32_t SFB::RingBuffer::BytesAvailableToRead() const noexcept
{
	const auto writePosition = mWritePosition.load(std::memory_order_acquire);
	const auto readPosition = mReadPosition.load(std::memory_order_acquire);

	if(writePosition > readPosition)
		return writePosition - readPosition;
	else
		return (writePosition - readPosition + mCapacityBytes) & mCapacityBytesMask;
}

uint32_t SFB::RingBuffer::BytesAvailableToWrite() const noexcept
{
	const auto writePosition = mWritePosition.load(std::memory_order_acquire);
	const auto readPosition = mReadPosition.load(std::memory_order_acquire);

	if(writePosition > readPosition)
		return ((readPosition - writePosition + mCapacityBytes) & mCapacityBytesMask) - 1;
	else if(writePosition < readPosition)
		return (readPosition - writePosition) - 1;
	else
		return mCapacityBytes - 1;
}

#pragma mark Reading and Writing Data

uint32_t SFB::RingBuffer::Read(void * const destinationBuffer, uint32_t byteCount, bool allowPartial) noexcept
{
	if(!destinationBuffer || byteCount == 0)
		return 0;

	const auto writePosition = mWritePosition.load(std::memory_order_acquire);
	const auto readPosition = mReadPosition.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = writePosition - readPosition;
	else
		bytesAvailable = (writePosition - readPosition + mCapacityBytes) & mCapacityBytesMask;

	if(bytesAvailable == 0 || (bytesAvailable < byteCount && !allowPartial))
		return 0;

	const auto bytesToRead = std::min(bytesAvailable, byteCount);
	if(readPosition + bytesToRead > mCapacityBytes) {
		const auto bytesAfterReadPointer = mCapacityBytes - readPosition;
		std::memcpy(destinationBuffer,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(mBuffer) + readPosition),
					bytesAfterReadPointer);
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(destinationBuffer) + bytesAfterReadPointer),
					mBuffer,
					bytesToRead - bytesAfterReadPointer);
	}
	else
		std::memcpy(destinationBuffer, reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(mBuffer) + readPosition), bytesToRead);

	mReadPosition.store((readPosition + bytesToRead) & mCapacityBytesMask, std::memory_order_release);

	return bytesToRead;
}

uint32_t SFB::RingBuffer::Peek(void * const destinationBuffer, uint32_t byteCount, bool allowPartial) const noexcept
{
	if(!destinationBuffer || byteCount == 0)
		return 0;

	const auto writePosition = mWritePosition.load(std::memory_order_acquire);
	const auto readPosition = mReadPosition.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = writePosition - readPosition;
	else
		bytesAvailable = (writePosition - readPosition + mCapacityBytes) & mCapacityBytesMask;

	if(bytesAvailable == 0 || (bytesAvailable < byteCount && !allowPartial))
		return 0;

	const auto bytesToRead = std::min(bytesAvailable, byteCount);
	if(readPosition + bytesToRead > mCapacityBytes) {
		auto bytesAfterReadPointer = mCapacityBytes - readPosition;
		std::memcpy(destinationBuffer,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(mBuffer) + readPosition),
					bytesAfterReadPointer);
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(destinationBuffer) + bytesAfterReadPointer),
					mBuffer,
					bytesToRead - bytesAfterReadPointer);
	}
	else
		std::memcpy(destinationBuffer, reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(mBuffer) + readPosition), bytesToRead);

	return bytesToRead;
}

uint32_t SFB::RingBuffer::Write(const void * const sourceBuffer, uint32_t byteCount, bool allowPartial) noexcept
{
	if(!sourceBuffer || byteCount == 0)
		return 0;

	const auto writePosition = mWritePosition.load(std::memory_order_acquire);
	const auto readPosition = mReadPosition.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = ((readPosition - writePosition + mCapacityBytes) & mCapacityBytesMask) - 1;
	else if(writePosition < readPosition)
		bytesAvailable = (readPosition - writePosition) - 1;
	else
		bytesAvailable = mCapacityBytes - 1;

	if(bytesAvailable == 0 || (bytesAvailable < byteCount && !allowPartial))
		return 0;

	const auto bytesToWrite = std::min(bytesAvailable, byteCount);
	if(writePosition + bytesToWrite > mCapacityBytes) {
		auto bytesAfterWritePointer = mCapacityBytes - writePosition;
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(mBuffer) + writePosition),
					sourceBuffer,
					bytesAfterWritePointer);
		std::memcpy(mBuffer,
					reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(sourceBuffer) + bytesAfterWritePointer),
					bytesToWrite - bytesAfterWritePointer);
	}
	else
		std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(mBuffer) + writePosition), sourceBuffer, bytesToWrite);

	mWritePosition.store((writePosition + bytesToWrite) & mCapacityBytesMask, std::memory_order_release);

	return bytesToWrite;
}

#pragma mark Advanced Reading and Writing

void SFB::RingBuffer::AdvanceReadPosition(uint32_t byteCount) noexcept
{
	mReadPosition.store((mReadPosition.load(std::memory_order_acquire) + byteCount) & mCapacityBytesMask, std::memory_order_release);
}

void SFB::RingBuffer::AdvanceWritePosition(uint32_t byteCount) noexcept
{
	mWritePosition.store((mWritePosition.load(std::memory_order_acquire) + byteCount) & mCapacityBytesMask, std::memory_order_release);
}

const SFB::RingBuffer::ReadBufferPair SFB::RingBuffer::ReadVector() const noexcept
{
	const auto writePosition = mWritePosition.load(std::memory_order_acquire);
	const auto readPosition = mReadPosition.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = writePosition - readPosition;
	else
		bytesAvailable = (writePosition - readPosition + mCapacityBytes) & mCapacityBytesMask;

	const auto endOfRead = readPosition + bytesAvailable;

	if(endOfRead > mCapacityBytes)
		return {
			{ reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(mBuffer) + readPosition), mCapacityBytes - readPosition },
			{ mBuffer, endOfRead & mCapacityBytes }
		};
	else
		return {
			{ reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(mBuffer) + readPosition), bytesAvailable },
			{}
		};
}

const SFB::RingBuffer::WriteBufferPair SFB::RingBuffer::WriteVector() const noexcept
{
	const auto writePosition = mWritePosition.load(std::memory_order_acquire);
	const auto readPosition = mReadPosition.load(std::memory_order_acquire);

	uint32_t bytesAvailable;
	if(writePosition > readPosition)
		bytesAvailable = ((readPosition - writePosition + mCapacityBytes) & mCapacityBytesMask) - 1;
	else if(writePosition < readPosition)
		bytesAvailable = (readPosition - writePosition) - 1;
	else
		bytesAvailable = mCapacityBytes - 1;

	const auto endOfWrite = writePosition + bytesAvailable;

	if(endOfWrite > mCapacityBytes)
		return {
			{ reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(mBuffer) + writePosition), mCapacityBytes - writePosition },
			{ mBuffer, endOfWrite & mCapacityBytes }
		};
	else
		return {
			{ reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(mBuffer) + writePosition), bytesAvailable },
			{}
		};
}
