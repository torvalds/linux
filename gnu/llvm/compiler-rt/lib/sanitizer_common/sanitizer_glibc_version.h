//===-- sanitizer_glibc_version.h -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of Sanitizer common code.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_GLIBC_VERSION_H
#define SANITIZER_GLIBC_VERSION_H

#include "sanitizer_platform.h"

#if SANITIZER_LINUX || SANITIZER_FUCHSIA
#include <features.h>
#endif

#ifndef __GLIBC_PREREQ
#define __GLIBC_PREREQ(x, y) 0
#endif

#endif
