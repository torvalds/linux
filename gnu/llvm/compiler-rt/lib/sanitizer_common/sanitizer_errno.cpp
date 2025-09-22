//===-- sanitizer_errno.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizers run-time libraries.
//
// Defines errno to avoid including errno.h and its dependencies into other
// files (e.g. interceptors are not supposed to include any system headers).
//
//===----------------------------------------------------------------------===//

#include "sanitizer_errno_codes.h"
#include "sanitizer_internal_defs.h"

#include <errno.h>

namespace __sanitizer {

COMPILER_CHECK(errno_ENOMEM == ENOMEM);
COMPILER_CHECK(errno_EBUSY == EBUSY);
COMPILER_CHECK(errno_EINVAL == EINVAL);

// EOWNERDEAD is not present in some older platforms.
#if defined(EOWNERDEAD)
extern const int errno_EOWNERDEAD = EOWNERDEAD;
#else
extern const int errno_EOWNERDEAD = -1;
#endif

}  // namespace __sanitizer
