#include "worker.h"

#include <chrono>
#include <thread>

#include "config.h"
#include "feature_hooks.h"
#include "il2cpp_api.h"
#include "log.h"

static void report(const char* name, bool ok, bool dry) {
    if (ok) LOGI("feature %-14s : %s", name, dry ? "FOUND (probe, not hooked)" : "INSTALLED");
    else    LOGW("feature %-14s : NOT FOUND - not hooked", name);
}

static void idle_loop() {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        cfg::reload_if_changed();
    }
}

void worker_main(std::string config_path) {
    using namespace std::chrono;
    const int stage = cfg::g.stage.load();
    LOGI("worker started, stage=%d, config=%s", stage, config_path.c_str());

    // Stay out of the way while the game boots.
    std::this_thread::sleep_for(seconds(5));

    il2cpp::LibRef lib;
    for (int i = 0; i < 360 && !il2cpp::locate_library(lib); ++i)
        std::this_thread::sleep_for(milliseconds(500));
    if (!lib.dynamic) { LOGE("libil2cpp.so never loaded - nothing hooked"); il2cpp::log_loaded_libs(); return idle_loop(); }
    LOGI("libil2cpp.so located (bias=%p)", (void*)lib.bias);

    if (!il2cpp::load_api(lib)) {
        LOGE("IL2CPP API exports are unavailable in this libil2cpp.so (names stripped?) - nothing hooked");
        return idle_loop();
    }
    LOGI("IL2CPP API resolved by name");
    if (stage < 2) { LOGI("stage 1 done (no hooks, no IL2CPP calls)"); return idle_loop(); }

    if (!il2cpp::wait_until_ready(180)) { LOGE("IL2CPP not ready - nothing hooked"); return idle_loop(); }

    const bool dry = stage < 3;
    report("attack_speed", features::install_attack_speed(dry), dry);
    report("move_speed",   features::install_move_speed(dry), dry);
    report("attack_range", features::install_attack_range(dry), dry);
    report("skill_no_cd",  features::install_skill_no_cd(dry), dry);
    report("god_mode",     features::install_god_mode(dry), dry);
    report("damage_rate",  features::install_damage_rate(dry), dry);
    report("event_points", features::install_event_points(dry), dry);
    report("skip_story",   features::install_skip_story(dry), dry);
    report("unity_speed",  features::install_unity_speed(dry), dry);
    report("game_speed",   features::install_game_speed(dry), dry);
    LOGI("kill_all_enemy   : NOT FEASIBLE - not implemented");
    LOGI(dry ? "stage 2 done: probe only, no hooks installed" : "stage 3 done: hooks installed");
    idle_loop();
}
