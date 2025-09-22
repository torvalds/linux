//===-- ubsan_win_weak_interception.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This module should be included in Ubsan when it is implemented as a shared
// library on Windows (dll), in order to delegate the calls of weak functions to
// the implementation in the main executable when a strong definition is
// provided.
//===----------------------------------------------------------------------===//
#ifdef SANITIZER_DYNAMIC
#include "sanitizer_common/sanitizer_win_weak_interception.h"
#include "ubsan_flags.h"
#include "ubsan_monitor.h"
// Check if strong definitions for weak functions are present in the main
// executable. If that is the case, override dll functions to point to strong
// implementations.
#define INTERFACE_FUNCTION(Name)
#define INTERFACE_WEAK_FUNCTION(Name) INTERCEPT_SANITIZER_WEAK_FUNCTION(Name)
#include "ubsan_interface.inc"
#endif // SANITIZER_DYNAMIC
