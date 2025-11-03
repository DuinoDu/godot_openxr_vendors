extends Node3D

const SUBMIT_INTERVAL_MS := 50

var _initialized := false
var securemr = null
var assets_dir := "res://assets"
var fw: int = 0
var pipeline_handle: int = 0
var tensors_map: Dictionary = {}
var global_tensors: Dictionary = {}
var _pipeline_bindings: Array = []
var _pipeline_ready: bool = false
var _last_submit_msec: int = 0
var passthrough_enabled: bool = false

@onready var world_environment: WorldEnvironment = $WorldEnvironment if has_node("WorldEnvironment") else null

func _ready():
	set_process(false)
	if Engine.is_editor_hint():
		return

	var oxr = XRServer.find_interface("OpenXR")
	if oxr:
		oxr.session_begun.connect(_on_openxr_session_begun, CONNECT_DEFERRED)
		if oxr.is_initialized():
			_on_openxr_session_begun()
	else:
		print("OpenXR interface not found.")

func _on_openxr_session_begun():
	if _initialized:
		return
	_initialized = true

	print("[MNIST] OpenXR session begun.")
	print("[MNIST] Has OpenXRPicoSecureMRExtensionWrapper singleton: ", Engine.has_singleton("OpenXRPicoSecureMRExtensionWrapper"))
	if Engine.has_singleton("OpenXRPicoSecureMRExtensionWrapper"):
		var ext = Engine.get_singleton("OpenXRPicoSecureMRExtensionWrapper")
		if ext and ext.has_method("is_secure_mr_supported"):
			print("[MNIST] Runtime reports SecureMR supported: ", ext.is_secure_mr_supported())

	# Start passthrough by default — mirror meta-scene-sample approach
	enable_passthrough(true)

	securemr = OpenXRPicoSecureMR.get_singleton()
	if securemr == null:
		print("[MNIST] OpenXRPicoSecureMR is null")
		return
	if not securemr.is_supported():
		print("[MNIST] OpenXRPicoSecureMR not supported.")
		return

	_deserialize_pipeline()
	_run_cpu_readback()

func enable_passthrough(enable: bool) -> void:
	if passthrough_enabled == enable:
		return
	var xr_interface: XRInterface = XRServer.find_interface("OpenXR")
	if xr_interface == null or not xr_interface.is_initialized():
		return
	var supported_blend_modes: Array = xr_interface.get_supported_environment_blend_modes()
	if XRInterface.XR_ENV_BLEND_MODE_ALPHA_BLEND in supported_blend_modes and XRInterface.XR_ENV_BLEND_MODE_OPAQUE in supported_blend_modes:
		if enable:
			xr_interface.environment_blend_mode = XRInterface.XR_ENV_BLEND_MODE_ALPHA_BLEND
			get_viewport().transparent_bg = true
			if world_environment:
				world_environment.environment.background_color = Color(0.0, 0.0, 0.0, 0.0)
		else:
			xr_interface.environment_blend_mode = XRInterface.XR_ENV_BLEND_MODE_OPAQUE
			get_viewport().transparent_bg = false
			if world_environment:
				world_environment.environment.background_color = Color(0.3, 0.3, 0.3, 1.0)
		passthrough_enabled = enable

func _deserialize_pipeline():
	fw = int(securemr.create_framework(3248, 2464))
	print("Framework created: ", fw)
	if fw == 0:
		print("[MNIST] Failed to create framework")
		return

	var json_path = assets_dir + "/mnist_pipeline.json"
	if not FileAccess.file_exists(json_path):
		print("Pipeline JSON not found: ", json_path)
		return

	var spec_str = FileAccess.get_file_as_string(json_path)
	var spec = JSON.parse_string(spec_str)
	if typeof(spec) != TYPE_DICTIONARY:
		print("Invalid pipeline JSON")
		return

	var res: Dictionary = securemr.deserialize_pipeline(fw, spec, assets_dir)
	pipeline_handle = int(res.get("pipeline", 0))
	tensors_map = res.get("tensors", {})
	print("Deserialized pipeline: ", pipeline_handle)
	_create_global_tensors()

func _run_cpu_readback():
	if fw == 0 or pipeline_handle == 0 or not _pipeline_ready:
		print("[MNIST] Framework or pipeline not ready; skip readback")
		return

	var rb = null
	if Engine.has_singleton("OpenXRPicoReadbackTensorExtensionWrapper"):
		rb = Engine.get_singleton("OpenXRPicoReadbackTensorExtensionWrapper")
		if rb.has_method("debug_info"):
			print("[MNIST][Readback Debug] ", rb.debug_info())

	if not _submit_pipeline(true):
		print("[MNIST] Failed to submit pipeline before readback")
		return

	var crop_global: int = int(global_tensors.get("cropped_image", 0))
	if crop_global == 0:
		print("[MNIST] crop_global == 0; skip readback")
		return

	if rb != null and rb.is_readback_supported():
		var cpu_bytes: PackedByteArray = rb.readback_global_tensor_cpu(crop_global)
		if cpu_bytes.size() == 224 * 224 * 3:
			var img := Image.create_from_data(224, 224, false, Image.FORMAT_RGB8, cpu_bytes)
			var cpu_path := OS.get_user_data_dir().path_join("mnist_readback_cpu.png")
			var err := img.save_png(cpu_path)
			print("[MNIST][CPU] Saved:", cpu_path, " err=", err)
		else:
			print("[MNIST][CPU] Unexpected byte size:", cpu_bytes.size())
	else:
		print("[MNIST][CPU] readback is not supported")

func _create_global_tensors() -> void:
	_pipeline_ready = false
	_pipeline_bindings.clear()
	if fw == 0 or pipeline_handle == 0:
		return

	global_tensors.clear()

	var dims_class := PackedInt32Array()
	dims_class.push_back(1)
	global_tensors["predicted_class"] = int(securemr.create_global_tensor_shape(fw, dims_class, 5, 1, 2, false)) # INT32 scalar

	var dims_score := PackedInt32Array()
	dims_score.push_back(1)
	global_tensors["predicted_score"] = int(securemr.create_global_tensor_shape(fw, dims_score, 6, 1, 2, false)) # FLOAT32 scalar

	var dims_crop := PackedInt32Array()
	dims_crop.push_back(224)
	dims_crop.push_back(224)
	global_tensors["cropped_image"] = int(securemr.create_global_tensor_shape(fw, dims_crop, 1, 3, 6, false)) # UINT8 RGB MAT

	for key in global_tensors.keys():
		if int(global_tensors[key]) == 0:
			print("[MNIST] Failed to create global tensor for ", key)
			return

	_build_pipeline_bindings()
	if _pipeline_bindings.size() != 3:
		print("[MNIST] Pipeline bindings incomplete; submit disabled")
		return

	_pipeline_ready = true
	_last_submit_msec = 0
	set_process(true)

func _build_pipeline_bindings() -> void:
	_pipeline_bindings.clear()
	var mapping_names := ["predicted_class", "predicted_score", "cropped_image"]
	for name in mapping_names:
		var local_handle := int(tensors_map.get(name, 0))
		var global_handle := int(global_tensors.get(name, 0))
		if local_handle == 0 or global_handle == 0:
			print("[MNIST] Missing tensor binding for ", name)
			continue
		_pipeline_bindings.append({
			"local": local_handle,
			"global": global_handle,
		})

func _submit_pipeline(force: bool = false) -> bool:
	if not _pipeline_ready or _pipeline_bindings.is_empty():
		return false
	var now := Time.get_ticks_msec()
	if not force and _last_submit_msec != 0 and now - _last_submit_msec < SUBMIT_INTERVAL_MS:
		return false
	securemr.execute_pipeline(pipeline_handle, _pipeline_bindings)
	_last_submit_msec = now
	return true

func _process(_delta: float) -> void:
	_submit_pipeline()
