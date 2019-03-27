/*-
 * Copyright (c) 2005-2008 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2010 Konstantin Belousov <kib@FreeBSD.org>
 * Copyright (c) 2014 The FreeBSD Foundation
 * Copyright (c) 2017 Conrad Meyer <cem@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed by John-Mark Gurney
 * under sponsorship of the FreeBSD Foundation and
 * Rubicon Communications, LLC (Netgate).
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
#include <sys/kobj.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/uio.h>
#include <sys/mbuf.h>
#include <sys/smp.h>

#include <crypto/aesni/aesni.h>
#include <crypto/aesni/sha_sse.h>
#include <crypto/sha1.h>
#include <crypto/sha2/sha224.h>
#include <crypto/sha2/sha256.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/gmac.h>
#include <cryptodev_if.h>

#include <machine/md_var.h>
#include <machine/specialreg.h>
#if defined(__i386__)
#include <machine/npx.h>
#elif defined(__amd64__)
#include <machine/fpu.h>
#endif

static struct mtx_padalign *ctx_mtx;
static struct fpu_kern_ctx **ctx_fpu;

struct aesni_softc {
	int32_t cid;
	bool	has_aes;
	bool	has_sha;
};

#define ACQUIRE_CTX(i, ctx)					\
	do {							\
		(i) = PCPU_GET(cpuid);				\
		mtx_lock(&ctx_mtx[(i)]);			\
		(ctx) = ctx_fpu[(i)];				\
	} while (0)
#define RELEASE_CTX(i, ctx)					\
	do {							\
		mtx_unlock(&ctx_mtx[(i)]);			\
		(i) = -1;					\
		(ctx) = NULL;					\
	} while (0)

static int aesni_newsession(device_t, crypto_session_t cses,
    struct cryptoini *cri);
static int aesni_cipher_setup(struct aesni_session *ses,
    struct cryptoini *encini, struct cryptoini *authini);
static int aesni_cipher_process(struct aesni_session *ses,
    struct cryptodesc *enccrd, struct cryptodesc *authcrd, struct cryptop *crp);
static int aesni_cipher_crypt(struct aesni_session *ses,
    struct cryptodesc *enccrd, struct cryptodesc *authcrd, struct cryptop *crp);
static int aesni_cipher_mac(struct aesni_session *ses, struct cryptodesc *crd,
    struct cryptop *crp);

MALLOC_DEFINE(M_AESNI, "aesni_data", "AESNI Data");

static void
aesni_identify(driver_t *drv, device_t parent)
{

	/* NB: order 10 is so we get attached after h/w devices */
	if (device_find_child(parent, "aesni", -1) == NULL &&
	    BUS_ADD_CHILD(parent, 10, "aesni", -1) == 0)
		panic("aesni: could not attach");
}

static void
detect_cpu_features(bool *has_aes, bool *has_sha)
{

	*has_aes = ((cpu_feature2 & CPUID2_AESNI) != 0 &&
	    (cpu_feature2 & CPUID2_SSE41) != 0);
	*has_sha = ((cpu_stdext_feature & CPUID_STDEXT_SHA) != 0 &&
	    (cpu_feature2 & CPUID2_SSSE3) != 0);
}

static int
aesni_probe(device_t dev)
{
	bool has_aes, has_sha;

	detect_cpu_features(&has_aes, &has_sha);
	if (!has_aes && !has_sha) {
		device_printf(dev, "No AES or SHA support.\n");
		return (EINVAL);
	} else if (has_aes && has_sha)
		device_set_desc(dev,
		    "AES-CBC,AES-XTS,AES-GCM,AES-ICM,SHA1,SHA256");
	else if (has_aes)
		device_set_desc(dev, "AES-CBC,AES-XTS,AES-GCM,AES-ICM");
	else
		device_set_desc(dev, "SHA1,SHA256");

	return (0);
}

static void
aesni_cleanctx(void)
{
	int i;

	/* XXX - no way to return driverid */
	CPU_FOREACH(i) {
		if (ctx_fpu[i] != NULL) {
			mtx_destroy(&ctx_mtx[i]);
			fpu_kern_free_ctx(ctx_fpu[i]);
		}
		ctx_fpu[i] = NULL;
	}
	free(ctx_mtx, M_AESNI);
	ctx_mtx = NULL;
	free(ctx_fpu, M_AESNI);
	ctx_fpu = NULL;
}

static int
aesni_attach(device_t dev)
{
	struct aesni_softc *sc;
	int i;

	sc = device_get_softc(dev);

	sc->cid = crypto_get_driverid(dev, sizeof(struct aesni_session),
	    CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SYNC);
	if (sc->cid < 0) {
		device_printf(dev, "Could not get crypto driver id.\n");
		return (ENOMEM);
	}

	ctx_mtx = malloc(sizeof *ctx_mtx * (mp_maxid + 1), M_AESNI,
	    M_WAITOK|M_ZERO);
	ctx_fpu = malloc(sizeof *ctx_fpu * (mp_maxid + 1), M_AESNI,
	    M_WAITOK|M_ZERO);

	CPU_FOREACH(i) {
		ctx_fpu[i] = fpu_kern_alloc_ctx(0);
		mtx_init(&ctx_mtx[i], "anifpumtx", NULL, MTX_DEF|MTX_NEW);
	}

	detect_cpu_features(&sc->has_aes, &sc->has_sha);
	if (sc->has_aes) {
		crypto_register(sc->cid, CRYPTO_AES_CBC, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_ICM, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_NIST_GCM_16, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_128_NIST_GMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_192_NIST_GMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_256_NIST_GMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_AES_XTS, 0, 0);
	}
	if (sc->has_sha) {
		crypto_register(sc->cid, CRYPTO_SHA1, 0, 0);
		crypto_register(sc->cid, CRYPTO_SHA1_HMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_SHA2_224, 0, 0);
		crypto_register(sc->cid, CRYPTO_SHA2_224_HMAC, 0, 0);
		crypto_register(sc->cid, CRYPTO_SHA2_256, 0, 0);
		crypto_register(sc->cid, CRYPTO_SHA2_256_HMAC, 0, 0);
	}
	return (0);
}

static int
aesni_detach(device_t dev)
{
	struct aesni_softc *sc;

	sc = device_get_softc(dev);

	crypto_unregister_all(sc->cid);

	aesni_cleanctx();

	return (0);
}

static int
aesni_newsession(device_t dev, crypto_session_t cses, struct cryptoini *cri)
{
	struct aesni_softc *sc;
	struct aesni_session *ses;
	struct cryptoini *encini, *authini;
	bool gcm_hash, gcm;
	int error;

	KASSERT(cses != NULL, ("EDOOFUS"));
	if (cri == NULL) {
		CRYPTDEB("no cri");
		return (EINVAL);
	}

	sc = device_get_softc(dev);

	ses = crypto_get_driver_session(cses);

	authini = NULL;
	encini = NULL;
	gcm = false;
	gcm_hash = false;
	for (; cri != NULL; cri = cri->cri_next) {
		switch (cri->cri_alg) {
		case CRYPTO_AES_NIST_GCM_16:
			gcm = true;
			/* FALLTHROUGH */
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_ICM:
		case CRYPTO_AES_XTS:
			if (!sc->has_aes)
				goto unhandled;
			if (encini != NULL) {
				CRYPTDEB("encini already set");
				return (EINVAL);
			}
			encini = cri;
			break;
		case CRYPTO_AES_128_NIST_GMAC:
		case CRYPTO_AES_192_NIST_GMAC:
		case CRYPTO_AES_256_NIST_GMAC:
			/*
			 * nothing to do here, maybe in the future cache some
			 * values for GHASH
			 */
			gcm_hash = true;
			break;
		case CRYPTO_SHA1:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_SHA2_224:
		case CRYPTO_SHA2_224_HMAC:
		case CRYPTO_SHA2_256:
		case CRYPTO_SHA2_256_HMAC:
			if (!sc->has_sha)
				goto unhandled;
			if (authini != NULL) {
				CRYPTDEB("authini already set");
				return (EINVAL);
			}
			authini = cri;
			break;
		default:
unhandled:
			CRYPTDEB("unhandled algorithm");
			return (EINVAL);
		}
	}
	if (encini == NULL && authini == NULL) {
		CRYPTDEB("no cipher");
		return (EINVAL);
	}
	/*
	 * GMAC algorithms are only supported with simultaneous GCM.  Likewise
	 * GCM is not supported without GMAC.
	 */
	if (gcm_hash != gcm)
		return (EINVAL);

	if (encini != NULL)
		ses->algo = encini->cri_alg;
	if (authini != NULL)
		ses->auth_algo = authini->cri_alg;

	error = aesni_cipher_setup(ses, encini, authini);
	if (error != 0) {
		CRYPTDEB("setup failed");
		return (error);
	}

	return (0);
}

static int
aesni_process(device_t dev, struct cryptop *crp, int hint __unused)
{
	struct aesni_session *ses;
	struct cryptodesc *crd, *enccrd, *authcrd;
	int error, needauth;

	ses = NULL;
	error = 0;
	enccrd = NULL;
	authcrd = NULL;
	needauth = 0;

	/* Sanity check. */
	if (crp == NULL)
		return (EINVAL);

	if (crp->crp_callback == NULL || crp->crp_desc == NULL ||
	    crp->crp_session == NULL) {
		error = EINVAL;
		goto out;
	}

	for (crd = crp->crp_desc; crd != NULL; crd = crd->crd_next) {
		switch (crd->crd_alg) {
		case CRYPTO_AES_NIST_GCM_16:
			needauth = 1;
			/* FALLTHROUGH */
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_ICM:
		case CRYPTO_AES_XTS:
			if (enccrd != NULL) {
				error = EINVAL;
				goto out;
			}
			enccrd = crd;
			break;

		case CRYPTO_AES_128_NIST_GMAC:
		case CRYPTO_AES_192_NIST_GMAC:
		case CRYPTO_AES_256_NIST_GMAC:
		case CRYPTO_SHA1:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_SHA2_224:
		case CRYPTO_SHA2_224_HMAC:
		case CRYPTO_SHA2_256:
		case CRYPTO_SHA2_256_HMAC:
			if (authcrd != NULL) {
				error = EINVAL;
				goto out;
			}
			authcrd = crd;
			break;

		default:
			error = EINVAL;
			goto out;
		}
	}

	if ((enccrd == NULL && authcrd == NULL) ||
	    (needauth && authcrd == NULL)) {
		error = EINVAL;
		goto out;
	}

	/* CBC & XTS can only handle full blocks for now */
	if (enccrd != NULL && (enccrd->crd_alg == CRYPTO_AES_CBC ||
	    enccrd->crd_alg == CRYPTO_AES_XTS) &&
	    (enccrd->crd_len % AES_BLOCK_LEN) != 0) {
		error = EINVAL;
		goto out;
	}

	ses = crypto_get_driver_session(crp->crp_session);
	KASSERT(ses != NULL, ("EDOOFUS"));

	error = aesni_cipher_process(ses, enccrd, authcrd, crp);
	if (error != 0)
		goto out;

out:
	crp->crp_etype = error;
	crypto_done(crp);
	return (error);
}

static uint8_t *
aesni_cipher_alloc(struct cryptodesc *enccrd, struct cryptop *crp,
    bool *allocated)
{
	uint8_t *addr;

	addr = crypto_contiguous_subsegment(crp->crp_flags,
	    crp->crp_buf, enccrd->crd_skip, enccrd->crd_len);
	if (addr != NULL) {
		*allocated = false;
		return (addr);
	}
	addr = malloc(enccrd->crd_len, M_AESNI, M_NOWAIT);
	if (addr != NULL) {
		*allocated = true;
		crypto_copydata(crp->crp_flags, crp->crp_buf, enccrd->crd_skip,
		    enccrd->crd_len, addr);
	} else
		*allocated = false;
	return (addr);
}

static device_method_t aesni_methods[] = {
	DEVMETHOD(device_identify, aesni_identify),
	DEVMETHOD(device_probe, aesni_probe),
	DEVMETHOD(device_attach, aesni_attach),
	DEVMETHOD(device_detach, aesni_detach),

	DEVMETHOD(cryptodev_newsession, aesni_newsession),
	DEVMETHOD(cryptodev_process, aesni_process),

	DEVMETHOD_END
};

static driver_t aesni_driver = {
	"aesni",
	aesni_methods,
	sizeof(struct aesni_softc),
};
static devclass_t aesni_devclass;

DRIVER_MODULE(aesni, nexus, aesni_driver, aesni_devclass, 0, 0);
MODULE_VERSION(aesni, 1);
MODULE_DEPEND(aesni, crypto, 1, 1, 1);

static int
aesni_authprepare(struct aesni_session *ses, int klen, const void *cri_key)
{
	int keylen;

	if (klen % 8 != 0)
		return (EINVAL);
	keylen = klen / 8;
	if (keylen > sizeof(ses->hmac_key))
		return (EINVAL);
	if (ses->auth_algo == CRYPTO_SHA1 && keylen > 0)
		return (EINVAL);
	memcpy(ses->hmac_key, cri_key, keylen);
	return (0);
}

static int
aesni_cipher_setup(struct aesni_session *ses, struct cryptoini *encini,
    struct cryptoini *authini)
{
	struct fpu_kern_ctx *ctx;
	int kt, ctxidx, error;

	switch (ses->auth_algo) {
	case CRYPTO_SHA1:
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_SHA2_224:
	case CRYPTO_SHA2_224_HMAC:
	case CRYPTO_SHA2_256:
	case CRYPTO_SHA2_256_HMAC:
		error = aesni_authprepare(ses, authini->cri_klen,
		    authini->cri_key);
		if (error != 0)
			return (error);
		ses->mlen = authini->cri_mlen;
	}

	kt = is_fpu_kern_thread(0) || (encini == NULL);
	if (!kt) {
		ACQUIRE_CTX(ctxidx, ctx);
		fpu_kern_enter(curthread, ctx,
		    FPU_KERN_NORMAL | FPU_KERN_KTHR);
	}

	error = 0;
	if (encini != NULL)
		error = aesni_cipher_setup_common(ses, encini->cri_key,
		    encini->cri_klen);

	if (!kt) {
		fpu_kern_leave(curthread, ctx);
		RELEASE_CTX(ctxidx, ctx);
	}
	return (error);
}

static int
intel_sha1_update(void *vctx, const void *vdata, u_int datalen)
{
	struct sha1_ctxt *ctx = vctx;
	const char *data = vdata;
	size_t gaplen;
	size_t gapstart;
	size_t off;
	size_t copysiz;
	u_int blocks;

	off = 0;
	/* Do any aligned blocks without redundant copying. */
	if (datalen >= 64 && ctx->count % 64 == 0) {
		blocks = datalen / 64;
		ctx->c.b64[0] += blocks * 64 * 8;
		intel_sha1_step(ctx->h.b32, data + off, blocks);
		off += blocks * 64;
	}

	while (off < datalen) {
		gapstart = ctx->count % 64;
		gaplen = 64 - gapstart;

		copysiz = (gaplen < datalen - off) ? gaplen : datalen - off;
		bcopy(&data[off], &ctx->m.b8[gapstart], copysiz);
		ctx->count += copysiz;
		ctx->count %= 64;
		ctx->c.b64[0] += copysiz * 8;
		if (ctx->count % 64 == 0)
			intel_sha1_step(ctx->h.b32, (void *)ctx->m.b8, 1);
		off += copysiz;
	}
	return (0);
}

static void
SHA1_Init_fn(void *ctx)
{
	sha1_init(ctx);
}

static void
SHA1_Finalize_fn(void *digest, void *ctx)
{
	sha1_result(ctx, digest);
}

static int
intel_sha256_update(void *vctx, const void *vdata, u_int len)
{
	SHA256_CTX *ctx = vctx;
	uint64_t bitlen;
	uint32_t r;
	u_int blocks;
	const unsigned char *src = vdata;

	/* Number of bytes left in the buffer from previous updates */
	r = (ctx->count >> 3) & 0x3f;

	/* Convert the length into a number of bits */
	bitlen = len << 3;

	/* Update number of bits */
	ctx->count += bitlen;

	/* Handle the case where we don't need to perform any transforms */
	if (len < 64 - r) {
		memcpy(&ctx->buf[r], src, len);
		return (0);
	}

	/* Finish the current block */
	memcpy(&ctx->buf[r], src, 64 - r);
	intel_sha256_step(ctx->state, ctx->buf, 1);
	src += 64 - r;
	len -= 64 - r;

	/* Perform complete blocks */
	if (len >= 64) {
		blocks = len / 64;
		intel_sha256_step(ctx->state, src, blocks);
		src += blocks * 64;
		len -= blocks * 64;
	}

	/* Copy left over data into buffer */
	memcpy(ctx->buf, src, len);
	return (0);
}

static void
SHA224_Init_fn(void *ctx)
{
	SHA224_Init(ctx);
}

static void
SHA224_Finalize_fn(void *digest, void *ctx)
{
	SHA224_Final(digest, ctx);
}

static void
SHA256_Init_fn(void *ctx)
{
	SHA256_Init(ctx);
}

static void
SHA256_Finalize_fn(void *digest, void *ctx)
{
	SHA256_Final(digest, ctx);
}

/*
 * Compute the HASH( (key ^ xorbyte) || buf )
 */
static void
hmac_internal(void *ctx, uint32_t *res,
	int (*update)(void *, const void *, u_int),
	void (*finalize)(void *, void *), uint8_t *key, uint8_t xorbyte,
	const void *buf, size_t off, size_t buflen, int crpflags)
{
	size_t i;

	for (i = 0; i < 64; i++)
		key[i] ^= xorbyte;
	update(ctx, key, 64);
	for (i = 0; i < 64; i++)
		key[i] ^= xorbyte;

	crypto_apply(crpflags, __DECONST(void *, buf), off, buflen,
	    __DECONST(int (*)(void *, void *, u_int), update), ctx);
	finalize(res, ctx);
}

static int
aesni_cipher_process(struct aesni_session *ses, struct cryptodesc *enccrd,
    struct cryptodesc *authcrd, struct cryptop *crp)
{
	struct fpu_kern_ctx *ctx;
	int error, ctxidx;
	bool kt;

	if (enccrd != NULL) {
		if ((enccrd->crd_alg == CRYPTO_AES_ICM ||
		    enccrd->crd_alg == CRYPTO_AES_NIST_GCM_16) &&
		    (enccrd->crd_flags & CRD_F_IV_EXPLICIT) == 0)
			return (EINVAL);
	}

	ctx = NULL;
	ctxidx = 0;
	error = 0;
	kt = is_fpu_kern_thread(0);
	if (!kt) {
		ACQUIRE_CTX(ctxidx, ctx);
		fpu_kern_enter(curthread, ctx,
		    FPU_KERN_NORMAL | FPU_KERN_KTHR);
	}

	/* Do work */
	if (enccrd != NULL && authcrd != NULL) {
		/* Perform the first operation */
		if (crp->crp_desc == enccrd)
			error = aesni_cipher_crypt(ses, enccrd, authcrd, crp);
		else
			error = aesni_cipher_mac(ses, authcrd, crp);
		if (error != 0)
			goto out;
		/* Perform the second operation */
		if (crp->crp_desc == enccrd)
			error = aesni_cipher_mac(ses, authcrd, crp);
		else
			error = aesni_cipher_crypt(ses, enccrd, authcrd, crp);
	} else if (enccrd != NULL)
		error = aesni_cipher_crypt(ses, enccrd, authcrd, crp);
	else
		error = aesni_cipher_mac(ses, authcrd, crp);

	if (error != 0)
		goto out;

out:
	if (!kt) {
		fpu_kern_leave(curthread, ctx);
		RELEASE_CTX(ctxidx, ctx);
	}
	return (error);
}

static int
aesni_cipher_crypt(struct aesni_session *ses, struct cryptodesc *enccrd,
	struct cryptodesc *authcrd, struct cryptop *crp)
{
	uint8_t iv[AES_BLOCK_LEN], tag[GMAC_DIGEST_LEN], *buf, *authbuf;
	int error, ivlen;
	bool encflag, allocated, authallocated;

	KASSERT(ses->algo != CRYPTO_AES_NIST_GCM_16 || authcrd != NULL,
	    ("AES_NIST_GCM_16 must include MAC descriptor"));

	ivlen = 0;
	authbuf = NULL;

	buf = aesni_cipher_alloc(enccrd, crp, &allocated);
	if (buf == NULL)
		return (ENOMEM);

	authallocated = false;
	if (ses->algo == CRYPTO_AES_NIST_GCM_16) {
		authbuf = aesni_cipher_alloc(authcrd, crp, &authallocated);
		if (authbuf == NULL) {
			error = ENOMEM;
			goto out;
		}
	}

	error = 0;
	encflag = (enccrd->crd_flags & CRD_F_ENCRYPT) == CRD_F_ENCRYPT;
	if ((enccrd->crd_flags & CRD_F_KEY_EXPLICIT) != 0) {
		error = aesni_cipher_setup_common(ses, enccrd->crd_key,
		    enccrd->crd_klen);
		if (error != 0)
			goto out;
	}

	switch (enccrd->crd_alg) {
	case CRYPTO_AES_CBC:
	case CRYPTO_AES_ICM:
		ivlen = AES_BLOCK_LEN;
		break;
	case CRYPTO_AES_XTS:
		ivlen = 8;
		break;
	case CRYPTO_AES_NIST_GCM_16:
		ivlen = 12;	/* should support arbitarily larger */
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

	switch (ses->algo) {
	case CRYPTO_AES_CBC:
		if (encflag)
			aesni_encrypt_cbc(ses->rounds, ses->enc_schedule,
			    enccrd->crd_len, buf, buf, iv);
		else
			aesni_decrypt_cbc(ses->rounds, ses->dec_schedule,
			    enccrd->crd_len, buf, iv);
		break;
	case CRYPTO_AES_ICM:
		/* encryption & decryption are the same */
		aesni_encrypt_icm(ses->rounds, ses->enc_schedule,
		    enccrd->crd_len, buf, buf, iv);
		break;
	case CRYPTO_AES_XTS:
		if (encflag)
			aesni_encrypt_xts(ses->rounds, ses->enc_schedule,
			    ses->xts_schedule, enccrd->crd_len, buf, buf,
			    iv);
		else
			aesni_decrypt_xts(ses->rounds, ses->dec_schedule,
			    ses->xts_schedule, enccrd->crd_len, buf, buf,
			    iv);
		break;
	case CRYPTO_AES_NIST_GCM_16:
		if (!encflag)
			crypto_copydata(crp->crp_flags, crp->crp_buf,
			    authcrd->crd_inject, GMAC_DIGEST_LEN, tag);
		else
			bzero(tag, sizeof tag);

		if (encflag) {
			AES_GCM_encrypt(buf, buf, authbuf, iv, tag,
			    enccrd->crd_len, authcrd->crd_len, ivlen,
			    ses->enc_schedule, ses->rounds);

			if (authcrd != NULL)
				crypto_copyback(crp->crp_flags, crp->crp_buf,
				    authcrd->crd_inject, GMAC_DIGEST_LEN, tag);
		} else {
			if (!AES_GCM_decrypt(buf, buf, authbuf, iv, tag,
			    enccrd->crd_len, authcrd->crd_len, ivlen,
			    ses->enc_schedule, ses->rounds))
				error = EBADMSG;
		}
		break;
	}

	if (allocated)
		crypto_copyback(crp->crp_flags, crp->crp_buf, enccrd->crd_skip,
		    enccrd->crd_len, buf);

out:
	if (allocated) {
		explicit_bzero(buf, enccrd->crd_len);
		free(buf, M_AESNI);
	}
	if (authallocated) {
		explicit_bzero(authbuf, authcrd->crd_len);
		free(authbuf, M_AESNI);
	}
	return (error);
}

static int
aesni_cipher_mac(struct aesni_session *ses, struct cryptodesc *crd,
    struct cryptop *crp)
{
	union {
		struct SHA256Context sha2 __aligned(16);
		struct sha1_ctxt sha1 __aligned(16);
	} sctx;
	uint32_t res[SHA2_256_HASH_LEN / sizeof(uint32_t)];
	int hashlen, error;
	void *ctx;
	void (*InitFn)(void *);
	int (*UpdateFn)(void *, const void *, unsigned);
	void (*FinalizeFn)(void *, void *);

	bool hmac;

	if ((crd->crd_flags & ~CRD_F_KEY_EXPLICIT) != 0) {
		CRYPTDEB("%s: Unsupported MAC flags: 0x%x", __func__,
		    (crd->crd_flags & ~CRD_F_KEY_EXPLICIT));
		return (EINVAL);
	}
	if ((crd->crd_flags & CRD_F_KEY_EXPLICIT) != 0) {
		error = aesni_authprepare(ses, crd->crd_klen, crd->crd_key);
		if (error != 0)
			return (error);
	}

	hmac = false;
	switch (ses->auth_algo) {
	case CRYPTO_SHA1_HMAC:
		hmac = true;
		/* FALLTHROUGH */
	case CRYPTO_SHA1:
		hashlen = SHA1_HASH_LEN;
		InitFn = SHA1_Init_fn;
		UpdateFn = intel_sha1_update;
		FinalizeFn = SHA1_Finalize_fn;
		ctx = &sctx.sha1;
		break;

	case CRYPTO_SHA2_256_HMAC:
		hmac = true;
		/* FALLTHROUGH */
	case CRYPTO_SHA2_256:
		hashlen = SHA2_256_HASH_LEN;
		InitFn = SHA256_Init_fn;
		UpdateFn = intel_sha256_update;
		FinalizeFn = SHA256_Finalize_fn;
		ctx = &sctx.sha2;
		break;

	case CRYPTO_SHA2_224_HMAC:
		hmac = true;
		/* FALLTHROUGH */
	case CRYPTO_SHA2_224:
		hashlen = SHA2_224_HASH_LEN;
		InitFn = SHA224_Init_fn;
		UpdateFn = intel_sha256_update;
		FinalizeFn = SHA224_Finalize_fn;
		ctx = &sctx.sha2;
		break;
	default:
		/*
		 * AES-GMAC authentication is verified while processing the
		 * enccrd
		 */
		return (0);
	}

	if (hmac) {
		/* Inner hash: (K ^ IPAD) || data */
		InitFn(ctx);
		hmac_internal(ctx, res, UpdateFn, FinalizeFn, ses->hmac_key,
		    0x36, crp->crp_buf, crd->crd_skip, crd->crd_len,
		    crp->crp_flags);
		/* Outer hash: (K ^ OPAD) || inner hash */
		InitFn(ctx);
		hmac_internal(ctx, res, UpdateFn, FinalizeFn, ses->hmac_key,
		    0x5C, res, 0, hashlen, 0);
	} else {
		InitFn(ctx);
		crypto_apply(crp->crp_flags, crp->crp_buf, crd->crd_skip,
		    crd->crd_len, __DECONST(int (*)(void *, void *, u_int),
		    UpdateFn), ctx);
		FinalizeFn(res, ctx);
	}

	if (ses->mlen != 0 && ses->mlen < hashlen)
		hashlen = ses->mlen;

	crypto_copyback(crp->crp_flags, crp->crp_buf, crd->crd_inject, hashlen,
	    (void *)res);
	return (0);
}
