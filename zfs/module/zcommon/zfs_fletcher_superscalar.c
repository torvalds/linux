/*
 * Implement fast Fletcher4 using superscalar pipelines.
 *
 * Use regular C code to compute
 * Fletcher4 in two incremental 64-bit parallel accumulator streams,
 * and then combine the streams to form the final four checksum words.
 * This implementation is a derivative of the AVX SIMD implementation by
 * James Guilford and Jinshan Xiong from Intel (see zfs_fletcher_intel.c).
 *
 * Copyright (C) 2016 Romain Dolbeau.
 *
 * Authors:
 *	Romain Dolbeau <romain.dolbeau@atos.net>
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#include <sys/byteorder.h>
#include <sys/spa_checksum.h>
#include <zfs_fletcher.h>
#include <strings.h>

static void
fletcher_4_superscalar_init(fletcher_4_ctx_t *ctx)
{
	bzero(ctx->superscalar, 4 * sizeof (zfs_fletcher_superscalar_t));
}

static void
fletcher_4_superscalar_fini(fletcher_4_ctx_t *ctx, zio_cksum_t *zcp)
{
	uint64_t A, B, C, D;
	A = ctx->superscalar[0].v[0] + ctx->superscalar[0].v[1];
	B = 2 * ctx->superscalar[1].v[0] + 2 * ctx->superscalar[1].v[1] -
	    ctx->superscalar[0].v[1];
	C = 4 * ctx->superscalar[2].v[0] - ctx->superscalar[1].v[0] +
	    4 * ctx->superscalar[2].v[1] - 3 * ctx->superscalar[1].v[1];
	D = 8 * ctx->superscalar[3].v[0] - 4 * ctx->superscalar[2].v[0] +
	    8 * ctx->superscalar[3].v[1] - 8 * ctx->superscalar[2].v[1] +
	    ctx->superscalar[1].v[1];
	ZIO_SET_CHECKSUM(zcp, A, B, C, D);
}

static void
fletcher_4_superscalar_native(fletcher_4_ctx_t *ctx,
    const void *buf, uint64_t size)
{
	const uint32_t *ip = buf;
	const uint32_t *ipend = ip + (size / sizeof (uint32_t));
	uint64_t a, b, c, d;
	uint64_t a2, b2, c2, d2;

	a = ctx->superscalar[0].v[0];
	b = ctx->superscalar[1].v[0];
	c = ctx->superscalar[2].v[0];
	d = ctx->superscalar[3].v[0];
	a2 = ctx->superscalar[0].v[1];
	b2 = ctx->superscalar[1].v[1];
	c2 = ctx->superscalar[2].v[1];
	d2 = ctx->superscalar[3].v[1];

	for (; ip < ipend; ip += 2) {
		a += ip[0];
		a2 += ip[1];
		b += a;
		b2 += a2;
		c += b;
		c2 += b2;
		d += c;
		d2 += c2;
	}

	ctx->superscalar[0].v[0] = a;
	ctx->superscalar[1].v[0] = b;
	ctx->superscalar[2].v[0] = c;
	ctx->superscalar[3].v[0] = d;
	ctx->superscalar[0].v[1] = a2;
	ctx->superscalar[1].v[1] = b2;
	ctx->superscalar[2].v[1] = c2;
	ctx->superscalar[3].v[1] = d2;
}

static void
fletcher_4_superscalar_byteswap(fletcher_4_ctx_t *ctx,
    const void *buf, uint64_t size)
{
	const uint32_t *ip = buf;
	const uint32_t *ipend = ip + (size / sizeof (uint32_t));
	uint64_t a, b, c, d;
	uint64_t a2, b2, c2, d2;

	a = ctx->superscalar[0].v[0];
	b = ctx->superscalar[1].v[0];
	c = ctx->superscalar[2].v[0];
	d = ctx->superscalar[3].v[0];
	a2 = ctx->superscalar[0].v[1];
	b2 = ctx->superscalar[1].v[1];
	c2 = ctx->superscalar[2].v[1];
	d2 = ctx->superscalar[3].v[1];

	for (; ip < ipend; ip += 2) {
		a += BSWAP_32(ip[0]);
		a2 += BSWAP_32(ip[1]);
		b += a;
		b2 += a2;
		c += b;
		c2 += b2;
		d += c;
		d2 += c2;
	}

	ctx->superscalar[0].v[0] = a;
	ctx->superscalar[1].v[0] = b;
	ctx->superscalar[2].v[0] = c;
	ctx->superscalar[3].v[0] = d;
	ctx->superscalar[0].v[1] = a2;
	ctx->superscalar[1].v[1] = b2;
	ctx->superscalar[2].v[1] = c2;
	ctx->superscalar[3].v[1] = d2;
}

static boolean_t fletcher_4_superscalar_valid(void)
{
	return (B_TRUE);
}

const fletcher_4_ops_t fletcher_4_superscalar_ops = {
	.init_native = fletcher_4_superscalar_init,
	.compute_native = fletcher_4_superscalar_native,
	.fini_native = fletcher_4_superscalar_fini,
	.init_byteswap = fletcher_4_superscalar_init,
	.compute_byteswap = fletcher_4_superscalar_byteswap,
	.fini_byteswap = fletcher_4_superscalar_fini,
	.valid = fletcher_4_superscalar_valid,
	.name = "superscalar"
};
