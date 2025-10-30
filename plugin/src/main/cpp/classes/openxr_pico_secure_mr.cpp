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
#include <godot_cpp/variant/utility_functions.hpp>

#include "extensions/openxr_pico_secure_mr_extension_wrapper.h"

using namespace godot;

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
    singleton = this;
}

OpenXRPicoSecureMR::~OpenXRPicoSecureMR() {
    singleton = nullptr;
}

bool OpenXRPicoSecureMR::is_supported() const {
    return wrapper && wrapper->is_secure_mr_supported();
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

void OpenXRPicoSecureMR::_bind_methods() {
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
}
