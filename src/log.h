#pragma once
#include <filesystem>

namespace logx {

// Opens <mod dir>/coop.log (the previous two kept as coop-prev.log, coop-prev2.log) and logs fatal faults. Safe to call once from DllMain.
void open(const std::filesystem::path& file);

void info(const char* fmt, ...);
void warn(const char* fmt, ...);
void error(const char* fmt, ...);

}  // namespace logx
