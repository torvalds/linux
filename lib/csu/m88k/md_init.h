/*	$OpenBSD: md_init.h,v 1.10 2023/11/18 16:26:16 deraadt Exp $	*/

/*
 * Copyright (c) 2012 Miodrag Vallat.
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

#define	MD_SECT_CALL_FUNC(section, func) __asm (			\
	"\t.section\t" #section ",\"ax\",@progbits\n"			\
	"\tbsr\t" #func "\n"					\
	"\t.previous")

#define	MD_SECTION_PROLOGUE(section, entry) __asm (			\
	"\t.section\t" #section ",\"ax\",@progbits\n"			\
	"\t.globl\t" #entry "\n"					\
	"\t.type\t" #entry ",@function\n"				\
	"\t.align\t2\n"							\
	#entry ":\n"							\
	"\tsubu\t%r31, %r31, 16\n"					\
	"\tst\t%r1, %r31, 0\n"						\
	"\t.previous")

#define	MD_SECTION_EPILOGUE(section) __asm(				\
	"\t.section\t" #section ",\"ax\",@progbits\n"			\
	"\tld\t%r1, %r31, 0\n"						\
	"\tjmp.n\t%r1\n"						\
	"\taddu\t%r31, %r31, 16\n"					\
	"\t.previous")

/*
 * When a program begins, r31 points to a structure passed by the kernel.
 *
 * This structure contains argc, the argv[] NULL-terminated array, and
 * the envp[] NULL-terminated array.
 *
 * Our start code starts with two nops because execution may skip up to
 * two instructions; see setregs() in the kernel for details.
 *
 * The definitions of environ and __progname prevent the creation
 * of COPY relocations for WEAK symbols.
 */
#define	MD_CRT0_START					\
	char **environ, *__progname;			\
	__asm(						\
	"	.text					\n" \
	"	.align 3				\n" \
	"	.globl __start				\n" \
	"	.globl _start				\n" \
	"__start:					\n" \
	"_start:					\n" \
	"	or	%r0, %r0, %r0			\n" \
	"	or	%r0, %r0, %r0			\n" \
	"	ld	%r2, %r31, 0	/* argc */	\n" \
	"	addu	%r3, %r31, 4	/* argv */	\n" \
	"	lda	%r4, %r3[%r2]			\n" \
	"	br.n	___start			\n" \
	"	 addu	%r4, %r4, 4			\n" \
	"	 /* envp = argv + argc + 1 */		\n" \
	"	.previous");

#define	MD_RCRT0_START					\
	__asm(						\
	"	.text					\n" \
	"	.align 3				\n" \
	"	.globl __start				\n" \
	"	.globl _start				\n" \
	"__start:					\n" \
	"_start:					\n" \
	"	or	%r0, %r0, %r0			\n" \
	"	or	%r0, %r0, %r0			\n" \
	\
	"	or	%r2, %r31, 0			\n" \
	"	subu	%r31, %r31, 4*16		\n" \
	"	or	%r3, %r31, 0			\n" \
	"	bsr	1f				\n" \
	"	bsr	_DYNAMIC#plt			\n" \
	"1:	ld	%r6, %r1, 0			\n" \
	"	mak	%r5, %r6, 26<2>			\n" \
	"	addu	%r4, %r5, %r1			\n" \
	"	bsr	_dl_boot_bind#plt		\n" \
	"	addu	%r31, %r31, 4*16		\n" \
	\
	"	ld	%r2, %r31, 0	/* argc */	\n" \
	"	addu	%r3, %r31, 4	/* argv */	\n" \
	"	lda	%r4, %r3[%r2]			\n" \
	"	or	%r5, %r0, %r0	/* cleanup */	\n" \
	"	br.n	___start			\n" \
	"	 addu	%r4, %r4, 4			\n" \
	"	 /* envp = argv + argc + 1 */		\n" \
	\
	"_csu_abort:					\n" \
	"	tb0	0, %r0, 130 /* breakpoint */	\n" \
	"	.previous");
