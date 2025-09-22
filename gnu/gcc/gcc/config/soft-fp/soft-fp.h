/* Software floating-point emulation.
   Copyright (C) 1997,1998,1999,2000,2002,2003,2005,2006
	Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Richard Henderson (rth@cygnus.com),
		  Jakub Jelinek (jj@ultra.linux.cz),
		  David S. Miller (davem@redhat.com) and
		  Peter Maydell (pmaydell@chiark.greenend.org.uk).

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   In addition to the permissions in the GNU Lesser General Public
   License, the Free Software Foundation gives you unlimited
   permission to link the compiled version of this file into
   combinations with other programs, and to distribute those
   combinations without any restriction coming from the use of this
   file.  (The Lesser General Public License restrictions do apply in
   other respects; for example, they cover modification of the file,
   and distribution when not linked into a combine executable.)

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#ifndef SOFT_FP_H
#define SOFT_FP_H

#ifdef _LIBC
#include <sfp-machine.h>
#else
#include "sfp-machine.h"
#endif

/* Allow sfp-machine to have its own byte order definitions. */
#ifndef __BYTE_ORDER
#ifdef _LIBC
#include <endian.h>
#else
#error "endianness not defined by sfp-machine.h"
#endif
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
#endif
#ifndef FP_ROUNDMODE
# define FP_ROUNDMODE		FP_RND_NEAREST
#endif

/* By default don't care about exceptions. */
#ifndef FP_EX_INVALID
#define FP_EX_INVALID		0
#endif
#ifndef FP_EX_OVERFLOW
#define FP_EX_OVERFLOW		0
#endif
#ifndef FP_EX_UNDERFLOW
#define FP_EX_UNDERFLOW		0
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

#ifndef FP_INHIBIT_RESULTS
/* By default we write the results always.
 * sfp-machine may override this and e.g.
 * check if some exceptions are unmasked
 * and inhibit it in such a case.
 */
#define FP_INHIBIT_RESULTS 0
#endif

#define FP_SET_EXCEPTION(ex)				\
  _fex |= (ex)

#define FP_UNSET_EXCEPTION(ex)				\
  _fex &= ~(ex)

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

#include "op-1.h"
#include "op-2.h"
#include "op-4.h"
#include "op-8.h"
#include "op-common.h"

/* Sigh.  Silly things longlong.h needs.  */
#define UWtype		_FP_W_TYPE
#define W_TYPE_SIZE	_FP_W_TYPE_SIZE

typedef int QItype __attribute__((mode(QI)));
typedef int SItype __attribute__((mode(SI)));
typedef int DItype __attribute__((mode(DI)));
typedef unsigned int UQItype __attribute__((mode(QI)));
typedef unsigned int USItype __attribute__((mode(SI)));
typedef unsigned int UDItype __attribute__((mode(DI)));
#if _FP_W_TYPE_SIZE == 32
typedef unsigned int UHWtype __attribute__((mode(HI)));
#elif _FP_W_TYPE_SIZE == 64
typedef USItype UHWtype;
#endif

#define SI_BITS		(__CHAR_BIT__ * (int)sizeof(SItype))
#define DI_BITS		(__CHAR_BIT__ * (int)sizeof(DItype))

#ifndef umul_ppmm
#ifdef _LIBC
#include <stdlib/longlong.h>
#else
#include "longlong.h"
#endif
#endif

#ifdef _LIBC
#include <stdlib.h>
#else
extern void abort (void);
#endif

#endif
