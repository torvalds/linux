/*
 * Copyright (c) 2012 Mark Tinguely
 *
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * $FreeBSD$
 */


#ifndef _MACHINE__VFP_H_
#define _MACHINE__VFP_H_

#ifdef _KERNEL
/* Only kernel defines exist here */

/* fpsid, fpscr, fpexc are defined in the newer gas */
#define	VFPSID			cr0
#define	VFPSCR			cr1
#define	VMVFR1			cr6
#define	VMVFR0			cr7
#define	VFPEXC			cr8
#define	VFPINST			cr9	/* vfp 1 and 2 except instruction */
#define	VFPINST2		cr10 	/* vfp 2? */

/* VFPSID */
#define	VFPSID_IMPLEMENTOR_OFF	24
#define	VFPSID_IMPLEMENTOR_MASK	(0xff000000)
#define	VFPSID_HARDSOFT_IMP	(0x00800000)
#define	VFPSID_SINGLE_PREC	20	 /* version 1 and 2 */
#define	VFPSID_SUBVERSION_OFF	16
#define	VFPSID_SUBVERSION2_MASK	(0x000f0000)	 /* version 1 and 2 */
#define	VFPSID_SUBVERSION3_MASK	(0x007f0000)	 /* version 3 */
#define VFP_ARCH3		(0x00030000)
#define	VFPSID_PARTNUMBER_OFF	8
#define	VFPSID_PARTNUMBER_MASK	(0x0000ff00)
#define	VFPSID_VARIANT_OFF	4
#define	VFPSID_VARIANT_MASK	(0x000000f0)
#define	VFPSID_REVISION_MASK	0x0f

/* VFPSCR */
#define	VFPSCR_CC_N		(0x80000000)	/* comparison less than */
#define	VFPSCR_CC_Z		(0x40000000)	/* comparison equal */
#define	VFPSCR_CC_C		(0x20000000)	/* comparison = > unordered */
#define	VFPSCR_CC_V		(0x10000000)	/* comparison unordered */
#define	VFPSCR_QC		(0x08000000)	/* cumulative saturation */
#define	VFPSCR_DN		(0x02000000)	/* default NaN enable */
#define	VFPSCR_FZ		(0x01000000)	/* flush to zero enabled */

#define	VFPSCR_RMODE_OFF	22		/* rounding mode offset */
#define	VFPSCR_RMODE_MASK	(0x00c00000)	/* rounding mode mask */
#define	VFPSCR_RMODE_RN		(0x00000000)	/* round nearest */
#define	VFPSCR_RMODE_RPI	(0x00400000)	/* round to plus infinity */
#define	VFPSCR_RMODE_RNI	(0x00800000)	/* round to neg infinity */
#define	VFPSCR_RMODE_RM		(0x00c00000)	/* round to zero */

#define	VFPSCR_STRIDE_OFF	20		/* vector stride -1 */
#define	VFPSCR_STRIDE_MASK	(0x00300000)
#define	VFPSCR_LEN_OFF		16		/* vector length -1 */
#define	VFPSCR_LEN_MASK		(0x00070000)
#define	VFPSCR_IDE		(0x00008000)	/* input subnormal exc enable */
#define	VFPSCR_IXE		(0x00001000)	/* inexact exception enable */
#define	VFPSCR_UFE		(0x00000800)	/* underflow exception enable */
#define	VFPSCR_OFE		(0x00000400)	/* overflow exception enable */
#define	VFPSCR_DNZ		(0x00000200)	/* div by zero exception en */
#define	VFPSCR_IOE		(0x00000100)	/* invalid op exec enable */
#define	VFPSCR_IDC		(0x00000080)	/* input subnormal cumul */
#define	VFPSCR_IXC		(0x00000010)	/* Inexact cumulative flag */
#define	VFPSCR_UFC		(0x00000008)	/* underflow cumulative flag */
#define	VFPSCR_OFC		(0x00000004)	/* overflow cumulative flag */
#define	VFPSCR_DZC		(0x00000002)	/* division by zero flag */
#define	VFPSCR_IOC		(0x00000001)	/* invalid operation cumul */

/* VFPEXC */
#define	VFPEXC_EX 		(0x80000000)	/* exception v1 v2 */
#define	VFPEXC_EN		(0x40000000)	/* vfp enable */

/* version 3 registers */
/* VMVFR0 */
#define	VMVFR0_RM_OFF		28
#define	VMVFR0_RM_MASK 		(0xf0000000)	/* VFP rounding modes */

#define	VMVFR0_SV_OFF		24
#define	VMVFR0_SV_MASK		(0x0f000000)	/* VFP short vector supp */
#define	VMVFR0_SR_OFF		20
#define	VMVFR0_SR		(0x00f00000)	/* VFP hw sqrt supp */
#define	VMVFR0_D_OFF		16
#define	VMVFR0_D_MASK		(0x000f0000)	/* VFP divide supp */
#define	VMVFR0_TE_OFF		12
#define	VMVFR0_TE_MASK		(0x0000f000)	/* VFP trap exception supp */
#define	VMVFR0_DP_OFF		8
#define	VMVFR0_DP_MASK		(0x00000f00)	/* VFP double prec support */
#define	VMVFR0_SP_OFF		4
#define	VMVFR0_SP_MASK		(0x000000f0)	/* VFP single prec support */
#define	VMVFR0_RB_MASK		(0x0000000f)	/* VFP 64 bit media support */

/* VMVFR1 */
#define	VMVFR1_SP_OFF		16
#define	VMVFR1_SP_MASK 		(0x000f0000)	/* Neon single prec support */
#define VMVFR1_I_OFF		12
#define	VMVFR1_I_MASK		(0x0000f000)	/* Neon integer support */
#define VMVFR1_LS_OFF		8
#define	VMVFR1_LS_MASK		(0x00000f00)	/* Neon ld/st instr support */
#define VMVFR1_DN_OFF		4
#define	VMVFR1_DN_MASK		(0x000000f0)	/* Neon prop NaN support */
#define	VMVFR1_FZ_MASK		(0x0000000f)	/* Neon denormal arith supp */

#define COPROC10		(0x3 << 20)
#define COPROC11		(0x3 << 22)

void		vfp_init(void);
void		vfp_discard(struct proc *);
uint32_t	vfp_save(void);
void		vfp_enable(void);

#endif /* _KERNEL */
#endif /* _MACHINE__VFP_H_ */
