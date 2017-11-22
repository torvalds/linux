/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2013 Saso Kiselkov.  All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */
#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/skein.h>

#include <sys/abd.h>

static int
skein_incremental(void *buf, size_t size, void *arg)
{
	Skein_512_Ctxt_t *ctx = arg;
	(void) Skein_512_Update(ctx, buf, size);
	return (0);
}
/*
 * Computes a native 256-bit skein MAC checksum. Please note that this
 * function requires the presence of a ctx_template that should be allocated
 * using abd_checksum_skein_tmpl_init.
 */
/*ARGSUSED*/
void
abd_checksum_skein_native(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	Skein_512_Ctxt_t	ctx;

	ASSERT(ctx_template != NULL);
	bcopy(ctx_template, &ctx, sizeof (ctx));
	(void) abd_iterate_func(abd, 0, size, skein_incremental, &ctx);
	(void) Skein_512_Final(&ctx, (uint8_t *)zcp);
	bzero(&ctx, sizeof (ctx));
}

/*
 * Byteswapped version of abd_checksum_skein_native. This just invokes
 * the native checksum function and byteswaps the resulting checksum (since
 * skein is internally endian-insensitive).
 */
void
abd_checksum_skein_byteswap(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	zio_cksum_t	tmp;

	abd_checksum_skein_native(abd, size, ctx_template, &tmp);
	zcp->zc_word[0] = BSWAP_64(tmp.zc_word[0]);
	zcp->zc_word[1] = BSWAP_64(tmp.zc_word[1]);
	zcp->zc_word[2] = BSWAP_64(tmp.zc_word[2]);
	zcp->zc_word[3] = BSWAP_64(tmp.zc_word[3]);
}

/*
 * Allocates a skein MAC template suitable for using in skein MAC checksum
 * computations and returns a pointer to it.
 */
void *
abd_checksum_skein_tmpl_init(const zio_cksum_salt_t *salt)
{
	Skein_512_Ctxt_t	*ctx;

	ctx = kmem_zalloc(sizeof (*ctx), KM_SLEEP);
	(void) Skein_512_InitExt(ctx, sizeof (zio_cksum_t) * 8, 0,
	    salt->zcs_bytes, sizeof (salt->zcs_bytes));
	return (ctx);
}

/*
 * Frees a skein context template previously allocated using
 * zio_checksum_skein_tmpl_init.
 */
void
abd_checksum_skein_tmpl_free(void *ctx_template)
{
	Skein_512_Ctxt_t	*ctx = ctx_template;

	bzero(ctx, sizeof (*ctx));
	kmem_free(ctx, sizeof (*ctx));
}
