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

.macro do_4rounds	i, m0, m1, m2, m3, e0, e1
.if \i < 16
	movdqu		\i*4(DATA_PTR), \m0
	pshufb		SHUF_MASK, \m0
.endif
.if \i == 0
	paddd		\m0, \e0
.else
	sha1nexte	\m0, \e0
.endif
	movdqa		ABCD, \e1
.if \i >= 12 && \i < 76
	sha1msg2	\m0, \m1
.endif
	sha1rnds4	$\i / 20, \e0, ABCD
.if \i >= 4 && \i < 68
	sha1msg1	\m0, \m3
.endif
.if \i >= 8 && \i < 72
	pxor		\m0, \m2
.endif
.endm

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

.irp i, 0, 16, 32, 48, 64
	do_4rounds	(\i + 0),  MSG0, MSG1, MSG2, MSG3, E0, E1
	do_4rounds	(\i + 4),  MSG1, MSG2, MSG3, MSG0, E1, E0
	do_4rounds	(\i + 8),  MSG2, MSG3, MSG0, MSG1, E0, E1
	do_4rounds	(\i + 12), MSG3, MSG0, MSG1, MSG2, E1, E0
.endr

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
