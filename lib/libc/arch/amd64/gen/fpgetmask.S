/*	$OpenBSD: fpgetmask.S,v 1.3 2018/07/03 23:14:05 mortimer Exp $	*/
/*	$NetBSD: fpgetmask.S,v 1.3 2002/06/12 19:17:22 fvdl Exp $	*/

/*
 * Written by J.T. Conklin, Apr 4, 1995
 * Public domain.
 */

#include <machine/asm.h>

/*
 * XXX only read x87 control word here. If an application only
 * uses the fp* interface to manipulate FP bits, it should
 * always remain in sync with the SSE mxcsr register.
 */

#ifdef WEAK_ALIAS
WEAK_ALIAS(fpgetmask, _fpgetmask)
ENTRY(_fpgetmask)
#else
ENTRY(fpgetmask)
#endif
	RETGUARD_SETUP(fpgetmask, r11)
	fnstcw -4(%rsp)
	movl -4(%rsp),%eax
	notl %eax
	andl $63,%eax
	RETGUARD_CHECK(fpgetmask, r11)
	ret
#ifdef WEAK_ALIAS
END(_fpgetmask)
#else
END(fpgetmask)
#endif
