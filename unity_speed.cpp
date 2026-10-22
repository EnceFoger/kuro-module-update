#include <atomic>
#include <chrono>
#include <unistd.h>

#include "config.h"
#include "feature_hooks.h"
#include "hook_util.h"
#include "il2cpp_api.h"
#include "log.h"

// Two speed features share one hook on UnityEngine.Time.set_timeScale:
//  * Unity Speed scales every timeScale the game requests (menus included; pause = 0 stays 0).
//  * Game Speed scales only the requests made from inside the battle's own time controller,
//    Bleach.Games.QuestBattle.GameTimeManager (its Update / FixedUpdate / ShiftOnFrame are wrapped so a
//    set_timeScale call made while one of them runs is recognised as "battle").
// When both are on the factors multiply. When the battle controller stops ticking (battle over) the
// battle factor is dropped again, so menus never keep a battle speed.
//
// Time.timeScale may only be set from Unity's main thread, so slider changes and battle start/stop are
// applied from a Time.get_deltaTime hook, and only on the thread that already called set_timeScale.
namespace {
using Clock = std::chrono::steady_clock;
constexpr long long kBattleIdleMs = 500;      // GameTimeManager silent for this long = not in battle

void  (*orig_set)(float, const void*) = nullptr;
float (*orig_dt)(const void*) = nullptr;
const void* set_mi = nullptr;
bool g_set_hooked = false;

std::atomic<float>    g_last{1.0f};            // last timeScale the game itself requested
std::atomic<int>      g_main_tid{0};
std::atomic<unsigned> g_applied_rev{0};
std::atomic<int>      g_in_gtm{0};             // > 0 while a GameTimeManager method is running
std::atomic<long long> g_gtm_tick_ms{0};       // time of the last GameTimeManager tick (0 = never)
std::atomic<unsigned> g_gtm_writes{0};         // timeScale requests seen from inside GameTimeManager
bool g_applied_battle = false;                 // main thread only
bool g_logged_confirm = false, g_logged_none = false;
long long g_battle_since_ms = 0;

inline long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

inline float factor(bool in_battle) {
    float f = cfg::g.unity_speed.load(std::memory_order_relaxed);
    if (in_battle) f *= cfg::g.game_speed.load(std::memory_order_relaxed);
    return f;
}

void hook_set(float value, const void* mi) {
    bool battle = g_in_gtm.load(std::memory_order_relaxed) > 0;
    g_last.store(value, std::memory_order_relaxed);
    g_main_tid.store(gettid(), std::memory_order_relaxed);
    if (battle) g_gtm_writes.fetch_add(1, std::memory_order_relaxed);
    orig_set(value * factor(battle), mi);
}

float hook_dt(const void* mi) {
    float r = orig_dt(mi);
    int main_tid = g_main_tid.load(std::memory_order_relaxed);
    if (main_tid == 0 || gettid() != main_tid) return r;

    long long now = now_ms(), tick = g_gtm_tick_ms.load(std::memory_order_relaxed);
    bool battle = tick != 0 && now - tick < kBattleIdleMs;
    unsigned rev = cfg::g.revision.load(std::memory_order_relaxed);
    if (rev != g_applied_rev.load(std::memory_order_relaxed) || battle != g_applied_battle) {
        g_applied_rev.store(rev, std::memory_order_relaxed);
        g_applied_battle = battle;
        orig_set(g_last.load(std::memory_order_relaxed) * factor(battle), set_mi);
    }

    // One-time diagnostics so the log shows whether Game Speed can work in this game version.
    if (battle) {
        if (g_battle_since_ms == 0) g_battle_since_ms = now;
        if (!g_logged_confirm && g_gtm_writes.load(std::memory_order_relaxed) > 0) {
            g_logged_confirm = true;
            LOGI("game_speed: confirmed - GameTimeManager sets Time.timeScale");
        } else if (!g_logged_none && g_gtm_writes.load(std::memory_order_relaxed) == 0 && now - g_battle_since_ms > 15000) {
            g_logged_none = true;
            LOGW("game_speed: GameTimeManager is active but never sets Time.timeScale itself; Game Speed will have no effect");
        }
    } else {
        g_battle_since_ms = 0;
    }
    return r;
}

// Wrapper for the battle time controller's parameterless methods.
template <int N>
struct Gtm {
    static void (*orig)(void*, const void*);
    static void fn(void* self, const void* mi) {
        g_in_gtm.fetch_add(1, std::memory_order_relaxed);
        g_gtm_tick_ms.store(now_ms(), std::memory_order_relaxed);
        orig(self, mi);
        g_in_gtm.fetch_sub(1, std::memory_order_relaxed);
    }
};
template <int N> void (*Gtm<N>::orig)(void*, const void*) = nullptr;
}  // namespace

bool features::install_unity_speed(bool dry_run) {
    il2cpp::MethodRef set, dt;
    if (!il2cpp::resolve("UnityEngine", "Time", "set_timeScale", 1, "System.Void", {"System.Single"}, set, true)) return false;
    bool have_dt = il2cpp::resolve("UnityEngine", "Time", "get_deltaTime", 0, "System.Single", {}, dt, true);
    if (dry_run) { LOGI("probe: unity_speed targets OK (not hooked)"); return true; }

    set_mi = set.info;
    if (!install_hook("unity_speed(set_timeScale)", set.addr, reinterpret_cast<void*>(&hook_set),
                      reinterpret_cast<void**>(&orig_set), /*check_patched=*/true))
        return false;   // e.g. Kuro already owns it: do not fight over the same method
    g_set_hooked = true;
    if (!have_dt || !install_hook("unity_speed(get_deltaTime)", dt.addr, reinterpret_cast<void*>(&hook_dt),
                                  reinterpret_cast<void**>(&orig_dt), /*check_patched=*/true))
        LOGW("unity_speed: slider changes apply only when the game next sets Time.timeScale");
    return true;
}

bool features::install_game_speed(bool dry_run) {
    const char* ns = "Bleach.Games.QuestBattle";
    il2cpp::MethodRef update, fixed, shift;
    bool u = il2cpp::resolve(ns, "GameTimeManager", "Update", 0, "System.Void", {}, update);
    bool f = il2cpp::resolve(ns, "GameTimeManager", "FixedUpdate", 0, "System.Void", {}, fixed);
    bool s = il2cpp::resolve(ns, "GameTimeManager", "ShiftOnFrame", 0, "System.Void", {}, shift);
    if (!u && !f && !s) return false;
    if (dry_run) { LOGI("probe: game_speed targets OK (not hooked)"); return true; }
    if (!g_set_hooked) {
        LOGW("game_speed: needs the Time.set_timeScale hook, which is not active (Unity Speed hook missing or owned by another module)");
        return false;
    }
    bool any = false;
    if (u) any |= install_hook("game_speed(Update)", update.addr, reinterpret_cast<void*>(&Gtm<0>::fn), reinterpret_cast<void**>(&Gtm<0>::orig), true);
    if (f) any |= install_hook("game_speed(FixedUpdate)", fixed.addr, reinterpret_cast<void*>(&Gtm<1>::fn), reinterpret_cast<void**>(&Gtm<1>::orig), true);
    if (s) any |= install_hook("game_speed(ShiftOnFrame)", shift.addr, reinterpret_cast<void*>(&Gtm<2>::fn), reinterpret_cast<void**>(&Gtm<2>::orig), true);
    return any;
}
