#pragma once
#include <atomic>
#include <string>

// Runtime settings, read from a plain-text file (see config/companion.cfg.example).
// Values are atomics so the game's threads can read them while the worker thread reloads.
namespace cfg {

struct Settings {
    std::atomic<float> attack_speed{1.0f};
    std::atomic<int>   attack_speed_mode{0};   // 0 = multiply, 1 = add (value - 1)
    std::atomic<float> move_speed{1.0f};
    std::atomic<int>   move_speed_mode{0};
    std::atomic<float> attack_range{1.0f};
    std::atomic<bool>  skill_no_cd{false};
    std::atomic<bool>  god_mode{false};
    std::atomic<bool>  skip_story{false};
    std::atomic<bool>  event_points{false};    // write event-point results for the overlay (read-only)
    std::atomic<float> damage_rate{1.0f};      // 1.0 = off
    std::atomic<int>   damage_rate_mode{1};    // 1 = add (default), 0 = multiply
    std::atomic<float> max_damage_rate{5.0f};
    std::atomic<float> unity_speed{1.0f};      // 1.0 = off
    // Upper limits for the sliders / values; default 5.0, configurable, validated to 1.0 .. 100.0.
    std::atomic<float> max_attack_speed{5.0f};
    std::atomic<float> max_move_speed{5.0f};
    std::atomic<float> max_attack_range{5.0f};
    std::atomic<float> max_unity_speed{5.0f};
    std::atomic<float> game_speed{1.0f};       // 1.0 = off; in-battle only (GameTimeManager)
    std::atomic<float> max_game_speed{5.0f};
    std::atomic<unsigned> revision{0};         // bumped after every successful (re)load
    // 0 = idle (log only) | 1 = locate libil2cpp + load API | 2 = + find methods, NO hooks | 3 = full
    std::atomic<int>   stage{1};
};

extern Settings g;

void set_path(const std::string& path);           // internal default config
void set_override_path(const std::string& path);  // used instead if it exists and is readable
void ensure_default_file();   // creates an all-disabled config if none exists
void reload_if_changed();     // cheap: stat() + parse only when mtime changed

// 1.0 means "off" and returns `original` untouched.
float apply(float original, float value, int mode);

}  // namespace cfg
