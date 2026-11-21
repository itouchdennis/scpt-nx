#pragma once
// Pandora Tomorrow's Unreal Engine 2 objects from the host: names, objects, properties, and the engine's own calls to
// make objects, run script functions and grow arrays. Game thread only.
#include <cstdint>
#include <string_view>

#include "cpu/x86_context.h"

namespace ue {

// UObject: +0x18 Outer, +0x1C ObjectFlags, +0x20 Name (FName index), +0x24 Class
constexpr uint32_t kOuter = 0x18, kName = 0x20, kClass = 0x24;
// UStruct: +0x2C SuperField, +0x3C Children; UField: +0x30 Next; UProperty: +0x44 Offset
constexpr uint32_t kSuper = 0x2C, kChildren = 0x3C, kNext = 0x30, kPropertyOffset = 0x44;
// UFunction: +0x64 ParmsSize (low 16 bits) and ReturnValueOffset (high 16)
constexpr uint32_t kParms = 0x64;
// FString / TArray: data, count, capacity (an FString is 16 bytes in a struct or a function's parameters)
constexpr uint32_t kArrayNum = 4, kArrayMax = 8, kStringSize = 16;

uint32_t FindName(std::string_view text);  // the FName index, ~0u when there is none
std::string_view NameText(uint32_t name);
uint32_t ObjectName(uint32_t obj);
uint32_t ObjectClass(uint32_t obj);
bool Valid(uint32_t obj);  // a readable object with a readable class
bool ClassIs(uint32_t obj, uint32_t class_name);       // the object's class, not its parents
uint32_t FindField(uint32_t cls, uint32_t name);       // a function or property of the class or its parents
uint32_t PropertyOffset(uint32_t cls, std::string_view name);  // ~0u when it has none
// the live objects of a class (by its FName); calls fn(object) until it returns true, returns that object
template <class F>
uint32_t EachObject(uint32_t class_name, F fn);
uint32_t ObjectCount();
uint32_t ObjectAt(uint32_t i);

// StaticConstructObject(Class, Outer, None, 0, Template): a copy of template's properties
uint32_t Construct(X86Context& ctx, uint32_t cls, uint32_t outer, uint32_t templ);
// UObject::ProcessEvent(Function, Parms)
void ProcessEvent(X86Context& ctx, uint32_t obj, uint32_t function, uint32_t parms);
// a TArray's capacity made at least max elements of size bytes through the engine's allocator, kept on growth
void Reserve(X86Context& ctx, uint32_t array, uint32_t size, uint32_t max);
// an FString set to text, in the engine's allocator (the engine frees it with the object)
void SetString(X86Context& ctx, uint32_t fstring, std::u16string_view text);
// a host owned FString's data (never freed by the engine: for parameters and localized text)
void HostString(uint32_t fstring, std::u16string_view text);

template <class F>
uint32_t EachObject(uint32_t class_name, F fn) {
    uint32_t n = ObjectCount();
    for (uint32_t i = 0; i < n; i++) {
        uint32_t obj = ObjectAt(i);
        if (obj && ClassIs(obj, class_name) && fn(obj)) return obj;
    }
    return 0;
}

}  // namespace ue
