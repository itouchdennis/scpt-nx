// See unreal.h. The addresses are Pandora Tomorrow's (config/scpt.toml [data]).
#include "scpt/unreal.h"

#include <cstring>
#include <string>
#include <unordered_map>

#include "kernel/memory.h"
#include "xdk/hle.h"

using namespace kernel;

namespace ue {

namespace {

// GNames: TArray of FNameEntry*, an entry is index, flags, hash next, then the ANSI text
constexpr uint32_t kNameText = 0xC;

// a pointer that may be read: in the user range the engine's heap lives in
bool Readable(uint32_t p) { return p >= 0x10000 && p < 0x7FFE0000; }

}  // namespace

uint32_t FindName(std::string_view text) {
    static std::unordered_map<std::string, uint32_t> cache;
    auto it = cache.find(std::string(text));
    if (it != cache.end()) return it->second;
    uint32_t data = R32(SYM(Engine_GNames)), n = R32(SYM(Engine_GNames) + kArrayNum);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = R32(data + i * 4);
        if (!Readable(e)) continue;
        const char* s = static_cast<const char*>(HostPtr(e + kNameText));
        if (std::strncmp(s, text.data(), text.size()) == 0 && s[text.size()] == 0) {
            cache.emplace(std::string(text), i);
            return i;
        }
    }
    return ~0u;  // not cached: the name may come with a later package
}

std::string_view NameText(uint32_t name) {
    uint32_t data = R32(SYM(Engine_GNames));
    if (name >= R32(SYM(Engine_GNames) + kArrayNum)) return {};
    uint32_t e = R32(data + name * 4);
    return Readable(e) ? std::string_view(static_cast<const char*>(HostPtr(e + kNameText))) : std::string_view();
}

uint32_t ObjectName(uint32_t obj) { return R32(obj + kName); }
uint32_t ObjectClass(uint32_t obj) { return R32(obj + kClass); }

bool ClassIs(uint32_t obj, uint32_t class_name) {
    uint32_t c = Readable(obj) ? ObjectClass(obj) : 0;
    return Readable(c) && ObjectName(c) == class_name;
}

uint32_t FindField(uint32_t cls, uint32_t name) {
    for (uint32_t c = cls; Readable(c); c = R32(c + kSuper))
        for (uint32_t f = R32(c + kChildren); Readable(f); f = R32(f + kNext))
            if (ObjectName(f) == name) return f;
    return 0;
}

uint32_t PropertyOffset(uint32_t cls, std::string_view name) {
    uint32_t f = FindField(cls, FindName(name));
    return f ? R32(f + kPropertyOffset) : ~0u;
}

uint32_t ObjectCount() { return R32(SYM(Engine_GObjects) + kArrayNum); }
uint32_t ObjectAt(uint32_t i) { return R32(R32(SYM(Engine_GObjects)) + i * 4); }

uint32_t Construct(X86Context& ctx, uint32_t cls, uint32_t outer, uint32_t templ) {
    return CallGuest(ctx, SYM(Engine_StaticConstructObject), {cls, outer, 0, 0, templ, 0, 0});
}

void ProcessEvent(X86Context& ctx, uint32_t obj, uint32_t function, uint32_t parms) {
    ctx.ecx = obj;
    CallGuest(ctx, SYM(Engine_ProcessEvent), {function, parms, 0});
}

void Reserve(X86Context& ctx, uint32_t array, uint32_t size, uint32_t max) {
    if (R32(array + kArrayMax) >= max && R32(array)) return;
    W32(array + kArrayMax, max);
    ctx.ecx = array;
    CallGuest(ctx, SYM(Engine_ArrayRealloc), {size});
}

void SetString(X86Context& ctx, uint32_t fstring, std::u16string_view text) {
    uint32_t n = uint32_t(text.size()) + 1;
    Reserve(ctx, fstring, 2, n);
    std::memcpy(HostPtr(R32(fstring)), text.data(), text.size() * 2);
    W16(R32(fstring) + uint32_t(text.size()) * 2, 0);
    W32(fstring + kArrayNum, n);
}

void HostString(uint32_t fstring, std::u16string_view text) {
    uint32_t n = uint32_t(text.size()) + 1;
    uint32_t data = mem::AllocSystem(n * 2);
    std::memcpy(HostPtr(data), text.data(), text.size() * 2);
    W16(data + uint32_t(text.size()) * 2, 0);
    W32(fstring, data);
    W32(fstring + kArrayNum, n);
    W32(fstring + kArrayMax, n);
}

}  // namespace ue
