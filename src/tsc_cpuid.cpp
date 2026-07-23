#include "tsc_cpuid.hpp"

#include <intrin.h>

#include <string>

std::int64_t AverageAdjustedTiming(
    const std::uint64_t cpuidTotal,
    const std::uint64_t rdtscTotal,
    const unsigned iterations)
{
    if (iterations == 0) return 0;
    const auto divisor = static_cast<std::uint64_t>(iterations);
    return static_cast<std::int64_t>(cpuidTotal / divisor) -
           static_cast<std::int64_t>(rdtscTotal / divisor);
}

static __declspec(noinline) CpuidRdtscTiming MeasureCpuidRdtscTiming()
{
    constexpr unsigned iterationCount = 100;
    std::uint64_t cpuidTotal = 0;
    std::uint64_t rdtscTotal = 0;
    int registers[4]{};
    volatile int cpuidResult = 0;

    for (unsigned index = 0; index < iterationCount; ++index) {
        const std::uint64_t before = __rdtsc();
        __cpuid(registers, 1);
        const std::uint64_t after = __rdtsc();
        cpuidTotal += after - before;
        cpuidResult = registers[0];
    }

    for (unsigned index = 0; index < iterationCount; ++index) {
        const std::uint64_t before = __rdtsc();
        const std::uint64_t after = __rdtsc();
        rdtscTotal += after - before;
    }

    const auto divisor = static_cast<std::uint64_t>(iterationCount);
    const CpuidRdtscTiming result{
        cpuidTotal / divisor,
        rdtscTotal / divisor,
        AverageAdjustedTiming(cpuidTotal, rdtscTotal, iterationCount)};
    static_cast<void>(cpuidResult);
    return result;
}

ModuleResult RunTscCpuidTimer()
{
    const CpuidRdtscTiming timing = MeasureCpuidRdtscTiming();
    return {
        "TSC-CPUID timer",
        {
            {"leaf / samples", {"1 / 100 CPUID + 100 RDTSC"}},
            {"cpuid_avg / rdtsc_avg",
             {std::to_string(timing.cpuidAverage) + " / " +
              std::to_string(timing.rdtscAverage)}},
            {"adjusted", {std::to_string(timing.adjustedAverage)}}},
        {false, true, 0},
        "Measures CPUID leaf 1 after subtracting RDTSC overhead. This module is informational."};
}
