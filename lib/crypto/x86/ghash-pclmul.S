/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Accelerated GHASH implementation with Intel PCLMULQDQ-NI
 * instructions. This file contains accelerated part of ghash
 * implementation. More information about PCLMULQDQ can be found at:
 *
 * https://www.intel.com/content/dam/develop/external/us/en/documents/clmul-wp-rev-2-02-2014-04-20.pdf
 *
 * Copyright (c) 2009 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *	     Vinodh Gopal
 *	     Erdinc Ozturk
 *	     Deniz Karakoyunlu
 */

#include <linux/linkage.h>
#include <asm/frame.h>

.section	.rodata.cst16.bswap_mask, "aM", @progbits, 16
.align 16
.Lbswap_mask:
	.octa 0x000102030405060708090a0b0c0d0e0f

#define ACC	%xmm0
#define KEY	%xmm1
#define T1	%xmm2
#define T2	%xmm3
#define T3	%xmm4
#define BSWAP	%xmm5
#define IN1	%xmm6

.text

/*
 * __clmul_gf128mul_ble:	internal ABI
 * input:
 *	ACC:			operand1
 *	KEY:			operand2, hash_key << 1 mod poly
 * output:
 *	ACC:			operand1 * operand2 mod poly
 * changed:
 *	T1
 *	T2
 *	T3
 */
SYM_FUNC_START_LOCAL(__clmul_gf128mul_ble)
	movaps ACC, T1
	pshufd $0b01001110, ACC, T2
	pshufd $0b01001110, KEY, T3
	pxor ACC, T2
	pxor KEY, T3

	pclmulqdq $0x00, KEY, ACC	# ACC = a0 * b0
	pclmulqdq $0x11, KEY, T1	# T1 = a1 * b1
	pclmulqdq $0x00, T3, T2		# T2 = (a1 + a0) * (b1 + b0)
	pxor ACC, T2
	pxor T1, T2			# T2 = a0 * b1 + a1 * b0

	movaps T2, T3
	pslldq $8, T3
	psrldq $8, T2
	pxor T3, ACC
	pxor T2, T1			# <T1:ACC> is result of
					# carry-less multiplication

	# first phase of the reduction
	movaps ACC, T3
	psllq $1, T3
	pxor ACC, T3
	psllq $5, T3
	pxor ACC, T3
	psllq $57, T3
	movaps T3, T2
	pslldq $8, T2
	psrldq $8, T3
	pxor T2, ACC
	pxor T3, T1

	# second phase of the reduction
	movaps ACC, T2
	psrlq $5, T2
	pxor ACC, T2
	psrlq $1, T2
	pxor ACC, T2
	psrlq $1, T2
	pxor T2, T1
	pxor T1, ACC
	RET
SYM_FUNC_END(__clmul_gf128mul_ble)

/*
 * void polyval_mul_pclmul(struct polyval_elem *a,
 *			   const struct polyval_elem *b)
 */
SYM_FUNC_START(polyval_mul_pclmul)
	FRAME_BEGIN
	movups (%rdi), ACC
	movups (%rsi), KEY
	call __clmul_gf128mul_ble
	movups ACC, (%rdi)
	FRAME_END
	RET
SYM_FUNC_END(polyval_mul_pclmul)

/*
 * void ghash_blocks_pclmul(struct polyval_elem *acc,
 *			    const struct polyval_elem *key,
 *			    const u8 *data, size_t nblocks)
 */
SYM_FUNC_START(ghash_blocks_pclmul)
	FRAME_BEGIN
	movaps .Lbswap_mask(%rip), BSWAP
	movups (%rdi), ACC
	movups (%rsi), KEY
.align 4
.Lnext_block:
	movups (%rdx), IN1
	pshufb BSWAP, IN1
	pxor IN1, ACC
	call __clmul_gf128mul_ble
	add $16, %rdx
	dec %rcx
	jnz .Lnext_block
	movups ACC, (%rdi)
	FRAME_END
	RET
SYM_FUNC_END(ghash_blocks_pclmul)
