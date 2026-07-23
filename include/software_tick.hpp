#pragma once

#include "common.hpp"
#include "cpu_topology.hpp"

ModuleResult RunSoftwareTickTimer(
    const BenchmarkOptions& options,
    LogicalCpu testCpu,
    LogicalCpu clockCpu,
    unsigned maximumLeaf,
    bool serializeSupported);
