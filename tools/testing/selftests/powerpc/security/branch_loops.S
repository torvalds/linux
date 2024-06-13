// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2019, Michael Ellerman, IBM Corp.
 */

#include <ppc-asm.h>

	.data

jump_table:
	.long	0x0
	.long	(.Lstate_1 - .Lstate_0)
	.long	(.Lstate_2 - .Lstate_0)
	.long	(.Lstate_3 - .Lstate_0)
	.long	(.Lstate_4 - .Lstate_0)
	.long	(.Lstate_5 - .Lstate_0)
	.long	(.Lstate_6 - .Lstate_0)
	.long	(.Lstate_7 - .Lstate_0)

	.text

#define ITER_SHIFT	31

.macro state number
	.balign	32
.Lstate_\number:
	.if	\number==7
	li	r3, 0
	.else
	li	r3, \number+1
	.endif
	b	.Lloop
.endm

FUNC_START(pattern_cache_loop)
	li	r3, 0
	li	r4, 1
	sldi	r4, r4, ITER_SHIFT

.Lloop:	cmpdi	r4, 0
	beqlr

	addi	r4, r4, -1

	ld	r6, jump_table@got(%r2)
	sldi	r5, r3, 2
	lwax	r6, r5, r6
	ld	r7, .Lstate_0@got(%r2)
	add	r6, r6, r7
	mtctr	r6
	bctr

	state	0
	state	1
	state	2
	state	3
	state	4
	state	5
	state	6
	state	7

FUNC_END(pattern_cache_loop)


FUNC_START(indirect_branch_loop)
	li	r3, 1
	sldi	r3, r3, ITER_SHIFT

1:	cmpdi	r3, 0
	beqlr

	addi	r3, r3, -1

	ld	r4, 2f@got(%r2)
	mtctr	r4
	bctr

	.balign 32
2:	b	1b

FUNC_END(indirect_branch_loop)
