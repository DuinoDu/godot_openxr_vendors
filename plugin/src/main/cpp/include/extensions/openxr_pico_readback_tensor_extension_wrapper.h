/**************************************************************************/
/*  openxr_pico_readback_tensor_extension_wrapper.h                       */
/**************************************************************************/
/*                       This file is part of:                            */
/*                              GODOT XR                                  */
/*                      https://godotengine.org                           */
/**************************************************************************/
/* Copyright (c) 2022-present Godot XR contributors (see CONTRIBUTORS.md) */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#ifndef OPENXR_PICO_READBACK_TENSOR_EXTENSION_WRAPPER_H
#define OPENXR_PICO_READBACK_TENSOR_EXTENSION_WRAPPER_H

#include <godot_cpp/classes/open_xr_extension_wrapper_extension.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

#include <openxr/openxr.h>

#include "util.h"

#include <map>

#ifdef ANDROID_ENABLED
#define XR_USE_PLATFORM_ANDROID
// Select exactly one graphics API based on Godot build macros.
#if defined(VULKAN_ENABLED)
    #define XR_USE_GRAPHICS_API_VULKAN
    #include <vulkan/vulkan.h>
#elif defined(GLES3_ENABLED) || defined(OPENGL_ENABLED)
    #define XR_USE_GRAPHICS_API_OPENGL_ES
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
#endif
// Only include platform header if a graphics API is defined.
#if defined(XR_USE_GRAPHICS_API_VULKAN) || defined(XR_USE_GRAPHICS_API_OPENGL_ES)
    #include <openxr/openxr_platform.h>
#endif
#endif // ANDROID_ENABLED

// Some Pico readback types may not be present in the shipped headers.
// Define minimal shims if missing so we can still compile and fetch procs.
#ifndef XR_EXT_FUTURE_EXTENSION_NAME
#define XR_EXT_FUTURE_EXTENSION_NAME "XR_EXT_future"
#endif

#ifndef XR_PICO_READBACK_TENSOR_EXTENSION_NAME
#define XR_PICO_readback_tensor 1
#define XR_PICO_READBACK_TENSOR_EXTENSION_NAME "XR_PICO_readback_tensor"

#ifndef XR_TYPE_CREATE_BUFFER_FROM_GLOBAL_TENSOR_COMPLETION_PICO
#define XR_TYPE_CREATE_BUFFER_FROM_GLOBAL_TENSOR_COMPLETION_PICO ((XrStructureType)1010027001)
#endif
#ifndef XR_TYPE_CREATE_TEXTURE_FROM_GLOBAL_TENSOR_COMPLETION_PICO
#define XR_TYPE_CREATE_TEXTURE_FROM_GLOBAL_TENSOR_COMPLETION_PICO ((XrStructureType)1010027002)
#endif

// Readback buffer structs (CPU path)
typedef struct XrReadbackTensorBufferPICO {
    uint32_t bufferCapacityInput; // bytes available in buffer
    uint32_t bufferSizeOutput;    // bytes written by runtime
    void *buffer;                 // pointer to CPU buffer
} XrReadbackTensorBufferPICO;

#ifndef XrCreateBufferFromGlobalTensorCompletionPICO_DEFINED_GODOT_PICO
#define XrCreateBufferFromGlobalTensorCompletionPICO_DEFINED_GODOT_PICO 1
typedef struct XrCreateBufferFromGlobalTensorCompletionPICO {
    XrStructureType type;
    const void *next;
    XrResult futureResult;
    XrReadbackTensorBufferPICO *tensorBuffer;
} XrCreateBufferFromGlobalTensorCompletionPICO;
#endif

// Function pointer typedefs (CPU path)
#ifndef PFN_xrCreateBufferFromGlobalTensorAsyncPICO
typedef XrResult(XRAPI_PTR *PFN_xrCreateBufferFromGlobalTensorAsyncPICO)(XrSecureMrTensorPICO tensor, XrFutureEXT *future);
typedef XrResult(XRAPI_PTR *PFN_xrCreateBufferFromGlobalTensorCompletePICO)(XrSecureMrTensorPICO tensor, XrFutureEXT future, XrCreateBufferFromGlobalTensorCompletionPICO *completion);
#endif

#endif // XR_PICO_READBACK_TENSOR_EXTENSION_NAME

#ifndef XR_PICO_READBACK_TENSOR_VULKAN_EXTENSION_NAME
#define XR_PICO_readback_tensor_vulkan 1
#define XR_PICO_READBACK_TENSOR_VULKAN_EXTENSION_NAME "XR_PICO_readback_tensor_vulkan"
// Ensure VkImage type exists even if Vulkan headers are not included.
#if !defined(XR_USE_GRAPHICS_API_VULKAN)
typedef uint64_t VkImage;
#endif
#ifndef XR_TYPE_READBACK_TEXTURE_IMAGE_VULKAN_PICO
#define XR_TYPE_READBACK_TEXTURE_IMAGE_VULKAN_PICO ((XrStructureType)1010028000)
#endif
typedef struct XrReadbackTextureImageVulkanPICO {
    XrStructureType type;
    const void *next;
    VkImage image; // External image handle
} XrReadbackTextureImageVulkanPICO;
#endif

#ifndef XR_PICO_READBACK_TENSOR_OPENGLES_EXTENSION_NAME
#define XR_PICO_readback_tensor_opengles 1
#define XR_PICO_READBACK_TENSOR_OPENGLES_EXTENSION_NAME "XR_PICO_readback_tensor_opengles"
#ifndef XR_TYPE_READBACK_TEXTURE_IMAGE_OPENGL_PICO
#define XR_TYPE_READBACK_TEXTURE_IMAGE_OPENGL_PICO ((XrStructureType)1010029000)
#endif
typedef struct XrReadbackTextureImageOpenGLPICO {
    XrStructureType type;
    const void *next;
    uint32_t texId; // GL texture id
} XrReadbackTextureImageOpenGLPICO;
#endif

#ifndef XR_TYPE_FUTURE_POLL_INFO_EXT
#define XR_TYPE_FUTURE_POLL_INFO_EXT ((XrStructureType)1000469001)
#endif
#ifndef XR_TYPE_FUTURE_POLL_RESULT_EXT
#define XR_TYPE_FUTURE_POLL_RESULT_EXT ((XrStructureType)1000469003)
#endif

// Common GPU base type and handle
#ifndef XrReadbackTextureImageBasePICO_DEFINED_GODOT_PICO
#define XrReadbackTextureImageBasePICO_DEFINED_GODOT_PICO 1
typedef struct XrReadbackTextureImageBasePICO {
    XrStructureType type;
    const void *next;
} XrReadbackTextureImageBasePICO;
#endif

#ifndef XR_DEFINE_HANDLE
// Fallback if not defined by headers.
typedef struct XrReadbackTexturePICO_T *XrReadbackTexturePICO;
#else
XR_DEFINE_HANDLE(XrReadbackTexturePICO)
#endif

// Function pointer typedefs (GPU path)
#ifndef PFN_xrCreateTextureFromGlobalTensorAsyncPICO
typedef XrResult(XRAPI_PTR *PFN_xrCreateTextureFromGlobalTensorAsyncPICO)(XrSecureMrTensorPICO tensor, XrFutureEXT *future);
typedef XrResult(XRAPI_PTR *PFN_xrCreateTextureFromGlobalTensorCompletePICO)(XrSecureMrTensorPICO tensor, XrFutureEXT future, struct XrCreateTextureFromGlobalTensorCompletionPICO *completion);
typedef XrResult(XRAPI_PTR *PFN_xrGetReadbackTextureImagePICO)(XrReadbackTexturePICO readbackTexture, XrReadbackTextureImageBasePICO *img);
typedef XrResult(XRAPI_PTR *PFN_xrReleaseReadbackTexturePICO)(XrReadbackTexturePICO readbackTexture);
#endif

// Completion struct (GPU path)
#ifndef XrCreateTextureFromGlobalTensorCompletionPICO_DEFINED_GODOT_PICO
#define XrCreateTextureFromGlobalTensorCompletionPICO_DEFINED_GODOT_PICO 1
typedef struct XrCreateTextureFromGlobalTensorCompletionPICO {
    XrStructureType type;
    const void *next;
    XrResult futureResult;
    XrReadbackTexturePICO texture;
} XrCreateTextureFromGlobalTensorCompletionPICO;
#endif

namespace godot {

class OpenXRPicoSecureMR;

class OpenXRPicoReadbackTensorExtensionWrapper : public OpenXRExtensionWrapperExtension {
    GDCLASS(OpenXRPicoReadbackTensorExtensionWrapper, OpenXRExtensionWrapperExtension);

public:
    enum GraphicsAPI {
        GRAPHICS_API_UNKNOWN = 0,
        GRAPHICS_API_OPENGL,
        GRAPHICS_API_VULKAN,
        GRAPHICS_API_UNSUPPORTED,
    };

    static OpenXRPicoReadbackTensorExtensionWrapper *get_singleton();

    OpenXRPicoReadbackTensorExtensionWrapper();
    ~OpenXRPicoReadbackTensorExtensionWrapper();

    godot::Dictionary _get_requested_extensions() override;

    void _on_instance_created(uint64_t p_instance) override;
    void _on_instance_destroyed() override;

    // Capability
    bool is_readback_supported() const { return readback_cpu_ext; }
    bool is_gpu_readback_supported() const { return readback_vulkan_ext || readback_opengles_ext; }
    int32_t get_graphics_api() const;

    // CPU readback: returns raw bytes (width*height*channels) when available, empty otherwise
    PackedByteArray readback_global_tensor_cpu(uint64_t global_tensor_handle);

    // GPU readback via RD wrapping (Vulkan/OpenGLES): returns raw bytes (RGB/RGBA)
    PackedByteArray readback_global_tensor_gpu(uint64_t global_tensor_handle, int32_t width, int32_t height, int32_t channels);

    // Debug info
    Dictionary debug_info();

protected:
    static void _bind_methods();

private:
    friend class OpenXRPicoSecureMR;

    static OpenXRPicoReadbackTensorExtensionWrapper *singleton;

    std::map<String, bool *> request_extensions;

    bool readback_cpu_ext = false;
    bool readback_vulkan_ext = false;
    bool readback_opengles_ext = false;
    bool future_ext = false;

    XrInstance xr_instance = XR_NULL_HANDLE;

    // Function pointers
    EXT_PROTO_XRRESULT_FUNC2(xrCreateBufferFromGlobalTensorAsyncPICO,
            (XrSecureMrTensorPICO), tensor,
            (XrFutureEXT *), future_out);
    EXT_PROTO_XRRESULT_FUNC3(xrCreateBufferFromGlobalTensorCompletePICO,
            (XrSecureMrTensorPICO), tensor,
            (XrFutureEXT), future,
            (XrCreateBufferFromGlobalTensorCompletionPICO *), completion_out);

    EXT_PROTO_XRRESULT_FUNC2(xrCreateTextureFromGlobalTensorAsyncPICO,
            (XrSecureMrTensorPICO), tensor,
            (XrFutureEXT *), future_out);
    EXT_PROTO_XRRESULT_FUNC3(xrCreateTextureFromGlobalTensorCompletePICO,
            (XrSecureMrTensorPICO), tensor,
            (XrFutureEXT), future,
            (XrCreateTextureFromGlobalTensorCompletionPICO *), completion_out);

    EXT_PROTO_XRRESULT_FUNC2(xrGetReadbackTextureImagePICO,
            (XrReadbackTexturePICO), readback_texture,
            (XrReadbackTextureImageBasePICO *), image_out);
    EXT_PROTO_XRRESULT_FUNC1(xrReleaseReadbackTexturePICO,
            (XrReadbackTexturePICO), readback_texture);
    EXT_PROTO_XRRESULT_FUNC3(xrPollFutureEXT,
            (XrInstance), instance,
            (const XrFuturePollInfoEXT *), poll_info,
            (XrFuturePollResultEXT *), poll_result);

    GraphicsAPI _detect_graphics_api() const;
    bool _wait_for_future_ready(XrFutureEXT future, uint64_t timeout_us = 500000) const;
};

} // namespace godot

#endif // OPENXR_PICO_READBACK_TENSOR_EXTENSION_WRAPPER_H
