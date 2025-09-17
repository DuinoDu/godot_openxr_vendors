extends Node

# Pico SecureMR enums (match Pico headers)
const DT_FLOAT32 := 1
const DT_UINT8 := 2
const DT_INT32 := 5

const TT_MAT := 1
const TT_POINT := 2
const TT_SCALAR := 4

const OP_ARITHMETIC_COMPOSE := 1
const OP_GET_AFFINE := 14
const OP_APPLY_AFFINE := 15
const OP_UV_TO_3D := 17
const OP_ASSIGNMENT := 18
const OP_RUN_MODEL := 19
const OP_NORMALIZE := 21
const OP_RECTIFIED_VST_ACCESS := 23
const OP_CONVERT_COLOR := 25

const CVT_RGB2GRAY := 7

# GLTF attribute enums (from XrSecureMrGltfOperatorAttributePICO)
const GLTF_ATTR_TEXTURE := 1
const GLTF_ATTR_ANIMATION := 2
const GLTF_ATTR_WORLD_POSE := 3
const GLTF_ATTR_LOCAL_TRANSFORM := 4
const GLTF_ATTR_MAT_METALLIC := 5
const GLTF_ATTR_MAT_ROUGHNESS := 6
const GLTF_ATTR_MAT_OCCLUSION_TEX := 7
const GLTF_ATTR_MAT_BASE_COLOR_FACTOR := 8
const GLTF_ATTR_MAT_EMISSIVE_FACTOR := 9
const GLTF_ATTR_MAT_EMISSIVE_STRENGTH := 10
const GLTF_ATTR_MAT_EMISSIVE_TEX := 11
const GLTF_ATTR_MAT_BASE_COLOR_TEX := 12
const GLTF_ATTR_MAT_NORMAL_MAP_TEX := 13
const GLTF_ATTR_MAT_METAL_ROUGH_TEX := 14

static func mat_shape(h: int, w: int) -> PackedInt32Array:
    return PackedInt32Array([h, w])

static func scalar_shape() -> PackedInt32Array:
    return PackedInt32Array([1])

static func points_shape(n: int) -> PackedInt32Array:
    return PackedInt32Array([n])

static func create_pipeline_mat(securemr, pipeline: int, w: int, h: int, dtype: int, channels: int, placeholder:=false) -> int:
    return securemr.create_pipeline_tensor_shape(pipeline, mat_shape(h, w), dtype, channels, TT_MAT, placeholder)

static func create_pipeline_scalar(securemr, pipeline: int, dtype: int, channels:=1, placeholder:=false) -> int:
    return securemr.create_pipeline_tensor_shape(pipeline, scalar_shape(), dtype, channels, TT_SCALAR, placeholder)

static func create_pipeline_points(securemr, pipeline: int, count: int, dtype: int, channels: int, placeholder:=false) -> int:
    return securemr.create_pipeline_tensor_shape(pipeline, points_shape(count), dtype, channels, TT_POINT, placeholder)

static func create_global_mat(securemr, framework: int, w: int, h: int, dtype: int, channels: int, placeholder:=false) -> int:
    return securemr.create_global_tensor_shape(framework, mat_shape(h, w), dtype, channels, TT_MAT, placeholder)

static func create_global_scalar(securemr, framework: int, dtype: int, channels:=1, placeholder:=false) -> int:
    return securemr.create_global_tensor_shape(framework, scalar_shape(), dtype, channels, TT_SCALAR, placeholder)
