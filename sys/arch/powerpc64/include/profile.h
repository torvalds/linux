/*	$OpenBSD: profile.h,v 1.1 2020/06/25 01:55:14 drahn Exp $ */

/*
 * Copyright (c) 2020 Dale Rahn drahn@openbsd.org
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

/* 
 * mcount frame size skips over the red zone (288B) (calling function may use)
 * and 128 bytes of local storage (32 bytes of reserved and 96 of our storage
 * this function assumes it will only every call the local __mcount function
 */
#define MCOUNT \
__asm__(" 								\n"\
	"	.section \".text\"					\n"\
	"	.p2align 2						\n"\
	"	.globl _mcount						\n"\
	"	.local __mcount						\n"\
	"	.type	_mcount,@function				\n"\
	"_mcount:							\n"\
	".L_mcount_gep0:						\n"\
	"	addis %r2, %r12, .TOC.-.L_mcount_gep0@ha;		\n"\
	"	addi %r2, %r2, .TOC.-.L_mcount_gep0@l;			\n"\
	".L_mcount_lep0:						\n"\
	".localentry     _mcount, .L_mcount_lep0-.L_mcount_gep0;	\n"\
	"	ld	%r11,16(%r1)					\n"\
	"	mflr	%r0						\n"\
	"	std	%r0, 16(%r1)					\n"\
	"	stdu	%r1,-(288+128)(%r1)				\n"\
	"	std	%r3, 32(%r1)					\n"\
	"	std	%r4, 40(%r1)					\n"\
	"	std	%r5, 48(%r1)					\n"\
	"	std	%r6, 56(%r1)					\n"\
	"	std	%r7, 64(%r1)					\n"\
	"	std	%r8, 72(%r1)					\n"\
	"	std	%r9, 80(%r1)					\n"\
	"	std	%r10,88(%r1)					\n"\
	"	std	%r11,96(%r1)					\n"\
	"	mr	%r4, %r0					\n"\
	"	mr 	%r3, %r11					\n"\
	"	bl __mcount 						\n"\
	"	nop 							\n"\
	"	ld	%r3, 32(%r1)					\n"\
	"	ld	%r4, 40(%r1)					\n"\
	"	ld	%r5, 48(%r1)					\n"\
	"	ld	%r6, 56(%r1)					\n"\
	"	ld	%r7, 64(%r1)					\n"\
	"	ld	%r8, 72(%r1)					\n"\
	"	ld	%r9, 80(%r1)					\n"\
	"	ld	%r10,88(%r1)					\n"\
	"	ld	%r11,96(%r1)					\n"\
	"	addi	%r1, %r1, (288+128)				\n"\
	"	ld	%r0, 16(%r1)					\n"\
	"	std	%r11,16(%r1)					\n"\
	"	mtlr	%r0						\n"\
	"	blr							\n"\
	"	.size _mcount, .-_mcount				\n"\
	);
#define _MCOUNT_DECL static void __mcount
