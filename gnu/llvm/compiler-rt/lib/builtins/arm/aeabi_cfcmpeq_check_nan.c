//===-- lib/arm/aeabi_cfcmpeq_helper.c - Helper for cdcmpeq ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../int_lib.h"
#include <stdint.h>

AEABI_RTABI __attribute__((visibility("hidden"))) int
__aeabi_cfcmpeq_check_nan(float a, float b) {
  return __builtin_isnan(a) || __builtin_isnan(b);
}
