#include "common.hpp"

#include <windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {

constexpr const char* kReset = "\x1b[0m";
constexpr const char* kBold = "\x1b[1m";
constexpr const char* kDim = "\x1b[2m";
constexpr const char* kRed = "\x1b[31m";
constexpr const char* kGreen = "\x1b[32m";
constexpr const char* kYellow = "\x1b[33m";
constexpr const char* kCyan = "\x1b[36m";
constexpr const char* kWhite = "\x1b[37m";

bool g_colorEnabled = false;

const char* ColorForValue(const std::string& value)
{
    if (value.find("PASS") != std::string::npos) return kGreen;
    if (value.find("FAIL") != std::string::npos) return kRed;
    if (value.find("SETUP_ERROR") != std::string::npos) return kYellow;
    if (value.find("unavailable") != std::string::npos) return kDim;
    return nullptr;
}

const char* ColorForTitle(const ModuleOutcome* outcome)
{
    if (outcome == nullptr) return kCyan;
    if (outcome->setupError >= 2) return kYellow;
    if (outcome->gated && !outcome->passed) return kRed;
    if (outcome->gated && outcome->passed) return kGreen;
    return kCyan;
}

void PrintPadded(
    const std::string& text,
    const std::size_t width,
    const bool rightAlign = false)
{
    if (rightAlign) {
        for (std::size_t i = text.size(); i < width; ++i) std::putchar(' ');
    }
    std::fputs(text.c_str(), stdout);
    if (!rightAlign) {
        for (std::size_t i = text.size(); i < width; ++i) std::putchar(' ');
    }
}

}  // namespace

bool ParseOptions(const int argc, char** argv, BenchmarkOptions& options)
{
    options = {};
    bool moduleSpecified = false;
    bool sampleSpecified = false;
    unsigned selected = 0;
    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];
        if (argument[0] != '-') {
            if (sampleSpecified) return false;
            char* end = nullptr;
            errno = 0;
            const unsigned long value = std::strtoul(argument, &end, 10);
            if (errno == ERANGE || end == argument || *end != '\0' ||
                value < 10000 || value > 10000000) {
                return false;
            }
            options.sampleCount = static_cast<unsigned>(value);
            sampleSpecified = true;
        } else if (std::strcmp(argument, "--software-tick") == 0) {
            selected |= static_cast<unsigned>(BenchmarkModule::SoftwareTick);
            moduleSpecified = true;
        } else if (std::strcmp(argument, "--tsc-exit") == 0) {
            selected |= static_cast<unsigned>(BenchmarkModule::TscExit);
            moduleSpecified = true;
        } else if (std::strcmp(argument, "--tsc-cpuid") == 0) {
            selected |= static_cast<unsigned>(BenchmarkModule::TscCpuid);
            moduleSpecified = true;
        } else if (std::strcmp(argument, "--all") == 0) {
            selected |= static_cast<unsigned>(BenchmarkModule::All);
            moduleSpecified = true;
        } else if (std::strcmp(argument, "--vmcall") == 0) {
            options.vmcall = true;
        } else if (std::strcmp(argument, "--plain") == 0) {
            options.plain = true;
        } else {
            return false;
        }
    }
    options.modules = static_cast<BenchmarkModule>(
        moduleSpecified ? selected : static_cast<unsigned>(BenchmarkModule::All));
    return true;
}

bool SoftwareTickPasses(const double ratio)
{
    return ratio < 2.5;
}

bool TscExitPasses(const std::uint64_t average)
{
    return average > 0 && average < 1000;
}

SoftwareTickTripwire DetectSoftwareTickTripwire(
    const double serializeTrimmedMean,
    const double leaf0TrimmedMean)
{
    return {
        serializeTrimmedMean == 1.0 || leaf0TrimmedMean == 1.0,
        serializeTrimmedMean > 2000.0 || leaf0TrimmedMean > 2000.0};
}

int CombineOutcome(const int currentExitCode, const ModuleOutcome& outcome)
{
    if (currentExitCode >= 2) return currentExitCode;
    if (outcome.setupError != 0) return outcome.setupError;
    if (currentExitCode == 1) return currentExitCode;
    return outcome.gated && !outcome.passed ? 1 : 0;
}

void EnableConsoleColor()
{
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || out == nullptr) return;
    DWORD mode = 0;
    if (!GetConsoleMode(out, &mode)) return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (SetConsoleMode(out, mode)) g_colorEnabled = true;
}

void PrintBanner(const bool plain)
{
    const bool color = g_colorEnabled && !plain;
    const char* reset = color ? kReset : "";
    const char* cyan = color ? kCyan : "";
    const char* bold = color ? kBold : "";
    const char* dim = color ? kDim : "";
    const char* white = color ? kWhite : "";

    static const char* const kArt[] = {
        "  ██╗  ██╗██╗   ██╗██████╗ ",
        "  ██║  ██║██║   ██║██╔══██╗",
        "  ███████║██║   ██║██║  ██║",
        "  ██╔══██║╚██╗ ██╔╝██║  ██║",
        "  ██║  ██║ ╚████╔╝ ██████╔╝",
        "  ╚═╝  ╚═╝  ╚═══╝  ╚═════╝ ",
    };

    std::putchar('\n');
    if (color) std::fputs(cyan, stdout);
    if (color) std::fputs(bold, stdout);
    for (const char* line : kArt) {
        std::puts(line);
    }
    if (color) std::fputs(reset, stdout);

    if (color) std::fputs(white, stdout);
    if (color) std::fputs(bold, stdout);
    std::puts("  Hypervisor Detector R0 - R1");
    if (color) std::fputs(reset, stdout);

    if (color) std::fputs(dim, stdout);
    std::puts("  Timing · MSR · research Type-1 / blue-pill hosts");
    std::puts("  modules: software-tick · tsc-exit · tsc-cpuid");
    if (color) std::fputs(reset, stdout);
    std::putchar('\n');
}

void PrintResult(const ModuleResult& result, const bool plain)
{
    const bool color = g_colorEnabled && !plain;
    const char* titleColor = color ? ColorForTitle(&result.outcome) : "";
    const char* reset = color ? kReset : "";
    const char* dim = color ? kDim : "";
    const char* bold = color ? kBold : "";

    if (!result.description.empty()) {
        if (color) std::fputs(dim, stdout);
        std::printf("  %s\n", result.description.c_str());
        if (color) std::fputs(reset, stdout);
    }

    if (plain) {
        std::printf("%s:\n", result.title.c_str());
        for (const PanelRow& row : result.rows) {
            std::fputs(row.name.c_str(), stdout);
            for (const std::string& value : row.values) {
                std::printf(" | %s", value.c_str());
            }
            std::putchar('\n');
        }
        std::putchar('\n');
        return;
    }

    std::size_t nameWidth = 0;
    std::size_t columnCount = 1;
    for (const PanelRow& row : result.rows) {
        nameWidth = (std::max)(nameWidth, row.name.size());
        columnCount = (std::max)(columnCount, row.values.size());
    }

    std::vector<std::size_t> columnWidths(columnCount);
    std::size_t widestSpanningValue = 0;
    for (const PanelRow& row : result.rows) {
        if (row.values.size() <= 1) {
            if (!row.values.empty()) {
                widestSpanningValue =
                    (std::max)(widestSpanningValue, row.values.front().size());
            }
            continue;
        }
        for (std::size_t index = 0; index < row.values.size(); ++index) {
            columnWidths[index] =
                (std::max)(columnWidths[index], row.values[index].size());
        }
    }

    std::size_t columnAreaWidth = 0;
    for (const std::size_t columnWidth : columnWidths) {
        columnAreaWidth += columnWidth;
    }
    if (columnCount > 1) {
        columnAreaWidth += (columnCount - 1) * 3;
    }

    const std::size_t valueWidth =
        (std::max)(columnAreaWidth, widestSpanningValue);
    const std::size_t width =
        (std::max)(result.title.size() + 3, nameWidth + valueWidth + 5);

    if (color) std::fputs(titleColor, stdout);
    std::printf("┌─ ");
    if (color) std::fputs(bold, stdout);
    std::printf("%s", result.title.c_str());
    if (color) std::fputs(reset, stdout);
    if (color) std::fputs(titleColor, stdout);
    std::putchar(' ');
    for (std::size_t i = result.title.size() + 3; i < width; ++i) {
        std::printf("─");
    }
    std::printf("┐");
    if (color) std::fputs(reset, stdout);
    std::putchar('\n');

    for (const PanelRow& row : result.rows) {
        if (color) std::fputs(dim, stdout);
        std::printf("│ ");
        if (color) std::fputs(reset, stdout);
        if (color) std::fputs(kWhite, stdout);
        PrintPadded(row.name, nameWidth);
        if (color) std::fputs(reset, stdout);
        if (color) std::fputs(dim, stdout);
        std::printf(" | ");
        if (color) std::fputs(reset, stdout);

        if (row.values.size() <= 1) {
            static const std::string empty;
            const std::string& value =
                row.values.empty() ? empty : row.values.front();
            const char* valueColor = color ? ColorForValue(value) : nullptr;
            if (valueColor != nullptr) {
                std::fputs(valueColor, stdout);
                if (color) std::fputs(bold, stdout);
            }
            PrintPadded(value, valueWidth, row.rightAlignValues);
            if (valueColor != nullptr) std::fputs(reset, stdout);
        } else {
            for (std::size_t index = 0; index < columnCount; ++index) {
                if (index != 0) {
                    if (color) std::fputs(dim, stdout);
                    std::printf(" | ");
                    if (color) std::fputs(reset, stdout);
                }
                static const std::string empty;
                const std::string& value =
                    index < row.values.size() ? row.values[index] : empty;
                const char* valueColor = color ? ColorForValue(value) : nullptr;
                if (valueColor != nullptr) {
                    std::fputs(valueColor, stdout);
                    if (color) std::fputs(bold, stdout);
                }
                PrintPadded(
                    value, columnWidths[index], row.rightAlignValues);
                if (valueColor != nullptr) std::fputs(reset, stdout);
            }
            for (std::size_t i = columnAreaWidth; i < valueWidth; ++i) {
                std::putchar(' ');
            }
        }

        if (color) std::fputs(dim, stdout);
        for (std::size_t i = nameWidth + valueWidth + 5; i < width; ++i) {
            std::putchar(' ');
        }
        std::printf(" │");
        if (color) std::fputs(reset, stdout);
        std::putchar('\n');
    }

    if (color) std::fputs(titleColor, stdout);
    std::printf("└");
    for (std::size_t i = 0; i < width; ++i) std::printf("─");
    std::printf("┘");
    if (color) std::fputs(reset, stdout);
    std::printf("\n\n");
}

void PrintRunSummary(const int exitCode, const bool plain)
{
    const bool color = g_colorEnabled && !plain;
    const char* reset = color ? kReset : "";
    const char* bold = color ? kBold : "";

    const char* status = "OK";
    const char* statusColor = kGreen;
    const char* detail = "completed";

    if (exitCode == 1) {
        status = "FAIL";
        statusColor = kRed;
        detail = "a gated threshold failed";
    } else if (exitCode >= 2) {
        status = "SETUP";
        statusColor = kYellow;
        detail = "setup failed; see the module output";
    }

    if (color) std::fputs(statusColor, stdout);
    if (color) std::fputs(bold, stdout);
    std::printf("summary: %s", status);
    if (color) std::fputs(reset, stdout);
    std::printf("  exit=%d  %s\n", exitCode, detail);
}

ModuleResult MakeSetupErrorResult(
    const char* const title,
    const int code,
    const std::string& message)
{
    char result[48];
    std::snprintf(result, sizeof(result), "SETUP_ERROR code=%d", code);
    return {
        title,
        {{"setup", {message}}, {"result", {result}}},
        {false, false, code},
        {}};
}

void PrintUsage(FILE* const output)
{
    std::fputs(
        "HvD.exe [samples] [flags]\n\n"
        "  samples              default 200000; software-tick only\n\n"
        "  --all                all modules\n"
        "  --software-tick\n"
        "  --tsc-exit\n"
        "  --tsc-cpuid\n"
        "  --vmcall             include VMCALL in software-tick\n"
        "  --plain              text framing, no color\n",
        output);
}
