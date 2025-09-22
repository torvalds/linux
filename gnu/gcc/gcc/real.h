/* Definitions of floating-point access for GNU compiler.
   Copyright (C) 1989, 1991, 1994, 1996, 1997, 1998, 1999,
   2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

#ifndef GCC_REAL_H
#define GCC_REAL_H

#include "machmode.h"

/* An expanded form of the represented number.  */

/* Enumerate the special cases of numbers that we encounter.  */
enum real_value_class {
  rvc_zero,
  rvc_normal,
  rvc_inf,
  rvc_nan
};

#define SIGNIFICAND_BITS	(128 + HOST_BITS_PER_LONG)
#define EXP_BITS		(32 - 6)
#define MAX_EXP			((1 << (EXP_BITS - 1)) - 1)
#define SIGSZ			(SIGNIFICAND_BITS / HOST_BITS_PER_LONG)
#define SIG_MSB			((unsigned long)1 << (HOST_BITS_PER_LONG - 1))

struct real_value GTY(())
{
  /* Use the same underlying type for all bit-fields, so as to make
     sure they're packed together, otherwise REAL_VALUE_TYPE_SIZE will
     be miscomputed.  */
  unsigned int /* ENUM_BITFIELD (real_value_class) */ cl : 2;
  unsigned int decimal : 1;
  unsigned int sign : 1;
  unsigned int signalling : 1;
  unsigned int canonical : 1;
  unsigned int uexp : EXP_BITS;
  unsigned long sig[SIGSZ];
};

#define REAL_EXP(REAL) \
  ((int)((REAL)->uexp ^ (unsigned int)(1 << (EXP_BITS - 1))) \
   - (1 << (EXP_BITS - 1)))
#define SET_REAL_EXP(REAL, EXP) \
  ((REAL)->uexp = ((unsigned int)(EXP) & (unsigned int)((1 << EXP_BITS) - 1)))

/* Various headers condition prototypes on #ifdef REAL_VALUE_TYPE, so it
   needs to be a macro.  We do need to continue to have a structure tag
   so that other headers can forward declare it.  */
#define REAL_VALUE_TYPE struct real_value

/* We store a REAL_VALUE_TYPE into an rtx, and we do this by putting it in
   consecutive "w" slots.  Moreover, we've got to compute the number of "w"
   slots at preprocessor time, which means we can't use sizeof.  Guess.  */

#define REAL_VALUE_TYPE_SIZE (SIGNIFICAND_BITS + 32)
#define REAL_WIDTH \
  (REAL_VALUE_TYPE_SIZE/HOST_BITS_PER_WIDE_INT \
   + (REAL_VALUE_TYPE_SIZE%HOST_BITS_PER_WIDE_INT ? 1 : 0)) /* round up */

/* Verify the guess.  */
extern char test_real_width
  [sizeof(REAL_VALUE_TYPE) <= REAL_WIDTH*sizeof(HOST_WIDE_INT) ? 1 : -1];

/* Calculate the format for CONST_DOUBLE.  We need as many slots as
   are necessary to overlay a REAL_VALUE_TYPE on them.  This could be
   as many as four (32-bit HOST_WIDE_INT, 128-bit REAL_VALUE_TYPE).

   A number of places assume that there are always at least two 'w'
   slots in a CONST_DOUBLE, so we provide them even if one would suffice.  */

#if REAL_WIDTH == 1
# define CONST_DOUBLE_FORMAT	 "ww"
#else
# if REAL_WIDTH == 2
#  define CONST_DOUBLE_FORMAT	 "ww"
# else
#  if REAL_WIDTH == 3
#   define CONST_DOUBLE_FORMAT	 "www"
#  else
#   if REAL_WIDTH == 4
#    define CONST_DOUBLE_FORMAT	 "wwww"
#   else
#    if REAL_WIDTH == 5
#     define CONST_DOUBLE_FORMAT "wwwww"
#    else
#     if REAL_WIDTH == 6
#      define CONST_DOUBLE_FORMAT "wwwwww"
#     else
       #error "REAL_WIDTH > 6 not supported"
#     endif
#    endif
#   endif
#  endif
# endif
#endif


/* Describes the properties of the specific target format in use.  */
struct real_format
{
  /* Move to and from the target bytes.  */
  void (*encode) (const struct real_format *, long *,
		  const REAL_VALUE_TYPE *);
  void (*decode) (const struct real_format *, REAL_VALUE_TYPE *,
		  const long *);

  /* The radix of the exponent and digits of the significand.  */
  int b;

  /* log2(b).  */
  int log2_b;

  /* Size of the significand in digits of radix B.  */
  int p;

  /* Size of the significant of a NaN, in digits of radix B.  */
  int pnan;

  /* The minimum negative integer, x, such that b**(x-1) is normalized.  */
  int emin;

  /* The maximum integer, x, such that b**(x-1) is representable.  */
  int emax;

  /* The bit position of the sign bit, for determining whether a value
     is positive/negative, or -1 for a complex encoding.  */
  int signbit_ro;

  /* The bit position of the sign bit, for changing the sign of a number,
     or -1 for a complex encoding.  */
  int signbit_rw;

  /* Properties of the format.  */
  bool has_nans;
  bool has_inf;
  bool has_denorm;
  bool has_signed_zero;
  bool qnan_msb_set;
};


/* The target format used for each floating point mode.
   Float modes are followed by decimal float modes, with entries for
   float modes indexed by (MODE - first float mode), and entries for
   decimal float modes indexed by (MODE - first decimal float mode) +
   the number of float modes.  */
extern const struct real_format *
  real_format_for_mode[MAX_MODE_FLOAT - MIN_MODE_FLOAT + 1
		       + MAX_MODE_DECIMAL_FLOAT - MIN_MODE_DECIMAL_FLOAT + 1];

#define REAL_MODE_FORMAT(MODE)						\
  (real_format_for_mode[DECIMAL_FLOAT_MODE_P (MODE)			\
			? ((MODE - MIN_MODE_DECIMAL_FLOAT)		\
			   + (MAX_MODE_FLOAT - MIN_MODE_FLOAT + 1))	\
			: (MODE - MIN_MODE_FLOAT)])

/* The following macro determines whether the floating point format is
   composite, i.e. may contain non-consecutive mantissa bits, in which
   case compile-time FP overflow may not model run-time overflow.  */
#define REAL_MODE_FORMAT_COMPOSITE_P(MODE) \
	((REAL_MODE_FORMAT(MODE))->pnan < (REAL_MODE_FORMAT (MODE))->p)

/* Declare functions in real.c.  */

/* Binary or unary arithmetic on tree_code.  */
extern bool real_arithmetic (REAL_VALUE_TYPE *, int, const REAL_VALUE_TYPE *,
			     const REAL_VALUE_TYPE *);

/* Compare reals by tree_code.  */
extern bool real_compare (int, const REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *);

/* Determine whether a floating-point value X is infinite.  */
extern bool real_isinf (const REAL_VALUE_TYPE *);

/* Determine whether a floating-point value X is a NaN.  */
extern bool real_isnan (const REAL_VALUE_TYPE *);

/* Determine whether a floating-point value X is negative.  */
extern bool real_isneg (const REAL_VALUE_TYPE *);

/* Determine whether a floating-point value X is minus zero.  */
extern bool real_isnegzero (const REAL_VALUE_TYPE *);

/* Compare two floating-point objects for bitwise identity.  */
extern bool real_identical (const REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *);

/* Extend or truncate to a new mode.  */
extern void real_convert (REAL_VALUE_TYPE *, enum machine_mode,
			  const REAL_VALUE_TYPE *);

/* Return true if truncating to NEW is exact.  */
extern bool exact_real_truncate (enum machine_mode, const REAL_VALUE_TYPE *);

/* Render R as a decimal floating point constant.  */
extern void real_to_decimal (char *, const REAL_VALUE_TYPE *, size_t,
			     size_t, int);

/* Render R as a hexadecimal floating point constant.  */
extern void real_to_hexadecimal (char *, const REAL_VALUE_TYPE *,
				 size_t, size_t, int);

/* Render R as an integer.  */
extern HOST_WIDE_INT real_to_integer (const REAL_VALUE_TYPE *);
extern void real_to_integer2 (HOST_WIDE_INT *, HOST_WIDE_INT *,
			      const REAL_VALUE_TYPE *);

/* Initialize R from a decimal or hexadecimal string.  */
extern void real_from_string (REAL_VALUE_TYPE *, const char *);
/* Wrapper to allow different internal representation for decimal floats. */
extern void real_from_string3 (REAL_VALUE_TYPE *, const char *, enum machine_mode);

/* Initialize R from an integer pair HIGH/LOW.  */
extern void real_from_integer (REAL_VALUE_TYPE *, enum machine_mode,
			       unsigned HOST_WIDE_INT, HOST_WIDE_INT, int);

extern long real_to_target_fmt (long *, const REAL_VALUE_TYPE *,
				const struct real_format *);
extern long real_to_target (long *, const REAL_VALUE_TYPE *, enum machine_mode);

extern void real_from_target_fmt (REAL_VALUE_TYPE *, const long *,
				  const struct real_format *);
extern void real_from_target (REAL_VALUE_TYPE *, const long *,
			      enum machine_mode);

extern void real_inf (REAL_VALUE_TYPE *);

extern bool real_nan (REAL_VALUE_TYPE *, const char *, int, enum machine_mode);

extern void real_maxval (REAL_VALUE_TYPE *, int, enum machine_mode);

extern void real_2expN (REAL_VALUE_TYPE *, int);

extern unsigned int real_hash (const REAL_VALUE_TYPE *);


/* Target formats defined in real.c.  */
extern const struct real_format ieee_single_format;
extern const struct real_format mips_single_format;
extern const struct real_format ieee_double_format;
extern const struct real_format mips_double_format;
extern const struct real_format ieee_extended_motorola_format;
extern const struct real_format ieee_extended_intel_96_format;
extern const struct real_format ieee_extended_intel_96_round_53_format;
extern const struct real_format ieee_extended_intel_128_format;
extern const struct real_format ibm_extended_format;
extern const struct real_format mips_extended_format;
extern const struct real_format ieee_quad_format;
extern const struct real_format mips_quad_format;
extern const struct real_format vax_f_format;
extern const struct real_format vax_d_format;
extern const struct real_format vax_g_format;
extern const struct real_format i370_single_format;
extern const struct real_format i370_double_format;
extern const struct real_format c4x_single_format;
extern const struct real_format c4x_extended_format;
extern const struct real_format real_internal_format;
extern const struct real_format decimal_single_format;
extern const struct real_format decimal_double_format;
extern const struct real_format decimal_quad_format;


/* ====================================================================== */
/* Crap.  */

#define REAL_ARITHMETIC(value, code, d1, d2) \
  real_arithmetic (&(value), code, &(d1), &(d2))

#define REAL_VALUES_IDENTICAL(x, y)	real_identical (&(x), &(y))
#define REAL_VALUES_EQUAL(x, y)		real_compare (EQ_EXPR, &(x), &(y))
#define REAL_VALUES_LESS(x, y)		real_compare (LT_EXPR, &(x), &(y))

/* Determine whether a floating-point value X is infinite.  */
#define REAL_VALUE_ISINF(x)		real_isinf (&(x))

/* Determine whether a floating-point value X is a NaN.  */
#define REAL_VALUE_ISNAN(x)		real_isnan (&(x))

/* Determine whether a floating-point value X is negative.  */
#define REAL_VALUE_NEGATIVE(x)		real_isneg (&(x))

/* Determine whether a floating-point value X is minus zero.  */
#define REAL_VALUE_MINUS_ZERO(x)	real_isnegzero (&(x))

/* IN is a REAL_VALUE_TYPE.  OUT is an array of longs.  */
#define REAL_VALUE_TO_TARGET_LONG_DOUBLE(IN, OUT)			\
  real_to_target (OUT, &(IN),						\
		  mode_for_size (LONG_DOUBLE_TYPE_SIZE, MODE_FLOAT, 0))

#define REAL_VALUE_TO_TARGET_DOUBLE(IN, OUT) \
  real_to_target (OUT, &(IN), mode_for_size (64, MODE_FLOAT, 0))

/* IN is a REAL_VALUE_TYPE.  OUT is a long.  */
#define REAL_VALUE_TO_TARGET_SINGLE(IN, OUT) \
  ((OUT) = real_to_target (NULL, &(IN), mode_for_size (32, MODE_FLOAT, 0)))

#define REAL_VALUE_FROM_INT(r, lo, hi, mode) \
  real_from_integer (&(r), mode, lo, hi, 0)

#define REAL_VALUE_FROM_UNSIGNED_INT(r, lo, hi, mode) \
  real_from_integer (&(r), mode, lo, hi, 1)

/* Real values to IEEE 754R decimal floats.  */

/* IN is a REAL_VALUE_TYPE.  OUT is an array of longs.  */
#define REAL_VALUE_TO_TARGET_DECIMAL128(IN, OUT) \
  real_to_target (OUT, &(IN), mode_for_size (128, MODE_DECIMAL_FLOAT, 0))

#define REAL_VALUE_TO_TARGET_DECIMAL64(IN, OUT) \
  real_to_target (OUT, &(IN), mode_for_size (64, MODE_DECIMAL_FLOAT, 0))

/* IN is a REAL_VALUE_TYPE.  OUT is a long.  */
#define REAL_VALUE_TO_TARGET_DECIMAL32(IN, OUT) \
  ((OUT) = real_to_target (NULL, &(IN), mode_for_size (32, MODE_DECIMAL_FLOAT, 0)))

extern REAL_VALUE_TYPE real_value_truncate (enum machine_mode,
					    REAL_VALUE_TYPE);

#define REAL_VALUE_TO_INT(plow, phigh, r) \
  real_to_integer2 (plow, phigh, &(r))

extern REAL_VALUE_TYPE real_arithmetic2 (int, const REAL_VALUE_TYPE *,
					 const REAL_VALUE_TYPE *);

#define REAL_VALUE_NEGATE(X) \
  real_arithmetic2 (NEGATE_EXPR, &(X), NULL)

#define REAL_VALUE_ABS(X) \
  real_arithmetic2 (ABS_EXPR, &(X), NULL)

extern int significand_size (enum machine_mode);

extern REAL_VALUE_TYPE real_from_string2 (const char *, enum machine_mode);

#define REAL_VALUE_ATOF(s, m) \
  real_from_string2 (s, m)

#define CONST_DOUBLE_ATOF(s, m) \
  CONST_DOUBLE_FROM_REAL_VALUE (real_from_string2 (s, m), m)

#define REAL_VALUE_FIX(r) \
  real_to_integer (&(r))

/* ??? Not quite right.  */
#define REAL_VALUE_UNSIGNED_FIX(r) \
  real_to_integer (&(r))

/* ??? These were added for Paranoia support.  */

/* Return floor log2(R).  */
extern int real_exponent (const REAL_VALUE_TYPE *);

/* R = A * 2**EXP.  */
extern void real_ldexp (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *, int);

/* **** End of software floating point emulator interface macros **** */

/* Constant real values 0, 1, 2, 3, 10, -1, -2, 0.5 and 1/3.  */

extern REAL_VALUE_TYPE dconst0;
extern REAL_VALUE_TYPE dconst1;
extern REAL_VALUE_TYPE dconst2;
extern REAL_VALUE_TYPE dconst3;
extern REAL_VALUE_TYPE dconst10;
extern REAL_VALUE_TYPE dconstm1;
extern REAL_VALUE_TYPE dconstm2;
extern REAL_VALUE_TYPE dconsthalf;
extern REAL_VALUE_TYPE dconstthird;
extern REAL_VALUE_TYPE dconstpi;
extern REAL_VALUE_TYPE dconste;

/* Function to return a real value (not a tree node)
   from a given integer constant.  */
REAL_VALUE_TYPE real_value_from_int_cst (tree, tree);

/* Given a CONST_DOUBLE in FROM, store into TO the value it represents.  */
#define REAL_VALUE_FROM_CONST_DOUBLE(to, from) \
  ((to) = *CONST_DOUBLE_REAL_VALUE (from))

/* Return a CONST_DOUBLE with value R and mode M.  */
#define CONST_DOUBLE_FROM_REAL_VALUE(r, m) \
  const_double_from_real_value (r, m)
extern rtx const_double_from_real_value (REAL_VALUE_TYPE, enum machine_mode);

/* Replace R by 1/R in the given machine mode, if the result is exact.  */
extern bool exact_real_inverse (enum machine_mode, REAL_VALUE_TYPE *);

/* In tree.c: wrap up a REAL_VALUE_TYPE in a tree node.  */
extern tree build_real (tree, REAL_VALUE_TYPE);

/* Calculate R as the square root of X in the given machine mode.  */
extern bool real_sqrt (REAL_VALUE_TYPE *, enum machine_mode,
		       const REAL_VALUE_TYPE *);

/* Calculate R as X raised to the integer exponent N in mode MODE.  */
extern bool real_powi (REAL_VALUE_TYPE *, enum machine_mode,
		       const REAL_VALUE_TYPE *, HOST_WIDE_INT);

/* Standard round to integer value functions.  */
extern void real_trunc (REAL_VALUE_TYPE *, enum machine_mode,
			const REAL_VALUE_TYPE *);
extern void real_floor (REAL_VALUE_TYPE *, enum machine_mode,
			const REAL_VALUE_TYPE *);
extern void real_ceil (REAL_VALUE_TYPE *, enum machine_mode,
		       const REAL_VALUE_TYPE *);
extern void real_round (REAL_VALUE_TYPE *, enum machine_mode,
			const REAL_VALUE_TYPE *);

/* Set the sign of R to the sign of X.  */
extern void real_copysign (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *);

#endif /* ! GCC_REAL_H */
