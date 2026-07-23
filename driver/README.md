# HvDProbe.sys

Kernel probe driver for HvD (device `\\.\HvD`).

## Probes

| IOCTL | Flag in HvD.exe | Behavior |
| --- | --- | --- |
| `HVD_IOCTL_K_TSC_CPUID` | `--k-tsc-cpuid` | Pin CPU, raise IRQL HIGH_LEVEL, 100× leaf1 RDTSC sandwich + 100× RDTSC overhead |
| `HVD_IOCTL_APERF_CPUID` | `--aperf-cpuid` | APERF/MPERF around CPUID(1); FAIL in usermode if APERF delta == 0 |
| `HVD_IOCTL_INVD` | `--invd` | WBINVD / write / INVD stack check |

## Build (WDK)

Requires Visual Studio + Windows Driver Kit.

```text
# From a WDK x64 Free Build Environment, in this directory:
build -cZ
```

Or open a KMDF/WDM driver project targeting `hvd_probe.c` and `../include/hvd_ioctl.h`.

Output name suggestion: `HvDProbe.sys`.

## Load (test machine only)

Unsigned drivers need test signing / DSE disable. Example service:

```powershell
sc.exe create HvDProbe type= kernel binPath= "C:\path\to\HvDProbe.sys"
sc.exe start HvDProbe
# run: HvD.exe --kernel
sc.exe stop HvDProbe
sc.exe delete HvDProbe
```

## Safety

- Raises **HIGH_LEVEL** and disables interrupts briefly — keep probes short.
- Test machines only; risk of freeze/bugcheck if the driver is wrong.
- Do not use on production systems.
