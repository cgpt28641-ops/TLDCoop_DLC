#include "custom_text.h"

#include <windows.h>

#include <cctype>
#include <string>
#include <unordered_map>

#include "il2cpp_api.h"

namespace custom_text {
namespace {

struct Entry {
    std::string en, pl;
    uintptr_t en_s = 0, pl_s = 0;  // gchandles: the managed strings, made on first use
};
std::unordered_map<std::u16string, Entry>& table() {
    static std::unordered_map<std::u16string, Entry> t;
    return t;
}

std::u16string wide(const char* s) {
    std::u16string out;
    for (const char* p = s; *p; ++p) out.push_back(static_cast<char16_t>(static_cast<unsigned char>(*p)));  // keys: ASCII
    return out;
}

// The game's language is Polish: Localization.s_Language, read (no managed call: this runs inside TLD's
// Localization.Get, also while its tables load) once a second at most.
bool polish() {
    static FieldInfo* field = nullptr;
    static bool looked = false;
    static bool pl = false;
    static unsigned long long last = 0;
    if (!looked) {
        looked = true;
        if (Il2CppClass* loc = il2::find_class("Assembly-CSharp.dll", "", "Localization"))
            field = il2::il2cpp_class_get_field_from_name(loc, "s_Language");
    }
    const unsigned long long now = GetTickCount64();
    if (field && now - last > 1000) {
        last = now;
        Il2CppString* value = nullptr;
        il2::il2cpp_field_static_get_value(field, &value);
        std::string lang = value ? il2::to_utf8(value) : std::string();
        for (char& c : lang) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        pl = lang.find("polish") != std::string::npos || lang.find("polski") != std::string::npos;
    }
    return pl;
}

}  // namespace

void add(const char* key, const char* english, const char* polish_text) {
    Entry& e = table()[wide(key)];
    e.en = english ? english : "";
    e.pl = polish_text && *polish_text ? polish_text : e.en;
}

Il2CppString* find(Il2CppString* key) {
    if (!key || table().empty()) return nullptr;
    const auto* chars = reinterpret_cast<const char16_t*>(il2::il2cpp_string_chars(key));
    const int32_t n = il2::il2cpp_string_length(key);
    if (!chars || n <= 0) return nullptr;
    auto it = table().find(std::u16string(chars, static_cast<size_t>(n)));
    if (it == table().end()) return nullptr;
    Entry& e = it->second;
    const bool pl = polish();
    uintptr_t& h = pl ? e.pl_s : e.en_s;
    Il2CppObject* s = h ? il2::il2cpp_gchandle_get_target(h) : nullptr;
    if (!s) {
        s = reinterpret_cast<Il2CppObject*>(il2::il2cpp_string_new((pl ? e.pl : e.en).c_str()));
        h = s ? il2::il2cpp_gchandle_new(s, false) : 0;
    }
    return reinterpret_cast<Il2CppString*>(s);
}

std::string text(const char* key) {
    auto it = table().find(wide(key));
    if (it == table().end()) return {};
    return polish() ? it->second.pl : it->second.en;
}

}  // namespace custom_text
