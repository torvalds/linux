/*
 * Implement fast Fletcher4 with SSE2,SSSE3 instructions. (x86)
 *
 * Use the 128-bit SSE2/SSSE3 SIMD instructions and registers to compute
 * Fletcher4 in two incremental 64-bit parallel accumulator streams,
 * and then combine the streams to form the final four checksum words.
 * This implementation is a derivative of the AVX SIMD implementation by
 * James Guilford and Jinshan Xiong from Intel (see zfs_fletcher_intel.c).
 *
 * Copyright (C) 2016 Tyler J. Stachecki.
 *
 * Authors:
 *	Tyler J. Stachecki <stachecki.tyler@gmail.com>
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#if defined(HAVE_SSE2)

#include <linux/simd_x86.h>
#include <sys/spa_checksum.h>
#include <sys/byteorder.h>
#include <zfs_fletcher.h>
#include <strings.h>

static void
fletcher_4_sse2_init(fletcher_4_ctx_t *ctx)
{
	bzero(ctx->sse, 4 * sizeof (zfs_fletcher_sse_t));
}

static void
fletcher_4_sse2_fini(fletcher_4_ctx_t *ctx, zio_cksum_t *zcp)
{
	uint64_t A, B, C, D;

	/*
	 * The mixing matrix for checksum calculation is:
	 * a = a0 + a1
	 * b = 2b0 + 2b1 - a1
	 * c = 4c0 - b0 + 4c1 -3b1
	 * d = 8d0 - 4c0 + 8d1 - 8c1 + b1;
	 *
	 * c and d are multiplied by 4 and 8, respectively,
	 * before spilling the vectors out to memory.
	 */
	A = ctx->sse[0].v[0] + ctx->sse[0].v[1];
	B = 2 * ctx->sse[1].v[0] + 2 * ctx->sse[1].v[1] - ctx->sse[0].v[1];
	C = 4 * ctx->sse[2].v[0] - ctx->sse[1].v[0] + 4 * ctx->sse[2].v[1] -
	    3 * ctx->sse[1].v[1];
	D = 8 * ctx->sse[3].v[0] - 4 * ctx->sse[2].v[0] + 8 * ctx->sse[3].v[1] -
	    8 * ctx->sse[2].v[1] + ctx->sse[1].v[1];

	ZIO_SET_CHECKSUM(zcp, A, B, C, D);
}

#define	FLETCHER_4_SSE_RESTORE_CTX(ctx)					\
{									\
	asm volatile("movdqu %0, %%xmm0" :: "m" ((ctx)->sse[0]));	\
	asm volatile("movdqu %0, %%xmm1" :: "m" ((ctx)->sse[1]));	\
	asm volatile("movdqu %0, %%xmm2" :: "m" ((ctx)->sse[2]));	\
	asm volatile("movdqu %0, %%xmm3" :: "m" ((ctx)->sse[3]));	\
}

#define	FLETCHER_4_SSE_SAVE_CTX(ctx)					\
{									\
	asm volatile("movdqu %%xmm0, %0" : "=m" ((ctx)->sse[0]));	\
	asm volatile("movdqu %%xmm1, %0" : "=m" ((ctx)->sse[1]));	\
	asm volatile("movdqu %%xmm2, %0" : "=m" ((ctx)->sse[2]));	\
	asm volatile("movdqu %%xmm3, %0" : "=m" ((ctx)->sse[3]));	\
}

static void
fletcher_4_sse2_native(fletcher_4_ctx_t *ctx, const void *buf, uint64_t size)
{
	const uint64_t *ip = buf;
	const uint64_t *ipend = (uint64_t *)((uint8_t *)ip + size);

	kfpu_begin();

	FLETCHER_4_SSE_RESTORE_CTX(ctx);

	asm volatile("pxor %xmm4, %xmm4");

	for (; ip < ipend; ip += 2) {
		asm volatile("movdqu %0, %%xmm5" :: "m"(*ip));
		asm volatile("movdqa %xmm5, %xmm6");
		asm volatile("punpckldq %xmm4, %xmm5");
		asm volatile("punpckhdq %xmm4, %xmm6");
		asm volatile("paddq %xmm5, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
		asm volatile("paddq %xmm6, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
	}

	FLETCHER_4_SSE_SAVE_CTX(ctx);

	kfpu_end();
}

static void
fletcher_4_sse2_byteswap(fletcher_4_ctx_t *ctx, const void *buf, uint64_t size)
{
	const uint32_t *ip = buf;
	const uint32_t *ipend = (uint32_t *)((uint8_t *)ip + size);

	kfpu_begin();

	FLETCHER_4_SSE_RESTORE_CTX(ctx);

	for (; ip < ipend; ip += 2) {
		uint32_t scratch1 = BSWAP_32(ip[0]);
		uint32_t scratch2 = BSWAP_32(ip[1]);
		asm volatile("movd %0, %%xmm5" :: "r"(scratch1));
		asm volatile("movd %0, %%xmm6" :: "r"(scratch2));
		asm volatile("punpcklqdq %xmm6, %xmm5");
		asm volatile("paddq %xmm5, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
	}

	FLETCHER_4_SSE_SAVE_CTX(ctx);

	kfpu_end();
}

static boolean_t fletcher_4_sse2_valid(void)
{
	return (zfs_sse2_available());
}

const fletcher_4_ops_t fletcher_4_sse2_ops = {
	.init_native = fletcher_4_sse2_init,
	.fini_native = fletcher_4_sse2_fini,
	.compute_native = fletcher_4_sse2_native,
	.init_byteswap = fletcher_4_sse2_init,
	.fini_byteswap = fletcher_4_sse2_fini,
	.compute_byteswap = fletcher_4_sse2_byteswap,
	.valid = fletcher_4_sse2_valid,
	.name = "sse2"
};

#endif /* defined(HAVE_SSE2) */

#if defined(HAVE_SSE2) && defined(HAVE_SSSE3)
static void
fletcher_4_ssse3_byteswap(fletcher_4_ctx_t *ctx, const void *buf, uint64_t size)
{
	static const zfs_fletcher_sse_t mask = {
		.v = { 0x0405060700010203, 0x0C0D0E0F08090A0B }
	};

	const uint64_t *ip = buf;
	const uint64_t *ipend = (uint64_t *)((uint8_t *)ip + size);

	kfpu_begin();

	FLETCHER_4_SSE_RESTORE_CTX(ctx);

	asm volatile("movdqu %0, %%xmm7"::"m" (mask));
	asm volatile("pxor %xmm4, %xmm4");

	for (; ip < ipend; ip += 2) {
		asm volatile("movdqu %0, %%xmm5"::"m" (*ip));
		asm volatile("pshufb %xmm7, %xmm5");
		asm volatile("movdqa %xmm5, %xmm6");
		asm volatile("punpckldq %xmm4, %xmm5");
		asm volatile("punpckhdq %xmm4, %xmm6");
		asm volatile("paddq %xmm5, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
		asm volatile("paddq %xmm6, %xmm0");
		asm volatile("paddq %xmm0, %xmm1");
		asm volatile("paddq %xmm1, %xmm2");
		asm volatile("paddq %xmm2, %xmm3");
	}

	FLETCHER_4_SSE_SAVE_CTX(ctx);

	kfpu_end();
}

static boolean_t fletcher_4_ssse3_valid(void)
{
	return (zfs_sse2_available() && zfs_ssse3_available());
}

const fletcher_4_ops_t fletcher_4_ssse3_ops = {
	.init_native = fletcher_4_sse2_init,
	.fini_native = fletcher_4_sse2_fini,
	.compute_native = fletcher_4_sse2_native,
	.init_byteswap = fletcher_4_sse2_init,
	.fini_byteswap = fletcher_4_sse2_fini,
	.compute_byteswap = fletcher_4_ssse3_byteswap,
	.valid = fletcher_4_ssse3_valid,
	.name = "ssse3"
};

#endif /* defined(HAVE_SSE2) && defined(HAVE_SSSE3) */
