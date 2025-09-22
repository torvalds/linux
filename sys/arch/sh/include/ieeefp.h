/*	$OpenBSD: ieeefp.h,v 1.4 2011/04/24 16:57:11 martynas Exp $	*/
/*	$NetBSD: ieeefp.h,v 1.3 2002/04/28 17:10:34 uch Exp $ */

/*
 * Written by J.T. Conklin, Apr 6, 1995
 * Public domain.
 */

#ifndef _SH_IEEEFP_H_
#define	_SH_IEEEFP_H_

typedef int fp_except;
#define	FP_X_INV	0x10	/* invalid operation exception */
#define	FP_X_DZ		0x08	/* divide-by-zero exception */
#define	FP_X_OFL	0x04	/* overflow exception */
#define	FP_X_UFL	0x02	/* underflow exception */
#define	FP_X_IMP	0x01	/* imprecise (loss of precision) */

typedef enum {
	FP_RN=0,		/* round to nearest representable number */
	FP_RZ=1,		/* round to zero (truncate) */
	/* the following two are not implemented on SH4{,A} */
	FP_RP=2,		/* round toward positive infinity */
	FP_RM=3			/* round toward negative infinity */
} fp_rnd;

#endif /* !_SH_IEEEFP_H_ */
