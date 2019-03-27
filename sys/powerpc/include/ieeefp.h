/* -
 * Written by J.T. Conklin, Apr 6, 1995
 * Public domain.
 * $NetBSD: ieeefp.h,v 1.2 1999/07/07 01:52:26 danw Exp $
 * $FreeBSD$
 */

#ifndef _MACHINE_IEEEFP_H_
#define _MACHINE_IEEEFP_H_

/* Deprecated historical FPU control interface */

typedef int fp_except_t;
#ifdef __SPE__
#define FP_X_OFL	0x01	/* overflow exception */
#define FP_X_UFL	0x02	/* underflow exception */
#define FP_X_DZ		0x04	/* divide-by-zero exception */
#define FP_X_INV	0x08	/* invalid operation exception */
#define FP_X_IMP	0x10	/* imprecise (loss of precision) */
#else
#define FP_X_IMP	0x01	/* imprecise (loss of precision) */
#define FP_X_DZ		0x02	/* divide-by-zero exception */
#define FP_X_UFL	0x04	/* underflow exception */
#define FP_X_OFL	0x08	/* overflow exception */
#define FP_X_INV	0x10	/* invalid operation exception */
#endif

typedef enum {
    FP_RN=0,			/* round to nearest representable number */
    FP_RZ=1,			/* round to zero (truncate) */
    FP_RP=2,			/* round toward positive infinity */
    FP_RM=3			/* round toward negative infinity */
} fp_rnd_t;

__BEGIN_DECLS
extern fp_rnd_t    fpgetround(void);
extern fp_rnd_t    fpsetround(fp_rnd_t);
extern fp_except_t fpgetmask(void);
extern fp_except_t fpsetmask(fp_except_t);
extern fp_except_t fpgetsticky(void);
__END_DECLS

#endif /* _MACHINE_IEEEFP_H_ */
