#include <zircon/features.h>
#include <zircon/syscalls.h>

static void CONSTRUCTOR_ATTRIBUTE init_have_lse_atomics(void) {
  // This ensures the vDSO is a direct link-time dependency of anything that
  // needs this initializer code.
#pragma comment(lib, "zircon")
  uint32_t features;
  zx_status_t status = _zx_system_get_features(ZX_FEATURE_KIND_CPU, &features);
  __aarch64_have_lse_atomics =
      status == ZX_OK && (features & ZX_ARM64_FEATURE_ISA_ATOMICS) != 0;
}
