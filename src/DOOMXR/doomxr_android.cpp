#ifdef __ANDROID__

#define VK_USE_PLATFORM_ANDROID_KHR

#include "DOOMXR/VrCommon.h"
#include "common/rendering/stereo3d/openxr/oxr_loader.h"
#include "common/rendering/v_video.h"
#include "common/utility/m_argv.h"
#include "cmdlib.h"

#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <unistd.h>
#include <zvulkan/vulkansurface.h>
#include <zvulkan/volk/volk.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

void VR_DoomMain(int argc, char** argv);
const char* M_GetActiveProfile();
FArgs* Args = nullptr;
extern bool AppActive;
void S_SetSoundPaused(int state);

namespace
{
	struct DOOMXRAppState
	{
		std::thread MainThread;
		char* CommandLineBuffer = nullptr;
		std::vector<char*> Argv;
		ANativeWindow* NativeWindow = nullptr;
		uint32_t SurfaceChangeToken = 0;
		bool HasIWADs = false;
		bool HasLauncher = false;
		bool MainStarted = false;
	};

	JavaVM* gJavaVm = nullptr;
	jobject gCallbackObject = nullptr;
	jmethodID gShutdownMethod = nullptr;
	jmethodID gReloadMethod = nullptr;
	jmethodID gHapticEventMethod = nullptr;
	jmethodID gHapticStopEventMethod = nullptr;
	jmethodID gHapticEnableMethod = nullptr;
	jmethodID gHapticDisableMethod = nullptr;
	std::mutex gAndroidBridgeMutex;
	DOOMXRAppState* gActiveAppState = nullptr;

	JNIEnv* GetEnv(bool& didAttachThread)
	{
		didAttachThread = false;
		if (gJavaVm == nullptr)
			return nullptr;

		JNIEnv* env = nullptr;
		const jint getEnvResult = gJavaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
		if (getEnvResult == JNI_OK)
			return env;
		if (getEnvResult != JNI_EDETACHED)
			return nullptr;
		if (gJavaVm->AttachCurrentThread(&env, nullptr) != JNI_OK)
			return nullptr;
		didAttachThread = true;
		return env;
	}

	void ReleaseEnv(bool didAttachThread)
	{
		if (didAttachThread && gJavaVm != nullptr)
			gJavaVm->DetachCurrentThread();
	}

	void ClearCallbackObject()
	{
		bool didAttachThread = false;
		JNIEnv* env = GetEnv(didAttachThread);
		if (env != nullptr && gCallbackObject != nullptr)
			env->DeleteGlobalRef(gCallbackObject);
		ReleaseEnv(didAttachThread);

		gCallbackObject = nullptr;
		gShutdownMethod = nullptr;
		gReloadMethod = nullptr;
		gHapticEventMethod = nullptr;
		gHapticStopEventMethod = nullptr;
		gHapticEnableMethod = nullptr;
		gHapticDisableMethod = nullptr;
	}

	void CallVoidMethod(jmethodID method)
	{
		bool didAttachThread = false;
		JNIEnv* env = GetEnv(didAttachThread);
		if (env != nullptr && gCallbackObject != nullptr && method != nullptr)
			env->CallVoidMethod(gCallbackObject, method);
		ReleaseEnv(didAttachThread);
	}

	void CallStringVoidMethod(jmethodID method, const char* value)
	{
		bool didAttachThread = false;
		JNIEnv* env = GetEnv(didAttachThread);
		if (env == nullptr || gCallbackObject == nullptr || method == nullptr)
		{
			ReleaseEnv(didAttachThread);
			return;
		}

		jstring stringValue = env->NewStringUTF(value != nullptr ? value : "");
		env->CallVoidMethod(gCallbackObject, method, stringValue);
		env->DeleteLocalRef(stringValue);
		ReleaseEnv(didAttachThread);
	}

	void CallHapticEventMethod(const char* event, int position, int intensity, float angle, float yHeight)
	{
		bool didAttachThread = false;
		JNIEnv* env = GetEnv(didAttachThread);
		if (env == nullptr || gCallbackObject == nullptr || gHapticEventMethod == nullptr)
		{
			ReleaseEnv(didAttachThread);
			return;
		}

		jstring eventString = env->NewStringUTF(event != nullptr ? event : "");
		env->CallVoidMethod(gCallbackObject, gHapticEventMethod, eventString, position, intensity, angle, yHeight);
		env->DeleteLocalRef(eventString);
		ReleaseEnv(didAttachThread);
	}

	void SetAndroidAppActive(bool active)
	{
		AppActive = active;
		S_SetSoundPaused(active ? 1 : 0);
	}

	void StartMainThreadIfReady(DOOMXRAppState* appState, int argc)
	{
		if (appState == nullptr || appState->MainStarted || !appState->HasIWADs || !appState->HasLauncher || appState->NativeWindow == nullptr)
			return;

		appState->MainStarted = true;
		appState->MainThread = std::thread([appState, argc]()
		{
			VR_DoomMain(argc, appState->Argv.data());
		});
		appState->MainThread.detach();
	}

	void EnsureAndroidFullscreen()
	{
		vid_fullscreen = true;
	}

	void EnsureCommandLineHasFullscreen(DOOMXRAppState* appState)
	{
		if (appState == nullptr)
			return;

		for (size_t i = 0; i + 1 < appState->Argv.size(); ++i)
		{
			if (appState->Argv[i] != nullptr && stricmp(appState->Argv[i], "+vid_fullscreen") == 0)
			{
				appState->Argv[i + 1] = const_cast<char*>("1");
				return;
			}
		}

		if (!appState->Argv.empty() && appState->Argv.back() == nullptr)
			appState->Argv.pop_back();

		appState->Argv.push_back(const_cast<char*>("+vid_fullscreen"));
		appState->Argv.push_back(const_cast<char*>("1"));
		appState->Argv.push_back(nullptr);
	}

	void UnEscapeQuotes(char* arg)
	{
		char* last = nullptr;
		while (*arg)
		{
			if (*arg == '"' && last != nullptr && *last == '\\')
			{
				char* current = arg;
				char* previous = last;
				while (*current)
				{
					*previous = *current;
					previous = current;
					current++;
				}
				*previous = '\0';
			}
			last = arg;
			arg++;
		}
	}

	int ParseCommandLine(char* cmdline, char** argv)
	{
		char* cursor = cmdline;
		char* last = nullptr;
		int argc = 0;
		int lastArgc = 0;

		for (; *cursor; )
		{
			while (isspace(*cursor))
				++cursor;

			if (*cursor == '"')
			{
				last = nullptr;
				++cursor;
				if (*cursor)
				{
					if (argv)
						argv[argc] = cursor;
					++argc;
				}
				while (*cursor && (*cursor != '"' || (last != nullptr && *last == '\\')))
				{
					last = cursor;
					++cursor;
				}
			}
			else
			{
				if (*cursor)
				{
					if (argv)
						argv[argc] = cursor;
					++argc;
				}
				while (*cursor && !isspace(*cursor))
					++cursor;
			}

			if (*cursor)
			{
				if (argv)
					*cursor = '\0';
				++cursor;
			}

			if (argv && lastArgc != argc)
				UnEscapeQuotes(argv[lastArgc]);
			lastArgc = argc;
		}

		if (argv)
			argv[argc] = nullptr;
		return argc;
	}
}

int DOOMXR_SetRefreshRate(int refreshRate)
{
	return 0;
}

void DOOMXR_GetScreenRes(uint32_t* width, uint32_t* height)
{
	std::lock_guard<std::mutex> lock(gAndroidBridgeMutex);
	if (gActiveAppState == nullptr || gActiveAppState->NativeWindow == nullptr)
	{
		if (width != nullptr) *width = 0;
		if (height != nullptr) *height = 0;
		return;
	}

	if (width != nullptr) *width = static_cast<uint32_t>(ANativeWindow_getWidth(gActiveAppState->NativeWindow));
	if (height != nullptr) *height = static_cast<uint32_t>(ANativeWindow_getHeight(gActiveAppState->NativeWindow));
}

void DOOMXR_Vibrate(float duration, int channel, float intensity)
{
	if (duration <= 0.0f || intensity <= 0.0f)
	{
		VR_HapticStopEvent("fire_pistol");
		return;
	}

	VR_HapticEnable();
	const int position = std::clamp(channel + 1, 0, 2);
	const int scaledIntensity = std::clamp(static_cast<int>(intensity * 100.0f), 0, 100);
	CallHapticEventMethod("fire_pistol", position, scaledIntensity, 0.0f, 0.0f);
}

void DOOMXR_HapticEvent(const char* event, int position, int intensity, float angle, float yHeight)
{
	CallHapticEventMethod(event, position, intensity, angle, yHeight);
}

void DOOMXR_HapticStopEvent(const char* event)
{
	CallStringVoidMethod(gHapticStopEventMethod, event);
}

void DOOMXR_HapticEnable()
{
	CallVoidMethod(gHapticEnableMethod);
}

void DOOMXR_HapticDisable()
{
	CallVoidMethod(gHapticDisableMethod);
}

void DOOMXR_Restart()
{
	const char* profile = M_GetActiveProfile();
	CallStringVoidMethod(gReloadMethod, profile);
}

bool DOOMXR_GetVulkanDrawableSize(int* width, int* height)
{
	uint32_t screenWidth = 0;
	uint32_t screenHeight = 0;
	DOOMXR_GetScreenRes(&screenWidth, &screenHeight);
	if (width != nullptr) *width = static_cast<int>(screenWidth);
	if (height != nullptr) *height = static_cast<int>(screenHeight);
	return screenWidth > 0 && screenHeight > 0;
}

bool DOOMXR_GetVulkanPlatformExtensions(unsigned int* count, const char** names)
{
	static const char* extensions[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
	};
	static const unsigned int extensionCount = static_cast<unsigned int>(sizeof(extensions) / sizeof(extensions[0]));

	if (count == nullptr && names == nullptr)
		return false;
	if (names == nullptr)
	{
		*count = extensionCount;
		return true;
	}

	const bool result = *count >= extensionCount;
	*count = std::min<unsigned int>(*count, extensionCount);
	for (unsigned int i = 0; i < *count; ++i)
		names[i] = extensions[i];
	return result;
}

bool DOOMXR_CreateVulkanSurface(void* instance, void* surface)
{
	std::lock_guard<std::mutex> lock(gAndroidBridgeMutex);
	if (gActiveAppState == nullptr || gActiveAppState->NativeWindow == nullptr || instance == nullptr || surface == nullptr)
		return false;

	VkAndroidSurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
	createInfo.window = gActiveAppState->NativeWindow;
	VkInstance vkInstance = reinterpret_cast<VkInstance>(instance);
	if (volkGetLoadedInstance() != vkInstance)
	{
		volkLoadInstance(vkInstance);
	}

	auto createSurface = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
		vkGetInstanceProcAddr(vkInstance, "vkCreateAndroidSurfaceKHR"));
	if (createSurface == nullptr)
		return false;

	return createSurface(vkInstance, &createInfo, nullptr, reinterpret_cast<VkSurfaceKHR*>(surface)) == VK_SUCCESS;
}

uint32_t DOOMXR_GetSurfaceChangeToken()
{
	std::lock_guard<std::mutex> lock(gAndroidBridgeMutex);
	return gActiveAppState != nullptr ? gActiveAppState->SurfaceChangeToken : 0;
}

extern "C"
{
	JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*)
	{
		gJavaVm = vm;
		return JNI_VERSION_1_6;
	}

	JNIEXPORT jlong JNICALL Java_com_ermac_doomxr_GLES3JNILib_onCreate(JNIEnv* env, jclass, jobject activity, jstring commandLineParams, jboolean hasIWADs, jboolean hasLauncher)
	{
		EnsureAndroidFullscreen();

		auto* appState = new DOOMXRAppState();
		appState->HasIWADs = hasIWADs != 0;
		appState->HasLauncher = hasLauncher != 0;

		const char* commandLine = env->GetStringUTFChars(commandLineParams, nullptr);
		char* commandLineCopy = commandLine != nullptr ? strdup(commandLine) : strdup("doomxr");
		env->ReleaseStringUTFChars(commandLineParams, commandLine);

		appState->CommandLineBuffer = commandLineCopy;
		appState->Argv.resize(256, nullptr);
		const int argc = ParseCommandLine(commandLineCopy, appState->Argv.data());
		appState->Argv.resize(argc + 1);
		EnsureCommandLineHasFullscreen(appState);

		{
			std::lock_guard<std::mutex> lock(gAndroidBridgeMutex);
			gActiveAppState = appState;
		}

		{
			std::lock_guard<std::mutex> lock(gAndroidBridgeMutex);
			StartMainThreadIfReady(appState, argc);
		}

		return reinterpret_cast<jlong>(appState);
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_onStart(JNIEnv* env, jobject, jlong, jobject callbackObject)
	{
		ClearCallbackObject();
		gCallbackObject = env->NewGlobalRef(callbackObject);
		jclass callbackClass = env->GetObjectClass(gCallbackObject);
		gShutdownMethod = env->GetMethodID(callbackClass, "shutdown", "()V");
		gReloadMethod = env->GetMethodID(callbackClass, "reload", "(Ljava/lang/String;)V");
		gHapticEventMethod = env->GetMethodID(callbackClass, "haptic_event", "(Ljava/lang/String;IIFF)V");
		gHapticStopEventMethod = env->GetMethodID(callbackClass, "haptic_stopevent", "(Ljava/lang/String;)V");
		gHapticEnableMethod = env->GetMethodID(callbackClass, "haptic_enable", "()V");
		gHapticDisableMethod = env->GetMethodID(callbackClass, "haptic_disable", "()V");
		env->DeleteLocalRef(callbackClass);
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_onResume(JNIEnv*, jobject, jlong)
	{
		EnsureAndroidFullscreen();
		SetAndroidAppActive(true);
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_onPause(JNIEnv*, jobject, jlong)
	{
		SetAndroidAppActive(false);
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_onStop(JNIEnv*, jobject, jlong)
	{
		SetAndroidAppActive(false);
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_requestMenuOpen(JNIEnv*, jobject, jlong)
	{
		// Wrapper-side menu requests are handled in the wrapper project.
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_onDestroy(JNIEnv*, jobject, jlong handle)
	{
		auto* appState = reinterpret_cast<DOOMXRAppState*>(handle);
		{
			std::lock_guard<std::mutex> lock(gAndroidBridgeMutex);
			if (gActiveAppState == appState)
			{
				if (gActiveAppState->NativeWindow != nullptr)
				{
					ANativeWindow_release(gActiveAppState->NativeWindow);
					gActiveAppState->NativeWindow = nullptr;
				}
				gActiveAppState = nullptr;
			}
		}

		SetAndroidAppActive(false);

		ClearCallbackObject();

		if (appState != nullptr)
		{
			if (!appState->MainStarted)
			{
				free(appState->CommandLineBuffer);
				delete appState;
			}
		}
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_onSurfaceCreated(JNIEnv* env, jobject, jlong handle, jobject surface)
	{
		EnsureAndroidFullscreen();

		auto* appState = reinterpret_cast<DOOMXRAppState*>(handle);
		if (appState == nullptr)
			return;

		ANativeWindow* nativeWindow = ANativeWindow_fromSurface(env, surface);
		std::lock_guard<std::mutex> lock(gAndroidBridgeMutex);
		if (appState->NativeWindow != nullptr)
			ANativeWindow_release(appState->NativeWindow);
		appState->NativeWindow = nativeWindow;
		++appState->SurfaceChangeToken;
		StartMainThreadIfReady(appState, static_cast<int>(appState->Argv.size()) - 1);
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_onSurfaceChanged(JNIEnv* env, jobject, jlong handle, jobject surface)
	{
		Java_com_ermac_doomxr_GLES3JNILib_onSurfaceCreated(env, nullptr, handle, surface);
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_onSurfaceDestroyed(JNIEnv*, jobject, jlong handle)
	{
		auto* appState = reinterpret_cast<DOOMXRAppState*>(handle);
		if (appState == nullptr)
			return;

		std::lock_guard<std::mutex> lock(gAndroidBridgeMutex);
		if (appState->NativeWindow != nullptr)
		{
			ANativeWindow_release(appState->NativeWindow);
			appState->NativeWindow = nullptr;
			++appState->SurfaceChangeToken;
		}
	}

	JNIEXPORT void JNICALL Java_com_ermac_doomxr_GLES3JNILib_prepareEnvironment(JNIEnv* env, jclass, jstring path)
	{
		const char* nativePath = env->GetStringUTFChars(path, nullptr);
		if (nativePath != nullptr)
		{
			progdir = nativePath;
			chdir(nativePath);
			env->ReleaseStringUTFChars(path, nativePath);
		}
	}
}

#endif
