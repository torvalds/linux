//===-- interception_mac.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Mac-specific interception methods.
//===----------------------------------------------------------------------===//

#if SANITIZER_APPLE

#if !defined(INCLUDED_FROM_INTERCEPTION_LIB)
# error "interception_mac.h should be included from interception.h only"
#endif

#ifndef INTERCEPTION_MAC_H
#define INTERCEPTION_MAC_H

#define INTERCEPT_FUNCTION_MAC(func)
#define INTERCEPT_FUNCTION_VER_MAC(func, symver)

#endif  // INTERCEPTION_MAC_H
#endif  // SANITIZER_APPLE
