// Whether Sam aims: the player controller's state, in UnrealScript. EPlayerController aims in the states whose names
// hold Targeting or Sniping (s_FirstPersonTargeting, s_Targeting, s_PlayerSniping, and the ones on a rope, in a split
// jump, hand over hand, at a laser mic or camera jammer) and zooms in s_Zooming (the right stick's click).
// input::Aiming() then gates the gyro's "only while aiming".
#include <string_view>

#include "core/input.h"
#include "core/log.h"
#include "scpt/scpt.h"
#include "scpt/unreal.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

constexpr uint32_t kStateFrame = 0xC;  // UObject: FStateFrame*
constexpr uint32_t kStateNode = 0x1C;  // FStateFrame: the UState it is in

uint32_t g_controller = 0;

}  // namespace

namespace scpt {

void UpdateAiming() {
    static uint32_t class_name = ~0u;
    if (class_name == ~0u) class_name = ue::FindName("EPlayerController");
    if (class_name == ~0u) return;
    if (!g_controller || !ue::ClassIs(g_controller, class_name)) {
        // the level's controller, not the class default (which has no state frame); looked for twice a second, the
        // menus have none
        static int wait = 0;
        input::SetAiming(false);
        g_controller = 0;
        if (wait-- > 0) return;
        wait = 30;
        g_controller = ue::EachObject(class_name, [](uint32_t obj) { return R32(obj + kStateFrame) != 0; });
        if (!g_controller) return;
    }
    uint32_t frame = R32(g_controller + kStateFrame);
    uint32_t state = frame >= 0x10000 && frame < 0x7FFE0000 ? R32(frame + kStateNode) : 0;
    std::string_view name = state ? ue::NameText(ue::ObjectName(state)) : std::string_view();
    bool aiming = name.find("Targeting") != std::string_view::npos || name.find("Sniping") != std::string_view::npos ||
                  name == "s_Zooming";
    if (aiming != input::Aiming()) LOGI("aiming: %s (%.*s)", aiming ? "on" : "off", int(name.size()), name.data());
    input::SetAiming(aiming);
}

}  // namespace scpt
