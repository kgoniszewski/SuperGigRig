// spsc_queue.hpp - bounded lock-free single-producer single-consumer queue.
//
// The only mechanism, besides atomic parameters, by which objects cross
// between the audio thread and the rest of the app (CLAUDE.md, real-time
// rules). Exactly one thread may push and exactly one thread may pop; neither
// side ever blocks, allocates or makes a system call.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace gigrig {

template <class T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>, "items are copied by value on the audio thread");
    static_assert(std::atomic<std::size_t>::is_always_lock_free);

public:
    // Producer side. Returns false (and leaves the queue unchanged) when full.
    bool push(const T& item) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_.load(std::memory_order_acquire) >= Capacity) {
            return false;
        }
        slots_[tail & kMask] = item;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false when empty.
    bool pop(T& item) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        item = slots_[head & kMask];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer side. Copies the oldest item without removing it; false when empty.
    bool peek(T& item) const noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        item = slots_[head & kMask];
        return true;
    }

    // Consumer side: number of items that can be popped now.
    std::size_t sizeForConsumer() const noexcept {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_relaxed);
    }

    // Producer side: free slots that can be pushed now.
    std::size_t freeForProducer() const noexcept {
        return Capacity - (tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_acquire));
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;
    // Separate cache lines so producer and consumer do not invalidate each other.
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    alignas(64) std::array<T, Capacity> slots_{};
};

}  // namespace gigrig
