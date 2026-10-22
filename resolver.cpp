#include "il2cpp_api.h"
#include "log.h"

#include <cstring>

namespace il2cpp {

std::string type_name(const void* t) {
    if (!t) return "<null>";
    char* s = api.type_get_name(t);
    if (!s) return "<null>";
    std::string out = s;
    if (api.free_mem) api.free_mem(s);
    return out;
}

void* find_class(const char* ns, const char* name) {
    size_t n = 0;
    void** asms = api.domain_get_assemblies(api.domain_get(), &n);
    if (!asms) return nullptr;
    for (size_t i = 0; i < n; ++i) {
        void* img = api.assembly_get_image(asms[i]);
        if (!img) continue;
        if (void* k = api.class_from_name(img, ns, name)) return k;
    }
    return nullptr;
}

bool resolve(const char* ns, const char* cls, const char* method, int argc,
             const char* expect_ret, std::initializer_list<const char*> expect_params,
             MethodRef& out, bool is_static) {
    void* klass = find_class(ns, cls);
    if (!klass) { LOGW("NOT FOUND class %s.%s", ns, cls); return false; }

    void* m = api.class_get_method_from_name(klass, method, argc);
    if (!m) { LOGW("NOT FOUND method %s.%s::%s(%d args)", ns, cls, method, argc); return false; }

    void* addr = *reinterpret_cast<void**>(m);   // MethodInfo::methodPointer is the first field
    if (!addr) { LOGW("NOT FOUND native code for %s.%s::%s (methodPointer is null)", ns, cls, method); return false; }

    uint32_t flags = api.method_get_flags(m, nullptr);
    if (((flags & 0x0010u) != 0) != is_static) {  // METHOD_ATTRIBUTE_STATIC
        LOGW("NOT FOUND %s method %s.%s::%s (it is %s)", is_static ? "static" : "instance", ns, cls, method,
             is_static ? "an instance method" : "static");
        return false;
    }

    std::string ret = type_name(api.method_get_return_type(m));
    if (expect_ret && ret != expect_ret) {
        LOGW("NOT FOUND %s.%s::%s with return %s (found %s)", ns, cls, method, expect_ret, ret.c_str());
        return false;
    }
    uint32_t i = 0;
    for (const char* want : expect_params) {
        std::string got = type_name(api.method_get_param(m, i));
        if (got != want) {
            LOGW("NOT FOUND %s.%s::%s param %u %s (found %s)", ns, cls, method, i, want, got.c_str());
            return false;
        }
        ++i;
    }
    out.info = m;
    out.addr = addr;
    out.ret_type = ret;
    out.flags = flags;
    LOGI("found %s.%s::%s -> %p (returns %s)", ns, cls, method, addr, ret.c_str());
    return true;
}

bool instance_field(void* klass, const char* label, const char* name, const char* expect_type, size_t* offset) {
    if (!api.class_get_field_from_name || !api.field_get_offset || !api.field_get_type || !api.field_get_flags) {
        LOGW("NOT FOUND field API in libil2cpp (needed for %s)", label);
        return false;
    }
    void* f = api.class_get_field_from_name(klass, name);
    if (!f) { LOGW("NOT FOUND field %s::%s", label, name); return false; }
    if (api.field_get_flags(f) & 0x0010) { LOGW("NOT FOUND instance field %s::%s (it is static)", label, name); return false; }
    if (expect_type) {
        std::string t = type_name(api.field_get_type(f));
        if (t != expect_type) { LOGW("NOT FOUND field %s::%s as %s (found %s)", label, name, expect_type, t.c_str()); return false; }
    }
    *offset = api.field_get_offset(f);
    return true;
}

bool params_in_registers(void* method, int argc, const char* label) {
    if (!api.type_get_type || !api.class_from_type || !api.class_is_enum) {
        LOGW("NOT FOUND type-inspection API in libil2cpp (needed for %s)", label);
        return false;
    }
    for (int i = 0; i < argc; ++i) {
        const void* t = api.method_get_param(method, (uint32_t)i);
        int kind = api.type_get_type(t);
        bool ok;
        switch (kind) {
            case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11:  // bool/char/ints
            case 14: case 15: case 16: case 18: case 20: case 24: case 25: case 28: case 29:   // string/ptr/ref/class/array/object
                ok = true; break;
            case 17: {                                                                         // value type: only enums
                void* k = api.class_from_type(t);
                ok = k && api.class_is_enum(k);
                break;
            }
            default: ok = false;
        }
        if (!ok) {
            LOGW("NOT FOUND %s: parameter %d is %s (kind %d), not safe to forward generically", label, i,
                 type_name(t).c_str(), kind);
            return false;
        }
    }
    return true;
}

}  // namespace il2cpp
