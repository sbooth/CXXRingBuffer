//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXRingBuffer
//

#ifndef MPSC_RING_BUFFER_HPP
#define MPSC_RING_BUFFER_HPP

#ifdef __has_feature
#if __has_feature(nullability)
#define RB_HAS_NULLABILITY
#endif
#endif

#ifdef RB_HAS_NULLABILITY
#define RB_NONNULL _Nonnull
#define RB_NULLABLE _Nullable
#else
#define RB_NONNULL
#define RB_NULLABLE
#endif

#include <algorithm>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace mpsc {

template <std::size_t N>
concept ValidPowerOfTwo = (N >= 2) && std::has_single_bit(N);

template <typename T>
concept ByteCopyable =
        std::is_object_v<std::remove_cvref_t<T>> && std::is_trivially_copyable_v<std::remove_cvref_t<T>> &&
        std::is_standard_layout_v<std::remove_cvref_t<T>> && !std::is_pointer_v<std::remove_cvref_t<T>>;

template <typename T>
concept ValueLike = ByteCopyable<T> && !std::ranges::range<std::remove_cvref_t<T>>;

/// A lock-free MPSC ring buffer.
///
/// This class is thread safe when used with multiple producers and a single consumer.
///
/// This ring buffer performs raw byte copies; it does not provide serialization.
template <std::size_t N>
    requires ValidPowerOfTwo<N>
class RingBuffer final {
  public:
    /// Unsigned integer type.
    using SizeType = std::size_t;
    /// Atomic unsigned integer type.
    using AtomicSizeType = std::atomic<SizeType>;

    /// The minimum supported slot count.
    static constexpr auto minSlots = SizeType{2};
    /// The maximum supported slot count.
    static constexpr auto maxSlots = SizeType{1} << (std::numeric_limits<SizeType>::digits - 1);

    // MARK: Construction and Destruction

    /// Creates an empty ring buffer.
    /// @note ``allocate`` must be called before the object may be used.
    RingBuffer() noexcept = default;

    /// Creates a ring buffer with the specified minimum slot count.
    ///
    /// The actual slot count will be the smallest integral power of two that is not less than the specified
    /// minimum slot count.
    /// @param minSlots The desired minimum slot count.
    /// @throw std::bad_alloc if memory could not be allocated or std::invalid_argument if the slot count is not
    /// supported.
    explicit RingBuffer(SizeType minSlots);

    RingBuffer(const RingBuffer &) = delete;
    RingBuffer &operator=(const RingBuffer &) = delete;

    /// Creates a ring buffer by moving the contents of another ring buffer.
    /// @note This method is not thread safe for the ring buffer being moved.
    /// @param other The ring buffer to move.
    RingBuffer(RingBuffer &&other) noexcept;

    /// Moves the contents of another ring buffer into this ring buffer.
    /// @note This method is not thread safe.
    /// @param other The ring buffer to move.
    RingBuffer &operator=(RingBuffer &&other) noexcept;

    /// Destroys the ring buffer and releases all associated resources.
    ~RingBuffer() noexcept = default;

    // MARK: Buffer Management

    /// Allocates space for data.
    ///
    /// The actual slot count will be the smallest integral power of two that is not less than the specified
    /// minimum slot count.
    /// @note This method is not thread safe.
    /// @param minSlots The desired minimum slot count.
    /// @return true on success, false if memory could not be allocated or the slot count is not supported.
    bool allocate(SizeType minSlots) noexcept [[clang::allocating]];

    /// Frees any space allocated for data.
    /// @note This method is not thread safe.
    void deallocate() noexcept;

    /// Returns true if the ring buffer has allocated space for data.
    [[nodiscard]] explicit operator bool() const noexcept [[clang::nonblocking]];

    // MARK: Buffer Information

    /// Returns the slot count of the ring buffer.
    /// @note This method is safe to call from both producer and consumer.
    /// @return The ring buffer slot count.
    [[nodiscard]] SizeType slotCount() const noexcept [[clang::nonblocking]];

    /// Returns the capacity of a single slot in the ring buffer.
    /// @note This method is safe to call from both producer and consumer.
    /// @return The capacity of a single slot in the ring buffer in bytes.
    [[nodiscard]] std::size_t slotCapacity() const noexcept [[clang::nonblocking]];

    // MARK: Buffer Usage

    /// Returns the number of empty slots in the ring buffer.
    /// @note The result of this method is only valid when called from a producer.
    /// @note The returned value is a transient snapshot and may become stale immediately after return.
    /// @return The number of empty slots available for writing.
    [[nodiscard]] SizeType emptySlots() const noexcept [[clang::nonblocking]];

    /// Returns true if the ring buffer is full.
    /// @note The result of this method is only valid when called from a producer.
    /// @note The returned value is a transient snapshot and may become stale immediately after return.
    /// @return true if the all slots in the buffer are occupied.
    [[nodiscard]] bool isFull() const noexcept [[clang::nonblocking]];

    /// Returns the number of occupied slots in the ring buffer.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return The number of occupied slots available for reading.
    [[nodiscard]] SizeType occupiedSlots() const noexcept [[clang::nonblocking]];

    /// Returns true if the ring buffer is empty.
    /// @note The result of this method is only accurate when called from the consumer.
    /// @return true if all slots in the buffer are empty.
    [[nodiscard]] bool isEmpty() const noexcept [[clang::nonblocking]];

    // MARK: Writing

    /// Writes data and advances the write position.
    /// @note This method is only safe to call from a producer.
    /// @param ptr An address containing the data to copy.
    /// @param size The number of bytes to copy.
    /// @return true if the data was successfully written.
    bool write(const void *RB_NONNULL ptr, SizeType size) noexcept [[clang::nonblocking]];

    /// Writes data and advances the write position.
    /// @note This method is only safe to call from a producer.
    /// @param data A span containing the data to copy.
    /// @return true if the data was successfully written.
    bool write(std::span<const unsigned char> data) noexcept [[clang::nonblocking]];

    /// Writes values and advances the write position.
    /// @note This method is only safe to call from a producer.
    /// @tparam Args The types to write.
    /// @param args The values to write.
    /// @return true if the values were successfully written.
    template <ValueLike... Args>
        requires(sizeof...(Args) > 0)
    bool writeValues(const Args &...args) noexcept [[clang::nonblocking]];

    // MARK: Reading

    /// Reads data and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param ptr An address to receive the data.
    /// @param capacity The maximum number of bytes to copy.
    /// @param written On return, the number of bytes read.
    /// @return true if data was successfully read.
    bool read(void *RB_NONNULL ptr, SizeType capacity, SizeType &written) noexcept [[clang::nonblocking]];

    /// Reads data and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param buffer A span to receive the data.
    /// @param written On return, the number of bytes read.
    /// @return true if data was successfully read.
    bool read(std::span<unsigned char> buffer, SizeType &written) noexcept [[clang::nonblocking]];

    /// Reads values and advances the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam Args The types to read.
    /// @param args The destination values.
    /// @return true if the values were successfully read.
    template <ValueLike... Args>
        requires(sizeof...(Args) > 0) && (std::assignable_from<Args&, Args> && ...)
    bool readValues(Args &...args) noexcept [[clang::nonblocking]];

    // MARK: Peeking

    /// Reads data without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param ptr An address to receive the data.
    /// @param capacity The maximum number of bytes to copy.
    /// @param written On return, the number of bytes read.
    /// @return true if data was successfully read.
    [[nodiscard]] bool peek(void *RB_NONNULL ptr, SizeType capacity, SizeType &written) const noexcept
            [[clang::nonblocking]];

    /// Reads data without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @param buffer A span to receive the data.
    /// @param written On return, the number of bytes read.
    /// @return true if data was successfully read.
    [[nodiscard]] bool peek(std::span<unsigned char> buffer, SizeType &written) const noexcept [[clang::nonblocking]];

    /// Reads values without advancing the read position.
    /// @note This method is only safe to call from the consumer.
    /// @tparam Args The types to read.
    /// @param args The destination values.
    /// @return true if the values were successfully read.
    template <ValueLike... Args>
        requires(sizeof...(Args) > 0) && (std::assignable_from<Args&, Args> && ...)
    [[nodiscard]] bool peekValues(Args &...args) const noexcept [[clang::nonblocking]];

  private:
    /// A ring buffer slot.
    struct Slot {
        /// The slot's generation.
        SizeType generation_{0};
        /// The number of valid bytes in data_
        SizeType dataSize_{0};
        /// The slot data.
        unsigned char data_[N];

        static_assert(std::atomic_ref<SizeType>::is_always_lock_free, "Lock-free std::atomic_ref<SizeType> required");
    };

    /// The ring buffer slots.
    std::unique_ptr<Slot[]> slots_;

    /// The number of slots in slots_.
    SizeType slotCount_{0};
    /// The number of slots in slots_ minus one.
    SizeType slotCountMask_{0};

    /// The free-running write location.
    AtomicSizeType writePosition_{0};
    /// The free-running read location.
    AtomicSizeType readPosition_{0};

    static_assert(AtomicSizeType::is_always_lock_free, "Lock-free AtomicSizeType required");

    // MARK: Helpers

    /// Claims a writable slot if available, and writes data using a callable.
    /// @tparam Writer The type of the callable object.
    /// @param writer A callable performing the write.
    /// @return true if a writable slot was claimed.
    template <typename Writer>
        requires std::invocable<Writer, Slot &> && std::is_nothrow_invocable_v<Writer, Slot &>
    bool writeToSlot(Writer &&writer) noexcept;

    /// Reads from the readable slot using a callable, optionally advancing the read position.
    /// @tparam Consume true if the read position should be advanced.
    /// @tparam Reader The type of the callable object.
    /// @param reader A callable performing the read.
    /// @return true if data was successfully read.
    template <bool Consume, typename Reader>
        requires std::invocable<Reader, std::span<const unsigned char>> &&
                 std::is_nothrow_invocable_v<Reader, std::span<const unsigned char>>
    bool readFromSlot(Reader &&reader) noexcept;

    /// Reads from the readable slot using a callable without advancing the read position.
    /// @tparam Reader The type of the callable object.
    /// @param reader A callable performing the read.
    /// @return true if data was successfully read.
    template <typename Reader>
        requires std::invocable<Reader, std::span<const unsigned char>> &&
                 std::is_nothrow_invocable_v<Reader, std::span<const unsigned char>>
    bool peekFromSlot(Reader &&reader) const noexcept;
};

// MARK: - Implementation -

// MARK: Construction and Destruction

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline RingBuffer<N>::RingBuffer(SizeType minSlots) {
    if (minSlots < RingBuffer::minSlots || minSlots > RingBuffer::maxSlots) [[unlikely]] {
        throw std::invalid_argument("slot count out of range");
    }
    if (!allocate(minSlots)) [[unlikely]] {
        throw std::bad_alloc();
    }
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline RingBuffer<N>::RingBuffer(RingBuffer &&other) noexcept
    : slots_{std::exchange(other.slots_, nullptr)}, slotCount_{std::exchange(other.slotCount_, 0)},
      slotCountMask_{std::exchange(other.slotCountMask_, 0)},
      writePosition_{other.writePosition_.exchange(0, std::memory_order_relaxed)},
      readPosition_{other.readPosition_.exchange(0, std::memory_order_relaxed)} {}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto RingBuffer<N>::operator=(RingBuffer &&other) noexcept -> RingBuffer & {
    if (this != &other) [[likely]] {
        slots_ = std::exchange(other.slots_, nullptr);
        slotCount_ = std::exchange(other.slotCount_, 0);
        slotCountMask_ = std::exchange(other.slotCountMask_, 0);

        writePosition_.store(other.writePosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
        readPosition_.store(other.readPosition_.exchange(0, std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

// MARK: Buffer Management

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool RingBuffer<N>::allocate(SizeType minSlots) noexcept {
    if (minSlots < RingBuffer::minSlots || minSlots > RingBuffer::maxSlots) [[unlikely]] {
        return false;
    }

    deallocate();

    const auto slotCount = std::bit_ceil(minSlots);

    try {
        slots_ = std::make_unique_for_overwrite<Slot[]>(slotCount);
    } catch (...) {
        return false;
    }

    for (SizeType i = 0; i < slotCount; ++i) {
        slots_[i].generation_ = i;
        slots_[i].dataSize_ = 0;
    }

    slotCount_ = slotCount;
    slotCountMask_ = slotCount - 1;

    writePosition_.store(0, std::memory_order_relaxed);
    readPosition_.store(0, std::memory_order_relaxed);

    return true;
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline void RingBuffer<N>::deallocate() noexcept {
    if (slots_) [[likely]] {
        slots_.reset();

        slotCount_ = 0;
        slotCountMask_ = 0;

        writePosition_.store(0, std::memory_order_relaxed);
        readPosition_.store(0, std::memory_order_relaxed);
    }
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline RingBuffer<N>::operator bool() const noexcept {
    return static_cast<bool>(slots_);
}

// MARK: Buffer Information

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto RingBuffer<N>::slotCount() const noexcept -> SizeType {
    return slotCount_;
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline std::size_t RingBuffer<N>::slotCapacity() const noexcept {
    return N;
}

// MARK: Buffer Usage

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto RingBuffer<N>::emptySlots() const noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    return slotCount_ - (writePos - readPos);
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool RingBuffer<N>::isFull() const noexcept {
    const auto writePos = writePosition_.load(std::memory_order_relaxed);
    const auto readPos = readPosition_.load(std::memory_order_acquire);
    return (writePos - readPos) == slotCount_;
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline auto RingBuffer<N>::occupiedSlots() const noexcept -> SizeType {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    return writePos - readPos;
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool RingBuffer<N>::isEmpty() const noexcept {
    const auto writePos = writePosition_.load(std::memory_order_acquire);
    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    return writePos == readPos;
}

// MARK: Writing

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool RingBuffer<N>::write(const void *RB_NONNULL ptr, SizeType size) noexcept {
    if (ptr == nullptr || size == 0 || size > N || slotCount_ == 0) [[unlikely]] {
        return false;
    }

    return writeToSlot([ptr, size](Slot &slot) noexcept {
        std::memcpy(slot.data_, ptr, size);
        slot.dataSize_ = size;
    });
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool RingBuffer<N>::write(std::span<const unsigned char> data) noexcept {
    return write(data.data(), data.size());
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
template <ValueLike... Args>
    requires(sizeof...(Args) > 0)
inline bool RingBuffer<N>::writeValues(const Args &...args) noexcept {
    constexpr auto totalSize = (sizeof(Args) + ...);
    if (totalSize > N || slotCount_ == 0) [[unlikely]] {
        return false;
    }

    return writeToSlot([&](Slot &slot) noexcept {
        std::size_t cursor = 0;
        const auto writeArg = [&](auto &arg) noexcept {
            constexpr auto size = sizeof(arg);
            std::memcpy(slot.data_ + cursor, std::addressof(arg), size);
            cursor += size;
        };
        (writeArg(args), ...);

        slot.dataSize_ = totalSize;
    });
}

// MARK: Reading

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool RingBuffer<N>::read(void *RB_NONNULL ptr, SizeType capacity, SizeType &written) noexcept {
    if (ptr == nullptr || capacity == 0 || slotCount_ == 0) [[unlikely]] {
        written = 0;
        return false;
    }

    return readFromSlot<true>([&](std::span<const unsigned char> data) noexcept -> bool {
        if (data.size() > capacity) {
            written = 0;
            return false;
        }

        std::memcpy(ptr, data.data(), data.size());
        written = data.size();
        return true;
    });
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool RingBuffer<N>::read(std::span<unsigned char> buffer, SizeType &written) noexcept {
    return read(buffer.data(), buffer.size(), written);
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
template <ValueLike... Args>
    requires(sizeof...(Args) > 0) && (std::assignable_from<Args&, Args> && ...)
inline bool RingBuffer<N>::readValues(Args &...args) noexcept {
    constexpr auto totalSize = (sizeof(Args) + ...);
    if (totalSize > N || slotCount_ == 0) [[unlikely]] {
        return false;
    }

    return readFromSlot<true>([&](std::span<const unsigned char> data) noexcept -> bool {
        if (data.size() < totalSize) {
            return false;
        }

        std::size_t cursor = 0;
        const auto readArg = [&](auto &arg) noexcept {
            constexpr auto size = sizeof(arg);
            std::memcpy(std::addressof(arg), data.data() + cursor, size);
            cursor += size;
        };
        (readArg(args), ...);

        return true;
    });
}

// MARK: Peeking

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool RingBuffer<N>::peek(void *RB_NONNULL ptr, SizeType capacity, SizeType &written) const noexcept {
    if (ptr == nullptr || capacity == 0 || slotCount_ == 0) [[unlikely]] {
        written = 0;
        return false;
    }

    return peekFromSlot([&](std::span<const unsigned char> data) noexcept -> bool {
        if (data.size() > capacity) {
            written = 0;
            return false;
        }

        std::memcpy(ptr, data.data(), data.size());
        written = data.size();
        return true;
    });
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
inline bool RingBuffer<N>::peek(std::span<unsigned char> buffer, SizeType &written) const noexcept {
    return peek(buffer.data(), buffer.size(), written);
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
template <ValueLike... Args>
    requires(sizeof...(Args) > 0) && (std::assignable_from<Args&, Args> && ...)
inline bool RingBuffer<N>::peekValues(Args &...args) const noexcept {
    constexpr auto totalSize = (sizeof(Args) + ...);
    if (totalSize > N || slotCount_ == 0) [[unlikely]] {
        return false;
    }

    return peekFromSlot([&](std::span<const unsigned char> data) noexcept -> bool {
        if (data.size() < totalSize) {
            return false;
        }

        std::size_t cursor = 0;
        const auto readArg = [&](auto &arg) noexcept {
            constexpr auto size = sizeof(arg);
            std::memcpy(std::addressof(arg), data.data() + cursor, size);
            cursor += size;
        };
        (readArg(args), ...);

        return true;
    });
}

// MARK: Helpers

template <std::size_t N>
    requires ValidPowerOfTwo<N>
template <typename Writer>
    requires std::invocable<Writer, typename RingBuffer<N>::Slot &> &&
             std::is_nothrow_invocable_v<Writer, typename RingBuffer<N>::Slot &>
inline bool RingBuffer<N>::writeToSlot(Writer &&writer) noexcept {
    auto writePos = writePosition_.load(std::memory_order_relaxed);

    while (true) {
        auto &slot = slots_[writePos & slotCountMask_];
        std::atomic_ref<SizeType> generation_atomic(slot.generation_);
        const auto generation = generation_atomic.load(std::memory_order_acquire);
        const auto udiff = generation - writePos;
        const auto diff = static_cast<std::make_signed_t<SizeType>>(udiff);

        if (diff == 0) {
            // Attempt to claim the slot
            if (writePosition_.compare_exchange_weak(writePos, writePos + 1, std::memory_order_relaxed,
                                                     std::memory_order_relaxed)) {
                std::invoke(std::forward<Writer>(writer), slot);
                generation_atomic.store(writePos + 1, std::memory_order_release);
                return true;
            }
        } else if (diff < 0) {
            // All slots are full
            return false;
        } else {
            // Another producer claimed this slot
            writePos = writePosition_.load(std::memory_order_relaxed);
        }
    }
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
template <bool Consume, typename Reader>
    requires std::invocable<Reader, std::span<const unsigned char>> &&
             std::is_nothrow_invocable_v<Reader, std::span<const unsigned char>>
inline bool RingBuffer<N>::readFromSlot(Reader &&reader) noexcept {
    if (slotCount_ == 0) [[unlikely]] {
        return false;
    }

    const auto readPos = readPosition_.load(std::memory_order_relaxed);
    auto &slot = slots_[readPos & slotCountMask_];

    std::atomic_ref<SizeType> generation_atomic(slot.generation_);
    const auto generation = generation_atomic.load(std::memory_order_acquire);
    const auto udiff = generation - (readPos + 1);
    const auto diff = static_cast<std::make_signed_t<SizeType>>(udiff);

    if (diff != 0) {
        return false;
    }

    const auto data = std::span<const unsigned char>{slot.data_, slot.dataSize_};
    if (!std::invoke(std::forward<Reader>(reader), data)) {
        return false;
    }

    if constexpr (Consume) {
        generation_atomic.store(readPos + slotCount_, std::memory_order_release);
        readPosition_.store(readPos + 1, std::memory_order_relaxed);
    }

    return true;
}

template <std::size_t N>
    requires ValidPowerOfTwo<N>
template <typename Reader>
    requires std::invocable<Reader, std::span<const unsigned char>> &&
             std::is_nothrow_invocable_v<Reader, std::span<const unsigned char>>
inline bool RingBuffer<N>::peekFromSlot(Reader &&reader) const noexcept {
    return const_cast<RingBuffer *>(this)->readFromSlot<false>(reader);
}

} /* namespace mpsc */

#endif
