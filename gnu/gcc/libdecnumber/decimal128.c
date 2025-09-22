/* Decimal 128-bit format module from the decNumber C Library.
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

/* ------------------------------------------------------------------ */
/* This module comprises the routines for decimal128 format numbers.  */
/* Conversions are supplied to and from decNumber and String.         */
/*                                                                    */
/* No arithmetic routines are included; decNumber provides these.     */
/*                                                                    */
/* Error handling is the same as decNumber (qv.).                     */
/* ------------------------------------------------------------------ */
#include <string.h>		/* [for memset/memcpy] */
#include <stdio.h>		/* [for printf] */

#define  DECNUMDIGITS 34	/* we need decNumbers with space for 34 */
#include "config.h"
#include "decNumber.h"		/* base number library */
#include "decNumberLocal.h"	/* decNumber local types, etc. */
#include "decimal128.h"		/* our primary include */
#include "decUtility.h"		/* utility routines */

#if DECTRACE || DECCHECK
void decimal128Show (const decimal128 *);	/* for debug */
void decNumberShow (const decNumber *);	/* .. */
#endif

/* Useful macro */
/* Clear a structure (e.g., a decNumber) */
#define DEC_clear(d) memset(d, 0, sizeof(*d))

/* ------------------------------------------------------------------ */
/* decimal128FromNumber -- convert decNumber to decimal128            */
/*                                                                    */
/*   ds is the target decimal128                                      */
/*   dn is the source number (assumed valid)                          */
/*   set is the context, used only for reporting errors               */
/*                                                                    */
/* The set argument is used only for status reporting and for the     */
/* rounding mode (used if the coefficient is more than DECIMAL128_Pmax*/
/* digits or an overflow is detected).  If the exponent is out of the */
/* valid range then Overflow or Underflow will be raised.             */
/* After Underflow a subnormal result is possible.                    */
/*                                                                    */
/* DEC_Clamped is set if the number has to be 'folded down' to fit,   */
/* by reducing its exponent and multiplying the coefficient by a      */
/* power of ten, or if the exponent on a zero had to be clamped.      */
/* ------------------------------------------------------------------ */
decimal128 *
decimal128FromNumber (decimal128 * d128, const decNumber * dn, decContext * set)
{
  uInt status = 0;		/* status accumulator */
  Int pad = 0;			/* coefficient pad digits */
  decNumber dw;			/* work */
  decContext dc;		/* .. */
  uByte isneg = dn->bits & DECNEG;	/* non-0 if original sign set */
  uInt comb, exp;		/* work */

  /* If the number is finite, and has too many digits, or the exponent */
  /* could be out of range then we reduce the number under the */
  /* appropriate constraints */
  if (!(dn->bits & DECSPECIAL))
    {				/* not a special value */
      Int ae = dn->exponent + dn->digits - 1;	/* adjusted exponent */
      if (dn->digits > DECIMAL128_Pmax	/* too many digits */
	  || ae > DECIMAL128_Emax	/* likely overflow */
	  || ae < DECIMAL128_Emin)
	{			/* likely underflow */
	  decContextDefault (&dc, DEC_INIT_DECIMAL128);	/* [no traps] */
	  dc.round = set->round;	/* use supplied rounding */
	  decNumberPlus (&dw, dn, &dc);	/* (round and check) */
	  /* [this changes -0 to 0, but it will be restored below] */
	  status |= dc.status;	/* save status */
	  dn = &dw;		/* use the work number */
	}
      /* [this could have pushed number to Infinity or zero, so this */
      /* rounding must be done before we generate the decimal128] */
    }

  DEC_clear (d128);		/* clean the target */
  if (dn->bits & DECSPECIAL)
    {				/* a special value */
      uByte top;		/* work */
      if (dn->bits & DECINF)
	top = DECIMAL_Inf;
      else
	{			/* sNaN or qNaN */
	  if ((*dn->lsu != 0 || dn->digits > 1)	/* non-zero coefficient */
	      && (dn->digits < DECIMAL128_Pmax))
	    {			/* coefficient fits */
	      decDensePackCoeff (dn, d128->bytes, sizeof (d128->bytes), 0);
	    }
	  if (dn->bits & DECNAN)
	    top = DECIMAL_NaN;
	  else
	    top = DECIMAL_sNaN;
	}
      d128->bytes[0] = top;
    }
  else if (decNumberIsZero (dn))
    {				/* a zero */
      /* set and clamp exponent */
      if (dn->exponent < -DECIMAL128_Bias)
	{
	  exp = 0;
	  status |= DEC_Clamped;
	}
      else
	{
	  exp = dn->exponent + DECIMAL128_Bias;	/* bias exponent */
	  if (exp > DECIMAL128_Ehigh)
	    {			/* top clamp */
	      exp = DECIMAL128_Ehigh;
	      status |= DEC_Clamped;
	    }
	}
      comb = (exp >> 9) & 0x18;	/* combination field */
      d128->bytes[0] = (uByte) (comb << 2);
      exp &= 0xfff;		/* remaining exponent bits */
      decimal128SetExpCon (d128, exp);
    }
  else
    {				/* non-zero finite number */
      uInt msd;			/* work */

      /* we have a dn that fits, but it may need to be padded */
      exp = (uInt) (dn->exponent + DECIMAL128_Bias);	/* bias exponent */

      if (exp > DECIMAL128_Ehigh)
	{			/* fold-down case */
	  pad = exp - DECIMAL128_Ehigh;
	  exp = DECIMAL128_Ehigh;	/* [to maximum] */
	  status |= DEC_Clamped;
	}

      decDensePackCoeff (dn, d128->bytes, sizeof (d128->bytes), pad);

      /* save and clear the top digit */
      msd = ((unsigned) d128->bytes[1] << 2) & 0x0c;	/* top 2 bits */
      msd |= ((unsigned) d128->bytes[2] >> 6);	/* low 2 bits */
      d128->bytes[1] &= 0xfc;
      d128->bytes[2] &= 0x3f;

      /* create the combination field */
      if (msd >= 8)
	comb = 0x18 | (msd & 0x01) | ((exp >> 11) & 0x06);
      else
	comb = (msd & 0x07) | ((exp >> 9) & 0x18);
      d128->bytes[0] = (uByte) (comb << 2);
      exp &= 0xfff;		/* remaining exponent bits */
      decimal128SetExpCon (d128, exp);
    }

  if (isneg)
    decimal128SetSign (d128, 1);
  if (status != 0)
    decContextSetStatus (set, status);	/* pass on status */

  /* decimal128Show(d128); */
  return d128;
}

/* ------------------------------------------------------------------ */
/* decimal128ToNumber -- convert decimal128 to decNumber              */
/*   d128 is the source decimal128                                    */
/*   dn is the target number, with appropriate space                  */
/* No error is possible.                                              */
/* ------------------------------------------------------------------ */
decNumber *
decimal128ToNumber (const decimal128 * d128, decNumber * dn)
{
  uInt msd;			/* coefficient MSD */
  decimal128 wk;		/* working copy, if needed */
  uInt top = d128->bytes[0] & 0x7f;	/* top byte, less sign bit */
  decNumberZero (dn);		/* clean target */
  /* set the sign if negative */
  if (decimal128Sign (d128))
    dn->bits = DECNEG;

  if (top >= 0x78)
    {				/* is a special */
      if ((top & 0x7c) == (DECIMAL_Inf & 0x7c))
	dn->bits |= DECINF;
      else if ((top & 0x7e) == (DECIMAL_NaN & 0x7e))
	dn->bits |= DECNAN;
      else
	dn->bits |= DECSNAN;
      msd = 0;			/* no top digit */
    }
  else
    {				/* have a finite number */
      uInt comb = top >> 2;	/* combination field */
      uInt exp;			/* exponent */

      if (comb >= 0x18)
	{
	  msd = 8 + (comb & 0x01);
	  exp = (comb & 0x06) << 11;	/* MSBs */
	}
      else
	{
	  msd = comb & 0x07;
	  exp = (comb & 0x18) << 9;
	}
      dn->exponent = exp + decimal128ExpCon (d128) - DECIMAL128_Bias;	/* remove bias */
    }

  /* get the coefficient, unless infinite */
  if (!(dn->bits & DECINF))
    {
      Int bunches = DECIMAL128_Pmax / 3;	/* coefficient full bunches to convert */
      Int odd = 0;		/* assume MSD is 0 (no odd bunch) */
      if (msd != 0)
	{			/* coefficient has leading non-0 digit */
	  /* make a copy of the decimal128, with an extra bunch which has */
	  /* the top digit ready for conversion */
	  wk = *d128;		/* take a copy */
	  wk.bytes[0] = 0;	/* clear all but coecon */
	  wk.bytes[1] = 0;	/* .. */
	  wk.bytes[2] &= 0x3f;	/* .. */
	  wk.bytes[1] |= (msd >> 2);	/* and prefix MSD */
	  wk.bytes[2] |= (msd << 6);	/* .. */
	  odd++;		/* indicate the extra */
	  d128 = &wk;		/* use the work copy */
	}
      decDenseUnpackCoeff (d128->bytes, sizeof (d128->bytes), dn, bunches,
			   odd);
    }

  /* decNumberShow(dn); */
  return dn;
}

/* ------------------------------------------------------------------ */
/* to-scientific-string -- conversion to numeric string               */
/* to-engineering-string -- conversion to numeric string              */
/*                                                                    */
/*   decimal128ToString(d128, string);                                */
/*   decimal128ToEngString(d128, string);                             */
/*                                                                    */
/*  d128 is the decimal128 format number to convert                   */
/*  string is the string where the result will be laid out            */
/*                                                                    */
/*  string must be at least 24 characters                             */
/*                                                                    */
/*  No error is possible, and no status can be set.                   */
/* ------------------------------------------------------------------ */
char *
decimal128ToString (const decimal128 * d128, char *string)
{
  decNumber dn;			/* work */
  decimal128ToNumber (d128, &dn);
  decNumberToString (&dn, string);
  return string;
}

char *
decimal128ToEngString (const decimal128 * d128, char *string)
{
  decNumber dn;			/* work */
  decimal128ToNumber (d128, &dn);
  decNumberToEngString (&dn, string);
  return string;
}

/* ------------------------------------------------------------------ */
/* to-number -- conversion from numeric string                        */
/*                                                                    */
/*   decimal128FromString(result, string, set);                       */
/*                                                                    */
/*  result  is the decimal128 format number which gets the result of  */
/*          the conversion                                            */
/*  *string is the character string which should contain a valid      */
/*          number (which may be a special value)                     */
/*  set     is the context                                            */
/*                                                                    */
/* The context is supplied to this routine is used for error handling */
/* (setting of status and traps) and for the rounding mode, only.     */
/* If an error occurs, the result will be a valid decimal128 NaN.     */
/* ------------------------------------------------------------------ */
decimal128 *
decimal128FromString (decimal128 * result, const char *string, decContext * set)
{
  decContext dc;		/* work */
  decNumber dn;			/* .. */

  decContextDefault (&dc, DEC_INIT_DECIMAL128);	/* no traps, please */
  dc.round = set->round;	/* use supplied rounding */

  decNumberFromString (&dn, string, &dc);	/* will round if needed */
  decimal128FromNumber (result, &dn, &dc);
  if (dc.status != 0)
    {				/* something happened */
      decContextSetStatus (set, dc.status);	/* .. pass it on */
    }
  return result;
}


#if DECTRACE || DECCHECK
/* ------------------------------------------------------------------ */
/* decimal128Show -- display a single in hexadecimal [debug aid]      */
/*   d128 -- the number to show                                       */
/* ------------------------------------------------------------------ */
/* Also shows sign/cob/expconfields extracted */
void
decimal128Show (const decimal128 * d128)
{
  char buf[DECIMAL128_Bytes * 2 + 1];
  Int i, j;
  j = 0;
  for (i = 0; i < DECIMAL128_Bytes; i++)
    {
      sprintf (&buf[j], "%02x", d128->bytes[i]);
      j = j + 2;
    }
  printf (" D128> %s [S:%d Cb:%02x E:%d]\n", buf,
	  decimal128Sign (d128), decimal128Comb (d128),
	  decimal128ExpCon (d128));
}
#endif
