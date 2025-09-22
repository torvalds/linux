/*	$OpenBSD: strcmp.S,v 1.8 2018/07/03 23:14:05 mortimer Exp $	*/
/*	$NetBSD: strcmp.S,v 1.2 2014/03/22 19:16:34 jakllsch Exp $	*/

/*
 * Written by J.T. Conklin <jtc@acorntoolworks.com>
 * Public domain.
 */

#include "DEFS.h"

ENTRY(strcmp)
	RETGUARD_SETUP(strcmp, r11)
	/*
	 * Align s1 to word boundary.
	 * Consider unrolling loop?
	 */
.Ls1align:
	testb	$7,%dil
	je	.Ls1aligned
	movb	(%rdi),%al
	incq	%rdi
	movb	(%rsi),%dl
	incq	%rsi
	testb	%al,%al
	je	.Ldone
	cmpb	%al,%dl
	je	.Ls1align
	jmp	.Ldone

	/*
	 * Check whether s2 is aligned to a word boundary.  If it is, we
	 * can compare by words.  Otherwise we have to compare by bytes.
	 */
.Ls1aligned:
	testb	$7,%sil
	jne	.Lbyte_loop

	movabsq	$0x0101010101010101,%r8
	subq	$8,%rdi
	movabsq	$0x8080808080808080,%r9
	subq	$8,%rsi

	_ALIGN_TEXT
.Lword_loop:
	movq	8(%rdi),%rax
	addq	$8,%rdi
	movq	8(%rsi),%rdx
	addq	$8,%rsi
	cmpq	%rax,%rdx
	jne	.Lbyte_loop
	subq	%r8,%rdx
	notq	%rax
	andq	%rax,%rdx
	testq	%r9,%rdx
	je	.Lword_loop

	_ALIGN_TEXT
.Lbyte_loop:
	movb	(%rdi),%al
	incq	%rdi
	movb	(%rsi),%dl
	incq	%rsi
	testb	%al,%al
	je	.Ldone
	cmpb	%al,%dl
	je	.Lbyte_loop

.Ldone:
	movzbq	%al,%rax
	movzbq	%dl,%rdx
	subq	%rdx,%rax
	RETGUARD_CHECK(strcmp, r11)
	ret
END_STRONG(strcmp)
