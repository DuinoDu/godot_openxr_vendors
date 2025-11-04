/**************************************************************************/
/*  openxr_pico_readback_tensor_extension_wrapper.cpp                     */
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

#include "extensions/openxr_pico_readback_tensor_extension_wrapper.h"

#include <godot_cpp/classes/open_xrapi_extension.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/engine.hpp>

#include <chrono>
#include <thread>

using namespace godot;

OpenXRPicoReadbackTensorExtensionWrapper *OpenXRPicoReadbackTensorExtensionWrapper::singleton = nullptr;

OpenXRPicoReadbackTensorExtensionWrapper *OpenXRPicoReadbackTensorExtensionWrapper::get_singleton() {
    if (singleton == nullptr) {
        singleton = memnew(OpenXRPicoReadbackTensorExtensionWrapper());
    }
    return singleton;
}

OpenXRPicoReadbackTensorExtensionWrapper::OpenXRPicoReadbackTensorExtensionWrapper() {
    ERR_FAIL_COND_MSG(singleton != nullptr, "An OpenXRPicoReadbackTensorExtensionWrapper singleton already exists.");

    request_extensions[XR_PICO_READBACK_TENSOR_EXTENSION_NAME] = &readback_cpu_ext;
    request_extensions[XR_EXT_FUTURE_EXTENSION_NAME] = &future_ext;

    // GPU readback extension depends on active rendering backend.
    GraphicsAPI api = _detect_graphics_api();
    if (api == GRAPHICS_API_VULKAN) {
        request_extensions[XR_PICO_READBACK_TENSOR_VULKAN_EXTENSION_NAME] = &readback_vulkan_ext;
    } else if (api == GRAPHICS_API_OPENGL) {
        request_extensions[XR_PICO_READBACK_TENSOR_OPENGLES_EXTENSION_NAME] = &readback_opengles_ext;
    }

    UtilityFunctions::print("[PicoReadback] Wrapper constructed. Requesting extensions: ",
            String(
                XR_PICO_READBACK_TENSOR_EXTENSION_NAME) + String(
                ", ") + String(
                XR_PICO_READBACK_TENSOR_VULKAN_EXTENSION_NAME) + String(
                ", ") + String(
                XR_PICO_READBACK_TENSOR_OPENGLES_EXTENSION_NAME) + String(
                ", ") + String(
                XR_EXT_FUTURE_EXTENSION_NAME));

    singleton = this;
}

OpenXRPicoReadbackTensorExtensionWrapper::~OpenXRPicoReadbackTensorExtensionWrapper() {
    singleton = nullptr;
}

void OpenXRPicoReadbackTensorExtensionWrapper::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_readback_supported"), &OpenXRPicoReadbackTensorExtensionWrapper::is_readback_supported);
    ClassDB::bind_method(D_METHOD("is_gpu_readback_supported"), &OpenXRPicoReadbackTensorExtensionWrapper::is_gpu_readback_supported);
    ClassDB::bind_method(D_METHOD("get_graphics_api"), &OpenXRPicoReadbackTensorExtensionWrapper::get_graphics_api);

    ClassDB::bind_method(D_METHOD("readback_global_tensor_cpu", "global_tensor_handle"), &OpenXRPicoReadbackTensorExtensionWrapper::readback_global_tensor_cpu);
    ClassDB::bind_method(D_METHOD("readback_global_tensor_gpu", "global_tensor_handle", "width", "height", "channels"), &OpenXRPicoReadbackTensorExtensionWrapper::readback_global_tensor_gpu);

    ClassDB::bind_method(D_METHOD("debug_info"), &OpenXRPicoReadbackTensorExtensionWrapper::debug_info);
}

Dictionary OpenXRPicoReadbackTensorExtensionWrapper::_get_requested_extensions() {
    Dictionary result;
    for (auto &kv : request_extensions) {
        String key = kv.first;
        uint64_t value = reinterpret_cast<uint64_t>(kv.second);
        result[key] = (Variant)value;
    }
    UtilityFunctions::print("[PicoReadback] Requesting extensions count: ", (int)result.size());
    return result;
}

void OpenXRPicoReadbackTensorExtensionWrapper::_on_instance_created(uint64_t p_instance) {
    xr_instance = (XrInstance)p_instance;

    if (readback_cpu_ext) {
        GDEXTENSION_INIT_XR_FUNC(xrCreateBufferFromGlobalTensorAsyncPICO);
        GDEXTENSION_INIT_XR_FUNC(xrCreateBufferFromGlobalTensorCompletePICO);
    }
    if (readback_vulkan_ext || readback_opengles_ext) {
        GDEXTENSION_INIT_XR_FUNC(xrCreateTextureFromGlobalTensorAsyncPICO);
        GDEXTENSION_INIT_XR_FUNC(xrCreateTextureFromGlobalTensorCompletePICO);
        GDEXTENSION_INIT_XR_FUNC(xrGetReadbackTextureImagePICO);
        GDEXTENSION_INIT_XR_FUNC(xrReleaseReadbackTexturePICO);
    }
    if (future_ext) {
        GDEXTENSION_INIT_XR_FUNC(xrPollFutureEXT);
    }

    UtilityFunctions::print("[PicoReadback] OpenXR instance created. CPU:", readback_cpu_ext, ", Vulkan:", readback_vulkan_ext, ", GLES:", readback_opengles_ext);
}

void OpenXRPicoReadbackTensorExtensionWrapper::_on_instance_destroyed() {
    xr_instance = XR_NULL_HANDLE;
}

int32_t OpenXRPicoReadbackTensorExtensionWrapper::get_graphics_api() const { return (int32_t)_detect_graphics_api(); }

OpenXRPicoReadbackTensorExtensionWrapper::GraphicsAPI OpenXRPicoReadbackTensorExtensionWrapper::_detect_graphics_api() const {
    OS *os = OS::get_singleton();
    if (os && os->has_feature("headless")) {
        return GRAPHICS_API_UNKNOWN;
    }

    Engine *engine = Engine::get_singleton();
    if (!engine || !engine->has_singleton("RenderingServer")) {
        return GRAPHICS_API_UNKNOWN;
    }

    RenderingServer *rs = RenderingServer::get_singleton();
    if (!rs) return GRAPHICS_API_UNKNOWN;
    String rendering_driver = rs->get_current_rendering_driver_name();
    if (rendering_driver.contains("opengl")) {
        return GRAPHICS_API_OPENGL;
    } else if (rendering_driver == "vulkan") {
        return GRAPHICS_API_VULKAN;
    }
    return GRAPHICS_API_UNSUPPORTED;
}

PackedByteArray OpenXRPicoReadbackTensorExtensionWrapper::readback_global_tensor_cpu(uint64_t global_tensor_handle) {
    PackedByteArray out;
    ERR_FAIL_COND_V_MSG(!readback_cpu_ext, out, "Pico readback CPU extension not available");
    ERR_FAIL_COND_V_MSG(xrCreateBufferFromGlobalTensorAsyncPICO_ptr == nullptr || xrCreateBufferFromGlobalTensorCompletePICO_ptr == nullptr, out, "Readback CPU functions not loaded");

    XrSecureMrTensorPICO tensor = (XrSecureMrTensorPICO)global_tensor_handle;

    XrFutureEXT future = 0;
    XrResult r = xrCreateBufferFromGlobalTensorAsyncPICO(tensor, &future);
    if (XR_FAILED(r)) {
        UtilityFunctions::printerr("[PicoReadback] xrCreateBufferFromGlobalTensorAsyncPICO failed, ret=", (int)r);
        return out;
    }

    XrCreateBufferFromGlobalTensorCompletionPICO completion;
    XrReadbackTensorBufferPICO buf = {};
    buf.bufferCapacityInput = 0;
    completion.tensorBuffer = &buf;

    XrResult ret = xrCreateBufferFromGlobalTensorCompletePICO(tensor, future, &completion); 
    if (ret == XR_SUCCESS) {
      completion.tensorBuffer->bufferCapacityInput = completion.tensorBuffer->bufferSizeOutput;
      completion.tensorBuffer->buffer = new char[completion.tensorBuffer->bufferCapacityInput];
      UtilityFunctions::print("[PicoReadback] 2, bufferCapacityInput: ", completion.tensorBuffer->bufferCapacityInput);
      ret = xrCreateBufferFromGlobalTensorCompletePICO(tensor, future, &completion);
      if (ret == XR_SUCCESS) {
        out.resize(buf.bufferSizeOutput);
        buf.bufferCapacityInput = buf.bufferSizeOutput;
        buf.buffer = out.ptrw();
      } else {
        UtilityFunctions::print("[PicoReadback] xrCreateBufferFromGlobalTensorCompletePICO_2 failed, ret=", (int)ret);
      }
    } else {
      UtilityFunctions::print("[PicoReadback] xrCreateBufferFromGlobalTensorCompletePICO_1 failed, ret=", (int)ret);
    }
    return out;
}

PackedByteArray OpenXRPicoReadbackTensorExtensionWrapper::readback_global_tensor_gpu(uint64_t global_tensor_handle, int32_t width, int32_t height, int32_t channels) {
    PackedByteArray out;
    ERR_FAIL_COND_V_MSG(!(readback_vulkan_ext || readback_opengles_ext), out, "Pico readback GPU extension not available");
    ERR_FAIL_COND_V_MSG(xrCreateTextureFromGlobalTensorAsyncPICO_ptr == nullptr || xrCreateTextureFromGlobalTensorCompletePICO_ptr == nullptr || xrGetReadbackTextureImagePICO_ptr == nullptr || xrReleaseReadbackTexturePICO_ptr == nullptr, out, "Readback GPU functions not loaded");

    XrSecureMrTensorPICO tensor = (XrSecureMrTensorPICO)global_tensor_handle;

    XrFutureEXT future = 0;
    XrResult r = xrCreateTextureFromGlobalTensorAsyncPICO(tensor, &future);
    if (XR_FAILED(r)) {
        UtilityFunctions::printerr("[PicoReadback] xrCreateTextureFromGlobalTensorAsyncPICO failed: ", (int)r);
        return out;
    }

    XrCreateTextureFromGlobalTensorCompletionPICO completion = {};
    completion.type = XR_TYPE_CREATE_TEXTURE_FROM_GLOBAL_TENSOR_COMPLETION_PICO;
    completion.next = nullptr;
    completion.futureResult = XR_SUCCESS;
    completion.texture = XR_NULL_HANDLE;

    auto complete_texture = [&](const char *label) -> bool {
        completion.futureResult = XR_SUCCESS;
        XrResult cr = xrCreateTextureFromGlobalTensorCompletePICO(tensor, future, &completion);
        if (cr == XR_ERROR_FUTURE_PENDING_EXT) {
            _wait_for_future_ready(future);
            completion.futureResult = XR_SUCCESS;
            cr = xrCreateTextureFromGlobalTensorCompletePICO(tensor, future, &completion);
            if (cr == XR_ERROR_FUTURE_PENDING_EXT) {
                UtilityFunctions::printerr("[PicoReadback] Future still pending during ", label, ". Run the producing pipeline before requesting readback.");
                return false;
            }
        }
        if (cr != XR_SUCCESS) {
            UtilityFunctions::printerr("[PicoReadback] Complete texture (", label, ") failed: ", (int)cr);
            return false;
        }
        if (completion.futureResult != XR_SUCCESS) {
            UtilityFunctions::printerr("[PicoReadback] Future result for ", label, " is not XR_SUCCESS: ", (int)completion.futureResult);
            return false;
        }
        return true;
    };

    if (!complete_texture("texture acquisition")) {
        return out;
    }

    if (completion.texture == XR_NULL_HANDLE) {
        UtilityFunctions::printerr("[PicoReadback] Invalid readback texture handle");
        return out;
    }

    // Wrap external image into Godot RD texture and pull bytes
    RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
    if (!rd) {
        UtilityFunctions::printerr("[PicoReadback] No RenderingDevice available");
        xrReleaseReadbackTexturePICO(completion.texture);
        return out;
    }

    PackedByteArray pixels;

    GraphicsAPI api = _detect_graphics_api();
    if (api == GRAPHICS_API_VULKAN) {
        XrReadbackTextureImageVulkanPICO vkimg = {};
        vkimg.type = XR_TYPE_READBACK_TEXTURE_IMAGE_VULKAN_PICO;
        vkimg.next = nullptr;
        r = xrGetReadbackTextureImagePICO(completion.texture, (XrReadbackTextureImageBasePICO *)&vkimg);
        if (r == XR_SUCCESS && vkimg.image != (VkImage)0) {
            RID rd_tex = rd->texture_create_from_extension(
                RenderingDevice::TEXTURE_TYPE_2D,
                channels == 3 ? RenderingDevice::DATA_FORMAT_R8G8B8_UNORM : RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
                RenderingDevice::TEXTURE_SAMPLES_1,
                (RenderingDevice::TextureUsageBits)(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_CPU_READ_BIT | RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT),
                reinterpret_cast<uint64_t>(vkimg.image),
                (uint64_t)width,
                (uint64_t)height,
                1,
                1);
            pixels = rd->texture_get_data(rd_tex, 0);
        }
    } else if (api == GRAPHICS_API_OPENGL) {
        XrReadbackTextureImageOpenGLPICO glimg = {};
        glimg.type = XR_TYPE_READBACK_TEXTURE_IMAGE_OPENGL_PICO;
        glimg.next = nullptr;
        r = xrGetReadbackTextureImagePICO(completion.texture, (XrReadbackTextureImageBasePICO *)&glimg);
        if (r == XR_SUCCESS && glimg.texId != 0) {
            RID rd_tex = rd->texture_create_from_extension(
                RenderingDevice::TEXTURE_TYPE_2D,
                channels == 3 ? RenderingDevice::DATA_FORMAT_R8G8B8_UNORM : RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM,
                RenderingDevice::TEXTURE_SAMPLES_1,
                (RenderingDevice::TextureUsageBits)(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT | RenderingDevice::TEXTURE_USAGE_CPU_READ_BIT | RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT),
                (uint64_t)glimg.texId,
                (uint64_t)width,
                (uint64_t)height,
                1,
                1);
            pixels = rd->texture_get_data(rd_tex, 0);
        }
    }

    xrReleaseReadbackTexturePICO(completion.texture);

    if (pixels.is_empty()) {
        UtilityFunctions::printerr("[PicoReadback] Failed to get texture data from readback texture");
        return out;
    }

    return pixels;
}

bool OpenXRPicoReadbackTensorExtensionWrapper::_wait_for_future_ready(XrFutureEXT future, uint64_t timeout_us) const {
    if (future == XR_NULL_HANDLE) {
        return false;
    }
    if (!future_ext || xrPollFutureEXT_ptr == nullptr || xr_instance == XR_NULL_HANDLE) {
        // No XR_EXT_future support; attempt a simple delay and hope the producer finishes.
        static bool warned = false;
        if (!warned) {
            UtilityFunctions::printerr("[PicoReadback] XR_EXT_future not available; skipping active wait for readback completion.");
            warned = true;
        }
        if (timeout_us > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(timeout_us));
        }
        return false;
    }

    const uint64_t poll_interval_us = 1000;
    uint64_t waited_us = 0;
    XrFuturePollInfoEXT poll_info;
    poll_info.type = XR_TYPE_FUTURE_POLL_INFO_EXT;
    poll_info.next = nullptr;
    poll_info.future = future;

    XrFuturePollResultEXT poll_result;
    poll_result.type = XR_TYPE_FUTURE_POLL_RESULT_EXT;
    poll_result.next = nullptr;
    poll_result.state = XR_FUTURE_STATE_PENDING_EXT;

    while (waited_us <= timeout_us) {
        XrResult pr = xrPollFutureEXT(xr_instance, &poll_info, &poll_result);
        if (XR_FAILED(pr)) {
            UtilityFunctions::printerr("[PicoReadback] xrPollFutureEXT failed: ", (int)pr);
            return false;
        }
        if (poll_result.state == XR_FUTURE_STATE_READY_EXT) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(poll_interval_us));
        waited_us += poll_interval_us;
    }

    UtilityFunctions::printerr("[PicoReadback] Future remained pending after waiting ", (int64_t)timeout_us, " microseconds.");
    return false;
}

Dictionary OpenXRPicoReadbackTensorExtensionWrapper::debug_info() {
    Dictionary d;
    d["requested_cpu_ext"] = String(XR_PICO_READBACK_TENSOR_EXTENSION_NAME);
    d["requested_vulkan_ext"] = String(XR_PICO_READBACK_TENSOR_VULKAN_EXTENSION_NAME);
    d["requested_gles_ext"] = String(XR_PICO_READBACK_TENSOR_OPENGLES_EXTENSION_NAME);
    d["cpu_enabled"] = readback_cpu_ext;
    d["vulkan_enabled"] = readback_vulkan_ext;
    d["gles_enabled"] = readback_opengles_ext;
    d["future_enabled"] = future_ext && xrPollFutureEXT_ptr != nullptr;
    d["graphics_api"] = get_graphics_api();
    return d;
}
