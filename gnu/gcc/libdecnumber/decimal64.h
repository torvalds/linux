/* Decimal 64-bit format module header for the decNumber C Library
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by IBM Corporation.  Author Mike Cowlishaw.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2, or (at your option) any later
   version.

   In addition to the permissions in the GNU General Public License,
   the Free Software Foundation gives you unlimited permission to link
   the compiled version of this file into combinations with other
   programs, and to distribute those combinations without any
   restriction coming from the use of this file.  (The General Public
   License restrictions do apply in other respects; for example, they
   cover modification of the file, and distribution when not linked
   into a combine executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

#if !defined(DECIMAL64)
#define DECIMAL64
#define DEC64NAME     "decimal64"	/* Short name */
#define DEC64FULLNAME "Decimal 64-bit Number"	/* Verbose name */
#define DEC64AUTHOR   "Mike Cowlishaw"	/* Who to blame */

#if defined(DECIMAL32)
#error decimal64.h must precede decimal32.h for correct DECNUMDIGITS
#endif

  /* parameters for decimal64s */
#define DECIMAL64_Bytes  8	/* length */
#define DECIMAL64_Pmax   16	/* maximum precision (digits) */
#define DECIMAL64_Emax   384	/* maximum adjusted exponent */
#define DECIMAL64_Emin  -383	/* minimum adjusted exponent */
#define DECIMAL64_Bias   398	/* bias for the exponent */
#define DECIMAL64_String 24	/* maximum string length, +1 */
  /* highest biased exponent (Elimit-1) */
#define DECIMAL64_Ehigh  (DECIMAL64_Emax+DECIMAL64_Bias-DECIMAL64_Pmax+1)

#ifndef DECNUMDIGITS
#define DECNUMDIGITS DECIMAL64_Pmax	/* size if not already defined */
#endif
#ifndef DECNUMBER
#include "decNumber.h"		/* context and number library */
#endif

  /* Decimal 64-bit type, accessible by bytes */
typedef struct
{
  uint8_t bytes[DECIMAL64_Bytes];	/* decimal64: 1, 5, 8, 50 bits */
} decimal64;

  /* special values [top byte excluding sign bit; last two bits are
     don't-care for Infinity on input, last bit don't-care for NaN] */
#if !defined(DECIMAL_NaN)
#define DECIMAL_NaN     0x7c	/* 0 11111 00 NaN */
#define DECIMAL_sNaN    0x7e	/* 0 11111 10 sNaN */
#define DECIMAL_Inf     0x78	/* 0 11110 00 Infinity */
#endif

  /* Macros for accessing decimal64 fields.  These assume the argument
     is a reference (pointer) to the decimal64 structure */
  /* Get sign */
#define decimal64Sign(d)       ((unsigned)(d)->bytes[0]>>7)

  /* Get combination field */
#define decimal64Comb(d)       (((d)->bytes[0] & 0x7c)>>2)

  /* Get exponent continuation [does not remove bias] */
#define decimal64ExpCon(d)     ((((d)->bytes[0] & 0x03)<<6)         \
                               | ((unsigned)(d)->bytes[1]>>2))

  /* Set sign [this assumes sign previously 0] */
#define decimal64SetSign(d, b) {                                    \
    (d)->bytes[0]|=((unsigned)(b)<<7);}

  /* Set exponent continuation [does not apply bias] */
  /* This assumes range has been checked and exponent previously 0; type */
  /* of exponent must be unsigned */
#define decimal64SetExpCon(d, e) {                                  \
    (d)->bytes[0]|=(uint8_t)((e)>>6);                                 \
    (d)->bytes[1]|=(uint8_t)(((e)&0x3F)<<2);}

  /* ------------------------------------------------------------------ */
  /* Routines                                                           */
  /* ------------------------------------------------------------------ */

#ifdef IN_LIBGCC2
#define decimal64FromString __decimal64FromString
#define decimal64ToString __decimal64ToString
#define decimal64ToEngString __decimal64ToEngString
#define decimal64FromNumber __decimal64FromNumber
#define decimal64ToNumber __decimal64ToNumber
#endif

  /* String conversions */
decimal64 *decimal64FromString (decimal64 *, const char *, decContext *);
char *decimal64ToString (const decimal64 *, char *);
char *decimal64ToEngString (const decimal64 *, char *);

  /* decNumber conversions */
decimal64 *decimal64FromNumber (decimal64 *, const decNumber *, decContext *);
decNumber *decimal64ToNumber (const decimal64 *, decNumber *);

#endif
