/*===-- int128_builtins.cpp - Implement __muloti4 --------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __muloti4, and is stolen from the compiler_rt library.
 *
 * FIXME: we steal and re-compile it into filesystem, which uses __int128_t,
 * and requires this builtin when sanitized. See llvm.org/PR30643
 *
 * ===----------------------------------------------------------------------===
 */
#include <__config>
#include <climits>

#if !defined(_LIBCPP_HAS_NO_INT128)

extern "C" __attribute__((no_sanitize("undefined"))) _LIBCPP_EXPORTED_FROM_ABI __int128_t
__muloti4(__int128_t a, __int128_t b, int* overflow) {
  const int N          = (int)(sizeof(__int128_t) * CHAR_BIT);
  const __int128_t MIN = (__int128_t)1 << (N - 1);
  const __int128_t MAX = ~MIN;
  *overflow            = 0;
  __int128_t result    = a * b;
  if (a == MIN) {
    if (b != 0 && b != 1)
      *overflow = 1;
    return result;
  }
  if (b == MIN) {
    if (a != 0 && a != 1)
      *overflow = 1;
    return result;
  }
  __int128_t sa    = a >> (N - 1);
  __int128_t abs_a = (a ^ sa) - sa;
  __int128_t sb    = b >> (N - 1);
  __int128_t abs_b = (b ^ sb) - sb;
  if (abs_a < 2 || abs_b < 2)
    return result;
  if (sa == sb) {
    if (abs_a > MAX / abs_b)
      *overflow = 1;
  } else {
    if (abs_a > MIN / -abs_b)
      *overflow = 1;
  }
  return result;
}

#endif
