/*
 * Implement fast Fletcher4 with AVX2 instructions. (x86_64)
 *
 * Use the 256-bit AVX2 SIMD instructions and registers to compute
 * Fletcher4 in four incremental 64-bit parallel accumulator streams,
 * and then combine the streams to form the final four checksum words.
 *
 * Copyright (C) 2015 Intel Corporation.
 *
 * Authors:
 *      James Guilford <james.guilford@intel.com>
 *      Jinshan Xiong <jinshan.xiong@intel.com>
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

#if defined(HAVE_AVX) && defined(HAVE_AVX2)

#include <linux/simd_x86.h>
#include <sys/spa_checksum.h>
#include <zfs_fletcher.h>
#include <strings.h>

static void
fletcher_4_avx2_init(fletcher_4_ctx_t *ctx)
{
	bzero(ctx->avx, 4 * sizeof (zfs_fletcher_avx_t));
}

static void
fletcher_4_avx2_fini(fletcher_4_ctx_t *ctx, zio_cksum_t *zcp)
{
	uint64_t A, B, C, D;

	A = ctx->avx[0].v[0] + ctx->avx[0].v[1] +
	    ctx->avx[0].v[2] + ctx->avx[0].v[3];
	B = 0 - ctx->avx[0].v[1] - 2 * ctx->avx[0].v[2] - 3 * ctx->avx[0].v[3] +
	    4 * ctx->avx[1].v[0] + 4 * ctx->avx[1].v[1] + 4 * ctx->avx[1].v[2] +
	    4 * ctx->avx[1].v[3];

	C = ctx->avx[0].v[2] + 3 * ctx->avx[0].v[3] - 6 * ctx->avx[1].v[0] -
	    10 * ctx->avx[1].v[1] - 14 * ctx->avx[1].v[2] -
	    18 * ctx->avx[1].v[3] + 16 * ctx->avx[2].v[0] +
	    16 * ctx->avx[2].v[1] + 16 * ctx->avx[2].v[2] +
	    16 * ctx->avx[2].v[3];

	D = 0 - ctx->avx[0].v[3] + 4 * ctx->avx[1].v[0] +
	    10 * ctx->avx[1].v[1] + 20 * ctx->avx[1].v[2] +
	    34 * ctx->avx[1].v[3] - 48 * ctx->avx[2].v[0] -
	    64 * ctx->avx[2].v[1] - 80 * ctx->avx[2].v[2] -
	    96 * ctx->avx[2].v[3] + 64 * ctx->avx[3].v[0] +
	    64 * ctx->avx[3].v[1] + 64 * ctx->avx[3].v[2] +
	    64 * ctx->avx[3].v[3];

	ZIO_SET_CHECKSUM(zcp, A, B, C, D);
}

#define	FLETCHER_4_AVX2_RESTORE_CTX(ctx)				\
{									\
	asm volatile("vmovdqu %0, %%ymm0" :: "m" ((ctx)->avx[0]));	\
	asm volatile("vmovdqu %0, %%ymm1" :: "m" ((ctx)->avx[1]));	\
	asm volatile("vmovdqu %0, %%ymm2" :: "m" ((ctx)->avx[2]));	\
	asm volatile("vmovdqu %0, %%ymm3" :: "m" ((ctx)->avx[3]));	\
}

#define	FLETCHER_4_AVX2_SAVE_CTX(ctx)					\
{									\
	asm volatile("vmovdqu %%ymm0, %0" : "=m" ((ctx)->avx[0]));	\
	asm volatile("vmovdqu %%ymm1, %0" : "=m" ((ctx)->avx[1]));	\
	asm volatile("vmovdqu %%ymm2, %0" : "=m" ((ctx)->avx[2]));	\
	asm volatile("vmovdqu %%ymm3, %0" : "=m" ((ctx)->avx[3]));	\
}


static void
fletcher_4_avx2_native(fletcher_4_ctx_t *ctx, const void *buf, uint64_t size)
{
	const uint64_t *ip = buf;
	const uint64_t *ipend = (uint64_t *)((uint8_t *)ip + size);

	kfpu_begin();

	FLETCHER_4_AVX2_RESTORE_CTX(ctx);

	for (; ip < ipend; ip += 2) {
		asm volatile("vpmovzxdq %0, %%ymm4"::"m" (*ip));
		asm volatile("vpaddq %ymm4, %ymm0, %ymm0");
		asm volatile("vpaddq %ymm0, %ymm1, %ymm1");
		asm volatile("vpaddq %ymm1, %ymm2, %ymm2");
		asm volatile("vpaddq %ymm2, %ymm3, %ymm3");
	}

	FLETCHER_4_AVX2_SAVE_CTX(ctx);
	asm volatile("vzeroupper");

	kfpu_end();
}

static void
fletcher_4_avx2_byteswap(fletcher_4_ctx_t *ctx, const void *buf, uint64_t size)
{
	static const zfs_fletcher_avx_t mask = {
		.v = { 0xFFFFFFFF00010203, 0xFFFFFFFF08090A0B,
		    0xFFFFFFFF00010203, 0xFFFFFFFF08090A0B }
	};
	const uint64_t *ip = buf;
	const uint64_t *ipend = (uint64_t *)((uint8_t *)ip + size);

	kfpu_begin();

	FLETCHER_4_AVX2_RESTORE_CTX(ctx);

	asm volatile("vmovdqu %0, %%ymm5" :: "m" (mask));

	for (; ip < ipend; ip += 2) {
		asm volatile("vpmovzxdq %0, %%ymm4"::"m" (*ip));
		asm volatile("vpshufb %ymm5, %ymm4, %ymm4");

		asm volatile("vpaddq %ymm4, %ymm0, %ymm0");
		asm volatile("vpaddq %ymm0, %ymm1, %ymm1");
		asm volatile("vpaddq %ymm1, %ymm2, %ymm2");
		asm volatile("vpaddq %ymm2, %ymm3, %ymm3");
	}

	FLETCHER_4_AVX2_SAVE_CTX(ctx);
	asm volatile("vzeroupper");

	kfpu_end();
}

static boolean_t fletcher_4_avx2_valid(void)
{
	return (zfs_avx_available() && zfs_avx2_available());
}

const fletcher_4_ops_t fletcher_4_avx2_ops = {
	.init_native = fletcher_4_avx2_init,
	.fini_native = fletcher_4_avx2_fini,
	.compute_native = fletcher_4_avx2_native,
	.init_byteswap = fletcher_4_avx2_init,
	.fini_byteswap = fletcher_4_avx2_fini,
	.compute_byteswap = fletcher_4_avx2_byteswap,
	.valid = fletcher_4_avx2_valid,
	.name = "avx2"
};

#endif /* defined(HAVE_AVX) && defined(HAVE_AVX2) */
