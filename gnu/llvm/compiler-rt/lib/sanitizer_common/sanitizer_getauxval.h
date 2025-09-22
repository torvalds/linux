//===-- sanitizer_getauxval.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Common getauxval() guards and definitions.
// getauxval() is not defined until glibc version 2.16, or until API level 21
// for Android.
// Implement the getauxval() compat function for NetBSD.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_GETAUXVAL_H
#define SANITIZER_GETAUXVAL_H

#include "sanitizer_platform.h"
#include "sanitizer_glibc_version.h"

#if SANITIZER_LINUX || SANITIZER_FUCHSIA

# if (__GLIBC_PREREQ(2, 16) || (SANITIZER_ANDROID && __ANDROID_API__ >= 21) || \
      SANITIZER_FUCHSIA) &&                                                    \
     !SANITIZER_GO
#  define SANITIZER_USE_GETAUXVAL 1
# else
#  define SANITIZER_USE_GETAUXVAL 0
# endif

# if SANITIZER_USE_GETAUXVAL
#  include <sys/auxv.h>
# else
// The weak getauxval definition allows to check for the function at runtime.
// This is useful for Android, when compiled at a lower API level yet running
// on a more recent platform that offers the function.
extern "C" SANITIZER_WEAK_ATTRIBUTE unsigned long getauxval(unsigned long type);
# endif

#elif SANITIZER_NETBSD

#define SANITIZER_USE_GETAUXVAL 1

#include <dlfcn.h>
#include <elf.h>

static inline decltype(AuxInfo::a_v) getauxval(decltype(AuxInfo::a_type) type) {
  for (const AuxInfo *aux = (const AuxInfo *)_dlauxinfo();
       aux->a_type != AT_NULL; ++aux) {
    if (type == aux->a_type)
      return aux->a_v;
  }

  return 0;
}

#endif

#endif // SANITIZER_GETAUXVAL_H
