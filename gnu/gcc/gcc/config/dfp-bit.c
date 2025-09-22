/* This is a software decimal floating point library.
   Copyright (C) 2005, 2006 Free Software Foundation, Inc.

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

/* This implements IEEE 754R decimal floating point arithmetic, but
   does not provide a mechanism for setting the rounding mode, or for
   generating or handling exceptions.  Conversions between decimal
   floating point types and other types depend on C library functions.

   Contributed by Ben Elliston  <bje@au.ibm.com>.  */

/* The intended way to use this file is to make two copies, add `#define '
   to one copy, then compile both copies and add them to libgcc.a.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "config/dfp-bit.h"

/* Forward declarations.  */
#if WIDTH == 32 || WIDTH_TO == 32
void __host_to_ieee_32 (_Decimal32 in, decimal32 *out);
void __ieee_to_host_32 (decimal32 in, _Decimal32 *out);
#endif
#if WIDTH == 64 || WIDTH_TO == 64
void __host_to_ieee_64 (_Decimal64 in, decimal64 *out);
void __ieee_to_host_64 (decimal64 in, _Decimal64 *out);
#endif
#if WIDTH == 128 || WIDTH_TO == 128
void __host_to_ieee_128 (_Decimal128 in, decimal128 *out);
void __ieee_to_host_128 (decimal128 in, _Decimal128 *out);
#endif

/* A pointer to a unary decNumber operation.  */
typedef decNumber* (*dfp_unary_func)
     (decNumber *, decNumber *, decContext *);

/* A pointer to a binary decNumber operation.  */
typedef decNumber* (*dfp_binary_func)
     (decNumber *, decNumber *, decNumber *, decContext *);

extern unsigned long __dec_byte_swap (unsigned long);

/* Unary operations.  */

static inline DFP_C_TYPE
dfp_unary_op (dfp_unary_func op, DFP_C_TYPE arg)
{
  DFP_C_TYPE result;
  decContext context;
  decNumber arg1, res;
  IEEE_TYPE a, encoded_result;

  HOST_TO_IEEE (arg, &a);

  decContextDefault (&context, CONTEXT_INIT);
  context.round = CONTEXT_ROUND;

  TO_INTERNAL (&a, &arg1);

  /* Perform the operation.  */
  op (&res, &arg1, &context);

  if (CONTEXT_TRAPS && CONTEXT_ERRORS (context))
    DFP_RAISE (0);

  TO_ENCODED (&encoded_result, &res, &context);
  IEEE_TO_HOST (encoded_result, &result);
  return result;
}

/* Binary operations.  */

static inline DFP_C_TYPE
dfp_binary_op (dfp_binary_func op, DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  DFP_C_TYPE result;
  decContext context;
  decNumber arg1, arg2, res;
  IEEE_TYPE a, b, encoded_result;

  HOST_TO_IEEE (arg_a, &a);
  HOST_TO_IEEE (arg_b, &b);

  decContextDefault (&context, CONTEXT_INIT);
  context.round = CONTEXT_ROUND;

  TO_INTERNAL (&a, &arg1);
  TO_INTERNAL (&b, &arg2);

  /* Perform the operation.  */
  op (&res, &arg1, &arg2, &context);

  if (CONTEXT_TRAPS && CONTEXT_ERRORS (context))
    DFP_RAISE (0);

  TO_ENCODED (&encoded_result, &res, &context);
  IEEE_TO_HOST (encoded_result, &result);
  return result;
}

/* Comparison operations.  */

static inline int
dfp_compare_op (dfp_binary_func op, DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  IEEE_TYPE a, b;
  decContext context;
  decNumber arg1, arg2, res;
  int result;

  HOST_TO_IEEE (arg_a, &a);
  HOST_TO_IEEE (arg_b, &b);

  decContextDefault (&context, CONTEXT_INIT);
  context.round = CONTEXT_ROUND;

  TO_INTERNAL (&a, &arg1);
  TO_INTERNAL (&b, &arg2);

  /* Perform the comparison.  */
  op (&res, &arg1, &arg2, &context);

  if (CONTEXT_TRAPS && CONTEXT_ERRORS (context))
    DFP_RAISE (0);

  if (decNumberIsNegative (&res))
    result = -1;
  else if (decNumberIsZero (&res))
    result = 0;
  else
    result = 1;

  return result;
}


#if defined(L_conv_sd)
void
__host_to_ieee_32 (_Decimal32 in, decimal32 *out)
{
  uint32_t t;

  if (!LIBGCC2_FLOAT_WORDS_BIG_ENDIAN)
    {
      memcpy (&t, &in, 4);
      t = __dec_byte_swap (t);
      memcpy (out, &t, 4);
    }
  else
    memcpy (out, &in, 4);
}

void
__ieee_to_host_32 (decimal32 in, _Decimal32 *out)
{
  uint32_t t;

  if (!LIBGCC2_FLOAT_WORDS_BIG_ENDIAN)
    {
      memcpy (&t, &in, 4);
      t = __dec_byte_swap (t);
      memcpy (out, &t, 4);
    }
  else
    memcpy (out, &in, 4);
}
#endif /* L_conv_sd */

#if defined(L_conv_dd)
static void
__swap64 (char *src, char *dst)
{
  uint32_t t1, t2;

  if (!LIBGCC2_FLOAT_WORDS_BIG_ENDIAN) 
    {
      memcpy (&t1, src, 4);
      memcpy (&t2, src + 4, 4);
      t1 = __dec_byte_swap (t1);
      t2 = __dec_byte_swap (t2);
      memcpy (dst, &t2, 4);
      memcpy (dst + 4, &t1, 4);
    }
  else
    memcpy (dst, src, 8);
}

void
__host_to_ieee_64 (_Decimal64 in, decimal64 *out)
{
  __swap64 ((char *) &in, (char *) out);
}

void
__ieee_to_host_64 (decimal64 in, _Decimal64 *out)
{
  __swap64 ((char *) &in, (char *) out);
}
#endif /* L_conv_dd */

#if defined(L_conv_td)
static void
__swap128 (char *src, char *dst)
{
  uint32_t t1, t2, t3, t4;

  if (!LIBGCC2_FLOAT_WORDS_BIG_ENDIAN)
    {
      memcpy (&t1, src, 4);
      memcpy (&t2, src + 4, 4);
      memcpy (&t3, src + 8, 4);
      memcpy (&t4, src + 12, 4);
      t1 = __dec_byte_swap (t1);
      t2 = __dec_byte_swap (t2);
      t3 = __dec_byte_swap (t3);
      t4 = __dec_byte_swap (t4);
      memcpy (dst, &t4, 4);
      memcpy (dst + 4, &t3, 4);
      memcpy (dst + 8, &t2, 4);
      memcpy (dst + 12, &t1, 4);
    }
  else
    memcpy (dst, src, 16);
}

void
__host_to_ieee_128 (_Decimal128 in, decimal128 *out)
{
  __swap128 ((char *) &in, (char *) out);
}

void
__ieee_to_host_128 (decimal128 in, _Decimal128 *out)
{
  __swap128 ((char *) &in, (char *) out);
}
#endif /* L_conv_td */

#if defined(L_addsub_sd) || defined(L_addsub_dd) || defined(L_addsub_td)
DFP_C_TYPE
DFP_ADD (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  return dfp_binary_op (decNumberAdd, arg_a, arg_b);
}

DFP_C_TYPE
DFP_SUB (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  return dfp_binary_op (decNumberSubtract, arg_a, arg_b);
}
#endif /* L_addsub */

#if defined(L_mul_sd) || defined(L_mul_dd) || defined(L_mul_td)
DFP_C_TYPE
DFP_MULTIPLY (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  return dfp_binary_op (decNumberMultiply, arg_a, arg_b);
}
#endif /* L_mul */

#if defined(L_div_sd) || defined(L_div_dd) || defined(L_div_td)
DFP_C_TYPE
DFP_DIVIDE (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  return dfp_binary_op (decNumberDivide, arg_a, arg_b);
}
#endif /* L_div */

#if defined (L_eq_sd) || defined (L_eq_dd) || defined (L_eq_td)
CMPtype
DFP_EQ (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  int stat;
  stat = dfp_compare_op (decNumberCompare, arg_a, arg_b);
  /* For EQ return zero for true, nonzero for false.  */
  return stat != 0;
}
#endif /* L_eq */

#if defined (L_ne_sd) || defined (L_ne_dd) || defined (L_ne_td)
CMPtype
DFP_NE (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  int stat;
  stat = dfp_compare_op (decNumberCompare, arg_a, arg_b);
  /* For NE return nonzero for true, zero for false.  */
  return stat != 0;
}
#endif /* L_ne */

#if defined (L_lt_sd) || defined (L_lt_dd) || defined (L_lt_td)
CMPtype
DFP_LT (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  int stat;
  stat = dfp_compare_op (decNumberCompare, arg_a, arg_b);
  /* For LT return -1 (<0) for true, 1 for false.  */
  return (stat == -1) ? -1 : 1;
}
#endif /* L_lt */

#if defined (L_gt_sd) || defined (L_gt_dd) || defined (L_gt_td)
CMPtype
DFP_GT (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  int stat;
  stat = dfp_compare_op (decNumberCompare, arg_a, arg_b);
  /* For GT return 1 (>0) for true, -1 for false.  */
  return (stat == 1) ? 1 : -1;
}
#endif

#if defined (L_le_sd) || defined (L_le_dd) || defined (L_le_td)
CMPtype
DFP_LE (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  int stat;
  stat = dfp_compare_op (decNumberCompare, arg_a, arg_b);
  /* For LE return 0 (<= 0) for true, 1 for false.  */
  return stat == 1;
}
#endif /* L_le */

#if defined (L_ge_sd) || defined (L_ge_dd) || defined (L_ge_td)
CMPtype
DFP_GE (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  int stat;
  stat = dfp_compare_op (decNumberCompare, arg_a, arg_b);
  /* For GE return 1 (>=0) for true, -1 for false.  */
  return (stat != -1) ? 1 : -1;
}
#endif /* L_ge */

#define BUFMAX 128

#if defined (L_sd_to_dd) || defined (L_sd_to_td) || defined (L_dd_to_sd) \
 || defined (L_dd_to_td) || defined (L_td_to_sd) || defined (L_td_to_dd)
DFP_C_TYPE_TO
DFP_TO_DFP (DFP_C_TYPE f_from)
{
  DFP_C_TYPE_TO f_to;
  IEEE_TYPE s_from;
  IEEE_TYPE_TO s_to;
  decNumber d;
  decContext context;

  decContextDefault (&context, CONTEXT_INIT);
  context.round = CONTEXT_ROUND;

  HOST_TO_IEEE (f_from, &s_from);
  TO_INTERNAL (&s_from, &d);
  TO_ENCODED_TO (&s_to, &d, &context);
  if (CONTEXT_TRAPS && (context.status & DEC_Inexact) != 0)
    DFP_RAISE (DEC_Inexact);

  IEEE_TO_HOST_TO (s_to, &f_to);
  return f_to;
}
#endif

#if defined (L_sd_to_si) || defined (L_dd_to_si) || defined (L_td_to_si) \
  || defined (L_sd_to_di) || defined (L_dd_to_di) || defined (L_td_to_di) \
  || defined (L_sd_to_usi) || defined (L_dd_to_usi) || defined (L_td_to_usi) \
  || defined (L_sd_to_udi) || defined (L_dd_to_udi) || defined (L_td_to_udi)
INT_TYPE
DFP_TO_INT (DFP_C_TYPE x)
{
  /* decNumber's decimal* types have the same format as C's _Decimal*
     types, but they have different calling conventions.  */

  IEEE_TYPE s;
  char buf[BUFMAX];
  char *pos;
  decNumber qval, n1, n2;
  decContext context;

  decContextDefault (&context, CONTEXT_INIT);
  /* Need non-default rounding mode here.  */
  context.round = DEC_ROUND_DOWN;

  HOST_TO_IEEE (x, &s);
  TO_INTERNAL (&s, &n1);
  /* Rescale if the exponent is less than zero.  */
  decNumberToIntegralValue (&n2, &n1, &context);
  /* Get a value to use for the quantize call.  */
  decNumberFromString (&qval, (char *) "1.0", &context);
  /* Force the exponent to zero.  */
  decNumberQuantize (&n1, &n2, &qval, &context);
  /* This is based on text in N1107 section 5.1; it might turn out to be
     undefined behavior instead.  */
  if (context.status & DEC_Invalid_operation)
    {
#if defined (L_sd_to_si) || defined (L_dd_to_si) || defined (L_td_to_si)
      if (decNumberIsNegative(&n2))
        return INT_MIN;
      else
        return INT_MAX;
#elif defined (L_sd_to_di) || defined (L_dd_to_di) || defined (L_td_to_di)
      if (decNumberIsNegative(&n2))
        /* Find a defined constant that will work here.  */
        return (-9223372036854775807LL - 1LL);
      else
        /* Find a defined constant that will work here.  */
        return 9223372036854775807LL;
#elif defined (L_sd_to_usi) || defined (L_dd_to_usi) || defined (L_td_to_usi)
      return UINT_MAX;
#elif defined (L_sd_to_udi) || defined (L_dd_to_udi) || defined (L_td_to_udi)
        /* Find a defined constant that will work here.  */
      return 18446744073709551615ULL;
#endif
    }
  /* Get a string, which at this point will not include an exponent.  */
  decNumberToString (&n1, buf);
  /* Ignore the fractional part.  */
  pos = strchr (buf, '.');
  if (pos)
    *pos = 0;
  /* Use a C library function to convert to the integral type.  */
  return STR_TO_INT (buf, NULL, 10);
}
#endif

#if defined (L_si_to_sd) || defined (L_si_to_dd) || defined (L_si_to_td) \
  || defined (L_di_to_sd) || defined (L_di_to_dd) || defined (L_di_to_td) \
  || defined (L_usi_to_sd) || defined (L_usi_to_dd) || defined (L_usi_to_td) \
  || defined (L_udi_to_sd) || defined (L_udi_to_dd) || defined (L_udi_to_td)
DFP_C_TYPE
INT_TO_DFP (INT_TYPE i)
{
  DFP_C_TYPE f;
  IEEE_TYPE s;
  char buf[BUFMAX];
  decContext context;

  decContextDefault (&context, CONTEXT_INIT);
  context.round = CONTEXT_ROUND;

  /* Use a C library function to get a floating point string.  */
  sprintf (buf, INT_FMT ".0", CAST_FOR_FMT(i));
  /* Convert from the floating point string to a decimal* type.  */
  FROM_STRING (&s, buf, &context);
  IEEE_TO_HOST (s, &f);
  if (CONTEXT_TRAPS && (context.status & DEC_Inexact) != 0)
    DFP_RAISE (DEC_Inexact);
  return f;
}
#endif

#if defined (L_sd_to_sf) || defined (L_dd_to_sf) || defined (L_td_to_sf) \
 || defined (L_sd_to_df) || defined (L_dd_to_df) || defined (L_td_to_df) \
 || ((defined (L_sd_to_xf) || defined (L_dd_to_xf) || defined (L_td_to_xf)) \
     && LIBGCC2_HAS_XF_MODE)
BFP_TYPE
DFP_TO_BFP (DFP_C_TYPE f)
{
  IEEE_TYPE s;
  char buf[BUFMAX];

  HOST_TO_IEEE (f, &s);
  /* Write the value to a string.  */
  TO_STRING (&s, buf);
  /* Read it as the binary floating point type and return that.  */
  return STR_TO_BFP (buf, NULL);
}
#endif
                                                                                
#if defined (L_sf_to_sd) || defined (L_sf_to_dd) || defined (L_sf_to_td) \
 || defined (L_df_to_sd) || defined (L_df_to_dd) || defined (L_df_to_td) \
 || ((defined (L_xf_to_sd) || defined (L_xf_to_dd) || defined (L_xf_to_td)) \
     && LIBGCC2_HAS_XF_MODE)
DFP_C_TYPE
BFP_TO_DFP (BFP_TYPE x)
{
  DFP_C_TYPE f;
  IEEE_TYPE s;
  char buf[BUFMAX];
  decContext context;

  decContextDefault (&context, CONTEXT_INIT);
  context.round = CONTEXT_ROUND;

  /* Use a C library function to write the floating point value to a string.  */
#ifdef BFP_VIA_TYPE
  /* FIXME: Is there a better way to output an XFmode variable in C?  */
  sprintf (buf, BFP_FMT, (BFP_VIA_TYPE) x);
#else
  sprintf (buf, BFP_FMT, x);
#endif

  /* Convert from the floating point string to a decimal* type.  */
  FROM_STRING (&s, buf, &context);
  IEEE_TO_HOST (s, &f);
  if (CONTEXT_TRAPS && (context.status & DEC_Inexact) != 0)
    DFP_RAISE (DEC_Inexact);
  return f;
}
#endif

#if defined (L_unord_sd) || defined (L_unord_dd) || defined (L_unord_td)
CMPtype
DFP_UNORD (DFP_C_TYPE arg_a, DFP_C_TYPE arg_b)
{
  decNumber arg1, arg2;
  IEEE_TYPE a, b;

  HOST_TO_IEEE (arg_a, &a);
  HOST_TO_IEEE (arg_b, &b);
  TO_INTERNAL (&a, &arg1);
  TO_INTERNAL (&b, &arg2);
  return (decNumberIsNaN (&arg1) || decNumberIsNaN (&arg2));
}
#endif /* L_unord_sd || L_unord_dd || L_unord_td */
