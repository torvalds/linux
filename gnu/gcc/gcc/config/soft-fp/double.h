/* Software floating-point emulation.
   Definitions for IEEE Double Precision
   Copyright (C) 1997,1998,1999,2006,2007 Free Software Foundation, Inc.
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

#if _FP_W_TYPE_SIZE < 32
#error "Here's a nickel kid.  Go buy yourself a real computer."
#endif

#if _FP_W_TYPE_SIZE < 64
#define _FP_FRACTBITS_D		(2 * _FP_W_TYPE_SIZE)
#else
#define _FP_FRACTBITS_D		_FP_W_TYPE_SIZE
#endif

#define _FP_FRACBITS_D		53
#define _FP_FRACXBITS_D		(_FP_FRACTBITS_D - _FP_FRACBITS_D)
#define _FP_WFRACBITS_D		(_FP_WORKBITS + _FP_FRACBITS_D)
#define _FP_WFRACXBITS_D	(_FP_FRACTBITS_D - _FP_WFRACBITS_D)
#define _FP_EXPBITS_D		11
#define _FP_EXPBIAS_D		1023
#define _FP_EXPMAX_D		2047

#define _FP_QNANBIT_D		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_D-2) % _FP_W_TYPE_SIZE)
#define _FP_QNANBIT_SH_D		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_D-2+_FP_WORKBITS) % _FP_W_TYPE_SIZE)
#define _FP_IMPLBIT_D		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_D-1) % _FP_W_TYPE_SIZE)
#define _FP_IMPLBIT_SH_D		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_D-1+_FP_WORKBITS) % _FP_W_TYPE_SIZE)
#define _FP_OVERFLOW_D		\
	((_FP_W_TYPE)1 << _FP_WFRACBITS_D % _FP_W_TYPE_SIZE)

typedef float DFtype __attribute__((mode(DF)));

#if _FP_W_TYPE_SIZE < 64

union _FP_UNION_D
{
  DFtype flt;
  struct {
#if __BYTE_ORDER == __BIG_ENDIAN
    unsigned sign  : 1;
    unsigned exp   : _FP_EXPBITS_D;
    unsigned frac1 : _FP_FRACBITS_D - (_FP_IMPLBIT_D != 0) - _FP_W_TYPE_SIZE;
    unsigned frac0 : _FP_W_TYPE_SIZE;
#else
    unsigned frac0 : _FP_W_TYPE_SIZE;
    unsigned frac1 : _FP_FRACBITS_D - (_FP_IMPLBIT_D != 0) - _FP_W_TYPE_SIZE;
    unsigned exp   : _FP_EXPBITS_D;
    unsigned sign  : 1;
#endif
  } bits __attribute__((packed));
};

#define FP_DECL_D(X)		_FP_DECL(2,X)
#define FP_UNPACK_RAW_D(X,val)	_FP_UNPACK_RAW_2(D,X,val)
#define FP_UNPACK_RAW_DP(X,val)	_FP_UNPACK_RAW_2_P(D,X,val)
#define FP_PACK_RAW_D(val,X)	_FP_PACK_RAW_2(D,val,X)
#define FP_PACK_RAW_DP(val,X)		\
  do {					\
    if (!FP_INHIBIT_RESULTS)		\
      _FP_PACK_RAW_2_P(D,val,X);	\
  } while (0)

#define FP_UNPACK_D(X,val)		\
  do {					\
    _FP_UNPACK_RAW_2(D,X,val);		\
    _FP_UNPACK_CANONICAL(D,2,X);	\
  } while (0)

#define FP_UNPACK_DP(X,val)		\
  do {					\
    _FP_UNPACK_RAW_2_P(D,X,val);	\
    _FP_UNPACK_CANONICAL(D,2,X);	\
  } while (0)

#define FP_UNPACK_SEMIRAW_D(X,val)	\
  do {					\
    _FP_UNPACK_RAW_2(D,X,val);		\
    _FP_UNPACK_SEMIRAW(D,2,X);		\
  } while (0)

#define FP_UNPACK_SEMIRAW_DP(X,val)	\
  do {					\
    _FP_UNPACK_RAW_2_P(D,X,val);	\
    _FP_UNPACK_SEMIRAW(D,2,X);		\
  } while (0)

#define FP_PACK_D(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(D,2,X);		\
    _FP_PACK_RAW_2(D,val,X);		\
  } while (0)

#define FP_PACK_DP(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(D,2,X);		\
    if (!FP_INHIBIT_RESULTS)		\
      _FP_PACK_RAW_2_P(D,val,X);	\
  } while (0)

#define FP_PACK_SEMIRAW_D(val,X)	\
  do {					\
    _FP_PACK_SEMIRAW(D,2,X);		\
    _FP_PACK_RAW_2(D,val,X);		\
  } while (0)

#define FP_PACK_SEMIRAW_DP(val,X)	\
  do {					\
    _FP_PACK_SEMIRAW(D,2,X);		\
    if (!FP_INHIBIT_RESULTS)		\
      _FP_PACK_RAW_2_P(D,val,X);	\
  } while (0)

#define FP_ISSIGNAN_D(X)		_FP_ISSIGNAN(D,2,X)
#define FP_NEG_D(R,X)			_FP_NEG(D,2,R,X)
#define FP_ADD_D(R,X,Y)			_FP_ADD(D,2,R,X,Y)
#define FP_SUB_D(R,X,Y)			_FP_SUB(D,2,R,X,Y)
#define FP_MUL_D(R,X,Y)			_FP_MUL(D,2,R,X,Y)
#define FP_DIV_D(R,X,Y)			_FP_DIV(D,2,R,X,Y)
#define FP_SQRT_D(R,X)			_FP_SQRT(D,2,R,X)
#define _FP_SQRT_MEAT_D(R,S,T,X,Q)	_FP_SQRT_MEAT_2(R,S,T,X,Q)

#define FP_CMP_D(r,X,Y,un)	_FP_CMP(D,2,r,X,Y,un)
#define FP_CMP_EQ_D(r,X,Y)	_FP_CMP_EQ(D,2,r,X,Y)
#define FP_CMP_UNORD_D(r,X,Y)	_FP_CMP_UNORD(D,2,r,X,Y)

#define FP_TO_INT_D(r,X,rsz,rsg)	_FP_TO_INT(D,2,r,X,rsz,rsg)
#define FP_FROM_INT_D(X,r,rs,rt)	_FP_FROM_INT(D,2,X,r,rs,rt)

#define _FP_FRAC_HIGH_D(X)	_FP_FRAC_HIGH_2(X)
#define _FP_FRAC_HIGH_RAW_D(X)	_FP_FRAC_HIGH_2(X)

#else

union _FP_UNION_D
{
  DFtype flt;
  struct {
#if __BYTE_ORDER == __BIG_ENDIAN
    unsigned sign   : 1;
    unsigned exp    : _FP_EXPBITS_D;
    _FP_W_TYPE frac : _FP_FRACBITS_D - (_FP_IMPLBIT_D != 0);
#else
    _FP_W_TYPE frac : _FP_FRACBITS_D - (_FP_IMPLBIT_D != 0);
    unsigned exp    : _FP_EXPBITS_D;
    unsigned sign   : 1;
#endif
  } bits __attribute__((packed));
};

#define FP_DECL_D(X)		_FP_DECL(1,X)
#define FP_UNPACK_RAW_D(X,val)	_FP_UNPACK_RAW_1(D,X,val)
#define FP_UNPACK_RAW_DP(X,val)	_FP_UNPACK_RAW_1_P(D,X,val)
#define FP_PACK_RAW_D(val,X)	_FP_PACK_RAW_1(D,val,X)
#define FP_PACK_RAW_DP(val,X)		\
  do {					\
    if (!FP_INHIBIT_RESULTS)		\
      _FP_PACK_RAW_1_P(D,val,X);	\
  } while (0)

#define FP_UNPACK_D(X,val)		\
  do {					\
    _FP_UNPACK_RAW_1(D,X,val);		\
    _FP_UNPACK_CANONICAL(D,1,X);	\
  } while (0)

#define FP_UNPACK_DP(X,val)		\
  do {					\
    _FP_UNPACK_RAW_1_P(D,X,val);	\
    _FP_UNPACK_CANONICAL(D,1,X);	\
  } while (0)

#define FP_UNPACK_SEMIRAW_D(X,val)	\
  do {					\
    _FP_UNPACK_RAW_2(1,X,val);		\
    _FP_UNPACK_SEMIRAW(D,1,X);		\
  } while (0)

#define FP_UNPACK_SEMIRAW_DP(X,val)	\
  do {					\
    _FP_UNPACK_RAW_2_P(1,X,val);	\
    _FP_UNPACK_SEMIRAW(D,1,X);		\
  } while (0)

#define FP_PACK_D(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(D,1,X);		\
    _FP_PACK_RAW_1(D,val,X);		\
  } while (0)

#define FP_PACK_DP(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(D,1,X);		\
    if (!FP_INHIBIT_RESULTS)		\
      _FP_PACK_RAW_1_P(D,val,X);	\
  } while (0)

#define FP_PACK_SEMIRAW_D(val,X)	\
  do {					\
    _FP_PACK_SEMIRAW(D,1,X);		\
    _FP_PACK_RAW_1(D,val,X);		\
  } while (0)

#define FP_PACK_SEMIRAW_DP(val,X)	\
  do {					\
    _FP_PACK_SEMIRAW(D,1,X);		\
    if (!FP_INHIBIT_RESULTS)		\
      _FP_PACK_RAW_1_P(D,val,X);	\
  } while (0)

#define FP_ISSIGNAN_D(X)		_FP_ISSIGNAN(D,1,X)
#define FP_NEG_D(R,X)			_FP_NEG(D,1,R,X)
#define FP_ADD_D(R,X,Y)			_FP_ADD(D,1,R,X,Y)
#define FP_SUB_D(R,X,Y)			_FP_SUB(D,1,R,X,Y)
#define FP_MUL_D(R,X,Y)			_FP_MUL(D,1,R,X,Y)
#define FP_DIV_D(R,X,Y)			_FP_DIV(D,1,R,X,Y)
#define FP_SQRT_D(R,X)			_FP_SQRT(D,1,R,X)
#define _FP_SQRT_MEAT_D(R,S,T,X,Q)	_FP_SQRT_MEAT_1(R,S,T,X,Q)

/* The implementation of _FP_MUL_D and _FP_DIV_D should be chosen by
   the target machine.  */

#define FP_CMP_D(r,X,Y,un)	_FP_CMP(D,1,r,X,Y,un)
#define FP_CMP_EQ_D(r,X,Y)	_FP_CMP_EQ(D,1,r,X,Y)
#define FP_CMP_UNORD_D(r,X,Y)	_FP_CMP_UNORD(D,1,r,X,Y)

#define FP_TO_INT_D(r,X,rsz,rsg)	_FP_TO_INT(D,1,r,X,rsz,rsg)
#define FP_FROM_INT_D(X,r,rs,rt)	_FP_FROM_INT(D,1,X,r,rs,rt)

#define _FP_FRAC_HIGH_D(X)	_FP_FRAC_HIGH_1(X)
#define _FP_FRAC_HIGH_RAW_D(X)	_FP_FRAC_HIGH_1(X)

#endif /* W_TYPE_SIZE < 64 */
