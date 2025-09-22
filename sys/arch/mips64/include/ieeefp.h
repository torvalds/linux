/*	$OpenBSD: ieeefp.h,v 1.4 2011/03/23 16:54:36 pirofti Exp $	*/

/*
 * Written by J.T. Conklin, Apr 11, 1995
 * Public domain.
 */

#ifndef _MIPS64_IEEEFP_H_
#define _MIPS64_IEEEFP_H_

typedef int fp_except;
#define FP_X_IMP	0x01	/* imprecise (loss of precision) */
#define FP_X_UFL	0x02	/* underflow exception */
#define FP_X_OFL	0x04	/* overflow exception */
#define FP_X_DZ		0x08	/* divide-by-zero exception */
#define FP_X_INV	0x10	/* invalid operation exception */

typedef enum {
    FP_RN=0,			/* round to nearest representable number */
    FP_RZ=1,			/* round to zero (truncate) */
    FP_RP=2,			/* round toward positive infinity */
    FP_RM=3			/* round toward negative infinity */
} fp_rnd;

#ifdef _KERNEL

/*
 * Defines for the floating-point completion/emulation code.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <machine/fpu.h>

#define	float_raise(bits) \
	do { curproc->p_md.md_regs->fsr |= (bits) << FPCSR_C_SHIFT; } while (0)
#define	float_set_inexact()	float_raise(FP_X_IMP)
#define	float_set_invalid()	float_raise(FP_X_INV)

#define	float_get_round(csr)	(csr & FPCSR_RM_MASK)
#define	fpgetround()		float_get_round(curproc->p_md.md_regs->fsr)

#endif

#endif /* !_MIPS64_IEEEFP_H_ */
