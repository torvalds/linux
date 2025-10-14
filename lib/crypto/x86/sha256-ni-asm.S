/*
 * Intel SHA Extensions optimized implementation of a SHA-256 update function
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

#define SHA256CONSTANTS	%rax

#define MSG		%xmm0  /* sha256rnds2 implicit operand */
#define STATE0		%xmm1
#define STATE1		%xmm2
#define MSG0		%xmm3
#define MSG1		%xmm4
#define MSG2		%xmm5
#define MSG3		%xmm6
#define TMP		%xmm7

#define SHUF_MASK	%xmm8

#define ABEF_SAVE	%xmm9
#define CDGH_SAVE	%xmm10

.macro do_4rounds	i, m0, m1, m2, m3
.if \i < 16
	movdqu		\i*4(DATA_PTR), \m0
	pshufb		SHUF_MASK, \m0
.endif
	movdqa		(\i-32)*4(SHA256CONSTANTS), MSG
	paddd		\m0, MSG
	sha256rnds2	STATE0, STATE1
.if \i >= 12 && \i < 60
	movdqa		\m0, TMP
	palignr		$4, \m3, TMP
	paddd		TMP, \m1
	sha256msg2	\m0, \m1
.endif
	punpckhqdq	MSG, MSG
	sha256rnds2	STATE1, STATE0
.if \i >= 4 && \i < 52
	sha256msg1	\m0, \m3
.endif
.endm

/*
 * Intel SHA Extensions optimized implementation of a SHA-256 block function
 *
 * This function takes a pointer to the current SHA-256 state, a pointer to the
 * input data, and the number of 64-byte blocks to process.  Once all blocks
 * have been processed, the state is updated with the new state.  This function
 * only processes complete blocks.  State initialization, buffering of partial
 * blocks, and digest finalization is expected to be handled elsewhere.
 *
 * void sha256_ni_transform(struct sha256_block_state *state,
 *			    const u8 *data, size_t nblocks);
 */
.text
SYM_FUNC_START(sha256_ni_transform)

	shl		$6, NUM_BLKS		/*  convert to bytes */
	add		DATA_PTR, NUM_BLKS	/* pointer to end of data */

	/*
	 * load initial hash values
	 * Need to reorder these appropriately
	 * DCBA, HGFE -> ABEF, CDGH
	 */
	movdqu		0*16(STATE_PTR), STATE0		/* DCBA */
	movdqu		1*16(STATE_PTR), STATE1		/* HGFE */

	movdqa		STATE0, TMP
	punpcklqdq	STATE1, STATE0			/* FEBA */
	punpckhqdq	TMP, STATE1			/* DCHG */
	pshufd		$0x1B, STATE0, STATE0		/* ABEF */
	pshufd		$0xB1, STATE1, STATE1		/* CDGH */

	movdqa		PSHUFFLE_BYTE_FLIP_MASK(%rip), SHUF_MASK
	lea		K256+32*4(%rip), SHA256CONSTANTS

.Lloop0:
	/* Save hash values for addition after rounds */
	movdqa		STATE0, ABEF_SAVE
	movdqa		STATE1, CDGH_SAVE

.irp i, 0, 16, 32, 48
	do_4rounds	(\i + 0),  MSG0, MSG1, MSG2, MSG3
	do_4rounds	(\i + 4),  MSG1, MSG2, MSG3, MSG0
	do_4rounds	(\i + 8),  MSG2, MSG3, MSG0, MSG1
	do_4rounds	(\i + 12), MSG3, MSG0, MSG1, MSG2
.endr

	/* Add current hash values with previously saved */
	paddd		ABEF_SAVE, STATE0
	paddd		CDGH_SAVE, STATE1

	/* Increment data pointer and loop if more to process */
	add		$64, DATA_PTR
	cmp		NUM_BLKS, DATA_PTR
	jne		.Lloop0

	/* Write hash values back in the correct order */
	movdqa		STATE0, TMP
	punpcklqdq	STATE1, STATE0			/* GHEF */
	punpckhqdq	TMP, STATE1			/* ABCD */
	pshufd		$0xB1, STATE0, STATE0		/* HGFE */
	pshufd		$0x1B, STATE1, STATE1		/* DCBA */

	movdqu		STATE1, 0*16(STATE_PTR)
	movdqu		STATE0, 1*16(STATE_PTR)

	RET
SYM_FUNC_END(sha256_ni_transform)

#undef DIGEST_PTR
#undef DATA_PTR
#undef NUM_BLKS
#undef SHA256CONSTANTS
#undef MSG
#undef STATE0
#undef STATE1
#undef MSG0
#undef MSG1
#undef MSG2
#undef MSG3
#undef TMP
#undef SHUF_MASK
#undef ABEF_SAVE
#undef CDGH_SAVE

// parameters for sha256_ni_finup2x()
#define CTX		%rdi
#define DATA1		%rsi
#define DATA2		%rdx
#define LEN		%ecx
#define LEN8		%cl
#define LEN64		%rcx
#define OUT1		%r8
#define OUT2		%r9

// other scalar variables
#define SHA256CONSTANTS	%rax
#define COUNT		%r10
#define COUNT32		%r10d
#define FINAL_STEP	%r11d

// rbx is used as a temporary.

#define MSG		%xmm0	// sha256rnds2 implicit operand
#define STATE0_A	%xmm1
#define STATE1_A	%xmm2
#define STATE0_B	%xmm3
#define STATE1_B	%xmm4
#define TMP_A		%xmm5
#define TMP_B		%xmm6
#define MSG0_A		%xmm7
#define MSG1_A		%xmm8
#define MSG2_A		%xmm9
#define MSG3_A		%xmm10
#define MSG0_B		%xmm11
#define MSG1_B		%xmm12
#define MSG2_B		%xmm13
#define MSG3_B		%xmm14
#define SHUF_MASK	%xmm15

#define OFFSETOF_STATE		0  // offsetof(struct __sha256_ctx, state)
#define OFFSETOF_BYTECOUNT	32 // offsetof(struct __sha256_ctx, bytecount)
#define OFFSETOF_BUF		40 // offsetof(struct __sha256_ctx, buf)

// Do 4 rounds of SHA-256 for each of two messages (interleaved).  m0_a and m0_b
// contain the current 4 message schedule words for the first and second message
// respectively.
//
// If not all the message schedule words have been computed yet, then this also
// computes 4 more message schedule words for each message.  m1_a-m3_a contain
// the next 3 groups of 4 message schedule words for the first message, and
// likewise m1_b-m3_b for the second.  After consuming the current value of
// m0_a, this macro computes the group after m3_a and writes it to m0_a, and
// likewise for *_b.  This means that the next (m0_a, m1_a, m2_a, m3_a) is the
// current (m1_a, m2_a, m3_a, m0_a), and likewise for *_b, so the caller must
// cycle through the registers accordingly.
.macro	do_4rounds_2x	i, m0_a, m1_a, m2_a, m3_a,  m0_b, m1_b, m2_b, m3_b
	movdqa		(\i-32)*4(SHA256CONSTANTS), TMP_A
	movdqa		TMP_A, TMP_B
	paddd		\m0_a, TMP_A
	paddd		\m0_b, TMP_B
.if \i < 48
	sha256msg1	\m1_a, \m0_a
	sha256msg1	\m1_b, \m0_b
.endif
	movdqa		TMP_A, MSG
	sha256rnds2	STATE0_A, STATE1_A
	movdqa		TMP_B, MSG
	sha256rnds2	STATE0_B, STATE1_B
	pshufd 		$0x0E, TMP_A, MSG
	sha256rnds2	STATE1_A, STATE0_A
	pshufd 		$0x0E, TMP_B, MSG
	sha256rnds2	STATE1_B, STATE0_B
.if \i < 48
	movdqa		\m3_a, TMP_A
	movdqa		\m3_b, TMP_B
	palignr		$4, \m2_a, TMP_A
	palignr		$4, \m2_b, TMP_B
	paddd		TMP_A, \m0_a
	paddd		TMP_B, \m0_b
	sha256msg2	\m3_a, \m0_a
	sha256msg2	\m3_b, \m0_b
.endif
.endm

//
// void sha256_ni_finup2x(const struct __sha256_ctx *ctx,
//			  const u8 *data1, const u8 *data2, int len,
//			  u8 out1[SHA256_DIGEST_SIZE],
//			  u8 out2[SHA256_DIGEST_SIZE]);
//
// This function computes the SHA-256 digests of two messages |data1| and
// |data2| that are both |len| bytes long, starting from the initial context
// |ctx|.  |len| must be at least SHA256_BLOCK_SIZE.
//
// The instructions for the two SHA-256 operations are interleaved.  On many
// CPUs, this is almost twice as fast as hashing each message individually due
// to taking better advantage of the CPU's SHA-256 and SIMD throughput.
//
SYM_FUNC_START(sha256_ni_finup2x)
	// Allocate 128 bytes of stack space, 16-byte aligned.
	push		%rbx
	push		%rbp
	mov		%rsp, %rbp
	sub		$128, %rsp
	and		$~15, %rsp

	// Load the shuffle mask for swapping the endianness of 32-bit words.
	movdqa		PSHUFFLE_BYTE_FLIP_MASK(%rip), SHUF_MASK

	// Set up pointer to the round constants.
	lea		K256+32*4(%rip), SHA256CONSTANTS

	// Initially we're not processing the final blocks.
	xor		FINAL_STEP, FINAL_STEP

	// Load the initial state from ctx->state.
	movdqu		OFFSETOF_STATE+0*16(CTX), STATE0_A	// DCBA
	movdqu		OFFSETOF_STATE+1*16(CTX), STATE1_A	// HGFE
	movdqa		STATE0_A, TMP_A
	punpcklqdq	STATE1_A, STATE0_A			// FEBA
	punpckhqdq	TMP_A, STATE1_A				// DCHG
	pshufd		$0x1B, STATE0_A, STATE0_A		// ABEF
	pshufd		$0xB1, STATE1_A, STATE1_A		// CDGH

	// Load ctx->bytecount.  Take the mod 64 of it to get the number of
	// bytes that are buffered in ctx->buf.  Also save it in a register with
	// LEN added to it.
	mov		LEN, LEN
	mov		OFFSETOF_BYTECOUNT(CTX), %rbx
	lea		(%rbx, LEN64, 1), COUNT
	and		$63, %ebx
	jz		.Lfinup2x_enter_loop	// No bytes buffered?

	// %ebx bytes (1 to 63) are currently buffered in ctx->buf.  Load them
	// followed by the first 64 - %ebx bytes of data.  Since LEN >= 64, we
	// just load 64 bytes from each of ctx->buf, DATA1, and DATA2
	// unconditionally and rearrange the data as needed.

	movdqu		OFFSETOF_BUF+0*16(CTX), MSG0_A
	movdqu		OFFSETOF_BUF+1*16(CTX), MSG1_A
	movdqu		OFFSETOF_BUF+2*16(CTX), MSG2_A
	movdqu		OFFSETOF_BUF+3*16(CTX), MSG3_A
	movdqa		MSG0_A, 0*16(%rsp)
	movdqa		MSG1_A, 1*16(%rsp)
	movdqa		MSG2_A, 2*16(%rsp)
	movdqa		MSG3_A, 3*16(%rsp)

	movdqu		0*16(DATA1), MSG0_A
	movdqu		1*16(DATA1), MSG1_A
	movdqu		2*16(DATA1), MSG2_A
	movdqu		3*16(DATA1), MSG3_A
	movdqu		MSG0_A, 0*16(%rsp,%rbx)
	movdqu		MSG1_A, 1*16(%rsp,%rbx)
	movdqu		MSG2_A, 2*16(%rsp,%rbx)
	movdqu		MSG3_A, 3*16(%rsp,%rbx)
	movdqa		0*16(%rsp), MSG0_A
	movdqa		1*16(%rsp), MSG1_A
	movdqa		2*16(%rsp), MSG2_A
	movdqa		3*16(%rsp), MSG3_A

	movdqu		0*16(DATA2), MSG0_B
	movdqu		1*16(DATA2), MSG1_B
	movdqu		2*16(DATA2), MSG2_B
	movdqu		3*16(DATA2), MSG3_B
	movdqu		MSG0_B, 0*16(%rsp,%rbx)
	movdqu		MSG1_B, 1*16(%rsp,%rbx)
	movdqu		MSG2_B, 2*16(%rsp,%rbx)
	movdqu		MSG3_B, 3*16(%rsp,%rbx)
	movdqa		0*16(%rsp), MSG0_B
	movdqa		1*16(%rsp), MSG1_B
	movdqa		2*16(%rsp), MSG2_B
	movdqa		3*16(%rsp), MSG3_B

	sub		$64, %rbx 	// rbx = buffered - 64
	sub		%rbx, DATA1	// DATA1 += 64 - buffered
	sub		%rbx, DATA2	// DATA2 += 64 - buffered
	add		%ebx, LEN	// LEN += buffered - 64
	movdqa		STATE0_A, STATE0_B
	movdqa		STATE1_A, STATE1_B
	jmp		.Lfinup2x_loop_have_data

.Lfinup2x_enter_loop:
	sub		$64, LEN
	movdqa		STATE0_A, STATE0_B
	movdqa		STATE1_A, STATE1_B
.Lfinup2x_loop:
	// Load the next two data blocks.
	movdqu		0*16(DATA1), MSG0_A
	movdqu		0*16(DATA2), MSG0_B
	movdqu		1*16(DATA1), MSG1_A
	movdqu		1*16(DATA2), MSG1_B
	movdqu		2*16(DATA1), MSG2_A
	movdqu		2*16(DATA2), MSG2_B
	movdqu		3*16(DATA1), MSG3_A
	movdqu		3*16(DATA2), MSG3_B
	add		$64, DATA1
	add		$64, DATA2
.Lfinup2x_loop_have_data:
	// Convert the words of the data blocks from big endian.
	pshufb		SHUF_MASK, MSG0_A
	pshufb		SHUF_MASK, MSG0_B
	pshufb		SHUF_MASK, MSG1_A
	pshufb		SHUF_MASK, MSG1_B
	pshufb		SHUF_MASK, MSG2_A
	pshufb		SHUF_MASK, MSG2_B
	pshufb		SHUF_MASK, MSG3_A
	pshufb		SHUF_MASK, MSG3_B
.Lfinup2x_loop_have_bswapped_data:

	// Save the original state for each block.
	movdqa		STATE0_A, 0*16(%rsp)
	movdqa		STATE0_B, 1*16(%rsp)
	movdqa		STATE1_A, 2*16(%rsp)
	movdqa		STATE1_B, 3*16(%rsp)

	// Do the SHA-256 rounds on each block.
.irp i, 0, 16, 32, 48
	do_4rounds_2x	(\i + 0),  MSG0_A, MSG1_A, MSG2_A, MSG3_A, \
				   MSG0_B, MSG1_B, MSG2_B, MSG3_B
	do_4rounds_2x	(\i + 4),  MSG1_A, MSG2_A, MSG3_A, MSG0_A, \
				   MSG1_B, MSG2_B, MSG3_B, MSG0_B
	do_4rounds_2x	(\i + 8),  MSG2_A, MSG3_A, MSG0_A, MSG1_A, \
				   MSG2_B, MSG3_B, MSG0_B, MSG1_B
	do_4rounds_2x	(\i + 12), MSG3_A, MSG0_A, MSG1_A, MSG2_A, \
				   MSG3_B, MSG0_B, MSG1_B, MSG2_B
.endr

	// Add the original state for each block.
	paddd		0*16(%rsp), STATE0_A
	paddd		1*16(%rsp), STATE0_B
	paddd		2*16(%rsp), STATE1_A
	paddd		3*16(%rsp), STATE1_B

	// Update LEN and loop back if more blocks remain.
	sub		$64, LEN
	jge		.Lfinup2x_loop

	// Check if any final blocks need to be handled.
	// FINAL_STEP = 2: all done
	// FINAL_STEP = 1: need to do count-only padding block
	// FINAL_STEP = 0: need to do the block with 0x80 padding byte
	cmp		$1, FINAL_STEP
	jg		.Lfinup2x_done
	je		.Lfinup2x_finalize_countonly
	add		$64, LEN
	jz		.Lfinup2x_finalize_blockaligned

	// Not block-aligned; 1 <= LEN <= 63 data bytes remain.  Pad the block.
	// To do this, write the padding starting with the 0x80 byte to
	// &sp[64].  Then for each message, copy the last 64 data bytes to sp
	// and load from &sp[64 - LEN] to get the needed padding block.  This
	// code relies on the data buffers being >= 64 bytes in length.
	mov		$64, %ebx
	sub		LEN, %ebx		// ebx = 64 - LEN
	sub		%rbx, DATA1		// DATA1 -= 64 - LEN
	sub		%rbx, DATA2		// DATA2 -= 64 - LEN
	mov		$0x80, FINAL_STEP   // using FINAL_STEP as a temporary
	movd		FINAL_STEP, MSG0_A
	pxor		MSG1_A, MSG1_A
	movdqa		MSG0_A, 4*16(%rsp)
	movdqa		MSG1_A, 5*16(%rsp)
	movdqa		MSG1_A, 6*16(%rsp)
	movdqa		MSG1_A, 7*16(%rsp)
	cmp		$56, LEN
	jge		1f	// will COUNT spill into its own block?
	shl		$3, COUNT
	bswap		COUNT
	mov		COUNT, 56(%rsp,%rbx)
	mov		$2, FINAL_STEP	// won't need count-only block
	jmp		2f
1:
	mov		$1, FINAL_STEP	// will need count-only block
2:
	movdqu		0*16(DATA1), MSG0_A
	movdqu		1*16(DATA1), MSG1_A
	movdqu		2*16(DATA1), MSG2_A
	movdqu		3*16(DATA1), MSG3_A
	movdqa		MSG0_A, 0*16(%rsp)
	movdqa		MSG1_A, 1*16(%rsp)
	movdqa		MSG2_A, 2*16(%rsp)
	movdqa		MSG3_A, 3*16(%rsp)
	movdqu		0*16(%rsp,%rbx), MSG0_A
	movdqu		1*16(%rsp,%rbx), MSG1_A
	movdqu		2*16(%rsp,%rbx), MSG2_A
	movdqu		3*16(%rsp,%rbx), MSG3_A

	movdqu		0*16(DATA2), MSG0_B
	movdqu		1*16(DATA2), MSG1_B
	movdqu		2*16(DATA2), MSG2_B
	movdqu		3*16(DATA2), MSG3_B
	movdqa		MSG0_B, 0*16(%rsp)
	movdqa		MSG1_B, 1*16(%rsp)
	movdqa		MSG2_B, 2*16(%rsp)
	movdqa		MSG3_B, 3*16(%rsp)
	movdqu		0*16(%rsp,%rbx), MSG0_B
	movdqu		1*16(%rsp,%rbx), MSG1_B
	movdqu		2*16(%rsp,%rbx), MSG2_B
	movdqu		3*16(%rsp,%rbx), MSG3_B
	jmp		.Lfinup2x_loop_have_data

	// Prepare a padding block, either:
	//
	//	{0x80, 0, 0, 0, ..., count (as __be64)}
	//	This is for a block aligned message.
	//
	//	{   0, 0, 0, 0, ..., count (as __be64)}
	//	This is for a message whose length mod 64 is >= 56.
	//
	// Pre-swap the endianness of the words.
.Lfinup2x_finalize_countonly:
	pxor		MSG0_A, MSG0_A
	jmp		1f

.Lfinup2x_finalize_blockaligned:
	mov		$0x80000000, %ebx
	movd		%ebx, MSG0_A
1:
	pxor		MSG1_A, MSG1_A
	pxor		MSG2_A, MSG2_A
	ror		$29, COUNT
	movq		COUNT, MSG3_A
	pslldq		$8, MSG3_A
	movdqa		MSG0_A, MSG0_B
	pxor		MSG1_B, MSG1_B
	pxor		MSG2_B, MSG2_B
	movdqa		MSG3_A, MSG3_B
	mov		$2, FINAL_STEP
	jmp		.Lfinup2x_loop_have_bswapped_data

.Lfinup2x_done:
	// Write the two digests with all bytes in the correct order.
	movdqa		STATE0_A, TMP_A
	movdqa		STATE0_B, TMP_B
	punpcklqdq	STATE1_A, STATE0_A		// GHEF
	punpcklqdq	STATE1_B, STATE0_B
	punpckhqdq	TMP_A, STATE1_A			// ABCD
	punpckhqdq	TMP_B, STATE1_B
	pshufd		$0xB1, STATE0_A, STATE0_A	// HGFE
	pshufd		$0xB1, STATE0_B, STATE0_B
	pshufd		$0x1B, STATE1_A, STATE1_A	// DCBA
	pshufd		$0x1B, STATE1_B, STATE1_B
	pshufb		SHUF_MASK, STATE0_A
	pshufb		SHUF_MASK, STATE0_B
	pshufb		SHUF_MASK, STATE1_A
	pshufb		SHUF_MASK, STATE1_B
	movdqu		STATE0_A, 1*16(OUT1)
	movdqu		STATE0_B, 1*16(OUT2)
	movdqu		STATE1_A, 0*16(OUT1)
	movdqu		STATE1_B, 0*16(OUT2)

	mov		%rbp, %rsp
	pop		%rbp
	pop		%rbx
	RET
SYM_FUNC_END(sha256_ni_finup2x)

.section	.rodata.cst256.K256, "aM", @progbits, 256
.align 64
K256:
	.long	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	.long	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	.long	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	.long	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	.long	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	.long	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	.long	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	.long	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	.long	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	.long	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	.long	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	.long	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	.long	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	.long	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	.long	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	.long	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2

.section	.rodata.cst16.PSHUFFLE_BYTE_FLIP_MASK, "aM", @progbits, 16
.align 16
PSHUFFLE_BYTE_FLIP_MASK:
	.octa 0x0c0d0e0f08090a0b0405060700010203
