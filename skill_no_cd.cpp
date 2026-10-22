#include "config.h"
#include "feature_hooks.h"
#include "hook_util.h"
#include "il2cpp_api.h"
#include "log.h"

// Skill cooldown is not zeroed directly (no verified field/setter contract). Instead the per-frame
// delta passed into the CoolTime update methods is scaled up, so a cooldown completes in about one
// frame. Only installed when the parameter is verified to be System.Single.
namespace {
constexpr float kDeltaScale = 1000.0f;

void (*orig_heavy)(void*, float, const void*) = nullptr;
void (*orig_extra)(void*, float, const void*) = nullptr;

inline float scaled(float dt) {
    return cfg::g.skill_no_cd.load(std::memory_order_relaxed) ? dt * kDeltaScale : dt;
}
void hook_heavy(void* self, float dt, const void* mi) { orig_heavy(self, scaled(dt), mi); }
void hook_extra(void* self, float dt, const void* mi) { orig_extra(self, scaled(dt), mi); }
}  // namespace

bool features::install_skill_no_cd(bool dry_run) {
    const char* ns = "Bleach.Games.QuestBattle.Characters";
    bool any = false;
    il2cpp::MethodRef m;
    if (il2cpp::resolve(ns, "CoolTime", "UpdateHeavyAttackCT", 1, "System.Void", {"System.Single"}, m)) {
        if (dry_run) { LOGI("probe: skill_no_cd(heavy) target OK (not hooked)"); any = true; }
        else any |= install_hook("skill_no_cd(heavy)", m.addr, reinterpret_cast<void*>(&hook_heavy), reinterpret_cast<void**>(&orig_heavy));
    }
    il2cpp::MethodRef m2;
    if (il2cpp::resolve(ns, "CoolTime", "UpdateExtraActionAttackCT", 1, "System.Void", {"System.Single"}, m2)) {
        if (dry_run) { LOGI("probe: skill_no_cd(extra) target OK (not hooked)"); any = true; }
        else any |= install_hook("skill_no_cd(extra)", m2.addr, reinterpret_cast<void*>(&hook_extra), reinterpret_cast<void**>(&orig_extra));
    }
    return any;
}
