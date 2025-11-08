//
// Copyright © 2014-2025 Stephen F. Booth
// Part of https://github.com/sbooth/CXXRingBuffer
// MIT license
//

#pragma once

#import <algorithm>
#import <atomic>
#import <cstring>
#import <optional>
#import <type_traits>
#import <utility>

#if defined(__has_include) && __has_include(<CoreFoundation/CFData.h>)
#import <CoreFoundation/CFData.h>

#if defined(__OBJC__)
#import <Foundation/NSData.h>
#endif /* defined(__OBJC__) */

#endif /* defined(__has_include) && __has_include(<CoreFoundation/CFData.h>) */

namespace SFB {

/// A lock-free ring buffer.
///
/// This class is thread safe when used from one reader thread and one writer thread (single producer, single consumer model).
class RingBuffer final
{

public:

	// MARK: Creation and Destruction

	/// Creates an empty ring buffer.
	/// @note ``Allocate`` must be called before the object may be used.
	RingBuffer() noexcept = default;

	/// Creates a ring buffer with the specified minimum capacity.
	/// @note Capacities from 2 to 2,147,483,647 (0x7FFFFFFF) bytes are supported.
	/// @param capacity The desired minimum capacity, in bytes.
	/// @throw @c std::bad_alloc if memory could not be allocated or @c std::invalid_argument if the capacity is not supported.
	explicit RingBuffer(uint32_t capacity);

	// This class is non-copyable
	RingBuffer(const RingBuffer&) = delete;

	/// Creates a new ring buffer by moving the contents of another ring buffer.
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
	/// @note This method is not thread safe.
	/// @note Capacities from 2 to 2,147,483,647 (0x7FFFFFFF) bytes are supported.
	/// @param capacity The desired minimum capacity, in bytes.
	/// @return @c true on success, @c false on error.
	bool Allocate(uint32_t capacity) noexcept;

	/// Frees any space allocated for data.
	/// @note This method is not thread safe.
	void Deallocate() noexcept;

	/// Resets the read and write positions to their default state.
	/// @note This method is not thread safe.
	void Reset() noexcept;

	// MARK: Buffer Information

	/// Returns the buffer capacity in bytes.
	/// @return The buffer capacity in bytes.
	uint32_t CapacityBytes() const noexcept;

	/// Returns the number of bytes available for reading.
	/// @return The number of bytes available for reading.
	uint32_t BytesAvailableToRead() const noexcept;

	/// Returns the free space available for writing in bytes.
	/// @return The number of bytes of free space.
	uint32_t BytesAvailableToWrite() const noexcept;

	// MARK: Reading and Writing Data

	/// Reads data and advances the read position.
	/// @param destination An address to receive the data.
	/// @param count The desired number of bytes to read.
	/// @param allowPartial Whether any bytes should be read if the number of bytes available for reading is less than @c count.
	/// @return The number of bytes actually read.
	uint32_t Read(void * const _Nonnull destination, uint32_t count, bool allowPartial = true) noexcept;

	/// Reads data without advancing the read position.
	/// @param destination An address to receive the data.
	/// @param count The desired number of bytes to read.
	/// @param allowPartial Whether any bytes should be read if the number of bytes available for reading is less than @c count.
	/// @return The number of bytes actually read.
	uint32_t Peek(void * const _Nonnull destination, uint32_t count, bool allowPartial = true) const noexcept;

	/// Writes data and advances the write position.
	/// @param source An address containing the data to copy.
	/// @param count The desired number of bytes to write.
	/// @param allowPartial Whether any bytes should be written if the free space available for writing is less than @c count.
	/// @return The number of bytes actually written.
	uint32_t Write(const void * const _Nonnull source, uint32_t count, bool allowPartial = true) noexcept;

	// MARK: Reading and Writing Types

	/// Reads a value and advances the read position.
	/// @tparam T The type to read.
	/// @param value The destination value.
	/// @return @c true on success, @c false otherwise.
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool ReadValue(T& value) noexcept
	{
		const auto size = static_cast<uint32_t>(sizeof(T));
		const auto bytesRead = Read(static_cast<void *>(&value), size, false);
		return bytesRead == size;
	}

	/// Reads values and advances the read position.
	/// @tparam Args The types to read.
	/// @param args The destination values.
	/// @return @c true if the values were successfully read.
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...)
	bool ReadValues(Args&... args) noexcept
	{
		const auto totalSize = static_cast<uint32_t>((sizeof(args) + ...));

		const auto rvec = GetReadVector();

		// Don't read anything if there is insufficient data
		if(rvec.first.length_ + rvec.second.length_ < totalSize)
			return false;

		uint32_t bytesRead = 0;

		([&]
		 {
			auto bytesRemaining = static_cast<uint32_t>(sizeof(args));

			// Read from rvec.first if data is available
			if(rvec.first.length_ > bytesRead) {
				const auto n = std::min(bytesRemaining, rvec.first.length_ - bytesRead);
				std::memcpy(static_cast<void *>(&args),
							reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(rvec.first.buffer_) + bytesRead),
							n);
				bytesRemaining -= n;
				bytesRead += n;
			}
			// Read from rvec.second
			if(bytesRemaining > 0) {
				const auto n = bytesRemaining;
				std::memcpy(static_cast<void *>(&args),
							reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(rvec.second.buffer_) + (bytesRead - rvec.first.length_)),
							n);
				bytesRead += n;
			}
		}(), ...);

		AdvanceReadPosition(bytesRead);

		return true;
	}

	/// Reads a value and advances the read position.
	/// @tparam T The type to read.
	/// @return A @c std::optional containing an instance of @c T if sufficient bytes were available for reading.
	template <typename T> requires std::is_default_constructible_v<T>
	std::optional<T> ReadValue() noexcept(std::is_nothrow_default_constructible_v<T>)
	{
		T value{};
		if(!ReadValue(value))
			return std::nullopt;
		return value;
	}

	/// Reads a value without advancing the read position.
	/// @tparam T The type to read.
	/// @param value The destination value.
	/// @return @c true on success, @c false otherwise.
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool PeekValue(T& value) const noexcept
	{
		const auto size = static_cast<uint32_t>(sizeof(T));
		const auto bytesRead = Peek(static_cast<void *>(&value), size, false);
		return bytesRead == size;
	}

	/// Reads a value without advancing the read position.
	/// @tparam T The type to read.
	/// @return A @c std::optional containing an instance of @c T if sufficient bytes were available for reading.
	template <typename T> requires std::is_default_constructible_v<T>
	std::optional<T> PeekValue() const noexcept(std::is_nothrow_default_constructible_v<T>)
	{
		T value{};
		if(!PeekValue(value))
			return std::nullopt;
		return value;
	}

	/// Writes a value and advances the write position.
	/// @tparam T The type to write.
	/// @param value The value to write.
	/// @return @c true if @c value was successfully written.
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool WriteValue(const T& value) noexcept
	{
		const auto size = static_cast<uint32_t>(sizeof(T));
		const auto bytesWritten = Write(static_cast<const void *>(&value), size, false);
		return bytesWritten == size;
	}

	/// Writes values and advances the write position.
	/// @tparam Args The types to write.
	/// @param args The values to write.
	/// @return @c true if the values were successfully written.
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...)
	bool WriteValues(const Args&... args) noexcept
	{
		const auto totalSize = static_cast<uint32_t>((sizeof(args) + ...));

		auto wvec = GetWriteVector();

		// Don't write anything if there is insufficient space
		if(wvec.first.capacity_ + wvec.second.capacity_ < totalSize)
			return false;

		uint32_t bytesWritten = 0;

		([&]
		 {
			auto bytesRemaining = static_cast<uint32_t>(sizeof(args));

			// Write to wvec.first if space is available
			if(wvec.first.capacity_ > bytesWritten) {
				const auto n = std::min(bytesRemaining, wvec.first.capacity_ - bytesWritten);
				std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(wvec.first.buffer_) + bytesWritten),
							static_cast<const void *>(&args),
							n);
				bytesRemaining -= n;
				bytesWritten += n;
			}
			// Write to wvec.second
			if(bytesRemaining > 0) {
				const auto n = bytesRemaining;
				std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(wvec.second.buffer_) + (bytesWritten - wvec.first.capacity_)),
							static_cast<const void *>(&args),
							n);
				bytesWritten += n;
			}
		}(), ...);

		AdvanceWritePosition(bytesWritten);

		return true;
	}

	// MARK: Advanced Reading and Writing

	/// Advances the read position by the specified number of bytes.
	/// @param count The number of bytes to advance the read position.
	void AdvanceReadPosition(uint32_t count) noexcept;

	/// Advances the write position by the specified number of bytes.
	/// @param count The number of bytes to advance the write position.
	void AdvanceWritePosition(uint32_t count) noexcept;

	/// A read-only memory buffer.
	struct ReadBuffer {
		/// The memory buffer location.
		const void * const _Nullable buffer_{nullptr};
		/// The number of bytes of valid data in @c buffer_.
		const uint32_t length_{0};

	private:
		friend class RingBuffer;

		/// Constructs an empty read buffer.
		ReadBuffer() noexcept = default;

		/// Constructs a read buffer with the specified location and size.
		/// @param buffer The memory buffer location.
		/// @param length The number of bytes of valid data in @c buffer.
		ReadBuffer(const void * const _Nullable buffer, uint32_t length) noexcept
		: buffer_{buffer}, length_{length}
		{}
	};

	/// A pair of read buffers.
	using ReadBufferPair = std::pair<const ReadBuffer, const ReadBuffer>;

	/// Returns a read vector containing the current readable data.
	/// @return A pair of read buffers containing the current readable data.
	const ReadBufferPair GetReadVector() const noexcept;

	/// A write-only memory buffer.
	struct WriteBuffer {
		/// The memory buffer location.
		void * const _Nullable buffer_{nullptr};
		/// The capacity of @c buffer_ in bytes.
		const uint32_t capacity_{0};

	private:
		friend class RingBuffer;

		/// Constructs an empty write buffer.
		WriteBuffer() noexcept = default;

		/// Constructs a write buffer with the specified location and capacity.
		/// @param buffer The memory buffer location.
		/// @param capacity The capacity of @c buffer in bytes.
		WriteBuffer(void * const _Nullable buffer, uint32_t capacity) noexcept
		: buffer_{buffer}, capacity_{capacity}
		{}
	};

	/// A pair of write buffers.
	using WriteBufferPair = std::pair<const WriteBuffer, const WriteBuffer>;

	/// Returns a write vector containing the current writable space.
	/// @return A pair of write buffers containing the current writable space.
	const WriteBufferPair GetWriteVector() const noexcept;

	// MARK: Extensions

#if defined(__has_include) && __has_include(<CoreFoundation/CFData.h>)
	/// Reads data and advances the read position.
	/// @param data The destination data object.
	/// @param count The desired number of bytes to read.
	void ReadData(CFMutableDataRef _Nonnull data, uint32_t count) noexcept
	{
		if(!data)
			return;
		CFDataSetLength(data, count);
		const auto length = Read(CFDataGetMutableBytePtr(data), count, false);
		if(length != count)
			CFDataSetLength(data, length);
	}

	/// Writes data and advances the write position.
	/// @param data The data to copy to the ring buffer.
	/// @return @c true if the data was successfully written or @c false if there is insufficient write space available.
	bool WriteData(CFDataRef _Nonnull data) noexcept
	{
		if(!data)
			return false;
		const auto length = CFDataGetLength(data);
		if(length < 0 || length > std::numeric_limits<uint32_t>::max())
			return false;
		const auto count = static_cast<uint32_t>(length);
		return Write(CFDataGetBytePtr(data), count, false) == count;
	}

#if defined(__OBJC__)
	/// Reads data and advances the read position.
	/// @param data The destination data object.
	/// @param count The desired number of bytes to read.
	void ReadData(NSMutableData * _Nonnull data, uint32_t count) noexcept
	{
		ReadData((__bridge CFMutableDataRef)data, count);
	}

	/// Reads data and advances the read position.
	/// @param count The desired number of bytes to read.
	/// @return An @c NSData object or @c nil if an error occurred.
	NSData * _Nullable ReadData(uint32_t count) noexcept
	{
		NSMutableData *data = [NSMutableData dataWithCapacity:count];
		ReadData((__bridge CFMutableDataRef)data, count);
		return data;
	}

	/// Writes data and advances the write position.
	/// @param data The data to copy to the ring buffer.
	/// @return @c true if the data was successfully written or @c false if there is insufficient write space available.
	bool WriteData(NSData * _Nonnull data) noexcept
	{
		return WriteData((__bridge CFDataRef)data);
	}
#endif /* defined(__OBJC__) */

#endif /* defined(__has_include) && __has_include(<CoreFoundation/CFData.h>) */

private:

	/// The memory buffer holding the data.
	void * _Nullable buffer_{nullptr};

	/// The capacity of @c buffer_ in bytes.
	uint32_t capacity_{0};
	/// The capacity of @c buffer_ in bytes minus one.
	uint32_t capacityMask_{0};

	/// The offset into @c buffer_ of the write location.
	std::atomic_uint32_t writePosition_{0};
	/// The offset into @c buffer_ of the read location.
	std::atomic_uint32_t readPosition_{0};

	static_assert(std::atomic_uint32_t::is_always_lock_free, "Lock-free std::atomic_uint32_t required");

};

} /* namespace SFB */
