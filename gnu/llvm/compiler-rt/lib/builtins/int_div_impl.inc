//===-- int_div_impl.inc - Integer division ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helpers used by __udivsi3, __umodsi3, __udivdi3, and __umodsi3.
//
//===----------------------------------------------------------------------===//

#define clz(a) (sizeof(a) == sizeof(unsigned long long) ? __builtin_clzll(a) : clzsi(a))

// Adapted from Figure 3-40 of The PowerPC Compiler Writer's Guide
static __inline fixuint_t __udivXi3(fixuint_t n, fixuint_t d) {
  const unsigned N = sizeof(fixuint_t) * CHAR_BIT;
  // d == 0 cases are unspecified.
  unsigned sr = (d ? clz(d) : N) - (n ? clz(n) : N);
  // 0 <= sr <= N - 1 or sr is very large.
  if (sr > N - 1) // n < d
    return 0;
  if (sr == N - 1) // d == 1
    return n;
  ++sr;
  // 1 <= sr <= N - 1. Shifts do not trigger UB.
  fixuint_t r = n >> sr;
  n <<= N - sr;
  fixuint_t carry = 0;
  for (; sr > 0; --sr) {
    r = (r << 1) | (n >> (N - 1));
    n = (n << 1) | carry;
    // Branch-less version of:
    // carry = 0;
    // if (r >= d) r -= d, carry = 1;
    const fixint_t s = (fixint_t)(d - r - 1) >> (N - 1);
    carry = s & 1;
    r -= d & s;
  }
  n = (n << 1) | carry;
  return n;
}

// Mostly identical to __udivXi3 but the return values are different.
static __inline fixuint_t __umodXi3(fixuint_t n, fixuint_t d) {
  const unsigned N = sizeof(fixuint_t) * CHAR_BIT;
  // d == 0 cases are unspecified.
  unsigned sr = (d ? clz(d) : N) - (n ? clz(n) : N);
  // 0 <= sr <= N - 1 or sr is very large.
  if (sr > N - 1) // n < d
    return n;
  if (sr == N - 1) // d == 1
    return 0;
  ++sr;
  // 1 <= sr <= N - 1. Shifts do not trigger UB.
  fixuint_t r = n >> sr;
  n <<= N - sr;
  fixuint_t carry = 0;
  for (; sr > 0; --sr) {
    r = (r << 1) | (n >> (N - 1));
    n = (n << 1) | carry;
    // Branch-less version of:
    // carry = 0;
    // if (r >= d) r -= d, carry = 1;
    const fixint_t s = (fixint_t)(d - r - 1) >> (N - 1);
    carry = s & 1;
    r -= d & s;
  }
  return r;
}

#ifdef COMPUTE_UDIV
static __inline fixint_t __divXi3(fixint_t a, fixint_t b) {
  const int N = (int)(sizeof(fixint_t) * CHAR_BIT) - 1;
  fixint_t s_a = a >> N;                            // s_a = a < 0 ? -1 : 0
  fixint_t s_b = b >> N;                            // s_b = b < 0 ? -1 : 0
  fixuint_t a_u = (fixuint_t)(a ^ s_a) + (-s_a);    // negate if s_a == -1
  fixuint_t b_u = (fixuint_t)(b ^ s_b) + (-s_b);    // negate if s_b == -1
  s_a ^= s_b;                                       // sign of quotient
  return (COMPUTE_UDIV(a_u, b_u) ^ s_a) + (-s_a);   // negate if s_a == -1
}
#endif // COMPUTE_UDIV

#ifdef ASSIGN_UMOD
static __inline fixint_t __modXi3(fixint_t a, fixint_t b) {
  const int N = (int)(sizeof(fixint_t) * CHAR_BIT) - 1;
  fixint_t s = b >> N;                              // s = b < 0 ? -1 : 0
  fixuint_t b_u = (fixuint_t)(b ^ s) + (-s);        // negate if s == -1
  s = a >> N;                                       // s = a < 0 ? -1 : 0
  fixuint_t a_u = (fixuint_t)(a ^ s) + (-s);        // negate if s == -1
  fixuint_t res;
  ASSIGN_UMOD(res, a_u, b_u);
  return (res ^ s) + (-s);                          // negate if s == -1
}
#endif // ASSIGN_UMOD
