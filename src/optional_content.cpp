#include "optional_content.h"

#include <cstring>
#include <vector>

#include "il2cpp_api.h"
#include "log.h"
#include "net/content.h"

namespace optional_content {
namespace {

// TLD 2.58: TLD.OptionalContent.OptionalContentManager, a plain class with a static instance.
//   static get_Instance(), get_IsReadyToBeUsed(), get_InstalledContent(), get_OwnedContent()
// The two lists' collection type isn't in the method names, so read_ids() accepts the usual ones (List, HashSet,
// an array, a read-only wrapper of any of them) and logs the type it got otherwise.
struct Api {
    bool resolved = false;
    Il2CppClass* manager = nullptr;
    const MethodInfo* instance = nullptr;
    const MethodInfo* ready = nullptr;
    const MethodInfo* installed = nullptr;
    const MethodInfo* owned = nullptr;
};
Api api;
std::string g_last;
std::string g_logged;  // the last list logged (logged again only when it changes)

std::string type_name(const Il2CppType* t) {
    if (!t) return "?";
    char* n = il2::il2cpp_type_get_name(t);
    std::string out = n ? n : "?";
    if (n) il2::il2cpp_free(n);
    return out;
}

void resolve() {
    if (api.resolved) return;
    api.resolved = true;
    api.manager = il2::find_class("TLD.OptionalContent.dll", "TLD.OptionalContent", "OptionalContentManager");
    if (!api.manager) {
        logx::warn("dlc: TLD.OptionalContent.OptionalContentManager not found; installed DLC unknown");
        return;
    }
    api.instance = il2::find_method(api.manager, "get_Instance", 0);
    api.ready = il2::find_method_quiet(api.manager, "get_IsReadyToBeUsed", 0);
    api.installed = il2::find_method(api.manager, "get_InstalledContent", 0);
    api.owned = il2::find_method_quiet(api.manager, "get_OwnedContent", 0);
    if (api.installed)
        logx::info("dlc: OptionalContentManager.InstalledContent is %s",
                   type_name(il2::il2cpp_method_get_return_type(api.installed)).c_str());
}

Il2CppClass* class_of(Il2CppObject* o) { return o ? il2::il2cpp_object_get_class(o) : nullptr; }

std::string class_simple_name(Il2CppObject* o) {
    Il2CppClass* k = class_of(o);
    const char* n = k ? il2::il2cpp_class_get_name(k) : nullptr;
    return n ? n : "";
}

ptrdiff_t field_of(Il2CppObject* o, const char* name) {
    Il2CppClass* k = class_of(o);
    // Inherited fields too (a subclass of List<T>, say).
    for (; k; k = il2::il2cpp_class_get_parent(k))
        if (FieldInfo* f = il2::il2cpp_class_get_field_from_name(k, name))
            return static_cast<ptrdiff_t>(il2::il2cpp_field_get_offset(f));
    return -1;
}

// One element as text: a string, or (for a list of something else) "" and `ok` false.
bool element_text(Il2CppObject* e, std::string& out) {
    if (!e) return false;
    if (class_simple_name(e) != "String") return false;
    out = il2::to_utf8(reinterpret_cast<Il2CppString*>(e));
    return true;
}

// The ids in a collection of strings. False (and `why`) for anything it doesn't know how to read.
bool read_ids(Il2CppObject* coll, std::vector<std::string>& ids, std::string& why, int depth = 0) {
    if (!coll) {
        why = "null";
        return false;
    }
    const std::string kind = class_simple_name(coll);
    auto add = [&](Il2CppObject* e) -> bool {
        if (!e) return true;  // an empty slot
        std::string id;
        if (!element_text(e, id)) {
            why = kind + " of " + class_simple_name(e);
            return false;
        }
        ids.push_back(id);
        return true;
    };
    if (kind.size() > 2 && kind.compare(kind.size() - 2, 2, "[]") == 0) {  // T[]
        for (size_t i = 0, n = il2::array_length(coll); i < n; ++i)
            if (!add(il2::array_object_at(coll, i))) return false;
        return true;
    }
    if (kind == "List`1") {
        const ptrdiff_t items = field_of(coll, "_items"), size = field_of(coll, "_size");
        if (items < 0 || size < 0) {
            why = "List`1 without _items/_size";
            return false;
        }
        Il2CppObject* arr = il2::read_field<Il2CppObject*>(coll, items);
        const int32_t n = il2::read_field<int32_t>(coll, size);
        for (int32_t i = 0; i < n && static_cast<size_t>(i) < il2::array_length(arr); ++i)
            if (!add(il2::array_object_at(arr, static_cast<size_t>(i)))) return false;
        return true;
    }
    if (kind == "HashSet`1") {
        // .NET's HashSet<T>: an array of {int hashCode, int next, T value} (16 bytes for a reference T), the used
        // ones first: _slots/_lastIndex (the class libraries Unity ships) or _entries/_count (newer .NET).
        ptrdiff_t slots = field_of(coll, "_slots"), count = field_of(coll, "_lastIndex");
        if (slots < 0) slots = field_of(coll, "_entries"), count = field_of(coll, "_count");
        if (slots < 0 || count < 0) {
            why = "HashSet`1 without _slots/_entries";
            return false;
        }
        Il2CppObject* arr = il2::read_field<Il2CppObject*>(coll, slots);
        const int32_t n = il2::read_field<int32_t>(coll, count);
        const size_t len = il2::array_length(arr);
        const char* base = reinterpret_cast<const char*>(arr) + 0x20;
        for (int32_t i = 0; i < n && static_cast<size_t>(i) < len; ++i) {
            Il2CppObject* e = nullptr;
            memcpy(&e, base + static_cast<size_t>(i) * 16 + 8, sizeof(e));
            if (!add(e)) return false;
        }
        return true;
    }
    if (kind == "Dictionary`2") {
        // TLD 2.58's InstalledContent: Dictionary<string, IResourceLocator>, the content ids as its keys. .NET's
        // Dictionary: an array of {int hashCode, int next, TKey key, TValue value} (24 bytes for reference key and
        // value), the used ones first: _entries/_count (newer .NET), entries/count (older class libraries). A
        // removed entry has no key.
        ptrdiff_t entries = field_of(coll, "_entries"), count = field_of(coll, "_count");
        if (entries < 0) entries = field_of(coll, "entries"), count = field_of(coll, "count");
        if (entries < 0 || count < 0) {
            why = "Dictionary`2 without _entries/entries";
            return false;
        }
        Il2CppObject* arr = il2::read_field<Il2CppObject*>(coll, entries);
        const int32_t n = il2::read_field<int32_t>(coll, count);
        const size_t len = il2::array_length(arr);
        const char* base = reinterpret_cast<const char*>(arr) + 0x20;
        for (int32_t i = 0; i < n && static_cast<size_t>(i) < len; ++i) {
            Il2CppObject* key = nullptr;
            memcpy(&key, base + static_cast<size_t>(i) * 24 + 8, sizeof(key));
            if (!add(key)) return false;
        }
        return true;
    }
    if (depth < 2) {  // ReadOnlyCollection<T> (list), a read-only set (_set / m_set)
        for (const char* inner : {"list", "_list", "_set", "m_set", "m_list"}) {
            const ptrdiff_t off = field_of(coll, inner);
            if (off >= 0) return read_ids(il2::read_field<Il2CppObject*>(coll, off), ids, why, depth + 1);
        }
    }
    why = "a " + kind + " (not a list, set or array the mod knows)";
    return false;
}

}  // namespace

std::string installed() {
    resolve();
    std::string result = net::kContentUnknown;
    Il2CppObject* mgr = api.instance ? il2::invoke(api.instance, nullptr) : nullptr;
    if (!api.installed) {
        // logged by resolve()
    } else if (!mgr) {
        logx::warn("dlc: TLD's optional content manager isn't there yet; installed DLC unknown");
    } else {
        bool ready = true;
        if (api.ready && !il2::invoke_value(api.ready, mgr, ready)) ready = false;
        if (!ready) {
            logx::warn("dlc: TLD's optional content manager isn't ready; installed DLC unknown");
        } else {
            std::vector<std::string> ids;
            std::string why;
            if (read_ids(il2::invoke(api.installed, mgr), ids, why)) {
                result = net::content_join(ids);
                if (result.size() >= net::kContentLen) {
                    logx::warn("dlc: the installed DLC list is too long to send (%zu bytes); treating it as unknown",
                               result.size());
                    result = net::kContentUnknown;
                }
            } else {
                logx::warn("dlc: couldn't read InstalledContent (%s); installed DLC unknown", why.c_str());
            }
            if (result != g_logged) {
                std::string owned_text = "?";
                std::vector<std::string> owned;
                std::string owned_why;
                if (api.owned && read_ids(il2::invoke(api.owned, mgr), owned, owned_why))
                    owned_text = net::content_join(owned);
                logx::info("dlc: installed optional content: %s (owned: %s)",
                           result.empty() ? "none" : result.c_str(), owned_text.empty() ? "none" : owned_text.c_str());
                g_logged = result;
            }
        }
    }
    g_last = result;
    return result;
}

const std::string& last() { return g_last; }

}  // namespace optional_content
