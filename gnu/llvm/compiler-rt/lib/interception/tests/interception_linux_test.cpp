//===-- interception_linux_test.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
// Tests for interception_linux.h.
//
//===----------------------------------------------------------------------===//

// Do not declare functions in ctype.h.
#define __NO_CTYPE

#include "interception/interception.h"

#include <stdlib.h>

#include "gtest/gtest.h"

#if SANITIZER_LINUX

static int isdigit_called;
namespace __interception {
int isalpha_called;
int isalnum_called;
int islower_called;
}  // namespace __interception
using namespace __interception;

DECLARE_REAL(int, isdigit, int);
DECLARE_REAL(int, isalpha, int);
DECLARE_REAL(int, isalnum, int);
DECLARE_REAL(int, islower, int);

INTERCEPTOR(void *, malloc, SIZE_T s) { return calloc(1, s); }
INTERCEPTOR(void, dummy_doesnt_exist__, ) { __builtin_trap(); }

INTERCEPTOR(int, isdigit, int d) {
  ++isdigit_called;
  return d >= '0' && d <= '9';
}

INTERCEPTOR(int, isalpha, int d) {
  // Use non-commutative arithmetic to verify order of calls.
  isalpha_called = isalpha_called * 10 + 3;
  return (d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z');
}

INTERCEPTOR(int, isalnum, int d) {
  isalnum_called = isalnum_called * 10 + 3;
  return __interceptor_isalpha(d) || __interceptor_isdigit(d);
}

INTERCEPTOR(int, islower, int d) {
  islower_called = islower_called * 10 + 3;
  return d >= 'a' && d <= 'z';
}

namespace __interception {

TEST(Interception, InterceptFunction) {
  uptr malloc_address = 0;
  EXPECT_TRUE(InterceptFunction("malloc", &malloc_address, (uptr)&malloc,
                                (uptr)&TRAMPOLINE(malloc)));
  EXPECT_NE(0U, malloc_address);
  EXPECT_FALSE(InterceptFunction("malloc", &malloc_address, (uptr)&calloc,
                                 (uptr)&TRAMPOLINE(malloc)));

  uptr dummy_address = 0;
  EXPECT_FALSE(InterceptFunction("dummy_doesnt_exist__", &dummy_address,
                                 (uptr)&dummy_doesnt_exist__,
                                 (uptr)&TRAMPOLINE(dummy_doesnt_exist__)));
  EXPECT_EQ(0U, dummy_address);
}

TEST(Interception, Basic) {
  EXPECT_TRUE(INTERCEPT_FUNCTION(isdigit));

  // After interception, the counter should be incremented.
  isdigit_called = 0;
  EXPECT_NE(0, isdigit('1'));
  EXPECT_EQ(1, isdigit_called);
  EXPECT_EQ(0, isdigit('a'));
  EXPECT_EQ(2, isdigit_called);

  // Calling the REAL function should not affect the counter.
  isdigit_called = 0;
  EXPECT_NE(0, REAL(isdigit)('1'));
  EXPECT_EQ(0, REAL(isdigit)('a'));
  EXPECT_EQ(0, isdigit_called);
}

TEST(Interception, ForeignOverrideDirect) {
  // Actual interceptor is overridden.
  EXPECT_FALSE(INTERCEPT_FUNCTION(isalpha));

  isalpha_called = 0;
  EXPECT_NE(0, isalpha('a'));
  EXPECT_EQ(13, isalpha_called);
  isalpha_called = 0;
  EXPECT_EQ(0, isalpha('_'));
  EXPECT_EQ(13, isalpha_called);

  isalpha_called = 0;
  EXPECT_NE(0, REAL(isalpha)('a'));
  EXPECT_EQ(0, REAL(isalpha)('_'));
  EXPECT_EQ(0, isalpha_called);
}

#if ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT
TEST(Interception, ForeignOverrideIndirect) {
  // Actual interceptor is _not_ overridden.
  EXPECT_TRUE(INTERCEPT_FUNCTION(isalnum));

  isalnum_called = 0;
  EXPECT_NE(0, isalnum('a'));
  EXPECT_EQ(13, isalnum_called);
  isalnum_called = 0;
  EXPECT_EQ(0, isalnum('_'));
  EXPECT_EQ(13, isalnum_called);

  isalnum_called = 0;
  EXPECT_NE(0, REAL(isalnum)('a'));
  EXPECT_EQ(0, REAL(isalnum)('_'));
  EXPECT_EQ(0, isalnum_called);
}

TEST(Interception, ForeignOverrideThree) {
  // Actual interceptor is overridden.
  EXPECT_FALSE(INTERCEPT_FUNCTION(islower));

  islower_called = 0;
  EXPECT_NE(0, islower('a'));
  EXPECT_EQ(123, islower_called);
  islower_called = 0;
  EXPECT_EQ(0, islower('A'));
  EXPECT_EQ(123, islower_called);

  islower_called = 0;
  EXPECT_NE(0, REAL(islower)('a'));
  EXPECT_EQ(0, REAL(islower)('A'));
  EXPECT_EQ(0, islower_called);
}
#endif  // ASM_INTERCEPTOR_TRAMPOLINE_SUPPORT

}  // namespace __interception

#endif  // SANITIZER_LINUX
