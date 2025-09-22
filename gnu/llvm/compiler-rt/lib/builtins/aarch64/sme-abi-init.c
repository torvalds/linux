// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

__attribute__((visibility("hidden"), nocommon))
_Bool __aarch64_has_sme_and_tpidr2_el0;

// We have multiple ways to check that the function has SME, depending on our
// target.
// * For Linux we can use __getauxval().
// * For newlib we can use __aarch64_sme_accessible().

#if defined(__linux__)

#ifndef AT_HWCAP2
#define AT_HWCAP2 26
#endif

#ifndef HWCAP2_SME
#define HWCAP2_SME (1 << 23)
#endif

extern unsigned long int __getauxval (unsigned long int);

static _Bool has_sme(void) {
  return __getauxval(AT_HWCAP2) & HWCAP2_SME;
}

#else  // defined(__linux__)

#if defined(COMPILER_RT_SHARED_LIB)
__attribute__((weak))
#endif
extern _Bool __aarch64_sme_accessible(void);

static _Bool has_sme(void)  {
#if defined(COMPILER_RT_SHARED_LIB)
  if (!__aarch64_sme_accessible)
    return 0;
#endif
  return __aarch64_sme_accessible();
}

#endif // defined(__linux__)

#if __GNUC__ >= 9
#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"
#endif
__attribute__((constructor(90)))
static void init_aarch64_has_sme(void) {
  __aarch64_has_sme_and_tpidr2_el0 = has_sme();
}
