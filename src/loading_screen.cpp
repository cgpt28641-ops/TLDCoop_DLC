#include "loading_screen.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "hooks.h"
#include "il2cpp_api.h"
#include "log.h"

namespace loading_screen {
namespace {

#include "loading_screen_data.inc"

constexpr const char* kGame = "Assembly-CSharp.dll";
constexpr const char* kCore = "UnityEngine.CoreModule.dll";
constexpr ptrdiff_t kCachedPtr = 0x10;
constexpr int kDontUnload = 32;  // HideFlags.DontUnloadUnusedAsset
constexpr const char* kName = "MOD_TLDCoopStartScreen";

struct Rect { float x, y, w, h; };
struct Vector3 { float x, y, z; };

struct Api {
    bool ok = false;
    Il2CppClass *go_class = nullptr, *tex_class = nullptr, *byte_class = nullptr;
    Il2CppObject* t_uitexture = nullptr;
    const MethodInfo *go_ctor = nullptr, *go_add = nullptr, *go_tf = nullptr, *go_active = nullptr, *go_set_active = nullptr;
    const MethodInfo *go_layer = nullptr, *go_set_layer = nullptr, *component_go = nullptr;
    const MethodInfo *tf_set_parent = nullptr, *tf_set_local_pos = nullptr, *tf_set_local_scale = nullptr;
    const MethodInfo *tf_child_count = nullptr, *tf_child = nullptr, *tf_parent = nullptr;
    const MethodInfo *hide_flags = nullptr, *tex_ctor = nullptr, *load_image = nullptr;
    const MethodInfo *set_main = nullptr, *set_uv = nullptr, *set_width = nullptr, *set_height = nullptr;
    const MethodInfo *get_depth = nullptr, *set_depth = nullptr, *set_alpha = nullptr, *mark_changed = nullptr;
    const MethodInfo *screen_w = nullptr, *screen_h = nullptr, *pixel_adjust = nullptr;
    ptrdiff_t main_disclaimer = -1, main_continue = -1, screens = -1, screen_continues = -1;
    ptrdiff_t loading_flow = -1, boot_flow = -1, loading_anims = -1, loading_continue = -1;
} api;

bool alive(Il2CppObject* o) { return o && il2::read_field<void*>(o, kCachedPtr) != nullptr; }
Il2CppObject* target(uintptr_t h) { return h ? il2::il2cpp_gchandle_get_target(h) : nullptr; }

constexpr int kPictures = static_cast<int>(sizeof(kStartPictures) / sizeof(kStartPictures[0]));
uintptr_t g_tex[kPictures] = {}, g_widget = 0;
int g_shown = -1;                    // which picture the widget has
std::vector<Il2CppObject*> g_screens;  // TLD's boot screens (roots) in the order they came: the n-th gets picture n
bool g_failed = false, g_logged = false;
int g_last_w = 0, g_last_h = 0;

void fail(const char* why) {
    if (!g_failed) logx::warn("start screen: TLD's own stays: %s", why);
    g_failed = true;
}

Il2CppObject* picture(int n) {
    if (Il2CppObject* t = target(g_tex[n]); alive(t)) return t;
    const StartPicture& pic = kStartPictures[n];
    Il2CppObject* bytes = il2::il2cpp_array_new(api.byte_class, pic.size);
    Il2CppObject* tex = il2::il2cpp_object_new(api.tex_class);
    if (!bytes || !tex) return nullptr;
    memcpy(reinterpret_cast<char*>(bytes) + 0x20, pic.jpg, pic.size);
    int32_t w = 2, h = 2;
    void* ca[] = {&w, &h};
    il2::invoke(api.tex_ctor, tex, ca);
    void* la[] = {tex, bytes};
    bool loaded = false;
    if (!il2::invoke_value(api.load_image, nullptr, loaded, la) || !loaded) return nullptr;
    int32_t flags = kDontUnload;
    void* fa[] = {&flags};
    il2::invoke(api.hide_flags, tex, fa);
    g_tex[n] = il2::il2cpp_gchandle_new(tex, false);
    return tex;
}

Il2CppObject* go_of(Il2CppObject* component) { return alive(component) ? il2::invoke(api.component_go, component) : nullptr; }
Il2CppObject* tf_of(Il2CppObject* go) { return go ? il2::invoke(api.go_tf, go) : nullptr; }

void set_active(Il2CppObject* go, bool on) {
    if (!alive(go)) return;
    bool now = false;
    il2::invoke_value(api.go_active, go, now);
    if (now == on) return;
    void* a[] = {&on};
    il2::invoke(api.go_set_active, go, a);
}

bool is_ancestor(Il2CppObject* tf, Il2CppObject* of) {  // tf is `of` or one of its parents
    for (int i = 0; of && i < 32; ++i) {
        if (of == tf) return true;
        of = il2::invoke(api.tf_parent, of);
    }
    return false;
}

// Everything under `root` off, except the way down to the labels we keep (and those labels)
void hide_all_but(Il2CppObject* root_go, const std::vector<Il2CppObject*>& keep_tfs) {
    Il2CppObject* root = tf_of(root_go);
    if (!root) return;
    int32_t n = 0;
    il2::invoke_value(api.tf_child_count, root, n);
    for (int32_t i = 0; i < n; ++i) {
        void* a[] = {&i};
        Il2CppObject* child = il2::invoke(api.tf_child, root, a);
        if (!child) continue;
        bool on_way = false, is_kept = false;
        for (Il2CppObject* k : keep_tfs) {
            if (child == k) is_kept = true;
            else if (is_ancestor(child, k)) on_way = true;
        }
        if (is_kept) continue;
        Il2CppObject* go = go_of(child);
        if (on_way) hide_all_but(go, keep_tfs);
        else set_active(go, false);
    }
}

// Our picture under the panel: full screen (NGUI units), the picture cut to the screen's proportions
bool show(Il2CppObject* panel, int depth, int n) {
    Il2CppObject* widget = target(g_widget);
    if (!alive(widget)) {
        Il2CppObject* go = il2::il2cpp_object_new(api.go_class);
        void* na[] = {il2::il2cpp_string_new(kName)};
        il2::invoke(api.go_ctor, go, na);
        Il2CppObject* panel_go = go_of(panel);
        bool stays = false;
        void* pa[] = {tf_of(panel_go), &stays};
        il2::invoke(api.tf_set_parent, tf_of(go), pa);
        int32_t layer = 0;
        if (il2::invoke_value(api.go_layer, panel_go, layer)) {
            void* la[] = {&layer};
            il2::invoke(api.go_set_layer, go, la);
        }
        Vector3 zero{0, 0, 0}, one{1, 1, 1};
        void* za[] = {&zero};
        il2::invoke(api.tf_set_local_pos, tf_of(go), za);
        void* oa[] = {&one};
        il2::invoke(api.tf_set_local_scale, tf_of(go), oa);
        void* ta[] = {api.t_uitexture};
        widget = il2::invoke(api.go_add, go, ta);
        if (!alive(widget)) return fail("NGUI's UITexture couldn't be added"), false;
        g_widget = il2::il2cpp_gchandle_new(widget, false);
        g_last_w = g_last_h = 0;
        g_shown = -1;
    }
    if (n != g_shown) {
        Il2CppObject* tex = picture(n);
        if (!tex) return fail("a picture couldn't be loaded"), false;
        void* ma[] = {tex};
        il2::invoke(api.set_main, widget, ma);
        logx::info("start screen: picture %d of %d", n + 1, kPictures);
        g_shown = n;
        g_last_w = g_last_h = 0;  // (its proportions)
    }
    int32_t sw = 0, sh = 0;
    il2::invoke_value(api.screen_w, nullptr, sw);
    il2::invoke_value(api.screen_h, nullptr, sh);
    if (sw <= 0 || sh <= 0) return true;
    if (sw != g_last_w || sh != g_last_h) {
        float adjust = 1.0f;
        void* aa[] = {go_of(widget)};
        il2::invoke_value(api.pixel_adjust, nullptr, adjust, aa);
        int32_t w = static_cast<int32_t>(sw * adjust + 2), h = static_cast<int32_t>(sh * adjust + 2);
        void* wa[] = {&w};
        il2::invoke(api.set_width, widget, wa);
        void* ha[] = {&h};
        il2::invoke(api.set_height, widget, ha);
        // fill, keeping the proportions: the picture's surplus side cut evenly
        const float screen = static_cast<float>(sw) / sh,
                    pic = static_cast<float>(kStartPictures[n].w) / kStartPictures[n].h;
        Rect uv{0, 0, 1, 1};
        if (pic > screen) uv.w = screen / pic, uv.x = (1 - uv.w) / 2;
        else uv.h = pic / screen, uv.y = (1 - uv.h) / 2;
        void* ua[] = {&uv};
        il2::invoke(api.set_uv, widget, ua);
        g_last_w = sw, g_last_h = sh;
        logx::info("start screen: Karol's picture on the boot screen, %dx%d (%d x %d NGUI, uv %.3f %.3f %.3f %.3f)", sw, sh, w,
                   h, uv.x, uv.y, uv.w, uv.h);
    }
    int32_t d = depth;
    void* da[] = {&d};
    il2::invoke(api.set_depth, widget, da);
    float alpha = 1.0f;
    void* aa[] = {&alpha};
    il2::invoke(api.set_alpha, widget, aa);
    set_active(go_of(widget), true);
    return true;
}

using UpdateFn = void (*)(Il2CppObject*, const MethodInfo*);
UpdateFn g_update = nullptr;

void update_detour(Il2CppObject* self, const MethodInfo* method) {
    g_update(self, method);
    if (g_failed || !alive(self)) return;
    // the labels that stay ("press to continue"), and the lowest depth among them
    std::vector<Il2CppObject*> keep;
    int depth = 1000000;
    auto keep_label = [&](Il2CppObject* label) {
        if (!alive(label)) return;
        keep.push_back(il2::invoke(api.go_tf, go_of(label)));
        int32_t d = 0;
        if (il2::invoke_value(api.get_depth, label, d) && d < depth) depth = d;
    };
    keep_label(il2::read_field<Il2CppObject*>(self, api.main_continue));
    keep_label(il2::read_field<Il2CppObject*>(self, api.loading_continue));
    Il2CppObject* conts = il2::read_field<Il2CppObject*>(self, api.screen_continues);
    for (size_t i = 0; i < il2::array_length(conts); ++i) keep_label(il2::array_object_at(conts, i));
    if (depth == 1000000) depth = 0;
    // which of TLD's boot screens is up (the disclaimers, the main one, the loading): the n-th new one, picture n
    Il2CppObject* current = nullptr;
    auto consider = [&](Il2CppObject* go) {
        bool on = false;
        if (!current && alive(go) && il2::invoke_value(api.go_active, go, on) && on) current = go;
    };
    Il2CppObject* list = il2::read_field<Il2CppObject*>(self, api.screens);
    for (size_t i = 0; i < il2::array_length(list); ++i) consider(il2::array_object_at(list, i));
    consider(il2::read_field<Il2CppObject*>(self, api.main_disclaimer));
    consider(il2::read_field<Il2CppObject*>(self, api.loading_flow));
    int n = 0;
    if (current) {
        auto it = std::find(g_screens.begin(), g_screens.end(), current);
        if (it == g_screens.end()) {
            g_screens.push_back(current);
            it = g_screens.end() - 1;
            static const MethodInfo* get_name =
                il2::find_method(il2::find_class(kCore, "UnityEngine", "Object"), "get_name", 0);
            auto* nm = reinterpret_cast<Il2CppString*>(il2::invoke(get_name, current));
            logx::info("start screen: TLD's boot screen #%zu is '%s'", g_screens.size(), nm ? il2::to_utf8(nm).c_str() : "?");
        }
        n = static_cast<int>(it - g_screens.begin());
    } else if (!g_screens.empty()) {
        n = static_cast<int>(g_screens.size()) - 1;
    }
    if (n >= kPictures) n = kPictures - 1;
    if (!show(self, depth - 1, n)) return;
    // TLD's warnings, logos and loading animations off (the labels stay)
    hide_all_but(il2::read_field<Il2CppObject*>(self, api.main_disclaimer), keep);
    Il2CppObject* screens = il2::read_field<Il2CppObject*>(self, api.screens);
    for (size_t i = 0; i < il2::array_length(screens); ++i) hide_all_but(il2::array_object_at(screens, i), keep);
    hide_all_but(il2::read_field<Il2CppObject*>(self, api.loading_flow), keep);
    set_active(il2::read_field<Il2CppObject*>(self, api.loading_anims), false);
    if (!g_logged) {
        g_logged = true;
        logx::info("start screen: TLD's warnings and boot loading hidden; %zu \"continue\" label(s) kept", keep.size());
    }
}

}  // namespace

void init() {
    Il2CppClass* boot = il2::find_class(kGame, "", "Panel_Boot");
    Il2CppClass* go = il2::find_class(kCore, "UnityEngine", "GameObject");
    Il2CppClass* tf = il2::find_class(kCore, "UnityEngine", "Transform");
    Il2CppClass* object = il2::find_class(kCore, "UnityEngine", "Object");
    Il2CppClass* component = il2::find_class(kCore, "UnityEngine", "Component");
    Il2CppClass* screen = il2::find_class(kCore, "UnityEngine", "Screen");
    Il2CppClass* conv = il2::find_class("UnityEngine.ImageConversionModule.dll", "UnityEngine", "ImageConversion");
    Il2CppClass* uitex = il2::find_class(kGame, "", "UITexture");
    Il2CppClass* widget = il2::find_class(kGame, "", "UIWidget");
    Il2CppClass* root = il2::find_class(kGame, "", "UIRoot");
    api.go_class = go;
    api.tex_class = il2::find_class(kCore, "UnityEngine", "Texture2D");
    api.byte_class = il2::find_class("mscorlib.dll", "System", "Byte");
    if (!boot || !go || !tf || !object || !component || !screen || !conv || !uitex || !widget || !root || !api.tex_class ||
        !api.byte_class || !il2::il2cpp_array_new) {
        logx::warn("start screen: TLD's own stays (classes missing: Panel_Boot %s, NGUI %s, ImageConversion %s)", boot ? "ok" : "NO",
                   uitex && widget && root ? "ok" : "NO", conv ? "ok" : "NO");
        return;
    }
    api.t_uitexture = il2::type_object(uitex);
    api.go_ctor = il2::find_method_params(go, ".ctor", {"System.String"});
    api.go_add = il2::find_method_params(go, "AddComponent", {"System.Type"});
    api.go_tf = il2::find_method(go, "get_transform", 0);
    api.go_active = il2::find_method(go, "get_activeSelf", 0);
    api.go_set_active = il2::find_method(go, "SetActive", 1);
    api.go_layer = il2::find_method(go, "get_layer", 0);
    api.go_set_layer = il2::find_method(go, "set_layer", 1);
    api.component_go = il2::find_method(component, "get_gameObject", 0);
    api.tf_set_parent = il2::find_method_params(tf, "SetParent", {"UnityEngine.Transform", "System.Boolean"});
    api.tf_set_local_pos = il2::find_method(tf, "set_localPosition", 1);
    api.tf_set_local_scale = il2::find_method(tf, "set_localScale", 1);
    api.tf_child_count = il2::find_method(tf, "get_childCount", 0);
    api.tf_child = il2::find_method(tf, "GetChild", 1);
    api.tf_parent = il2::find_method(tf, "get_parent", 0);
    api.hide_flags = il2::find_method(object, "set_hideFlags", 1);
    api.tex_ctor = il2::find_method_params(api.tex_class, ".ctor", {"System.Int32", "System.Int32"});
    api.load_image = il2::find_method_params(conv, "LoadImage", {"UnityEngine.Texture2D", "System.Byte[]"});
    api.set_main = il2::find_method(uitex, "set_mainTexture", 1);
    api.set_uv = il2::find_method(uitex, "set_uvRect", 1);
    api.set_width = il2::find_method(widget, "set_width", 1);
    api.set_height = il2::find_method(widget, "set_height", 1);
    api.get_depth = il2::find_method(widget, "get_depth", 0);
    api.set_depth = il2::find_method(widget, "set_depth", 1);
    api.set_alpha = il2::find_method(widget, "set_alpha", 1);
    api.screen_w = il2::find_method(screen, "get_width", 0);
    api.screen_h = il2::find_method(screen, "get_height", 0);
    api.pixel_adjust = il2::find_method_params(root, "GetPixelSizeAdjustment", {"UnityEngine.GameObject"});
    api.main_disclaimer = il2::field_offset(boot, "m_MainDisclaimer");
    api.main_continue = il2::field_offset(boot, "m_MainDisclaimerContinue");
    api.screens = il2::field_offset(boot, "m_DisclaimerScreenList");
    api.screen_continues = il2::field_offset(boot, "m_DisclaimerScreenContinues");
    api.loading_flow = il2::field_offset(boot, "m_LoadingFlow");
    api.loading_anims = il2::field_offset(boot, "m_LoadingAnimationsRoot");
    api.loading_continue = il2::field_offset(boot, "m_LoadingContinue");
    const MethodInfo* update = il2::find_method(boot, "Update", 0);
    const bool all = api.go_ctor && api.go_add && api.go_tf && api.go_active && api.go_set_active && api.go_layer &&
                     api.go_set_layer && api.component_go && api.tf_set_parent && api.tf_set_local_pos && api.tf_set_local_scale &&
                     api.tf_child_count && api.tf_child && api.tf_parent && api.hide_flags && api.tex_ctor && api.load_image &&
                     api.set_main && api.set_uv && api.set_width && api.set_height && api.get_depth && api.set_depth &&
                     api.set_alpha && api.screen_w && api.screen_h && api.pixel_adjust && api.main_disclaimer >= 0 &&
                     api.main_continue >= 0 && api.screens >= 0 && api.screen_continues >= 0 && api.loading_flow >= 0 &&
                     api.loading_anims >= 0 && api.loading_continue >= 0 && update;
    if (!all) {
        logx::warn("start screen: TLD's own stays (some of TLD's or NGUI's calls are missing)");
        return;
    }
    api.ok = hooks::install_method("Panel_Boot.Update (start screen)", update, reinterpret_cast<void*>(&update_detour),
                                   reinterpret_cast<void**>(&g_update));
    logx::info("start screen: %s", api.ok ? "Karol's picture instead of TLD's warnings when the game starts"
                                          : "TLD's own stays (the hook failed)");
}

}  // namespace loading_screen
