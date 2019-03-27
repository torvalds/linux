/*	$OpenBSD: ieeefp.h,v 1.2 1999/01/27 04:46:05 imp Exp $	*/

/*-
 * Written by J.T. Conklin, Apr 11, 1995
 * Public domain.
 *
 *	JNPR: ieeefp.h,v 1.1 2006/08/07 05:38:57 katta
 * $FreeBSD$
 */

#ifndef _MACHINE_IEEEFP_H_
#define	_MACHINE_IEEEFP_H_

/* Deprecated historical FPU control interface */

typedef int fp_except;
typedef int fp_except_t;

#define	FP_X_IMP	0x01	/* imprecise (loss of precision) */
#define	FP_X_UFL	0x02	/* underflow exception */
#define	FP_X_OFL	0x04	/* overflow exception */
#define	FP_X_DZ		0x08	/* divide-by-zero exception */
#define	FP_X_INV	0x10	/* invalid operation exception */

typedef enum {
	FP_RN=0,		/* round to nearest representable number */
	FP_RZ=1,		/* round to zero (truncate) */
	FP_RP=2,		/* round toward positive infinity */
	FP_RM=3			/* round toward negative infinity */
} fp_rnd;

typedef fp_rnd fp_rnd_t;

#endif /* !_MACHINE_IEEEFP_H_ */
