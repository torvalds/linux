/*	$OpenBSD: movstrSI12_i4.S,v 1.1 2019/04/25 21:47:51 deraadt Exp $	 */

/*
 * Public domain
 * Theo de Raadt
 */

#include <machine/asm.h>

NENTRY(__movstrSI12_i4)
	mov.l	@r5, r0
	mov.l	@(4, r5), r1
	mov.l	@(8, r5), r2
	mov.l	r0, @r4
	mov.l	r1, @(4, r4)
	rts
	 mov.l	r2, @(8, r4)

STRONG_ALIAS(__movmemSI12_i4, __movstrSI12_i4)
