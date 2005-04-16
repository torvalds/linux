/* Software floating-point emulation.
   Basic two-word fraction declaration and manipulation.
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

#ifndef __MATH_EMU_OP_2_H__
#define __MATH_EMU_OP_2_H__

#define _FP_FRAC_DECL_2(X)	_FP_W_TYPE X##_f0, X##_f1
#define _FP_FRAC_COPY_2(D,S)	(D##_f0 = S##_f0, D##_f1 = S##_f1)
#define _FP_FRAC_SET_2(X,I)	__FP_FRAC_SET_2(X, I)
#define _FP_FRAC_HIGH_2(X)	(X##_f1)
#define _FP_FRAC_LOW_2(X)	(X##_f0)
#define _FP_FRAC_WORD_2(X,w)	(X##_f##w)

#define _FP_FRAC_SLL_2(X,N)						\
  do {									\
    if ((N) < _FP_W_TYPE_SIZE)						\
      {									\
	if (__builtin_constant_p(N) && (N) == 1) 			\
	  {								\
	    X##_f1 = X##_f1 + X##_f1 + (((_FP_WS_TYPE)(X##_f0)) < 0);	\
	    X##_f0 += X##_f0;						\
	  }								\
	else								\
	  {								\
	    X##_f1 = X##_f1 << (N) | X##_f0 >> (_FP_W_TYPE_SIZE - (N));	\
	    X##_f0 <<= (N);						\
	  }								\
      }									\
    else								\
      {									\
	X##_f1 = X##_f0 << ((N) - _FP_W_TYPE_SIZE);			\
	X##_f0 = 0;							\
      }									\
  } while (0)

#define _FP_FRAC_SRL_2(X,N)						\
  do {									\
    if ((N) < _FP_W_TYPE_SIZE)						\
      {									\
	X##_f0 = X##_f0 >> (N) | X##_f1 << (_FP_W_TYPE_SIZE - (N));	\
	X##_f1 >>= (N);							\
      }									\
    else								\
      {									\
	X##_f0 = X##_f1 >> ((N) - _FP_W_TYPE_SIZE);			\
	X##_f1 = 0;							\
      }									\
  } while (0)

/* Right shift with sticky-lsb.  */
#define _FP_FRAC_SRS_2(X,N,sz)						\
  do {									\
    if ((N) < _FP_W_TYPE_SIZE)						\
      {									\
	X##_f0 = (X##_f1 << (_FP_W_TYPE_SIZE - (N)) | X##_f0 >> (N) |	\
		  (__builtin_constant_p(N) && (N) == 1			\
		   ? X##_f0 & 1						\
		   : (X##_f0 << (_FP_W_TYPE_SIZE - (N))) != 0));	\
	X##_f1 >>= (N);							\
      }									\
    else								\
      {									\
	X##_f0 = (X##_f1 >> ((N) - _FP_W_TYPE_SIZE) |			\
		(((X##_f1 << (2*_FP_W_TYPE_SIZE - (N))) | X##_f0) != 0)); \
	X##_f1 = 0;							\
      }									\
  } while (0)

#define _FP_FRAC_ADDI_2(X,I)	\
  __FP_FRAC_ADDI_2(X##_f1, X##_f0, I)

#define _FP_FRAC_ADD_2(R,X,Y)	\
  __FP_FRAC_ADD_2(R##_f1, R##_f0, X##_f1, X##_f0, Y##_f1, Y##_f0)

#define _FP_FRAC_SUB_2(R,X,Y)	\
  __FP_FRAC_SUB_2(R##_f1, R##_f0, X##_f1, X##_f0, Y##_f1, Y##_f0)

#define _FP_FRAC_DEC_2(X,Y)	\
  __FP_FRAC_DEC_2(X##_f1, X##_f0, Y##_f1, Y##_f0)

#define _FP_FRAC_CLZ_2(R,X)	\
  do {				\
    if (X##_f1)			\
      __FP_CLZ(R,X##_f1);	\
    else 			\
    {				\
      __FP_CLZ(R,X##_f0);	\
      R += _FP_W_TYPE_SIZE;	\
    }				\
  } while(0)

/* Predicates */
#define _FP_FRAC_NEGP_2(X)	((_FP_WS_TYPE)X##_f1 < 0)
#define _FP_FRAC_ZEROP_2(X)	((X##_f1 | X##_f0) == 0)
#define _FP_FRAC_OVERP_2(fs,X)	(_FP_FRAC_HIGH_##fs(X) & _FP_OVERFLOW_##fs)
#define _FP_FRAC_CLEAR_OVERP_2(fs,X)	(_FP_FRAC_HIGH_##fs(X) &= ~_FP_OVERFLOW_##fs)
#define _FP_FRAC_EQ_2(X, Y)	(X##_f1 == Y##_f1 && X##_f0 == Y##_f0)
#define _FP_FRAC_GT_2(X, Y)	\
  (X##_f1 > Y##_f1 || (X##_f1 == Y##_f1 && X##_f0 > Y##_f0))
#define _FP_FRAC_GE_2(X, Y)	\
  (X##_f1 > Y##_f1 || (X##_f1 == Y##_f1 && X##_f0 >= Y##_f0))

#define _FP_ZEROFRAC_2		0, 0
#define _FP_MINFRAC_2		0, 1
#define _FP_MAXFRAC_2		(~(_FP_WS_TYPE)0), (~(_FP_WS_TYPE)0)

/*
 * Internals 
 */

#define __FP_FRAC_SET_2(X,I1,I0)	(X##_f0 = I0, X##_f1 = I1)

#define __FP_CLZ_2(R, xh, xl)	\
  do {				\
    if (xh)			\
      __FP_CLZ(R,xh);		\
    else 			\
    {				\
      __FP_CLZ(R,xl);		\
      R += _FP_W_TYPE_SIZE;	\
    }				\
  } while(0)

#if 0

#ifndef __FP_FRAC_ADDI_2
#define __FP_FRAC_ADDI_2(xh, xl, i)	\
  (xh += ((xl += i) < i))
#endif
#ifndef __FP_FRAC_ADD_2
#define __FP_FRAC_ADD_2(rh, rl, xh, xl, yh, yl)	\
  (rh = xh + yh + ((rl = xl + yl) < xl))
#endif
#ifndef __FP_FRAC_SUB_2
#define __FP_FRAC_SUB_2(rh, rl, xh, xl, yh, yl)	\
  (rh = xh - yh - ((rl = xl - yl) > xl))
#endif
#ifndef __FP_FRAC_DEC_2
#define __FP_FRAC_DEC_2(xh, xl, yh, yl)	\
  do {					\
    UWtype _t = xl;			\
    xh -= yh + ((xl -= yl) > _t);	\
  } while (0)
#endif

#else

#undef __FP_FRAC_ADDI_2
#define __FP_FRAC_ADDI_2(xh, xl, i)	add_ssaaaa(xh, xl, xh, xl, 0, i)
#undef __FP_FRAC_ADD_2
#define __FP_FRAC_ADD_2			add_ssaaaa
#undef __FP_FRAC_SUB_2
#define __FP_FRAC_SUB_2			sub_ddmmss
#undef __FP_FRAC_DEC_2
#define __FP_FRAC_DEC_2(xh, xl, yh, yl)	sub_ddmmss(xh, xl, xh, xl, yh, yl)

#endif

/*
 * Unpack the raw bits of a native fp value.  Do not classify or
 * normalize the data.
 */

#define _FP_UNPACK_RAW_2(fs, X, val)			\
  do {							\
    union _FP_UNION_##fs _flo; _flo.flt = (val);	\
							\
    X##_f0 = _flo.bits.frac0;				\
    X##_f1 = _flo.bits.frac1;				\
    X##_e  = _flo.bits.exp;				\
    X##_s  = _flo.bits.sign;				\
  } while (0)

#define _FP_UNPACK_RAW_2_P(fs, X, val)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
      (union _FP_UNION_##fs *)(val);			\
							\
    X##_f0 = _flo->bits.frac0;				\
    X##_f1 = _flo->bits.frac1;				\
    X##_e  = _flo->bits.exp;				\
    X##_s  = _flo->bits.sign;				\
  } while (0)


/*
 * Repack the raw bits of a native fp value.
 */

#define _FP_PACK_RAW_2(fs, val, X)			\
  do {							\
    union _FP_UNION_##fs _flo;				\
							\
    _flo.bits.frac0 = X##_f0;				\
    _flo.bits.frac1 = X##_f1;				\
    _flo.bits.exp   = X##_e;				\
    _flo.bits.sign  = X##_s;				\
							\
    (val) = _flo.flt;					\
  } while (0)

#define _FP_PACK_RAW_2_P(fs, val, X)			\
  do {							\
    union _FP_UNION_##fs *_flo =			\
      (union _FP_UNION_##fs *)(val);			\
							\
    _flo->bits.frac0 = X##_f0;				\
    _flo->bits.frac1 = X##_f1;				\
    _flo->bits.exp   = X##_e;				\
    _flo->bits.sign  = X##_s;				\
  } while (0)


/*
 * Multiplication algorithms:
 */

/* Given a 1W * 1W => 2W primitive, do the extended multiplication.  */

#define _FP_MUL_MEAT_2_wide(wfracbits, R, X, Y, doit)			\
  do {									\
    _FP_FRAC_DECL_4(_z); _FP_FRAC_DECL_2(_b); _FP_FRAC_DECL_2(_c);	\
									\
    doit(_FP_FRAC_WORD_4(_z,1), _FP_FRAC_WORD_4(_z,0), X##_f0, Y##_f0);	\
    doit(_b_f1, _b_f0, X##_f0, Y##_f1);					\
    doit(_c_f1, _c_f0, X##_f1, Y##_f0);					\
    doit(_FP_FRAC_WORD_4(_z,3), _FP_FRAC_WORD_4(_z,2), X##_f1, Y##_f1);	\
									\
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1), 0, _b_f1, _b_f0,		\
		    _FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1));				\
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1), 0, _c_f1, _c_f0,		\
		    _FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1));				\
									\
    /* Normalize since we know where the msb of the multiplicands	\
       were (bit B), we know that the msb of the of the product is	\
       at either 2B or 2B-1.  */					\
    _FP_FRAC_SRS_4(_z, wfracbits-1, 2*wfracbits);			\
    R##_f0 = _FP_FRAC_WORD_4(_z,0);					\
    R##_f1 = _FP_FRAC_WORD_4(_z,1);					\
  } while (0)

/* Given a 1W * 1W => 2W primitive, do the extended multiplication.
   Do only 3 multiplications instead of four. This one is for machines
   where multiplication is much more expensive than subtraction.  */

#define _FP_MUL_MEAT_2_wide_3mul(wfracbits, R, X, Y, doit)		\
  do {									\
    _FP_FRAC_DECL_4(_z); _FP_FRAC_DECL_2(_b); _FP_FRAC_DECL_2(_c);	\
    _FP_W_TYPE _d;							\
    int _c1, _c2;							\
									\
    _b_f0 = X##_f0 + X##_f1;						\
    _c1 = _b_f0 < X##_f0;						\
    _b_f1 = Y##_f0 + Y##_f1;						\
    _c2 = _b_f1 < Y##_f0;						\
    doit(_d, _FP_FRAC_WORD_4(_z,0), X##_f0, Y##_f0);			\
    doit(_FP_FRAC_WORD_4(_z,2), _FP_FRAC_WORD_4(_z,1), _b_f0, _b_f1);	\
    doit(_c_f1, _c_f0, X##_f1, Y##_f1);					\
									\
    _b_f0 &= -_c2;							\
    _b_f1 &= -_c1;							\
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1), (_c1 & _c2), 0, _d,		\
		    0, _FP_FRAC_WORD_4(_z,2), _FP_FRAC_WORD_4(_z,1));	\
    __FP_FRAC_ADDI_2(_FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		     _b_f0);						\
    __FP_FRAC_ADDI_2(_FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		     _b_f1);						\
    __FP_FRAC_DEC_3(_FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1),				\
		    0, _d, _FP_FRAC_WORD_4(_z,0));			\
    __FP_FRAC_DEC_3(_FP_FRAC_WORD_4(_z,3),_FP_FRAC_WORD_4(_z,2),	\
		    _FP_FRAC_WORD_4(_z,1), 0, _c_f1, _c_f0);		\
    __FP_FRAC_ADD_2(_FP_FRAC_WORD_4(_z,3), _FP_FRAC_WORD_4(_z,2),	\
		    _c_f1, _c_f0,					\
		    _FP_FRAC_WORD_4(_z,3), _FP_FRAC_WORD_4(_z,2));	\
									\
    /* Normalize since we know where the msb of the multiplicands	\
       were (bit B), we know that the msb of the of the product is	\
       at either 2B or 2B-1.  */					\
    _FP_FRAC_SRS_4(_z, wfracbits-1, 2*wfracbits);			\
    R##_f0 = _FP_FRAC_WORD_4(_z,0);					\
    R##_f1 = _FP_FRAC_WORD_4(_z,1);					\
  } while (0)

#define _FP_MUL_MEAT_2_gmp(wfracbits, R, X, Y)				\
  do {									\
    _FP_FRAC_DECL_4(_z);						\
    _FP_W_TYPE _x[2], _y[2];						\
    _x[0] = X##_f0; _x[1] = X##_f1;					\
    _y[0] = Y##_f0; _y[1] = Y##_f1;					\
									\
    mpn_mul_n(_z_f, _x, _y, 2);						\
									\
    /* Normalize since we know where the msb of the multiplicands	\
       were (bit B), we know that the msb of the of the product is	\
       at either 2B or 2B-1.  */					\
    _FP_FRAC_SRS_4(_z, wfracbits-1, 2*wfracbits);			\
    R##_f0 = _z_f[0];							\
    R##_f1 = _z_f[1];							\
  } while (0)

/* Do at most 120x120=240 bits multiplication using double floating
   point multiplication.  This is useful if floating point
   multiplication has much bigger throughput than integer multiply.
   It is supposed to work for _FP_W_TYPE_SIZE 64 and wfracbits
   between 106 and 120 only.  
   Caller guarantees that X and Y has (1LLL << (wfracbits - 1)) set.
   SETFETZ is a macro which will disable all FPU exceptions and set rounding
   towards zero,  RESETFE should optionally reset it back.  */

#define _FP_MUL_MEAT_2_120_240_double(wfracbits, R, X, Y, setfetz, resetfe)	\
  do {										\
    static const double _const[] = {						\
      /* 2^-24 */ 5.9604644775390625e-08,					\
      /* 2^-48 */ 3.5527136788005009e-15,					\
      /* 2^-72 */ 2.1175823681357508e-22,					\
      /* 2^-96 */ 1.2621774483536189e-29,					\
      /* 2^28 */ 2.68435456e+08,						\
      /* 2^4 */ 1.600000e+01,							\
      /* 2^-20 */ 9.5367431640625e-07,						\
      /* 2^-44 */ 5.6843418860808015e-14,					\
      /* 2^-68 */ 3.3881317890172014e-21,					\
      /* 2^-92 */ 2.0194839173657902e-28,					\
      /* 2^-116 */ 1.2037062152420224e-35};					\
    double _a240, _b240, _c240, _d240, _e240, _f240, 				\
	   _g240, _h240, _i240, _j240, _k240;					\
    union { double d; UDItype i; } _l240, _m240, _n240, _o240,			\
				   _p240, _q240, _r240, _s240;			\
    UDItype _t240, _u240, _v240, _w240, _x240, _y240 = 0;			\
										\
    if (wfracbits < 106 || wfracbits > 120)					\
      abort();									\
										\
    setfetz;									\
										\
    _e240 = (double)(long)(X##_f0 & 0xffffff);					\
    _j240 = (double)(long)(Y##_f0 & 0xffffff);					\
    _d240 = (double)(long)((X##_f0 >> 24) & 0xffffff);				\
    _i240 = (double)(long)((Y##_f0 >> 24) & 0xffffff);				\
    _c240 = (double)(long)(((X##_f1 << 16) & 0xffffff) | (X##_f0 >> 48));	\
    _h240 = (double)(long)(((Y##_f1 << 16) & 0xffffff) | (Y##_f0 >> 48));	\
    _b240 = (double)(long)((X##_f1 >> 8) & 0xffffff);				\
    _g240 = (double)(long)((Y##_f1 >> 8) & 0xffffff);				\
    _a240 = (double)(long)(X##_f1 >> 32);					\
    _f240 = (double)(long)(Y##_f1 >> 32);					\
    _e240 *= _const[3];								\
    _j240 *= _const[3];								\
    _d240 *= _const[2];								\
    _i240 *= _const[2];								\
    _c240 *= _const[1];								\
    _h240 *= _const[1];								\
    _b240 *= _const[0];								\
    _g240 *= _const[0];								\
    _s240.d =							      _e240*_j240;\
    _r240.d =						_d240*_j240 + _e240*_i240;\
    _q240.d =				  _c240*_j240 + _d240*_i240 + _e240*_h240;\
    _p240.d =		    _b240*_j240 + _c240*_i240 + _d240*_h240 + _e240*_g240;\
    _o240.d = _a240*_j240 + _b240*_i240 + _c240*_h240 + _d240*_g240 + _e240*_f240;\
    _n240.d = _a240*_i240 + _b240*_h240 + _c240*_g240 + _d240*_f240;		\
    _m240.d = _a240*_h240 + _b240*_g240 + _c240*_f240;				\
    _l240.d = _a240*_g240 + _b240*_f240;					\
    _k240 =   _a240*_f240;							\
    _r240.d += _s240.d;								\
    _q240.d += _r240.d;								\
    _p240.d += _q240.d;								\
    _o240.d += _p240.d;								\
    _n240.d += _o240.d;								\
    _m240.d += _n240.d;								\
    _l240.d += _m240.d;								\
    _k240 += _l240.d;								\
    _s240.d -= ((_const[10]+_s240.d)-_const[10]);				\
    _r240.d -= ((_const[9]+_r240.d)-_const[9]);					\
    _q240.d -= ((_const[8]+_q240.d)-_const[8]);					\
    _p240.d -= ((_const[7]+_p240.d)-_const[7]);					\
    _o240.d += _const[7];							\
    _n240.d += _const[6];							\
    _m240.d += _const[5];							\
    _l240.d += _const[4];							\
    if (_s240.d != 0.0) _y240 = 1;						\
    if (_r240.d != 0.0) _y240 = 1;						\
    if (_q240.d != 0.0) _y240 = 1;						\
    if (_p240.d != 0.0) _y240 = 1;						\
    _t240 = (DItype)_k240;							\
    _u240 = _l240.i;								\
    _v240 = _m240.i;								\
    _w240 = _n240.i;								\
    _x240 = _o240.i;								\
    R##_f1 = (_t240 << (128 - (wfracbits - 1)))					\
	     | ((_u240 & 0xffffff) >> ((wfracbits - 1) - 104));			\
    R##_f0 = ((_u240 & 0xffffff) << (168 - (wfracbits - 1)))			\
    	     | ((_v240 & 0xffffff) << (144 - (wfracbits - 1)))			\
    	     | ((_w240 & 0xffffff) << (120 - (wfracbits - 1)))			\
    	     | ((_x240 & 0xffffff) >> ((wfracbits - 1) - 96))			\
    	     | _y240;								\
    resetfe;									\
  } while (0)

/*
 * Division algorithms:
 */

#define _FP_DIV_MEAT_2_udiv(fs, R, X, Y)				\
  do {									\
    _FP_W_TYPE _n_f2, _n_f1, _n_f0, _r_f1, _r_f0, _m_f1, _m_f0;		\
    if (_FP_FRAC_GT_2(X, Y))						\
      {									\
	_n_f2 = X##_f1 >> 1;						\
	_n_f1 = X##_f1 << (_FP_W_TYPE_SIZE - 1) | X##_f0 >> 1;		\
	_n_f0 = X##_f0 << (_FP_W_TYPE_SIZE - 1);			\
      }									\
    else								\
      {									\
	R##_e--;							\
	_n_f2 = X##_f1;							\
	_n_f1 = X##_f0;							\
	_n_f0 = 0;							\
      }									\
									\
    /* Normalize, i.e. make the most significant bit of the 		\
       denominator set. */						\
    _FP_FRAC_SLL_2(Y, _FP_WFRACXBITS_##fs);				\
									\
    udiv_qrnnd(R##_f1, _r_f1, _n_f2, _n_f1, Y##_f1);			\
    umul_ppmm(_m_f1, _m_f0, R##_f1, Y##_f0);				\
    _r_f0 = _n_f0;							\
    if (_FP_FRAC_GT_2(_m, _r))						\
      {									\
	R##_f1--;							\
	_FP_FRAC_ADD_2(_r, Y, _r);					\
	if (_FP_FRAC_GE_2(_r, Y) && _FP_FRAC_GT_2(_m, _r))		\
	  {								\
	    R##_f1--;							\
	    _FP_FRAC_ADD_2(_r, Y, _r);					\
	  }								\
      }									\
    _FP_FRAC_DEC_2(_r, _m);						\
									\
    if (_r_f1 == Y##_f1)						\
      {									\
	/* This is a special case, not an optimization			\
	   (_r/Y##_f1 would not fit into UWtype).			\
	   As _r is guaranteed to be < Y,  R##_f0 can be either		\
	   (UWtype)-1 or (UWtype)-2.  But as we know what kind		\
	   of bits it is (sticky, guard, round),  we don't care.	\
	   We also don't care what the reminder is,  because the	\
	   guard bit will be set anyway.  -jj */			\
	R##_f0 = -1;							\
      }									\
    else								\
      {									\
	udiv_qrnnd(R##_f0, _r_f1, _r_f1, _r_f0, Y##_f1);		\
	umul_ppmm(_m_f1, _m_f0, R##_f0, Y##_f0);			\
	_r_f0 = 0;							\
	if (_FP_FRAC_GT_2(_m, _r))					\
	  {								\
	    R##_f0--;							\
	    _FP_FRAC_ADD_2(_r, Y, _r);					\
	    if (_FP_FRAC_GE_2(_r, Y) && _FP_FRAC_GT_2(_m, _r))		\
	      {								\
		R##_f0--;						\
		_FP_FRAC_ADD_2(_r, Y, _r);				\
	      }								\
	  }								\
	if (!_FP_FRAC_EQ_2(_r, _m))					\
	  R##_f0 |= _FP_WORK_STICKY;					\
      }									\
  } while (0)


#define _FP_DIV_MEAT_2_gmp(fs, R, X, Y)					\
  do {									\
    _FP_W_TYPE _x[4], _y[2], _z[4];					\
    _y[0] = Y##_f0; _y[1] = Y##_f1;					\
    _x[0] = _x[3] = 0;							\
    if (_FP_FRAC_GT_2(X, Y))						\
      {									\
	R##_e++;							\
	_x[1] = (X##_f0 << (_FP_WFRACBITS_##fs-1 - _FP_W_TYPE_SIZE) |	\
		 X##_f1 >> (_FP_W_TYPE_SIZE -				\
			    (_FP_WFRACBITS_##fs-1 - _FP_W_TYPE_SIZE)));	\
	_x[2] = X##_f1 << (_FP_WFRACBITS_##fs-1 - _FP_W_TYPE_SIZE);	\
      }									\
    else								\
      {									\
	_x[1] = (X##_f0 << (_FP_WFRACBITS_##fs - _FP_W_TYPE_SIZE) |	\
		 X##_f1 >> (_FP_W_TYPE_SIZE -				\
			    (_FP_WFRACBITS_##fs - _FP_W_TYPE_SIZE)));	\
	_x[2] = X##_f1 << (_FP_WFRACBITS_##fs - _FP_W_TYPE_SIZE);	\
      }									\
									\
    (void) mpn_divrem (_z, 0, _x, 4, _y, 2);				\
    R##_f1 = _z[1];							\
    R##_f0 = _z[0] | ((_x[0] | _x[1]) != 0);				\
  } while (0)


/*
 * Square root algorithms:
 * We have just one right now, maybe Newton approximation
 * should be added for those machines where division is fast.
 */
 
#define _FP_SQRT_MEAT_2(R, S, T, X, q)			\
  do {							\
    while (q)						\
      {							\
	T##_f1 = S##_f1 + q;				\
	if (T##_f1 <= X##_f1)				\
	  {						\
	    S##_f1 = T##_f1 + q;			\
	    X##_f1 -= T##_f1;				\
	    R##_f1 += q;				\
	  }						\
	_FP_FRAC_SLL_2(X, 1);				\
	q >>= 1;					\
      }							\
    q = (_FP_W_TYPE)1 << (_FP_W_TYPE_SIZE - 1);		\
    while (q != _FP_WORK_ROUND)				\
      {							\
	T##_f0 = S##_f0 + q;				\
	T##_f1 = S##_f1;				\
	if (T##_f1 < X##_f1 || 				\
	    (T##_f1 == X##_f1 && T##_f0 <= X##_f0))	\
	  {						\
	    S##_f0 = T##_f0 + q;			\
	    S##_f1 += (T##_f0 > S##_f0);		\
	    _FP_FRAC_DEC_2(X, T);			\
	    R##_f0 += q;				\
	  }						\
	_FP_FRAC_SLL_2(X, 1);				\
	q >>= 1;					\
      }							\
    if (X##_f0 | X##_f1)				\
      {							\
	if (S##_f1 < X##_f1 || 				\
	    (S##_f1 == X##_f1 && S##_f0 < X##_f0))	\
	  R##_f0 |= _FP_WORK_ROUND;			\
	R##_f0 |= _FP_WORK_STICKY;			\
      }							\
  } while (0)


/*
 * Assembly/disassembly for converting to/from integral types.  
 * No shifting or overflow handled here.
 */

#define _FP_FRAC_ASSEMBLE_2(r, X, rsize)	\
  do {						\
    if (rsize <= _FP_W_TYPE_SIZE)		\
      r = X##_f0;				\
    else					\
      {						\
	r = X##_f1;				\
	r <<= _FP_W_TYPE_SIZE;			\
	r += X##_f0;				\
      }						\
  } while (0)

#define _FP_FRAC_DISASSEMBLE_2(X, r, rsize)				\
  do {									\
    X##_f0 = r;								\
    X##_f1 = (rsize <= _FP_W_TYPE_SIZE ? 0 : r >> _FP_W_TYPE_SIZE);	\
  } while (0)

/*
 * Convert FP values between word sizes
 */

#define _FP_FRAC_CONV_1_2(dfs, sfs, D, S)				\
  do {									\
    if (S##_c != FP_CLS_NAN)						\
      _FP_FRAC_SRS_2(S, (_FP_WFRACBITS_##sfs - _FP_WFRACBITS_##dfs),	\
		     _FP_WFRACBITS_##sfs);				\
    else								\
      _FP_FRAC_SRL_2(S, (_FP_WFRACBITS_##sfs - _FP_WFRACBITS_##dfs));	\
    D##_f = S##_f0;							\
  } while (0)

#define _FP_FRAC_CONV_2_1(dfs, sfs, D, S)				\
  do {									\
    D##_f0 = S##_f;							\
    D##_f1 = 0;								\
    _FP_FRAC_SLL_2(D, (_FP_WFRACBITS_##dfs - _FP_WFRACBITS_##sfs));	\
  } while (0)

#endif
