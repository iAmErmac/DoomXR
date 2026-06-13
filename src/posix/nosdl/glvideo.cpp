/*
** sdlglvideo.cpp
**
**---------------------------------------------------------------------------
** Copyright 2005-2016 Christoph Oelckers et.al.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

// HEADER FILES ------------------------------------------------------------

#include "doomtype.h"

#include "templates.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "v_video.h"
#include "stats.h"
#include "version.h"
#include "c_console.h"

#include "glvideo.h"
#include "gl_sysfb.h"
#include "gl_system.h"
#include "r_defs.h"

#include "gl_framebuffer.h"

#ifdef HAVE_GLES2
#include "gles_framebuffer.h"
#endif

#ifdef HAVE_VULKAN
#include "vulkan/system/vk_renderdevice.h"
#include "common/rendering/stereo3d/openxr/oxr_loader.h"
#include "common/rendering/hwrenderer/data/hw_vrmodes.h"
#include <zvulkan/vulkanbuilders.h>
#include <zvulkan/vulkandevice.h>
#include <zvulkan/vulkaninstance.h>
#include <zvulkan/vulkansurface.h>
#include <android/native_window.h>
#endif

#include "../../../../QzDoom/DOOMXR_Android.h"

// MACROS ------------------------------------------------------------------

// TYPES -------------------------------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

extern IVideo *Video;
// extern int vid_renderer;

EXTERN_CVAR (Float, Gamma)
EXTERN_CVAR (Int, vid_adapter)
EXTERN_CVAR (Int, vid_displaybits)
EXTERN_CVAR (Int, vid_renderer)
EXTERN_CVAR (Int, vid_maxfps)
EXTERN_CVAR (Int, vid_defwidth)
EXTERN_CVAR (Int, vid_defheight)
EXTERN_CVAR (Int, vid_refreshrate)
EXTERN_CVAR (Int, vid_preferbackend)
EXTERN_CVAR (Bool, cl_capfps)
EXTERN_CVAR (Bool, vk_debug)

#ifdef HAVE_VULKAN
EXTERN_CVAR (Int, vr_mode)
#endif


DFrameBuffer *CreateGLSWFrameBuffer(int width, int height, bool bgra, bool fullscreen);

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CUSTOM_CVAR(Bool, gl_debug, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}
#ifdef __arm__
CUSTOM_CVAR(Bool, gl_es, false, CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}
#else
CUSTOM_CVAR(Bool, gl_es, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}
#endif

// PRIVATE DATA DEFINITIONS ------------------------------------------------

// CODE --------------------------------------------------------------------

NoSDLGLVideo::NoSDLGLVideo (int parm)
{
	IteratorBits = 0;
}

NoSDLGLVideo::~NoSDLGLVideo ()
{
}

int TBXR_GetRefresh();

#ifdef HAVE_VULKAN
static bool I_GetVulkanPlatformExtensions(unsigned int* count, const char** names)
{
	static const char* const extensions[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
	};
	static constexpr unsigned int extensionCount = 2;

	if (count == nullptr)
	{
		return false;
	}

	if (names == nullptr)
	{
		*count = extensionCount;
		return true;
	}

	if (*count < extensionCount)
	{
		return false;
	}

	for (unsigned int i = 0; i < extensionCount; i++)
	{
		names[i] = extensions[i];
	}
	*count = extensionCount;
	return true;
}

static bool I_CreateVulkanSurface(VkInstance instance, VkSurfaceKHR* surface)
{
	ANativeWindow* nativeWindow = nullptr;
	for (int waitedMs = 0; waitedMs <= 5000; waitedMs += 10)
	{
		nativeWindow = DOOMXR_GetNativeWindow();
		if (nativeWindow != nullptr)
		{
			break;
		}

		if (waitedMs < 5000)
		{
			usleep(10 * 1000);
		}
	}

	if (surface == nullptr || nativeWindow == nullptr)
	{
		Printf(TEXTCOLOR_RED "I_CreateVulkanSurface: Android native window was not ready\n");
		return false;
	}

	ANativeWindow_acquire(nativeWindow);
	const int nativeWidth = ANativeWindow_getWidth(nativeWindow);
	const int nativeHeight = ANativeWindow_getHeight(nativeWindow);
	const int nativeFormat = ANativeWindow_getFormat(nativeWindow);
	Printf("I_CreateVulkanSurface: using native window %p size=%dx%d format=%d\n",
		nativeWindow,
		nativeWidth,
		nativeHeight,
		nativeFormat);

	VkAndroidSurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
	createInfo.window = nativeWindow;

	auto createAndroidSurface = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
		vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
	Printf("I_CreateVulkanSurface: vkCreateAndroidSurfaceKHR global=%p local=%p\n",
		vkCreateAndroidSurfaceKHR,
		createAndroidSurface);
	if (createAndroidSurface == nullptr)
	{
		ANativeWindow_release(nativeWindow);
		Printf(TEXTCOLOR_RED "I_CreateVulkanSurface: vkGetInstanceProcAddr could not resolve vkCreateAndroidSurfaceKHR\n");
		return false;
	}

	const VkResult result = createAndroidSurface(instance, &createInfo, nullptr, surface);
	ANativeWindow_release(nativeWindow);
	if (result != VK_SUCCESS)
	{
		Printf(TEXTCOLOR_RED "I_CreateVulkanSurface: vkCreateAndroidSurfaceKHR failed (%d)\n", int(result));
		return false;
	}

	return true;
}
#endif

DFrameBuffer *NoSDLGLVideo::CreateFrameBuffer ()
{
	DFrameBuffer* fb = nullptr;
#ifdef HAVE_VULKAN
	FString vulkanFailureReason;
#endif

#ifdef HAVE_VULKAN
	const bool preferVulkan =
#ifdef __MOBILE__
		vr_mode == VR_OPENXR_MOBILE ||
#endif
		V_GetBackend() == 1;

	if (preferVulkan)
	{
		try
		{
			Printf("NoSDLGLVideo: attempting Vulkan framebuffer for backend=%d vr_mode=%d\n",
				int(V_GetBackend()),
				int(vr_mode));

			unsigned int count = 8;
			const char* names[8];
			if (!I_GetVulkanPlatformExtensions(&count, names))
			{
				VulkanError("I_GetVulkanPlatformExtensions failed");
			}

			VulkanInstanceBuilder builder;
			builder.DebugLayer(vk_debug);
			for (unsigned int i = 0; i < count; i++)
			{
				builder.RequireExtension(names[i]);
			}

			if (vr_mode == VR_OPENXR_MOBILE)
			{
				OpenXRVulkanBootstrapInfo xrInfo;
				if (QueryOpenXRVulkanBootstrapInfo(xrInfo))
				{
					Printf("NoSDLGLVideo: OpenXR Vulkan bootstrap ready with %d instance extensions and %d device extensions\n",
						int(xrInfo.requiredInstanceExtensions.size()),
						int(xrInfo.requiredDeviceExtensions.size()));
					for (const auto& ext : xrInfo.requiredInstanceExtensions)
					{
						builder.RequireExtension(ext);
					}

					std::vector<uint32_t> apiVersions = { VK_API_VERSION_1_2, VK_API_VERSION_1_1, VK_API_VERSION_1_0 };
					if (xrInfo.minApiVersionSupported != 0 || xrInfo.maxApiVersionSupported != 0)
					{
						std::vector<uint32_t> filtered;
						for (uint32_t version : apiVersions)
						{
							const uint64_t v = version;
							if ((xrInfo.minApiVersionSupported == 0 || v >= xrInfo.minApiVersionSupported) &&
								(xrInfo.maxApiVersionSupported == 0 || v <= xrInfo.maxApiVersionSupported))
							{
								filtered.push_back(version);
							}
						}
						if (!filtered.empty())
						{
							builder.ApiVersionsToTry(filtered);
						}
					}
				}
				else
				{
					Printf(TEXTCOLOR_RED "NoSDLGLVideo: OpenXR Vulkan bootstrap query failed\n");
				}
			}

			auto instance = builder.Create();
			Printf("NoSDLGLVideo: Vulkan instance created\n");

			VkSurfaceKHR surfaceHandle = VK_NULL_HANDLE;
			if (!I_CreateVulkanSurface(instance->Instance, &surfaceHandle))
			{
				VulkanError("I_CreateVulkanSurface failed");
			}
			Printf("NoSDLGLVideo: Vulkan surface created\n");

			auto surface = std::make_shared<VulkanSurface>(instance, surfaceHandle);
			Printf("NoSDLGLVideo: constructing VulkanRenderDevice\n");
			fb = new VulkanRenderDevice(nullptr, true, surface);
			Printf("NoSDLGLVideo: VulkanRenderDevice constructed\n");
		}
		catch (const CVulkanError& error)
		{
			vulkanFailureReason = error.what();
			Printf(TEXTCOLOR_RED "Initialization of Vulkan failed: %s\n", error.what());
		}
		catch (const std::exception& error)
		{
			vulkanFailureReason = error.what();
			Printf(TEXTCOLOR_RED "Initialization of Vulkan threw std::exception: %s\n", error.what());
		}
		catch (...)
		{
			vulkanFailureReason = "unknown exception";
			Printf(TEXTCOLOR_RED "Initialization of Vulkan threw an unknown exception\n");
		}
	}

#ifdef __MOBILE__
	if (fb == nullptr && vr_mode == VR_OPENXR_MOBILE)
	{
		if (vulkanFailureReason.IsEmpty())
		{
			vulkanFailureReason = "framebuffer creation returned null without an exception";
		}

		I_FatalError("Quest mobile Vulkan/OpenXR initialization failed: %s\n"
			"See earlier Vulkan/OpenXR log lines for the failing stage.\n"
			"The engine will not fall back to GL for vr_mode 15 on headset.",
			vulkanFailureReason.GetChars());
	}
#endif
#endif

	if (fb == nullptr)
	{
		Printf("NoSDLGLVideo: falling back to GL framebuffer for backend=%d\n", int(V_GetBackend()));
#ifdef HAVE_GLES2
		if (V_GetBackend() != 0)
		{
			fb = new OpenGLESRenderer::OpenGLFrameBuffer(0, true);
		}
		else
#endif
		{
			fb = new OpenGLRenderer::OpenGLFrameBuffer(0, true);
		}
	}

	return fb;
}

void NoSDLGLVideo::SetWindowedScale (float scale)
{
}

//==========================================================================
//
// 
//
//==========================================================================
#ifdef __MOBILE__
extern "C" int glesLoad;
#endif

void NoSDLGLVideo::SetupPixelFormat(bool allowsoftware, int multisample, const int *glver)
{
		
#ifdef __MOBILE__

	int major,min;

	const char *version = Args->CheckValue("-glversion");
	if( !strcmp(version, "gles1") )
	{
		glesLoad = 1;
		major = 1;
		min = 0;
	}
	else if ( !strcmp(version, "gles2") )
	{
		glesLoad = 2;
        major = 2;
        min = 0;
	}
    else if ( !strcmp(version, "gles3") )
	{
		glesLoad = 3;
		major = 3;
		min = 1;
	}
#endif

}


IVideo *gl_CreateVideo()
{
	return new NoSDLGLVideo(0);
}


// FrameBuffer implementation -----------------------------------------------

SystemBaseFrameBuffer::SystemBaseFrameBuffer(void*, bool)
	: DFrameBuffer(vid_defwidth, vid_defheight)
{
}

bool SystemBaseFrameBuffer::IsFullscreen()
{
	return true;
}

int SystemBaseFrameBuffer::GetClientWidth()
{
	uint32_t w, h;
	QzDoom_GetScreenRes(&w, &h);
	return int(w);
}

int SystemBaseFrameBuffer::GetClientHeight()
{
	uint32_t w, h;
	QzDoom_GetScreenRes(&w, &h);
	return int(h);
}

void SystemBaseFrameBuffer::ToggleFullscreen(bool)
{
}

void SystemBaseFrameBuffer::SetWindowSize(int, int)
{
}

SystemGLFrameBuffer::SystemGLFrameBuffer (void *, bool fullscreen)
	: SystemBaseFrameBuffer(nullptr, fullscreen)
{
}

SystemGLFrameBuffer::~SystemGLFrameBuffer ()
{
}


void SystemGLFrameBuffer::InitializeState() 
{
}

bool SystemGLFrameBuffer::IsFullscreen ()
{
	return Super::IsFullscreen();
}

void SystemGLFrameBuffer::SetVSync( bool vsync )
{
}

int QzDoom_SetRefreshRate(int refreshRate);

void SystemGLFrameBuffer::NewRefreshRate ()
{
	if (QzDoom_SetRefreshRate(vid_refreshrate) != 0) {
		Printf("Failed to set refresh rate to %dHz.\n", *vid_refreshrate);
	}
}

void SystemGLFrameBuffer::SwapBuffers()
{
	//No swapping required
}

int SystemGLFrameBuffer::GetClientWidth()
{
	return Super::GetClientWidth();
}

int SystemGLFrameBuffer::GetClientHeight()
{
	return Super::GetClientHeight();
}


// each platform has its own specific version of this function.
void I_SetWindowTitle(const char* caption)
{
}

void I_FocusWindow()
{
}
