#include "common.h"

	.data
	.balign 8
message:
	.ascii "success: switch_endian_test\n\0"

	.section ".toc"
	.balign 8
pattern:
	.8byte 0x5555AAAA5555AAAA

	.text
FUNC_START(_start)
	/* Load the pattern */
	ld	r15, pattern@TOC(%r2)

	/* Setup CR, only CR2-CR4 are maintained */
	lis	r3, 0x00FF
	ori	r3, r3, 0xF000
	mtcr	r3

	/* Load the pattern slightly modified into the registers */
	mr	r3, r15
	addi	r4, r15, 4

	addi	r5, r15, 32
	mtlr	r5

	addi	r5, r15, 5
	addi	r6, r15, 6
	addi	r7, r15, 7
	addi	r8, r15, 8

	/* r9 - r12 are clobbered */

	addi	r13, r15, 13
	addi	r14, r15, 14

	/* Skip r15 we're using it */

	addi	r16, r15, 16
	addi	r17, r15, 17
	addi	r18, r15, 18
	addi	r19, r15, 19
	addi	r20, r15, 20
	addi	r21, r15, 21
	addi	r22, r15, 22
	addi	r23, r15, 23
	addi	r24, r15, 24
	addi	r25, r15, 25
	addi	r26, r15, 26
	addi	r27, r15, 27
	addi	r28, r15, 28
	addi	r29, r15, 29
	addi	r30, r15, 30
	addi	r31, r15, 31

	/*
	 * Call the syscall to switch endian.
	 * It clobbers r9-r12, XER, CTR and CR0-1,5-7.
	 */
	li r0, __NR_switch_endian
	sc

#include "check-reversed.S"

	/* Flip back, r0 already has the switch syscall number */
	.long	0x02000044	/* sc */

#include "check.S"

	li	r0, __NR_write
	li	r3, 1	/* stdout */
	ld	r4, message@got(%r2)
	li	r5, 28	/* strlen(message3) */
	sc
	li      r0, __NR_exit
	li	r3, 0
	sc
	b       .
