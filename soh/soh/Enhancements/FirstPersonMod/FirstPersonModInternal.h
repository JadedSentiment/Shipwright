#pragma once

// Shared between the First Person Mod gameplay code and its menu.

#include <imgui.h>

#define FPS_CVAR(name) "gFirstPerson." name

namespace FirstPersonMod {

enum Action {
    ACT_FORWARD,
    ACT_BACK,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_INTERACT,
    ACT_ROLL,
    ACT_ATTACK,
    ACT_SHIELD,
    ACT_FOCUS,
    ACT_CROUCH,
    ACT_NAVI,
    ACT_SWORD,
    ACT_LAST_ITEM,
    ACT_WALK,
    ACT_PAUSE,
    ACT_SLOT_1,
    ACT_SLOT_2,
    ACT_SLOT_3,
    ACT_SLOT_4,
    ACT_SLOT_5,
    ACT_SLOT_6,
    ACT_SLOT_7,
    ACT_NEXT_ITEM,
    ACT_PREV_ITEM,
    ACT_MENU_BACK,
    ACT_COUNT,
};

struct ActionInfo {
    const char* id;      // CVar suffix
    const char* label;   // shown in the menu
    ImGuiKey defaultKey; // ImGuiKey_None means "unbound"
};

extern const ActionInfo kActions[ACT_COUNT];

ImGuiKey GetBinding(Action action);
void SetBinding(Action action, ImGuiKey key);
void ResetBindings();
const char* KeyName(ImGuiKey key);

void ApplyComfortPreset();
void ApplyHeavyPreset();

// Called when the menu toggles the mod so mouse capture follows immediately.
void OnEnabledChanged();

} // namespace FirstPersonMod
