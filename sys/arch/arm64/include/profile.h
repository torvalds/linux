/* $OpenBSD: profile.h,v 1.2 2021/02/17 12:11:45 kettenis Exp $ */
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
__asm__ (".text;"						\
	 ".align 3;"						\
	 ".globl " MCOUNT_ASM_NAME ";"				\
	 ".type " MCOUNT_ASM_NAME ",@function;"			\
	 MCOUNT_ASM_NAME ":;"					\
	 "	stp	x0, x1, [sp, #-160]!;"			\
	 "	stp	x2, x3, [sp, #16];"			\
	 "	stp	x4, x5, [sp, #32];"			\
	 "	stp	x6, x7, [sp, #48];"			\
	 "	stp	x8, x9, [sp, #64];"			\
	 "	stp	x10,x11,[sp, #80];"			\
	 "	stp	x12,x13,[sp, #96];"			\
	 "	stp	x14,x15,[sp, #112];"			\
	 "	stp	x16,x17,[sp, #128];"			\
	 "	stp	x29,lr, [sp, #144];"			\
	 /* load from pc at 8 off frame pointer */		\
	 "	ldr	x0, [x29, #8];"				\
	 "	mov	x1, lr;"				\
	 "	bl	" __STRING(_mcount) PLTSYM ";"		\
	 /* restore argument registers */			\
	 "	ldp	x2, x3, [sp, #16];"			\
	 "	ldp	x4, x5, [sp, #32];"			\
	 "	ldp	x6, x7, [sp, #48];"			\
	 "	ldp	x8, x9, [sp, #64];"			\
	 "	ldp	x10,x11,[sp, #80];"			\
	 "	ldp	x12,x13,[sp, #96];"			\
	 "	ldp	x14,x15,[sp, #112];"			\
	 "	ldp	x16,x17,[sp, #128];"			\
	 "	ldp	x29,lr, [sp, #144];"			\
	 "	ldp	x0, x1, [sp], #160;"			\
	 "	ret;");

#ifdef _KERNEL
// Change this to dair read/set, then restore.
#define MCOUNT_ENTER						\
__asm__ ("mrs %x0, daif; msr daifset, #0x3": "=r"(s));
#define	MCOUNT_EXIT						\
__asm__ ("msr daif, %x0":: "r"(s));
	
#endif // _KERNEL
