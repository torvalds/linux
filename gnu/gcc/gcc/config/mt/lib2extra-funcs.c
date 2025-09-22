/* Copyright (C) 2005 Free Software Foundation,

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

#define BITS_PER_UNIT	8

typedef 	 int HItype		__attribute__ ((mode (HI)));
typedef unsigned int UHItype		__attribute__ ((mode (HI)));

typedef		 int SItype		__attribute__ ((mode (SI)));
typedef unsigned int USItype		__attribute__ ((mode (SI)));

typedef int word_type			__attribute__ ((mode (__word__)));

struct SIstruct {HItype low, high;};

typedef union
{
  struct SIstruct s;
  SItype ll;
} SIunion;

SItype
__lshrsi3 (SItype u, word_type b)
{
  SIunion w;
  word_type bm;
  SIunion uu;

  if (b == 0)
    return u;

  uu.ll = u;

  bm = (sizeof (HItype) * BITS_PER_UNIT) - b;
  if (bm <= 0)
    {
      w.s.high = 0;
      w.s.low = (UHItype)uu.s.high >> -bm;
    }
  else
    {
      UHItype carries = (UHItype)uu.s.high << bm;
      w.s.high = (UHItype)uu.s.high >> b;
      w.s.low = ((UHItype)uu.s.low >> b) | carries;
    }

  return w.ll;
}

SItype
__ashlsi3 (SItype u, word_type b)
{
  SIunion w;
  word_type bm;
  SIunion uu;

  if (b == 0)
    return u;

  uu.ll = u;

  bm = (sizeof (HItype) * BITS_PER_UNIT) - b;
  if (bm <= 0)
    {
      w.s.low = 0;
      w.s.high = (UHItype)uu.s.low << -bm;
    }
  else
    {
      UHItype carries = (UHItype)uu.s.low >> bm;
      w.s.low = (UHItype)uu.s.low << b;
      w.s.high = ((UHItype)uu.s.high << b) | carries;
    }

  return w.ll;
}

SItype
__ashrsi3 (SItype u, word_type b)
{
  SIunion w;
  word_type bm;
  SIunion uu;

  if (b == 0)
    return u;

  uu.ll = u;

  bm = (sizeof (HItype) * BITS_PER_UNIT) - b;
  if (bm <= 0)
    {
      /* w.s.high = 1..1 or 0..0 */
      w.s.high = uu.s.high >> (sizeof (HItype) * BITS_PER_UNIT - 1);
      w.s.low = uu.s.high >> -bm;
    }
  else
    {
      UHItype carries = (UHItype)uu.s.high << bm;
      w.s.high = uu.s.high >> b;
      w.s.low = ((UHItype)uu.s.low >> b) | carries;
    }

  return w.ll;
}

USItype
__mulsi3 (USItype a, USItype b)
{
  USItype c = 0;

  while (a != 0)
    {
      if (a & 1)
	c += b;
      a >>= 1;
      b <<= 1;
    }

  return c;
}

USItype
udivmodsi4(USItype num, USItype den, word_type modwanted)
{
  USItype bit = 1;
  USItype res = 0;

  while (den < num && bit && !(den & (1L<<31)))
    {
      den <<=1;
      bit <<=1;
    }
  while (bit)
    {
      if (num >= den)
	{
	  num -= den;
	  res |= bit;
	}
      bit >>=1;
      den >>=1;
    }
  if (modwanted) return num;
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
