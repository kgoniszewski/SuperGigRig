// pre_fx.hpp - the Pre FX section: Boost, Env Filter, Gold OD, Twin OD and
// Bucket Delay in the reference's fixed order (plan section 4, rows 1 to 5).
//
// Reads its parameters once per block from the flat parameter array and runs
// the pedals in place. Every pedal that is off costs nothing and leaves the
// signal bit exact.
#pragma once

#include "boost.hpp"
#include "bucket_delay.hpp"
#include "env_filter.hpp"
#include "gold_od.hpp"
#include "parameters.hpp"
#include "twin_od.hpp"

namespace gigrig {

class PreFx {
public:
    // Not real-time safe.
    void prepare(double sampleRate, int maxBlockSize, const ParameterValues& params);
    void reset(const ParameterValues& params) noexcept;

    // Real-time safe; in place; 1 <= n <= prepared maximum.
    void process(float* io, int n, const ParameterValues& params) noexcept;

    Boost& boost() noexcept { return boost_; }
    EnvFilter& envFilter() noexcept { return envFilter_; }
    GoldOd& goldOd() noexcept { return goldOd_; }
    TwinOd& twinOd() noexcept { return twinOd_; }
    BucketDelay& bucketDelay() noexcept { return bucketDelay_; }

private:
    void readSettings(const ParameterValues& p) noexcept;
    void applySettings() noexcept;

    Boost::Settings boostSettings_;
    EnvFilter::Settings envSettings_;
    GoldOd::Settings goldSettings_;
    TwinOd::Settings twinSettings_;
    BucketDelay::Settings delaySettings_;

    Boost boost_;
    EnvFilter envFilter_;
    GoldOd goldOd_;
    TwinOd twinOd_;
    BucketDelay bucketDelay_;
};

}  // namespace gigrig
