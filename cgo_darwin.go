//go:build darwin

package goface

/*
#cgo CFLAGS: -I${SRCDIR}/third_party/inspireface/include
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib/inspireface/lib/darwin_arm64 -L${SRCDIR}/third_party/inspireface/lib/darwin_arm64 -L/usr/local/lib -L/usr/lib -lInspireFace
#cgo LDFLAGS: -Wl,-rpath,${SRCDIR}/../../../../lib/inspireface/lib/darwin_arm64
#cgo LDFLAGS: -Wl,-rpath,${SRCDIR}/third_party/inspireface/lib/darwin_arm64
*/
import "C"
