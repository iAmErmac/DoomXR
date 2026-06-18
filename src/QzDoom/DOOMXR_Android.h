#pragma once

#include <android/native_window.h>
#include <jni.h>

ANativeWindow* DOOMXR_GetNativeWindow();
JavaVM* DOOMXR_GetJavaVm();
jobject DOOMXR_GetActivityObject();
void DOOMXR_SetMetaKeyboardEnabled(bool enabled);
bool DOOMXR_IsMetaKeyboardEnabled();
void DOOMXR_SetTextInputActive(bool active);
bool DOOMXR_IsTextInputActive();
void DOOMXR_ShowTextInput();
void DOOMXR_HideTextInput();
