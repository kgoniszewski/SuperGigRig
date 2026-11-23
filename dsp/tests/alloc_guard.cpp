#include "alloc_guard.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace gigrig::test {
namespace {

thread_local int guardDepth = 0;
std::atomic<long> violations{0};

bool abortRequested() noexcept {
    static const bool requested = [] {
        const char* v = std::getenv("GIGRIG_ALLOC_GUARD_ABORT");
        return v != nullptr && v[0] == '1';
    }();
    return requested;
}

}  // namespace

AllocationGuard::AllocationGuard() noexcept {
    ++guardDepth;
}

AllocationGuard::~AllocationGuard() {
    --guardDepth;
}

long allocationViolations() noexcept {
    return violations.load();
}

void resetAllocationViolations() noexcept {
    violations.store(0);
}

namespace detail {

void noteAllocation(std::size_t size) noexcept {
    if (guardDepth > 0) {
        violations.fetch_add(1);
        // Re-entrancy: fprintf to stderr does not use operator new.
        std::fprintf(stderr, "  allocation guard: %zu byte allocation on the audio path\n", size);
        if (abortRequested()) {
            std::abort();
        }
    }
}

void* allocate(std::size_t size) {
    noteAllocation(size);
    void* p = std::malloc(size == 0 ? 1 : size);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

void* allocateAligned(std::size_t size, std::align_val_t alignment) {
    noteAllocation(size);
    const auto align = static_cast<std::size_t>(alignment);
    // aligned_alloc needs a size that is a multiple of the alignment.
    const std::size_t rounded = ((size == 0 ? 1 : size) + align - 1) / align * align;
    void* p = std::aligned_alloc(align, rounded);
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    return p;
}

}  // namespace detail
}  // namespace gigrig::test

using gigrig::test::detail::allocate;
using gigrig::test::detail::allocateAligned;

void* operator new(std::size_t size) {
    return allocate(size);
}

void* operator new[](std::size_t size) {
    return allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocate(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocateAligned(size, alignment);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return allocateAligned(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return allocateAligned(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    std::free(p);
}
