//===-- asan_suppressions.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan-private header for asan_suppressions.cpp.
//===----------------------------------------------------------------------===//
#ifndef ASAN_SUPPRESSIONS_H
#define ASAN_SUPPRESSIONS_H

#include "asan_internal.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

namespace __asan {

void InitializeSuppressions();
bool IsInterceptorSuppressed(const char *interceptor_name);
bool HaveStackTraceBasedSuppressions();
bool IsStackTraceSuppressed(const StackTrace *stack);
bool IsODRViolationSuppressed(const char *global_var_name);

} // namespace __asan

#endif // ASAN_SUPPRESSIONS_H
