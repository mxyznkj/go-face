package goface

import "image"

// ImageFormat represents the pixel format of raw image bytes.
type ImageFormat int

const (
	FormatRGB   ImageFormat = 0
	FormatBGR   ImageFormat = 1
	FormatRGBA  ImageFormat = 2
	FormatBGRA  ImageFormat = 3
	FormatNV12  ImageFormat = 4
	FormatNV21  ImageFormat = 5
	FormatI420  ImageFormat = 6
	FormatGRAY  ImageFormat = 7
)

// Rotation represents the camera rotation of the image.
type Rotation int

const (
	Rotation0   Rotation = 0
	Rotation90  Rotation = 1
	Rotation180 Rotation = 2
	Rotation270 Rotation = 3
)

// Backend selects the image-processing backend.
type Backend int

const (
	BackendAuto Backend = 0 // Probe hardware accel first, fallback to CPU
	BackendCPU  Backend = 1
	BackendRGA  Backend = 2 // Rockchip RGA (RK3588 etc.)
)

// DetectMode selects the session detection mode (maps to HFDetectMode).
type DetectMode int

const (
	// DetectModeAlwaysDetect: image mode, full detect every call (default, legacy behavior).
	DetectModeAlwaysDetect DetectMode = 0
	// DetectModeLightTrack: video mode, light tracking every call with a periodic full detect.
	// TrackID/TrackCount are only meaningful in this mode.
	DetectModeLightTrack DetectMode = 1
)

// FaceImage holds the aligned face image returned by the SDK.
type FaceImage struct {
	Data     []byte
	Width    int
	Height   int
	Channels int
}

// Face holds detection / recognition result for a single face.
type Face struct {
	Rect       image.Rectangle
	Roll       float32
	Yaw        float32
	Pitch      float32
	Confidence float32
	TrackID    int32 // stable track id per continuous face (LightTrack mode; -1 if unavailable)
	TrackCount int32 // number of frames this track has been followed (0 if unavailable)
	Feature    []float32  // embedding vector; nil if recognition disabled
	FaceImage  *FaceImage // aligned face image; nil if unavailable
}

// InitOption configures global SDK initialization.
type InitOption struct {
	ModelPath string  // Path to InspireFace resource pack (.tar archive or directory; directories are auto-resolved to <dir>.tar)
	Backend   Backend // Desired image-processing backend
	LogLevel  int     // 0=error, 1=warn, 2=info, 3=debug
}

// SessionOption configures a face algorithm session.
type SessionOption struct {
	MaxFaces            int // Maximum number of faces to detect (default 10)
	DetectPixelLevel    int // Detector input resolution: 160, 320, 640; -1 = default 320
	EnableRecognition   int // Extract face embedding (0 or 1)
	EnableFacePose      int // Estimate head pose angles (0 or 1)
	EnableQuality       int // Face quality assessment (0 or 1)
	EnableLiveness      int // RGB liveness detection (0 or 1)
	EnableMaskDetect    int // Mask detection (0 or 1)
	SkipAlignedImage    int // Skip per-face aligned image fetch (0 or 1, default 0 = fetch); set 1 for pure tracking sessions to save per-face cost
	DetectMode          DetectMode // ALWAYS_DETECT (default) or LIGHT_TRACK
	TrackDetectInterval int // LIGHT_TRACK only: run full detect every N ExecuteFaceTrack calls; 0 = SDK default (20)
}

// DefaultSessionOption returns a SessionOption with sensible defaults.
func DefaultSessionOption() SessionOption {
	return SessionOption{
		MaxFaces:            10,
		DetectPixelLevel:    -1,
		EnableRecognition:   1,
		EnableFacePose:      1,
		EnableQuality:       0,
		EnableLiveness:      0,
		EnableMaskDetect:    0,
		SkipAlignedImage:    0,
		DetectMode:          DetectModeAlwaysDetect,
		TrackDetectInterval: 0,
	}
}
