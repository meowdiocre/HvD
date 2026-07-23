#include "cpu_topology.hpp"

bool PinCurrentThread(const LogicalCpu cpu)
{
    GROUP_AFFINITY affinity{};
    affinity.Group = cpu.group;
    affinity.Mask = KAFFINITY{1} << cpu.number;
    return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != FALSE;
}

std::vector<LogicalCpu> FirstLogicalCpuPerCore()
{
    DWORD bytes = 0;
    if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
        return {};
    }

    std::vector<BYTE> storage(bytes);
    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data()),
            &bytes)) {
        return {};
    }

    std::vector<LogicalCpu> result;
    for (DWORD offset = 0; offset < bytes;) {
        auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
            storage.data() + offset);
        if (entry->Size == 0 || entry->Size > bytes - offset) {
            return {};
        }
        if (entry->Relationship == RelationProcessorCore) {
            const auto& relationship = entry->Processor;
            for (WORD groupIndex = 0; groupIndex < relationship.GroupCount;
                 ++groupIndex) {
                const GROUP_AFFINITY& group = relationship.GroupMask[groupIndex];
                unsigned long number = 0;
                if (_BitScanForward64(&number, group.Mask)) {
                    result.push_back({group.Group, static_cast<BYTE>(number)});
                    break;
                }
            }
        }
        offset += entry->Size;
    }
    return result;
}
