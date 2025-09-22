/* Software floating-point emulation.
   Definitions for IEEE Extended Precision.
   Copyright (C) 1999,2006,2007 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Jakub Jelinek (jj@ultra.linux.cz).

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
#error "Here's a nickel, kid. Go buy yourself a real computer."
#endif

#if _FP_W_TYPE_SIZE < 64
#define _FP_FRACTBITS_E         (4*_FP_W_TYPE_SIZE)
#else
#define _FP_FRACTBITS_E		(2*_FP_W_TYPE_SIZE)
#endif

#define _FP_FRACBITS_E		64
#define _FP_FRACXBITS_E		(_FP_FRACTBITS_E - _FP_FRACBITS_E)
#define _FP_WFRACBITS_E		(_FP_WORKBITS + _FP_FRACBITS_E)
#define _FP_WFRACXBITS_E	(_FP_FRACTBITS_E - _FP_WFRACBITS_E)
#define _FP_EXPBITS_E		15
#define _FP_EXPBIAS_E		16383
#define _FP_EXPMAX_E		32767

#define _FP_QNANBIT_E		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_E-2) % _FP_W_TYPE_SIZE)
#define _FP_QNANBIT_SH_E		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_E-2+_FP_WORKBITS) % _FP_W_TYPE_SIZE)
#define _FP_IMPLBIT_E		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_E-1) % _FP_W_TYPE_SIZE)
#define _FP_IMPLBIT_SH_E		\
	((_FP_W_TYPE)1 << (_FP_FRACBITS_E-1+_FP_WORKBITS) % _FP_W_TYPE_SIZE)
#define _FP_OVERFLOW_E		\
	((_FP_W_TYPE)1 << (_FP_WFRACBITS_E % _FP_W_TYPE_SIZE))

typedef float XFtype __attribute__((mode(XF)));

#if _FP_W_TYPE_SIZE < 64

union _FP_UNION_E
{
   XFtype flt;
   struct 
   {
#if __BYTE_ORDER == __BIG_ENDIAN
      unsigned long pad1 : _FP_W_TYPE_SIZE;
      unsigned long pad2 : (_FP_W_TYPE_SIZE - 1 - _FP_EXPBITS_E);
      unsigned long sign : 1;
      unsigned long exp : _FP_EXPBITS_E;
      unsigned long frac1 : _FP_W_TYPE_SIZE;
      unsigned long frac0 : _FP_W_TYPE_SIZE;
#else
      unsigned long frac0 : _FP_W_TYPE_SIZE;
      unsigned long frac1 : _FP_W_TYPE_SIZE;
      unsigned exp : _FP_EXPBITS_E;
      unsigned sign : 1;
#endif /* not bigendian */
   } bits __attribute__((packed));
};


#define FP_DECL_E(X)		_FP_DECL(4,X)

#define FP_UNPACK_RAW_E(X, val)				\
  do {							\
    union _FP_UNION_E _flo; _flo.flt = (val);		\
							\
    X##_f[2] = 0; X##_f[3] = 0;				\
    X##_f[0] = _flo.bits.frac0;				\
    X##_f[1] = _flo.bits.frac1;				\
    X##_e  = _flo.bits.exp;				\
    X##_s  = _flo.bits.sign;				\
  } while (0)

#define FP_UNPACK_RAW_EP(X, val)			\
  do {							\
    union _FP_UNION_E *_flo =				\
    (union _FP_UNION_E *)(val);				\
							\
    X##_f[2] = 0; X##_f[3] = 0;				\
    X##_f[0] = _flo->bits.frac0;			\
    X##_f[1] = _flo->bits.frac1;			\
    X##_e  = _flo->bits.exp;				\
    X##_s  = _flo->bits.sign;				\
  } while (0)

#define FP_PACK_RAW_E(val, X)				\
  do {							\
    union _FP_UNION_E _flo;				\
							\
    if (X##_e) X##_f[1] |= _FP_IMPLBIT_E;		\
    else X##_f[1] &= ~(_FP_IMPLBIT_E);			\
    _flo.bits.frac0 = X##_f[0];				\
    _flo.bits.frac1 = X##_f[1];				\
    _flo.bits.exp   = X##_e;				\
    _flo.bits.sign  = X##_s;				\
							\
    (val) = _flo.flt;					\
  } while (0)

#define FP_PACK_RAW_EP(val, X)				\
  do {							\
    if (!FP_INHIBIT_RESULTS)				\
      {							\
	union _FP_UNION_E *_flo =			\
	  (union _FP_UNION_E *)(val);			\
							\
	if (X##_e) X##_f[1] |= _FP_IMPLBIT_E;		\
	else X##_f[1] &= ~(_FP_IMPLBIT_E);		\
	_flo->bits.frac0 = X##_f[0];			\
	_flo->bits.frac1 = X##_f[1];			\
	_flo->bits.exp   = X##_e;			\
	_flo->bits.sign  = X##_s;			\
      }							\
  } while (0)

#define FP_UNPACK_E(X,val)		\
  do {					\
    FP_UNPACK_RAW_E(X,val);		\
    _FP_UNPACK_CANONICAL(E,4,X);	\
  } while (0)

#define FP_UNPACK_EP(X,val)		\
  do {					\
    FP_UNPACK_RAW_EP(X,val);		\
    _FP_UNPACK_CANONICAL(E,4,X);	\
  } while (0)

#define FP_UNPACK_SEMIRAW_E(X,val)	\
  do {					\
    FP_UNPACK_RAW_E(X,val);		\
    _FP_UNPACK_SEMIRAW(E,4,X);		\
  } while (0)

#define FP_UNPACK_SEMIRAW_EP(X,val)	\
  do {					\
    FP_UNPACK_RAW_EP(X,val);		\
    _FP_UNPACK_SEMIRAW(E,4,X);		\
  } while (0)

#define FP_PACK_E(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(E,4,X);		\
    FP_PACK_RAW_E(val,X);		\
  } while (0)

#define FP_PACK_EP(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(E,4,X);		\
    FP_PACK_RAW_EP(val,X);		\
  } while (0)

#define FP_PACK_SEMIRAW_E(val,X)	\
  do {					\
    _FP_PACK_SEMIRAW(E,4,X);		\
    FP_PACK_RAW_E(val,X);		\
  } while (0)

#define FP_PACK_SEMIRAW_EP(val,X)	\
  do {					\
    _FP_PACK_SEMIRAW(E,4,X);		\
    FP_PACK_RAW_EP(val,X);		\
  } while (0)

#define FP_ISSIGNAN_E(X)	_FP_ISSIGNAN(E,4,X)
#define FP_NEG_E(R,X)		_FP_NEG(E,4,R,X)
#define FP_ADD_E(R,X,Y)		_FP_ADD(E,4,R,X,Y)
#define FP_SUB_E(R,X,Y)		_FP_SUB(E,4,R,X,Y)
#define FP_MUL_E(R,X,Y)		_FP_MUL(E,4,R,X,Y)
#define FP_DIV_E(R,X,Y)		_FP_DIV(E,4,R,X,Y)
#define FP_SQRT_E(R,X)		_FP_SQRT(E,4,R,X)

/*
 * Square root algorithms:
 * We have just one right now, maybe Newton approximation
 * should be added for those machines where division is fast.
 * This has special _E version because standard _4 square
 * root would not work (it has to start normally with the
 * second word and not the first), but as we have to do it
 * anyway, we optimize it by doing most of the calculations
 * in two UWtype registers instead of four.
 */
 
#define _FP_SQRT_MEAT_E(R, S, T, X, q)			\
  do {							\
    q = (_FP_W_TYPE)1 << (_FP_W_TYPE_SIZE - 1);		\
    _FP_FRAC_SRL_4(X, (_FP_WORKBITS));			\
    while (q)						\
      {							\
	T##_f[1] = S##_f[1] + q;			\
	if (T##_f[1] <= X##_f[1])			\
	  {						\
	    S##_f[1] = T##_f[1] + q;			\
	    X##_f[1] -= T##_f[1];			\
	    R##_f[1] += q;				\
	  }						\
	_FP_FRAC_SLL_2(X, 1);				\
	q >>= 1;					\
      }							\
    q = (_FP_W_TYPE)1 << (_FP_W_TYPE_SIZE - 1);		\
    while (q)						\
      {							\
	T##_f[0] = S##_f[0] + q;			\
	T##_f[1] = S##_f[1];				\
	if (T##_f[1] < X##_f[1] || 			\
	    (T##_f[1] == X##_f[1] &&			\
	     T##_f[0] <= X##_f[0]))			\
	  {						\
	    S##_f[0] = T##_f[0] + q;			\
	    S##_f[1] += (T##_f[0] > S##_f[0]);		\
	    _FP_FRAC_DEC_2(X, T);			\
	    R##_f[0] += q;				\
	  }						\
	_FP_FRAC_SLL_2(X, 1);				\
	q >>= 1;					\
      }							\
    _FP_FRAC_SLL_4(R, (_FP_WORKBITS));			\
    if (X##_f[0] | X##_f[1])				\
      {							\
	if (S##_f[1] < X##_f[1] || 			\
	    (S##_f[1] == X##_f[1] &&			\
	     S##_f[0] < X##_f[0]))			\
	  R##_f[0] |= _FP_WORK_ROUND;			\
	R##_f[0] |= _FP_WORK_STICKY;			\
      }							\
  } while (0)

#define FP_CMP_E(r,X,Y,un)	_FP_CMP(E,4,r,X,Y,un)
#define FP_CMP_EQ_E(r,X,Y)	_FP_CMP_EQ(E,4,r,X,Y)
#define FP_CMP_UNORD_E(r,X,Y)	_FP_CMP_UNORD(E,4,r,X,Y)

#define FP_TO_INT_E(r,X,rsz,rsg)	_FP_TO_INT(E,4,r,X,rsz,rsg)
#define FP_FROM_INT_E(X,r,rs,rt)	_FP_FROM_INT(E,4,X,r,rs,rt)

#define _FP_FRAC_HIGH_E(X)	(X##_f[2])
#define _FP_FRAC_HIGH_RAW_E(X)	(X##_f[1])

#else   /* not _FP_W_TYPE_SIZE < 64 */
union _FP_UNION_E
{
  XFtype flt;
  struct {
#if __BYTE_ORDER == __BIG_ENDIAN
    _FP_W_TYPE pad  : (_FP_W_TYPE_SIZE - 1 - _FP_EXPBITS_E);
    unsigned sign   : 1;
    unsigned exp    : _FP_EXPBITS_E;
    _FP_W_TYPE frac : _FP_W_TYPE_SIZE;
#else
    _FP_W_TYPE frac : _FP_W_TYPE_SIZE;
    unsigned exp    : _FP_EXPBITS_E;
    unsigned sign   : 1;
#endif
  } bits;
};

#define FP_DECL_E(X)		_FP_DECL(2,X)

#define FP_UNPACK_RAW_E(X, val)					\
  do {								\
    union _FP_UNION_E _flo; _flo.flt = (val);			\
								\
    X##_f0 = _flo.bits.frac;					\
    X##_f1 = 0;							\
    X##_e = _flo.bits.exp;					\
    X##_s = _flo.bits.sign;					\
  } while (0)

#define FP_UNPACK_RAW_EP(X, val)				\
  do {								\
    union _FP_UNION_E *_flo =					\
      (union _FP_UNION_E *)(val);				\
								\
    X##_f0 = _flo->bits.frac;					\
    X##_f1 = 0;							\
    X##_e = _flo->bits.exp;					\
    X##_s = _flo->bits.sign;					\
  } while (0)

#define FP_PACK_RAW_E(val, X)					\
  do {								\
    union _FP_UNION_E _flo;					\
								\
    if (X##_e) X##_f0 |= _FP_IMPLBIT_E;				\
    else X##_f0 &= ~(_FP_IMPLBIT_E);				\
    _flo.bits.frac = X##_f0;					\
    _flo.bits.exp  = X##_e;					\
    _flo.bits.sign = X##_s;					\
								\
    (val) = _flo.flt;						\
  } while (0)

#define FP_PACK_RAW_EP(fs, val, X)				\
  do {								\
    if (!FP_INHIBIT_RESULTS)					\
      {								\
	union _FP_UNION_E *_flo =				\
	  (union _FP_UNION_E *)(val);				\
								\
	if (X##_e) X##_f0 |= _FP_IMPLBIT_E;			\
	else X##_f0 &= ~(_FP_IMPLBIT_E);			\
	_flo->bits.frac = X##_f0;				\
	_flo->bits.exp  = X##_e;				\
	_flo->bits.sign = X##_s;				\
      }								\
  } while (0)


#define FP_UNPACK_E(X,val)		\
  do {					\
    FP_UNPACK_RAW_E(X,val);		\
    _FP_UNPACK_CANONICAL(E,2,X);	\
  } while (0)

#define FP_UNPACK_EP(X,val)		\
  do {					\
    FP_UNPACK_RAW_EP(X,val);		\
    _FP_UNPACK_CANONICAL(E,2,X);	\
  } while (0)

#define FP_UNPACK_SEMIRAW_E(X,val)	\
  do {					\
    FP_UNPACK_RAW_E(X,val);		\
    _FP_UNPACK_SEMIRAW(E,2,X);		\
  } while (0)

#define FP_UNPACK_SEMIRAW_EP(X,val)	\
  do {					\
    FP_UNPACK_RAW_EP(X,val);		\
    _FP_UNPACK_SEMIRAW(E,2,X);		\
  } while (0)

#define FP_PACK_E(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(E,2,X);		\
    FP_PACK_RAW_E(val,X);		\
  } while (0)

#define FP_PACK_EP(val,X)		\
  do {					\
    _FP_PACK_CANONICAL(E,2,X);		\
    FP_PACK_RAW_EP(val,X);		\
  } while (0)

#define FP_PACK_SEMIRAW_E(val,X)	\
  do {					\
    _FP_PACK_SEMIRAW(E,2,X);		\
    FP_PACK_RAW_E(val,X);		\
  } while (0)

#define FP_PACK_SEMIRAW_EP(val,X)	\
  do {					\
    _FP_PACK_SEMIRAW(E,2,X);		\
    FP_PACK_RAW_EP(val,X);		\
  } while (0)

#define FP_ISSIGNAN_E(X)	_FP_ISSIGNAN(E,2,X)
#define FP_NEG_E(R,X)		_FP_NEG(E,2,R,X)
#define FP_ADD_E(R,X,Y)		_FP_ADD(E,2,R,X,Y)
#define FP_SUB_E(R,X,Y)		_FP_SUB(E,2,R,X,Y)
#define FP_MUL_E(R,X,Y)		_FP_MUL(E,2,R,X,Y)
#define FP_DIV_E(R,X,Y)		_FP_DIV(E,2,R,X,Y)
#define FP_SQRT_E(R,X)		_FP_SQRT(E,2,R,X)

/*
 * Square root algorithms:
 * We have just one right now, maybe Newton approximation
 * should be added for those machines where division is fast.
 * We optimize it by doing most of the calculations
 * in one UWtype registers instead of two, although we don't
 * have to.
 */
#define _FP_SQRT_MEAT_E(R, S, T, X, q)			\
  do {							\
    q = (_FP_W_TYPE)1 << (_FP_W_TYPE_SIZE - 1);		\
    _FP_FRAC_SRL_2(X, (_FP_WORKBITS));			\
    while (q)						\
      {							\
        T##_f0 = S##_f0 + q;				\
        if (T##_f0 <= X##_f0)				\
          {						\
            S##_f0 = T##_f0 + q;			\
            X##_f0 -= T##_f0;				\
            R##_f0 += q;				\
          }						\
        _FP_FRAC_SLL_1(X, 1);				\
        q >>= 1;					\
      }							\
    _FP_FRAC_SLL_2(R, (_FP_WORKBITS));			\
    if (X##_f0)						\
      {							\
	if (S##_f0 < X##_f0)				\
	  R##_f0 |= _FP_WORK_ROUND;			\
	R##_f0 |= _FP_WORK_STICKY;			\
      }							\
  } while (0)
 
#define FP_CMP_E(r,X,Y,un)	_FP_CMP(E,2,r,X,Y,un)
#define FP_CMP_EQ_E(r,X,Y)	_FP_CMP_EQ(E,2,r,X,Y)
#define FP_CMP_UNORD_E(r,X,Y)	_FP_CMP_UNORD(E,2,r,X,Y)

#define FP_TO_INT_E(r,X,rsz,rsg)	_FP_TO_INT(E,2,r,X,rsz,rsg)
#define FP_FROM_INT_E(X,r,rs,rt)	_FP_FROM_INT(E,2,X,r,rs,rt)

#define _FP_FRAC_HIGH_E(X)	(X##_f1)
#define _FP_FRAC_HIGH_RAW_E(X)	(X##_f0)

#endif /* not _FP_W_TYPE_SIZE < 64 */
