#pragma once

/*
 * Shared usermode / kernel ABI for HvD probe driver.
 * Keep this header C-compatible and pack-stable.
 */

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

#define HVD_DEVICE_TYPE 0x8000u

#define HVD_IOCTL_K_TSC_CPUID \
    CTL_CODE(HVD_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS)
#define HVD_IOCTL_APERF_CPUID \
    CTL_CODE(HVD_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)
#define HVD_IOCTL_INVD \
    CTL_CODE(HVD_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS)

#define HVD_DEVICE_SYMLINK_USER "\\\\.\\HvD"
#define HVD_DEVICE_NAME_KERNEL L"\\Device\\HvD"
#define HVD_SYMLINK_NAME_KERNEL L"\\DosDevices\\HvD"

#define HVD_STATUS_OK 0u
#define HVD_STATUS_FAILED 1u
#define HVD_STATUS_UNSUPPORTED 2u

#pragma pack(push, 8)

typedef struct _HVD_K_TSC_CPUID_RESULT {
    unsigned long long CpuidTotal;
    unsigned long long RdtscTotal;
    unsigned long long CpuidAverage;
    unsigned long long RdtscAverage;
    long long Adjusted;
    unsigned long Iterations;
    unsigned long Status;
} HVD_K_TSC_CPUID_RESULT;

typedef struct _HVD_APERF_RESULT {
    unsigned long long AperfBefore;
    unsigned long long AperfAfter;
    unsigned long long MperfBefore;
    unsigned long long MperfAfter;
    unsigned long long AperfDelta;
    unsigned long long MperfDelta;
    unsigned long Status;
    unsigned long Reserved;
} HVD_APERF_RESULT;

typedef struct _HVD_INVD_RESULT {
    unsigned long long Value;
    unsigned long Detected; /* 1 = likely bad INVD emulation */
    unsigned long Status;
} HVD_INVD_RESULT;

#pragma pack(pop)
