#include "tsc_exit.hpp"

#include <windows.h>
#include <intrin.h>

#include <string>

ModuleResult RunTscExitTimer()
{
    constexpr unsigned iterationCount = 10;
    std::uint64_t total = 0;
    int registers[4]{};
    volatile int cpuidResult[4]{};

    for (unsigned index = 0; index < iterationCount; ++index) {
        const std::uint64_t before = __rdtsc();
        __cpuid(registers, 0);
        const std::uint64_t after = __rdtsc();
        total += after - before;
        if (index + 1 < iterationCount) {
            Sleep(500);
        }
        cpuidResult[0] = registers[0];
        cpuidResult[1] = registers[1];
        cpuidResult[2] = registers[2];
        cpuidResult[3] = registers[3];
    }

    const std::uint64_t average = total / iterationCount;
    const bool passed = TscExitPasses(average);
    static_cast<void>(cpuidResult[0]);
    return {
        "TSC-exit timer",
        {
            {"samples / sleep / leaf", {"10 / 500ms / 0"}},
            {"average / threshold", {std::to_string(average) + " / 1000"}},
            {"result", {passed ? "PASS" : "FAIL"}}},
        {true, passed, 0},
        "Measures RDTSC; CPUID(0); RDTSC. PASS requires 0 < average < 1000."};
}
