/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <ppc-asm.h>

	.text

FUNC_START(core_busy_loop)
	stdu	%r1, -168(%r1)
	std	r14, 160(%r1)
	std	r15, 152(%r1)
	std	r16, 144(%r1)
	std	r17, 136(%r1)
	std	r18, 128(%r1)
	std	r19, 120(%r1)
	std	r20, 112(%r1)
	std	r21, 104(%r1)
	std	r22, 96(%r1)
	std	r23, 88(%r1)
	std	r24, 80(%r1)
	std	r25, 72(%r1)
	std	r26, 64(%r1)
	std	r27, 56(%r1)
	std	r28, 48(%r1)
	std	r29, 40(%r1)
	std	r30, 32(%r1)
	std	r31, 24(%r1)

	li	 r3, 0x3030
	std	 r3, -96(%r1)
	li	 r4, 0x4040
	std	 r4, -104(%r1)
	li	 r5, 0x5050
	std	 r5, -112(%r1)
	li	 r6, 0x6060
	std	 r6, -120(%r1)
	li	 r7, 0x7070
	std	 r7, -128(%r1)
	li	 r8, 0x0808
	std	 r8, -136(%r1)
	li	 r9, 0x0909
	std	 r9, -144(%r1)
	li	r10, 0x1010
	std	r10, -152(%r1)
	li	r11, 0x1111
	std	r11, -160(%r1)
	li	r14, 0x1414
	std	r14, -168(%r1)
	li	r15, 0x1515
	std	r15, -176(%r1)
	li	r16, 0x1616
	std	r16, -184(%r1)
	li	r17, 0x1717
	std	r17, -192(%r1)
	li	r18, 0x1818
	std	r18, -200(%r1)
	li	r19, 0x1919
	std	r19, -208(%r1)
	li	r20, 0x2020
	std	r20, -216(%r1)
	li	r21, 0x2121
	std	r21, -224(%r1)
	li	r22, 0x2222
	std	r22, -232(%r1)
	li	r23, 0x2323
	std	r23, -240(%r1)
	li	r24, 0x2424
	std	r24, -248(%r1)
	li	r25, 0x2525
	std	r25, -256(%r1)
	li	r26, 0x2626
	std	r26, -264(%r1)
	li	r27, 0x2727
	std	r27, -272(%r1)
	li	r28, 0x2828
	std	r28, -280(%r1)
	li	r29, 0x2929
	std	r29, -288(%r1)
	li	r30, 0x3030
	li	r31, 0x3131

	li	r3, 0
0:	addi	r3, r3, 1
	cmpwi	r3, 100
	blt	0b

	/* Return 1 (fail) unless we get through all the checks */
	li	r3, 1

	/* Check none of our registers have been corrupted */
	cmpwi	r4,  0x4040
	bne	1f
	cmpwi	r5,  0x5050
	bne	1f
	cmpwi	r6,  0x6060
	bne	1f
	cmpwi	r7,  0x7070
	bne	1f
	cmpwi	r8,  0x0808
	bne	1f
	cmpwi	r9,  0x0909
	bne	1f
	cmpwi	r10, 0x1010
	bne	1f
	cmpwi	r11, 0x1111
	bne	1f
	cmpwi	r14, 0x1414
	bne	1f
	cmpwi	r15, 0x1515
	bne	1f
	cmpwi	r16, 0x1616
	bne	1f
	cmpwi	r17, 0x1717
	bne	1f
	cmpwi	r18, 0x1818
	bne	1f
	cmpwi	r19, 0x1919
	bne	1f
	cmpwi	r20, 0x2020
	bne	1f
	cmpwi	r21, 0x2121
	bne	1f
	cmpwi	r22, 0x2222
	bne	1f
	cmpwi	r23, 0x2323
	bne	1f
	cmpwi	r24, 0x2424
	bne	1f
	cmpwi	r25, 0x2525
	bne	1f
	cmpwi	r26, 0x2626
	bne	1f
	cmpwi	r27, 0x2727
	bne	1f
	cmpwi	r28, 0x2828
	bne	1f
	cmpwi	r29, 0x2929
	bne	1f
	cmpwi	r30, 0x3030
	bne	1f
	cmpwi	r31, 0x3131
	bne	1f

	/* Load junk into all our registers before we reload them from the stack. */
	li	r3,  0xde
	li	r4,  0xad
	li	r5,  0xbe
	li	r6,  0xef
	li	r7,  0xde
	li	r8,  0xad
	li	r9,  0xbe
	li	r10, 0xef
	li	r11, 0xde
	li	r14, 0xad
	li	r15, 0xbe
	li	r16, 0xef
	li	r17, 0xde
	li	r18, 0xad
	li	r19, 0xbe
	li	r20, 0xef
	li	r21, 0xde
	li	r22, 0xad
	li	r23, 0xbe
	li	r24, 0xef
	li	r25, 0xde
	li	r26, 0xad
	li	r27, 0xbe
	li	r28, 0xef
	li	r29, 0xdd

	ld	r3,	-96(%r1)
	cmpwi	r3,  0x3030
	bne	1f
	ld	r4,	-104(%r1)
	cmpwi	r4,  0x4040
	bne	1f
	ld	r5,	-112(%r1)
	cmpwi	r5,  0x5050
	bne	1f
	ld	r6,	-120(%r1)
	cmpwi	r6,  0x6060
	bne	1f
	ld	r7,	-128(%r1)
	cmpwi	r7,  0x7070
	bne	1f
	ld	r8,	-136(%r1)
	cmpwi	r8,  0x0808
	bne	1f
	ld	r9,	-144(%r1)
	cmpwi	r9,  0x0909
	bne	1f
	ld	r10, -152(%r1)
	cmpwi	r10, 0x1010
	bne	1f
	ld	r11, -160(%r1)
	cmpwi	r11, 0x1111
	bne	1f
	ld	r14, -168(%r1)
	cmpwi	r14, 0x1414
	bne	1f
	ld	r15, -176(%r1)
	cmpwi	r15, 0x1515
	bne	1f
	ld	r16, -184(%r1)
	cmpwi	r16, 0x1616
	bne	1f
	ld	r17, -192(%r1)
	cmpwi	r17, 0x1717
	bne	1f
	ld	r18, -200(%r1)
	cmpwi	r18, 0x1818
	bne	1f
	ld	r19, -208(%r1)
	cmpwi	r19, 0x1919
	bne	1f
	ld	r20, -216(%r1)
	cmpwi	r20, 0x2020
	bne	1f
	ld	r21, -224(%r1)
	cmpwi	r21, 0x2121
	bne	1f
	ld	r22, -232(%r1)
	cmpwi	r22, 0x2222
	bne	1f
	ld	r23, -240(%r1)
	cmpwi	r23, 0x2323
	bne	1f
	ld	r24, -248(%r1)
	cmpwi	r24, 0x2424
	bne	1f
	ld	r25, -256(%r1)
	cmpwi	r25, 0x2525
	bne	1f
	ld	r26, -264(%r1)
	cmpwi	r26, 0x2626
	bne	1f
	ld	r27, -272(%r1)
	cmpwi	r27, 0x2727
	bne	1f
	ld	r28, -280(%r1)
	cmpwi	r28, 0x2828
	bne	1f
	ld	r29, -288(%r1)
	cmpwi	r29, 0x2929
	bne	1f

	/* Load 0 (success) to return */
	li	r3, 0

1:	ld	r14, 160(%r1)
	ld	r15, 152(%r1)
	ld	r16, 144(%r1)
	ld	r17, 136(%r1)
	ld	r18, 128(%r1)
	ld	r19, 120(%r1)
	ld	r20, 112(%r1)
	ld	r21, 104(%r1)
	ld	r22, 96(%r1)
	ld	r23, 88(%r1)
	ld	r24, 80(%r1)
	ld	r25, 72(%r1)
	ld	r26, 64(%r1)
	ld	r27, 56(%r1)
	ld	r28, 48(%r1)
	ld	r29, 40(%r1)
	ld	r30, 32(%r1)
	ld	r31, 24(%r1)
	addi	%r1, %r1, 168
	blr
