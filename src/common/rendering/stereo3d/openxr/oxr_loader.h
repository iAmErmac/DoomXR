#pragma once

#include "i_module.h"
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h> // Required for IUnknown in openxr_platform.h
#define XR_USE_PLATFORM_WIN32
#elif defined(__ANDROID__)
#define XR_USE_PLATFORM_ANDROID
#include <jni.h>
#endif

#ifdef HAVE_VULKAN
// Include Vulkan headers before OpenXR platform headers
#include <zvulkan/vulkandevice.h>
#define XR_USE_GRAPHICS_API_VULKAN
#endif

// Only disable OpenXR prototypes when this build is using the dynamic loader wrappers.
#if defined(DYN_OPENXR) && !defined(XR_NO_PROTOTYPES)
#define XR_NO_PROTOTYPES
#endif

// OpenXR headers
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#ifdef DYN_OPENXR
extern FModule OpenXRModule;

#define OXR_PROC(name) extern TReqProc<OpenXRModule, PFN_##name> name;
#define OXR_OPT_PROC(name) extern TOptProc<OpenXRModule, PFN_##name> name;

#include "oxr_procs.h"

#undef OXR_PROC
#undef OXR_OPT_PROC
#endif

bool IsOpenXRPresent();

struct OpenXRVulkanBootstrapInfo
{
	bool available = false;
	bool supportsVulkanEnable = false;
	bool supportsVulkanEnable2 = false;
	uint64_t minApiVersionSupported = 0;
	uint64_t maxApiVersionSupported = 0;
	std::vector<std::string> requiredInstanceExtensions;
	std::vector<std::string> requiredDeviceExtensions;
};

bool QueryOpenXRVulkanBootstrapInfo(OpenXRVulkanBootstrapInfo& outInfo);
bool QueryOpenXRVulkanPreferredPhysicalDevice(VkInstance instance, VkPhysicalDevice& outPhysicalDevice);
