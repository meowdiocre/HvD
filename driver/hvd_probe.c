#include <ntddk.h>
#include <intrin.h>

#include "../include/hvd_ioctl.h"

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD HvdUnload;

static PDEVICE_OBJECT g_DeviceObject = NULL;

static NTSTATUS
HvdCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static VOID
HvdPinCurrentProcessor(
    _Out_ PGROUP_AFFINITY Previous
    )
{
    GROUP_AFFINITY affinity;
    PROCESSOR_NUMBER number;

    RtlZeroMemory(&affinity, sizeof(affinity));
    RtlZeroMemory(Previous, sizeof(*Previous));
    RtlZeroMemory(&number, sizeof(number));

    KeGetCurrentProcessorNumberEx(&number);
    affinity.Group = number.Group;
    affinity.Mask = (KAFFINITY)1 << number.Number;
    KeSetSystemGroupAffinityThread(&affinity, Previous);
}

static VOID
HvdRunKTscCpuid(
    _Out_ HVD_K_TSC_CPUID_RESULT* Result
    )
{
    KIRQL oldIrql;
    GROUP_AFFINITY previous;
    unsigned long long cpuidTotal = 0;
    unsigned long long rdtscTotal = 0;
    unsigned index;
    int regs[4];
    volatile int sink[4];

    RtlZeroMemory(Result, sizeof(*Result));
    Result->Iterations = 100;
    Result->Status = HVD_STATUS_OK;

    HvdPinCurrentProcessor(&previous);
    KeRaiseIrql(HIGH_LEVEL, &oldIrql);

    for (index = 0; index < 100; ++index) {
        const unsigned long long before = __rdtsc();
        __cpuid(regs, 1);
        sink[0] = regs[0];
        sink[1] = regs[1];
        sink[2] = regs[2];
        sink[3] = regs[3];
        {
            const unsigned long long after = __rdtsc();
            cpuidTotal += after - before;
        }
    }

    for (index = 0; index < 100; ++index) {
        const unsigned long long before = __rdtsc();
        const unsigned long long after = __rdtsc();
        rdtscTotal += after - before;
    }

    KeLowerIrql(oldIrql);
    KeRevertToUserGroupAffinityThread(&previous);

    Result->CpuidTotal = cpuidTotal;
    Result->RdtscTotal = rdtscTotal;
    Result->CpuidAverage = cpuidTotal / 100ull;
    Result->RdtscAverage = rdtscTotal / 100ull;
    Result->Adjusted =
        (long long)Result->CpuidAverage - (long long)Result->RdtscAverage;
    UNREFERENCED_PARAMETER(sink);
}

static VOID
HvdRunAperfCpuid(
    _Out_ HVD_APERF_RESULT* Result
    )
{
    KIRQL oldIrql;
    GROUP_AFFINITY previous;
    int regs[4];
    int cpuInfo[4];

    RtlZeroMemory(Result, sizeof(*Result));

    __cpuid(cpuInfo, 0);
    if ((unsigned)cpuInfo[0] < 6u) {
        Result->Status = HVD_STATUS_UNSUPPORTED;
        return;
    }
    __cpuid(cpuInfo, 6);
    if (((unsigned)cpuInfo[2] & 1u) == 0u) {
        Result->Status = HVD_STATUS_UNSUPPORTED;
        return;
    }

    HvdPinCurrentProcessor(&previous);
    KeRaiseIrql(HIGH_LEVEL, &oldIrql);
    Result->MperfBefore = __readmsr(0xE7);
    Result->AperfBefore = __readmsr(0xE8);
    __cpuid(regs, 1);
    Result->AperfAfter = __readmsr(0xE8);
    Result->MperfAfter = __readmsr(0xE7);
    KeLowerIrql(oldIrql);
    KeRevertToUserGroupAffinityThread(&previous);

    Result->AperfDelta = Result->AperfAfter - Result->AperfBefore;
    Result->MperfDelta = Result->MperfAfter - Result->MperfBefore;
    Result->Status = HVD_STATUS_OK;
    UNREFERENCED_PARAMETER(regs);
}

static NTSTATUS
HvdDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    PIO_STACK_LOCATION irpSp;
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR information = 0;
    PVOID buffer;
    ULONG outLen;

    UNREFERENCED_PARAMETER(DeviceObject);

    irpSp = IoGetCurrentIrpStackLocation(Irp);
    buffer = Irp->AssociatedIrp.SystemBuffer;
    outLen = irpSp->Parameters.DeviceIoControl.OutputBufferLength;

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
    case HVD_IOCTL_K_TSC_CPUID:
        if (buffer == NULL || outLen < sizeof(HVD_K_TSC_CPUID_RESULT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        HvdRunKTscCpuid((HVD_K_TSC_CPUID_RESULT*)buffer);
        information = sizeof(HVD_K_TSC_CPUID_RESULT);
        status = STATUS_SUCCESS;
        break;

    case HVD_IOCTL_APERF_CPUID:
        if (buffer == NULL || outLen < sizeof(HVD_APERF_RESULT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        HvdRunAperfCpuid((HVD_APERF_RESULT*)buffer);
        information = sizeof(HVD_APERF_RESULT);
        status = STATUS_SUCCESS;
        break;

    case HVD_IOCTL_INVD:
        if (buffer == NULL || outLen < sizeof(HVD_INVD_RESULT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        RtlZeroMemory(buffer, sizeof(HVD_INVD_RESULT));
        ((HVD_INVD_RESULT*)buffer)->Status = HVD_STATUS_UNSUPPORTED;
        information = sizeof(HVD_INVD_RESULT);
        status = STATUS_SUCCESS;
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

VOID
HvdUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNICODE_STRING symlink;

    UNREFERENCED_PARAMETER(DriverObject);
    RtlInitUnicodeString(&symlink, HVD_SYMLINK_NAME_KERNEL);
    IoDeleteSymbolicLink(&symlink);
    if (g_DeviceObject != NULL) {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
    }
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;
    UNICODE_STRING deviceName;
    UNICODE_STRING symlink;

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlInitUnicodeString(&deviceName, HVD_DEVICE_NAME_KERNEL);
    RtlInitUnicodeString(&symlink, HVD_SYMLINK_NAME_KERNEL);

    status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_DeviceObject);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = IoCreateSymbolicLink(&symlink, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
        return status;
    }

    DriverObject->DriverUnload = HvdUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = HvdCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = HvdCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HvdDeviceControl;

    g_DeviceObject->Flags |= DO_BUFFERED_IO;
    g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
