/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 * Adapted for NetBSD/x86_64 by Frank van der Linden <fvdl@wasabisystems.com>
 */

#include <machine/asm.h>

ENTRY(memchr)
	RETGUARD_SETUP(memchr, r11)
	movb	%sil,%al		/* set character to search for */
	movq	%rdx,%rcx		/* set length of search */
	testq	%rcx,%rcx		/* test for len == 0 */
	jz	1f
	repne				/* search! */
	scasb
	jne	1f			/* scan failed, return null */
	leaq	-1(%rdi),%rax		/* adjust result of scan */
	jmp	2f
1:	xorq	%rax,%rax
2:	RETGUARD_CHECK(memchr, r11)
	ret
	lfence
END(memchr)
