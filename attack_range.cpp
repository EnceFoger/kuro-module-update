#include <cmath>

#include "config.h"
#include "feature_hooks.h"
#include "hook_util.h"
#include "il2cpp_api.h"
#include "log.h"

namespace {
float (*orig_f)(void*, const void*) = nullptr;
int   (*orig_i)(void*, const void*) = nullptr;

float hook_f(void* self, const void* mi) {
    return orig_f(self, mi) * cfg::g.attack_range.load(std::memory_order_relaxed);
}
int hook_i(void* self, const void* mi) {
    return (int)std::lround((double)orig_i(self, mi) * cfg::g.attack_range.load(std::memory_order_relaxed));
}
}  // namespace

bool features::install_attack_range(bool dry_run) {
    il2cpp::MethodRef m;
    // Return type is not asserted up front: only System.Single and System.Int32 are supported.
    if (!il2cpp::resolve("Bleach.Games.QuestBattle.Battle", "Attack", "get_AttackRange", 0, nullptr, {}, m))
        return false;
    bool is_f = m.ret_type == "System.Single", is_i = m.ret_type == "System.Int32";
    if (!is_f && !is_i) {
        LOGW("NOT FOUND supported signature for Attack.get_AttackRange (returns %s)", m.ret_type.c_str());
        return false;
    }
    if (dry_run) { LOGI("probe: attack_range target OK (%s, not hooked)", m.ret_type.c_str()); return true; }
    if (is_f)
        return install_hook("attack_range(float)", m.addr, reinterpret_cast<void*>(&hook_f), reinterpret_cast<void**>(&orig_f));
    return install_hook("attack_range(int)", m.addr, reinterpret_cast<void*>(&hook_i), reinterpret_cast<void**>(&orig_i));
}
