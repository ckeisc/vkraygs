# Hyperscape Opacity Trace Report

**Date:** 2026-09-18
**Binary:** `libConstellusUnityPlugin.so` (ARM64, 6.2 MB, stripped)
**Question:** Why does Hyperscape render the garage door opaque while vkraygs renders it semi-transparent, from the same SPZ?

## Summary

The native renderer applies a GPU-side **alpha correction** via a shader uniform block named `alphaCorrectionParams`. The correction parameters are a 20-byte struct set at runtime from configuration (not hardcoded). **The exact mathematical formula could not be isolated** because it is implemented in the GPU shader, which is not present in the binary or in the extracted Unity assets. What follows is the traced mechanism, the evidence, and the limits.

## Traced call path

1. **`constellus::UnifiedGaussianSplatter::setAlphaCorrectionParams`**
   - Address: `0x284c20`, size: 24 bytes
   - Signature: `setAlphaCorrectionParams(const AlphaCorrectionParams&)`
   - Disassembly:
     ```
     0x284c20: ldr w8, [x1, #0x10]   ; load 4 bytes from params+16
     0x284c24: ldr q0, [x1]          ; load 16 bytes from params+0
     0x284c28: add x9, x0, #0x4f8    ; dest = this + 0x4f8
     0x284c2c: str w8, [x0, #0x508]  ; store 4 bytes at this+0x508
     0x284c30: str q0, [x9]          ; store 16 bytes at this+0x4f8
     0x284c34: ret
     ```
   - Stores a 20-byte struct: 16 bytes (likely 4 floats) at offset `0x4f8`, plus 4 bytes (likely 1 float/int) at offset `0x508`.

2. **Caller:** single call site at `0x25ba3c`.
   - The parameters are built on the stack from a `folly::dynamic` config value (type-tag dispatch on `int`/`float`/`double`, `fcvt` to float). Values come from runtime configuration, not from hardcoded constants in the binary.

3. **Constructor** `UnifiedGaussianSplatter::UnifiedGaussianSplatter` (`0x27be4c`, 6064 bytes) only zero-initializes offset `0x4f8` (`strb wzr, [x19, #0x4f8]` at `0x27c304`). No default correction values are baked in; the struct is populated solely via `setAlphaCorrectionParams`.

4. **GPU uniform:** the literal string `alphaCorrectionParams` appears at file offset `0x14ab30`, alongside other shader parameter block names (`cullingCropParams`, `transformParams`, `trimmingParams`, `fragmentShadingRateParams`, …). This confirms the 20-byte struct is uploaded as a shader uniform block and the correction math executes GPU-side.

## What was checked and ruled out

- **`getUnifiedGaussianSplatterParams` (`0x2112f0`, 264 bytes):** fully disassembled. It only zero-fills a ~160-byte output struct, calls an initializer (`0x28ef1c`), and copies/conditionally maps fields from `AppParams`. No floating-point opacity math. **Ruled out** as the opacity transform site.
- **Unity asset shaders:** all 25 SPIR-V modules in `globalgamemanagers.assets` are Compute shaders for post-processing (Blit, Copy Depth, Copy Inverse Depth, VRS foveation). Module 24 (29 KB) is the VRS shader (`_VrsMainTex`, `_ShadingRateNativeValues`). **None perform splat rasterization.** Ruled out.
- **`sharedassets0_1_8bi2.resource` (20.9 MB):** contains zero SPIR-V modules and no `alphaCorrectionParams` string. Ruled out as the shader source.
- **The `.so` itself:** contains no SPIR-V magic (`0x07230203`). The Constellus splat shaders are not embedded in the native library in standard form (likely generated at runtime, packed in a custom format, or shipped separately).

## Candidate formula (low confidence)

The 20-byte layout (4 floats + 1 float/int) is consistent with a parameterized remap such as:

```
alpha_out = clamp(alpha_in * scale + bias, 0, 1)
```

possibly followed by a power or smoothstep using the remaining floats. **This is an inference from the struct size and the observed behavior, not a traced formula.** It is implemented behind the `--alpha-scale` / `--alpha-bias` test flags as an experimental tool, explicitly *not* as the reverse-engineered Hyperscape formula.

## Confidence

- **High:** an alpha-correction stage exists, is GPU-side, takes ~20 bytes of runtime-configured parameters.
- **Low:** the exact formula. It lives in a shader we do not have.

## What would resolve it

The Constellus splat shaders (SPIR-V) from a full APK install, or a GPU capture (RenderDoc) of the Hyperscape viewer showing the uniform values and the resulting pixels. Either would turn the candidate into a verified formula.
