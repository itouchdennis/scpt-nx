// The port's settings in Pandora Tomorrow's own menus. The game builds them on Unreal Engine 2's GUI (GUIController,
// GUIPage, GUIButtonSC2), in UnrealScript: the in-mission GAME MENU is the page DemoMenuInGamePause, its buttons
// Load Game, Options, Controller and Back to Single Player Menu are subobjects its InitComponent wires to
// OnClickHandle. A fifth button, RECOMP OPTIONS, is a copy of the last one the engine makes (StaticConstructObject
// with it as the template), placed below it and added to the page's controls, so the GUI draws it and moves the focus
// to it as to the others. A on it opens the settings page.
#include <cstring>
#include <string>
#include <utility>

#include "core/log.h"
#include "game/console.h"
#include "kernel/memory.h"
#include "scpt/unreal.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

constexpr char16_t kRecompKey[] = u"RECOMPOPTIONS";  // the button's caption, a localization key as the game's
constexpr char16_t kRecompText[] = u"RECOMP OPTIONS";
constexpr float kRowStep = 30.0f;  // the pause page's buttons stand 30 apart

struct Names {
    uint32_t controller = ~0u, pause = ~0u;
} g_names;

struct Offsets {
    uint32_t active_page = 0, controls = 0, focused = 0, caption = 0, win_top = 0;
    bool ok = false;
} g_off;

uint32_t g_controller = 0;   // the GUIController in use
uint32_t g_page = 0;         // the pause page the button was added to
uint32_t g_button = 0;       // RECOMP OPTIONS on it
uint32_t g_open_menu = 0;    // GUIController.OpenMenu
bool g_a_down = false;       // A held at the last pad read
bool g_a_pressed = false;    // A went down since the last frame

bool Resolve() {
    if (g_off.ok) return true;
    g_names.controller = ue::FindName("GUIController");
    g_names.pause = ue::FindName("DemoMenuInGamePause");
    if (g_names.controller == ~0u || g_names.pause == ~0u) return false;
    return true;
}

// the GUIController that has a page open, or the transient one; looked for again when it went
uint32_t Controller() {
    if (g_controller && ue::ClassIs(g_controller, g_names.controller)) return g_controller;
    g_controller = ue::EachObject(g_names.controller, [](uint32_t obj) { return (R32(obj + ue::kOuter) != 0); });
    if (!g_controller) return 0;
    uint32_t cls = ue::ObjectClass(g_controller);
    g_off.active_page = ue::PropertyOffset(cls, "ActivePage");
    g_open_menu = ue::FindField(cls, ue::FindName("OpenMenu"));
    LOGI("menu: GUIController %08X, ActivePage +%X, OpenMenu %08X", g_controller, g_off.active_page, g_open_menu);
    return g_controller;
}

void AddButton(X86Context& ctx, uint32_t page) {
    uint32_t cls = ue::ObjectClass(page);
    g_off.controls = ue::PropertyOffset(cls, "Controls");
    g_off.focused = ue::PropertyOffset(cls, "FocusedControl");
    uint32_t exit_off = ue::PropertyOffset(cls, "btnExit");
    if (g_off.controls == ~0u || exit_off == ~0u) return;
    uint32_t exit = R32(page + exit_off);
    if (!exit) return;
    uint32_t bcls = ue::ObjectClass(exit);
    g_off.caption = ue::PropertyOffset(bcls, "Caption");
    g_off.win_top = ue::PropertyOffset(bcls, "WinTop");
    uint32_t button = ue::Construct(ctx, bcls, R32(exit + ue::kOuter), exit);
    if (!button) {
        LOGI("menu: could not make the button");
        return;
    }
    ue::SetString(ctx, button + g_off.caption, kRecompKey);
    float top;
    std::memcpy(&top, HostPtr(exit + g_off.win_top), 4);
    top += kRowStep;
    std::memcpy(HostPtr(button + g_off.win_top), &top, 4);
    // after Back to Single Player Menu in the controls, the order the focus moves in
    uint32_t controls = page + g_off.controls;
    uint32_t n = R32(controls + ue::kArrayNum);
    ue::Reserve(ctx, controls, 4, n + 1);
    uint32_t data = R32(controls);
    uint32_t at = n;
    for (uint32_t i = 0; i < n; i++)
        if (R32(data + i * 4) == exit) at = i + 1;
    std::memmove(HostPtr(data + (at + 1) * 4), HostPtr(data + at * 4), (n - at) * 4);
    W32(data + at * 4, button);
    W32(controls + ue::kArrayNum, n + 1);
    g_page = page;
    g_button = button;
    LOGI("menu: RECOMP OPTIONS %08X on the pause page %08X", button, page);
}

// GUIController.OpenMenu(NewMenuName, Param1, Param2)
void OpenMenu(X86Context& ctx, std::u16string_view name) {
    if (!g_open_menu) return;
    static uint32_t parms = 0;
    uint32_t size = R32(g_open_menu + ue::kParms) & 0xFFFF;
    if (!parms) parms = mem::AllocSystem(0x80);
    std::memset(HostPtr(parms), 0, 0x80);
    static std::u16string last;
    static uint32_t text = 0;
    if (last != name) {
        last = name;
        text = mem::AllocSystem(uint32_t(name.size() + 1) * 2);
        std::memcpy(HostPtr(text), name.data(), name.size() * 2);
        W16(text + uint32_t(name.size()) * 2, 0);
    }
    W32(parms, text);
    W32(parms + ue::kArrayNum, uint32_t(name.size() + 1));
    W32(parms + ue::kArrayMax, uint32_t(name.size() + 1));
    (void)size;
    ue::ProcessEvent(ctx, g_controller, g_open_menu, parms);
}

}  // namespace

namespace game {

void FrameTick(X86Context& ctx) {
    bool pressed = std::exchange(g_a_pressed, false);
    if (!Resolve()) return;
    uint32_t ctrl = Controller();
    if (!ctrl || g_off.active_page == ~0u) return;
    uint32_t page = R32(ctrl + g_off.active_page);
    if (!page || !ue::ClassIs(page, g_names.pause)) return;
    if (page != g_page) AddButton(ctx, page);
    if (page != g_page) return;
    if (pressed && R32(page + g_off.focused) == g_button) {
        LOGI("menu: RECOMP OPTIONS");
        OpenMenu(ctx, u"GUI.MenuOptions");
    }
}

void MenuPad(int port, input::Pad& pad) {
    if (port != 0) return;
    bool down = pad.analog[0] > 0x20;
    if (down && !g_a_down) g_a_pressed = true;
    g_a_down = down;
}

}  // namespace game

// the caption's key reads RECOMP OPTIONS; Localize(Section, Key, Package, LangExt, Optional), cdecl
X86_EXTERN_FUNC(GUEST_ORIG(Engine_Localize));
X86_FUNC(GUEST(Engine_Localize)) {
    uint32_t key = R32(ctx.esp + 8);
    static uint32_t text = 0;
    if (key && std::memcmp(HostPtr(key), kRecompKey, sizeof(kRecompKey)) == 0) {
        if (!text) {
            text = mem::AllocSystem(sizeof(kRecompText));
            std::memcpy(HostPtr(text), kRecompText, sizeof(kRecompText));
        }
        kernel::Return(ctx, text, 0);
        return;
    }
    GUEST_ORIG(Engine_Localize)(ctx, base);
}
