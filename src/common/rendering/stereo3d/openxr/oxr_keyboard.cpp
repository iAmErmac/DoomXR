#include "oxr_keyboard.h"

#include "common/engine/printf.h"

#include <cstring>
#include <vector>

namespace s3d {

namespace {

struct KeyboardTrackingState
{
	XrInstance instance = XR_NULL_HANDLE;
	XrSession session = XR_NULL_HANDLE;
	XrSpace keyboardSpace = XR_NULL_HANDLE;
	bool extensionAvailable = false;
	bool keyboardVisible = false;
	PFN_xrQuerySystemTrackedKeyboardFB xrQuerySystemTrackedKeyboardFB = nullptr;
	PFN_xrCreateKeyboardSpaceFB xrCreateKeyboardSpaceFB = nullptr;
};

KeyboardTrackingState g_keyboardState;

template <typename T>
bool LoadKeyboardProc(const char* name, T& outProc)
{
	if (g_keyboardState.instance == XR_NULL_HANDLE)
	{
		return false;
	}

	PFN_xrVoidFunction proc = nullptr;
	if (XR_FAILED(xrGetInstanceProcAddr(g_keyboardState.instance, name, &proc)) || proc == nullptr)
	{
		outProc = nullptr;
		return false;
	}

	outProc = reinterpret_cast<T>(proc);
	return true;
}

void ResetKeyboardSpace()
{
	if (g_keyboardState.keyboardSpace != XR_NULL_HANDLE)
	{
		xrDestroySpace(g_keyboardState.keyboardSpace);
		g_keyboardState.keyboardSpace = XR_NULL_HANDLE;
	}
	g_keyboardState.keyboardVisible = false;
}

} // namespace

bool HasKeyboardTrackingExtension()
{
	uint32_t extensionCount = 0;
	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr)))
	{
		return false;
	}

	std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data())))
	{
		return false;
	}

	for (const auto& ext : extensions)
	{
		if (strcmp(ext.extensionName, XR_FB_KEYBOARD_TRACKING_EXTENSION_NAME) == 0)
		{
			return true;
		}
	}

	return false;
}

bool InitializeKeyboardTracking(XrInstance instance, XrSession session)
{
	if (instance == XR_NULL_HANDLE || session == XR_NULL_HANDLE)
	{
		return false;
	}

	DestroyKeyboardTracking();

	g_keyboardState.instance = instance;
	g_keyboardState.session = session;

	if (!HasKeyboardTrackingExtension())
	{
		return false;
	}

	bool loaded = true;
	loaded &= LoadKeyboardProc("xrQuerySystemTrackedKeyboardFB", g_keyboardState.xrQuerySystemTrackedKeyboardFB);
	loaded &= LoadKeyboardProc("xrCreateKeyboardSpaceFB", g_keyboardState.xrCreateKeyboardSpaceFB);

	if (!loaded)
	{
		DestroyKeyboardTracking();
		return false;
	}

	g_keyboardState.extensionAvailable = true;
	return true;
}

bool ShowKeyboard()
{
	if (!g_keyboardState.extensionAvailable || g_keyboardState.session == XR_NULL_HANDLE)
	{
		return false;
	}

	if (g_keyboardState.keyboardVisible)
	{
		return true;
	}

	if (g_keyboardState.xrQuerySystemTrackedKeyboardFB == nullptr || g_keyboardState.xrCreateKeyboardSpaceFB == nullptr)
	{
		return false;
	}

	XrKeyboardTrackingQueryFB queryInfo{XR_TYPE_KEYBOARD_TRACKING_QUERY_FB};
	queryInfo.flags = XR_KEYBOARD_TRACKING_QUERY_LOCAL_BIT_FB;

	XrKeyboardTrackingDescriptionFB keyboardDescription{};
	XrResult result = g_keyboardState.xrQuerySystemTrackedKeyboardFB(
		g_keyboardState.session,
		&queryInfo,
		&keyboardDescription);
	if (XR_FAILED(result))
	{
		return false;
	}

	if (keyboardDescription.trackedKeyboardId == 0)
	{
		return false;
	}

	XrKeyboardSpaceCreateInfoFB spaceInfo{XR_TYPE_KEYBOARD_SPACE_CREATE_INFO_FB};
	spaceInfo.trackedKeyboardId = keyboardDescription.trackedKeyboardId;

	result = g_keyboardState.xrCreateKeyboardSpaceFB(
		g_keyboardState.session,
		&spaceInfo,
		&g_keyboardState.keyboardSpace);
	if (XR_FAILED(result))
	{
		return false;
	}

	g_keyboardState.keyboardVisible = true;
	return true;
}

bool HideKeyboard()
{
	if (!g_keyboardState.extensionAvailable)
	{
		return false;
	}

	ResetKeyboardSpace();
	return true;
}

bool IsKeyboardVisible()
{
	return g_keyboardState.extensionAvailable && g_keyboardState.keyboardVisible;
}

void DestroyKeyboardTracking()
{
	ResetKeyboardSpace();
	g_keyboardState = KeyboardTrackingState{};
}

} // namespace s3d
