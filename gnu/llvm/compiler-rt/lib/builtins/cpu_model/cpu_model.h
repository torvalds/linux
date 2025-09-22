//===-- cpu_model_common.c - Utilities for cpu model detection ----*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements common utilities for runtime cpu model detection.
//
//===----------------------------------------------------------------------===//

#ifndef COMPILER_RT_LIB_BUILTINS_CPU_MODEL_COMMON_H
#define COMPILER_RT_LIB_BUILTINS_CPU_MODEL_COMMON_H

#define bool int
#define true 1
#define false 0

#ifndef __has_attribute
#define __has_attribute(attr) 0
#endif

#if __has_attribute(constructor)
#if __GNUC__ >= 9
// Ordinarily init priorities below 101 are disallowed as they are reserved for
// the implementation. However, we are the implementation, so silence the
// diagnostic, since it doesn't apply to us.
#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"
#endif
// We're choosing init priority 90 to force our constructors to run before any
// constructors in the end user application (starting at priority 101). This
// value matches the libgcc choice for the same functions.
#define CONSTRUCTOR_ATTRIBUTE __attribute__((constructor(90)))
#else
// FIXME: For MSVC, we should make a function pointer global in .CRT$X?? so that
// this runs during initialization.
#define CONSTRUCTOR_ATTRIBUTE
#endif

#endif
