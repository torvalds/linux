/* Software floating-point emulation.
   Basic four-word fraction declaration and manipulation.
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

#ifndef __MATH_EMU_OP_4_H__
#define __MATH_EMU_OP_4_H__

#define _FP_FRAC_DECL_4(X)	_FP_W_TYPE X##_f[4]
#define _FP_FRAC_COPY_4(D,S)			\
  (D##_f[0] = S##_f[0], D##_f[1] = S##_f[1],	\
   D##_f[2] = S##_f[2], D##_f[3] = S##_f[3])
#define _FP_FRAC_SET_4(X,I)	__FP_FRAC_SET_4(X, I)
#define _FP_FRAC_HIGH_4(X)	(X##_f[3])
#define _FP_FRAC_LOW_4(X)	(X##_f[0])
#define _FP_FRAC_WORD_4(X,w)	(X##_f[w])

#define _FP_FRAC_SLL_4(X,N)						\
  do {									\
    _FP_I_TYPE _up, _down, _skip, _i;					\
    _skip = (N) / _FP_W_TYPE_SIZE;					\
    _up = (N) % _FP_W_TYPE_SIZE;					\
    _down = _FP_W_TYPE_SIZE - _up;					\
    if (!_up)								\
      for (_i = 3; _i >= _skip; --_i)					\
	X##_f[_i] = X##_f[_i-_skip];					\
    else								\
      {									\
	for (_i = 3; _i > _skip; --_i)					\
	  X##_f[_i] = X##_f[_i-_skip] << _up				\
		      | X##_f[_i-_skip-1] >> _down;			\
	X##_f[_i--] = X##_f[0] << _up; 					\
      }									\
    for (; _i >= 0; --_i)						\
      X##_f[_i] = 0;							\
  } while (0)

/* This one was broken too */
#define _FP_FRAC_SRL_4(X,N)						\
  do {									\
    _FP_I_TYPE _up, _down, _skip, _i;					\
    _skip = (N) / _FP_W_TYPE_SIZE;					\
    _down = (N) % _FP_W_TYPE_SIZE;					\
    _up = _FP_W_TYPE_SIZE - _down;					\
    if (!_down)								\
      for (_i = 0; _i <= 3-_skip; ++_i)					\
	X##_f[_i] = X##_f[_i+_skip];					\
    else								\
      {									\
	for (_i = 0; _i < 3-_skip; ++_i)				\
	  X##_f[_i] = X##_f[_i+_skip] >> _down				\
		      | X##_f[_i+_skip+1] << _up;			\
	X##_f[_i++] = X##_f[3] >> _down;				\
      }									\
    for (; _i < 4; ++_i)						\
      X##_f[_i] = 0;							\
  } while (0)


/* Right shift with sticky-lsb. 
 * What this actually means is that we do a standard right-shift,
 * but that if any of the bits that fall off the right hand side
 * were one then we always set the LSbit.
 */
#define _FP_FRAC_SRS_4(X,N,size)					\
  do {									\
    _FP_I_TYPE _up, _down, _skip, _i;					\
    _FP_W_TYPE _s;							\
    _skip = (N) / _FP_W_TYPE_SIZE;					\
    _down = (N) % _FP_W_TYPE_SIZE;					\
    _up = _FP_W_TYPE_SIZE - _down;					\
    for (_s = _i = 0; _i < _skip; ++_i)					\
      _s |= X##_f[_i];							\
    _s |= X##_f[_i] << _up;						\
/* s is now != 0 if we want to set the LSbit */				\
    if (!_down)								\
      for (_i = 0; _i <= 3-_skip; ++_i)					\
	X##_f[_i] = X##_f[_i+_skip];					\
    else								\
      {									\
	for (_i = 0; _i < 3-_skip; ++_i)				\
	  X##_f[_i] = X##_f[_i+_skip] >> _down				\
		      | X##_f[_i+_skip+1] << _up;			\
	X##_f[_i++] = X##_f[3] >> _down;				\
      }									\
    for (; _i < 4; ++_i)						\
      X##_f[_i] = 0;							\
    /* don't fix the LSB until the very end when we're sure f[0] is stable */	\
    X##_f[0] |= (_s != 0);						\
  } while (0)

#define _FP_FRAC_ADD_4(R,X,Y)						\
  __FP_FRAC_ADD_4(R##_f[3], R##_f[2], R##_f[1], R##_f[0],		\
		  X##_f[3], X##_f[2], X##_f[1], X##_f[0],		\
		  Y##_f[3], Y##_f[2], Y##_f[1], Y##_f[0])

#define _FP_FRAC_SUB_4(R,X,Y)						\
  __FP_FRAC_SUB_4(R##_f[3], R##_f[2], R##_f[1], R##_f[0],		\
		  X##_f[3], X##_f[2], X##_f[1], X##_f[0],		\
		  Y##_f[3], Y##_f[2], Y##_f[1], Y##_f[0])

#define _FP_FRAC_DEC_4(X,Y)						\
  __FP_FRAC_DEC_4(X##_f[3], X##_f[2], X##_f[1], X##_f[0],		\
		  Y##_f[3], Y##_f[2], Y##_f[1], Y##_f[0])

#define _FP_FRAC_ADDI_4(X,I)						\
  __FP_FRAC_ADDI_4(X##_f[3], X##_f[2], X##_f[1], X##_f[0], I)

#define _FP_ZEROFRAC_4  0,0,0,0
#define _FP_MINFRAC_4   0,0,0,1
#define _FP_MAXFRAC_4	(~(_FP_WS_TYPE)0), (~(_FP_WS_TYPE)0), (~(_FP_WS_TYPE)0), (~(_FP_WS_TYPE)0)

#define _FP_FRAC_ZEROP_4(X)     ((X##_f[0] | X##_f[1] | X##_f[2] | X##_f[3]) == 0)
#define _FP_FRAC_NEGP_4(X)      ((_FP_WS_TYPE)X##_f[3] < 0)
#define _FP_FRAC_OVERP_4(fs,X)  (_FP_FRAC_HIGH_##fs(X) & _FP_OVERFLOW_##fs)
#define _FP_FRAC_CLEAR_OVERP_4(fs,X)  (_FP_FRAC_HIGH_##fs(X) &= ~_FP_OVERFLOW_##fs)

#define _FP_FRAC_EQ_4(X,Y)				\
 (X##_f[0] == Y##_f[0] && X##_f[1] == Y##_f[1]		\
  && X##_f[2] == Y##_f[2] && X##_f[3] == Y##_f[3])

#define _FP_FRAC_GT_4(X,Y)				\
 (X##_f[3] > Y##_f[3] ||				\
  (X##_f[3] == Y##_f[3] && (X##_f[2] > Y##_f[2] ||	\
   (X##_f[2] == Y##_f[2] && (X##_f[1] > Y##_f[1] ||	\
    (X##_f[1] == Y##_f[1] && X##_f[0] > Y##_f[0])	\
   ))							\
  ))							\
 )

#define _FP_FRAC_GE_4(X,Y)				\
 (X##_f[3] > Y##_f[3] ||				\
  (X##_f[3] == Y##_f[3] && (X##_f[2] > Y##_f[2] ||	\
   (X##_f[2] == Y##_f[2] && (X##_f[1] > Y##_f[1] ||	\
    (X##_f[1] == Y##_f[1] && X##_f[0] >= Y##_f[0])	\
   ))							\
  ))							\
 )


#define _FP_FRAC_CLZ_4(R,X)		\
  do {					\
    if (X##_f[3])			\
    {					\
	__FP_CLZ(R,X##_f[3]);		\
    }					\
    else if (X##_f[2])			\
    {					\
	__FP_CLZ(R,X##_f[2]);		\
	R += _FP_W_TYPE_SIZE;		\
    }					\
    else if (X##_f[1])			\
    {					\
	__FP_CLZ(R,X##_f[2]);		\
	R += _FP_W_TYPE_SIZE*2;		\
    }					\
    else				\
    {					\
	__FP_CLZ(R,X##_f[0]);		\
	R += _FP_W_TYPE_SIZE*3;		\
    }					\
  } while(0)


#define _FP_UNPACK_RAW_4(fs, X, val)				\
  do {								\
    union _FP_UNION_##fs _flo; _flo.flt = (val);		\
    X##_f[0] = _flo.bits.frac0;					\
    X##_f[1] = _flo.bits.frac1;					\
    X##_f[2] = _flo.bits.frac2;					\
    X##_f[3] = _flo.bits.frac3;					\
    X##_e  = _flo.bits.exp;					\
    X##_s  = _flo.bits.sign;					\
  } while (0)

#define _FP_UNPACK_RAW_4_P(fs, X, val)				\
  do {								\
    union _FP_UNION_##fs *_flo =				\
      (union _FP_UNION_##fs *)(val);				\
								\
    X##_f[0] = _flo->bits.frac0;				\
    X##_f[1] = _flo->bits.frac1;				\
    X##_f[2] = _flo->bits.frac2;				\
    X##_f[3] = _flo->bits.frac3;				\
    X##_e  = _flo->bits.exp;					\
    X##_s  = _flo->bits.sign;					\
  } while (0)

#define _FP_PACK_RAW_4(fs, val, X)				\
  do {								\
    union _FP_UNION_##fs _flo;					\
    _flo.bits.frac0 = X##_f[0];					\
    _flo.bits.frac1 = X##_f[1];					\
    _flo.bits.frac2 = X##_f[2];					\
    _flo.bits.frac3 = X##_f[3];					\
    _flo.bits.exp   = X##_e;					\
    _flo.bits.sign  = X##_s;					\
    (val) = _flo.flt;				   		\
  } while (0)

#define _FP_PACK_RAW_4_P(fs, val, X)				\
  do {								\
    union _FP_UNION_##fs *_flo =				\
      (union _FP_UNION_##fs *)(val);				\
								\
    _flo->bits.frac0 = X##_f[0];				\
    _flo->bits.frac1 = X##_f[1];				\
    _flo->bits.frac2 = X##_f[2];				\
    _flo->bits.frac3 = X##_f[3];				\
    _flo->bits.exp   = X##_e;					\
    _flo->bits.sign  = X##_s;					\
  } while (0)

/*
 * Multiplication algorithms:
 */

/* Given a 1W * 1W => 2W primitive, do the extended multiplication.  */

#define _FP_MUL_MEAT_4_wide(wfracbits, R, X, Y, doit)			    \
  do {									    \
    _FP_FRAC_DECL_8(_z); _FP_FRAC_DECL_2(_b); _FP_FRAC_DECL_2(_c);	    \
    _FP_FRAC_DECL_2(_d); _FP_FRAC_DECL_2(_e); _FP_FRAC_DECL_2(_f);	    \
									    \
    doit(_FP_FRAC_WORD_8(_z,1), _FP_FRAC_WORD_8(_z,0), X##_f[0], Y##_f[0]); \
    doit(_b_f1, _b_f0, X##_f[0], Y##_f[1]);				    \
    doit(_c_f1, _c_f0, X##_f[1], Y##_f[0]);				    \
    doit(_d_f1, _d_f0, X##_f[1], Y##_f[1]);				    \
    doit(_e_f1, _e_f0, X##_f[0], Y##_f[2]);				    \
    doit(_f_f1, _f_f0, X##_f[2], Y##_f[0]);				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,3),_FP_FRAC_WORD_8(_z,2),	    \
		    _FP_FRAC_WORD_8(_z,1), 0,_b_f1,_b_f0,		    \
		    0,0,_FP_FRAC_WORD_8(_z,1));				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,3),_FP_FRAC_WORD_8(_z,2),	    \
		    _FP_FRAC_WORD_8(_z,1), 0,_c_f1,_c_f0,		    \
		    _FP_FRAC_WORD_8(_z,3),_FP_FRAC_WORD_8(_z,2),	    \
		    _FP_FRAC_WORD_8(_z,1));				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,4),_FP_FRAC_WORD_8(_z,3),	    \
		    _FP_FRAC_WORD_8(_z,2), 0,_d_f1,_d_f0,		    \
		    0,_FP_FRAC_WORD_8(_z,3),_FP_FRAC_WORD_8(_z,2));	    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,4),_FP_FRAC_WORD_8(_z,3),	    \
		    _FP_FRAC_WORD_8(_z,2), 0,_e_f1,_e_f0,		    \
		    _FP_FRAC_WORD_8(_z,4),_FP_FRAC_WORD_8(_z,3),	    \
		    _FP_FRAC_WORD_8(_z,2));				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,4),_FP_FRAC_WORD_8(_z,3),	    \
		    _FP_FRAC_WORD_8(_z,2), 0,_f_f1,_f_f0,		    \
		    _FP_FRAC_WORD_8(_z,4),_FP_FRAC_WORD_8(_z,3),	    \
		    _FP_FRAC_WORD_8(_z,2));				    \
    doit(_b_f1, _b_f0, X##_f[0], Y##_f[3]);				    \
    doit(_c_f1, _c_f0, X##_f[3], Y##_f[0]);				    \
    doit(_d_f1, _d_f0, X##_f[1], Y##_f[2]);				    \
    doit(_e_f1, _e_f0, X##_f[2], Y##_f[1]);				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,5),_FP_FRAC_WORD_8(_z,4),	    \
		    _FP_FRAC_WORD_8(_z,3), 0,_b_f1,_b_f0,		    \
		    0,_FP_FRAC_WORD_8(_z,4),_FP_FRAC_WORD_8(_z,3));	    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,5),_FP_FRAC_WORD_8(_z,4),	    \
		    _FP_FRAC_WORD_8(_z,3), 0,_c_f1,_c_f0,		    \
		    _FP_FRAC_WORD_8(_z,5),_FP_FRAC_WORD_8(_z,4),	    \
		    _FP_FRAC_WORD_8(_z,3));				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,5),_FP_FRAC_WORD_8(_z,4),	    \
		    _FP_FRAC_WORD_8(_z,3), 0,_d_f1,_d_f0,		    \
		    _FP_FRAC_WORD_8(_z,5),_FP_FRAC_WORD_8(_z,4),	    \
		    _FP_FRAC_WORD_8(_z,3));				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,5),_FP_FRAC_WORD_8(_z,4),	    \
		    _FP_FRAC_WORD_8(_z,3), 0,_e_f1,_e_f0,		    \
		    _FP_FRAC_WORD_8(_z,5),_FP_FRAC_WORD_8(_z,4),	    \
		    _FP_FRAC_WORD_8(_z,3));				    \
    doit(_b_f1, _b_f0, X##_f[2], Y##_f[2]);				    \
    doit(_c_f1, _c_f0, X##_f[1], Y##_f[3]);				    \
    doit(_d_f1, _d_f0, X##_f[3], Y##_f[1]);				    \
    doit(_e_f1, _e_f0, X##_f[2], Y##_f[3]);				    \
    doit(_f_f1, _f_f0, X##_f[3], Y##_f[2]);				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,6),_FP_FRAC_WORD_8(_z,5),	    \
		    _FP_FRAC_WORD_8(_z,4), 0,_b_f1,_b_f0,		    \
		    0,_FP_FRAC_WORD_8(_z,5),_FP_FRAC_WORD_8(_z,4));	    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,6),_FP_FRAC_WORD_8(_z,5),	    \
		    _FP_FRAC_WORD_8(_z,4), 0,_c_f1,_c_f0,		    \
		    _FP_FRAC_WORD_8(_z,6),_FP_FRAC_WORD_8(_z,5),	    \
		    _FP_FRAC_WORD_8(_z,4));				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,6),_FP_FRAC_WORD_8(_z,5),	    \
		    _FP_FRAC_WORD_8(_z,4), 0,_d_f1,_d_f0,		    \
		    _FP_FRAC_WORD_8(_z,6),_FP_FRAC_WORD_8(_z,5),	    \
		    _FP_FRAC_WORD_8(_z,4));				    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,7),_FP_FRAC_WORD_8(_z,6),	    \
		    _FP_FRAC_WORD_8(_z,5), 0,_e_f1,_e_f0,		    \
		    0,_FP_FRAC_WORD_8(_z,6),_FP_FRAC_WORD_8(_z,5));	    \
    __FP_FRAC_ADD_3(_FP_FRAC_WORD_8(_z,7),_FP_FRAC_WORD_8(_z,6),	    \
		    _FP_FRAC_WORD_8(_z,5), 0,_f_f1,_f_f0,		    \
		    _FP_FRAC_WORD_8(_z,7),_FP_FRAC_WORD_8(_z,6),	    \
		    _FP_FRAC_WORD_8(_z,5));				    \
    doit(_b_f1, _b_f0, X##_f[3], Y##_f[3]);				    \
    __FP_FRAC_ADD_2(_FP_FRAC_WORD_8(_z,7),_FP_FRAC_WORD_8(_z,6),	    \
		    _b_f1,_b_f0,					    \
		    _FP_FRAC_WORD_8(_z,7),_FP_FRAC_WORD_8(_z,6));	    \
									    \
    /* Normalize since we know where the msb of the multiplicands	    \
       were (bit B), we know that the msb of the of the product is	    \
       at either 2B or 2B-1.  */					    \
    _FP_FRAC_SRS_8(_z, wfracbits-1, 2*wfracbits);			    \
    __FP_FRAC_SET_4(R, _FP_FRAC_WORD_8(_z,3), _FP_FRAC_WORD_8(_z,2),	    \
		    _FP_FRAC_WORD_8(_z,1), _FP_FRAC_WORD_8(_z,0));	    \
  } while (0)

#define _FP_MUL_MEAT_4_gmp(wfracbits, R, X, Y)				    \
  do {									    \
    _FP_FRAC_DECL_8(_z);						    \
									    \
    mpn_mul_n(_z_f, _x_f, _y_f, 4);					    \
									    \
    /* Normalize since we know where the msb of the multiplicands	    \
       were (bit B), we know that the msb of the of the product is	    \
       at either 2B or 2B-1.  */					    \
    _FP_FRAC_SRS_8(_z, wfracbits-1, 2*wfracbits);	 		    \
    __FP_FRAC_SET_4(R, _FP_FRAC_WORD_8(_z,3), _FP_FRAC_WORD_8(_z,2),	    \
		    _FP_FRAC_WORD_8(_z,1), _FP_FRAC_WORD_8(_z,0));	    \
  } while (0)

/*
 * Helper utility for _FP_DIV_MEAT_4_udiv:
 * pppp = m * nnn
 */
#define umul_ppppmnnn(p3,p2,p1,p0,m,n2,n1,n0)				    \
  do {									    \
    UWtype _t;								    \
    umul_ppmm(p1,p0,m,n0);						    \
    umul_ppmm(p2,_t,m,n1);						    \
    __FP_FRAC_ADDI_2(p2,p1,_t);						    \
    umul_ppmm(p3,_t,m,n2);						    \
    __FP_FRAC_ADDI_2(p3,p2,_t);						    \
  } while (0)

/*
 * Division algorithms:
 */

#define _FP_DIV_MEAT_4_udiv(fs, R, X, Y)				    \
  do {									    \
    int _i;								    \
    _FP_FRAC_DECL_4(_n); _FP_FRAC_DECL_4(_m);				    \
    _FP_FRAC_SET_4(_n, _FP_ZEROFRAC_4);					    \
    if (_FP_FRAC_GT_4(X, Y))						    \
      {									    \
	_n_f[3] = X##_f[0] << (_FP_W_TYPE_SIZE - 1);			    \
	_FP_FRAC_SRL_4(X, 1);						    \
      }									    \
    else								    \
      R##_e--;								    \
									    \
    /* Normalize, i.e. make the most significant bit of the 		    \
       denominator set. */						    \
    _FP_FRAC_SLL_4(Y, _FP_WFRACXBITS_##fs);				    \
									    \
    for (_i = 3; ; _i--)						    \
      {									    \
        if (X##_f[3] == Y##_f[3])					    \
          {								    \
            /* This is a special case, not an optimization		    \
               (X##_f[3]/Y##_f[3] would not fit into UWtype).		    \
               As X## is guaranteed to be < Y,  R##_f[_i] can be either	    \
               (UWtype)-1 or (UWtype)-2.  */				    \
            R##_f[_i] = -1;						    \
            if (!_i)							    \
	      break;							    \
            __FP_FRAC_SUB_4(X##_f[3], X##_f[2], X##_f[1], X##_f[0],	    \
			    Y##_f[2], Y##_f[1], Y##_f[0], 0,		    \
			    X##_f[2], X##_f[1], X##_f[0], _n_f[_i]);	    \
            _FP_FRAC_SUB_4(X, Y, X);					    \
            if (X##_f[3] > Y##_f[3])					    \
              {								    \
                R##_f[_i] = -2;						    \
                _FP_FRAC_ADD_4(X, Y, X);				    \
              }								    \
          }								    \
        else								    \
          {								    \
            udiv_qrnnd(R##_f[_i], X##_f[3], X##_f[3], X##_f[2], Y##_f[3]);  \
            umul_ppppmnnn(_m_f[3], _m_f[2], _m_f[1], _m_f[0],		    \
			  R##_f[_i], Y##_f[2], Y##_f[1], Y##_f[0]);	    \
            X##_f[2] = X##_f[1];					    \
            X##_f[1] = X##_f[0];					    \
            X##_f[0] = _n_f[_i];					    \
            if (_FP_FRAC_GT_4(_m, X))					    \
              {								    \
                R##_f[_i]--;						    \
                _FP_FRAC_ADD_4(X, Y, X);				    \
                if (_FP_FRAC_GE_4(X, Y) && _FP_FRAC_GT_4(_m, X))	    \
                  {							    \
		    R##_f[_i]--;					    \
		    _FP_FRAC_ADD_4(X, Y, X);				    \
                  }							    \
              }								    \
            _FP_FRAC_DEC_4(X, _m);					    \
            if (!_i)							    \
	      {								    \
		if (!_FP_FRAC_EQ_4(X, _m))				    \
		  R##_f[0] |= _FP_WORK_STICKY;				    \
		break;							    \
	      }								    \
          }								    \
      }									    \
  } while (0)


/*
 * Square root algorithms:
 * We have just one right now, maybe Newton approximation
 * should be added for those machines where division is fast.
 */
 
#define _FP_SQRT_MEAT_4(R, S, T, X, q)				\
  do {								\
    while (q)							\
      {								\
	T##_f[3] = S##_f[3] + q;				\
	if (T##_f[3] <= X##_f[3])				\
	  {							\
	    S##_f[3] = T##_f[3] + q;				\
	    X##_f[3] -= T##_f[3];				\
	    R##_f[3] += q;					\
	  }							\
	_FP_FRAC_SLL_4(X, 1);					\
	q >>= 1;						\
      }								\
    q = (_FP_W_TYPE)1 << (_FP_W_TYPE_SIZE - 1);			\
    while (q)							\
      {								\
	T##_f[2] = S##_f[2] + q;				\
	T##_f[3] = S##_f[3];					\
	if (T##_f[3] < X##_f[3] || 				\
	    (T##_f[3] == X##_f[3] && T##_f[2] <= X##_f[2]))	\
	  {							\
	    S##_f[2] = T##_f[2] + q;				\
	    S##_f[3] += (T##_f[2] > S##_f[2]);			\
	    __FP_FRAC_DEC_2(X##_f[3], X##_f[2],			\
			    T##_f[3], T##_f[2]);		\
	    R##_f[2] += q;					\
	  }							\
	_FP_FRAC_SLL_4(X, 1);					\
	q >>= 1;						\
      }								\
    q = (_FP_W_TYPE)1 << (_FP_W_TYPE_SIZE - 1);			\
    while (q)							\
      {								\
	T##_f[1] = S##_f[1] + q;				\
	T##_f[2] = S##_f[2];					\
	T##_f[3] = S##_f[3];					\
	if (T##_f[3] < X##_f[3] || 				\
	    (T##_f[3] == X##_f[3] && (T##_f[2] < X##_f[2] ||	\
	     (T##_f[2] == X##_f[2] && T##_f[1] <= X##_f[1]))))	\
	  {							\
	    S##_f[1] = T##_f[1] + q;				\
	    S##_f[2] += (T##_f[1] > S##_f[1]);			\
	    S##_f[3] += (T##_f[2] > S##_f[2]);			\
	    __FP_FRAC_DEC_3(X##_f[3], X##_f[2], X##_f[1],	\
	    		    T##_f[3], T##_f[2], T##_f[1]);	\
	    R##_f[1] += q;					\
	  }							\
	_FP_FRAC_SLL_4(X, 1);					\
	q >>= 1;						\
      }								\
    q = (_FP_W_TYPE)1 << (_FP_W_TYPE_SIZE - 1);			\
    while (q != _FP_WORK_ROUND)					\
      {								\
	T##_f[0] = S##_f[0] + q;				\
	T##_f[1] = S##_f[1];					\
	T##_f[2] = S##_f[2];					\
	T##_f[3] = S##_f[3];					\
	if (_FP_FRAC_GE_4(X,T))					\
	  {							\
	    S##_f[0] = T##_f[0] + q;				\
	    S##_f[1] += (T##_f[0] > S##_f[0]);			\
	    S##_f[2] += (T##_f[1] > S##_f[1]);			\
	    S##_f[3] += (T##_f[2] > S##_f[2]);			\
	    _FP_FRAC_DEC_4(X, T);				\
	    R##_f[0] += q;					\
	  }							\
	_FP_FRAC_SLL_4(X, 1);					\
	q >>= 1;						\
      }								\
    if (!_FP_FRAC_ZEROP_4(X))					\
      {								\
	if (_FP_FRAC_GT_4(X,S))					\
	  R##_f[0] |= _FP_WORK_ROUND;				\
	R##_f[0] |= _FP_WORK_STICKY;				\
      }								\
  } while (0)


/*
 * Internals 
 */

#define __FP_FRAC_SET_4(X,I3,I2,I1,I0)					\
  (X##_f[3] = I3, X##_f[2] = I2, X##_f[1] = I1, X##_f[0] = I0)

#ifndef __FP_FRAC_ADD_3
#define __FP_FRAC_ADD_3(r2,r1,r0,x2,x1,x0,y2,y1,y0)		\
  do {								\
    int _c1, _c2;							\
    r0 = x0 + y0;						\
    _c1 = r0 < x0;						\
    r1 = x1 + y1;						\
    _c2 = r1 < x1;						\
    r1 += _c1;							\
    _c2 |= r1 < _c1;						\
    r2 = x2 + y2 + _c2;						\
  } while (0)
#endif

#ifndef __FP_FRAC_ADD_4
#define __FP_FRAC_ADD_4(r3,r2,r1,r0,x3,x2,x1,x0,y3,y2,y1,y0)	\
  do {								\
    int _c1, _c2, _c3;						\
    r0 = x0 + y0;						\
    _c1 = r0 < x0;						\
    r1 = x1 + y1;						\
    _c2 = r1 < x1;						\
    r1 += _c1;							\
    _c2 |= r1 < _c1;						\
    r2 = x2 + y2;						\
    _c3 = r2 < x2;						\
    r2 += _c2;							\
    _c3 |= r2 < _c2;						\
    r3 = x3 + y3 + _c3;						\
  } while (0)
#endif

#ifndef __FP_FRAC_SUB_3
#define __FP_FRAC_SUB_3(r2,r1,r0,x2,x1,x0,y2,y1,y0)		\
  do {								\
    int _c1, _c2;							\
    r0 = x0 - y0;						\
    _c1 = r0 > x0;						\
    r1 = x1 - y1;						\
    _c2 = r1 > x1;						\
    r1 -= _c1;							\
    _c2 |= r1 > _c1;						\
    r2 = x2 - y2 - _c2;						\
  } while (0)
#endif

#ifndef __FP_FRAC_SUB_4
#define __FP_FRAC_SUB_4(r3,r2,r1,r0,x3,x2,x1,x0,y3,y2,y1,y0)	\
  do {								\
    int _c1, _c2, _c3;						\
    r0 = x0 - y0;						\
    _c1 = r0 > x0;						\
    r1 = x1 - y1;						\
    _c2 = r1 > x1;						\
    r1 -= _c1;							\
    _c2 |= r1 > _c1;						\
    r2 = x2 - y2;						\
    _c3 = r2 > x2;						\
    r2 -= _c2;							\
    _c3 |= r2 > _c2;						\
    r3 = x3 - y3 - _c3;						\
  } while (0)
#endif

#ifndef __FP_FRAC_DEC_3
#define __FP_FRAC_DEC_3(x2,x1,x0,y2,y1,y0)				\
  do {									\
    UWtype _t0, _t1, _t2;						\
    _t0 = x0, _t1 = x1, _t2 = x2;					\
    __FP_FRAC_SUB_3 (x2, x1, x0, _t2, _t1, _t0, y2, y1, y0);		\
  } while (0)
#endif

#ifndef __FP_FRAC_DEC_4
#define __FP_FRAC_DEC_4(x3,x2,x1,x0,y3,y2,y1,y0)			\
  do {									\
    UWtype _t0, _t1, _t2, _t3;						\
    _t0 = x0, _t1 = x1, _t2 = x2, _t3 = x3;				\
    __FP_FRAC_SUB_4 (x3,x2,x1,x0,_t3,_t2,_t1,_t0, y3,y2,y1,y0);		\
  } while (0)
#endif

#ifndef __FP_FRAC_ADDI_4
#define __FP_FRAC_ADDI_4(x3,x2,x1,x0,i)					\
  do {									\
    UWtype _t;								\
    _t = ((x0 += i) < i);						\
    x1 += _t; _t = (x1 < _t);						\
    x2 += _t; _t = (x2 < _t);						\
    x3 += _t;								\
  } while (0)
#endif

/* Convert FP values between word sizes. This appears to be more
 * complicated than I'd have expected it to be, so these might be
 * wrong... These macros are in any case somewhat bogus because they
 * use information about what various FRAC_n variables look like 
 * internally [eg, that 2 word vars are X_f0 and x_f1]. But so do
 * the ones in op-2.h and op-1.h. 
 */
#define _FP_FRAC_CONV_1_4(dfs, sfs, D, S)				\
   do {									\
     if (S##_c != FP_CLS_NAN)						\
       _FP_FRAC_SRS_4(S, (_FP_WFRACBITS_##sfs - _FP_WFRACBITS_##dfs),	\
			  _FP_WFRACBITS_##sfs);				\
     else								\
       _FP_FRAC_SRL_4(S, (_FP_WFRACBITS_##sfs - _FP_WFRACBITS_##dfs));	\
     D##_f = S##_f[0];							\
  } while (0)

#define _FP_FRAC_CONV_2_4(dfs, sfs, D, S)				\
   do {									\
     if (S##_c != FP_CLS_NAN)						\
       _FP_FRAC_SRS_4(S, (_FP_WFRACBITS_##sfs - _FP_WFRACBITS_##dfs),	\
		      _FP_WFRACBITS_##sfs);				\
     else								\
       _FP_FRAC_SRL_4(S, (_FP_WFRACBITS_##sfs - _FP_WFRACBITS_##dfs));	\
     D##_f0 = S##_f[0];							\
     D##_f1 = S##_f[1];							\
  } while (0)

/* Assembly/disassembly for converting to/from integral types.  
 * No shifting or overflow handled here.
 */
/* Put the FP value X into r, which is an integer of size rsize. */
#define _FP_FRAC_ASSEMBLE_4(r, X, rsize)				\
  do {									\
    if (rsize <= _FP_W_TYPE_SIZE)					\
      r = X##_f[0];							\
    else if (rsize <= 2*_FP_W_TYPE_SIZE)				\
    {									\
      r = X##_f[1];							\
      r <<= _FP_W_TYPE_SIZE;						\
      r += X##_f[0];							\
    }									\
    else								\
    {									\
      /* I'm feeling lazy so we deal with int == 3words (implausible)*/	\
      /* and int == 4words as a single case.			 */	\
      r = X##_f[3];							\
      r <<= _FP_W_TYPE_SIZE;						\
      r += X##_f[2];							\
      r <<= _FP_W_TYPE_SIZE;						\
      r += X##_f[1];							\
      r <<= _FP_W_TYPE_SIZE;						\
      r += X##_f[0];							\
    }									\
  } while (0)

/* "No disassemble Number Five!" */
/* move an integer of size rsize into X's fractional part. We rely on
 * the _f[] array consisting of words of size _FP_W_TYPE_SIZE to avoid
 * having to mask the values we store into it.
 */
#define _FP_FRAC_DISASSEMBLE_4(X, r, rsize)				\
  do {									\
    X##_f[0] = r;							\
    X##_f[1] = (rsize <= _FP_W_TYPE_SIZE ? 0 : r >> _FP_W_TYPE_SIZE);	\
    X##_f[2] = (rsize <= 2*_FP_W_TYPE_SIZE ? 0 : r >> 2*_FP_W_TYPE_SIZE); \
    X##_f[3] = (rsize <= 3*_FP_W_TYPE_SIZE ? 0 : r >> 3*_FP_W_TYPE_SIZE); \
  } while (0)

#define _FP_FRAC_CONV_4_1(dfs, sfs, D, S)				\
   do {									\
     D##_f[0] = S##_f;							\
     D##_f[1] = D##_f[2] = D##_f[3] = 0;				\
     _FP_FRAC_SLL_4(D, (_FP_WFRACBITS_##dfs - _FP_WFRACBITS_##sfs));	\
   } while (0)

#define _FP_FRAC_CONV_4_2(dfs, sfs, D, S)				\
   do {									\
     D##_f[0] = S##_f0;							\
     D##_f[1] = S##_f1;							\
     D##_f[2] = D##_f[3] = 0;						\
     _FP_FRAC_SLL_4(D, (_FP_WFRACBITS_##dfs - _FP_WFRACBITS_##sfs));	\
   } while (0)

#endif
