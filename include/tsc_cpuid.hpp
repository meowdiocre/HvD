#pragma once

#include "common.hpp"

#include <cstdint>

struct CpuidRdtscTiming {
    std::uint64_t cpuidAverage;
    std::uint64_t rdtscAverage;
    std::int64_t adjustedAverage;
};

std::int64_t AverageAdjustedTiming(
    std::uint64_t cpuidTotal,
    std::uint64_t rdtscTotal,
    unsigned iterations);

ModuleResult RunTscCpuidTimer();
