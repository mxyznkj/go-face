//go:build linux && amd64 && cuda

package goface

import "fmt"

// initAuto 的 CUDA 变体：优先 TensorRT 模型（Megatron_TRT，需 NVIDIA GPU 与
// TensorRT 运行时），初始化失败依次回退 CPU 模型 Megatron / Pikachu。
func initAuto(basePath string) (string, error) {
	candidates := []string{"Megatron_TRT", "Megatron", "Pikachu"}
	for _, name := range candidates {
		modelPath := joinModelPath(basePath, name)
		if err := fallbackInit(modelPath); err == nil {
			return modelPath, nil
		}
		// Local model missing — try downloading from the built-in whitelist.
		if err := DownloadModel(basePath, name); err == nil {
			if err := fallbackInit(modelPath); err == nil {
				return modelPath, nil
			}
		}
	}
	return "", fmt.Errorf("all models failed")
}
