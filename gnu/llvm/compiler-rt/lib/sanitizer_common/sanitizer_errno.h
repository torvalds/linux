//===-- sanitizer_errno.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizers run-time libraries.
//
// Defines errno to avoid including errno.h and its dependencies into sensitive
// files (e.g. interceptors are not supposed to include any system headers).
// It's ok to use errno.h directly when your file already depend on other system
// includes though.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ERRNO_H
#define SANITIZER_ERRNO_H

#include "sanitizer_errno_codes.h"
#include "sanitizer_platform.h"

#if SANITIZER_FREEBSD || SANITIZER_APPLE
#  define __errno_location __error
#elif SANITIZER_ANDROID || SANITIZER_NETBSD
#  define __errno_location __errno
#elif SANITIZER_SOLARIS
#  define __errno_location ___errno
#elif SANITIZER_WINDOWS
#  define __errno_location _errno
#endif

extern "C" int *__errno_location();

#define errno (*__errno_location())

#endif  // SANITIZER_ERRNO_H
