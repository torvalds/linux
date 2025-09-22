//===-- tsan_interface_ann.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Interface for dynamic annotations.
//===----------------------------------------------------------------------===//
#ifndef TSAN_INTERFACE_ANN_H
#define TSAN_INTERFACE_ANN_H

#include <sanitizer_common/sanitizer_internal_defs.h>

// This header should NOT include any other headers.
// All functions in this header are extern "C" and start with __tsan_.

#ifdef __cplusplus
extern "C" {
#endif

SANITIZER_INTERFACE_ATTRIBUTE void __tsan_acquire(void *addr);
SANITIZER_INTERFACE_ATTRIBUTE void __tsan_release(void *addr);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // TSAN_INTERFACE_ANN_H
