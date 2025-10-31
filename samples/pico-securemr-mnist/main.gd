extends Node3D

var _initialized := false

func _ready():
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
	var setting_key := "xr/openxr/extensions/pico/secure_mixed_reality"
	if ProjectSettings.has_setting(setting_key):
		print("[MNIST] Project setting '", setting_key, "' = ", ProjectSettings.get_setting(setting_key))
	else:
		print("[MNIST] Project setting '", setting_key, "' is missing")

	print("[MNIST] Has OpenXRPicoSecureMRExtensionWrapper singleton: ", Engine.has_singleton("OpenXRPicoSecureMRExtensionWrapper"))
	if Engine.has_singleton("OpenXRPicoSecureMRExtensionWrapper"):
		var ext = Engine.get_singleton("OpenXRPicoSecureMRExtensionWrapper")
		if ext and ext.has_method("is_secure_mr_supported"):
			print("[MNIST] Runtime reports SecureMR supported: ", ext.is_secure_mr_supported())

	var securemr = OpenXRPicoSecureMR.get_singleton()
	if securemr == null:
		print("[MNIST] OpenXRPicoSecureMR is null")
		return
	if not securemr.is_supported():
		print("[MNIST] OpenXRPicoSecureMR not supported.")
		return

	var fw = securemr.create_framework(3248, 2464)
	print("Framework created: ", fw)

	var assets_dir = "res://assets"
	var json_path = assets_dir + "/mnist_pipeline.json"
	if not FileAccess.file_exists(json_path):
		print("Pipeline JSON not found: ", json_path)
		return

	var spec_str = FileAccess.get_file_as_string(json_path)
	var spec = JSON.parse_string(spec_str)
	if typeof(spec) != TYPE_DICTIONARY:
		print("Invalid pipeline JSON")
		return

	var res = securemr.deserialize_pipeline(fw, spec, assets_dir)
	print("Deserialized pipeline: ", res.get("pipeline", 0))

	# Optionally, you could run once by mapping placeholder to globals if needed.
