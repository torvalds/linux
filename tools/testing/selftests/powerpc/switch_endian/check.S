#include "common.h"

/*
 * Checks that registers contain what we expect, ie. they were not clobbered by
 * the syscall.
 *
 * r15: pattern to check registers against.
 *
 * At the end r3 == 0 if everything's OK.
 */
	nop			# guaranteed to be illegal in reverse-endian
	mr	r9,r15
	cmpd    r9,r3		# check r3
	bne     1f
	addi    r9,r15,4	# check r4
	cmpd    r9,r4
	bne     1f
	lis     r9,0x00FF	# check CR
	ori     r9,r9,0xF000
	mfcr    r10
	and     r10,r10,r9
	cmpw    r9,r10
	addi    r9,r15,34
	bne     1f
	addi    r9,r15,32	# check LR
	mflr    r10
	cmpd    r9,r10
	bne     1f
	addi    r9,r15,5	# check r5
	cmpd    r9,r5
	bne     1f
	addi    r9,r15,6	# check r6
	cmpd    r9,r6
	bne     1f
	addi    r9,r15,7	# check r7
	cmpd    r9,r7
	bne     1f
	addi    r9,r15,8	# check r8
	cmpd    r9,r8
	bne     1f
	addi    r9,r15,13	# check r13
	cmpd    r9,r13
	bne     1f
	addi    r9,r15,14	# check r14
	cmpd    r9,r14
	bne     1f
	addi    r9,r15,16	# check r16
	cmpd    r9,r16
	bne     1f
	addi    r9,r15,17	# check r17
	cmpd    r9,r17
	bne     1f
	addi    r9,r15,18	# check r18
	cmpd    r9,r18
	bne     1f
	addi    r9,r15,19	# check r19
	cmpd    r9,r19
	bne     1f
	addi    r9,r15,20	# check r20
	cmpd    r9,r20
	bne     1f
	addi    r9,r15,21	# check r21
	cmpd    r9,r21
	bne     1f
	addi    r9,r15,22	# check r22
	cmpd    r9,r22
	bne     1f
	addi    r9,r15,23	# check r23
	cmpd    r9,r23
	bne     1f
	addi    r9,r15,24	# check r24
	cmpd    r9,r24
	bne     1f
	addi    r9,r15,25	# check r25
	cmpd    r9,r25
	bne     1f
	addi    r9,r15,26	# check r26
	cmpd    r9,r26
	bne     1f
	addi    r9,r15,27	# check r27
	cmpd    r9,r27
	bne     1f
	addi    r9,r15,28	# check r28
	cmpd    r9,r28
	bne     1f
	addi    r9,r15,29	# check r29
	cmpd    r9,r29
	bne     1f
	addi    r9,r15,30	# check r30
	cmpd    r9,r30
	bne     1f
	addi    r9,r15,31	# check r31
	cmpd    r9,r31
	bne     1f
	b	2f
1:	mr	r3, r9
	li	r0, __NR_exit
	sc
2:	li	r0, __NR_switch_endian
	nop
