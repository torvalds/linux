/* Header file for dfp-bit.c.
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

#ifndef _DFPBIT_H
#define _DFPBIT_H

#include "tconfig.h"
#include "coretypes.h"
#include "tm.h"

#ifndef LIBGCC2_WORDS_BIG_ENDIAN
#define LIBGCC2_WORDS_BIG_ENDIAN WORDS_BIG_ENDIAN
#endif

#ifndef LIBGCC2_FLOAT_WORDS_BIG_ENDIAN
#define LIBGCC2_FLOAT_WORDS_BIG_ENDIAN LIBGCC2_WORDS_BIG_ENDIAN
#endif

#ifndef LIBGCC2_LONG_DOUBLE_TYPE_SIZE
#define LIBGCC2_LONG_DOUBLE_TYPE_SIZE LONG_DOUBLE_TYPE_SIZE
#endif

#ifndef LIBGCC2_HAS_XF_MODE
#define LIBGCC2_HAS_XF_MODE \
  (BITS_PER_UNIT == 8 && LIBGCC2_LONG_DOUBLE_TYPE_SIZE == 80)
#endif

/* Depending on WIDTH, define a number of macros:

   DFP_C_TYPE: type of the arguments to the libgcc functions;
	(eg _Decimal32)

   IEEE_TYPE: the corresponding (encoded) IEEE754R type;
	(eg decimal32)
   
   TO_INTERNAL: the name of the decNumber function to convert an
   encoded value into the decNumber internal representation;

   TO_ENCODED: the name of the decNumber function to convert an
   internally represented decNumber into the encoded
   representation.

   FROM_STRING: the name of the decNumber function to read an
   encoded value from a string.

   TO_STRING: the name of the decNumber function to write an
   encoded value to a string.  */

#if WIDTH == 32
#define DFP_C_TYPE	_Decimal32
#define IEEE_TYPE	decimal32
#define HOST_TO_IEEE	__host_to_ieee_32
#define IEEE_TO_HOST	__ieee_to_host_32
#define TO_INTERNAL	__decimal32ToNumber
#define TO_ENCODED	__decimal32FromNumber
#define FROM_STRING	__decimal32FromString
#define TO_STRING	__decimal32ToString
#elif WIDTH == 64
#define DFP_C_TYPE	_Decimal64
#define IEEE_TYPE	decimal64
#define HOST_TO_IEEE	__host_to_ieee_64
#define IEEE_TO_HOST	__ieee_to_host_64
#define TO_INTERNAL	__decimal64ToNumber
#define TO_ENCODED	__decimal64FromNumber
#define FROM_STRING	__decimal64FromString
#define TO_STRING	__decimal64ToString
#elif WIDTH == 128
#define DFP_C_TYPE	_Decimal128
#define IEEE_TYPE	decimal128
#define HOST_TO_IEEE	__host_to_ieee_128
#define IEEE_TO_HOST	__ieee_to_host_128
#define TO_INTERNAL	__decimal128ToNumber
#define TO_ENCODED	__decimal128FromNumber
#define FROM_STRING	__decimal128FromString
#define TO_STRING	__decimal128ToString
#else
#error invalid decimal float word width
#endif

/* We define __DEC_EVAL_METHOD__ to 2, saying that we evaluate all
   operations and constants to the range and precision of the _Decimal128
   type.  Make it so.  */
#if WIDTH == 32
#define CONTEXT_INIT DEC_INIT_DECIMAL32
#elif WIDTH == 64
#define CONTEXT_INIT DEC_INIT_DECIMAL64
#elif WIDTH == 128
#define CONTEXT_INIT DEC_INIT_DECIMAL128
#endif

/* Define CONTEXT_ROUND to obtain the current decNumber rounding mode.  */
extern enum rounding	__decGetRound (void);
#define CONTEXT_ROUND	__decGetRound ()

extern int __dfp_traps;
#define CONTEXT_TRAPS	__dfp_traps
#define CONTEXT_ERRORS(context)	context.status & DEC_Errors
extern void __dfp_raise (int);
#define DFP_RAISE(A)	__dfp_raise(A)

/* Conversions between different decimal float types use WIDTH_TO to
   determine additional macros to define.  */

#if defined (L_dd_to_sd) || defined (L_td_to_sd)
#define WIDTH_TO 32
#elif defined (L_sd_to_dd) || defined (L_td_to_dd)
#define WIDTH_TO 64
#elif defined (L_sd_to_td) || defined (L_dd_to_td)
#define WIDTH_TO 128
#endif

/* If WIDTH_TO is defined, define additional macros:

   DFP_C_TYPE_TO: type of the result of dfp to dfp conversion.

   IEEE_TYPE_TO: the corresponding (encoded) IEEE754R type.

   TO_ENCODED_TO: the name of the decNumber function to convert an
   internally represented decNumber into the encoded representation
   for the destination.  */

#if WIDTH_TO == 32
#define DFP_C_TYPE_TO	_Decimal32
#define IEEE_TYPE_TO	decimal32
#define TO_ENCODED_TO	__decimal32FromNumber
#define IEEE_TO_HOST_TO __ieee_to_host_32
#elif WIDTH_TO == 64
#define DFP_C_TYPE_TO	_Decimal64
#define IEEE_TYPE_TO	decimal64
#define TO_ENCODED_TO	__decimal64FromNumber
#define IEEE_TO_HOST_TO __ieee_to_host_64
#elif WIDTH_TO == 128
#define DFP_C_TYPE_TO	_Decimal128
#define IEEE_TYPE_TO	decimal128
#define TO_ENCODED_TO	__decimal128FromNumber
#define IEEE_TO_HOST_TO __ieee_to_host_128
#endif

/* Conversions between decimal float types and integral types use INT_KIND
   to determine the data type and C functions to use.  */

#if defined (L_sd_to_si) || defined (L_dd_to_si) || defined (L_td_to_si)  \
   || defined (L_si_to_sd) || defined (L_si_to_dd) || defined (L_si_to_td)
#define INT_KIND 1
#elif defined (L_sd_to_di) || defined (L_dd_to_di) || defined (L_td_to_di) \
   || defined (L_di_to_sd) || defined (L_di_to_dd) || defined (L_di_to_td)
#define INT_KIND 2
#elif defined (L_sd_to_usi) || defined (L_dd_to_usi) || defined (L_td_to_usi) \
   || defined (L_usi_to_sd) || defined (L_usi_to_dd) || defined (L_usi_to_td)
#define INT_KIND 3
#elif defined (L_sd_to_udi) || defined (L_dd_to_udi) || defined (L_td_to_udi) \
   || defined (L_udi_to_sd) || defined (L_udi_to_dd) || defined (L_udi_to_td)
#define INT_KIND 4
#endif

/*  If INT_KIND is defined, define additional macros:

    INT_TYPE: The integer data type.

    INT_FMT: The format string for writing the integer to a string.

    CAST_FOR_FMT: Cast variable of INT_KIND to C type for sprintf.
    This works for ILP32 and LP64, won't for other type size systems.

    STR_TO_INT: The function to read the integer from a string.  */

#if INT_KIND == 1
#define INT_TYPE SItype
#define INT_FMT "%d"
#define CAST_FOR_FMT(A) (int)A
#define STR_TO_INT strtol
#elif INT_KIND == 2
#define INT_TYPE DItype
#define INT_FMT "%lld"
#define CAST_FOR_FMT(A) (long long)A
#define STR_TO_INT strtoll
#elif INT_KIND == 3
#define INT_TYPE USItype
#define INT_FMT "%u"
#define CAST_FOR_FMT(A) (unsigned int)A
#define STR_TO_INT strtoul
#elif INT_KIND == 4
#define INT_TYPE UDItype
#define INT_FMT "%llu"
#define CAST_FOR_FMT(A) (unsigned long long)A
#define STR_TO_INT strtoull
#endif

/* Conversions between decimal float types and binary float types use
   BFP_KIND to determine the data type and C functions to use.  */

#if defined (L_sd_to_sf) || defined (L_dd_to_sf) || defined (L_td_to_sf) \
 || defined (L_sf_to_sd) || defined (L_sf_to_dd) || defined (L_sf_to_td)
#define BFP_KIND 1
#elif defined (L_sd_to_df) || defined (L_dd_to_df ) || defined (L_td_to_df) \
 ||   defined (L_df_to_sd) || defined (L_df_to_dd) || defined (L_df_to_td)
#define BFP_KIND 2
#elif defined (L_sd_to_xf) || defined (L_dd_to_xf ) || defined (L_td_to_xf) \
 ||   defined (L_xf_to_sd) || defined (L_xf_to_dd) || defined (L_xf_to_td)
#define BFP_KIND 3
#endif

/*  If BFP_KIND is defined, define additional macros:

    BFP_TYPE: The binary floating point data type.

    BFP_FMT: The format string for writing the value to a string.

    STR_TO_BFP: The function to read the value from a string.  */

#if BFP_KIND == 1
/* strtof is declared in <stdlib.h> only for C99.  */
extern float strtof (const char *, char **);
#define BFP_TYPE SFtype
#define BFP_FMT "%e"
#define STR_TO_BFP strtof

#elif BFP_KIND == 2
#define BFP_TYPE DFtype
#define BFP_FMT "%e"
#define STR_TO_BFP strtod

#elif BFP_KIND == 3
#if LIBGCC2_HAS_XF_MODE
/* These aren't used if XF mode is not supported.  */
#define BFP_TYPE XFtype
#define BFP_FMT "%e"
#define BFP_VIA_TYPE double
#define STR_TO_BFP strtod
#endif

#endif /* BFP_KIND */

#if WIDTH == 128 || WIDTH_TO == 128
#include "decimal128.h"
#endif
#if WIDTH == 64 || WIDTH_TO == 64
#include "decimal64.h"
#endif
#if WIDTH == 32 || WIDTH_TO == 32
#include "decimal32.h"
#endif
#include "decNumber.h"

/* Names of arithmetic functions.  */

#if WIDTH == 32
#define DFP_ADD		__addsd3
#define DFP_SUB		__subsd3
#define DFP_MULTIPLY	__mulsd3
#define DFP_DIVIDE	__divsd3
#define DFP_EQ		__eqsd2
#define DFP_NE		__nesd2
#define DFP_LT		__ltsd2
#define DFP_GT		__gtsd2
#define DFP_LE		__lesd2
#define DFP_GE		__gesd2
#define DFP_UNORD	__unordsd2
#elif WIDTH == 64
#define DFP_ADD		__adddd3
#define DFP_SUB		__subdd3
#define DFP_MULTIPLY	__muldd3
#define DFP_DIVIDE	__divdd3
#define DFP_EQ		__eqdd2
#define DFP_NE		__nedd2
#define DFP_LT		__ltdd2
#define DFP_GT		__gtdd2
#define DFP_LE		__ledd2
#define DFP_GE		__gedd2
#define DFP_UNORD	__unorddd2
#elif WIDTH == 128
#define DFP_ADD		__addtd3
#define DFP_SUB		__subtd3
#define DFP_MULTIPLY	__multd3
#define DFP_DIVIDE	__divtd3
#define DFP_EQ		__eqtd2
#define DFP_NE		__netd2
#define DFP_LT		__lttd2
#define DFP_GT		__gttd2
#define DFP_LE		__letd2
#define DFP_GE		__getd2
#define DFP_UNORD	__unordtd2
#endif

/* Names of functions to convert between different decimal float types.  */

#if WIDTH == 32
#if WIDTH_TO == 64
#define DFP_TO_DFP	__extendsddd2
#elif WIDTH_TO == 128
#define DFP_TO_DFP	__extendsdtd2
#endif
#elif WIDTH == 64	
#if WIDTH_TO == 32
#define DFP_TO_DFP	__truncddsd2
#elif WIDTH_TO == 128
#define DFP_TO_DFP	__extendddtd2
#endif
#elif WIDTH == 128
#if WIDTH_TO == 32
#define DFP_TO_DFP	__trunctdsd2
#elif WIDTH_TO == 64
#define DFP_TO_DFP	__trunctddd2
#endif
#endif

/* Names of functions to convert between decimal float and integers.  */

#if WIDTH == 32
#if INT_KIND == 1
#define INT_TO_DFP	__floatsisd
#define DFP_TO_INT	__fixsdsi
#elif INT_KIND == 2
#define INT_TO_DFP	__floatdisd
#define DFP_TO_INT	__fixsddi
#elif INT_KIND == 3
#define INT_TO_DFP	__floatunssisd
#define DFP_TO_INT	__fixunssdsi
#elif INT_KIND == 4
#define INT_TO_DFP	__floatunsdisd
#define DFP_TO_INT	__fixunssddi
#endif
#elif WIDTH == 64
#if INT_KIND == 1
#define INT_TO_DFP	__floatsidd
#define DFP_TO_INT	__fixddsi
#elif INT_KIND == 2
#define INT_TO_DFP	__floatdidd
#define DFP_TO_INT	__fixdddi
#elif INT_KIND == 3
#define INT_TO_DFP	__floatunssidd
#define DFP_TO_INT	__fixunsddsi
#elif INT_KIND == 4
#define INT_TO_DFP	__floatunsdidd
#define DFP_TO_INT	__fixunsdddi
#endif
#elif WIDTH == 128
#if INT_KIND == 1
#define INT_TO_DFP	__floatsitd
#define DFP_TO_INT	__fixtdsi
#elif INT_KIND == 2
#define INT_TO_DFP	__floatditd
#define DFP_TO_INT	__fixtddi
#elif INT_KIND == 3
#define INT_TO_DFP	__floatunssitd
#define DFP_TO_INT	__fixunstdsi
#elif INT_KIND == 4
#define INT_TO_DFP	__floatunsditd
#define DFP_TO_INT	__fixunstddi
#endif
#endif

/* Names of functions to convert between decimal float and binary float.  */

#if WIDTH == 32
#if BFP_KIND == 1
#define BFP_TO_DFP	__extendsfsd
#define DFP_TO_BFP	__truncsdsf
#elif BFP_KIND == 2
#define BFP_TO_DFP	__truncdfsd
#define DFP_TO_BFP	__extendsddf
#elif BFP_KIND == 3
#define BFP_TO_DFP	__truncxfsd
#define DFP_TO_BFP	__extendsdxf
#endif /* BFP_KIND */

#elif WIDTH == 64
#if BFP_KIND == 1
#define BFP_TO_DFP	__extendsfdd
#define DFP_TO_BFP	__truncddsf
#elif BFP_KIND == 2
#define BFP_TO_DFP	__extenddfdd
#define DFP_TO_BFP	__truncdddf
#elif BFP_KIND == 3
#define BFP_TO_DFP	__truncxfdd
#define DFP_TO_BFP	__extendddxf
#endif /* BFP_KIND */

#elif WIDTH == 128
#if BFP_KIND == 1
#define BFP_TO_DFP	__extendsftd
#define DFP_TO_BFP	__trunctdsf
#elif BFP_KIND == 2
#define BFP_TO_DFP	__extenddftd
#define DFP_TO_BFP	__trunctddf
#elif BFP_KIND == 3
#define BFP_TO_DFP	__extendxftd
#define DFP_TO_BFP	__trunctdxf
#endif /* BFP_KIND */

#endif /* WIDTH */

/* Some handy typedefs.  */

typedef float SFtype __attribute__ ((mode (SF)));
typedef float DFtype __attribute__ ((mode (DF)));
#if LIBGCC2_HAS_XF_MODE
typedef float XFtype __attribute__ ((mode (XF)));
#endif /* LIBGCC2_HAS_XF_MODE */

typedef int SItype __attribute__ ((mode (SI)));
typedef int DItype __attribute__ ((mode (DI)));
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef unsigned int UDItype __attribute__ ((mode (DI)));

/* The type of the result of a decimal float comparison.  This must
   match `word_mode' in GCC for the target.  */

typedef int CMPtype __attribute__ ((mode (word)));

/* Prototypes.  */

#if defined (L_mul_sd) || defined (L_mul_dd) || defined (L_mul_td)
extern DFP_C_TYPE DFP_MULTIPLY (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_div_sd) || defined (L_div_dd) || defined (L_div_td)
extern DFP_C_TYPE DFP_DIVIDE (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_addsub_sd) || defined (L_addsub_dd) || defined (L_addsub_td)
extern DFP_C_TYPE DFP_ADD (DFP_C_TYPE, DFP_C_TYPE);
extern DFP_C_TYPE DFP_SUB (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_eq_sd) || defined (L_eq_dd) || defined (L_eq_td)
extern CMPtype DFP_EQ (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_ne_sd) || defined (L_ne_dd) || defined (L_ne_td)
extern CMPtype DFP_NE (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_lt_sd) || defined (L_lt_dd) || defined (L_lt_td)
extern CMPtype DFP_LT (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_gt_sd) || defined (L_gt_dd) || defined (L_gt_td)
extern CMPtype DFP_GT (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_le_sd) || defined (L_le_dd) || defined (L_le_td)
extern CMPtype DFP_LE (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_ge_sd) || defined (L_ge_dd) || defined (L_ge_td)
extern CMPtype DFP_GE (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_unord_sd) || defined (L_unord_dd) || defined (L_unord_td)
extern CMPtype DFP_UNORD (DFP_C_TYPE, DFP_C_TYPE);
#endif

#if defined (L_sd_to_dd) || defined (L_sd_to_td) || defined (L_dd_to_sd) \
 || defined (L_dd_to_td) || defined (L_td_to_sd) || defined (L_td_to_dd)
extern DFP_C_TYPE_TO DFP_TO_DFP (DFP_C_TYPE);
#endif

#if defined (L_sd_to_si) || defined (L_dd_to_si) || defined (L_td_to_si) \
 || defined (L_sd_to_di) || defined (L_dd_to_di) || defined (L_td_to_di) \
 || defined (L_sd_to_usi) || defined (L_dd_to_usi) || defined (L_td_to_usi) \
 || defined (L_sd_to_udi) || defined (L_dd_to_udi) || defined (L_td_to_udi)
extern INT_TYPE DFP_TO_INT (DFP_C_TYPE);
#endif

#if defined (L_si_to_sd) || defined (L_si_to_dd) || defined (L_si_to_td) \
 || defined (L_di_to_sd) || defined (L_di_to_dd) || defined (L_di_to_td) \
 || defined (L_usi_to_sd) || defined (L_usi_to_dd) || defined (L_usi_to_td) \
 || defined (L_udi_to_sd) || defined (L_udi_to_dd) || defined (L_udi_to_td)
extern DFP_C_TYPE INT_TO_DFP (INT_TYPE);
#endif

#if defined (L_sd_to_sf) || defined (L_dd_to_sf) || defined (L_td_to_sf) \
 || defined (L_sd_to_df) || defined (L_dd_to_df) || defined (L_td_to_df) \
 || ((defined (L_sd_to_xf) || defined (L_dd_to_xf) || defined (L_td_to_xf)) \
     && LIBGCC2_HAS_XF_MODE)
extern BFP_TYPE DFP_TO_BFP (DFP_C_TYPE);
#endif

#if defined (L_sf_to_sd) || defined (L_sf_to_dd) || defined (L_sf_to_td) \
 || defined (L_df_to_sd) || defined (L_df_to_dd) || defined (L_df_to_td) \
 || ((defined (L_xf_to_sd) || defined (L_xf_to_dd) || defined (L_xf_to_td)) \
     && LIBGCC2_HAS_XF_MODE)
extern DFP_C_TYPE BFP_TO_DFP (BFP_TYPE);
#endif

#endif /* _DFPBIT_H */
