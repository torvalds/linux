/*	$OpenBSD: dbl_float.h,v 1.13 2021/03/11 11:16:57 jsg Exp $	*/
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
/* @(#)dbl_float.h: Revision: 2.9.88.1 Date: 93/12/07 15:05:32 */

/**************************************
 * Declare double precision functions *
 **************************************/

/* 32-bit word grabbing functions */
#define Dbl_firstword(value) Dallp1(value)
#define Dbl_secondword(value) Dallp2(value)
#define Dbl_thirdword(value) dummy_location
#define Dbl_fourthword(value) dummy_location

#define Dbl_sign(object) Dsign(object)
#define Dbl_exponent(object) Dexponent(object)
#define Dbl_signexponent(object) Dsignexponent(object)
#define Dbl_mantissap1(object) Dmantissap1(object)
#define Dbl_mantissap2(object) Dmantissap2(object)
#define Dbl_exponentmantissap1(object) Dexponentmantissap1(object)
#define Dbl_allp1(object) Dallp1(object)
#define Dbl_allp2(object) Dallp2(object)

/* dbl_and_signs ands the sign bits of each argument and puts the result
 * into the first argument. dbl_or_signs ors those same sign bits */
#define Dbl_and_signs( src1dst, src2)		\
    Dallp1(src1dst) = (Dallp1(src2)|~(1<<31)) & Dallp1(src1dst)
#define Dbl_or_signs( src1dst, src2)		\
    Dallp1(src1dst) = (Dallp1(src2)&(1<<31)) | Dallp1(src1dst)

/* The hidden bit is always the low bit of the exponent */
#define Dbl_clear_exponent_set_hidden(srcdst) Deposit_dexponent(srcdst,1)
#define Dbl_clear_signexponent_set_hidden(srcdst) \
    Deposit_dsignexponent(srcdst,1)
#define Dbl_clear_sign(srcdst) Dallp1(srcdst) &= ~(1<<31)
#define Dbl_clear_signexponent(srcdst) \
    Dallp1(srcdst) &= Dmantissap1((unsigned)-1)

/* Exponent field for doubles has already been cleared and may be
 * included in the shift.  Here we need to generate two double width
 * variable shifts.  The insignificant bits can be ignored.
 *      MTSAR f(varamount)
 *      VSHD	srcdst.high,srcdst.low => srcdst.low
 *	VSHD	0,srcdst.high => srcdst.high
 * This is very difficult to model with C expressions since the shift amount
 * could exceed 32.  */
/* varamount must be less than 64 */
#define Dbl_rightshift(srcdstA, srcdstB, varamount)			\
    {if((varamount) >= 32) {						\
	Dallp2(srcdstB) = Dallp1(srcdstA) >> (varamount-32);		\
	Dallp1(srcdstA)=0;						\
    }									\
    else if(varamount > 0) {						\
	Variable_shift_double(Dallp1(srcdstA), Dallp2(srcdstB),		\
	  (varamount), Dallp2(srcdstB));				\
	Dallp1(srcdstA) >>= varamount;					\
    } }
/* varamount must be less than 64 */
#define Dbl_rightshift_exponentmantissa(srcdstA, srcdstB, varamount)	\
    {if((varamount) >= 32) {						\
	Dallp2(srcdstB) = Dexponentmantissap1(srcdstA) >> ((varamount)-32); \
	Dallp1(srcdstA) &= (1<<31);  /* clear exponentmantissa field */ \
    }									\
    else if(varamount > 0) {						\
	Variable_shift_double(Dexponentmantissap1(srcdstA), Dallp2(srcdstB), \
	(varamount), Dallp2(srcdstB));					\
	Deposit_dexponentmantissap1(srcdstA,				\
	    (Dexponentmantissap1(srcdstA)>>(varamount)));			\
    } }
/* varamount must be less than 64 */
#define Dbl_leftshift(srcdstA, srcdstB, varamount)			\
    {if((varamount) >= 32) {						\
	Dallp1(srcdstA) = Dallp2(srcdstB) << (varamount-32);		\
	Dallp2(srcdstB)=0;						\
    }									\
    else {								\
	if ((varamount) > 0) {						\
	    Dallp1(srcdstA) = (Dallp1(srcdstA) << (varamount)) |	\
		(Dallp2(srcdstB) >> (32-(varamount)));			\
	    Dallp2(srcdstB) <<= varamount;				\
	}								\
    } }
#define Dbl_leftshiftby1_withextent(lefta,leftb,right,resulta,resultb)	\
    Shiftdouble(Dallp1(lefta), Dallp2(leftb), 31, Dallp1(resulta));	\
    Shiftdouble(Dallp2(leftb), Extall(right), 31, Dallp2(resultb))

#define Dbl_rightshiftby1_withextent(leftb,right,dst)		\
    Extall(dst) = (Dallp2(leftb) << 31) | ((unsigned)Extall(right) >> 1) | \
		  Extlow(right)

#define Dbl_arithrightshiftby1(srcdstA,srcdstB)			\
    Shiftdouble(Dallp1(srcdstA),Dallp2(srcdstB),1,Dallp2(srcdstB));\
    Dallp1(srcdstA) = (int)Dallp1(srcdstA) >> 1

/* Sign extend the sign bit with an integer destination */
#define Dbl_signextendedsign(value)  Dsignedsign(value)

#define Dbl_isone_hidden(dbl_value) (Is_dhidden(dbl_value)!=0)
/* Singles and doubles may include the sign and exponent fields.  The
 * hidden bit and the hidden overflow must be included. */
#define Dbl_increment(dbl_valueA,dbl_valueB) \
    if( (Dallp2(dbl_valueB) += 1) == 0 )  Dallp1(dbl_valueA) += 1
#define Dbl_increment_mantissa(dbl_valueA,dbl_valueB) \
    if( (Dmantissap2(dbl_valueB) += 1) == 0 )  \
    Deposit_dmantissap1(dbl_valueA,dbl_valueA+1)
#define Dbl_decrement(dbl_valueA,dbl_valueB) \
    if( Dallp2(dbl_valueB) == 0 )  Dallp1(dbl_valueA) -= 1; \
    Dallp2(dbl_valueB) -= 1

#define Dbl_isone_sign(dbl_value) (Is_dsign(dbl_value)!=0)
#define Dbl_isone_hiddenoverflow(dbl_value) (Is_dhiddenoverflow(dbl_value)!=0)
#define Dbl_isone_lowmantissap1(dbl_valueA) (Is_dlowp1(dbl_valueA)!=0)
#define Dbl_isone_lowmantissap2(dbl_valueB) (Is_dlowp2(dbl_valueB)!=0)
#define Dbl_isone_signaling(dbl_value) (Is_dsignaling(dbl_value)!=0)
#define Dbl_is_signalingnan(dbl_value) (Dsignalingnan(dbl_value)==0xfff)
#define Dbl_isnotzero(dbl_valueA,dbl_valueB) \
    (Dallp1(dbl_valueA) || Dallp2(dbl_valueB))
#define Dbl_isnotzero_hiddenhigh7mantissa(dbl_value) \
    (Dhiddenhigh7mantissa(dbl_value)!=0)
#define Dbl_isnotzero_exponent(dbl_value) (Dexponent(dbl_value)!=0)
#define Dbl_isnotzero_mantissa(dbl_valueA,dbl_valueB) \
    (Dmantissap1(dbl_valueA) || Dmantissap2(dbl_valueB))
#define Dbl_isnotzero_mantissap1(dbl_valueA) (Dmantissap1(dbl_valueA)!=0)
#define Dbl_isnotzero_mantissap2(dbl_valueB) (Dmantissap2(dbl_valueB)!=0)
#define Dbl_isnotzero_exponentmantissa(dbl_valueA,dbl_valueB) \
    (Dexponentmantissap1(dbl_valueA) || Dmantissap2(dbl_valueB))
#define Dbl_isnotzero_low4p2(dbl_value) (Dlow4p2(dbl_value)!=0)
#define Dbl_iszero(dbl_valueA,dbl_valueB) (Dallp1(dbl_valueA)==0 && \
    Dallp2(dbl_valueB)==0)
#define Dbl_iszero_allp1(dbl_value) (Dallp1(dbl_value)==0)
#define Dbl_iszero_allp2(dbl_value) (Dallp2(dbl_value)==0)
#define Dbl_iszero_hidden(dbl_value) (Is_dhidden(dbl_value)==0)
#define Dbl_iszero_hiddenoverflow(dbl_value) (Is_dhiddenoverflow(dbl_value)==0)
#define Dbl_iszero_hiddenhigh3mantissa(dbl_value) \
    (Dhiddenhigh3mantissa(dbl_value)==0)
#define Dbl_iszero_hiddenhigh7mantissa(dbl_value) \
    (Dhiddenhigh7mantissa(dbl_value)==0)
#define Dbl_iszero_sign(dbl_value) (Is_dsign(dbl_value)==0)
#define Dbl_iszero_exponent(dbl_value) (Dexponent(dbl_value)==0)
#define Dbl_iszero_mantissa(dbl_valueA,dbl_valueB) \
    (Dmantissap1(dbl_valueA)==0 && Dmantissap2(dbl_valueB)==0)
#define Dbl_iszero_exponentmantissa(dbl_valueA,dbl_valueB) \
    (Dexponentmantissap1(dbl_valueA)==0 && Dmantissap2(dbl_valueB)==0)
#define Dbl_isinfinity_exponent(dbl_value)		\
    (Dexponent(dbl_value)==DBL_INFINITY_EXPONENT)
#define Dbl_isnotinfinity_exponent(dbl_value)		\
    (Dexponent(dbl_value)!=DBL_INFINITY_EXPONENT)
#define Dbl_isinfinity(dbl_valueA,dbl_valueB)			\
    (Dexponent(dbl_valueA)==DBL_INFINITY_EXPONENT &&	\
    Dmantissap1(dbl_valueA)==0 && Dmantissap2(dbl_valueB)==0)
#define Dbl_isnan(dbl_valueA,dbl_valueB)		\
    (Dexponent(dbl_valueA)==DBL_INFINITY_EXPONENT &&	\
    (Dmantissap1(dbl_valueA)!=0 || Dmantissap2(dbl_valueB)!=0))
#define Dbl_isnotnan(dbl_valueA,dbl_valueB)		\
    (Dexponent(dbl_valueA)!=DBL_INFINITY_EXPONENT ||	\
    (Dmantissap1(dbl_valueA)==0 && Dmantissap2(dbl_valueB)==0))

#define Dbl_islessthan(dbl_op1a,dbl_op1b,dbl_op2a,dbl_op2b)	\
    (Dallp1(dbl_op1a) < Dallp1(dbl_op2a) ||			\
     (Dallp1(dbl_op1a) == Dallp1(dbl_op2a) &&			\
      Dallp2(dbl_op1b) < Dallp2(dbl_op2b)))
#define Dbl_isgreaterthan(dbl_op1a,dbl_op1b,dbl_op2a,dbl_op2b)	\
    (Dallp1(dbl_op1a) > Dallp1(dbl_op2a) ||			\
     (Dallp1(dbl_op1a) == Dallp1(dbl_op2a) &&			\
      Dallp2(dbl_op1b) > Dallp2(dbl_op2b)))
#define Dbl_isnotlessthan(dbl_op1a,dbl_op1b,dbl_op2a,dbl_op2b)	\
    (Dallp1(dbl_op1a) > Dallp1(dbl_op2a) ||			\
     (Dallp1(dbl_op1a) == Dallp1(dbl_op2a) &&			\
      Dallp2(dbl_op1b) >= Dallp2(dbl_op2b)))
#define Dbl_isnotgreaterthan(dbl_op1a,dbl_op1b,dbl_op2a,dbl_op2b) \
    (Dallp1(dbl_op1a) < Dallp1(dbl_op2a) ||			\
     (Dallp1(dbl_op1a) == Dallp1(dbl_op2a) &&			\
      Dallp2(dbl_op1b) <= Dallp2(dbl_op2b)))
#define Dbl_isequal(dbl_op1a,dbl_op1b,dbl_op2a,dbl_op2b)	\
     ((Dallp1(dbl_op1a) == Dallp1(dbl_op2a)) &&			\
      (Dallp2(dbl_op1b) == Dallp2(dbl_op2b)))

#define Dbl_leftshiftby8(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),24,Dallp1(dbl_valueA)); \
    Dallp2(dbl_valueB) <<= 8
#define Dbl_leftshiftby7(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),25,Dallp1(dbl_valueA)); \
    Dallp2(dbl_valueB) <<= 7
#define Dbl_leftshiftby4(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),28,Dallp1(dbl_valueA)); \
    Dallp2(dbl_valueB) <<= 4
#define Dbl_leftshiftby3(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),29,Dallp1(dbl_valueA)); \
    Dallp2(dbl_valueB) <<= 3
#define Dbl_leftshiftby2(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),30,Dallp1(dbl_valueA)); \
    Dallp2(dbl_valueB) <<= 2
#define Dbl_leftshiftby1(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),31,Dallp1(dbl_valueA)); \
    Dallp2(dbl_valueB) <<= 1

#define Dbl_rightshiftby8(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),8,Dallp2(dbl_valueB)); \
    Dallp1(dbl_valueA) >>= 8
#define Dbl_rightshiftby4(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),4,Dallp2(dbl_valueB)); \
    Dallp1(dbl_valueA) >>= 4
#define Dbl_rightshiftby2(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),2,Dallp2(dbl_valueB)); \
    Dallp1(dbl_valueA) >>= 2
#define Dbl_rightshiftby1(dbl_valueA,dbl_valueB) \
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),1,Dallp2(dbl_valueB)); \
    Dallp1(dbl_valueA) >>= 1

/* This magnitude comparison uses the signless first words and
 * the regular part2 words.  The comparison is graphically:
 *
 *       1st greater?  -------------
 *				   |
 *       1st less?-----------------+---------
 *				   |	    |
 *       2nd greater or equal----->|	    |
 *				 False     True
 */
#define Dbl_ismagnitudeless(leftB,rightB,signlessleft,signlessright)	\
      ((signlessleft <= signlessright) &&				\
       ( (signlessleft < signlessright) || (Dallp2(leftB)<Dallp2(rightB)) ))

#define Dbl_copytoint_exponentmantissap1(src,dest) \
    dest = Dexponentmantissap1(src)

/* A quiet NaN has the high mantissa bit clear and at least on other (in this
 * case the adjacent bit) bit set. */
#define Dbl_set_quiet(dbl_value) Deposit_dhigh2mantissa(dbl_value,1)
#define Dbl_set_exponent(dbl_value, exp) Deposit_dexponent(dbl_value,exp)

#define Dbl_set_mantissa(desta,destb,valuea,valueb)	\
    Deposit_dmantissap1(desta,valuea);			\
    Dmantissap2(destb) = Dmantissap2(valueb)
#define Dbl_set_mantissap1(desta,valuea)		\
    Deposit_dmantissap1(desta,valuea)
#define Dbl_set_mantissap2(destb,valueb)		\
    Dmantissap2(destb) = Dmantissap2(valueb)

#define Dbl_set_exponentmantissa(desta,destb,valuea,valueb)	\
    Deposit_dexponentmantissap1(desta,valuea);			\
    Dmantissap2(destb) = Dmantissap2(valueb)
#define Dbl_set_exponentmantissap1(dest,value)			\
    Deposit_dexponentmantissap1(dest,value)

#define Dbl_copyfromptr(src,desta,destb) \
    Dallp1(desta) = src->wd0;		\
    Dallp2(destb) = src->wd1
#define Dbl_copytoptr(srca,srcb,dest)	\
    dest->wd0 = Dallp1(srca);		\
    dest->wd1 = Dallp2(srcb)

/*  An infinity is represented with the max exponent and a zero mantissa */
#define Dbl_setinfinity_exponent(dbl_value) \
    Deposit_dexponent(dbl_value,DBL_INFINITY_EXPONENT)
#define Dbl_setinfinity_exponentmantissa(dbl_valueA,dbl_valueB)	\
    Deposit_dexponentmantissap1(dbl_valueA,			\
    (DBL_INFINITY_EXPONENT << (32-(1+DBL_EXP_LENGTH))));	\
    Dmantissap2(dbl_valueB) = 0
#define Dbl_setinfinitypositive(dbl_valueA,dbl_valueB)		\
    Dallp1(dbl_valueA)						\
	= (DBL_INFINITY_EXPONENT << (32-(1+DBL_EXP_LENGTH)));	\
    Dmantissap2(dbl_valueB) = 0
#define Dbl_setinfinitynegative(dbl_valueA,dbl_valueB)		\
    Dallp1(dbl_valueA) = (1<<31) |				\
	(DBL_INFINITY_EXPONENT << (32-(1+DBL_EXP_LENGTH)));	\
    Dmantissap2(dbl_valueB) = 0
#define Dbl_setinfinity(dbl_valueA,dbl_valueB,sign)		\
    Dallp1(dbl_valueA) = (sign << 31) |				\
	(DBL_INFINITY_EXPONENT << (32-(1+DBL_EXP_LENGTH)));	\
    Dmantissap2(dbl_valueB) = 0

#define Dbl_sethigh4bits(dbl_value, extsign) Deposit_dhigh4p1(dbl_value,extsign)
#define Dbl_set_sign(dbl_value,sign) Deposit_dsign(dbl_value,sign)
#define Dbl_invert_sign(dbl_value) Deposit_dsign(dbl_value,~Dsign(dbl_value))
#define Dbl_setone_sign(dbl_value) Deposit_dsign(dbl_value,1)
#define Dbl_setone_lowmantissap2(dbl_value) Deposit_dlowp2(dbl_value,1)
#define Dbl_setzero_sign(dbl_value) Dallp1(dbl_value) &= 0x7fffffff
#define Dbl_setzero_exponent(dbl_value)			\
    Dallp1(dbl_value) &= 0x800fffff
#define Dbl_setzero_mantissa(dbl_valueA,dbl_valueB)	\
    Dallp1(dbl_valueA) &= 0xfff00000;			\
    Dallp2(dbl_valueB) = 0
#define Dbl_setzero_mantissap1(dbl_value) Dallp1(dbl_value) &= 0xfff00000
#define Dbl_setzero_mantissap2(dbl_value) Dallp2(dbl_value) = 0
#define Dbl_setzero_exponentmantissa(dbl_valueA,dbl_valueB)	\
    Dallp1(dbl_valueA) &= 0x80000000;		\
    Dallp2(dbl_valueB) = 0
#define Dbl_setzero_exponentmantissap1(dbl_valueA)	\
    Dallp1(dbl_valueA) &= 0x80000000
#define Dbl_setzero(dbl_valueA,dbl_valueB) \
    Dallp1(dbl_valueA) = 0; Dallp2(dbl_valueB) = 0
#define Dbl_setzerop1(dbl_value) Dallp1(dbl_value) = 0
#define Dbl_setzerop2(dbl_value) Dallp2(dbl_value) = 0
#define Dbl_setnegativezero(dbl_value) \
    Dallp1(dbl_value) = 1U << 31; Dallp2(dbl_value) = 0
#define Dbl_setnegativezerop1(dbl_value) Dallp1(dbl_value) = 1U << 31

/* Use the following macro for both overflow & underflow conditions */
#define ovfl -
#define unfl +
#define Dbl_setwrapped_exponent(dbl_value,exponent,op) \
    Deposit_dexponent(dbl_value,(exponent op DBL_WRAP))

#define Dbl_setlargestpositive(dbl_valueA,dbl_valueB)			\
    Dallp1(dbl_valueA) = ((DBL_EMAX+DBL_BIAS) << (32-(1+DBL_EXP_LENGTH))) \
			| ((1<<(32-(1+DBL_EXP_LENGTH))) - 1 );		\
    Dallp2(dbl_valueB) = 0xFFFFFFFF
#define Dbl_setlargestnegative(dbl_valueA,dbl_valueB)			\
    Dallp1(dbl_valueA) = ((DBL_EMAX+DBL_BIAS) << (32-(1+DBL_EXP_LENGTH))) \
			| ((1<<(32-(1+DBL_EXP_LENGTH))) - 1 ) | (1<<31); \
    Dallp2(dbl_valueB) = 0xFFFFFFFF
#define Dbl_setlargest_exponentmantissa(dbl_valueA,dbl_valueB)		\
    Deposit_dexponentmantissap1(dbl_valueA,				\
	(((DBL_EMAX+DBL_BIAS) << (32-(1+DBL_EXP_LENGTH)))		\
			| ((1<<(32-(1+DBL_EXP_LENGTH))) - 1 )));	\
    Dallp2(dbl_valueB) = 0xFFFFFFFF

#define Dbl_setnegativeinfinity(dbl_valueA,dbl_valueB)			\
    Dallp1(dbl_valueA) = ((1<<DBL_EXP_LENGTH) | DBL_INFINITY_EXPONENT)	\
			 << (32-(1+DBL_EXP_LENGTH)) ;			\
    Dallp2(dbl_valueB) = 0
#define Dbl_setlargest(dbl_valueA,dbl_valueB,sign)			\
    Dallp1(dbl_valueA) = (sign << 31) |					\
	((DBL_EMAX+DBL_BIAS) << (32-(1+DBL_EXP_LENGTH))) |		\
	 ((1 << (32-(1+DBL_EXP_LENGTH))) - 1 );				\
    Dallp2(dbl_valueB) = 0xFFFFFFFF


/* The high bit is always zero so arithmetic or logical shifts will work. */
#define Dbl_right_align(srcdstA,srcdstB,shift,extent)			\
    if( shift >= 32 )							\
	{								\
	/* Big shift requires examining the portion shift off		\
	the end to properly set inexact.  */				\
	if(shift < 64)							\
	    {								\
	    if(shift > 32)						\
		{							\
		Variable_shift_double(Dallp1(srcdstA),Dallp2(srcdstB),	\
		 shift-32, Extall(extent));				\
		if(Dallp2(srcdstB) << (64 - (shift))) Ext_setone_low(extent); \
		}							\
	    else Extall(extent) = Dallp2(srcdstB);			\
	    Dallp2(srcdstB) = Dallp1(srcdstA) >> (shift - 32);		\
	    }								\
	else								\
	    {								\
	    Extall(extent) = Dallp1(srcdstA);				\
	    if(Dallp2(srcdstB)) Ext_setone_low(extent);			\
	    Dallp2(srcdstB) = 0;					\
	    }								\
	Dallp1(srcdstA) = 0;						\
	}								\
    else								\
	{								\
	/* Small alignment is simpler.  Extension is easily set. */	\
	if (shift > 0)							\
	    {								\
	    Extall(extent) = Dallp2(srcdstB) << (32 - (shift));		\
	    Variable_shift_double(Dallp1(srcdstA),Dallp2(srcdstB),shift, \
	     Dallp2(srcdstB));						\
	    Dallp1(srcdstA) >>= shift;					\
	    }								\
	else Extall(extent) = 0;					\
	}

/*
 * Here we need to shift the result right to correct for an overshift
 * (due to the exponent becoming negative) during normalization.
 */
#define Dbl_fix_overshift(srcdstA,srcdstB,shift,extent)			\
	    Extall(extent) = Dallp2(srcdstB) << (32 - (shift));		\
	    Dallp2(srcdstB) = (Dallp1(srcdstA) << (32 - (shift))) |	\
		(Dallp2(srcdstB) >> (shift));				\
	    Dallp1(srcdstA) = Dallp1(srcdstA) >> shift

#define Dbl_hiddenhigh3mantissa(dbl_value) Dhiddenhigh3mantissa(dbl_value)
#define Dbl_hidden(dbl_value) Dhidden(dbl_value)
#define Dbl_lowmantissap2(dbl_value) Dlowp2(dbl_value)

/* The left argument is never smaller than the right argument */
#define Dbl_subtract(lefta,leftb,righta,rightb,resulta,resultb)			\
    if( Dallp2(rightb) > Dallp2(leftb) ) Dallp1(lefta)--;	\
    Dallp2(resultb) = Dallp2(leftb) - Dallp2(rightb);		\
    Dallp1(resulta) = Dallp1(lefta) - Dallp1(righta)

/* Subtract right augmented with extension from left augmented with zeros and
 * store into result and extension. */
#define Dbl_subtract_withextension(lefta,leftb,righta,rightb,extent,resulta,resultb)	\
    Dbl_subtract(lefta,leftb,righta,rightb,resulta,resultb);		\
    if( (Extall(extent) = 0-Extall(extent)) )				\
	{								\
	if((Dallp2(resultb)--) == 0) Dallp1(resulta)--;			\
	}

#define Dbl_addition(lefta,leftb,righta,rightb,resulta,resultb)		\
    /* If the sum of the low words is less than either source, then	\
     * an overflow into the next word occurred. */			\
    Dallp1(resulta) = Dallp1(lefta) + Dallp1(righta);			\
    if((Dallp2(resultb) = Dallp2(leftb) + Dallp2(rightb)) < Dallp2(rightb)) \
	Dallp1(resulta)++

#define Dbl_xortointp1(left,right,result)			\
    result = Dallp1(left) XOR Dallp1(right)

#define Dbl_xorfromintp1(left,right,result)			\
    Dallp1(result) = left XOR Dallp1(right)

#define Dbl_swap_lower(left,right)				\
    Dallp2(left)  = Dallp2(left) XOR Dallp2(right);		\
    Dallp2(right) = Dallp2(left) XOR Dallp2(right);		\
    Dallp2(left)  = Dallp2(left) XOR Dallp2(right)

/* Need to Initialize */
#define Dbl_makequietnan(desta,destb)					\
    Dallp1(desta) = ((DBL_EMAX+DBL_BIAS)+1)<< (32-(1+DBL_EXP_LENGTH))	\
		| (1<<(32-(1+DBL_EXP_LENGTH+2)));			\
    Dallp2(destb) = 0
#define Dbl_makesignalingnan(desta,destb)				\
    Dallp1(desta) = ((DBL_EMAX+DBL_BIAS)+1)<< (32-(1+DBL_EXP_LENGTH))	\
		| (1<<(32-(1+DBL_EXP_LENGTH+1)));			\
    Dallp2(destb) = 0

#define Dbl_normalize(dbl_opndA,dbl_opndB,exponent)			\
	while(Dbl_iszero_hiddenhigh7mantissa(dbl_opndA)) {		\
		Dbl_leftshiftby8(dbl_opndA,dbl_opndB);			\
		exponent -= 8;						\
	}								\
	if(Dbl_iszero_hiddenhigh3mantissa(dbl_opndA)) {			\
		Dbl_leftshiftby4(dbl_opndA,dbl_opndB);			\
		exponent -= 4;						\
	}								\
	while(Dbl_iszero_hidden(dbl_opndA)) {				\
		Dbl_leftshiftby1(dbl_opndA,dbl_opndB);			\
		exponent -= 1;						\
	}

#define Twoword_add(src1dstA,src1dstB,src2A,src2B)		\
	/*							\
	 * want this macro to generate:				\
	 *	ADD	src1dstB,src2B,src1dstB;		\
	 *	ADDC	src1dstA,src2A,src1dstA;		\
	 */							\
	if ((src1dstB) + (src2B) < (src1dstB)) Dallp1(src1dstA)++; \
	Dallp1(src1dstA) += (src2A);				\
	Dallp2(src1dstB) += (src2B)

#define Twoword_subtract(src1dstA,src1dstB,src2A,src2B)		\
	/*							\
	 * want this macro to generate:				\
	 *	SUB	src1dstB,src2B,src1dstB;		\
	 *	SUBB	src1dstA,src2A,src1dstA;		\
	 */							\
	if ((src1dstB) < (src2B)) Dallp1(src1dstA)--;		\
	Dallp1(src1dstA) -= (src2A);				\
	Dallp2(src1dstB) -= (src2B)

#define Dbl_setoverflow(resultA,resultB)				\
	/* set result to infinity or largest number */			\
	switch (Rounding_mode()) {					\
		case ROUNDPLUS:						\
			if (Dbl_isone_sign(resultA)) {			\
				Dbl_setlargestnegative(resultA,resultB); \
			}						\
			else {						\
				Dbl_setinfinitypositive(resultA,resultB); \
			}						\
			break;						\
		case ROUNDMINUS:					\
			if (Dbl_iszero_sign(resultA)) {			\
				Dbl_setlargestpositive(resultA,resultB); \
			}						\
			else {						\
				Dbl_setinfinitynegative(resultA,resultB); \
			}						\
			break;						\
		case ROUNDNEAREST:					\
			Dbl_setinfinity_exponentmantissa(resultA,resultB); \
			break;						\
		case ROUNDZERO:						\
			Dbl_setlargest_exponentmantissa(resultA,resultB); \
	}

#define Dbl_denormalize(opndp1,opndp2,exponent,guard,sticky,inexact)	\
    Dbl_clear_signexponent_set_hidden(opndp1);				\
    if (exponent >= (1-DBL_P)) {					\
	if (exponent >= -31) {						\
	    guard = (Dallp2(opndp2) >> (-(exponent))) & 1;		\
	    if (exponent < 0) sticky |= Dallp2(opndp2) << (32+exponent); \
	    if (exponent > -31) {					\
		Variable_shift_double(opndp1,opndp2,1-exponent,opndp2);	\
		Dallp1(opndp1) >>= 1-exponent;				\
	    }								\
	    else {							\
		Dallp2(opndp2) = Dallp1(opndp1);			\
		Dbl_setzerop1(opndp1);					\
	    }								\
	}								\
	else {								\
	    guard = (Dallp1(opndp1) >> (-32-(exponent))) & 1;		\
	    if (exponent == -32) sticky |= Dallp2(opndp2);		\
	    else sticky |= (Dallp2(opndp2) | Dallp1(opndp1) << (64+(exponent))); \
	    Dallp2(opndp2) = Dallp1(opndp1) >> (-31-(exponent));	\
	    Dbl_setzerop1(opndp1);					\
	}								\
	inexact = guard | sticky;					\
    }									\
    else {								\
	guard = 0;							\
	sticky |= (Dallp1(opndp1) | Dallp2(opndp2));			\
	Dbl_setzero(opndp1,opndp2);					\
	inexact = sticky;						\
    }


int dbl_fadd(dbl_floating_point *, dbl_floating_point *, dbl_floating_point *, unsigned int *);
int dbl_fcmp(dbl_floating_point *, dbl_floating_point *, unsigned int, unsigned int *);
int dbl_fdiv(dbl_floating_point *, dbl_floating_point *, dbl_floating_point *, unsigned int *);
int dbl_fmpy(dbl_floating_point *, dbl_floating_point *, dbl_floating_point *, unsigned int *);
int dbl_frem(dbl_floating_point *, dbl_floating_point *, dbl_floating_point *, unsigned int *);
int dbl_fsqrt(dbl_floating_point *, dbl_floating_point *, dbl_floating_point *, unsigned int *);
int dbl_fsub(dbl_floating_point *, dbl_floating_point *, dbl_floating_point *, unsigned int *);

int sgl_to_dbl_fcnvff(sgl_floating_point *, sgl_floating_point *, dbl_floating_point *, unsigned int *);
int dbl_to_sgl_fcnvff(dbl_floating_point *, dbl_floating_point *, sgl_floating_point *, unsigned int *);

int dbl_frnd(dbl_floating_point *, dbl_floating_point *, dbl_floating_point *, unsigned int *);
