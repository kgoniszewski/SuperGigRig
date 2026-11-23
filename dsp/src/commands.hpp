// commands.hpp - structural changes handed to the audio thread, and the way
// replaced objects come back to be freed.
//
// A non-audio thread (the app's worker queue) builds an object, for example a
// prepared convolver, and pushes a Command that carries it by pointer. At the
// start of the next block the audio thread applies the command, which usually
// swaps a pointer, and pushes whatever it replaced onto the return queue.
// gigrig_collect_garbage later frees those objects off the audio thread.
// The audio thread therefore never allocates or frees.
#pragma once

#include <cstdint>
#include <memory>

#include "spsc_queue.hpp"

namespace gigrig {

using DestroyFn = void (*)(void*) noexcept;

enum class CommandType : std::uint32_t {
    // Carries its object through the audio thread and straight back to be
    // freed. Used by tests, and the template for real commands such as the
    // cabinet IR swap in work package 3.
    Noop = 0,
};

struct Command {
    CommandType type = CommandType::Noop;
    std::int32_t slot = 0;        // which block or slot the command addresses
    void* object = nullptr;       // owned; ownership passes to the engine on a successful push
    DestroyFn destroy = nullptr;  // frees object; never called on the audio thread
};

// An object the audio thread no longer uses, waiting to be freed.
struct Retired {
    void* object = nullptr;
    DestroyFn destroy = nullptr;
};

template <class T>
void destroyObject(void* p) noexcept {
    delete static_cast<T*>(p);
}

// Wraps an owned object into a command. The caller keeps nothing: if the push
// fails, call disposeCommand to free it.
template <class T>
Command makeCommand(CommandType type, std::int32_t slot, std::unique_ptr<T> object) noexcept {
    return Command{type, slot, object.release(), &destroyObject<T>};
}

inline void disposeCommand(const Command& c) noexcept {
    if (c.object != nullptr && c.destroy != nullptr) {
        c.destroy(c.object);
    }
}

class CommandQueues {
public:
    static constexpr std::size_t kCapacity = 64;

    ~CommandQueues() { clear(); }

    // Producer thread. Returns false when the queue is full; the caller still owns c.object.
    bool push(const Command& c) noexcept { return commands_.push(c); }

    // Audio thread. Applies pending commands in order. apply(const Command&) returns
    // the Retired object (or an empty Retired). A command is only taken when the
    // return queue has room for what it may retire, so nothing is ever leaked or
    // freed here; a command that does not fit waits for the next block.
    template <class Apply>
    void drain(Apply&& apply) noexcept {
        Command c;
        while (retired_.freeForProducer() > 0 && commands_.pop(c)) {
            const Retired r = apply(c);
            if (r.object != nullptr) {
                retired_.push(r);
            }
        }
    }

    // Garbage thread. Frees retired objects; returns how many.
    int collectGarbage() noexcept {
        int freed = 0;
        Retired r;
        while (retired_.pop(r)) {
            if (r.destroy != nullptr) {
                r.destroy(r.object);
            }
            ++freed;
        }
        return freed;
    }

    // Only when the audio thread is not running (destruction, prepare): frees
    // everything still queued in either direction.
    void clear() noexcept {
        Command c;
        while (commands_.pop(c)) {
            disposeCommand(c);
        }
        collectGarbage();
    }

private:
    SpscQueue<Command, kCapacity> commands_;
    SpscQueue<Retired, kCapacity> retired_;
};

}  // namespace gigrig
