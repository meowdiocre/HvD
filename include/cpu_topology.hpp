#pragma once

#include <windows.h>

#include <vector>

struct LogicalCpu {
    WORD group;
    BYTE number;
};

bool PinCurrentThread(LogicalCpu cpu);
std::vector<LogicalCpu> FirstLogicalCpuPerCore();
