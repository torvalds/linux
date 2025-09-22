//===-- sanitizer/ubsan_interface.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of UBSanitizer (UBSan).
//
// Public interface header.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_UBSAN_INTERFACE_H
#define SANITIZER_UBSAN_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif
/// User-provided default option settings.
///
/// You can provide your own implementation of this function to return a string
/// containing UBSan runtime options (for example,
/// <c>verbosity=1:halt_on_error=0</c>).
///
/// \returns Default options string.
const char *SANITIZER_CDECL __ubsan_default_options(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SANITIZER_UBSAN_INTERFACE_H
