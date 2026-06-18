#pragma once

#include "oxr_loader.h"

namespace s3d {

bool HasKeyboardTrackingExtension();
bool InitializeKeyboardTracking(XrInstance instance, XrSession session);
bool ShowKeyboard();
bool HideKeyboard();
bool IsKeyboardVisible();
void DestroyKeyboardTracking();

} // namespace s3d
