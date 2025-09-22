/* This file contains 16-bit versions of some of the functions found in
   libgcc2.c.  Really libgcc ought to be moved out of the gcc directory
   and into its own top level directory, and then split up into multiple
   files.  On this glorious day maybe this code can be integrated into
   it too.  */

/* Copyright (C) 2005  Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2, or (at your option) any later
   version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combine
   executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "tconfig.h"
#include "tsystem.h"
#include "coretypes.h"
#include "tm.h"

#ifdef HAVE_GAS_HIDDEN
#define ATTRIBUTE_HIDDEN  __attribute__ ((__visibility__ ("hidden")))
#else
#define ATTRIBUTE_HIDDEN
#endif

#include "libgcc2.h"
#undef int

/* These prototypes would normally live in libgcc2.h, but this can
   only happen once the code below is integrated into libgcc2.c.  */

extern USItype udivmodsi4 (USItype, USItype, word_type);
extern SItype __divsi3 (SItype, SItype);
extern SItype __modsi3 (SItype, SItype);
extern SItype __udivsi3 (SItype, SItype);
extern SItype __umodsi3 (SItype, SItype);
extern SItype __ashlsi3 (SItype, SItype);
extern SItype __ashrsi3 (SItype, SItype);
extern USItype __lshrsi3 (USItype, USItype);
extern int __popcounthi2 (UHWtype);
extern int __parityhi2 (UHWtype);
extern int __clzhi2 (UHWtype);
extern int __ctzhi2 (UHWtype);



USItype
udivmodsi4 (USItype num, USItype den, word_type modwanted)
{
  USItype bit = 1;
  USItype res = 0;

  while (den < num && bit && !(den & (1L << 31)))
    {
      den <<= 1;
      bit <<= 1;
    }
  while (bit)
    {
      if (num >= den)
	{
	  num -= den;
	  res |= bit;
	}
      bit >>= 1;
      den >>= 1;
    }

  if (modwanted)
    return num;
  return res;
}

SItype
__divsi3 (SItype a, SItype b)
{
  word_type neg = 0;
  SItype res;

  if (a < 0)
    {
      a = -a;
      neg = !neg;
    }

  if (b < 0)
    {
      b = -b;
      neg = !neg;
    }

  res = udivmodsi4 (a, b, 0);

  if (neg)
    res = -res;

  return res;
}

SItype
__modsi3 (SItype a, SItype b)
{
  word_type neg = 0;
  SItype res;

  if (a < 0)
    {
      a = -a;
      neg = 1;
    }

  if (b < 0)
    b = -b;

  res = udivmodsi4 (a, b, 1);

  if (neg)
    res = -res;

  return res;
}

SItype
__udivsi3 (SItype a, SItype b)
{
  return udivmodsi4 (a, b, 0);
}

SItype
__umodsi3 (SItype a, SItype b)
{
  return udivmodsi4 (a, b, 1);
}

SItype
__ashlsi3 (SItype a, SItype b)
{
  word_type i;
  
  if (b & 16)
    a <<= 16;
  if (b & 8)
    a <<= 8;
  for (i = (b & 0x7); i > 0; --i)
    a <<= 1;
  return a;
}

SItype
__ashrsi3 (SItype a, SItype b)
{
  word_type i;
  
  if (b & 16)
    a >>= 16;
  if (b & 8)
    a >>= 8;
  for (i = (b & 0x7); i > 0; --i)
    a >>= 1;
  return a;
}

USItype
__lshrsi3 (USItype a, USItype b)
{
  word_type i;
  
  if (b & 16)
    a >>= 16;
  if (b & 8)
    a >>= 8;
  for (i = (b & 0x7); i > 0; --i)
    a >>= 1;
  return a;
}

/* Returns the number of set bits in X.
   FIXME:  The return type really should be unsigned,
   but this is not how the builtin is prototyped.  */
int
__popcounthi2 (UHWtype x)
{
  int ret;

  ret = __popcount_tab [x & 0xff];
  ret += __popcount_tab [(x >> 8) & 0xff];

  return ret;
}

/* Returns the number of set bits in X, modulo 2.
   FIXME:  The return type really should be unsigned,
   but this is not how the builtin is prototyped.  */

int
__parityhi2 (UHWtype x)
{
  x ^= x >> 8;
  x ^= x >> 4;
  x &= 0xf;
  return (0x6996 >> x) & 1;
}

/* Returns the number of leading zero bits in X.
   FIXME:  The return type really should be unsigned,
   but this is not how the builtin is prototyped.  */

int
__clzhi2 (UHWtype x)
{
  if (x > 0xff)
    return 8 - __clz_tab[x >> 8];
  return 16 - __clz_tab[x];
}

/* Returns the number of trailing zero bits in X.
   FIXME:  The return type really should be unsigned,
   but this is not how the builtin is prototyped.  */

int
__ctzhi2 (UHWtype x)
{
  /* This is cunning.  It converts X into a number with only the one bit
     set, the bit was the least significant bit in X.  From this we can
     use the __clz_tab[] array to compute the number of trailing bits.  */
  x &= - x;

  if (x > 0xff)
    return __clz_tab[x >> 8] + 7;
  return __clz_tab[x] - 1;
}
