#include "tsc_cpuid.hpp"

#include <windows.h>
#include <intrin.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

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

static double MedianOf(std::vector<double> samples)
{
    if (samples.empty()) return 0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2u];
}

static std::uint64_t MedianOf(std::vector<std::uint64_t> samples)
{
    if (samples.empty()) return 0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2u];
}

static std::string FormatDecimal(const double value, const int precision)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", precision, value);
    return text;
}

static bool CheckMedianHelpers()
{
    const bool passed =
        MedianOf(std::vector<double>{3.0, 1.0, 2.0}) == 2.0 &&
        MedianOf(std::vector<std::uint64_t>{9, 1, 5}) == 5;
    assert(passed);
    return passed;
}

// Serialized RDTSC edges (Intel SDM / opt guide style) reduce OO reordering noise.
static __forceinline std::uint64_t RdtscSerialized()
{
    _mm_lfence();
    const std::uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}

static void WarmUpProcessor()
{
    constexpr std::int64_t warmupMilliseconds = 20;
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    LARGE_INTEGER now{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    const std::int64_t targetTicks =
        (frequency.QuadPart * warmupMilliseconds) / 1000;

    int registers[4]{};
    volatile int cpuidResult = 0;
    do {
        __cpuid(registers, 1);
        cpuidResult ^= registers[0];
        QueryPerformanceCounter(&now);
    } while (now.QuadPart - start.QuadPart < targetTicks);
    static_cast<void>(cpuidResult);
}

static std::uint64_t MeasureReferenceBlock(const unsigned repeatCount)
{
    std::uint64_t total = 0;
    for (unsigned index = 0; index < repeatCount; ++index) {
        const std::uint64_t before = RdtscSerialized();
        const std::uint64_t after = RdtscSerialized();
        total += after - before;
    }
    return total;
}

static std::uint64_t MeasureCpuid(int registers[4])
{
    const std::uint64_t before = RdtscSerialized();
    __cpuid(registers, 1);
    const std::uint64_t after = RdtscSerialized();
    return after - before;
}

static __declspec(noinline) CpuidRdtscTiming MeasureCpuidRdtscTiming()
{
    constexpr unsigned sampleCount = 100;
    constexpr unsigned referenceRepeats = 16;
    constexpr double stabilityLimitPercent = 15.0;
    std::vector<double> ratios;
    std::vector<double> references;
    ratios.reserve(sampleCount);
    references.reserve(sampleCount);
    std::vector<std::uint64_t> cpuidSamples;
    cpuidSamples.reserve(sampleCount);
    int registers[4]{};
    volatile int cpuidResult = 0;

    for (unsigned index = 0; index < sampleCount; ++index) {
        std::uint64_t referenceTicks = 0;
        std::uint64_t cpuidTicks = 0;
        if ((index & 1u) == 0) {
            referenceTicks = MeasureReferenceBlock(referenceRepeats);
            cpuidTicks = MeasureCpuid(registers);
        } else {
            cpuidTicks = MeasureCpuid(registers);
            referenceTicks = MeasureReferenceBlock(referenceRepeats);
        }

        if (referenceTicks == 0 || cpuidTicks == 0) continue;
        const double referencePerOperation =
            static_cast<double>(referenceTicks) /
            static_cast<double>(referenceRepeats);
        references.push_back(referencePerOperation);
        ratios.push_back(
            static_cast<double>(cpuidTicks) / referencePerOperation);
        cpuidSamples.push_back(cpuidTicks);
        cpuidResult ^= registers[0];
    }

    const double ratioMedian = MedianOf(ratios);
    const double referenceMedian = MedianOf(references);
    std::vector<double> deviations;
    deviations.reserve(references.size());
    for (const double value : references) {
        deviations.push_back(std::abs(value - referenceMedian));
    }
    const double referenceMad = MedianOf(deviations);
    const double stabilityPercent = referenceMedian > 0.0
        ? 100.0 * referenceMad / referenceMedian
        : 100.0;
    const std::uint64_t cpuidMedian = MedianOf(cpuidSamples);
    const bool stable = ratios.size() >= sampleCount / 2u &&
                        stabilityPercent <= stabilityLimitPercent;
    static_cast<void>(cpuidResult);
    return {
        cpuidMedian,
        referenceMedian,
        ratioMedian - 1.0,
        stabilityPercent,
        stable};
}

ModuleResult RunTscCpuidTimer()
{
    constexpr unsigned trialCount = 5;
    constexpr double trialStabilityLimitPercent = 15.0;
    if (!CheckMedianHelpers()) {
        return {
            "TSC-CPUID timer",
            {{"result", {"SETUP_ERROR median self-check"}}},
            {false, false, 8},
            "Internal median aggregation self-check failed."};
    }
    WarmUpProcessor();
    std::vector<double> trialRatios;
    std::vector<double> trialReferenceStability;
    std::vector<double> referenceMedians;
    std::vector<std::uint64_t> cpuidMedians;
    trialRatios.reserve(trialCount);
    trialReferenceStability.reserve(trialCount);
    referenceMedians.reserve(trialCount);
    cpuidMedians.reserve(trialCount);
    bool allStable = true;

    for (unsigned trial = 0; trial < trialCount; ++trial) {
        const CpuidRdtscTiming timing = MeasureCpuidRdtscTiming();
        cpuidMedians.push_back(timing.cpuidMedian);
        referenceMedians.push_back(timing.referenceMedian);
        trialRatios.push_back(timing.adjustedRatio);
        trialReferenceStability.push_back(timing.stabilityPercent);
        allStable = allStable && timing.stable;
    }

    const std::uint64_t cpuidMedian = MedianOf(cpuidMedians);
    const double referenceMedian = MedianOf(referenceMedians);
    const double ratio = MedianOf(trialRatios);
    const double referenceStability = MedianOf(trialReferenceStability);
    std::vector<double> ratioDeviations;
    ratioDeviations.reserve(trialRatios.size());
    for (const double trialRatio : trialRatios) {
        ratioDeviations.push_back(std::abs(trialRatio - ratio));
    }
    const double ratioMad = MedianOf(ratioDeviations);
    const double trialStability = ratio != 0.0
        ? 100.0 * ratioMad / std::abs(ratio)
        : 100.0;
    const bool stable = allStable && !trialRatios.empty() &&
                        trialStability <= trialStabilityLimitPercent;
    const char* measurement = stable ? "STABLE" : "UNSTABLE";
    const char* classification = stable ? "UNCALIBRATED" : "UNAVAILABLE";
    return {
        "TSC-CPUID timer",
        {
            {"configuration", {"leaf=1 samples=100 trials=5 reference=16"}},
            {"CPUID median", {std::to_string(cpuidMedian) + " ticks"}},
            {"reference median", {FormatDecimal(referenceMedian, 3) + " ticks"}},
            {"normalized cost", {FormatDecimal(ratio, 3)}},
            {"reference jitter", {FormatDecimal(referenceStability, 3) + "%"}},
            {"trial jitter", {FormatDecimal(trialStability, 3) + "%"}},
            {"stability", {measurement}},
            {"result", {classification}},
        },
        {false, true, 0},
        {}};
}
