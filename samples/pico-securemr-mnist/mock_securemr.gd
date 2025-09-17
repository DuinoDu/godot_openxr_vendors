extends Node

# Simple mock of OpenXRPicoSecureMRExtensionWrapper for desktop smoke tests.
# Emulates handle creation and connection calls; logs the DAG and mappings.

var _next: int = 1
var _frameworks := {}
var _pipelines := {}
var _ops := {}
var _tensors := {}
var _edges := [] # [{type:"in|out", op, name_or_index, tensor, by:"name|index"}]
var _execs := [] # mappings arrays

func _new_handle() -> int:
    _next += 1
    return _next

func is_secure_mr_supported() -> bool:
    return true

func create_framework(w: int, h: int) -> int:
    var hndl = _new_handle()
    _frameworks[hndl] = {"w":w, "h":h}
    print("[MockSMR] create_framework ", w, "x", h, " -> ", hndl)
    return hndl

func destroy_framework(framework: int) -> void:
    _frameworks.erase(framework)

func create_pipeline(framework: int) -> int:
    var hndl = _new_handle()
    _pipelines[hndl] = {"fw":framework}
    print("[MockSMR] create_pipeline fw=", framework, " -> ", hndl)
    return hndl

func destroy_pipeline(pipeline: int) -> void:
    _pipelines.erase(pipeline)

func create_operator_basic(pipeline: int, operator_type: int) -> int:
    var hndl = _new_handle()
    _ops[hndl] = {"pipe":pipeline, "type":operator_type}
    print("[MockSMR] create_operator_basic type=", operator_type, " -> ", hndl)
    return hndl

func create_operator_arithmetic_compose(pipeline: int, config_text: String) -> int:
    var hndl = _new_handle()
    _ops[hndl] = {"pipe":pipeline, "type":1, "cfg":config_text}
    print("[MockSMR] create_operator_arithmetic_compose cfg=", config_text, " -> ", hndl)
    return hndl

func create_operator_convert_color(pipeline: int, code: int) -> int:
    var hndl = _new_handle()
    _ops[hndl] = {"pipe":pipeline, "type":25, "code":code}
    print("[MockSMR] create_operator_convert_color code=", code, " -> ", hndl)
    return hndl

func create_operator_normalize(pipeline: int, normalize_type: int) -> int:
    var hndl = _new_handle()
    _ops[hndl] = {"pipe":pipeline, "type":21, "norm":normalize_type}
    print("[MockSMR] create_operator_normalize type=", normalize_type, " -> ", hndl)
    return hndl

func create_operator_model(pipeline: int, model_data: PackedByteArray, model_name: String, input_name: String, output_names: PackedStringArray, output_encodings: PackedInt32Array) -> int:
    var hndl = _new_handle()
    _ops[hndl] = {"pipe":pipeline, "type":19, "name":model_name, "in":input_name, "outs":output_names}
    print("[MockSMR] create_operator_model name=", model_name, " in=", input_name, " outs=", output_names, " -> ", hndl)
    return hndl

func create_pipeline_tensor_shape(pipeline: int, dims: PackedInt32Array, dtype: int, channels: int, ttype: int, placeholder: bool) -> int:
    var hndl = _new_handle()
    _tensors[hndl] = {"pipe":pipeline, "dims":dims, "dtype":dtype, "ch":channels, "tt":ttype, "ph":placeholder}
    print("[MockSMR] create_pipeline_tensor dims=", dims, " dt=", dtype, " ch=", channels, " tt=", ttype, " ph=", placeholder, " -> ", hndl)
    return hndl

func create_global_tensor_shape(framework: int, dims: PackedInt32Array, dtype: int, channels: int, ttype: int, placeholder: bool) -> int:
    var hndl = _new_handle()
    _tensors[hndl] = {"fw":framework, "dims":dims, "dtype":dtype, "ch":channels, "tt":ttype, "ph":placeholder, "global":true}
    print("[MockSMR] create_global_tensor dims=", dims, " -> ", hndl)
    return hndl

func reset_pipeline_tensor_bytes(pipeline: int, tensor: int, data: PackedByteArray) -> void:
    _tensors[tensor]["bytes_len"] = data.size()

func reset_pipeline_tensor_floats(pipeline: int, tensor: int, data: PackedFloat32Array) -> void:
    _tensors[tensor]["floats_len"] = data.size()

func set_operator_input_by_name(pipeline: int, op: int, t: int, name: String) -> void:
    _edges.append({"type":"in", "op":op, "by":"name", "name":name, "tensor":t})

func set_operator_output_by_name(pipeline: int, op: int, t: int, name: String) -> void:
    _edges.append({"type":"out", "op":op, "by":"name", "name":name, "tensor":t})

func set_operator_input_by_index(pipeline: int, op: int, t: int, idx: int) -> void:
    _edges.append({"type":"in", "op":op, "by":"index", "index":idx, "tensor":t})

func set_operator_output_by_index(pipeline: int, op: int, t: int, idx: int) -> void:
    _edges.append({"type":"out", "op":op, "by":"index", "index":idx, "tensor":t})

func execute_pipeline(pipeline: int, mappings: Array) -> void:
    _execs.append(mappings)
    print("[MockSMR] execute_pipeline pipeline=", pipeline)
    print("  ops=", _ops.size(), " tensors=", _tensors.size())
    for e in _edges:
        print("  ", e)
    print("  mappings=", mappings)

