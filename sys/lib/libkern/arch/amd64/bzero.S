/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 * Adapted for NetBSD/x86_64 by Frank van der Linden <fvdl@wasabisystems.com>
 */

#include <machine/asm.h>

ENTRY(bzero)
	RETGUARD_SETUP(bzero, r11)
	movq	%rsi,%rdx

	xorq	%rax,%rax		/* set fill data to 0 */

	/*
	 * if the string is too short, it's really not worth the overhead
	 * of aligning to word boundaries, etc.  So we jump to a plain
	 * unaligned set.
	 */
	cmpq	$16,%rdx
	jb	1f

	movq	%rdi,%rcx		/* compute misalignment */
	negq	%rcx
	andq	$7,%rcx
	subq	%rcx,%rdx
	rep				/* zero until word aligned */
	stosb

	movq	%rdx,%rcx		/* zero by words */
	shrq	$3,%rcx
	andq	$7,%rdx
	rep
	stosq

1:	movq	%rdx,%rcx		/* zero remainder by bytes */
	rep
	stosb
	RETGUARD_CHECK(bzero, r11)
	ret
	lfence
END(bzero)
