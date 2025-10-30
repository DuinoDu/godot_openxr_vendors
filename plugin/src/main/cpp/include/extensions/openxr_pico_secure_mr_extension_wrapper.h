/**************************************************************************/
/*  openxr_pico_secure_mr_extension_wrapper.h                             */
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

#ifndef OPENXR_PICO_SECURE_MR_EXTENSION_WRAPPER_H
#define OPENXR_PICO_SECURE_MR_EXTENSION_WRAPPER_H

#include <godot_cpp/classes/open_xr_extension_wrapper_extension.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <openxr/openxr.h>

#include "util.h"

#include <map>

using namespace godot;

// Wrapper for the Pico Secure Mixed Reality extension.
class OpenXRPicoSecureMRExtensionWrapper : public OpenXRExtensionWrapperExtension {
    GDCLASS(OpenXRPicoSecureMRExtensionWrapper, OpenXRExtensionWrapperExtension);

public:
    static OpenXRPicoSecureMRExtensionWrapper *get_singleton();

    OpenXRPicoSecureMRExtensionWrapper();
    ~OpenXRPicoSecureMRExtensionWrapper();

    godot::Dictionary _get_requested_extensions() override;

    void _on_instance_created(uint64_t p_instance) override;
    void _on_instance_destroyed() override;
    void _on_session_created(uint64_t p_session) override;
    void _on_session_destroyed() override;

    // GDScript API
    bool is_secure_mr_supported() const { return pico_secure_mr_ext; }

    // Framework / pipeline lifecycle
    uint64_t create_framework(int32_t image_width, int32_t image_height);
    void destroy_framework(uint64_t framework_handle);
    uint64_t create_pipeline(uint64_t framework_handle);
    void destroy_pipeline(uint64_t pipeline_handle);

    // Operators
    uint64_t create_operator_basic(uint64_t pipeline_handle, int32_t operator_type);
    uint64_t create_operator_arithmetic_compose(uint64_t pipeline_handle, String config_text);
    uint64_t create_operator_convert_color(uint64_t pipeline_handle, int32_t convert_code);
    uint64_t create_operator_normalize(uint64_t pipeline_handle, int32_t normalize_type);
    uint64_t create_operator_model(uint64_t pipeline_handle, PackedByteArray model_data, String model_name, String input_name, PackedStringArray output_names, PackedInt32Array output_encodings);
    uint64_t create_operator_comparison(uint64_t pipeline_handle, int32_t comparison);
    uint64_t create_operator_nms(uint64_t pipeline_handle, float threshold);
    uint64_t create_operator_sort_matrix(uint64_t pipeline_handle, int32_t sort_type);
    uint64_t create_operator_render_text(uint64_t pipeline_handle, int32_t typeface, String language_and_locale, int32_t width, int32_t height);
    uint64_t create_operator_uv_to_3d(uint64_t pipeline_handle);

    // Tensors
    uint64_t create_pipeline_tensor_shape(uint64_t pipeline_handle, PackedInt32Array dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder);
    uint64_t create_global_tensor_shape(uint64_t framework_handle, PackedInt32Array dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder);

    // GLTF tensors
    uint64_t create_pipeline_tensor_gltf(uint64_t pipeline_handle, PackedByteArray buffer, bool placeholder);
    uint64_t create_global_tensor_gltf(uint64_t framework_handle, PackedByteArray buffer, bool placeholder);

    // GLTF operator
    uint64_t create_operator_update_gltf(uint64_t pipeline_handle, int32_t attribute);

    // Tensor content
    void reset_pipeline_tensor_bytes(uint64_t pipeline_handle, uint64_t tensor_handle, PackedByteArray data);
    void reset_pipeline_tensor_floats(uint64_t pipeline_handle, uint64_t tensor_handle, PackedFloat32Array data);

    // Graph wiring
    void set_operator_input_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, String name);
    void set_operator_output_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, String name);
    void set_operator_input_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index);
    void set_operator_output_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index);

    // Execute
    void execute_pipeline(uint64_t pipeline_handle, Array mappings);

protected:
    static void _bind_methods();

private:
    // SecureMR function pointers
    EXT_PROTO_XRRESULT_FUNC3(xrCreateSecureMrFrameworkPICO,
            (XrSession), session,
            (const XrSecureMrFrameworkCreateInfoPICO *), create_info,
            (XrSecureMrFrameworkPICO *), framework_out);
    EXT_PROTO_XRRESULT_FUNC1(xrDestroySecureMrFrameworkPICO,
            (XrSecureMrFrameworkPICO), framework);
    EXT_PROTO_XRRESULT_FUNC3(xrCreateSecureMrPipelinePICO,
            (XrSecureMrFrameworkPICO), framework,
            (const XrSecureMrPipelineCreateInfoPICO *), create_info,
            (XrSecureMrPipelinePICO *), pipeline_out);
    EXT_PROTO_XRRESULT_FUNC1(xrDestroySecureMrPipelinePICO,
            (XrSecureMrPipelinePICO), pipeline);
    EXT_PROTO_XRRESULT_FUNC3(xrCreateSecureMrOperatorPICO,
            (XrSecureMrPipelinePICO), pipeline,
            (const XrSecureMrOperatorCreateInfoPICO *), create_info,
            (XrSecureMrOperatorPICO *), operator_out);
    EXT_PROTO_XRRESULT_FUNC3(xrCreateSecureMrTensorPICO,
            (XrSecureMrFrameworkPICO), framework,
            (const XrSecureMrTensorCreateInfoBaseHeaderPICO *), create_info,
            (XrSecureMrTensorPICO *), tensor_out);
    EXT_PROTO_XRRESULT_FUNC1(xrDestroySecureMrTensorPICO,
            (XrSecureMrTensorPICO), tensor);
    EXT_PROTO_XRRESULT_FUNC3(xrCreateSecureMrPipelineTensorPICO,
            (XrSecureMrPipelinePICO), pipeline,
            (const XrSecureMrTensorCreateInfoBaseHeaderPICO *), create_info,
            (XrSecureMrPipelineTensorPICO *), pipeline_tensor_out);
    EXT_PROTO_XRRESULT_FUNC2(xrResetSecureMrTensorPICO,
            (XrSecureMrTensorPICO), tensor,
            (XrSecureMrTensorBufferPICO *), tensor_buffer);
    EXT_PROTO_XRRESULT_FUNC3(xrResetSecureMrPipelineTensorPICO,
            (XrSecureMrPipelinePICO), pipeline,
            (XrSecureMrPipelineTensorPICO), tensor,
            (XrSecureMrTensorBufferPICO *), tensor_buffer);
    EXT_PROTO_XRRESULT_FUNC4(xrSetSecureMrOperatorOperandByNamePICO,
            (XrSecureMrPipelinePICO), pipeline,
            (XrSecureMrOperatorPICO), tensor_operator,
            (XrSecureMrPipelineTensorPICO), pipeline_tensor,
            (const char *), input_name);
    EXT_PROTO_XRRESULT_FUNC4(xrSetSecureMrOperatorOperandByIndexPICO,
            (XrSecureMrPipelinePICO), pipeline,
            (XrSecureMrOperatorPICO), tensor_operator,
            (XrSecureMrPipelineTensorPICO), pipeline_tensor,
            (int32_t), index);
    EXT_PROTO_XRRESULT_FUNC3(xrExecuteSecureMrPipelinePICO,
            (XrSecureMrPipelinePICO), pipeline,
            (const XrSecureMrPipelineExecuteParameterPICO *), parameter,
            (XrSecureMrPipelineRunPICO *), pipeline_run_out);
    EXT_PROTO_XRRESULT_FUNC4(xrSetSecureMrOperatorResultByNamePICO,
            (XrSecureMrPipelinePICO), pipeline,
            (XrSecureMrOperatorPICO), tensor_operator,
            (XrSecureMrPipelineTensorPICO), pipeline_tensor,
            (const char *), name);
    EXT_PROTO_XRRESULT_FUNC4(xrSetSecureMrOperatorResultByIndexPICO,
            (XrSecureMrPipelinePICO), pipeline,
            (XrSecureMrOperatorPICO), tensor_operator,
            (XrSecureMrPipelineTensorPICO), pipeline_tensor,
            (int32_t), index);

    static OpenXRPicoSecureMRExtensionWrapper *singleton;

    std::map<godot::String, bool *> request_extensions;

    bool pico_secure_mr_ext = false;
    XrInstance xr_instance = XR_NULL_HANDLE;
    XrSession xr_session = XR_NULL_HANDLE;
};

#endif // OPENXR_PICO_SECURE_MR_EXTENSION_WRAPPER_H
