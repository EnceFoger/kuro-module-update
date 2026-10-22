#include <chrono>
#include <cstdint>

#include "config.h"
#include "feature_hooks.h"
#include "hook_util.h"
#include "il2cpp_api.h"
#include "log.h"

// Skip Story: presses the story dialog's own Skip button. CharacterTheaterController.Skip is a coroutine
// (returns IEnumerator) and cannot be called directly, so instead SkipButtonView.OnClicked -- the button's
// click handler -- is invoked, exactly once per theater, after the dialog has run for a moment.
//
// One controller can play several dialogs in a row, so the action is armed again for every new dialog: after
// the current one has ended/been skipped, or when the controller's currentTheater changes.
//
// Conditions (otherwise nothing happens): feature ON; the theater has started and neither ended nor been
// skipped; it has run for kSettleMs; the game has not disabled its Skip button (SkipButtonView.Disable);
// and the button object is still alive (UnityEngine.Object.op_Implicit) and active+enabled
// (Behaviour.get_isActiveAndEnabled). OFF = the game behaves normally.
namespace {
constexpr int64_t kSettleMs = 1200;

using Void0 = void (*)(void*, const void*);
using BoolFn = bool (*)(void*, const void*);
Void0 orig_awake = nullptr, orig_update = nullptr;             // CharacterTheaterController
Void0 orig_btn_awake = nullptr, orig_enable = nullptr, orig_disable = nullptr;   // SkipButtonView
Void0 click_fn = nullptr;
const void* click_mi = nullptr;
BoolFn unity_alive = nullptr, behaviour_active = nullptr;     // Object.op_Implicit (static), Behaviour.get_isActiveAndEnabled
const void *alive_mi = nullptr, *active_mi = nullptr;
size_t off_started, off_ended, off_skipped, off_current = 0;
bool have_current = false;      // currentTheater field found (optional, used to detect the next dialog)

// All on Unity's main thread.
void* g_cur = nullptr;          // current theater controller
void* g_btn = nullptr;          // most recently created Skip button
bool g_seen = false, g_done = false, g_block_logged = false;
void* g_last_theater = nullptr;
bool g_skip_enabled = true;     // the Skip button is available unless the game said Disable()
std::chrono::steady_clock::time_point g_t0;

inline bool read_bool(void* obj, size_t off) { return *reinterpret_cast<volatile uint8_t*>(static_cast<char*>(obj) + off) != 0; }

void hook_awake(void* self, const void* mi) {
    orig_awake(self, mi);
    g_cur = self; g_seen = false; g_done = false; g_block_logged = false; g_last_theater = nullptr;
}
void hook_btn_awake(void* self, const void* mi) { orig_btn_awake(self, mi); g_btn = self; }
void hook_enable(void* self, const void* mi)    { orig_enable(self, mi);  g_btn = self; g_skip_enabled = true; }
void hook_disable(void* self, const void* mi)   { orig_disable(self, mi); if (self == g_btn) g_skip_enabled = false; }

void hook_update(void* self, const void* mi) {
    orig_update(self, mi);
    if (!cfg::g.skip_story.load(std::memory_order_relaxed)) return;
    if (self != g_cur) return;

    bool playing = read_bool(self, off_started) && !read_bool(self, off_ended) && !read_bool(self, off_skipped);
    void* theater = have_current ? *reinterpret_cast<void* volatile*>(static_cast<char*>(self) + off_current) : nullptr;

    if (g_done) {                                    // already pressed for the current dialog
        bool next_dialog = !playing || (have_current && theater != g_last_theater);
        if (!next_dialog) return;
        g_done = false; g_seen = false;              // arm again for the following dialog
    }
    if (!playing) return;

    auto now = std::chrono::steady_clock::now();
    if (!g_seen || (have_current && theater != g_last_theater)) {   // a dialog just began: start its timer
        g_seen = true; g_t0 = now; g_last_theater = theater; g_block_logged = false;
        return;
    }
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - g_t0).count() < kSettleMs) return;

    const char* blocked = nullptr;
    if (!g_skip_enabled || !g_btn) blocked = "the game offers no Skip button right now";
    else if (!unity_alive(g_btn, alive_mi)) { g_btn = nullptr; blocked = "the Skip button object was destroyed"; }
    else if (!behaviour_active(g_btn, active_mi)) blocked = "the Skip button is hidden or inactive";
    if (blocked) {
        if (!g_block_logged) { g_block_logged = true; LOGI("skip_story: waiting - %s", blocked); }
        return;
    }
    g_done = true;                                   // before the call: never twice for this dialog
    LOGI("skip_story: pressing the Skip button");
    click_fn(g_btn, click_mi);
}
}  // namespace

bool features::install_skip_story(bool dry_run) {
    using namespace il2cpp;
    const char* ns = "Bleach.Menus.CharacterTheater";
    const char* cls = "CharacterTheaterController";
    const char* btn = "SkipButtonView";
    MethodRef awake, update, bawake, enable, disable, click, alive, active;
    if (!resolve(ns, cls, "Awake", 0, "System.Void", {}, awake)) return false;
    if (!resolve(ns, cls, "Update", 0, "System.Void", {}, update)) return false;
    if (!resolve(ns, btn, "Awake", 0, "System.Void", {}, bawake)) return false;
    if (!resolve(ns, btn, "Enable", 0, "System.Void", {}, enable)) return false;
    if (!resolve(ns, btn, "Disable", 0, "System.Void", {}, disable)) return false;
    if (!resolve(ns, btn, "OnClicked", 0, "System.Void", {}, click)) return false;
    if (!resolve("UnityEngine", "Object", "op_Implicit", 1, "System.Boolean", {"UnityEngine.Object"}, alive, true)) return false;
    if (!resolve("UnityEngine", "Behaviour", "get_isActiveAndEnabled", 0, "System.Boolean", {}, active)) return false;
    void* k = find_class(ns, cls);
    if (!k) return false;
    if (!instance_field(k, cls, "started", "System.Boolean", &off_started) ||
        !instance_field(k, cls, "ended", "System.Boolean", &off_ended) ||
        !instance_field(k, cls, "skipped", "System.Boolean", &off_skipped))
        return false;
    have_current = instance_field(k, cls, "currentTheater", nullptr, &off_current);
    if (!have_current) LOGW("skip_story: field currentTheater not found; only dialogs that end are re-armed");
    if (dry_run) { LOGI("probe: skip_story targets and fields OK (not hooked)"); return true; }

    click_fn = reinterpret_cast<Void0>(click.addr);            click_mi = click.info;
    unity_alive = reinterpret_cast<BoolFn>(alive.addr);        alive_mi = alive.info;
    behaviour_active = reinterpret_cast<BoolFn>(active.addr);  active_mi = active.info;
    // Update is installed last: it is the only hook that can trigger the action, so a partial set stays inert.
    bool ok = install_hook("skip_story(Awake)", awake.addr, reinterpret_cast<void*>(&hook_awake), reinterpret_cast<void**>(&orig_awake), true)
           && install_hook("skip_story(ButtonAwake)", bawake.addr, reinterpret_cast<void*>(&hook_btn_awake), reinterpret_cast<void**>(&orig_btn_awake), true)
           && install_hook("skip_story(Enable)", enable.addr, reinterpret_cast<void*>(&hook_enable), reinterpret_cast<void**>(&orig_enable), true)
           && install_hook("skip_story(Disable)", disable.addr, reinterpret_cast<void*>(&hook_disable), reinterpret_cast<void**>(&orig_disable), true)
           && install_hook("skip_story(Update)", update.addr, reinterpret_cast<void*>(&hook_update), reinterpret_cast<void**>(&orig_update), true);
    if (!ok) LOGW("skip_story: not every hook could be installed; the feature stays inactive");
    return ok;
}
