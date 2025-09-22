/* Copyright (C) 2004 Free Software Foundation, Inc.

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

/* Implemented from the mm3dnow.h (of supposedly AMD origin) included with
   MSVC 7.1.  */

#ifndef _MM3DNOW_H_INCLUDED
#define _MM3DNOW_H_INCLUDED

#ifdef __3dNOW__

#include <mmintrin.h>

/* Internal data types for implementing the intrinsics.  */
typedef float __v2sf __attribute__ ((__vector_size__ (8)));

static __inline void
_m_femms (void)
{
  __builtin_ia32_femms();
}

static __inline __m64
_m_pavgusb (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pavgusb ((__v8qi)__A, (__v8qi)__B);
}

static __inline __m64
_m_pf2id (__m64 __A)
{
  return (__m64)__builtin_ia32_pf2id ((__v2sf)__A);
}

static __inline __m64
_m_pfacc (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfacc ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfadd (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfadd ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfcmpeq (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfcmpeq ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfcmpge (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfcmpge ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfcmpgt (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfcmpgt ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfmax (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfmax ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfmin (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfmin ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfmul (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfmul ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfrcp (__m64 __A)
{
  return (__m64)__builtin_ia32_pfrcp ((__v2sf)__A);
}

static __inline __m64
_m_pfrcpit1 (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfrcpit1 ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfrcpit2 (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfrcpit2 ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfrsqrt (__m64 __A)
{
  return (__m64)__builtin_ia32_pfrsqrt ((__v2sf)__A);
}

static __inline __m64
_m_pfrsqit1 (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfrsqit1 ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfsub (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfsub ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfsubr (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfsubr ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pi2fd (__m64 __A)
{
  return (__m64)__builtin_ia32_pi2fd ((__v2si)__A);
}

static __inline __m64
_m_pmulhrw (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pmulhrw ((__v4hi)__A, (__v4hi)__B);
}

static __inline void
_m_prefetch (void *__P)
{
  __builtin_prefetch (__P, 0, 3 /* _MM_HINT_T0 */);
}

static __inline void
_m_prefetchw (void *__P)
{
  __builtin_prefetch (__P, 1, 3 /* _MM_HINT_T0 */);
}

static __inline __m64
_m_from_float (float __A)
{
  return (__m64)(__v2sf){ __A, 0 };
}

static __inline float
_m_to_float (__m64 __A)
{
  union { __v2sf v; float a[2]; } __tmp = { (__v2sf)__A };
  return __tmp.a[0];
}

#ifdef __3dNOW_A__

static __inline __m64
_m_pf2iw (__m64 __A)
{
  return (__m64)__builtin_ia32_pf2iw ((__v2sf)__A);
}

static __inline __m64
_m_pfnacc (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfnacc ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pfpnacc (__m64 __A, __m64 __B)
{
  return (__m64)__builtin_ia32_pfpnacc ((__v2sf)__A, (__v2sf)__B);
}

static __inline __m64
_m_pi2fw (__m64 __A)
{
  return (__m64)__builtin_ia32_pi2fw ((__v2si)__A);
}

static __inline __m64
_m_pswapd (__m64 __A)
{
  return (__m64)__builtin_ia32_pswapdsf ((__v2sf)__A);
}

#endif /* __3dNOW_A__ */
#endif /* __3dNOW__ */

#endif /* _MM3DNOW_H_INCLUDED */
