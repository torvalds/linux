/*
 * Copyright (C) 2013 Andrew Turner
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
 *
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include "aeabi_vfp.h"

.fpu	vfp
.syntax	unified

/* void __aeabi_cdcmpeq(double, double) */
AEABI_ENTRY(cdcmpeq)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vcmp.f64	d0, d1
	vmrs		APSR_nzcv, fpscr
	RET
AEABI_END(cdcmpeq)

/* void __aeabi_cdcmple(double, double) */
AEABI_ENTRY(cdcmple)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vcmpe.f64	d0, d1
	vmrs		APSR_nzcv, fpscr
	RET
AEABI_END(cdcmple)

/* void __aeabi_cdrcmple(double, double) */
AEABI_ENTRY(cdrcmple)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vcmpe.f64	d1, d0
	vmrs		APSR_nzcv, fpscr
	RET
AEABI_END(cdrcmple)

/* int __aeabi_dcmpeq(double, double) */
AEABI_ENTRY(dcmpeq)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vcmp.f64 d0, d1
	vmrs     APSR_nzcv, fpscr
	ite      ne
	movne    r0, #0
	moveq    r0, #1
	RET
AEABI_END(dcmpeq)

/* int __aeabi_dcmplt(double, double) */
AEABI_ENTRY(dcmplt)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vcmp.f64 d0, d1
	vmrs     APSR_nzcv, fpscr
	ite      cs
	movcs    r0, #0
	movcc    r0, #1
	RET
AEABI_END(dcmplt)

/* int __aeabi_dcmple(double, double) */
AEABI_ENTRY(dcmple)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vcmp.f64 d0, d1
	vmrs     APSR_nzcv, fpscr
	ite      hi
	movhi    r0, #0
	movls    r0, #1
	RET
AEABI_END(dcmple)

/* int __aeabi_dcmpge(double, double) */
AEABI_ENTRY(dcmpge)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vcmp.f64 d0, d1
	vmrs     APSR_nzcv, fpscr
	ite      lt
	movlt    r0, #0
	movge    r0, #1
	RET
AEABI_END(dcmpge)

/* int __aeabi_dcmpgt(double, double) */
AEABI_ENTRY(dcmpgt)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vcmp.f64 d0, d1
	vmrs     APSR_nzcv, fpscr
	ite      le
	movle    r0, #0
	movgt    r0, #1
	RET
AEABI_END(dcmpgt)

/* int __aeabi_dcmpun(double, double) */
AEABI_ENTRY(dcmpun)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vcmp.f64 d0, d1
	vmrs     APSR_nzcv, fpscr
	ite      vc
	movvc    r0, #0
	movvs    r0, #1
	RET
AEABI_END(dcmpun)

/* int __aeabi_d2iz(double) */
AEABI_ENTRY(d2iz)
	LOAD_DREG(d0, r0, r1)
#if 0
	/*
	 * This should be the correct instruction, but binutils incorrectly
	 * encodes it as the version that used FPSCR to determine the rounding.
	 * When binutils is fixed we can use this again.
	 */
	vcvt.s32.f64 s0, d0
#else
	ftosizd s0, d0
#endif
	vmov         r0, s0
	RET
AEABI_END(d2iz)

/* float __aeabi_d2f(double) */
AEABI_ENTRY(d2f)
	LOAD_DREG(d0, r0, r1)
	vcvt.f32.f64 s0, d0
	UNLOAD_SREG(r0, s0)
	RET
AEABI_END(d2f)

/* double __aeabi_i2d(int) */
AEABI_ENTRY(i2d)
	vmov         s0, r0
	vcvt.f64.s32 d0, s0
	UNLOAD_DREG(r0, r1, d0)
	RET
AEABI_END(i2d)

/* double __aeabi_dadd(double, double) */
AEABI_ENTRY(dadd)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vadd.f64 d0, d0, d1
	UNLOAD_DREG(r0, r1, d0)
	RET
AEABI_END(dadd)

/* double __aeabi_ddiv(double, double) */
AEABI_ENTRY(ddiv)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vdiv.f64 d0, d0, d1
	UNLOAD_DREG(r0, r1, d0)
	RET
AEABI_END(ddiv)

/* double __aeabi_dmul(double, double) */
AEABI_ENTRY(dmul)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vmul.f64 d0, d0, d1
	UNLOAD_DREG(r0, r1, d0)
	RET
AEABI_END(dmul)

/* double __aeabi_dsub(double, double) */
AEABI_ENTRY(dsub)
	LOAD_DREG(d0, r0, r1)
	LOAD_DREG(d1, r2, r3)
	vsub.f64 d0, d0, d1
	UNLOAD_DREG(r0, r1, d0)
	RET
AEABI_END(dsub)

	.section .note.GNU-stack,"",%progbits
