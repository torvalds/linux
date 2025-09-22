#include <sys/auxv.h>

static void CONSTRUCTOR_ATTRIBUTE init_have_lse_atomics(void) {
  unsigned long hwcap = getauxval(AT_HWCAP);
  __aarch64_have_lse_atomics = (hwcap & HWCAP_ATOMICS) != 0;
}
