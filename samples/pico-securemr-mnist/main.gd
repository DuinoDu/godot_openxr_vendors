extends Node

const SMR = preload("res://samples/pico-securemr-mnist/securemr_utils.gd")

var xr_interface: OpenXRInterface
var securemr

func _ready():
    xr_interface = XRServer.find_interface("OpenXR")
    if xr_interface == null:
        push_error("OpenXR interface not found")
        return

    # Initialize OpenXR and start XR so we have a valid XrSession.
    if not xr_interface.is_initialized():
        var ok := xr_interface.initialize()
        if not ok:
            push_error("Failed to initialize OpenXR interface")
            return
    get_viewport().use_xr = true

    securemr = OpenXRPicoSecureMRExtensionWrapper.get_singleton()
    if securemr == null or !securemr.is_secure_mr_supported():
        push_error("Pico Secure MR extension not supported or not enabled in project settings")
        return

    # NOTE: framework size should match camera image. Use project settings if provided.
    var img_w := ProjectSettings.get_setting_with_override("pico/secure_mr/image_width", 1280)
    var img_h := ProjectSettings.get_setting_with_override("pico/secure_mr/image_height", 720)
    var framework := securemr.create_framework(img_w, img_h)
    if framework == 0:
        push_error("Failed to create SecureMR framework")
        return

    var pipeline := securemr.create_pipeline(framework)
    if pipeline == 0:
        push_error("Failed to create SecureMR pipeline")
        return

    # Operators
    var vst_op := securemr.create_operator_basic(pipeline, SMR.OP_RECTIFIED_VST_ACCESS)
    var get_affine_op := securemr.create_operator_basic(pipeline, SMR.OP_GET_AFFINE)
    var apply_affine_op := securemr.create_operator_basic(pipeline, SMR.OP_APPLY_AFFINE)
    var convert_color_op := securemr.create_operator_convert_color(pipeline, SMR.CVT_RGB2GRAY)
    var assignment_op := securemr.create_operator_basic(pipeline, SMR.OP_ASSIGNMENT)
    var normalize_op := securemr.create_operator_arithmetic_compose(pipeline, "{0} / 255.0")

    # Tensors
    var image_w := int(img_w)
    var image_h := int(img_h)
    var crop_w := 224
    var crop_h := 224

    var raw_rgb := SMR.create_pipeline_mat(securemr, pipeline, image_w, image_h, SMR.DT_UINT8, 3, false)
    var crop_gray := SMR.create_pipeline_mat(securemr, pipeline, crop_w, crop_h, SMR.DT_UINT8, 1, false)
    var crop_gray_f := SMR.create_pipeline_mat(securemr, pipeline, crop_w, crop_h, SMR.DT_FLOAT32, 1, false)
    var crop_rgb_write := SMR.create_pipeline_mat(securemr, pipeline, crop_w, crop_h, SMR.DT_UINT8, 3, true)

    var src_pts := SMR.create_pipeline_points(securemr, pipeline, 3, SMR.DT_FLOAT32, 2, false)
    var dst_pts := SMR.create_pipeline_points(securemr, pipeline, 3, SMR.DT_FLOAT32, 2, false)
    var affine_mat := SMR.create_pipeline_mat(securemr, pipeline, 3, 2, SMR.DT_FLOAT32, 1, false)

    # Fill src/dst points for crop (simple centered square)
    var cx := float(image_w) * 0.5
    var cy := float(image_h) * 0.5
    var hw := float(crop_w) * 0.5
    var hh := float(crop_h) * 0.5
    var src_data := PackedFloat32Array([cx-hw, cy-hh, cx+hw, cy-hh, cx+hw, cy+hh])
    var dst_data := PackedFloat32Array([0.0, 0.0, crop_w, 0.0, crop_w, crop_h])
    securemr.reset_pipeline_tensor_floats(pipeline, src_pts, src_data)
    securemr.reset_pipeline_tensor_floats(pipeline, dst_pts, dst_data)

    # Model operator
    var model_bytes := FileAccess.get_file_as_bytes("res://assets/mnist.serialized.bin")
    if model_bytes.is_empty():
        push_error("Missing model asset: mnist.serialized.bin")
        return
    var out_names := PackedStringArray(["_538", "_539"]) # score, class
    var out_enc := PackedInt32Array([SMR.DT_FLOAT32, SMR.DT_INT32])
    var model_op := securemr.create_operator_model(pipeline, model_bytes, "mnist", "input_1", out_names, out_enc)

    # Global output tensors
    var pred_score_global := SMR.create_global_scalar(securemr, framework, SMR.DT_FLOAT32, 1, false)
    var pred_class_global := SMR.create_global_scalar(securemr, framework, SMR.DT_INT32, 1, false)
    var crop_rgb_global := SMR.create_global_mat(securemr, framework, crop_w, crop_h, SMR.DT_UINT8, 3, false)

    # Graph wiring
    securemr.set_operator_output_by_name(pipeline, vst_op, raw_rgb, "left image")

    securemr.set_operator_input_by_name(pipeline, get_affine_op, src_pts, "src")
    securemr.set_operator_input_by_name(pipeline, get_affine_op, dst_pts, "dst")
    securemr.set_operator_output_by_name(pipeline, get_affine_op, affine_mat, "result")

    securemr.set_operator_input_by_name(pipeline, apply_affine_op, affine_mat, "affine")
    securemr.set_operator_input_by_name(pipeline, apply_affine_op, raw_rgb, "src image")
    securemr.set_operator_output_by_name(pipeline, apply_affine_op, crop_rgb_write, "dst image")

    securemr.set_operator_input_by_name(pipeline, convert_color_op, crop_rgb_write, "src")
    securemr.set_operator_output_by_name(pipeline, convert_color_op, crop_gray, "dst")

    securemr.set_operator_input_by_name(pipeline, assignment_op, crop_gray, "src")
    securemr.set_operator_output_by_name(pipeline, assignment_op, crop_gray_f, "dst")

    securemr.set_operator_input_by_name(pipeline, normalize_op, crop_gray_f, "{0}")
    # Feed normalize output directly into the model input tensor
    securemr.set_operator_output_by_index(pipeline, normalize_op, 0, crop_gray_f)

    # Connect model I/O
    # input
    securemr.set_operator_input_by_name(pipeline, model_op, crop_gray_f, "input_1")
    # outputs
    var pred_score_write := SMR.create_pipeline_scalar(securemr, pipeline, SMR.DT_FLOAT32, 1, true)
    var pred_class_write := SMR.create_pipeline_scalar(securemr, pipeline, SMR.DT_INT32, 1, true)
    securemr.set_operator_output_by_name(pipeline, model_op, pred_score_write, "_538")
    securemr.set_operator_output_by_name(pipeline, model_op, pred_class_write, "_539")

    # Execute pipeline: map pipeline placeholders to global tensors
    var mappings := []
    mappings.append({"local": pred_class_write, "global": pred_class_global})
    mappings.append({"local": pred_score_write, "global": pred_score_global})
    mappings.append({"local": crop_rgb_write, "global": crop_rgb_global})
    securemr.execute_pipeline(pipeline, mappings)

    # Optional: display GLTF in Godot (preview). On device, you can also wire GLTF tensors via SecureMR.
    var display_gltf := bool(ProjectSettings.get_setting_with_override("pico/secure_mr/display_gltf", true))
    if display_gltf:
        if smoke_test:
            var gltf_scene := load("res://assets/tv.gltf")
            if gltf_scene and gltf_scene is PackedScene:
                var node := (gltf_scene as PackedScene).instantiate()
                add_child(node)
                node.translation = Vector3(0, 0, -1)
        else:
            # Device path: create GLTF tensors/operators so runtime can render.
            var tv_bytes := FileAccess.get_file_as_bytes("res://assets/tv.gltf")
            var gltf_global := securemr.create_global_tensor_gltf(framework, tv_bytes, false)

            # Create a pipeline placeholder tensor to bind GLTF in the graph and map it to the global GLTF tensor.
            var gltf_local := securemr.create_pipeline_tensor_gltf(pipeline, PackedByteArray(), true)

            # Optionally update GLTF world pose via UPDATE_GLTF operator.
            var upd_world_pose := securemr.create_operator_update_gltf(pipeline, SMR.GLTF_ATTR_WORLD_POSE)

            # Create a 4x4 identity transform as world pose input.
            var pose_mat := SMR.create_pipeline_mat(securemr, pipeline, 4, 4, SMR.DT_FLOAT32, 1, false)
            var identity16 := PackedFloat32Array([
                1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0,
            ])
            securemr.reset_pipeline_tensor_floats(pipeline, pose_mat, identity16)

            # Wire: input 0 = GLTF tensor, input 1 = world pose.
            securemr.set_operator_input_by_index(pipeline, upd_world_pose, gltf_local, 0)
            securemr.set_operator_input_by_index(pipeline, upd_world_pose, pose_mat, 1)

            # Map the local GLTF placeholder to the global GLTF tensor so runtime knows what to render.
            mappings.append({"local": gltf_local, "global": gltf_global})

            # Build a renderer pipeline to draw pred_class and pred_score as text into GLTF.
            var pipeline_r := securemr.create_pipeline(framework)
            # Operators: RenderText for class, SwitchGLTFRenderStatus to compose into world.
            var render_text_cls := securemr.create_operator_basic(pipeline_r, 32) # XR_SECURE_MR_OPERATOR_TYPE_RENDER_TEXT_PICO
            var render_gltf_cls := securemr.create_operator_basic(pipeline_r, 30) # XR_SECURE_MR_OPERATOR_TYPE_SWITCH_GLTF_RENDER_STATUS_PICO
            var render_text_score := securemr.create_operator_basic(pipeline_r, 32)
            var render_gltf_score := securemr.create_operator_basic(pipeline_r, 30)

            # Tensors for class text rendering.
            var start_cls := SMR.create_pipeline_points(securemr, pipeline_r, 1, SMR.DT_FLOAT32, 2, false)
            var colors_cls := securemr.create_pipeline_tensor_shape(pipeline_r, PackedInt32Array([2]), SMR.DT_UINT8, 4, SMR.TT_SCALAR, false)
            var tex_id_cls := SMR.create_pipeline_scalar(securemr, pipeline_r, 2, 1, false) # uint16 => use DT_UINT8*2, kept simple here
            var font_size_cls := SMR.create_pipeline_scalar(securemr, pipeline_r, SMR.DT_FLOAT32, 1, false)
            var pose_cls := SMR.create_pipeline_mat(securemr, pipeline_r, 4, 4, SMR.DT_FLOAT32, 1, false)
            var gltf_local_cls := securemr.create_pipeline_tensor_gltf(pipeline_r, PackedByteArray(), true)

            # Tensors for score text rendering.
            var start_scr := SMR.create_pipeline_points(securemr, pipeline_r, 1, SMR.DT_FLOAT32, 2, false)
            var colors_scr := SMR.create_pipeline_tensor_shape(pipeline_r, PackedInt32Array([2]), SMR.DT_UINT8, 4, SMR.TT_SCALAR, false)
            var tex_id_scr := SMR.create_pipeline_scalar(securemr, pipeline_r, 2, 1, false)
            var font_size_scr := SMR.create_pipeline_scalar(securemr, pipeline_r, SMR.DT_FLOAT32, 1, false)
            var pose_scr := SMR.create_pipeline_mat(securemr, pipeline_r, 4, 4, SMR.DT_FLOAT32, 1, false)
            var gltf_local_scr := securemr.create_pipeline_tensor_gltf(pipeline_r, PackedByteArray(), true)

            # Local placeholders to read predicted class/score mapped from global tensors.
            var pred_class_read := SMR.create_pipeline_scalar(securemr, pipeline_r, SMR.DT_INT32, 1, true)
            var pred_score_read := SMR.create_pipeline_scalar(securemr, pipeline_r, SMR.DT_FLOAT32, 1, true)

            # Fill defaults for positions/colors/font.
            var start_cls_xy := PackedFloat32Array([0.1, 0.1])
            var start_scr_xy := PackedFloat32Array([0.1, 0.2])
            securemr.reset_pipeline_tensor_floats(pipeline_r, start_cls, start_cls_xy)
            securemr.reset_pipeline_tensor_floats(pipeline_r, start_scr, start_scr_xy)

            var colors_rgba := PackedByteArray([255, 255, 255, 255, 255, 255, 0, 255])
            securemr.reset_pipeline_tensor_bytes(pipeline_r, colors_cls, colors_rgba)
            securemr.reset_pipeline_tensor_bytes(pipeline_r, colors_scr, colors_rgba)
            var font32 := PackedFloat32Array([32.0])
            securemr.reset_pipeline_tensor_floats(pipeline_r, font_size_cls, font32)
            securemr.reset_pipeline_tensor_floats(pipeline_r, font_size_scr, font32)
            securemr.reset_pipeline_tensor_floats(pipeline_r, pose_cls, identity16)
            securemr.reset_pipeline_tensor_floats(pipeline_r, pose_scr, identity16)

            # Wire class text rendering.
            securemr.set_operator_input_by_name(pipeline_r, render_text_cls, pred_class_read, "text")
            securemr.set_operator_input_by_name(pipeline_r, render_text_cls, start_cls, "start")
            securemr.set_operator_input_by_name(pipeline_r, render_text_cls, colors_cls, "colors")
            securemr.set_operator_input_by_name(pipeline_r, render_text_cls, tex_id_cls, "texture ID")
            securemr.set_operator_input_by_name(pipeline_r, render_text_cls, font_size_cls, "font size")
            securemr.set_operator_input_by_name(pipeline_r, render_text_cls, gltf_local_cls, "gltf")
            securemr.set_operator_input_by_name(pipeline_r, render_gltf_cls, gltf_local_cls, "gltf")
            securemr.set_operator_input_by_name(pipeline_r, render_gltf_cls, pose_cls, "world pose")

            # Wire score text rendering.
            securemr.set_operator_input_by_name(pipeline_r, render_text_score, pred_score_read, "text")
            securemr.set_operator_input_by_name(pipeline_r, render_text_score, start_scr, "start")
            securemr.set_operator_input_by_name(pipeline_r, render_text_score, colors_scr, "colors")
            securemr.set_operator_input_by_name(pipeline_r, render_text_score, tex_id_scr, "texture ID")
            securemr.set_operator_input_by_name(pipeline_r, render_text_score, font_size_scr, "font size")
            securemr.set_operator_input_by_name(pipeline_r, render_text_score, gltf_local_scr, "gltf")
            securemr.set_operator_input_by_name(pipeline_r, render_gltf_score, gltf_local_scr, "gltf")
            securemr.set_operator_input_by_name(pipeline_r, render_gltf_score, pose_scr, "world pose")

            # Execute renderer pipeline: map local placeholders to globals including predictions and GLTF.
            var mappings_r := []
            mappings_r.append({"local": gltf_local_cls, "global": gltf_global})
            mappings_r.append({"local": gltf_local_scr, "global": gltf_global})
            mappings_r.append({"local": pred_class_read, "global": pred_class_global})
            mappings_r.append({"local": pred_score_read, "global": pred_score_global})
            securemr.execute_pipeline(pipeline_r, mappings_r)

    var label := $StatusLabel if has_node("StatusLabel") else null
    if label:
        label.text = "Executed"
    print("SecureMR MNIST pipeline executed.")
