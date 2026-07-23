#include "software_tick.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

extern "C" void MeasureSerialize(volatile std::uint64_t*, std::uint64_t*, unsigned);
extern "C" void MeasureCpuidLeaf0(volatile std::uint64_t*, std::uint64_t*, unsigned);
extern "C" void MeasureCpuidLeaf16(volatile std::uint64_t*, std::uint64_t*, unsigned);
extern "C" void MeasureVmcall(volatile std::uint64_t*, std::uint64_t*, unsigned);

#pragma warning(push)
#pragma warning(disable : 4324)
struct alignas(64) ClockLine {
    volatile std::uint64_t value;
};

struct alignas(64) ControlLine {
    std::atomic<bool> ready;
    std::atomic<bool> setupSucceeded;
    std::atomic<DWORD> setupError;
    std::atomic<bool> stop;
};
#pragma warning(pop)

struct Statistics {
    double mean;
    double trimmedMean;
    std::uint64_t p10;
    std::uint64_t median;
    std::uint64_t p90;
};

using Probe = void (*)(volatile std::uint64_t*, std::uint64_t*, unsigned);

static std::string FormatDecimal(const double value, const int precision)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.*f", precision, value);
    return text;
}

static __declspec(noinline) bool InvokeProbeSeh(
    const Probe probe,
    volatile std::uint64_t* counter,
    std::uint64_t* samples,
    const unsigned sampleCount,
    DWORD* const exceptionCode)
{
    __try {
        probe(counter, samples, sampleCount);
        return true;
    } __except ((*exceptionCode = GetExceptionCode()), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static Statistics Summarize(std::vector<std::uint64_t> samples)
{
    std::sort(samples.begin(), samples.end());
    const std::size_t count = samples.size();
    long double sum = 0;
    for (const auto value : samples) sum += value;

    const std::size_t trim = count / 100;
    long double trimmed = 0;
    for (std::size_t index = trim; index < count - trim; ++index) {
        trimmed += samples[index];
    }
    return {
        static_cast<double>(sum / count),
        static_cast<double>(trimmed / (count - 2 * trim)),
        samples[count / 10],
        samples[count / 2],
        samples[(count * 9) / 10]};
}

static bool RunProbe(
    const Probe probe,
    ClockLine& clock,
    const unsigned sampleCount,
    Statistics& statistics,
    DWORD& exceptionCode)
{
    std::vector<std::uint64_t> warmup(4096);
    std::vector<std::uint64_t> samples(sampleCount);
    exceptionCode = 0;
    if (!InvokeProbeSeh(
            probe,
            &clock.value,
            warmup.data(),
            static_cast<unsigned>(warmup.size()),
            &exceptionCode) ||
        !InvokeProbeSeh(
            probe, &clock.value, samples.data(), sampleCount, &exceptionCode)) {
        return false;
    }
    statistics = Summarize(std::move(samples));
    return true;
}

ModuleResult RunSoftwareTickTimer(
    const BenchmarkOptions& options,
    const LogicalCpu testCpu,
    const LogicalCpu clockCpu,
    const unsigned maximumLeaf,
    const bool serializeSupported)
{
    if (!serializeSupported) {
        return MakeSetupErrorResult(
            "Software-tick timer",
            6,
            "SERIALIZE is not enumerated by CPUID.7.0:EDX[14]");
    }

    ClockLine clock{};
    ControlLine control{};
    std::thread clockThread([&] {
        const bool pinned = PinCurrentThread(clockCpu);
        DWORD setupError = pinned ? ERROR_SUCCESS : GetLastError();
        const bool prioritized =
            pinned &&
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) !=
                FALSE;
        if (pinned && !prioritized) setupError = GetLastError();
        control.setupError.store(setupError, std::memory_order_relaxed);
        control.setupSucceeded.store(pinned && prioritized, std::memory_order_relaxed);
        control.ready.store(true, std::memory_order_release);
        while (!control.stop.load(std::memory_order_relaxed)) ++clock.value;
    });

    while (!control.ready.load(std::memory_order_acquire)) YieldProcessor();
    if (!control.setupSucceeded.load(std::memory_order_relaxed)) {
        control.stop.store(true, std::memory_order_relaxed);
        clockThread.join();
        char message[96];
        std::snprintf(
            message,
            sizeof(message),
            "clock affinity/priority failed (error %lu)",
            control.setupError.load(std::memory_order_relaxed));
        return MakeSetupErrorResult("Software-tick timer", 5, message);
    }

    Sleep(50);
    Statistics serialize{};
    Statistics leaf0{};
    Statistics leaf16{};
    Statistics vmcall{};
    DWORD serializeException = 0;
    DWORD leaf0Exception = 0;
    DWORD leaf16Exception = 0;
    DWORD vmcallException = 0;
    const bool haveSerialize = RunProbe(
        MeasureSerialize, clock, options.sampleCount, serialize, serializeException);
    const bool haveLeaf0 = RunProbe(
        MeasureCpuidLeaf0, clock, options.sampleCount, leaf0, leaf0Exception);
    const bool leaf16Supported = maximumLeaf >= 0x16;
    const bool haveLeaf16 =
        leaf16Supported &&
        RunProbe(
            MeasureCpuidLeaf16, clock, options.sampleCount, leaf16, leaf16Exception);
    const bool haveVmcall =
        options.vmcall &&
        RunProbe(MeasureVmcall, clock, options.sampleCount, vmcall, vmcallException);

    control.stop.store(true, std::memory_order_relaxed);
    clockThread.join();

    std::vector<PanelRow> rows;
    char topology[128];
    std::snprintf(
        topology,
        sizeof(topology),
        "%u / group%u/cpu%u / group%u/cpu%u",
        options.sampleCount,
        testCpu.group,
        testCpu.number,
        clockCpu.group,
        clockCpu.number);
    rows.push_back({"samples / test / clock", {topology}});
    rows.push_back(
        {"probe", {"mean", "trim-mean", "p10", "median", "p90", "ratio(trim)"}});

    const auto appendProbe = [&](const char* const name,
                                 const Statistics& value,
                                 const bool available,
                                 const double ratio,
                                 const char* const unavailable) {
        if (!available) {
            rows.push_back({name, {unavailable}});
            return;
        }
        rows.push_back(
            {name,
             {FormatDecimal(value.mean, 2),
              FormatDecimal(value.trimmedMean, 2),
              std::to_string(value.p10),
              std::to_string(value.median),
              std::to_string(value.p90),
              FormatDecimal(ratio, 3)},
             true});
    };

    const double leaf0Ratio =
        haveSerialize && serialize.trimmedMean != 0.0 && haveLeaf0
            ? leaf0.trimmedMean / serialize.trimmedMean
            : 0.0;
    const double leaf16Ratio =
        haveSerialize && serialize.trimmedMean != 0.0 && haveLeaf16
            ? leaf16.trimmedMean / serialize.trimmedMean
            : 0.0;
    const double vmcallRatio =
        haveSerialize && serialize.trimmedMean != 0.0 && haveVmcall
            ? vmcall.trimmedMean / serialize.trimmedMean
            : 0.0;

    char serializeUnavailable[64];
    char leaf0Unavailable[64];
    char leaf16Unavailable[64];
    char vmcallUnavailable[64];
    std::snprintf(
        serializeUnavailable,
        sizeof(serializeUnavailable),
        "unavailable (exception 0x%08lX)",
        serializeException);
    std::snprintf(
        leaf0Unavailable,
        sizeof(leaf0Unavailable),
        "unavailable (exception 0x%08lX)",
        leaf0Exception);
    if (leaf16Supported) {
        std::snprintf(
            leaf16Unavailable,
            sizeof(leaf16Unavailable),
            "unavailable (exception 0x%08lX)",
            leaf16Exception);
    } else {
        std::snprintf(
            leaf16Unavailable, sizeof(leaf16Unavailable), "unavailable (unsupported)");
    }
    std::snprintf(
        vmcallUnavailable,
        sizeof(vmcallUnavailable),
        "unavailable (exception 0x%08lX)",
        vmcallException);

    appendProbe("SERIALIZE", serialize, haveSerialize, 1.0, serializeUnavailable);
    appendProbe("CPUID leaf 0", leaf0, haveLeaf0, leaf0Ratio, leaf0Unavailable);
    appendProbe(
        "CPUID leaf 16h", leaf16, haveLeaf16, leaf16Ratio, leaf16Unavailable);
    if (options.vmcall) {
        appendProbe(
            "VMCALL floor", vmcall, haveVmcall, vmcallRatio, vmcallUnavailable);
    }

    // Only SERIALIZE and CPUID leaf 0 are required for the gate.
    // Leaf 16h and VMCALL failures remain informational.
    int setupError = 0;
    if (!haveSerialize || !haveLeaf0 || serialize.trimmedMean <= 0.0 ||
        leaf0.trimmedMean <= 0.0) {
        setupError = 7;
    }

    const bool gateRan = haveSerialize && haveLeaf0 && serialize.trimmedMean > 0.0 &&
                         leaf0.trimmedMean > 0.0;
    const bool passed = gateRan && SoftwareTickPasses(leaf0Ratio);
    if (gateRan) {
        char gate[96];
        std::snprintf(
            gate,
            sizeof(gate),
            "leaf0_ratio=%.3f threshold=2.5 result=%s",
            leaf0Ratio,
            passed ? "PASS" : "FAIL");
        rows.push_back({"software-tick", {gate}});

        const SoftwareTickTripwire tripwire =
            DetectSoftwareTickTripwire(serialize.trimmedMean, leaf0.trimmedMean);
        char trimmed[96];
        char flags[96];
        std::snprintf(
            trimmed,
            sizeof(trimmed),
            "trim_serialize=%.2f trim_leaf0=%.2f",
            serialize.trimmedMean,
            leaf0.trimmedMean);
        std::snprintf(
            flags,
            sizeof(flags),
            "tripwire_eq1=%s tripwire_gt2000=%s",
            tripwire.equalOne ? "yes" : "no",
            tripwire.greaterThan2000 ? "yes" : "no");
        rows.push_back({"tripwire trim", {trimmed}});
        rows.push_back({"tripwire flags", {flags}});
    } else {
        rows.push_back({"software-tick", {"result=SETUP_ERROR code=7"}});
    }

    return {
        "Software-tick timer",
        std::move(rows),
        {gateRan, passed, setupError},
        "Compares CPUID leaf 0 with SERIALIZE. PASS requires a ratio below 2.5."};
}
