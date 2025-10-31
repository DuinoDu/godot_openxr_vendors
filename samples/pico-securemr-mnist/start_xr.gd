class_name StartXR
extends Node3D

signal focus_lost
signal focus_gained
signal pose_recentered

@export var maximum_refresh_rate: int = 90

var xr_interface: OpenXRInterface
var xr_is_focussed := false

func _ready():
	xr_interface = XRServer.find_interface("OpenXR")
	if xr_interface and xr_interface.is_initialized():
		var vp: Viewport = get_viewport()
		vp.use_xr = true
		DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)

		if RenderingServer.get_rendering_device():
			vp.vrs_mode = Viewport.VRS_XR

		xr_interface.session_begun.connect(_on_openxr_session_begun)
		xr_interface.session_visible.connect(_on_openxr_visible_state)
		xr_interface.session_focussed.connect(_on_openxr_focused_state)
		xr_interface.session_stopping.connect(_on_openxr_stopping)
		xr_interface.pose_recentered.connect(_on_openxr_pose_recentered)
		print("OpenXR is instantiated!")
	else:
		print("OpenXR not instantiated!")
		# On desktop/editor this is expected.

func _on_openxr_session_begun() -> void:
	var current_refresh_rate = xr_interface.get_display_refresh_rate()
	var available_rates: Array = xr_interface.get_available_display_refresh_rates()
	var new_rate = current_refresh_rate
	for rate in available_rates:
		if rate > new_rate and rate <= maximum_refresh_rate:
			new_rate = rate
	if current_refresh_rate != new_rate:
		xr_interface.set_display_refresh_rate(new_rate)
		current_refresh_rate = new_rate
	if current_refresh_rate > 0:
		Engine.physics_ticks_per_second = current_refresh_rate

func _on_openxr_visible_state() -> void:
	if xr_is_focussed:
		xr_is_focussed = false
		process_mode = Node.PROCESS_MODE_DISABLED
		emit_signal("focus_lost")

func _on_openxr_focused_state() -> void:
	xr_is_focussed = true
	process_mode = Node.PROCESS_MODE_INHERIT
	emit_signal("focus_gained")

func _on_openxr_stopping() -> void:
	pass

func _on_openxr_pose_recentered() -> void:
	emit_signal("pose_recentered")
