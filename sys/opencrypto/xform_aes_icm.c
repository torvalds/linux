/*	$OpenBSD: xform.c,v 1.16 2001/08/28 12:20:43 ben Exp $	*/
/*-
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr),
 * Niels Provos (provos@physnet.uni-hamburg.de) and
 * Damien Miller (djm@mindrot.org).
 *
 * This code was written by John Ioannidis for BSD/OS in Athens, Greece,
 * in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Additional features in 1999 by Angelos D. Keromytis.
 *
 * AES XTS implementation in 2008 by Damien Miller
 *
 * Copyright (C) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
 *
 * Copyright (C) 2001, Angelos D. Keromytis.
 *
 * Copyright (C) 2008, Damien Miller
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by John-Mark Gurney
 * under sponsorship of the FreeBSD Foundation and
 * Rubicon Communications, LLC (Netgate).
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <opencrypto/xform_enc.h>

static	int aes_icm_setkey(u_int8_t **, u_int8_t *, int);
static	void aes_icm_crypt(caddr_t, u_int8_t *);
static	void aes_icm_zerokey(u_int8_t **);
static	void aes_icm_reinit(caddr_t, u_int8_t *);
static	void aes_gcm_reinit(caddr_t, u_int8_t *);
static	void aes_ccm_reinit(caddr_t, u_int8_t *);

/* Encryption instances */
struct enc_xform enc_xform_aes_icm = {
	CRYPTO_AES_ICM, "AES-ICM",
	AES_BLOCK_LEN, AES_BLOCK_LEN, AES_MIN_KEY, AES_MAX_KEY,
	aes_icm_crypt,
	aes_icm_crypt,
	aes_icm_setkey,
	aes_icm_zerokey,
	aes_icm_reinit,
};

struct enc_xform enc_xform_aes_nist_gcm = {
	CRYPTO_AES_NIST_GCM_16, "AES-GCM",
	AES_ICM_BLOCK_LEN, AES_GCM_IV_LEN, AES_MIN_KEY, AES_MAX_KEY,
	aes_icm_crypt,
	aes_icm_crypt,
	aes_icm_setkey,
	aes_icm_zerokey,
	aes_gcm_reinit,
};

struct enc_xform enc_xform_ccm = {
	.type = CRYPTO_AES_CCM_16,
	.name = "AES-CCM",
	.blocksize = AES_ICM_BLOCK_LEN, .ivsize = AES_CCM_IV_LEN,
	.minkey = AES_MIN_KEY, .maxkey = AES_MAX_KEY,
	.encrypt = aes_icm_crypt,
	.decrypt = aes_icm_crypt,
	.setkey = aes_icm_setkey,
	.zerokey = aes_icm_zerokey,
	.reinit = aes_ccm_reinit,
};

/*
 * Encryption wrapper routines.
 */
static void
aes_icm_reinit(caddr_t key, u_int8_t *iv)
{
	struct aes_icm_ctx *ctx;

	ctx = (struct aes_icm_ctx *)key;
	bcopy(iv, ctx->ac_block, AESICM_BLOCKSIZE);
}

static void
aes_gcm_reinit(caddr_t key, u_int8_t *iv)
{
	struct aes_icm_ctx *ctx;

	aes_icm_reinit(key, iv);

	ctx = (struct aes_icm_ctx *)key;
	/* GCM starts with 2 as counter 1 is used for final xor of tag. */
	bzero(&ctx->ac_block[AESICM_BLOCKSIZE - 4], 4);
	ctx->ac_block[AESICM_BLOCKSIZE - 1] = 2;
}

static void
aes_ccm_reinit(caddr_t key, u_int8_t *iv)
{
	struct aes_icm_ctx *ctx;

	ctx = (struct aes_icm_ctx*)key;

	/* CCM has flags, then the IV, then the counter, which starts at 1 */
	bzero(ctx->ac_block, sizeof(ctx->ac_block));
	/* 3 bytes for length field; this gives a nonce of 12 bytes */
	ctx->ac_block[0] = (15 - AES_CCM_IV_LEN) - 1;
	bcopy(iv, ctx->ac_block+1, AES_CCM_IV_LEN);
	ctx->ac_block[AESICM_BLOCKSIZE - 1] = 1;
}

static void
aes_icm_crypt(caddr_t key, u_int8_t *data)
{
	struct aes_icm_ctx *ctx;
	u_int8_t keystream[AESICM_BLOCKSIZE];
	int i;

	ctx = (struct aes_icm_ctx *)key;
	rijndaelEncrypt(ctx->ac_ek, ctx->ac_nr, ctx->ac_block, keystream);
	for (i = 0; i < AESICM_BLOCKSIZE; i++)
		data[i] ^= keystream[i];
	explicit_bzero(keystream, sizeof(keystream));

	/* increment counter */
	for (i = AESICM_BLOCKSIZE - 1;
	     i >= 0; i--)
		if (++ctx->ac_block[i])   /* continue on overflow */
			break;
}

static int
aes_icm_setkey(u_int8_t **sched, u_int8_t *key, int len)
{
	struct aes_icm_ctx *ctx;

	if (len != 16 && len != 24 && len != 32)
		return EINVAL;

	*sched = KMALLOC(sizeof(struct aes_icm_ctx), M_CRYPTO_DATA,
	    M_NOWAIT | M_ZERO);
	if (*sched == NULL)
		return ENOMEM;

	ctx = (struct aes_icm_ctx *)*sched;
	ctx->ac_nr = rijndaelKeySetupEnc(ctx->ac_ek, (u_char *)key, len * 8);
	return 0;
}

static void
aes_icm_zerokey(u_int8_t **sched)
{

	bzero(*sched, sizeof(struct aes_icm_ctx));
	KFREE(*sched, M_CRYPTO_DATA);
	*sched = NULL;
}
