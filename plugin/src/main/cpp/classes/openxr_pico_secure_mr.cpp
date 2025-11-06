/**************************************************************************/
/*  openxr_pico_secure_mr.cpp                                            */
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

#include "classes/openxr_pico_secure_mr.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/classes/file_access.hpp>

#include "extensions/openxr_pico_secure_mr_extension_wrapper.h"
#include "extensions/openxr_pico_readback_tensor_extension_wrapper.h"

#include <algorithm>
#include <vector>
#include <utility>

using namespace godot;

namespace {

constexpr std::chrono::milliseconds READBACK_POLL_INTERVAL_MS{5};
constexpr size_t READBACK_QUEUE_MAX_DEPTH = 100;

} // namespace

bool OpenXRPicoSecureMR::_wait_for_readback_completion(const std::shared_ptr<ReadbackTarget> &target, XrSecureMrTensorPICO tensor, XrFutureEXT future, XrReadbackTensorBufferPICO &buffer, XrCreateBufferFromGlobalTensorCompletionPICO &completion) {
    if (!target || readback_wrapper == nullptr) {
        return false;
    }

    while (true) {
      XrResult result; 

      if (!readback_thread_stop.load(std::memory_order_acquire)) {
          if (!target->active) {
              return false;
          }

          completion.type = XR_TYPE_CREATE_BUFFER_FROM_GLOBAL_TENSOR_COMPLETION_PICO;
          completion.next = nullptr;
          completion.futureResult = XR_SUCCESS;
          completion.tensorBuffer = &buffer;

          result = readback_wrapper->xrCreateBufferFromGlobalTensorCompletePICO(tensor, future, &completion);
          if (result == XR_SUCCESS) {
              return true;
          }
      }
      UtilityFunctions::print(vformat("[PicoSecureMR] xrCreateBufferFromGlobalTensorCompletePICO failed with %d", (int)result));
      std::this_thread::sleep_for(READBACK_POLL_INTERVAL_MS);
    }


    return false;
}

bool OpenXRPicoSecureMR::_complete_future_for_target(const std::shared_ptr<ReadbackTarget> &target, XrFutureEXT future, PackedByteArray &out_data) {
    if (!target || readback_wrapper == nullptr) {
        return false;
    }

    XrSecureMrTensorPICO tensor = (XrSecureMrTensorPICO)target->tensor_handle;

    XrReadbackTensorBufferPICO buffer = {};
    buffer.bufferCapacityInput = 0;
    buffer.bufferSizeOutput = 0;
    buffer.buffer = nullptr;

    XrCreateBufferFromGlobalTensorCompletionPICO completion = {};

    if (!_wait_for_readback_completion(target, tensor, future, buffer, completion)) {
        return false;
    }

    const uint32_t required_size = buffer.bufferSizeOutput;
    out_data.resize(required_size);
    if (required_size > 0) {
        buffer.bufferCapacityInput = required_size;
        buffer.buffer = out_data.ptrw();
    }

    if (!_wait_for_readback_completion(target, tensor, future, buffer, completion)) {
        out_data.resize(0);
        return false;
    }

    if (completion.futureResult != XR_SUCCESS) {
        UtilityFunctions::printerr(vformat("[PicoSecureMR] Readback future returned %d for tensor %llu", (int)completion.futureResult, (uint64_t)target->tensor_handle));
        out_data.resize(0);
        return false;
    }

    if (buffer.bufferSizeOutput < required_size) {
        out_data.resize(buffer.bufferSizeOutput);
    }

    return true;
}

OpenXRPicoSecureMR *OpenXRPicoSecureMR::singleton = nullptr;

OpenXRPicoSecureMR *OpenXRPicoSecureMR::get_singleton() {
    if (singleton == nullptr) {
        singleton = memnew(OpenXRPicoSecureMR());
    }
    return singleton;
}

OpenXRPicoSecureMR::OpenXRPicoSecureMR() {
    ERR_FAIL_COND_MSG(singleton != nullptr, "An OpenXRPicoSecureMR singleton already exists.");
    wrapper = OpenXRPicoSecureMRExtensionWrapper::get_singleton();
    _update_readback_wrapper();
    singleton = this;
}

OpenXRPicoSecureMR::~OpenXRPicoSecureMR() {
    _stop_readback_thread();
    singleton = nullptr;
}

bool OpenXRPicoSecureMR::is_supported() const {
    const bool has_wrapper = wrapper != nullptr;
    const bool ext_supported = has_wrapper ? wrapper->is_secure_mr_supported() : false;
    return has_wrapper && ext_supported;
}

uint64_t OpenXRPicoSecureMR::create_framework(int32_t image_width, int32_t image_height) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_framework(image_width, image_height);
}

void OpenXRPicoSecureMR::destroy_framework(uint64_t framework_handle) {
    if (!wrapper) return;
    wrapper->destroy_framework(framework_handle);
}

uint64_t OpenXRPicoSecureMR::create_pipeline(uint64_t framework_handle) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_pipeline(framework_handle);
}

void OpenXRPicoSecureMR::destroy_pipeline(uint64_t pipeline_handle) {
    _release_pipeline_buffers(pipeline_handle);
    if (!wrapper) return;
    wrapper->destroy_pipeline(pipeline_handle);
}

uint64_t OpenXRPicoSecureMR::create_pipeline_tensor_shape(uint64_t pipeline_handle, const PackedInt32Array &dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_pipeline_tensor_shape(pipeline_handle, dimensions, data_type, channels, tensor_type, placeholder);
}

uint64_t OpenXRPicoSecureMR::create_global_tensor_shape(uint64_t framework_handle, const PackedInt32Array &dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_global_tensor_shape(framework_handle, dimensions, data_type, channels, tensor_type, placeholder);
}

uint64_t OpenXRPicoSecureMR::create_pipeline_tensor_gltf(uint64_t pipeline_handle, const PackedByteArray &buffer, bool placeholder) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_pipeline_tensor_gltf(pipeline_handle, buffer, placeholder);
}

uint64_t OpenXRPicoSecureMR::create_global_tensor_gltf(uint64_t framework_handle, const PackedByteArray &buffer, bool placeholder) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_global_tensor_gltf(framework_handle, buffer, placeholder);
}

void OpenXRPicoSecureMR::reset_pipeline_tensor_bytes(uint64_t pipeline_handle, uint64_t tensor_handle, const PackedByteArray &data) {
    if (!wrapper) return;
    wrapper->reset_pipeline_tensor_bytes(pipeline_handle, tensor_handle, data);
}

void OpenXRPicoSecureMR::reset_pipeline_tensor_floats(uint64_t pipeline_handle, uint64_t tensor_handle, const PackedFloat32Array &data) {
    if (!wrapper) return;
    wrapper->reset_pipeline_tensor_floats(pipeline_handle, tensor_handle, data);
}

uint64_t OpenXRPicoSecureMR::create_operator_basic(uint64_t pipeline_handle, int32_t operator_type) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_basic(pipeline_handle, operator_type);
}

uint64_t OpenXRPicoSecureMR::create_operator_arithmetic(uint64_t pipeline_handle, const String &config_text) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_arithmetic_compose(pipeline_handle, config_text);
}

uint64_t OpenXRPicoSecureMR::create_operator_convert_color(uint64_t pipeline_handle, int32_t convert_code) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_convert_color(pipeline_handle, convert_code);
}

uint64_t OpenXRPicoSecureMR::create_operator_normalize(uint64_t pipeline_handle, int32_t normalize_type) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_normalize(pipeline_handle, normalize_type);
}

uint64_t OpenXRPicoSecureMR::create_operator_model(uint64_t pipeline_handle, const PackedByteArray &model_data, const String &model_name, const String &input_name, const PackedStringArray &output_names, const PackedInt32Array &output_encodings) {
    ERR_FAIL_NULL_V(wrapper, 0);
    return wrapper->create_operator_model(pipeline_handle, model_data, model_name, input_name, output_names, output_encodings);
}

void OpenXRPicoSecureMR::set_operator_input_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, const String &name) {
    if (!wrapper) return;
    wrapper->set_operator_input_by_name(pipeline_handle, operator_handle, pipeline_tensor_handle, name);
}

void OpenXRPicoSecureMR::set_operator_output_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, const String &name) {
    if (!wrapper) return;
    wrapper->set_operator_output_by_name(pipeline_handle, operator_handle, pipeline_tensor_handle, name);
}

void OpenXRPicoSecureMR::set_operator_input_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index) {
    if (!wrapper) return;
    wrapper->set_operator_input_by_index(pipeline_handle, operator_handle, pipeline_tensor_handle, index);
}

void OpenXRPicoSecureMR::set_operator_output_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index) {
    if (!wrapper) return;
    wrapper->set_operator_output_by_index(pipeline_handle, operator_handle, pipeline_tensor_handle, index);
}

void OpenXRPicoSecureMR::execute_pipeline(uint64_t pipeline_handle, const Array &mappings) {
    if (!wrapper) return;
    wrapper->execute_pipeline(pipeline_handle, mappings);
}

void OpenXRPicoSecureMR::set_named_input(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t tensor_handle, const char *name) {
    if (tensor_handle != 0) {
        wrapper->set_operator_input_by_name(pipeline_handle, operator_handle, tensor_handle, name);
    }
}

void OpenXRPicoSecureMR::set_named_output(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t tensor_handle, const char *name) {
    if (tensor_handle != 0) {
        wrapper->set_operator_output_by_name(pipeline_handle, operator_handle, tensor_handle, name);
    }
}

void OpenXRPicoSecureMR::do_elementwise(uint64_t pipeline_handle, int32_t op_type, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, op_type);
    set_named_input(pipeline_handle, op, a_tensor, "operand0");
    set_named_input(pipeline_handle, op, b_tensor, "operand1");
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

// Convenience ops
void OpenXRPicoSecureMR::op_camera_access(uint64_t pipeline_handle, uint64_t left_image_tensor, uint64_t right_image_tensor, uint64_t timestamp_tensor, uint64_t camera_matrix_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_RECTIFIED_VST_ACCESS_PICO);
    set_named_output(pipeline_handle, op, left_image_tensor, "left image");
    set_named_output(pipeline_handle, op, right_image_tensor, "right image");
    set_named_output(pipeline_handle, op, timestamp_tensor, "timestamp");
    set_named_output(pipeline_handle, op, camera_matrix_tensor, "camera matrix");
}

void OpenXRPicoSecureMR::op_camera_space_to_world(uint64_t pipeline_handle, uint64_t timestamp_tensor, uint64_t left_transform_tensor, uint64_t right_transform_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_CAMERA_SPACE_TO_WORLD_PICO);
    set_named_input(pipeline_handle, op, timestamp_tensor, "timestamp");
    set_named_output(pipeline_handle, op, left_transform_tensor, "left");
    set_named_output(pipeline_handle, op, right_transform_tensor, "right");
}

void OpenXRPicoSecureMR::op_assignment(uint64_t pipeline_handle, uint64_t src_tensor, uint64_t dst_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ASSIGNMENT_PICO);
    set_named_input(pipeline_handle, op, src_tensor, "src");
    set_named_output(pipeline_handle, op, dst_tensor, "dst");
}

void OpenXRPicoSecureMR::op_arithmetic_compose(uint64_t pipeline_handle, const String &expression, const PackedInt64Array &operand_tensors, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_arithmetic_compose(pipeline_handle, expression);
    for (int i = 0; i < operand_tensors.size(); i++) {
        wrapper->set_operator_input_by_index(pipeline_handle, op, (uint64_t)operand_tensors[i], i);
    }
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_elementwise_min(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MIN_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_elementwise_max(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MAX_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_elementwise_multiply(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MULTIPLY_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_elementwise_or(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_OR_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_elementwise_and(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor) {
    do_elementwise(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_AND_PICO, a_tensor, b_tensor, result_tensor);
}

void OpenXRPicoSecureMR::op_all(uint64_t pipeline_handle, uint64_t operand_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ALL_PICO);
    set_named_input(pipeline_handle, op, operand_tensor, "operand");
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_any(uint64_t pipeline_handle, uint64_t operand_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ANY_PICO);
    set_named_input(pipeline_handle, op, operand_tensor, "operand");
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_solve_pnp(uint64_t pipeline_handle, uint64_t object_points_tensor, uint64_t image_points_tensor, uint64_t camera_matrix_tensor, uint64_t rot_result_tensor, uint64_t trans_result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_SOLVE_P_N_P_PICO);
    set_named_input(pipeline_handle, op, object_points_tensor, "object points");
    set_named_input(pipeline_handle, op, image_points_tensor, "image points");
    set_named_input(pipeline_handle, op, camera_matrix_tensor, "camera matrix");
    set_named_output(pipeline_handle, op, rot_result_tensor, "rotation");
    set_named_output(pipeline_handle, op, trans_result_tensor, "translation");
}

void OpenXRPicoSecureMR::op_get_affine(uint64_t pipeline_handle, uint64_t src_points_tensor, uint64_t dst_points_tensor, uint64_t result_affine_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_GET_AFFINE_PICO);
    set_named_input(pipeline_handle, op, src_points_tensor, "src");
    set_named_input(pipeline_handle, op, dst_points_tensor, "dst");
    set_named_output(pipeline_handle, op, result_affine_tensor, "result");
}

void OpenXRPicoSecureMR::op_apply_affine(uint64_t pipeline_handle, uint64_t affine_tensor, uint64_t src_image_tensor, uint64_t result_image_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_PICO);
    set_named_input(pipeline_handle, op, affine_tensor, "affine");
    set_named_input(pipeline_handle, op, src_image_tensor, "src image");
    set_named_output(pipeline_handle, op, result_image_tensor, "dst image");
}

void OpenXRPicoSecureMR::op_apply_affine_point(uint64_t pipeline_handle, uint64_t affine_tensor, uint64_t src_points_tensor, uint64_t result_points_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_POINT_PICO);
    set_named_input(pipeline_handle, op, affine_tensor, "affine");
    set_named_input(pipeline_handle, op, src_points_tensor, "src points");
    set_named_output(pipeline_handle, op, result_points_tensor, "dst points");
}

void OpenXRPicoSecureMR::op_uv_to_3d(uint64_t pipeline_handle, uint64_t uv_tensor, uint64_t timestamp_tensor, uint64_t camera_matrix_tensor, uint64_t left_image_tensor, uint64_t right_image_tensor, uint64_t result_points3d_tensor) {
    uint64_t op = wrapper->create_operator_uv_to_3d(pipeline_handle);
    set_named_input(pipeline_handle, op, uv_tensor, "uv");
    set_named_input(pipeline_handle, op, timestamp_tensor, "timestamp");
    set_named_input(pipeline_handle, op, camera_matrix_tensor, "camera intrinsic");
    set_named_input(pipeline_handle, op, left_image_tensor, "left image");
    set_named_input(pipeline_handle, op, right_image_tensor, "right image");
    set_named_output(pipeline_handle, op, result_points3d_tensor, "point_xyz");
}

void OpenXRPicoSecureMR::op_argmax(uint64_t pipeline_handle, uint64_t src_tensor, uint64_t result_indices_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_ARGMAX_PICO);
    set_named_input(pipeline_handle, op, src_tensor, "operand");
    set_named_output(pipeline_handle, op, result_indices_tensor, "result");
}

void OpenXRPicoSecureMR::op_sort_vec(uint64_t pipeline_handle, uint64_t src_vec_tensor, uint64_t result_sorted_vec_tensor, uint64_t result_indices_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_SORT_VEC_PICO);
    set_named_input(pipeline_handle, op, src_vec_tensor, "input");
    set_named_output(pipeline_handle, op, result_sorted_vec_tensor, "sorted");
    set_named_output(pipeline_handle, op, result_indices_tensor, "indices");
}

void OpenXRPicoSecureMR::op_nms(uint64_t pipeline_handle, uint64_t scores_tensor, uint64_t boxes_tensor, uint64_t result_scores_tensor, uint64_t result_boxes_tensor, uint64_t result_indices_tensor, float threshold) {
    uint64_t op = wrapper->create_operator_nms(pipeline_handle, threshold);
    set_named_input(pipeline_handle, op, scores_tensor, "scores");
    set_named_input(pipeline_handle, op, boxes_tensor, "boxes");
    set_named_output(pipeline_handle, op, result_scores_tensor, "scores");
    set_named_output(pipeline_handle, op, result_boxes_tensor, "boxes");
    set_named_output(pipeline_handle, op, result_indices_tensor, "indices");
}

void OpenXRPicoSecureMR::op_compare(uint64_t pipeline_handle, int32_t comparison, uint64_t left_tensor, uint64_t right_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_comparison(pipeline_handle, comparison);
    set_named_input(pipeline_handle, op, left_tensor, "operand0");
    set_named_input(pipeline_handle, op, right_tensor, "operand1");
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_sort_mat(uint64_t pipeline_handle, uint64_t src_mat_tensor, uint64_t result_sorted_mat_tensor, uint64_t result_indices_tensor, int32_t sort_type) {
    uint64_t op = wrapper->create_operator_sort_matrix(pipeline_handle, sort_type);
    set_named_input(pipeline_handle, op, src_mat_tensor, "input");
    set_named_output(pipeline_handle, op, result_sorted_mat_tensor, "sorted");
    set_named_output(pipeline_handle, op, result_indices_tensor, "indices");
}

void OpenXRPicoSecureMR::op_render_text(uint64_t pipeline_handle, uint64_t gltf_placeholder_tensor, uint64_t text_tensor, uint64_t start_position_tensor, uint64_t colors_tensor, uint64_t texture_id_tensor, uint64_t font_size_tensor, int32_t typeface, const String &language_and_locale, int32_t width, int32_t height) {
    uint64_t op = wrapper->create_operator_render_text(pipeline_handle, typeface, language_and_locale, width, height);
    set_named_input(pipeline_handle, op, gltf_placeholder_tensor, "gltf");
    set_named_input(pipeline_handle, op, text_tensor, "text");
    set_named_input(pipeline_handle, op, start_position_tensor, "start");
    set_named_input(pipeline_handle, op, colors_tensor, "colors");
    set_named_input(pipeline_handle, op, texture_id_tensor, "texture ID");
    set_named_input(pipeline_handle, op, font_size_tensor, "font size");
}

// Note: Some operators used in utils (e.g., SVD, NORM, HWC<->CHW) may not be available in this header set.

void OpenXRPicoSecureMR::op_inversion(uint64_t pipeline_handle, uint64_t src_mat_tensor, uint64_t result_inverted_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_INVERSION_PICO);
    set_named_input(pipeline_handle, op, src_mat_tensor, "operand");
    set_named_output(pipeline_handle, op, result_inverted_tensor, "result");
}

void OpenXRPicoSecureMR::op_transform(uint64_t pipeline_handle, uint64_t rotation_tensor, uint64_t translation_tensor, uint64_t scale_tensor, uint64_t result_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_GET_TRANSFORM_MAT_PICO);
    set_named_input(pipeline_handle, op, rotation_tensor, "rotation");
    set_named_input(pipeline_handle, op, translation_tensor, "translation");
    if (scale_tensor != 0) {
        set_named_input(pipeline_handle, op, scale_tensor, "scale");
    }
    set_named_output(pipeline_handle, op, result_tensor, "result");
}

void OpenXRPicoSecureMR::op_gltf_new_texture(uint64_t pipeline_handle, uint64_t gltf_placeholder_tensor, uint64_t image_tensor, uint64_t texture_id_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_LOAD_TEXTURE_PICO);
    set_named_input(pipeline_handle, op, gltf_placeholder_tensor, "gltf");
    set_named_input(pipeline_handle, op, image_tensor, "rgb image");
    set_named_output(pipeline_handle, op, texture_id_tensor, "texture ID");
}

void OpenXRPicoSecureMR::op_gltf_switch_render(uint64_t pipeline_handle, uint64_t gltf_placeholder_tensor, uint64_t pose_tensor, uint64_t view_locked_tensor, uint64_t visible_tensor) {
    uint64_t op = wrapper->create_operator_basic(pipeline_handle, XR_SECURE_MR_OPERATOR_TYPE_SWITCH_GLTF_RENDER_STATUS_PICO);
    set_named_input(pipeline_handle, op, gltf_placeholder_tensor, "gltf");
    set_named_input(pipeline_handle, op, pose_tensor, "world pose");
    set_named_input(pipeline_handle, op, view_locked_tensor, "view locked");
    set_named_input(pipeline_handle, op, visible_tensor, "visible");
}

void OpenXRPicoSecureMR::op_gltf_update(uint64_t pipeline_handle, int32_t attribute, uint64_t gltf_placeholder_tensor, const Dictionary &operands_by_name) {
    // Build the update operator with attribute header on wrapper
    uint64_t op = wrapper->create_operator_update_gltf(pipeline_handle, attribute);
    // Always set gltf first
    set_named_input(pipeline_handle, op, gltf_placeholder_tensor, "gltf");

    // Apply provided operands by their required names
    Array keys = operands_by_name.keys();
    for (int i = 0; i < keys.size(); i++) {
        String name = keys[i];
        uint64_t tensor = (uint64_t)operands_by_name[name];
        if (tensor != 0) {
            wrapper->set_operator_input_by_name(pipeline_handle, op, tensor, name);
        }
    }
}

bool OpenXRPicoSecureMR::ensure_readback(uint64_t global_tensor_handle, int32_t interval_msec) {
    if (global_tensor_handle == 0) {
        return false;
    }

    _update_readback_wrapper();
    if (readback_wrapper == nullptr || !readback_wrapper->is_readback_supported()) {
        UtilityFunctions::push_error("[PicoSecureMR] Readback extension not available.");
        return false;
    }

    const int32_t clamped_interval = interval_msec < 0 ? 0 : interval_msec;

    {
        std::lock_guard<std::mutex> lock(readback_mutex);
        auto it = readback_targets.find(global_tensor_handle);
        if (it != readback_targets.end()) {
            auto target = it->second;
            if (target) {
                target->interval_ms = clamped_interval;
                target->active = true;
                if (!target->in_flight) {
                    target->next_schedule = std::chrono::steady_clock::now();
                }
            }
        } else {
            auto target = std::make_shared<ReadbackTarget>();
            target->tensor_handle = global_tensor_handle;
            target->interval_ms = clamped_interval;
            target->next_schedule = std::chrono::steady_clock::now();
            readback_targets.emplace(global_tensor_handle, target);
        }
    }

    readback_cv.notify_all();
    _ensure_readback_thread();
    return true;
}

void OpenXRPicoSecureMR::release_readback(uint64_t global_tensor_handle) {
    if (global_tensor_handle == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(readback_mutex);
    auto it = readback_targets.find(global_tensor_handle);
    if (it == readback_targets.end()) {
        return;
    }
    auto target = it->second;
    if (target) {
        target->active = false;
        target->manual_request = false;
        target->data_ready = false;
        target->latest_data = PackedByteArray();
    }
    readback_targets.erase(it);
    readback_cv.notify_all();
}

bool OpenXRPicoSecureMR::request_readback(uint64_t global_tensor_handle) {
    if (global_tensor_handle == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(readback_mutex);
    auto it = readback_targets.find(global_tensor_handle);
    if (it == readback_targets.end()) {
        return false;
    }
    auto target = it->second;
    if (!target || !target->active) {
        return false;
    }
    target->manual_request = true;
    target->next_schedule = std::chrono::steady_clock::now();
    readback_cv.notify_all();
    return true;
}

PackedByteArray OpenXRPicoSecureMR::pop_readback(uint64_t global_tensor_handle) {
    PackedByteArray out;
    if (global_tensor_handle == 0) {
        return out;
    }

    std::lock_guard<std::mutex> lock(readback_mutex);
    auto it = readback_targets.find(global_tensor_handle);
    if (it == readback_targets.end()) {
        return out;
    }

    auto target = it->second;
    if (!target || !target->data_ready) {
        return out;
    }

    out = target->latest_data;
    target->latest_data = PackedByteArray();
    target->data_ready = false;
    return out;
}

// Minimal string->enum mapping for common SecureMR operators used by the MNIST sample.
static int _oxr_securemr_op_from_string(const String &s) {
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_UNKNOWN_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_UNKNOWN_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ARITHMETIC_COMPOSE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ARITHMETIC_COMPOSE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MIN_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MIN_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MAX_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MAX_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MULTIPLY_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_MULTIPLY_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_CUSTOMIZED_COMPARE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_CUSTOMIZED_COMPARE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_OR_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_OR_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_AND_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ELEMENTWISE_AND_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ALL_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ALL_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ANY_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ANY_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_NMS_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_NMS_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_SOLVE_P_N_P_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_SOLVE_P_N_P_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_GET_AFFINE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_GET_AFFINE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_POINT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_APPLY_AFFINE_POINT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_UV_TO_3D_IN_CAM_SPACE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_UV_TO_3D_IN_CAM_SPACE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ASSIGNMENT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ASSIGNMENT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_RUN_MODEL_INFERENCE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_RUN_MODEL_INFERENCE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_NORMALIZE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_NORMALIZE_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_CAMERA_SPACE_TO_WORLD_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_CAMERA_SPACE_TO_WORLD_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_RECTIFIED_VST_ACCESS_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_RECTIFIED_VST_ACCESS_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_ARGMAX_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_ARGMAX_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_CONVERT_COLOR_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_CONVERT_COLOR_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_SORT_VEC_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_SORT_VEC_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_INVERSION_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_INVERSION_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_GET_TRANSFORM_MAT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_GET_TRANSFORM_MAT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_SORT_MAT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_SORT_MAT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_SWITCH_GLTF_RENDER_STATUS_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_SWITCH_GLTF_RENDER_STATUS_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_UPDATE_GLTF_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_UPDATE_GLTF_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_RENDER_TEXT_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_RENDER_TEXT_PICO;
    if (s == String("XR_SECURE_MR_OPERATOR_TYPE_LOAD_TEXTURE_PICO")) return XR_SECURE_MR_OPERATOR_TYPE_LOAD_TEXTURE_PICO;
    return -1;
}

void OpenXRPicoSecureMR::_update_readback_wrapper() {
    if (readback_wrapper != nullptr) {
        return;
    }
    readback_wrapper = OpenXRPicoReadbackTensorExtensionWrapper::get_singleton();
}

void OpenXRPicoSecureMR::_ensure_readback_thread() {
    bool expected = false;
    if (readback_thread_running.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        readback_thread_stop.store(false, std::memory_order_release);
        readback_thread = std::thread(&OpenXRPicoSecureMR::_readback_thread_main, this);
    }
}

void OpenXRPicoSecureMR::_stop_readback_thread() {
    readback_thread_stop.store(true, std::memory_order_release);
    readback_cv.notify_all();
    if (readback_thread.joinable()) {
        readback_thread.join();
    }
    readback_thread_running.store(false, std::memory_order_release);

    std::lock_guard<std::mutex> lock(readback_mutex);
    readback_targets.clear();
    readback_future_queue.clear();
}

bool OpenXRPicoSecureMR::_has_ready_target_locked() {
    auto now = std::chrono::steady_clock::now();
    for (auto &kv : readback_targets) {
        const auto &target = kv.second;
        if (!target || !target->active) {
            continue;
        }
        if (target->manual_request) {
            return true;
        }
        if (!target->in_flight) {
            if (target->next_schedule == std::chrono::steady_clock::time_point() || target->next_schedule <= now) {
                return true;
            }
        }
    }
    return false;
}

void OpenXRPicoSecureMR::_schedule_ready_targets(std::vector<std::shared_ptr<ReadbackTarget>> &out_ready) {
    auto now = std::chrono::steady_clock::now();
    for (auto &kv : readback_targets) {
        auto target = kv.second;
        if (!target || !target->active || target->in_flight) {
            continue;
        }
        if (target->manual_request) {
            target->manual_request = false;
            target->in_flight = true;
            out_ready.push_back(target);
            continue;
        }
        if (target->next_schedule == std::chrono::steady_clock::time_point()) {
            target->next_schedule = now;
        }
        if (target->next_schedule <= now) {
            target->in_flight = true;
            out_ready.push_back(target);
        }
    }
}

void OpenXRPicoSecureMR::_enqueue_future_for_target(const std::shared_ptr<ReadbackTarget> &target) {
    if (!target || readback_wrapper == nullptr) {
        return;
    }

    XrFutureEXT future = XR_NULL_HANDLE;
    XrSecureMrTensorPICO tensor = (XrSecureMrTensorPICO)target->tensor_handle;
    XrResult result = readback_wrapper->xrCreateBufferFromGlobalTensorAsyncPICO(tensor, &future);
    if (XR_FAILED(result) || future == XR_NULL_HANDLE) {
        UtilityFunctions::printerr(vformat("[PicoSecureMR] xrCreateBufferFromGlobalTensorAsyncPICO failed with %d for tensor %llu", (int)result, (uint64_t)target->tensor_handle));
        std::lock_guard<std::mutex> lock(readback_mutex);
        target->in_flight = false;
        target->next_schedule = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, target->interval_ms));
        return;
    }

    std::unique_lock<std::mutex> lock(readback_mutex);
    while (readback_future_queue.size() >= READBACK_QUEUE_MAX_DEPTH && !readback_thread_stop.load(std::memory_order_acquire)) {
        lock.unlock();
        if (!_process_next_future()) {
            std::this_thread::sleep_for(READBACK_POLL_INTERVAL_MS);
        }
        lock.lock();
    }
    readback_future_queue.push_back({target, future});
    lock.unlock();
    readback_cv.notify_all();
}

bool OpenXRPicoSecureMR::_process_next_future() {
    std::shared_ptr<ReadbackTarget> target;
    XrFutureEXT future = XR_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> lock(readback_mutex);
        if (readback_future_queue.empty()) {
            return false;
        }
        ReadbackPendingFuture pending = readback_future_queue.front();
        readback_future_queue.pop_front();
        target = pending.target;
        future = pending.future;
    }

    PackedByteArray payload;
    bool success = _complete_future_for_target(target, future, payload);

    {
        std::lock_guard<std::mutex> lock(readback_mutex);
        if (target) {
            target->in_flight = false;
            const int interval = std::max(0, target->interval_ms);
            auto now = std::chrono::steady_clock::now();
            target->next_schedule = interval > 0 ? now + std::chrono::milliseconds(interval) : now;
            if (success) {
                target->latest_data = payload;
                target->data_ready = true;
            } else {
                target->data_ready = false;
                target->latest_data = PackedByteArray();
            }
        }
    }

    readback_cv.notify_all();
    return true;
}

void OpenXRPicoSecureMR::_process_pending_futures() {
    while (_process_next_future()) {
        if (readback_thread_stop.load(std::memory_order_acquire)) {
            // Drain remaining futures even if stopping; continue looping to flush queue.
            continue;
        }
    }
}

void OpenXRPicoSecureMR::_readback_thread_main() {
    std::unique_lock<std::mutex> lock(readback_mutex);
    while (!readback_thread_stop.load(std::memory_order_acquire)) {
        bool has_future = !readback_future_queue.empty();
        bool has_ready = _has_ready_target_locked();

        if (!has_future && !has_ready) {
            if (readback_targets.empty()) {
                readback_cv.wait(lock, [this]() {
                    return readback_thread_stop.load(std::memory_order_acquire) || !readback_future_queue.empty() || _has_ready_target_locked();
                });
                continue;
            }

            auto now = std::chrono::steady_clock::now();
            auto next_wake = std::chrono::steady_clock::time_point::max();
            for (auto &kv : readback_targets) {
                auto target = kv.second;
                if (!target || !target->active || target->in_flight) {
                    continue;
                }
                auto candidate = target->manual_request ? now : target->next_schedule;
                if (candidate == std::chrono::steady_clock::time_point()) {
                    candidate = now;
                }
                if (candidate < next_wake) {
                    next_wake = candidate;
                }
            }

            if (next_wake == std::chrono::steady_clock::time_point::max()) {
                readback_cv.wait(lock, [this]() {
                    return readback_thread_stop.load(std::memory_order_acquire) || !readback_future_queue.empty() || _has_ready_target_locked();
                });
            } else {
                readback_cv.wait_until(lock, next_wake, [this]() {
                    return readback_thread_stop.load(std::memory_order_acquire) || !readback_future_queue.empty() || _has_ready_target_locked();
                });
            }
            continue;
        }

        std::vector<std::shared_ptr<ReadbackTarget>> to_schedule;
        _schedule_ready_targets(to_schedule);
        lock.unlock();

        for (auto &target : to_schedule) {
            _enqueue_future_for_target(target);
        }

        _process_pending_futures();

        lock.lock();
    }

    lock.unlock();
    _process_pending_futures();
    readback_thread_running.store(false, std::memory_order_release);
}

static int32_t _oxr_securemr_encoding_from_data_type(int32_t data_type) {
    switch (data_type) {
        case XR_SECURE_MR_TENSOR_DATA_TYPE_UINT8_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_UFIXED_POINT8_PICO;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_INT8_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_SFIXED_POINT8_PICO;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_UINT16_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_UFIXED_POINT16_PICO;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_INT32_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_INT32_PICO;
        case XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT32_PICO:
        case XR_SECURE_MR_TENSOR_DATA_TYPE_FLOAT64_PICO:
            return XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
        default:
            return XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
    }
}

Dictionary OpenXRPicoSecureMR::deserialize_pipeline(uint64_t framework_handle, const Dictionary &spec, const String &assets_base_path) {
    Dictionary out;
    ERR_FAIL_NULL_V(wrapper, out);

    uint64_t pipeline = create_pipeline(framework_handle);
    out["pipeline"] = (uint64_t)pipeline;
    _release_pipeline_buffers(pipeline);

    Dictionary tensors_out;
    Dictionary tensor_data_types;

    // Tensors
    if (spec.has("tensors") && spec["tensors"].get_type() == Variant::DICTIONARY) {
        Dictionary tensors = spec["tensors"];
        Array tnames = tensors.keys();
        for (int i = 0; i < tnames.size(); i++) {
            String tname = tnames[i];
            Variant tv = tensors[tname];
            if (tv.get_type() != Variant::DICTIONARY) continue;
            Dictionary td = (Dictionary)tv;

            // Dimensions
            PackedInt32Array dims;
            if (td.has("dimensions") && td["dimensions"].get_type() == Variant::ARRAY) {
                Array da = td["dimensions"];
                dims.resize(da.size());
                for (int di = 0; di < da.size(); di++) dims.set(di, (int32_t)(int64_t)da[di]);
            }

            int32_t channels = td.has("channels") ? (int32_t)(int64_t)td["channels"] : 1;
            int32_t data_type = td.has("data_type") ? (int32_t)(int64_t)td["data_type"] : 6; // float32 default
            int32_t tensor_type = td.has("usage") ? (int32_t)(int64_t)td["usage"] : 6;    // mat default
            bool placeholder = td.has("is_placeholder") ? (bool)td["is_placeholder"] : false;

            uint64_t ph = create_pipeline_tensor_shape(pipeline, dims, data_type, channels, tensor_type, placeholder);
            tensors_out[tname] = (uint64_t)ph;
            tensor_data_types[tname] = data_type;

            // Optional initial value
            if (td.has("value") && td["value"].get_type() == Variant::ARRAY) {
                Array arr = td["value"];
                if (data_type == 6) {
                    PackedFloat32Array f;
                    f.resize(arr.size());
                    for (int vi = 0; vi < arr.size(); vi++) f.set(vi, (float)(double)arr[vi]);
                    reset_pipeline_tensor_floats(pipeline, ph, f);
                } else {
                    PackedByteArray b;
                    b.resize(arr.size());
                    for (int vi = 0; vi < arr.size(); vi++) b.set(vi, (uint8_t)(int64_t)arr[vi]);
                    reset_pipeline_tensor_bytes(pipeline, ph, b);
                }
            }
        }
    }

    // Operators
    if (spec.has("operators") && spec["operators"].get_type() == Variant::ARRAY) {
        Array ops = spec["operators"];
        for (int oi = 0; oi < ops.size(); oi++) {
            if (ops[oi].get_type() != Variant::DICTIONARY) continue;
            Dictionary od = ops[oi];
            String type_str = od.has("type") ? (String)od["type"] : String();
            int type = _oxr_securemr_op_from_string(type_str);
            uint64_t oph = 0;

            if (type == XR_SECURE_MR_OPERATOR_TYPE_ARITHMETIC_COMPOSE_PICO) {
                String expr = od.has("expression") ? (String)od["expression"] : String();
                oph = wrapper->create_operator_arithmetic_compose(pipeline, expr);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_CONVERT_COLOR_PICO) {
                int32_t flag = od.has("flag") ? (int32_t)(int64_t)od["flag"] : 0;
                oph = wrapper->create_operator_convert_color(pipeline, flag);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_NORMALIZE_PICO) {
                int32_t normalize_type = od.has("normalize_type") ? (int32_t)(int64_t)od["normalize_type"] : 0;
                oph = wrapper->create_operator_normalize(pipeline, normalize_type);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_RUN_MODEL_INFERENCE_PICO) {
                String model_asset = od.has("model_asset") ? (String)od["model_asset"] : String();
                String model_name = od.has("model_name") ? (String)od["model_name"] : String("model");

                String resolved_model_path = model_asset;
                PackedByteArray model_data;
                if (model_asset.length() > 0) {
                    String path = model_asset;
                    if (assets_base_path.length() > 0 && !model_asset.begins_with("res://") && !model_asset.begins_with("user://")) {
                        path = assets_base_path.path_join(model_asset);
                    }
                    resolved_model_path = path;
                    if (FileAccess::file_exists(path)) {
                        model_data = FileAccess::get_file_as_bytes(path);
                    } else {
                        UtilityFunctions::push_error(vformat("[PicoSecureMR] Model asset '%s' not found.", path));
                    }
                }
                if (model_data.is_empty()) {
                    UtilityFunctions::push_error(vformat("[PicoSecureMR] Model asset '%s' could not be loaded or is empty.", resolved_model_path));
                    continue;
                }

                // Persist the model buffer so native runtime can read it after deserialization returns.
                PackedByteArray stored_model = _retain_pipeline_buffer(pipeline, model_data);
                if (stored_model.is_empty()) {
                    UtilityFunctions::push_error(vformat("[PicoSecureMR] Failed to retain model buffer for pipeline %llu.", (uint64_t)pipeline));
                    continue;
                }

                String input_name = "input";
                if (od.has("inputs") && od["inputs"].get_type() == Variant::ARRAY) {
                    Array ia = od["inputs"];
                    if (ia.size() > 0 && ia[0].get_type() == Variant::DICTIONARY) {
                        Dictionary iid = ia[0];
                        if (iid.has("name")) input_name = (String)iid["name"];
                    }
                }
                PackedStringArray out_names;
                PackedInt32Array out_enc;
                if (od.has("outputs") && od["outputs"].get_type() == Variant::ARRAY) {
                    Array oa = od["outputs"];
                    for (int i = 0; i < oa.size(); i++) {
                        if (oa[i].get_type() != Variant::DICTIONARY) {
                            continue;
                        }
                        Dictionary ood = oa[i];
                        if (!ood.has("name")) {
                            continue;
                        }
                        String output_name = (String)ood["name"];
                        out_names.push_back(output_name);

                        int32_t encoding = XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
                        if (ood.has("encoding")) {
                            encoding = (int32_t)(int64_t)ood["encoding"];
                        } else if (ood.has("tensor")) {
                            String tensor_name = (String)ood["tensor"];
                            if (tensor_data_types.has(tensor_name)) {
                                encoding = _oxr_securemr_encoding_from_data_type((int32_t)(int64_t)tensor_data_types[tensor_name]);
                            }
                        }
                        int new_index = out_enc.size();
                        out_enc.resize(new_index + 1);
                        out_enc.set(new_index, encoding);
                    }
                }

                oph = wrapper->create_operator_model(pipeline, stored_model, model_name, input_name, out_names, out_enc);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_NMS_PICO) {
                float threshold = od.has("threshold") ? (float)(double)od["threshold"] : 0.5f;
                oph = wrapper->create_operator_nms(pipeline, threshold);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_CUSTOMIZED_COMPARE_PICO) {
                int32_t cmp = od.has("comparison") ? (int32_t)(int64_t)od["comparison"] : 0;
                oph = wrapper->create_operator_comparison(pipeline, cmp);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_SORT_MAT_PICO) {
                int32_t sort_type = od.has("sort_type") ? (int32_t)(int64_t)od["sort_type"] : 0;
                oph = wrapper->create_operator_sort_matrix(pipeline, sort_type);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_RENDER_TEXT_PICO) {
                int32_t typeface = od.has("typeface") ? (int32_t)(int64_t)od["typeface"] : 0;
                String lang = od.has("language_and_locale") ? (String)od["language_and_locale"] : (od.has("language") ? (String)od["language"] : String("en-US"));
                int32_t width = od.has("width") ? (int32_t)(int64_t)od["width"] : 256;
                int32_t height = od.has("height") ? (int32_t)(int64_t)od["height"] : 256;
                oph = wrapper->create_operator_render_text(pipeline, typeface, lang, width, height);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_UPDATE_GLTF_PICO) {
                int32_t attribute = od.has("attribute") ? (int32_t)(int64_t)od["attribute"] : 0;
                oph = wrapper->create_operator_update_gltf(pipeline, attribute);
            } else if (type == XR_SECURE_MR_OPERATOR_TYPE_UV_TO_3D_IN_CAM_SPACE_PICO) {
                oph = wrapper->create_operator_uv_to_3d(pipeline);
            } else {
                if (type < 0) {
                    UtilityFunctions::push_error(String("Unknown SecureMR operator type: ") + type_str);
                    continue;
                }
                oph = wrapper->create_operator_basic(pipeline, type);
            }

            // Inputs wiring
            if (od.has("inputs") && od["inputs"].get_type() == Variant::ARRAY) {
                Array ia = od["inputs"];
                for (int i = 0; i < ia.size(); i++) {
                    if (ia[i].get_type() == Variant::STRING) {
                        String tname = ia[i];
                        if (tensors_out.has(tname)) wrapper->set_operator_input_by_index(pipeline, oph, (uint64_t)tensors_out[tname], i);
                    } else if (ia[i].get_type() == Variant::DICTIONARY) {
                        Dictionary id = ia[i];
                        String tname = id.has("tensor") ? (String)id["tensor"] : String();
                        String in_name = id.has("name") ? (String)id["name"] : String();
                        if (tname.length() > 0 && tensors_out.has(tname)) {
                            if (in_name.length() > 0) wrapper->set_operator_input_by_name(pipeline, oph, (uint64_t)tensors_out[tname], in_name);
                            else wrapper->set_operator_input_by_index(pipeline, oph, (uint64_t)tensors_out[tname], i);
                        }
                    }
                }
            }

            // Outputs wiring
            if (od.has("outputs") && od["outputs"].get_type() == Variant::ARRAY) {
                Array oa = od["outputs"];
                for (int i = 0; i < oa.size(); i++) {
                    if (oa[i].get_type() == Variant::STRING) {
                        String tname = oa[i];
                        if (tensors_out.has(tname)) wrapper->set_operator_output_by_index(pipeline, oph, (uint64_t)tensors_out[tname], i);
                    } else if (oa[i].get_type() == Variant::DICTIONARY) {
                        Dictionary od2 = oa[i];
                        String tname = od2.has("tensor") ? (String)od2["tensor"] : String();
                        String out_name = od2.has("name") ? (String)od2["name"] : String();
                        if (tname.length() > 0 && tensors_out.has(tname)) {
                            if (out_name.length() > 0) wrapper->set_operator_output_by_name(pipeline, oph, (uint64_t)tensors_out[tname], out_name);
                            else wrapper->set_operator_output_by_index(pipeline, oph, (uint64_t)tensors_out[tname], i);
                        }
                    }
                }
            }
        }
    }

    out["tensors"] = tensors_out;
    if (spec.has("inputs")) out["inputs"] = spec["inputs"];
    if (spec.has("outputs")) out["outputs"] = spec["outputs"];
    return out;
}

PackedByteArray OpenXRPicoSecureMR::_retain_pipeline_buffer(uint64_t pipeline_handle, const PackedByteArray &buffer) {
    PackedByteArray out;
    if (pipeline_handle == 0 || buffer.is_empty()) {
        return out;
    }

    Variant key = (uint64_t)pipeline_handle;
    Variant existing = pipeline_model_buffers.get(key, Variant());
    Array stored;
    if (existing.get_type() == Variant::ARRAY) {
        stored = existing;
    }
    // Append a copy of the buffer so the underlying data stays alive while the pipeline exists.
    stored.append(buffer);
    pipeline_model_buffers.set(key, stored);

    Variant last = stored[stored.size() - 1];
    if (last.get_type() == Variant::PACKED_BYTE_ARRAY) {
        out = last;
    }
    return out;
}

void OpenXRPicoSecureMR::_release_pipeline_buffers(uint64_t pipeline_handle) {
    if (pipeline_handle == 0 || pipeline_model_buffers.is_empty()) {
        return;
    }

    Variant key = (uint64_t)pipeline_handle;
    pipeline_model_buffers.erase(key);
}

void OpenXRPicoSecureMR::_bind_methods() {
    // Static singleton accessor for GDScript (ClassName.get_singleton()).
    ClassDB::bind_static_method("OpenXRPicoSecureMR", D_METHOD("get_singleton"), &OpenXRPicoSecureMR::get_singleton);

    ClassDB::bind_method(D_METHOD("is_supported"), &OpenXRPicoSecureMR::is_supported);

    ClassDB::bind_method(D_METHOD("create_framework", "image_width", "image_height"), &OpenXRPicoSecureMR::create_framework);
    ClassDB::bind_method(D_METHOD("destroy_framework", "framework_handle"), &OpenXRPicoSecureMR::destroy_framework);
    ClassDB::bind_method(D_METHOD("create_pipeline", "framework_handle"), &OpenXRPicoSecureMR::create_pipeline);
    ClassDB::bind_method(D_METHOD("destroy_pipeline", "pipeline_handle"), &OpenXRPicoSecureMR::destroy_pipeline);

    ClassDB::bind_method(D_METHOD("create_pipeline_tensor_shape", "pipeline_handle", "dimensions", "data_type", "channels", "tensor_type", "placeholder"), &OpenXRPicoSecureMR::create_pipeline_tensor_shape);
    ClassDB::bind_method(D_METHOD("create_global_tensor_shape", "framework_handle", "dimensions", "data_type", "channels", "tensor_type", "placeholder"), &OpenXRPicoSecureMR::create_global_tensor_shape);
    ClassDB::bind_method(D_METHOD("create_pipeline_tensor_gltf", "pipeline_handle", "buffer", "placeholder"), &OpenXRPicoSecureMR::create_pipeline_tensor_gltf);
    ClassDB::bind_method(D_METHOD("create_global_tensor_gltf", "framework_handle", "buffer", "placeholder"), &OpenXRPicoSecureMR::create_global_tensor_gltf);

    ClassDB::bind_method(D_METHOD("reset_pipeline_tensor_bytes", "pipeline_handle", "tensor_handle", "data"), &OpenXRPicoSecureMR::reset_pipeline_tensor_bytes);
    ClassDB::bind_method(D_METHOD("reset_pipeline_tensor_floats", "pipeline_handle", "tensor_handle", "data"), &OpenXRPicoSecureMR::reset_pipeline_tensor_floats);

    ClassDB::bind_method(D_METHOD("ensure_readback", "global_tensor_handle", "interval_msec"), &OpenXRPicoSecureMR::ensure_readback, DEFVAL(33));
    ClassDB::bind_method(D_METHOD("release_readback", "global_tensor_handle"), &OpenXRPicoSecureMR::release_readback);
    ClassDB::bind_method(D_METHOD("request_readback", "global_tensor_handle"), &OpenXRPicoSecureMR::request_readback);
    ClassDB::bind_method(D_METHOD("pop_readback", "global_tensor_handle"), &OpenXRPicoSecureMR::pop_readback);

    ClassDB::bind_method(D_METHOD("create_operator_basic", "pipeline_handle", "operator_type"), &OpenXRPicoSecureMR::create_operator_basic);
    ClassDB::bind_method(D_METHOD("create_operator_arithmetic", "pipeline_handle", "config_text"), &OpenXRPicoSecureMR::create_operator_arithmetic);
    ClassDB::bind_method(D_METHOD("create_operator_convert_color", "pipeline_handle", "convert_code"), &OpenXRPicoSecureMR::create_operator_convert_color);
    ClassDB::bind_method(D_METHOD("create_operator_normalize", "pipeline_handle", "normalize_type"), &OpenXRPicoSecureMR::create_operator_normalize);
    ClassDB::bind_method(D_METHOD("create_operator_model", "pipeline_handle", "model_data", "model_name", "input_name", "output_names", "output_encodings"), &OpenXRPicoSecureMR::create_operator_model);

    ClassDB::bind_method(D_METHOD("set_operator_input_by_name", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "name"), &OpenXRPicoSecureMR::set_operator_input_by_name);
    ClassDB::bind_method(D_METHOD("set_operator_output_by_name", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "name"), &OpenXRPicoSecureMR::set_operator_output_by_name);
    ClassDB::bind_method(D_METHOD("set_operator_input_by_index", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "index"), &OpenXRPicoSecureMR::set_operator_input_by_index);
    ClassDB::bind_method(D_METHOD("set_operator_output_by_index", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "index"), &OpenXRPicoSecureMR::set_operator_output_by_index);

    ClassDB::bind_method(D_METHOD("execute_pipeline", "pipeline_handle", "mappings"), &OpenXRPicoSecureMR::execute_pipeline);

    // Convenience ops
    ClassDB::bind_method(D_METHOD("op_camera_access", "pipeline_handle", "left_image_tensor", "right_image_tensor", "timestamp_tensor", "camera_matrix_tensor"), &OpenXRPicoSecureMR::op_camera_access);
    ClassDB::bind_method(D_METHOD("op_camera_space_to_world", "pipeline_handle", "timestamp_tensor", "left_transform_tensor", "right_transform_tensor"), &OpenXRPicoSecureMR::op_camera_space_to_world);
    ClassDB::bind_method(D_METHOD("op_assignment", "pipeline_handle", "src_tensor", "dst_tensor"), &OpenXRPicoSecureMR::op_assignment);
    ClassDB::bind_method(D_METHOD("op_arithmetic_compose", "pipeline_handle", "expression", "operand_tensors", "result_tensor"), &OpenXRPicoSecureMR::op_arithmetic_compose);
    ClassDB::bind_method(D_METHOD("op_elementwise_min", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_min);
    ClassDB::bind_method(D_METHOD("op_elementwise_max", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_max);
    ClassDB::bind_method(D_METHOD("op_elementwise_multiply", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_multiply);
    ClassDB::bind_method(D_METHOD("op_elementwise_or", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_or);
    ClassDB::bind_method(D_METHOD("op_elementwise_and", "pipeline_handle", "a_tensor", "b_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_elementwise_and);
    ClassDB::bind_method(D_METHOD("op_all", "pipeline_handle", "operand_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_all);
    ClassDB::bind_method(D_METHOD("op_any", "pipeline_handle", "operand_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_any);
    ClassDB::bind_method(D_METHOD("op_solve_pnp", "pipeline_handle", "object_points_tensor", "image_points_tensor", "camera_matrix_tensor", "rot_result_tensor", "trans_result_tensor"), &OpenXRPicoSecureMR::op_solve_pnp);
    ClassDB::bind_method(D_METHOD("op_get_affine", "pipeline_handle", "src_points_tensor", "dst_points_tensor", "result_affine_tensor"), &OpenXRPicoSecureMR::op_get_affine);
    ClassDB::bind_method(D_METHOD("op_apply_affine", "pipeline_handle", "affine_tensor", "src_image_tensor", "result_image_tensor"), &OpenXRPicoSecureMR::op_apply_affine);
    ClassDB::bind_method(D_METHOD("op_apply_affine_point", "pipeline_handle", "affine_tensor", "src_points_tensor", "result_points_tensor"), &OpenXRPicoSecureMR::op_apply_affine_point);
    ClassDB::bind_method(D_METHOD("op_uv_to_3d", "pipeline_handle", "uv_tensor", "timestamp_tensor", "camera_matrix_tensor", "left_image_tensor", "right_image_tensor", "result_points3d_tensor"), &OpenXRPicoSecureMR::op_uv_to_3d);
    ClassDB::bind_method(D_METHOD("op_argmax", "pipeline_handle", "src_tensor", "result_indices_tensor"), &OpenXRPicoSecureMR::op_argmax);
    ClassDB::bind_method(D_METHOD("op_sort_vec", "pipeline_handle", "src_vec_tensor", "result_sorted_vec_tensor", "result_indices_tensor"), &OpenXRPicoSecureMR::op_sort_vec);
    ClassDB::bind_method(D_METHOD("op_nms", "pipeline_handle", "scores_tensor", "boxes_tensor", "result_scores_tensor", "result_boxes_tensor", "result_indices_tensor", "threshold"), &OpenXRPicoSecureMR::op_nms);
    ClassDB::bind_method(D_METHOD("op_compare", "pipeline_handle", "comparison", "left_tensor", "right_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_compare);
    ClassDB::bind_method(D_METHOD("op_sort_mat", "pipeline_handle", "src_mat_tensor", "result_sorted_mat_tensor", "result_indices_tensor", "sort_type"), &OpenXRPicoSecureMR::op_sort_mat);
    ClassDB::bind_method(D_METHOD("op_render_text", "pipeline_handle", "gltf_placeholder_tensor", "text_tensor", "start_position_tensor", "colors_tensor", "texture_id_tensor", "font_size_tensor", "typeface", "language_and_locale", "width", "height"), &OpenXRPicoSecureMR::op_render_text);
    ClassDB::bind_method(D_METHOD("op_inversion", "pipeline_handle", "src_mat_tensor", "result_inverted_tensor"), &OpenXRPicoSecureMR::op_inversion);
    ClassDB::bind_method(D_METHOD("op_transform", "pipeline_handle", "rotation_tensor", "translation_tensor", "scale_tensor", "result_tensor"), &OpenXRPicoSecureMR::op_transform);
    ClassDB::bind_method(D_METHOD("op_gltf_new_texture", "pipeline_handle", "gltf_placeholder_tensor", "image_tensor", "texture_id_tensor"), &OpenXRPicoSecureMR::op_gltf_new_texture);
    ClassDB::bind_method(D_METHOD("op_gltf_switch_render", "pipeline_handle", "gltf_placeholder_tensor", "pose_tensor", "view_locked_tensor", "visible_tensor"), &OpenXRPicoSecureMR::op_gltf_switch_render);
    ClassDB::bind_method(D_METHOD("op_gltf_update", "pipeline_handle", "attribute", "gltf_placeholder_tensor", "operands_by_name"), &OpenXRPicoSecureMR::op_gltf_update);

    // Deserialization
    ClassDB::bind_method(D_METHOD("deserialize_pipeline", "framework_handle", "spec", "assets_base_path"), &OpenXRPicoSecureMR::deserialize_pipeline, DEFVAL(String("")));
}
