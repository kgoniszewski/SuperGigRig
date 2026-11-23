// denormals.hpp - scoped flush-to-zero / denormals-are-zero for the audio thread.
//
// Denormal floats can slow DSP code by orders of magnitude. The engine wraps
// every process call in a ScopedNoDenormals so filters and feedback paths
// never fall into the denormal range, whatever the host thread's FP state is.
#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <immintrin.h>
#define GIGRIG_DENORMALS_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define GIGRIG_DENORMALS_ARM64 1
#endif

namespace gigrig {

class ScopedNoDenormals {
public:
    ScopedNoDenormals() noexcept {
#if defined(GIGRIG_DENORMALS_X86)
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u);  // FTZ (bit 15) | DAZ (bit 6)
#elif defined(GIGRIG_DENORMALS_ARM64)
        std::uint64_t fpcr;
        __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        fpcr |= (std::uint64_t{1} << 24);  // FZ
        __asm__ __volatile__("msr fpcr, %0" : : "r"(fpcr));
#endif
    }

    ~ScopedNoDenormals() {
#if defined(GIGRIG_DENORMALS_X86)
        _mm_setcsr(saved_);
#elif defined(GIGRIG_DENORMALS_ARM64)
        __asm__ __volatile__("msr fpcr, %0" : : "r"(saved_));
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
#if defined(GIGRIG_DENORMALS_X86)
    unsigned int saved_ = 0;
#elif defined(GIGRIG_DENORMALS_ARM64)
    std::uint64_t saved_ = 0;
#endif
};

}  // namespace gigrig
