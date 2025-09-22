/*	$OpenBSD: aes_xts.c,v 1.2 2017/05/31 00:34:33 djm Exp $	*/
/*
 * Copyright (C) 2008, Damien Miller
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#include <lib/libsa/stand.h>

#include "aes_xts.h"

void
aes_xts_reinit(struct aes_xts_ctx *ctx, u_int8_t *iv)
{
	u_int64_t blocknum;
	u_int i;

	/*
	 * Prepare tweak as E_k2(IV). IV is specified as LE representation
	 * of a 64-bit block number which we allow to be passed in directly.
	 */
	bcopy(iv, &blocknum, AES_XTS_IVSIZE);
	for (i = 0; i < AES_XTS_IVSIZE; i++) {
		ctx->tweak[i] = blocknum & 0xff;
		blocknum >>= 8;
	}
	/* Last 64 bits of IV are always zero */
	bzero(ctx->tweak + AES_XTS_IVSIZE, AES_XTS_IVSIZE);

	rijndael_encrypt(&ctx->key2, ctx->tweak, ctx->tweak);
}

void
aes_xts_crypt(struct aes_xts_ctx *ctx, u_int8_t *data, u_int do_encrypt)
{
	u_int8_t block[AES_XTS_BLOCKSIZE];
	u_int i, carry_in, carry_out;

	for (i = 0; i < AES_XTS_BLOCKSIZE; i++)
		block[i] = data[i] ^ ctx->tweak[i];

	if (do_encrypt)
		rijndael_encrypt(&ctx->key1, block, data);
	else
		rijndael_decrypt(&ctx->key1, block, data);

	for (i = 0; i < AES_XTS_BLOCKSIZE; i++)
		data[i] ^= ctx->tweak[i];

	/* Exponentiate tweak */
	carry_in = 0;
	for (i = 0; i < AES_XTS_BLOCKSIZE; i++) {
		carry_out = ctx->tweak[i] & 0x80;
		ctx->tweak[i] = (ctx->tweak[i] << 1) | carry_in;
		carry_in = carry_out >> 7;
	}
	ctx->tweak[0] ^= (AES_XTS_ALPHA & -carry_in);
	explicit_bzero(block, sizeof(block));
}

void
aes_xts_encrypt(struct aes_xts_ctx *ctx, u_int8_t *data)
{
	aes_xts_crypt(ctx, data, 1);
}

void
aes_xts_decrypt(struct aes_xts_ctx *ctx, u_int8_t *data)
{
	aes_xts_crypt(ctx, data, 0);
}

int
aes_xts_setkey(struct aes_xts_ctx *ctx, u_int8_t *key, int len)
{
	if (len != 32 && len != 64)
		return -1;

	rijndael_set_key(&ctx->key1, key, len * 4);
	rijndael_set_key(&ctx->key2, key + (len / 2), len * 4);

	return 0;
}

void
aes_xts_zerokey(struct aes_xts_ctx *ctx)
{
	explicit_bzero(ctx, sizeof(struct aes_xts_ctx));
}
