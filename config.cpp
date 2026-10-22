#include "config.h"
#include "log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace cfg {

Settings g;

static const float kHardMax    = 100.0f;  // absolute ceiling for any value (Unity itself caps timeScale at 100)
static const float kDefaultMax = 5.0f;
static const float kMinValue   = 0.1f;

static std::string s_path;
static std::string s_over;
static std::string s_used;
static long long   s_stamp = -1;

static const char* kDefaultFile =
    "# Kuro Companion settings. Re-read automatically every ~2 seconds.\n"
    "# 1.0 = feature off. Missing or invalid lines fall back to defaults.\n"
    "stage=1                    # 0 idle | 1 find API | 2 find methods, no hooks | 3 full (set 3 to use features)\n"
    "attack_speed=1.0\n"
    "attack_speed_mode=mul      # mul: result = original * value | add: result = original + (value - 1)\n"
    "move_speed=1.0\n"
    "move_speed_mode=mul\n"
    "attack_range=1.0\n"
    "unity_speed=1.0\n"
    "game_speed=1.0            # speed of the battle only (menus unchanged)\n"
    "skill_no_cd=0              # 1 = on (experimental)\n"
    "god_mode=0                 # 1 = on (player characters only)\n"
    "skip_story=0               # 1 = on (skips story dialogs with the game's own Skip)\n"
    "damage_rate=1.0\n"
    "damage_rate_mode=add       # add (default) or mul\n"
    "# Slider/value upper limits (1.0 .. 100.0, default 5.0):\n"
    "max_attack_speed=5.0\n"
    "max_move_speed=5.0\n"
    "max_attack_range=5.0\n"
    "max_unity_speed=5.0\n"
    "max_game_speed=5.0\n"
    "max_damage_rate=5.0\n";

void set_path(const std::string& path) { s_path = path; }
void set_override_path(const std::string& path) { s_over = path; }

static std::string active_path() {
    if (!s_over.empty() && access(s_over.c_str(), R_OK) == 0) return s_over;
    return s_path;
}

static std::string trim(std::string s) {
    auto issp = [](unsigned char c) { return std::isspace(c); };
    while (!s.empty() && issp(s.front())) s.erase(s.begin());
    while (!s.empty() && issp(s.back())) s.pop_back();
    return s;
}

// A finite number inside 0 .. kHardMax, otherwise rejected.
static bool parse_float(const std::string& v, float& out) {
    char* end = nullptr;
    float f = strtof(v.c_str(), &end);
    if (end == v.c_str() || !std::isfinite(f) || f < 0.0f) return false;
    out = std::min(kHardMax, f);
    return true;
}

static bool parse_bool(const std::string& v, bool& out) {
    if (v == "1" || v == "true" || v == "on")  { out = true;  return true; }
    if (v == "0" || v == "false" || v == "off") { out = false; return true; }
    return false;
}

static bool parse_mode(const std::string& v, int& out) {
    if (v == "mul") { out = 0; return true; }
    if (v == "add") { out = 1; return true; }
    return false;
}

// Upper limits must be 1.0 .. 100.0; anything else falls back to the default of 5.0.
static float parse_max(const std::string& key, const std::string& v) {
    float f;
    if (parse_float(v, f) && f >= 1.0f) return f;
    LOGW("config: invalid %s='%s' -> using default %.1f", key.c_str(), v.c_str(), kDefaultMax);
    return kDefaultMax;
}

static void reset_defaults() {
    g.attack_speed = 1.0f; g.attack_speed_mode = 0;
    g.move_speed = 1.0f;   g.move_speed_mode = 0;
    g.attack_range = 1.0f; g.unity_speed = 1.0f; g.game_speed = 1.0f;
    g.skill_no_cd = false; g.god_mode = false; g.skip_story = false; g.event_points = false;
    g.damage_rate = 1.0f; g.damage_rate_mode = 1; g.max_damage_rate = kDefaultMax;
    g.max_attack_speed = kDefaultMax; g.max_move_speed = kDefaultMax;
    g.max_attack_range = kDefaultMax; g.max_unity_speed = kDefaultMax; g.max_game_speed = kDefaultMax;
}

static void clamp_to(std::atomic<float>& value, float limit) {
    float v = value.load();
    value.store(std::max(kMinValue, std::min(limit, v)));
}

void ensure_default_file() {
    if (s_path.empty() || access(s_path.c_str(), F_OK) == 0) return;
    size_t slash = s_path.find_last_of('/');
    if (slash != std::string::npos) mkdir(s_path.substr(0, slash).c_str(), 0771);
    std::ofstream f(s_path);
    if (f) {
        f << kDefaultFile;
        LOGI("created default config: %s", s_path.c_str());
    } else {
        LOGW("could not create default config at %s (all features stay off)", s_path.c_str());
    }
}

void reload_if_changed() {
    struct stat st;
    std::string path = active_path();
    if (path != s_used) { s_used = path; s_stamp = -1; LOGI("config file in use: %s", path.c_str()); }
    if (path.empty() || stat(path.c_str(), &st) != 0) return;
    long long stamp = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
    if (stamp == s_stamp) return;

    std::ifstream in(path);
    if (!in) return;
    s_stamp = stamp;

    reset_defaults();
    std::string line;
    while (std::getline(in, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        float f; bool b; int m;
        if      (key == "attack_speed"      && parse_float(val, f)) g.attack_speed = f;
        else if (key == "attack_speed_mode" && parse_mode(val, m))  g.attack_speed_mode = m;
        else if (key == "move_speed"        && parse_float(val, f)) g.move_speed = f;
        else if (key == "move_speed_mode"   && parse_mode(val, m))  g.move_speed_mode = m;
        else if (key == "attack_range"      && parse_float(val, f)) g.attack_range = f;
        else if (key == "unity_speed"       && parse_float(val, f)) g.unity_speed = f;
        else if (key == "game_speed"        && parse_float(val, f)) g.game_speed = f;
        else if (key == "skill_no_cd"       && parse_bool(val, b))  g.skill_no_cd = b;
        else if (key == "god_mode"          && parse_bool(val, b))  g.god_mode = b;
        else if (key == "skip_story"         && parse_bool(val, b))  g.skip_story = b;
        else if (key == "event_points"       && parse_bool(val, b))  g.event_points = b;
        else if (key == "damage_rate"       && parse_float(val, f)) g.damage_rate = f;
        else if (key == "damage_rate_mode"  && parse_mode(val, m))  g.damage_rate_mode = (m == 1 ? 1 : 0);
        else if (key == "max_damage_rate")  g.max_damage_rate = parse_max(key, val);
        else if (key == "max_attack_speed") g.max_attack_speed = parse_max(key, val);
        else if (key == "max_move_speed")   g.max_move_speed   = parse_max(key, val);
        else if (key == "max_attack_range") g.max_attack_range = parse_max(key, val);
        else if (key == "max_unity_speed")  g.max_unity_speed  = parse_max(key, val);
        else if (key == "max_game_speed")   g.max_game_speed   = parse_max(key, val);
        else if (key == "stage") {
            char* e = nullptr; long v = strtol(val.c_str(), &e, 10);
            if (e != val.c_str()) g.stage = (int)std::min(3L, std::max(0L, v));
        }
        else LOGW("config: ignored line '%s=%s'", key.c_str(), val.c_str());
    }
    // Values may never exceed their configured limit (applied after parsing, so line order is irrelevant).
    clamp_to(g.attack_speed, g.max_attack_speed.load());
    clamp_to(g.move_speed,   g.max_move_speed.load());
    clamp_to(g.attack_range, g.max_attack_range.load());
    clamp_to(g.unity_speed,  g.max_unity_speed.load());
    clamp_to(g.game_speed,   g.max_game_speed.load());
    clamp_to(g.damage_rate,  g.max_damage_rate.load());
    g.revision.fetch_add(1);

    LOGI("config loaded: stage=%d attack_speed=%.1f(%s)/%.1f move_speed=%.1f(%s)/%.1f attack_range=%.1f/%.1f "
         "unity_speed=%.1f/%.1f game_speed=%.1f/%.1f damage_rate=%.1f(%s)/%.1f skill_no_cd=%d god_mode=%d skip_story=%d event_points=%d",
         g.stage.load(), g.attack_speed.load(), g.attack_speed_mode.load() ? "add" : "mul", g.max_attack_speed.load(),
         g.move_speed.load(), g.move_speed_mode.load() ? "add" : "mul", g.max_move_speed.load(),
         g.attack_range.load(), g.max_attack_range.load(),
         g.unity_speed.load(), g.max_unity_speed.load(), g.game_speed.load(), g.max_game_speed.load(), g.damage_rate.load(), g.damage_rate_mode.load() ? "add" : "mul",
         g.max_damage_rate.load(), (int)g.skill_no_cd.load(), (int)g.god_mode.load(), (int)g.skip_story.load(), (int)g.event_points.load());
}

float apply(float original, float value, int mode) {
    if (value == 1.0f) return original;
    return mode == 1 ? original + (value - 1.0f) : original * value;
}

}  // namespace cfg
