// il2cpp bridge: typed wrappers over the resolved obfuscated il2cpp C API.
//
// Resolves pointers lazily via GetProcAddress(GameAssembly.dll, obfuscated_name).
// Provides the subset of il2cpp C API functions the patches need, plus helpers
// for the common patterns (string<->managed, invoke, class/method walking).

#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace symphytum::il2cpp {

// --- Opaque il2cpp pointer types (we never deref these beyond known offsets) ---
using Il2CppClass    = void;
using Il2CppImage    = void;
using Il2CppAssembly = void;
using Il2CppDomain   = void;
using Il2CppMethod   = void;   // MethodInfo*
using Il2CppField    = void;   // FieldInfo*
using Il2CppType     = void;
using Il2CppObject   = void;
using Il2CppString   = void;
using Il2CppThread   = void;
using Il2CppException= void;

// MethodInfo struct offsets we rely on. Stable for Unity 2021+ il2cpp.
//   methodPointer at offset 0 (void*)
//   name           at offset 8 (const char*)
// We only read methodPointer for the GuardPatch method-pointer overwrite.
struct MethodInfo {
    void*       methodPointer;   // +0
    void*       invoker_method;  // +8
    const char* name;            // +16
    Il2CppClass* klass;          // +24
    // ... more fields we don't touch
};

// Il2CppType: we only read the `type` enum byte at offset 0 (Il2CppTypeEnum).
// (Actual layout: { type : 1, ... } but byte access is fine for our use.)

// --- One-time init: resolve all function pointers. Returns false on failure. ---
bool init();

// --- Core API wrappers (return raw values; nullptr/0 on failure) ---
Il2CppDomain*   domain_get();
Il2CppAssembly** domain_get_assemblies(size_t* out_count);
Il2CppImage*    assembly_get_image(Il2CppAssembly* a);
const char*     image_get_name(Il2CppImage* img);
size_t          image_get_class_count(Il2CppImage* img);
Il2CppClass*    image_get_class(Il2CppImage* img, size_t index);

Il2CppClass*    class_from_name(Il2CppImage* img, const char* ns, const char* name);
const char*     class_get_name(Il2CppClass* klass);
const char*     class_get_namespace(Il2CppClass* klass);
Il2CppClass*    class_get_parent(Il2CppClass* klass);
bool            class_is_enum(Il2CppClass* klass);
bool            class_is_valuetype(Il2CppClass* klass);
bool            class_is_interface(Il2CppClass* klass);
Il2CppMethod*   class_get_methods(Il2CppClass* klass, void** iter);
Il2CppMethod*   class_get_method_from_name(Il2CppClass* klass, const char* name, int args_count);
Il2CppField*    class_get_fields(Il2CppClass* klass, void** iter);
const char*     field_get_name(Il2CppField* field);
size_t          field_get_offset(Il2CppField* field);
Il2CppType*     field_get_type(Il2CppField* field);
int32_t         class_instance_size(Il2CppClass* klass);
Il2CppType*     class_get_type(Il2CppClass* klass);
Il2CppClass*    class_from_type(Il2CppType* t);
bool            class_is_generic(Il2CppClass* klass);
Il2CppClass*    class_get_element_class(Il2CppClass* klass);

const char*     method_get_name(Il2CppMethod* m);
Il2CppType*     method_get_return_type(Il2CppMethod* m);
uint32_t        method_get_param_count(Il2CppMethod* m);
bool            method_is_instance(Il2CppMethod* m);

int             type_get_type(Il2CppType* t);
const char*     type_get_name(Il2CppType* t);
Il2CppClass*    type_get_class_or_element_class(Il2CppType* t);

void*           runtime_invoke(Il2CppMethod* m, void* obj, void** params, void** exc);
void            runtime_class_init(Il2CppClass* klass);
void*           object_new(Il2CppClass* klass);
Il2CppClass*    object_get_class(Il2CppObject* obj);
Il2CppString*   string_new(const char* utf8);
int32_t         string_length(Il2CppString* s);
const uint16_t* string_chars(Il2CppString* s);

Il2CppThread*   thread_attach(Il2CppDomain* domain);

// --- Helpers ---

// Convert an il2cpp string to a UTF-8 std::string.
std::string     string_to_utf8(Il2CppString* s);

// Find a class by namespace+name by scanning all loaded assemblies/images.
Il2CppClass*    find_class(const char* ns, const char* name);

// Find a method by name on a class (any arg count, first match).
Il2CppMethod*   find_method(Il2CppClass* klass, const char* name);

}  // namespace symphytum::il2cpp
