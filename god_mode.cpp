#include "config.h"
#include "feature_hooks.h"
#include "hook_util.h"
#include "il2cpp_api.h"
#include "log.h"

// Makes the game's own invincibility check return true, for player-side characters only
// (CharaBase.get_IsPlayableSide), so enemies stay killable. Off = the original getter untouched.
namespace {
bool (*orig_invincible)(void*, const void*) = nullptr;
bool (*playable_side)(void*, const void*) = nullptr;
const void* playable_side_mi = nullptr;

bool hook(void* self, const void* mi) {
    if (self && cfg::g.god_mode.load(std::memory_order_relaxed) && playable_side(self, playable_side_mi))
        return true;
    return orig_invincible(self, mi);
}
}  // namespace

bool features::install_god_mode(bool dry_run) {
    const char* ns = "Bleach.Games.QuestBattle.Characters";
    il2cpp::MethodRef inv, side;
    if (!il2cpp::resolve(ns, "CharaBase", "get_IsInvincible", 0, "System.Boolean", {}, inv)) return false;
    if (!il2cpp::resolve(ns, "CharaBase", "get_IsPlayableSide", 0, "System.Boolean", {}, side)) return false;
    if (side.flags & 0x0040u) {   // virtual: a direct call could skip an override
        LOGW("NOT FOUND usable CharaBase.get_IsPlayableSide (it is virtual)");
        return false;
    }
    if (dry_run) { LOGI("probe: god_mode targets OK (not hooked)"); return true; }
    playable_side = reinterpret_cast<bool (*)(void*, const void*)>(side.addr);
    playable_side_mi = side.info;
    return install_hook("god_mode", inv.addr, reinterpret_cast<void*>(&hook),
                        reinterpret_cast<void**>(&orig_invincible), /*check_patched=*/true);
}
