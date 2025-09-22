/*	$OpenBSD: fpemu.S,v 1.15 2018/11/15 21:27:31 jmc Exp $	*/

/*
 * Copyright (c) 2000-2004 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>
#include <machine/cpu.h>
#include "assym.h"

#define	FPEMU_VERSION	(1 << 11)

#define	FP_TABLE2(name,ep0,ep1,ep2,ep3) \
	ldil	L%$fpemu_tbl$name, t1			! \
	ldo	R%$fpemu_tbl$name(t1), t1		! \
	ldwx,s	r1(t1), t2				! \
	bv	r0(t2)					! \
	copy	r0, ret0				! \
	.label $fpemu_tbl$name				! \
	.import	ep0##_##name, code			! \
	.import	ep1##_##name, code			! \
	.import	ep2##_##name, code			! \
	.import	ep3##_##name, code			! \
	.word	ep0##_##name, ep1##_##name, ep2##_##name, ep3##_##name

#define	FP_TABLE3(name,ep0,ep1,ep2,ep3,ep4,ep5,ep6,ep7,ep8,ep9,epa,epb,epc,epd,epe,epf) \
	ldil	L%$fpemu_tbl$name, t1			! \
	ldo	R%$fpemu_tbl$name(t1), t1		! \
	ldwx,s	r1(t1), t2				! \
	bv	r0(t2)					! \
	nop						! \
	.label	$fpemu_tbl$name				! \
	.import	ep0##_##name, code			! \
	.import	ep1##_##name, code			! \
	.import	ep2##_##name, code			! \
	.import	ep3##_##name, code			! \
	.import	ep4##_##name, code			! \
	.import	ep5##_##name, code			! \
	.import	ep6##_##name, code			! \
	.import	ep7##_##name, code			! \
	.import	ep8##_##name, code			! \
	.import	ep9##_##name, code			! \
	.import	epa##_##name, code			! \
	.import	epb##_##name, code			! \
	.import	epc##_##name, code			! \
	.import	epd##_##name, code			! \
	.import	epe##_##name, code			! \
	.import	epf##_##name, code			! \
	.word	ep0##_##name, ep1##_##name, ep2##_##name, ep3##_##name, \
		ep4##_##name, ep5##_##name, ep6##_##name, ep7##_##name, \
		ep8##_##name, ep9##_##name, epa##_##name, epb##_##name, \
		epc##_##name, epe##_##name, epf##_##name 

	.text
/*
 * fpu_emulate(iir,0,fpregs)
 */
LEAF_ENTRY(fpu_emulate)

	copy	arg0, t4
	extru	arg0, 18, 3, r31
	extru	arg0, 20, 2, r1
	extru	arg0, 22, 2, t3
	subi,<>	1, t3, r0
	extru	arg0, 16, 2, r31

	/*
	 * theoretically we would need to determine the fpu instruction
	 * exception type (there could be 4 of those, but stick w/
	 * non-timex fpus for now)
	 */
	extru,<> arg0, 10, 5, t1
	ldi	32, t1	/* fpemu zero reg */
	extru,<> arg0, 31, 5, t2
	b,n	$fpemu_nzt
	nop
	/*comib,=,n 2, t3, $fpemu_exit*/
	nop
$fpemu_nzt
	/*
	 * arg0 -- source register (address)
	 * arg1 -- fpregs context
	 * arg2 -- target register (address)
	 * arg3 -- fpregs context
	 * t3 -- class
	 * r31 -- subop
	 * r1 -- format specifier
	 * (t4 -- copy of arg0, ie iir)
	 */
	copy	arg2, arg3
	copy	arg2, arg1
	sh3add	t1, arg2, arg0
	sh3add	t2, arg2, arg2
	stw	r0, 32*8+0(arg1)	/* make sure zero reg is zero */
	stw	r0, 32*8+4(arg1)

	extru,=	t4, 24, 1, r0		/* adjust for the L in source */
	addi	4, arg0, arg0

	comib,=,n 2, t3, $fpemu0c_2
	nop

	extru,=	t4, 24, 1, r0		/* adjust for the L in target */
	addi	4, arg2, arg2

	comib,=,n 0, t3, $fpemu0c_0
	comib,=,n 1, t3, $fpemu0c_1
	comib,=,n 3, t3, $fpemu0c_3

$fpemu0c_0
	comib,=,n 2, r1, $fpemu_exit

	comib,=,n 0, r31, $fpemu0c_0_0
	comib,=,n 1, r31, $fpemu_exit
	comib,=,n 2, r31, $fpemu0c_0_2
	comib,=,n 3, r31, $fpemu0c_0_3
	comib,=,n 4, r31, $fpemu0c_0_4
	comib,=,n 5, r31, $fpemu0c_0_5
	comib,=,n 6, r31, $fpemu_exit
	comib,=,n 7, r31, $fpemu_exit

$fpemu0c_0_0
	ldi	FPEMU_VERSION, t4
	stw	t4, 0(arg3)
	bv	0(rp)
	copy	r0, ret0

$fpemu0c_0_2	/* fcpy */
	subi	3, r1, r1
	ldw	0*4(arg0), t1
	ldw	1*4(arg0), t2
	ldw	2*4(arg0), t3
	blr,n	r1, r0
	ldw	3*4(arg0), t4
	stw	t3, 2*4(arg2)
	stw	t4, 3*4(arg2)
	nop
	nop
	nop
	stw	t2, 1*4(arg2)
	stw	t1, 0*4(arg2)
	bv	0(rp)
	copy	r0, ret0

$fpemu0c_0_3	/* fabs */
	subi	3, r1, r1
	ldw	0*4(arg0), t1
	ldw	1*4(arg0), t2
	ldw	2*4(arg0), t3
	ldw	3*4(arg0), t4
	blr,n	r1, r0
	depi	0, 0, 1, t1
	stw	t3, 2*4(arg2)
	stw	t4, 3*4(arg2)
	nop
	nop
	nop
	stw	t2, 1*4(arg2)
	stw	t1, 0*4(arg2)
	bv	0(rp)
	copy	r0, ret0

$fpemu0c_0_4	/* fsqrt */
	/* quad not implemented */
	FP_TABLE2(fsqrt,sgl,dbl,invalid,invalid)

$fpemu0c_0_5	/* frnd */
	/* quad not implemented */
	FP_TABLE2(frnd,sgl,dbl,invalid,quad)

$fpemu0c_1
	extru	t4, 20, 4, r1
	comib,=,n 0, r31, $fpemu0c_1_0
	comib,=,n 1, r31, $fpemu0c_1_1
	comib,=,n 2, r31, $fpemu0c_1_2
	comib,=,n 3, r31, $fpemu0c_1_3

$fpemu0c_1_0	/* fcnvff */
	FP_TABLE3(fcnvff, invalid, dbl_to_sgl, invalid, quad_to_sgl, sgl_to_dbl, invalid, invalid, quad_to_dbl, invalid, invalid, invalid, invalid, sgl_to_quad, dbl_to_quad, invalid, invalid)

$fpemu0c_1_1	/* fcnvxf */
	FP_TABLE3(fcnvxf, sgl_to_sgl, dbl_to_sgl, invalid, quad_to_sgl, sgl_to_dbl, dbl_to_dbl, invalid, quad_to_dbl, invalid, invalid, invalid, invalid, sgl_to_quad, dbl_to_quad, invalid, quad_to_quad)

$fpemu0c_1_2	/* fcnvfx */
	FP_TABLE3(fcnvfx, sgl_to_sgl, dbl_to_sgl, invalid, quad_to_sgl, sgl_to_dbl, dbl_to_dbl, invalid, quad_to_dbl, invalid, invalid, invalid, invalid, sgl_to_quad, dbl_to_quad, invalid, quad_to_quad)

$fpemu0c_1_3	/* fcnvfxt */
	FP_TABLE3(fcnvfxt, sgl_to_sgl, dbl_to_sgl, invalid, quad_to_sgl, sgl_to_dbl, dbl_to_dbl, invalid, quad_to_dbl, invalid, invalid, invalid, invalid, sgl_to_quad, dbl_to_quad, invalid, quad_to_quad)

$fpemu0c_2
	comib,=,n 1, r31, $fpemu0c_2_1
	comib,<>,n 0, r31, $fpemu_exit

$fpemu0c_2_0
	extru,<> t4, 15, 5, t1
	ldi	32, t1
	sh3add	t1, arg3, arg1
	extru,=	t4, 19, 1, r0	/* see if it's the L reg */
	addi	4, arg1, arg1
	extru	t4, 31, 5, arg2
	FP_TABLE2(fcmp,sgl,dbl,invalid,invalid)

$fpemu0c_2_1
	comib,<>,n 0, r1, $fpemu_exit

	/* XXX timex is much more complicated */
	ldw	0(arg3), t1
	ldi	0, ret0
	extru,<> t1, 5, 1, r0
	bv,n	r0(rp)

	/* advance the pcqueue */
	mtctl	r0, pcsq
	mfctl	pcsq, t2
	mtctl	t2, pcsq
	mtctl	t2, pcsq
	mtctl	r0, pcoq
	mfctl	pcoq, t2
	mtctl	t2, pcoq
	ldo	4(t2), t2
	bv	r0(rp)
	mtctl	t2, pcoq

$fpemu0c_3
	extru,<> t4, 15, 5, t1
	ldi	32, t1
	extru,=	t4, 19, 1, r0	/* see if it's the L reg */
	addi	4, arg1, arg1
	blr	r31, r0
	nop

	b	$fpemu0c_3_0
	sh3add	t1, arg1, arg1
	b	$fpemu0c_3_1
	sh3add	t1, arg1, arg1
	b	$fpemu0c_3_2
	sh3add	t1, arg1, arg1
	b	$fpemu0c_3_3
	sh3add	t1, arg1, arg1
	b	$fpemu0c_3_4
	sh3add	t1, arg1, arg1
	b	$fpemu_exit
	sh3add	t1, arg1, arg1
	b	$fpemu_exit
	sh3add	t1, arg1, arg1
	b	$fpemu_exit
	sh3add	t1, arg1, arg1

$fpemu0c_3_0	/* fadd */
	FP_TABLE2(fadd,sgl,dbl,invalid,invalid)

$fpemu0c_3_1	/* fsub */
	FP_TABLE2(fsub,sgl,dbl,invalid,invalid)

$fpemu0c_3_2	/* fmpy/xmpy */
	bb,>=	t4, 23, $fpemu0c_3_2_f
	nop

	FP_TABLE2(xmpy,s,u,s,u)
$fpemu0c_3_2_f
	FP_TABLE2(fmpy,sgl,dbl,invalid,invalid)

$fpemu0c_3_3	/* fdiv */
	FP_TABLE2(fdiv,sgl,dbl,invalid,invalid)

$fpemu0c_3_4	/* frem */
	FP_TABLE2(frem,sgl,dbl,invalid,invalid)

	.export	$fpemu_exit, code
$fpemu_exit
	/* these look very ugly, but we don't want to mess up w/ m4 just
	 * for the sake of overall world prettiness value growth XXX */
invalid_fsqrt
invalid_frnd
invalid_fcnvff
sgl_to_quad_fcnvff
dbl_to_quad_fcnvff
quad_to_sgl_fcnvff
quad_to_dbl_fcnvff
invalid_fcnvxf
sgl_to_quad_fcnvxf
dbl_to_quad_fcnvxf
quad_to_sgl_fcnvxf
quad_to_dbl_fcnvxf
quad_to_quad_fcnvxf
invalid_fcnvfx
sgl_to_quad_fcnvfx
dbl_to_quad_fcnvfx
quad_to_sgl_fcnvfx
quad_to_dbl_fcnvfx
quad_to_quad_fcnvfx
invalid_fcnvfxt
sgl_to_quad_fcnvfxt
dbl_to_quad_fcnvfxt
quad_to_sgl_fcnvfxt
quad_to_dbl_fcnvfxt
quad_to_quad_fcnvfxt
invalid_fcmp
invalid_fadd
invalid_fsub
invalid_fmpy
invalid_fdiv
invalid_frem
	bv	0(rp)
	ldi	HPPA_FPU_ILL, ret0
EXIT(fpu_emulate)

	.end
