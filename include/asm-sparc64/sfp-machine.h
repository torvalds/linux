/* Machine-dependent software floating-point definitions.
   Sparc64 kernel version.
   Copyright (C) 1997,1998,1999 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Richard Henderson (rth@cygnus.com),
		  Jakub Jelinek (jj@ultra.linux.cz) and
		  David S. Miller (davem@redhat.com).

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

#ifndef _SFP_MACHINE_H
#define _SFP_MACHINE_H
   
#define _FP_W_TYPE_SIZE		64
#define _FP_W_TYPE		unsigned long
#define _FP_WS_TYPE		signed long
#define _FP_I_TYPE		long

#define _FP_MUL_MEAT_S(R,X,Y)					\
  _FP_MUL_MEAT_1_imm(_FP_WFRACBITS_S,R,X,Y)
#define _FP_MUL_MEAT_D(R,X,Y)					\
  _FP_MUL_MEAT_1_wide(_FP_WFRACBITS_D,R,X,Y,umul_ppmm)
#define _FP_MUL_MEAT_Q(R,X,Y)					\
  _FP_MUL_MEAT_2_wide(_FP_WFRACBITS_Q,R,X,Y,umul_ppmm)

#define _FP_DIV_MEAT_S(R,X,Y)	_FP_DIV_MEAT_1_imm(S,R,X,Y,_FP_DIV_HELP_imm)
#define _FP_DIV_MEAT_D(R,X,Y)	_FP_DIV_MEAT_1_udiv_norm(D,R,X,Y)
#define _FP_DIV_MEAT_Q(R,X,Y)	_FP_DIV_MEAT_2_udiv(Q,R,X,Y)

#define _FP_NANFRAC_S		((_FP_QNANBIT_S << 1) - 1)
#define _FP_NANFRAC_D		((_FP_QNANBIT_D << 1) - 1)
#define _FP_NANFRAC_Q		((_FP_QNANBIT_Q << 1) - 1), -1
#define _FP_NANSIGN_S		0
#define _FP_NANSIGN_D		0
#define _FP_NANSIGN_Q		0

#define _FP_KEEPNANFRACP 1

/* If one NaN is signaling and the other is not,
 * we choose that one, otherwise we choose X.
 */
/* For _Qp_* and _Q_*, this should prefer X, for
 * CPU instruction emulation this should prefer Y.
 * (see SPAMv9 B.2.2 section).
 */
#define _FP_CHOOSENAN(fs, wc, R, X, Y, OP)			\
  do {								\
    if ((_FP_FRAC_HIGH_RAW_##fs(Y) & _FP_QNANBIT_##fs)		\
	&& !(_FP_FRAC_HIGH_RAW_##fs(X) & _FP_QNANBIT_##fs))	\
      {								\
	R##_s = X##_s;						\
	_FP_FRAC_COPY_##wc(R,X);				\
      }								\
    else							\
      {								\
	R##_s = Y##_s;						\
	_FP_FRAC_COPY_##wc(R,Y);				\
      }								\
    R##_c = FP_CLS_NAN;						\
  } while (0)

/* Obtain the current rounding mode. */
#ifndef FP_ROUNDMODE
#define FP_ROUNDMODE	((current_thread_info()->xfsr[0] >> 30) & 0x3)
#endif

/* Exception flags. */
#define FP_EX_INVALID		(1 << 4)
#define FP_EX_OVERFLOW		(1 << 3)
#define FP_EX_UNDERFLOW		(1 << 2)
#define FP_EX_DIVZERO		(1 << 1)
#define FP_EX_INEXACT		(1 << 0)

#define FP_HANDLE_EXCEPTIONS return _fex

#define FP_INHIBIT_RESULTS ((current_thread_info()->xfsr[0] >> 23) & _fex)

#define FP_TRAPPING_EXCEPTIONS ((current_thread_info()->xfsr[0] >> 23) & 0x1f)

#endif
