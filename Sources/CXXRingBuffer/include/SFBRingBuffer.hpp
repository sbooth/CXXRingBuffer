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

namespace SFB {

/// A generic ring buffer.
///
/// This class is thread safe when used from one reader thread and one writer thread (single producer, single consumer model).
class RingBuffer final
{

public:

#pragma mark Creation and Destruction

	/// Creates a new @c RingBuffer.
	/// @note @c Allocate() must be called before the object may be used.
	constexpr RingBuffer() noexcept = default;

	// This class is non-copyable
	RingBuffer(const RingBuffer&) = delete;

	/// Creates a new @c RingBuffer by moving the contents of @c other.
	/// @attention This method is not thread safe for @c other.
	/// @param other The @c RingBuffer to move
	RingBuffer(RingBuffer&& other) noexcept;

	// This class is non-assignable
	RingBuffer& operator=(const RingBuffer&) = delete;

	/// Moves the contents of @c other into this @c RingBuffer.
	/// @attention This method is not thread safe.
	/// @param other The @c RingBuffer to move
	RingBuffer& operator=(RingBuffer&& other) noexcept;

	/// Destroys the @c RingBuffer and releases all associated resources.
	~RingBuffer() noexcept;

#pragma mark Buffer Management

	/// Allocates space for data.
	/// @attention This method is not thread safe.
	/// @note Capacities from 2 to 2,147,483,648 (0x80000000) bytes are supported
	/// @param byteCount The desired capacity, in bytes
	/// @return @c true on success, @c false on error
	bool Allocate(uint32_t byteCount) noexcept;

	/// Frees the resources used by this @c RingBuffer.
	/// @attention This method is not thread safe.
	void Deallocate() noexcept;

	/// Resets this @c RingBuffer to its default state.
	/// @attention This method is not thread safe.
	void Reset() noexcept;

#pragma mark Buffer Information

	/// Returns the capacity of this RingBuffer in bytes.
	uint32_t CapacityBytes() const noexcept;

	/// Returns the number of bytes available for reading.
	uint32_t BytesAvailableToRead() const noexcept;

	/// Returns the free space available for writing in bytes.
	uint32_t BytesAvailableToWrite() const noexcept;

#pragma mark Reading and Writing Data

	/// Reads data from the @c RingBuffer and advances the read pointer.
	/// @param destinationBuffer An address to receive the data
	/// @param byteCount The desired number of bytes to read
	/// @param allowPartial Whether any bytes should be read if the number of bytes available for reading is less than @c byteCount
	/// @return The number of bytes actually read
	uint32_t Read(void * const _Nonnull destinationBuffer, uint32_t byteCount, bool allowPartial = true) noexcept;

	/// Reads data from the @c RingBuffer without advancing the read pointer.
	/// @param destinationBuffer An address to receive the data
	/// @param byteCount The desired number of bytes to read
	/// @param allowPartial Whether any bytes should be read if the number of bytes available for reading is less than @c byteCount
	/// @return The number of bytes actually read
	uint32_t Peek(void * const _Nonnull destinationBuffer, uint32_t byteCount, bool allowPartial = true) const noexcept;

	/// Writes data to the @c RingBuffer and advances the write pointer.
	/// @param sourceBuffer An address containing the data to copy
	/// @param byteCount The desired number of bytes to write
	/// @param allowPartial Whether any bytes should be written if the free space available for writing is less than @c byteCount
	/// @return The number of bytes actually written
	uint32_t Write(const void * const _Nonnull sourceBuffer, uint32_t byteCount, bool allowPartial = true) noexcept;

#pragma mark Reading and Writing Types

	/// Reads a value from the @c RingBuffer and advances the read pointer.
	/// @tparam T The type to read
	/// @param value The destination value
	/// @return @c true on success, @c false otherwise
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool ReadValue(T& value) noexcept
	{
		const auto size = static_cast<uint32_t>(sizeof(T));
		const auto bytesRead = Read(static_cast<void *>(&value), size, false);
		return bytesRead == size;
	}

	/// Reads values from the @c RingBuffer and advances the read pointer.
	/// @tparam Args The types to read
	/// @param args The destination values
	/// @return @c true if the values were successfully read
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...)
	bool ReadValues(Args&... args) noexcept
	{
		const auto totalSize = static_cast<uint32_t>((sizeof(args) + ...));

		const auto rvec = ReadVector();

		// Don't read anything if there is insufficient data
		if(rvec.first.mBufferSize + rvec.second.mBufferSize < totalSize)
			return false;

		uint32_t bytesRead = 0;

		([&]
		 {
			auto bytesRemaining = static_cast<uint32_t>(sizeof(args));

			// Read from rvec.first if data is available
			if(rvec.first.mBufferSize > bytesRead) {
				const auto n = std::min(bytesRemaining, rvec.first.mBufferSize - bytesRead);
				std::memcpy(static_cast<void *>(&args),
							reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(rvec.first.mBuffer) + bytesRead),
							n);
				bytesRemaining -= n;
				bytesRead += n;
			}
			// Read from rvec.second
			if(bytesRemaining > 0) {
				const auto n = bytesRemaining;
				std::memcpy(static_cast<void *>(&args),
							reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(rvec.second.mBuffer) + (bytesRead - rvec.first.mBufferSize)),
							n);
				bytesRead += n;
			}
		}(), ...);

		AdvanceReadPosition(bytesRead);

		return true;
	}

	/// Reads a value from the @c RingBuffer and advances the read pointer.
	/// @tparam T The type to read
	/// @return A @c std::optional containing an instance of @c T if sufficient bytes were available for reading
	template <typename T> requires std::is_default_constructible_v<T>
	std::optional<T> ReadValue() noexcept(std::is_nothrow_default_constructible_v<T>)
	{
		T value{};
		if(!ReadValue(value))
			return std::nullopt;
		return value;
	}

	/// Reads a value from the @c RingBuffer without advancing the read pointer.
	/// @tparam T The type to read
	/// @param value The destination value
	/// @return @c true on success, @c false otherwise
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool PeekValue(T& value) const noexcept
	{
		const auto size = static_cast<uint32_t>(sizeof(T));
		const auto bytesRead = Peek(static_cast<void *>(&value), size, false);
		return bytesRead == size;
	}

	/// Reads a value from the @c RingBuffer without advancing the read pointer.
	/// @tparam T The type to read
	/// @return A @c std::optional containing an instance of @c T if sufficient bytes were available for reading
	template <typename T> requires std::is_default_constructible_v<T>
	std::optional<T> PeekValue() const noexcept(std::is_nothrow_default_constructible_v<T>)
	{
		T value{};
		if(!PeekValue(value))
			return std::nullopt;
		return value;
	}

	/// Writes a value to the @c RingBuffer and advances the write pointer.
	/// @tparam T The type to write
	/// @param value The value to write
	/// @return @c true if @c value was successfully written
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool WriteValue(const T& value) noexcept
	{
		const auto size = static_cast<uint32_t>(sizeof(T));
		const auto bytesWritten = Write(static_cast<const void *>(&value), size, false);
		return bytesWritten == size;
	}

	/// Writes values to the @c RingBuffer and advances the write pointer.
	/// @tparam Args The types to write
	/// @param args The values to write
	/// @return @c true if the values were successfully written
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...)
	bool WriteValues(const Args&... args) noexcept
	{
		const auto totalSize = static_cast<uint32_t>((sizeof(args) + ...));

		auto wvec = WriteVector();

		// Don't write anything if there is insufficient space
		if(wvec.first.mBufferCapacity + wvec.second.mBufferCapacity < totalSize)
			return false;

		uint32_t bytesWritten = 0;

		([&]
		 {
			auto bytesRemaining = static_cast<uint32_t>(sizeof(args));

			// Write to wvec.first if space is available
			if(wvec.first.mBufferCapacity > bytesWritten) {
				const auto n = std::min(bytesRemaining, wvec.first.mBufferCapacity - bytesWritten);
				std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(wvec.first.mBuffer) + bytesWritten),
							static_cast<const void *>(&args),
							n);
				bytesRemaining -= n;
				bytesWritten += n;
			}
			// Write to wvec.second
			if(bytesRemaining > 0) {
				const auto n = bytesRemaining;
				std::memcpy(reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(wvec.second.mBuffer) + (bytesWritten - wvec.first.mBufferCapacity)),
							static_cast<const void *>(&args),
							n);
				bytesWritten += n;
			}
		}(), ...);

		AdvanceWritePosition(bytesWritten);

		return true;
	}

#pragma mark Advanced Reading and Writing

	/// Advances the read position by the specified number of bytes.
	void AdvanceReadPosition(uint32_t byteCount) noexcept;

	/// Advances the write position by the specified number of bytes.
	void AdvanceWritePosition(uint32_t byteCount) noexcept;


	/// A read-only memory buffer
	struct ReadBuffer {
		/// The memory buffer location
		const void * const _Nullable mBuffer{nullptr};
		/// The number of bytes of valid data in @c mBuffer
		const uint32_t mBufferSize{0};

	private:
		friend class RingBuffer;

		/// Construct an empty @c ReadBuffer
		ReadBuffer() noexcept = default;

		/// Construct a @c ReadBuffer for the specified location and size
		/// @param buffer The memory buffer location
		/// @param bufferSize The number of bytes of valid data in @c buffer
		ReadBuffer(const void * const _Nullable buffer, uint32_t bufferSize) noexcept
		: mBuffer{buffer}, mBufferSize{bufferSize}
		{}
	};

	/// A pair of @c ReadBuffer objects
	using ReadBufferPair = std::pair<const ReadBuffer, const ReadBuffer>;

	/// Returns the read vector containing the current readable data.
	const ReadBufferPair ReadVector() const noexcept;


	/// A write-only memory buffer
	struct WriteBuffer {
		/// The memory buffer location
		void * const _Nullable mBuffer{nullptr};
		/// The capacity of @c mBuffer in bytes
		const uint32_t mBufferCapacity{0};

	private:
		friend class RingBuffer;

		/// Construct an empty @c WriteBuffer
		WriteBuffer() noexcept = default;

		/// Construct a @c WriteBuffer for the specified location and capacity
		/// @param buffer The memory buffer location
		/// @param bufferCapacity The capacity of @c buffer in bytes
		WriteBuffer(void * const _Nullable buffer, uint32_t bufferCapacity) noexcept
		: mBuffer{buffer}, mBufferCapacity{bufferCapacity}
		{}
	};

	/// A pair of @c WriteBuffer objects
	using WriteBufferPair = std::pair<const WriteBuffer, const WriteBuffer>;

	/// Returns the write vector containing the current writable space.
	const WriteBufferPair WriteVector() const noexcept;

private:

	/// The memory buffer holding the data
	void * _Nullable mBuffer{nullptr};

	/// The capacity of @c mBuffer in bytes
	uint32_t mCapacityBytes{0};
	/// The capacity of @c mBuffer in bytes minus one
	uint32_t mCapacityBytesMask{0};

	/// The offset into @c mBuffer of the write location
	std::atomic_uint32_t mWritePosition{0};
	/// The offset into @c mBuffer of the read location
	std::atomic_uint32_t mReadPosition{0};

	static_assert(std::atomic_uint32_t::is_always_lock_free, "Lock-free std::atomic_uint32_t required");

};

} /* namespace SFB */
