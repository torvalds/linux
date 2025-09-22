/* $OpenBSD: md_init.h,v 1.5 2023/11/18 16:26:16 deraadt Exp $ */

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

#define MD_SECT_CALL_FUNC(section, func)				\
	__asm (".section "#section", \"ax\"				\n" \
	"	bl " #func "						\n" \
	"	.previous")

#define MD_SECTION_PROLOGUE(sect, entry_pt)				\
	__asm (								\
	".section "#sect",\"ax\",@progbits				\n" \
	"	.globl " #entry_pt "					\n" \
	"	.type " #entry_pt ",@function				\n" \
	"	.align 4						\n" \
	#entry_pt":							\n" \
	".L_"sect"_gep:							\n" \
	"	addis %r2, %r12, .TOC.-.L_"sect"_gep@ha			\n" \
	"	addi %r2, %r2, .TOC.-.L_"sect"_gep@l			\n" \
	".L_"sect"_lep:							\n" \
	"	.localentry " #entry_pt", .L_"sect"_lep-.L_"sect"_gep;	\n" \
	"	mflr	%r0						\n" \
	"	std	%r0,16(%r1)					\n" \
	"	stdu	%r1,-64(%r1)					\n" \
	"	/* fall thru */						\n" \
	"	.previous")


#define MD_SECTION_EPILOGUE(sect)					\
	__asm (								\
	".section "#sect",\"ax\",@progbits				\n" \
	"	addi	%r1,%r1,64					\n" \
	"	ld	%r0,16(%r1)					\n" \
	"	mtlr	%r0						\n" \
	"	blr							\n" \
	"	.previous")

#define	MD_CRT0_START							\
__asm(									\
"	.text								\n" \
"	.section	\".text\"					\n" \
"	.align 2							\n" \
"	.globl	_start							\n" \
"	.type	_start, @function					\n" \
"	.globl	__start							\n" \
"	.type	__start, @function					\n" \
"_start:								\n" \
"__start:								\n" \
"	bl	1f							\n" \
"1:									\n" \
"	mflr	%r30							\n" \
"	addis	%r2, %r30, .TOC.-1b@ha					\n" \
"	addi	%r2, %r2, .TOC.-1b@l					\n" \
"	# put cleanup in r6 instead of r7				\n" \
"	mr	%r6, %r7						\n" \
"	li	%r7, 0							\n" \
"	stdu	%r7, -64(%r1)						\n" \
"	b ___start							\n" \
)

#define	MD_RCRT0_START							\
__asm(									\
"	.text								\n" \
"	.section	\".text\"					\n" \
"	.align 2							\n" \
"	.globl	_start							\n" \
"	.type	_start, @function					\n" \
"	.globl	__start							\n" \
"	.type	__start, @function					\n" \
"_start:								\n" \
"__start:								\n" \
"	bl	1f							\n" \
"1:									\n" \
"	mflr	%r31							\n" \
"	addis	%r2, %r31, .TOC.-1b@ha					\n" \
"	addi	%r2, %r2, .TOC.-1b@l					\n" \
"	stdu	1, (-48 -((9+3)*8))(%r1) # allocate dl_data		\n" \
"									\n" \
"	# Preserve program parameters during setup 			\n" \
"	mr %r15, %r3			# argc				\n" \
"	mr %r16, %r4			# argv				\n" \
"	mr %r17, %r5			# envp				\n" \
"									\n" \
"	addis	%r14, %r2, _DYNAMIC@toc@ha				\n" \
"	addi	%r14, %r14, _DYNAMIC@toc@l				\n" \
"									\n" \
"	subi	%r3, %r16, 8	# pointer to argc on stack.		\n" \
"	addi	%r4, %r1, 48	# dl_data				\n" \
"	mr	%r5, %r14	# dynamicp				\n" \
"									\n" \
"      bl      _dl_boot_bind						\n" \
"									\n" \
"	# restore program arguments 					\n" \
"	mr %r3, %r15							\n" \
"	mr %r4, %r16							\n" \
"	mr %r5, %r17							\n" \
"	li %r6, 0							\n" \
"	bl ___start							\n" \
"									\n" \
"	.globl	_csu_abort						\n" \
"	.type	_csu_abort, @function					\n" \
"_csu_abort:								\n" \
"	.long 0 # illegal						\n" \
)
