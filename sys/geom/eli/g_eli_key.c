/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2011 Pawel Jakub Dawidek <pawel@dawidek.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#ifdef _KERNEL
#include <sys/malloc.h>
#include <sys/systm.h>
#include <geom/geom.h>
#else
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#endif

#include <geom/eli/g_eli.h>

#ifdef _KERNEL
MALLOC_DECLARE(M_ELI);
#endif

/*
 * Verify if the given 'key' is correct.
 * Return 1 if it is correct and 0 otherwise.
 */
static int
g_eli_mkey_verify(const unsigned char *mkey, const unsigned char *key)
{
	const unsigned char *odhmac;	/* On-disk HMAC. */
	unsigned char chmac[SHA512_MDLEN];	/* Calculated HMAC. */
	unsigned char hmkey[SHA512_MDLEN];	/* Key for HMAC. */

	/*
	 * The key for HMAC calculations is: hmkey = HMAC_SHA512(Derived-Key, 0)
	 */
	g_eli_crypto_hmac(key, G_ELI_USERKEYLEN, "\x00", 1, hmkey, 0);

	odhmac = mkey + G_ELI_DATAIVKEYLEN;

	/* Calculate HMAC from Data-Key and IV-Key. */
	g_eli_crypto_hmac(hmkey, sizeof(hmkey), mkey, G_ELI_DATAIVKEYLEN,
	    chmac, 0);

	explicit_bzero(hmkey, sizeof(hmkey));

	/*
	 * Compare calculated HMAC with HMAC from metadata.
	 * If two HMACs are equal, 'key' is correct.
	 */
	return (!bcmp(odhmac, chmac, SHA512_MDLEN));
}

/*
 * Calculate HMAC from Data-Key and IV-Key.
 */
void
g_eli_mkey_hmac(unsigned char *mkey, const unsigned char *key)
{
	unsigned char hmkey[SHA512_MDLEN];	/* Key for HMAC. */
	unsigned char *odhmac;	/* On-disk HMAC. */

	/*
	 * The key for HMAC calculations is: hmkey = HMAC_SHA512(Derived-Key, 0)
	 */
	g_eli_crypto_hmac(key, G_ELI_USERKEYLEN, "\x00", 1, hmkey, 0);

	odhmac = mkey + G_ELI_DATAIVKEYLEN;
	/* Calculate HMAC from Data-Key and IV-Key. */
	g_eli_crypto_hmac(hmkey, sizeof(hmkey), mkey, G_ELI_DATAIVKEYLEN,
	    odhmac, 0);

	explicit_bzero(hmkey, sizeof(hmkey));
}

/*
 * Find and decrypt Master Key encrypted with 'key' at slot 'nkey'.
 * Return 0 on success, > 0 on failure, -1 on bad key.
 */
int
g_eli_mkey_decrypt(const struct g_eli_metadata *md, const unsigned char *key,
    unsigned char *mkey, unsigned nkey)
{
	unsigned char tmpmkey[G_ELI_MKEYLEN];
	unsigned char enckey[SHA512_MDLEN];	/* Key for encryption. */
	const unsigned char *mmkey;
	int bit, error;

	if (nkey > G_ELI_MKEYLEN)
		return (-1);

	/*
	 * The key for encryption is: enckey = HMAC_SHA512(Derived-Key, 1)
	 */
	g_eli_crypto_hmac(key, G_ELI_USERKEYLEN, "\x01", 1, enckey, 0);

	mmkey = md->md_mkeys + G_ELI_MKEYLEN * nkey;
	bit = (1 << nkey);
	if (!(md->md_keys & bit))
		return (-1);
	bcopy(mmkey, tmpmkey, G_ELI_MKEYLEN);
	error = g_eli_crypto_decrypt(md->md_ealgo, tmpmkey,
	    G_ELI_MKEYLEN, enckey, md->md_keylen);
	if (error != 0) {
		explicit_bzero(tmpmkey, sizeof(tmpmkey));
		explicit_bzero(enckey, sizeof(enckey));
		return (error);
	}
	if (g_eli_mkey_verify(tmpmkey, key)) {
		bcopy(tmpmkey, mkey, G_ELI_DATAIVKEYLEN);
		explicit_bzero(tmpmkey, sizeof(tmpmkey));
		explicit_bzero(enckey, sizeof(enckey));
		return (0);
	}
	explicit_bzero(enckey, sizeof(enckey));
	explicit_bzero(tmpmkey, sizeof(tmpmkey));

	return (-1);
}

/*
 * Find and decrypt Master Key encrypted with 'key'.
 * Return decrypted Master Key number in 'nkeyp' if not NULL.
 * Return 0 on success, > 0 on failure, -1 on bad key.
 */
int
g_eli_mkey_decrypt_any(const struct g_eli_metadata *md,
    const unsigned char *key, unsigned char *mkey, unsigned *nkeyp)
{
	int error, nkey;

	if (nkeyp != NULL)
		*nkeyp = -1;

	error = -1;
	for (nkey = 0; nkey < G_ELI_MAXMKEYS; nkey++) {
		error = g_eli_mkey_decrypt(md, key, mkey, nkey);
		if (error == 0) {
			if (nkeyp != NULL)
				*nkeyp = nkey;
			break;
		} else if (error > 0) {
			break;
		}
	}

	return (error);
}

/*
 * Encrypt the Master-Key and calculate HMAC to be able to verify it in the
 * future.
 */
int
g_eli_mkey_encrypt(unsigned algo, const unsigned char *key, unsigned keylen,
    unsigned char *mkey)
{
	unsigned char enckey[SHA512_MDLEN];	/* Key for encryption. */
	int error;

	/*
	 * To calculate HMAC, the whole key (G_ELI_USERKEYLEN bytes long) will
	 * be used.
	 */
	g_eli_mkey_hmac(mkey, key);
	/*
	 * The key for encryption is: enckey = HMAC_SHA512(Derived-Key, 1)
	 */
	g_eli_crypto_hmac(key, G_ELI_USERKEYLEN, "\x01", 1, enckey, 0);
	/*
	 * Encrypt the Master-Key and HMAC() result with the given key (this
	 * time only 'keylen' bits from the key are used).
	 */
	error = g_eli_crypto_encrypt(algo, mkey, G_ELI_MKEYLEN, enckey, keylen);

	explicit_bzero(enckey, sizeof(enckey));

	return (error);
}

#ifdef _KERNEL
/*
 * When doing encryption only, copy IV key and encryption key.
 * When doing encryption and authentication, copy IV key, generate encryption
 * key and generate authentication key.
 */
void
g_eli_mkey_propagate(struct g_eli_softc *sc, const unsigned char *mkey)
{

	/* Remember the Master Key. */
	bcopy(mkey, sc->sc_mkey, sizeof(sc->sc_mkey));

	bcopy(mkey, sc->sc_ivkey, sizeof(sc->sc_ivkey));
	mkey += sizeof(sc->sc_ivkey);

	/*
	 * The authentication key is: akey = HMAC_SHA512(Data-Key, 0x11)
	 */
	if ((sc->sc_flags & G_ELI_FLAG_AUTH) != 0) {
		g_eli_crypto_hmac(mkey, G_ELI_MAXKEYLEN, "\x11", 1,
		    sc->sc_akey, 0);
	} else {
		arc4rand(sc->sc_akey, sizeof(sc->sc_akey), 0);
	}

	/* Initialize encryption keys. */
	g_eli_key_init(sc);

	if ((sc->sc_flags & G_ELI_FLAG_AUTH) != 0) {
		/*
		 * Precalculate SHA256 for HMAC key generation.
		 * This is expensive operation and we can do it only once now or
		 * for every access to sector, so now will be much better.
		 */
		SHA256_Init(&sc->sc_akeyctx);
		SHA256_Update(&sc->sc_akeyctx, sc->sc_akey,
		    sizeof(sc->sc_akey));
	}
	/*
	 * Precalculate SHA256 for IV generation.
	 * This is expensive operation and we can do it only once now or for
	 * every access to sector, so now will be much better.
	 */
	switch (sc->sc_ealgo) {
	case CRYPTO_AES_XTS:
		break;
	default:
		SHA256_Init(&sc->sc_ivctx);
		SHA256_Update(&sc->sc_ivctx, sc->sc_ivkey,
		    sizeof(sc->sc_ivkey));
		break;
	}
}
#endif
