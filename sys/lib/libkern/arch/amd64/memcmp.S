/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 * Adapted for NetBSD/x86_64 by Frank van der Linden <fvdl@wasabisystems.com>
 */

#include <machine/asm.h>

ENTRY(memcmp)
	RETGUARD_SETUP(memcmp, r11)
	movq	%rdx,%rcx		/* compare by longs */
	shrq	$3,%rcx
	repe
	cmpsq
	jne	5f			/* do we match so far? */

	movq	%rdx,%rcx		/* compare remainder by bytes */
	andq	$7,%rcx
	repe
	cmpsb
	jne	6f			/* do we match? */

	xorl	%eax,%eax		/* we match, return zero	*/
	jmp	7f

5:	movl	$8,%ecx			/* We know that one of the next	*/
	subq	%rcx,%rdi		/* eight pairs of bytes do not	*/
	subq	%rcx,%rsi		/* match.			*/
	repe
	cmpsb
6:	xorl	%eax,%eax		/* Perform unsigned comparison	*/
	movb	-1(%rdi),%al
	xorl	%edx,%edx
	movb	-1(%rsi),%dl
	subl    %edx,%eax
7:	RETGUARD_CHECK(memcmp, r11)
	ret
	lfence
END(memcmp)
