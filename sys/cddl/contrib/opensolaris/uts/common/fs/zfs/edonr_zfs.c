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
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */
#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/edonr.h>
#include <sys/abd.h>

#define	EDONR_MODE		512
#define	EDONR_BLOCK_SIZE	EdonR512_BLOCK_SIZE

static int
edonr_incremental(void *buf, size_t size, void *arg)
{
	EdonRState *ctx = arg;
	EdonRUpdate(ctx, buf, size * 8);
	return (0);
}

/*
 * Native zio_checksum interface for the Edon-R hash function.
 */
/*ARGSUSED*/
void
abd_checksum_edonr_native(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	uint8_t		digest[EDONR_MODE / 8];
	EdonRState	ctx;

	ASSERT(ctx_template != NULL);
	bcopy(ctx_template, &ctx, sizeof (ctx));
	(void) abd_iterate_func(abd, 0, size, edonr_incremental, &ctx);
	EdonRFinal(&ctx, digest);
	bcopy(digest, zcp->zc_word, sizeof (zcp->zc_word));
}

/*
 * Byteswapped zio_checksum interface for the Edon-R hash function.
 */
void
abd_checksum_edonr_byteswap(abd_t *abd, uint64_t size,
    const void *ctx_template, zio_cksum_t *zcp)
{
	zio_cksum_t	tmp;

	abd_checksum_edonr_native(abd, size, ctx_template, &tmp);
	zcp->zc_word[0] = BSWAP_64(zcp->zc_word[0]);
	zcp->zc_word[1] = BSWAP_64(zcp->zc_word[1]);
	zcp->zc_word[2] = BSWAP_64(zcp->zc_word[2]);
	zcp->zc_word[3] = BSWAP_64(zcp->zc_word[3]);
}

void *
abd_checksum_edonr_tmpl_init(const zio_cksum_salt_t *salt)
{
	EdonRState	*ctx;
	uint8_t		salt_block[EDONR_BLOCK_SIZE];

	/*
	 * Edon-R needs all but the last hash invocation to be on full-size
	 * blocks, but the salt is too small. Rather than simply padding it
	 * with zeros, we expand the salt into a new salt block of proper
	 * size by double-hashing it (the new salt block will be composed of
	 * H(salt) || H(H(salt))).
	 */
	CTASSERT(EDONR_BLOCK_SIZE == 2 * (EDONR_MODE / 8));
	EdonRHash(EDONR_MODE, salt->zcs_bytes, sizeof (salt->zcs_bytes) * 8,
	    salt_block);
	EdonRHash(EDONR_MODE, salt_block, EDONR_MODE, salt_block +
	    EDONR_MODE / 8);

	/*
	 * Feed the new salt block into the hash function - this will serve
	 * as our MAC key.
	 */
	ctx = kmem_zalloc(sizeof (*ctx), KM_SLEEP);
	EdonRInit(ctx, EDONR_MODE);
	EdonRUpdate(ctx, salt_block, sizeof (salt_block) * 8);
	return (ctx);
}

void
abd_checksum_edonr_tmpl_free(void *ctx_template)
{
	EdonRState	*ctx = ctx_template;

	bzero(ctx, sizeof (*ctx));
	kmem_free(ctx, sizeof (*ctx));
}
