//===-- msan_dl.h ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// Helper functions for unpoisoning results of dladdr and dladdr1.
//===----------------------------------------------------------------------===//

#ifndef MSAN_DL_H
#define MSAN_DL_H

#include "msan.h"
#include "sanitizer_common/sanitizer_common.h"

namespace __msan {

void UnpoisonDllAddrInfo(void *info);

#if SANITIZER_GLIBC
void UnpoisonDllAddr1ExtraInfo(void **extra_info, int flags);
#endif

}  // namespace __msan

#endif  // MSAN_DL_H
