//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//===----------------------------------------------------------------------===//

#ifndef LIBUNWIND_CET_UNWIND_H
#define LIBUNWIND_CET_UNWIND_H

#include "libunwind.h"

// Currently, CET is implemented on Linux x86 platforms.
#if defined(_LIBUNWIND_TARGET_LINUX) && defined(__CET__) && defined(__SHSTK__)
#define _LIBUNWIND_USE_CET 1
#endif

#if defined(_LIBUNWIND_USE_CET)
#include <cet.h>
#include <immintrin.h>

#define _LIBUNWIND_POP_CET_SSP(x)                                              \
  do {                                                                         \
    unsigned long ssp = _get_ssp();                                            \
    if (ssp != 0) {                                                            \
      unsigned int tmp = (x);                                                  \
      while (tmp > 255) {                                                      \
        _inc_ssp(255);                                                         \
        tmp -= 255;                                                            \
      }                                                                        \
      _inc_ssp(tmp);                                                           \
    }                                                                          \
  } while (0)
#endif

// On AArch64 we use _LIBUNWIND_USE_GCS to indicate that GCS is supported. We
// need to guard any use of GCS instructions with __chkfeat though, as GCS may
// not be enabled.
#if defined(_LIBUNWIND_TARGET_AARCH64) && defined(__ARM_FEATURE_GCS_DEFAULT)
#include <arm_acle.h>

// We can only use GCS if arm_acle.h defines the GCS intrinsics.
#ifdef _CHKFEAT_GCS
#define _LIBUNWIND_USE_GCS 1
#endif

#define _LIBUNWIND_POP_CET_SSP(x)                                              \
  do {                                                                         \
    if (__chkfeat(_CHKFEAT_GCS)) {                                             \
      unsigned tmp = (x);                                                      \
      while (tmp--)                                                            \
        __gcspopm();                                                           \
    }                                                                          \
  } while (0)

#endif

extern void *__libunwind_cet_get_registers(unw_cursor_t *);
extern void *__libunwind_cet_get_jump_target(void);

#endif
