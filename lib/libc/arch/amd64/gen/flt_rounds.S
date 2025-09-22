/*	$OpenBSD: flt_rounds.S,v 1.8 2018/07/03 23:14:05 mortimer Exp $	*/
/*	$NetBSD: flt_rounds.S,v 1.2 2002/06/06 23:04:35 fvdl Exp $	*/

/*
 * Written by J.T. Conklin, Apr 4, 1995
 * Public domain.
 */

#include "DEFS.h"

	.section .rodata
_map:
	.byte 1		/* round to nearest */
	.byte 3		/* round to negative infinity */
	.byte 2		/* round to positive infinity */
	.byte 0		/* round to zero */

ENTRY(__flt_rounds)
	RETGUARD_SETUP(__flt_rounds, r11)
	fnstcw -4(%rsp)
	movl -4(%rsp),%eax
	shrl $10,%eax
	andl $3,%eax
#ifdef __PIC__
	leaq _map(%rip),%rcx
	movb (%rcx,%rax,1),%al
#else
	movb _map(,%rax,1),%al
#endif
	RETGUARD_CHECK(__flt_rounds, r11)
	ret
END_STRONG(__flt_rounds)
