package goface

/*
#include <stdlib.h>
#include "goface.h"
*/
import "C"

import (
	"fmt"
	"image"
	"os"
	"unsafe"
)

// ============================================================================
// Global Init / Deinit
// ============================================================================

// resolveModelPath ensures we pass a .tar file path to the SDK.
// The bundled libInspireFace v1.2.3 expects a tar archive; passing a directory
// triggers a double-free crash in its internal mtar error path.
func resolveModelPath(p string) (string, error) {
	st, err := os.Stat(p)
	if err != nil {
		// Path does not exist — check for a corresponding .tar file.
		tarPath := p + ".tar"
		if _, err2 := os.Stat(tarPath); err2 == nil {
			return tarPath, nil
		}
		return "", fmt.Errorf("model path does not exist: %w", err)
	}
	if !st.IsDir() {
		return p, nil
	}
	// Path is a directory — try the corresponding .tar file.
	tarPath := p + ".tar"
	if _, err := os.Stat(tarPath); err == nil {
		return tarPath, nil
	}
	return "", fmt.Errorf("model path is a directory and corresponding archive %q not found; please provide a .tar file or create %s", tarPath, tarPath)
}

// Init launches the global InspireFace SDK. Must be called once before any sessions are created.
func Init(opt InitOption) error {
	modelPath, err := resolveModelPath(opt.ModelPath)
	if err != nil {
		return err
	}
	cPath := C.CString(modelPath)
	defer C.free(unsafe.Pointer(cPath))

	cOpt := C.goface_init_opt_t{
		model_path: cPath,
		backend:    C.goface_backend_t(opt.Backend),
		log_level:  C.int(opt.LogLevel),
	}

	ret := C.goface_init(&cOpt)
	if ret != 0 {
		return fmt.Errorf("goface_init failed: %s", StrError(int(ret)))
	}
	return nil
}

// Deinit terminates the global SDK and frees all resources.
func Deinit() {
	C.goface_deinit()
}

// ============================================================================
// Session
// ============================================================================

// Session wraps a face-algorithm session. It is NOT safe for concurrent use.
type Session struct {
	handle unsafe.Pointer
}

// NewSession creates a session. Call Init() before using this.
func NewSession(opt SessionOption) (*Session, error) {
	cOpt := gofaceSessionOpt(opt)

	ptr := C.goface_session_create(&cOpt)
	if ptr == nil {
		return nil, fmt.Errorf("goface_session_create failed")
	}
	return &Session{handle: unsafe.Pointer(ptr)}, nil
}

// gofaceSessionOpt maps SessionOption to its C counterpart (shared by NewSession and LowLevelSessionCreate).
func gofaceSessionOpt(opt SessionOption) C.goface_session_opt_t {
	return C.goface_session_opt_t{
		max_faces:             C.int(opt.MaxFaces),
		detect_pixel_level:    C.int(opt.DetectPixelLevel),
		enable_recognition:    C.int(opt.EnableRecognition),
		enable_face_pose:      C.int(opt.EnableFacePose),
		enable_quality:        C.int(opt.EnableQuality),
		enable_liveness:       C.int(opt.EnableLiveness),
		enable_mask_detect:    C.int(opt.EnableMaskDetect),
		skip_aligned_image:    C.int(opt.SkipAlignedImage),
		detect_mode:           C.int(opt.DetectMode),
		track_detect_interval: C.int(opt.TrackDetectInterval),
	}
}

// SetTrackDetectInterval sets the full-detect interval (in ExecuteFaceTrack calls) for a
// LIGHT_TRACK session. It overrides the value given at session creation.
func (s *Session) SetTrackDetectInterval(num int) error {
	if s == nil || s.handle == nil {
		return fmt.Errorf("session is closed")
	}
	ret := C.goface_session_set_track_detect_interval((*C.goface_session_t)(s.handle), C.int(num))
	if ret != 0 {
		return fmt.Errorf("set track detect interval failed: %s", StrError(int(ret)))
	}
	return nil
}

// Close destroys the session and releases associated memory.
func (s *Session) Close() {
	if s != nil && s.handle != nil {
		C.goface_session_destroy((*C.goface_session_t)(s.handle))
		s.handle = nil
	}
}

// Detect runs face detection with default rotation (Rotation0).
func (s *Session) Detect(data []byte, width, height int, format ImageFormat) ([]Face, error) {
	return s.DetectWithRotation(data, width, height, format, Rotation0)
}

// DetectWithRotation runs face detection, feature extraction and aligned-face cropping.
// All heavy lifting happens inside the C layer; Go only marshals I/O.
func (s *Session) DetectWithRotation(data []byte, width, height int, format ImageFormat, rotation Rotation) ([]Face, error) {
	if s == nil || s.handle == nil {
		return nil, fmt.Errorf("session is closed")
	}
	if len(data) == 0 || width <= 0 || height <= 0 {
		return nil, fmt.Errorf("invalid image parameters")
	}

	var cResult *C.goface_result_t
	ret := C.goface_session_detect(
		(*C.goface_session_t)(s.handle),
		(*C.uint8_t)(unsafe.Pointer(&data[0])),
		C.int(width),
		C.int(height),
		C.goface_format_t(format),
		C.goface_rotation_t(rotation),
		&cResult,
	)
	if ret != 0 {
		return nil, fmt.Errorf("goface_session_detect failed: %s", StrError(int(ret)))
	}
	if cResult == nil {
		return nil, fmt.Errorf("goface_session_detect returned nil result")
	}
	defer C.goface_result_free(cResult)

	count := int(C.goface_result_face_count(cResult))
	faces := make([]Face, 0, count)

	for i := 0; i < count; i++ {
		var x, y, w, h C.int
		var roll, yaw, pitch, confidence C.float
		var trackID, trackCount C.int
		var feature *C.float
		var featureSize C.int
		var faceImg *C.uint8_t
		var fiw, fih, fic C.int

		cRet := C.goface_result_get_face(
			cResult, C.int(i),
			&x, &y, &w, &h,
			&roll, &yaw, &pitch,
			&confidence,
			&trackID, &trackCount,
			&feature, &featureSize,
			&faceImg, &fiw, &fih, &fic,
		)
		if cRet != 0 {
			continue
		}

		f := Face{
			Rect: image.Rect(int(x), int(y), int(x)+int(w), int(y)+int(h)),
			Roll: float32(roll),
			Yaw:  float32(yaw),
			Pitch: float32(pitch),
			Confidence: float32(confidence),
			TrackID:    int32(trackID),
			TrackCount: int32(trackCount),
		}

		if feature != nil && featureSize > 0 {
			featSlice := (*[1 << 28]C.float)(unsafe.Pointer(feature))[:featureSize:featureSize]
			f.Feature = make([]float32, featureSize)
			for j := 0; j < int(featureSize); j++ {
				f.Feature[j] = float32(featSlice[j])
			}
		}

		if faceImg != nil && fiw > 0 && fih > 0 {
			sz := int(fiw) * int(fih) * int(fic)
			f.FaceImage = &FaceImage{
				Data:     C.GoBytes(unsafe.Pointer(faceImg), C.int(sz)),
				Width:    int(fiw),
				Height:   int(fih),
				Channels: int(fic),
			}
		}

		faces = append(faces, f)
	}

	return faces, nil
}

// ============================================================================
// Feature utilities
// ============================================================================

// CompareFeatures returns cosine similarity in [-1, 1].
func CompareFeatures(a, b []float32) float32 {
	if len(a) == 0 || len(a) != len(b) {
		return -2.0
	}
	return float32(C.goface_feature_compare(
		(*C.float)(unsafe.Pointer(&a[0])),
		(*C.float)(unsafe.Pointer(&b[0])),
		C.int(len(a)),
	))
}

// RecommendedThreshold returns the SDK-recommended cosine threshold.
func RecommendedThreshold() float32 {
	return float32(C.goface_feature_recommended_threshold())
}

// StrError converts an InspireFace error code to a string.
func StrError(code int) string {
	return C.GoString(C.goface_strerror(C.int(code)))
}

// ============================================================================
// Low-level API (zero-copy friendly)
// ============================================================================

// LowLevelSession exposes the raw C session pointer for advanced use.
type LowLevelSession struct {
	Ptr unsafe.Pointer
}

// LowLevelDetect calls the C detect routine directly.
// 'data' must remain valid for the duration of the call.
// On success, resultPtr is an opaque *C.goface_result_t that must be freed with LowLevelResultFree.
func LowLevelDetect(sessionPtr unsafe.Pointer, data []byte, width, height int, format ImageFormat, rotation Rotation) (resultPtr unsafe.Pointer, count int, err error) {
	if sessionPtr == nil {
		return nil, 0, fmt.Errorf("nil session")
	}
	if len(data) == 0 {
		return nil, 0, fmt.Errorf("empty data")
	}

	var cResult *C.goface_result_t
	ret := C.goface_session_detect(
		(*C.goface_session_t)(sessionPtr),
		(*C.uint8_t)(unsafe.Pointer(&data[0])),
		C.int(width), C.int(height),
		C.goface_format_t(format),
		C.goface_rotation_t(rotation),
		&cResult,
	)
	if ret != 0 {
		return nil, 0, fmt.Errorf("detect failed: %s", StrError(int(ret)))
	}
	cnt := int(C.goface_result_face_count(cResult))
	return unsafe.Pointer(cResult), cnt, nil
}

// LowLevelResultCount returns the number of faces in an opaque result handle.
func LowLevelResultCount(resultPtr unsafe.Pointer) int {
	if resultPtr == nil {
		return 0
	}
	return int(C.goface_result_face_count((*C.goface_result_t)(resultPtr)))
}

// LowLevelResultGetFace extracts one face from an opaque result handle into the supplied Face value.
// The Face.Feature and FaceImage.Data slices are freshly allocated on each call.
func LowLevelResultGetFace(resultPtr unsafe.Pointer, index int, face *Face) error {
	if resultPtr == nil || face == nil {
		return fmt.Errorf("nil argument")
	}

	var x, y, w, h C.int
	var roll, yaw, pitch, confidence C.float
	var trackID, trackCount C.int
	var feature *C.float
	var featureSize C.int
	var faceImg *C.uint8_t
	var fiw, fih, fic C.int

	ret := C.goface_result_get_face(
		(*C.goface_result_t)(resultPtr), C.int(index),
		&x, &y, &w, &h,
		&roll, &yaw, &pitch,
		&confidence,
		&trackID, &trackCount,
		&feature, &featureSize,
		&faceImg, &fiw, &fih, &fic,
	)
	if ret != 0 {
		return fmt.Errorf("goface_result_get_face failed")
	}

	*face = Face{
		Rect:       image.Rect(int(x), int(y), int(x)+int(w), int(y)+int(h)),
		Roll:       float32(roll),
		Yaw:        float32(yaw),
		Pitch:      float32(pitch),
		Confidence: float32(confidence),
		TrackID:    int32(trackID),
		TrackCount: int32(trackCount),
	}

	if feature != nil && featureSize > 0 {
		featSlice := (*[1 << 28]C.float)(unsafe.Pointer(feature))[:featureSize:featureSize]
		face.Feature = make([]float32, featureSize)
		for j := 0; j < int(featureSize); j++ {
			face.Feature[j] = float32(featSlice[j])
		}
	}

	if faceImg != nil && fiw > 0 && fih > 0 {
		sz := int(fiw) * int(fih) * int(fic)
		face.FaceImage = &FaceImage{
			Data:     C.GoBytes(unsafe.Pointer(faceImg), C.int(sz)),
			Width:    int(fiw),
			Height:   int(fih),
			Channels: int(fic),
		}
	}

	return nil
}

// LowLevelResultFree releases an opaque result handle obtained from LowLevelDetect.
func LowLevelResultFree(resultPtr unsafe.Pointer) {
	if resultPtr != nil {
		C.goface_result_free((*C.goface_result_t)(resultPtr))
	}
}

// LowLevelSessionCreate creates a raw C session. Use LowLevelSessionDestroy to free it.
func LowLevelSessionCreate(opt SessionOption) (unsafe.Pointer, error) {
	cOpt := gofaceSessionOpt(opt)
	ptr := C.goface_session_create(&cOpt)
	if ptr == nil {
		return nil, fmt.Errorf("goface_session_create failed")
	}
	return unsafe.Pointer(ptr), nil
}

// LowLevelSessionDestroy destroys a raw C session.
func LowLevelSessionDestroy(sessionPtr unsafe.Pointer) {
	if sessionPtr != nil {
		C.goface_session_destroy((*C.goface_session_t)(sessionPtr))
	}
}
