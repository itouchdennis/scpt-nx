// The port's settings in Pandora Tomorrow's own menus. The game builds them on Unreal Engine 2's GUI (GUIController,
// GUIPage, GUIButtonSC2), in UnrealScript: the in-mission GAME MENU is the page DemoMenuInGamePause, its buttons
// Load Game, Options, Controller and Back to Single Player Menu are subobjects its InitComponent wires to
// OnClickHandle.
//
// A fifth button, RECOMP OPTIONS, is a copy of the last one the engine makes (StaticConstructObject with it as the
// template), placed below it and added to the page's controls, so the GUI draws it and moves the focus to it as to the
// others. A on it opens an empty GUIPageSC1, the game's own page frame, whose B closes it back to the pause menu. Into
// it go copies of the pause page's title and buttons: a row is a button (the label, the focus box) and a button
// (the value) as well, slots of a list that scrolls. Their captions are localization keys (RCPL<n>, RCPV<n>) the host's
// Localize answers from the rows' text, so a change shows at once. On that page the host takes the D-pad and A: Up
// and Down move the focus (headings are skipped), Left, Right and A step the value through app::rows, which saves it.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "app/app.h"
#include "app/translations.h"
#include "core/log.h"
#include "game/console.h"
#include "kernel/memory.h"
#include "scpt/scpt.h"
#include "scpt/unreal.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

constexpr char16_t kRecompKey[] = u"RECOMPOPTIONS";  // the button's and the page title's caption
constexpr float kButtonStep = 30.0f;  // the pause page's buttons stand 30 apart

// the settings page's list: slots from kListTop, kSlotStep apart, labels left, values right (the GUI's 640 x 480)
constexpr int kSlots = 8;
constexpr float kListTop = 146.0f, kSlotStep = 25.0f;
constexpr float kLabelLeft = 70.0f, kLabelWidth = 300.0f;
constexpr float kValueLeft = 290.0f, kValueWidth = 280.0f;
constexpr size_t kTextChars = 64;

// XINPUT_GAMEPAD's D-pad bits in input::Pad::buttons, A in analog[0]
constexpr uint16_t kUp = 1, kDown = 2, kLeft = 4, kRight = 8;
constexpr int kRepeatFirstMs = 400, kRepeatMs = 110;

struct Names {
    uint32_t controller = ~0u, pause = ~0u;
} g_names;

struct Offsets {
    uint32_t active_page = ~0u, controls = 0, caption = 0, win_top = 0, win_left = 0, win_width = 0, text_align = 0,
             label_align = 0, menu_owner = 0;
} g_off;

uint32_t g_controller = 0;  // the GUIController in use
uint32_t g_open_menu = 0;   // GUIController.OpenMenu
uint32_t g_set_focus = 0;   // GUIComponent.SetFocus

// the pause page and its RECOMP OPTIONS button
uint32_t g_pause = 0, g_button = 0;

// the settings page
struct Entry {
    const app::rows::Row* row;  // a heading or a value row
};
struct Settings {
    uint32_t page = 0;  // the open GUIPageSC1, 0 when it is not
    uint32_t labels[kSlots] = {}, values[kSlots] = {};
    std::vector<Entry> list;
    int cursor = 0, first = 0;
    int focused_slot = -1;
} g_set;

// the slots' text, answered to Localize: UTF-16 buffers in guest memory rewritten in place
uint32_t g_label_text[kSlots] = {}, g_value_text[kSlots] = {};

// the D-pad and A as the host takes them on the settings page
struct Input {
    uint16_t held = 0;  // D-pad bits and 0x100 for A, at the last pad read
    uint16_t pressed = 0;  // went down (or repeated) since the last frame
    std::chrono::steady_clock::time_point repeat_at;
    bool a_down = false, a_pressed = false;  // A on the pause page
} g_in;

bool Resolve() {
    g_names.controller = ue::FindName("GUIController");
    g_names.pause = ue::FindName("DemoMenuInGamePause");
    return g_names.controller != ~0u && g_names.pause != ~0u;
}

// the GUIController that is not a class default; looked for again when it went
uint32_t Controller() {
    if (g_controller && ue::ClassIs(g_controller, g_names.controller)) return g_controller;
    g_controller = ue::EachObject(g_names.controller, [](uint32_t obj) { return R32(obj + ue::kOuter) != 0; });
    if (!g_controller) return 0;
    uint32_t cls = ue::ObjectClass(g_controller);
    g_off.active_page = ue::PropertyOffset(cls, "ActivePage");
    g_open_menu = ue::FindField(cls, ue::FindName("OpenMenu"));
    LOGI("menu: GUIController %08X, ActivePage +%X, OpenMenu %08X", g_controller, g_off.active_page, g_open_menu);
    return g_controller;
}

float F(uint32_t a) {
    float v;
    std::memcpy(&v, HostPtr(a), 4);
    return v;
}
void SetF(uint32_t a, float v) { std::memcpy(HostPtr(a), &v, 4); }

// in the game's language, in capitals as its menus' entries (the F10 panel's phrases, translations.cpp)
std::u16string Text(std::string_view text) {
    std::u16string t = app::Translate(text, [](std::string_view s) { return std::string(s); });
    for (char16_t& c : t)
        if ((c >= u'a' && c <= u'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) c = char16_t(c - 0x20);
    return t;
}

void PutText(uint32_t buffer, std::u16string_view text) {
    size_t n = std::min(text.size(), kTextChars - 1);
    std::memcpy(HostPtr(buffer), text.data(), n * 2);
    W16(buffer + uint32_t(n) * 2, 0);
}

// a control appended to the page's controls (the order the GUI draws and focuses them in), after another if given
void AddControl(X86Context& ctx, uint32_t page, uint32_t control, uint32_t after) {
    uint32_t controls = page + g_off.controls;
    uint32_t n = R32(controls + ue::kArrayNum);
    ue::Reserve(ctx, controls, 4, n + 1);
    uint32_t data = R32(controls);
    uint32_t at = n;
    for (uint32_t i = 0; after && i < n; i++)
        if (R32(data + i * 4) == after) at = i + 1;
    std::memmove(HostPtr(data + (at + 1) * 4), HostPtr(data + at * 4), (n - at) * 4);
    W32(data + at * 4, control);
    W32(controls + ue::kArrayNum, n + 1);
}

// a copy of a control for a page, its caption a localization key
uint32_t Copy(X86Context& ctx, uint32_t templ, uint32_t page, std::u16string_view key) {
    uint32_t c = ue::Construct(ctx, ue::ObjectClass(templ), R32(templ + ue::kOuter), templ);
    if (!c) return 0;
    uint32_t caption = ue::PropertyOffset(ue::ObjectClass(c), "Caption");
    if (caption != ~0u) ue::SetString(ctx, c + caption, key);
    if (g_off.menu_owner != ~0u) W32(c + g_off.menu_owner, page);
    return c;
}

void AddButton(X86Context& ctx, uint32_t page) {
    uint32_t cls = ue::ObjectClass(page);
    g_off.controls = ue::PropertyOffset(cls, "Controls");
    uint32_t exit_off = ue::PropertyOffset(cls, "btnExit");
    if (g_off.controls == ~0u || exit_off == ~0u) return;
    uint32_t exit = R32(page + exit_off);
    if (!exit) return;
    uint32_t bcls = ue::ObjectClass(exit);
    g_off.caption = ue::PropertyOffset(bcls, "Caption");
    g_off.win_top = ue::PropertyOffset(bcls, "WinTop");
    g_off.win_left = ue::PropertyOffset(bcls, "WinLeft");
    g_off.win_width = ue::PropertyOffset(bcls, "WinWidth");
    g_off.text_align = ue::PropertyOffset(bcls, "TextAlign");
    g_off.menu_owner = ue::PropertyOffset(bcls, "MenuOwner");
    g_set_focus = ue::FindField(bcls, ue::FindName("SetFocus"));
    uint32_t button = Copy(ctx, exit, page, kRecompKey);
    if (!button) {
        LOGI("menu: could not make the button");
        return;
    }
    SetF(button + g_off.win_top, F(exit + g_off.win_top) + kButtonStep);
    AddControl(ctx, page, button, exit);
    g_pause = page;
    g_button = button;
    LOGI("menu: RECOMP OPTIONS %08X on the pause page %08X", button, page);
}

// GUIController.OpenMenu(NewMenuName): the page it opened, 0 if none
uint32_t OpenMenu(X86Context& ctx, std::u16string_view name) {
    if (!g_open_menu) return 0;
    static uint32_t parms = 0, text = 0;
    if (!parms) parms = mem::AllocSystem(0x80);
    if (!text) text = mem::AllocSystem(64 * 2);
    std::memset(HostPtr(parms), 0, 0x80);
    size_t n = std::min<size_t>(name.size(), 63);
    std::memcpy(HostPtr(text), name.data(), n * 2);
    W16(text + uint32_t(n) * 2, 0);
    W32(parms, text);
    W32(parms + ue::kArrayNum, uint32_t(n + 1));
    W32(parms + ue::kArrayMax, uint32_t(n + 1));
    ue::ProcessEvent(ctx, g_controller, g_open_menu, parms);
    return R32(g_controller + g_off.active_page);
}

void SetFocus(X86Context& ctx, uint32_t control) {
    if (!g_set_focus || !control) return;
    static uint32_t parms = 0;
    if (!parms) parms = mem::AllocSystem(16);
    std::memset(HostPtr(parms), 0, 16);
    ue::ProcessEvent(ctx, control, g_set_focus, parms);
}

bool IsHeading(int i) { return g_set.list[size_t(i)].row->kind == app::rows::Kind::Heading; }

// the rows the page lists: the settings and the controller's value rows the port shows, each heading with a row
std::vector<Entry> List() {
    std::vector<Entry> out;
    for (int page : {0, 2}) {
        size_t count = 0;
        const app::rows::Row* rows = app::rows::Page(page, &count);
        const app::rows::Row* heading = nullptr;
        for (size_t i = 0; i < count; i++) {
            const app::rows::Row& r = rows[i];
            if (r.kind == app::rows::Kind::Heading) {
                heading = &r;
            } else if (r.kind == app::rows::Kind::Value && app::rows::Shown(r)) {
                if (heading) out.push_back({heading});
                heading = nullptr;
                out.push_back({&r});
            }
        }
    }
    return out;
}

// the slots' text from the list, the focus on the cursor's slot
void Show(X86Context& ctx) {
    int n = int(g_set.list.size());
    g_set.first = std::clamp(g_set.first, 0, std::max(0, n - kSlots));
    if (g_set.cursor < g_set.first) g_set.first = g_set.cursor;
    if (g_set.cursor >= g_set.first + kSlots) g_set.first = g_set.cursor - kSlots + 1;
    // a heading right above the cursor stays in sight
    if (g_set.cursor == g_set.first + 1 && g_set.first > 0 && IsHeading(g_set.first - 1)) g_set.first--;
    for (int s = 0; s < kSlots; s++) {
        int i = g_set.first + s;
        std::u16string label, value;
        if (i < n) {
            const app::rows::Row& r = *g_set.list[size_t(i)].row;
            if (r.kind == app::rows::Kind::Heading) {
                label = u"- " + Text(r.label) + u" -";
            } else {
                label = Text(r.label);
                value = app::rows::Enabled(r) ? Text(app::rows::Value(r)) : u"-";
            }
        }
        PutText(g_label_text[s], label);
        PutText(g_value_text[s], value);
    }
    int slot = g_set.cursor - g_set.first;
    if (slot != g_set.focused_slot) {
        g_set.focused_slot = slot;
        SetFocus(ctx, g_set.labels[slot]);
    }
}

void BuildSettings(X86Context& ctx, uint32_t page) {
    g_set = {};
    g_set.page = page;
    g_set.list = List();
    if (g_set.list.empty() || !g_pause) return;
    uint32_t pcls = ue::ObjectClass(g_pause);
    uint32_t title = R32(R32(g_pause + g_off.controls));  // the pause page's GAME MENU title
    auto member = [&](const char* name) {
        uint32_t off = ue::PropertyOffset(pcls, name);
        return off == ~0u ? 0u : R32(g_pause + off);
    };
    uint32_t exit = member("btnExit");
    if (!ue::Valid(page) || !ue::Valid(title) || !ue::Valid(exit)) return;
    uint32_t page_controls = ue::PropertyOffset(ue::ObjectClass(page), "Controls");
    if (page_controls != g_off.controls) return;  // both GUIMultiComponent's
    if (uint32_t t = Copy(ctx, title, page, kRecompKey)) AddControl(ctx, page, t, 0);
    for (int s = 0; s < kSlots; s++) {
        if (!g_label_text[s]) {
            g_label_text[s] = mem::AllocSystem(kTextChars * 2);
            g_value_text[s] = mem::AllocSystem(kTextChars * 2);
        }
        std::u16string lk = u"RCPL" + std::u16string(1, char16_t(u'0' + s));
        std::u16string vk = u"RCPV" + std::u16string(1, char16_t(u'0' + s));
        // the value a button too, as the label's font, never focused (the host moves the focus)
        uint32_t label = Copy(ctx, exit, page, lk), value = Copy(ctx, exit, page, vk);
        if (!label || !value) return;
        float top = kListTop + float(s) * kSlotStep;
        SetF(label + g_off.win_top, top);
        SetF(label + g_off.win_left, kLabelLeft);
        SetF(label + g_off.win_width, kLabelWidth);
        if (g_off.text_align != ~0u) *static_cast<uint8_t*>(HostPtr(label + g_off.text_align)) = 0;  // left
        SetF(value + g_off.win_top, top);
        SetF(value + g_off.win_left, kValueLeft);
        SetF(value + g_off.win_width, kValueWidth);
        if (g_off.text_align != ~0u) *static_cast<uint8_t*>(HostPtr(value + g_off.text_align)) = 2;  // right
        AddControl(ctx, page, label, 0);
        AddControl(ctx, page, value, 0);
        g_set.labels[s] = label;
        g_set.values[s] = value;
    }
    while (g_set.cursor < int(g_set.list.size()) - 1 && IsHeading(g_set.cursor)) g_set.cursor++;
    Show(ctx);
    LOGI("menu: settings page %08X, %zu rows", page, g_set.list.size());
}

void MoveCursor(int step) {
    int n = int(g_set.list.size()), c = g_set.cursor;
    do {
        c += step;
    } while (c >= 0 && c < n && IsHeading(c));
    if (c >= 0 && c < n) g_set.cursor = c;
}

void SettingsInput(X86Context& ctx, uint16_t pressed) {
    if (pressed & kUp) MoveCursor(-1);
    if (pressed & kDown) MoveCursor(1);
    const app::rows::Row& r = *g_set.list[size_t(g_set.cursor)].row;
    int step = (pressed & kRight) || (pressed & 0x100) ? 1 : (pressed & kLeft) ? -1 : 0;
    if (step && r.kind == app::rows::Kind::Value && app::rows::Enabled(r)) app::rows::Step(r, step);
    Show(ctx);
}

}  // namespace

namespace game {

void FrameTick(X86Context& ctx) {
    scpt::UpdateAiming();
    bool a_pressed = std::exchange(g_in.a_pressed, false);
    uint16_t pressed = std::exchange(g_in.pressed, 0);
    if (!Resolve()) return;
    uint32_t ctrl = Controller();
    if (!ctrl || g_off.active_page == ~0u) return;
    uint32_t page = R32(ctrl + g_off.active_page);
    if (g_set.page && page != g_set.page) g_set.page = 0;  // closed
    if (g_set.page) {
        if (pressed) SettingsInput(ctx, pressed);
        else Show(ctx);  // values another way changed (the console, the F10 panel)
        return;
    }
    if (!page || !ue::ClassIs(page, g_names.pause)) return;
    if (page != g_pause) AddButton(ctx, page);
    if (page != g_pause) return;
    uint32_t focused = ue::PropertyOffset(ue::ObjectClass(page), "FocusedControl");
    if (a_pressed && focused != ~0u && R32(page + focused) == g_button) {
        uint32_t opened = OpenMenu(ctx, u"GUI.GUIPageSC1");
        if (opened && opened != page) BuildSettings(ctx, opened);
    }
}

void MenuPad(int port, input::Pad& pad) {
    if (port != 0) return;
    bool a = pad.analog[0] > 0x20;
    if (!g_set.page) {
        if (a && !g_in.a_down) g_in.a_pressed = true;
        g_in.a_down = a;
        g_in.held = 0;
        return;
    }
    // the settings page: the D-pad (and the left stick as one) and A are the host's
    uint16_t held = pad.buttons & (kUp | kDown | kLeft | kRight);
    constexpr int16_t kStick = 16000;
    if (pad.ly > kStick) held |= kUp;
    if (pad.ly < -kStick) held |= kDown;
    if (pad.lx < -kStick) held |= kLeft;
    if (pad.lx > kStick) held |= kRight;
    if (a) held |= 0x100;
    auto now = std::chrono::steady_clock::now();
    uint16_t down = held & ~g_in.held;
    if (down) {
        g_in.pressed |= down;
        g_in.repeat_at = now + std::chrono::milliseconds(kRepeatFirstMs);
    } else if ((held & 0xFF) && now >= g_in.repeat_at) {
        g_in.pressed |= held & 0xFF;  // the D-pad repeats, A does not
        g_in.repeat_at = now + std::chrono::milliseconds(kRepeatMs);
    }
    g_in.held = held;
    pad.buttons &= uint16_t(~(kUp | kDown | kLeft | kRight));
    pad.analog[0] = 0;
    pad.lx = pad.ly = 0;
    g_in.a_down = a;
}

}  // namespace game

// Localize(Section, Key, Package, LangExt, Optional), cdecl: the port's keys read its text
X86_EXTERN_FUNC(GUEST_ORIG(Engine_Localize));
X86_FUNC(GUEST(Engine_Localize)) {
    uint32_t key = R32(ctx.esp + 8);
    if (key && R16(key) == u'R') {
        static uint32_t recomp = 0;
        if (std::memcmp(HostPtr(key), kRecompKey, sizeof(kRecompKey)) == 0) {
            if (!recomp) {
                recomp = mem::AllocSystem(kTextChars * 2);
                PutText(recomp, Text("RECOMP OPTIONS"));
            }
            kernel::Return(ctx, recomp, 0);
            return;
        }
        // RCPL<n> and RCPV<n>: the settings page's slots
        if (R16(key + 2) == u'C' && R16(key + 4) == u'P' && R16(key + 10) == 0) {
            int s = int(R16(key + 8)) - u'0';
            uint16_t kind = R16(key + 6);
            if (s >= 0 && s < kSlots && (kind == u'L' || kind == u'V')) {
                uint32_t text = kind == u'L' ? g_label_text[s] : g_value_text[s];
                if (text) {
                    kernel::Return(ctx, text, 0);
                    return;
                }
            }
        }
    }
    GUEST_ORIG(Engine_Localize)(ctx, base);
}
