#pragma once
// TLDCoop_DLC: UTF-8 text in capitals, Polish (and any other) letters too: "Leśniczówka" -> "LEŚNICZÓWKA". An ASCII-only
// upper left ś and ó small among the capitals on the HUD.

#include <windows.h>

#include <string>

inline std::string utf8_upper(const std::string& s) {
    if (s.empty()) return s;
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return s;
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    CharUpperBuffW(w.data(), static_cast<DWORD>(w.size()));
    const int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(m), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), n, out.data(), m, nullptr, nullptr);
    return out;
}
