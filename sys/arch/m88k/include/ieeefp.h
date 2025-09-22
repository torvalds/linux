/*	$OpenBSD: ieeefp.h,v 1.5 2014/06/09 16:26:32 miod Exp $ */
/*
 * Copyright (c) 1996 Nivas Madhur
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * Values for fp_except are selected to match the bits in FPSR (see
 * m88100 user's manual page 6-33). This file is derived from the
 * definitions in the ABI/88k manual and sparc port.
 * 			       -- Nivas
 */

#ifndef _M88K_IEEEFP_H_
#define _M88K_IEEEFP_H_

typedef int fp_except;
#define FP_X_INV	0x10	/* invalid operation exception */
#define FP_X_DZ		0x08	/* divide-by-zero exception */
#define FP_X_UFL	0x04	/* underflow exception */
#define FP_X_OFL	0x02	/* overflow exception */
#define FP_X_IMP	0x01	/* imprecise (loss of precision) */

typedef enum {
    FP_RN=0,		/* round to nearest representable number */
    FP_RZ=1,		/* round to zero (truncate) */
    FP_RM=2,		/* round toward negative infinity */
    FP_RP=3		/* round toward positive infinity */
} fp_rnd;

#ifdef _KERNEL

/*
 * Defines for the floating-point completion code.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#define	float_raise(bits)	curproc->p_md.md_tf->tf_fpsr |= bits
#define	float_set_inexact()	float_raise(FP_X_IMP)
#define	float_set_invalid()	float_raise(FP_X_INV)

/* rounding mode bits position in FPCR */
#define	FPCR_RD_SHIFT	14
#define	FPCR_RD_MASK	0x03

#define	float_get_round(fpcr)	(((fpcr) >> FPCR_RD_SHIFT) & FPCR_RD_MASK)
#define	fpgetround()		float_get_round(curproc->p_md.md_tf->tf_fpcr)

#define	SOFTFLOAT_MD_CLZ
static inline int
countLeadingZeros32(u_int32_t a)
{
	int rc;
	asm volatile("ff1 %0, %1" : "=r"(rc) : "r"(a));
	return (a != 0 ? 31 - rc : rc);
}

#endif /* _KERNEL */

#endif /* _M88K_IEEEFP_H_ */
