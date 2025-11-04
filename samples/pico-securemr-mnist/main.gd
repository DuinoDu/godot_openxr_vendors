extends Node3D

const SUBMIT_INTERVAL_MS := 33
const CAMERA_PERMISSION := "android.permission.CAMERA"
const IMAGE_WIDTH := 512
const IMAGE_HEIGHT := 512
const IMAGE_CHANNELS := 3
const TENSOR_DATA_TYPE_UINT8 := 1
const TENSOR_TYPE_MAT := 6

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
var _camera_permission_granted: bool = false
var _camera_permission_requested: bool = false
var _left_image_placeholder: int = 0
var _left_image_global: int = 0
var _readback_registered: bool = false

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
		
	_ensure_camera_permission()

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

	_try_build_pipeline()

	_last_submit_msec = 0
	set_process(true)

func _exit_tree() -> void:
	if securemr != null and _left_image_global != 0 and _readback_registered:
		securemr.release_readback(_left_image_global)
		_readback_registered = false

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

func _process(_delta: float) -> void:
	_ensure_camera_permission()
	_try_build_pipeline()

	var now := Time.get_ticks_msec()
	if _last_submit_msec != 0 and now - _last_submit_msec < SUBMIT_INTERVAL_MS:
		return
	if not _pipeline_ready or _pipeline_bindings.is_empty():
		return

	if securemr == null:
		return

	securemr.execute_pipeline(pipeline_handle, _pipeline_bindings)

	if _readback_registered and _left_image_global != 0:
		var data: PackedByteArray = securemr.pop_readback(_left_image_global)
		if data.size() > 0:
			_on_readback_result(_left_image_global, data)
		securemr.request_readback(_left_image_global)

	_last_submit_msec = now

func _ensure_camera_permission() -> void:
	if _camera_permission_granted:
		return

	if CAMERA_PERMISSION in OS.get_granted_permissions():
		_camera_permission_granted = true
		return

	if not _camera_permission_requested:
		if not OS.is_connected("on_request_permissions_result", Callable(self, "_on_request_permissions_result")):
			OS.connect("on_request_permissions_result", Callable(self, "_on_request_permissions_result"))
		_camera_permission_requested = true
		var granted := OS.request_permission(CAMERA_PERMISSION)
		if granted:
			_camera_permission_granted = true

func _on_request_permissions_result(permission: String, granted: bool) -> void:
	if permission != CAMERA_PERMISSION:
		return
	_camera_permission_requested = false
	_camera_permission_granted = granted
	if granted:
		print("[MNIST] Camera permission granted.")
		call_deferred("_try_build_pipeline")
	else:
		print("[MNIST] Camera permission denied. SecureMR readback unavailable.")

func _try_build_pipeline() -> void:
	if _pipeline_ready:
		return
	if securemr == null or not _camera_permission_granted:
		return

	if fw == 0:
		fw = securemr.create_framework(IMAGE_WIDTH, IMAGE_HEIGHT)
		if fw == 0:
			print("[MNIST] Failed to create SecureMR framework.")
			return
		print("[MNIST] SecureMR framework created (handle=%d)." % fw)

	if pipeline_handle == 0:
		pipeline_handle = securemr.create_pipeline(fw)
		if pipeline_handle == 0:
			print("[MNIST] Failed to create SecureMR pipeline.")
			return
		print("[MNIST] SecureMR pipeline created (handle=%d)." % pipeline_handle)

	if _left_image_placeholder == 0:
		var dims := PackedInt32Array([IMAGE_HEIGHT, IMAGE_WIDTH])
		_left_image_placeholder = securemr.create_pipeline_tensor_shape(pipeline_handle, dims, TENSOR_DATA_TYPE_UINT8, IMAGE_CHANNELS, TENSOR_TYPE_MAT, true)
		if _left_image_placeholder == 0:
			print("[MNIST] Failed to create pipeline tensor placeholder for VST image.")
			return

	if _left_image_global == 0:
		var dims := PackedInt32Array([IMAGE_HEIGHT, IMAGE_WIDTH])
		_left_image_global = securemr.create_global_tensor_shape(fw, dims, TENSOR_DATA_TYPE_UINT8, IMAGE_CHANNELS, TENSOR_TYPE_MAT, false)
		if _left_image_global == 0:
			print("[MNIST] Failed to create global tensor for VST image.")
			return
		global_tensors["left_vst"] = _left_image_global

	if _pipeline_bindings.is_empty():
		securemr.op_camera_access(pipeline_handle, _left_image_placeholder, 0, 0, 0)
		tensors_map["left_vst_placeholder"] = _left_image_placeholder
		_pipeline_bindings = [{
			"local": _left_image_placeholder,
			"global": _left_image_global,
		}]

	_pipeline_ready = true
	if not _readback_registered and _left_image_global != 0:
		_readback_registered = securemr.ensure_readback(_left_image_global, SUBMIT_INTERVAL_MS)
		if _readback_registered:
			print("[MNIST] Readback registered for global tensor %d" % _left_image_global)
			securemr.request_readback(_left_image_global)
		else:
			print("[MNIST] Failed to register readback for tensor %d" % _left_image_global)

func _on_readback_result(handle: int, data: PackedByteArray) -> void:
	if handle != _left_image_global:
		return
	var expected_size := IMAGE_WIDTH * IMAGE_HEIGHT * IMAGE_CHANNELS
	if data.size() > 0:
		print("[MNIST][Readback] left_vst shape: %dx%dx%d bytes=%d%s" % [
			IMAGE_HEIGHT,
			IMAGE_WIDTH,
			IMAGE_CHANNELS,
			data.size(),
			"" if data.size() == expected_size else " (expected %d)" % expected_size
		])
	else:
		print("[MNIST][Readback] left_vst readback returned empty buffer.")
	print("[MNIST] Readback pipeline ready. Placeholder=%d Global=%d" % [_left_image_placeholder, _left_image_global])
