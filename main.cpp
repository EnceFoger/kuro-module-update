#include <jni.h>

#include <cstring>
#include <string>
#include <thread>

#include "config.h"
#include "feature_hooks.h"
#include "log.h"
#include "worker.h"
#include "zygisk.hpp"  // official Zygisk module API header - see external/README.txt

namespace {
constexpr const char* kTargetPackage = "com.klab.bleach";
constexpr const char* kConfigName    = "kuro_companion.cfg";
}

class CompanionModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        std::string nice = jstr(args->nice_name);
        match_ = (nice == kTargetPackage);
        if (!match_) {
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);  // not our game: unload
            return;
        }
        std::string dir = jstr(args->app_data_dir);
        if (dir.empty()) dir = std::string("/data/data/") + kTargetPackage;
        config_path_ = dir + "/files/" + kConfigName;
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        if (!match_) return;
        std::string base = config_path_.substr(0, config_path_.find_last_of('/'));   // <data>/files
        klog_set_paths((base + "/kuro_companion.log").c_str(),
                       (std::string("/storage/emulated/0/Android/data/") + kTargetPackage + "/files/kuro_companion.log").c_str());
        features::event_points_set_paths(base + "/kuro_companion_points.txt",
                                         std::string("/storage/emulated/0/Android/data/") + kTargetPackage + "/files/kuro_companion_points.txt");
        cfg::set_path(config_path_);
        cfg::set_override_path(std::string("/storage/emulated/0/Android/data/") + kTargetPackage + "/files/" + kConfigName);
        cfg::ensure_default_file();
        cfg::reload_if_changed();
        LOGI("Kuro Companion loaded into %s, stage=%d", kTargetPackage, cfg::g.stage.load());
        if (cfg::g.stage.load() == 0) { LOGI("stage 0: idle, nothing started"); return; }
        std::thread(worker_main, config_path_).detach();
    }

private:
    std::string jstr(jstring s) {
        if (!s) return {};
        const char* c = env_->GetStringUTFChars(s, nullptr);
        std::string out = c ? c : "";
        if (c) env_->ReleaseStringUTFChars(s, c);
        return out;
    }

    zygisk::Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
    bool match_ = false;
    std::string config_path_;
};

REGISTER_ZYGISK_MODULE(CompanionModule)
