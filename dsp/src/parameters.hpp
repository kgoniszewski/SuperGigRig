// parameters.hpp - C++ side of the parameter table in gigrig_params.h.
#pragma once

#include <gigrig/gigrig_params.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gigrig {

inline constexpr int kParamCount = static_cast<int>(GIGRIG_PARAM_COUNT);

// The table, built from GIGRIG_PARAMETER_LIST.
const std::array<GigRigParamInfo, kParamCount>& parameterTable() noexcept;

inline bool isValidParam(int id) noexcept {
    return id >= 0 && id < kParamCount;
}

// Precondition: isValidParam(id).
inline const GigRigParamInfo& parameterInfo(int id) noexcept {
    return parameterTable()[static_cast<std::size_t>(id)];
}

// -1 when no parameter has this key.
int findParameter(std::string_view key) noexcept;

// Clamps to the range and rounds stepped parameters.
float constrainParameter(const GigRigParamInfo& info, float value) noexcept;
float parameterToNormalized(const GigRigParamInfo& info, float value) noexcept;
float parameterFromNormalized(const GigRigParamInfo& info, float normalized) noexcept;

// The flat array of atomic values that crosses from the UI to the audio thread.
// Writers store constrained values; the audio thread reads each one once per block.
class ParameterValues {
    static_assert(std::atomic<float>::is_always_lock_free, "parameters must be lock free");

public:
    ParameterValues() noexcept;

    // Any thread. Ignores unknown ids and NaN.
    void set(int id, float value) noexcept;

    // Any thread. 0 for an unknown id.
    float get(int id) const noexcept {
        return isValidParam(id) ? values_[static_cast<std::size_t>(id)].load(std::memory_order_relaxed)
                                : 0.0f;
    }

private:
    std::array<std::atomic<float>, kParamCount> values_;
};

}  // namespace gigrig
