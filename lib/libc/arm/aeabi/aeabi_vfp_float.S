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

/* void __aeabi_cfcmpeq(float, float) */
AEABI_ENTRY(cfcmpeq)
	LOAD_SREGS(s0, s1, r0, r1)
	vcmp.f32	s0, s1
	vmrs     	APSR_nzcv, fpscr
	RET
AEABI_END(cfcmpeq)

/* void __aeabi_cfcmple(float, float) */
AEABI_ENTRY(cfcmple)
	LOAD_SREGS(s0, s1, r0, r1)
	vcmpe.f32	s0, s1
	vmrs     	APSR_nzcv, fpscr
	RET
AEABI_END(cfcmple)

/* void __aeabi_cfrcmple(float, float) */
AEABI_ENTRY(cfrcmple)
	LOAD_SREGS(s0, s1, r0, r1)
	vcmpe.f32	s1, s0
	vmrs     	APSR_nzcv, fpscr
	RET
AEABI_END(cfrcmple)

/* int __aeabi_fcmpeq(float, float) */
AEABI_ENTRY(fcmpeq)
	LOAD_SREGS(s0, s1, r0, r1)
	vcmp.f32 s0, s1
	vmrs     APSR_nzcv, fpscr
	ite      ne
	movne    r0, #0
	moveq    r0, #1
	RET
AEABI_END(fcmpeq)

/* int __aeabi_fcmplt(float, float) */
AEABI_ENTRY(fcmplt)
	LOAD_SREGS(s0, s1, r0, r1)
	vcmp.f32 s0, s1
	vmrs     APSR_nzcv, fpscr
	ite      cs
	movcs    r0, #0
	movcc    r0, #1
	RET
AEABI_END(fcmplt)

/* int __aeabi_fcmple(float, float) */
AEABI_ENTRY(fcmple)
	LOAD_SREGS(s0, s1, r0, r1)
	vcmp.f32 s0, s1
	vmrs     APSR_nzcv, fpscr
	ite      hi
	movhi    r0, #0
	movls    r0, #1
	RET
AEABI_END(fcmple)

/* int __aeabi_fcmpge(float, float) */
AEABI_ENTRY(fcmpge)
	LOAD_SREGS(s0, s1, r0, r1)
	vcmp.f32 s0, s1
	vmrs     APSR_nzcv, fpscr
	ite      lt
	movlt    r0, #0
	movge    r0, #1
	RET
AEABI_END(fcmpge)

/* int __aeabi_fcmpgt(float, float) */
AEABI_ENTRY(fcmpgt)
	LOAD_SREGS(s0, s1, r0, r1)
	vcmp.f32 s0, s1
	vmrs     APSR_nzcv, fpscr
	ite      le
	movle    r0, #0
	movgt    r0, #1
	RET
AEABI_END(fcmpgt)

/* int __aeabi_fcmpun(float, float) */
AEABI_ENTRY(fcmpun)
	LOAD_SREGS(s0, s1, r0, r1)
	vcmp.f32 s0, s1
	vmrs     APSR_nzcv, fpscr
	ite      vc
	movvc    r0, #0
	movvs    r0, #1
	RET
AEABI_END(fcmpun)

/* int __aeabi_f2iz(float) */
AEABI_ENTRY(f2iz)
	LOAD_SREG(s0, r0)
#if 0
	/*
	 * This should be the correct instruction, but binutils incorrectly
	 * encodes it as the version that used FPSCR to determine the rounding.
	 * When binutils is fixed we can use this again.
	 */
	vcvt.s32.f32 s0, s0
#else
	ftosizs      s0, s0
#endif
	vmov         r0, s0
	RET
AEABI_END(f2iz)

/* double __aeabi_f2d(float) */
AEABI_ENTRY(f2d)
	LOAD_SREG(s0, r0)
	vcvt.f64.f32 d0, s0
	UNLOAD_DREG(r0, r1, d0)
	RET
AEABI_END(f2d)

/* float __aeabi_i2f(int) */
AEABI_ENTRY(i2f)
	vmov         s0, r0
	vcvt.f32.s32 s0, s0
	UNLOAD_SREG(r0, s0)
	RET
AEABI_END(i2f)

/* float __aeabi_fadd(float, float) */
AEABI_ENTRY(fadd)
	LOAD_SREGS(s0, s1, r0, r1)
	vadd.f32 s0, s0, s1
	UNLOAD_SREG(r0, s0)
	RET
AEABI_END(fadd)

/* float __aeabi_fmul(float, float) */
AEABI_ENTRY(fdiv)
	LOAD_SREGS(s0, s1, r0, r1)
	vdiv.f32 s0, s0, s1
	UNLOAD_SREG(r0, s0)
	RET
AEABI_END(fdiv)

/* float __aeabi_fmul(float, float) */
AEABI_ENTRY(fmul)
	LOAD_SREGS(s0, s1, r0, r1)
	vmul.f32 s0, s0, s1
	UNLOAD_SREG(r0, s0)
	RET
AEABI_END(fmul)

/* float __aeabi_fsub(float, float) */
AEABI_ENTRY(fsub)
	LOAD_SREGS(s0, s1, r0, r1)
	vsub.f32 s0, s0, s1
	UNLOAD_SREG(r0, s0)
	RET
AEABI_END(fsub)

	.section .note.GNU-stack,"",%progbits
