//===-- ubsan_platform.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the platforms which UBSan is supported at.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_PLATFORM_H
#define UBSAN_PLATFORM_H

// Other platforms should be easy to add, and probably work as-is.
#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__) ||        \
    defined(__NetBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)  ||  \
    (defined(__sun__) && defined(__svr4__)) || defined(_WIN32) ||              \
    defined(__Fuchsia__)
#define CAN_SANITIZE_UB 1
#else
# define CAN_SANITIZE_UB 0
#endif

#endif
