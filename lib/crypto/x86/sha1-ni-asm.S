/*
 * Intel SHA Extensions optimized implementation of a SHA-1 update function
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Contact Information:
 * 	Sean Gulley <sean.m.gulley@intel.com>
 * 	Tim Chen <tim.c.chen@linux.intel.com>
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 	* Redistributions of source code must retain the above copyright
 * 	  notice, this list of conditions and the following disclaimer.
 * 	* Redistributions in binary form must reproduce the above copyright
 * 	  notice, this list of conditions and the following disclaimer in
 * 	  the documentation and/or other materials provided with the
 * 	  distribution.
 * 	* Neither the name of Intel Corporation nor the names of its
 * 	  contributors may be used to endorse or promote products derived
 * 	  from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/linkage.h>

#define STATE_PTR	%rdi	/* 1st arg */
#define DATA_PTR	%rsi	/* 2nd arg */
#define NUM_BLKS	%rdx	/* 3rd arg */

#define ABCD		%xmm0
#define E0		%xmm1	/* Need two E's b/c they ping pong */
#define E1		%xmm2
#define MSG0		%xmm3
#define MSG1		%xmm4
#define MSG2		%xmm5
#define MSG3		%xmm6
#define SHUF_MASK	%xmm7
#define ABCD_SAVED	%xmm8
#define E0_SAVED	%xmm9

/*
 * Intel SHA Extensions optimized implementation of a SHA-1 block function
 *
 * This function takes a pointer to the current SHA-1 state, a pointer to the
 * input data, and the number of 64-byte blocks to process.  The number of
 * blocks to process is assumed to be nonzero.  Once all blocks have been
 * processed, the state is updated with the new state.  This function only
 * processes complete blocks.  State initialization, buffering of partial
 * blocks, and digest finalization are expected to be handled elsewhere.
 *
 * The indented lines in the loop are instructions related to rounds processing.
 * The non-indented lines are instructions related to the message schedule.
 *
 * void sha1_ni_transform(struct sha1_block_state *state,
 *			  const u8 *data, size_t nblocks)
 */
.text
SYM_FUNC_START(sha1_ni_transform)

	/* Load the initial state from STATE_PTR. */
	pxor		E0, E0
	pinsrd		$3, 16(STATE_PTR), E0
	movdqu		(STATE_PTR), ABCD
	pshufd		$0x1B, ABCD, ABCD

	movdqa		PSHUFFLE_BYTE_FLIP_MASK(%rip), SHUF_MASK

.Lnext_block:
	/* Save the state for addition after the rounds. */
	movdqa		E0, E0_SAVED
	movdqa		ABCD, ABCD_SAVED

	/* Rounds 0-3 */
	movdqu		0*16(DATA_PTR), MSG0
	pshufb		SHUF_MASK, MSG0
		paddd		MSG0, E0
		movdqa		ABCD, E1
		sha1rnds4	$0, E0, ABCD

	/* Rounds 4-7 */
	movdqu		1*16(DATA_PTR), MSG1
	pshufb		SHUF_MASK, MSG1
		sha1nexte	MSG1, E1
		movdqa		ABCD, E0
		sha1rnds4	$0, E1, ABCD
	sha1msg1	MSG1, MSG0

	/* Rounds 8-11 */
	movdqu		2*16(DATA_PTR), MSG2
	pshufb		SHUF_MASK, MSG2
		sha1nexte	MSG2, E0
		movdqa		ABCD, E1
		sha1rnds4	$0, E0, ABCD
	sha1msg1	MSG2, MSG1
	pxor		MSG2, MSG0

	/* Rounds 12-15 */
	movdqu		3*16(DATA_PTR), MSG3
	pshufb		SHUF_MASK, MSG3
		sha1nexte	MSG3, E1
		movdqa		ABCD, E0
	sha1msg2	MSG3, MSG0
		sha1rnds4	$0, E1, ABCD
	sha1msg1	MSG3, MSG2
	pxor		MSG3, MSG1

	/* Rounds 16-19 */
		sha1nexte	MSG0, E0
		movdqa		ABCD, E1
	sha1msg2	MSG0, MSG1
		sha1rnds4	$0, E0, ABCD
	sha1msg1	MSG0, MSG3
	pxor		MSG0, MSG2

	/* Rounds 20-23 */
		sha1nexte	MSG1, E1
		movdqa		ABCD, E0
	sha1msg2	MSG1, MSG2
		sha1rnds4	$1, E1, ABCD
	sha1msg1	MSG1, MSG0
	pxor		MSG1, MSG3

	/* Rounds 24-27 */
		sha1nexte	MSG2, E0
		movdqa		ABCD, E1
	sha1msg2	MSG2, MSG3
		sha1rnds4	$1, E0, ABCD
	sha1msg1	MSG2, MSG1
	pxor		MSG2, MSG0

	/* Rounds 28-31 */
		sha1nexte	MSG3, E1
		movdqa		ABCD, E0
	sha1msg2	MSG3, MSG0
		sha1rnds4	$1, E1, ABCD
	sha1msg1	MSG3, MSG2
	pxor		MSG3, MSG1

	/* Rounds 32-35 */
		sha1nexte	MSG0, E0
		movdqa		ABCD, E1
	sha1msg2	MSG0, MSG1
		sha1rnds4	$1, E0, ABCD
	sha1msg1	MSG0, MSG3
	pxor		MSG0, MSG2

	/* Rounds 36-39 */
		sha1nexte	MSG1, E1
		movdqa		ABCD, E0
	sha1msg2	MSG1, MSG2
		sha1rnds4	$1, E1, ABCD
	sha1msg1	MSG1, MSG0
	pxor		MSG1, MSG3

	/* Rounds 40-43 */
		sha1nexte	MSG2, E0
		movdqa		ABCD, E1
	sha1msg2	MSG2, MSG3
		sha1rnds4	$2, E0, ABCD
	sha1msg1	MSG2, MSG1
	pxor		MSG2, MSG0

	/* Rounds 44-47 */
		sha1nexte	MSG3, E1
		movdqa		ABCD, E0
	sha1msg2	MSG3, MSG0
		sha1rnds4	$2, E1, ABCD
	sha1msg1	MSG3, MSG2
	pxor		MSG3, MSG1

	/* Rounds 48-51 */
		sha1nexte	MSG0, E0
		movdqa		ABCD, E1
	sha1msg2	MSG0, MSG1
		sha1rnds4	$2, E0, ABCD
	sha1msg1	MSG0, MSG3
	pxor		MSG0, MSG2

	/* Rounds 52-55 */
		sha1nexte	MSG1, E1
		movdqa		ABCD, E0
	sha1msg2	MSG1, MSG2
		sha1rnds4	$2, E1, ABCD
	sha1msg1	MSG1, MSG0
	pxor		MSG1, MSG3

	/* Rounds 56-59 */
		sha1nexte	MSG2, E0
		movdqa		ABCD, E1
	sha1msg2	MSG2, MSG3
		sha1rnds4	$2, E0, ABCD
	sha1msg1	MSG2, MSG1
	pxor		MSG2, MSG0

	/* Rounds 60-63 */
		sha1nexte	MSG3, E1
		movdqa		ABCD, E0
	sha1msg2	MSG3, MSG0
		sha1rnds4	$3, E1, ABCD
	sha1msg1	MSG3, MSG2
	pxor		MSG3, MSG1

	/* Rounds 64-67 */
		sha1nexte	MSG0, E0
		movdqa		ABCD, E1
	sha1msg2	MSG0, MSG1
		sha1rnds4	$3, E0, ABCD
	sha1msg1	MSG0, MSG3
	pxor		MSG0, MSG2

	/* Rounds 68-71 */
		sha1nexte	MSG1, E1
		movdqa		ABCD, E0
	sha1msg2	MSG1, MSG2
		sha1rnds4	$3, E1, ABCD
	pxor		MSG1, MSG3

	/* Rounds 72-75 */
		sha1nexte	MSG2, E0
		movdqa		ABCD, E1
	sha1msg2	MSG2, MSG3
		sha1rnds4	$3, E0, ABCD

	/* Rounds 76-79 */
		sha1nexte	MSG3, E1
		movdqa		ABCD, E0
		sha1rnds4	$3, E1, ABCD

	/* Add the previous state (before the rounds) to the current state. */
	sha1nexte	E0_SAVED, E0
	paddd		ABCD_SAVED, ABCD

	/* Advance to the next block, or break if there are no more blocks. */
	add		$64, DATA_PTR
	dec		NUM_BLKS
	jnz		.Lnext_block

	/* Store the new state to STATE_PTR. */
	pextrd		$3, E0, 16(STATE_PTR)
	pshufd		$0x1B, ABCD, ABCD
	movdqu		ABCD, (STATE_PTR)

	RET
SYM_FUNC_END(sha1_ni_transform)

.section	.rodata.cst16.PSHUFFLE_BYTE_FLIP_MASK, "aM", @progbits, 16
.align 16
PSHUFFLE_BYTE_FLIP_MASK:
	.octa 0x000102030405060708090a0b0c0d0e0f
