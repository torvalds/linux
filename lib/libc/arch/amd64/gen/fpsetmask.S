/*	$OpenBSD: fpsetmask.S,v 1.3 2018/07/03 23:14:05 mortimer Exp $	*/
/*	$NetBSD: fpsetmask.S,v 1.3 2002/06/12 19:17:22 fvdl Exp $	*/

/*
 * Written by Frank van der Linden at Wasabi Systems for NetBSD.
 * Public domain.
 */

#include <machine/asm.h>

/*
 * XXX set both the x87 control word and the SSE mxcsr register.
 * Applications should only set exception and round flags
 * via the fp*() interface, otherwise the status words
 * will get our of sync.
 */

#ifdef WEAK_ALIAS
WEAK_ALIAS(fpsetmask, _fpsetmask)
ENTRY(_fpsetmask)
#else
ENTRY(fpsetmask)
#endif
	RETGUARD_SETUP(fpsetmask, r11)
	fnstcw	-4(%rsp)
	stmxcsr	-8(%rsp)
	andl	$63,%edi
	notl	%edi

	movl	-4(%rsp),%edx
	movl	%edx,%eax
	andl	%edi,%edx
	movl	%edx,-4(%rsp)

	movl	-8(%rsp),%edx
	roll	$7,%edi
	andl	%edi,%edx
	movl	%edx,-8(%rsp)

	fldcw	-4(%rsp)
	ldmxcsr	-8(%rsp)
	andl	$63,%eax
	RETGUARD_CHECK(fpsetmask, r11)
	ret
#ifdef WEAK_ALIAS
END(_fpsetmask)
#else
END(fpsetmask)
#endif
