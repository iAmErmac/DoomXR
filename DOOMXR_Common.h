#pragma once

// Transitional wrapper header: keep the old TBXR implementation callable
// while DOOMXR becomes the primary mobile OpenXR-facing name in this project.
#include "../QzDoom/TBXR_Common.h"

#define DOOMXR_GetXrInstance TBXR_GetXrInstance
#define DOOMXR_GetTimeInMilliSeconds TBXR_GetTimeInMilliSeconds
#define DOOMXR_GetRefresh TBXR_GetRefresh
#define DOOMXR_Recenter TBXR_Recenter
#define DOOMXR_InitialiseOpenXR TBXR_InitialiseOpenXR
#define DOOMXR_WaitForSessionActive TBXR_WaitForSessionActive
#define DOOMXR_InitRenderer TBXR_InitRenderer
#define DOOMXR_EnterVR TBXR_EnterVR
#define DOOMXR_LeaveVR TBXR_LeaveVR
#define DOOMXR_GetScreenRes TBXR_GetScreenRes
#define DOOMXR_InitActions TBXR_InitActions
#define DOOMXR_Vibrate TBXR_Vibrate
#define DOOMXR_ProcessHaptics TBXR_ProcessHaptics
#define DOOMXR_FrameSetup TBXR_FrameSetup
#define DOOMXR_updateProjections TBXR_updateProjections
#define DOOMXR_UpdateControllers TBXR_UpdateControllers
#define DOOMXR_prepareEyeBuffer TBXR_prepareEyeBuffer
#define DOOMXR_finishEyeBuffer TBXR_finishEyeBuffer
#define DOOMXR_submitFrame TBXR_submitFrame
