//===-- interception_linux_foreign_test.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
// Tests that foreign interceptors work.
//
//===----------------------------------------------------------------------===//

// Do not declare functions in ctype.h.
#define __NO_CTYPE

#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_asm.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

#if SANITIZER_LINUX

extern "C" int isalnum(int d);
extern "C" int __interceptor_isalpha(int d);
extern "C" int ___interceptor_isalnum(int d);  // the sanitizer interceptor
extern "C" int ___interceptor_islower(int d);  // the sanitizer interceptor

namespace __interception {
extern int isalpha_called;
extern int isalnum_called;
extern int islower_called;
}  // namespace __interception
using namespace __interception;

// Direct foreign interceptor. This is the "normal" protocol that other
// interceptors should follow.
extern "C" int isalpha(int d) {
  // Use non-commutative arithmetic to verify order of calls.
  isalpha_called = isalpha_called * 10 + 1;
  return __interceptor_isalpha(d);
}

#if ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT
// Indirect foreign interceptor. This pattern should only be used to co-exist
// with direct foreign interceptors and sanitizer interceptors.
extern "C" int __interceptor_isalnum(int d) {
  isalnum_called = isalnum_called * 10 + 1;
  return ___interceptor_isalnum(d);
}

extern "C" int __interceptor_islower(int d) {
  islower_called = islower_called * 10 + 2;
  return ___interceptor_islower(d);
}

extern "C" int islower(int d) {
  islower_called = islower_called * 10 + 1;
  return __interceptor_islower(d);
}
#endif  // ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT

namespace __interception {

TEST(ForeignInterception, ForeignOverrideDirect) {
  isalpha_called = 0;
  EXPECT_NE(0, isalpha('a'));
  EXPECT_EQ(13, isalpha_called);
  isalpha_called = 0;
  EXPECT_EQ(0, isalpha('_'));
  EXPECT_EQ(13, isalpha_called);
}

#if ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT
TEST(ForeignInterception, ForeignOverrideIndirect) {
  isalnum_called = 0;
  EXPECT_NE(0, isalnum('a'));
  EXPECT_EQ(13, isalnum_called);
  isalnum_called = 0;
  EXPECT_EQ(0, isalnum('_'));
  EXPECT_EQ(13, isalnum_called);
}

TEST(ForeignInterception, ForeignOverrideThree) {
  islower_called = 0;
  EXPECT_NE(0, islower('a'));
  EXPECT_EQ(123, islower_called);
  islower_called = 0;
  EXPECT_EQ(0, islower('_'));
  EXPECT_EQ(123, islower_called);
}
#endif  // ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT

}  // namespace __interception

#endif  // SANITIZER_LINUX
