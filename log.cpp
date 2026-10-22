#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static std::mutex g_mu;
static int g_fd[2] = {-1, -1};

static int open_log(const char* path) {
    if (!path || !*path) return -1;
    std::string p = path;
    size_t slash = p.find_last_of('/');
    if (slash != std::string::npos) mkdir(p.substr(0, slash).c_str(), 0771);
    return open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
}

void klog_set_paths(const char* a, const char* b) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_fd[0] = open_log(a);
    g_fd[1] = open_log(b);
}

void klog(int prio, const char* fmt, ...) {
    char msg[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    __android_log_write(prio, LOG_TAG, msg);

    std::lock_guard<std::mutex> lk(g_mu);
    if (g_fd[0] < 0 && g_fd[1] < 0) return;
    struct timeval tv; gettimeofday(&tv, nullptr);
    struct tm tmv; localtime_r(&tv.tv_sec, &tmv);
    char line[700];
    int n = snprintf(line, sizeof line, "%02d:%02d:%02d.%03d %c %s\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
                     (int)(tv.tv_usec / 1000), prio >= ANDROID_LOG_ERROR ? 'E' : prio >= ANDROID_LOG_WARN ? 'W' : 'I', msg);
    if (n < 0) return;
    if (n > (int)sizeof line - 1) n = sizeof line - 1;
    for (int fd : g_fd) if (fd >= 0) { ssize_t r = write(fd, line, n); (void)r; fsync(fd); }
}
