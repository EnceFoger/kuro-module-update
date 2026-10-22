#pragma once
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

// IL2CPP runtime access by *name* only. No hard-coded offsets anywhere.
namespace il2cpp {

struct Api {
    void*        (*domain_get)();
    void**       (*domain_get_assemblies)(void* domain, size_t* count);
    void*        (*assembly_get_image)(void* assembly);
    const char*  (*image_get_name)(void* image);
    void*        (*class_from_name)(void* image, const char* ns, const char* name);
    void*        (*class_get_method_from_name)(void* klass, const char* name, int argc);
    const void*  (*method_get_return_type)(void* method);
    const void*  (*method_get_param)(void* method, uint32_t index);
    uint32_t     (*method_get_flags)(void* method, uint32_t* iflags);
    char*        (*type_get_name)(const void* type);
    void*        (*thread_attach)(void* domain);
    void         (*free_mem)(void* ptr);   // optional
    // Optional (needed only by features that read fields / check parameter kinds):
    int          (*type_get_type)(const void* type);
    void*        (*class_from_type)(const void* type);
    bool         (*class_is_enum)(void* klass);
    void*        (*object_get_class)(void* obj);
    const char*  (*class_get_name)(void* klass);
    void*        (*class_get_field_from_name)(void* klass, const char* name);
    size_t       (*field_get_offset)(void* field);
    const void*  (*field_get_type)(void* field);
    int          (*field_get_flags)(void* field);
};

extern Api api;

struct LibRef { uintptr_t bias = 0; const void* dynamic = nullptr; };

bool locate_library(LibRef& out);                 // finds the loaded libil2cpp.so (or a renamed copy)
void log_loaded_libs();                           // logs il2cpp/unity-related loaded libraries (diagnostics)
bool load_api(const LibRef& lib);                 // resolves every Api member by exported name
bool wait_until_ready(int timeout_seconds);       // domain + Assembly-CSharp present, thread attached

struct MethodRef {
    void*       info = nullptr;   // MethodInfo*
    void*       addr = nullptr;   // native function pointer (MethodInfo::methodPointer)
    std::string ret_type;
    uint32_t    flags = 0;    // METHOD_ATTRIBUTE_* (0x10 static, 0x40 virtual)
};

// Looks the method up by namespace/class/name/argc and verifies it is an instance method with the
// expected return and parameter types. Returns false and logs "NOT FOUND ..." on any mismatch.
// expect_ret == nullptr accepts any return type (caller inspects ret_type).
bool resolve(const char* ns, const char* cls, const char* method, int argc,
             const char* expect_ret, std::initializer_list<const char*> expect_params,
             MethodRef& out, bool is_static = false);

std::string type_name(const void* type);
void* find_class(const char* ns, const char* name);

// Offset of a non-static instance field, with its type name checked (expect_type may be nullptr).
bool instance_field(void* klass, const char* label, const char* name, const char* expect_type, size_t* offset);

// True when every parameter of `method` travels in an integer register (bool/int/enum/reference), so a
// generic forwarding hook can pass the arguments through unchanged. Logs the first offending type.
bool params_in_registers(void* method, int argc, const char* label);

}  // namespace il2cpp
