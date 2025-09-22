/*===---- bmiintrin.h - Implementation of BMI2 intrinsics on PowerPC -------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#if !defined X86GPRINTRIN_H_
#error "Never use <bmi2intrin.h> directly; include <x86gprintrin.h> instead."
#endif

#ifndef BMI2INTRIN_H_
#define BMI2INTRIN_H_

extern __inline unsigned int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _bzhi_u32(unsigned int __X, unsigned int __Y) {
  return ((__X << (32 - __Y)) >> (32 - __Y));
}

extern __inline unsigned int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mulx_u32(unsigned int __X, unsigned int __Y, unsigned int *__P) {
  unsigned long long __res = (unsigned long long)__X * __Y;
  *__P = (unsigned int)(__res >> 32);
  return (unsigned int)__res;
}

#ifdef __PPC64__
extern __inline unsigned long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _bzhi_u64(unsigned long long __X, unsigned long long __Y) {
  return ((__X << (64 - __Y)) >> (64 - __Y));
}

/* __int128 requires base 64-bit.  */
extern __inline unsigned long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _mulx_u64(unsigned long long __X, unsigned long long __Y,
              unsigned long long *__P) {
  unsigned __int128 __res = (unsigned __int128)__X * __Y;
  *__P = (unsigned long long)(__res >> 64);
  return (unsigned long long)__res;
}

#ifdef _ARCH_PWR7
/* popcount and bpermd require power7 minimum.  */
extern __inline unsigned long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _pdep_u64(unsigned long long __X, unsigned long long __M) {
  unsigned long __result = 0x0UL;
  const unsigned long __mask = 0x8000000000000000UL;
  unsigned long __m = __M;
  unsigned long __c, __t;
  unsigned long __p;

  /* The pop-count of the mask gives the number of the bits from
   source to process.  This is also needed to shift bits from the
   source into the correct position for the result.  */
  __p = 64 - __builtin_popcountl(__M);

  /* The loop is for the number of '1' bits in the mask and clearing
   each mask bit as it is processed.  */
  while (__m != 0) {
    __c = __builtin_clzl(__m);
    __t = __X << (__p - __c);
    __m ^= (__mask >> __c);
    __result |= (__t & (__mask >> __c));
    __p++;
  }
  return __result;
}

extern __inline unsigned long long
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _pext_u64(unsigned long long __X, unsigned long long __M) {
  unsigned long __p = 0x4040404040404040UL; // initial bit permute control
  const unsigned long __mask = 0x8000000000000000UL;
  unsigned long __m = __M;
  unsigned long __c;
  unsigned long __result;

  /* if the mask is constant and selects 8 bits or less we can use
   the Power8 Bit permute instruction.  */
  if (__builtin_constant_p(__M) && (__builtin_popcountl(__M) <= 8)) {
    /* Also if the pext mask is constant, then the popcount is
     constant, we can evaluate the following loop at compile
     time and use a constant bit permute vector.  */
    long __i;
    for (__i = 0; __i < __builtin_popcountl(__M); __i++) {
      __c = __builtin_clzl(__m);
      __p = (__p << 8) | __c;
      __m ^= (__mask >> __c);
    }
    __result = __builtin_bpermd(__p, __X);
  } else {
    __p = 64 - __builtin_popcountl(__M);
    __result = 0;
    /* We could a use a for loop here, but that combined with
     -funroll-loops can expand to a lot of code.  The while
     loop avoids unrolling and the compiler commons the xor
     from clearing the mask bit with the (m != 0) test.  The
     result is a more compact loop setup and body.  */
    while (__m != 0) {
      unsigned long __t;
      __c = __builtin_clzl(__m);
      __t = (__X & (__mask >> __c)) >> (__p - __c);
      __m ^= (__mask >> __c);
      __result |= (__t);
      __p++;
    }
  }
  return __result;
}

/* these 32-bit implementations depend on 64-bit pdep/pext
   which depend on _ARCH_PWR7.  */
extern __inline unsigned int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _pdep_u32(unsigned int __X, unsigned int __Y) {
  return _pdep_u64(__X, __Y);
}

extern __inline unsigned int
    __attribute__((__gnu_inline__, __always_inline__, __artificial__))
    _pext_u32(unsigned int __X, unsigned int __Y) {
  return _pext_u64(__X, __Y);
}
#endif /* _ARCH_PWR7  */
#endif /* __PPC64__  */

#endif /* BMI2INTRIN_H_ */
