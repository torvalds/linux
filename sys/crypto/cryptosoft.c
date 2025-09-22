/*	$OpenBSD: cryptosoft.c,v 1.91 2021/10/24 10:26:22 patrick Exp $	*/

/*
 * The author of this code is Angelos D. Keromytis (angelos@cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Athens, Greece, in
 * February 2000. Network Security Technologies Inc. (NSTI) kindly
 * supported the development of this code.
 *
 * Copyright (c) 2000, 2001 Angelos D. Keromytis
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <crypto/md5.h>
#include <crypto/sha1.h>
#include <crypto/rmd160.h>
#include <crypto/cast.h>
#include <crypto/cryptodev.h>
#include <crypto/cryptosoft.h>
#include <crypto/xform.h>

const u_int8_t hmac_ipad_buffer[HMAC_MAX_BLOCK_LEN] = {
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36,
	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
};

const u_int8_t hmac_opad_buffer[HMAC_MAX_BLOCK_LEN] = {
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
	0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C
};


struct swcr_list *swcr_sessions = NULL;
u_int32_t swcr_sesnum = 0;
int32_t swcr_id = -1;

#define COPYBACK(x, a, b, c, d) \
	do { \
		if ((x) == CRYPTO_BUF_MBUF) \
			m_copyback((struct mbuf *)a,b,c,d,M_NOWAIT); \
		else \
			cuio_copyback((struct uio *)a,b,c,d); \
	} while (0)
#define COPYDATA(x, a, b, c, d) \
	do { \
		if ((x) == CRYPTO_BUF_MBUF) \
			m_copydata((struct mbuf *)a,b,c,d); \
		else \
			cuio_copydata((struct uio *)a,b,c,d); \
	} while (0)

/*
 * Apply a symmetric encryption/decryption algorithm.
 */
int
swcr_encdec(struct cryptodesc *crd, struct swcr_data *sw, caddr_t buf,
    int outtype)
{
	unsigned char iv[EALG_MAX_BLOCK_LEN], blk[EALG_MAX_BLOCK_LEN], *idat;
	unsigned char *ivp, *nivp, iv2[EALG_MAX_BLOCK_LEN];
	const struct enc_xform *exf;
	int i, k, j, blks, ind, count, ivlen;
	struct mbuf *m = NULL;
	struct uio *uio = NULL;

	exf = sw->sw_exf;
	blks = exf->blocksize;
	ivlen = exf->ivsize;

	/* Check for non-padded data */
	if (crd->crd_len % blks)
		return EINVAL;

	if (outtype == CRYPTO_BUF_MBUF)
		m = (struct mbuf *) buf;
	else
		uio = (struct uio *) buf;

	/* Initialize the IV */
	if (crd->crd_flags & CRD_F_ENCRYPT) {
		/* IV explicitly provided ? */
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, iv, ivlen);
		else
			arc4random_buf(iv, ivlen);

		/* Do we need to write the IV */
		if (!(crd->crd_flags & CRD_F_IV_PRESENT))
			COPYBACK(outtype, buf, crd->crd_inject, ivlen, iv);

	} else {	/* Decryption */
			/* IV explicitly provided ? */
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crd->crd_iv, iv, ivlen);
		else {
			/* Get IV off buf */
			COPYDATA(outtype, buf, crd->crd_inject, ivlen, iv);
		}
	}

	ivp = iv;

	/*
	 * xforms that provide a reinit method perform all IV
	 * handling themselves.
	 */
	if (exf->reinit)
		exf->reinit(sw->sw_kschedule, iv);

	if (outtype == CRYPTO_BUF_MBUF) {
		/* Find beginning of data */
		m = m_getptr(m, crd->crd_skip, &k);
		if (m == NULL)
			return EINVAL;

		i = crd->crd_len;

		while (i > 0) {
			/*
			 * If there's insufficient data at the end of
			 * an mbuf, we have to do some copying.
			 */
			if (m->m_len < k + blks && m->m_len != k) {
				m_copydata(m, k, blks, blk);

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
				m_copyback(m, k, blks, blk, M_NOWAIT);

				/* Advance pointer */
				m = m_getptr(m, k + blks, &k);
				if (m == NULL)
					return EINVAL;

				i -= blks;

				/* Could be done... */
				if (i == 0)
					break;
			}

			/* Skip possibly empty mbufs */
			if (k == m->m_len) {
				for (m = m->m_next; m && m->m_len == 0;
				    m = m->m_next)
					;
				k = 0;
			}

			/* Sanity check */
			if (m == NULL)
				return EINVAL;

			/*
			 * Warning: idat may point to garbage here, but
			 * we only use it in the while() loop, only if
			 * there are indeed enough data.
			 */
			idat = mtod(m, unsigned char *) + k;

			while (m->m_len >= k + blks && i > 0) {
				if (exf->reinit) {
					if (crd->crd_flags & CRD_F_ENCRYPT) {
						exf->encrypt(sw->sw_kschedule,
						    idat);
					} else {
						exf->decrypt(sw->sw_kschedule,
						    idat);
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

				idat += blks;
				k += blks;
				i -= blks;
			}
		}
	} else {
		/* Find beginning of data */
		count = crd->crd_skip;
		ind = cuio_getptr(uio, count, &k);
		if (ind == -1)
			return EINVAL;

		i = crd->crd_len;

		while (i > 0) {
			/*
			 * If there's insufficient data at the end,
			 * we have to do some copying.
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
				if (ind == -1)
					return (EINVAL);

				i -= blks;

				/* Could be done... */
				if (i == 0)
					break;
			}

			/*
			 * Warning: idat may point to garbage here, but
			 * we only use it in the while() loop, only if
			 * there are indeed enough data.
			 */
			idat = (char *)uio->uio_iov[ind].iov_base + k;

			while (uio->uio_iov[ind].iov_len >= k + blks &&
			    i > 0) {
				if (exf->reinit) {
					if (crd->crd_flags & CRD_F_ENCRYPT) {
						exf->encrypt(sw->sw_kschedule,
						    idat);
					} else {
						exf->decrypt(sw->sw_kschedule,
						    idat);
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

				idat += blks;
				count += blks;
				k += blks;
				i -= blks;
			}

			/*
			 * Advance to the next iov if the end of the current iov
			 * is aligned with the end of a cipher block.
			 * Note that the code is equivalent to calling:
			 *	ind = cuio_getptr(uio, count, &k);
			 */
			if (i > 0 && k == uio->uio_iov[ind].iov_len) {
				k = 0;
				ind++;
				if (ind >= uio->uio_iovcnt)
					return (EINVAL);
			}
		}
	}

	return 0; /* Done with encryption/decryption */
}

/*
 * Compute keyed-hash authenticator.
 */
int
swcr_authcompute(struct cryptop *crp, struct cryptodesc *crd,
    struct swcr_data *sw, caddr_t buf, int outtype)
{
	unsigned char aalg[AALG_MAX_RESULT_LEN];
	const struct auth_hash *axf;
	union authctx ctx;
	int err;

	if (sw->sw_ictx == 0)
		return EINVAL;

	axf = sw->sw_axf;

	bcopy(sw->sw_ictx, &ctx, axf->ctxsize);

	if (outtype == CRYPTO_BUF_MBUF)
		err = m_apply((struct mbuf *) buf, crd->crd_skip, crd->crd_len,
		    (int (*)(caddr_t, caddr_t, unsigned int)) axf->Update,
		    (caddr_t) &ctx);
	else
		err = cuio_apply((struct uio *) buf, crd->crd_skip,
		    crd->crd_len,
		    (int (*)(caddr_t, caddr_t, unsigned int)) axf->Update,
		    (caddr_t) &ctx);

	if (err)
		return err;

	if (crd->crd_flags & CRD_F_ESN)
		axf->Update(&ctx, crd->crd_esn, 4);

	switch (sw->sw_alg) {
	case CRYPTO_MD5_HMAC:
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_RIPEMD160_HMAC:
	case CRYPTO_SHA2_256_HMAC:
	case CRYPTO_SHA2_384_HMAC:
	case CRYPTO_SHA2_512_HMAC:
		if (sw->sw_octx == NULL)
			return EINVAL;

		axf->Final(aalg, &ctx);
		bcopy(sw->sw_octx, &ctx, axf->ctxsize);
		axf->Update(&ctx, aalg, axf->hashsize);
		axf->Final(aalg, &ctx);
		break;
	}

	/* Inject the authentication data */
	if (outtype == CRYPTO_BUF_MBUF)
		COPYBACK(outtype, buf, crd->crd_inject, axf->authsize, aalg);
	else
		bcopy(aalg, crp->crp_mac, axf->authsize);

	return 0;
}

/*
 * Apply a combined encryption-authentication transformation
 */
int
swcr_authenc(struct cryptop *crp)
{
	uint32_t blkbuf[howmany(EALG_MAX_BLOCK_LEN, sizeof(uint32_t))];
	u_char *blk = (u_char *)blkbuf;
	u_char aalg[AALG_MAX_RESULT_LEN];
	u_char iv[EALG_MAX_BLOCK_LEN];
	union authctx ctx;
	struct cryptodesc *crd, *crda = NULL, *crde = NULL;
	struct swcr_list *session;
	struct swcr_data *sw, *swa, *swe = NULL;
	const struct auth_hash *axf = NULL;
	const struct enc_xform *exf = NULL;
	caddr_t buf = (caddr_t)crp->crp_buf;
	uint32_t *blkp;
	int aadlen, blksz, i, ivlen, outtype, len, iskip, oskip;

	ivlen = blksz = iskip = oskip = 0;

	session = &swcr_sessions[crp->crp_sid & 0xffffffff];
	for (i = 0; i < crp->crp_ndesc; i++) {
		crd = &crp->crp_desc[i];
		SLIST_FOREACH(sw, session, sw_next) {
			if (sw->sw_alg == crd->crd_alg)
				break;
		}
		if (sw == NULL)
			return (EINVAL);

		switch (sw->sw_alg) {
		case CRYPTO_AES_GCM_16:
		case CRYPTO_AES_GMAC:
		case CRYPTO_CHACHA20_POLY1305:
			swe = sw;
			crde = crd;
			exf = swe->sw_exf;
			ivlen = exf->ivsize;
			break;
		case CRYPTO_AES_128_GMAC:
		case CRYPTO_AES_192_GMAC:
		case CRYPTO_AES_256_GMAC:
		case CRYPTO_CHACHA20_POLY1305_MAC:
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

	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		outtype = CRYPTO_BUF_MBUF;
	} else {
		outtype = CRYPTO_BUF_IOV;
	}

	/* Initialize the IV */
	if (crde->crd_flags & CRD_F_ENCRYPT) {
		/* IV explicitly provided ? */
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crde->crd_iv, iv, ivlen);
		else
			arc4random_buf(iv, ivlen);

		/* Do we need to write the IV */
		if (!(crde->crd_flags & CRD_F_IV_PRESENT))
			COPYBACK(outtype, buf, crde->crd_inject, ivlen, iv);

	} else {	/* Decryption */
			/* IV explicitly provided ? */
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			bcopy(crde->crd_iv, iv, ivlen);
		else {
			/* Get IV off buf */
			COPYDATA(outtype, buf, crde->crd_inject, ivlen, iv);
		}
	}

	/* Supply MAC with IV */
	if (axf->Reinit)
		axf->Reinit(&ctx, iv, ivlen);

	/* Supply MAC with AAD */
	aadlen = crda->crd_len;
	/*
	 * Section 5 of RFC 4106 specifies that AAD construction consists of
	 * {SPI, ESN, SN} whereas the real packet contains only {SPI, SN}.
	 * Unfortunately it doesn't follow a good example set in the Section
	 * 3.3.2.1 of RFC 4303 where upper part of the ESN, located in the
	 * external (to the packet) memory buffer, is processed by the hash
	 * function in the end thus allowing to retain simple programming
	 * interfaces and avoid kludges like the one below.
	 */
	if (crda->crd_flags & CRD_F_ESN) {
		aadlen += 4;
		/* SPI */
		COPYDATA(outtype, buf, crda->crd_skip, 4, blk);
		iskip = 4; /* loop below will start with an offset of 4 */
		/* ESN */
		bcopy(crda->crd_esn, blk + 4, 4);
		oskip = iskip + 4; /* offset output buffer blk by 8 */
	}
	for (i = iskip; i < crda->crd_len; i += axf->hashsize) {
		len = MIN(crda->crd_len - i, axf->hashsize - oskip);
		COPYDATA(outtype, buf, crda->crd_skip + i, len, blk + oskip);
		bzero(blk + len + oskip, axf->hashsize - len - oskip);
		axf->Update(&ctx, blk, axf->hashsize);
		oskip = 0; /* reset initial output offset */
	}

	if (exf->reinit)
		exf->reinit(swe->sw_kschedule, iv);

	/* Do encryption/decryption with MAC */
	for (i = 0; i < crde->crd_len; i += blksz) {
		len = MIN(crde->crd_len - i, blksz);
		if (len < blksz)
			bzero(blk, blksz);
		COPYDATA(outtype, buf, crde->crd_skip + i, len, blk);
		if (crde->crd_flags & CRD_F_ENCRYPT) {
			exf->encrypt(swe->sw_kschedule, blk);
			axf->Update(&ctx, blk, len);
		} else {
			axf->Update(&ctx, blk, len);
			exf->decrypt(swe->sw_kschedule, blk);
		}
		COPYBACK(outtype, buf, crde->crd_skip + i, len, blk);
	}

	/* Do any required special finalization */
	switch (crda->crd_alg) {
		case CRYPTO_AES_128_GMAC:
		case CRYPTO_AES_192_GMAC:
		case CRYPTO_AES_256_GMAC:
			/* length block */
			bzero(blk, axf->hashsize);
			blkp = (uint32_t *)blk + 1;
			*blkp = htobe32(aadlen * 8);
			blkp = (uint32_t *)blk + 3;
			*blkp = htobe32(crde->crd_len * 8);
			axf->Update(&ctx, blk, axf->hashsize);
			break;
		case CRYPTO_CHACHA20_POLY1305_MAC:
			/* length block */
			bzero(blk, axf->hashsize);
			blkp = (uint32_t *)blk;
			*blkp = htole32(aadlen);
			blkp = (uint32_t *)blk + 2;
			*blkp = htole32(crde->crd_len);
			axf->Update(&ctx, blk, axf->hashsize);
			break;
	}

	/* Finalize MAC */
	axf->Final(aalg, &ctx);

	/* Inject the authentication data */
	if (outtype == CRYPTO_BUF_MBUF)
		COPYBACK(outtype, buf, crda->crd_inject, axf->authsize, aalg);
	else
		bcopy(aalg, crp->crp_mac, axf->authsize);

	return (0);
}

/*
 * Apply a compression/decompression algorithm
 */
int
swcr_compdec(struct cryptodesc *crd, struct swcr_data *sw,
    caddr_t buf, int outtype)
{
	u_int8_t *data, *out;
	const struct comp_algo *cxf;
	int adj;
	u_int32_t result;

	cxf = sw->sw_cxf;

	/* We must handle the whole buffer of data in one time
	 * then if there is not all the data in the mbuf, we must
	 * copy in a buffer.
	 */

	data = malloc(crd->crd_len, M_CRYPTO_DATA, M_NOWAIT);
	if (data == NULL)
		return (EINVAL);
	COPYDATA(outtype, buf, crd->crd_skip, crd->crd_len, data);

	if (crd->crd_flags & CRD_F_COMP)
		result = cxf->compress(data, crd->crd_len, &out);
	else
		result = cxf->decompress(data, crd->crd_len, &out);

	free(data, M_CRYPTO_DATA, crd->crd_len);
	if (result == 0)
		return EINVAL;

	/* Copy back the (de)compressed data. m_copyback is
	 * extending the mbuf as necessary.
	 */
	sw->sw_size = result;
	/* Check the compressed size when doing compression */
	if (crd->crd_flags & CRD_F_COMP) {
		if (result > crd->crd_len) {
			/* Compression was useless, we lost time */
			free(out, M_CRYPTO_DATA, result);
			return 0;
		}
	}

	COPYBACK(outtype, buf, crd->crd_skip, result, out);
	if (result < crd->crd_len) {
		adj = result - crd->crd_len;
		if (outtype == CRYPTO_BUF_MBUF) {
			adj = result - crd->crd_len;
			m_adj((struct mbuf *)buf, adj);
		} else {
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
	free(out, M_CRYPTO_DATA, result);
	return 0;
}

/*
 * Generate a new software session.
 */
int
swcr_newsession(u_int32_t *sid, struct cryptoini *cri)
{
	struct swcr_list *session;
	struct swcr_data *swd, *prev;
	const struct auth_hash *axf;
	const struct enc_xform *txf;
	const struct comp_algo *cxf;
	u_int32_t i;
	int k;

	if (sid == NULL || cri == NULL)
		return EINVAL;

	if (swcr_sessions != NULL) {
		for (i = 1; i < swcr_sesnum; i++)
			if (SLIST_EMPTY(&swcr_sessions[i]))
				break;
	}

	if (swcr_sessions == NULL || i == swcr_sesnum) {
		if (swcr_sessions == NULL) {
			i = 1; /* We leave swcr_sessions[0] empty */
			swcr_sesnum = CRYPTO_SW_SESSIONS;
		} else
			swcr_sesnum *= 2;

		session = mallocarray(swcr_sesnum, sizeof(struct swcr_list),
		    M_CRYPTO_DATA, M_NOWAIT | M_ZERO);
		if (session == NULL) {
			/* Reset session number */
			if (swcr_sesnum == CRYPTO_SW_SESSIONS)
				swcr_sesnum = 0;
			else
				swcr_sesnum /= 2;
			return ENOBUFS;
		}

		/* Copy existing sessions */
		if (swcr_sessions) {
			bcopy(swcr_sessions, session,
			    (swcr_sesnum / 2) * sizeof(struct swcr_list));
			free(swcr_sessions, M_CRYPTO_DATA,
			    (swcr_sesnum / 2) * sizeof(struct swcr_list));
		}

		swcr_sessions = session;
	}

	session = &swcr_sessions[i];
	*sid = i;
	prev = NULL;

	while (cri) {
		swd = malloc(sizeof(struct swcr_data), M_CRYPTO_DATA,
		    M_NOWAIT | M_ZERO);
		if (swd == NULL) {
			swcr_freesession(i);
			return ENOBUFS;
		}
		if (prev == NULL)
			SLIST_INSERT_HEAD(session, swd, sw_next);
		else
			SLIST_INSERT_AFTER(prev, swd, sw_next);

		switch (cri->cri_alg) {
		case CRYPTO_3DES_CBC:
			txf = &enc_xform_3des;
			goto enccommon;
		case CRYPTO_BLF_CBC:
			txf = &enc_xform_blf;
			goto enccommon;
		case CRYPTO_CAST_CBC:
			txf = &enc_xform_cast5;
			goto enccommon;
		case CRYPTO_AES_CBC:
			txf = &enc_xform_aes;
			goto enccommon;
		case CRYPTO_AES_CTR:
			txf = &enc_xform_aes_ctr;
			goto enccommon;
		case CRYPTO_AES_XTS:
			txf = &enc_xform_aes_xts;
			goto enccommon;
		case CRYPTO_AES_GCM_16:
			txf = &enc_xform_aes_gcm;
			goto enccommon;
		case CRYPTO_AES_GMAC:
			txf = &enc_xform_aes_gmac;
			swd->sw_exf = txf;
			break;
		case CRYPTO_CHACHA20_POLY1305:
			txf = &enc_xform_chacha20_poly1305;
			goto enccommon;
		case CRYPTO_NULL:
			txf = &enc_xform_null;
			goto enccommon;
		enccommon:
			if (txf->ctxsize > 0) {
				swd->sw_kschedule = malloc(txf->ctxsize,
				    M_CRYPTO_DATA, M_NOWAIT | M_ZERO);
				if (swd->sw_kschedule == NULL) {
					swcr_freesession(i);
					return EINVAL;
				}
			}
			if (txf->setkey(swd->sw_kschedule, cri->cri_key,
			    cri->cri_klen / 8) < 0) {
				swcr_freesession(i);
				return EINVAL;
			}
			swd->sw_exf = txf;
			break;

		case CRYPTO_MD5_HMAC:
			axf = &auth_hash_hmac_md5_96;
			goto authcommon;
		case CRYPTO_SHA1_HMAC:
			axf = &auth_hash_hmac_sha1_96;
			goto authcommon;
		case CRYPTO_RIPEMD160_HMAC:
			axf = &auth_hash_hmac_ripemd_160_96;
			goto authcommon;
		case CRYPTO_SHA2_256_HMAC:
			axf = &auth_hash_hmac_sha2_256_128;
			goto authcommon;
		case CRYPTO_SHA2_384_HMAC:
			axf = &auth_hash_hmac_sha2_384_192;
			goto authcommon;
		case CRYPTO_SHA2_512_HMAC:
			axf = &auth_hash_hmac_sha2_512_256;
			goto authcommon;
		authcommon:
			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				swcr_freesession(i);
				return ENOBUFS;
			}

			swd->sw_octx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_octx == NULL) {
				swcr_freesession(i);
				return ENOBUFS;
			}

			for (k = 0; k < cri->cri_klen / 8; k++)
				cri->cri_key[k] ^= HMAC_IPAD_VAL;

			axf->Init(swd->sw_ictx);
			axf->Update(swd->sw_ictx, cri->cri_key,
			    cri->cri_klen / 8);
			axf->Update(swd->sw_ictx, hmac_ipad_buffer,
			    axf->blocksize - (cri->cri_klen / 8));

			for (k = 0; k < cri->cri_klen / 8; k++)
				cri->cri_key[k] ^= (HMAC_IPAD_VAL ^ HMAC_OPAD_VAL);

			axf->Init(swd->sw_octx);
			axf->Update(swd->sw_octx, cri->cri_key,
			    cri->cri_klen / 8);
			axf->Update(swd->sw_octx, hmac_opad_buffer,
			    axf->blocksize - (cri->cri_klen / 8));

			for (k = 0; k < cri->cri_klen / 8; k++)
				cri->cri_key[k] ^= HMAC_OPAD_VAL;
			swd->sw_axf = axf;
			break;

		case CRYPTO_AES_128_GMAC:
			axf = &auth_hash_gmac_aes_128;
			goto authenccommon;
		case CRYPTO_AES_192_GMAC:
			axf = &auth_hash_gmac_aes_192;
			goto authenccommon;
		case CRYPTO_AES_256_GMAC:
			axf = &auth_hash_gmac_aes_256;
			goto authenccommon;
		case CRYPTO_CHACHA20_POLY1305_MAC:
			axf = &auth_hash_chacha20_poly1305;
			goto authenccommon;
		authenccommon:
			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				swcr_freesession(i);
				return ENOBUFS;
			}
			axf->Init(swd->sw_ictx);
			axf->Setkey(swd->sw_ictx, cri->cri_key,
			    cri->cri_klen / 8);
			swd->sw_axf = axf;
			break;

		case CRYPTO_DEFLATE_COMP:
			cxf = &comp_algo_deflate;
			swd->sw_cxf = cxf;
			break;
		case CRYPTO_ESN:
			/* nothing to do */
			break;
		default:
			swcr_freesession(i);
			return EINVAL;
		}

		swd->sw_alg = cri->cri_alg;
		cri = cri->cri_next;
		prev = swd;
	}
	return 0;
}

/*
 * Free a session.
 */
int
swcr_freesession(u_int64_t tid)
{
	struct swcr_list *session;
	struct swcr_data *swd;
	const struct enc_xform *txf;
	const struct auth_hash *axf;
	u_int32_t sid = ((u_int32_t) tid) & 0xffffffff;

	if (sid > swcr_sesnum || swcr_sessions == NULL ||
	    SLIST_EMPTY(&swcr_sessions[sid]))
		return EINVAL;

	/* Silently accept and return */
	if (sid == 0)
		return 0;

	session = &swcr_sessions[sid];
	while (!SLIST_EMPTY(session)) {
		swd = SLIST_FIRST(session);
		SLIST_REMOVE_HEAD(session, sw_next);

		switch (swd->sw_alg) {
		case CRYPTO_3DES_CBC:
		case CRYPTO_BLF_CBC:
		case CRYPTO_CAST_CBC:
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_CTR:
		case CRYPTO_AES_XTS:
		case CRYPTO_AES_GCM_16:
		case CRYPTO_AES_GMAC:
		case CRYPTO_CHACHA20_POLY1305:
		case CRYPTO_NULL:
			txf = swd->sw_exf;

			if (swd->sw_kschedule) {
				explicit_bzero(swd->sw_kschedule, txf->ctxsize);
				free(swd->sw_kschedule, M_CRYPTO_DATA,
				    txf->ctxsize);
			}
			break;

		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			axf = swd->sw_axf;

			if (swd->sw_ictx) {
				explicit_bzero(swd->sw_ictx, axf->ctxsize);
				free(swd->sw_ictx, M_CRYPTO_DATA, axf->ctxsize);
			}
			if (swd->sw_octx) {
				explicit_bzero(swd->sw_octx, axf->ctxsize);
				free(swd->sw_octx, M_CRYPTO_DATA, axf->ctxsize);
			}
			break;

		case CRYPTO_AES_128_GMAC:
		case CRYPTO_AES_192_GMAC:
		case CRYPTO_AES_256_GMAC:
		case CRYPTO_CHACHA20_POLY1305_MAC:
			axf = swd->sw_axf;

			if (swd->sw_ictx) {
				explicit_bzero(swd->sw_ictx, axf->ctxsize);
				free(swd->sw_ictx, M_CRYPTO_DATA, axf->ctxsize);
			}
			break;
		}

		free(swd, M_CRYPTO_DATA, sizeof(*swd));
	}
	return 0;
}

/*
 * Process a software request.
 */
int
swcr_process(struct cryptop *crp)
{
	struct cryptodesc *crd;
	struct swcr_list *session;
	struct swcr_data *sw;
	u_int32_t lid;
	int err = 0;
	int type;
	int i;

	KASSERT(crp->crp_ndesc >= 1);

	if (crp->crp_buf == NULL) {
		err = EINVAL;
		goto done;
	}

	lid = crp->crp_sid & 0xffffffff;
	if (lid >= swcr_sesnum || lid == 0 ||
	    SLIST_EMPTY(&swcr_sessions[lid])) {
		err = ENOENT;
		goto done;
	}

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		type = CRYPTO_BUF_MBUF;
	else
		type = CRYPTO_BUF_IOV;

	/* Go through crypto descriptors, processing as we go */
	session = &swcr_sessions[lid];
	for (i = 0; i < crp->crp_ndesc; i++) {
		crd = &crp->crp_desc[i];
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
		SLIST_FOREACH(sw, session, sw_next) {
			if (sw->sw_alg == crd->crd_alg)
				break;
		}

		/* No such context ? */
		if (sw == NULL) {
			err = EINVAL;
			goto done;
		}

		switch (sw->sw_alg) {
		case CRYPTO_NULL:
			break;
		case CRYPTO_3DES_CBC:
		case CRYPTO_BLF_CBC:
		case CRYPTO_CAST_CBC:
		case CRYPTO_RIJNDAEL128_CBC:
		case CRYPTO_AES_CTR:
		case CRYPTO_AES_XTS:
			if ((err = swcr_encdec(crd, sw,
			    crp->crp_buf, type)) != 0)
				goto done;
			break;
		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			if ((err = swcr_authcompute(crp, crd, sw,
			    crp->crp_buf, type)) != 0)
				goto done;
			break;

		case CRYPTO_AES_GCM_16:
		case CRYPTO_AES_GMAC:
		case CRYPTO_AES_128_GMAC:
		case CRYPTO_AES_192_GMAC:
		case CRYPTO_AES_256_GMAC:
		case CRYPTO_CHACHA20_POLY1305:
		case CRYPTO_CHACHA20_POLY1305_MAC:
			err = swcr_authenc(crp);
			goto done;

		case CRYPTO_DEFLATE_COMP:
			if ((err = swcr_compdec(crd, sw,
			    crp->crp_buf, type)) != 0)
				goto done;
			else
				crp->crp_olen = (int)sw->sw_size;
			break;

		default:
			/* Unknown/unsupported algorithm */
			err = EINVAL;
			goto done;
		}
	}

done:
	return err;
}

/*
 * Initialize the driver, called from the kernel main().
 */
void
swcr_init(void)
{
	int algs[CRYPTO_ALGORITHM_MAX + 1];
	int flags = CRYPTOCAP_F_SOFTWARE;

	swcr_id = crypto_get_driverid(flags);
	if (swcr_id < 0) {
		/* This should never happen */
		panic("Software crypto device cannot initialize!");
	}

	bzero(algs, sizeof(algs));

	algs[CRYPTO_3DES_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_BLF_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_CAST_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_MD5_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA1_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_RIPEMD160_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_CTR] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_XTS] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_GCM_16] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_DEFLATE_COMP] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_NULL] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_256_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_384_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_512_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_128_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_192_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_256_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_CHACHA20_POLY1305] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_CHACHA20_POLY1305_MAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_ESN] = CRYPTO_ALG_FLAG_SUPPORTED;

	crypto_register(swcr_id, algs, swcr_newsession,
	    swcr_freesession, swcr_process);
}
