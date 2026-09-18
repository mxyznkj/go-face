/**
 * goface.h
 * Go-Face C API Layer
 * All core InspireFace logic is encapsulated here.
 * CGO only does I/O exchange with this layer.
 */

#ifndef GOFACE_H
#define GOFACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Image pixel formats */
typedef enum {
    GOFACE_FORMAT_RGB   = 0,
    GOFACE_FORMAT_BGR   = 1,
    GOFACE_FORMAT_RGBA  = 2,
    GOFACE_FORMAT_BGRA  = 3,
    GOFACE_FORMAT_NV12  = 4,
    GOFACE_FORMAT_NV21  = 5,
    GOFACE_FORMAT_I420  = 6,
    GOFACE_FORMAT_GRAY  = 7,
} goface_format_t;

/* Rotation angles */
typedef enum {
    GOFACE_ROTATION_0   = 0,
    GOFACE_ROTATION_90  = 1,
    GOFACE_ROTATION_180 = 2,
    GOFACE_ROTATION_270 = 3,
} goface_rotation_t;

/* Session detection mode (maps to HFDetectMode) */
typedef enum {
    GOFACE_DETECT_MODE_ALWAYS_DETECT = 0,  /* Image mode: full detect every call */
    GOFACE_DETECT_MODE_LIGHT_TRACK   = 1,  /* Video mode: track every call, full detect periodically */
} goface_detect_mode_t;

/* Image processing backend */
typedef enum {
    GOFACE_BACKEND_AUTO = 0,  /* Probe hardware accel first, fallback to CPU */
    GOFACE_BACKEND_CPU  = 1,
    GOFACE_BACKEND_RGA  = 2,  /* Rockchip RGA (RK3588 etc.) */
} goface_backend_t;

/* Initialization options */
typedef struct {
    const char*       model_path;   /* Path to InspireFace resource pack */
    goface_backend_t  backend;      /* Desired image-processing backend */
    int               log_level;    /* 0=error, 1=warn, 2=info, 3=debug */
} goface_init_opt_t;

/* Session creation options */
typedef struct {
    int max_faces;            /* Maximum faces to detect (default 10) */
    int detect_pixel_level;   /* 160, 320, 640, -1 = default 320 */
    int enable_recognition;   /* Extract face embedding (0/1) */
    int enable_face_pose;     /* Estimate roll/yaw/pitch (0/1) */
    int enable_quality;       /* Face quality assessment (0/1) */
    int enable_liveness;      /* RGB liveness detection (0/1) */
    int enable_mask_detect;   /* Mask detection (0/1) */
    int skip_aligned_image;  /* Skip per-face aligned image fetch (0/1, default 0 = fetch, legacy behavior) */
    int detect_mode;          /* goface_detect_mode_t, default ALWAYS_DETECT */
    int track_detect_interval; /* LIGHT_TRACK: full-detect every N calls; 0 = SDK default (20) */
} goface_session_opt_t;

/* Opaque handle to detection results */
typedef struct goface_result goface_result_t;

/* Opaque session handle */
typedef struct goface_session goface_session_t;

/**
 * Global initialization. Must be called once before any other API.
 * Returns 0 on success, non-zero InspireFace error code on failure.
 */
int goface_init(const goface_init_opt_t* opt);

/**
 * Global de-initialization. Releases all InspireFace resources.
 */
void goface_deinit(void);

/**
 * Create a face algorithm session.
 * Returns NULL on failure.
 */
goface_session_t* goface_session_create(const goface_session_opt_t* opt);

/**
 * Destroy a session and free its resources.
 */
void goface_session_destroy(goface_session_t* session);

/**
 * Core detection function (ALL logic lives in C).
 *
 * Performs face detection, feature extraction, and aligned-face cropping
 * inside a single C call.
 *
 * @param session   Valid session handle.
 * @param data      Raw image bytes (caller retains ownership).
 * @param width     Image width.
 * @param height    Image height.
 * @param format    Pixel format.
 * @param rotation  Camera rotation.
 * @param result    OUT: handle to detection results (caller must free).
 * @return 0 on success, non-zero error code on failure.
 */
int goface_session_detect(goface_session_t* session,
                          const uint8_t* data,
                          int width, int height,
                          goface_format_t format,
                          goface_rotation_t rotation,
                          goface_result_t** result);

/**
 * Return number of faces stored in result.
 */
int goface_result_face_count(const goface_result_t* result);

/**
 * Retrieve a single face from result set.
 *
 * All OUT pointers may be NULL if the caller is not interested in that field.
 * Buffers pointed to by feature and face_image remain valid until
 * goface_result_free() is called.
 *
 * @param result              Result handle.
 * @param index               Face index [0, count).
 * @param x,y,w,h            OUT: bounding box.
 * @param roll,yaw,pitch     OUT: head pose angles.
 * @param confidence         OUT: detection confidence.
 * @param track_id           OUT: track id (LightTrack mode; -1 if unavailable).
 * @param track_count        OUT: tracked frame count (LightTrack mode; 0 if unavailable).
 * @param feature            OUT: pointer to float array (feature_size elements).
 * @param feature_size       OUT: dimension of feature vector.
 * @param face_image         OUT: pointer to RGB/BGR byte array.
 * @param face_img_w         OUT: aligned face image width.
 * @param face_img_h         OUT: aligned face image height.
 * @param face_img_channels  OUT: aligned face image channels.
 * @return 0 on success.
 */
int goface_result_get_face(const goface_result_t* result, int index,
                           int* x, int* y, int* w, int* h,
                           float* roll, float* yaw, float* pitch,
                           float* confidence,
                           int* track_id, int* track_count,
                           const float** feature, int* feature_size,
                           const uint8_t** face_image,
                           int* face_img_w, int* face_img_h,
                           int* face_img_channels);

/**
 * Set the full-detect interval (in ExecuteFaceTrack calls) for a LIGHT_TRACK session.
 * Returns 0 on success.
 */
int goface_session_set_track_detect_interval(goface_session_t* session, int num);

/**
 * Free a result object and all internally held buffers.
 */
void goface_result_free(goface_result_t* result);

/**
 * Cosine similarity between two feature vectors.
 * @return similarity in [-1, 1].
 */
float goface_feature_compare(const float* a, const float* b, int size);

/**
 * Recommended cosine threshold for same-person judgement.
 */
float goface_feature_recommended_threshold(void);

/**
 * Convert InspireFace error code to human-readable string.
 */
const char* goface_strerror(int errcode);

#ifdef __cplusplus
}
#endif

#endif /* GOFACE_H */
