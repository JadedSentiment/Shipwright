// First Person Mod: settings menu (Enhancements > First Person) and key bindings.

#include "FirstPersonModInternal.h"

#include <memory>
#include <string>
#include <vector>

#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>

#include "soh/SohGui/MenuTypes.h"
#include "soh/SohGui/SohMenu.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/ShipInit.hpp"

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
}

namespace FirstPersonMod {

const ActionInfo kActions[ACT_COUNT] = {
    { "Forward", "Move forward", ImGuiKey_W },
    { "Back", "Move back", ImGuiKey_S },
    { "Left", "Strafe left", ImGuiKey_A },
    { "Right", "Strafe right", ImGuiKey_D },
    { "Interact", "Interact (talk, open, climb, grab)", ImGuiKey_E },
    { "Roll", "Roll / sidehop / backflip", ImGuiKey_Space },
    { "Attack", "Sword / use readied item", ImGuiKey_MouseLeft },
    { "Shield", "Shield", ImGuiKey_MouseRight },
    { "Focus", "Focus (hold to lock on)", ImGuiKey_LeftShift },
    { "Crouch", "Crouch behind shield", ImGuiKey_LeftCtrl },
    { "Navi", "Talk to Navi", ImGuiKey_Q },
    { "Sword", "Draw / sheathe sword", ImGuiKey_R },
    { "LastItem", "Swap to last item", ImGuiKey_F },
    { "Walk", "Walk (hold)", ImGuiKey_LeftAlt },
    { "Pause", "Pause menu", ImGuiKey_Tab },
    { "Slot1", "Ready C-Left item", ImGuiKey_1 },
    { "Slot2", "Ready C-Down item", ImGuiKey_2 },
    { "Slot3", "Ready C-Right item", ImGuiKey_3 },
    { "Slot4", "Ready D-Pad Up item", ImGuiKey_4 },
    { "Slot5", "Ready D-Pad Down item", ImGuiKey_5 },
    { "Slot6", "Ready D-Pad Left item", ImGuiKey_6 },
    { "Slot7", "Ready D-Pad Right item", ImGuiKey_7 },
    { "NextItem", "Next item (also mouse wheel)", ImGuiKey_None },
    { "PrevItem", "Previous item (also mouse wheel)", ImGuiKey_None },
    { "MenuBack", "Back / cancel in menus", ImGuiKey_Backspace },
};

static std::string BindingCVar(Action action) {
    return std::string(FPS_CVAR("Keys.")) + kActions[action].id;
}

ImGuiKey GetBinding(Action action) {
    static std::string names[ACT_COUNT];
    if (names[action].empty()) {
        names[action] = BindingCVar(action);
    }
    return (ImGuiKey)CVarGetInteger(names[action].c_str(), kActions[action].defaultKey);
}

void SetBinding(Action action, ImGuiKey key) {
    CVarSetInteger(BindingCVar(action).c_str(), (int32_t)key);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void ResetBindings() {
    for (int i = 0; i < ACT_COUNT; i++) {
        CVarClear(BindingCVar((Action)i).c_str());
    }
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

const char* KeyName(ImGuiKey key) {
    if (key == ImGuiKey_None) {
        return "(unbound)";
    }
    switch (key) {
        case ImGuiKey_MouseLeft:
            return "Left Click";
        case ImGuiKey_MouseRight:
            return "Right Click";
        case ImGuiKey_MouseMiddle:
            return "Middle Click";
        case ImGuiKey_MouseX1:
            return "Mouse 4";
        case ImGuiKey_MouseX2:
            return "Mouse 5";
        default:
            return ImGui::GetKeyName(key);
    }
}

static void SetCameraMotion(float bob, float sway, float landing, float rollSpin, float hitShake) {
    CVarSetFloat(FPS_CVAR("Bob"), bob);
    CVarSetFloat(FPS_CVAR("Sway"), sway);
    CVarSetFloat(FPS_CVAR("Landing"), landing);
    CVarSetFloat(FPS_CVAR("RollSpin"), rollSpin);
    CVarSetFloat(FPS_CVAR("HitShake"), hitShake);
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void ApplyComfortPreset() {
    SetCameraMotion(0.25f, 0.25f, 0.3f, 0.0f, 0.3f);
}

void ApplyHeavyPreset() {
    SetCameraMotion(1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
}

} // namespace FirstPersonMod

using namespace FirstPersonMod;
using namespace UIWidgets;

namespace {

int sRebinding = -1;       // action waiting for a key, or -1
bool sRebindArmed = false; // wait for every key/button to be released first, so the click itself isn't captured

bool AnyKeyDown() {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        ImGuiKey key = (ImGuiKey)k;
        if (key >= ImGuiKey_MouseWheelX && key <= ImGuiKey_MouseWheelY) {
            continue;
        }
        if (ImGui::IsKeyDown(key)) {
            return true;
        }
    }
    return false;
}

void PollRebind() {
    if (sRebinding < 0) {
        return;
    }
    if (!sRebindArmed) {
        sRebindArmed = !AnyKeyDown();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        sRebinding = -1;
        return;
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        ImGuiKey key = (ImGuiKey)k;
        // Skip analog / alias keys that aren't real buttons
        if ((key >= ImGuiKey_MouseWheelX && key <= ImGuiKey_MouseWheelY) ||
            (key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadRStickDown) || key == ImGuiKey_ReservedForModCtrl ||
            key == ImGuiKey_ReservedForModShift || key == ImGuiKey_ReservedForModAlt ||
            key == ImGuiKey_ReservedForModSuper) {
            continue;
        }
        if (ImGui::IsKeyPressed(key, false)) {
            SetBinding((Action)sRebinding, key);
            sRebinding = -1;
            return;
        }
    }
}

void KeyBindingsWidget(WidgetInfo& info) {
    PollRebind();

    ImGui::TextWrapped("Click a key to change it, then press the new key or mouse button. Esc cancels.");
    ImGui::Spacing();

    if (ImGui::BeginTable("FirstPersonKeys", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 0.62f);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.38f);
        for (int i = 0; i < ACT_COUNT; i++) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kActions[i].label);
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(i);
            std::string label = (sRebinding == i) ? std::string("Press a key...") : KeyName(GetBinding((Action)i));
            if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) {
                sRebinding = i;
                sRebindArmed = false;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                SetBinding((Action)i, ImGuiKey_None);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Right-click to unbind");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (UIWidgets::Button("Reset Keys to Defaults", ButtonOptions().Size(ImVec2(-FLT_MIN, 0)))) {
        ResetBindings();
        sRebinding = -1;
    }
}

void AddAgeSliders(std::shared_ptr<SohGui::SohMenu> menu, WidgetPath& path, const char* title, const char* cvarAge,
                   const char* tag) {
    // CVar names have to outlive the menu, so they're kept in static storage
    static std::vector<std::unique_ptr<std::string>> sNames;
    auto cvar = [&](const char* key) {
        sNames.push_back(std::make_unique<std::string>(std::string(FPS_CVAR("")) + cvarAge + "." + key));
        return sNames.back()->c_str();
    };
    auto label = [&](const char* text) { return std::string(text) + "##" + tag; };

    menu->AddWidget(path, title, WIDGET_SEPARATOR_TEXT);
    menu->AddWidget(path, label("Camera Height: %.1f"), WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(cvar("CamHeight"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(-30.0f).Max(30.0f).DefaultValue(0.0f).Step(0.5f).Format("%.1f").Tooltip(
            "Raises or lowers your eyes from Link's head height."));
    menu->AddWidget(path, label("Camera Forward: %.1f"), WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(cvar("CamForward"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(-20.0f).Max(20.0f).DefaultValue(0.0f).Step(0.5f).Format("%.1f").Tooltip(
            "Moves your eyes forward or back from Link's head."));
    menu->AddWidget(path, label("Arm Raise: %.0f deg"), WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(cvar("MountRaise"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(-30.0f).Max(90.0f).DefaultValue(30.0f).Step(1.0f).Format("%.0f deg").Tooltip(
            "Tilts the arms forward and up into view."));
    menu->AddWidget(path, label("Arm Forward: %.1f"), WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(cvar("MountForward"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(-20.0f).Max(40.0f).DefaultValue(8.0f).Step(0.5f).Format("%.1f").Tooltip(
            "Moves the arms away from or closer to the camera."));
    menu->AddWidget(path, label("Arm Height: %.1f"), WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(cvar("MountHeight"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(-30.0f).Max(30.0f).DefaultValue(2.0f).Step(0.5f).Format("%.1f").Tooltip(
            "Moves the arms up or down on screen."));
    menu->AddWidget(path, label("Arm Side: %.1f"), WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(cvar("MountSide"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(-15.0f).Max(15.0f).DefaultValue(0.0f).Step(0.5f).Format("%.1f").Tooltip(
            "Shifts the arms left or right."));
    menu->AddWidget(path, label("Arms Follow Look: %.0f%%"), WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(cvar("MountPitchFollow"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.85f).IsPercentage().Format("%.0f%%").Tooltip(
            "How much the arms tilt with you when you look up or down."));
}

void RegisterFirstPersonMenu() {
    auto menu = SohGui::mSohMenu;
    menu->AddSidebarEntry("Enhancements", "First Person", 3);
    WidgetPath path = { "Enhancements", "First Person", SECTION_COLUMN_1 };

    // Column 1: general
    menu->AddWidget(path, "First Person", WIDGET_SEPARATOR_TEXT);
    menu->AddWidget(path, "Enable First Person Mode", WIDGET_CVAR_CHECKBOX)
        .CVar(FPS_CVAR("Enabled"))
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) { OnEnabledChanged(); })
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Play from Link's eyes with mouse and keyboard. Keyboard and mouse replace the controller while this is "
            "on. Press Esc to open this menu and free the mouse."));
    menu->AddWidget(path, "Mouse Sensitivity: %.2f", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(FPS_CVAR("Sensitivity"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(0.1f).Max(10.0f).DefaultValue(2.5f).Step(0.05f).Format("%.2f"));
    menu->AddWidget(path, "Invert Mouse Y", WIDGET_CVAR_CHECKBOX).CVar(FPS_CVAR("InvertY")).RaceDisable(false);
    menu->AddWidget(path, "Field of View: %.0f", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(FPS_CVAR("Fov"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(50.0f).Max(110.0f).DefaultValue(75.0f).Step(1.0f).Format("%.0f").Tooltip(
            "Vertical field of view in degrees. The original game uses 60."));
    menu->AddWidget(path, "Show Crosshair", WIDGET_CVAR_CHECKBOX)
        .CVar(FPS_CVAR("Crosshair"))
        .RaceDisable(false)
        .Options(CheckboxOptions().DefaultValue(true).Tooltip(
            "Turns red over enemies and green over people and things you can interact with."));
    menu->AddWidget(path, "Aim Assist: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(FPS_CVAR("AimAssist"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.0f).IsPercentage().Format("%.0f%%").Tooltip(
            "Slows the crosshair down while it's over an enemy."));
    menu->AddWidget(path, "Body", WIDGET_CVAR_COMBOBOX)
        .CVar(FPS_CVAR("BodyMode"))
        .RaceDisable(false)
        .Options(ComboboxOptions()
                     .ComboMap({ { 0, "Arms only" }, { 1, "Arms + legs (experimental)" } })
                     .DefaultIndex(0)
                     .Tooltip("What you can see of Link from his eyes. Other players always see all of him."));

    // Column 2: camera feel and viewmodel
    path.column = SECTION_COLUMN_2;
    menu->AddWidget(path, "Camera Motion", WIDGET_SEPARATOR_TEXT);
    menu->AddWidget(path, "Comfort Preset", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) { ApplyComfortPreset(); })
        .Options(ButtonOptions().Tooltip("Gentle motion, easy on long sessions."));
    menu->AddWidget(path, "Heavy Preset", WIDGET_BUTTON)
        .RaceDisable(false)
        .Callback([](WidgetInfo& info) { ApplyHeavyPreset(); })
        .Options(ButtonOptions().Tooltip("Everything maxed: strong bob, sway, landing jolts and a full roll tumble."));
    menu->AddWidget(path, "Head Bob: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(FPS_CVAR("Bob"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.25f).IsPercentage().Format("%.0f%%"));
    menu->AddWidget(path, "Sway & Strafe Lean: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(FPS_CVAR("Sway"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.25f).IsPercentage().Format("%.0f%%"));
    menu->AddWidget(path, "Landing Impact: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(FPS_CVAR("Landing"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.3f).IsPercentage().Format("%.0f%%"));
    menu->AddWidget(path, "Roll Tumble: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(FPS_CVAR("RollSpin"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.0f).IsPercentage().Format("%.0f%%").Tooltip(
            "How far the view turns over during a roll. 100% is a full somersault."));
    menu->AddWidget(path, "Hit Shake: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(FPS_CVAR("HitShake"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.3f).IsPercentage().Format("%.0f%%"));

    menu->AddWidget(path, "Mask Vignette: %.0f%%", WIDGET_CVAR_SLIDER_FLOAT)
        .CVar(FPS_CVAR("MaskVignette"))
        .RaceDisable(false)
        .Options(FloatSliderOptions().Min(0.0f).Max(1.0f).DefaultValue(0.85f).IsPercentage().Format("%.0f%%").Tooltip(
            "How dark the edges of the screen get while you're wearing a mask."));

    // Body proportions: one set for each age
    AddAgeSliders(menu, path, "Young Link", "Child", "Child");
    AddAgeSliders(menu, path, "Adult Link", "Adult", "Adult");

    // Column 3: keys
    path.column = SECTION_COLUMN_3;
    menu->AddWidget(path, "Keys", WIDGET_SEPARATOR_TEXT);
    menu->AddWidget(path, "FirstPersonKeyBindings", WIDGET_CUSTOM)
        .RaceDisable(false)
        .HideInSearch(true)
        .CustomFunction(KeyBindingsWidget);
}

static RegisterMenuInitFunc sMenuInit(RegisterFirstPersonMenu);

} // namespace
