#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

namespace drivers {

typedef enum {
    URDF_SUCCESS = 0,
    URDF_ERROR_INVALID_HANDLE = -1,
    URDF_ERROR_INVALID_PARAMETER = -2,
    URDF_ERROR_FILE_NOT_FOUND = -3,
    URDF_ERROR_PARSE_FAILED = -4,
    URDF_ERROR_INIT_FAILED = -5,
    URDF_ERROR_RENDER_FAILED = -6,
    URDF_ERROR_NO_MODEL_LOADED = -7,
    URDF_ERROR_JOINT_NOT_FOUND = -8,
    URDF_ERROR_OUT_OF_MEMORY = -9,
    URDF_ERROR_NOT_IMPLEMENTED = -10
} UrdfPluginError;

typedef enum {
    URDF_IMAGE_FORMAT_RGBA = 0,
    URDF_IMAGE_FORMAT_RGB = 1,
    URDF_IMAGE_FORMAT_PNG = 2,
    URDF_IMAGE_FORMAT_JPEG = 3
} UrdfImageFormat;

typedef enum {
    URDF_RENDER_MODE_SINGLE = 0,
    URDF_RENDER_MODE_CONTINUOUS = 1
} UrdfRenderMode;

typedef struct {
    float position[3];
    float look_at[3];
    float up[3];
    float fov_degrees;
    float near_clip;
    float far_clip;
} UrdfCameraConfig;

typedef struct {
    uint32_t width;
    uint32_t height;
    bool transparent_background;
    float background_color[4];
    uint32_t anti_aliasing;
} UrdfRenderConfig;

typedef struct {
    uint8_t* data;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    size_t data_size;
    UrdfImageFormat format;
} UrdfImageData;

typedef struct {
    char name[256];
    double current_angle;
    double min_limit;
    double max_limit;
    int joint_type;
} UrdfJointInfo;

} // namespace drivers
