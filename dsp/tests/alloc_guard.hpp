// alloc_guard.hpp - catches heap allocation on the audio path in tests.
//
// The test binary replaces the global operator new and delete (alloc_guard.cpp).
// While an AllocationGuard is alive on a thread, every operator new on that
// thread is recorded as a violation of the real-time rules, with a message.
// Set the environment variable GIGRIG_ALLOC_GUARD_ABORT=1 to abort instead,
// which gives a stack trace under a debugger or sanitiser.
//
// It catches C++ allocation (new, std::vector growth, std::function,
// std::string), which is what DSP code can reach by accident. Direct malloc
// calls are not intercepted, because the sanitisers own malloc; CLAUDE.md
// rules out malloc on the audio path anyway.
#pragma once

namespace gigrig::test {

class AllocationGuard {
public:
    AllocationGuard() noexcept;
    ~AllocationGuard();
    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;
};

// Number of allocations made inside a guard since the last reset, all threads.
long allocationViolations() noexcept;
void resetAllocationViolations() noexcept;

}  // namespace gigrig::test
