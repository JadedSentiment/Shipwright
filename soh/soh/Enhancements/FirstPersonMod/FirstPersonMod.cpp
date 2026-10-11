// First Person Mod: input, camera, targeting, items and viewmodel.
// See FirstPersonMod.h for the overview.

#include "FirstPersonMod.h"
#include "FirstPersonModInternal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/GuiWindow.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "z64player.h"
#include "z64camera.h"
extern PlayState* gPlayState;
void Mouse_GetDelta(s32* x, s32* y);
s8 Player_ItemToItemAction(s32 item);
s32 Player_GetItemOnButton(PlayState* play, s32 index);
void Player_UseItem(PlayState* play, Player* player, s32 item);
void func_8083DF68(Player* player, f32 arg1, s16 arg2);
void func_8084029C(Player* player, f32 arg1);
s32 Player_GetMovementSpeedAndYaw(Player* player, f32* outSpeedTarget, s16* outYawTarget, f32 speedMode,
                                  PlayState* play);
}

using namespace FirstPersonMod;

void EnsureOverlay();

// ---------------------------------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------------------------------

#define CVAR_ENABLED FPS_CVAR("Enabled")

namespace {

bool CfgEnabled() {
    return CVarGetInteger(CVAR_ENABLED, 1) != 0;
}
float CfgSensitivity() {
    return CVarGetFloat(FPS_CVAR("Sensitivity"), 2.5f);
}
bool CfgInvertY() {
    return CVarGetInteger(FPS_CVAR("InvertY"), 0) != 0;
}
float CfgFov() {
    return CVarGetFloat(FPS_CVAR("Fov"), 75.0f);
}
float CfgBob() {
    return CVarGetFloat(FPS_CVAR("Bob"), 0.25f);
}
float CfgSway() {
    return CVarGetFloat(FPS_CVAR("Sway"), 0.25f);
}
float CfgLanding() {
    return CVarGetFloat(FPS_CVAR("Landing"), 0.3f);
}
float CfgRollSpin() {
    return CVarGetFloat(FPS_CVAR("RollSpin"), 0.0f);
}
float CfgHitShake() {
    return CVarGetFloat(FPS_CVAR("HitShake"), 0.3f);
}
float CfgAimAssist() {
    return CVarGetFloat(FPS_CVAR("AimAssist"), 0.0f);
}
bool CfgCrosshair() {
    return CVarGetInteger(FPS_CVAR("Crosshair"), 1) != 0;
}
int CfgBodyMode() {
    return CVarGetInteger(FPS_CVAR("BodyMode"), 0);
}
// Child and adult Link have their own copy of the body-proportion settings ("gFirstPerson.Child.*" / ".Adult.*").
// Older single values under "gFirstPerson.Viewmodel.*" are used as the starting point when an age has none yet.
float AgeCfg(const char* key, const char* legacyKey, float def) {
    std::string name = std::string(LINK_IS_ADULT ? FPS_CVAR("Adult.") : FPS_CVAR("Child.")) + key;
    float fallback = (legacyKey != nullptr) ? CVarGetFloat(legacyKey, def) : def;
    return CVarGetFloat(name.c_str(), fallback);
}
float CfgVmRaise() {
    return AgeCfg("ArmRaise", FPS_CVAR("Viewmodel.Raise"), 12.0f);
}
float CfgVmForward() {
    return AgeCfg("ArmForward", FPS_CVAR("Viewmodel.Forward"), 4.0f);
}
float CfgVmHeight() {
    return AgeCfg("ArmHeight", FPS_CVAR("Viewmodel.Height"), -1.0f);
}
float CfgVmSide() {
    return AgeCfg("ArmSide", nullptr, 0.0f);
}
float CfgVmPitchFollow() {
    return AgeCfg("ArmPitchFollow", FPS_CVAR("Viewmodel.PitchFollow"), 0.6f);
}
float CfgCamHeight() {
    return AgeCfg("CamHeight", nullptr, 0.0f);
}
float CfgCamForward() {
    return AgeCfg("CamForward", nullptr, 0.0f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Small math helpers (binary angles: 0x10000 = 360 degrees)
// ---------------------------------------------------------------------------------------------------------------------

constexpr float kBinangToRad = (float)(M_PI / 32768.0);
constexpr float kDegToBinang = 65536.0f / 360.0f;

inline Vec3f V3(float x, float y, float z) {
    Vec3f v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
inline Vec3f Add(Vec3f a, Vec3f b) {
    return V3(a.x + b.x, a.y + b.y, a.z + b.z);
}
inline Vec3f Sub(Vec3f a, Vec3f b) {
    return V3(a.x - b.x, a.y - b.y, a.z - b.z);
}
inline Vec3f Scale(Vec3f a, float s) {
    return V3(a.x * s, a.y * s, a.z * s);
}
inline float Dot(Vec3f a, Vec3f b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3f Cross(Vec3f a, Vec3f b) {
    return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline float Len(Vec3f a) {
    return sqrtf(Dot(a, a));
}
inline Vec3f Norm(Vec3f a) {
    float l = Len(a);
    return (l > 0.0001f) ? Scale(a, 1.0f / l) : V3(0, 0, 1);
}
inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}
inline Vec3f LerpV(Vec3f a, Vec3f b, float t) {
    return V3(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t));
}
inline float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float WrapBinang(float a) {
    while (a > 32768.0f) {
        a -= 65536.0f;
    }
    while (a < -32768.0f) {
        a += 65536.0f;
    }
    return a;
}
// Direction a binary yaw/pitch looks at. Pitch is positive when looking up.
inline Vec3f DirFromAngles(float yaw, float pitch) {
    float y = yaw * kBinangToRad;
    float p = pitch * kBinangToRad;
    return V3(sinf(y) * cosf(p), sinf(p), cosf(y) * cosf(p));
}
// Camera right vector for a yaw (turning right lowers the yaw in OoT).
inline Vec3f RightFromYaw(float yaw) {
    float y = yaw * kBinangToRad;
    return V3(-cosf(y), 0.0f, sinf(y));
}

// Rotation of `angle` radians around a unit axis, right-handed.
void SetRotation(MtxF* mf, Vec3f axis, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;
    float x = axis.x, y = axis.y, z = axis.z;
    SkinMatrix_Clear(mf);
    mf->xx = c + x * x * t;
    mf->xy = x * y * t - z * s;
    mf->xz = x * z * t + y * s;
    mf->yx = y * x * t + z * s;
    mf->yy = c + y * y * t;
    mf->yz = y * z * t - x * s;
    mf->zx = z * x * t - y * s;
    mf->zy = z * y * t + x * s;
    mf->zz = c + z * z * t;
}

Vec3f MtxRotate(MtxF* mf, Vec3f v) {
    return V3(mf->xx * v.x + mf->xy * v.y + mf->xz * v.z, mf->yx * v.x + mf->yy * v.y + mf->yz * v.z,
              mf->zx * v.x + mf->zy * v.y + mf->zz * v.z);
}

// ---------------------------------------------------------------------------------------------------------------------
// Input sampling
// ---------------------------------------------------------------------------------------------------------------------

struct InputState {
    bool down[ACT_COUNT] = {};
    bool pressed[ACT_COUNT] = {};
    float wheel = 0.0f;
};

// Filled at render rate by the overlay window so quick taps between 20Hz game frames are never lost.
InputState sLatch;
bool sPrevDown[ACT_COUNT] = {};
InputState sFrame; // the snapshot used by the current game frame

bool IsKeyDownSafe(ImGuiKey key) {
    if (key == ImGuiKey_None || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }
    if (ImGui::IsKeyDown(key)) {
        return true;
    }
    // The window backends always know the true mouse button state, even while the cursor is captured.
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    if (window != nullptr) {
        if (key == ImGuiKey_MouseLeft) {
            return window->GetMouseState(Ship::LUS_MOUSE_BTN_LEFT);
        }
        if (key == ImGuiKey_MouseRight) {
            return window->GetMouseState(Ship::LUS_MOUSE_BTN_RIGHT);
        }
        if (key == ImGuiKey_MouseMiddle) {
            return window->GetMouseState(Ship::LUS_MOUSE_BTN_MIDDLE);
        }
    }
    return false;
}

bool GuiCapturesInput() {
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    if (window == nullptr || window->GetGui() == nullptr) {
        return true;
    }
    return window->GetGui()->GetMenuOrMenubarVisible();
}

void SampleInputRenderRate() {
    if (!CfgEnabled() || GuiCapturesInput()) {
        sLatch = InputState();
        return;
    }
    for (int i = 0; i < ACT_COUNT; i++) {
        ImGuiKey key = GetBinding((Action)i);
        bool down = IsKeyDownSafe(key);
        sLatch.down[i] = down;
        if (key != ImGuiKey_None && ImGui::IsKeyPressed(key, false)) {
            sLatch.pressed[i] = true;
        }
    }
    sLatch.wheel += ImGui::GetIO().MouseWheel;
}

// Takes the latched input for this game frame.
void TakeInputFrame() {
    if (!CfgEnabled() || GuiCapturesInput()) {
        sFrame = InputState();
        memset(sPrevDown, 0, sizeof(sPrevDown));
        sLatch = InputState();
        return;
    }
    for (int i = 0; i < ACT_COUNT; i++) {
        // Re-read the held state in case the overlay hasn't run yet this frame.
        bool down = sLatch.down[i] || IsKeyDownSafe(GetBinding((Action)i));
        sFrame.down[i] = down;
        sFrame.pressed[i] = sLatch.pressed[i] || (down && !sPrevDown[i]);
        sPrevDown[i] = down;
        sLatch.pressed[i] = false;
    }
    sFrame.wheel = sLatch.wheel;
    sLatch.wheel = 0.0f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared gameplay state
// ---------------------------------------------------------------------------------------------------------------------

enum InputMode {
    MODE_NONE,     // mod off, or SOH menu open
    MODE_MENU,     // pause menu, title, file select
    MODE_TEXT,     // a message box is open
    MODE_OCARINA,  // playing or being taught a song
    MODE_GAMEPLAY, // first-person controls
};

InputMode sMode = MODE_NONE;

// Look angles (binary angles as floats for precision)
float sYaw = 0.0f;
float sPitch = 0.0f;
float sYawVel = 0.0f; // per-frame change, for viewmodel sway
float sPitchVel = 0.0f;

// Camera ownership
bool sCamActive = false;
int sCamInactiveFrames = 1000;
Vec3f sEye = { 0, 0, 0 };
Vec3f sViewDir = { 0, 0, 1 };
float sViewYaw = 0.0f; // final yaw used for the view (with effects)

// Head position relative to Link's origin, captured during the last draw
Vec3f sDrawWorldPos = { 0, 0, 0 };
Vec3f sHeadOffset = { 0, 50.0f, 0 };
bool sHeadOffsetValid = false;
Vec3f sSmoothedStandOffset = { 0, 50.0f, 0 };
float sSpecialBlend = 0.0f;

// Comfort effects
float sBobPhase = 0.0f;
float sBobAmountSmoothed = 0.0f;
float sLandOffset = 0.0f;
float sLandVel = 0.0f;
float sLandPitch = 0.0f;
bool sWasAirborne = false;
float sMinAirVelY = 0.0f;
float sStrafeTilt = 0.0f;
float sHitPitch = 0.0f, sHitRoll = 0.0f, sHitYaw = 0.0f;
float sHitTimer = 0.0f;

// Viewmodel
float sPoseWeight = 0.0f;
Vec3f sSwayOffset = { 0, 0, 0 };
Vec3f sSwayVel = { 0, 0, 0 };

// Controls
bool sWantParallel = false;
bool sFocusPressed = false;
s32 sAPressKinds = 0;     // FPS_A_* bits pressed this frame
bool sCrouchHeld = false;
bool sFocusHeld = false;
bool sLockRequested = false; // we asked the game to lock on, so we're the ones to release it

// Items: slot 0 = none, 1..3 = C-Left/Down/Right, 4..7 = D-Pad Up/Down/Left/Right
int sReadiedSlot = 0;
int sLastSlot = 0;
int sPendingReadySlot = 0;
int sPendingReadyTimer = 0;
bool sPendingSword = false;
int sPendingSwordTimer = 0;
int sSuppressUseTimer = 0;
int sPulseSlot = 0;       // press this slot's button for one frame (toggle items: Lens, masks)
bool sPendingPutAway = false;
int sPendingPutAwayTimer = 0;

const u16 kSlotButtons[8] = { 0, BTN_CLEFT, BTN_CDOWN, BTN_CRIGHT, BTN_DUP, BTN_DDOWN, BTN_DLEFT, BTN_DRIGHT };

// ---------------------------------------------------------------------------------------------------------------------
// Helpers on game state
// ---------------------------------------------------------------------------------------------------------------------

Player* LocalPlayer() {
    if (gPlayState == nullptr) {
        return nullptr;
    }
    return GET_PLAYER(gPlayState);
}

bool IsLocalPlayer(Player* player) {
    return player != nullptr && gPlayState != nullptr && player == GET_PLAYER(gPlayState) &&
           player->actor.category == ACTORCAT_PLAYER;
}

bool InGameplay() {
    return gPlayState != nullptr && LocalPlayer() != nullptr && gSaveContext.gameMode == GAMEMODE_NORMAL;
}

bool DpadEquipsOn() {
    return CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) != 0;
}

int SlotCount() {
    return DpadEquipsOn() ? 7 : 3;
}

s32 SlotItem(int slot) {
    if (gPlayState == nullptr || slot <= 0 || slot > SlotCount()) {
        return ITEM_NONE;
    }
    return Player_GetItemOnButton(gPlayState, slot);
}

bool IsSwordAction(s32 ia) {
    return ia >= PLAYER_IA_SWORD_MASTER && ia <= PLAYER_IA_SWORD_BIGGORON;
}

bool IsRangedAction(s32 ia) {
    return (ia >= PLAYER_IA_BOW && ia <= PLAYER_IA_LONGSHOT) || ia == PLAYER_IA_BOOMERANG;
}

// Items Link holds in his hand until put away. Readying one takes it out without using it.
bool IsHeldAction(s32 ia) {
    if (ia == PLAYER_IA_FISHING_POLE || ia == PLAYER_IA_DEKU_STICK || ia == PLAYER_IA_HAMMER) {
        return true;
    }
    return IsSwordAction(ia) || IsRangedAction(ia);
}

bool IsBottleAction(s32 ia) {
    return ia >= PLAYER_IA_BOTTLE && ia <= PLAYER_IA_BOTTLE_FAIRY;
}

bool IsOcarinaAction(s32 ia) {
    return ia == PLAYER_IA_OCARINA_FAIRY || ia == PLAYER_IA_OCARINA_OF_TIME;
}

// Items that switch on and off rather than being aimed or swung: they toggle the moment you press their key.
bool IsToggleAction(s32 ia) {
    return ia == PLAYER_IA_LENS_OF_TRUTH || (ia >= PLAYER_IA_MASK_KEATON && ia <= PLAYER_IA_MASK_TRUTH);
}

bool IsTalking(Player* player) {
    return (player->stateFlags1 & PLAYER_STATE1_TALKING) != 0;
}

bool MessageOpen() {
    return gPlayState != nullptr && gPlayState->msgCtx.msgMode != MSGMODE_NONE;
}

bool OcarinaMessageMode() {
    if (gPlayState == nullptr) {
        return false;
    }
    s32 mode = gPlayState->msgCtx.msgMode;
    if (mode >= MSGMODE_OCARINA_STARTING && mode <= MSGMODE_OCARINA_AWAIT_INPUT) {
        // The "you played X" text boxes are plain text
        return !(mode >= MSGMODE_SETUP_DISPLAY_SONG_PLAYED && mode <= MSGMODE_DISPLAY_SONG_PLAYED_TEXT);
    }
    return (mode >= MSGMODE_SCARECROW_LONG_RECORDING_START && mode <= MSGMODE_FROGS_WAITING);
}

bool PrerenderedRoom(PlayState* play) {
    return play->roomCtx.curRoom.meshHeader != nullptr && play->roomCtx.curRoom.meshHeader->base.type == 1;
}

// ---------------------------------------------------------------------------------------------------------------------
// Mouse capture
// ---------------------------------------------------------------------------------------------------------------------

bool sCaptureRequested = false;

void UpdateMouseCapture() {
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    if (window == nullptr) {
        return;
    }
    bool want = CfgEnabled();
    if (want != sCaptureRequested) {
        sCaptureRequested = want;
        window->GetMouseStateManager()->SetAutoCaptureMouse(want);
        window->GetMouseStateManager()->UpdateMouseCapture();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Pad building
// ---------------------------------------------------------------------------------------------------------------------

void SetStick(OSContPad* pad, bool up, bool down, bool left, bool right, float magnitude) {
    float x = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    float y = (up ? 1.0f : 0.0f) - (down ? 1.0f : 0.0f);
    float len = sqrtf(x * x + y * y);
    if (len > 0.0f) {
        x /= len;
        y /= len;
    }
    pad->stick_x = (s8)(x * magnitude);
    pad->stick_y = (s8)(y * magnitude);
}

void BuildMenuPad(OSContPad* pad) {
    const bool* d = sFrame.down;
    bool up = d[ACT_FORWARD] || ImGui::IsKeyDown(ImGuiKey_UpArrow);
    bool down = d[ACT_BACK] || ImGui::IsKeyDown(ImGuiKey_DownArrow);
    bool left = d[ACT_LEFT] || ImGui::IsKeyDown(ImGuiKey_LeftArrow);
    bool right = d[ACT_RIGHT] || ImGui::IsKeyDown(ImGuiKey_RightArrow);
    SetStick(pad, up, down, left, right, 80.0f);

    if (d[ACT_INTERACT] || d[ACT_ROLL] || d[ACT_ATTACK] || ImGui::IsKeyDown(ImGuiKey_Enter)) {
        pad->button |= BTN_A;
    }
    if (d[ACT_SHIELD] || d[ACT_MENU_BACK]) {
        pad->button |= BTN_B;
    }
    if (d[ACT_PAUSE]) {
        pad->button |= BTN_START;
    }
    // Number keys equip items onto the matching C-buttons / D-Pad in the pause menu
    for (int slot = 1; slot <= 7; slot++) {
        if (d[ACT_SLOT_1 + slot - 1]) {
            pad->button |= kSlotButtons[slot];
        }
    }
    if (d[ACT_NAVI] || d[ACT_PREV_ITEM]) {
        pad->button |= BTN_Z; // page left
    }
    if (d[ACT_SWORD] || d[ACT_NEXT_ITEM]) {
        pad->button |= BTN_R; // page right
    }
    // Mouse wheel flips pause pages too
    if (sFrame.wheel > 0.5f) {
        pad->button |= BTN_Z;
    } else if (sFrame.wheel < -0.5f) {
        pad->button |= BTN_R;
    }
}

void BuildTextPad(OSContPad* pad) {
    const bool* d = sFrame.down;
    SetStick(pad, d[ACT_FORWARD], d[ACT_BACK], d[ACT_LEFT], d[ACT_RIGHT], 80.0f);
    if (d[ACT_INTERACT] || d[ACT_ROLL] || d[ACT_ATTACK]) {
        pad->button |= BTN_A;
    }
    if (d[ACT_SHIELD] || d[ACT_MENU_BACK]) {
        pad->button |= BTN_B;
    }
    if (d[ACT_PAUSE]) {
        pad->button |= BTN_START;
    }
}

void BuildOcarinaPad(OSContPad* pad) {
    const bool* d = sFrame.down;
    pad->stick_x = 0;
    pad->stick_y = 0;
    if (d[ACT_FORWARD]) {
        pad->button |= BTN_CUP;
    }
    if (d[ACT_LEFT]) {
        pad->button |= BTN_CLEFT;
    }
    if (d[ACT_BACK]) {
        pad->button |= BTN_CDOWN;
    }
    if (d[ACT_RIGHT]) {
        pad->button |= BTN_CRIGHT;
    }
    if (d[ACT_INTERACT]) {
        pad->button |= BTN_A;
    }
    if (d[ACT_SHIELD] || d[ACT_MENU_BACK] || d[ACT_SWORD]) {
        pad->button |= BTN_B;
    }
    if (d[ACT_PAUSE]) {
        pad->button |= BTN_START;
    }
}

int NextFilledSlot(int from, int dir) {
    int count = SlotCount();
    int slot = from;
    for (int i = 0; i < count; i++) {
        slot += dir;
        if (slot > count) {
            slot = 1;
        }
        if (slot < 1) {
            slot = count;
        }
        if (SlotItem(slot) != ITEM_NONE) {
            return slot;
        }
    }
    return 0;
}

void ReadySlot(int slot) {
    if (slot <= 0 || SlotItem(slot) == ITEM_NONE) {
        return;
    }
    Player* player = LocalPlayer();
    s32 ia = Player_ItemToItemAction(SlotItem(slot));
    if (IsToggleAction(ia)) {
        sPulseSlot = slot; // Lens and masks switch on/off right away and don't replace what you're holding
        return;
    }
    if (sReadiedSlot != slot) {
        sLastSlot = sReadiedSlot;
    }
    sReadiedSlot = slot;
    sPendingPutAway = false;
    if (player == nullptr) {
        return;
    }
    // Held items (bow, hookshot, sticks...) are pulled out right away so you can see what's ready.
    if (IsHeldAction(ia) && player->heldItemAction != ia) {
        sPendingReadySlot = slot;
        sPendingReadyTimer = 10;
    } else if (!IsHeldAction(ia) && player->heldItemAction != PLAYER_IA_NONE &&
               player->heldItemAction != PLAYER_IA_SWORD_CS) {
        // Bottles, the ocarina and other one-shot items: put away whatever is in hand. The readied item is then
        // shown in Link's hand until you use it.
        sPendingPutAway = true;
        sPendingPutAwayTimer = 10;
    }
}

bool WantParallel(Player* player) {
    if (!sCamActive || sCrouchHeld || sFocusHeld) {
        return false;
    }
    if (IsRangedAction(player->heldItemAction)) {
        return false; // ranged items aim in first person, which can't start while strafing
    }
    if (player->stateFlags1 &
        (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_CLIMBING_LADDER |
         PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_TALKING |
         PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_DEAD)) {
        return false;
    }
    if (player->stateFlags2 & (PLAYER_STATE2_CRAWLING | PLAYER_STATE2_GRABBING_DYNAPOLY | PLAYER_STATE2_UNDERWATER)) {
        return false;
    }
    if (player->unk_6AD != 0) {
        return false;
    }
    // Walking forward into a wall: let vanilla handle climbing, pushing and crawlspaces (they don't run while strafing)
    if ((player->actor.bgCheckFlags & BGCHECKFLAG_PLAYER_WALL_INTERACT) && sFrame.down[ACT_FORWARD]) {
        s16 toWall = (s16)(player->actor.wallYaw + 0x8000);
        s16 diff = (s16)((s16)sYaw - toWall);
        if (ABS(diff) < 0x2AAA) {
            return false;
        }
    }
    return true;
}

void BuildGameplayPad(OSContPad* pad, Player* player) {
    const bool* d = sFrame.down;
    const bool* p = sFrame.pressed;

    // Movement: the game reads the stick relative to the camera, which is our view.
    float magnitude = d[ACT_WALK] ? 38.0f : 80.0f;
    SetStick(pad, d[ACT_FORWARD], d[ACT_BACK], d[ACT_LEFT], d[ACT_RIGHT], magnitude);

    // Split A button
    sAPressKinds = 0;
    if (p[ACT_INTERACT]) {
        sAPressKinds |= FPS_A_INTERACT;
    }
    if (p[ACT_ROLL]) {
        sAPressKinds |= FPS_A_MOVE;
    }
    if (d[ACT_INTERACT] || d[ACT_ROLL]) {
        pad->button |= BTN_A;
    }

    // Item slots and the sword
    if (p[ACT_SWORD]) {
        sReadiedSlot = 0;
        sPendingReadySlot = 0;
        sPendingSword = true;
        sPendingSwordTimer = 10;
    }
    for (int slot = 1; slot <= 7; slot++) {
        if (p[ACT_SLOT_1 + slot - 1] && slot <= SlotCount()) {
            ReadySlot(slot);
        }
    }
    if (sFrame.wheel <= -0.5f || p[ACT_NEXT_ITEM]) {
        ReadySlot(NextFilledSlot(sReadiedSlot, 1));
    } else if (sFrame.wheel >= 0.5f || p[ACT_PREV_ITEM]) {
        ReadySlot(NextFilledSlot(sReadiedSlot, -1));
    }
    if (p[ACT_LAST_ITEM] && sLastSlot != 0) {
        ReadySlot(sLastSlot);
    }
    if (sReadiedSlot != 0 && SlotItem(sReadiedSlot) == ITEM_NONE) {
        sReadiedSlot = 0; // the item was unequipped or used up
    }

    // Left click: use the readied item, otherwise the sword
    if (p[ACT_ATTACK]) {
        sSuppressUseTimer = 0;
    }
    if (d[ACT_ATTACK]) {
        pad->button |= (sReadiedSlot != 0) ? kSlotButtons[sReadiedSlot] : BTN_B;
    }
    if (sPulseSlot != 0) {
        pad->button |= kSlotButtons[sPulseSlot];
        sPulseSlot = 0;
    }

    // Right click: shield. Ctrl: crouch behind the shield (needs Link out of strafing mode)
    sCrouchHeld = d[ACT_CROUCH];
    if (d[ACT_SHIELD] || d[ACT_CROUCH]) {
        pad->button |= BTN_R;
    }

    // Focus (Z) and strafing. Z is held whenever Link should strafe so the game keeps him in "parallel" mode.
    sFocusHeld = d[ACT_FOCUS];
    if (p[ACT_FOCUS]) {
        sFocusPressed = true;
    } else if (!sFocusHeld) {
        sFocusPressed = false;
    }
    if (sLockRequested && !sFocusHeld && !IsTalking(player)) {
        sLockRequested = false;
        if (player->focusActor != nullptr && player->autoLockOnActor == nullptr) {
            Player_FirstPersonMod_ReleaseLockOn(player);
        }
    }
    sWantParallel = WantParallel(player);
    if (sWantParallel || sFocusHeld) {
        pad->button |= BTN_Z;
    }

    // Navi: only when she has something to say, otherwise C-Up would drop into the vanilla look mode
    if (d[ACT_NAVI] && (player->stateFlags2 & PLAYER_STATE2_NAVI_ALERT)) {
        pad->button |= BTN_CUP;
    }

    if (d[ACT_PAUSE]) {
        pad->button |= BTN_START;
    }

    if (sPendingReadyTimer > 0 && --sPendingReadyTimer == 0) {
        sPendingReadySlot = 0;
    }
    if (sPendingSwordTimer > 0 && --sPendingSwordTimer == 0) {
        sPendingSword = false;
    }
    if (sPendingPutAwayTimer > 0 && --sPendingPutAwayTimer == 0) {
        sPendingPutAway = false;
    }
    if (sSuppressUseTimer > 0) {
        sSuppressUseTimer--;
    }
}

void UpdateLook(Player* player) {
    s32 dx = 0, dy = 0;
    Mouse_GetDelta(&dx, &dy);
    float prevYaw = sYaw, prevPitch = sPitch;

    if (sMode == MODE_GAMEPLAY && sCamActive && !IsTalking(player)) {
        float k = CfgSensitivity() * 0.022f * kDegToBinang;
        // Aim assist: the crosshair slows down while it's over an enemy
        Actor* target = gPlayState->actorCtx.targetCtx.arrowPointedActor;
        if (target != nullptr && CHECK_FLAG_ALL(target->flags, ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE)) {
            k *= 1.0f - 0.5f * Clampf(CfgAimAssist(), 0.0f, 1.0f);
        }
        sYaw = WrapBinang(sYaw - dx * k);
        sPitch -= dy * k * (CfgInvertY() ? -1.0f : 1.0f);
        sPitch = Clampf(sPitch, -85.0f * kDegToBinang, 85.0f * kDegToBinang);
    }

    sYawVel = WrapBinang(sYaw - prevYaw);
    sPitchVel = sPitch - prevPitch;
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Public C API
// ---------------------------------------------------------------------------------------------------------------------

extern "C" s32 FirstPersonMod_IsEnabled(void) {
    return CfgEnabled();
}

extern "C" s32 FirstPersonMod_ControlsActive(void) {
    return CfgEnabled() && sMode != MODE_NONE;
}

extern "C" s32 FirstPersonMod_CameraActive(void) {
    return CfgEnabled() && sCamActive;
}

extern "C" void FirstPersonMod_ProcessInput(void* contPad) {
    OSContPad* pad = (OSContPad*)contPad;
    EnsureOverlay();
    UpdateMouseCapture();
    TakeInputFrame();

    if (!CfgEnabled() || GuiCapturesInput()) {
        sMode = MODE_NONE;
        return;
    }

    Player* player = LocalPlayer();
    if (!InGameplay()) {
        sMode = MODE_MENU;
    } else if (gPlayState->pauseCtx.state != 0 || gPlayState->pauseCtx.debugState != 0) {
        sMode = MODE_MENU;
    } else if (OcarinaMessageMode()) {
        sMode = MODE_OCARINA;
    } else if (MessageOpen()) {
        sMode = MODE_TEXT;
    } else {
        sMode = MODE_GAMEPLAY;
    }

    // Keyboard and mouse replace the controller on port 1 while the mod is on.
    pad->button = 0;
    pad->stick_x = pad->stick_y = 0;
    pad->right_stick_x = pad->right_stick_y = 0;
    pad->gyro_x = pad->gyro_y = 0.0f;

    switch (sMode) {
        case MODE_MENU:
            BuildMenuPad(pad);
            break;
        case MODE_TEXT:
            BuildTextPad(pad);
            break;
        case MODE_OCARINA:
            BuildOcarinaPad(pad);
            break;
        case MODE_GAMEPLAY:
            BuildGameplayPad(pad, player);
            break;
        default:
            break;
    }

    if (player != nullptr && InGameplay()) {
        UpdateLook(player);
        if (sCamActive && gPlayState->activeCamera == CAM_ID_MAIN) {
            Camera* cam = GET_ACTIVE_CAM(gPlayState);
            cam->inputDir.x = (s16)(-sPitch);
            cam->inputDir.y = (s16)sYaw;
        }
    }
}

extern "C" s32 FirstPersonMod_AllowAPress(Player* player, s32 kind) {
    if (!IsLocalPlayer(player) || sMode != MODE_GAMEPLAY) {
        return true;
    }
    return (sAPressKinds & kind) != 0;
}

extern "C" s32 FirstPersonMod_AllowAHeld(Player* player, s32 kind) {
    if (!IsLocalPlayer(player) || sMode != MODE_GAMEPLAY) {
        return true;
    }
    return (kind == FPS_A_INTERACT) ? sFrame.down[ACT_INTERACT] : sFrame.down[ACT_ROLL];
}

extern "C" s32 FirstPersonMod_ZTargetPress(Player* player, s32 vanillaPress) {
    if (!IsLocalPlayer(player) || sMode != MODE_GAMEPLAY) {
        return vanillaPress;
    }
    // Z is held to keep Link strafing, so only the Focus key starts a lock-on.
    bool pressed = sFocusPressed;
    sFocusPressed = false;
    if (pressed) {
        sLockRequested = true;
    }
    return pressed;
}

extern "C" void FirstPersonMod_UpdateParallel(Player* player) {
    if (!IsLocalPlayer(player)) {
        return;
    }
    if (sMode != MODE_GAMEPLAY) {
        sFocusPressed = false;
        return;
    }
    if (sWantParallel && player->focusActor == nullptr &&
        !(player->stateFlags1 & PLAYER_STATE1_LOCK_ON_FORCED_TO_RELEASE)) {
        if (!(player->stateFlags1 & PLAYER_STATE1_PARALLEL)) {
            Player_FirstPersonMod_SetParallel(player);
        }
        player->stateFlags1 |= PLAYER_STATE1_Z_TARGETING;
        // Don't fight a roll that's heading somewhere else; it straightens out once it ends.
        if (Player_FirstPersonMod_IsRolling(player)) {
            player->parallelYaw = player->actor.shape.rot.y;
        } else {
            player->parallelYaw = (s16)sYaw;
        }
    } else if (!sWantParallel && (player->stateFlags1 & PLAYER_STATE1_PARALLEL) && !sFocusHeld) {
        player->stateFlags1 &= ~PLAYER_STATE1_PARALLEL;
    }
}

extern "C" s32 FirstPersonMod_RollInsteadOfHop(Player* player, s32 stickDirection) {
    // Without Focus held, Space rolls toward whichever way you're moving instead of hopping.
    return IsLocalPlayer(player) && sMode == MODE_GAMEPLAY && !sFocusHeld && player->focusActor == nullptr;
}

extern "C" s32 FirstPersonMod_BlockStandingRoll(Player* player) {
    // Standing still, vanilla A would sheathe the sword or toggle Navi. R handles the sword now.
    return IsLocalPlayer(player) && sMode == MODE_GAMEPLAY && !(sFrame.down[ACT_FORWARD] || sFrame.down[ACT_BACK] ||
                                                                 sFrame.down[ACT_LEFT] || sFrame.down[ACT_RIGHT]);
}

extern "C" void FirstPersonMod_OverrideZAimPitch(Player* player) {
    if (IsLocalPlayer(player) && sMode == MODE_GAMEPLAY && sCamActive) {
        player->actor.focus.rot.x = (s16)(-sPitch);
    }
}

extern "C" s32 FirstPersonMod_TargetingActive(void) {
    return CfgEnabled() && sCamActive && sMode == MODE_GAMEPLAY;
}

extern "C" f32 FirstPersonMod_CrosshairWeight(PlayState* play, Actor* actor) {
    Vec3f toActor = Sub(actor->focus.pos, sEye);
    float dist = Len(toActor);
    if (dist < 1.0f) {
        return 0.0f;
    }
    float cosAngle = Dot(toActor, sViewDir) / dist;
    // Close actors get a wider cone so you don't have to aim exactly at someone standing next to you.
    float maxDeg = Clampf(10.0f + 600.0f / dist, 10.0f, 30.0f);
    float angle = acosf(Clampf(cosAngle, -1.0f, 1.0f)) * (180.0f / (float)M_PI);
    if (angle > maxDeg) {
        return -1.0f;
    }
    // Mostly how close to the crosshair, a little bit how close to Link
    return angle * angle + dist * 0.01f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------------------------------------------------

namespace {

bool CameraFuncAllowed(s32 funcIdx) {
    switch (funcIdx) {
        case CAM_FUNC_NORM0:
        case CAM_FUNC_NORM1:
        case CAM_FUNC_NORM2:
        case CAM_FUNC_NORM3:
        case CAM_FUNC_NORM4:
        case CAM_FUNC_PARA0:
        case CAM_FUNC_PARA1:
        case CAM_FUNC_PARA2:
        case CAM_FUNC_PARA3:
        case CAM_FUNC_PARA4:
        case CAM_FUNC_KEEP0:
        case CAM_FUNC_KEEP1:
        case CAM_FUNC_KEEP2:
        case CAM_FUNC_KEEP3:
        case CAM_FUNC_KEEP4:
        case CAM_FUNC_SUBJ0:
        case CAM_FUNC_SUBJ1:
        case CAM_FUNC_SUBJ2:
        case CAM_FUNC_SUBJ3:
        case CAM_FUNC_SUBJ4:
        case CAM_FUNC_JUMP0:
        case CAM_FUNC_JUMP1:
        case CAM_FUNC_JUMP2:
        case CAM_FUNC_JUMP3:
        case CAM_FUNC_JUMP4:
        case CAM_FUNC_BATT0:
        case CAM_FUNC_BATT1:
        case CAM_FUNC_BATT2:
        case CAM_FUNC_BATT3:
        case CAM_FUNC_BATT4:
        case CAM_FUNC_FIXD0:
        case CAM_FUNC_FIXD1:
        case CAM_FUNC_FIXD2:
        case CAM_FUNC_FIXD3:
        case CAM_FUNC_FIXD4:
        case CAM_FUNC_UNIQ1: // hanging from ledges
        case CAM_FUNC_SPEC5: // flying on the hookshot
            return true;
        default:
            return false;
    }
}

bool CameraShouldBeFirstPerson(Camera* camera, s32 funcIdx) {
    if (!CfgEnabled()) {
        return false;
    }
    PlayState* play = camera->play;
    if (play == nullptr || camera->thisIdx != CAM_ID_MAIN || play->activeCamera != CAM_ID_MAIN) {
        return false;
    }
    Player* player = (Player*)camera->player;
    if (!IsLocalPlayer(player) || gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return false;
    }
    // Story cutscenes, item fanfares, doors and deaths keep their original cameras
    if (play->csCtx.state != CS_STATE_IDLE || player->csAction != 0) {
        return false;
    }
    if (player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_CUTSCENE)) {
        return false;
    }
    if (play->transitionTrigger != TRANS_TRIGGER_OFF && play->transitionTrigger != TRANS_TRIGGER_END) {
        return false;
    }
    // Pre-rendered rooms (Market, some houses) are flat pictures that only work from their fixed camera
    if (PrerenderedRoom(play)) {
        return false;
    }
    return CameraFuncAllowed(funcIdx);
}

// States where the camera should follow Link's animated head closely (it moves far from his origin).
bool InSpecialPose(Player* player, float* follow) {
    bool rolling = Player_FirstPersonMod_IsRolling(player);
    if (rolling) {
        *follow = 0.4f + 0.6f * Clampf(CfgBob(), 0.0f, 1.0f);
        return true;
    }
    if ((player->stateFlags1 & (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE |
                                PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_IN_WATER | PLAYER_STATE1_ON_HORSE |
                                PLAYER_STATE1_DAMAGED | PLAYER_STATE1_SHIELDING)) ||
        (player->stateFlags2 & (PLAYER_STATE2_CRAWLING | PLAYER_STATE2_HOPPING | PLAYER_STATE2_UNDERWATER |
                                PLAYER_STATE2_DIVING)) ||
        player->unk_6AD != 0) {
        *follow = 1.0f;
        return true;
    }
    return false;
}

void UpdateComfortEffects(Player* player) {
    // Landing: a spring that dips the view when you hit the ground
    bool airborne = !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
    if (airborne) {
        sMinAirVelY = std::min(sMinAirVelY, player->actor.velocity.y);
    } else if (sWasAirborne) {
        float impact = Clampf(-sMinAirVelY - 4.0f, 0.0f, 16.0f) * Clampf(CfgLanding(), 0.0f, 1.0f);
        sLandVel -= impact * 0.9f;
        sLandPitch -= impact * 90.0f;
        sMinAirVelY = 0.0f;
    }
    if (!airborne) {
        sMinAirVelY = 0.0f;
    }
    sWasAirborne = airborne;
    sLandVel += -sLandOffset * 0.35f - sLandVel * 0.45f;
    sLandOffset += sLandVel;
    sLandOffset = Clampf(sLandOffset, -18.0f, 6.0f);
    sLandPitch *= 0.7f;

    // Strafing leans the view slightly
    Vec3f vel = V3(player->actor.velocity.x, 0, player->actor.velocity.z);
    float lateral = Dot(vel, RightFromYaw(sYaw));
    float tiltTarget = -lateral * 55.0f * Clampf(CfgSway(), 0.0f, 1.0f);
    sStrafeTilt = Lerp(sStrafeTilt, tiltTarget, 0.25f);

    // Getting hit
    if (sHitTimer > 0.0f) {
        sHitTimer -= 1.0f;
    }
    sHitPitch *= 0.72f;
    sHitRoll *= 0.72f;
    sHitYaw *= 0.72f;
}

void ResetComfortEffects() {
    sLandOffset = sLandVel = sLandPitch = 0.0f;
    sWasAirborne = false;
    sMinAirVelY = 0.0f;
    sStrafeTilt = 0.0f;
    sHitPitch = sHitRoll = sHitYaw = 0.0f;
    sSwayOffset = sSwayVel = V3(0, 0, 0);
    sSpecialBlend = 0.0f;
}

Vec3f ComputeEye(Player* player) {
    Vec3f base = player->actor.world.pos;
    Vec3f head = sHeadOffsetValid ? sHeadOffset : V3(0, Player_GetHeight(player) * 0.85f, 0);

    float follow = 0.0f;
    bool special = InSpecialPose(player, &follow);
    sSpecialBlend = Lerp(sSpecialBlend, special ? follow : 0.0f, 0.35f);

    if (!special) {
        sSmoothedStandOffset = LerpV(sSmoothedStandOffset, head, 0.15f);
    }
    Vec3f stable = V3(0, sSmoothedStandOffset.y, 0);
    float bob = Clampf(CfgBob(), 0.0f, 1.0f);

    // Normal walking: a steady eye, with a share of Link's real head motion scaled by the bob slider
    Vec3f offset = LerpV(stable, head, bob * 0.5f);
    offset = LerpV(offset, head, sSpecialBlend);

    // Procedural head bob while moving on the ground
    bool grounded = (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) != 0;
    float speed = sqrtf(SQ(player->actor.velocity.x) + SQ(player->actor.velocity.z));
    float bobTarget = (grounded && !special) ? Clampf(speed / 6.0f, 0.0f, 1.0f) : 0.0f;
    sBobAmountSmoothed = Lerp(sBobAmountSmoothed, bobTarget, 0.2f);
    sBobPhase += speed * 0.11f;
    float bobY = sinf(sBobPhase * 2.0f) * 1.6f * bob * sBobAmountSmoothed;
    float bobX = sinf(sBobPhase) * 1.0f * bob * sBobAmountSmoothed;
    offset = Add(offset, Add(V3(0, bobY + sLandOffset, 0), Scale(RightFromYaw(sYaw), bobX)));

    // Camera height / forward sliders (per age), forward along the flat look direction
    offset.y += CfgCamHeight();
    offset = Add(offset, Scale(DirFromAngles(sYaw, 0.0f), CfgCamForward()));

    return Add(base, offset);
}

void SteerTowards(Vec3f eye, Vec3f target, float rate) {
    Vec3f d = Sub(target, eye);
    float xz = sqrtf(d.x * d.x + d.z * d.z);
    if (xz < 0.01f && fabsf(d.y) < 0.01f) {
        return;
    }
    float targetYaw = atan2f(d.x, d.z) / kBinangToRad;
    float targetPitch = atan2f(d.y, xz) / kBinangToRad;
    sYaw = WrapBinang(sYaw + WrapBinang(targetYaw - sYaw) * rate);
    sPitch = Lerp(sPitch, Clampf(targetPitch, -60.0f * kDegToBinang, 60.0f * kDegToBinang), rate);
}

void UpdatePoseWeight(Player* player) {
    float target = 0.0f;
    if (!Player_FirstPersonMod_IsMeleeAttacking(player) && player->meleeWeaponState == 0 &&
        player->unk_6AD == 0 && !Player_FirstPersonMod_IsRolling(player) &&
        !(player->stateFlags1 &
          (PLAYER_STATE1_SHIELDING | PLAYER_STATE1_CHARGING_SPIN_ATTACK | PLAYER_STATE1_CLIMBING_LADDER |
           PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_IN_WATER |
           PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_DAMAGED |
           PLAYER_STATE1_START_CHANGING_HELD_ITEM | PLAYER_STATE1_READY_TO_FIRE | PLAYER_STATE1_TALKING)) &&
        !(player->stateFlags2 &
          (PLAYER_STATE2_CRAWLING | PLAYER_STATE2_HOPPING | PLAYER_STATE2_SPIN_ATTACKING | PLAYER_STATE2_SWORD_LUNGE |
           PLAYER_STATE2_OCARINA_PLAYING))) {
        target = 1.0f;
    }
    sPoseWeight = Lerp(sPoseWeight, target, 0.3f);

    // Viewmodel sway lags behind mouse turns and springs back
    float sway = Clampf(CfgSway(), 0.0f, 1.0f);
    Vec3f swayTarget = V3(Clampf(sYawVel * 0.0045f, -6.0f, 6.0f) * sway,
                          Clampf(-sPitchVel * 0.0045f, -6.0f, 6.0f) * sway, 0.0f);
    sSwayVel = Add(Scale(sSwayVel, 0.55f), Scale(Sub(swayTarget, sSwayOffset), 0.35f));
    sSwayOffset = Add(sSwayOffset, sSwayVel);
}

} // namespace

extern "C" s32 FirstPersonMod_UpdateCamera(void* cameraPtr, s32 funcIdx) {
    Camera* camera = (Camera*)cameraPtr;
    if (camera == nullptr || camera->thisIdx != CAM_ID_MAIN) {
        return false;
    }

    if (!CameraShouldBeFirstPerson(camera, funcIdx)) {
        if (sCamActive && camera->play != nullptr) {
            camera->play->view.zNear = 10.0f;
        }
        sCamActive = false;
        sCamInactiveFrames++;
        return false;
    }

    PlayState* play = camera->play;
    Player* player = (Player*)camera->player;

    // Coming back from a cutscene or menu: start looking the way Link faces.
    if (sCamInactiveFrames > 2) {
        sYaw = player->actor.shape.rot.y;
        sPitch = 0.0f;
        sHeadOffsetValid = false;
        sSmoothedStandOffset = V3(0, Player_GetHeight(player) * 0.85f, 0);
        ResetComfortEffects();
    }
    sCamInactiveFrames = 0;
    sCamActive = true;

    // Talking turns your head toward whoever is speaking
    if (IsTalking(player) && player->talkActor != nullptr) {
        SteerTowards(sEye, player->talkActor->focus.pos, 0.2f);
    }

    UpdateComfortEffects(player);
    UpdatePoseWeight(player);

    Vec3f eye = ComputeEye(player);

    // View angles with effects layered on
    float yaw = sYaw + sHitYaw;
    float pitch = sPitch + sLandPitch + sHitPitch;
    float roll = sStrafeTilt + sHitRoll;

    // Heavy setting: the view tumbles with Link's roll
    float spin = Clampf(CfgRollSpin(), 0.0f, 1.0f);
    if (spin > 0.0f && Player_FirstPersonMod_IsRolling(player) && player->skelAnime.endFrame > 0.0f) {
        float t = Clampf(player->skelAnime.curFrame / player->skelAnime.endFrame, 0.0f, 1.0f);
        float eased = t * t * (3.0f - 2.0f * t);
        float angle = eased * 65536.0f * spin;
        float rel = WrapBinang((float)player->actor.shape.rot.y - sYaw) * kBinangToRad;
        pitch -= angle * cosf(rel);
        roll += angle * sinf(rel);
    }

    // Build the view explicitly so pitches past straight up/down (the roll tumble) still work.
    float yr = yaw * kBinangToRad, pr = pitch * kBinangToRad;
    Vec3f fwd = V3(sinf(yr) * cosf(pr), sinf(pr), cosf(yr) * cosf(pr));
    Vec3f up = V3(-sinf(yr) * sinf(pr), cosf(pr), -cosf(yr) * sinf(pr));
    float rr = roll * kBinangToRad;
    Vec3f side = Cross(fwd, up);
    up = Add(Scale(up, cosf(rr)), Scale(side, sinf(rr)));

    camera->eye = eye;
    camera->eyeNext = eye;
    camera->at = Add(eye, Scale(fwd, 100.0f));
    camera->up = up;
    camera->paramFlags |= 4; // use camera->up as-is
    camera->roll = 0;
    camera->dist = 0.0f;

    float fov = Clampf(CfgFov(), 50.0f, 110.0f);
    if (player->unk_6AD == 2) {
        fov *= 0.85f; // a little zoom while aiming
    }
    camera->fov = fov;

    // The game reads the stick relative to this direction
    camera->inputDir.x = (s16)(-sPitch);
    camera->inputDir.y = (s16)sYaw;
    camera->inputDir.z = 0;

    // Bring the near clip in so Link's hands don't get cut off
    play->view.zNear = 3.0f;

    sEye = eye;
    sViewDir = DirFromAngles(sYaw, sPitch);
    sViewYaw = yaw;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Items: readying slots and the sword key
// ---------------------------------------------------------------------------------------------------------------------

namespace {

void RegisterItemHooks() {
    bool enabled = CfgEnabled();

    // Runs inside the game's item button processing, the one safe place to change what Link holds.
    COND_VB_SHOULD(VB_OVERRIDE_BUTTON_ITEM_USED, enabled, {
        s32* buttonIndex = va_arg(args, s32*);
        Player* player = va_arg(args, Player*);
        s32* item = va_arg(args, s32*);
        if (!IsLocalPlayer(player) || sMode != MODE_GAMEPLAY) {
            return;
        }

        if (sPendingSword) {
            sPendingSword = false;
            sPendingSwordTimer = 0;
            if (IsSwordAction(player->heldItemAction)) {
                // Sheathe
                Player_UseItem(gPlayState, player, ITEM_NONE);
                *item = ITEM_NONE;
            } else if (B_BTN_ITEM != ITEM_NONE) {
                // Draw the sword (putting away whatever else is in hand) without swinging it
                *buttonIndex = 0;
                *item = B_BTN_ITEM;
                sSuppressUseTimer = 20;
            }
            return;
        }

        if (sPendingPutAway) {
            sPendingPutAway = false;
            sPendingPutAwayTimer = 0;
            if (player->heldItemAction != PLAYER_IA_NONE) {
                Player_UseItem(gPlayState, player, ITEM_NONE);
                *item = ITEM_NONE;
            }
            return;
        }

        if (sPendingReadySlot != 0) {
            int slot = sPendingReadySlot;
            sPendingReadySlot = 0;
            sPendingReadyTimer = 0;
            s32 slotItem = SlotItem(slot);
            if (slotItem != ITEM_NONE && player->heldItemAction != Player_ItemToItemAction(slotItem)) {
                *buttonIndex = slot;
                *item = slotItem;
                sSuppressUseTimer = 20;
            }
        }
    });

    // Readying an item or drawing the sword shouldn't also use it.
    COND_VB_SHOULD(VB_USE_HELD_ITEM_AFTER_CHANGE, enabled, {
        Player* player = va_arg(args, Player*);
        if (IsLocalPlayer(player) && sSuppressUseTimer > 0 && sMode == MODE_GAMEPLAY) {
            *should = false;
        }
    });

    // First-person aiming (bow, slingshot, hookshot, boomerang): WASD keeps working and the mouse aims.
    COND_VB_SHOULD(VB_PLAYER_AIM_WITH_LEFT_STICK, enabled, {
        if (sMode == MODE_GAMEPLAY) {
            *should = false;
        }
    });

    COND_HOOK(OnPlayerFirstPersonControl, enabled, [](Player* player) {
        if (IsLocalPlayer(player) && sMode == MODE_GAMEPLAY && sCamActive) {
            player->actor.focus.rot.y = (s16)sYaw;
            player->actor.focus.rot.x = (s16)(-sPitch);
        }
    });

    COND_VB_SHOULD(VB_PLAYER_FIRST_PERSON_DECELERATE, enabled, {
        Player* player = va_arg(args, Player*);
        if (!IsLocalPlayer(player) || sMode != MODE_GAMEPLAY) {
            return;
        }
        f32 speedTarget;
        s16 yawTarget;
        u8 firstPersonMode = player->unk_6AD;
        player->unk_6AD = 0;
        bool moving = Player_GetMovementSpeedAndYaw(player, &speedTarget, &yawTarget, 0.018f, gPlayState);
        player->unk_6AD = firstPersonMode;

        if ((player->stateFlags1 & PLAYER_STATE1_IN_WATER) && (player->currentBoots != PLAYER_BOOTS_IRON)) {
            f32 swimSpeed = R_RUN_SPEED_LIMIT / 100.0f * 0.8f;
            speedTarget = CLAMP_MAX(speedTarget, swimSpeed);
        } else {
            speedTarget *= 0.6f; // aiming slows you down a bit
        }
        if (moving) {
            player->yaw = yawTarget;
        }
        func_8083DF68(player, speedTarget, player->yaw);
        if (!(player->stateFlags1 & PLAYER_STATE1_IN_WATER) && (player->linearVelocity > 0.0f)) {
            func_8084029C(player, (REG(30) / 1000.0f) + ((REG(32) / 1000.0f) * player->linearVelocity));
        }
        *should = false;
    });

    COND_VB_SHOULD(VB_PLAYER_FIRST_PERSON_ALIGN_YAW, enabled, {
        Player* player = va_arg(args, Player*);
        if (IsLocalPlayer(player) && sMode == MODE_GAMEPLAY && player->linearVelocity != 0.0f) {
            *should = false;
        }
    });

    // Hit shake
    COND_HOOK(OnPlayerHealthChange, enabled, [](int16_t amount) {
        if (amount < 0 && sCamActive) {
            float s = Clampf(CfgHitShake(), 0.0f, 1.0f);
            float m = Clampf(-amount / 16.0f, 0.5f, 3.0f);
            sHitPitch += (300.0f + Rand_ZeroOne() * 300.0f) * m * s;
            sHitRoll += (Rand_ZeroOne() - 0.5f) * 1400.0f * m * s;
            sHitYaw += (Rand_ZeroOne() - 0.5f) * 500.0f * m * s;
            sHitTimer = 8.0f;
        }
    });

    // Clean slate on new scenes / loads
    COND_HOOK(OnSceneSpawnActors, enabled, []() {
        sCamInactiveFrames = 1000;
        sHeadOffsetValid = false;
        sFocusPressed = false;
    });
}

static RegisterShipInitFunc sItemHooksInit(RegisterItemHooks, { CVAR_ENABLED });

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Drawing Link: hide the body, pose the arms as a viewmodel
// ---------------------------------------------------------------------------------------------------------------------

namespace {

Gfx sEmptyDL[] = { gsSPEndDisplayList() };

bool IsArmChainRoot(s32 limb) {
    return limb == PLAYER_LIMB_L_SHOULDER || limb == PLAYER_LIMB_R_SHOULDER;
}

bool ShouldHideLimb(s32 limb) {
    bool legs = CfgBodyMode() == 1;
    switch (limb) {
        case PLAYER_LIMB_WAIST:
        case PLAYER_LIMB_LOWER:
        case PLAYER_LIMB_R_THIGH:
        case PLAYER_LIMB_R_SHIN:
        case PLAYER_LIMB_R_FOOT:
        case PLAYER_LIMB_L_THIGH:
        case PLAYER_LIMB_L_SHIN:
        case PLAYER_LIMB_L_FOOT:
            return !legs;
        case PLAYER_LIMB_UPPER:
        case PLAYER_LIMB_HEAD:
        case PLAYER_LIMB_HAT:
        case PLAYER_LIMB_COLLAR:
        case PLAYER_LIMB_TORSO:
        case PLAYER_LIMB_SHEATH:
            return true;
        default:
            return false;
    }
}

// Applies a world-space transform W on top of the current limb matrix: M' = W * M.
// W rotates around the eye (so the arms move like they're attached to your view) and then offsets in view space.
void ApplyViewmodelTransform(Player* player) {
    MtxF current, rotY, rotP, rot, world, result;
    Matrix_Get(&current);

    // Align arms with the view yaw when Link's body faces elsewhere (aiming, swimming, turning)
    float yawDiff = WrapBinang(sViewYaw - (float)player->actor.shape.rot.y);
    // When Link is strafing he already faces the view; only nudge
    SetRotation(&rotY, V3(0, 1, 0), yawDiff * kBinangToRad);

    // Arms follow the look pitch, plus a raise into view when idle
    bool vanillaPitched = player->upperLimbRot.x != 0 || player->unk_6AD != 0;
    float pitchFollow = vanillaPitched ? 0.0f : Clampf(CfgVmPitchFollow(), 0.0f, 1.0f);
    float raise = CfgVmRaise() * kDegToBinang * sPoseWeight;
    float pitchAngle = (sPitch * pitchFollow + raise) * kBinangToRad;
    Vec3f right = RightFromYaw(sViewYaw);
    // right = forward x up, so a positive rotation around +right tips forward toward up
    SetRotation(&rotP, right, pitchAngle);

    SkinMatrix_MtxFMtxFMult(&rotP, &rotY, &rot);

    // Offset in view space: forward/height from the pose, plus sway and bob
    Vec3f fwd = DirFromAngles(sViewYaw, sPitch);
    Vec3f up = Norm(Cross(right, fwd));
    float bob = Clampf(CfgBob(), 0.0f, 1.0f) * sBobAmountSmoothed;
    Vec3f offset = Add(Scale(fwd, CfgVmForward() * sPoseWeight), Scale(up, CfgVmHeight() * sPoseWeight));
    offset = Add(offset, Scale(right, CfgVmSide() * sPoseWeight + sSwayOffset.x + sinf(sBobPhase) * 0.6f * bob));
    offset = Add(offset, Scale(up, sSwayOffset.y + sinf(sBobPhase * 2.0f) * 0.5f * bob));

    // W(v) = rot * (v - pivot) + pivot + offset
    Vec3f pivot = sEye;
    Vec3f rp = MtxRotate(&rot, pivot);
    world = rot;
    world.xw = pivot.x - rp.x + offset.x;
    world.yw = pivot.y - rp.y + offset.y;
    world.zw = pivot.z - rp.z + offset.z;
    world.wx = world.wy = world.wz = 0.0f;
    world.ww = 1.0f;

    SkinMatrix_MtxFMtxFMult(&world, &current, &result);
    Matrix_Put(&result);
}

} // namespace

extern "C" void FirstPersonMod_OverrideLimbDraw(PlayState* play, Player* player, s32 limbIndex, void** dListPtr,
                                                void* pos, void* rot) {
    if (!sCamActive || !IsLocalPlayer(player)) {
        return;
    }
    Gfx** dList = (Gfx**)dListPtr;

    if (limbIndex == PLAYER_LIMB_ROOT) {
        sDrawWorldPos = player->actor.world.pos;
        return;
    }

    if (ShouldHideLimb(limbIndex)) {
        // An empty list instead of NULL so the game still tracks this body part's position
        if (*dList != nullptr) {
            *dList = sEmptyDL;
        }
        return;
    }

    if (IsArmChainRoot(limbIndex)) {
        ApplyViewmodelTransform(player);
    }
}

extern "C" s32 FirstPersonMod_GetCosmeticHeldItem(Player* player) {
    if (!sCamActive || !IsLocalPlayer(player) || sMode != MODE_GAMEPLAY || sReadiedSlot == 0) {
        return -1;
    }
    s32 ia = Player_ItemToItemAction(SlotItem(sReadiedSlot));
    if (!IsBottleAction(ia) && !IsOcarinaAction(ia)) {
        return -1;
    }
    // Only while nothing else is in hand and the item isn't being used right now
    if ((player->heldItemAction != PLAYER_IA_NONE && player->heldItemAction != PLAYER_IA_SWORD_CS) ||
        player->itemAction != player->heldItemAction || player->unk_6AD != 0 ||
        (player->stateFlags1 & (PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_IN_CUTSCENE |
                                PLAYER_STATE1_SWINGING_BOTTLE)) ||
        (player->stateFlags2 & PLAYER_STATE2_OCARINA_PLAYING)) {
        return -1;
    }
    return ia;
}

extern "C" s32 FirstPersonMod_SnapStrafeYaw(Player* player) {
    return sCamActive && IsLocalPlayer(player) && sMode == MODE_GAMEPLAY && player->focusActor == nullptr;
}

extern "C" s32 FirstPersonMod_GetAimAngles(Player* player, s16* yaw, s16* pitch) {
    if (!sCamActive || !IsLocalPlayer(player) || sMode != MODE_GAMEPLAY || player->focusActor != nullptr) {
        return false;
    }
    *yaw = (s16)sYaw;
    *pitch = (s16)(-sPitch);
    return true;
}

// Head offset for the next camera update, taken right after Link is drawn.
static void CaptureHeadOffset() {
    Player* player = LocalPlayer();
    if (player == nullptr || !sCamActive) {
        return;
    }
    Vec3f offset = Sub(player->actor.focus.pos, player->actor.world.pos);
    // Ignore frames where the head position wasn't updated (Link not drawn, warping)
    if (Len(offset) < 120.0f) {
        sHeadOffset = offset;
        sHeadOffsetValid = true;
    }
}

extern "C" s32 FirstPersonMod_HideHeadAttachments(Player* player) {
    return sCamActive && IsLocalPlayer(player);
}

// ---------------------------------------------------------------------------------------------------------------------
// Ocarina "E" note glyph
// ---------------------------------------------------------------------------------------------------------------------

namespace {

alignas(8) u8 sOcarinaETex[16 * 16];
bool sOcarinaETexBuilt = false;

void BuildOcarinaETexture() {
    static const char* kGlyph[16] = {
        "................", "................", "....########....", "....########....",
        "....##..........", "....##..........", "....##..........", "....#######.....",
        "....#######.....", "....##..........", "....##..........", "....##..........",
        "....########....", "....########....", "................", "................",
    };
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            u8 value = 0x00;
            if (kGlyph[y][x] == '#') {
                value = 0xFF; // full intensity, opaque
            } else {
                // Dark outline around the letter, like the vanilla button glyphs
                bool nearFill = false;
                for (int oy = -1; oy <= 1 && !nearFill; oy++) {
                    for (int ox = -1; ox <= 1; ox++) {
                        int nx = x + ox, ny = y + oy;
                        if (nx >= 0 && nx < 16 && ny >= 0 && ny < 16 && kGlyph[ny][nx] == '#') {
                            nearFill = true;
                            break;
                        }
                    }
                }
                if (nearFill) {
                    value = 0x2C;
                }
            }
            sOcarinaETex[y * 16 + x] = value;
        }
    }
    sOcarinaETexBuilt = true;
}

} // namespace

extern "C" void* FirstPersonMod_GetOcarinaANoteTexture(void) {
    if (!CfgEnabled()) {
        return nullptr;
    }
    if (!sOcarinaETexBuilt) {
        BuildOcarinaETexture();
    }
    return sOcarinaETex;
}

// ---------------------------------------------------------------------------------------------------------------------
// Overlay: render-rate input sampling and the crosshair
// ---------------------------------------------------------------------------------------------------------------------

namespace {

class FirstPersonOverlay final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }

    void Draw() override {
        SampleInputRenderRate();
        DrawMaskVignette();
        DrawCrosshair();
    }

  private:
    // A dark frame around the screen while a mask is worn, so you can tell it's on
    void DrawMaskVignette() {
        if (!CfgEnabled() || !sCamActive || gPlayState == nullptr) {
            return;
        }
        Player* player = LocalPlayer();
        float target = (player != nullptr && player->currentMask != PLAYER_MASK_NONE)
                           ? Clampf(CVarGetFloat(FPS_CVAR("MaskVignette"), 0.85f), 0.0f, 1.0f)
                           : 0.0f;
        mVignette = Lerp(mVignette, target, 0.15f);
        if (mVignette < 0.01f) {
            return;
        }
        ImGuiWindow* game = ImGui::FindWindowByName("Main Game");
        if (game == nullptr) {
            return;
        }
        ImDrawList* draw = ImGui::GetForegroundDrawList(game->Viewport);
        ImVec2 a = game->Pos;
        ImVec2 b = ImVec2(game->Pos.x + game->Size.x, game->Pos.y + game->Size.y);
        float thick = std::min(game->Size.x, game->Size.y) * 0.28f;
        ImU32 dark = IM_COL32(0, 0, 0, (int)(235 * mVignette));
        ImU32 clear = IM_COL32(0, 0, 0, 0);
        // top, bottom, left, right gradients
        draw->AddRectFilledMultiColor(a, ImVec2(b.x, a.y + thick), dark, dark, clear, clear);
        draw->AddRectFilledMultiColor(ImVec2(a.x, b.y - thick), b, clear, clear, dark, dark);
        draw->AddRectFilledMultiColor(a, ImVec2(a.x + thick, b.y), dark, clear, clear, dark);
        draw->AddRectFilledMultiColor(ImVec2(b.x - thick, a.y), b, clear, dark, dark, clear);
    }

    float mVignette = 0.0f;

    void DrawCrosshair() {
        if (!CfgEnabled() || !CfgCrosshair() || !sCamActive || sMode != MODE_GAMEPLAY || gPlayState == nullptr) {
            return;
        }
        Player* player = LocalPlayer();
        if (player == nullptr || player->unk_6AD == 2) {
            return; // the vanilla aiming reticle takes over
        }
        ImGuiWindow* game = ImGui::FindWindowByName("Main Game");
        if (game == nullptr) {
            return;
        }
        ImVec2 center = ImVec2(game->Pos.x + game->Size.x * 0.5f, game->Pos.y + game->Size.y * 0.5f);
        ImDrawList* draw = ImGui::GetForegroundDrawList(game->Viewport);
        float scale = std::max(1.0f, game->Size.y / 720.0f);

        ImU32 color = IM_COL32(255, 255, 255, 200);
        Actor* target = gPlayState->actorCtx.targetCtx.arrowPointedActor;
        if (target != nullptr) {
            color = CHECK_FLAG_ALL(target->flags, ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE)
                        ? IM_COL32(255, 90, 70, 230)
                        : IM_COL32(120, 230, 140, 230);
        }
        ImU32 shadow = IM_COL32(0, 0, 0, 140);
        float gap = 4.0f * scale, len = 6.0f * scale, thick = 1.6f * scale;
        auto tick = [&](float dx, float dy) {
            ImVec2 a = ImVec2(center.x + dx * gap, center.y + dy * gap);
            ImVec2 b = ImVec2(center.x + dx * (gap + len), center.y + dy * (gap + len));
            draw->AddLine(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x + 1, b.y + 1), shadow, thick);
            draw->AddLine(a, b, color, thick);
        };
        tick(1, 0);
        tick(-1, 0);
        tick(0, 1);
        tick(0, -1);
        draw->AddCircleFilled(center, 1.4f * scale, color);
    }
};

std::shared_ptr<FirstPersonOverlay> sOverlay;

void RegisterOverlayAndDrawHooks() {
    COND_HOOK(OnPlayDrawEnd, CfgEnabled(), []() { CaptureHeadOffset(); });
}

static RegisterShipInitFunc sOverlayInit(RegisterOverlayAndDrawHooks, { CVAR_ENABLED });

} // namespace

// The overlay is created lazily from the game loop, once the GUI exists.
void EnsureOverlay() {
    if (sOverlay != nullptr) {
        return;
    }
    auto window = Ship::Context::GetRawInstance()->GetWindow();
    if (window == nullptr || window->GetGui() == nullptr) {
        return;
    }
    sOverlay = std::make_shared<FirstPersonOverlay>(FPS_CVAR("OverlayWindow"), true, "First Person Overlay");
    window->GetGui()->AddGuiWindow(sOverlay);
    sOverlay->Show();
}

void FirstPersonMod::OnEnabledChanged() {
    UpdateMouseCapture();
    sCamInactiveFrames = 1000;
    sReadiedSlot = 0;
}
