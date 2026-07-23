#pragma once

#include "common.hpp"

// Kernel-backed modules (require HvDProbe.sys loaded as \\.\HvD).
// setupError 8 = driver not available.

ModuleResult RunKTscCpuidTimer();
ModuleResult RunAperfCpuidTimer();
ModuleResult RunInvdEmulationCheck();
