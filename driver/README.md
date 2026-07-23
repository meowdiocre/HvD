# HvDProbe.sys

Kernel probe driver for HvD (`\\.\HvD`).

## Build (Visual Studio + WDK)

**Requires:** VS 2022 with **Windows Driver Kit** (WDK) matching your SDK (e.g. 10.0.26100).


## Probes

| IOCTL | Flag in HvD.exe | Behavior |
| --- | --- | --- |
| `HVD_IOCTL_K_TSC_CPUID` | `--k-tsc-cpuid` | Pin + HIGH_LEVEL, leaf1 100+100 |
| `HVD_IOCTL_APERF_CPUID` | `--aperf-cpuid` | APERF/MPERF around CPUID |
| `HVD_IOCTL_INVD` | `--invd` | Disabled; returns unsupported |
| | `--kernel` | K-TSC-CPUID and APERF-CPUID |

## Load (test machine only)

Unsigned drivers need **test signing** or BYOVD
