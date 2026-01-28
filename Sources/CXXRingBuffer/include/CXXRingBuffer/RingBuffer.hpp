//
// SPDX-FileCopyrightText: 2014 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(__has_feature)
#if __has_feature(nullability)
#define RB_HAS_NULLABILITY 1
#endif
#endif

#if defined(RB_HAS_NULLABILITY)
#define RB_NONNULL _Nonnull
#define RB_NULLABLE _Nullable
#else
#define RB_NONNULL
#define RB_NULLABLE
#endif

namespace CXXRingBuffer {

template <typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

template <typename T> struct is_span_trait : std::false_type {};
template <typename T, std::size_t Extent> struct is_span_trait<std::span<T, Extent>> : std::true_type {};

template <typename T> inline constexpr bool is_span_v = is_span_trait<std::remove_cvref_t<T>>::value;

template <typename T>
concept SingleValue = std::is_trivially_copyable_v<T> && !std::is_pointer_v<std::remove_cvref_t<T>> &&
                      !is_span_v<std::remove_cvref_t<T>>;

/// A lock-free SPSC ring buffer.
///
/// This class is thread safe when used with a single producer and a single consumer.
///
/// This ring buffer performs raw byte copies; it does not provide serialization.
class RingBuffer final {
  public:
    /// Unsigned integer type.
    using SizeType = std::size_t;
    /// Atomic unsigned integer type.
    using AtomicSizeType = std::atomic<SizeType>;

    /// A write vector.
    using WriteVector = std::pair<std::span<unsigned char>, std::span<unsigned char>>;
    /// A read vector.
    using ReadVector = std::pair<std::span<const unsigned char>, std::span<const unsigned char>>;

    /// The minimum supported ring buffer capacity in bytes.
    static constexpr auto minCapacity = SizeType{2};
    /// The maximum supported ring buffer capacity in bytes.
    static constexpr auto maxCapacity = SizeType{1} << (std::numeric_limits<SizeType>::digits - 1);

    // MARK: Construction and Destruction

    /// Creates an empty ring buffer.
    /// @note ``allocate`` must be called before the object may be used.
    RingBuffer() noexcept = default;

    /// Creates a ring buffer with the specified minimum capacity.
    ///
    /// The actual ring buffer capacity will be the smallest integral power of two that is not less than the specified
    /// minimum capacity.
    /// @param minCapacity The desired minimum capacity in bytes.
    /// @throw std::bad_alloc if memory could not be allocated or std::invalid_argument if the buffer capacity is not
    /// supported.
    explicit RingBuffer(SizeType minCapacity);

    // This class is non-copyable
    RingBuffer(const RingBuffer &) = delete;

    /// Creates a ring buffer by moving the contents of another ring buffer.
    /// @note This method is not thread safe for the ring buffer being moved.
    /// @param other The ring buffer to move.
    RingBuffer(RingBuffer &&other) noexcept;

    // This class is non-assignable
    RingBuffer &operator=(const RingBuffer &) = delete;

    /// Moves the contents of another ring buffer into this ring buffer.
    /// @note This method is not thread safe.
    /// @param other The ring buffer to move.
    RingBuffer &operator=(RingBuffer &&other) noexcept;

    /// Destroys the ring buffer and releases all associated resources.
    ~RingBuffer() noexcept;

    // MARK: Buffer Management

    /// Allocates space for data.
    ///
    /// The actual ring buffer capacity will be the smallest integral power of two that is not less than the specified
    /// minimum capacity.
    /// @note This method is not thread safe.
    /// @param minCapacity The desired minimum capacity in bytes.
    /// @return true on success, false if memory could not be allocated or the buffer capacity is not supported.
    bool allocate(SizeType minCapacity) noexcept;

    /// Frees any space allocated for data.
    /// @note This method is not thread safe.
    void deallocate() noexcept;

    /// Returns true if the ring buffer has allocated space for data.
    [[nodiscard]] explicit operator bool() const noexcept;

    // MARK: Buffer Information

    /// Returns the capacity of the ring buffer.
    /// @note This method is safe to call from both producer and consumer.
    /// @return The ring buffer capacity in bytes.
    [[nodiscard]] SizeType capacity() const noexcept;

    // MARK: Buffer Usage

    /// Returns the amount of free space in the ring buffer.
    /// @note The result of this method is only accurate when called from the producer.
    /// @return The number of bytes of free space available for writing.
    [[nodiscard]] SizeType freeSpace() const noexcept;

    /// Returns true if the ring buffer is full.
    /// @note The result of this method is only accurate when called from the producer.
    /// @return true if the buffer is full.
    [[nodiscard]] bool isFull() const noexcept;

    /// Returns the amount of data in the ring buffer.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return The number of bytes available for reading.
    [[nodiscard]] SizeType availableBytes() const noexcept;

    /// Returns true if the ring buffer is empty.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return true if the buffer contains no data.
    [[nodiscard]] bool isEmpty() const noexcept;

    // MARK: Writing

    /// Writes data and advances the write position.
    /// @note This method is only safe to call from the producer.
    /// @param ptr An address containing the data to copy.
    /// @param itemSize The size of an individual item in bytes.
    /// @param itemCount The desired number of items to write.
    /// @param allowPartial Whether any items should be written if insufficient free space is available to write all
    /// items.
    /// @return The number of items actually written.
    SizeType write(const void *const RB_NONNULL ptr, SizeType itemSize, SizeType itemCount, bool allowPartial) noexcept;

    /// Writes items and advances the write position.
    /// @note This method is only safe to call from the producer.
    /// @tparam T The type to write.
    /// @param data A span containing the items to copy.
    /// @param allowPartial Whether any items should be written if insufficient free space is available to write all
    /// items.
    /// @return The number of items actually written.
    template <TriviallyCopyable T> SizeType write(std::span<const T> data, bool allowPartial = true) noexcept;

    /// Writes a value and advances the write position.
    /// @note This method is only safe to call from the producer.
    /// @tparam T The type to write.
    /// @param value The value to write.
    /// @return true if value was successfully written.
    bool write(SingleValue auto const &value) noexcept;

    template <typename T>
        requires std::is_pointer_v<std::remove_cvref_t<T>>
    bool write(T &&) = delete;

    /// Writes values and advances the write position.
    /// @note This method is only safe to call from the producer.
    /// @tparam Args The types to write.
    /// @param args The values to write.
    /// @return true if the values were successfully written.
    template <SingleValue... Args>
        requires(sizeof...(Args) > 1)
    bool writeAll(const Args &...args) noexcept;

    // MARK: Reading

    /// Reads data and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param ptr An address to receive the data.
    /// @param itemSize The size of an individual item in bytes.
    /// @param itemCount The desired number of items to read.
    /// @param allowPartial Whether any items should be read if the number of items available to read is less than
    /// itemCount.
    /// @return The number of items actually read.
    SizeType read(void *const RB_NONNULL ptr, SizeType itemSize, SizeType itemCount, bool allowPartial) noexcept;

    /// Reads items and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam T The type to read.
    /// @param buffer A span to receive the items.
    /// @param allowPartial Whether any items should be read if the number of items available to read is less than
    /// buffer.size().
    /// @return The number of items actually read.
    template <TriviallyCopyable T> SizeType read(std::span<T> buffer, bool allowPartial = true) noexcept;

    /// Reads a value and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam T The type to read.
    /// @param value The destination value.
    /// @return true on success, false otherwise.
    bool read(SingleValue auto &value) noexcept;

    template <typename T>
        requires std::is_pointer_v<std::remove_cvref_t<T>>
    bool read(T &&) = delete;

    /// Reads a value and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam T The type to read.
    /// @return A std::optional containing an instance of T if sufficient bytes were available for reading.
    /// @throw Any exceptions thrown by the default constructor of T.
    template <SingleValue T> std::optional<T> read() noexcept(std::is_nothrow_default_constructible_v<T>);

    /// Reads values and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam Args The types to read.
    /// @param args The destination values.
    /// @return true if the values were successfully read.
    template <SingleValue... Args>
        requires(sizeof...(Args) > 1)
    bool readAll(Args &...args) noexcept;

    /// Reads values and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam Args The types to read.
    /// @return A std::optional containing a std::tuple of the values if they were successfully read.
    /// @throw Any exceptions thrown by the default constructors of Args.
    template <SingleValue... Args>
        requires(sizeof...(Args) > 1)
    std::optional<std::tuple<Args...>> readAll() noexcept((std::is_nothrow_default_constructible_v<Args> && ...));

    // MARK: Peeking

    /// Reads data without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param ptr An address to receive the data.
    /// @param itemSize The size of an individual item in bytes.
    /// @param itemCount The desired number of items to read.
    /// @return True if the requested items were read, false otherwise.
    [[nodiscard]] bool peek(void *const RB_NONNULL ptr, SizeType itemSize, SizeType itemCount) const noexcept;

    /// Reads items without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam T The type to read.
    /// @param buffer A span to receive the data.
    /// @return True if the requested items were read, false otherwise.
    template <TriviallyCopyable T> [[nodiscard]] bool peek(std::span<T> buffer) const noexcept;

    /// Reads a value without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam T The type to read.
    /// @param value The destination value.
    /// @return true on success, false otherwise.
    [[nodiscard]] bool peek(SingleValue auto &value) const noexcept;

    template <typename T>
        requires std::is_pointer_v<std::remove_cvref_t<T>>
    bool peek(T &&) const = delete;

    /// Reads a value without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam T The type to read.
    /// @return A std::optional containing an instance of T if sufficient bytes were available for reading.
    /// @throw Any exceptions thrown by the default constructor of T.
    template <SingleValue T>
    [[nodiscard]] std::optional<T> peek() const noexcept(std::is_nothrow_default_constructible_v<T>);

    /// Reads values without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam Args The types to read.
    /// @param args The destination values.
    /// @return true if the values were successfully read.
    template <SingleValue... Args>
        requires(sizeof...(Args) > 1)
    [[nodiscard]] bool peekAll(Args &...args) const noexcept;

    /// Reads values without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam Args The types to read.
    /// @return A std::optional containing a std::tuple of the values if they were successfully read.
    /// @throw Any exceptions thrown by the default constructors of Args.
    template <SingleValue... Args>
        requires(sizeof...(Args) > 1)
    [[nodiscard]] std::optional<std::tuple<Args...>> peekAll() const
            noexcept((std::is_nothrow_default_constructible_v<Args> && ...));

    // MARK: Discarding Data

    /// Skips data and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param itemSize The size of an individual item in bytes.
    /// @param itemCount The desired number of items to skip.
    /// @param allowPartial Whether any items should be skipped if the number of items available to skip is less than
    /// itemCount.
    /// @return The number of items actually skipped.
    SizeType skip(SizeType itemSize, SizeType itemCount, bool allowPartial = true) noexcept;

    /// Skips items and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param itemCount The number of items to skip.
    /// @return true if the items were successfully skipped.
    template <SingleValue T> bool skip(SizeType itemCount = 1) noexcept;

    /// Advances the read position to the write position, emptying the buffer.
    /// @note This method is only safe to call from the consumer.
    /// @return The number of bytes discarded.
    SizeType drain() noexcept;

    // MARK: Advanced Writing and Reading

    /// Returns a write vector containing the current writable space.
    /// @note This method is only safe to call from the producer.
    /// @return A pair of spans containing the current writable space.
    [[nodiscard]] WriteVector writeVector() const noexcept;

    /// Finalizes a write transaction by writing staged data to the ring buffer.
    /// @warning The behavior is undefined if count is greater than the free space in the write vector.
    /// @note This method is only safe to call from the producer.
    /// @param count The number of bytes that were successfully written to the write vector.
    void commitWrite(SizeType count) noexcept;

    /// Returns a read vector containing the current readable data.
    /// @note This method is only safe to call from the consumer.
    /// @return A pair of spans containing the current readable data.
    [[nodiscard]] ReadVector readVector() const noexcept;

    /// Finalizes a read transaction by removing data from the front of the ring buffer.
    /// @warning The behavior is undefined if count is greater than the available data in the read vector.
    /// @note This method is only safe to call from the consumer.
    /// @param count The number of bytes that were successfully read from the read vector.
    void commitRead(SizeType count) noexcept;

  private:
    /// The memory buffer holding the data.
    void *RB_NULLABLE buffer_{nullptr};

    /// The capacity of buffer_ in bytes.
    SizeType capacity_{0};
    /// The capacity of buffer_ in bytes minus one.
    SizeType capacityMask_{0};

    /// The free-running write location.
    AtomicSizeType writePosition_{0};
    /// The free-running read location.
    AtomicSizeType readPosition_{0};

    static_assert(AtomicSizeType::is_always_lock_free, "Lock-free AtomicSizeType required");
};

// MARK: - Implementation -

// MARK: Buffer Management

inline RingBuffer::operator bool() const noexcept { return buffer_ != nullptr; }

// MARK: Buffer Information

inline auto RingBuffer::capacity() const noexcept -> SizeType { return capacity_; }

// MARK: Buffer Usage

inline auto RingBuffer::freeSpace() const noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    return capacity_ - (writePos - readPos);
}

inline bool RingBuffer::isFull() const noexcept {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    return (writePos - readPos) == capacity_;
}

inline auto RingBuffer::availableBytes() const noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    return writePos - readPos;
}

inline bool RingBuffer::isEmpty() const noexcept {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    return writePos == readPos;
}

// MARK: Writing

inline auto RingBuffer::write(const void *const RB_NONNULL ptr, SizeType itemSize, SizeType itemCount,
                              bool allowPartial) noexcept -> SizeType {
    if ((ptr == nullptr) || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]] {
        return 0;
    }

    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    const auto bytesUsed = writePos - readPos;
    const auto bytesFree = capacity_ - bytesUsed;
    const auto itemsFree = bytesFree / itemSize;

    if (itemsFree == 0 || (itemsFree < itemCount && !allowPartial)) {
        return 0;
    }

    const auto itemsToWrite = std::min(itemsFree, itemCount);
    const auto bytesToWrite = itemsToWrite * itemSize;
    auto *dst = static_cast<unsigned char *>(buffer_);
    const auto *src = static_cast<const unsigned char *>(ptr);
    const auto writeIndex = writePos & capacityMask_;
    const auto bytesToEnd = capacity_ - writeIndex;

    if (bytesToWrite <= bytesToEnd) [[likely]] {
        std::memcpy(dst + writeIndex, src, bytesToWrite);
    } else [[unlikely]] {
        std::memcpy(dst + writeIndex, src, bytesToEnd);
        std::memcpy(dst, src + bytesToEnd, bytesToWrite - bytesToEnd);
    }

    writePosition_.store(writePos + bytesToWrite, std::memory_order_release);
    return itemsToWrite;
}

template <TriviallyCopyable T>
inline auto RingBuffer::write(std::span<const T> data, bool allowPartial) noexcept -> SizeType {
    return write(data.data(), sizeof(T), data.size(), allowPartial);
}

inline bool RingBuffer::write(SingleValue auto const &value) noexcept {
    return write(static_cast<const void *>(std::addressof(value)), sizeof value, 1, false) == 1;
}

template <SingleValue... Args>
    requires(sizeof...(Args) > 1)
inline bool RingBuffer::writeAll(const Args &...args) noexcept {
    constexpr auto totalSize = (sizeof args + ...);
    auto [front, back] = writeVector();
    const auto frontSize = front.size();

    if (frontSize + back.size() < totalSize) {
        return false;
    }

    std::size_t cursor = 0;
    const auto writeArg = [&](const void *arg, std::size_t len) noexcept {
        const auto *src = static_cast<const unsigned char *>(arg);
        if (cursor + len <= frontSize) {
            std::memcpy(front.data() + cursor, src, len);
        } else if (cursor >= frontSize) {
            std::memcpy(back.data() + (cursor - frontSize), src, len);
        } else [[unlikely]] {
            const std::size_t toFront = frontSize - cursor;
            std::memcpy(front.data() + cursor, src, toFront);
            std::memcpy(back.data(), src + toFront, len - toFront);
        }
        cursor += len;
    };

    (writeArg(std::addressof(args), sizeof args), ...);

    commitWrite(totalSize);
    return true;
}

// MARK: Reading

inline auto RingBuffer::read(void *const RB_NONNULL ptr, SizeType itemSize, SizeType itemCount,
                             bool allowPartial) noexcept -> SizeType {
    if ((ptr == nullptr) || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]] {
        return 0;
    }

    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto bytesUsed = writePos - readPos;
    const auto itemsAvailable = bytesUsed / itemSize;

    if (itemsAvailable == 0 || (itemsAvailable < itemCount && !allowPartial)) {
        return 0;
    }

    const auto itemsToRead = std::min(itemsAvailable, itemCount);
    const auto bytesToRead = itemsToRead * itemSize;
    auto *dst = static_cast<unsigned char *>(ptr);
    const auto *src = static_cast<const unsigned char *>(buffer_);
    const auto readIndex = readPos & capacityMask_;
    const auto bytesToEnd = capacity_ - readIndex;

    if (bytesToRead <= bytesToEnd) [[likely]] {
        std::memcpy(dst, src + readIndex, bytesToRead);
    } else [[unlikely]] {
        std::memcpy(dst, src + readIndex, bytesToEnd);
        std::memcpy(dst + bytesToEnd, src, bytesToRead - bytesToEnd);
    }

    readPosition_.store(readPos + bytesToRead, std::memory_order_release);
    return itemsToRead;
}

template <TriviallyCopyable T>
inline auto RingBuffer::read(std::span<T> buffer, bool allowPartial) noexcept -> SizeType {
    return read(buffer.data(), sizeof(T), buffer.size(), allowPartial);
}

inline bool RingBuffer::read(SingleValue auto &value) noexcept {
    return read(std::addressof(value), sizeof value, 1, false) == 1;
}

template <SingleValue T>
inline auto RingBuffer::read() noexcept(std::is_nothrow_default_constructible_v<T>) -> std::optional<T> {
    if (std::optional<T> result; read(result.emplace())) {
        return result;
    }
    return std::nullopt;
}

template <SingleValue... Args>
    requires(sizeof...(Args) > 1)
inline bool RingBuffer::readAll(Args &...args) noexcept {
    if (!peekAll(args...)) {
        return false;
    }
    commitRead((sizeof args + ...));
    return true;
}

template <SingleValue... Args>
    requires(sizeof...(Args) > 1)
inline auto RingBuffer::readAll() noexcept((std::is_nothrow_default_constructible_v<Args> && ...))
        -> std::optional<std::tuple<Args...>> {
    auto result = peekAll<Args...>();
    if (!result) {
        return std::nullopt;
    }
    commitRead((sizeof(Args) + ...));
    return result;
}

// MARK: Peeking

inline bool RingBuffer::peek(void *const RB_NONNULL ptr, SizeType itemSize, SizeType itemCount) const noexcept {
    if ((ptr == nullptr) || itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]] {
        return false;
    }

    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto bytesUsed = writePos - readPos;
    const auto itemsAvailable = bytesUsed / itemSize;

    if (itemsAvailable < itemCount) {
        return false;
    }

    const auto bytesToPeek = itemCount * itemSize;
    auto *dst = static_cast<unsigned char *>(ptr);
    const auto *src = static_cast<const unsigned char *>(buffer_);
    const auto readIndex = readPos & capacityMask_;
    const auto bytesToEnd = capacity_ - readIndex;

    if (bytesToPeek <= bytesToEnd) [[likely]] {
        std::memcpy(dst, src + readIndex, bytesToPeek);
    } else [[unlikely]] {
        std::memcpy(dst, src + readIndex, bytesToEnd);
        std::memcpy(dst + bytesToEnd, src, bytesToPeek - bytesToEnd);
    }

    return true;
}

template <TriviallyCopyable T> inline bool RingBuffer::peek(std::span<T> buffer) const noexcept {
    return peek(buffer.data(), sizeof(T), buffer.size());
}

inline bool RingBuffer::peek(SingleValue auto &value) const noexcept {
    return peek(std::addressof(value), sizeof value, 1);
}

template <SingleValue T>
inline auto RingBuffer::peek() const noexcept(std::is_nothrow_default_constructible_v<T>) -> std::optional<T> {
    if (std::optional<T> result; peek(result.emplace())) {
        return result;
    }
    return std::nullopt;
}

template <SingleValue... Args>
    requires(sizeof...(Args) > 1)
inline bool RingBuffer::peekAll(Args &...args) const noexcept {
    constexpr auto totalSize = (sizeof args + ...);
    auto [front, back] = readVector();
    const auto frontSize = front.size();

    if (frontSize + back.size() < totalSize) {
        return false;
    }

    std::size_t cursor = 0;
    const auto readArg = [&](void *arg, std::size_t len) noexcept {
        auto *dst = static_cast<unsigned char *>(arg);
        if (cursor + len <= frontSize) {
            std::memcpy(dst, front.data() + cursor, len);
        } else if (cursor >= frontSize) {
            std::memcpy(dst, back.data() + (cursor - frontSize), len);
        } else [[unlikely]] {
            const std::size_t fromFront = frontSize - cursor;
            std::memcpy(dst, front.data() + cursor, fromFront);
            std::memcpy(dst + fromFront, back.data(), len - fromFront);
        }
        cursor += len;
    };

    (readArg(std::addressof(args), sizeof args), ...);
    return true;
}

template <SingleValue... Args>
    requires(sizeof...(Args) > 1)
inline auto RingBuffer::peekAll() const noexcept((std::is_nothrow_default_constructible_v<Args> && ...))
        -> std::optional<std::tuple<Args...>> {
    if (std::tuple<Args...> result; std::apply([&](Args &...args) noexcept { return peekAll(args...); }, result)) {
        return result;
    }
    return std::nullopt;
}

// MARK: Discarding Data

inline auto RingBuffer::skip(SizeType itemSize, SizeType itemCount, bool allowPartial) noexcept -> SizeType {
    if (itemSize == 0 || itemCount == 0 || capacity_ == 0) [[unlikely]] {
        return 0;
    }

    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto bytesUsed = writePos - readPos;
    const auto itemsAvailable = bytesUsed / itemSize;

    if (itemsAvailable == 0 || (itemsAvailable < itemCount && !allowPartial)) {
        return 0;
    }

    const auto itemsToSkip = std::min(itemsAvailable, itemCount);
    const auto bytesToSkip = itemsToSkip * itemSize;

    readPosition_.store(readPos + bytesToSkip, std::memory_order_release);
    return itemsToSkip;
}

template <SingleValue T> inline bool RingBuffer::skip(SizeType itemCount) noexcept {
    return skip(sizeof(T), itemCount, false) == itemCount;
}

inline auto RingBuffer::drain() noexcept -> SizeType {
    if (capacity_ == 0) [[unlikely]] {
        return 0;
    }

    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto bytesUsed = writePos - readPos;

    if (bytesUsed == 0) [[unlikely]] {
        return 0;
    }

    readPosition_.store(writePos, std::memory_order_release);
    return bytesUsed;
}

// MARK: Advanced Writing and Reading

inline auto RingBuffer::writeVector() const noexcept -> WriteVector {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    const auto bytesUsed = writePos - readPos;
    const auto bytesFree = capacity_ - bytesUsed;

    if (bytesFree == 0) [[unlikely]] {
        return {};
    }

    auto *dst = static_cast<unsigned char *>(buffer_);
    const auto writeIndex = writePos & capacityMask_;
    const auto bytesToEnd = capacity_ - writeIndex;

    if (bytesFree > bytesToEnd) [[unlikely]] {
        return {{dst + writeIndex, bytesToEnd}, {dst, bytesFree - bytesToEnd}};
    }
    return {{dst + writeIndex, bytesFree}, {}};
}

inline void RingBuffer::commitWrite(SizeType count) noexcept {
    assert(count <= freeSpace() && "Logic error: Write committing more than available free space");
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    writePosition_.store(writePos + count, std::memory_order_release);
}

inline auto RingBuffer::readVector() const noexcept -> ReadVector {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    const auto bytesUsed = writePos - readPos;

    if (bytesUsed == 0) [[unlikely]] {
        return {};
    }

    const auto *src = static_cast<const unsigned char *>(buffer_);
    const auto readIndex = readPos & capacityMask_;
    const auto bytesToEnd = capacity_ - readIndex;

    if (bytesUsed > bytesToEnd) [[unlikely]] {
        return {{src + readIndex, bytesToEnd}, {src, bytesUsed - bytesToEnd}};
    }
    return {{src + readIndex, bytesUsed}, {}};
}

inline void RingBuffer::commitRead(SizeType count) noexcept {
    assert(count <= availableBytes() && "Logic error: Read committing more than available data");
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    readPosition_.store(readPos + count, std::memory_order_release);
}

} /* namespace CXXRingBuffer */
