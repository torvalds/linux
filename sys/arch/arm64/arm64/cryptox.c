/*	$OpenBSD: cryptox.c,v 1.6 2022/01/01 18:52:36 kettenis Exp $	*/
/*
 * Copyright (c) 2003 Jason Wright
 * Copyright (c) 2003, 2004 Theo de Raadt
 * Copyright (c) 2010 Thordur I. Bjornsson
 * Copyright (c) 2010 Mike Belopuhov
 * Copyright (c) 2020 Tobias Heider
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/mbuf.h>
#include <sys/smr.h>

#include <crypto/cryptodev.h>
#include <crypto/aes.h>
#include <crypto/gmac.h>
#include <crypto/xform.h>
#include <crypto/cryptosoft.h>

#include <machine/fpu.h>

struct cryptox_aes_key {
	uint32_t rd_key[4 *(AES_MAXROUNDS + 1)];
	int rounds;
};

struct cryptox_session {
	struct cryptox_aes_key	 ses_ekey;
	struct cryptox_aes_key	 ses_dkey;
	uint32_t		 ses_klen;
	int			 ses_sid;
	struct swcr_data	*ses_swd;
	SMR_LIST_ENTRY(cryptox_session)
				 ses_entries;
	uint8_t			*ses_buf;
	size_t			 ses_buflen;
	struct smr_entry	 ses_smr;
};

struct cryptox_softc {
	int32_t			 sc_cid;
	uint32_t		 sc_sid;
	struct mutex		 sc_mtx;
	SMR_LIST_HEAD(, cryptox_session)
				 sc_sessions;
} *cryptox_sc;

struct pool cryptoxpl;

uint32_t cryptox_ops;

extern int aes_v8_set_encrypt_key(const uint8_t *user_key, const int bits,
	    struct cryptox_aes_key *key);
extern int aes_v8_set_decrypt_key(const uint8_t *user_key, const int bits,
	    struct cryptox_aes_key *key);
extern void aes_v8_encrypt(const uint8_t *in, uint8_t *out,
	    const struct cryptox_aes_key *key);
extern void aes_v8_decrypt(const uint8_t *in, uint8_t *out,
	    const struct cryptox_aes_key *key);
extern void aes_v8_cbc_encrypt(const uint8_t *in, uint8_t *out, size_t length,
	    const struct cryptox_aes_key *key, uint8_t *ivec, const int enc);
extern void aes_v8_ctr32_encrypt_blocks(const uint8_t *in, uint8_t *out,
	    size_t len, const struct cryptox_aes_key *key,
	    const uint8_t ivec[16]);

void	cryptox_setup(void);
int	cryptox_newsession(u_int32_t *, struct cryptoini *);
int	cryptox_freesession(u_int64_t);
int	cryptox_process(struct cryptop *);

struct cryptox_session *
	cryptox_get(uint32_t);
void	cryptox_free(struct cryptox_session *);
void	cryptox_free_smr(void *);

int	cryptox_swauth(struct cryptop *, struct cryptodesc *, struct swcr_data *,
	    caddr_t);

int	cryptox_encdec(struct cryptop *, struct cryptodesc *,
	    struct cryptox_session *);

void
cryptox_setup(void)
{
	int algs[CRYPTO_ALGORITHM_MAX + 1];

	cryptox_sc = malloc(sizeof(*cryptox_sc), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (cryptox_sc == NULL)
		return;

	bzero(algs, sizeof(algs));

	/* Encryption algorithms. */
	algs[CRYPTO_AES_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;

	/* HMACs needed for IPsec, uses software crypto. */
	algs[CRYPTO_MD5_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA1_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_RIPEMD160_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_256_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_384_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_512_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;

	/* IPsec Extended Sequence Numbers. */
	algs[CRYPTO_ESN] = CRYPTO_ALG_FLAG_SUPPORTED;

	cryptox_sc->sc_cid = crypto_get_driverid(CRYPTOCAP_F_MPSAFE);
	if (cryptox_sc->sc_cid < 0) {
		free(cryptox_sc, M_DEVBUF, sizeof(*cryptox_sc));
		cryptox_sc = NULL;
		return;
	}

	pool_init(&cryptoxpl, sizeof(struct cryptox_session), 16, IPL_VM, 0,
	    "cryptox", NULL);
	pool_setlowat(&cryptoxpl, 2);

	mtx_init(&cryptox_sc->sc_mtx, IPL_VM);

	crypto_register(cryptox_sc->sc_cid, algs, cryptox_newsession,
	    cryptox_freesession, cryptox_process);
}

int
cryptox_newsession(u_int32_t *sidp, struct cryptoini *cri)
{
	struct cryptox_session *ses = NULL;
	struct cryptoini *c;
	const struct auth_hash *axf;
	struct swcr_data *swd;
	int i;

	if (sidp == NULL || cri == NULL)
		return (EINVAL);

	ses = pool_get(&cryptoxpl, PR_NOWAIT | PR_ZERO);
	if (!ses)
		return (ENOMEM);
	smr_init(&ses->ses_smr);

	ses->ses_buf = malloc(PAGE_SIZE, M_DEVBUF, M_NOWAIT|M_ZERO);
	if (ses->ses_buf != NULL)
		ses->ses_buflen = PAGE_SIZE;

	for (c = cri; c != NULL; c = c->cri_next) {
		switch (c->cri_alg) {
		case CRYPTO_AES_CBC:
			ses->ses_klen = c->cri_klen / 8;
			fpu_kernel_enter();
			aes_v8_set_encrypt_key(c->cri_key, c->cri_klen, &ses->ses_ekey);
			aes_v8_set_decrypt_key(c->cri_key, c->cri_klen, &ses->ses_dkey);
			fpu_kernel_exit();
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
		authcommon:
			swd = malloc(sizeof(struct swcr_data), M_CRYPTO_DATA,
			    M_NOWAIT|M_ZERO);
			if (swd == NULL) {
				cryptox_free(ses);
				return (ENOMEM);
			}
			ses->ses_swd = swd;

			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				cryptox_free(ses);
				return (ENOMEM);
			}

			swd->sw_octx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_octx == NULL) {
				cryptox_free(ses);
				return (ENOMEM);
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
			cryptox_free(ses);
			return (EINVAL);
		}
	}

	mtx_enter(&cryptox_sc->sc_mtx);
	ses->ses_sid = ++cryptox_sc->sc_sid;
	SMR_LIST_INSERT_HEAD_LOCKED(&cryptox_sc->sc_sessions, ses, ses_entries);
	mtx_leave(&cryptox_sc->sc_mtx);

	*sidp = ses->ses_sid;
	return (0);
}

int
cryptox_freesession(u_int64_t tid)
{
	struct cryptox_session *ses;
	u_int32_t sid = (u_int32_t)tid;

	mtx_enter(&cryptox_sc->sc_mtx);
	SMR_LIST_FOREACH_LOCKED(ses, &cryptox_sc->sc_sessions, ses_entries) {
		if (ses->ses_sid == sid) {
			SMR_LIST_REMOVE_LOCKED(ses, ses_entries);
			break;
		}
	}
	mtx_leave(&cryptox_sc->sc_mtx);

	if (ses == NULL)
		return (EINVAL);

	smr_call(&ses->ses_smr, cryptox_free_smr, ses);

	return (0);
}

void
cryptox_free(struct cryptox_session *ses)
{
	struct swcr_data *swd;
	const struct auth_hash *axf;

	if (ses->ses_swd) {
		swd = ses->ses_swd;
		axf = swd->sw_axf;

		if (swd->sw_ictx) {
			explicit_bzero(swd->sw_ictx, axf->ctxsize);
			free(swd->sw_ictx, M_CRYPTO_DATA, axf->ctxsize);
		}
		if (swd->sw_octx) {
			explicit_bzero(swd->sw_octx, axf->ctxsize);
			free(swd->sw_octx, M_CRYPTO_DATA, axf->ctxsize);
		}
		free(swd, M_CRYPTO_DATA, sizeof(*swd));
	}

	if (ses->ses_buf) {
		explicit_bzero(ses->ses_buf, ses->ses_buflen);
		free(ses->ses_buf, M_DEVBUF, ses->ses_buflen);
	}

	explicit_bzero(ses, sizeof (*ses));
	pool_put(&cryptoxpl, ses);
}

void
cryptox_free_smr(void *arg)
{
	struct cryptox_session *ses = arg;

	cryptox_free(ses);
}

struct cryptox_session *
cryptox_get(uint32_t sid)
{
	struct cryptox_session *ses = NULL;

	SMR_ASSERT_CRITICAL();
	SMR_LIST_FOREACH(ses, &cryptox_sc->sc_sessions, ses_entries) {
		if (ses->ses_sid == sid)
			break;
	}
	return (ses);
}

int
cryptox_swauth(struct cryptop *crp, struct cryptodesc *crd,
    struct swcr_data *sw, caddr_t buf)
{
	int type;

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		type = CRYPTO_BUF_MBUF;
	else
		type = CRYPTO_BUF_IOV;

	return (swcr_authcompute(crp, crd, sw, buf, type));
}

int
cryptox_encdec(struct cryptop *crp, struct cryptodesc *crd,
    struct cryptox_session *ses)
{
	int err, ivlen, iskip, oskip, rlen;
	uint8_t iv[EALG_MAX_BLOCK_LEN];
	uint8_t *buf = ses->ses_buf;

	rlen = err = iskip = oskip = 0;

	if (crd->crd_len > ses->ses_buflen) {
		if (buf != NULL) {
			explicit_bzero(buf, ses->ses_buflen);
			free(buf, M_DEVBUF, ses->ses_buflen);
		}

		ses->ses_buflen = 0;
		rlen = roundup(crd->crd_len, EALG_MAX_BLOCK_LEN);
		ses->ses_buf = buf = malloc(rlen, M_DEVBUF, M_NOWAIT |
		    M_ZERO);
		if (buf == NULL)
			return (ENOMEM);
		ses->ses_buflen = rlen;
	}

	/* CBC uses 16 */
	ivlen =  16;

	/* Initialize the IV */
	if (crd->crd_flags & CRD_F_ENCRYPT) {
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crd->crd_iv, ivlen);
		else
			arc4random_buf(iv, ivlen);

		/* Do we need to write the IV */
		if ((crd->crd_flags & CRD_F_IV_PRESENT) == 0) {
			if (crp->crp_flags & CRYPTO_F_IMBUF) {
				if (m_copyback((struct mbuf *)crp->crp_buf,
				    crd->crd_inject, ivlen, iv, M_NOWAIT)) {
					err = ENOMEM;
					goto out;
				}
			} else
				cuio_copyback((struct uio *)crp->crp_buf,
				    crd->crd_inject, ivlen, iv);
		}
	} else {
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(iv, crd->crd_iv, ivlen);
		else {
			if (crp->crp_flags & CRYPTO_F_IMBUF)
				m_copydata((struct mbuf *)crp->crp_buf,
				    crd->crd_inject, ivlen, iv);
			else
				cuio_copydata((struct uio *)crp->crp_buf,
				    crd->crd_inject, ivlen, iv);
		}
	}

	/* Copy data to be processed to the buffer */
	if (crp->crp_flags & CRYPTO_F_IMBUF)
		m_copydata((struct mbuf *)crp->crp_buf, crd->crd_skip,
		    crd->crd_len, buf);
	else
		cuio_copydata((struct uio *)crp->crp_buf, crd->crd_skip,
		    crd->crd_len, buf);

	/* Apply cipher */
	fpu_kernel_enter();
	switch (crd->crd_alg) {
	case CRYPTO_AES_CBC:
		if (crd->crd_flags & CRD_F_ENCRYPT)
			aes_v8_cbc_encrypt(buf, buf, crd->crd_len, &ses->ses_ekey, iv, 1);
		else
			aes_v8_cbc_encrypt(buf, buf, crd->crd_len, &ses->ses_dkey, iv, 0);
		break;
	}
	fpu_kernel_exit();

	cryptox_ops++;

	/* Copy back the result */
	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		if (m_copyback((struct mbuf *)crp->crp_buf, crd->crd_skip,
		    crd->crd_len, buf, M_NOWAIT)) {
			err = ENOMEM;
			goto out;
		}
	} else
		cuio_copyback((struct uio *)crp->crp_buf, crd->crd_skip,
		    crd->crd_len, buf);

out:
	explicit_bzero(buf, roundup(crd->crd_len, EALG_MAX_BLOCK_LEN));
	return (err);
}

int
cryptox_process(struct cryptop *crp)
{
	struct cryptox_session *ses;
	struct cryptodesc *crd, *crde;
	int err = 0;
	int i;

	KASSERT(crp->crp_ndesc >= 1);

	smr_read_enter();
	ses = cryptox_get(crp->crp_sid & 0xffffffff);
	if (!ses) {
		err = EINVAL;
		goto out;
	}

	crde = NULL;
	for (i = 0; i < crp->crp_ndesc; i++) {
		crd = &crp->crp_desc[i];
		switch (crd->crd_alg) {
		case CRYPTO_AES_CBC:
			err = cryptox_encdec(crp, crd, ses);
			if (err != 0)
				goto out;
			break;
		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			err = cryptox_swauth(crp, crd, ses->ses_swd,
			    crp->crp_buf);
			if (err != 0)
				goto out;
			break;

		default:
			err = EINVAL;
			goto out;
		}
	}

out:
	smr_read_leave();
	return (err);
}
