//go:build linux && amd64 && cuda && !rk3588

package goface

// cuda 变体的 cgo 链接配置：链接 linux_x86_cuda 动态库（InspireFace + MNN CUDA/TensorRT）。

/*
#cgo CFLAGS: -I${SRCDIR}/third_party/inspireface/include
#cgo LDFLAGS: -L${SRCDIR}/../../../../lib/inspireface/lib/linux_x86_cuda -L${SRCDIR}/third_party/inspireface/lib/linux_x86_cuda -L/usr/local/lib -L/usr/lib -L/usr/lib/x86_64-linux-gnu -lInspireFace -lMNN -lMNN_Cuda_Main -lm -ldl
#cgo LDFLAGS: -Wl,-rpath,\$ORIGIN
#cgo LDFLAGS: -Wl,-rpath,\$ORIGIN/lib/inspireface/lib/linux_x86_cuda
#cgo LDFLAGS: -Wl,-rpath,\$ORIGIN/lib
#cgo LDFLAGS: -Wl,-rpath,${SRCDIR}/third_party/inspireface/lib/linux_x86_cuda
*/
import "C"
