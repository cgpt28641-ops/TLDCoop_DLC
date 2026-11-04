#include "coop_save.h"

#include <windows.h>

#include <cstdint>
#include <string>

#include "hooks.h"
#include "il2cpp_api.h"
#include "coop_report.h"
#include "log.h"
#include "net_runtime.h"

namespace coop_save {
namespace {

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kItemId = "tldcoop_save";
constexpr int32_t kItemValue = 100;  // not one of TLD's PauseMenuItemType values (0-4)
// TLDCoop_DLC: "POBIERZ LOGI" (Karol: one click for everyone's newest logs), in co-op only
constexpr const char* kLogsId = "tldcoop_logs";
constexpr int32_t kLogsValue = 101;

struct Color {
    float r, g, b, a;
};

struct Api {
    ptrdiff_t basic_menu = -1;                    // Panel_PauseMenu.m_BasicMenu
    const MethodInfo* add_item = nullptr;         // BasicMenu.AddItem(string, int, int, string, string, string, Action, Color, Color)
    const MethodInfo* item_count = nullptr;       // BasicMenu.GetItemCount()
    const MethodInfo* has_item = nullptr;         // BasicMenu.HasActiveItemByID(string)
    const MethodInfo* selected_id = nullptr;      // BasicMenu.GetSelectedItemId()
    const MethodInfo* refresh = nullptr;          // BasicMenu.Refresh()
    const MethodInfo* on_done = nullptr;          // Panel_PauseMenu.OnDone(): back to the game
    const MethodInfo* save = nullptr;             // static GameManager.SaveGameAndDisplayHUDMessage()
    ptrdiff_t models = -1;                        // BasicMenu.m_ItemModelList (List<BasicMenuItemModel>)
    ptrdiff_t model_id = -1;                      // BasicMenuItemModel.m_Id
} api;

uintptr_t g_menu = 0;     // the pause menu's BasicMenu (GC handle)
int32_t g_index = -1;     // our row in it
double g_save_at = 0;     // save once the menu has closed (a frame later)

double now_ms() { return static_cast<double>(GetTickCount64()); }

Il2CppObject* menu() { return g_menu ? il2::il2cpp_gchandle_get_target(g_menu) : nullptr; }

using ConfigureFn = void (*)(Il2CppObject* self, const MethodInfo* method);
ConfigureFn g_configure = nullptr;
using ClickFn = void (*)(Il2CppObject* self, int32_t index, const MethodInfo* method);
ClickFn g_clicked = nullptr, g_double_clicked = nullptr;
using ConfirmFn = void (*)(Il2CppObject* self, const MethodInfo* method);
ConfirmFn g_confirm = nullptr;
Il2CppObject* g_pause_panel = nullptr;  // (only touched on the main thread, while its menu is up)

// TLD has built the pause menu's rows: ours goes last.
void configure_detour(Il2CppObject* self, const MethodInfo* method) {
    g_configure(self, method);
    if (!self || api.basic_menu < 0 || !api.add_item) return;
    Il2CppObject* m = il2::read_field<Il2CppObject*>(self, api.basic_menu);
    if (!m) return;
    g_pause_panel = self;
    if (g_menu) il2::il2cpp_gchandle_free(g_menu);
    g_menu = il2::il2cpp_gchandle_new(m, false);
    bool has = false;
    void* has_args[] = {il2::il2cpp_string_new(kItemId)};
    if (api.has_item) il2::invoke_value(api.has_item, m, has, has_args);
    int32_t count = 0;
    if (api.item_count) il2::invoke_value(api.item_count, m, count);
    if (!has) {
    int32_t value = kItemValue, index = count;
    Color normal{1, 1, 1, 1}, highlight{1, 1, 1, 1};
    void* args[] = {il2::il2cpp_string_new(kItemId), &value, &index, il2::il2cpp_string_new("ZAPISZ GRĘ"),
                    il2::il2cpp_string_new("Zapisuje grę teraz."), il2::il2cpp_string_new(""), nullptr, &normal,
                    &highlight};
    il2::invoke(api.add_item, m, args);
    g_index = index;
    if (api.refresh) il2::invoke(api.refresh, m);
    static int logged = 0;
    if (logged++ < 3) logx::info("save: 'ZAPISZ GRĘ' added to ESC's menu (row %d)", index);
    }
    // TLDCoop_DLC: everyone's logs, while playing together
    bool has_logs = false;
    void* has_logs_args[] = {il2::il2cpp_string_new(kLogsId)};
    if (api.has_item) il2::invoke_value(api.has_item, m, has_logs, has_logs_args);
    if (has_logs || !netrt::status().connected) return;
    if (api.item_count) il2::invoke_value(api.item_count, m, count);
    int32_t lv = kLogsValue, li = count;
    Color normal{1, 1, 1, 1}, highlight{1, 1, 1, 1};
    void* largs[] = {il2::il2cpp_string_new(kLogsId), &lv, &li, il2::il2cpp_string_new("POBIERZ LOGI"),
                     il2::il2cpp_string_new("Zbiera najnowsze logi wszystkich graczy do TLDCoop\\reports."),
                     il2::il2cpp_string_new(""), nullptr, &normal, &highlight};
    il2::invoke(api.add_item, m, largs);
    if (api.refresh) il2::invoke(api.refresh, m);
}

// The id of the menu's row `index` ("" if it can't be read).
bool row_id(Il2CppObject* m, int32_t index, std::string& out) {
    if (!m || api.models < 0 || api.model_id < 0 || index < 0) return false;
    Il2CppObject* list = il2::read_field<Il2CppObject*>(m, api.models);
    if (!list) return false;
    Il2CppObject* items = il2::read_field<Il2CppObject*>(list, 0x10);
    const int32_t size = il2::read_field<int32_t>(list, 0x18);
    if (index >= size || static_cast<size_t>(index) >= il2::array_length(items)) return false;
    Il2CppObject* model = il2::array_object_at(items, static_cast<size_t>(index));
    Il2CppObject* id = model ? il2::read_field<Il2CppObject*>(model, api.model_id) : nullptr;
    out = id ? il2::to_utf8(reinterpret_cast<Il2CppString*>(id)) : std::string();
    return true;
}
// Our row, by its id (the row's index only when the rows can't be read).
bool ours(Il2CppObject* self, int32_t index) {
    if (!self || self != menu()) return false;
    std::string id;
    if (row_id(self, index, id)) return id == kItemId;
    return index == g_index && g_index >= 0;
}
bool logs_row(Il2CppObject* self, int32_t index) {
    std::string id;
    return self && self == menu() && row_id(self, index, id) && id == kLogsId;
}
void do_logs_from_menu() {
    logx::info("report: 'POBIERZ LOGI' chosen");
    if (g_pause_panel && api.on_done) il2::invoke(api.on_done, g_pause_panel);
    coop_report::collect_all();
}

void do_save_from_menu() {
    logx::info("save: 'ZAPISZ GRĘ' chosen; the game saves as the menu closes");
    if (g_pause_panel && api.on_done) il2::invoke(api.on_done, g_pause_panel);
    g_save_at = now_ms() + 300;
}

void clicked_detour(Il2CppObject* self, int32_t index, const MethodInfo* method) {
    if (ours(self, index)) return do_save_from_menu();  // (our row has no Action of its own)
    if (logs_row(self, index)) return do_logs_from_menu();
    g_clicked(self, index, method);
}

void double_clicked_detour(Il2CppObject* self, int32_t index, const MethodInfo* method) {
    if (ours(self, index)) return do_save_from_menu();
    if (logs_row(self, index)) return do_logs_from_menu();
    g_double_clicked(self, index, method);
}

void confirm_detour(Il2CppObject* self, const MethodInfo* method) {
    if (self && self == menu() && api.selected_id) {
        if (Il2CppObject* id = il2::invoke(api.selected_id, self)) {
            const std::string sid = il2::to_utf8(reinterpret_cast<Il2CppString*>(id));
            if (sid == kItemId) return do_save_from_menu();
            if (sid == kLogsId) return do_logs_from_menu();
        }
    }
    g_confirm(self, method);
}

}  // namespace

void init() {
    Il2CppClass* pause = il2::find_class(kGame, "", "Panel_PauseMenu");
    Il2CppClass* basic = il2::find_class(kGame, "", "BasicMenu");
    Il2CppClass* gm = il2::find_class(kGame, "", "GameManager");
    if (!pause || !basic || !gm) {
        logx::warn("save: TLD's pause menu not found; no save button");
        return;
    }
    api.basic_menu = il2::field_offset(pause, "m_BasicMenu");
    api.add_item = il2::find_method(basic, "AddItem", 9);
    api.item_count = il2::find_method(basic, "GetItemCount", 0);
    api.has_item = il2::find_method(basic, "HasActiveItemByID", 1);
    api.selected_id = il2::find_method(basic, "GetSelectedItemId", 0);
    api.refresh = il2::find_method_quiet(basic, "Refresh", 0);
    api.on_done = il2::find_method(pause, "OnDone", 0);
    api.save = il2::find_method(gm, "SaveGameAndDisplayHUDMessage", 0);
    const MethodInfo* configure = il2::find_method(pause, "ConfigureMenu", 0);
    const MethodInfo* clicked = il2::find_method(basic, "OnItemClicked", 1);
    const MethodInfo* double_clicked = il2::find_method(basic, "OnItemDoubleClicked", 1);
    const MethodInfo* confirm = il2::find_method(basic, "OnConfirm", 0);
    api.models = il2::field_offset_quiet(basic, "m_ItemModelList");
    if (Il2CppClass* model = il2::find_nested_class(basic, "BasicMenuItemModel")) api.model_id = il2::field_offset_quiet(model, "m_Id");
    // The handlers first: the row is added only once a click on it is ours (else it would do nothing).
    bool ok = api.basic_menu >= 0 && api.add_item && api.save && configure && clicked &&
              hooks::install_method("BasicMenu.OnItemClicked", clicked, reinterpret_cast<void*>(&clicked_detour),
                                    reinterpret_cast<void**>(&g_clicked));
    if (ok && double_clicked)
        hooks::install_method("BasicMenu.OnItemDoubleClicked", double_clicked, reinterpret_cast<void*>(&double_clicked_detour),
                              reinterpret_cast<void**>(&g_double_clicked));
    if (ok && confirm)
        hooks::install_method("BasicMenu.OnConfirm", confirm, reinterpret_cast<void*>(&confirm_detour),
                              reinterpret_cast<void**>(&g_confirm));
    ok = ok && hooks::install_method("Panel_PauseMenu.ConfigureMenu", configure, reinterpret_cast<void*>(&configure_detour),
                                     reinterpret_cast<void**>(&g_configure));
    logx::info("save: %s", ok ? "ready ('ZAPISZ GRĘ' in ESC's menu)" : "UNAVAILABLE (TLD API missing)");
}

void on_frame(bool in_game) {
    if (g_save_at <= 0 || now_ms() < g_save_at) return;
    g_save_at = 0;
    if (!in_game || !api.save) return;
    il2::invoke(api.save, nullptr);
    logx::info("save: the game saved (from ESC's menu)");
}

}  // namespace coop_save
