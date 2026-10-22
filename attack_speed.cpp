#include "config.h"
#include "feature_hooks.h"
#include "hook_util.h"
#include "il2cpp_api.h"
#include "log.h"

namespace {
float (*orig)(void*, const void*) = nullptr;

float hook(void* self, const void* mi) {
    float v = orig(self, mi);
    return cfg::apply(v, cfg::g.attack_speed.load(std::memory_order_relaxed),
                      cfg::g.attack_speed_mode.load(std::memory_order_relaxed));
}
}  // namespace

bool features::install_attack_speed(bool dry_run) {
    il2cpp::MethodRef m;
    if (!il2cpp::resolve("Bleach.Games.QuestBattle.Characters", "AbilityEnhancement",
                         "get_NormalAtkSpeedUpRate", 0, "System.Single", {}, m)) return false;
    if (dry_run) { LOGI("probe: attack_speed target OK (not hooked)"); return true; }
    return install_hook("attack_speed", m.addr, reinterpret_cast<void*>(&hook), reinterpret_cast<void**>(&orig));
}
