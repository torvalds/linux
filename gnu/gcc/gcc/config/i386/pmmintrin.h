/* Copyright (C) 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, if you include this header file into source
   files compiled by GCC, this header file does not by itself cause
   the resulting executable to be covered by the GNU General Public
   License.  This exception does not however invalidate any other
   reasons why the executable file might be covered by the GNU General
   Public License.  */

/* Implemented from the specification included in the Intel C++ Compiler
   User Guide and Reference, version 9.0.  */

#ifndef _PMMINTRIN_H_INCLUDED
#define _PMMINTRIN_H_INCLUDED

#ifdef __SSE3__
#include <xmmintrin.h>
#include <emmintrin.h>

/* Additional bits in the MXCSR.  */
#define _MM_DENORMALS_ZERO_MASK		0x0040
#define _MM_DENORMALS_ZERO_ON		0x0040
#define _MM_DENORMALS_ZERO_OFF		0x0000

#define _MM_SET_DENORMALS_ZERO_MODE(mode) \
  _mm_setcsr ((_mm_getcsr () & ~_MM_DENORMALS_ZERO_MASK) | (mode))
#define _MM_GET_DENORMALS_ZERO_MODE() \
  (_mm_getcsr() & _MM_DENORMALS_ZERO_MASK)

static __inline __m128 __attribute__((__always_inline__))
_mm_addsub_ps (__m128 __X, __m128 __Y)
{
  return (__m128) __builtin_ia32_addsubps ((__v4sf)__X, (__v4sf)__Y);
}

static __inline __m128 __attribute__((__always_inline__))
_mm_hadd_ps (__m128 __X, __m128 __Y)
{
  return (__m128) __builtin_ia32_haddps ((__v4sf)__X, (__v4sf)__Y);
}

static __inline __m128 __attribute__((__always_inline__))
_mm_hsub_ps (__m128 __X, __m128 __Y)
{
  return (__m128) __builtin_ia32_hsubps ((__v4sf)__X, (__v4sf)__Y);
}

static __inline __m128 __attribute__((__always_inline__))
_mm_movehdup_ps (__m128 __X)
{
  return (__m128) __builtin_ia32_movshdup ((__v4sf)__X);
}

static __inline __m128 __attribute__((__always_inline__))
_mm_moveldup_ps (__m128 __X)
{
  return (__m128) __builtin_ia32_movsldup ((__v4sf)__X);
}

static __inline __m128d __attribute__((__always_inline__))
_mm_addsub_pd (__m128d __X, __m128d __Y)
{
  return (__m128d) __builtin_ia32_addsubpd ((__v2df)__X, (__v2df)__Y);
}

static __inline __m128d __attribute__((__always_inline__))
_mm_hadd_pd (__m128d __X, __m128d __Y)
{
  return (__m128d) __builtin_ia32_haddpd ((__v2df)__X, (__v2df)__Y);
}

static __inline __m128d __attribute__((__always_inline__))
_mm_hsub_pd (__m128d __X, __m128d __Y)
{
  return (__m128d) __builtin_ia32_hsubpd ((__v2df)__X, (__v2df)__Y);
}

static __inline __m128d __attribute__((__always_inline__))
_mm_loaddup_pd (double const *__P)
{
  return _mm_load1_pd (__P);
}

static __inline __m128d __attribute__((__always_inline__))
_mm_movedup_pd (__m128d __X)
{
  return _mm_shuffle_pd (__X, __X, _MM_SHUFFLE2 (0,0));
}

static __inline __m128i __attribute__((__always_inline__))
_mm_lddqu_si128 (__m128i const *__P)
{
  return (__m128i) __builtin_ia32_lddqu ((char const *)__P);
}

static __inline void __attribute__((__always_inline__))
_mm_monitor (void const * __P, unsigned int __E, unsigned int __H)
{
  __builtin_ia32_monitor (__P, __E, __H);
}

static __inline void __attribute__((__always_inline__))
_mm_mwait (unsigned int __E, unsigned int __H)
{
  __builtin_ia32_mwait (__E, __H);
}

#endif /* __SSE3__ */

#endif /* _PMMINTRIN_H_INCLUDED */
