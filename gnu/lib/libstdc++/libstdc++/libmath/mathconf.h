/* Configuration data for libmath subpart of libstdc++. */

/* Copyright (C) 1997-1999, 2000, 2001 Free Software Foundation, Inc.

   This file is part of the GNU ISO C++ Library.  This library is free
   software; you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option)
   any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this library; see the file COPYING.  If not, write to the Free
   Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA.

   As a special exception, you may use this file as part of a free software
   library without restriction.  Specifically, if other files instantiate
   templates or use macros or inline functions from this file, or you compile
   this file and link it with other files to produce an executable, this
   file does not by itself cause the resulting executable to be covered by
   the GNU General Public License.  This exception does not however
   invalidate any other reasons why the executable file might be covered by
   the GNU General Public License.  */


#include <config.h>

#ifdef HAVE_ENDIAN_H
# include <endian.h>
#else
# ifdef HAVE_MACHINE_ENDIAN_H
#  ifdef HAVE_SYS_TYPES_H
#   include <sys/types.h>
#  endif
#  include <machine/endian.h>
# else
#  ifdef HAVE_SYS_MACHINE_H
#   include <sys/machine.h>
#  else
#   if defined HAVE_SYS_ISA_DEFS_H || defined HAVE_MACHINE_PARAM_H
/* This is on Solaris.  */
#    ifdef HAVE_SYS_ISA_DEFS_H
#     include <sys/isa_defs.h>
#    endif
#    ifdef HAVE_MACHINE_PARAM_H
#     include <machine/param.h>
#    endif
#    ifdef _LITTLE_ENDIAN
#     define LITTLE_ENDIAN 1
#    endif
#    ifdef _BIG_ENDIAN
#     define BIG_ENDIAN 1
#    endif
#    define BYTE_ORDER 1
#   else
/* We have to rely on the AC_C_BIGENDIAN test.  */
#    ifdef WORDS_BIGENDIAN
#     define BIG_ENDIAN 1
#    else
#     define LITTLE_ENDIAN 1
#    endif
#    define BYTE_ORDER 1
#   endif
#  endif
# endif
#endif

typedef unsigned int U_int32_t __attribute ((mode (SI)));
typedef int Int32_t __attribute ((mode (SI)));
typedef unsigned int U_int64_t __attribute ((mode (DI)));
typedef int Int64_t __attribute ((mode (DI)));

#ifdef HAVE_NAN_H
# include <nan.h>
#endif

#ifndef NAN
# define NAN (nan())
double nan (void);
#endif

#ifdef HAVE_IEEEFP_H
# include <ieeefp.h>
#endif

#ifdef HAVE_FP_H
# include <fp.h>
#endif

#ifdef HAVE_FLOAT_H
# include <float.h>
#endif

/* `float' variant of HUGE_VAL.  */
#ifndef HUGE_VALF
# ifdef HUGE_VALf
#  define HUGE_VALF HUGE_VALf
# else
#  define HUGE_VALF HUGE_VAL
# endif
#endif

/* `long double' variant of HUGE_VAL.  */
#ifndef HUGE_VALL
# ifdef HUGE_VALl
#  define HUGE_VALL HUGE_VALl
# else
#  define HUGE_VALL HUGE_VAL
# endif
#endif

/* Make sure that at least HUGE_VAL is defined.  */
#ifndef HUGE_VAL
# ifdef HUGE
#  define HUGE_VAL HUGE
# else
#  ifdef MAXFLOAT
#   define HUGE_VAL MAXFLOAT
#  else
#   error "We need HUGE_VAL!"
#  endif
# endif
#endif

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif


#ifdef __cplusplus
extern "C" {
#endif

/* signbit is a macro in ISO C99.  */
#ifndef signbit
extern int __signbitf (float);
extern int __signbit (double);
extern int __signbitl (long double);

# define signbit(x) \
     (sizeof (x) == sizeof (float) ?                                          \
        __signbitf (x)                                                        \
      : sizeof (x) == sizeof (double) ?                                       \
        __signbit (x) : __signbitl (x))
#endif

#if BYTE_ORDER == BIG_ENDIAN
typedef union
{
  double value;
  struct
  {
    U_int32_t msw;
    U_int32_t lsw;
  } parts;
} ieee_double_shape_type;
#endif
#if BYTE_ORDER == LITTLE_ENDIAN
typedef union
{
  double value;
  struct
  {
    U_int32_t lsw;
    U_int32_t msw;
  } parts;
} ieee_double_shape_type;
#endif
/* Get the more significant 32 bit int from a double.  */
#define GET_HIGH_WORD(i,d)                                      \
do {                                                            \
  ieee_double_shape_type gh_u;                                  \
  gh_u.value = (d);                                             \
  (i) = gh_u.parts.msw;                                         \
} while (0)


typedef union
{
  float value;
  U_int32_t word;
} ieee_float_shape_type;
/* Get a 32 bit int from a float.  */
#define GET_FLOAT_WORD(i,d)                                     \
do {                                                            \
  ieee_float_shape_type gf_u;                                   \
  gf_u.value = (d);                                             \
  (i) = gf_u.word;                                              \
} while (0)


#if BYTE_ORDER == BIG_ENDIAN
typedef union
{
  long double value;
  struct
  {
    unsigned int sign_exponent:16;
    unsigned int empty:16;
    U_int32_t msw;
    U_int32_t lsw;
  } parts;
} ieee_long_double_shape_type;
#endif
#if BYTE_ORDER == LITTLE_ENDIAN
typedef union
{
  long double value;
  struct
  {
    U_int32_t lsw;
    U_int32_t msw;
    unsigned int sign_exponent:16;
    unsigned int empty:16;
  } parts;
} ieee_long_double_shape_type;
#endif
/* Get int from the exponent of a long double.  */
#define GET_LDOUBLE_EXP(exp,d)                                  \
do {                                                            \
  ieee_long_double_shape_type ge_u;                             \
  ge_u.value = (d);                                             \
  (exp) = ge_u.parts.sign_exponent;                             \
} while (0)

#if BYTE_ORDER == BIG_ENDIAN
typedef union
{
  long double value;
  struct
  {
    U_int64_t msw;
    U_int64_t lsw;
  } parts64;
  struct
  {
    U_int32_t w0, w1, w2, w3;
  } parts32;
} ieee_quad_double_shape_type;
#endif
#if BYTE_ORDER == LITTLE_ENDIAN
typedef union
{
  long double value;
  struct
  {
    U_int64_t lsw;
    U_int64_t msw;
  } parts64;
  struct
  {
    U_int32_t w3, w2, w1, w0;
  } parts32;
} ieee_quad_double_shape_type;
#endif
/* Get most significant 64 bit int from a quad long double.  */
#define GET_LDOUBLE_MSW64(msw,d)				\
do {								\
  ieee_quad_double_shape_type qw_u;				\
  qw_u.value = (d);						\
  (msw) = qw_u.parts64.msw;					\
} while (0)


/* Replacement for non-existing float functions.  */
#if !defined(HAVE_FABSF) && !defined(HAVE___BUILTIN_FABSF)
# define fabsf(x) fabs (x)
#endif
#if !defined(HAVE_COSF) && !defined(HAVE___BUILTIN_COSF)
# define cosf(x) cos (x)
#endif
#ifndef HAVE_COSHF
# define coshf(x) cosh (x)
#endif
#ifndef HAVE_EXPF
# define expf(x) expf (x)
#endif
#ifndef HAVE_LOGF
# define logf(x) log(x)
#endif
#ifndef HAVE_LOG10F
# define log10f(x) log10 (x)
#endif
#ifndef HAVE_POWF
# define powf(x, y) pow (x, y)
#endif
#if !defined(HAVE_SINF) && !defined(HAVE___BUILTIN_SINF)
# define sinf(x) sin (x)
#endif
#ifndef HAVE_SINHF
# define sinhf(x) sinh (x)
#endif
#if !defined(HAVE_SQRTF) && !defined(HAVE___BUILTIN_SQRTF)
# define sqrtf(x) sqrt (x)
#endif
#ifndef HAVE_TANF
# define tanf(x) tan (x)
#endif
#ifndef HAVE_TANHF
# define tanhf(x) tanh (x)
#endif
#ifndef HAVE_STRTOF
# define strtof(s, e) strtod (s, e)
#endif

#ifdef __cplusplus
}
#endif

