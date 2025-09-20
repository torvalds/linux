/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Test basic matrix multiply assist (MMA) functionality if available.
 *
 * Copyright 2020, Alistair Popple, IBM Corp.
 */
	.global test_mma
test_mma:
	/* Load accumulator via VSX registers from image passed in r3 */
	lxvh8x	4,0,3
	lxvh8x	5,0,4

	/* Clear and prime the accumulator (xxsetaccz) */
	.long	0x7c030162

	/* Prime the accumulator with MMA VSX move to accumulator
	* X-form (xxmtacc) (not needed due to above zeroing) */
	//.long 0x7c010162

	/* xvi16ger2s */
	.long	0xec042958

	/* Deprime the accumulator - xxmfacc 0 */
	.long 0x7c000162

	/* Store result in image passed in r5 */
	stxvw4x	0,0,5
	addi	5,5,16
	stxvw4x	1,0,5
	addi	5,5,16
	stxvw4x	2,0,5
	addi	5,5,16
	stxvw4x	3,0,5
	addi	5,5,16

	blr
