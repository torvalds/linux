/*	$OpenBSD: ieeefp.h,v 1.2 2021/05/01 20:07:01 kettenis Exp $	*/

/*
 * Based on ieeefp.h written by J.T. Conklin, Apr 28, 1995
 * Public domain.
 */

#ifndef _MACHINE_IEEEFP_H_
#define _MACHINE_IEEEFP_H_

typedef int fp_except;
#define	FP_X_INV	0x01	/* invalid operation exception */
#define	FP_X_DZ		0x02	/* divide-by-zero exception */
#define	FP_X_OFL	0x04	/* overflow exception */
#define	FP_X_UFL	0x08	/* underflow exception */
#define	FP_X_IMP	0x10	/* imprecise (loss of precision) */

typedef enum {
    FP_RN=0,			/* round to nearest representable number */
    FP_RP=1,			/* round toward positive infinity */
    FP_RM=2,			/* round toward negative infinity */
    FP_RZ=3			/* round to zero (truncate) */
} fp_rnd;

#endif /* _MACHINE_IEEEFP_H_ */
