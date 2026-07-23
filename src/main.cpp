#include <windows.h>
#include <intrin.h>

#include <cstdio>
#include <string>

#include "common.hpp"
#include "cpu_topology.hpp"
#include "kernel_modules.hpp"
#include "software_tick.hpp"
#include "tsc_cpuid.hpp"
#include "tsc_exit.hpp"

int main(int argc, char** argv)
{
    BenchmarkOptions options{};
    if (!ParseOptions(argc, argv, options)) {
        PrintUsage(stderr);
        return 2;
    }

    SetConsoleOutputCP(CP_UTF8);
    if (!options.plain) EnableConsoleColor();
    PrintBanner(options.plain);

    const auto cores = FirstLogicalCpuPerCore();
    const auto selected = [&](const BenchmarkModule module) {
        return (static_cast<unsigned>(options.modules) &
                static_cast<unsigned>(module)) != 0;
    };
    const auto printAffected = [&](const int code, const std::string& message) {
        if (selected(BenchmarkModule::SoftwareTick)) {
            PrintResult(
                MakeSetupErrorResult("Software-tick timer", code, message),
                options.plain);
        }
        if (selected(BenchmarkModule::TscExit)) {
            PrintResult(
                MakeSetupErrorResult("TSC-exit timer", code, message),
                options.plain);
        }
        if (selected(BenchmarkModule::TscCpuid)) {
            PrintResult(
                MakeSetupErrorResult("TSC-CPUID timer", code, message),
                options.plain);
        }
    };

    if (cores.empty()) {
        printAffected(3, "no physical-core logical CPU was discovered");
        PrintRunSummary(3, options.plain);
        return 3;
    }

    const LogicalCpu testCpu = cores[0];
    const bool testPinned = PinCurrentThread(testCpu);
    DWORD testSetupError = testPinned ? ERROR_SUCCESS : GetLastError();
    const bool testPrioritized =
        testPinned &&
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) !=
            FALSE;
    if (testPinned && !testPrioritized) testSetupError = GetLastError();
    if (!testPinned || !testPrioritized) {
        char message[96];
        std::snprintf(
            message,
            sizeof(message),
            "test affinity/priority failed (error %lu)",
            testSetupError);
        printAffected(4, message);
        PrintRunSummary(4, options.plain);
        return 4;
    }

    int cpuid[4]{};
    __cpuid(cpuid, 0);
    const unsigned maximumLeaf = static_cast<unsigned>(cpuid[0]);
    bool serializeSupported = false;
    if (maximumLeaf >= 7) {
        __cpuidex(cpuid, 7, 0);
        serializeSupported =
            (static_cast<unsigned>(cpuid[3]) & (1u << 14)) != 0;
    }

    int exitCode = 0;

    if (selected(BenchmarkModule::SoftwareTick)) {
        ModuleResult result =
            cores.size() >= 2
                ? RunSoftwareTickTimer(
                      options,
                      testCpu,
                      cores[1],
                      maximumLeaf,
                      serializeSupported)
                : MakeSetupErrorResult(
                      "Software-tick timer",
                      3,
                      "software-tick requires two physical cores");
        PrintResult(result, options.plain);
        exitCode = CombineOutcome(exitCode, result.outcome);
    }

    if (selected(BenchmarkModule::TscExit)) {
        const ModuleResult result = RunTscExitTimer();
        PrintResult(result, options.plain);
        exitCode = CombineOutcome(exitCode, result.outcome);
    }

    if (selected(BenchmarkModule::TscCpuid)) {
        const ModuleResult result = RunTscCpuidTimer();
        PrintResult(result, options.plain);
        exitCode = CombineOutcome(exitCode, result.outcome);
    }

    if (selected(BenchmarkModule::KTscCpuid)) {
        const ModuleResult result = RunKTscCpuidTimer();
        PrintResult(result, options.plain);
        exitCode = CombineOutcome(exitCode, result.outcome);
    }

    if (selected(BenchmarkModule::AperfCpuid)) {
        const ModuleResult result = RunAperfCpuidTimer();
        PrintResult(result, options.plain);
        exitCode = CombineOutcome(exitCode, result.outcome);
    }

    if (selected(BenchmarkModule::Invd)) {
        const ModuleResult result = RunInvdEmulationCheck();
        PrintResult(result, options.plain);
        exitCode = CombineOutcome(exitCode, result.outcome);
    }

    PrintRunSummary(exitCode, options.plain);
    return exitCode;
}
