/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Chelsio Communications, Inc.
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
 * All rights reserved.
 * Largely borrowed from ccr(4), Written by: John Baldwin <jhb@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "opt_ddb.h"

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/module.h>
#include <sys/random.h>
#include <sys/sglist.h>
#include <sys/sysctl.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <dev/pci/pcivar.h>

#include <dev/random/randomdev.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/xform.h>

#include "cryptodev_if.h"

#include "ccp.h"
#include "ccp_hardware.h"

MALLOC_DEFINE(M_CCP, "ccp", "AMD CCP crypto");

/*
 * Need a global softc available for garbage random_source API, which lacks any
 * context pointer.  It's also handy for debugging.
 */
struct ccp_softc *g_ccp_softc;

bool g_debug_print = false;
SYSCTL_BOOL(_hw_ccp, OID_AUTO, debug, CTLFLAG_RWTUN, &g_debug_print, 0,
    "Set to enable debugging log messages");

static struct pciid {
	uint32_t devid;
	const char *desc;
} ccp_ids[] = {
	{ 0x14561022, "AMD CCP-5a" },
	{ 0x14681022, "AMD CCP-5b" },
};

static struct random_source random_ccp = {
	.rs_ident = "AMD CCP TRNG",
	.rs_source = RANDOM_PURE_CCP,
	.rs_read = random_ccp_read,
};

/*
 * ccp_populate_sglist() generates a scatter/gather list that covers the entire
 * crypto operation buffer.
 */
static int
ccp_populate_sglist(struct sglist *sg, struct cryptop *crp)
{
	int error;

	sglist_reset(sg);
	if (crp->crp_flags & CRYPTO_F_IMBUF)
		error = sglist_append_mbuf(sg, crp->crp_mbuf);
	else if (crp->crp_flags & CRYPTO_F_IOV)
		error = sglist_append_uio(sg, crp->crp_uio);
	else
		error = sglist_append(sg, crp->crp_buf, crp->crp_ilen);
	return (error);
}

/*
 * Handle a GCM request with an empty payload by performing the
 * operation in software.  Derived from swcr_authenc().
 */
static void
ccp_gcm_soft(struct ccp_session *s, struct cryptop *crp,
    struct cryptodesc *crda, struct cryptodesc *crde)
{
	struct aes_gmac_ctx gmac_ctx;
	char block[GMAC_BLOCK_LEN];
	char digest[GMAC_DIGEST_LEN];
	char iv[AES_BLOCK_LEN];
	int i, len;

	/*
	 * This assumes a 12-byte IV from the crp.  See longer comment
	 * above in ccp_gcm() for more details.
	 */
	if (crde->crd_flags & CRD_F_ENCRYPT) {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crde->crd_iv, 12);
		else
			arc4rand(iv, 12, 0);
		if ((crde->crd_flags & CRD_F_IV_PRESENT) == 0)
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    crde->crd_inject, 12, iv);
	} else {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crde->crd_iv, 12);
		else
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    crde->crd_inject, 12, iv);
	}
	*(uint32_t *)&iv[12] = htobe32(1);

	/* Initialize the MAC. */
	AES_GMAC_Init(&gmac_ctx);
	AES_GMAC_Setkey(&gmac_ctx, s->blkcipher.enckey, s->blkcipher.key_len);
	AES_GMAC_Reinit(&gmac_ctx, iv, sizeof(iv));

	/* MAC the AAD. */
	for (i = 0; i < crda->crd_len; i += sizeof(block)) {
		len = imin(crda->crd_len - i, sizeof(block));
		crypto_copydata(crp->crp_flags, crp->crp_buf, crda->crd_skip +
		    i, len, block);
		bzero(block + len, sizeof(block) - len);
		AES_GMAC_Update(&gmac_ctx, block, sizeof(block));
	}

	/* Length block. */
	bzero(block, sizeof(block));
	((uint32_t *)block)[1] = htobe32(crda->crd_len * 8);
	AES_GMAC_Update(&gmac_ctx, block, sizeof(block));
	AES_GMAC_Final(digest, &gmac_ctx);

	if (crde->crd_flags & CRD_F_ENCRYPT) {
		crypto_copyback(crp->crp_flags, crp->crp_buf, crda->crd_inject,
		    sizeof(digest), digest);
		crp->crp_etype = 0;
	} else {
		char digest2[GMAC_DIGEST_LEN];

		crypto_copydata(crp->crp_flags, crp->crp_buf, crda->crd_inject,
		    sizeof(digest2), digest2);
		if (timingsafe_bcmp(digest, digest2, sizeof(digest)) == 0)
			crp->crp_etype = 0;
		else
			crp->crp_etype = EBADMSG;
	}
	crypto_done(crp);
}

static int
ccp_probe(device_t dev)
{
	struct pciid *ip;
	uint32_t id;

	id = pci_get_devid(dev);
	for (ip = ccp_ids; ip < &ccp_ids[nitems(ccp_ids)]; ip++) {
		if (id == ip->devid) {
			device_set_desc(dev, ip->desc);
			return (0);
		}
	}
	return (ENXIO);
}

static void
ccp_initialize_queues(struct ccp_softc *sc)
{
	struct ccp_queue *qp;
	size_t i;

	for (i = 0; i < nitems(sc->queues); i++) {
		qp = &sc->queues[i];

		qp->cq_softc = sc;
		qp->cq_qindex = i;
		mtx_init(&qp->cq_lock, "ccp queue", NULL, MTX_DEF);
		/* XXX - arbitrarily chosen sizes */
		qp->cq_sg_crp = sglist_alloc(32, M_WAITOK);
		/* Two more SGEs than sg_crp to accommodate ipad. */
		qp->cq_sg_ulptx = sglist_alloc(34, M_WAITOK);
		qp->cq_sg_dst = sglist_alloc(2, M_WAITOK);
	}
}

static void
ccp_free_queues(struct ccp_softc *sc)
{
	struct ccp_queue *qp;
	size_t i;

	for (i = 0; i < nitems(sc->queues); i++) {
		qp = &sc->queues[i];

		mtx_destroy(&qp->cq_lock);
		sglist_free(qp->cq_sg_crp);
		sglist_free(qp->cq_sg_ulptx);
		sglist_free(qp->cq_sg_dst);
	}
}

static int
ccp_attach(device_t dev)
{
	struct ccp_softc *sc;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	sc->cid = crypto_get_driverid(dev, sizeof(struct ccp_session),
	    CRYPTOCAP_F_HARDWARE);
	if (sc->cid < 0) {
		device_printf(dev, "could not get crypto driver id\n");
		return (ENXIO);
	}

	error = ccp_hw_attach(dev);
	if (error != 0)
		return (error);

	mtx_init(&sc->lock, "ccp", NULL, MTX_DEF);

	ccp_initialize_queues(sc);

	if (g_ccp_softc == NULL) {
		g_ccp_softc = sc;
		if ((sc->hw_features & VERSION_CAP_TRNG) != 0)
			random_source_register(&random_ccp);
	}

	if ((sc->hw_features & VERSION_CAP_AES) != 0) {
		crypto_register(sc->cid, CRYPTO_AES_CBC, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_ICM, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_NIST_GCM_16, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_128_NIST_GMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_192_NIST_GMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_256_NIST_GMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_XTS, 0, 0);
	}
	if ((sc->hw_features & VERSION_CAP_SHA) != 0) {
		crypto_register(sc->cid, CRYPTO_SHA1_HMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_SHA2_256_HMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_SHA2_384_HMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_SHA2_512_HMAC, 0, 0);
	}

	return (0);
}

static int
ccp_detach(device_t dev)
{
	struct ccp_softc *sc;

	sc = device_get_softc(dev);

	mtx_lock(&sc->lock);
	sc->detaching = true;
	mtx_unlock(&sc->lock);

	crypto_unregister_all(sc->cid);
	if (g_ccp_softc == sc && (sc->hw_features & VERSION_CAP_TRNG) != 0)
		random_source_deregister(&random_ccp);

	ccp_hw_detach(dev);
	ccp_free_queues(sc);

	if (g_ccp_softc == sc)
		g_ccp_softc = NULL;

	mtx_destroy(&sc->lock);
	return (0);
}

static void
ccp_init_hmac_digest(struct ccp_session *s, int cri_alg, char *key,
    int klen)
{
	union authctx auth_ctx;
	struct auth_hash *axf;
	u_int i;

	/*
	 * If the key is larger than the block size, use the digest of
	 * the key as the key instead.
	 */
	axf = s->hmac.auth_hash;
	klen /= 8;
	if (klen > axf->blocksize) {
		axf->Init(&auth_ctx);
		axf->Update(&auth_ctx, key, klen);
		axf->Final(s->hmac.ipad, &auth_ctx);
		explicit_bzero(&auth_ctx, sizeof(auth_ctx));
		klen = axf->hashsize;
	} else
		memcpy(s->hmac.ipad, key, klen);

	memset(s->hmac.ipad + klen, 0, axf->blocksize - klen);
	memcpy(s->hmac.opad, s->hmac.ipad, axf->blocksize);

	for (i = 0; i < axf->blocksize; i++) {
		s->hmac.ipad[i] ^= HMAC_IPAD_VAL;
		s->hmac.opad[i] ^= HMAC_OPAD_VAL;
	}
}

static int
ccp_aes_check_keylen(int alg, int klen)
{

	switch (klen) {
	case 128:
	case 192:
		if (alg == CRYPTO_AES_XTS)
			return (EINVAL);
		break;
	case 256:
		break;
	case 512:
		if (alg != CRYPTO_AES_XTS)
			return (EINVAL);
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

static void
ccp_aes_setkey(struct ccp_session *s, int alg, const void *key, int klen)
{
	unsigned kbits;

	if (alg == CRYPTO_AES_XTS)
		kbits = klen / 2;
	else
		kbits = klen;

	switch (kbits) {
	case 128:
		s->blkcipher.cipher_type = CCP_AES_TYPE_128;
		break;
	case 192:
		s->blkcipher.cipher_type = CCP_AES_TYPE_192;
		break;
	case 256:
		s->blkcipher.cipher_type = CCP_AES_TYPE_256;
		break;
	default:
		panic("should not get here");
	}

	s->blkcipher.key_len = klen / 8;
	memcpy(s->blkcipher.enckey, key, s->blkcipher.key_len);
}

static int
ccp_newsession(device_t dev, crypto_session_t cses, struct cryptoini *cri)
{
	struct ccp_softc *sc;
	struct ccp_session *s;
	struct auth_hash *auth_hash;
	struct cryptoini *c, *hash, *cipher;
	enum ccp_aes_mode cipher_mode;
	unsigned auth_mode, iv_len;
	unsigned partial_digest_len;
	unsigned q;
	int error;
	bool gcm_hash;

	if (cri == NULL)
		return (EINVAL);

	s = crypto_get_driver_session(cses);

	gcm_hash = false;
	cipher = NULL;
	hash = NULL;
	auth_hash = NULL;
	/* XXX reconcile auth_mode with use by ccp_sha */
	auth_mode = 0;
	cipher_mode = CCP_AES_MODE_ECB;
	iv_len = 0;
	partial_digest_len = 0;
	for (c = cri; c != NULL; c = c->cri_next) {
		switch (c->cri_alg) {
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
		case CRYPTO_AES_128_NIST_GMAC:
		case CRYPTO_AES_192_NIST_GMAC:
		case CRYPTO_AES_256_NIST_GMAC:
			if (hash)
				return (EINVAL);
			hash = c;
			switch (c->cri_alg) {
			case CRYPTO_SHA1_HMAC:
				auth_hash = &auth_hash_hmac_sha1;
				auth_mode = SHA1;
				partial_digest_len = SHA1_HASH_LEN;
				break;
			case CRYPTO_SHA2_256_HMAC:
				auth_hash = &auth_hash_hmac_sha2_256;
				auth_mode = SHA2_256;
				partial_digest_len = SHA2_256_HASH_LEN;
				break;
			case CRYPTO_SHA2_384_HMAC:
				auth_hash = &auth_hash_hmac_sha2_384;
				auth_mode = SHA2_384;
				partial_digest_len = SHA2_512_HASH_LEN;
				break;
			case CRYPTO_SHA2_512_HMAC:
				auth_hash = &auth_hash_hmac_sha2_512;
				auth_mode = SHA2_512;
				partial_digest_len = SHA2_512_HASH_LEN;
				break;
			case CRYPTO_AES_128_NIST_GMAC:
			case CRYPTO_AES_192_NIST_GMAC:
			case CRYPTO_AES_256_NIST_GMAC:
				gcm_hash = true;
#if 0
				auth_mode = CHCR_SCMD_AUTH_MODE_GHASH;
#endif
				break;
			}
			break;
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_ICM:
		case CRYPTO_AES_NIST_GCM_16:
		case CRYPTO_AES_XTS:
			if (cipher)
				return (EINVAL);
			cipher = c;
			switch (c->cri_alg) {
			case CRYPTO_AES_CBC:
				cipher_mode = CCP_AES_MODE_CBC;
				iv_len = AES_BLOCK_LEN;
				break;
			case CRYPTO_AES_ICM:
				cipher_mode = CCP_AES_MODE_CTR;
				iv_len = AES_BLOCK_LEN;
				break;
			case CRYPTO_AES_NIST_GCM_16:
				cipher_mode = CCP_AES_MODE_GCTR;
				iv_len = AES_GCM_IV_LEN;
				break;
			case CRYPTO_AES_XTS:
				cipher_mode = CCP_AES_MODE_XTS;
				iv_len = AES_BLOCK_LEN;
				break;
			}
			if (c->cri_key != NULL) {
				error = ccp_aes_check_keylen(c->cri_alg,
				    c->cri_klen);
				if (error != 0)
					return (error);
			}
			break;
		default:
			return (EINVAL);
		}
	}
	if (gcm_hash != (cipher_mode == CCP_AES_MODE_GCTR))
		return (EINVAL);
	if (hash == NULL && cipher == NULL)
		return (EINVAL);
	if (hash != NULL && hash->cri_key == NULL)
		return (EINVAL);

	sc = device_get_softc(dev);
	mtx_lock(&sc->lock);
	if (sc->detaching) {
		mtx_unlock(&sc->lock);
		return (ENXIO);
	}

	/* Just grab the first usable queue for now. */
	for (q = 0; q < nitems(sc->queues); q++)
		if ((sc->valid_queues & (1 << q)) != 0)
			break;
	if (q == nitems(sc->queues)) {
		mtx_unlock(&sc->lock);
		return (ENXIO);
	}
	s->queue = q;

	if (gcm_hash)
		s->mode = GCM;
	else if (hash != NULL && cipher != NULL)
		s->mode = AUTHENC;
	else if (hash != NULL)
		s->mode = HMAC;
	else {
		MPASS(cipher != NULL);
		s->mode = BLKCIPHER;
	}
	if (gcm_hash) {
		if (hash->cri_mlen == 0)
			s->gmac.hash_len = AES_GMAC_HASH_LEN;
		else
			s->gmac.hash_len = hash->cri_mlen;
	} else if (hash != NULL) {
		s->hmac.auth_hash = auth_hash;
		s->hmac.auth_mode = auth_mode;
		s->hmac.partial_digest_len = partial_digest_len;
		if (hash->cri_mlen == 0)
			s->hmac.hash_len = auth_hash->hashsize;
		else
			s->hmac.hash_len = hash->cri_mlen;
		ccp_init_hmac_digest(s, hash->cri_alg, hash->cri_key,
		    hash->cri_klen);
	}
	if (cipher != NULL) {
		s->blkcipher.cipher_mode = cipher_mode;
		s->blkcipher.iv_len = iv_len;
		if (cipher->cri_key != NULL)
			ccp_aes_setkey(s, cipher->cri_alg, cipher->cri_key,
			    cipher->cri_klen);
	}

	s->active = true;
	mtx_unlock(&sc->lock);

	return (0);
}

static void
ccp_freesession(device_t dev, crypto_session_t cses)
{
	struct ccp_session *s;

	s = crypto_get_driver_session(cses);

	if (s->pending != 0)
		device_printf(dev,
		    "session %p freed with %d pending requests\n", s,
		    s->pending);
	s->active = false;
}

static int
ccp_process(device_t dev, struct cryptop *crp, int hint)
{
	struct ccp_softc *sc;
	struct ccp_queue *qp;
	struct ccp_session *s;
	struct cryptodesc *crd, *crda, *crde;
	int error;
	bool qpheld;

	qpheld = false;
	qp = NULL;
	if (crp == NULL)
		return (EINVAL);

	crd = crp->crp_desc;
	s = crypto_get_driver_session(crp->crp_session);
	sc = device_get_softc(dev);
	mtx_lock(&sc->lock);
	qp = &sc->queues[s->queue];
	mtx_unlock(&sc->lock);
	error = ccp_queue_acquire_reserve(qp, 1 /* placeholder */, M_NOWAIT);
	if (error != 0)
		goto out;
	qpheld = true;

	error = ccp_populate_sglist(qp->cq_sg_crp, crp);
	if (error != 0)
		goto out;

	switch (s->mode) {
	case HMAC:
		if (crd->crd_flags & CRD_F_KEY_EXPLICIT)
			ccp_init_hmac_digest(s, crd->crd_alg, crd->crd_key,
			    crd->crd_klen);
		error = ccp_hmac(qp, s, crp);
		break;
	case BLKCIPHER:
		if (crd->crd_flags & CRD_F_KEY_EXPLICIT) {
			error = ccp_aes_check_keylen(crd->crd_alg,
			    crd->crd_klen);
			if (error != 0)
				break;
			ccp_aes_setkey(s, crd->crd_alg, crd->crd_key,
			    crd->crd_klen);
		}
		error = ccp_blkcipher(qp, s, crp);
		break;
	case AUTHENC:
		error = 0;
		switch (crd->crd_alg) {
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_ICM:
		case CRYPTO_AES_XTS:
			/* Only encrypt-then-authenticate supported. */
			crde = crd;
			crda = crd->crd_next;
			if (!(crde->crd_flags & CRD_F_ENCRYPT)) {
				error = EINVAL;
				break;
			}
			s->cipher_first = true;
			break;
		default:
			crda = crd;
			crde = crd->crd_next;
			if (crde->crd_flags & CRD_F_ENCRYPT) {
				error = EINVAL;
				break;
			}
			s->cipher_first = false;
			break;
		}
		if (error != 0)
			break;
		if (crda->crd_flags & CRD_F_KEY_EXPLICIT)
			ccp_init_hmac_digest(s, crda->crd_alg, crda->crd_key,
			    crda->crd_klen);
		if (crde->crd_flags & CRD_F_KEY_EXPLICIT) {
			error = ccp_aes_check_keylen(crde->crd_alg,
			    crde->crd_klen);
			if (error != 0)
				break;
			ccp_aes_setkey(s, crde->crd_alg, crde->crd_key,
			    crde->crd_klen);
		}
		error = ccp_authenc(qp, s, crp, crda, crde);
		break;
	case GCM:
		error = 0;
		if (crd->crd_alg == CRYPTO_AES_NIST_GCM_16) {
			crde = crd;
			crda = crd->crd_next;
			s->cipher_first = true;
		} else {
			crda = crd;
			crde = crd->crd_next;
			s->cipher_first = false;
		}
		if (crde->crd_flags & CRD_F_KEY_EXPLICIT) {
			error = ccp_aes_check_keylen(crde->crd_alg,
			    crde->crd_klen);
			if (error != 0)
				break;
			ccp_aes_setkey(s, crde->crd_alg, crde->crd_key,
			    crde->crd_klen);
		}
		if (crde->crd_len == 0) {
			mtx_unlock(&qp->cq_lock);
			ccp_gcm_soft(s, crp, crda, crde);
			return (0);
		}
		error = ccp_gcm(qp, s, crp, crda, crde);
		break;
	}

	if (error == 0)
		s->pending++;

out:
	if (qpheld) {
		if (error != 0) {
			/*
			 * Squash EAGAIN so callers don't uselessly and
			 * expensively retry if the ring was full.
			 */
			if (error == EAGAIN)
				error = ENOMEM;
			ccp_queue_abort(qp);
		} else
			ccp_queue_release(qp);
	}

	if (error != 0) {
		DPRINTF(dev, "%s: early error:%d\n", __func__, error);
		crp->crp_etype = error;
		crypto_done(crp);
	}
	return (0);
}

static device_method_t ccp_methods[] = {
	DEVMETHOD(device_probe,		ccp_probe),
	DEVMETHOD(device_attach,	ccp_attach),
	DEVMETHOD(device_detach,	ccp_detach),

	DEVMETHOD(cryptodev_newsession,	ccp_newsession),
	DEVMETHOD(cryptodev_freesession, ccp_freesession),
	DEVMETHOD(cryptodev_process,	ccp_process),

	DEVMETHOD_END
};

static driver_t ccp_driver = {
	"ccp",
	ccp_methods,
	sizeof(struct ccp_softc)
};

static devclass_t ccp_devclass;
DRIVER_MODULE(ccp, pci, ccp_driver, ccp_devclass, NULL, NULL);
MODULE_VERSION(ccp, 1);
MODULE_DEPEND(ccp, crypto, 1, 1, 1);
MODULE_DEPEND(ccp, random_device, 1, 1, 1);
#if 0	/* There are enough known issues that we shouldn't load automatically */
MODULE_PNP_INFO("W32:vendor/device", pci, ccp, ccp_ids,
    nitems(ccp_ids));
#endif

static int
ccp_queue_reserve_space(struct ccp_queue *qp, unsigned n, int mflags)
{
	struct ccp_softc *sc;

	mtx_assert(&qp->cq_lock, MA_OWNED);
	sc = qp->cq_softc;

	if (n < 1 || n >= (1 << sc->ring_size_order))
		return (EINVAL);

	while (true) {
		if (ccp_queue_get_ring_space(qp) >= n)
			return (0);
		if ((mflags & M_WAITOK) == 0)
			return (EAGAIN);
		qp->cq_waiting = true;
		msleep(&qp->cq_tail, &qp->cq_lock, 0, "ccpqfull", 0);
	}
}

int
ccp_queue_acquire_reserve(struct ccp_queue *qp, unsigned n, int mflags)
{
	int error;

	mtx_lock(&qp->cq_lock);
	qp->cq_acq_tail = qp->cq_tail;
	error = ccp_queue_reserve_space(qp, n, mflags);
	if (error != 0)
		mtx_unlock(&qp->cq_lock);
	return (error);
}

void
ccp_queue_release(struct ccp_queue *qp)
{

	mtx_assert(&qp->cq_lock, MA_OWNED);
	if (qp->cq_tail != qp->cq_acq_tail) {
		wmb();
		ccp_queue_write_tail(qp);
	}
	mtx_unlock(&qp->cq_lock);
}

void
ccp_queue_abort(struct ccp_queue *qp)
{
	unsigned i;

	mtx_assert(&qp->cq_lock, MA_OWNED);

	/* Wipe out any descriptors associated with this aborted txn. */
	for (i = qp->cq_acq_tail; i != qp->cq_tail;
	    i = (i + 1) % (1 << qp->cq_softc->ring_size_order)) {
		memset(&qp->desc_ring[i], 0, sizeof(qp->desc_ring[i]));
	}
	qp->cq_tail = qp->cq_acq_tail;

	mtx_unlock(&qp->cq_lock);
}

#ifdef DDB
#define	_db_show_lock(lo)	LOCK_CLASS(lo)->lc_ddb_show(lo)
#define	db_show_lock(lk)	_db_show_lock(&(lk)->lock_object)
static void
db_show_ccp_sc(struct ccp_softc *sc)
{

	db_printf("ccp softc at %p\n", sc);
	db_printf(" cid: %d\n", (int)sc->cid);

	db_printf(" lock: ");
	db_show_lock(&sc->lock);

	db_printf(" detaching: %d\n", (int)sc->detaching);
	db_printf(" ring_size_order: %u\n", sc->ring_size_order);

	db_printf(" hw_version: %d\n", (int)sc->hw_version);
	db_printf(" hw_features: %b\n", (int)sc->hw_features,
	    "\20\24ELFC\23TRNG\22Zip_Compress\16Zip_Decompress\13ECC\12RSA"
	    "\11SHA\0103DES\07AES");

	db_printf(" hw status:\n");
	db_ccp_show_hw(sc);
}

static void
db_show_ccp_qp(struct ccp_queue *qp)
{

	db_printf(" lock: ");
	db_show_lock(&qp->cq_lock);

	db_printf(" cq_qindex: %u\n", qp->cq_qindex);
	db_printf(" cq_softc: %p\n", qp->cq_softc);

	db_printf(" head: %u\n", qp->cq_head);
	db_printf(" tail: %u\n", qp->cq_tail);
	db_printf(" acq_tail: %u\n", qp->cq_acq_tail);
	db_printf(" desc_ring: %p\n", qp->desc_ring);
	db_printf(" completions_ring: %p\n", qp->completions_ring);
	db_printf(" descriptors (phys): 0x%jx\n",
	    (uintmax_t)qp->desc_ring_bus_addr);

	db_printf(" hw status:\n");
	db_ccp_show_queue_hw(qp);
}

DB_SHOW_COMMAND(ccp, db_show_ccp)
{
	struct ccp_softc *sc;
	unsigned unit, qindex;

	if (!have_addr)
		goto usage;

	unit = (unsigned)addr;

	sc = devclass_get_softc(ccp_devclass, unit);
	if (sc == NULL) {
		db_printf("No such device ccp%u\n", unit);
		goto usage;
	}

	if (count == -1) {
		db_show_ccp_sc(sc);
		return;
	}

	qindex = (unsigned)count;
	if (qindex >= nitems(sc->queues)) {
		db_printf("No such queue %u\n", qindex);
		goto usage;
	}
	db_show_ccp_qp(&sc->queues[qindex]);
	return;

usage:
	db_printf("usage: show ccp <unit>[,<qindex>]\n");
	return;
}
#endif /* DDB */
