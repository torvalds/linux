//===-- sanitizer_mallinfo.h ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer common code.
//
// Definition for mallinfo on different platforms.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_MALLINFO_H
#define SANITIZER_MALLINFO_H

#include "sanitizer_internal_defs.h"
#include "sanitizer_platform.h"

namespace __sanitizer {

#if SANITIZER_ANDROID

struct __sanitizer_struct_mallinfo {
  uptr v[10];
};

#elif SANITIZER_LINUX || SANITIZER_APPLE || SANITIZER_FUCHSIA

struct __sanitizer_struct_mallinfo {
  int v[10];
};

struct __sanitizer_struct_mallinfo2 {
  uptr v[10];
};

#endif

}  // namespace __sanitizer

#endif  // SANITIZER_MALLINFO_H
