extends Node3D

const DEFAULT_MODEL_DIR := "user://assets"
const MODEL_BIN := "model.serialized.bin"
const MODEL_JSON := "model.serialized.json"
const FRAMEWORK_WIDTH := 640
const FRAMEWORK_HEIGHT := 480
const READBACK_TIMEOUT_MS := 5000
const READBACK_POLL_MS := 50

const TENSOR_DATA_TYPE_UINT8 := 1
const TENSOR_DATA_TYPE_INT8 := 2
const TENSOR_DATA_TYPE_UINT16 := 3
const TENSOR_DATA_TYPE_INT16 := 4
const TENSOR_DATA_TYPE_INT32 := 5
const TENSOR_DATA_TYPE_FLOAT32 := 6
const TENSOR_DATA_TYPE_FLOAT64 := 7
const TENSOR_DATA_TYPE_DYNAMIC_TEXTURE_UINT8 := 8
const TENSOR_DATA_TYPE_DYNAMIC_TEXTURE_FLOAT32 := 9

const TENSOR_TYPE_SCALAR := 2
const TENSOR_TYPE_MAT := 6

const MODEL_ENCODING_FLOAT32 := 1
const MODEL_ENCODING_UFIXED_POINT8 := 2
const MODEL_ENCODING_SFIXED_POINT8 := 3
const MODEL_ENCODING_UFIXED_POINT16 := 4
const MODEL_ENCODING_INT32 := 5
const CAMERA_PERMISSION := "android.permission.CAMERA"

var securemr: OpenXRPicoSecureMR
var framework_handle: int = 0
var pipeline_handle: int = 0
var input_bindings: Array = []
var output_bindings: Array = []
var placeholder_bindings: Array = []
var output_targets: Array = []
var readback_handle: int = 0
var model_data: PackedByteArray = PackedByteArray()
var model_name := "model_inspect"
var model_input_name := "input"
var output_dir := "user://model_inspect"

var _initialized := false
var _init_ok := false
var _submitted := false
var _readback_done := false
var _submit_time_ms: int = 0
var _remaining_outputs := 0
var _xr_interface: XRInterface = null

func _ready() -> void:
	set_process(false)
	if Engine.is_editor_hint():
		return

	_ensure_camera_permission()

	_xr_interface = XRServer.find_interface("OpenXR")
	if _xr_interface:
		if not _xr_interface.is_initialized():
			var ok: bool = _xr_interface.initialize()
			if not ok:
				printerr("[ModelInspect] Failed to initialize OpenXR interface.")
				return
			print("[ModelInspect] OpenXR interface initialized.")
		XRServer.set_primary_interface(_xr_interface)
		_enable_xr_viewport()
		_enable_passthrough(true)
		_xr_interface.session_begun.connect(_on_openxr_session_begun, CONNECT_DEFERRED)
		if _xr_interface.is_initialized():
			_on_openxr_session_begun()
	else:
		printerr("[ModelInspect] OpenXR interface not found.")

func _exit_tree() -> void:
	if securemr and readback_handle != 0:
		securemr.stop_tensor_readback(readback_handle)
		readback_handle = 0
	if securemr and pipeline_handle != 0:
		securemr.destroy_pipeline(pipeline_handle)
		pipeline_handle = 0
	if securemr and framework_handle != 0:
		securemr.destroy_framework(framework_handle)
		framework_handle = 0

func _on_openxr_session_begun() -> void:
	if _initialized:
		return
	_enable_xr_viewport()
	_enable_passthrough(true)
	_initialized = true
	_init_ok = _initialize_pipeline()
	set_process(true)

func _enable_xr_viewport() -> void:
	if _xr_interface and _xr_interface.is_initialized():
		get_viewport().use_xr = true

func _enable_passthrough(enable: bool) -> void:
	if not _xr_interface or not _xr_interface.is_initialized():
		return
	var supported = _xr_interface.get_supported_environment_blend_modes()
	if enable and XRInterface.XR_ENV_BLEND_MODE_ALPHA_BLEND in supported:
		_xr_interface.environment_blend_mode = XRInterface.XR_ENV_BLEND_MODE_ALPHA_BLEND
		get_viewport().transparent_bg = true
	elif XRInterface.XR_ENV_BLEND_MODE_OPAQUE in supported:
		_xr_interface.environment_blend_mode = XRInterface.XR_ENV_BLEND_MODE_OPAQUE
		get_viewport().transparent_bg = false

func _process(_delta: float) -> void:
	if not _init_ok:
		set_process(false)
		return

	if not _submitted:
		_submit_pipeline()
	elif not _readback_done:
		_poll_readback()
	else:
		set_process(false)

func _initialize_pipeline() -> bool:
	securemr = OpenXRPicoSecureMR.get_singleton()
	if securemr == null:
		printerr("[ModelInspect] OpenXRPicoSecureMR singleton unavailable.")
		return false
	if not securemr.is_supported():
		printerr("[ModelInspect] SecureMR extension not supported on this runtime.")
		return false

	var model_dir := ProjectSettings.globalize_path(DEFAULT_MODEL_DIR)
	if not _ensure_model_assets(model_dir):
		printerr("[ModelInspect] Failed to stage model assets into %s" % model_dir)
		return false
	var input_path := ""
	var candidate_input := model_dir.path_join("input.bin")
	if FileAccess.file_exists(candidate_input):
		input_path = candidate_input

	var bin_path := model_dir.path_join(MODEL_BIN)
	var json_path := model_dir.path_join(MODEL_JSON)
	if not FileAccess.file_exists(bin_path):
		printerr("[ModelInspect] Missing model binary at %s" % bin_path)
		return false
	if not FileAccess.file_exists(json_path):
		printerr("[ModelInspect] Missing model JSON at %s" % json_path)
		return false

	model_data = FileAccess.get_file_as_bytes(bin_path)
	if model_data.is_empty():
		printerr("[ModelInspect] Failed to read model binary %s" % bin_path)
		return false
	var json_text := FileAccess.get_file_as_string(json_path)
	var parsed_json = JSON.parse_string(json_text)
	if typeof(parsed_json) != TYPE_DICTIONARY:
		printerr("[ModelInspect] Failed to parse JSON spec at %s" % json_path)
		return false

	var prep: Dictionary = securemr.prepare_bindings(parsed_json)
	for warning in prep.get("warnings", []):
		print("[ModelInspect][Warn] %s" % warning)
	if not prep.get("ok", false):
		for err in prep.get("errors", []):
			printerr("[ModelInspect][Error] %s" % err)
		return false

	input_bindings = prep.get("inputs", [])
	output_bindings = prep.get("outputs", [])
	model_name = prep.get("model_name", "model_inspect")
	if input_bindings.is_empty() or output_bindings.is_empty():
		printerr("[ModelInspect] Parsed bindings are empty.")
		return false
	model_input_name = input_bindings[0].get("name", "input")

	framework_handle = securemr.create_framework(FRAMEWORK_WIDTH, FRAMEWORK_HEIGHT)
	if framework_handle == 0:
		printerr("[ModelInspect] Failed to create SecureMR framework.")
		return false
	pipeline_handle = securemr.create_pipeline(framework_handle)
	if pipeline_handle == 0:
		printerr("[ModelInspect] Failed to create SecureMR pipeline.")
		return false

	placeholder_bindings.clear()
	output_targets.clear()
	DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(output_dir))

	for i in input_bindings.size():
		input_bindings[i] = _create_binding(input_bindings[i], true, input_path)
	for i in output_bindings.size():
		output_bindings[i] = _create_binding(output_bindings[i], false, "")
		output_targets.append({
			"global_tensor": output_bindings[i].get("global", 0),
			"name": output_bindings[i].get("name", "output"),
			"dimensions": output_bindings[i].get("dimensions", PackedInt32Array()),
			"channels": output_bindings[i].get("channels", 1),
			"data_type": output_bindings[i].get("data_type", 0),
		})

	_remaining_outputs = output_targets.size()
	print("[ModelInspect] Prepared %d inputs, %d outputs for model '%s'." % [input_bindings.size(), output_bindings.size(), model_name])
	return true

func _create_binding(binding: Dictionary, is_input: bool, input_path: String) -> Dictionary:
	var dims: PackedInt32Array = binding.get("dimensions", PackedInt32Array())
	var channels: int = binding.get("channels", 1)
	var data_type: int = binding.get("data_type", TENSOR_DATA_TYPE_FLOAT32)
	var usage: int = binding.get("usage", TENSOR_TYPE_MAT)

	var placeholder = securemr.create_pipeline_tensor_shape(pipeline_handle, dims, data_type, channels, usage, true)
	var global_tensor = securemr.create_global_tensor_shape(framework_handle, dims, data_type, channels, usage, false)
	binding["placeholder"] = placeholder
	binding["global"] = global_tensor
	placeholder_bindings.append({"local": placeholder, "global": global_tensor})

	if is_input:
		var payload := _load_input_payload(dims, channels, data_type, input_path, binding.get("name", "input"))
		if not payload.is_empty():
			securemr.reset_global_tensor_bytes(global_tensor, payload)
	else:
		var expected_bytes: int = int(securemr.element_count(dims, channels) * securemr.bytes_per_element(data_type))
		if expected_bytes > 0:
			var zero := PackedByteArray()
			zero.resize(expected_bytes)
			securemr.reset_global_tensor_bytes(global_tensor, zero)

	return binding

func _encoding_for_data_type(data_type: int) -> int:
	match data_type:
		TENSOR_DATA_TYPE_UINT8:
			return MODEL_ENCODING_UFIXED_POINT8
		TENSOR_DATA_TYPE_INT8:
			return MODEL_ENCODING_SFIXED_POINT8
		TENSOR_DATA_TYPE_UINT16:
			return MODEL_ENCODING_UFIXED_POINT16
		TENSOR_DATA_TYPE_INT32:
			return MODEL_ENCODING_INT32
		_:
			return MODEL_ENCODING_FLOAT32

func _submit_pipeline() -> void:
	if pipeline_handle == 0:
		return

	var output_names := PackedStringArray()
	var output_encodings := PackedInt32Array()
	for binding in output_bindings:
		output_names.push_back(binding.get("name", "output"))
		var enc_idx := output_encodings.size()
		output_encodings.resize(enc_idx + 1)
		output_encodings[enc_idx] = _encoding_for_data_type(binding.get("data_type", TENSOR_DATA_TYPE_FLOAT32))

	var op_model = securemr.create_operator_model(pipeline_handle, model_data, model_name, model_input_name, output_names, output_encodings)
	for binding in input_bindings:
		securemr.set_operator_input_by_name(pipeline_handle, op_model, binding.get("placeholder", 0), binding.get("name", "input"))
	for binding in output_bindings:
		securemr.set_operator_output_by_name(pipeline_handle, op_model, binding.get("placeholder", 0), binding.get("name", "output"))

	if readback_handle == 0 and not output_targets.is_empty():
		readback_handle = securemr.start_tensor_readback(output_targets, READBACK_POLL_MS)
		if readback_handle == 0:
			printerr("[ModelInspect] Failed to start readback worker.")
		else:
			print("[ModelInspect] Readback worker started (handle=%d)." % readback_handle)

	_submit_time_ms = Time.get_ticks_msec()
	securemr.execute_pipeline(pipeline_handle, placeholder_bindings)
	_submitted = true
	print("[ModelInspect] Pipeline submitted; awaiting readback.")

func _poll_readback() -> void:
	if readback_handle == 0:
		_readback_done = true
		return
	var results: Array = securemr.poll_tensor_readback(readback_handle)
	if results.is_empty():
		if Time.get_ticks_msec() - _submit_time_ms > READBACK_TIMEOUT_MS:
			printerr("[ModelInspect] Readback timed out.")
			securemr.stop_tensor_readback(readback_handle)
			readback_handle = 0
			_readback_done = true
		return

	for result in results:
		_handle_readback(result)

func _handle_readback(result: Dictionary) -> void:
	var tensor_name: String = result.get("name", "output")
	var payload: PackedByteArray = result.get("data", PackedByteArray())
	if payload.is_empty():
		print("[ModelInspect] Readback future for %s failed with empty payload" % tensor_name)
		return
	var dims: PackedInt32Array = result.get("dimensions", PackedInt32Array())
	var out_path := output_dir.path_join("model_inspect_output_%s.bin" % tensor_name)
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	if f:
		f.store_buffer(payload)
		f.close()
		print("[ModelInspect] Dumped %s bytes to %s (dims=%s)" % [payload.size(), out_path, dims])
	_log_preview(tensor_name, payload, result.get("data_type", 0))

	_remaining_outputs -= 1
	if _remaining_outputs <= 0:
		securemr.stop_tensor_readback(readback_handle)
		readback_handle = 0
		_readback_done = true
		print("[ModelInspect] Readback finished.")

func _load_input_payload(dims: PackedInt32Array, channels: int, data_type: int, input_path: String, name: String) -> PackedByteArray:
	var expected_bytes: int = int(securemr.element_count(dims, channels) * securemr.bytes_per_element(data_type))
	if expected_bytes <= 0:
		return PackedByteArray()

	if input_path.is_empty():
		print("[ModelInspect] Generating random input for %s" % name)
		return securemr.generate_random_data(dims, channels, data_type)

	if not FileAccess.file_exists(input_path):
		print("[ModelInspect] Input file %s not found; generating random data for %s" % [input_path, name])
		return securemr.generate_random_data(dims, channels, data_type)

	var f := FileAccess.open(input_path, FileAccess.READ)
	if f == null:
		print("[ModelInspect] Failed to open input file %s; generating random data for %s" % [input_path, name])
		return securemr.generate_random_data(dims, channels, data_type)

	var data := f.get_buffer(expected_bytes)
	if data.size() < expected_bytes:
		var padded := PackedByteArray()
		padded.resize(expected_bytes)
		for i in data.size():
			padded[i] = data[i]
		data = padded
		print("[ModelInspect] Input file shorter than expected for %s (%d < %d), padding with zeros" % [name, f.get_length(), expected_bytes])
	return data

func _log_preview(name: String, payload: PackedByteArray, data_type: int) -> void:
	var values: Array = []
	var spb := StreamPeerBuffer.new()
	spb.big_endian = false
	spb.data_array = payload

	match data_type:
		TENSOR_DATA_TYPE_FLOAT32, TENSOR_DATA_TYPE_DYNAMIC_TEXTURE_FLOAT32:
			var count = min(8, payload.size() / 4)
			for i in count:
				values.append(spb.get_float())
		TENSOR_DATA_TYPE_FLOAT64:
			var count64 = min(8, payload.size() / 8)
			for i in count64:
				values.append(spb.get_double())
		TENSOR_DATA_TYPE_INT32:
			var count32 = min(8, payload.size() / 4)
			for i in count32:
				values.append(spb.get_32())
		TENSOR_DATA_TYPE_INT16:
			var count16 = min(8, payload.size() / 2)
			for i in count16:
				values.append(spb.get_16())
		TENSOR_DATA_TYPE_UINT16:
			var countu16 = min(8, payload.size() / 2)
			for i in countu16:
				values.append(spb.get_u16())
		_:
			var cnt = min(8, payload.size())
			for i in cnt:
				values.append(payload[i])

	var preview := []
	for v in values:
		preview.append(str(v))
	print("[ModelInspect] %s preview [%s]" % [name, ", ".join(preview)])

func _read_property(prop: String) -> String:
	var output: Array = []
	var exit_code := OS.execute("getprop", [prop], output, true, true)
	if exit_code != 0 or output.is_empty():
		return ""
	return output[0].strip_edges()

func _ensure_camera_permission() -> void:
	if not OS.has_feature("Android"):
		return
	if not OS.has_permission(CAMERA_PERMISSION):
		OS.request_permission(CAMERA_PERMISSION)

func _ensure_model_assets(model_dir: String) -> bool:
	DirAccess.make_dir_recursive_absolute(model_dir)
	var files := [
		{"src": "res://assets/mnist.serialized.bin", "dst": model_dir.path_join(MODEL_BIN)},
		{"src": "res://assets/mnist.serialized.json", "dst": model_dir.path_join(MODEL_JSON)},
	]
	for f in files:
		var src: String = f["src"]
		var dst: String = f["dst"]
		if FileAccess.file_exists(dst):
			continue
		var data := FileAccess.get_file_as_bytes(src)
		if data.is_empty():
			printerr("[ModelInspect] Failed to copy %s to %s" % [src, dst])
			return false
		var fh := FileAccess.open(dst, FileAccess.WRITE)
		if fh == null:
			printerr("[ModelInspect] Failed to open %s for writing" % dst)
			return false
		fh.store_buffer(data)
		fh.close()
		print("[ModelInspect] Staged %s -> %s" % [src, dst])
	return true
