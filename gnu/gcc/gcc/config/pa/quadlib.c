/* Subroutines for long double support.
   Copyright (C) 2000, 2002, 2004, 2005, 2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* HPUX TFmode compare requires a library call to _U_Qfcmp.  It takes
   a magic number as its third argument which indicates what to do.
   The return value is an integer to be compared against zero.  The
   comparison conditions are the same as those listed in Table 8-12
   of the PA-RISC 2.0 Architecture book for the fcmp instruction.  */

/* Raise FP_INVALID on SNaN as a side effect.  */
#define QCMP_INV 1

/* Comparison relations.  */
#define QCMP_UNORD 2
#define QCMP_EQ 4
#define QCMP_LT 8
#define QCMP_GT 16

int _U_Qfcmp (long double a, long double b, int);
long _U_Qfcnvfxt_quad_to_sgl (long double);

int _U_Qfeq (long double, long double);
int _U_Qfne (long double, long double);
int _U_Qfgt (long double, long double);
int _U_Qfge (long double, long double);
int _U_Qflt (long double, long double);
int _U_Qfle (long double, long double);
int _U_Qfltgt (long double, long double);
int _U_Qfunle (long double, long double);
int _U_Qfunlt (long double, long double);
int _U_Qfunge (long double, long double);
int _U_Qfungt (long double, long double);
int _U_Qfuneq (long double, long double);
int _U_Qfunord (long double, long double);
int _U_Qford (long double, long double);

int _U_Qfcomp (long double, long double);

long double _U_Qfneg (long double);

#ifdef __LP64__
int __U_Qfcnvfxt_quad_to_sgl (long double);
#endif
unsigned int _U_Qfcnvfxt_quad_to_usgl(long double);
long double _U_Qfcnvxf_usgl_to_quad (unsigned int);
unsigned long long _U_Qfcnvfxt_quad_to_udbl(long double);
long double _U_Qfcnvxf_udbl_to_quad (unsigned long long);

int
_U_Qfeq (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_EQ) != 0);
}

int
_U_Qfne (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_EQ) == 0);
}
	
int
_U_Qfgt (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_GT) != 0);
}

int
_U_Qfge (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_EQ | QCMP_GT) != 0);
}

int
_U_Qflt (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_LT) != 0);
}

int
_U_Qfle (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_EQ | QCMP_LT) != 0);
}

int
_U_Qfltgt (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_LT | QCMP_GT) != 0);
}

int
_U_Qfunle (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_UNORD | QCMP_EQ | QCMP_LT) != 0);
}

int
_U_Qfunlt (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_UNORD | QCMP_LT) != 0);
}

int
_U_Qfunge (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_UNORD | QCMP_EQ | QCMP_GT) != 0);
}

int
_U_Qfungt (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_UNORD | QCMP_GT) != 0);
}

int
_U_Qfuneq (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_UNORD | QCMP_EQ) != 0);
}

int
_U_Qfunord (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_UNORD) != 0);
}

int
_U_Qford (long double a, long double b)
{
  return (_U_Qfcmp (a, b, QCMP_INV | QCMP_EQ | QCMP_LT | QCMP_GT) != 0);
}

int
_U_Qfcomp (long double a, long double b)
{
  if (_U_Qfcmp (a, b, QCMP_EQ) == 0)
    return 0;

  return (_U_Qfcmp (a, b, QCMP_UNORD | QCMP_EQ | QCMP_GT) != 0 ? 1 : -1);
}


/* Negate long double A.  */
long double
_U_Qfneg (long double a)
{
  union
   {
     long double ld;
     int i[4];
   } u;

  u.ld = a;
  u.i[0] ^= 0x80000000;
  return u.ld;
}

#ifdef __LP64__
/* This routine is only necessary for the PA64 port; for reasons unknown
   _U_Qfcnvfxt_quad_to_sgl returns the integer in the high 32bits of the
   return value.  Ugh.  */
int
__U_Qfcnvfxt_quad_to_sgl (long double a)
{
  return _U_Qfcnvfxt_quad_to_sgl (a) >> 32;
}
#endif

/* HP only has signed conversion in the C library, so need to synthesize
   unsigned versions.  */
unsigned int
_U_Qfcnvfxt_quad_to_usgl (long double a)
{
  extern long long _U_Qfcnvfxt_quad_to_dbl (long double a);
  return (unsigned int) _U_Qfcnvfxt_quad_to_dbl (a);
}

long double
_U_Qfcnvxf_usgl_to_quad (unsigned int a)
{
  extern long double _U_Qfcnvxf_dbl_to_quad (long long);
  return _U_Qfcnvxf_dbl_to_quad ((long long) a);
}

typedef union {
    unsigned long long u[2];
    long double d[1];
} quad_type;

unsigned long long
_U_Qfcnvfxt_quad_to_udbl (long double a)
{
  extern quad_type _U_Qfcnvfxt_quad_to_quad (long double a);
  quad_type u;
  u = _U_Qfcnvfxt_quad_to_quad(a);
  return u.u[1];
}

long double
_U_Qfcnvxf_udbl_to_quad (unsigned long long a)
{
  extern long double _U_Qfcnvxf_quad_to_quad (quad_type a);
  quad_type u;
  u.u[0] = 0;
  u.u[1] = a;
  return _U_Qfcnvxf_quad_to_quad (u);
}
