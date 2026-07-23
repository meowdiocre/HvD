#pragma once

#include "common.hpp"

#include <cstdint>

struct CpuidRdtscTiming {
    std::uint64_t cpuidMedian;
    double referenceMedian;
    double adjustedRatio;
    double stabilityPercent;
    bool stable;
};

std::int64_t AverageAdjustedTiming(
    std::uint64_t cpuidTotal,
    std::uint64_t rdtscTotal,
    unsigned iterations);

ModuleResult RunTscCpuidTimer();
