/*-
 * Copyright (c) 2005-2008 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2010 Konstantin Belousov <kib@FreeBSD.org>
 * Copyright (c) 2014,2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by John-Mark Gurney
 * under sponsorship of the FreeBSD Foundation and
 * Rubicon Communications, LLC (Netgate).
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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

/*
 * This is based on the aesni code.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/queue.h>
#include <sys/rwlock.h>
#include <sys/smp.h>
#include <sys/uio.h>

#include <machine/vfp.h>

#include <opencrypto/cryptodev.h>
#include <cryptodev_if.h>
#include <crypto/armv8/armv8_crypto.h>
#include <crypto/rijndael/rijndael.h>

struct armv8_crypto_softc {
	int		dieing;
	int32_t		cid;
	struct rwlock	lock;
};

static struct mtx *ctx_mtx;
static struct fpu_kern_ctx **ctx_vfp;

#define AQUIRE_CTX(i, ctx)					\
	do {							\
		(i) = PCPU_GET(cpuid);				\
		mtx_lock(&ctx_mtx[(i)]);			\
		(ctx) = ctx_vfp[(i)];				\
	} while (0)
#define RELEASE_CTX(i, ctx)					\
	do {							\
		mtx_unlock(&ctx_mtx[(i)]);			\
		(i) = -1;					\
		(ctx) = NULL;					\
	} while (0)

static int armv8_crypto_cipher_process(struct armv8_crypto_session *,
    struct cryptodesc *, struct cryptop *);

MALLOC_DEFINE(M_ARMV8_CRYPTO, "armv8_crypto", "ARMv8 Crypto Data");

static void
armv8_crypto_identify(driver_t *drv, device_t parent)
{

	/* NB: order 10 is so we get attached after h/w devices */
	if (device_find_child(parent, "armv8crypto", -1) == NULL &&
	    BUS_ADD_CHILD(parent, 10, "armv8crypto", -1) == 0)
		panic("ARMv8 crypto: could not attach");
}

static int
armv8_crypto_probe(device_t dev)
{
	uint64_t reg;
	int ret = ENXIO;

	reg = READ_SPECIALREG(id_aa64isar0_el1);

	switch (ID_AA64ISAR0_AES(reg)) {
	case ID_AA64ISAR0_AES_BASE:
	case ID_AA64ISAR0_AES_PMULL:
		ret = 0;
		break;
	}

	device_set_desc_copy(dev, "AES-CBC");

	/* TODO: Check more fields as we support more features */

	return (ret);
}

static int
armv8_crypto_attach(device_t dev)
{
	struct armv8_crypto_softc *sc;
	int i;

	sc = device_get_softc(dev);
	sc->dieing = 0;

	sc->cid = crypto_get_driverid(dev, sizeof(struct armv8_crypto_session),
	    CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SYNC);
	if (sc->cid < 0) {
		device_printf(dev, "Could not get crypto driver id.\n");
		return (ENOMEM);
	}

	rw_init(&sc->lock, "armv8crypto");

	ctx_mtx = malloc(sizeof(*ctx_mtx) * (mp_maxid + 1), M_ARMV8_CRYPTO,
	    M_WAITOK|M_ZERO);
	ctx_vfp = malloc(sizeof(*ctx_vfp) * (mp_maxid + 1), M_ARMV8_CRYPTO,
	    M_WAITOK|M_ZERO);

	CPU_FOREACH(i) {
		ctx_vfp[i] = fpu_kern_alloc_ctx(0);
		mtx_init(&ctx_mtx[i], "armv8cryptoctx", NULL, MTX_DEF|MTX_NEW);
	}

	crypto_register(sc->cid, CRYPTO_AES_CBC, 0, 0);

	return (0);
}

static int
armv8_crypto_detach(device_t dev)
{
	struct armv8_crypto_softc *sc;
	int i;

	sc = device_get_softc(dev);

	rw_wlock(&sc->lock);
	sc->dieing = 1;
	rw_wunlock(&sc->lock);
	crypto_unregister_all(sc->cid);

	rw_destroy(&sc->lock);

	CPU_FOREACH(i) {
		if (ctx_vfp[i] != NULL) {
			mtx_destroy(&ctx_mtx[i]);
			fpu_kern_free_ctx(ctx_vfp[i]);
		}
		ctx_vfp[i] = NULL;
	}
	free(ctx_mtx, M_ARMV8_CRYPTO);
	ctx_mtx = NULL;
	free(ctx_vfp, M_ARMV8_CRYPTO);
	ctx_vfp = NULL;

	return (0);
}

static int
armv8_crypto_cipher_setup(struct armv8_crypto_session *ses,
    struct cryptoini *encini)
{
	int i;

	switch (ses->algo) {
	case CRYPTO_AES_CBC:
		switch (encini->cri_klen) {
		case 128:
			ses->rounds = AES128_ROUNDS;
			break;
		case 192:
			ses->rounds = AES192_ROUNDS;
			break;
		case 256:
			ses->rounds = AES256_ROUNDS;
			break;
		default:
			CRYPTDEB("invalid CBC/ICM/GCM key length");
			return (EINVAL);
		}
		break;
	default:
		return (EINVAL);
	}

	rijndaelKeySetupEnc(ses->enc_schedule, encini->cri_key,
	    encini->cri_klen);
	rijndaelKeySetupDec(ses->dec_schedule, encini->cri_key,
	    encini->cri_klen);
	for (i = 0; i < nitems(ses->enc_schedule); i++) {
		ses->enc_schedule[i] = bswap32(ses->enc_schedule[i]);
		ses->dec_schedule[i] = bswap32(ses->dec_schedule[i]);
	}

	return (0);
}

static int
armv8_crypto_newsession(device_t dev, crypto_session_t cses,
    struct cryptoini *cri)
{
	struct armv8_crypto_softc *sc;
	struct armv8_crypto_session *ses;
	struct cryptoini *encini;
	int error;

	if (cri == NULL) {
		CRYPTDEB("no cri");
		return (EINVAL);
	}

	sc = device_get_softc(dev);
	if (sc->dieing)
		return (EINVAL);

	ses = NULL;
	encini = NULL;
	for (; cri != NULL; cri = cri->cri_next) {
		switch (cri->cri_alg) {
		case CRYPTO_AES_CBC:
			if (encini != NULL) {
				CRYPTDEB("encini already set");
				return (EINVAL);
			}
			encini = cri;
			break;
		default:
			CRYPTDEB("unhandled algorithm");
			return (EINVAL);
		}
	}
	if (encini == NULL) {
		CRYPTDEB("no cipher");
		return (EINVAL);
	}

	rw_wlock(&sc->lock);
	if (sc->dieing) {
		rw_wunlock(&sc->lock);
		return (EINVAL);
	}

	ses = crypto_get_driver_session(cses);
	ses->algo = encini->cri_alg;

	error = armv8_crypto_cipher_setup(ses, encini);
	if (error != 0) {
		CRYPTDEB("setup failed");
		return (error);
	}

	return (0);
}

static int
armv8_crypto_process(device_t dev, struct cryptop *crp, int hint __unused)
{
	struct cryptodesc *crd, *enccrd;
	struct armv8_crypto_session *ses;
	int error;

	error = 0;
	enccrd = NULL;

	/* Sanity check. */
	if (crp == NULL)
		return (EINVAL);

	if (crp->crp_callback == NULL || crp->crp_desc == NULL) {
		error = EINVAL;
		goto out;
	}

	for (crd = crp->crp_desc; crd != NULL; crd = crd->crd_next) {
		switch (crd->crd_alg) {
		case CRYPTO_AES_CBC:
			if (enccrd != NULL) {
				error = EINVAL;
				goto out;
			}
			enccrd = crd;
			break;
		default:
			error = EINVAL;
			goto out;
		}
	}

	if (enccrd == NULL) {
		error = EINVAL;
		goto out;
	}

	/* We can only handle full blocks for now */
	if ((enccrd->crd_len % AES_BLOCK_LEN) != 0) {
		error = EINVAL;
		goto out;
	}

	ses = crypto_get_driver_session(crp->crp_session);
	error = armv8_crypto_cipher_process(ses, enccrd, crp);

out:
	crp->crp_etype = error;
	crypto_done(crp);
	return (error);
}

static uint8_t *
armv8_crypto_cipher_alloc(struct cryptodesc *enccrd, struct cryptop *crp,
    int *allocated)
{
	struct mbuf *m;
	struct uio *uio;
	struct iovec *iov;
	uint8_t *addr;

	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		m = (struct mbuf *)crp->crp_buf;
		if (m->m_next != NULL)
			goto alloc;
		addr = mtod(m, uint8_t *);
	} else if (crp->crp_flags & CRYPTO_F_IOV) {
		uio = (struct uio *)crp->crp_buf;
		if (uio->uio_iovcnt != 1)
			goto alloc;
		iov = uio->uio_iov;
		addr = (uint8_t *)iov->iov_base;
	} else
		addr = (uint8_t *)crp->crp_buf;
	*allocated = 0;
	addr += enccrd->crd_skip;
	return (addr);

alloc:
	addr = malloc(enccrd->crd_len, M_ARMV8_CRYPTO, M_NOWAIT);
	if (addr != NULL) {
		*allocated = 1;
		crypto_copydata(crp->crp_flags, crp->crp_buf, enccrd->crd_skip,
		    enccrd->crd_len, addr);
	} else
		*allocated = 0;
	return (addr);
}

static int
armv8_crypto_cipher_process(struct armv8_crypto_session *ses,
    struct cryptodesc *enccrd, struct cryptop *crp)
{
	struct fpu_kern_ctx *ctx;
	uint8_t *buf;
	uint8_t iv[AES_BLOCK_LEN];
	int allocated, i;
	int encflag, ivlen;
	int kt;

	encflag = (enccrd->crd_flags & CRD_F_ENCRYPT) == CRD_F_ENCRYPT;

	buf = armv8_crypto_cipher_alloc(enccrd, crp, &allocated);
	if (buf == NULL)
		return (ENOMEM);

	kt = is_fpu_kern_thread(0);
	if (!kt) {
		AQUIRE_CTX(i, ctx);
		fpu_kern_enter(curthread, ctx,
		    FPU_KERN_NORMAL | FPU_KERN_KTHR);
	}

	if ((enccrd->crd_flags & CRD_F_KEY_EXPLICIT) != 0) {
		panic("CRD_F_KEY_EXPLICIT");
	}

	switch (enccrd->crd_alg) {
	case CRYPTO_AES_CBC:
		ivlen = AES_BLOCK_LEN;
		break;
	}

	/* Setup iv */
	if (encflag) {
		if ((enccrd->crd_flags & CRD_F_IV_EXPLICIT) != 0)
			bcopy(enccrd->crd_iv, iv, ivlen);
		else
			arc4rand(iv, ivlen, 0);

		if ((enccrd->crd_flags & CRD_F_IV_PRESENT) == 0)
			crypto_copyback(crp->crp_flags, crp->crp_buf,
			    enccrd->crd_inject, ivlen, iv);
	} else {
		if ((enccrd->crd_flags & CRD_F_IV_EXPLICIT) != 0)
			bcopy(enccrd->crd_iv, iv, ivlen);
		else
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    enccrd->crd_inject, ivlen, iv);
	}

	/* Do work */
	switch (ses->algo) {
	case CRYPTO_AES_CBC:
		if (encflag)
			armv8_aes_encrypt_cbc(ses->rounds, ses->enc_schedule,
			    enccrd->crd_len, buf, buf, iv);
		else
			armv8_aes_decrypt_cbc(ses->rounds, ses->dec_schedule,
			    enccrd->crd_len, buf, iv);
		break;
	}

	if (allocated)
		crypto_copyback(crp->crp_flags, crp->crp_buf, enccrd->crd_skip,
		    enccrd->crd_len, buf);

	if (!kt) {
		fpu_kern_leave(curthread, ctx);
		RELEASE_CTX(i, ctx);
	}
	if (allocated) {
		bzero(buf, enccrd->crd_len);
		free(buf, M_ARMV8_CRYPTO);
	}
	return (0);
}

static device_method_t armv8_crypto_methods[] = {
	DEVMETHOD(device_identify,	armv8_crypto_identify),
	DEVMETHOD(device_probe,		armv8_crypto_probe),
	DEVMETHOD(device_attach,	armv8_crypto_attach),
	DEVMETHOD(device_detach,	armv8_crypto_detach),

	DEVMETHOD(cryptodev_newsession,	armv8_crypto_newsession),
	DEVMETHOD(cryptodev_process,	armv8_crypto_process),

	DEVMETHOD_END,
};

static DEFINE_CLASS_0(armv8crypto, armv8_crypto_driver, armv8_crypto_methods,
    sizeof(struct armv8_crypto_softc));
static devclass_t armv8_crypto_devclass;

DRIVER_MODULE(armv8crypto, nexus, armv8_crypto_driver, armv8_crypto_devclass,
    0, 0);
