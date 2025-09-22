/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include "DEFS.h"

ENTRY(ldiv)
	movl	4(%esp),%eax
	movl	8(%esp),%ecx
	cdq
	idiv	%ecx
	movl	%eax,4(%esp)
	movl	%edx,8(%esp)
	ret
END_STRONG(ldiv)
