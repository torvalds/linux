/*	$OpenBSD: fpgetround.S,v 1.3 2018/07/03 23:14:05 mortimer Exp $	*/
/*	$NetBSD: fpgetround.S,v 1.3 2002/06/12 19:17:22 fvdl Exp $	*/

/*
 * Written by J.T. Conklin, Apr 4, 1995
 * Public domain.
 */

#include <machine/asm.h>

/*
 * XXX store only x87 state. If an application only uses the fp*
 * interface, this should be in sync with the SSE mxcsr register.
 */

#ifdef WEAK_ALIAS
WEAK_ALIAS(fpgetround, _fpgetround)
ENTRY(_fpgetround)
#else
ENTRY(fpgetround)
#endif
	RETGUARD_SETUP(fpgetround, r11)
	fnstcw -4(%rsp)
	movl -4(%rsp),%eax
	rorl $10,%eax
	andl $3,%eax
	RETGUARD_CHECK(fpgetround, r11)
	ret
#ifdef WEAK_ALIAS
END(_fpgetround)
#else
END(fpgetround)
#endif
