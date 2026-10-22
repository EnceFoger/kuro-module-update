#include "hook_util.h"
#include "log.h"

#include <cstdint>
#include <set>

#include "And64InlineHook.hpp"   // downloaded by the workflow (Rprop/And64InlineHook, arm64 only)

static bool looks_patched(void* target) {
    const uint32_t* ins = static_cast<const uint32_t*>(target);
    if ((ins[0] & 0xFC000000u) == 0x14000000u) return true;                 // B imm26
    for (int i = 0; i < 4; ++i)
        if ((ins[i] & 0xFFFFFC1Fu) == 0xD61F0000u) return true;             // BR Xn
    return false;
}

bool install_hook(const char* label, void* target, void* replacement, void** original,
                  bool check_patched) {
    static std::set<void*> hooked;
    if (!target || !replacement || !original) {
        LOGE("hook %s: invalid arguments", label);
        return false;
    }
    if (hooked.count(target)) {
        LOGW("hook %s: address %p is already hooked by this module - skipped", label, target);
        return false;
    }
    if (check_patched && looks_patched(target)) {
        LOGW("hook %s: target %p already looks patched by another module - skipped", label, target);
        return false;
    }
    *original = nullptr;
    A64HookFunction(target, replacement, original);
    if (!*original) {
        LOGE("hook %s: A64HookFunction produced no trampoline", label);
        return false;
    }
    hooked.insert(target);
    LOGI("hook %s: installed at %p", label, target);
    return true;
}
