/*	$OpenBSD: sgl_float.h,v 1.13 2021/03/11 11:16:57 jsg Exp $	*/
/*
  (c) Copyright 1986 HEWLETT-PACKARD COMPANY
  To anyone who acknowledges that this file is provided "AS IS"
  without any express or implied warranty:
      permission to use, copy, modify, and distribute this file
  for any purpose is hereby granted without fee, provided that
  the above copyright notice and this notice appears in all
  copies, and that the name of Hewlett-Packard Company not be
  used in advertising or publicity pertaining to distribution
  of the software without specific, written prior permission.
  Hewlett-Packard Company makes no representations about the
  suitability of this software for any purpose.
*/
/* @(#)sgl_float.h: Revision: 2.8.88.1 Date: 93/12/07 15:07:17 */

#include <sys/cdefs.h>

/******************************
 * Single precision functions *
 ******************************/

/* 32-bit word grabbing functions */
#define Sgl_firstword(value) Sall(value)
#define Sgl_secondword(value) dummy_location
#define Sgl_thirdword(value) dummy_location
#define Sgl_fourthword(value) dummy_location

#define Sgl_sign(object) Ssign(object)
#define Sgl_exponent(object) Sexponent(object)
#define Sgl_signexponent(object) Ssignexponent(object)
#define Sgl_mantissa(object) Smantissa(object)
#define Sgl_exponentmantissa(object) Sexponentmantissa(object)
#define Sgl_all(object) Sall(object)

/* sgl_and_signs ands the sign bits of each argument and puts the result
 * into the first argument. sgl_or_signs ors those same sign bits */
#define Sgl_and_signs( src1dst, src2)		\
    Sall(src1dst) = (Sall(src2)|~(1<<31)) & Sall(src1dst)
#define Sgl_or_signs( src1dst, src2)		\
    Sall(src1dst) = (Sall(src2)&(1<<31)) | Sall(src1dst)

/* The hidden bit is always the low bit of the exponent */
#define Sgl_clear_exponent_set_hidden(srcdst) Deposit_sexponent(srcdst,1)
#define Sgl_clear_signexponent_set_hidden(srcdst) \
    Deposit_ssignexponent(srcdst,1)
#define Sgl_clear_sign(srcdst) Sall(srcdst) &= ~(1<<31)
#define Sgl_clear_signexponent(srcdst) Sall(srcdst) &= 0x007fffff

/* varamount must be less than 32 for the next three functions */
#define Sgl_rightshift(srcdst, varamount)	\
    Sall(srcdst) >>= varamount
#define Sgl_leftshift(srcdst, varamount)	\
    Sall(srcdst) <<= varamount
#define Sgl_rightshift_exponentmantissa(srcdst, varamount) \
    Sall(srcdst) = \
	(Sexponentmantissa(srcdst) >> (varamount)) | (Sall(srcdst) & (1<<31))

#define Sgl_leftshiftby1_withextent(left,right,result) \
    Shiftdouble(Sall(left),Extall(right),31,Sall(result))

#define Sgl_rightshiftby1_withextent(left,right,dst)		\
    Shiftdouble(Sall(left),Extall(right),1,Extall(right))
#define Sgl_arithrightshiftby1(srcdst)	\
    Sall(srcdst) = (int)Sall(srcdst) >> 1

/* Sign extend the sign bit with an integer destination */
#define Sgl_signextendedsign(value) Ssignedsign(value)

#define Sgl_isone_hidden(sgl_value) (Shidden(sgl_value))
#define Sgl_increment(sgl_value) Sall(sgl_value) += 1
#define Sgl_increment_mantissa(sgl_value) \
    Deposit_smantissa(sgl_value,sgl_value+1)
#define Sgl_decrement(sgl_value) Sall(sgl_value) -= 1

#define Sgl_isone_sign(sgl_value) (Is_ssign(sgl_value)!=0)
#define Sgl_isone_hiddenoverflow(sgl_value) \
    (Is_shiddenoverflow(sgl_value)!=0)
#define Sgl_isone_lowmantissa(sgl_value) (Is_slow(sgl_value)!=0)
#define Sgl_isone_signaling(sgl_value) (Is_ssignaling(sgl_value)!=0)
#define Sgl_is_signalingnan(sgl_value) (Ssignalingnan(sgl_value)==0x1ff)
#define Sgl_isnotzero(sgl_value) (Sall(sgl_value)!=0)
#define Sgl_isnotzero_hiddenhigh7mantissa(sgl_value) \
    (Shiddenhigh7mantissa(sgl_value)!=0)
#define Sgl_isnotzero_low4(sgl_value) (Slow4(sgl_value)!=0)
#define Sgl_isnotzero_exponent(sgl_value) (Sexponent(sgl_value)!=0)
#define Sgl_isnotzero_mantissa(sgl_value) (Smantissa(sgl_value)!=0)
#define Sgl_isnotzero_exponentmantissa(sgl_value) \
    (Sexponentmantissa(sgl_value)!=0)
#define Sgl_iszero(sgl_value) (Sall(sgl_value)==0)
#define Sgl_iszero_signaling(sgl_value) (Is_ssignaling(sgl_value)==0)
#define Sgl_iszero_hidden(sgl_value) (Is_shidden(sgl_value)==0)
#define Sgl_iszero_hiddenoverflow(sgl_value) \
    (Is_shiddenoverflow(sgl_value)==0)
#define Sgl_iszero_hiddenhigh3mantissa(sgl_value) \
    (Shiddenhigh3mantissa(sgl_value)==0)
#define Sgl_iszero_hiddenhigh7mantissa(sgl_value) \
    (Shiddenhigh7mantissa(sgl_value)==0)
#define Sgl_iszero_sign(sgl_value) (Is_ssign(sgl_value)==0)
#define Sgl_iszero_exponent(sgl_value) (Sexponent(sgl_value)==0)
#define Sgl_iszero_mantissa(sgl_value) (Smantissa(sgl_value)==0)
#define Sgl_iszero_exponentmantissa(sgl_value) \
    (Sexponentmantissa(sgl_value)==0)
#define Sgl_isinfinity_exponent(sgl_value)		\
    (Sgl_exponent(sgl_value)==SGL_INFINITY_EXPONENT)
#define Sgl_isnotinfinity_exponent(sgl_value)		\
    (Sgl_exponent(sgl_value)!=SGL_INFINITY_EXPONENT)
#define Sgl_isinfinity(sgl_value)			\
    (Sgl_exponent(sgl_value)==SGL_INFINITY_EXPONENT &&	\
    Sgl_mantissa(sgl_value)==0)
#define Sgl_isnan(sgl_value)				\
    (Sgl_exponent(sgl_value)==SGL_INFINITY_EXPONENT &&	\
    Sgl_mantissa(sgl_value)!=0)
#define Sgl_isnotnan(sgl_value)				\
    (Sgl_exponent(sgl_value)!=SGL_INFINITY_EXPONENT ||	\
    Sgl_mantissa(sgl_value)==0)
#define Sgl_islessthan(sgl_op1,sgl_op2)			\
    (Sall(sgl_op1) < Sall(sgl_op2))
#define Sgl_isgreaterthan(sgl_op1,sgl_op2)		\
    (Sall(sgl_op1) > Sall(sgl_op2))
#define Sgl_isnotlessthan(sgl_op1,sgl_op2)		\
    (Sall(sgl_op1) >= Sall(sgl_op2))
#define Sgl_isequal(sgl_op1,sgl_op2)			\
    (Sall(sgl_op1) == Sall(sgl_op2))

#define Sgl_leftshiftby8(sgl_value) \
    Sall(sgl_value) <<= 8
#define Sgl_leftshiftby4(sgl_value) \
    Sall(sgl_value) <<= 4
#define Sgl_leftshiftby3(sgl_value) \
    Sall(sgl_value) <<= 3
#define Sgl_leftshiftby2(sgl_value) \
    Sall(sgl_value) <<= 2
#define Sgl_leftshiftby1(sgl_value) \
    Sall(sgl_value) <<= 1
#define Sgl_rightshiftby1(sgl_value) \
    Sall(sgl_value) >>= 1
#define Sgl_rightshiftby4(sgl_value) \
    Sall(sgl_value) >>= 4
#define Sgl_rightshiftby8(sgl_value) \
    Sall(sgl_value) >>= 8

#define Sgl_ismagnitudeless(signlessleft,signlessright)			\
/*  unsigned int signlessleft, signlessright; */			\
      (signlessleft < signlessright)


#define Sgl_copytoint_exponentmantissa(source,dest)     \
    dest = Sexponentmantissa(source)

/* A quiet NaN has the high mantissa bit clear and at least on other (in this
 * case the adjacent bit) bit set. */
#define Sgl_set_quiet(sgl_value) Deposit_shigh2mantissa(sgl_value,1)
#define Sgl_set_exponent(sgl_value,exp) Deposit_sexponent(sgl_value,exp)

#define Sgl_set_mantissa(dest,value) Deposit_smantissa(dest,value)
#define Sgl_set_exponentmantissa(dest,value) \
    Deposit_sexponentmantissa(dest,value)

/*  An infinity is represented with the max exponent and a zero mantissa */
#define Sgl_setinfinity_exponent(sgl_value) \
    Deposit_sexponent(sgl_value,SGL_INFINITY_EXPONENT)
#define Sgl_setinfinity_exponentmantissa(sgl_value)	\
    Deposit_sexponentmantissa(sgl_value, \
	(SGL_INFINITY_EXPONENT << (32-(1+SGL_EXP_LENGTH))))
#define Sgl_setinfinitypositive(sgl_value)		\
    Sall(sgl_value) = (SGL_INFINITY_EXPONENT << (32-(1+SGL_EXP_LENGTH)))
#define Sgl_setinfinitynegative(sgl_value)		\
    Sall(sgl_value) = (SGL_INFINITY_EXPONENT << (32-(1+SGL_EXP_LENGTH))) \
    | (1<<31)
#define Sgl_setinfinity(sgl_value,sign)					\
    Sall(sgl_value) = (SGL_INFINITY_EXPONENT << (32-(1+SGL_EXP_LENGTH))) | \
     (sign << 31)
#define Sgl_sethigh4bits(sgl_value, extsign)  \
    Deposit_shigh4(sgl_value,extsign)
#define Sgl_set_sign(sgl_value,sign) Deposit_ssign(sgl_value,sign)
#define Sgl_invert_sign(sgl_value)  \
    Deposit_ssign(sgl_value,~Ssign(sgl_value))
#define Sgl_setone_sign(sgl_value) Deposit_ssign(sgl_value,1)
#define Sgl_setone_lowmantissa(sgl_value) Deposit_slow(sgl_value,1)
#define Sgl_setzero_sign(sgl_value)  Sall(sgl_value) &= 0x7fffffff
#define Sgl_setzero_exponent(sgl_value) Sall(sgl_value) &= 0x807fffff
#define Sgl_setzero_mantissa(sgl_value) Sall(sgl_value) &= 0xff800000
#define Sgl_setzero_exponentmantissa(sgl_value)  Sall(sgl_value) &= 0x80000000
#define Sgl_setzero(sgl_value) Sall(sgl_value) = 0
#define Sgl_setnegativezero(sgl_value) Sall(sgl_value) = 1U << 31

/* Use following macro for both overflow & underflow conditions */
#define ovfl -
#define unfl +
#define Sgl_setwrapped_exponent(sgl_value,exponent,op) \
    Deposit_sexponent(sgl_value,(exponent op SGL_WRAP))

#define Sgl_setlargestpositive(sgl_value)				\
    Sall(sgl_value) = ((SGL_EMAX+SGL_BIAS) << (32-(1+SGL_EXP_LENGTH)))	\
			| ((1<<(32-(1+SGL_EXP_LENGTH))) - 1)
#define Sgl_setlargestnegative(sgl_value)				\
    Sall(sgl_value) = ((SGL_EMAX+SGL_BIAS) << (32-(1+SGL_EXP_LENGTH)))	\
			| ((1<<(32-(1+SGL_EXP_LENGTH))) - 1 ) | (1<<31)

#define Sgl_setnegativeinfinity(sgl_value)	\
    Sall(sgl_value) =				\
    ((1<<SGL_EXP_LENGTH) | SGL_INFINITY_EXPONENT) << (32-(1+SGL_EXP_LENGTH))
#define Sgl_setlargest(sgl_value,sign)					\
    Sall(sgl_value) = ((sign) << 31) |					\
	(((SGL_EMAX+SGL_BIAS) << (32-(1+SGL_EXP_LENGTH)))		\
	  | ((1 << (32-(1+SGL_EXP_LENGTH))) - 1 ))
#define Sgl_setlargest_exponentmantissa(sgl_value)			\
    Sall(sgl_value) = (Sall(sgl_value) & (1<<31)) |			\
	(((SGL_EMAX+SGL_BIAS) << (32-(1+SGL_EXP_LENGTH)))		\
	  | ((1 << (32-(1+SGL_EXP_LENGTH))) - 1 ))

/* The high bit is always zero so arithmetic or logical shifts will work. */
#define Sgl_right_align(srcdst,shift,extent)				\
    /* sgl_floating_point srcdst; int shift; extension extent */	\
    if (shift < 32) {							\
	Extall(extent) = Sall(srcdst) << (32-(shift));			\
	Sall(srcdst) >>= shift;						\
    }									\
    else {								\
	Extall(extent) = Sall(srcdst);					\
	Sall(srcdst) = 0;						\
    }
#define Sgl_hiddenhigh3mantissa(sgl_value) Shiddenhigh3mantissa(sgl_value)
#define Sgl_hidden(sgl_value) Shidden(sgl_value)
#define Sgl_lowmantissa(sgl_value) Slow(sgl_value)

/* The left argument is never smaller than the right argument */
#define Sgl_subtract(sgl_left,sgl_right,sgl_result) \
    Sall(sgl_result) = Sall(sgl_left) - Sall(sgl_right)

/* Subtract right augmented with extension from left augmented with zeros and
 * store into result and extension. */
#define Sgl_subtract_withextension(left,right,extent,result)		\
    /* sgl_floating_point left,right,result; extension extent */	\
  Sgl_subtract(left,right,result);					\
  if((Extall(extent) = 0-Extall(extent)))				\
      Sall(result) = Sall(result)-1

#define Sgl_addition(sgl_left,sgl_right,sgl_result) \
    Sall(sgl_result) = Sall(sgl_left) + Sall(sgl_right)

#define Sgl_xortointp1(left,right,result)			\
    result = Sall(left) XOR Sall(right);

#define Sgl_xorfromintp1(left,right,result)			\
    Sall(result) = left XOR Sall(right)

/* Need to Initialize */
#define Sgl_makequietnan(dest)						\
    Sall(dest) = ((SGL_EMAX+SGL_BIAS)+1)<< (32-(1+SGL_EXP_LENGTH))	\
		| (1<<(32-(1+SGL_EXP_LENGTH+2)))
#define Sgl_makesignalingnan(dest)					\
    Sall(dest) = ((SGL_EMAX+SGL_BIAS)+1)<< (32-(1+SGL_EXP_LENGTH))	\
		| (1<<(32-(1+SGL_EXP_LENGTH+1)))

#define Sgl_normalize(sgl_opnd,exponent)			\
	while(Sgl_iszero_hiddenhigh7mantissa(sgl_opnd)) {	\
		Sgl_leftshiftby8(sgl_opnd);			\
		exponent -= 8;					\
	}							\
	if(Sgl_iszero_hiddenhigh3mantissa(sgl_opnd)) {		\
		Sgl_leftshiftby4(sgl_opnd);			\
		exponent -= 4;					\
	}							\
	while(Sgl_iszero_hidden(sgl_opnd)) {			\
		Sgl_leftshiftby1(sgl_opnd);			\
		exponent -= 1;					\
	}

#define Sgl_setoverflow(sgl_opnd)				\
	/* set result to infinity or largest number */		\
	switch (Rounding_mode()) {				\
		case ROUNDPLUS:					\
			if (Sgl_isone_sign(sgl_opnd)) {		\
				Sgl_setlargestnegative(sgl_opnd); \
			}					\
			else {					\
				Sgl_setinfinitypositive(sgl_opnd); \
			}					\
			break;					\
		case ROUNDMINUS:				\
			if (Sgl_iszero_sign(sgl_opnd)) {	\
				Sgl_setlargestpositive(sgl_opnd); \
			}					\
			else {					\
				Sgl_setinfinitynegative(sgl_opnd); \
			}					\
			break;					\
		case ROUNDNEAREST:				\
			Sgl_setinfinity_exponentmantissa(sgl_opnd); \
			break;					\
		case ROUNDZERO:					\
			Sgl_setlargest_exponentmantissa(sgl_opnd); \
	}

#define Sgl_denormalize(opnd,exponent,guard,sticky,inexact)		\
	Sgl_clear_signexponent_set_hidden(opnd);			\
	if (exponent >= (1 - SGL_P)) {					\
		guard = (Sall(opnd) >> (-(exponent))) & 1;		\
		if (exponent < 0) sticky |= Sall(opnd) << (32+exponent); \
		inexact = (guard) | (sticky);				\
		Sall(opnd) >>= (1-exponent);				\
	}								\
	else {								\
		guard = 0;						\
		sticky |= Sall(opnd);					\
		inexact = sticky;					\
		Sgl_setzero(opnd);					\
	}

int sgl_fadd(sgl_floating_point *, sgl_floating_point *, sgl_floating_point *, unsigned int *);
int sgl_fcmp(sgl_floating_point *, sgl_floating_point *, unsigned int, unsigned int *);
int sgl_fdiv(sgl_floating_point *, sgl_floating_point *, sgl_floating_point *, unsigned int *);
int sgl_fmpy(sgl_floating_point *, sgl_floating_point *, sgl_floating_point *, unsigned int *);
int sgl_frem(sgl_floating_point *, sgl_floating_point *, sgl_floating_point *, unsigned int *);
int sgl_fsqrt(sgl_floating_point *, sgl_floating_point *, sgl_floating_point *, unsigned int *);
int sgl_fsub(sgl_floating_point *, sgl_floating_point *, sgl_floating_point *, unsigned int *);
int sgl_frnd(sgl_floating_point *, sgl_floating_point *, sgl_floating_point *, unsigned int *);
