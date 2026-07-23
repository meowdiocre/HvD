#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

enum class BenchmarkModule : unsigned {
    SoftwareTick = 1,
    TscExit = 2,
    TscCpuid = 4,
    All = 7
};

struct BenchmarkOptions {
    unsigned sampleCount = 200000;
    BenchmarkModule modules = BenchmarkModule::All;
    bool vmcall = false;
    bool plain = false;
};

struct PanelRow {
    std::string name;
    std::vector<std::string> values;
    bool rightAlignValues = false;
};

struct ModuleOutcome {
    bool gated;
    bool passed;
    int setupError;
};

struct ModuleResult {
    std::string title;
    std::vector<PanelRow> rows;
    ModuleOutcome outcome;
    std::string description;
};

struct SoftwareTickTripwire {
    bool equalOne;
    bool greaterThan2000;
};

bool ParseOptions(int argc, char** argv, BenchmarkOptions& options);
bool SoftwareTickPasses(double ratio);
bool TscExitPasses(std::uint64_t average);
SoftwareTickTripwire DetectSoftwareTickTripwire(
    double serializeTrimmedMean,
    double leaf0TrimmedMean);
int CombineOutcome(int currentExitCode, const ModuleOutcome& outcome);
void EnableConsoleColor();
void PrintBanner(bool plain);
void PrintResult(const ModuleResult& result, bool plain);
void PrintRunSummary(int exitCode, bool plain);
ModuleResult MakeSetupErrorResult(
    const char* title,
    int code,
    const std::string& message);
void PrintUsage(FILE* output);
