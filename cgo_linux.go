//go:build linux && amd64 && !rk3588 && !cuda

package goface

/*
#cgo CFLAGS: -I${SRCDIR}/third_party/inspireface/include
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib/inspireface/lib/linux_x86 -L${SRCDIR}/third_party/inspireface/lib/linux_x86 -L/usr/local/lib -L/usr/lib -L/usr/lib/x86_64-linux-gnu -lInspireFace -lm -ldl
#cgo LDFLAGS: -Wl,-rpath,\$ORIGIN
#cgo LDFLAGS: -Wl,-rpath,\$ORIGIN/lib/inspireface/lib/linux_x86
#cgo LDFLAGS: -Wl,-rpath,\$ORIGIN/lib
#cgo LDFLAGS: -Wl,-rpath,${SRCDIR}/third_party/inspireface/lib/linux_x86
*/
import "C"
