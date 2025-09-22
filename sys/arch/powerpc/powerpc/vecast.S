/*	$OpenBSD: vecast.S,v 1.1 2022/10/22 00:58:56 gkoehler Exp $	*/

/*
 * Copyright (c) 2022 George Koehler <gkoehler@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <machine/asm.h>
#include <machine/psl.h>

/*
 * To load or store an arbitrary AltiVec register, we extract its
 * number from the instruction and multiply it by 8.  We do both using
 * rlwinm to rotate it left into bits 24 to 28.
 *
 * 0        10   15   20  24  28
 * |         |    |    |   |   |
 * 000100dddddaaaaabbbbbcccccxxxxxx
 */
#define VD_ROTATE	14, 24, 28
#define VA_ROTATE	19, 24, 28
#define VB_ROTATE	24, 24, 28
#define VC_ROTATE	29, 24, 28

/*
 * vctuxs, vctsxs have an unsigned immediate UI in bits 11 to 15.  We
 * extract it into bits 4 to 8, then add FLOAT_1_IS to make 2**UI.
 */
#define UI_ROTATE	7, 4, 8
#define FLOAT_1_IS	0x3f80		/* (float 1) >> 16 */

	.rodata
	.balign 4
.Lzero:		.float	0
.Lone:		.float	1
.Ln126:		.float	126
.Ltwo63:	.float	0x1p63
.Ltwo126:	.float	0x1p126
.Lmin:		.float	0x1p-126	/* FLT_MIN */	

	.text

/* This is the stack frame for vecast_asm. */
#define s_size		128
#define s_f31		120
#define s_f30		112
#define s_f29		104
#define s_f28		96
#define s_f27		88
#define s_f26		80
#define s_f25		72
#define s_f24		64
#define s_vc		48
#define s_vb		32
#define s_va		16

/*
 * vecast_asm(insn r3, label r4) emulates an AltiVec instruction when
 * it traps a denormal or subnormal float (with an AltiVec assist
 * exception).  Such a float f has 0 < |f| < FLT_MIN 2**-126.
 *
 * MPC7450 RISC Microprocessor Family Reference Manual, 7.1.2.5 Java
 * Mode, NaNs, Denormalized Numbers, and Zeros, has a list of trapping
 * instructions: vaddfp, vsubfp, vmaddfp, vnmsubfp, vrefp, vrsqrtefp,
 * vlogefp, vexptefp, vctsxs, vctuxs.
 */
ENTRY(vecast_asm)
	mflr	%r0			/* r0 = return address */
	RETGUARD_SETUP_LATE(vecast_asm, %r9, %r0)
	stwu	%r1, -s_size(%r1)
	mfmsr	%r5			/* r5 = old msr */

	/*
	 * Borrow the vector and floating-point units.  We must
	 * preserve all float and most vector registers.
	 */
	rlwinm	%r6, %r5, 0, 17, 15	/* r6 = r5 & ~PSL_EE */
	oris	%r6, %r6, PSL_VEC >> 16
	ori	%r6, %r6, PSL_FP
	mtmsr	%r6
	isync

	stfd	%f31, s_f31(%r1)
	stfd	%f30, s_f30(%r1)
	stfd	%f29, s_f29(%r1)
	stfd	%f28, s_f28(%r1)
	stfd	%f27, s_f27(%r1)
	stfd	%f26, s_f26(%r1)
	stfd	%f25, s_f25(%r1)
	stfd	%f24, s_f24(%r1)
	mffs	%f31			/* f31 = old fpscr */

	lis	%r6, .Lzero@ha
	la	%r6, .Lzero@l(%r6)	/* r6 = address of .Lzero */

	/* fpscr = zero (round to nearest, no traps) */
	lfs	%f30, 0(%r6)		/* f30 = zero */
	mtfsf	255, %f30

	/* All instructions do s_vb = VB now; VD = s_va at finish. */
	rlwinm	%r7, %r3, VB_ROTATE
	la	%r8, s_vb(%r1)
	bl	vecast_store_vector

	mtctr	%r4
	li	%r4, 4		/* r4 = 4 loop iterations */
	bctr			/* Branch to our instruction's label. */

/*
 * vaddfp: d = a + b
 */
	.globl vecast_vaddfp
vecast_vaddfp:
	rlwinm	%r7, %r3, VA_ROTATE
	la	%r8, s_va(%r1)
	bl	vecast_store_vector

	/* s_va = s_va + s_vb */
	mtctr	%r4
	la	%r7, (s_va - 4)(%r1)
1:	lfsu	%f30, 4(%r7)		/* r7 += 4, then load (r7). */
	lfs	%f29, (s_vb - s_va)(%r7)
	fadds	%f30, %f30, %f29
	stfs	%f30, 0(%r7)
	bdnz	1b			/* Loop 4 times. */
	b	vecast_finish

/*
 * vsubfp: d = a + b
 */
	.globl vecast_vsubfp
vecast_vsubfp:
	rlwinm	%r7, %r3, VA_ROTATE
	la	%r8, s_va(%r1)
	bl	vecast_store_vector

	/* s_va = s_va - s_vb */
	mtctr	%r4
	la	%r7, (s_va - 4)(%r1)
1:	lfsu	%f30, 4(%r7)
	lfs	%f29, (s_vb - s_va)(%r7)
	fsubs	%f30, %f30, %f29
	stfs	%f30, 0(%r7)
	bdnz	1b
	b	vecast_finish

/*
 * vmaddfp: d = a * c + b
 */
	.globl vecast_vmaddfp
vecast_vmaddfp:
	rlwinm	%r7, %r3, VA_ROTATE
	la	%r8, s_va(%r1)
	bl	vecast_store_vector
	rlwinm	%r7, %r3, VC_ROTATE
	la	%r8, s_vc(%r1)
	bl	vecast_store_vector

	/* s_va = s_va * s_vc + s_vb */
	mtctr	%r4
	la	%r7, (s_va - 4)(%r1)
1:	lfsu	%f30, 4(%r7)
	lfs	%f29, (s_vb - s_va)(%r7)
	lfs	%f28, (s_vc - s_va)(%r7)
	fmadds	%f30, %f30, %f28, %f29
	stfs	%f30, 0(%r7)
	bdnz	1b
	b	vecast_finish

/*
 * vnmsubfp: d = b - a * c
 */
	.globl vecast_vnmsubfp
vecast_vnmsubfp:
	rlwinm	%r7, %r3, VA_ROTATE
	la	%r8, s_va(%r1)
	bl	vecast_store_vector
	rlwinm	%r7, %r3, VC_ROTATE
	la	%r8, s_vc(%r1)
	bl	vecast_store_vector

	/* s_va = -(s_va * s_vc - s_vb) */
	mtctr	%r4
	la	%r7, (s_va - 4)(%r1)
1:	lfsu	%f30, 4(%r7)
	lfs	%f29, (s_vb - s_va)(%r7)
	lfs	%f28, (s_vc - s_va)(%r7)
	fnmsubs	%f30, %f30, %f28, %f29
	stfs	%f30, 0(%r7)
	bdnz	1b
	b	vecast_finish

/*
 * vrefp: d = estimate 1 / b
 */
	.globl vecast_vrefp
vecast_vrefp:
	/* s_va = estimate 1 / s_vb */
	mtctr	%r4
	la	%r7, (s_vb - 4)(%r1)
1:	lfsu	%f30, 4(%r7)
	fres	%f30, %f30
	stfs	%f30, (s_va - s_vb)(%r7)
	bdnz	1b
	b	vecast_finish

/*
 * vrsqrtefp: d = estimate 1 / sqrt(b)
 * 1 / sqrt(b) = 1 / sqrt(b * 2**126) * 2**63 when b < 2**-126
 *
 * MPC7455's frsqrte does 1 / sqrt(1) = 0.984375, relative error 1/64.
 * AltiVec must not err over 1/4096, so avoid frsqrte.
 */
	.globl vecast_vrsqrtefp
vecast_vrsqrtefp:
	/* f30 = 1; f29 = 2**63, f28 = 2**126; f27 = 2**-126 */
	lfs	%f30, (.Lone - .Lzero)(%r6)
	lfs	%f29, (.Ltwo63 - .Lzero)(%r6)
	lfs	%f28, (.Ltwo126 - .Lzero)(%r6)
	lfs	%f27, (.Lmin - .Lzero)(%r6)

	/*
	 * s_vb = s_vb * 2**126, s_va = 2**63 when b < 2**-126
	 * s_va = 1 when b >= 2**-126
	 */
	mtctr	%r4
	la	%r7, (s_vb - 4)(%r1)
1:	lfsu	%f26, 4(%r7)
	fmuls	%f25, %f26, %f28
	fsubs	%f24, %f26, %f27	/* f24 selects b >= 2**-126 */
	fsel	%f26, %f24, %f26, %f25	/* f26 = b or b * 2**126 */
	stfs	%f26, 0(%r7)
	fsel	%f25, %f24, %f30, %f29	/* f25 = 1 or 2**63 */
	stfs	%f25, (s_va - s_vb)(%r7)
	bdnz	1b

	/* s_vb = estimate 1 / sqrt(s_vb) */
	la	%r7, s_vc(%r1)
	la	%r8, s_vb(%r1)
	stvx	%v31, 0, %r7		/* Save v31 in s_vc. */
	lvx	%v31, 0, %r8
	vrsqrtefp %v31, %v31
	stvx	%v31, 0, %r8
	lvx	%v31, 0, %r7

	/* s_va = s_vb * s_va */
	mtctr	%r4
	la	%r7, (s_va - 4)(%r1)
1:	lfsu	%f30, 4(%r7)
	lfs	%f29, (s_vb - s_va)(%r7)
	fmuls	%f30, %f29, %f30
	stfs	%f30, 0(%r7)
	bdnz	1b
	b	vecast_finish

/*
 * vlogefp: d = estimate log2(b)
 * log2(b) = log2(b * 2**126) - 126 when b < 2**-126
 */
	.globl	vecast_vlogefp
vecast_vlogefp:
	/* f30 = 0; f29 = 126; f28 = 2**126; f27 = 2**-126 */
	lfs	%f29, (.Ln126 - .Lzero)(%r6)
	lfs	%f28, (.Ltwo126 - .Lzero)(%r6)
	lfs	%f27, (.Lmin - .Lzero)(%r6)

	/*
	 * s_vb = s_vb * 2**126, s_va = 126 when s_vb < 2**-126
	 * s_va = 0 when s_vb >= 2**-126
	 */
	mtctr	%r4
	la	%r7, (s_vb - 4)(%r1)
1:	lfsu	%f26, 4(%r7)
	fmuls	%f25, %f26, %f28
	fsubs	%f24, %f26, %f27	/* f24 selects b >= 2**-126 */
	fsel	%f26, %f24, %f26, %f25	/* f26 = b or b * 2**126 */
	stfs	%f26, 0(%r7)
	fsel	%f25, %f24, %f30, %f29	/* f25 = 0 or 126 */
	stfs	%f25, (s_va - s_vb)(%r7)
	bdnz	1b

	/* s_vb = estimate log2(s_vb) */
	la	%r7, s_vc(%r1)
	la	%r8, s_vb(%r1)
	stvx	%v31, 0, %r7
	lvx	%v31, 0, %r8
	vlogefp	%v31, %v31
	stvx	%v31, 0, %r8
	lvx	%v31, 0, %r7

	/* s_va = s_vb - s_va */
	mtctr	%r4
	la	%r7, (s_va - 4)(%r1)
1:	lfsu	%f30, 4(%r7)
	lfs	%f29, (s_vb - s_va)(%r7)
	fsubs	%f30, %f29, %f30
	stfs	%f30, 0(%r7)
	bdnz	1b
	b	vecast_finish

/*
 * vexptefp: d = estimate 2**b
 * 2**b = 2**(b + 126) * 2**-126 when -252 <= b < -126
 */
	.globl	vecast_vexptefp
vecast_vexptefp:
	/* f30 = 1; f29 = 126; f28 = 2**-126 */
	lfs	%f30, (.Lone - .Lzero)(%r6)
	lfs	%f29, (.Ln126 - .Lzero)(%r6)
	lfs	%f28, (.Lmin - .Lzero)(%r6)

	/*
	 * s_vb = s_vb + 126 when -252 <= b < -126
	 * s_va = 2**-126 when b < -126
	 * s_va = 1 when b >= -126
	 *
	 * If b < -252, we avoid a possibly subnormal 2**(b + 126)
	 * by calculating 2**b * 2**-126 = 0 * 2**-126 = 0.
	 */
	mtctr	%r4
	la	%r7, (s_vb - 4)(%r1)
1:	lfsu	%f27, 4(%r7)
	fadds	%f26, %f27, %f29	/* f26 selects b >= -126 */
	fadds	%f25, %f26, %f29	/* f25 selects b >= -252 */
	fsel	%f24, %f26, %f27, %f26
	fsel	%f24, %f25, %f24, %f27	/* f24 = b or b + 126 */
	stfs	%f24, 0(%r7)
	fsel	%f27, %f26, %f30, %f28	/* f27 = 1 or 2**-126 */
	stfs	%f27, (s_va - s_vb)(%r7)
	bdnz	1b

	/* s_vb = estimate 2**s_vb */
	la	%r7, s_vc(%r1)
	la	%r8, s_vb(%r1)
	stvx	%v31, 0, %r7
	lvx	%v31, 0, %r8
	vexptefp %v31, %v31
	stvx	%v31, 0, %r8
	lvx	%v31, 0, %r7

 	/* s_va = s_vb * s_va */
	mtctr	%r4
	la	%r7, (s_va - 4)(%r1)
1:	lfsu	%f30, 4(%r7)
	lfs	%f29, (s_vb - s_va)(%r7)
	fmuls	%f30, %f29, %f30
	stfs	%f30, 0(%r7)
	bdnz	1b
	b	vecast_finish

/*
 * vctsxs: d = (int32_t)(b * 2**u) where 0 <= u < 32
 * d = 0 when |b| < 2**-126
 */
	.globl	vecast_vctsxs
vecast_vctsxs:
	/* f30 = 0; f29 = 2**-126; f28 = 2**u */
	lfs	%f29, (.Lmin - .Lzero)(%r6)
	rlwinm	%r7, %r3, UI_ROTATE
	addis	%r7, %r7, FLOAT_1_IS
	stw	%r7, s_va(%r1)
	lfs	%f28, s_va(%r1)

	/* s_va = s_vb * 2**u, unless b is tiny. */
	mtctr	%r4
	la	%r7, (s_vb - 4)(%r1)
1:	lfsu	%f27, 4(%r7)
	fmuls	%f26, %f27, %f28
	fabs	%f27, %f27
	fsubs	%f27, %f27, %f29	/* f27 selects |b| >= 2**-126 */
	fsel	%f26, %f27, %f26, %f30	/* f26 = b * 2**u or 0 */
	stfs	%f26, (s_va - s_vb)(%r7)
	bdnz	1b

	/* s_va = (int32_t)b */
	la	%r7, s_vc(%r1)
	la	%r8, s_va(%r1)
	stvx	%v31, 0, %r7
	lvx	%v31, 0, %r8
	vctsxs	%v31, %v31, 0		/* May set SAT in vscr. */
	stvx	%v31, 0, %r8
	lvx	%v31, 0, %r7
	b	vecast_finish

/*
 * vctuxs: d = (uint32_t)(b * 2**u) where 0 <= u < 32
 * d = 0 when |b| < 2**-126
 */
	.globl	vecast_vctuxs
vecast_vctuxs:
	/* f30 = 0; f29 = 2**-126; f28 = 2**u */
	lfs	%f29, (.Lmin - .Lzero)(%r6)
	rlwinm	%r7, %r3, UI_ROTATE
	addis	%r7, %r7, FLOAT_1_IS
	stw	%r7, s_va(%r1)
	lfs	%f28, s_va(%r1)

	/* s_va = s_vb * 2**u, unless b is tiny. */
	mtctr	%r4
	la	%r7, (s_vb - 4)(%r1)
1:	lfsu	%f27, 4(%r7)
	fmuls	%f26, %f27, %f28
	fabs	%f27, %f27
	fsubs	%f27, %f27, %f29	/* f27 selects |b| >= 2**-126 */
	fsel	%f26, %f27, %f26, %f30	/* f26 = b * 2**u or 0 */
	stfs	%f26, (s_va - s_vb)(%r7)
	bdnz	1b

	/* s_va = (uint32_t)b */
	la	%r7, s_vc(%r1)
	la	%r8, s_va(%r1)
	stvx	%v31, 0, %r7
	lvx	%v31, 0, %r8
	vctuxs	%v31, %v31, 0		/* May set SAT in vscr. */
	stvx	%v31, 0, %r8
	lvx	%v31, 0, %r7
	/* b	vecast_finish */

vecast_finish:
	/* VD = s_va */
	rlwinm	%r7, %r3, VD_ROTATE
	addis	%r7, %r7, 1f@ha
	addi	%r7, %r7, 1f@l
	mtctr	%r7
	la	%r8, s_va(%r1)
	bctr
#define M(n) lvx %v##n, 0, %r8; b 2f
1:	M( 0); M( 1); M( 2); M( 3); M( 4); M( 5); M( 6); M( 7)
	M( 8); M( 9); M(10); M(11); M(12); M(13); M(14); M(15)
	M(16); M(17); M(18); M(19); M(20); M(21); M(22); M(23)
	M(24); M(25); M(26); M(27); M(28); M(29); M(30); M(31)
#undef M
2:	mtlr	%r0
	mtfsf	255, %f31		/* Restore old fpscr. */
	lfd	%f24, s_f24(%r1)
	lfd	%f25, s_f25(%r1)
	lfd	%f26, s_f26(%r1)
	lfd	%f27, s_f27(%r1)
	lfd	%f28, s_f28(%r1)
	lfd	%f29, s_f29(%r1)
	lfd	%f30, s_f30(%r1)
	lfd	%f31, s_f31(%r1)
	mtmsr	%r5			/* Restore old msr. */
	isync
	addi	%r1, %r1, s_size
	RETGUARD_CHECK(vecast_asm, %r9, %r0)
	blr

/*
 * Stores vector v(r7 / 8) to address r8.
 */
vecast_store_vector:
	RETGUARD_SETUP(vecast_store_vector, %r11, %r12)
	addis	%r7, %r7, 1f@ha
	addi	%r7, %r7, 1f@l
	mtctr	%r7
	bctr
#define M(n)	stvx	%v##n, 0, %r8; b 2f
1:	M( 0); M( 1); M( 2); M( 3); M( 4); M( 5); M( 6); M( 7)
	M( 8); M( 9); M(10); M(11); M(12); M(13); M(14); M(15)
	M(16); M(17); M(18); M(19); M(20); M(21); M(22); M(23)
	M(24); M(25); M(26); M(27); M(28); M(29); M(30); M(31)
#undef M
2:	RETGUARD_CHECK(vecast_store_vector, %r11, %r12)
	blr
