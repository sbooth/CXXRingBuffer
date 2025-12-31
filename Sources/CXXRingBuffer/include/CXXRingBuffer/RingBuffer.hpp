//
// Copyright © 2014-2025 Stephen F. Booth
// Part of https://github.com/sbooth/CXXRingBuffer
// MIT license
//

#pragma once

#import <algorithm>
#import <atomic>
#import <cassert>
#import <cstddef>
#import <cstring>
#import <limits>
#import <memory>
#import <optional>
#import <span>
#import <type_traits>
#import <utility>

#if defined(__has_include) && __has_include(<CoreFoundation/CFData.h>)
#import <CoreFoundation/CFData.h>

#if defined(__OBJC__)
#import <Foundation/NSData.h>
#endif

#endif

namespace CXXRingBuffer {

/// A lock-free SPSC ring buffer.
///
/// This class is thread safe when used with a single producer and a single consumer.
class RingBuffer final {
public:
	/// Unsigned integer type.
	using size_type = std::size_t;
	/// Atomic unsigned integer type.
	using atomic_size_type = std::atomic<size_type>;

	/// A write vector.
	using write_vector = std::pair<std::span<unsigned char>, std::span<unsigned char>>;
	/// A read vector.
	using read_vector = std::pair<std::span<const unsigned char>, std::span<const unsigned char>>;

	/// The minimum supported ring buffer capacity in bytes.
	static constexpr size_type min_capacity = size_type{2};
	/// The maximum supported ring buffer capacity in bytes.
	static constexpr size_type max_capacity = size_type{1} << (std::numeric_limits<size_type>::digits - 1);

	// MARK: Creation and Destruction

	/// Creates an empty ring buffer.
	/// @note ``Allocate`` must be called before the object may be used.
	RingBuffer() noexcept = default;

	/// Creates a ring buffer with the specified minimum capacity.
	///
	/// The actual ring buffer capacity will be the smallest integral power of two that is not less than the specified minimum capacity.
	/// @param minCapacity The desired minimum capacity in bytes.
	/// @throw std::bad_alloc if memory could not be allocated or std::invalid_argument if the buffer capacity is not supported.
	explicit RingBuffer(size_type minCapacity);

	// This class is non-copyable
	RingBuffer(const RingBuffer&) = delete;

	/// Creates a ring buffer by moving the contents of another ring buffer.
	/// @note This method is not thread safe for the ring buffer being moved.
	/// @param other The ring buffer to move.
	RingBuffer(RingBuffer&& other) noexcept;

	// This class is non-assignable
	RingBuffer& operator=(const RingBuffer&) = delete;

	/// Moves the contents of another ring buffer into this ring buffer.
	/// @note This method is not thread safe.
	/// @param other The ring buffer to move.
	RingBuffer& operator=(RingBuffer&& other) noexcept;

	/// Destroys the ring buffer and releases all associated resources.
	~RingBuffer() noexcept;

	// MARK: Buffer Management

	/// Allocates space for data.
	///
	/// The actual ring buffer capacity will be the smallest integral power of two that is not less than the specified minimum capacity.
	/// @note This method is not thread safe.
	/// @param minCapacity The desired minimum capacity in bytes.
	/// @return true on success, false if memory could not be allocated or the buffer capacity is not supported.
	bool Allocate(size_type minCapacity) noexcept;

	/// Frees any space allocated for data.
	/// @note This method is not thread safe.
	void Deallocate() noexcept;

	/// Returns true if the ring buffer has allocated space for data.
	explicit operator bool() const noexcept
	{
		return buffer_ != nullptr;
	}

	// MARK: Buffer Information

	/// Returns the capacity of the ring buffer.
	/// @note This method is safe to call from both producer and consumer.
	/// @return The ring buffer capacity in bytes.
	[[nodiscard]] size_type Capacity() const noexcept
	{
		return capacity_;
	}

	// MARK: Buffer Usage

	/// Returns the amount of free space in the ring buffer.
	/// @note The result of this method is only accurate when called from the producer.
	/// @return The number of bytes of free space available for writing.
	[[nodiscard]] size_type FreeSpace() const noexcept
	{
		const auto writePos = writePosition_.load(std::memory_order_relaxed);
		const auto readPos = readPosition_.load(std::memory_order_acquire);
		return capacity_ - (writePos - readPos);
	}

	/// Returns true if the ring buffer is full.
	/// @note The result of this method is only accurate when called from the producer.
	/// @return true if the buffer is full.
	[[nodiscard]] bool IsFull() const noexcept
	{
		const auto writePos = writePosition_.load(std::memory_order_relaxed);
		const auto readPos = readPosition_.load(std::memory_order_acquire);
		return (writePos - readPos) == capacity_;
	}

	/// Returns the amount of data in the ring buffer.
	/// @note The result of this method is only accurate when called from the consumer.
	/// @return The number of bytes available for reading.
	[[nodiscard]] size_type AvailableBytes() const noexcept
	{
		const auto writePos = writePosition_.load(std::memory_order_acquire);
		const auto readPos = readPosition_.load(std::memory_order_relaxed);
		return writePos - readPos;
	}

	/// Returns true if the ring buffer is empty.
	/// @note The result of this method is only accurate when called from the consumer.
	/// @return true if the buffer contains no data.
	[[nodiscard]] bool IsEmpty() const noexcept
	{
		const auto writePos = writePosition_.load(std::memory_order_acquire);
		const auto readPos = readPosition_.load(std::memory_order_relaxed);
		return writePos == readPos;
	}

	// MARK: Writing and Reading Data

	/// Writes data and advances the write position.
	/// @note This method is only safe to call from the producer.
	/// @param ptr An address containing the data to copy.
	/// @param itemSize The size of an individual item in bytes.
	/// @param itemCount The desired number of items to write.
	/// @param allowPartial Whether any items should be written if insufficient free space is available to write all items.
	/// @return The number of items actually written.
	size_type Write(const void * const _Nonnull ptr, size_type itemSize, size_type itemCount, bool allowPartial) noexcept
	{
		if(!ptr || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
			return 0;

		const auto writePos = writePosition_.load(std::memory_order_relaxed);
		const auto readPos = readPosition_.load(std::memory_order_acquire);

		const auto bytesUsed = writePos - readPos;
		const auto bytesFree = capacity_ - bytesUsed;
		const auto itemsFree = bytesFree / itemSize;
		if(itemsFree == 0 || (itemsFree < itemCount && !allowPartial))
			return 0;

		const auto itemsToWrite = std::min(itemsFree, itemCount);
		const auto bytesToWrite = itemsToWrite * itemSize;

		auto *dst = static_cast<unsigned char *>(buffer_);
		const auto *src = static_cast<const unsigned char *>(ptr);

		const auto writeIndex = writePos & capacityMask_;
		const auto bytesToEnd = capacity_ - writeIndex;
		if(bytesToWrite <= bytesToEnd) [[likely]]
			std::memcpy(dst + writeIndex, src, bytesToWrite);
		else [[unlikely]] {
			std::memcpy(dst + writeIndex, src, bytesToEnd);
			std::memcpy(dst, src + bytesToEnd, bytesToWrite - bytesToEnd);
		}

		writePosition_.store(writePos + bytesToWrite, std::memory_order_release);

		return itemsToWrite;
	}

	/// Reads data and advances the read position.
	/// @note This method is only safe to call from the consumer.
	/// @param ptr An address to receive the data.
	/// @param itemSize The size of an individual item in bytes.
	/// @param itemCount The desired number of items to read.
	/// @param allowPartial Whether any items should be read if the number of items available for reading is less than count.
	/// @return The number of items actually read.
	size_type Read(void * const _Nonnull ptr, size_type itemSize, size_type itemCount, bool allowPartial) noexcept
	{
		if(!ptr || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
			return 0;

		const auto writePos = writePosition_.load(std::memory_order_acquire);
		const auto readPos = readPosition_.load(std::memory_order_relaxed);

		const auto bytesUsed = writePos - readPos;
		const auto itemsAvailable = bytesUsed / itemSize;
		if(itemsAvailable == 0 || (itemsAvailable < itemCount && !allowPartial))
			return 0;

		const auto itemsToRead = std::min(itemsAvailable, itemCount);
		const auto bytesToRead = itemsToRead * itemSize;

		auto *dst = static_cast<unsigned char *>(ptr);
		const auto *src = static_cast<const unsigned char *>(buffer_);

		const auto readIndex = readPos & capacityMask_;
		const auto bytesToEnd = capacity_ - readIndex;
		if(bytesToRead <= bytesToEnd) [[likely]]
			std::memcpy(dst, src + readIndex, bytesToRead);
		else [[unlikely]] {
			std::memcpy(dst, src + readIndex, bytesToEnd);
			std::memcpy(dst + bytesToEnd, src, bytesToRead - bytesToEnd);
		}

		readPosition_.store(readPos + bytesToRead, std::memory_order_release);

		return itemsToRead;
	}

	/// Reads data without advancing the read position.
	/// @note This method is only safe to call from the consumer.
	/// @param ptr An address to receive the data.
	/// @param itemSize The size of an individual item in bytes.
	/// @param itemCount The desired number of items to read.
	/// @return True if the requested items were read, false otherwise.
	[[nodiscard]] bool Peek(void * const _Nonnull ptr, size_type itemSize, size_type itemCount) const noexcept
	{
		if(!ptr || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
			return false;

		const auto writePos = writePosition_.load(std::memory_order_acquire);
		const auto readPos = readPosition_.load(std::memory_order_relaxed);

		const auto bytesUsed = writePos - readPos;
		const auto itemsAvailable = bytesUsed / itemSize;
		if(itemsAvailable < itemCount)
			return false;

		const auto bytesToPeek = itemCount * itemSize;

		auto *dst = static_cast<unsigned char *>(ptr);
		const auto *src = static_cast<const unsigned char *>(buffer_);

		const auto readIndex = readPos & capacityMask_;
		const auto bytesToEnd = capacity_ - readIndex;
		if(bytesToPeek <= bytesToEnd) [[likely]]
			std::memcpy(dst, src + readIndex, bytesToPeek);
		else [[unlikely]] {
			std::memcpy(dst, src + readIndex, bytesToEnd);
			std::memcpy(dst + bytesToEnd, src, bytesToPeek - bytesToEnd);
		}

		return true;
	}

	// MARK: Discarding Data

	/// Skips data and advances the read position.
	/// @note This method is only safe to call from the consumer.
	/// @param itemSize The size of an individual item in bytes.
	/// @param itemCount The desired number of items to skip.
	/// @return The number of items actually skipped.
	size_type Skip(size_type itemSize, size_type itemCount) noexcept
	{
		if(itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]]
			return 0;

		const auto writePos = writePosition_.load(std::memory_order_acquire);
		const auto readPos = readPosition_.load(std::memory_order_relaxed);

		const auto bytesUsed = writePos - readPos;
		const auto itemsAvailable = bytesUsed / itemSize;
		if(itemsAvailable == 0) [[unlikely]]
			return 0;

		const auto itemsToSkip = std::min(itemsAvailable, itemCount);
		const auto bytesToSkip = itemsToSkip * itemSize;

		readPosition_.store(readPos + bytesToSkip, std::memory_order_release);

		return itemsToSkip;
	}

	/// Advances the read position to the write position, emptying the buffer.
	/// @note This method is only safe to call from the consumer.
	/// @return The number of bytes discarded.
	size_type Drain() noexcept
	{
		const auto writePos = writePosition_.load(std::memory_order_acquire);
		const auto readPos = readPosition_.load(std::memory_order_relaxed);

		const auto bytesUsed = writePos - readPos;
		if(bytesUsed == 0) [[unlikely]]
			return 0;

		readPosition_.store(writePos, std::memory_order_release);
		return bytesUsed;
	}

	// MARK: Writing and Reading Spans

	/// Writes items and advances the write position.
	/// @note This method is only safe to call from the producer.
	/// @tparam T The type to write.
	/// @param data A span containing the items to copy.
	/// @param allowPartial Whether any items should be written if insufficient free space is available to write all items.
	/// @return The number of items actually written.
	template <typename T> requires std::is_trivially_copyable_v<T>
	size_type Write(std::span<const T> data, bool allowPartial = true) noexcept
	{
		return Write(data.data(), sizeof(T), data.size(), allowPartial);
	}

	/// Reads items and advances the read position.
	/// @note This method is only safe to call from the consumer.
	/// @tparam T The type to read.
	/// @param buffer A span to receive the items.
	/// @param allowPartial Whether any items should be read if the number of items available for reading is less than buffer.size().
	/// @return The number of items actually read.
	template <typename T> requires std::is_trivially_copyable_v<T>
	size_type Read(std::span<T> buffer, bool allowPartial = true) noexcept
	{
		return Read(buffer.data(), sizeof(T), buffer.size(), allowPartial);
	}

	/// Reads items without advancing the read position.
	/// @note This method is only safe to call from the consumer.
	/// @tparam T The type to read.
	/// @param buffer A span to receive the data.
	/// @return True if the requested items were read, false otherwise.
	template <typename T> requires std::is_trivially_copyable_v<T>
	[[nodiscard]] bool Peek(std::span<T> buffer) const noexcept
	{
		return Peek(buffer.data(), sizeof(T), buffer.size());
	}

	// MARK: Writing and Reading Single Values

	/// Writes a value and advances the write position.
	/// @note This method is only safe to call from the producer.
	/// @tparam T The type to write.
	/// @param value The value to write.
	/// @return true if value was successfully written.
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool WriteValue(const T& value) noexcept
	{
		return Write(static_cast<const void *>(std::addressof(value)), sizeof value, 1, false) == 1;
	}

	/// Reads a value and advances the read position.
	/// @note This method is only safe to call from the consumer.
	/// @tparam T The type to read.
	/// @param value The destination value.
	/// @return true on success, false otherwise.
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool ReadValue(T& value) noexcept
	{
		return Read(static_cast<void *>(std::addressof(value)), sizeof value, 1, false) == 1;
	}

	/// Reads a value and advances the read position.
	/// @note This method is only safe to call from the consumer.
	/// @tparam T The type to read.
	/// @return A std::optional containing an instance of T if sufficient bytes were available for reading.
	/// @throw Any exceptions thrown by the default constructor of T.
	template <typename T> requires std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>
	std::optional<T> ReadValue() noexcept(std::is_nothrow_default_constructible_v<T>)
	{
		if(T value{}; ReadValue(value))
			return value;
		return std::nullopt;
	}

	/// Reads a value without advancing the read position.
	/// @note This method is only safe to call from the consumer.
	/// @tparam T The type to read.
	/// @param value The destination value.
	/// @return true on success, false otherwise.
	template <typename T> requires std::is_trivially_copyable_v<T>
	[[nodiscard]] bool PeekValue(T& value) const noexcept
	{
		return Peek(static_cast<void *>(std::addressof(value)), sizeof value, 1);
	}

	/// Reads a value without advancing the read position.
	/// @note This method is only safe to call from the consumer.
	/// @tparam T The type to read.
	/// @return A std::optional containing an instance of T if sufficient bytes were available for reading.
	/// @throw Any exceptions thrown by the default constructor of T.
	template <typename T> requires std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>
	[[nodiscard]] std::optional<T> PeekValue() const noexcept(std::is_nothrow_default_constructible_v<T>)
	{
		if(T value{}; PeekValue(value))
			return value;
		return std::nullopt;
	}

	// MARK: Writing and Reading Multiple Values

	/// Writes values and advances the write position.
	/// @note This method is only safe to call from the producer.
	/// @tparam Args The types to write.
	/// @param args The values to write.
	/// @return true if the values were successfully written.
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...) && (sizeof...(Args) > 0)
	bool WriteValues(const Args&... args) noexcept
	{
		constexpr auto totalSize = (sizeof args + ...);
		auto [front, back] = GetWriteVector();

		const auto frontSize = front.size();
		if(frontSize + back.size() < totalSize)
			return false;

		std::size_t cursor = 0;
		const auto write_single_arg = [&](const void *arg, std::size_t len) noexcept {
			const auto *src = static_cast<const unsigned char *>(arg);
			if(cursor + len <= frontSize)
				std::memcpy(front.data() + cursor, src, len);
			else if(cursor >= frontSize)
				std::memcpy(back.data() + (cursor - frontSize), src, len);
			else [[unlikely]] {
				const size_t toFront = frontSize - cursor;
				std::memcpy(front.data() + cursor, src, toFront);
				std::memcpy(back.data(), src + toFront, len - toFront);
			}
			cursor += len;
		};

		(write_single_arg(std::addressof(args), sizeof args), ...);

		CommitWrite(totalSize);
		return true;
	}

	/// Reads values and advances the read position.
	/// @note This method is only safe to call from the consumer.
	/// @tparam Args The types to read.
	/// @param args The destination values.
	/// @return true if the values were successfully read.
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...) && (sizeof...(Args) > 0)
	bool ReadValues(Args&... args) noexcept
	{
		if(!PeekValues(args...))
			return false;
		CommitRead((sizeof args + ...));
		return true;
	}

	/// Reads values without advancing the read position.
	/// @note This method is only safe to call from the consumer.
	/// @tparam Args The types to read.
	/// @param args The destination values.
	/// @return true if the values were successfully read.
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...) && (sizeof...(Args) > 0)
	[[nodiscard]] bool PeekValues(Args&... args) const noexcept
	{
		return CopyFromReadVector<Args...>([&](auto&& copier) noexcept { (copier(std::addressof(args), sizeof args), ...); });
	}

	// MARK: Advanced Writing and Reading

	/// Returns a write vector containing the current writable space.
	/// @note This method is only safe to call from the producer.
	/// @return A pair of spans containing the current writable space.
	[[nodiscard]] write_vector GetWriteVector() const noexcept
	{
		const auto writePos = writePosition_.load(std::memory_order_relaxed);
		const auto readPos = readPosition_.load(std::memory_order_acquire);

		const auto bytesUsed = writePos - readPos;
		const auto bytesFree = capacity_ - bytesUsed;
		if(bytesFree == 0) [[unlikely]]
			return {};

		auto *dst = static_cast<unsigned char *>(buffer_);

		const auto writeIndex = writePos & capacityMask_;
		const auto bytesToEnd = capacity_ - writeIndex;
		if(bytesFree <= bytesToEnd) [[likely]]
			return {{dst + writeIndex, bytesFree}, {}};
		else [[unlikely]]
			return {{dst + writeIndex, bytesToEnd}, {dst, bytesFree - bytesToEnd}};
	}

	/// Finalizes a write transaction by writing staged data to the ring buffer.
	/// @warning The behavior is undefined if count is greater than the free space in the write vector.
	/// @note This method is only safe to call from the producer.
	/// @param count The number of bytes that were successfully written to the write vector.
	void CommitWrite(size_type count) noexcept
	{
		assert(count <= FreeSpace() && "Logic error: Write committing more than available free space");
		const auto writePos = writePosition_.load(std::memory_order_relaxed);
		writePosition_.store(writePos + count, std::memory_order_release);
	}

	/// Returns a read vector containing the current readable data.
	/// @note This method is only safe to call from the consumer.
	/// @return A pair of spans containing the current readable data.
	[[nodiscard]] read_vector GetReadVector() const noexcept
	{
		const auto writePos = writePosition_.load(std::memory_order_acquire);
		const auto readPos = readPosition_.load(std::memory_order_relaxed);

		const auto bytesUsed = writePos - readPos;
		if(bytesUsed == 0) [[unlikely]]
			return {};

		const auto *src = static_cast<const unsigned char *>(buffer_);

		const auto readIndex = readPos & capacityMask_;
		const auto bytesToEnd = capacity_ - readIndex;
		if(bytesUsed <= bytesToEnd) [[likely]]
			return {{src + readIndex, bytesUsed}, {}};
		else [[unlikely]]
			return {{src + readIndex, bytesToEnd}, {src, bytesUsed - bytesToEnd}};
	}

	/// Finalizes a read transaction by removing data from the front of the ring buffer.
	/// @warning The behavior is undefined if count is greater than the available data in the read vector.
	/// @note This method is only safe to call from the consumer.
	/// @param count The number of bytes that were successfully read from the read vector.
	void CommitRead(size_type count) noexcept
	{
		assert(count <= AvailableBytes() && "Logic error: Read committing more than available data");
		const auto readPos = readPosition_.load(std::memory_order_relaxed);
		readPosition_.store(readPos + count, std::memory_order_release);
	}

	// MARK: Extensions

#if defined(__has_include) && __has_include(<CoreFoundation/CFData.h>)
	/// Writes data and advances the write position.
	/// @param data The data to copy to the ring buffer.
	/// @return @c true if the data was successfully written or @c false if there is insufficient write space available.
	bool WriteData(CFDataRef _Nonnull data) noexcept
	{
		if(!data)
			return false;
		const auto length = CFDataGetLength(data);
		const auto count = static_cast<size_type>(length);
		return Write(CFDataGetBytePtr(data), 1, count, false) == count;
	}

	/// Reads data and advances the read position.
	/// @param data The destination data object.
	/// @param count The desired number of bytes to read.
	void ReadData(CFMutableDataRef _Nonnull data, CFIndex count) noexcept
	{
		if(!data || count < 0)
			return;
		CFDataSetLength(data, count);
		const auto length = Read(CFDataGetMutableBytePtr(data), 1, count, false);
		if(length != count)
			CFDataSetLength(data, length);
	}

#if defined(__OBJC__)
	/// Writes data and advances the write position.
	/// @param data The data to copy to the ring buffer.
	/// @return @c true if the data was successfully written or @c false if there is insufficient write space available.
	bool WriteData(NSData * _Nonnull data) noexcept
	{
		return WriteData((__bridge CFDataRef)data);
	}

	/// Reads data and advances the read position.
	/// @param data The destination data object.
	/// @param count The desired number of bytes to read.
	void ReadData(NSMutableData * _Nonnull data, NSUInteger count) noexcept
	{
		ReadData((__bridge CFMutableDataRef)data, count);
	}

	/// Reads data and advances the read position.
	/// @param count The desired number of bytes to read.
	/// @return An @c NSData object or @c nil if an error occurred.
	NSData * _Nullable ReadData(NSUInteger count) noexcept
	{
		NSMutableData *data = [NSMutableData dataWithCapacity:count];
		ReadData((__bridge CFMutableDataRef)data, count);
		return data;
	}
#endif

#endif

private:
	/// Copies values from the read vector without advancing the read position.
	/// @note This method is only safe to call from the consumer.
	/// @tparam Args The types to read.
	/// @param processor A lambda accepting a copier parameter.
	/// @return true if the values were successfully copied.
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...) && (sizeof...(Args) > 0)
	bool CopyFromReadVector(auto&& processor) const noexcept
	{
		using copier_type = void(*)(void *, std::size_t) noexcept;
		static_assert(std::is_nothrow_invocable_v<decltype(processor), copier_type>, "Processor must be callable with a noexcept copier without throwing");

		constexpr auto totalSize = (sizeof(Args) + ...);
		const auto [front, back] = GetReadVector();

		const auto frontSize = front.size();
		if(frontSize + back.size() < totalSize)
			return false;

		std::size_t cursor = 0;
		const auto copier = [&](void *arg, std::size_t len) noexcept {
			auto *dst = static_cast<unsigned char *>(arg);
			if(cursor + len <= frontSize)
				std::memcpy(dst, front.data() + cursor, len);
			else if(cursor >= frontSize)
				std::memcpy(dst, back.data() + (cursor - frontSize), len);
			else [[unlikely]] {
				const size_t fromFront = frontSize - cursor;
				std::memcpy(dst, front.data() + cursor, fromFront);
				std::memcpy(dst + fromFront, back.data(), len - fromFront);
			}
			cursor += len;
		};

		processor(copier);
		return true;
	}

	/// The memory buffer holding the data.
	void * _Nullable buffer_{nullptr};

	/// The capacity of buffer_ in bytes.
	size_type capacity_{0};
	/// The capacity of buffer_ in bytes minus one.
	size_type capacityMask_{0};

	/// The free-running write location.
	atomic_size_type writePosition_{0};
	/// The free-running read location.
	atomic_size_type readPosition_{0};

	static_assert(atomic_size_type::is_always_lock_free, "Lock-free atomic_size_type required");
};

} /* namespace CXXRingBuffer */
