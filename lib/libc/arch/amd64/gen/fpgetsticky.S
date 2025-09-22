/*	$OpenBSD: fpgetsticky.S,v 1.3 2018/07/03 23:14:05 mortimer Exp $	*/
/*	$NetBSD: fpgetsticky.S,v 1.3 2002/06/12 19:17:22 fvdl Exp $	*/

/*
 * Written by Frank van der Linden at Wasabi Systems for NetBSD.
 * Public domain.
 *
 */

#include <machine/asm.h>

/*
 * XXX read both the x87 and SSE status words here, and OR
 * them to get a complete picture of exceptions.
 */

#ifdef WEAK_ALIAS
WEAK_ALIAS(fpgetsticky, _fpgetsticky)
ENTRY(_fpgetsticky)
#else
ENTRY(fpgetsticky)
#endif
	RETGUARD_SETUP(fpgetsticky, r11)
	fnstsw	-4(%rsp)
	stmxcsr	-8(%rsp)
	movl	-4(%rsp),%eax
	orl	-8(%rsp),%eax
	andl	$63,%eax
	RETGUARD_CHECK(fpgetsticky, r11)
	ret
#ifdef WEAK_ALIAS
END(_fpgetsticky)
#else
END(fpgetsticky)
#endif
