/* $OpenBSD: md_init.h,v 1.4 2023/11/18 16:26:16 deraadt Exp $ */
/*
 * Copyright (c) 2020 Dale Rahn <drahn@openbsd.org>
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

#define MD_SECT_CALL_FUNC(section, func) 				\
	__asm (".section "#section", \"ax\"				\n" \
	"call " # func "@plt 						\n" \
	"	.previous")

#define MD_SECTION_PROLOGUE(sect, entry_pt)				\
	__asm (								\
	".section "#sect",\"ax\",%progbits				\n" \
	"	.globl " #entry_pt "					\n" \
	"	.type " #entry_pt ",@function				\n" \
	"	.align 4						\n" \
	#entry_pt":							\n" \
	"	addi	sp, sp, -16					\n" \
	"	sd	ra,8(sp)					\n" \
	"	sd	s0,0(sp)					\n" \
	"	addi	s0, sp, 16					\n" \
	"	/* fall thru */						\n" \
	"	.previous")


#define MD_SECTION_EPILOGUE(sect)					\
	__asm (								\
	".section "#sect",\"ax\",%progbits				\n" \
	"	ld	ra, 8(sp)					\n" \
	"	ld	s0, 0(sp)					\n" \
	"	addi	sp, sp, 16					\n" \
	"	jr	ra						\n" \
	"	.previous")


#define	MD_CRT0_START							\
	__asm(								\
	".text								\n" \
	"	.align	0						\n" \
	"	.globl	_start						\n" \
	"	.globl	__start						\n" \
	"	.type	_start, @function				\n" \
	"	.type	__start, @function				\n" \
	"_start:							\n" \
	"__start:							\n" \
	"/* Get argc/argv/envp from stack */				\n" \
	"	ld	a0, (sp)					\n" \
	"	addi	a1, sp, 0x8					\n" \
	"	slli	t0, a0, 0x3					\n" \
	"	add	a2, a1, t0					\n" \
	"	addi	a2, a2, 0x8					\n" \
	"								\n" \
	"	addi	sp, sp, -16					\n" \
	"	li	t0, 0						\n" \
	"	sd	t0,(sp)						\n" \
	"								\n" \
	"	j	___start					\n" \
	"	.size	_start, .-_start				\n" \
	"	.size	__start, .-__start				\n" \
	".previous");

#define	MD_RCRT0_START							\
	char **environ, *__progname;					\
	__asm(								\
	".text								\n" \
	"	.align	0						\n" \
	"	.globl	_start						\n" \
	"	.globl	__start						\n" \
	"	.type	_start, @function				\n" \
	"	.type	__start, @function				\n" \
	"_start:							\n" \
	"__start:							\n" \
	"	mv	a0, sp						\n" \
	"	mv	fp, sp						\n" \
	"								\n" \
	"	addi	sp, sp, -(8+8+(16*8))				\n" \
	"	addi	a1, sp, 4					\n" \
	"								\n" \
	"1:	auipc	a2, %pcrel_hi(_DYNAMIC)				\n" \
	"	addi	a2, a2, %pcrel_lo(1b)				\n" \
	"								\n" \
	"	call	_dl_boot_bind					\n" \
	"								\n" \
	"	mv	sp, fp						\n" \
	"	li	fp, 0x0						\n" \
	"								\n" \
	"	li	a3, 0x0	/* cleanup */				\n" \
	"/* Get argc/argv/envp from stack */				\n" \
	"	ld	a0, (sp)					\n" \
	"	addi	a1, sp, 0x0008					\n" \
	"	slli	a2, a0, 0x3					\n" \
	"	add	a2, a1, a2					\n" \
	"	addi	a2, a2, 0x0008					\n" \
	"								\n" \
	"	j	___start					\n" \
	"	.size	_start, .-_start				\n" \
	"	.size	__start, .-__start				\n" \
	"_csu_abort:							\n" \
	"	unimp							\n" \
	".previous");
