/*	$OpenBSD: cnv_float.h,v 1.9 2003/04/10 17:27:58 mickey Exp $	*/
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
/* @(#)cnv_float.h: Revision: 2.9.88.1 Date: 93/12/07 15:05:29 */

/*
 * Some more constants
 */
#define SGL_FX_MAX_EXP 30
#define DBL_FX_MAX_EXP 62
#define QUAD_FX_MAX_EXP 126


#define Dintp1(object) (object)
#define Dintp2(object) (object)

#define Qintp0(object) (object)
#define Qintp1(object) (object)
#define Qintp2(object) (object)
#define Qintp3(object) (object)


/*
 * These macros will be used specifically by the convert instructions.
 *
 *
 * Single format macros
 */

#define Sgl_to_dbl_exponent(src_exponent,dest)			\
    Deposit_dexponent(dest,src_exponent+(DBL_BIAS-SGL_BIAS))

#define Sgl_to_dbl_mantissa(src_mantissa,destA,destB)	\
    Deposit_dmantissap1(destA,src_mantissa>>3);		\
    Dmantissap2(destB) = src_mantissa << 29

#define Sgl_isinexact_to_fix(sgl_value,exponent)	\
    ((exponent < (SGL_P - 1)) ?				\
     (Sall(sgl_value) << (SGL_EXP_LENGTH + 1 + exponent)) : FALSE)

#define Int_isinexact_to_sgl(int_value)	(int_value << (33 - SGL_EXP_LENGTH))

#define Sgl_roundnearest_from_int(int_value,sgl_value)			\
    if (int_value & 1<<(SGL_EXP_LENGTH - 2))   /* round bit */		\
	if ((int_value << (34 - SGL_EXP_LENGTH)) || Slow(sgl_value))	\
		Sall(sgl_value)++

#define Dint_isinexact_to_sgl(dint_valueA,dint_valueB)		\
    ((Dintp1(dint_valueA) << (33 - SGL_EXP_LENGTH)) || Dintp2(dint_valueB))

#define Sgl_roundnearest_from_dint(dint_valueA,dint_valueB,sgl_value)	\
    if (Dintp1(dint_valueA) & 1<<(SGL_EXP_LENGTH - 2))			\
	if ((Dintp1(dint_valueA) << (34 - SGL_EXP_LENGTH)) ||		\
	Dintp2(dint_valueB) || Slow(sgl_value)) Sall(sgl_value)++

#define Dint_isinexact_to_dbl(dint_value)	\
    (Dintp2(dint_value) << (33 - DBL_EXP_LENGTH))

#define Dbl_roundnearest_from_dint(dint_opndB,dbl_opndA,dbl_opndB)	\
    if (Dintp2(dint_opndB) & 1<<(DBL_EXP_LENGTH - 2))			\
       if ((Dintp2(dint_opndB) << (34 - DBL_EXP_LENGTH)) || Dlowp2(dbl_opndB))  \
          if ((++Dallp2(dbl_opndB))==0) Dallp1(dbl_opndA)++

#define Sgl_isone_roundbit(sgl_value,exponent)			\
    ((Sall(sgl_value) << (SGL_EXP_LENGTH + 1 + exponent)) >> 31)

#define Sgl_isone_stickybit(sgl_value,exponent)		\
    (exponent < (SGL_P - 2) ?				\
     Sall(sgl_value) << (SGL_EXP_LENGTH + 2 + exponent) : FALSE)


/*
 * Double format macros
 */

#define Dbl_to_sgl_exponent(src_exponent,dest)			\
    dest = src_exponent + (SGL_BIAS - DBL_BIAS)

#define Dbl_to_sgl_mantissa(srcA,srcB,dest,inexact,guard,sticky,odd)	\
    Shiftdouble(Dmantissap1(srcA),Dmantissap2(srcB),29,dest);	\
    guard = Dbit3p2(srcB);					\
    sticky = Dallp2(srcB)<<4;					\
    inexact = guard | sticky;					\
    odd = Dbit2p2(srcB)

#define Dbl_to_sgl_denormalized(srcA,srcB,exp,dest,inexact,guard,sticky,odd,tiny) \
    Deposit_dexponent(srcA,1);						\
    tiny = TRUE;							\
    if (exp >= -2) {							\
	if (exp == 0) {							\
	    inexact = Dallp2(srcB) << 3;				\
	    guard = inexact >> 31;					\
	    sticky = inexact << 1;					\
	    Shiftdouble(Dmantissap1(srcA),Dmantissap2(srcB),29,dest);	\
	    odd = dest << 31;						\
	    if (inexact) {						\
		switch(Rounding_mode()) {				\
		    case ROUNDPLUS:					\
			if (Dbl_iszero_sign(srcA)) {			\
			    dest++;					\
			    if (Sgl_isone_hidden(dest))	\
				tiny = FALSE;				\
			    dest--;					\
			}						\
			break;						\
		    case ROUNDMINUS:					\
			if (Dbl_isone_sign(srcA)) {			\
			    dest++;					\
			    if (Sgl_isone_hidden(dest))	\
				tiny = FALSE;				\
			    dest--;					\
			}						\
			break;						\
		    case ROUNDNEAREST:					\
			if (guard && (sticky || odd)) {			\
			    dest++;					\
			    if (Sgl_isone_hidden(dest))	\
				tiny = FALSE;				\
			    dest--;					\
			}						\
			break;						\
		}							\
	    }								\
		/* shift right by one to get correct result */		\
		guard = odd;						\
		sticky = inexact;					\
		inexact |= guard;					\
		dest >>= 1;						\
		Deposit_dsign(srcA,0);					\
	        Shiftdouble(Dallp1(srcA),Dallp2(srcB),30,dest);		\
	        odd = dest << 31;					\
	}								\
	else {								\
	    inexact = Dallp2(srcB) << (2 + exp);			\
	    guard = inexact >> 31;					\
	    sticky = inexact << 1;					\
	    Deposit_dsign(srcA,0);					\
	    if (exp == -2) dest = Dallp1(srcA);				\
	    else Variable_shift_double(Dallp1(srcA),Dallp2(srcB),30-exp,dest); \
	    odd = dest << 31;						\
	}								\
    }									\
    else {								\
	Deposit_dsign(srcA,0);						\
	if (exp > (1 - SGL_P)) {					\
	    dest = Dallp1(srcA) >> (- 2 - exp);				\
	    inexact = Dallp1(srcA) << (34 + exp);			\
	    guard = inexact >> 31;					\
	    sticky = (inexact << 1) | Dallp2(srcB);			\
	    inexact |= Dallp2(srcB);					\
	    odd = dest << 31;						\
	}								\
	else {								\
	    dest = 0;							\
	    inexact = Dallp1(srcA) | Dallp2(srcB);			\
	    if (exp == (1 - SGL_P)) {					\
		guard = Dhidden(srcA);					\
		sticky = Dmantissap1(srcA) | Dallp2(srcB);		\
	    }								\
	    else {							\
		guard = 0;						\
		sticky = inexact;					\
	    }								\
	    odd = 0;							\
	}								\
    }									\
    exp = 0

#define Dbl_isinexact_to_fix(dbl_valueA,dbl_valueB,exponent)		\
    (exponent < (DBL_P-33) ?						\
     Dallp2(dbl_valueB) || Dallp1(dbl_valueA) << (DBL_EXP_LENGTH+1+exponent) : \
     (exponent < (DBL_P-1) ? Dallp2(dbl_valueB) << (exponent + (33-DBL_P)) :   \
      FALSE))

#define Dbl_isoverflow_to_int(exponent,dbl_valueA,dbl_valueB)		\
    ((exponent > SGL_FX_MAX_EXP + 1) || Dsign(dbl_valueA)==0 ||		\
     Dmantissap1(dbl_valueA)!=0 || (Dallp2(dbl_valueB)>>21)!=0 )

#define Dbl_isone_roundbit(dbl_valueA,dbl_valueB,exponent)              \
    ((exponent < (DBL_P - 33) ?						\
      Dallp1(dbl_valueA) >> ((30 - DBL_EXP_LENGTH) - exponent) :	\
      Dallp2(dbl_valueB) >> ((DBL_P - 2) - exponent)) & 1)

#define Dbl_isone_stickybit(dbl_valueA,dbl_valueB,exponent)		\
    (exponent < (DBL_P-34) ?						\
     (Dallp2(dbl_valueB) || Dallp1(dbl_valueA)<<(DBL_EXP_LENGTH+2+exponent)) : \
     (exponent<(DBL_P-2) ? (Dallp2(dbl_valueB) << (exponent + (34-DBL_P))) : \
      FALSE))


/* Int macros */

#define Int_from_sgl_mantissa(sgl_value,exponent)	\
    Sall(sgl_value) =				\
	(unsigned)(Sall(sgl_value) << SGL_EXP_LENGTH)>>(31 - exponent)

#define Int_from_dbl_mantissa(dbl_valueA,dbl_valueB,exponent)	\
    Shiftdouble(Dallp1(dbl_valueA),Dallp2(dbl_valueB),22,Dallp1(dbl_valueA)); \
    if (exponent < 31) Dallp1(dbl_valueA) >>= 30 - exponent;	\
    else Dallp1(dbl_valueA) <<= 1

#define Int_negate(int_value) int_value = -int_value


/* Dint macros */

#define Dint_from_sgl_mantissa(sgl_value,exponent,dresultA,dresultB)	\
    {Sall(sgl_value) <<= SGL_EXP_LENGTH;  /*  left-justify  */		\
    if (exponent <= 31) {						\
	Dintp1(dresultA) = 0;						\
	Dintp2(dresultB) = (unsigned)Sall(sgl_value) >> (31 - exponent); \
    }									\
    else {								\
	Dintp1(dresultA) = Sall(sgl_value) >> (63 - exponent);		\
	Dintp2(dresultB) = Sall(sgl_value) << (exponent - 31);		\
    }}


#define Dint_from_dbl_mantissa(dbl_valueA,dbl_valueB,exponent,destA,destB) \
    {if (exponent < 32) {						\
	Dintp1(destA) = 0;						\
	if (exponent <= 20)						\
	    Dintp2(destB) = Dallp1(dbl_valueA) >> (20-exponent);	\
	else Variable_shift_double(Dallp1(dbl_valueA),Dallp2(dbl_valueB), \
	     52-exponent,Dintp2(destB));					\
    }									\
    else {								\
	if (exponent <= 52) {						\
	    Dintp1(destA) = Dallp1(dbl_valueA) >> (52-exponent);	\
	    if (exponent == 52) Dintp2(destB) = Dallp2(dbl_valueB);	\
	    else Variable_shift_double(Dallp1(dbl_valueA),Dallp2(dbl_valueB), \
	    52-exponent,Dintp2(destB));					\
        }								\
	else {								\
	    Variable_shift_double(Dallp1(dbl_valueA),Dallp2(dbl_valueB), \
	    84-exponent,Dintp1(destA));					\
	    Dintp2(destB) = Dallp2(dbl_valueB) << (exponent-52);	\
	}								\
    }}

#define Dint_setzero(dresultA,dresultB)		\
    Dintp1(dresultA) = 0;			\
    Dintp2(dresultB) = 0

#define Dint_setone_sign(dresultA,dresultB)		\
    Dintp1(dresultA) = ~Dintp1(dresultA);		\
    if ((Dintp2(dresultB) = -Dintp2(dresultB)) == 0) Dintp1(dresultA)++

#define Dint_set_minint(dresultA,dresultB)		\
    Dintp1(dresultA) = 1<<31;				\
    Dintp2(dresultB) = 0

#define Dint_isone_lowp2(dresultB)  (Dintp2(dresultB) & 01)

#define Dint_increment(dresultA,dresultB)		\
    if ((++Dintp2(dresultB))==0) Dintp1(dresultA)++

#define Dint_decrement(dresultA,dresultB)		\
    if ((Dintp2(dresultB)--)==0) Dintp1(dresultA)--

#define Dint_negate(dresultA,dresultB)			\
    Dintp1(dresultA) = ~Dintp1(dresultA);		\
    if ((Dintp2(dresultB) = -Dintp2(dresultB))==0) Dintp1(dresultA)++

#define Dint_copyfromptr(src,destA,destB) \
     Dintp1(destA) = src->wd0;		\
     Dintp2(destB) = src->wd1
#define Dint_copytoptr(srcA,srcB,dest)	\
    dest->wd0 = Dintp1(srcA);		\
    dest->wd1 = Dintp2(srcB)


/* other macros  */

#define Find_ms_one_bit(value, position)	\
    {						\
	int var;				\
	for (var = 8; var >= 1; var >>= 1) {	\
	    if (value >> (32 - position))	\
		position -= var;		\
		else position += var;		\
	}					\
	if ((value >> (32 - position)) == 0)	\
	    position--;				\
	else position -= 2;			\
    }

int sgl_to_sgl_fcnvfx(sgl_floating_point *, sgl_floating_point *, sgl_floating_point *, unsigned int *);
int sgl_to_dbl_fcnvfx(sgl_floating_point *, sgl_floating_point *, dbl_integer *, unsigned int *);
int dbl_to_sgl_fcnvfx(dbl_floating_point *, dbl_floating_point *, int *, unsigned int *);
int dbl_to_dbl_fcnvfx(dbl_floating_point *, dbl_floating_point *, dbl_integer *, unsigned int *);

int sgl_to_sgl_fcnvfxt(sgl_floating_point *, sgl_floating_point *, int *, unsigned int *);
int sgl_to_dbl_fcnvfxt(sgl_floating_point *, sgl_floating_point *, dbl_integer *, unsigned int *);
int dbl_to_sgl_fcnvfxt(dbl_floating_point *, dbl_floating_point *, int *, unsigned int *);
int dbl_to_dbl_fcnvfxt(dbl_floating_point *, dbl_floating_point *, dbl_integer *, unsigned int *);

int sgl_to_sgl_fcnvxf(int *, int *, sgl_floating_point *, unsigned int *);
int sgl_to_dbl_fcnvxf(int *, int *, dbl_floating_point *, unsigned int *);
int dbl_to_sgl_fcnvxf(dbl_integer *, dbl_integer *, sgl_floating_point *, unsigned int *);
int dbl_to_dbl_fcnvxf(dbl_integer *, dbl_integer *, dbl_floating_point *, unsigned int *);
