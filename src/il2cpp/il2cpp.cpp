// il2cpp bridge implementation.
#include "il2cpp.hpp"
#include "il2cpp_names.hpp"
#include "log.hpp"
#include <windows.h>
#include <cstring>
#include <string>
#include <unordered_map>

namespace symphytum::il2cpp {

namespace {

// Resolved function pointer cache: canonical name -> raw address.
std::unordered_map<std::string, void*> g_fns;

HMODULE g_game_assembly = nullptr;

// Resolve one il2cpp C API function by canonical name. Caches the result.
void* fn(const char* canonical) {
    auto it = g_fns.find(canonical);
    if (it != g_fns.end()) return it->second;
    void* p = nullptr;
    const std::string& obf = il2cpp_names::lookup(canonical);
    if (!obf.empty() && g_game_assembly) {
        p = reinterpret_cast<void*>(GetProcAddress(g_game_assembly, obf.c_str()));
    }
    g_fns[canonical] = p;
    if (!p) SYM_LOG("il2cpp", "failed to resolve {} (obf={})",
                     canonical, obf.empty() ? "<missing>" : obf.c_str());
    return p;
}

// Typed function-pointer fetch + cast helper.
template <typename T>
T fn_t(const char* canonical) {
    return reinterpret_cast<T>(fn(canonical));
}

}  // namespace

bool init() {
    g_game_assembly = GetModuleHandleW(L"GameAssembly.dll");
    if (!g_game_assembly) {
        SYM_LOG("il2cpp", "GameAssembly.dll not loaded");
        return false;
    }
    // Pre-resolve the subset we use, so failures are visible up front.
    // (Any null return is logged by fn().)
    (void)fn("il2cpp_domain_get");
    (void)fn("il2cpp_domain_get_assemblies");
    (void)fn("il2cpp_assembly_get_image");
    (void)fn("il2cpp_image_get_name");
    (void)fn("il2cpp_image_get_class_count");
    (void)fn("il2cpp_image_get_class");
    (void)fn("il2cpp_class_from_name");
    (void)fn("il2cpp_class_get_name");
    (void)fn("il2cpp_class_get_namespace");
    (void)fn("il2cpp_class_get_parent");
    (void)fn("il2cpp_class_is_enum");
    (void)fn("il2cpp_class_is_valuetype");
    (void)fn("il2cpp_class_is_interface");
    (void)fn("il2cpp_class_get_methods");
    (void)fn("il2cpp_class_get_method_from_name");
    (void)fn("il2cpp_class_get_fields");
    (void)fn("il2cpp_field_get_name");
    (void)fn("il2cpp_field_get_offset");
    (void)fn("il2cpp_field_get_type");
    (void)fn("il2cpp_class_get_type");
    (void)fn("il2cpp_class_from_type");
    (void)fn("il2cpp_class_is_generic");
    (void)fn("il2cpp_class_get_element_class");
    (void)fn("il2cpp_method_get_name");
    (void)fn("il2cpp_method_get_return_type");
    (void)fn("il2cpp_method_get_param_count");
    (void)fn("il2cpp_method_is_instance");
    (void)fn("il2cpp_type_get_type");
    (void)fn("il2cpp_type_get_name");
    (void)fn("il2cpp_runtime_invoke");
    (void)fn("il2cpp_runtime_class_init");
    (void)fn("il2cpp_object_get_class");
    (void)fn("il2cpp_object_new");
    (void)fn("il2cpp_string_chars");
    (void)fn("il2cpp_string_length");
    (void)fn("il2cpp_thread_attach");
    SYM_LOG("il2cpp", "bridge initialized");
    return true;
}

Il2CppDomain* domain_get() {
    using Sig = Il2CppDomain* (*)();
    auto p = fn_t<Sig>("il2cpp_domain_get");
    return p ? p() : nullptr;
}

Il2CppAssembly** domain_get_assemblies(size_t* out_count) {
    if (out_count) *out_count = 0;
    using Sig = Il2CppAssembly** (*)(Il2CppDomain*, size_t*);
    auto p = fn_t<Sig>("il2cpp_domain_get_assemblies");
    if (!p) return nullptr;
    auto* d = domain_get();
    return d ? p(d, out_count) : nullptr;
}

Il2CppImage* assembly_get_image(Il2CppAssembly* a) {
    using Sig = Il2CppImage* (*)(Il2CppAssembly*);
    return fn_t<Sig>("il2cpp_assembly_get_image")(a);
}

const char* image_get_name(Il2CppImage* img) {
    using Sig = const char* (*)(Il2CppImage*);
    return fn_t<Sig>("il2cpp_image_get_name")(img);
}

size_t image_get_class_count(Il2CppImage* img) {
    using Sig = size_t (*)(Il2CppImage*);
    return fn_t<Sig>("il2cpp_image_get_class_count")(img);
}

Il2CppClass* image_get_class(Il2CppImage* img, size_t index) {
    using Sig = Il2CppClass* (*)(Il2CppImage*, size_t);
    return fn_t<Sig>("il2cpp_image_get_class")(img, index);
}

Il2CppClass* class_from_name(Il2CppImage* img, const char* ns, const char* name) {
    using Sig = Il2CppClass* (*)(Il2CppImage*, const char*, const char*);
    return fn_t<Sig>("il2cpp_class_from_name")(img, ns, name);
}

const char* class_get_name(Il2CppClass* klass) {
    using Sig = const char* (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_get_name")(klass);
}
const char* class_get_namespace(Il2CppClass* klass) {
    using Sig = const char* (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_get_namespace")(klass);
}
Il2CppClass* class_get_parent(Il2CppClass* klass) {
    using Sig = Il2CppClass* (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_get_parent")(klass);
}
bool class_is_enum(Il2CppClass* klass) {
    using Sig = bool (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_is_enum")(klass);
}
bool class_is_valuetype(Il2CppClass* klass) {
    using Sig = bool (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_is_valuetype")(klass);
}
bool class_is_interface(Il2CppClass* klass) {
    using Sig = bool (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_is_interface")(klass);
}
Il2CppMethod* class_get_methods(Il2CppClass* klass, void** iter) {
    using Sig = Il2CppMethod* (*)(Il2CppClass*, void**);
    return fn_t<Sig>("il2cpp_class_get_methods")(klass, iter);
}
Il2CppMethod* class_get_method_from_name(Il2CppClass* klass, const char* name, int args_count) {
    using Sig = Il2CppMethod* (*)(Il2CppClass*, const char*, int);
    return fn_t<Sig>("il2cpp_class_get_method_from_name")(klass, name, args_count);
}
Il2CppField* class_get_fields(Il2CppClass* klass, void** iter) {
    using Sig = Il2CppField* (*)(Il2CppClass*, void**);
    return fn_t<Sig>("il2cpp_class_get_fields")(klass, iter);
}
const char* field_get_name(Il2CppField* field) {
    using Sig = const char* (*)(Il2CppField*);
    return fn_t<Sig>("il2cpp_field_get_name")(field);
}
size_t field_get_offset(Il2CppField* field) {
    using Sig = size_t (*)(Il2CppField*);
    return fn_t<Sig>("il2cpp_field_get_offset")(field);
}
Il2CppType* field_get_type(Il2CppField* field) {
    using Sig = Il2CppType* (*)(Il2CppField*);
    return fn_t<Sig>("il2cpp_field_get_type")(field);
}
int32_t class_instance_size(Il2CppClass* klass) {
    using Sig = int32_t (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_instance_size")(klass);
}
Il2CppType* class_get_type(Il2CppClass* klass) {
    using Sig = Il2CppType* (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_get_type")(klass);
}
Il2CppClass* class_from_type(Il2CppType* t) {
    using Sig = Il2CppClass* (*)(Il2CppType*);
    return fn_t<Sig>("il2cpp_class_from_type")(t);
}
bool class_is_generic(Il2CppClass* klass) {
    using Sig = bool (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_is_generic")(klass);
}
Il2CppClass* class_get_element_class(Il2CppClass* klass) {
    using Sig = Il2CppClass* (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_class_get_element_class")(klass);
}

const char* method_get_name(Il2CppMethod* m) {
    using Sig = const char* (*)(Il2CppMethod*);
    return fn_t<Sig>("il2cpp_method_get_name")(m);
}
Il2CppType* method_get_return_type(Il2CppMethod* m) {
    using Sig = Il2CppType* (*)(Il2CppMethod*);
    return fn_t<Sig>("il2cpp_method_get_return_type")(m);
}
uint32_t method_get_param_count(Il2CppMethod* m) {
    using Sig = uint32_t (*)(Il2CppMethod*);
    return fn_t<Sig>("il2cpp_method_get_param_count")(m);
}
bool method_is_instance(Il2CppMethod* m) {
    using Sig = bool (*)(Il2CppMethod*);
    return fn_t<Sig>("il2cpp_method_is_instance")(m);
}

int type_get_type(Il2CppType* t) {
    using Sig = int (*)(Il2CppType*);
    return fn_t<Sig>("il2cpp_type_get_type")(t);
}

const char* type_get_name(Il2CppType* t) {
    using Sig = const char* (*)(Il2CppType*);
    return fn_t<Sig>("il2cpp_type_get_name")(t);
}
Il2CppClass* type_get_class_or_element_class(Il2CppType* t) {
    using Sig = Il2CppClass* (*)(Il2CppType*);
    return fn_t<Sig>("il2cpp_type_get_class_or_element_class")(t);
}

void* runtime_invoke(Il2CppMethod* m, void* obj, void** params, void** exc) {
    using Sig = void* (*)(Il2CppMethod*, void*, void**, void**);
    return fn_t<Sig>("il2cpp_runtime_invoke")(m, obj, params, exc);
}
void runtime_class_init(Il2CppClass* klass) {
    using Sig = void (*)(Il2CppClass*);
    fn_t<Sig>("il2cpp_runtime_class_init")(klass);
}
void* object_new(Il2CppClass* klass) {
    using Sig = void* (*)(Il2CppClass*);
    return fn_t<Sig>("il2cpp_object_new")(klass);
}
Il2CppClass* object_get_class(Il2CppObject* obj) {
    using Sig = Il2CppClass* (*)(Il2CppObject*);
    return fn_t<Sig>("il2cpp_object_get_class")(obj);
}

Il2CppString* string_new(const char* utf8) {
    using Sig = Il2CppString* (*)(const char*);
    return fn_t<Sig>("il2cpp_string_new")(utf8);
}
int32_t string_length(Il2CppString* s) {
    using Sig = int32_t (*)(Il2CppString*);
    return fn_t<Sig>("il2cpp_string_length")(s);
}
const uint16_t* string_chars(Il2CppString* s) {
    using Sig = const uint16_t* (*)(Il2CppString*);
    return fn_t<Sig>("il2cpp_string_chars")(s);
}

Il2CppThread* thread_attach(Il2CppDomain* domain) {
    using Sig = Il2CppThread* (*)(Il2CppDomain*);
    return fn_t<Sig>("il2cpp_thread_attach")(domain);
}

std::string string_to_utf8(Il2CppString* s) {
    if (!s) return {};
    int32_t n = string_length(s);
    const uint16_t* chars = string_chars(s);
    if (!chars || n <= 0) return {};
    // Narrow UTF-16 -> UTF-8 (handle BMP only, which is all we expect for these
    // identifiers and URLs). Simple per-codepoint conversion.
    std::string out;
    out.reserve(static_cast<size_t>(n));
    for (int32_t i = 0; i < n; ++i) {
        uint32_t cp = chars[i];
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {  // BMP, no surrogates expected for our data
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

Il2CppClass* find_class(const char* ns, const char* name) {
    size_t count = 0;
    Il2CppAssembly** asms = domain_get_assemblies(&count);
    if (!asms) return nullptr;

    // Try the provided namespace, then empty namespace (many game classes live
    // in the global namespace). il2cpp_class_from_name is the fast path.
    const char* namespaces[] = { ns, "" };
    for (const char* try_ns : namespaces) {
        for (size_t i = 0; i < count; ++i) {
            Il2CppImage* img = assembly_get_image(asms[i]);
            if (!img) continue;
            Il2CppClass* k = class_from_name(img, try_ns, name);
            if (k) {
                if (try_ns != ns)
                    SYM_LOG("il2cpp", "found {}.{} in global namespace", ns, name);
                return k;
            }
        }
    }

    // Fallback: full image×class scan comparing class_get_name. Slow but
    // bulletproof — handles classes the fast path misses (wrong namespace,
    // nested types, image quirks).
    for (size_t i = 0; i < count; ++i) {
        Il2CppImage* img = assembly_get_image(asms[i]);
        if (!img) continue;
        size_t cn = image_get_class_count(img);
        for (size_t c = 0; c < cn; ++c) {
            Il2CppClass* k = image_get_class(img, c);
            if (!k) continue;
            const char* cn_name = class_get_name(k);
            if (cn_name && strcmp(cn_name, name) == 0) {
                const char* cn_ns = class_get_namespace(k);
                SYM_LOG("il2cpp", "found {} in image[{}] via scan (ns=\"{}\")",
                        name, i, cn_ns ? cn_ns : "");
                return k;
            }
        }
    }
    return nullptr;
}

Il2CppMethod* find_method(Il2CppClass* klass, const char* name) {
    void* iter = nullptr;
    while (auto* m = class_get_methods(klass, &iter)) {
        if (strcmp(method_get_name(m), name) == 0) return m;
    }
    return nullptr;
}

}  // namespace symphytum::il2cpp
