/*	$OpenBSD: cryptosoft.c,v 1.35 2002/04/26 08:43:50 deraadt Exp $	*/

/*-
 * The author of this code is Angelos D. Keromytis (angelos@cis.upenn.edu)
 * Copyright (c) 2002-2006 Sam Leffler, Errno Consulting
 *
 * This code was written by Angelos D. Keromytis in Athens, Greece, in
 * February 2000. Network Security Technologies Inc. (NSTI) kindly
 * supported the development of this code.
 *
 * Copyright (c) 2000, 2001 Angelos D. Keromytis
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by John-Mark Gurney
 * under sponsorship of the FreeBSD Foundation and
 * Rubicon Communications, LLC (Netgate).
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all source code copies of any software which is or includes a copy or
 * modification of this software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/random.h>
#include <sys/kernel.h>
#include <sys/uio.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/endian.h>
#include <sys/limits.h>
#include <sys/mutex.h>

#include <crypto/blowfish/blowfish.h>
#include <crypto/sha1.h>
#include <opencrypto/rmd160.h>
#include <opencrypto/cast.h>
#include <opencrypto/skipjack.h>
#include <sys/md5.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/cryptosoft.h>
#include <opencrypto/xform.h>

#include <sys/kobj.h>
#include <sys/bus.h>
#include "cryptodev_if.h"

_Static_assert(AES_CCM_IV_LEN == AES_GCM_IV_LEN,
    "AES_GCM_IV_LEN must currently be the same as AES_CCM_IV_LEN");

static	int32_t swcr_id;

u_int8_t hmac_ipad_buffer[HMAC_MAX_BLOCK_LEN];
u_int8_t hmac_opad_buffer[HMAC_MAX_BLOCK_LEN];

static	int swcr_encdec(struct cryptodesc *, struct swcr_data *, caddr_t, int);
static	int swcr_authcompute(struct cryptodesc *, struct swcr_data *, caddr_t, int);
static	int swcr_authenc(struct cryptop *crp);
static	int swcr_compdec(struct cryptodesc *, struct swcr_data *, caddr_t, int);
static	void swcr_freesession(device_t dev, crypto_session_t cses);

/*
 * Apply a symmetric encryption/decryption algorithm.
 */
static int
swcr_encdec(struct cryptodesc *crd, struct swcr_data *sw, caddr_t buf,
    int flags)
{
	unsigned char iv[EALG_MAX_BLOCK_LEN], blk[EALG_MAX_BLOCK_LEN];
	unsigned char *ivp, *nivp, iv2[EALG_MAX_BLOCK_LEN];
	struct enc_xform *exf;
	int i, j, k, blks, ind, count, ivlen;
	struct uio *uio, uiolcl;
	struct iovec iovlcl[4];
	struct iovec *iov;
	int iovcnt, iovalloc;
	int error;

	error = 0;

	exf = sw->sw_exf;
	blks = exf->blocksize;
	ivlen = exf->ivsize;

	/* Check for non-padded data */
	if (crd->crd_len % blks)
		return EINVAL;

	if (crd->crd_alg == CRYPTO_AES_ICM &&
	    (crd->crd_flags & CRD_F_IV_EXPLICIT) == 0)
		return (EINVAL);

	/* Initialize the IV */
	if (crd->crd_flags & CRD_F_ENCRYPT) {
		/* IV explicitly provided ? */
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, iv, ivlen);
		else
			arc4rand(iv, ivlen, 0);

		/* Do we need to write the IV */
		if (!(crd->crd_flags & CRD_F_IV_PRESENT))
			crypto_copyback(flags, buf, crd->crd_inject, ivlen, iv);

	} else {	/* Decryption */
		/* IV explicitly provided ? */
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, iv, ivlen);
		else {
			/* Get IV off buf */
			crypto_copydata(flags, buf, crd->crd_inject, ivlen, iv);
		}
	}

	if (crd->crd_flags & CRD_F_KEY_EXPLICIT) {
		int error; 

		if (sw->sw_kschedule)
			exf->zerokey(&(sw->sw_kschedule));

		error = exf->setkey(&sw->sw_kschedule,
				crd->crd_key, crd->crd_klen / 8);
		if (error)
			return (error);
	}

	iov = iovlcl;
	iovcnt = nitems(iovlcl);
	iovalloc = 0;
	uio = &uiolcl;
	if ((flags & CRYPTO_F_IMBUF) != 0) {
		error = crypto_mbuftoiov((struct mbuf *)buf, &iov, &iovcnt,
		    &iovalloc);
		if (error)
			return (error);
		uio->uio_iov = iov;
		uio->uio_iovcnt = iovcnt;
	} else if ((flags & CRYPTO_F_IOV) != 0)
		uio = (struct uio *)buf;
	else {
		iov[0].iov_base = buf;
		iov[0].iov_len = crd->crd_skip + crd->crd_len;
		uio->uio_iov = iov;
		uio->uio_iovcnt = 1;
	}

	ivp = iv;

	if (exf->reinit) {
		/*
		 * xforms that provide a reinit method perform all IV
		 * handling themselves.
		 */
		exf->reinit(sw->sw_kschedule, iv);
	}

	count = crd->crd_skip;
	ind = cuio_getptr(uio, count, &k);
	if (ind == -1) {
		error = EINVAL;
		goto out;
	}

	i = crd->crd_len;

	while (i > 0) {
		/*
		 * If there's insufficient data at the end of
		 * an iovec, we have to do some copying.
		 */
		if (uio->uio_iov[ind].iov_len < k + blks &&
		    uio->uio_iov[ind].iov_len != k) {
			cuio_copydata(uio, count, blks, blk);

			/* Actual encryption/decryption */
			if (exf->reinit) {
				if (crd->crd_flags & CRD_F_ENCRYPT) {
					exf->encrypt(sw->sw_kschedule,
					    blk);
				} else {
					exf->decrypt(sw->sw_kschedule,
					    blk);
				}
			} else if (crd->crd_flags & CRD_F_ENCRYPT) {
				/* XOR with previous block */
				for (j = 0; j < blks; j++)
					blk[j] ^= ivp[j];

				exf->encrypt(sw->sw_kschedule, blk);

				/*
				 * Keep encrypted block for XOR'ing
				 * with next block
				 */
				bcopy(blk, iv, blks);
				ivp = iv;
			} else {	/* decrypt */
				/*	
				 * Keep encrypted block for XOR'ing
				 * with next block
				 */
				nivp = (ivp == iv) ? iv2 : iv;
				bcopy(blk, nivp, blks);

				exf->decrypt(sw->sw_kschedule, blk);

				/* XOR with previous block */
				for (j = 0; j < blks; j++)
					blk[j] ^= ivp[j];

				ivp = nivp;
			}

			/* Copy back decrypted block */
			cuio_copyback(uio, count, blks, blk);

			count += blks;

			/* Advance pointer */
			ind = cuio_getptr(uio, count, &k);
			if (ind == -1) {
				error = EINVAL;
				goto out;
			}

			i -= blks;

			/* Could be done... */
			if (i == 0)
				break;
		}

		while (uio->uio_iov[ind].iov_len >= k + blks && i > 0) {
			uint8_t *idat;
			size_t nb, rem;

			nb = blks;
			rem = MIN((size_t)i,
			    uio->uio_iov[ind].iov_len - (size_t)k);
			idat = (uint8_t *)uio->uio_iov[ind].iov_base + k;

			if (exf->reinit) {
				if ((crd->crd_flags & CRD_F_ENCRYPT) != 0 &&
				    exf->encrypt_multi == NULL)
					exf->encrypt(sw->sw_kschedule,
					    idat);
				else if ((crd->crd_flags & CRD_F_ENCRYPT) != 0) {
					nb = rounddown(rem, blks);
					exf->encrypt_multi(sw->sw_kschedule,
					    idat, nb);
				} else if (exf->decrypt_multi == NULL)
					exf->decrypt(sw->sw_kschedule,
					    idat);
				else {
					nb = rounddown(rem, blks);
					exf->decrypt_multi(sw->sw_kschedule,
					    idat, nb);
				}
			} else if (crd->crd_flags & CRD_F_ENCRYPT) {
				/* XOR with previous block/IV */
				for (j = 0; j < blks; j++)
					idat[j] ^= ivp[j];

				exf->encrypt(sw->sw_kschedule, idat);
				ivp = idat;
			} else {	/* decrypt */
				/*
				 * Keep encrypted block to be used
				 * in next block's processing.
				 */
				nivp = (ivp == iv) ? iv2 : iv;
				bcopy(idat, nivp, blks);

				exf->decrypt(sw->sw_kschedule, idat);

				/* XOR with previous block/IV */
				for (j = 0; j < blks; j++)
					idat[j] ^= ivp[j];

				ivp = nivp;
			}

			count += nb;
			k += nb;
			i -= nb;
		}

		/*
		 * Advance to the next iov if the end of the current iov
		 * is aligned with the end of a cipher block.
		 * Note that the code is equivalent to calling:
		 *      ind = cuio_getptr(uio, count, &k);
		 */
		if (i > 0 && k == uio->uio_iov[ind].iov_len) {
			k = 0;
			ind++;
			if (ind >= uio->uio_iovcnt) {
				error = EINVAL;
				goto out;
			}
		}
	}

out:
	if (iovalloc)
		free(iov, M_CRYPTO_DATA);

	return (error);
}

static int __result_use_check
swcr_authprepare(struct auth_hash *axf, struct swcr_data *sw, u_char *key,
    int klen)
{
	int k;

	klen /= 8;

	switch (axf->type) {
	case CRYPTO_MD5_HMAC:
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_SHA2_224_HMAC:
	case CRYPTO_SHA2_256_HMAC:
	case CRYPTO_SHA2_384_HMAC:
	case CRYPTO_SHA2_512_HMAC:
	case CRYPTO_NULL_HMAC:
	case CRYPTO_RIPEMD160_HMAC:
		for (k = 0; k < klen; k++)
			key[k] ^= HMAC_IPAD_VAL;
	
		axf->Init(sw->sw_ictx);
		axf->Update(sw->sw_ictx, key, klen);
		axf->Update(sw->sw_ictx, hmac_ipad_buffer, axf->blocksize - klen);
	
		for (k = 0; k < klen; k++)
			key[k] ^= (HMAC_IPAD_VAL ^ HMAC_OPAD_VAL);
	
		axf->Init(sw->sw_octx);
		axf->Update(sw->sw_octx, key, klen);
		axf->Update(sw->sw_octx, hmac_opad_buffer, axf->blocksize - klen);
	
		for (k = 0; k < klen; k++)
			key[k] ^= HMAC_OPAD_VAL;
		break;
	case CRYPTO_MD5_KPDK:
	case CRYPTO_SHA1_KPDK:
	{
		/* 
		 * We need a buffer that can hold an md5 and a sha1 result
		 * just to throw it away.
		 * What we do here is the initial part of:
		 *   ALGO( key, keyfill, .. )
		 * adding the key to sw_ictx and abusing Final() to get the
		 * "keyfill" padding.
		 * In addition we abuse the sw_octx to save the key to have
		 * it to be able to append it at the end in swcr_authcompute().
		 */
		u_char buf[SHA1_RESULTLEN];

		sw->sw_klen = klen;
		bcopy(key, sw->sw_octx, klen);
		axf->Init(sw->sw_ictx);
		axf->Update(sw->sw_ictx, key, klen);
		axf->Final(buf, sw->sw_ictx);
		break;
	}
	case CRYPTO_POLY1305:
		if (klen != POLY1305_KEY_LEN) {
			CRYPTDEB("bad poly1305 key size %d", klen);
			return EINVAL;
		}
		/* FALLTHROUGH */
	case CRYPTO_BLAKE2B:
	case CRYPTO_BLAKE2S:
		axf->Setkey(sw->sw_ictx, key, klen);
		axf->Init(sw->sw_ictx);
		break;
	default:
		printf("%s: CRD_F_KEY_EXPLICIT flag given, but algorithm %d "
		    "doesn't use keys.\n", __func__, axf->type);
		return EINVAL;
	}
	return 0;
}

/*
 * Compute keyed-hash authenticator.
 */
static int
swcr_authcompute(struct cryptodesc *crd, struct swcr_data *sw, caddr_t buf,
    int flags)
{
	unsigned char aalg[HASH_MAX_LEN];
	struct auth_hash *axf;
	union authctx ctx;
	int err;

	if (sw->sw_ictx == 0)
		return EINVAL;

	axf = sw->sw_axf;

	if (crd->crd_flags & CRD_F_KEY_EXPLICIT) {
		err = swcr_authprepare(axf, sw, crd->crd_key, crd->crd_klen);
		if (err != 0)
			return err;
	}

	bcopy(sw->sw_ictx, &ctx, axf->ctxsize);

	err = crypto_apply(flags, buf, crd->crd_skip, crd->crd_len,
	    (int (*)(void *, void *, unsigned int))axf->Update, (caddr_t)&ctx);
	if (err)
		return err;

	switch (sw->sw_alg) {
	case CRYPTO_SHA1:
	case CRYPTO_SHA2_224:
	case CRYPTO_SHA2_256:
	case CRYPTO_SHA2_384:
	case CRYPTO_SHA2_512:
		axf->Final(aalg, &ctx);
		break;

	case CRYPTO_MD5_HMAC:
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_SHA2_224_HMAC:
	case CRYPTO_SHA2_256_HMAC:
	case CRYPTO_SHA2_384_HMAC:
	case CRYPTO_SHA2_512_HMAC:
	case CRYPTO_RIPEMD160_HMAC:
		if (sw->sw_octx == NULL)
			return EINVAL;

		axf->Final(aalg, &ctx);
		bcopy(sw->sw_octx, &ctx, axf->ctxsize);
		axf->Update(&ctx, aalg, axf->hashsize);
		axf->Final(aalg, &ctx);
		break;

	case CRYPTO_MD5_KPDK:
	case CRYPTO_SHA1_KPDK:
		/* If we have no key saved, return error. */
		if (sw->sw_octx == NULL)
			return EINVAL;

		/*
		 * Add the trailing copy of the key (see comment in
		 * swcr_authprepare()) after the data:
		 *   ALGO( .., key, algofill )
		 * and let Final() do the proper, natural "algofill"
		 * padding.
		 */
		axf->Update(&ctx, sw->sw_octx, sw->sw_klen);
		axf->Final(aalg, &ctx);
		break;

	case CRYPTO_BLAKE2B:
	case CRYPTO_BLAKE2S:
	case CRYPTO_NULL_HMAC:
	case CRYPTO_POLY1305:
		axf->Final(aalg, &ctx);
		break;
	}

	/* Inject the authentication data */
	crypto_copyback(flags, buf, crd->crd_inject,
	    sw->sw_mlen == 0 ? axf->hashsize : sw->sw_mlen, aalg);
	return 0;
}

CTASSERT(INT_MAX <= (1ll<<39) - 256);	/* GCM: plain text < 2^39-256 */
CTASSERT(INT_MAX <= (uint64_t)-1);	/* GCM: associated data <= 2^64-1 */

/*
 * Apply a combined encryption-authentication transformation
 */
static int
swcr_authenc(struct cryptop *crp)
{
	uint32_t blkbuf[howmany(EALG_MAX_BLOCK_LEN, sizeof(uint32_t))];
	u_char *blk = (u_char *)blkbuf;
	u_char aalg[AALG_MAX_RESULT_LEN];
	u_char uaalg[AALG_MAX_RESULT_LEN];
	u_char iv[EALG_MAX_BLOCK_LEN];
	union authctx ctx;
	struct swcr_session *ses;
	struct cryptodesc *crd, *crda = NULL, *crde = NULL;
	struct swcr_data *sw, *swa, *swe = NULL;
	struct auth_hash *axf = NULL;
	struct enc_xform *exf = NULL;
	caddr_t buf = (caddr_t)crp->crp_buf;
	uint32_t *blkp;
	int aadlen, blksz, i, ivlen, len, iskip, oskip, r;
	int isccm = 0;

	ivlen = blksz = iskip = oskip = 0;

	ses = crypto_get_driver_session(crp->crp_session);

	for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
		for (i = 0; i < nitems(ses->swcr_algorithms) &&
		    ses->swcr_algorithms[i].sw_alg != crd->crd_alg; i++)
			;
		if (i == nitems(ses->swcr_algorithms))
			return (EINVAL);

		sw = &ses->swcr_algorithms[i];
		switch (sw->sw_alg) {
		case CRYPTO_AES_CCM_16:
		case CRYPTO_AES_NIST_GCM_16:
		case CRYPTO_AES_NIST_GMAC:
			swe = sw;
			crde = crd;
			exf = swe->sw_exf;
			/* AES_CCM_IV_LEN and AES_GCM_IV_LEN are both 12 */
			ivlen = AES_CCM_IV_LEN;
			break;
		case CRYPTO_AES_CCM_CBC_MAC:
			isccm = 1;
			/* FALLTHROUGH */
		case CRYPTO_AES_128_NIST_GMAC:
		case CRYPTO_AES_192_NIST_GMAC:
		case CRYPTO_AES_256_NIST_GMAC:
			swa = sw;
			crda = crd;
			axf = swa->sw_axf;
			if (swa->sw_ictx == 0)
				return (EINVAL);
			bcopy(swa->sw_ictx, &ctx, axf->ctxsize);
			blksz = axf->blocksize;
			break;
		default:
			return (EINVAL);
		}
	}
	if (crde == NULL || crda == NULL)
		return (EINVAL);
	/*
	 * We need to make sure that the auth algorithm matches the
	 * encr algorithm.  Specifically, for AES-GCM must go with
	 * AES NIST GMAC, and AES-CCM must go with CBC-MAC.
	 */
	if (crde->crd_alg == CRYPTO_AES_NIST_GCM_16) {
		switch (crda->crd_alg) {
		case CRYPTO_AES_128_NIST_GMAC:
		case CRYPTO_AES_192_NIST_GMAC:
		case CRYPTO_AES_256_NIST_GMAC:
			break;	/* Good! */
		default:
			return (EINVAL);	/* Not good! */
		}
	} else if (crde->crd_alg == CRYPTO_AES_CCM_16 &&
	    crda->crd_alg != CRYPTO_AES_CCM_CBC_MAC)
		return (EINVAL);

	if ((crde->crd_alg == CRYPTO_AES_NIST_GCM_16 ||
	     crde->crd_alg == CRYPTO_AES_CCM_16) &&
	    (crde->crd_flags & CRD_F_IV_EXPLICIT) == 0)
		return (EINVAL);

	if (crde->crd_klen != crda->crd_klen)
		return (EINVAL);

	/* Initialize the IV */
	if (crde->crd_flags & CRD_F_ENCRYPT) {
		/* IV explicitly provided ? */
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crde->crd_iv, iv, ivlen);
		else
			arc4rand(iv, ivlen, 0);

		/* Do we need to write the IV */
		if (!(crde->crd_flags & CRD_F_IV_PRESENT))
			crypto_copyback(crp->crp_flags, buf, crde->crd_inject,
			    ivlen, iv);

	} else {	/* Decryption */
			/* IV explicitly provided ? */
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crde->crd_iv, iv, ivlen);
		else {
			/* Get IV off buf */
			crypto_copydata(crp->crp_flags, buf, crde->crd_inject,
			    ivlen, iv);
		}
	}

	if (swa->sw_alg == CRYPTO_AES_CCM_CBC_MAC) {
		/*
		 * AES CCM-CBC needs to know the length of
		 * both the auth data, and payload data, before
		 * doing the auth computation.
		 */
		ctx.aes_cbc_mac_ctx.authDataLength = crda->crd_len;
		ctx.aes_cbc_mac_ctx.cryptDataLength = crde->crd_len;
	}
	/* Supply MAC with IV */
	if (axf->Reinit)
		axf->Reinit(&ctx, iv, ivlen);

	/* Supply MAC with AAD */
	aadlen = crda->crd_len;

	for (i = iskip; i < crda->crd_len; i += blksz) {
		len = MIN(crda->crd_len - i, blksz - oskip);
		crypto_copydata(crp->crp_flags, buf, crda->crd_skip + i, len,
		    blk + oskip);
		bzero(blk + len + oskip, blksz - len - oskip);
		axf->Update(&ctx, blk, blksz);
		oskip = 0; /* reset initial output offset */
	}

	if (exf->reinit)
		exf->reinit(swe->sw_kschedule, iv);

	/* Do encryption/decryption with MAC */
	for (i = 0; i < crde->crd_len; i += len) {
		if (exf->encrypt_multi != NULL) {
			len = rounddown(crde->crd_len - i, blksz);
			if (len == 0)
				len = blksz;
			else
				len = MIN(len, sizeof(blkbuf));
		} else
			len = blksz;
		len = MIN(crde->crd_len - i, len);
		if (len < blksz)
			bzero(blk, blksz);
		crypto_copydata(crp->crp_flags, buf, crde->crd_skip + i, len,
		    blk);
		/*
		 * One of the problems with CCM+CBC is that the authentication
		 * is done on the unecncrypted data.  As a result, we have
		 * to do the authentication update at different times,
		 * depending on whether it's CCM or not.
		 */
		if (crde->crd_flags & CRD_F_ENCRYPT) {
			if (isccm)
				axf->Update(&ctx, blk, len);
			if (exf->encrypt_multi != NULL)
				exf->encrypt_multi(swe->sw_kschedule, blk,
				    len);
			else
				exf->encrypt(swe->sw_kschedule, blk);
			if (!isccm)
				axf->Update(&ctx, blk, len);
			crypto_copyback(crp->crp_flags, buf,
			    crde->crd_skip + i, len, blk);
		} else {
			if (isccm) {
				KASSERT(exf->encrypt_multi == NULL,
				    ("assume CCM is single-block only"));
				exf->decrypt(swe->sw_kschedule, blk);
			}
			axf->Update(&ctx, blk, len);
		}
	}

	/* Do any required special finalization */
	switch (crda->crd_alg) {
		case CRYPTO_AES_128_NIST_GMAC:
		case CRYPTO_AES_192_NIST_GMAC:
		case CRYPTO_AES_256_NIST_GMAC:
			/* length block */
			bzero(blk, blksz);
			blkp = (uint32_t *)blk + 1;
			*blkp = htobe32(aadlen * 8);
			blkp = (uint32_t *)blk + 3;
			*blkp = htobe32(crde->crd_len * 8);
			axf->Update(&ctx, blk, blksz);
			break;
	}

	/* Finalize MAC */
	axf->Final(aalg, &ctx);

	/* Validate tag */
	if (!(crde->crd_flags & CRD_F_ENCRYPT)) {
		crypto_copydata(crp->crp_flags, buf, crda->crd_inject,
		    axf->hashsize, uaalg);

		r = timingsafe_bcmp(aalg, uaalg, axf->hashsize);
		if (r == 0) {
			/* tag matches, decrypt data */
			if (isccm) {
				KASSERT(exf->reinit != NULL,
				    ("AES-CCM reinit function must be set"));
				exf->reinit(swe->sw_kschedule, iv);
			}
			for (i = 0; i < crde->crd_len; i += blksz) {
				len = MIN(crde->crd_len - i, blksz);
				if (len < blksz)
					bzero(blk, blksz);
				crypto_copydata(crp->crp_flags, buf,
				    crde->crd_skip + i, len, blk);
				exf->decrypt(swe->sw_kschedule, blk);
				crypto_copyback(crp->crp_flags, buf,
				    crde->crd_skip + i, len, blk);
			}
		} else
			return (EBADMSG);
	} else {
		/* Inject the authentication data */
		crypto_copyback(crp->crp_flags, buf, crda->crd_inject,
		    axf->hashsize, aalg);
	}

	return (0);
}

/*
 * Apply a compression/decompression algorithm
 */
static int
swcr_compdec(struct cryptodesc *crd, struct swcr_data *sw,
    caddr_t buf, int flags)
{
	u_int8_t *data, *out;
	struct comp_algo *cxf;
	int adj;
	u_int32_t result;

	cxf = sw->sw_cxf;

	/* We must handle the whole buffer of data in one time
	 * then if there is not all the data in the mbuf, we must
	 * copy in a buffer.
	 */

	data = malloc(crd->crd_len, M_CRYPTO_DATA,  M_NOWAIT);
	if (data == NULL)
		return (EINVAL);
	crypto_copydata(flags, buf, crd->crd_skip, crd->crd_len, data);

	if (crd->crd_flags & CRD_F_COMP)
		result = cxf->compress(data, crd->crd_len, &out);
	else
		result = cxf->decompress(data, crd->crd_len, &out);

	free(data, M_CRYPTO_DATA);
	if (result == 0)
		return EINVAL;

	/* Copy back the (de)compressed data. m_copyback is
	 * extending the mbuf as necessary.
	 */
	sw->sw_size = result;
	/* Check the compressed size when doing compression */
	if (crd->crd_flags & CRD_F_COMP) {
		if (result >= crd->crd_len) {
			/* Compression was useless, we lost time */
			free(out, M_CRYPTO_DATA);
			return 0;
		}
	}

	crypto_copyback(flags, buf, crd->crd_skip, result, out);
	if (result < crd->crd_len) {
		adj = result - crd->crd_len;
		if (flags & CRYPTO_F_IMBUF) {
			adj = result - crd->crd_len;
			m_adj((struct mbuf *)buf, adj);
		} else if (flags & CRYPTO_F_IOV) {
			struct uio *uio = (struct uio *)buf;
			int ind;

			adj = crd->crd_len - result;
			ind = uio->uio_iovcnt - 1;

			while (adj > 0 && ind >= 0) {
				if (adj < uio->uio_iov[ind].iov_len) {
					uio->uio_iov[ind].iov_len -= adj;
					break;
				}

				adj -= uio->uio_iov[ind].iov_len;
				uio->uio_iov[ind].iov_len = 0;
				ind--;
				uio->uio_iovcnt--;
			}
		}
	}
	free(out, M_CRYPTO_DATA);
	return 0;
}

/*
 * Generate a new software session.
 */
static int
swcr_newsession(device_t dev, crypto_session_t cses, struct cryptoini *cri)
{
	struct swcr_session *ses;
	struct swcr_data *swd;
	struct auth_hash *axf;
	struct enc_xform *txf;
	struct comp_algo *cxf;
	size_t i;
	int len;
	int error;

	if (cses == NULL || cri == NULL)
		return EINVAL;

	ses = crypto_get_driver_session(cses);
	mtx_init(&ses->swcr_lock, "swcr session lock", NULL, MTX_DEF);

	for (i = 0; cri != NULL && i < nitems(ses->swcr_algorithms); i++) {
		swd = &ses->swcr_algorithms[i];

		switch (cri->cri_alg) {
		case CRYPTO_DES_CBC:
			txf = &enc_xform_des;
			goto enccommon;
		case CRYPTO_3DES_CBC:
			txf = &enc_xform_3des;
			goto enccommon;
		case CRYPTO_BLF_CBC:
			txf = &enc_xform_blf;
			goto enccommon;
		case CRYPTO_CAST_CBC:
			txf = &enc_xform_cast5;
			goto enccommon;
		case CRYPTO_SKIPJACK_CBC:
			txf = &enc_xform_skipjack;
			goto enccommon;
		case CRYPTO_RIJNDAEL128_CBC:
			txf = &enc_xform_rijndael128;
			goto enccommon;
		case CRYPTO_AES_XTS:
			txf = &enc_xform_aes_xts;
			goto enccommon;
		case CRYPTO_AES_ICM:
			txf = &enc_xform_aes_icm;
			goto enccommon;
		case CRYPTO_AES_NIST_GCM_16:
			txf = &enc_xform_aes_nist_gcm;
			goto enccommon;
		case CRYPTO_AES_CCM_16:
			txf = &enc_xform_ccm;
			goto enccommon;
		case CRYPTO_AES_NIST_GMAC:
			txf = &enc_xform_aes_nist_gmac;
			swd->sw_exf = txf;
			break;
		case CRYPTO_CAMELLIA_CBC:
			txf = &enc_xform_camellia;
			goto enccommon;
		case CRYPTO_NULL_CBC:
			txf = &enc_xform_null;
			goto enccommon;
		case CRYPTO_CHACHA20:
			txf = &enc_xform_chacha20;
			goto enccommon;
		enccommon:
			if (cri->cri_key != NULL) {
				error = txf->setkey(&swd->sw_kschedule,
				    cri->cri_key, cri->cri_klen / 8);
				if (error) {
					swcr_freesession(dev, cses);
					return error;
				}
			}
			swd->sw_exf = txf;
			break;
	
		case CRYPTO_MD5_HMAC:
			axf = &auth_hash_hmac_md5;
			goto authcommon;
		case CRYPTO_SHA1_HMAC:
			axf = &auth_hash_hmac_sha1;
			goto authcommon;
		case CRYPTO_SHA2_224_HMAC:
			axf = &auth_hash_hmac_sha2_224;
			goto authcommon;
		case CRYPTO_SHA2_256_HMAC:
			axf = &auth_hash_hmac_sha2_256;
			goto authcommon;
		case CRYPTO_SHA2_384_HMAC:
			axf = &auth_hash_hmac_sha2_384;
			goto authcommon;
		case CRYPTO_SHA2_512_HMAC:
			axf = &auth_hash_hmac_sha2_512;
			goto authcommon;
		case CRYPTO_NULL_HMAC:
			axf = &auth_hash_null;
			goto authcommon;
		case CRYPTO_RIPEMD160_HMAC:
			axf = &auth_hash_hmac_ripemd_160;
		authcommon:
			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				swcr_freesession(dev, cses);
				return ENOBUFS;
			}
	
			swd->sw_octx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_octx == NULL) {
				swcr_freesession(dev, cses);
				return ENOBUFS;
			}

			if (cri->cri_key != NULL) {
				error = swcr_authprepare(axf, swd,
				    cri->cri_key, cri->cri_klen);
				if (error != 0) {
					swcr_freesession(dev, cses);
					return error;
				}
			}

			swd->sw_mlen = cri->cri_mlen;
			swd->sw_axf = axf;
			break;
	
		case CRYPTO_MD5_KPDK:
			axf = &auth_hash_key_md5;
			goto auth2common;
	
		case CRYPTO_SHA1_KPDK:
			axf = &auth_hash_key_sha1;
		auth2common:
			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				swcr_freesession(dev, cses);
				return ENOBUFS;
			}
	
			swd->sw_octx = malloc(cri->cri_klen / 8,
			    M_CRYPTO_DATA, M_NOWAIT);
			if (swd->sw_octx == NULL) {
				swcr_freesession(dev, cses);
				return ENOBUFS;
			}

			/* Store the key so we can "append" it to the payload */
			if (cri->cri_key != NULL) {
				error = swcr_authprepare(axf, swd,
				    cri->cri_key, cri->cri_klen);
				if (error != 0) {
					swcr_freesession(dev, cses);
					return error;
				}
			}

			swd->sw_mlen = cri->cri_mlen;
			swd->sw_axf = axf;
			break;
#ifdef notdef
		case CRYPTO_MD5:
			axf = &auth_hash_md5;
			goto auth3common;
#endif

		case CRYPTO_SHA1:
			axf = &auth_hash_sha1;
			goto auth3common;
		case CRYPTO_SHA2_224:
			axf = &auth_hash_sha2_224;
			goto auth3common;
		case CRYPTO_SHA2_256:
			axf = &auth_hash_sha2_256;
			goto auth3common;
		case CRYPTO_SHA2_384:
			axf = &auth_hash_sha2_384;
			goto auth3common;
		case CRYPTO_SHA2_512:
			axf = &auth_hash_sha2_512;

		auth3common:
			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				swcr_freesession(dev, cses);
				return ENOBUFS;
			}

			axf->Init(swd->sw_ictx);
			swd->sw_mlen = cri->cri_mlen;
			swd->sw_axf = axf;
			break;

		case CRYPTO_AES_CCM_CBC_MAC:
			switch (cri->cri_klen) {
			case 128:
				axf = &auth_hash_ccm_cbc_mac_128;
				break;
			case 192:
				axf = &auth_hash_ccm_cbc_mac_192;
				break;
			case 256:
				axf = &auth_hash_ccm_cbc_mac_256;
				break;
			default:
				swcr_freesession(dev, cses);
				return EINVAL;
			}
			goto auth4common;
		case CRYPTO_AES_128_NIST_GMAC:
			axf = &auth_hash_nist_gmac_aes_128;
			goto auth4common;

		case CRYPTO_AES_192_NIST_GMAC:
			axf = &auth_hash_nist_gmac_aes_192;
			goto auth4common;

		case CRYPTO_AES_256_NIST_GMAC:
			axf = &auth_hash_nist_gmac_aes_256;
		auth4common:
			len = cri->cri_klen / 8;
			if (len != 16 && len != 24 && len != 32) {
				swcr_freesession(dev, cses);
				return EINVAL;
			}

			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				swcr_freesession(dev, cses);
				return ENOBUFS;
			}
			axf->Init(swd->sw_ictx);
			axf->Setkey(swd->sw_ictx, cri->cri_key, len);
			swd->sw_axf = axf;
			break;

		case CRYPTO_BLAKE2B:
			axf = &auth_hash_blake2b;
			goto auth5common;
		case CRYPTO_BLAKE2S:
			axf = &auth_hash_blake2s;
			goto auth5common;
		case CRYPTO_POLY1305:
			axf = &auth_hash_poly1305;
		auth5common:
			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				swcr_freesession(dev, cses);
				return ENOBUFS;
			}
			axf->Setkey(swd->sw_ictx, cri->cri_key,
			    cri->cri_klen / 8);
			axf->Init(swd->sw_ictx);
			swd->sw_axf = axf;
			break;

		case CRYPTO_DEFLATE_COMP:
			cxf = &comp_algo_deflate;
			swd->sw_cxf = cxf;
			break;
		default:
			swcr_freesession(dev, cses);
			return EINVAL;
		}
	
		swd->sw_alg = cri->cri_alg;
		cri = cri->cri_next;
		ses->swcr_nalgs++;
	}

	if (cri != NULL) {
		CRYPTDEB("Bogus session request for three or more algorithms");
		return EINVAL;
	}
	return 0;
}

static void
swcr_freesession(device_t dev, crypto_session_t cses)
{
	struct swcr_session *ses;
	struct swcr_data *swd;
	struct enc_xform *txf;
	struct auth_hash *axf;
	size_t i;

	ses = crypto_get_driver_session(cses);

	mtx_destroy(&ses->swcr_lock);
	for (i = 0; i < nitems(ses->swcr_algorithms); i++) {
		swd = &ses->swcr_algorithms[i];

		switch (swd->sw_alg) {
		case CRYPTO_DES_CBC:
		case CRYPTO_3DES_CBC:
		case CRYPTO_BLF_CBC:
		case CRYPTO_CAST_CBC:
		case CRYPTO_SKIPJACK_CBC:
		case CRYPTO_RIJNDAEL128_CBC:
		case CRYPTO_AES_XTS:
		case CRYPTO_AES_ICM:
		case CRYPTO_AES_NIST_GCM_16:
		case CRYPTO_AES_NIST_GMAC:
		case CRYPTO_CAMELLIA_CBC:
		case CRYPTO_NULL_CBC:
		case CRYPTO_CHACHA20:
		case CRYPTO_AES_CCM_16:
			txf = swd->sw_exf;

			if (swd->sw_kschedule)
				txf->zerokey(&(swd->sw_kschedule));
			break;

		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_SHA2_224_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_NULL_HMAC:
		case CRYPTO_AES_CCM_CBC_MAC:
			axf = swd->sw_axf;

			if (swd->sw_ictx) {
				bzero(swd->sw_ictx, axf->ctxsize);
				free(swd->sw_ictx, M_CRYPTO_DATA);
			}
			if (swd->sw_octx) {
				bzero(swd->sw_octx, axf->ctxsize);
				free(swd->sw_octx, M_CRYPTO_DATA);
			}
			break;

		case CRYPTO_MD5_KPDK:
		case CRYPTO_SHA1_KPDK:
			axf = swd->sw_axf;

			if (swd->sw_ictx) {
				bzero(swd->sw_ictx, axf->ctxsize);
				free(swd->sw_ictx, M_CRYPTO_DATA);
			}
			if (swd->sw_octx) {
				bzero(swd->sw_octx, swd->sw_klen);
				free(swd->sw_octx, M_CRYPTO_DATA);
			}
			break;

		case CRYPTO_BLAKE2B:
		case CRYPTO_BLAKE2S:
		case CRYPTO_MD5:
		case CRYPTO_POLY1305:
		case CRYPTO_SHA1:
		case CRYPTO_SHA2_224:
		case CRYPTO_SHA2_256:
		case CRYPTO_SHA2_384:
		case CRYPTO_SHA2_512:
		case CRYPTO_AES_128_NIST_GMAC:
		case CRYPTO_AES_192_NIST_GMAC:
		case CRYPTO_AES_256_NIST_GMAC:
			axf = swd->sw_axf;

			if (swd->sw_ictx) {
				explicit_bzero(swd->sw_ictx, axf->ctxsize);
				free(swd->sw_ictx, M_CRYPTO_DATA);
			}
			break;

		case CRYPTO_DEFLATE_COMP:
			/* Nothing to do */
			break;
		}
	}
}

/*
 * Process a software request.
 */
static int
swcr_process(device_t dev, struct cryptop *crp, int hint)
{
	struct swcr_session *ses = NULL;
	struct cryptodesc *crd;
	struct swcr_data *sw;
	size_t i;

	/* Sanity check */
	if (crp == NULL)
		return EINVAL;

	if (crp->crp_desc == NULL || crp->crp_buf == NULL) {
		crp->crp_etype = EINVAL;
		goto done;
	}

	ses = crypto_get_driver_session(crp->crp_session);
	mtx_lock(&ses->swcr_lock);

	/* Go through crypto descriptors, processing as we go */
	for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
		/*
		 * Find the crypto context.
		 *
		 * XXX Note that the logic here prevents us from having
		 * XXX the same algorithm multiple times in a session
		 * XXX (or rather, we can but it won't give us the right
		 * XXX results). To do that, we'd need some way of differentiating
		 * XXX between the various instances of an algorithm (so we can
		 * XXX locate the correct crypto context).
		 */
		for (i = 0; i < nitems(ses->swcr_algorithms) &&
		    ses->swcr_algorithms[i].sw_alg != crd->crd_alg; i++)
			;

		/* No such context ? */
		if (i == nitems(ses->swcr_algorithms)) {
			crp->crp_etype = EINVAL;
			goto done;
		}
		sw = &ses->swcr_algorithms[i];
		switch (sw->sw_alg) {
		case CRYPTO_DES_CBC:
		case CRYPTO_3DES_CBC:
		case CRYPTO_BLF_CBC:
		case CRYPTO_CAST_CBC:
		case CRYPTO_SKIPJACK_CBC:
		case CRYPTO_RIJNDAEL128_CBC:
		case CRYPTO_AES_XTS:
		case CRYPTO_AES_ICM:
		case CRYPTO_CAMELLIA_CBC:
		case CRYPTO_CHACHA20:
			if ((crp->crp_etype = swcr_encdec(crd, sw,
			    crp->crp_buf, crp->crp_flags)) != 0)
				goto done;
			break;
		case CRYPTO_NULL_CBC:
			crp->crp_etype = 0;
			break;
		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_SHA2_224_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_NULL_HMAC:
		case CRYPTO_MD5_KPDK:
		case CRYPTO_SHA1_KPDK:
		case CRYPTO_MD5:
		case CRYPTO_SHA1:
		case CRYPTO_SHA2_224:
		case CRYPTO_SHA2_256:
		case CRYPTO_SHA2_384:
		case CRYPTO_SHA2_512:
		case CRYPTO_BLAKE2B:
		case CRYPTO_BLAKE2S:
		case CRYPTO_POLY1305:
			if ((crp->crp_etype = swcr_authcompute(crd, sw,
			    crp->crp_buf, crp->crp_flags)) != 0)
				goto done;
			break;

		case CRYPTO_AES_NIST_GCM_16:
		case CRYPTO_AES_NIST_GMAC:
		case CRYPTO_AES_128_NIST_GMAC:
		case CRYPTO_AES_192_NIST_GMAC:
		case CRYPTO_AES_256_NIST_GMAC:
		case CRYPTO_AES_CCM_16:
		case CRYPTO_AES_CCM_CBC_MAC:
			crp->crp_etype = swcr_authenc(crp);
			goto done;

		case CRYPTO_DEFLATE_COMP:
			if ((crp->crp_etype = swcr_compdec(crd, sw, 
			    crp->crp_buf, crp->crp_flags)) != 0)
				goto done;
			else
				crp->crp_olen = (int)sw->sw_size;
			break;

		default:
			/* Unknown/unsupported algorithm */
			crp->crp_etype = EINVAL;
			goto done;
		}
	}

done:
	if (ses)
		mtx_unlock(&ses->swcr_lock);
	crypto_done(crp);
	return 0;
}

static void
swcr_identify(driver_t *drv, device_t parent)
{
	/* NB: order 10 is so we get attached after h/w devices */
	if (device_find_child(parent, "cryptosoft", -1) == NULL &&
	    BUS_ADD_CHILD(parent, 10, "cryptosoft", 0) == 0)
		panic("cryptosoft: could not attach");
}

static int
swcr_probe(device_t dev)
{
	device_set_desc(dev, "software crypto");
	return (BUS_PROBE_NOWILDCARD);
}

static int
swcr_attach(device_t dev)
{
	memset(hmac_ipad_buffer, HMAC_IPAD_VAL, HMAC_MAX_BLOCK_LEN);
	memset(hmac_opad_buffer, HMAC_OPAD_VAL, HMAC_MAX_BLOCK_LEN);

	swcr_id = crypto_get_driverid(dev, sizeof(struct swcr_session),
			CRYPTOCAP_F_SOFTWARE | CRYPTOCAP_F_SYNC);
	if (swcr_id < 0) {
		device_printf(dev, "cannot initialize!");
		return ENOMEM;
	}
#define	REGISTER(alg) \
	crypto_register(swcr_id, alg, 0,0)
	REGISTER(CRYPTO_DES_CBC);
	REGISTER(CRYPTO_3DES_CBC);
	REGISTER(CRYPTO_BLF_CBC);
	REGISTER(CRYPTO_CAST_CBC);
	REGISTER(CRYPTO_SKIPJACK_CBC);
	REGISTER(CRYPTO_NULL_CBC);
	REGISTER(CRYPTO_MD5_HMAC);
	REGISTER(CRYPTO_SHA1_HMAC);
	REGISTER(CRYPTO_SHA2_224_HMAC);
	REGISTER(CRYPTO_SHA2_256_HMAC);
	REGISTER(CRYPTO_SHA2_384_HMAC);
	REGISTER(CRYPTO_SHA2_512_HMAC);
	REGISTER(CRYPTO_RIPEMD160_HMAC);
	REGISTER(CRYPTO_NULL_HMAC);
	REGISTER(CRYPTO_MD5_KPDK);
	REGISTER(CRYPTO_SHA1_KPDK);
	REGISTER(CRYPTO_MD5);
	REGISTER(CRYPTO_SHA1);
	REGISTER(CRYPTO_SHA2_224);
	REGISTER(CRYPTO_SHA2_256);
	REGISTER(CRYPTO_SHA2_384);
	REGISTER(CRYPTO_SHA2_512);
	REGISTER(CRYPTO_RIJNDAEL128_CBC);
	REGISTER(CRYPTO_AES_XTS);
	REGISTER(CRYPTO_AES_ICM);
	REGISTER(CRYPTO_AES_NIST_GCM_16);
	REGISTER(CRYPTO_AES_NIST_GMAC);
	REGISTER(CRYPTO_AES_128_NIST_GMAC);
	REGISTER(CRYPTO_AES_192_NIST_GMAC);
	REGISTER(CRYPTO_AES_256_NIST_GMAC);
 	REGISTER(CRYPTO_CAMELLIA_CBC);
	REGISTER(CRYPTO_DEFLATE_COMP);
	REGISTER(CRYPTO_BLAKE2B);
	REGISTER(CRYPTO_BLAKE2S);
	REGISTER(CRYPTO_CHACHA20);
	REGISTER(CRYPTO_AES_CCM_16);
	REGISTER(CRYPTO_AES_CCM_CBC_MAC);
	REGISTER(CRYPTO_POLY1305);
#undef REGISTER

	return 0;
}

static int
swcr_detach(device_t dev)
{
	crypto_unregister_all(swcr_id);
	return 0;
}

static device_method_t swcr_methods[] = {
	DEVMETHOD(device_identify,	swcr_identify),
	DEVMETHOD(device_probe,		swcr_probe),
	DEVMETHOD(device_attach,	swcr_attach),
	DEVMETHOD(device_detach,	swcr_detach),

	DEVMETHOD(cryptodev_newsession,	swcr_newsession),
	DEVMETHOD(cryptodev_freesession,swcr_freesession),
	DEVMETHOD(cryptodev_process,	swcr_process),

	{0, 0},
};

static driver_t swcr_driver = {
	"cryptosoft",
	swcr_methods,
	0,		/* NB: no softc */
};
static devclass_t swcr_devclass;

/*
 * NB: We explicitly reference the crypto module so we
 * get the necessary ordering when built as a loadable
 * module.  This is required because we bundle the crypto
 * module code together with the cryptosoft driver (otherwise
 * normal module dependencies would handle things).
 */
extern int crypto_modevent(struct module *, int, void *);
/* XXX where to attach */
DRIVER_MODULE(cryptosoft, nexus, swcr_driver, swcr_devclass, crypto_modevent,0);
MODULE_VERSION(cryptosoft, 1);
MODULE_DEPEND(cryptosoft, crypto, 1, 1, 1);
