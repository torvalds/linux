#include <string.h>
#include <sys/auxv.h>
#include <sys/system_properties.h>

static bool __isExynos9810(void) {
  char arch[PROP_VALUE_MAX];
  return __system_property_get("ro.arch", arch) > 0 &&
    strncmp(arch, "exynos9810", sizeof("exynos9810") - 1) == 0;
}

static void CONSTRUCTOR_ATTRIBUTE init_have_lse_atomics(void) {
  unsigned long hwcap = getauxval(AT_HWCAP);
  _Bool result = (hwcap & HWCAP_ATOMICS) != 0;
  if (result) {
    // Some cores in the Exynos 9810 CPU are ARMv8.2 and others are ARMv8.0;
    // only the former support LSE atomics.  However, the kernel in the
    // initial Android 8.0 release of Galaxy S9/S9+ devices incorrectly
    // reported the feature as being supported.
    //
    // The kernel appears to have been corrected to mark it unsupported as of
    // the Android 9.0 release on those devices, and this issue has not been
    // observed anywhere else. Thus, this workaround may be removed if
    // compiler-rt ever drops support for Android 8.0.
    if (__isExynos9810())
      result = false;
  }
  __aarch64_have_lse_atomics = result;
}
