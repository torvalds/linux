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

#include <crypto/sha2/sha224.h>
#include <crypto/sha2/sha256.h>
#include <crypto/sha2/sha384.h>
#include <crypto/sha2/sha512.h>
#include <opencrypto/xform_auth.h>

static	int SHA224Update_int(void *, const u_int8_t *, u_int16_t);
static	int SHA256Update_int(void *, const u_int8_t *, u_int16_t);
static	int SHA384Update_int(void *, const u_int8_t *, u_int16_t);
static	int SHA512Update_int(void *, const u_int8_t *, u_int16_t);

/* Plain hashes */
struct auth_hash auth_hash_sha2_224 = {
	.type = CRYPTO_SHA2_224,
	.name = "SHA2-224",
	.hashsize = SHA2_224_HASH_LEN,
	.ctxsize = sizeof(SHA224_CTX),
	.blocksize = SHA2_224_BLOCK_LEN,
	.Init = (void (*)(void *)) SHA224_Init,
	.Update = SHA224Update_int,
	.Final = (void (*)(u_int8_t *, void *)) SHA224_Final,
};

struct auth_hash auth_hash_sha2_256 = {
	.type = CRYPTO_SHA2_256,
	.name = "SHA2-256",
	.keysize = SHA2_256_BLOCK_LEN,
	.hashsize = SHA2_256_HASH_LEN,
	.ctxsize = sizeof(SHA256_CTX),
	.blocksize = SHA2_256_BLOCK_LEN,
	.Init = (void (*)(void *)) SHA256_Init,
	.Update = SHA256Update_int,
	.Final = (void (*)(u_int8_t *, void *)) SHA256_Final,
};

struct auth_hash auth_hash_sha2_384 = {
	.type = CRYPTO_SHA2_384,
	.name = "SHA2-384",
	.keysize = SHA2_384_BLOCK_LEN,
	.hashsize = SHA2_384_HASH_LEN,
	.ctxsize = sizeof(SHA384_CTX),
	.blocksize = SHA2_384_BLOCK_LEN,
	.Init = (void (*)(void *)) SHA384_Init,
	.Update = SHA384Update_int,
	.Final = (void (*)(u_int8_t *, void *)) SHA384_Final,
};

struct auth_hash auth_hash_sha2_512 = {
	.type = CRYPTO_SHA2_512,
	.name = "SHA2-512",
	.keysize = SHA2_512_BLOCK_LEN,
	.hashsize = SHA2_512_HASH_LEN,
	.ctxsize = sizeof(SHA512_CTX),
	.blocksize = SHA2_512_BLOCK_LEN,
	.Init = (void (*)(void *)) SHA512_Init,
	.Update = SHA512Update_int,
	.Final = (void (*)(u_int8_t *, void *)) SHA512_Final,
};

/* Authentication instances */
struct auth_hash auth_hash_hmac_sha2_224 = {
	.type = CRYPTO_SHA2_224_HMAC,
	.name = "HMAC-SHA2-224",
	.keysize = SHA2_224_BLOCK_LEN,
	.hashsize = SHA2_224_HASH_LEN,
	.ctxsize = sizeof(SHA224_CTX),
	.blocksize = SHA2_224_BLOCK_LEN,
	.Init = (void (*)(void *)) SHA224_Init,
	.Update = SHA224Update_int,
	.Final = (void (*)(u_int8_t *, void *)) SHA224_Final,
};

struct auth_hash auth_hash_hmac_sha2_256 = {
	.type = CRYPTO_SHA2_256_HMAC,
	.name = "HMAC-SHA2-256",
	.keysize = SHA2_256_BLOCK_LEN,
	.hashsize = SHA2_256_HASH_LEN,
	.ctxsize = sizeof(SHA256_CTX),
	.blocksize = SHA2_256_BLOCK_LEN,
	.Init = (void (*)(void *)) SHA256_Init,
	.Update = SHA256Update_int,
	.Final = (void (*)(u_int8_t *, void *)) SHA256_Final,
};

struct auth_hash auth_hash_hmac_sha2_384 = {
	.type = CRYPTO_SHA2_384_HMAC,
	.name = "HMAC-SHA2-384",
	.keysize = SHA2_384_BLOCK_LEN,
	.hashsize = SHA2_384_HASH_LEN,
	.ctxsize = sizeof(SHA384_CTX),
	.blocksize = SHA2_384_BLOCK_LEN,
	.Init = (void (*)(void *)) SHA384_Init,
	.Update = SHA384Update_int,
	.Final = (void (*)(u_int8_t *, void *)) SHA384_Final,
};

struct auth_hash auth_hash_hmac_sha2_512 = {
	.type = CRYPTO_SHA2_512_HMAC,
	.name = "HMAC-SHA2-512",
	.keysize = SHA2_512_BLOCK_LEN,
	.hashsize = SHA2_512_HASH_LEN,
	.ctxsize = sizeof(SHA512_CTX),
	.blocksize = SHA2_512_BLOCK_LEN,
	.Init = (void (*)(void *)) SHA512_Init,
	.Update = SHA512Update_int,
	.Final = (void (*)(u_int8_t *, void *)) SHA512_Final,
};

/*
 * And now for auth.
 */
static int
SHA224Update_int(void *ctx, const u_int8_t *buf, u_int16_t len)
{
	SHA224_Update(ctx, buf, len);
	return 0;
}

static int
SHA256Update_int(void *ctx, const u_int8_t *buf, u_int16_t len)
{
	SHA256_Update(ctx, buf, len);
	return 0;
}

static int
SHA384Update_int(void *ctx, const u_int8_t *buf, u_int16_t len)
{
	SHA384_Update(ctx, buf, len);
	return 0;
}

static int
SHA512Update_int(void *ctx, const u_int8_t *buf, u_int16_t len)
{
	SHA512_Update(ctx, buf, len);
	return 0;
}
