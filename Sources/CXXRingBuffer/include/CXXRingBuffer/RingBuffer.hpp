//
// Copyright © 2014-2025 Stephen F. Booth
// Part of https://github.com/sbooth/CXXRingBuffer
// MIT license
//

#pragma once

#import <algorithm>
#import <atomic>
#import <cstddef>
#import <cstring>
#import <limits>
#import <optional>
#import <span>
#import <type_traits>
#import <utility>

namespace CXXRingBuffer {

/// A lock-free SPSC ring buffer.
///
/// This class is thread safe when used from one reader thread and one writer thread.
class RingBuffer final {
public:
	/// Unsigned integer type.
	using size_type = std::size_t;
	/// A write vector.
	using write_vector = std::pair<std::span<uint8_t>, std::span<uint8_t>>;
	/// A read vector.
	using read_vector = std::pair<std::span<const uint8_t>, std::span<const uint8_t>>;

	/// The minimum supported ring buffer size in bytes.
	static constexpr size_type min_buffer_size = size_type{2};
	/// The maximum supported ring buffer size in bytes.
	static constexpr size_type max_buffer_size = size_type{1} << (std::numeric_limits<size_type>::digits - 1);

	// MARK: Creation and Destruction

	/// Creates an empty ring buffer.
	/// @note ``Allocate`` must be called before the object may be used.
	RingBuffer() noexcept = default;

	/// Creates a ring buffer with the specified buffer size.
	/// @note The usable ring buffer capacity will be one less than the smallest integral power of two that is not less than the specified size.
	/// @param size The desired buffer size, in bytes.
	/// @throw std::bad_alloc if memory could not be allocated or std::invalid_argument if the buffer size is not supported.
	explicit RingBuffer(size_type size);

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
	/// @note This method is not thread safe.
	/// @note The usable ring buffer capacity will be one less than the smallest integral power of two that is not less than the specified size.
	/// @param size The desired buffer size, in bytes.
	/// @return true on success, false if memory could not be allocated or the buffer size is not supported.
	bool Allocate(size_type size) noexcept;

	/// Frees any space allocated for data.
	/// @note This method is not thread safe.
	void Deallocate() noexcept;

	/// Resets the read and write positions to their default state, emptying the buffer.
	/// @note This method is not thread safe.
	void Reset() noexcept;

	// MARK: Buffer Information

	/// Returns the usable capacity of the ring buffer.
	/// @return The usable ring buffer capacity in bytes.
	size_type Capacity() const noexcept;

	/// Returns the amount of free space in the buffer.
	/// @return The number of bytes of free space available for writing.
	size_type FreeSpace() const noexcept;

	/// Returns the amount of data in the buffer.
	/// @return The number of bytes available for reading.
	size_type AvailableBytes() const noexcept;

	/// Returns true if the ring buffer is empty.
	bool IsEmpty() const noexcept;

	// MARK: Writing and Reading Data

	/// Writes data and advances the write position.
	/// @param ptr An address containing the data to copy.
	/// @param itemSize The size of an individual item in bytes.
	/// @param itemCount The desired number of items to write.
	/// @param allowPartial Whether any items should be written if insufficient free space is available to write all items.
	/// @return The number of items actually written.
	size_type Write(const void * const _Nonnull ptr, size_type itemSize, size_type itemCount, bool allowPartial) noexcept;

	/// Reads data and advances the read position.
	/// @param ptr An address to receive the data.
	/// @param itemSize The size of an individual item in bytes.
	/// @param itemCount The desired number of items to read.
	/// @param allowPartial Whether any items should be read if the number of items available for reading is less than count.
	/// @return The number of items actually read.
	size_type Read(void * const _Nonnull ptr, size_type itemSize, size_type itemCount, bool allowPartial) noexcept;

	/// Reads data without advancing the read position.
	/// @param ptr An address to receive the data.
	/// @param itemSize The size of an individual item in bytes.
	/// @param itemCount The desired number of items to read.
	/// @return True if the requested items were read, false otherwise.
	bool Peek(void * const _Nonnull ptr, size_type itemSize, size_type itemCount) const noexcept;

	// MARK: Writing and Reading Spans

	/// Writes items and advances the write position.
	/// @param data A span containing the items to copy.
	/// @param allowPartial Whether any items should be written if insufficient free space is available to write all items.
	/// @return The number of items actually written.
	template <typename T> requires std::is_trivially_copyable_v<T>
	size_type Write(std::span<const T> data, bool allowPartial = true) noexcept
	{
		return Write(data.data(), sizeof(T), data.size(), allowPartial);
	}

	/// Reads items and advances the read position.
	/// @param buffer A span to receive the items.
	/// @param allowPartial Whether any items should be read if the number of items available for reading is less than buffer.size().
	/// @return The number of items actually read.
	template <typename T> requires std::is_trivially_copyable_v<T>
	size_type Read(std::span<T> buffer, bool allowPartial = true) noexcept
	{
		return Read(buffer.data(), sizeof(T), buffer.size(), allowPartial);
	}

	/// Reads items without advancing the read position.
	/// @param buffer A span to receive the data.
	/// @return True if the requested items were read, false otherwise.
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool Peek(std::span<T> buffer) noexcept
	{
		return Peek(buffer.data(), sizeof(T), buffer.size());
	}

	// MARK: Writing and Reading Single Values

	/// Writes a value and advances the write position.
	/// @tparam T The type to write.
	/// @param value The value to write.
	/// @return true if value was successfully written.
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool WriteValue(const T& value) noexcept
	{
		const auto nitems = Write(static_cast<const void *>(&value), sizeof(T), 1, false);
		return nitems == 1;
	}

	/// Reads a value and advances the read position.
	/// @tparam T The type to read.
	/// @param value The destination value.
	/// @return true on success, false otherwise.
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool ReadValue(T& value) noexcept
	{
		const auto nitems = Read(static_cast<void *>(&value), sizeof(T), 1, false);
		return nitems == 1;
	}

	/// Reads a value and advances the read position.
	/// @tparam T The type to read.
	/// @return A std::optional containing an instance of T if sufficient bytes were available for reading.
	/// @throw Any exceptions thrown by the default constructor of T.
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
	/// @return true on success, false otherwise.
	template <typename T> requires std::is_trivially_copyable_v<T>
	bool PeekValue(T& value) const noexcept
	{
		return Peek(static_cast<void *>(&value), sizeof(T), 1);
	}

	/// Reads a value without advancing the read position.
	/// @tparam T The type to read.
	/// @return A std::optional containing an instance of T if sufficient bytes were available for reading.
	/// @throw Any exceptions thrown by the default constructor of T.
	template <typename T> requires std::is_default_constructible_v<T>
	std::optional<T> PeekValue() const noexcept(std::is_nothrow_default_constructible_v<T>)
	{
		T value{};
		if(!PeekValue(value))
			return std::nullopt;
		return value;
	}

	// MARK: Writing and Reading Multiple Values

	/// Writes values and advances the write position.
	/// @tparam Args The types to write.
	/// @param args The values to write.
	/// @return true if the values were successfully written.
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...)
	bool WriteValues(const Args&... args) noexcept
	{
		const auto totalSize = (sizeof args + ...);
		auto wvec = GetWriteVector();
		if(wvec.first.size() + wvec.second.size() < totalSize)
			return false;

		size_type bytesWritten = 0;

		([&] {
			auto bytesRemaining = sizeof args;

			// Write to wvec.first if space is available
			if(wvec.first.size() > bytesWritten) {
				const auto n = std::min(bytesRemaining, wvec.first.size() - bytesWritten);
				std::memcpy(wvec.first.data() + bytesWritten,
							static_cast<const void *>(&args),
							n);
				bytesRemaining -= n;
				bytesWritten += n;
			}
			// Write to wvec.second
			if(bytesRemaining > 0) {
				const auto n = bytesRemaining;
				std::memcpy(wvec.second.data() + (bytesWritten - wvec.first.size()),
							static_cast<const void *>(&args),
							n);
				bytesWritten += n;
			}
		}(), ...);

		CommitWrite(bytesWritten);
		return true;
	}

	/// Reads values and advances the read position.
	/// @tparam Args The types to read.
	/// @param args The destination values.
	/// @return true if the values were successfully read.
	template <typename... Args> requires (std::is_trivially_copyable_v<Args> && ...)
	bool ReadValues(Args&... args) noexcept
	{
		const auto totalSize = (sizeof args + ...);
		const auto rvec = GetReadVector();
		if(rvec.first.size() + rvec.second.size() < totalSize)
			return false;

		size_type bytesRead = 0;

		([&] {
			auto bytesRemaining = sizeof args;

			// Read from rvec.first if data is available
			if(rvec.first.size() > bytesRead) {
				const auto n = std::min(bytesRemaining, rvec.first.size() - bytesRead);
				std::memcpy(static_cast<void *>(&args),
							rvec.first.data() + bytesRead,
							n);
				bytesRemaining -= n;
				bytesRead += n;
			}
			// Read from rvec.second
			if(bytesRemaining > 0) {
				const auto n = bytesRemaining;
				std::memcpy(static_cast<void *>(&args),
							rvec.second.data() + (bytesRead - rvec.first.size()),
							n);
				bytesRead += n;
			}
		}(), ...);

		CommitRead(bytesRead);
		return true;
	}

	// MARK: Advanced Writing and Reading

	/// Returns a write vector containing the current writable space.
	/// @return A pair of spans containing the current writable space.
	write_vector GetWriteVector() const noexcept;

	/// Returns a read vector containing the current readable data.
	/// @return A pair of spans containing the current readable data.
	read_vector GetReadVector() const noexcept;

	/// Finalizes a write transaction by writing staged data to the ring buffer.
	/// @param count The number of bytes that were successfully written to the write vector.
	void CommitWrite(size_type count) noexcept;

	/// Finalizes a read transaction by removing data from the front of the ring buffer.
	/// @param count The number of bytes that were successfully read from the read vector.
	void CommitRead(size_type count) noexcept;

private:
	/// The memory buffer holding the data.
	void * _Nullable buffer_{nullptr};

	/// The capacity of buffer_ in bytes.
	size_type capacity_{0};
	/// The capacity of buffer_ in bytes minus one.
	size_type capacityMask_{0};

	/// The offset into buffer_ of the write location.
	alignas(std::hardware_destructive_interference_size)
	std::atomic<size_type> writePosition_{0};
	/// The offset into buffer_ of the read location.
	alignas(std::hardware_destructive_interference_size)
	std::atomic<size_type> readPosition_{0};

	static_assert(std::atomic<size_type>::is_always_lock_free, "Lock-free std::atomic<size_type> required");
	static_assert(std::hardware_destructive_interference_size >= alignof(std::atomic<size_type>));
};

} /* namespace CXXRingBuffer */
