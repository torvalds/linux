/* Definitions for simple data type for positive real numbers.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef GCC_SREAL_H
#define GCC_SREAL_H

/* SREAL_PART_BITS has to be an even number.  */
#if (HOST_BITS_PER_WIDE_INT / 2) % 2 == 1
#define SREAL_PART_BITS (HOST_BITS_PER_WIDE_INT / 2 - 1)
#else
#define SREAL_PART_BITS (HOST_BITS_PER_WIDE_INT / 2)
#endif

#define uhwi unsigned HOST_WIDE_INT
#define MAX_HOST_WIDE_INT (((uhwi) 1 << (HOST_BITS_PER_WIDE_INT - 1)) - 1)

#define SREAL_MIN_SIG ((uhwi) 1 << (SREAL_PART_BITS - 1))
#define SREAL_MAX_SIG (((uhwi) 1 << SREAL_PART_BITS) - 1)
#define SREAL_MAX_EXP (INT_MAX / 4)

#if SREAL_PART_BITS < 32
#define SREAL_BITS (SREAL_PART_BITS * 2)
#else
#define SREAL_BITS SREAL_PART_BITS
#endif

/* Structure for holding a simple real number.  */
typedef struct sreal
{
#if SREAL_PART_BITS < 32
  unsigned HOST_WIDE_INT sig_lo;	/* Significant (lower part).  */
  unsigned HOST_WIDE_INT sig_hi;	/* Significant (higher part).  */
#else
  unsigned HOST_WIDE_INT sig;		/* Significant.  */
#endif
  signed int exp;			/* Exponent.  */
} sreal;

extern void dump_sreal (FILE *, sreal *);
extern sreal *sreal_init (sreal *, unsigned HOST_WIDE_INT, signed int);
extern HOST_WIDE_INT sreal_to_int (sreal *);
extern int sreal_compare (sreal *, sreal *);
extern sreal *sreal_add (sreal *, sreal *, sreal *);
extern sreal *sreal_sub (sreal *, sreal *, sreal *);
extern sreal *sreal_mul (sreal *, sreal *, sreal *);
extern sreal *sreal_div (sreal *, sreal *, sreal *);

#endif
