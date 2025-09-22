/*	$OpenBSD: profile.h,v 1.4 2021/05/21 16:49:57 deraadt Exp $	*/

/*
 * Copyright (c) 2015 Dale Rahn <drahn@dalerahn.com>
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

#define	_MCOUNT_DECL void _mcount

#define MCOUNT_ASM_NAME "__mcount"

#ifdef __PIC__
#define	PLTSYM		"" /* XXX -aarch64 defaults to PLT? */
#else
#define	PLTSYM		""
#endif

#define MCOUNT							\
__asm__ (".text						\n;"	\
	 ".align 3					\n;"	\
	 ".globl " MCOUNT_ASM_NAME "			\n;"	\
	 ".type " MCOUNT_ASM_NAME ",@function		\n;"	\
	 MCOUNT_ASM_NAME ":				\n;"	\
	 "	addi	sp, sp, -176			\n"	\
	 "	sd	fp, 0(sp)			\n"	\
	 "	sd	ra, 8(sp)			\n"	\
	 "	sd	s1, 24(sp)			\n"	\
	 "	sd	a0, 32(sp)			\n"	\
	 "	sd	a1, 40(sp)			\n"	\
	 "	sd	a2, 48(sp)			\n"	\
	 "	sd	a3, 56(sp)			\n"	\
	 "	sd	a4, 64(sp)			\n"	\
	 "	sd	a5, 72(sp)			\n"	\
	 "	sd	a6, 80(sp)			\n"	\
	 "	sd	a7, 88(sp)			\n"	\
	 "	sd	s2, 96(sp)			\n"	\
	 "	sd	s3, 104(sp)			\n"	\
	 "	sd	s4, 112(sp)			\n"	\
	 "	sd	s5, 120(sp)			\n"	\
	 "	sd	s6, 128(sp)			\n"	\
	 "	sd	s7, 136(sp)			\n"	\
	 "	sd	s8, 144(sp)			\n"	\
	 "	sd	s9, 152(sp)			\n"	\
	 "	sd	s10, 160(sp)			\n"	\
	 "	sd	s11, 168(sp)			\n"	\
	 "	ld	a0, 8(fp)			\n"	\
	 "	mv	a1, x1				\n"	\
	 "	call	" __STRING(_mcount) PLTSYM "	\n"	\
	 /* restore argument registers */			\
	 "	ld	fp, 0(sp)			\n"	\
	 "	ld	ra, 8(sp)			\n"	\
	 "	ld	s1, 24(sp)			\n"	\
	 "	ld	a0, 32(sp)			\n"	\
	 "	ld	a1, 40(sp)			\n"	\
	 "	ld	a2, 48(sp)			\n"	\
	 "	ld	a3, 56(sp)			\n"	\
	 "	ld	a4, 64(sp)			\n"	\
	 "	ld	a5, 72(sp)			\n"	\
	 "	ld	a6, 80(sp)			\n"	\
	 "	ld	a7, 88(sp)			\n"	\
	 "	ld	s2, 96(sp)			\n"	\
	 "	ld	s3, 104(sp)			\n"	\
	 "	ld	s4, 112(sp)			\n"	\
	 "	ld	s5, 120(sp)			\n"	\
	 "	ld	s6, 128(sp)			\n"	\
	 "	ld	s7, 136(sp)			\n"	\
	 "	ld	s8, 144(sp)			\n"	\
	 "	ld	s9, 152(sp)			\n"	\
	 "	ld	s10, 160(sp)			\n"	\
	 "	ld	s11, 168(sp)			\n"	\
	 "	addi	sp, sp, 176			\n"	\
	 "	jr	ra				\n");

#ifdef _KERNEL
// Change this to dair read/set, then restore.
#define MCOUNT_ENTER						\
__asm__ ("mrs %x0,daif; msr daifset, #0x2": "=r"(s));
#define	MCOUNT_EXIT						\
__asm__ ("msr daif, %x0":: "r"(s));

#endif
