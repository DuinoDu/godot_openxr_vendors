/**************************************************************************/
/*  openxr_pico_secure_mr.h                                              */
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

#ifndef OPENXR_PICO_SECURE_MR_H
#define OPENXR_PICO_SECURE_MR_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include <openxr/openxr.h>
#include "extensions/openxr_pico_secure_mr_extension_wrapper.h"
#include "extensions/openxr_pico_readback_tensor_extension_wrapper.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace godot {

// High-level helper for Pico SecureMR, mirroring SecureMR utils for Godot.
class OpenXRPicoSecureMR : public Object {
    GDCLASS(OpenXRPicoSecureMR, Object);

public:
    static OpenXRPicoSecureMR *get_singleton();

    OpenXRPicoSecureMR();
    ~OpenXRPicoSecureMR();

    // Capability
    bool is_supported() const;

    // Framework / pipeline lifecycle
    uint64_t create_framework(int32_t image_width, int32_t image_height);
    void destroy_framework(uint64_t framework_handle);
    uint64_t create_pipeline(uint64_t framework_handle);
    void destroy_pipeline(uint64_t pipeline_handle);

    // Tensor creation
    uint64_t create_pipeline_tensor_shape(uint64_t pipeline_handle, const PackedInt32Array &dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder);
    uint64_t create_global_tensor_shape(uint64_t framework_handle, const PackedInt32Array &dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder);
    uint64_t create_pipeline_tensor_gltf(uint64_t pipeline_handle, const PackedByteArray &buffer, bool placeholder);
    uint64_t create_global_tensor_gltf(uint64_t framework_handle, const PackedByteArray &buffer, bool placeholder);

    // Tensor content
    void reset_pipeline_tensor_bytes(uint64_t pipeline_handle, uint64_t tensor_handle, const PackedByteArray &data);
    void reset_pipeline_tensor_floats(uint64_t pipeline_handle, uint64_t tensor_handle, const PackedFloat32Array &data);

    // Deserialize pipeline from a Godot Dictionary spec.
    // Spec schema is inspired by SecureMR utils (tensors/operators/inputs/outputs).
    // Returns a Dictionary with keys:
    // - "pipeline": uint64 handle
    // - "tensors": Dictionary name -> uint64 pipeline tensor handle
    Dictionary deserialize_pipeline(uint64_t framework_handle, const Dictionary &spec, const String &assets_base_path = "");

    // Generic operator helpers
    uint64_t create_operator_basic(uint64_t pipeline_handle, int32_t operator_type);
    uint64_t create_operator_arithmetic(uint64_t pipeline_handle, const String &config_text);
    uint64_t create_operator_convert_color(uint64_t pipeline_handle, int32_t convert_code);
    uint64_t create_operator_normalize(uint64_t pipeline_handle, int32_t normalize_type);
    uint64_t create_operator_model(uint64_t pipeline_handle, const PackedByteArray &model_data, const String &model_name, const String &input_name, const PackedStringArray &output_names, const PackedInt32Array &output_encodings);

    // Wire operator IO
    void set_operator_input_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, const String &name);
    void set_operator_output_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, const String &name);
    void set_operator_input_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index);
    void set_operator_output_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index);

    // Execute
    void execute_pipeline(uint64_t pipeline_handle, const Array &mappings);

    // Tensor readback (asynchronous polling, modeled after SecureMR utils).
    uint64_t start_tensor_readback(const Array &targets, int32_t polling_interval_ms = 33);
    void stop_tensor_readback(uint64_t readback_handle);
    Array poll_tensor_readback(uint64_t readback_handle);

    // Convenience wrappers mirroring common SecureMR utils:
    // Camera access: outputs any of the provided placeholders
    void op_camera_access(uint64_t pipeline_handle, uint64_t left_image_tensor, uint64_t right_image_tensor, uint64_t timestamp_tensor, uint64_t camera_matrix_tensor);
    // Camera space to world (local). timestamp is required; left/right are optional (0 to skip)
    void op_camera_space_to_world(uint64_t pipeline_handle, uint64_t timestamp_tensor, uint64_t left_transform_tensor, uint64_t right_transform_tensor);
    // Assignment / type convert
    void op_assignment(uint64_t pipeline_handle, uint64_t src_tensor, uint64_t dst_tensor);
    // Arithmetic compose (expression with indexed operands)
    void op_arithmetic_compose(uint64_t pipeline_handle, const String &expression, const PackedInt64Array &operand_tensors, uint64_t result_tensor);
    // Elementwise helpers
    void op_elementwise_min(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor);
    void op_elementwise_max(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor);
    void op_elementwise_multiply(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor);
    void op_elementwise_or(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor);
    void op_elementwise_and(uint64_t pipeline_handle, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor);
    // ALL / ANY
    void op_all(uint64_t pipeline_handle, uint64_t operand_tensor, uint64_t result_tensor);
    void op_any(uint64_t pipeline_handle, uint64_t operand_tensor, uint64_t result_tensor);
    // SolvePnP
    void op_solve_pnp(uint64_t pipeline_handle, uint64_t object_points_tensor, uint64_t image_points_tensor, uint64_t camera_matrix_tensor, uint64_t rot_result_tensor, uint64_t trans_result_tensor);
    // Affine
    void op_get_affine(uint64_t pipeline_handle, uint64_t src_points_tensor, uint64_t dst_points_tensor, uint64_t result_affine_tensor);
    void op_apply_affine(uint64_t pipeline_handle, uint64_t affine_tensor, uint64_t src_image_tensor, uint64_t result_image_tensor);
    void op_apply_affine_point(uint64_t pipeline_handle, uint64_t affine_tensor, uint64_t src_points_tensor, uint64_t result_points_tensor);
    // UV -> 3D in camera space
    void op_uv_to_3d(uint64_t pipeline_handle, uint64_t uv_tensor, uint64_t timestamp_tensor, uint64_t camera_matrix_tensor, uint64_t left_image_tensor, uint64_t right_image_tensor, uint64_t result_points3d_tensor);
    // ArgMax
    void op_argmax(uint64_t pipeline_handle, uint64_t src_tensor, uint64_t result_indices_tensor);
    // Sort vector
    void op_sort_vec(uint64_t pipeline_handle, uint64_t src_vec_tensor, uint64_t result_sorted_vec_tensor, uint64_t result_indices_tensor);
    // NMS
    void op_nms(uint64_t pipeline_handle, uint64_t scores_tensor, uint64_t boxes_tensor, uint64_t result_scores_tensor, uint64_t result_boxes_tensor, uint64_t result_indices_tensor, float threshold);
    // Comparison
    void op_compare(uint64_t pipeline_handle, int32_t comparison, uint64_t left_tensor, uint64_t right_tensor, uint64_t result_tensor);
    // Sort matrix
    void op_sort_mat(uint64_t pipeline_handle, uint64_t src_mat_tensor, uint64_t result_sorted_mat_tensor, uint64_t result_indices_tensor, int32_t sort_type);
    // Render text into a texture
    void op_render_text(uint64_t pipeline_handle, uint64_t gltf_placeholder_tensor, uint64_t text_tensor, uint64_t start_position_tensor, uint64_t colors_tensor, uint64_t texture_id_tensor, uint64_t font_size_tensor, int32_t typeface, const String &language_and_locale, int32_t width, int32_t height);
    // SVD, Norm, channel layout convert, inversion
    // Operators potentially not present in current Pico headers are omitted (SVD, norm, HWC<->CHW).
    void op_inversion(uint64_t pipeline_handle, uint64_t src_mat_tensor, uint64_t result_inverted_tensor);
    // Transform matrix from R, T, optional S
    void op_transform(uint64_t pipeline_handle, uint64_t rotation_tensor, uint64_t translation_tensor, uint64_t scale_tensor, uint64_t result_tensor);
    // Create new texture to GLTF from image
    void op_gltf_new_texture(uint64_t pipeline_handle, uint64_t gltf_placeholder_tensor, uint64_t image_tensor, uint64_t texture_id_tensor);
    // Switch GLTF render status (pose, view locked, visible)
    void op_gltf_switch_render(uint64_t pipeline_handle, uint64_t gltf_placeholder_tensor, uint64_t pose_tensor, uint64_t view_locked_tensor, uint64_t visible_tensor);
    // Update GLTF with attribute (texture/animation/pose/local transform/material). Attribute is XrSecureMrGltfOperatorAttributePICO value.
    void op_gltf_update(uint64_t pipeline_handle, int32_t attribute, uint64_t gltf_placeholder_tensor, const Dictionary &operands_by_name);

protected:
    static void _bind_methods();

private:
    static OpenXRPicoSecureMR *singleton;
    OpenXRPicoSecureMRExtensionWrapper *wrapper = nullptr;
    OpenXRPicoReadbackTensorExtensionWrapper *readback_wrapper = nullptr;
    Dictionary pipeline_model_buffers;

    struct TensorReadbackWorker;
    uint64_t readback_handle_counter = 1;
    std::mutex readback_workers_mutex;
    std::unordered_map<uint64_t, std::shared_ptr<TensorReadbackWorker>> readback_workers;

    PackedByteArray _retain_pipeline_buffer(uint64_t pipeline_handle, const PackedByteArray &buffer);
    void _release_pipeline_buffers(uint64_t pipeline_handle);
    void _stop_all_tensor_readbacks();
    void set_named_input(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t tensor_handle, const char *name);
    void set_named_output(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t tensor_handle, const char *name);
    void do_elementwise(uint64_t pipeline_handle, int32_t op_type, uint64_t a_tensor, uint64_t b_tensor, uint64_t result_tensor);
};

} // namespace godot

#endif // OPENXR_PICO_SECURE_MR_H
