#pragma once
#include <android/log.h>

#define LOG_TAG "KuroCompanion"

// Writes to logcat and, once klog_set_paths() was called, to up to two log files.
void klog_set_paths(const char* path_a, const char* path_b);
void klog(int prio, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

#define LOGI(...) klog(ANDROID_LOG_INFO,  __VA_ARGS__)
#define LOGW(...) klog(ANDROID_LOG_WARN,  __VA_ARGS__)
#define LOGE(...) klog(ANDROID_LOG_ERROR, __VA_ARGS__)
