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

#include <opencrypto/gmac.h>
#include <opencrypto/xform_auth.h>

/* Encryption instances */
struct enc_xform enc_xform_aes_nist_gmac = {
	CRYPTO_AES_NIST_GMAC, "AES-GMAC",
	AES_ICM_BLOCK_LEN, AES_GCM_IV_LEN, AES_MIN_KEY, AES_MAX_KEY,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

/* Authentication instances */
struct auth_hash auth_hash_nist_gmac_aes_128 = {
	CRYPTO_AES_128_NIST_GMAC, "GMAC-AES-128",
	AES_128_GMAC_KEY_LEN, AES_GMAC_HASH_LEN, sizeof(struct aes_gmac_ctx),
	GMAC_BLOCK_LEN,
	(void (*)(void *)) AES_GMAC_Init,
	(void (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Setkey,
	(void (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Reinit,
	(int  (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Update,
	(void (*)(u_int8_t *, void *)) AES_GMAC_Final
};

struct auth_hash auth_hash_nist_gmac_aes_192 = {
	CRYPTO_AES_192_NIST_GMAC, "GMAC-AES-192",
	AES_192_GMAC_KEY_LEN, AES_GMAC_HASH_LEN, sizeof(struct aes_gmac_ctx),
	GMAC_BLOCK_LEN,
	(void (*)(void *)) AES_GMAC_Init,
	(void (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Setkey,
	(void (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Reinit,
	(int  (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Update,
	(void (*)(u_int8_t *, void *)) AES_GMAC_Final
};

struct auth_hash auth_hash_nist_gmac_aes_256 = {
	CRYPTO_AES_256_NIST_GMAC, "GMAC-AES-256",
	AES_256_GMAC_KEY_LEN, AES_GMAC_HASH_LEN, sizeof(struct aes_gmac_ctx),
	GMAC_BLOCK_LEN,
	(void (*)(void *)) AES_GMAC_Init,
	(void (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Setkey,
	(void (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Reinit,
	(int  (*)(void *, const u_int8_t *, u_int16_t)) AES_GMAC_Update,
	(void (*)(u_int8_t *, void *)) AES_GMAC_Final
};
