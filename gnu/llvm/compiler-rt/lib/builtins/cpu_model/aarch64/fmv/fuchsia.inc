#include <zircon/features.h>
#include <zircon/syscalls.h>

void __init_cpu_features_resolver() {
  if (__atomic_load_n(&__aarch64_cpu_features.features, __ATOMIC_RELAXED))
    return;

    // This ensures the vDSO is a direct link-time dependency of anything that
    // needs this initializer code.
#pragma comment(lib, "zircon")
  uint32_t features;
  zx_status_t status = _zx_system_get_features(ZX_FEATURE_KIND_CPU, &features);
  if (status != ZX_OK)
    return;

  unsigned long long feat = 0;
#define setCPUFeature(cpu_feature) feat |= 1ULL << cpu_feature

  if (features & ZX_ARM64_FEATURE_ISA_FP)
    setCPUFeature(FEAT_FP);
  if (features & ZX_ARM64_FEATURE_ISA_ASIMD)
    setCPUFeature(FEAT_SIMD);
  if (features & ZX_ARM64_FEATURE_ISA_AES)
    setCPUFeature(FEAT_AES);
  if (features & ZX_ARM64_FEATURE_ISA_PMULL)
    setCPUFeature(FEAT_PMULL);
  if (features & ZX_ARM64_FEATURE_ISA_SHA1)
    setCPUFeature(FEAT_SHA1);
  if (features & ZX_ARM64_FEATURE_ISA_SHA256)
    setCPUFeature(FEAT_SHA2);
  if (features & ZX_ARM64_FEATURE_ISA_CRC32)
    setCPUFeature(FEAT_CRC);
  if (features & ZX_ARM64_FEATURE_ISA_RDM)
    setCPUFeature(FEAT_RDM);
  if (features & ZX_ARM64_FEATURE_ISA_SHA3)
    setCPUFeature(FEAT_SHA3);
  if (features & ZX_ARM64_FEATURE_ISA_SM4)
    setCPUFeature(FEAT_SM4);
  if (features & ZX_ARM64_FEATURE_ISA_DP)
    setCPUFeature(FEAT_DOTPROD);
  if (features & ZX_ARM64_FEATURE_ISA_FHM)
    setCPUFeature(FEAT_FP16FML);
  if (features & ZX_ARM64_FEATURE_ISA_SHA512)
    setCPUFeature(FEAT_SHA3);
  if (features & ZX_ARM64_FEATURE_ISA_I8MM)
    setCPUFeature(FEAT_I8MM);
  if (features & ZX_ARM64_FEATURE_ISA_SVE)
    setCPUFeature(FEAT_SVE);

  setCPUFeature(FEAT_INIT);

  __atomic_store_n(&__aarch64_cpu_features.features, feat, __ATOMIC_RELAXED);
}
