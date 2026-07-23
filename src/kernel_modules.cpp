#include "kernel_modules.hpp"
#include "hvd_ioctl.h"

#include <windows.h>

#include <string>

namespace {

HANDLE OpenProbeDevice()
{
    return CreateFileA(
        HVD_DEVICE_SYMLINK_USER,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
}

bool DeviceIoctl(
    const DWORD code,
    void* outBuffer,
    const DWORD outSize)
{
    const HANDLE device = OpenProbeDevice();
    if (device == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(
        device,
        code,
        nullptr,
        0,
        outBuffer,
        outSize,
        &returned,
        nullptr);
    CloseHandle(device);
    return ok != FALSE && returned >= outSize;
}

}  // namespace

ModuleResult RunKTscCpuidTimer()
{
    HVD_K_TSC_CPUID_RESULT result{};
    if (!DeviceIoctl(
            HVD_IOCTL_K_TSC_CPUID, &result, sizeof(result))) {
        return MakeSetupErrorResult(
            "K-TSC-CPUID timer",
            8,
            "probe driver not loaded (\\\\.\\HvD)");
    }

    if (result.Status != HVD_STATUS_OK) {
        return MakeSetupErrorResult(
            "K-TSC-CPUID timer",
            7,
            "kernel probe returned failure status");
    }

    const bool passed = TscCpuidPasses(result.Adjusted);

    return {
        "K-TSC-CPUID timer",
        {
            {"env", {"kernel HIGH_LEVEL, leaf 1, 100+100"}},
            {"cpuid_avg / rdtsc_avg",
             {std::to_string(result.CpuidAverage) + " / " +
              std::to_string(result.RdtscAverage)}},
            {"adjusted / threshold",
             {std::to_string(result.Adjusted) + " / 1500"}},
            {"result", {passed ? "PASS" : "FAIL"}},
        },
        {true, passed, 0},
        "EAC-style quiet leaf1 sandwich (kernel). PASS if 0 < adjusted < 1500."};
}

ModuleResult RunAperfCpuidTimer()
{
    HVD_APERF_RESULT result{};
    if (!DeviceIoctl(HVD_IOCTL_APERF_CPUID, &result, sizeof(result))) {
        return MakeSetupErrorResult(
            "APERF-CPUID timer",
            8,
            "probe driver not loaded (\\\\.\\HvD)");
    }

    if (result.Status == HVD_STATUS_UNSUPPORTED) {
        return MakeSetupErrorResult(
            "APERF-CPUID timer",
            6,
            "IA32_APERF/MPERF not supported (CPUID.06H:ECX[0])");
    }
    if (result.Status != HVD_STATUS_OK) {
        return MakeSetupErrorResult(
            "APERF-CPUID timer",
            7,
            "kernel APERF probe failed");
    }

    // Gate: zero APERF advance across CPUID is a classic thin-HV fingerprint.
    const bool passed = result.AperfDelta != 0;
    return {
        "APERF-CPUID timer",
        {
            {"aperf_before / after",
             {std::to_string(result.AperfBefore) + " / " +
              std::to_string(result.AperfAfter)}},
            {"mperf_before / after",
             {std::to_string(result.MperfBefore) + " / " +
              std::to_string(result.MperfAfter)}},
            {"aperf_delta / mperf_delta",
             {std::to_string(result.AperfDelta) + " / " +
              std::to_string(result.MperfDelta)}},
            {"result", {passed ? "PASS" : "FAIL"}},
        },
        {true, passed, 0},
        "APERF/MPERF around CPUID — FAIL if APERF delta is 0"};
}

ModuleResult RunInvdEmulationCheck()
{
    HVD_INVD_RESULT result{};
    if (!DeviceIoctl(HVD_IOCTL_INVD, &result, sizeof(result))) {
        return MakeSetupErrorResult(
            "INVD-emulation check",
            8,
            "probe driver not loaded (\\\\.\\HvD)");
    }

    if (result.Status == HVD_STATUS_UNSUPPORTED) {
        return MakeSetupErrorResult(
            "INVD-emulation check",
            6,
            "probe disabled: INVD can raise a fatal kernel exception");
    }
    if (result.Status != HVD_STATUS_OK) {
        return MakeSetupErrorResult(
            "INVD-emulation check",
            7,
            "kernel INVD probe failed or #GP");
    }

    const bool passed = result.Detected == 0;
    return {
        "INVD-emulation check",
        {
            {"value", {std::to_string(result.Value)}},
            {"detected_bad_emu", {result.Detected ? "yes" : "no"}},
            {"result", {passed ? "PASS" : "FAIL"}},
        },
        {true, passed, 0},
        "WBINVD/INVD coherence — FAIL if result looks emulated"};
}
