/*	$OpenBSD: rpcc.S,v 1.1 2023/03/11 20:56:01 miod Exp $	*/
/*	$NetBSD: rpcc.S,v 1.2 1996/10/02 21:18:50 cgd Exp $	*/

#include <machine/asm.h>

	.text
LEAF(alpha_rpcc,1)
	rpcc	v0
	RET
	END(alpha_rpcc)
