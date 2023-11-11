/* Software floating-point emulation.
   Definitions for IEEE Single Precision.
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

#ifndef    __MATH_EMU_SINGLE_H__
#define    __MATH_EMU_SINGLE_H__

#if _FP_W_TYPE_SIZE < 32
#error "Here's a nickel kid.  Go buy yourself a real computer."
#endif

#define _FP_FRACBITS_S		24
#define _FP_FRACXBITS_S		(_FP_W_TYPE_SIZE - _FP_FRACBITS_S)
#define _FP_WFRACBITS_S		(_FP_WORKBITS + _FP_FRACBITS_S)
#define _FP_WFRACXBITS_S	(_FP_W_TYPE_SIZE - _FP_WFRACBITS_S)
#define _FP_EXPBITS_S		8
#define _FP_EXPBIAS_S		127
#define _FP_EXPMAX_S		255
#define _FP_QNANBIT_S		((_FP_W_TYPE)1 << (_FP_FRACBITS_S-2))
#define _FP_IMPLBIT_S		((_FP_W_TYPE)1 << (_FP_FRACBITS_S-1))
#define _FP_OVERFLOW_S		((_FP_W_TYPE)1 << (_FP_WFRACBITS_S))

/* The implementation of _FP_MUL_MEAT_S and _FP_DIV_MEAT_S should be
   chosen by the target machine.  */

union _FP_UNION_S
{
  float flt;
  struct {
#if __BYTE_ORDER == __BIG_ENDIAN
    unsigned sign : 1;
    unsigned exp  : _FP_EXPBITS_S;
    unsigned frac : _FP_FRACBITS_S - (_FP_IMPLBIT_S != 0);
#else
    unsigned frac : _FP_FRACBITS_S - (_FP_IMPLBIT_S != 0);
    unsigned exp  : _FP_EXPBITS_S;
    unsigned sign : 1;
#endif
  } bits __attribute__((packed));
};

#define FP_DECL_S(X)		_FP_DECL(1,X)
#define FP_UNPACK_RAW_S(X,val)	_FP_UNPACK_RAW_1(S,X,val)
#define FP_UNPACK_RAW_SP(X,val)	_FP_UNPACK_RAW_1_P(S,X,val)
#define FP_PACK_RAW_S(val,X)	_FP_PACK_RAW_1(S,val,X)
#define FP_PACK_RAW_SP(val,X)		\
  do {					\
    if (!FP_INHIBIT_RESULTS)		\
      _FP_PACK_RAW_1_P(S,val,X);	\
  } while (0)

#define FP_UNPACK_S(X,val)		\
  do {					\
    _FP_UNPACK_RAW_1(S,X,val);		\
    _FP_UNPACK_CANONICAL(S,1,X);	\
  } while (0)

#define FP_UNPACK_SP(X,val)		\
  do {					\
    _FP_UNPACK_RAW_1_P(S,X,val);	\
    _FP_UNPACK_CANONICAL(S,1,X);	\
  } while (0)

#define FP_PACK_S(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(S,1,X);		\
    _FP_PACK_RAW_1(S,val,X);		\
  } while (0)

#define FP_PACK_SP(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(S,1,X);		\
    if (!FP_INHIBIT_RESULTS)		\
      _FP_PACK_RAW_1_P(S,val,X);	\
  } while (0)

#define FP_ISSIGNAN_S(X)		_FP_ISSIGNAN(S,1,X)
#define FP_NEG_S(R,X)			_FP_NEG(S,1,R,X)
#define FP_ADD_S(R,X,Y)			_FP_ADD(S,1,R,X,Y)
#define FP_SUB_S(R,X,Y)			_FP_SUB(S,1,R,X,Y)
#define FP_MUL_S(R,X,Y)			_FP_MUL(S,1,R,X,Y)
#define FP_DIV_S(R,X,Y)			_FP_DIV(S,1,R,X,Y)
#define FP_SQRT_S(R,X)			_FP_SQRT(S,1,R,X)
#define _FP_SQRT_MEAT_S(R,S,T,X,Q)	_FP_SQRT_MEAT_1(R,S,T,X,Q)

#define FP_CMP_S(r,X,Y,un)	_FP_CMP(S,1,r,X,Y,un)
#define FP_CMP_EQ_S(r,X,Y)	_FP_CMP_EQ(S,1,r,X,Y)

#define FP_TO_INT_S(r,X,rsz,rsg)	_FP_TO_INT(S,1,r,X,rsz,rsg)
#define FP_TO_INT_ROUND_S(r,X,rsz,rsg)	_FP_TO_INT_ROUND(S,1,r,X,rsz,rsg)
#define FP_FROM_INT_S(X,r,rs,rt)	_FP_FROM_INT(S,1,X,r,rs,rt)

#define _FP_FRAC_HIGH_S(X)	_FP_FRAC_HIGH_1(X)
#define _FP_FRAC_HIGH_RAW_S(X)	_FP_FRAC_HIGH_1(X)

#endif /* __MATH_EMU_SINGLE_H__ */
