#include "parameters.hpp"

#include <algorithm>
#include <cmath>

namespace gigrig {
namespace {

// Checks at compile time that ids are dense, start at 0 and follow list order,
// which is what makes them usable as indices into one flat array.
constexpr bool idsAreDense() {
    int expected = 0;
    bool ok = true;
#define GIGRIG_PARAM_CHECK_ENTRY(name, id, key, display, unit, lo, hi, def, taper, smoothing) \
    ok = ok && (id) == expected && static_cast<int>(name) == expected;                        \
    ++expected;
    GIGRIG_PARAMETER_LIST(GIGRIG_PARAM_CHECK_ENTRY)
#undef GIGRIG_PARAM_CHECK_ENTRY
    return ok && expected == kParamCount;
}
static_assert(idsAreDense(), "parameter ids must be 0, 1, 2 ... in list order; only append");

constexpr std::array<GigRigParamInfo, kParamCount> kTable = {{
#define GIGRIG_PARAM_TABLE_ENTRY(name, id, key, display, unit, lo, hi, def, taper, smoothing) \
    GigRigParamInfo{id, key, display, unit, lo, hi, def, taper, smoothing},
    GIGRIG_PARAMETER_LIST(GIGRIG_PARAM_TABLE_ENTRY)
#undef GIGRIG_PARAM_TABLE_ENTRY
}};

constexpr bool rangesAreValid() {
    for (const auto& p : kTable) {
        if (!(p.min_value < p.max_value) || p.default_value < p.min_value || p.default_value > p.max_value) {
            return false;
        }
        if (p.taper == GIGRIG_TAPER_LOG && !(p.min_value > 0.0f)) {
            return false;
        }
    }
    return true;
}
static_assert(rangesAreValid(), "every parameter needs min < max, a default inside, and min > 0 for log");

}  // namespace

const std::array<GigRigParamInfo, kParamCount>& parameterTable() noexcept {
    return kTable;
}

int findParameter(std::string_view key) noexcept {
    for (const auto& p : kTable) {
        if (key == p.key) {
            return p.id;
        }
    }
    return -1;
}

float constrainParameter(const GigRigParamInfo& info, float value) noexcept {
    float v = std::clamp(value, info.min_value, info.max_value);
    if (info.taper == GIGRIG_TAPER_STEPPED) {
        v = std::round(v);
    }
    return v;
}

float parameterToNormalized(const GigRigParamInfo& info, float value) noexcept {
    const double v = static_cast<double>(constrainParameter(info, value));
    const auto lo = static_cast<double>(info.min_value);
    const auto hi = static_cast<double>(info.max_value);
    const double n =
        info.taper == GIGRIG_TAPER_LOG ? std::log(v / lo) / std::log(hi / lo) : (v - lo) / (hi - lo);
    return static_cast<float>(std::clamp(n, 0.0, 1.0));
}

float parameterFromNormalized(const GigRigParamInfo& info, float normalized) noexcept {
    const double n = std::clamp(static_cast<double>(normalized), 0.0, 1.0);
    const auto lo = static_cast<double>(info.min_value);
    const auto hi = static_cast<double>(info.max_value);
    const double v = info.taper == GIGRIG_TAPER_LOG ? lo * std::pow(hi / lo, n) : lo + n * (hi - lo);
    return constrainParameter(info, static_cast<float>(v));
}

ParameterValues::ParameterValues() noexcept {
    for (const auto& p : kTable) {
        values_[static_cast<std::size_t>(p.id)].store(p.default_value, std::memory_order_relaxed);
    }
}

void ParameterValues::set(int id, float value) noexcept {
    if (!isValidParam(id) || std::isnan(value)) {
        return;
    }
    values_[static_cast<std::size_t>(id)].store(constrainParameter(parameterInfo(id), value),
                                                std::memory_order_relaxed);
}

}  // namespace gigrig
