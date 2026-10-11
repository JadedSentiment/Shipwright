#pragma once

// First Person Mod for Ship of Harkinian
//
// Turns Ocarina of Time into a mouse and keyboard first-person game:
// mouse look, WASD strafing, a split action key (E interact / Space roll),
// items fired with left click, crosshair targeting, viewmodel arms and
// comfort-first camera motion with intensity sliders.
//
// Everything here only changes how the *local* player is controlled, seen and
// drawn. Link's real animations, position and state are left vanilla, so Anchor
// peers keep seeing a normal third-person Link.

#include <libultraship/libultra/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;
struct Player;
struct Actor;

// Which half of the split A button a vanilla A-press check belongs to.
#define FPS_A_INTERACT 1 // E: talk, open, check, climb, grab, enter crawlspaces
#define FPS_A_MOVE 2     // Space: roll, sidehop, backflip, jump attack

// True when the mod is switched on in the menu.
s32 FirstPersonMod_IsEnabled(void);
// True when keyboard and mouse are driving the game (mod on, no SOH menu open).
s32 FirstPersonMod_ControlsActive(void);
// True when the first-person camera owned the view on the most recent camera update.
s32 FirstPersonMod_CameraActive(void);

// padmgr.c: replace controller port 1 with the keyboard and mouse layout.
void FirstPersonMod_ProcessInput(void* contPad);

// z_camera.c: compute the first-person view. Returns true if it replaced the vanilla camera function.
s32 FirstPersonMod_UpdateCamera(void* camera, s32 funcIdx); // Camera*

// z_player.c hooks. All of them only affect the real player, never Dark Link.
s32 FirstPersonMod_AllowAPress(struct Player* player, s32 kind);
s32 FirstPersonMod_AllowAHeld(struct Player* player, s32 kind);
s32 FirstPersonMod_ZTargetPress(struct Player* player, s32 vanillaPress);
void FirstPersonMod_UpdateParallel(struct Player* player);
s32 FirstPersonMod_RollInsteadOfHop(struct Player* player, s32 stickDirection);
s32 FirstPersonMod_BlockStandingRoll(struct Player* player);
void FirstPersonMod_OverrideZAimPitch(struct Player* player);

// z_actor.c: crosshair targeting. Returns a weight (smaller is better) or a negative value to skip the actor.
s32 FirstPersonMod_TargetingActive(void);
f32 FirstPersonMod_CrosshairWeight(struct PlayState* play, struct Actor* actor);

// z_player_lib.c: hide Link's body and pose the viewmodel arms for the local player.
void FirstPersonMod_OverrideLimbDraw(struct PlayState* play, struct Player* player, s32 limbIndex, void** dList,
                                     void* pos, void* rot);

// z_player.c: masks would sit around the camera, so they're skipped for the local first-person player.
s32 FirstPersonMod_HideHeadAttachments(struct Player* player);

// z_player_lib.c: a readied bottle / ocarina item action to show in Link's hand, or -1.
s32 FirstPersonMod_GetCosmeticHeldItem(struct Player* player);

// z_player.c: strafing starts straight sideways instead of curving in from forward.
s32 FirstPersonMod_SnapStrafeYaw(struct Player* player);
// z_player.c: aim direction for thrown items (boomerang). Returns false when the game should aim normally.
s32 FirstPersonMod_GetAimAngles(struct Player* player, s16* yaw, s16* pitch);

// z_message_PAL.c: texture for the A-button ocarina note ("E" glyph), or NULL to keep vanilla.
void* FirstPersonMod_GetOcarinaANoteTexture(void);

// Exported from z_player.c for the mod.
s32 Player_FirstPersonMod_IsMeleeAttacking(struct Player* player);
s32 Player_FirstPersonMod_IsRolling(struct Player* player);
void Player_FirstPersonMod_ReleaseLockOn(struct Player* player);
void Player_FirstPersonMod_SetParallel(struct Player* player);

#ifdef __cplusplus
}
#endif
