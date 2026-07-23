#include "tsc_exit.hpp"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

static __forceinline std::uint64_t RdtscSerialized()
{
    _mm_lfence();
    const std::uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}

ModuleResult RunTscExitTimer()
{
    // Pafish/Al-Khaser: leaf 0, 10 samples. Gate on p10 (clean cluster), not
    // mean/median, so one interrupt does not fail bare metal. Threshold 1000.
    constexpr unsigned iterationCount = 10;
    std::vector<std::uint64_t> samples(iterationCount);
    std::uint64_t total = 0;
    int registers[4]{};
    volatile int cpuidResult[4]{};

    // Warmup
    for (unsigned index = 0; index < 20; ++index) {
        const std::uint64_t before = RdtscSerialized();
        __cpuid(registers, 0);
        const std::uint64_t after = RdtscSerialized();
        static_cast<void>(after - before);
        cpuidResult[0] = registers[0];
    }

    for (unsigned index = 0; index < iterationCount; ++index) {
        const std::uint64_t before = RdtscSerialized();
        __cpuid(registers, 0);
        const std::uint64_t after = RdtscSerialized();
        const std::uint64_t delta = after - before;
        samples[index] = delta;
        total += delta;
        if (index + 1 < iterationCount) {
            Sleep(500);
        }
        cpuidResult[0] = registers[0];
        cpuidResult[1] = registers[1];
        cpuidResult[2] = registers[2];
        cpuidResult[3] = registers[3];
    }

    std::sort(samples.begin(), samples.end());
    const std::uint64_t p10 = samples[1];  // ~10th of 10
    const std::uint64_t median = samples[iterationCount / 2u];
    const std::uint64_t average = total / iterationCount;
    const bool passed = TscExitPasses(p10);
    static_cast<void>(cpuidResult[0]);
    return {
        "TSC-exit timer",
        {
            {"samples / sleep / leaf", {"10 / 500ms / 0"}},
            {"p10 / threshold", {std::to_string(p10) + " / 1000"}},
            {"median / mean (diag)",
             {std::to_string(median) + " / " + std::to_string(average)}},
            {"result", {passed ? "PASS" : "FAIL"}},
        },
        {true, passed, 0},
        "RDTSC; CPUID(0); RDTSC with lfence. Gate on p10 of 10: PASS if 0 < p10 < 1000."};
}
