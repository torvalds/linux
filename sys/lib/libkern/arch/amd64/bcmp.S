#include <machine/asm.h>

ENTRY(bcmp)
	RETGUARD_SETUP(bcmp, r11)
	xorl	%eax,%eax		/* clear return value */

	movq	%rdx,%rcx		/* compare by words */
	shrq	$3,%rcx
	repe
	cmpsq
	jne	1f

	movq	%rdx,%rcx		/* compare remainder by bytes */
	andq	$7,%rcx
	repe
	cmpsb
	je	2f

1:	incl	%eax
2:	RETGUARD_CHECK(bcmp, r11)
	ret
	lfence
END(bcmp)
