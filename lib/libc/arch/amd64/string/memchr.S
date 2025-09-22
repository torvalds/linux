/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 * Adapted for NetBSD/x86_64 by Frank van der Linden <fvdl@wasabisystems.com>
 */

#include "DEFS.h"

ENTRY(memchr)
	RETGUARD_SETUP(memchr, r11)
	movb	%sil,%al		/* set character to search for */
	movq	%rdx,%rcx		/* set length of search */
	testq	%rcx,%rcx		/* test for len == 0 */
	jz	L1
	cld				/* set search forward */
	repne				/* search! */
	scasb
	jne	L1			/* scan failed, return null */
	leaq	-1(%rdi),%rax		/* adjust result of scan */
	jmp L2
L1:	xorq	%rax,%rax
L2:	RETGUARD_CHECK(memchr, r11)
	ret
END_STRONG(memchr)
