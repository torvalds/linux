/* Software floating-point emulation.
   Copyright (C) 1997,1998,1999 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Richard Henderson (rth@cygnus.com),
		  Jakub Jelinek (jj@ultra.linux.cz),
		  David S. Miller (davem@redhat.com) and
		  Peter Maydell (pmaydell@chiark.greenend.org.uk).

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If
   not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#ifndef __MATH_EMU_SOFT_FP_H__
#define __MATH_EMU_SOFT_FP_H__

#include <asm/sfp-machine.h>

/* Allow sfp-machine to have its own byte order definitions. */
#ifndef __BYTE_ORDER
#include <endian.h>
#endif

#define _FP_WORKBITS		3
#define _FP_WORK_LSB		((_FP_W_TYPE)1 << 3)
#define _FP_WORK_ROUND		((_FP_W_TYPE)1 << 2)
#define _FP_WORK_GUARD		((_FP_W_TYPE)1 << 1)
#define _FP_WORK_STICKY		((_FP_W_TYPE)1 << 0)

#ifndef FP_RND_NEAREST
# define FP_RND_NEAREST		0
# define FP_RND_ZERO		1
# define FP_RND_PINF		2
# define FP_RND_MINF		3
#ifndef FP_ROUNDMODE
# define FP_ROUNDMODE		FP_RND_NEAREST
#endif
#endif

/* By default don't care about exceptions. */
#ifndef FP_EX_INVALID
#define FP_EX_INVALID		0
#endif
#ifndef FP_EX_INVALID_SNAN
#define FP_EX_INVALID_SNAN	0
#endif
/* inf - inf */
#ifndef FP_EX_INVALID_ISI
#define FP_EX_INVALID_ISI	0
#endif
/* inf / inf */
#ifndef FP_EX_INVALID_IDI
#define FP_EX_INVALID_IDI	0
#endif
/* 0 / 0 */
#ifndef FP_EX_INVALID_ZDZ
#define FP_EX_INVALID_ZDZ	0
#endif
/* inf * 0 */
#ifndef FP_EX_INVALID_IMZ
#define FP_EX_INVALID_IMZ	0
#endif
#ifndef FP_EX_OVERFLOW
#define FP_EX_OVERFLOW		0
#endif
#ifndef FP_EX_UNDERFLOW
#define FP_EX_UNDERFLOW		
#endif
#ifndef FP_EX_DIVZERO
#define FP_EX_DIVZERO		0
#endif
#ifndef FP_EX_INEXACT
#define FP_EX_INEXACT		0
#endif
#ifndef FP_EX_DENORM
#define FP_EX_DENORM		0
#endif

#ifdef _FP_DECL_EX
#define FP_DECL_EX					\
  int _fex = 0;						\
  _FP_DECL_EX
#else
#define FP_DECL_EX int _fex = 0
#endif
  
#ifndef FP_INIT_ROUNDMODE
#define FP_INIT_ROUNDMODE do {} while (0)
#endif

#ifndef FP_HANDLE_EXCEPTIONS
#define FP_HANDLE_EXCEPTIONS do {} while (0)
#endif

/* By default we never flush denormal input operands to signed zero. */
#ifndef FP_DENORM_ZERO
#define FP_DENORM_ZERO 0
#endif

#ifndef FP_INHIBIT_RESULTS
/* By default we write the results always.
 * sfp-machine may override this and e.g.
 * check if some exceptions are unmasked
 * and inhibit it in such a case.
 */
#define FP_INHIBIT_RESULTS 0
#endif

#ifndef FP_TRAPPING_EXCEPTIONS
#define FP_TRAPPING_EXCEPTIONS 0
#endif

#define FP_SET_EXCEPTION(ex)				\
  _fex |= (ex)
  
#define FP_UNSET_EXCEPTION(ex)				\
  _fex &= ~(ex)

#define FP_CUR_EXCEPTIONS				\
  (_fex)

#define FP_CLEAR_EXCEPTIONS				\
  _fex = 0

#define _FP_ROUND_NEAREST(wc, X)			\
do {							\
    if ((_FP_FRAC_LOW_##wc(X) & 15) != _FP_WORK_ROUND)	\
      _FP_FRAC_ADDI_##wc(X, _FP_WORK_ROUND);		\
} while (0)

#define _FP_ROUND_ZERO(wc, X)		(void)0

#define _FP_ROUND_PINF(wc, X)				\
do {							\
    if (!X##_s && (_FP_FRAC_LOW_##wc(X) & 7))		\
      _FP_FRAC_ADDI_##wc(X, _FP_WORK_LSB);		\
} while (0)

#define _FP_ROUND_MINF(wc, X)				\
do {							\
    if (X##_s && (_FP_FRAC_LOW_##wc(X) & 7))		\
      _FP_FRAC_ADDI_##wc(X, _FP_WORK_LSB);		\
} while (0)

#define _FP_ROUND(wc, X)			\
do {						\
	if (_FP_FRAC_LOW_##wc(X) & 7)		\
	  FP_SET_EXCEPTION(FP_EX_INEXACT);	\
	switch (FP_ROUNDMODE)			\
	{					\
	  case FP_RND_NEAREST:			\
	    _FP_ROUND_NEAREST(wc,X);		\
	    break;				\
	  case FP_RND_ZERO:			\
	    _FP_ROUND_ZERO(wc,X);		\
	    break;				\
	  case FP_RND_PINF:			\
	    _FP_ROUND_PINF(wc,X);		\
	    break;				\
	  case FP_RND_MINF:			\
	    _FP_ROUND_MINF(wc,X);		\
	    break;				\
	}					\
} while (0)

#define FP_CLS_NORMAL		0
#define FP_CLS_ZERO		1
#define FP_CLS_INF		2
#define FP_CLS_NAN		3

#define _FP_CLS_COMBINE(x,y)	(((x) << 2) | (y))

#include <math-emu/op-1.h>
#include <math-emu/op-2.h>
#include <math-emu/op-4.h>
#include <math-emu/op-8.h>
#include <math-emu/op-common.h>

/* Sigh.  Silly things longlong.h needs.  */
#define UWtype		_FP_W_TYPE
#define W_TYPE_SIZE	_FP_W_TYPE_SIZE

typedef int SItype __attribute__((mode(SI)));
typedef int DItype __attribute__((mode(DI)));
typedef unsigned int USItype __attribute__((mode(SI)));
typedef unsigned int UDItype __attribute__((mode(DI)));
#if _FP_W_TYPE_SIZE == 32
typedef unsigned int UHWtype __attribute__((mode(HI)));
#elif _FP_W_TYPE_SIZE == 64
typedef USItype UHWtype;
#endif

#ifndef umul_ppmm
#include <stdlib/longlong.h>
#endif

#endif /* __MATH_EMU_SOFT_FP_H__ */
