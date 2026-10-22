#include "il2cpp_api.h"
#include "log.h"

#include <chrono>
#include <cstring>
#include <strings.h>
#include <elf.h>
#include <link.h>
#include <thread>
#include <unistd.h>
#include <type_traits>

namespace il2cpp {

Api api{};

// ---- locate libil2cpp.so and look up exported symbols without dlopen/namespace problems ----

struct FindCtx { LibRef* out; bool found; bool exact; };

static bool ci_contains(const char* hay, const char* needle) {
    size_t n = strlen(needle);
    for (; *hay; ++hay) if (strncasecmp(hay, needle, n) == 0) return true;
    return false;
}

static void take(FindCtx* ctx, const dl_phdr_info* info) {
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            ctx->out->bias = info->dlpi_addr;
            ctx->out->dynamic = reinterpret_cast<const void*>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            ctx->found = true;
            return;
        }
    }
}

// Exact "libil2cpp.so" wins; otherwise any loaded library whose file name contains "il2cpp"
// (e.g. a renamed copy such as libil2cpp_1.so) is accepted as a fallback.
static int phdr_cb(struct dl_phdr_info* info, size_t, void* data) {
    auto* ctx = static_cast<FindCtx*>(data);
    if (!info->dlpi_name || !*info->dlpi_name) return 0;
    const char* base = strrchr(info->dlpi_name, '/');
    base = base ? base + 1 : info->dlpi_name;
    if (strcmp(base, "libil2cpp.so") == 0) { take(ctx, info); ctx->exact = ctx->found; return ctx->found ? 1 : 0; }
    if (!ctx->found && ci_contains(base, "il2cpp")) take(ctx, info);
    return 0;
}

bool locate_library(LibRef& out) {
    FindCtx ctx{&out, false, false};
    dl_iterate_phdr(phdr_cb, &ctx);
    return ctx.found;
}

// Diagnostics for the log when nothing was found: which relevant libraries ARE loaded.
void log_loaded_libs() {
    dl_iterate_phdr([](struct dl_phdr_info* info, size_t, void*) -> int {
        if (info->dlpi_name && *info->dlpi_name &&
            (ci_contains(info->dlpi_name, "il2cpp") || ci_contains(info->dlpi_name, "unity") ||
             ci_contains(info->dlpi_name, "libmain") || ci_contains(info->dlpi_name, "com.klab")))
            LOGW("loaded lib: %s", info->dlpi_name);
        return 0;
    }, nullptr);
}

static uintptr_t fix(uintptr_t bias, uintptr_t p) { return p < bias ? bias + p : p; }

static size_t symbol_count(uintptr_t hash, uintptr_t gnu_hash) {
    if (hash) return reinterpret_cast<const uint32_t*>(hash)[1];  // nchain
    if (!gnu_hash) return 0;
    auto* h = reinterpret_cast<const uint32_t*>(gnu_hash);
    uint32_t nbuckets = h[0], symoffset = h[1], bloom_size = h[2];
    auto* bloom = reinterpret_cast<const uint64_t*>(h + 4);
    auto* buckets = reinterpret_cast<const uint32_t*>(bloom + bloom_size);
    auto* chain = buckets + nbuckets;
    uint32_t max = 0;
    for (uint32_t i = 0; i < nbuckets; ++i) if (buckets[i] > max) max = buckets[i];
    if (max < symoffset) return symoffset;
    while (!(chain[max - symoffset] & 1)) ++max;
    return max + 1;
}

struct SymTab { const ElfW(Sym)* syms = nullptr; const char* str = nullptr; size_t count = 0; uintptr_t bias = 0; };

static bool read_symtab(const LibRef& lib, SymTab& t) {
    uintptr_t symtab = 0, strtab = 0, hash = 0, gnu = 0;
    for (auto* d = static_cast<const ElfW(Dyn)*>(lib.dynamic); d->d_tag != DT_NULL; ++d) {
        switch (d->d_tag) {
            case DT_SYMTAB:   symtab = fix(lib.bias, d->d_un.d_ptr); break;
            case DT_STRTAB:   strtab = fix(lib.bias, d->d_un.d_ptr); break;
            case DT_HASH:     hash   = fix(lib.bias, d->d_un.d_ptr); break;
            case DT_GNU_HASH: gnu    = fix(lib.bias, d->d_un.d_ptr); break;
        }
    }
    if (!symtab || !strtab) return false;
    t.syms = reinterpret_cast<const ElfW(Sym)*>(symtab);
    t.str = reinterpret_cast<const char*>(strtab);
    t.bias = lib.bias;
    t.count = symbol_count(hash, gnu);
    return t.count != 0;
}

static void* lookup(const SymTab& t, const char* name) {
    for (size_t i = 1; i < t.count; ++i) {
        const ElfW(Sym)& s = t.syms[i];
        if (s.st_shndx == SHN_UNDEF || s.st_value == 0) continue;
        if (strcmp(t.str + s.st_name, name) == 0) return reinterpret_cast<void*>(t.bias + s.st_value);
    }
    return nullptr;
}

bool load_api(const LibRef& lib) {
    SymTab t;
    if (!read_symtab(lib, t)) { LOGE("libil2cpp.so: cannot read dynamic symbol table"); return false; }

    bool ok = true;
    auto need = [&](const char* name, auto& slot) {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(lookup(t, name));
        if (!slot) { LOGE("NOT FOUND: libil2cpp export %s", name); ok = false; }
    };
    need("il2cpp_domain_get",                 api.domain_get);
    need("il2cpp_domain_get_assemblies",      api.domain_get_assemblies);
    need("il2cpp_assembly_get_image",         api.assembly_get_image);
    need("il2cpp_image_get_name",             api.image_get_name);
    need("il2cpp_class_from_name",            api.class_from_name);
    need("il2cpp_class_get_method_from_name", api.class_get_method_from_name);
    need("il2cpp_method_get_return_type",     api.method_get_return_type);
    need("il2cpp_method_get_param",           api.method_get_param);
    need("il2cpp_method_get_flags",           api.method_get_flags);
    need("il2cpp_type_get_name",              api.type_get_name);
    need("il2cpp_thread_attach",              api.thread_attach);
    api.free_mem = reinterpret_cast<decltype(api.free_mem)>(lookup(t, "il2cpp_free"));  // optional
    auto opt = [&](const char* name, auto& slot) {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(lookup(t, name));
    };
    opt("il2cpp_type_get_type",             api.type_get_type);
    opt("il2cpp_class_from_type",           api.class_from_type);
    opt("il2cpp_class_is_enum",             api.class_is_enum);
    opt("il2cpp_object_get_class",          api.object_get_class);
    opt("il2cpp_class_get_name",            api.class_get_name);
    opt("il2cpp_class_get_field_from_name", api.class_get_field_from_name);
    opt("il2cpp_field_get_offset",          api.field_get_offset);
    opt("il2cpp_field_get_type",            api.field_get_type);
    opt("il2cpp_field_get_flags",           api.field_get_flags);
    return ok;
}

// ---- readiness ----

static void* g_domain = nullptr;

static bool has_game_assembly() {
    size_t n = 0;
    LOGI("step: il2cpp_domain_get_assemblies");
    void** asms = api.domain_get_assemblies(g_domain, &n);
    LOGI("step: assemblies=%p count=%zu", (void*)asms, n);
    if (!asms) return false;
    for (size_t i = 0; i < n; ++i) {
        void* img = api.assembly_get_image(asms[i]);
        const char* name = img ? api.image_get_name(img) : nullptr;
        if (name && strncmp(name, "Assembly-CSharp", 15) == 0) return true;
    }
    return false;
}

// Reads 16 bytes at `p` without risking a crash: the kernel copies them into a pipe and returns
// EFAULT for an unmapped address instead of raising SIGSEGV.
static bool safe_read16(const void* p, uint32_t out[4]) {
    static int fds[2] = {-1, -1};
    if (fds[0] < 0 && pipe(fds) != 0) { fds[0] = fds[1] = -1; return false; }
    if (write(fds[1], p, 16) != 16) return false;
    return read(fds[0], out, 16) == 16;
}

// aarch64 code never starts with four zero words (0x00000000 is the permanently-undefined UDF).
// The shipped libil2cpp.so has an all-zero code segment on disk, so a function whose first words are
// still zero has not been restored/decrypted yet and calling it would raise SIGILL.
static bool code_ready(void* fn) {
    uint32_t w[4];
    if (!fn || !safe_read16(fn, w)) return false;
    return (w[0] | w[1] | w[2] | w[3]) != 0;
}

static bool api_code_ready(std::string* missing) {
    struct E { const char* n; void* p; } list[] = {
        {"domain_get", (void*)api.domain_get}, {"domain_get_assemblies", (void*)api.domain_get_assemblies},
        {"assembly_get_image", (void*)api.assembly_get_image}, {"image_get_name", (void*)api.image_get_name},
        {"class_from_name", (void*)api.class_from_name}, {"class_get_method_from_name", (void*)api.class_get_method_from_name},
        {"method_get_return_type", (void*)api.method_get_return_type}, {"method_get_param", (void*)api.method_get_param},
        {"method_get_flags", (void*)api.method_get_flags}, {"type_get_name", (void*)api.type_get_name},
        {"thread_attach", (void*)api.thread_attach}};
    bool all = true;
    for (auto& e : list) if (!code_ready(e.p)) { all = false; if (missing) { *missing += e.n; *missing += ' '; } }
    return all;
}

bool wait_until_ready(int timeout_seconds) {
    using namespace std::chrono;
    auto deadline = steady_clock::now() + seconds(timeout_seconds);
    int waits = 0;
    for (;;) {
        std::string missing;
        if (api_code_ready(&missing)) break;
        if (steady_clock::now() >= deadline) { LOGE("IL2CPP function code still empty (not restored) - not calling it: %s", missing.c_str()); return false; }
        if (waits++ % 20 == 0) LOGI("step: waiting for IL2CPP code to be restored in memory: %s", missing.c_str());
        std::this_thread::sleep_for(milliseconds(500));
    }
    LOGI("step: IL2CPP function code is present in memory");
    int tries = 0;
    while (steady_clock::now() < deadline) {
        if (tries == 0) LOGI("step: calling il2cpp_domain_get (%p)", (void*)api.domain_get);
        g_domain = api.domain_get();
        if (tries == 0) LOGI("step: il2cpp_domain_get returned %p", g_domain);
        if (g_domain) break;
        if (++tries % 20 == 0) LOGI("step: domain still null after %d tries", tries);
        std::this_thread::sleep_for(milliseconds(500));
    }
    if (!g_domain) { LOGE("IL2CPP domain never became available"); return false; }
    LOGI("IL2CPP domain available; letting runtime finish initialising");
    std::this_thread::sleep_for(seconds(3));
    LOGI("step: il2cpp_thread_attach");
    api.thread_attach(g_domain);
    LOGI("step: thread attached");
    int polls = 0;
    while (steady_clock::now() < deadline) {
        if (has_game_assembly()) { LOGI("Assembly-CSharp loaded"); return true; }
        if (++polls % 10 == 0) LOGI("step: Assembly-CSharp not yet loaded (%d polls)", polls);
        std::this_thread::sleep_for(seconds(1));
    }
    LOGE("Assembly-CSharp never appeared");
    return false;
}

}  // namespace il2cpp
