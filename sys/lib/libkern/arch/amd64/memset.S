/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 * Adapted for NetBSD/x86_64 by Frank van der Linden <fvdl@wasabisystems.com>
 */

#include <machine/asm.h>

ENTRY(memset)
	RETGUARD_SETUP(memset, r10)
	movq	%rsi,%rax
	andq	$0xff,%rax
	movq	%rdx,%rcx
	movq	%rdi,%r11

	/*
	 * if the string is too short, it's really not worth the overhead
	 * of aligning to word boundaries, etc.  So we jump to a plain
	 * unaligned set.
	 */
	cmpq	$0x0f,%rcx
	jle	1f

	movb	%al,%ah			/* copy char to all bytes in word */
	movl	%eax,%edx
	sall	$16,%eax
	orl	%edx,%eax

	movl	%eax,%edx
	salq	$32,%rax
	orq	%rdx,%rax

	movq	%rdi,%rdx		/* compute misalignment */
	negq	%rdx
	andq	$7,%rdx
	movq	%rcx,%r8
	subq	%rdx,%r8

	movq	%rdx,%rcx		/* set until word aligned */
	rep
	stosb

	movq	%r8,%rcx
	shrq	$3,%rcx			/* set by words */
	rep
	stosq

	movq	%r8,%rcx		/* set remainder by bytes */
	andq	$7,%rcx
1:	rep
	stosb
	movq	%r11,%rax
	RETGUARD_CHECK(memset, r10)
	ret
	lfence
END(memset)
