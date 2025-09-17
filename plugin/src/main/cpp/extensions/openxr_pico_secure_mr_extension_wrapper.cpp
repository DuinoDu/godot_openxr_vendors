/**************************************************************************/
/*  openxr_pico_secure_mr_extension_wrapper.cpp                           */
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

#include "extensions/openxr_pico_secure_mr_extension_wrapper.h"

#include <godot_cpp/classes/open_xrapi_extension.hpp>

using namespace godot;

OpenXRPicoSecureMRExtensionWrapper *OpenXRPicoSecureMRExtensionWrapper::singleton = nullptr;

OpenXRPicoSecureMRExtensionWrapper *OpenXRPicoSecureMRExtensionWrapper::get_singleton() {
    if (singleton == nullptr) {
        singleton = memnew(OpenXRPicoSecureMRExtensionWrapper());
    }
    return singleton;
}

OpenXRPicoSecureMRExtensionWrapper::OpenXRPicoSecureMRExtensionWrapper() {
    ERR_FAIL_COND_MSG(singleton != nullptr, "An OpenXRPicoSecureMRExtensionWrapper singleton already exists.");

    request_extensions[XR_PICO_SECURE_MIXED_REALITY_EXTENSION_NAME] = &pico_secure_mr_ext;

    singleton = this;
}

OpenXRPicoSecureMRExtensionWrapper::~OpenXRPicoSecureMRExtensionWrapper() {
    singleton = nullptr;
}

void OpenXRPicoSecureMRExtensionWrapper::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_secure_mr_supported"), &OpenXRPicoSecureMRExtensionWrapper::is_secure_mr_supported);

    ClassDB::bind_method(D_METHOD("create_framework", "image_width", "image_height"), &OpenXRPicoSecureMRExtensionWrapper::create_framework);
    ClassDB::bind_method(D_METHOD("destroy_framework", "framework_handle"), &OpenXRPicoSecureMRExtensionWrapper::destroy_framework);
    ClassDB::bind_method(D_METHOD("create_pipeline", "framework_handle"), &OpenXRPicoSecureMRExtensionWrapper::create_pipeline);
    ClassDB::bind_method(D_METHOD("destroy_pipeline", "pipeline_handle"), &OpenXRPicoSecureMRExtensionWrapper::destroy_pipeline);

    ClassDB::bind_method(D_METHOD("create_operator_basic", "pipeline_handle", "operator_type"), &OpenXRPicoSecureMRExtensionWrapper::create_operator_basic);
    ClassDB::bind_method(D_METHOD("create_operator_arithmetic_compose", "pipeline_handle", "config_text"), &OpenXRPicoSecureMRExtensionWrapper::create_operator_arithmetic_compose);
    ClassDB::bind_method(D_METHOD("create_operator_convert_color", "pipeline_handle", "convert_code"), &OpenXRPicoSecureMRExtensionWrapper::create_operator_convert_color);
    ClassDB::bind_method(D_METHOD("create_operator_normalize", "pipeline_handle", "normalize_type"), &OpenXRPicoSecureMRExtensionWrapper::create_operator_normalize);
    ClassDB::bind_method(D_METHOD("create_operator_model", "pipeline_handle", "model_data", "model_name", "input_name", "output_names", "output_encodings"), &OpenXRPicoSecureMRExtensionWrapper::create_operator_model);

    ClassDB::bind_method(D_METHOD("create_pipeline_tensor_shape", "pipeline_handle", "dimensions", "data_type", "channels", "tensor_type", "placeholder"), &OpenXRPicoSecureMRExtensionWrapper::create_pipeline_tensor_shape);
    ClassDB::bind_method(D_METHOD("create_global_tensor_shape", "framework_handle", "dimensions", "data_type", "channels", "tensor_type", "placeholder"), &OpenXRPicoSecureMRExtensionWrapper::create_global_tensor_shape);

    ClassDB::bind_method(D_METHOD("reset_pipeline_tensor_bytes", "pipeline_handle", "tensor_handle", "data"), &OpenXRPicoSecureMRExtensionWrapper::reset_pipeline_tensor_bytes);
    ClassDB::bind_method(D_METHOD("reset_pipeline_tensor_floats", "pipeline_handle", "tensor_handle", "data"), &OpenXRPicoSecureMRExtensionWrapper::reset_pipeline_tensor_floats);

    ClassDB::bind_method(D_METHOD("set_operator_input_by_name", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "name"), &OpenXRPicoSecureMRExtensionWrapper::set_operator_input_by_name);
    ClassDB::bind_method(D_METHOD("set_operator_output_by_name", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "name"), &OpenXRPicoSecureMRExtensionWrapper::set_operator_output_by_name);
    ClassDB::bind_method(D_METHOD("set_operator_input_by_index", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "index"), &OpenXRPicoSecureMRExtensionWrapper::set_operator_input_by_index);
    ClassDB::bind_method(D_METHOD("set_operator_output_by_index", "pipeline_handle", "operator_handle", "pipeline_tensor_handle", "index"), &OpenXRPicoSecureMRExtensionWrapper::set_operator_output_by_index);

    ClassDB::bind_method(D_METHOD("execute_pipeline", "pipeline_handle", "mappings"), &OpenXRPicoSecureMRExtensionWrapper::execute_pipeline);
}

godot::Dictionary OpenXRPicoSecureMRExtensionWrapper::_get_requested_extensions() {
    godot::Dictionary result;

    for (auto ext : request_extensions) {
        godot::String key = ext.first;
        uint64_t value = reinterpret_cast<uint64_t>(ext.second);
        result[key] = (godot::Variant)value;
    }

    return result;
}

void OpenXRPicoSecureMRExtensionWrapper::_on_instance_created(uint64_t p_instance) {
    xr_instance = (XrInstance)p_instance;
    if (pico_secure_mr_ext) {
        // Load all function pointers we need. If any are missing, these macros will early-return.
        GDEXTENSION_INIT_XR_FUNC(xrCreateSecureMrFrameworkPICO);
        GDEXTENSION_INIT_XR_FUNC(xrDestroySecureMrFrameworkPICO);
        GDEXTENSION_INIT_XR_FUNC(xrCreateSecureMrPipelinePICO);
        GDEXTENSION_INIT_XR_FUNC(xrDestroySecureMrPipelinePICO);
        GDEXTENSION_INIT_XR_FUNC(xrCreateSecureMrOperatorPICO);
        GDEXTENSION_INIT_XR_FUNC(xrCreateSecureMrTensorPICO);
        GDEXTENSION_INIT_XR_FUNC(xrDestroySecureMrTensorPICO);
        GDEXTENSION_INIT_XR_FUNC(xrCreateSecureMrPipelineTensorPICO);
        GDEXTENSION_INIT_XR_FUNC(xrResetSecureMrTensorPICO);
        GDEXTENSION_INIT_XR_FUNC(xrResetSecureMrPipelineTensorPICO);
        GDEXTENSION_INIT_XR_FUNC(xrSetSecureMrOperatorOperandByNamePICO);
        GDEXTENSION_INIT_XR_FUNC(xrSetSecureMrOperatorOperandByIndexPICO);
        GDEXTENSION_INIT_XR_FUNC(xrExecuteSecureMrPipelinePICO);
        GDEXTENSION_INIT_XR_FUNC(xrSetSecureMrOperatorResultByNamePICO);
        GDEXTENSION_INIT_XR_FUNC(xrSetSecureMrOperatorResultByIndexPICO);
    }
}

void OpenXRPicoSecureMRExtensionWrapper::_on_instance_destroyed() {
    xr_instance = XR_NULL_HANDLE;
}

void OpenXRPicoSecureMRExtensionWrapper::_on_session_created(uint64_t p_session) {
    xr_session = (XrSession)p_session;
}

void OpenXRPicoSecureMRExtensionWrapper::_on_session_destroyed() {
    xr_session = XR_NULL_HANDLE;
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_framework(int32_t image_width, int32_t image_height) {
    ERR_FAIL_COND_V_MSG(!pico_secure_mr_ext, 0, "Pico SecureMR extension not available");
    ERR_FAIL_COND_V_MSG(xr_session == XR_NULL_HANDLE, 0, "OpenXR session not available");

    XrSecureMrFrameworkCreateInfoPICO create_info = { XR_TYPE_SECURE_MR_FRAMEWORK_CREATE_INFO_PICO, nullptr, image_width, image_height };
    XrSecureMrFrameworkPICO framework = XR_NULL_HANDLE;
    XrResult res = xrCreateSecureMrFrameworkPICO(xr_session, &create_info, &framework);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrFrameworkPICO failed");
    return (uint64_t)framework;
}

void OpenXRPicoSecureMRExtensionWrapper::destroy_framework(uint64_t framework_handle) {
    if (!framework_handle) return;
    XrSecureMrFrameworkPICO framework = (XrSecureMrFrameworkPICO)framework_handle;
    xrDestroySecureMrFrameworkPICO(framework);
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_pipeline(uint64_t framework_handle) {
    ERR_FAIL_COND_V_MSG(!pico_secure_mr_ext, 0, "Pico SecureMR extension not available");
    XrSecureMrFrameworkPICO framework = (XrSecureMrFrameworkPICO)framework_handle;
    XrSecureMrPipelineCreateInfoPICO create_info = { XR_TYPE_SECURE_MR_PIPELINE_CREATE_INFO_PICO, nullptr };
    XrSecureMrPipelinePICO pipeline = XR_NULL_HANDLE;
    XrResult res = xrCreateSecureMrPipelinePICO(framework, &create_info, &pipeline);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrPipelinePICO failed");
    return (uint64_t)pipeline;
}

void OpenXRPicoSecureMRExtensionWrapper::destroy_pipeline(uint64_t pipeline_handle) {
    if (!pipeline_handle) return;
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    xrDestroySecureMrPipelinePICO(pipeline);
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_operator_basic(uint64_t pipeline_handle, int32_t operator_type) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    XrSecureMrOperatorBaseHeaderPICO header = { XR_TYPE_SECURE_MR_OPERATOR_BASE_HEADER_PICO, nullptr };
    XrSecureMrOperatorCreateInfoPICO create_info = { XR_TYPE_SECURE_MR_OPERATOR_CREATE_INFO_PICO, nullptr, &header, (XrSecureMrOperatorTypePICO)operator_type };
    XrSecureMrOperatorPICO op = XR_NULL_HANDLE;
    XrResult res = xrCreateSecureMrOperatorPICO(pipeline, &create_info, &op);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrOperatorPICO (basic) failed");
    return (uint64_t)op;
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_operator_arithmetic_compose(uint64_t pipeline_handle, String config_text) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    XrSecureMrOperatorArithmeticComposePICO header = { XR_TYPE_SECURE_MR_OPERATOR_ARITHMETIC_COMPOSE_PICO, nullptr, {0} };
    CharString cs = config_text.utf8();
    // Ensure null-terminated and within buffer size
    size_t to_copy = MIN((size_t)XR_MAX_ARITHMETIC_COMPOSE_OPERATOR_CONFIG_LENGTH_PICO - 1, (size_t)cs.length());
    memcpy(header.configText, cs.get_data(), to_copy);
    header.configText[to_copy] = '\0';
    XrSecureMrOperatorCreateInfoPICO create_info = { XR_TYPE_SECURE_MR_OPERATOR_CREATE_INFO_PICO, nullptr, (XrSecureMrOperatorBaseHeaderPICO *)&header, XR_SECURE_MR_OPERATOR_TYPE_ARITHMETIC_COMPOSE_PICO };
    XrSecureMrOperatorPICO op = XR_NULL_HANDLE;
    XrResult res = xrCreateSecureMrOperatorPICO(pipeline, &create_info, &op);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrOperatorPICO (arithmetic compose) failed");
    return (uint64_t)op;
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_operator_convert_color(uint64_t pipeline_handle, int32_t convert_code) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    XrSecureMrOperatorColorConvertPICO header = { XR_TYPE_SECURE_MR_OPERATOR_COLOR_CONVERT_PICO, nullptr, convert_code };
    XrSecureMrOperatorCreateInfoPICO create_info = { XR_TYPE_SECURE_MR_OPERATOR_CREATE_INFO_PICO, nullptr, (XrSecureMrOperatorBaseHeaderPICO *)&header, XR_SECURE_MR_OPERATOR_TYPE_CONVERT_COLOR_PICO };
    XrSecureMrOperatorPICO op = XR_NULL_HANDLE;
    XrResult res = xrCreateSecureMrOperatorPICO(pipeline, &create_info, &op);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrOperatorPICO (convert color) failed");
    return (uint64_t)op;
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_operator_normalize(uint64_t pipeline_handle, int32_t normalize_type) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    XrSecureMrOperatorNormalizePICO header = { XR_TYPE_SECURE_MR_OPERATOR_NORMALIZE_PICO, nullptr, (XrSecureMrNormalizeTypePICO)normalize_type };
    XrSecureMrOperatorCreateInfoPICO create_info = { XR_TYPE_SECURE_MR_OPERATOR_CREATE_INFO_PICO, nullptr, (XrSecureMrOperatorBaseHeaderPICO *)&header, XR_SECURE_MR_OPERATOR_TYPE_NORMALIZE_PICO };
    XrSecureMrOperatorPICO op = XR_NULL_HANDLE;
    XrResult res = xrCreateSecureMrOperatorPICO(pipeline, &create_info, &op);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrOperatorPICO (normalize) failed");
    return (uint64_t)op;
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_operator_model(uint64_t pipeline_handle, PackedByteArray model_data, String model_name, String input_name, PackedStringArray output_names, PackedInt32Array output_encodings) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;

    // Build IO maps
    XrSecureMrOperatorIOMapPICO input_map = {};
    input_map.type = XR_TYPE_SECURE_MR_OPERATOR_IO_MAP_PICO;
    input_map.next = nullptr;
    input_map.encodingType = XR_SECURE_MR_MODEL_ENCODING_FLOAT_32_PICO;
    CharString input_name_cs = input_name.utf8();
    memset(input_map.nodeName, 0, XR_MAX_OPERATOR_NODE_NAME_PICO);
    memset(input_map.operatorIOName, 0, XR_MAX_OPERATOR_NODE_NAME_PICO);
    memcpy(input_map.nodeName, input_name_cs.get_data(), MIN((int)input_name_cs.length(), (int)XR_MAX_OPERATOR_NODE_NAME_PICO - 1));
    memcpy(input_map.operatorIOName, input_name_cs.get_data(), MIN((int)input_name_cs.length(), (int)XR_MAX_OPERATOR_NODE_NAME_PICO - 1));

    int out_count = MIN(output_names.size(), output_encodings.size());
    Vector<XrSecureMrOperatorIOMapPICO> outputs;
    outputs.resize(out_count);
    for (int i = 0; i < out_count; i++) {
        XrSecureMrOperatorIOMapPICO &m = outputs.write[i];
        m.type = XR_TYPE_SECURE_MR_OPERATOR_IO_MAP_PICO;
        m.next = nullptr;
        m.encodingType = (XrSecureMrModelEncodingPICO)(int)output_encodings[i];
        String oname_s = output_names[i];
        CharString oname = oname_s.utf8();
        memset(m.nodeName, 0, XR_MAX_OPERATOR_NODE_NAME_PICO);
        memset(m.operatorIOName, 0, XR_MAX_OPERATOR_NODE_NAME_PICO);
        memcpy(m.nodeName, oname.get_data(), MIN((int)oname.length(), (int)XR_MAX_OPERATOR_NODE_NAME_PICO - 1));
        memcpy(m.operatorIOName, oname.get_data(), MIN((int)oname.length(), (int)XR_MAX_OPERATOR_NODE_NAME_PICO - 1));
    }

    XrSecureMrOperatorModelPICO model_info = {};
    model_info.type = XR_TYPE_SECURE_MR_OPERATOR_MODEL_PICO;
    model_info.next = nullptr;
    model_info.modelInputCount = 1;
    model_info.modelInputs = &input_map;
    model_info.modelOutputCount = out_count;
    model_info.modelOutputs = outputs.ptrw();
    model_info.bufferSize = model_data.size();
    model_info.buffer = (void *)model_data.ptrw();
    model_info.modelType = XR_SECURE_MR_MODEL_TYPE_QNN_CONTEXT_BINARY_PICO;
    CharString mname = model_name.utf8();
    model_info.modelName = mname.get_data();

    XrSecureMrOperatorCreateInfoPICO create_info = { XR_TYPE_SECURE_MR_OPERATOR_CREATE_INFO_PICO, nullptr, (XrSecureMrOperatorBaseHeaderPICO *)&model_info, XR_SECURE_MR_OPERATOR_TYPE_RUN_MODEL_INFERENCE_PICO };
    XrSecureMrOperatorPICO op = XR_NULL_HANDLE;
    XrResult res = xrCreateSecureMrOperatorPICO(pipeline, &create_info, &op);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrOperatorPICO (model) failed");
    return (uint64_t)op;
}

static void fill_tensor_format(XrSecureMrTensorFormatPICO &fmt, int32_t data_type, int32_t channels, int32_t tensor_type) {
    fmt.dataType = (XrSecureMrTensorDataTypePICO)data_type;
    fmt.channel = (int8_t)channels;
    fmt.tensorType = (XrSecureMrTensorTypePICO)tensor_type;
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_pipeline_tensor_shape(uint64_t pipeline_handle, PackedInt32Array dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    XrSecureMrTensorFormatPICO fmt = {};
    fill_tensor_format(fmt, data_type, channels, tensor_type);
    XrSecureMrTensorCreateInfoShapePICO ci = {};
    ci.type = XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO;
    ci.next = nullptr;
    ci.placeHolder = placeholder ? XR_TRUE : XR_FALSE;
    ci.dimensionsCount = dimensions.size();
    ci.dimensions = (void *)dimensions.ptrw();
    ci.format = &fmt;
    XrSecureMrPipelineTensorPICO tensor = {};
    XrResult res = xrCreateSecureMrPipelineTensorPICO(pipeline, (XrSecureMrTensorCreateInfoBaseHeaderPICO *)&ci, &tensor);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrPipelineTensorPICO failed");
    return (uint64_t)tensor;
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_global_tensor_shape(uint64_t framework_handle, PackedInt32Array dimensions, int32_t data_type, int32_t channels, int32_t tensor_type, bool placeholder) {
    XrSecureMrFrameworkPICO framework = (XrSecureMrFrameworkPICO)framework_handle;
    XrSecureMrTensorFormatPICO fmt = {};
    fill_tensor_format(fmt, data_type, channels, tensor_type);
    XrSecureMrTensorCreateInfoShapePICO ci = {};
    ci.type = XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_SHAPE_PICO;
    ci.next = nullptr;
    ci.placeHolder = placeholder ? XR_TRUE : XR_FALSE;
    ci.dimensionsCount = dimensions.size();
    ci.dimensions = (void *)dimensions.ptrw();
    ci.format = &fmt;
    XrSecureMrTensorPICO tensor = {};
    XrResult res = xrCreateSecureMrTensorPICO(framework, (XrSecureMrTensorCreateInfoBaseHeaderPICO *)&ci, &tensor);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrTensorPICO failed");
    return (uint64_t)tensor;
}

void OpenXRPicoSecureMRExtensionWrapper::reset_pipeline_tensor_bytes(uint64_t pipeline_handle, uint64_t tensor_handle, PackedByteArray data) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    XrSecureMrPipelineTensorPICO tensor = (XrSecureMrPipelineTensorPICO)tensor_handle;
    XrSecureMrTensorBufferPICO buf = { XR_TYPE_SECURE_MR_TENSOR_BUFFER_PICO, nullptr, (uint32_t)data.size(), (void *)data.ptrw() };
    xrResetSecureMrPipelineTensorPICO(pipeline, tensor, &buf);
}

void OpenXRPicoSecureMRExtensionWrapper::reset_pipeline_tensor_floats(uint64_t pipeline_handle, uint64_t tensor_handle, PackedFloat32Array data) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    XrSecureMrPipelineTensorPICO tensor = (XrSecureMrPipelineTensorPICO)tensor_handle;
    XrSecureMrTensorBufferPICO buf = { XR_TYPE_SECURE_MR_TENSOR_BUFFER_PICO, nullptr, (uint32_t)(data.size() * sizeof(float)), (void *)data.ptrw() };
    xrResetSecureMrPipelineTensorPICO(pipeline, tensor, &buf);
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_pipeline_tensor_gltf(uint64_t pipeline_handle, PackedByteArray buffer, bool placeholder) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    XrSecureMrTensorCreateInfoGltfPICO ci = {};
    ci.type = XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_GLTF_PICO;
    ci.next = nullptr;
    ci.placeHolder = placeholder ? XR_TRUE : XR_FALSE;
    ci.bufferSize = buffer.size();
    ci.buffer = (void *)buffer.ptrw();
    XrSecureMrPipelineTensorPICO tensor = {};
    XrResult res = xrCreateSecureMrPipelineTensorPICO(pipeline, (XrSecureMrTensorCreateInfoBaseHeaderPICO *)&ci, &tensor);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrPipelineTensorPICO (GLTF) failed");
    return (uint64_t)tensor;
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_global_tensor_gltf(uint64_t framework_handle, PackedByteArray buffer, bool placeholder) {
    XrSecureMrFrameworkPICO framework = (XrSecureMrFrameworkPICO)framework_handle;
    XrSecureMrTensorCreateInfoGltfPICO ci = {};
    ci.type = XR_TYPE_SECURE_MR_TENSOR_CREATE_INFO_GLTF_PICO;
    ci.next = nullptr;
    ci.placeHolder = placeholder ? XR_TRUE : XR_FALSE;
    ci.bufferSize = buffer.size();
    ci.buffer = (void *)buffer.ptrw();
    XrSecureMrTensorPICO tensor = {};
    XrResult res = xrCreateSecureMrTensorPICO(framework, (XrSecureMrTensorCreateInfoBaseHeaderPICO *)&ci, &tensor);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrTensorPICO (GLTF) failed");
    return (uint64_t)tensor;
}

uint64_t OpenXRPicoSecureMRExtensionWrapper::create_operator_update_gltf(uint64_t pipeline_handle, int32_t attribute) {
    XrSecureMrPipelinePICO pipeline = (XrSecureMrPipelinePICO)pipeline_handle;
    XrSecureMrOperatorUpdateGltfPICO header = {};
    header.type = XR_TYPE_SECURE_MR_OPERATOR_UPDATE_GLTF_PICO;
    header.next = nullptr;
    header.attribute = (XrSecureMrGltfOperatorAttributePICO)attribute;
    XrSecureMrOperatorCreateInfoPICO ci = { XR_TYPE_SECURE_MR_OPERATOR_CREATE_INFO_PICO, nullptr, (XrSecureMrOperatorBaseHeaderPICO *)&header, XR_SECURE_MR_OPERATOR_TYPE_UPDATE_GLTF_PICO };
    XrSecureMrOperatorPICO op = XR_NULL_HANDLE;
    XrResult res = xrCreateSecureMrOperatorPICO(pipeline, &ci, &op);
    ERR_FAIL_COND_V_MSG(XR_FAILED(res), 0, "xrCreateSecureMrOperatorPICO (UPDATE_GLTF) failed");
    return (uint64_t)op;
}

void OpenXRPicoSecureMRExtensionWrapper::set_operator_input_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, String name) {
    CharString n = name.utf8();
    xrSetSecureMrOperatorOperandByNamePICO((XrSecureMrPipelinePICO)pipeline_handle, (XrSecureMrOperatorPICO)operator_handle, (XrSecureMrPipelineTensorPICO)pipeline_tensor_handle, n.get_data());
}

void OpenXRPicoSecureMRExtensionWrapper::set_operator_output_by_name(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, String name) {
    CharString n = name.utf8();
    xrSetSecureMrOperatorResultByNamePICO((XrSecureMrPipelinePICO)pipeline_handle, (XrSecureMrOperatorPICO)operator_handle, (XrSecureMrPipelineTensorPICO)pipeline_tensor_handle, n.get_data());
}

void OpenXRPicoSecureMRExtensionWrapper::set_operator_input_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index) {
    xrSetSecureMrOperatorOperandByIndexPICO((XrSecureMrPipelinePICO)pipeline_handle, (XrSecureMrOperatorPICO)operator_handle, (XrSecureMrPipelineTensorPICO)pipeline_tensor_handle, index);
}

void OpenXRPicoSecureMRExtensionWrapper::set_operator_output_by_index(uint64_t pipeline_handle, uint64_t operator_handle, uint64_t pipeline_tensor_handle, int32_t index) {
    xrSetSecureMrOperatorResultByIndexPICO((XrSecureMrPipelinePICO)pipeline_handle, (XrSecureMrOperatorPICO)operator_handle, (XrSecureMrPipelineTensorPICO)pipeline_tensor_handle, index);
}

void OpenXRPicoSecureMRExtensionWrapper::execute_pipeline(uint64_t pipeline_handle, Array mappings) {
    Vector<XrSecureMrPipelineIOPairPICO> pairs;
    pairs.resize(mappings.size());
    for (int i = 0; i < mappings.size(); i++) {
        Dictionary d = mappings[i];
        XrSecureMrPipelineIOPairPICO &p = pairs.write[i];
        p.type = XR_TYPE_SECURE_MR_PIPELINE_IO_PAIR_PICO;
        p.next = nullptr;
        p.localPlaceHolderTensor = (XrSecureMrPipelineTensorPICO)((uint64_t)d["local"]);
        p.globalTensor = (XrSecureMrTensorPICO)((uint64_t)d["global"]);
    }
    XrSecureMrPipelineExecuteParameterPICO ep = {};
    ep.type = XR_TYPE_SECURE_MR_PIPELINE_EXECUTE_PARAMETER_PICO;
    ep.next = nullptr;
    ep.pipelineRunToBeWaited = XR_PIPELINE_RUN_IDLE_PICO;
    ep.conditionTensor = XR_NULL_HANDLE;
    ep.pairCount = pairs.size();
    ep.pipelineIOPair = pairs.ptrw();
    XrSecureMrPipelineRunPICO run = XR_PIPELINE_RUN_IDLE_PICO;
    xrExecuteSecureMrPipelinePICO((XrSecureMrPipelinePICO)pipeline_handle, &ep, &run);
}
