/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Core SHA-224/SHA-256 transform using v8 Crypto Extensions
 *
 * Copyright (C) 2014 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

	.text
	.arch		armv8-a+crypto

	dga		.req	q20
	dgav		.req	v20
	dgb		.req	q21
	dgbv		.req	v21

	t0		.req	v22
	t1		.req	v23

	dg0q		.req	q24
	dg0v		.req	v24
	dg1q		.req	q25
	dg1v		.req	v25
	dg2q		.req	q26
	dg2v		.req	v26

	.macro		add_only, ev, rc, s0
	mov		dg2v.16b, dg0v.16b
	.ifeq		\ev
	add		t1.4s, v\s0\().4s, \rc\().4s
	sha256h		dg0q, dg1q, t0.4s
	sha256h2	dg1q, dg2q, t0.4s
	.else
	.ifnb		\s0
	add		t0.4s, v\s0\().4s, \rc\().4s
	.endif
	sha256h		dg0q, dg1q, t1.4s
	sha256h2	dg1q, dg2q, t1.4s
	.endif
	.endm

	.macro		add_update, ev, rc, s0, s1, s2, s3
	sha256su0	v\s0\().4s, v\s1\().4s
	add_only	\ev, \rc, \s1
	sha256su1	v\s0\().4s, v\s2\().4s, v\s3\().4s
	.endm

	/*
	 * The SHA-256 round constants
	 */
	.section	".rodata", "a"
	.align		4
.Lsha2_rcon:
	.word		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
	.word		0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
	.word		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
	.word		0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
	.word		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
	.word		0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
	.word		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
	.word		0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
	.word		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
	.word		0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
	.word		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
	.word		0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
	.word		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
	.word		0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
	.word		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
	.word		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2

	.macro load_round_constants	tmp
	adr_l		\tmp, .Lsha2_rcon
	ld1		{ v0.4s- v3.4s}, [\tmp], #64
	ld1		{ v4.4s- v7.4s}, [\tmp], #64
	ld1		{ v8.4s-v11.4s}, [\tmp], #64
	ld1		{v12.4s-v15.4s}, [\tmp]
	.endm

	/*
	 * size_t __sha256_ce_transform(struct sha256_block_state *state,
	 *				const u8 *data, size_t nblocks);
	 */
	.text
SYM_FUNC_START(__sha256_ce_transform)

	load_round_constants	x8

	/* load state */
	ld1		{dgav.4s, dgbv.4s}, [x0]

	/* load input */
0:	ld1		{v16.4s-v19.4s}, [x1], #64
	sub		x2, x2, #1

CPU_LE(	rev32		v16.16b, v16.16b	)
CPU_LE(	rev32		v17.16b, v17.16b	)
CPU_LE(	rev32		v18.16b, v18.16b	)
CPU_LE(	rev32		v19.16b, v19.16b	)

	add		t0.4s, v16.4s, v0.4s
	mov		dg0v.16b, dgav.16b
	mov		dg1v.16b, dgbv.16b

	add_update	0,  v1, 16, 17, 18, 19
	add_update	1,  v2, 17, 18, 19, 16
	add_update	0,  v3, 18, 19, 16, 17
	add_update	1,  v4, 19, 16, 17, 18

	add_update	0,  v5, 16, 17, 18, 19
	add_update	1,  v6, 17, 18, 19, 16
	add_update	0,  v7, 18, 19, 16, 17
	add_update	1,  v8, 19, 16, 17, 18

	add_update	0,  v9, 16, 17, 18, 19
	add_update	1, v10, 17, 18, 19, 16
	add_update	0, v11, 18, 19, 16, 17
	add_update	1, v12, 19, 16, 17, 18

	add_only	0, v13, 17
	add_only	1, v14, 18
	add_only	0, v15, 19
	add_only	1

	/* update state */
	add		dgav.4s, dgav.4s, dg0v.4s
	add		dgbv.4s, dgbv.4s, dg1v.4s

	/* return early if voluntary preemption is needed */
	cond_yield	1f, x5, x6

	/* handled all input blocks? */
	cbnz		x2, 0b

	/* store new state */
1:	st1		{dgav.4s, dgbv.4s}, [x0]
	mov		x0, x2
	ret
SYM_FUNC_END(__sha256_ce_transform)

	.unreq dga
	.unreq dgav
	.unreq dgb
	.unreq dgbv
	.unreq t0
	.unreq t1
	.unreq dg0q
	.unreq dg0v
	.unreq dg1q
	.unreq dg1v
	.unreq dg2q
	.unreq dg2v

	// parameters for sha256_ce_finup2x()
	ctx		.req	x0
	data1		.req	x1
	data2		.req	x2
	len		.req	w3
	out1		.req	x4
	out2		.req	x5

	// other scalar variables
	count		.req	x6
	final_step	.req	w7

	// x8-x9 are used as temporaries.

	// v0-v15 are used to cache the SHA-256 round constants.
	// v16-v19 are used for the message schedule for the first message.
	// v20-v23 are used for the message schedule for the second message.
	// v24-v31 are used for the state and temporaries as given below.
	// *_a are for the first message and *_b for the second.
	state0_a_q	.req	q24
	state0_a	.req	v24
	state1_a_q	.req	q25
	state1_a	.req	v25
	state0_b_q	.req	q26
	state0_b	.req	v26
	state1_b_q	.req	q27
	state1_b	.req	v27
	t0_a		.req	v28
	t0_b		.req	v29
	t1_a_q		.req	q30
	t1_a		.req	v30
	t1_b_q		.req	q31
	t1_b		.req	v31

#define OFFSETOF_BYTECOUNT	32 // offsetof(struct __sha256_ctx, bytecount)
#define OFFSETOF_BUF		40 // offsetof(struct __sha256_ctx, buf)
// offsetof(struct __sha256_ctx, state) is assumed to be 0.

	// Do 4 rounds of SHA-256 for each of two messages (interleaved).  m0_a
	// and m0_b contain the current 4 message schedule words for the first
	// and second message respectively.
	//
	// If not all the message schedule words have been computed yet, then
	// this also computes 4 more message schedule words for each message.
	// m1_a-m3_a contain the next 3 groups of 4 message schedule words for
	// the first message, and likewise m1_b-m3_b for the second.  After
	// consuming the current value of m0_a, this macro computes the group
	// after m3_a and writes it to m0_a, and likewise for *_b.  This means
	// that the next (m0_a, m1_a, m2_a, m3_a) is the current (m1_a, m2_a,
	// m3_a, m0_a), and likewise for *_b, so the caller must cycle through
	// the registers accordingly.
	.macro	do_4rounds_2x	i, k,  m0_a, m1_a, m2_a, m3_a,  \
				       m0_b, m1_b, m2_b, m3_b
	add		t0_a\().4s, \m0_a\().4s, \k\().4s
	add		t0_b\().4s, \m0_b\().4s, \k\().4s
	.if \i < 48
	sha256su0	\m0_a\().4s, \m1_a\().4s
	sha256su0	\m0_b\().4s, \m1_b\().4s
	sha256su1	\m0_a\().4s, \m2_a\().4s, \m3_a\().4s
	sha256su1	\m0_b\().4s, \m2_b\().4s, \m3_b\().4s
	.endif
	mov		t1_a.16b, state0_a.16b
	mov		t1_b.16b, state0_b.16b
	sha256h		state0_a_q, state1_a_q, t0_a\().4s
	sha256h		state0_b_q, state1_b_q, t0_b\().4s
	sha256h2	state1_a_q, t1_a_q, t0_a\().4s
	sha256h2	state1_b_q, t1_b_q, t0_b\().4s
	.endm

	.macro	do_16rounds_2x	i, k0, k1, k2, k3
	do_4rounds_2x	\i + 0,  \k0,  v16, v17, v18, v19,  v20, v21, v22, v23
	do_4rounds_2x	\i + 4,  \k1,  v17, v18, v19, v16,  v21, v22, v23, v20
	do_4rounds_2x	\i + 8,  \k2,  v18, v19, v16, v17,  v22, v23, v20, v21
	do_4rounds_2x	\i + 12, \k3,  v19, v16, v17, v18,  v23, v20, v21, v22
	.endm

//
// void sha256_ce_finup2x(const struct __sha256_ctx *ctx,
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
SYM_FUNC_START(sha256_ce_finup2x)
	sub		sp, sp, #128
	mov		final_step, #0
	load_round_constants	x8

	// Load the initial state from ctx->state.
	ld1		{state0_a.4s-state1_a.4s}, [ctx]

	// Load ctx->bytecount.  Take the mod 64 of it to get the number of
	// bytes that are buffered in ctx->buf.  Also save it in a register with
	// len added to it.
	ldr		x8, [ctx, #OFFSETOF_BYTECOUNT]
	add		count, x8, len, sxtw
	and		x8, x8, #63
	cbz		x8, .Lfinup2x_enter_loop	// No bytes buffered?

	// x8 bytes (1 to 63) are currently buffered in ctx->buf.  Load them
	// followed by the first 64 - x8 bytes of data.  Since len >= 64, we
	// just load 64 bytes from each of ctx->buf, data1, and data2
	// unconditionally and rearrange the data as needed.
	add		x9, ctx, #OFFSETOF_BUF
	ld1		{v16.16b-v19.16b}, [x9]
	st1		{v16.16b-v19.16b}, [sp]

	ld1		{v16.16b-v19.16b}, [data1], #64
	add		x9, sp, x8
	st1		{v16.16b-v19.16b}, [x9]
	ld1		{v16.4s-v19.4s}, [sp]

	ld1		{v20.16b-v23.16b}, [data2], #64
	st1		{v20.16b-v23.16b}, [x9]
	ld1		{v20.4s-v23.4s}, [sp]

	sub		len, len, #64
	sub		data1, data1, x8
	sub		data2, data2, x8
	add		len, len, w8
	mov		state0_b.16b, state0_a.16b
	mov		state1_b.16b, state1_a.16b
	b		.Lfinup2x_loop_have_data

.Lfinup2x_enter_loop:
	sub		len, len, #64
	mov		state0_b.16b, state0_a.16b
	mov		state1_b.16b, state1_a.16b
.Lfinup2x_loop:
	// Load the next two data blocks.
	ld1		{v16.4s-v19.4s}, [data1], #64
	ld1		{v20.4s-v23.4s}, [data2], #64
.Lfinup2x_loop_have_data:
	// Convert the words of the data blocks from big endian.
CPU_LE(	rev32		v16.16b, v16.16b	)
CPU_LE(	rev32		v17.16b, v17.16b	)
CPU_LE(	rev32		v18.16b, v18.16b	)
CPU_LE(	rev32		v19.16b, v19.16b	)
CPU_LE(	rev32		v20.16b, v20.16b	)
CPU_LE(	rev32		v21.16b, v21.16b	)
CPU_LE(	rev32		v22.16b, v22.16b	)
CPU_LE(	rev32		v23.16b, v23.16b	)
.Lfinup2x_loop_have_bswapped_data:

	// Save the original state for each block.
	st1		{state0_a.4s-state1_b.4s}, [sp]

	// Do the SHA-256 rounds on each block.
	do_16rounds_2x	0,  v0, v1, v2, v3
	do_16rounds_2x	16, v4, v5, v6, v7
	do_16rounds_2x	32, v8, v9, v10, v11
	do_16rounds_2x	48, v12, v13, v14, v15

	// Add the original state for each block.
	ld1		{v16.4s-v19.4s}, [sp]
	add		state0_a.4s, state0_a.4s, v16.4s
	add		state1_a.4s, state1_a.4s, v17.4s
	add		state0_b.4s, state0_b.4s, v18.4s
	add		state1_b.4s, state1_b.4s, v19.4s

	// Update len and loop back if more blocks remain.
	sub		len, len, #64
	tbz		len, #31, .Lfinup2x_loop	// len >= 0?

	// Check if any final blocks need to be handled.
	// final_step = 2: all done
	// final_step = 1: need to do count-only padding block
	// final_step = 0: need to do the block with 0x80 padding byte
	tbnz		final_step, #1, .Lfinup2x_done
	tbnz		final_step, #0, .Lfinup2x_finalize_countonly
	add		len, len, #64
	cbz		len, .Lfinup2x_finalize_blockaligned

	// Not block-aligned; 1 <= len <= 63 data bytes remain.  Pad the block.
	// To do this, write the padding starting with the 0x80 byte to
	// &sp[64].  Then for each message, copy the last 64 data bytes to sp
	// and load from &sp[64 - len] to get the needed padding block.  This
	// code relies on the data buffers being >= 64 bytes in length.
	sub		w8, len, #64		// w8 = len - 64
	add		data1, data1, w8, sxtw	// data1 += len - 64
	add		data2, data2, w8, sxtw	// data2 += len - 64
CPU_LE(	mov		x9, #0x80		)
CPU_LE(	fmov		d16, x9			)
CPU_BE(	movi		v16.16b, #0		)
CPU_BE(	mov		x9, #0x8000000000000000	)
CPU_BE(	mov		v16.d[1], x9		)
	movi		v17.16b, #0
	stp		q16, q17, [sp, #64]
	stp		q17, q17, [sp, #96]
	sub		x9, sp, w8, sxtw	// x9 = &sp[64 - len]
	cmp		len, #56
	b.ge		1f		// will count spill into its own block?
	lsl		count, count, #3
CPU_LE(	rev		count, count		)
	str		count, [x9, #56]
	mov		final_step, #2	// won't need count-only block
	b		2f
1:
	mov		final_step, #1	// will need count-only block
2:
	ld1		{v16.16b-v19.16b}, [data1]
	st1		{v16.16b-v19.16b}, [sp]
	ld1		{v16.4s-v19.4s}, [x9]
	ld1		{v20.16b-v23.16b}, [data2]
	st1		{v20.16b-v23.16b}, [sp]
	ld1		{v20.4s-v23.4s}, [x9]
	b		.Lfinup2x_loop_have_data

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
	movi		v16.2d, #0
	b		1f
.Lfinup2x_finalize_blockaligned:
	mov		x8, #0x80000000
	fmov		d16, x8
1:
	movi		v17.2d, #0
	movi		v18.2d, #0
	ror		count, count, #29	// ror(lsl(count, 3), 32)
	mov		v19.d[0], xzr
	mov		v19.d[1], count
	mov		v20.16b, v16.16b
	movi		v21.2d, #0
	movi		v22.2d, #0
	mov		v23.16b, v19.16b
	mov		final_step, #2
	b		.Lfinup2x_loop_have_bswapped_data

.Lfinup2x_done:
	// Write the two digests with all bytes in the correct order.
CPU_LE(	rev32		state0_a.16b, state0_a.16b	)
CPU_LE(	rev32		state1_a.16b, state1_a.16b	)
CPU_LE(	rev32		state0_b.16b, state0_b.16b	)
CPU_LE(	rev32		state1_b.16b, state1_b.16b	)
	st1		{state0_a.4s-state1_a.4s}, [out1]
	st1		{state0_b.4s-state1_b.4s}, [out2]
	add		sp, sp, #128
	ret
SYM_FUNC_END(sha256_ce_finup2x)
