/**
 * goface.c
 * Core implementation of goface C layer.
 * All InspireFace orchestration happens here; Go/CGO only does I/O.
 */

#include "goface.h"
#include "inspireface.h"
#include "herror.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>

/* ==========================================================================
 * Internal structures
 * ========================================================================== */

typedef struct {
    int    x, y, w, h;
    float  roll, yaw, pitch;
    float  confidence;
    int    track_id;       /* LightTrack mode; -1 if unavailable */
    int    track_count;    /* LightTrack mode; 0 if unavailable */

    float* feature;        /* owned, NULL if recognition disabled */
    int    feature_size;

    uint8_t* face_image;   /* owned aligned face image, NULL if failed */
    int      face_img_w;
    int      face_img_h;
    int      face_img_channels;
} goface_face_internal_t;

struct goface_result {
    int                     count;
    goface_face_internal_t* faces;
};

struct goface_session {
    HFSession handle;
    int       enable_recognition;
    int       enable_aligned_image;
    int       detect_mode;
};

static int g_initialized = 0;

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static int map_format(goface_format_t fmt) {
    switch (fmt) {
        case GOFACE_FORMAT_RGB:  return HF_STREAM_RGB;
        case GOFACE_FORMAT_BGR:  return HF_STREAM_BGR;
        case GOFACE_FORMAT_RGBA: return HF_STREAM_RGBA;
        case GOFACE_FORMAT_BGRA: return HF_STREAM_BGRA;
        case GOFACE_FORMAT_NV12: return HF_STREAM_YUV_NV12;
        case GOFACE_FORMAT_NV21: return HF_STREAM_YUV_NV21;
        case GOFACE_FORMAT_I420: return HF_STREAM_I420;
        case GOFACE_FORMAT_GRAY: return HF_STREAM_GRAY;
        default:                 return HF_STREAM_BGR;
    }
}

static int map_rotation(goface_rotation_t rot) {
    switch (rot) {
        case GOFACE_ROTATION_0:   return HF_CAMERA_ROTATION_0;
        case GOFACE_ROTATION_90:  return HF_CAMERA_ROTATION_90;
        case GOFACE_ROTATION_180: return HF_CAMERA_ROTATION_180;
        case GOFACE_ROTATION_270: return HF_CAMERA_ROTATION_270;
        default:                  return HF_CAMERA_ROTATION_0;
    }
}

static int map_log_level(int lvl) {
    switch (lvl) {
        case 0:  return HF_LOG_ERROR;
        case 1:  return HF_LOG_WARN;
        case 2:  return HF_LOG_INFO;
        default: return HF_LOG_DEBUG;
    }
}

/* ==========================================================================
 * Public API
 * ========================================================================== */

int goface_init(const goface_init_opt_t* opt) {
    if (!opt || !opt->model_path) return -1;

    struct stat st;
    if (stat(opt->model_path, &st) != 0) {
        fprintf(stderr, "[goface] Model path does not exist: %s\n", opt->model_path);
        return HERR_INVALID_PARAM;
    }

    HResult ret;
    ret = HFLaunchInspireFace(opt->model_path);
    if (ret != HSUCCEED) {
        fprintf(stderr, "[goface] HFLaunchInspireFace failed: %lu\n", (unsigned long)ret);
        return (int)ret;
    }

    /* Backend selection with automatic fallback */
    if (opt->backend == GOFACE_BACKEND_AUTO || opt->backend == GOFACE_BACKEND_RGA) {
        int rga_compiled = 0;
        HFQueryExpansiveHardwareRGACompileOption(&rga_compiled);
        if (rga_compiled && opt->backend == GOFACE_BACKEND_RGA) {
            HFSwitchImageProcessingBackend(HF_IMAGE_PROCESSING_RGA);
        } else {
            HFSwitchImageProcessingBackend(HF_IMAGE_PROCESSING_CPU);
        }
    } else {
        HFSwitchImageProcessingBackend(HF_IMAGE_PROCESSING_CPU);
    }

    HFSetLogLevel(map_log_level(opt->log_level));

    g_initialized = 1;
    return 0;
}

void goface_deinit(void) {
    if (g_initialized) {
        HFTerminateInspireFace();
        g_initialized = 0;
    }
}

goface_session_t* goface_session_create(const goface_session_opt_t* opt) {
    if (!g_initialized) {
        fprintf(stderr, "[goface] Not initialized. Call goface_init() first.\n");
        return NULL;
    }

    HOption option = HF_ENABLE_NONE;
    if (opt->enable_recognition) option |= HF_ENABLE_FACE_RECOGNITION;
    if (opt->enable_face_pose)   option |= HF_ENABLE_FACE_POSE;
    if (opt->enable_quality)     option |= HF_ENABLE_QUALITY;
    if (opt->enable_liveness)    option |= HF_ENABLE_LIVENESS;
    if (opt->enable_mask_detect) option |= HF_ENABLE_MASK_DETECT;

    HFSession session_handle = {0};
    int detect_mode = opt->detect_mode == GOFACE_DETECT_MODE_LIGHT_TRACK
                          ? HF_DETECT_MODE_LIGHT_TRACK
                          : HF_DETECT_MODE_ALWAYS_DETECT;
    HResult ret = HFCreateInspireFaceSessionOptional(
        option,
        detect_mode,
        opt->max_faces > 0 ? opt->max_faces : 10,
        opt->detect_pixel_level,
        -1,
        &session_handle);

    if (ret != HSUCCEED) {
        fprintf(stderr, "[goface] HFCreateInspireFaceSessionOptional failed: %lu\n", (unsigned long)ret);
        return NULL;
    }

    if (detect_mode == HF_DETECT_MODE_LIGHT_TRACK && opt->track_detect_interval > 0) {
        ret = HFSessionSetTrackModeDetectInterval(session_handle, opt->track_detect_interval);
        if (ret != HSUCCEED) {
            fprintf(stderr, "[goface] HFSessionSetTrackModeDetectInterval failed: %lu\n", (unsigned long)ret);
            HFReleaseInspireFaceSession(session_handle);
            return NULL;
        }
    }

    goface_session_t* s = (goface_session_t*)calloc(1, sizeof(goface_session_t));
    if (!s) {
        HFReleaseInspireFaceSession(session_handle);
        return NULL;
    }

    s->handle = session_handle;
    s->enable_recognition = opt->enable_recognition;
    s->enable_aligned_image = !opt->skip_aligned_image;
    s->detect_mode = opt->detect_mode;
    return s;
}

void goface_session_destroy(goface_session_t* session) {
    if (!session) return;
    if (session->handle) {
        HFReleaseInspireFaceSession(session->handle);
        session->handle = NULL;
    }
    free(session);
}

int goface_session_detect(goface_session_t* session,
                          const uint8_t* data,
                          int width, int height,
                          goface_format_t format,
                          goface_rotation_t rotation,
                          goface_result_t** result) {
    if (!session || !session->handle || !data || !result) return -1;
    if (width <= 0 || height <= 0) return -1;

    *result = NULL;
    HResult ret;

    /* Build image stream from raw buffer */
    HFImageData image_data = {0};
    image_data.data   = (HPUInt8)data;
    image_data.width  = width;
    image_data.height = height;
    image_data.format = map_format(format);
    image_data.rotation = map_rotation(rotation);

    HFImageStream stream = {0};
    ret = HFCreateImageStream(&image_data, &stream);
    if (ret != HSUCCEED) {
        fprintf(stderr, "[goface] HFCreateImageStream failed: %lu\n", (unsigned long)ret);
        return (int)ret;
    }

    /* Run face detection / tracking */
    HFMultipleFaceData multi_face = {0};
    ret = HFExecuteFaceTrack(session->handle, stream, &multi_face);
    if (ret != HSUCCEED) {
        HFReleaseImageStream(stream);
        fprintf(stderr, "[goface] HFExecuteFaceTrack failed: %lu\n", (unsigned long)ret);
        return (int)ret;
    }

    int count = multi_face.detectedNum;
    if (count <= 0) {
        HFReleaseImageStream(stream);
        /* No face is not an error; return empty result. */
        goface_result_t* res = (goface_result_t*)calloc(1, sizeof(goface_result_t));
        if (!res) return -1;
        *result = res;
        return 0;
    }

    goface_result_t* res = (goface_result_t*)calloc(1, sizeof(goface_result_t));
    if (!res) {
        HFReleaseImageStream(stream);
        return -1;
    }
    res->count = count;
    res->faces = (goface_face_internal_t*)calloc(count, sizeof(goface_face_internal_t));
    if (!res->faces) {
        free(res);
        HFReleaseImageStream(stream);
        return -1;
    }

    /* Pre-allocate one reusable FaceFeature object */
    HFFaceFeature reusable_feature = {0};
    if (session->enable_recognition) {
        ret = HFCreateFaceFeature(&reusable_feature);
        if (ret != HSUCCEED) {
            /* If allocation fails, silently disable recognition for this call */
            session->enable_recognition = 0;
        }
    }

    for (int i = 0; i < count; ++i) {
        goface_face_internal_t* f = &res->faces[i];

        /* Bounding box */
        HFaceRect r = multi_face.rects[i];
        f->x = r.x;
        f->y = r.y;
        f->w = r.width;
        f->h = r.height;

        /* Pose */
        if (multi_face.angles.roll) f->roll = multi_face.angles.roll[i];
        if (multi_face.angles.yaw)  f->yaw  = multi_face.angles.yaw[i];
        if (multi_face.angles.pitch) f->pitch = multi_face.angles.pitch[i];

        /* Confidence */
        if (multi_face.detConfidence) f->confidence = multi_face.detConfidence[i];

        /* Tracking (LightTrack mode) */
        f->track_id = -1;
        f->track_count = 0;
        if (multi_face.trackIds) f->track_id = multi_face.trackIds[i];
        if (multi_face.trackCounts) f->track_count = multi_face.trackCounts[i];

        HFFaceBasicToken token = multi_face.tokens[i];

        /* Feature extraction */
        if (session->enable_recognition) {
            ret = HFFaceFeatureExtractTo(session->handle, stream, token, reusable_feature);
            if (ret == HSUCCEED && reusable_feature.data && reusable_feature.size > 0) {
                int fsz = reusable_feature.size;
                f->feature = (float*)malloc(sizeof(float) * fsz);
                if (f->feature) {
                    memcpy(f->feature, reusable_feature.data, sizeof(float) * fsz);
                    f->feature_size = fsz;
                }
            }
        }

        /* Aligned face image */
        if (session->enable_aligned_image) {
            HFImageBitmap aligned_bitmap = {0};
            ret = HFFaceGetFaceAlignmentImage(session->handle, stream, token, &aligned_bitmap);
            if (ret == HSUCCEED && aligned_bitmap) {
                HFImageBitmapData bitmap_data = {0};
                ret = HFImageBitmapGetData(aligned_bitmap, &bitmap_data);
                if (ret == HSUCCEED && bitmap_data.data && bitmap_data.width > 0 && bitmap_data.height > 0) {
                    int channels = bitmap_data.channels > 0 ? bitmap_data.channels : 3;
                    size_t img_bytes = (size_t)bitmap_data.width * bitmap_data.height * channels;
                    f->face_image = (uint8_t*)malloc(img_bytes);
                    if (f->face_image) {
                        memcpy(f->face_image, bitmap_data.data, img_bytes);
                        f->face_img_w = bitmap_data.width;
                        f->face_img_h = bitmap_data.height;
                        f->face_img_channels = channels;
                    }
                }
                HFReleaseImageBitmap(aligned_bitmap);
            }
        }
    }

    if (session->enable_recognition && reusable_feature.data) {
        HFReleaseFaceFeature(&reusable_feature);
    }

    HFReleaseImageStream(stream);
    *result = res;
    return 0;
}

int goface_result_face_count(const goface_result_t* result) {
    return result ? result->count : 0;
}

int goface_result_get_face(const goface_result_t* result, int index,
                           int* x, int* y, int* w, int* h,
                           float* roll, float* yaw, float* pitch,
                           float* confidence,
                           int* track_id, int* track_count,
                           const float** feature, int* feature_size,
                           const uint8_t** face_image,
                           int* face_img_w, int* face_img_h,
                           int* face_img_channels) {
    if (!result || index < 0 || index >= result->count) return -1;

    const goface_face_internal_t* f = &result->faces[index];

    if (x) *x = f->x;
    if (y) *y = f->y;
    if (w) *w = f->w;
    if (h) *h = f->h;

    if (roll) *roll = f->roll;
    if (yaw)  *yaw  = f->yaw;
    if (pitch) *pitch = f->pitch;

    if (confidence) *confidence = f->confidence;

    if (track_id) *track_id = f->track_id;
    if (track_count) *track_count = f->track_count;

    if (feature)      *feature = f->feature;
    if (feature_size) *feature_size = f->feature_size;

    if (face_image)      *face_image = f->face_image;
    if (face_img_w)      *face_img_w = f->face_img_w;
    if (face_img_h)      *face_img_h = f->face_img_h;
    if (face_img_channels) *face_img_channels = f->face_img_channels;

    return 0;
}

int goface_session_set_track_detect_interval(goface_session_t* session, int num) {
    if (!session || !session->handle || num <= 0) return HERR_INVALID_PARAM;
    HResult ret = HFSessionSetTrackModeDetectInterval(session->handle, num);
    return ret == HSUCCEED ? 0 : (int)ret;
}

void goface_result_free(goface_result_t* result) {
    if (!result) return;
    if (result->faces) {
        for (int i = 0; i < result->count; ++i) {
            goface_face_internal_t* f = &result->faces[i];
            if (f->feature) {
                free(f->feature);
                f->feature = NULL;
            }
            if (f->face_image) {
                free(f->face_image);
                f->face_image = NULL;
            }
        }
        free(result->faces);
    }
    free(result);
}

float goface_feature_compare(const float* a, const float* b, int size) {
    if (!a || !b || size <= 0) return -2.0f;

    double dot = 0.0, na = 0.0, nb = 0.0;
    for (int i = 0; i < size; ++i) {
        dot += (double)a[i] * (double)b[i];
        na  += (double)a[i] * (double)a[i];
        nb  += (double)b[i] * (double)b[i];
    }
    double denom = sqrt(na) * sqrt(nb);
    if (denom < 1e-12) return 0.0f;
    return (float)(dot / denom);
}

float goface_feature_recommended_threshold(void) {
    float thr = 0.48f;
    HFGetRecommendedCosineThreshold(&thr);
    return thr;
}

const char* goface_strerror(int errcode) {
    switch (errcode) {
        case HSUCCEED: return "Success";
        case HERR_INVALID_PARAM: return "Invalid parameter";
        case HERR_INVALID_IMAGE_STREAM_HANDLE: return "Invalid image stream handle";
        case HERR_INVALID_CONTEXT_HANDLE: return "Invalid context handle";
        case HERR_INVALID_FACE_TOKEN: return "Invalid face token";
        case HERR_INVALID_FACE_FEATURE: return "Invalid face feature";
        case HERR_INVALID_FACE_LIST: return "Invalid face list";
        case HERR_INVALID_BUFFER_SIZE: return "Invalid buffer size";
        case HERR_INVALID_IMAGE_STREAM_PARAM: return "Invalid image stream param";
        case HERR_INVALID_SERIALIZATION_FAILED: return "Serialization failed";
        case HERR_INVALID_DETECTION_INPUT: return "Invalid detection input";
        case HERR_INVALID_IMAGE_BITMAP_HANDLE: return "Invalid image bitmap handle";
        case HERR_IMAGE_STREAM_DECODE_FAILED: return "Image stream decode failed";
        case HERR_SESS_FUNCTION_UNUSABLE: return "Session function unusable";
        case HERR_SESS_TRACKER_FAILURE: return "Session tracker failure";
        case HERR_SESS_PIPELINE_FAILURE: return "Session pipeline failure";
        case HERR_SESS_INVALID_RESOURCE: return "Session invalid resource";
        case HERR_SESS_REC_EXTRACT_FAILURE: return "Feature extraction failure";
        case HERR_FT_HUB_DISABLE: return "FeatureHub disabled";
        case HERR_FT_HUB_NOT_FOUND_FEATURE: return "Feature not found";
        case HERR_ARCHIVE_LOAD_FAILURE: return "Archive load failure";
        case HERR_ARCHIVE_LOAD_MODEL_FAILURE: return "Model load failure";
        case HERR_DEVICE_CUDA_NOT_SUPPORT: return "CUDA not supported";
        default: return "Unknown error";
    }
}
