/*	$NetBSD: ieeefp.h,v 1.1 2001/01/10 19:02:06 bjh21 Exp $	*/
/* $FreeBSD$ */
/*-
 * Based on ieeefp.h written by J.T. Conklin, Apr 28, 1995
 * Public domain.
 */

#ifndef _MACHINE_IEEEFP_H_
#define _MACHINE_IEEEFP_H_

/* Deprecated historical FPU control interface */

/* FP exception codes */
#define FP_EXCEPT_INV	0
#define FP_EXCEPT_DZ	1
#define FP_EXCEPT_OFL	2
#define FP_EXCEPT_UFL	3
#define FP_EXCEPT_IMP	4

/* Exception type (used by fpsetmask() et al.) */

typedef int fp_except;

/* Bit defines for fp_except */

#define	FP_X_INV	(1 << FP_EXCEPT_INV)	/* invalid operation exception */
#define	FP_X_DZ		(1 << FP_EXCEPT_DZ)	/* divide-by-zero exception */
#define	FP_X_OFL	(1 << FP_EXCEPT_OFL)	/* overflow exception */
#define	FP_X_UFL	(1 << FP_EXCEPT_UFL)	/* underflow exception */
#define	FP_X_IMP	(1 << FP_EXCEPT_IMP)	/* imprecise (loss of precision; "inexact") */

/* Rounding modes */

typedef enum {
    FP_RN=0,			/* round to nearest representable number */
    FP_RP=1,			/* round toward positive infinity */
    FP_RM=2,			/* round toward negative infinity */
    FP_RZ=3			/* round to zero (truncate) */
} fp_rnd_t;

/*
 * FP precision modes
 */
typedef enum {
	FP_PS=0,	/* 24 bit (single-precision) */
	FP_PRS,		/* reserved */
	FP_PD,		/* 53 bit (double-precision) */
	FP_PE		/* 64 bit (extended-precision) */
} fp_prec_t;

#define fp_except_t	int

#endif /* _MACHINE_IEEEFP_H_ */
