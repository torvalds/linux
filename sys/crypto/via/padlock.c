/*-
 * Copyright (c) 2005-2008 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/malloc.h>
#include <sys/libkern.h>
#if defined(__amd64__) || defined(__i386__)
#include <machine/cpufunc.h>
#include <machine/cputypes.h>
#include <machine/md_var.h>
#include <machine/specialreg.h>
#endif

#include <opencrypto/cryptodev.h>

#include <crypto/via/padlock.h>

#include <sys/kobj.h>
#include <sys/bus.h>
#include "cryptodev_if.h"

/*
 * Technical documentation about the PadLock engine can be found here:
 *
 * http://www.via.com.tw/en/downloads/whitepapers/initiatives/padlock/programming_guide.pdf
 */

struct padlock_softc {
	int32_t		sc_cid;
};

static int padlock_newsession(device_t, crypto_session_t cses, struct cryptoini *cri);
static void padlock_freesession(device_t, crypto_session_t cses);
static void padlock_freesession_one(struct padlock_softc *sc,
    struct padlock_session *ses);
static int padlock_process(device_t, struct cryptop *crp, int hint __unused);

MALLOC_DEFINE(M_PADLOCK, "padlock_data", "PadLock Data");

static void
padlock_identify(driver_t *drv, device_t parent)
{
	/* NB: order 10 is so we get attached after h/w devices */
	if (device_find_child(parent, "padlock", -1) == NULL &&
	    BUS_ADD_CHILD(parent, 10, "padlock", -1) == 0)
		panic("padlock: could not attach");
}

static int
padlock_probe(device_t dev)
{
	char capp[256];

#if defined(__amd64__) || defined(__i386__)
	/* If there is no AES support, we has nothing to do here. */
	if (!(via_feature_xcrypt & VIA_HAS_AES)) {
		device_printf(dev, "No ACE support.\n");
		return (EINVAL);
	}
	strlcpy(capp, "AES-CBC", sizeof(capp));
#if 0
	strlcat(capp, ",AES-EBC", sizeof(capp));
	strlcat(capp, ",AES-CFB", sizeof(capp));
	strlcat(capp, ",AES-OFB", sizeof(capp));
#endif
	if (via_feature_xcrypt & VIA_HAS_SHA) {
		strlcat(capp, ",SHA1", sizeof(capp));
		strlcat(capp, ",SHA256", sizeof(capp));
	}
#if 0
	if (via_feature_xcrypt & VIA_HAS_AESCTR)
		strlcat(capp, ",AES-CTR", sizeof(capp));
	if (via_feature_xcrypt & VIA_HAS_MM)
		strlcat(capp, ",RSA", sizeof(capp));
#endif
	device_set_desc_copy(dev, capp);
	return (0);
#else
	return (EINVAL);
#endif
}

static int
padlock_attach(device_t dev)
{
	struct padlock_softc *sc = device_get_softc(dev);

	sc->sc_cid = crypto_get_driverid(dev, sizeof(struct padlock_session),
	    CRYPTOCAP_F_HARDWARE);
	if (sc->sc_cid < 0) {
		device_printf(dev, "Could not get crypto driver id.\n");
		return (ENOMEM);
	}

	crypto_register(sc->sc_cid, CRYPTO_AES_CBC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_MD5_HMAC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_SHA1_HMAC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_RIPEMD160_HMAC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_SHA2_256_HMAC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_SHA2_384_HMAC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_SHA2_512_HMAC, 0, 0);
	return (0);
}

static int
padlock_detach(device_t dev)
{
	struct padlock_softc *sc = device_get_softc(dev);

	crypto_unregister_all(sc->sc_cid);
	return (0);
}

static int
padlock_newsession(device_t dev, crypto_session_t cses, struct cryptoini *cri)
{
	struct padlock_softc *sc = device_get_softc(dev);
	struct padlock_session *ses = NULL;
	struct cryptoini *encini, *macini;
	struct thread *td;
	int error;

	if (cri == NULL)
		return (EINVAL);

	encini = macini = NULL;
	for (; cri != NULL; cri = cri->cri_next) {
		switch (cri->cri_alg) {
		case CRYPTO_NULL_HMAC:
		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			if (macini != NULL)
				return (EINVAL);
			macini = cri;
			break;
		case CRYPTO_AES_CBC:
			if (encini != NULL)
				return (EINVAL);
			encini = cri;
			break;
		default:
			return (EINVAL);
		}
	}

	/*
	 * We only support HMAC algorithms to be able to work with
	 * ipsec(4), so if we are asked only for authentication without
	 * encryption, don't pretend we can accellerate it.
	 */
	if (encini == NULL)
		return (EINVAL);

	ses = crypto_get_driver_session(cses);
	ses->ses_fpu_ctx = fpu_kern_alloc_ctx(FPU_KERN_NORMAL);

	error = padlock_cipher_setup(ses, encini);
	if (error != 0) {
		padlock_freesession_one(sc, ses);
		return (error);
	}

	if (macini != NULL) {
		td = curthread;
		fpu_kern_enter(td, ses->ses_fpu_ctx, FPU_KERN_NORMAL |
		    FPU_KERN_KTHR);
		error = padlock_hash_setup(ses, macini);
		fpu_kern_leave(td, ses->ses_fpu_ctx);
		if (error != 0) {
			padlock_freesession_one(sc, ses);
			return (error);
		}
	}

	return (0);
}

static void
padlock_freesession_one(struct padlock_softc *sc, struct padlock_session *ses)
{

	padlock_hash_free(ses);
	fpu_kern_free_ctx(ses->ses_fpu_ctx);
}

static void
padlock_freesession(device_t dev, crypto_session_t cses)
{
	struct padlock_softc *sc = device_get_softc(dev);
	struct padlock_session *ses;

	ses = crypto_get_driver_session(cses);
	padlock_freesession_one(sc, ses);
}

static int
padlock_process(device_t dev, struct cryptop *crp, int hint __unused)
{
	struct padlock_session *ses = NULL;
	struct cryptodesc *crd, *enccrd, *maccrd;
	int error = 0;

	enccrd = maccrd = NULL;

	/* Sanity check. */
	if (crp == NULL)
		return (EINVAL);

	if (crp->crp_callback == NULL || crp->crp_desc == NULL) {
		error = EINVAL;
		goto out;
	}

	for (crd = crp->crp_desc; crd != NULL; crd = crd->crd_next) {
		switch (crd->crd_alg) {
		case CRYPTO_NULL_HMAC:
		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			if (maccrd != NULL) {
				error = EINVAL;
				goto out;
			}
			maccrd = crd;
			break;
		case CRYPTO_AES_CBC:
			if (enccrd != NULL) {
				error = EINVAL;
				goto out;
			}
			enccrd = crd;
			break;
		default:
			return (EINVAL);
		}
	}
	if (enccrd == NULL || (enccrd->crd_len % AES_BLOCK_LEN) != 0) {
		error = EINVAL;
		goto out;
	}

	ses = crypto_get_driver_session(crp->crp_session);

	/* Perform data authentication if requested before encryption. */
	if (maccrd != NULL && maccrd->crd_next == enccrd) {
		error = padlock_hash_process(ses, maccrd, crp);
		if (error != 0)
			goto out;
	}

	error = padlock_cipher_process(ses, enccrd, crp);
	if (error != 0)
		goto out;

	/* Perform data authentication if requested after encryption. */
	if (maccrd != NULL && enccrd->crd_next == maccrd) {
		error = padlock_hash_process(ses, maccrd, crp);
		if (error != 0)
			goto out;
	}

out:
#if 0
	/*
	 * This code is not necessary, because contexts will be freed on next
	 * padlock_setup_mackey() call or at padlock_freesession() call.
	 */
	if (ses != NULL && maccrd != NULL &&
	    (maccrd->crd_flags & CRD_F_KEY_EXPLICIT) != 0) {
		padlock_free_ctx(ses->ses_axf, ses->ses_ictx);
		padlock_free_ctx(ses->ses_axf, ses->ses_octx);
	}
#endif
	crp->crp_etype = error;
	crypto_done(crp);
	return (error);
}

static device_method_t padlock_methods[] = {
	DEVMETHOD(device_identify,	padlock_identify),
	DEVMETHOD(device_probe,		padlock_probe),
	DEVMETHOD(device_attach,	padlock_attach),
	DEVMETHOD(device_detach,	padlock_detach),

	DEVMETHOD(cryptodev_newsession,	padlock_newsession),
	DEVMETHOD(cryptodev_freesession,padlock_freesession),
	DEVMETHOD(cryptodev_process,	padlock_process),

	{0, 0},
};

static driver_t padlock_driver = {
	"padlock",
	padlock_methods,
	sizeof(struct padlock_softc),
};
static devclass_t padlock_devclass;

/* XXX where to attach */
DRIVER_MODULE(padlock, nexus, padlock_driver, padlock_devclass, 0, 0);
MODULE_VERSION(padlock, 1);
MODULE_DEPEND(padlock, crypto, 1, 1, 1);
