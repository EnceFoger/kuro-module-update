#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include "config.h"
#include "feature_hooks.h"
#include "hook_util.h"
#include "il2cpp_api.h"
#include "log.h"

// Read-only event-point tracker. When the solo result screen reports CLEAR, the event points of that quest
// are read from the game's own QuestResultModel (LimitedEventPoint / LimitedTotalEventPoint) and written to
// kuro_companion_points.txt, which the Kuro Control overlay displays. Nothing in the game is modified.
//
// Only recorded when: the feature is ON; it is the single-player result (isCoQuestBattle == false and the model
// class is exactly QuestResultModel); and the game says the quest uses event points (IsEventQuestUseEventPoint).
namespace {
using Void0 = void (*)(void*, const void*);
using BoolGet = bool (*)(void*, const void*);

struct NumGetter {
    int32_t (*f32)(void*, const void*) = nullptr;
    int64_t (*f64)(void*, const void*) = nullptr;
    const void* mi = nullptr;
    int64_t read(void* model) const { return f64 ? f64(model, mi) : (int64_t)f32(model, mi); }
};

Void0 orig_clear = nullptr;
size_t off_model = 0, off_coop = 0;
BoolGet get_uses = nullptr;
const void* uses_mi = nullptr;
NumGetter get_gained, get_total;
std::string g_path_a, g_path_b;

// Session counters (reset every time the game starts). Main thread only.
unsigned g_seq = 0;
int64_t g_session_points = 0;
int g_session_quests = 0;
int64_t g_start = 0, g_prev = 0;     // session start / time of the previous recorded quest (epoch seconds)

inline bool read_bool(void* obj, size_t off) { return *reinterpret_cast<volatile uint8_t*>(static_cast<char*>(obj) + off) != 0; }

void write_atomic(const std::string& path, const std::string& content) {
    if (path.empty()) return;
    std::string tmp = path + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd >= 0) {
        ssize_t w = write(fd, content.data(), content.size());
        close(fd);
        if (w == (ssize_t)content.size() && rename(tmp.c_str(), path.c_str()) == 0) return;
        unlink(tmp.c_str());
    }
    fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);   // fallback: direct write
    if (fd >= 0) { ssize_t w = write(fd, content.data(), content.size()); (void)w; close(fd); }
}

void write_state(unsigned seq, int64_t gained, int64_t total, int64_t last_run) {
    char buf[320];
    snprintf(buf, sizeof buf,
             "seq=%u\ngained=%lld\ntotal=%lld\nsession_points=%lld\nsession_quests=%d\nstart=%lld\nlast_run=%lld\ntime=%lld\n",
             seq, (long long)gained, (long long)total, (long long)g_session_points, g_session_quests,
             (long long)g_start, (long long)last_run, (long long)time(nullptr));
    write_atomic(g_path_a, buf);
    write_atomic(g_path_b, buf);
}

// Fresh file at every game start, so the overlay never shows a previous session's numbers.
void init_session() {
    g_seq = 0; g_session_points = 0; g_session_quests = 0;
    g_start = g_prev = (int64_t)time(nullptr);
    write_state(0, 0, 0, 0);
}

void record(void* self) {
    if (read_bool(self, off_coop)) return;                                    // multiplayer result: not tracked
    void* model = *reinterpret_cast<void* volatile*>(static_cast<char*>(self) + off_model);
    if (!model) return;
    const char* cls = il2cpp::api.class_get_name(il2cpp::api.object_get_class(model));
    if (!cls || strcmp(cls, "QuestResultModel") != 0) return;
    if (!get_uses(model, uses_mi)) return;                                    // quest gives no event points

    int64_t gained = get_gained.read(model), total = get_total.read(model);
    if (gained < 0) gained = 0;
    int64_t now = (int64_t)time(nullptr);
    if (g_start == 0) g_start = g_prev = now;                                 // feature switched on mid-session
    int64_t last_run = now - g_prev; if (last_run < 0) last_run = 0;
    g_prev = now;
    ++g_seq; ++g_session_quests; g_session_points += gained;
    write_state(g_seq, gained, total, last_run);
    LOGI("event_points: +%lld (total %lld, session +%lld over %d quests)", (long long)gained, (long long)total,
         (long long)g_session_points, g_session_quests);
}

void hook_clear(void* self, const void* mi) {
    orig_clear(self, mi);
    if (self && cfg::g.event_points.load(std::memory_order_relaxed)) record(self);
}
}  // namespace

void features::event_points_set_paths(const std::string& a, const std::string& b) { g_path_a = a; g_path_b = b; }

bool features::install_event_points(bool dry_run) {
    using namespace il2cpp;
    const char* ns = "Bleach.Games.QuestBattle.Result";
    const char* cls = "QuestResultViewController";
    if (!api.object_get_class || !api.class_get_name) { LOGW("NOT FOUND object/class name API (needed for event_points)"); return false; }

    MethodRef clear;
    if (!resolve(ns, cls, "StartClearMode", 0, "System.Void", {}, clear)) return false;
    void* k = find_class(ns, cls);
    if (!k) return false;
    if (!instance_field(k, cls, "resultModel", nullptr, &off_model) ||
        !instance_field(k, cls, "isCoQuestBattle", "System.Boolean", &off_coop))
        return false;

    const char* mns = "Bleach.Model.QuestBattle.Result";
    MethodRef uses, gained, total;
    if (!resolve(mns, "QuestResultModel", "get_IsEventQuestUseEventPoint", 0, "System.Boolean", {}, uses)) return false;
    if (!resolve(mns, "QuestResultModel", "get_LimitedEventPoint", 0, nullptr, {}, gained)) return false;
    if (!resolve(mns, "QuestResultModel", "get_LimitedTotalEventPoint", 0, nullptr, {}, total)) return false;
    // These getters implement an interface, so the metadata marks them virtual. Calling them directly is still
    // exact: record() only reads models whose runtime class is exactly QuestResultModel.
    for (const MethodRef* m : {&gained, &total}) {
        if (m->ret_type != "System.Int32" && m->ret_type != "System.Int64") {
            LOGW("NOT FOUND supported event-point getter type (returns %s)", m->ret_type.c_str());
            return false;
        }
    }
    if (dry_run) { LOGI("probe: event_points targets OK (not hooked)"); return true; }

    get_uses = reinterpret_cast<BoolGet>(uses.addr);  uses_mi = uses.info;
    auto setup = [](NumGetter& g, const MethodRef& m) {
        g.mi = m.info;
        if (m.ret_type == "System.Int64") g.f64 = reinterpret_cast<int64_t (*)(void*, const void*)>(m.addr);
        else                              g.f32 = reinterpret_cast<int32_t (*)(void*, const void*)>(m.addr);
    };
    setup(get_gained, gained);
    setup(get_total, total);
    bool ok = install_hook("event_points(StartClearMode)", clear.addr, reinterpret_cast<void*>(&hook_clear),
                           reinterpret_cast<void**>(&orig_clear), /*check_patched=*/true);
    if (ok) init_session();
    return ok;
}
