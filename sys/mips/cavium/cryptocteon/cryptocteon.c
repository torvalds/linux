/*
 * Octeon Crypto for OCF
 *
 * Written by David McCullough <david_mccullough@securecomputing.com>
 * Copyright (C) 2009 David McCullough
 *
 * LICENSE TERMS
 *
 * The free distribution and use of this software in both source and binary
 * form is allowed (with or without changes) provided that:
 *
 *   1. distributions of this source code include the above copyright
 *      notice, this list of conditions and the following disclaimer;
 *
 *   2. distributions in binary form include the above copyright
 *      notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other associated materials;
 *
 *   3. the copyright holder's name is not used to endorse products
 *      built using this software without specific written permission.
 *
 * DISCLAIMER
 *
 * This software is provided 'as is' with no explicit or implied warranties
 * in respect of its properties, including, but not limited to, correctness
 * and/or fitness for purpose.
 * ---------------------------------------------------------------------------
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/uio.h>

#include <opencrypto/cryptodev.h>

#include <contrib/octeon-sdk/cvmx.h>

#include <mips/cavium/cryptocteon/cryptocteonvar.h>

#include "cryptodev_if.h"

struct cryptocteon_softc {
	int32_t			sc_cid;		/* opencrypto id */
};

int cryptocteon_debug = 0;
TUNABLE_INT("hw.cryptocteon.debug", &cryptocteon_debug);

static void cryptocteon_identify(driver_t *, device_t);
static int cryptocteon_probe(device_t);
static int cryptocteon_attach(device_t);

static int cryptocteon_process(device_t, struct cryptop *, int);
static int cryptocteon_newsession(device_t, crypto_session_t, struct cryptoini *);

static void
cryptocteon_identify(driver_t *drv, device_t parent)
{
	if (octeon_has_feature(OCTEON_FEATURE_CRYPTO))
		BUS_ADD_CHILD(parent, 0, "cryptocteon", 0);
}

static int
cryptocteon_probe(device_t dev)
{
	device_set_desc(dev, "Octeon Secure Coprocessor");
	return (0);
}

static int
cryptocteon_attach(device_t dev)
{
	struct cryptocteon_softc *sc;

	sc = device_get_softc(dev);

	sc->sc_cid = crypto_get_driverid(dev, sizeof(struct octo_sess),
	    CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SYNC);
	if (sc->sc_cid < 0) {
		device_printf(dev, "crypto_get_driverid ret %d\n", sc->sc_cid);
		return (ENXIO);
	}

	crypto_register(sc->sc_cid, CRYPTO_MD5_HMAC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_SHA1_HMAC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_DES_CBC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_3DES_CBC, 0, 0);
	crypto_register(sc->sc_cid, CRYPTO_AES_CBC, 0, 0);

	return (0);
}

/*
 * Generate a new octo session.  We artifically limit it to a single
 * hash/cipher or hash-cipher combo just to make it easier, most callers
 * do not expect more than this anyway.
 */
static int
cryptocteon_newsession(device_t dev, crypto_session_t cses,
    struct cryptoini *cri)
{
	struct cryptoini *c, *encini = NULL, *macini = NULL;
	struct cryptocteon_softc *sc;
	struct octo_sess *ocd;
	int i;

	sc = device_get_softc(dev);

	if (cri == NULL || sc == NULL)
		return (EINVAL);

	/*
	 * To keep it simple, we only handle hash, cipher or hash/cipher in a
	 * session,  you cannot currently do multiple ciphers/hashes in one
	 * session even though it would be possibel to code this driver to
	 * handle it.
	 */
	for (i = 0, c = cri; c && i < 2; i++) {
		if (c->cri_alg == CRYPTO_MD5_HMAC ||
		    c->cri_alg == CRYPTO_SHA1_HMAC ||
		    c->cri_alg == CRYPTO_NULL_HMAC) {
			if (macini) {
				break;
			}
			macini = c;
		}
		if (c->cri_alg == CRYPTO_DES_CBC ||
		    c->cri_alg == CRYPTO_3DES_CBC ||
		    c->cri_alg == CRYPTO_AES_CBC ||
		    c->cri_alg == CRYPTO_NULL_CBC) {
			if (encini) {
				break;
			}
			encini = c;
		}
		c = c->cri_next;
	}
	if (!macini && !encini) {
		dprintf("%s,%d - EINVAL bad cipher/hash or combination\n",
				__FILE__, __LINE__);
		return EINVAL;
	}
	if (c) {
		dprintf("%s,%d - EINVAL cannot handle chained cipher/hash combos\n",
				__FILE__, __LINE__);
		return EINVAL;
	}

	/*
	 * So we have something we can do, lets setup the session
	 */
	ocd = crypto_get_driver_session(cses);

	if (encini && encini->cri_key) {
		ocd->octo_encklen = (encini->cri_klen + 7) / 8;
		memcpy(ocd->octo_enckey, encini->cri_key, ocd->octo_encklen);
	}

	if (macini && macini->cri_key) {
		ocd->octo_macklen = (macini->cri_klen + 7) / 8;
		memcpy(ocd->octo_mackey, macini->cri_key, ocd->octo_macklen);
	}

	ocd->octo_mlen = 0;
	if (encini && encini->cri_mlen)
		ocd->octo_mlen = encini->cri_mlen;
	else if (macini && macini->cri_mlen)
		ocd->octo_mlen = macini->cri_mlen;
	else
		ocd->octo_mlen = 12;

	/*
	 * point c at the enc if it exists, otherwise the mac
	 */
	c = encini ? encini : macini;

	switch (c->cri_alg) {
	case CRYPTO_DES_CBC:
	case CRYPTO_3DES_CBC:
		ocd->octo_ivsize  = 8;
		switch (macini ? macini->cri_alg : -1) {
		case CRYPTO_MD5_HMAC:
			ocd->octo_encrypt = octo_des_cbc_md5_encrypt;
			ocd->octo_decrypt = octo_des_cbc_md5_decrypt;
			octo_calc_hash(0, macini->cri_key, ocd->octo_hminner,
					ocd->octo_hmouter);
			break;
		case CRYPTO_SHA1_HMAC:
			ocd->octo_encrypt = octo_des_cbc_sha1_encrypt;
			ocd->octo_decrypt = octo_des_cbc_sha1_encrypt;
			octo_calc_hash(1, macini->cri_key, ocd->octo_hminner,
					ocd->octo_hmouter);
			break;
		case -1:
			ocd->octo_encrypt = octo_des_cbc_encrypt;
			ocd->octo_decrypt = octo_des_cbc_decrypt;
			break;
		default:
			dprintf("%s,%d: EINVALn", __FILE__, __LINE__);
			return EINVAL;
		}
		break;
	case CRYPTO_AES_CBC:
		ocd->octo_ivsize  = 16;
		switch (macini ? macini->cri_alg : -1) {
		case CRYPTO_MD5_HMAC:
			ocd->octo_encrypt = octo_aes_cbc_md5_encrypt;
			ocd->octo_decrypt = octo_aes_cbc_md5_decrypt;
			octo_calc_hash(0, macini->cri_key, ocd->octo_hminner,
					ocd->octo_hmouter);
			break;
		case CRYPTO_SHA1_HMAC:
			ocd->octo_encrypt = octo_aes_cbc_sha1_encrypt;
			ocd->octo_decrypt = octo_aes_cbc_sha1_decrypt;
			octo_calc_hash(1, macini->cri_key, ocd->octo_hminner,
					ocd->octo_hmouter);
			break;
		case -1:
			ocd->octo_encrypt = octo_aes_cbc_encrypt;
			ocd->octo_decrypt = octo_aes_cbc_decrypt;
			break;
		default:
			dprintf("%s,%d: EINVALn", __FILE__, __LINE__);
			return EINVAL;
		}
		break;
	case CRYPTO_MD5_HMAC:
		ocd->octo_encrypt = octo_null_md5_encrypt;
		ocd->octo_decrypt = octo_null_md5_encrypt;
		octo_calc_hash(0, macini->cri_key, ocd->octo_hminner,
				ocd->octo_hmouter);
		break;
	case CRYPTO_SHA1_HMAC:
		ocd->octo_encrypt = octo_null_sha1_encrypt;
		ocd->octo_decrypt = octo_null_sha1_encrypt;
		octo_calc_hash(1, macini->cri_key, ocd->octo_hminner,
				ocd->octo_hmouter);
		break;
	default:
		dprintf("%s,%d: EINVALn", __FILE__, __LINE__);
		return EINVAL;
	}

	ocd->octo_encalg = encini ? encini->cri_alg : -1;
	ocd->octo_macalg = macini ? macini->cri_alg : -1;

	return (0);
}

/*
 * Process a request.
 */
static int
cryptocteon_process(device_t dev, struct cryptop *crp, int hint)
{
	struct cryptodesc *crd;
	struct octo_sess *od;
	size_t iovcnt, iovlen;
	struct mbuf *m = NULL;
	struct uio *uiop = NULL;
	struct cryptodesc *enccrd = NULL, *maccrd = NULL;
	unsigned char *ivp = NULL;
	unsigned char iv_data[HASH_MAX_LEN];
	int auth_off = 0, auth_len = 0, crypt_off = 0, crypt_len = 0, icv_off = 0;
	struct cryptocteon_softc *sc;

	sc = device_get_softc(dev);

	if (sc == NULL || crp == NULL)
		return EINVAL;

	crp->crp_etype = 0;

	if (crp->crp_desc == NULL || crp->crp_buf == NULL) {
		dprintf("%s,%d: EINVAL\n", __FILE__, __LINE__);
		crp->crp_etype = EINVAL;
		goto done;
	}

	od = crypto_get_driver_session(crp->crp_session);

	/*
	 * do some error checking outside of the loop for m and IOV processing
	 * this leaves us with valid m or uiop pointers for later
	 */
	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		unsigned frags;

		m = (struct mbuf *) crp->crp_buf;
		for (frags = 0; m != NULL; frags++)
			m = m->m_next;

		if (frags >= UIO_MAXIOV) {
			printf("%s,%d: %d frags > UIO_MAXIOV", __FILE__, __LINE__, frags);
			goto done;
		}

		m = (struct mbuf *) crp->crp_buf;
	} else if (crp->crp_flags & CRYPTO_F_IOV) {
		uiop = (struct uio *) crp->crp_buf;
		if (uiop->uio_iovcnt > UIO_MAXIOV) {
			printf("%s,%d: %d uio_iovcnt > UIO_MAXIOV", __FILE__, __LINE__,
			       uiop->uio_iovcnt);
			goto done;
		}
	}

	/* point our enccrd and maccrd appropriately */
	crd = crp->crp_desc;
	if (crd->crd_alg == od->octo_encalg)
		enccrd = crd;
	if (crd->crd_alg == od->octo_macalg)
		maccrd = crd;
	crd = crd->crd_next;
	if (crd) {
		if (crd->crd_alg == od->octo_encalg)
			enccrd = crd;
		if (crd->crd_alg == od->octo_macalg)
			maccrd = crd;
		crd = crd->crd_next;
	}
	if (crd) {
		crp->crp_etype = EINVAL;
		dprintf("%s,%d: ENOENT - descriptors do not match session\n",
				__FILE__, __LINE__);
		goto done;
	}

	if (enccrd) {
		if (enccrd->crd_flags & CRD_F_IV_EXPLICIT) {
			ivp = enccrd->crd_iv;
		} else {
			ivp = iv_data;
			crypto_copydata(crp->crp_flags, crp->crp_buf,
					enccrd->crd_inject, od->octo_ivsize, (caddr_t) ivp);
		}

		if (maccrd) {
			auth_off = maccrd->crd_skip;
			auth_len = maccrd->crd_len;
			icv_off  = maccrd->crd_inject;
		}

		crypt_off = enccrd->crd_skip;
		crypt_len = enccrd->crd_len;
	} else { /* if (maccrd) */
		auth_off = maccrd->crd_skip;
		auth_len = maccrd->crd_len;
		icv_off  = maccrd->crd_inject;
	}

	/*
	 * setup the I/O vector to cover the buffer
	 */
	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		iovcnt = 0;
		iovlen = 0;

		while (m != NULL) {
			od->octo_iov[iovcnt].iov_base = mtod(m, void *);
			od->octo_iov[iovcnt].iov_len = m->m_len;

			m = m->m_next;
			iovlen += od->octo_iov[iovcnt++].iov_len;
		}
	} else if (crp->crp_flags & CRYPTO_F_IOV) {
		iovlen = 0;
		for (iovcnt = 0; iovcnt < uiop->uio_iovcnt; iovcnt++) {
			od->octo_iov[iovcnt].iov_base = uiop->uio_iov[iovcnt].iov_base;
			od->octo_iov[iovcnt].iov_len = uiop->uio_iov[iovcnt].iov_len;

			iovlen += od->octo_iov[iovcnt].iov_len;
		}
	} else {
		iovlen = crp->crp_ilen;
		od->octo_iov[0].iov_base = crp->crp_buf;
		od->octo_iov[0].iov_len = crp->crp_ilen;
		iovcnt = 1;
	}


	/*
	 * setup a new explicit key
	 */
	if (enccrd) {
		if (enccrd->crd_flags & CRD_F_KEY_EXPLICIT) {
			od->octo_encklen = (enccrd->crd_klen + 7) / 8;
			memcpy(od->octo_enckey, enccrd->crd_key, od->octo_encklen);
		}
	}
	if (maccrd) {
		if (maccrd->crd_flags & CRD_F_KEY_EXPLICIT) {
			od->octo_macklen = (maccrd->crd_klen + 7) / 8;
			memcpy(od->octo_mackey, maccrd->crd_key, od->octo_macklen);
			od->octo_mackey_set = 0;
		}
		if (!od->octo_mackey_set) {
			octo_calc_hash(maccrd->crd_alg == CRYPTO_MD5_HMAC ? 0 : 1,
				maccrd->crd_key, od->octo_hminner, od->octo_hmouter);
			od->octo_mackey_set = 1;
		}
	}


	if (!enccrd || (enccrd->crd_flags & CRD_F_ENCRYPT))
		(*od->octo_encrypt)(od, od->octo_iov, iovcnt, iovlen,
				auth_off, auth_len, crypt_off, crypt_len, icv_off, ivp);
	else
		(*od->octo_decrypt)(od, od->octo_iov, iovcnt, iovlen,
				auth_off, auth_len, crypt_off, crypt_len, icv_off, ivp);

done:
	crypto_done(crp);
	return (0);
}

static device_method_t cryptocteon_methods[] = {
	/* device methods */
	DEVMETHOD(device_identify,	cryptocteon_identify),
	DEVMETHOD(device_probe,		cryptocteon_probe),
	DEVMETHOD(device_attach,	cryptocteon_attach),

	/* crypto device methods */
	DEVMETHOD(cryptodev_newsession,	cryptocteon_newsession),
	DEVMETHOD(cryptodev_process,	cryptocteon_process),

	{ 0, 0 }
};

static driver_t cryptocteon_driver = {
	"cryptocteon",
	cryptocteon_methods,
	sizeof (struct cryptocteon_softc),
};
static devclass_t cryptocteon_devclass;
DRIVER_MODULE(cryptocteon, nexus, cryptocteon_driver, cryptocteon_devclass, 0, 0);
