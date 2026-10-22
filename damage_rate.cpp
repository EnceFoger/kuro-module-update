#include <cstdint>

#include "config.h"
#include "feature_hooks.h"
#include "hook_util.h"
#include "il2cpp_api.h"
#include "log.h"

// Scales the game's damage-up rate. The two CalculateDamageUpRate overloads have different argument
// lists, so the hook forwards all integer argument registers unchanged; installation is refused unless
// every parameter is verified to be an integer/enum/reference and the return type is System.Single.
// If one overload calls the other, only the outermost call is modified.
namespace {
using Fn = float (*)(void*, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
thread_local int g_depth = 0;

template <int N>
struct Hook {
    static Fn orig;
    static float fn(void* self, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
                    uint64_t a5, uint64_t a6, uint64_t a7) {
        ++g_depth;
        float r = orig(self, a1, a2, a3, a4, a5, a6, a7);
        --g_depth;
        if (g_depth != 0) return r;
        return cfg::apply(r, cfg::g.damage_rate.load(std::memory_order_relaxed),
                          cfg::g.damage_rate_mode.load(std::memory_order_relaxed));
    }
};
template <int N> Fn Hook<N>::orig = nullptr;

template <int N>
bool install_overload(int argc, bool dry_run) {
    il2cpp::MethodRef m;
    if (!il2cpp::resolve("Bleach.Games.Common.Masters", "AbilityBelongAttributeMaster", "CalculateDamageUpRate",
                         argc, "System.Single", {}, m)) return false;
    if (!il2cpp::params_in_registers(m.info, argc, "CalculateDamageUpRate")) return false;
    if (dry_run) { LOGI("probe: damage_rate(%d args) target OK (not hooked)", argc); return true; }
    return install_hook(argc == 2 ? "damage_rate(2 args)" : "damage_rate(3 args)", m.addr,
                        reinterpret_cast<void*>(&Hook<N>::fn), reinterpret_cast<void**>(&Hook<N>::orig),
                        /*check_patched=*/true);
}
}  // namespace

bool features::install_damage_rate(bool dry_run) {
    bool a = install_overload<2>(2, dry_run);
    bool b = install_overload<3>(3, dry_run);
    return a || b;
}
