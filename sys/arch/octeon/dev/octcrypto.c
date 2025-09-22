/*	$OpenBSD: octcrypto.c,v 1.8 2021/10/24 10:26:22 patrick Exp $	*/

/*
 * Copyright (c) 2018 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver for the OCTEON cryptographic unit.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/pool.h>
#include <sys/smr.h>
#include <sys/tree.h>

#include <crypto/cryptodev.h>
#include <crypto/cryptosoft.h>
#include <crypto/xform.h>

#include <mips64/mips_cpu.h>

#include <machine/octeonvar.h>

/* Maximum number of dwords in hash IV. */
#define MAX_IVNW		8

/* Number of dwords needed to cover `n' bytes. */
#define ndwords(n)		(roundup(n, 8) / (8))

struct octcrypto_softc;

struct octcrypto_hmac {
	void			(*transform)(const void *, size_t);
	void			(*get_iv)(uint64_t *);
	void			(*set_iv)(const uint64_t *);
	void			(*clear)(void);
	uint16_t		 blocklen;
	uint16_t		 taglen;
	uint16_t		 countlen;
};

struct octcrypto_session {
	uint32_t		 ses_sid;		/* RB key, keep first */
	RBT_ENTRY(octrcypto_session)
				 ses_entry;
	struct octcrypto_softc	*ses_sc;
	struct smr_entry	 ses_smr;

	/* AES parameters */
	uint64_t		 ses_key[4];
	int			 ses_klen;
	uint8_t			 ses_nonce[AESCTR_NONCESIZE];

	/* HMAC parameters */
	const struct octcrypto_hmac
				*ses_hmac;
	uint64_t		 ses_iiv[MAX_IVNW];	/* HMAC inner IV */
	uint64_t		 ses_oiv[MAX_IVNW];	/* HMAC outer IV */

	/* GHASH parameters */
	uint64_t		 ses_ghkey[2];

	struct swcr_data	*ses_swd;
};

struct octcrypto_cpu {
	uint8_t			*pcpu_buf;
	size_t			 pcpu_buflen;
};

struct octcrypto_softc {
	struct device		 sc_dev;
	int32_t			 sc_cid;
	uint32_t		 sc_sid;
	struct mutex		 sc_mtx;
	RBT_HEAD(octcrypto_tree, octcrypto_session)
				 sc_sessions;
	struct octcrypto_cpu	 sc_cpu[MAXCPUS];
};

int	octcrypto_match(struct device *, void *, void *);
void	octcrypto_attach(struct device *, struct device *, void *);

int	octcrypto_newsession(uint32_t *, struct cryptoini *);
int	octcrypto_freesession(uint64_t);
int	octcrypto_process(struct cryptop *);

struct octcrypto_session *
	octcrypto_get(struct octcrypto_softc *, uint32_t);
void	octcrypto_free(struct octcrypto_session *);
void	octcrypto_free_smr(void *);

void	octcrypto_hmac(struct cryptodesc *, uint8_t *, size_t,
	    struct octcrypto_session *, uint64_t *);
int	octcrypto_authenc_gmac(struct cryptop *, struct cryptodesc *,
	    struct cryptodesc *, struct octcrypto_session *);
int	octcrypto_authenc_hmac(struct cryptop *, struct cryptodesc *,
	    struct cryptodesc *, struct octcrypto_session *);
int	octcrypto_swauth(struct cryptop *, struct cryptodesc *,
	    struct swcr_data *, uint8_t *);

void	octcrypto_ghash_update_md(GHASH_CTX *, uint8_t *, size_t);

void	octcrypto_aes_clear(void);
void	octcrypto_aes_cbc_dec(void *, size_t, const void *);
void	octcrypto_aes_cbc_enc(void *, size_t, const void *);
void	octcrypto_aes_ctr_enc(void *, size_t, const void *);
void	octcrypto_aes_enc(uint64_t *);
void	octcrypto_aes_set_key(const uint64_t *, int);

void	octcrypto_ghash_finish(uint64_t *);
void	octcrypto_ghash_init(const uint64_t *, const uint64_t *);
void	octcrypto_ghash_update(const void *, size_t);

void	octcrypto_hash_md5(const void *, size_t);
void	octcrypto_hash_sha1(const void *, size_t);
void	octcrypto_hash_sha256(const void *, size_t);
void	octcrypto_hash_sha512(const void *, size_t);
void	octcrypto_hash_clearn(void);
void	octcrypto_hash_clearw(void);
void	octcrypto_hash_get_ivn(uint64_t *);
void	octcrypto_hash_get_ivw(uint64_t *);
void	octcrypto_hash_set_ivn(const uint64_t *);
void	octcrypto_hash_set_ivw(const uint64_t *);

const struct cfattach octcrypto_ca = {
	sizeof(struct octcrypto_softc), octcrypto_match, octcrypto_attach
};

struct cfdriver octcrypto_cd = {
	NULL, "octcrypto", DV_DULL
};

static const struct octcrypto_hmac hmac_md5_96 = {
	.transform = octcrypto_hash_md5,
	.get_iv = octcrypto_hash_get_ivn,
	.set_iv = octcrypto_hash_set_ivn,
	.clear = octcrypto_hash_clearn,
	.blocklen = 64,
	.taglen = 12,
	.countlen = 8
};

static const struct octcrypto_hmac hmac_sha1_96 = {
	.transform = octcrypto_hash_sha1,
	.get_iv = octcrypto_hash_get_ivn,
	.set_iv = octcrypto_hash_set_ivn,
	.clear = octcrypto_hash_clearn,
	.blocklen = 64,
	.taglen = 12,
	.countlen = 8
};

static const struct octcrypto_hmac hmac_sha2_256_128 = {
	.transform = octcrypto_hash_sha256,
	.get_iv = octcrypto_hash_get_ivn,
	.set_iv = octcrypto_hash_set_ivn,
	.clear = octcrypto_hash_clearn,
	.blocklen = 64,
	.taglen = 16,
	.countlen = 8
};

static const struct octcrypto_hmac hmac_sha2_384_192 = {
	.transform = octcrypto_hash_sha512,
	.get_iv = octcrypto_hash_get_ivw,
	.set_iv = octcrypto_hash_set_ivw,
	.clear = octcrypto_hash_clearw,
	.blocklen = 128,
	.taglen = 24,
	.countlen = 16
};

static const struct octcrypto_hmac hmac_sha2_512_256 = {
	.transform = octcrypto_hash_sha512,
	.get_iv = octcrypto_hash_get_ivw,
	.set_iv = octcrypto_hash_set_ivw,
	.clear = octcrypto_hash_clearw,
	.blocklen = 128,
	.taglen = 32,
	.countlen = 16
};

static struct pool		 octcryptopl;
static struct octcrypto_softc	*octcrypto_sc;

static inline int
octcrypto_cmp(const struct octcrypto_session *a,
    const struct octcrypto_session *b)
{
	if (a->ses_sid < b->ses_sid)
		return -1;
	if (a->ses_sid > b->ses_sid)
		return 1;
	return 0;
}

RBT_PROTOTYPE(octcrypto_tree, octcrypto_session, sess_entry, octcrypto_cmp);
RBT_GENERATE(octcrypto_tree, octcrypto_session, ses_entry, octcrypto_cmp);

static inline void
cop2_enable(void)
{
	setsr(getsr() | SR_COP_2_BIT);
}

static inline void
cop2_disable(void)
{
	setsr(getsr() & ~SR_COP_2_BIT);
}

int
octcrypto_match(struct device *parent, void *match, void *aux)
{
	return 1;
}

void
octcrypto_attach(struct device *parent, struct device *self, void *aux)
{
	int algs[CRYPTO_ALGORITHM_MAX + 1];
	struct octcrypto_softc *sc = (struct octcrypto_softc *)self;

	pool_init(&octcryptopl, sizeof(struct octcrypto_session), 0, IPL_VM, 0,
	    "octcrypto", NULL);
	pool_setlowat(&octcryptopl, 2);

	mtx_init(&sc->sc_mtx, IPL_VM);
	RBT_INIT(octcrypto_tree, &sc->sc_sessions);

	sc->sc_cid = crypto_get_driverid(CRYPTOCAP_F_MPSAFE);
	if (sc->sc_cid < 0) {
		printf(": could not get driver id\n");
		return;
	}

	printf("\n");

	memset(algs, 0, sizeof(algs));

	algs[CRYPTO_AES_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_CTR] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_GCM_16] = CRYPTO_ALG_FLAG_SUPPORTED;

	algs[CRYPTO_AES_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_128_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_192_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_AES_256_GMAC] = CRYPTO_ALG_FLAG_SUPPORTED;

	algs[CRYPTO_MD5_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA1_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_256_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_384_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_512_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;

	algs[CRYPTO_RIPEMD160_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;

	algs[CRYPTO_ESN] = CRYPTO_ALG_FLAG_SUPPORTED;

	octcrypto_sc = sc;

	crypto_register(sc->sc_cid, algs, octcrypto_newsession,
	    octcrypto_freesession, octcrypto_process);

	ghash_update = octcrypto_ghash_update_md;
}

struct octcrypto_session *
octcrypto_get(struct octcrypto_softc *sc, uint32_t sid)
{
	struct octcrypto_session *ses;

	SMR_ASSERT_CRITICAL();

	mtx_enter(&sc->sc_mtx);
	ses = RBT_FIND(octcrypto_tree, &sc->sc_sessions,
	    (struct octcrypto_session *)&sid);
	mtx_leave(&sc->sc_mtx);
	return ses;
}

void
octcrypto_free(struct octcrypto_session *ses)
{
	const struct auth_hash *axf;
	struct swcr_data *swd;

	if (ses->ses_swd != NULL) {
		swd = ses->ses_swd;
		axf = swd->sw_axf;

		if (swd->sw_ictx != NULL) {
			explicit_bzero(swd->sw_ictx, axf->ctxsize);
			free(swd->sw_ictx, M_CRYPTO_DATA, axf->ctxsize);
		}
		if (swd->sw_octx != NULL) {
			explicit_bzero(swd->sw_octx, axf->ctxsize);
			free(swd->sw_octx, M_CRYPTO_DATA, axf->ctxsize);
		}
		free(swd, M_CRYPTO_DATA, sizeof(*swd));
	}

	explicit_bzero(ses, sizeof(*ses));
	pool_put(&octcryptopl, ses);
}

void
octcrypto_free_smr(void *arg)
{
	struct octcrypto_session *ses = arg;

	octcrypto_free(ses);
}

int
octcrypto_newsession(uint32_t *sidp, struct cryptoini *cri)
{
	uint64_t block[ndwords(HMAC_MAX_BLOCK_LEN)];
	const struct auth_hash *axf;
	struct cryptoini *c;
	const struct octcrypto_hmac *hmac = NULL;
	struct octcrypto_softc *sc = octcrypto_sc;
	struct octcrypto_session *ses = NULL;
	struct swcr_data *swd;
	uint8_t *bptr;
	size_t klen;
	int i;
	uint32_t sid;

	if (sidp == NULL || cri == NULL)
		return EINVAL;

	ses = pool_get(&octcryptopl, PR_NOWAIT | PR_ZERO);
	if (ses == NULL)
		return ENOMEM;
	ses->ses_sc = sc;
	smr_init(&ses->ses_smr);

	for (c = cri; c != NULL; c = c->cri_next) {
		switch (c->cri_alg) {
		case CRYPTO_AES_CBC:
			ses->ses_klen = c->cri_klen / 8;
			memcpy(ses->ses_key, c->cri_key, ses->ses_klen);
			break;

		case CRYPTO_AES_CTR:
		case CRYPTO_AES_GCM_16:
		case CRYPTO_AES_GMAC:
			ses->ses_klen = c->cri_klen / 8 - AESCTR_NONCESIZE;
			memcpy(ses->ses_key, c->cri_key, ses->ses_klen);
			memcpy(ses->ses_nonce, c->cri_key + ses->ses_klen,
			    AESCTR_NONCESIZE);
			break;

		case CRYPTO_AES_128_GMAC:
		case CRYPTO_AES_192_GMAC:
		case CRYPTO_AES_256_GMAC:
			cop2_enable();
			octcrypto_aes_set_key(ses->ses_key, ses->ses_klen);
			octcrypto_aes_enc(ses->ses_ghkey);
			octcrypto_aes_clear();
			cop2_disable();
			break;

		case CRYPTO_MD5_HMAC:
			ses->ses_iiv[0] = 0x0123456789abcdefULL;
			ses->ses_iiv[1] = 0xfedcba9876543210ULL;
			ses->ses_hmac = &hmac_md5_96;
			goto hwauthcommon;

		case CRYPTO_SHA1_HMAC:
			ses->ses_iiv[0] = 0x67452301efcdab89ULL;
			ses->ses_iiv[1] = 0x98badcfe10325476ULL;
			ses->ses_iiv[2] = 0xc3d2e1f000000000ULL;
			ses->ses_hmac = &hmac_sha1_96;
			goto hwauthcommon;

		case CRYPTO_SHA2_256_HMAC:
			ses->ses_iiv[0] = 0x6a09e667bb67ae85ULL;
			ses->ses_iiv[1] = 0x3c6ef372a54ff53aULL;
			ses->ses_iiv[2] = 0x510e527f9b05688cULL;
			ses->ses_iiv[3] = 0x1f83d9ab5be0cd19ULL;
			ses->ses_hmac = &hmac_sha2_256_128;
			goto hwauthcommon;

		case CRYPTO_SHA2_384_HMAC:
			ses->ses_iiv[0] = 0xcbbb9d5dc1059ed8ULL;
			ses->ses_iiv[1] = 0x629a292a367cd507ULL;
			ses->ses_iiv[2] = 0x9159015a3070dd17ULL;
			ses->ses_iiv[3] = 0x152fecd8f70e5939ULL;
			ses->ses_iiv[4] = 0x67332667ffc00b31ULL;
			ses->ses_iiv[5] = 0x8eb44a8768581511ULL;
			ses->ses_iiv[6] = 0xdb0c2e0d64f98fa7ULL;
			ses->ses_iiv[7] = 0x47b5481dbefa4fa4ULL;
			ses->ses_hmac = &hmac_sha2_384_192;
			goto hwauthcommon;

		case CRYPTO_SHA2_512_HMAC:
			ses->ses_iiv[0] = 0x6a09e667f3bcc908ULL;
			ses->ses_iiv[1] = 0xbb67ae8584caa73bULL;
			ses->ses_iiv[2] = 0x3c6ef372fe94f82bULL;
			ses->ses_iiv[3] = 0xa54ff53a5f1d36f1ULL;
			ses->ses_iiv[4] = 0x510e527fade682d1ULL;
			ses->ses_iiv[5] = 0x9b05688c2b3e6c1fULL;
			ses->ses_iiv[6] = 0x1f83d9abfb41bd6bULL;
			ses->ses_iiv[7] = 0x5be0cd19137e2179ULL;
			ses->ses_hmac = &hmac_sha2_512_256;

		hwauthcommon:
			memcpy(ses->ses_oiv, ses->ses_iiv,
			    sizeof(uint64_t) * MAX_IVNW);

			bptr = (char *)block;
			klen = c->cri_klen / 8;
			hmac = ses->ses_hmac;

			memcpy(bptr, c->cri_key, klen);
			memset(bptr + klen, 0, hmac->blocklen - klen);
			for (i = 0; i < hmac->blocklen; i++)
				bptr[i] ^= HMAC_IPAD_VAL;

			cop2_enable();
			hmac->set_iv(ses->ses_iiv);
			hmac->transform(block, hmac->blocklen);
			hmac->get_iv(ses->ses_iiv);

			for (i = 0; i < hmac->blocklen; i++)
				bptr[i] ^= (HMAC_IPAD_VAL ^ HMAC_OPAD_VAL);

			hmac->set_iv(ses->ses_oiv);
			hmac->transform(block, hmac->blocklen);
			hmac->get_iv(ses->ses_oiv);
			hmac->clear();
			cop2_disable();

			explicit_bzero(block, hmac->blocklen);
			break;

		case CRYPTO_RIPEMD160_HMAC:
			axf = &auth_hash_hmac_ripemd_160_96;
			goto swauthcommon;

		swauthcommon:
			swd = malloc(sizeof(struct swcr_data), M_CRYPTO_DATA,
			    M_NOWAIT | M_ZERO);
			if (swd == NULL) {
				octcrypto_free(ses);
				return ENOMEM;
			}
			ses->ses_swd = swd;

			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				octcrypto_free(ses);
				return ENOMEM;
			}

			swd->sw_octx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_octx == NULL) {
				octcrypto_free(ses);
				return ENOMEM;
			}

			for (i = 0; i < c->cri_klen / 8; i++)
				c->cri_key[i] ^= HMAC_IPAD_VAL;

			axf->Init(swd->sw_ictx);
			axf->Update(swd->sw_ictx, c->cri_key, c->cri_klen / 8);
			axf->Update(swd->sw_ictx, hmac_ipad_buffer,
			    axf->blocksize - (c->cri_klen / 8));

			for (i = 0; i < c->cri_klen / 8; i++)
				c->cri_key[i] ^= (HMAC_IPAD_VAL ^
				    HMAC_OPAD_VAL);

			axf->Init(swd->sw_octx);
			axf->Update(swd->sw_octx, c->cri_key, c->cri_klen / 8);
			axf->Update(swd->sw_octx, hmac_opad_buffer,
			    axf->blocksize - (c->cri_klen / 8));

			for (i = 0; i < c->cri_klen / 8; i++)
				c->cri_key[i] ^= HMAC_OPAD_VAL;

			swd->sw_axf = axf;
			swd->sw_alg = c->cri_alg;

			break;

		case CRYPTO_ESN:
			/* nothing to do */
			break;

		default:
			octcrypto_free(ses);
			return EINVAL;
		}
	}

	mtx_enter(&sc->sc_mtx);
	/* Find a free session ID. Assume there is one. */
	do {
		sc->sc_sid++;
		if (sc->sc_sid == 0)
			sc->sc_sid = 1;
		sid = sc->sc_sid;
	} while (RBT_FIND(octcrypto_tree, &sc->sc_sessions,
	    (struct octcrypto_session *)&sid) != NULL);
	ses->ses_sid = sid;
	RBT_INSERT(octcrypto_tree, &sc->sc_sessions, ses);
	mtx_leave(&sc->sc_mtx);

	*sidp = ses->ses_sid;
	return 0;
}

int
octcrypto_freesession(uint64_t tid)
{
	struct octcrypto_softc *sc = octcrypto_sc;
	struct octcrypto_session *ses;
	uint32_t sid = (uint32_t)tid;

	mtx_enter(&sc->sc_mtx);
	ses = RBT_FIND(octcrypto_tree, &sc->sc_sessions,
	    (struct octcrypto_session *)&sid);
	if (ses != NULL)
		RBT_REMOVE(octcrypto_tree, &sc->sc_sessions, ses);
	mtx_leave(&sc->sc_mtx);

	if (ses == NULL)
		return EINVAL;

	smr_call(&ses->ses_smr, octcrypto_free_smr, ses);

	return 0;
}

enum {
	ALG_UNHANDLED,
	ALG_AES,
	ALG_AES_GHASH,
	ALG_GMAC,
	ALG_HMAC
};

static int
alg_class(int alg)
{
	switch (alg) {
	case CRYPTO_AES_CBC:
	case CRYPTO_AES_CTR:
		return ALG_AES;
	case CRYPTO_AES_GCM_16:
	case CRYPTO_AES_GMAC:
		return ALG_AES_GHASH;
	case CRYPTO_AES_128_GMAC:
	case CRYPTO_AES_192_GMAC:
	case CRYPTO_AES_256_GMAC:
		return ALG_GMAC;
	case CRYPTO_MD5_HMAC:
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_SHA2_256_HMAC:
	case CRYPTO_SHA2_384_HMAC:
	case CRYPTO_SHA2_512_HMAC:
		return ALG_HMAC;
	default:
		return ALG_UNHANDLED;
	}
}

int
octcrypto_process(struct cryptop *crp)
{
	struct cryptodesc *crd, *crd2;
	struct octcrypto_softc *sc = octcrypto_sc;
	struct octcrypto_session *ses = NULL;
	int alg, alg2;
	int error = 0;
	int i;

	KASSERT(crp->crp_ndesc >= 1);

	smr_read_enter();
	ses = octcrypto_get(sc, (uint32_t)crp->crp_sid);
	if (ses == NULL) {
		error = EINVAL;
		goto out;
	}

	if (crp->crp_ndesc == 2) {
		crd = &crp->crp_desc[0];
		crd2 = &crp->crp_desc[1];
		alg = alg_class(crd->crd_alg);
		alg2 = alg_class(crd2->crd_alg);

		if ((alg == ALG_AES) && (alg2 == ALG_HMAC)) {
			error = octcrypto_authenc_hmac(crp, crd, crd2, ses);
			goto out;
		} else if ((alg2 == ALG_AES) && (alg == ALG_HMAC)) {
			error = octcrypto_authenc_hmac(crp, crd2, crd, ses);
			goto out;
		} else if ((alg == ALG_AES_GHASH) && (alg2 == ALG_GMAC)) {
			error = octcrypto_authenc_gmac(crp, crd, crd2, ses);
			goto out;
		} else if ((alg2 == ALG_AES_GHASH) && (alg == ALG_GMAC)) {
			error = octcrypto_authenc_gmac(crp, crd2, crd, ses);
			goto out;
		}
	}

	for (i = 0; i < crp->crp_ndesc; i++) {
		crd = &crp->crp_desc[i];
		switch (crd->crd_alg) {
		case CRYPTO_AES_CBC:
		case CRYPTO_AES_CTR:
			error = octcrypto_authenc_hmac(crp, crd, NULL, ses);
			break;

		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			error = octcrypto_authenc_hmac(crp, NULL, crd, ses);
			break;

		case CRYPTO_RIPEMD160_HMAC:
			error = octcrypto_swauth(crp, crd, ses->ses_swd,
			    crp->crp_buf);
			break;

		default:
			error = EINVAL;
			break;
		}
	}

out:
	smr_read_leave();
	return error;
}

int
octcrypto_swauth(struct cryptop *crp, struct cryptodesc *crd,
    struct swcr_data *sw, uint8_t *buf)
{
	int type;

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		type = CRYPTO_BUF_MBUF;
	else
		type = CRYPTO_BUF_IOV;

	return swcr_authcompute(crp, crd, sw, buf, type);
}

int
octcrypto_authenc_gmac(struct cryptop *crp, struct cryptodesc *crde,
    struct cryptodesc *crda, struct octcrypto_session *ses)
{
	uint64_t block[ndwords(AESCTR_BLOCKSIZE)];
	uint64_t icb[ndwords(AESCTR_BLOCKSIZE)];
	uint64_t iv[ndwords(AESCTR_BLOCKSIZE)];
	uint64_t tag[ndwords(GMAC_BLOCK_LEN)];
	uint8_t *buf;
	struct octcrypto_cpu *pcpu = &ses->ses_sc->sc_cpu[cpu_number()];
	size_t aadlen;
	size_t ivlen = 8;
	size_t rlen;
	int error = 0;
	unsigned int iskip = 0;
	unsigned int oskip = 0;

	KASSERT(crda != NULL);
	KASSERT(crde != NULL);

	rlen = roundup(crde->crd_len, AESCTR_BLOCKSIZE);
	if (rlen > pcpu->pcpu_buflen) {
		if (pcpu->pcpu_buf != NULL) {
			explicit_bzero(pcpu->pcpu_buf, pcpu->pcpu_buflen);
			free(pcpu->pcpu_buf, M_DEVBUF, pcpu->pcpu_buflen);
		}
		pcpu->pcpu_buflen = 0;
		pcpu->pcpu_buf = malloc(rlen, M_DEVBUF, M_NOWAIT | M_ZERO);
		if (pcpu->pcpu_buf == NULL)
			return ENOMEM;
		pcpu->pcpu_buflen = rlen;
	}
	buf = pcpu->pcpu_buf;

	/* Prepare the IV. */
	if (crde->crd_flags & CRD_F_ENCRYPT) {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crde->crd_iv, ivlen);
		else
			arc4random_buf(iv, ivlen);

		if ((crde->crd_flags & CRD_F_IV_PRESENT) == 0) {
			if (crp->crp_flags & CRYPTO_F_IMBUF) {
				if (m_copyback((struct mbuf *)crp->crp_buf,
				    crde->crd_inject, ivlen, (uint8_t *)iv,
				    M_NOWAIT)) {
					error = ENOMEM;
					goto out;
				}
			} else {
				cuio_copyback((struct uio *)crp->crp_buf,
				    crde->crd_inject, ivlen, (uint8_t *)iv);
			}
		}
	} else {
		if (crde->crd_flags & CRD_F_IV_EXPLICIT) {
			memcpy(iv, crde->crd_iv, ivlen);
		} else {
			if (crp->crp_flags & CRYPTO_F_IMBUF)
				m_copydata((struct mbuf *)crp->crp_buf,
				    crde->crd_inject, ivlen, iv);
			else
				cuio_copydata((struct uio *)crp->crp_buf,
				    crde->crd_inject, ivlen, (uint8_t *)iv);
		}
	}

	memset(icb, 0, sizeof(icb));
	memcpy(icb, ses->ses_nonce, AESCTR_NONCESIZE);
	memcpy((uint8_t *)icb + AESCTR_NONCESIZE, iv, AESCTR_IVSIZE);
	((uint8_t *)icb)[AESCTR_BLOCKSIZE - 1] = 1;

	/* Prepare the AAD. */
	aadlen = crda->crd_len;
	if (crda->crd_flags & CRD_F_ESN) {
		aadlen += 4;
		if (crp->crp_flags & CRYPTO_F_IMBUF)
			m_copydata((struct mbuf *)crp->crp_buf,
			    crda->crd_skip, 4, buf);
		else
			cuio_copydata((struct uio *)crp->crp_buf,
			    crda->crd_skip, 4, buf);
		memcpy(buf + 4, crda->crd_esn, 4);
		iskip = 4;
		oskip = 8;
	}
	if (crp->crp_flags & CRYPTO_F_IMBUF)
		m_copydata((struct mbuf *)crp->crp_buf,
		    crda->crd_skip + iskip, crda->crd_len - iskip, buf + oskip);
	else
		cuio_copydata((struct uio *)crp->crp_buf,
		    crda->crd_skip + iskip, crda->crd_len - iskip, buf + oskip);

	cop2_enable();
	octcrypto_ghash_init(ses->ses_ghkey, NULL);
	octcrypto_ghash_update(buf, roundup(aadlen, GMAC_BLOCK_LEN));
	cop2_disable();

	memset(buf, 0, aadlen);

	/* Copy input to the working buffer. */
	if (crp->crp_flags & CRYPTO_F_IMBUF)
		m_copydata((struct mbuf *)crp->crp_buf, crde->crd_skip,
		    crde->crd_len, buf);
	else
		cuio_copydata((struct uio *)crp->crp_buf, crde->crd_skip,
		    crde->crd_len, buf);

	cop2_enable();
	octcrypto_aes_set_key(ses->ses_key, ses->ses_klen);

	switch (crde->crd_alg) {
	case CRYPTO_AES_GCM_16:
		if (crde->crd_flags & CRD_F_ENCRYPT) {
			octcrypto_aes_ctr_enc(buf, rlen, icb);
			memset(buf + crde->crd_len, 0, rlen - crde->crd_len);
			octcrypto_ghash_update(buf, rlen);
		} else {
			octcrypto_ghash_update(buf, rlen);
			octcrypto_aes_ctr_enc(buf, rlen, icb);
		}
		break;

	case CRYPTO_AES_GMAC:
		octcrypto_ghash_update(buf, rlen);
		break;
	}

	block[0] = htobe64(aadlen * 8);
	block[1] = htobe64(crde->crd_len * 8);
	octcrypto_ghash_update(block, GMAC_BLOCK_LEN);
	octcrypto_ghash_finish(tag);

	block[0] = icb[0];
	block[1] = icb[1];
	octcrypto_aes_enc(block);
	tag[0] ^= block[0];
	tag[1] ^= block[1];

	octcrypto_aes_clear();
	cop2_disable();

	/* Copy back the output. */
	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		if (m_copyback((struct mbuf *)crp->crp_buf,
		    crde->crd_skip, crde->crd_len, buf, M_NOWAIT)) {
			error = ENOMEM;
			goto out;
		}
	} else {
		cuio_copyback((struct uio *)crp->crp_buf,
		    crde->crd_skip, crde->crd_len, buf);
	}

	/* Copy back the authentication tag. */
	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		if (m_copyback((struct mbuf *)crp->crp_buf, crda->crd_inject,
		    GMAC_DIGEST_LEN, tag, M_NOWAIT)) {
			error = ENOMEM;
			goto out;
		}
	} else {
		memcpy(crp->crp_mac, tag, GMAC_DIGEST_LEN);
	}

out:
	explicit_bzero(buf, rlen);
	explicit_bzero(icb, sizeof(icb));
	explicit_bzero(tag, sizeof(tag));

	return error;
}

void
octcrypto_hmac(struct cryptodesc *crda, uint8_t *buf, size_t len,
    struct octcrypto_session *ses, uint64_t *res)
{
	uint64_t block[ndwords(HMAC_MAX_BLOCK_LEN)];
	uint8_t *bptr = (uint8_t *)block;
	const struct octcrypto_hmac *hmac = ses->ses_hmac;
	size_t left;

	cop2_enable();

	/*
	 * Compute the inner hash.
	 */

	hmac->set_iv(ses->ses_iiv);
	hmac->transform(buf, len);

	memset(block, 0, hmac->blocklen);
	left = len & (hmac->blocklen - 1);
	bptr[left] = 0x80;
	if (left > 0) {
		memcpy(block, buf + len - left, left);

		if (roundup(left + 1, hmac->countlen) >
		    (hmac->blocklen - hmac->countlen)) {
			hmac->transform(block, hmac->blocklen);
			memset(block, 0, hmac->blocklen);
		}
	}

	switch (crda->crd_alg) {
	case CRYPTO_MD5_HMAC:
		block[7] = htole64((64 + len) * 8);
		break;
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_SHA2_256_HMAC:
		block[7] = htobe64((64 + len) * 8);
		break;
	case CRYPTO_SHA2_384_HMAC:
	case CRYPTO_SHA2_512_HMAC:
		block[15] = htobe64((128 + len) * 8);
		break;
	}

	hmac->transform(block, hmac->blocklen);

	/*
	 * Compute the outer hash.
	 */

	memset(block, 0, hmac->blocklen);
	hmac->get_iv(block);
	hmac->set_iv(ses->ses_oiv);

	switch (crda->crd_alg) {
	case CRYPTO_MD5_HMAC:
		block[2] = htobe64(1ULL << 63);
		block[7] = htole64((64 + 16) * 8);
		break;
	case CRYPTO_SHA1_HMAC:
		block[2] |= htobe64(1ULL << 31);
		block[7] = htobe64((64 + 20) * 8);
		break;
	case CRYPTO_SHA2_256_HMAC:
		block[4] = htobe64(1ULL << 63);
		block[7] = htobe64((64 + 32) * 8);
		break;
	case CRYPTO_SHA2_384_HMAC:
		/*
		 * The computed digest is 512 bits long.
		 * It has to be truncated to 384 bits.
		 */
		block[6] = htobe64(1ULL << 63);
		block[7] = 0;	/* truncation */
		block[15] = htobe64((128 + 48) * 8);
		break;
	case CRYPTO_SHA2_512_HMAC:
		block[8] = htobe64(1ULL << 63);
		block[15] = htobe64((128 + 64) * 8);
		break;
	}

	hmac->transform(block, hmac->blocklen);
	hmac->get_iv(res);
	hmac->clear();

	cop2_disable();

	explicit_bzero(block, sizeof(block));
}

int
octcrypto_authenc_hmac(struct cryptop *crp, struct cryptodesc *crde,
    struct cryptodesc *crda, struct octcrypto_session *ses)
{
	uint64_t icb[ndwords(AESCTR_BLOCKSIZE)];
	uint64_t iv[ndwords(EALG_MAX_BLOCK_LEN)];
	uint64_t tag[ndwords(AALG_MAX_RESULT_LEN)];
	struct octcrypto_cpu *pcpu = &ses->ses_sc->sc_cpu[cpu_number()];
	uint8_t *buf, *authbuf, *encbuf;
	size_t authlen;
	size_t buflen;
	size_t len;
	size_t skip;
	off_t authskip = 0;
	off_t encskip = 0;
	int error = 0;
	int ivlen;

	if (crde != NULL && crda != NULL) {
		skip = MIN(crde->crd_skip, crda->crd_skip);
		len = MAX(crde->crd_skip + crde->crd_len,
		    crda->crd_skip + crda->crd_len) - skip;

		if (crda->crd_skip < crde->crd_skip)
			encskip = crde->crd_skip - crda->crd_skip;
		else
			authskip = crda->crd_skip - crde->crd_skip;
	} else if (crde != NULL) {
		skip = crde->crd_skip;
		len = crde->crd_len;
	} else {
		KASSERT(crda != NULL);

		skip = crda->crd_skip;
		len = crda->crd_len;
	}

	buflen = len;

	/* Reserve space for ESN. */
	if (crda != NULL && (crda->crd_flags & CRD_F_ESN) != 0)
		buflen += 4;

	buflen = roundup(buflen, EALG_MAX_BLOCK_LEN);
	if (buflen > pcpu->pcpu_buflen) {
		if (pcpu->pcpu_buf != NULL) {
			explicit_bzero(pcpu->pcpu_buf, pcpu->pcpu_buflen);
			free(pcpu->pcpu_buf, M_DEVBUF, pcpu->pcpu_buflen);
		}
		pcpu->pcpu_buflen = 0;
		pcpu->pcpu_buf = malloc(buflen, M_DEVBUF, M_NOWAIT | M_ZERO);
		if (pcpu->pcpu_buf == NULL)
			return ENOMEM;
		pcpu->pcpu_buflen = buflen;
	}
	buf = pcpu->pcpu_buf;

	authbuf = buf + authskip;
	encbuf = buf + encskip;

	/* Prepare the IV. */
	if (crde != NULL) {
		/* CBC uses 16 bytes, CTR 8 bytes. */
		ivlen = (crde->crd_alg == CRYPTO_AES_CBC) ? 16 : 8;

		if (crde->crd_flags & CRD_F_ENCRYPT) {
			if (crde->crd_flags & CRD_F_IV_EXPLICIT)
				memcpy(iv, crde->crd_iv, ivlen);
			else
				arc4random_buf(iv, ivlen);

			if ((crde->crd_flags & CRD_F_IV_PRESENT) == 0) {
				if (crp->crp_flags & CRYPTO_F_IMBUF) {
					if (m_copyback(
					    (struct mbuf *)crp->crp_buf,
					    crde->crd_inject, ivlen, iv,
					    M_NOWAIT)) {
						error = ENOMEM;
						goto out;
					}
				} else {
					cuio_copyback(
					    (struct uio *)crp->crp_buf,
					    crde->crd_inject, ivlen, iv);
				}
			}
		} else {
			if (crde->crd_flags & CRD_F_IV_EXPLICIT) {
				memcpy(iv, crde->crd_iv, ivlen);
			} else {
				if (crp->crp_flags & CRYPTO_F_IMBUF)
					m_copydata((struct mbuf *)crp->crp_buf,
					    crde->crd_inject, ivlen, iv);
				else
					cuio_copydata(
					    (struct uio *)crp->crp_buf,
					    crde->crd_inject, ivlen,
					    (uint8_t *)iv);
			}
		}
	}

	/* Copy input to the working buffer. */
	if (crp->crp_flags & CRYPTO_F_IMBUF)
		m_copydata((struct mbuf *)crp->crp_buf, skip, len, buf);
	else
		cuio_copydata((struct uio *)crp->crp_buf, skip, len, buf);

	/* If ESN is used, append it to the buffer. */
	if (crda != NULL) {
		authlen = crda->crd_len;
		if (crda->crd_flags & CRD_F_ESN) {
			memcpy(buf + len, crda->crd_esn, 4);
			authlen += 4;
		}
	}

	if (crde != NULL) {
		/* Compute authentication tag before decryption. */
		if (crda != NULL && (crde->crd_flags & CRD_F_ENCRYPT) == 0)
			octcrypto_hmac(crda, authbuf, authlen, ses, tag);

		/* Apply the cipher. */
		switch (crde->crd_alg) {
		case CRYPTO_AES_CBC:
			cop2_enable();
			octcrypto_aes_set_key(ses->ses_key, ses->ses_klen);
			if (crde->crd_flags & CRD_F_ENCRYPT)
				octcrypto_aes_cbc_enc(encbuf, crde->crd_len,
				    iv);
			else
				octcrypto_aes_cbc_dec(encbuf, crde->crd_len,
				    iv);
			octcrypto_aes_clear();
			cop2_disable();
			break;

		case CRYPTO_AES_CTR:
			memset(icb, 0, sizeof(icb));
			memcpy(icb, ses->ses_nonce, AESCTR_NONCESIZE);
			memcpy((uint8_t *)icb + AESCTR_NONCESIZE, iv,
			    AESCTR_IVSIZE);
			cop2_enable();
			octcrypto_aes_set_key(ses->ses_key, ses->ses_klen);
			octcrypto_aes_ctr_enc(encbuf, crde->crd_len, icb);
			octcrypto_aes_clear();
			cop2_disable();
			explicit_bzero(icb, sizeof(icb));
			break;
		}

		/* Copy back the output. */
		if (crp->crp_flags & CRYPTO_F_IMBUF) {
			if (m_copyback((struct mbuf *)crp->crp_buf,
			    crde->crd_skip, crde->crd_len, encbuf, M_NOWAIT)) {
				error = ENOMEM;
				goto out;
			}
		} else {
			cuio_copyback((struct uio *)crp->crp_buf,
			    crde->crd_skip, crde->crd_len, encbuf);
		}
	}

	if (crda != NULL) {
		/*
		 * Compute authentication tag after encryption.
		 * This also handles the authentication only case.
		 */
		if (crde == NULL || (crde->crd_flags & CRD_F_ENCRYPT) != 0)
			octcrypto_hmac(crda, authbuf, authlen, ses, tag);

		/* Copy back the authentication tag. */
		if (crp->crp_flags & CRYPTO_F_IMBUF) {
			if (m_copyback((struct mbuf *)crp->crp_buf,
			    crda->crd_inject, ses->ses_hmac->taglen, tag,
			    M_NOWAIT)) {
				error = ENOMEM;
				goto out;
			}
		} else {
			memcpy(crp->crp_mac, tag, ses->ses_hmac->taglen);
		}

		explicit_bzero(tag, sizeof(tag));
	}

out:
	explicit_bzero(buf, len);
	return error;
}

void
octcrypto_ghash_update_md(GHASH_CTX *ghash, uint8_t *src, size_t len)
{
	CTASSERT(offsetof(GHASH_CTX, H) % 8 == 0);
	CTASSERT(offsetof(GHASH_CTX, S) % 8 == 0);

	cop2_enable();
	octcrypto_ghash_init((uint64_t *)ghash->H, (uint64_t *)ghash->S);
	octcrypto_ghash_update(src, len);
	octcrypto_ghash_finish((uint64_t *)ghash->S);
	cop2_disable();
}
