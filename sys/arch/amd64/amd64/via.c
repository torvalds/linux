/*	$OpenBSD: via.c,v 1.37 2021/10/24 10:26:22 patrick Exp $	*/
/*	$NetBSD: machdep.c,v 1.214 1996/11/10 03:16:17 thorpej Exp $	*/

/*-
 * Copyright (c) 2003 Jason Wright
 * Copyright (c) 2003, 2004 Theo de Raadt
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
#include <sys/timeout.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>

#ifdef CRYPTO
#include <crypto/cryptodev.h>
#include <crypto/aes.h>
#include <crypto/xform.h>
#include <crypto/cryptosoft.h>
#endif

#include <machine/cpufunc.h>
#include <machine/specialreg.h>

void	viac3_rnd(void *);


#ifdef CRYPTO

struct viac3_session {
	u_int32_t	ses_ekey[4 * (AES_MAXROUNDS + 1) + 4];	/* 128 bit aligned */
	u_int32_t	ses_dkey[4 * (AES_MAXROUNDS + 1) + 4];	/* 128 bit aligned */
	u_int32_t	ses_cw0;
	struct swcr_data *swd;
	int		ses_klen;
	int		ses_used;
};

struct viac3_softc {
	u_int32_t		op_cw[4];		/* 128 bit aligned */
	u_int8_t		op_iv[16];		/* 128 bit aligned */
	void			*op_buf;

	/* normal softc stuff */
	int32_t			sc_cid;
	int			sc_nsessions;
	struct viac3_session	*sc_sessions;
};

#define VIAC3_SESSION(sid)		((sid) & 0x0fffffff)
#define	VIAC3_SID(crd,ses)		(((crd) << 28) | ((ses) & 0x0fffffff))

static struct viac3_softc *vc3_sc;

extern const u_int8_t hmac_ipad_buffer[HMAC_MAX_BLOCK_LEN];
extern const u_int8_t hmac_opad_buffer[HMAC_MAX_BLOCK_LEN];

void viac3_crypto_setup(void);
int viac3_crypto_newsession(u_int32_t *, struct cryptoini *);
int viac3_crypto_process(struct cryptop *);
int viac3_crypto_swauth(struct cryptop *, struct cryptodesc *,
    struct swcr_data *, caddr_t);
int viac3_crypto_encdec(struct cryptop *, struct cryptodesc *,
    struct viac3_session *, struct viac3_softc *, caddr_t);
int viac3_crypto_freesession(u_int64_t);
static __inline void viac3_cbc(void *, void *, void *, void *, int, void *);

void
viac3_crypto_setup(void)
{
	int algs[CRYPTO_ALGORITHM_MAX + 1];

	vc3_sc = malloc(sizeof(*vc3_sc), M_DEVBUF, M_NOWAIT|M_ZERO);
	if (vc3_sc == NULL)
		return;

	bzero(algs, sizeof(algs));
	algs[CRYPTO_AES_CBC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_MD5_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA1_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_RIPEMD160_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_256_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_384_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_SHA2_512_HMAC] = CRYPTO_ALG_FLAG_SUPPORTED;
	algs[CRYPTO_ESN] = CRYPTO_ALG_FLAG_SUPPORTED;

	vc3_sc->sc_cid = crypto_get_driverid(0);
	if (vc3_sc->sc_cid < 0) {
		free(vc3_sc, M_DEVBUF, sizeof(*vc3_sc));
		vc3_sc = NULL;
		return;
	}

	crypto_register(vc3_sc->sc_cid, algs, viac3_crypto_newsession,
	    viac3_crypto_freesession, viac3_crypto_process);
}

int
viac3_crypto_newsession(u_int32_t *sidp, struct cryptoini *cri)
{
	struct cryptoini	*c;
	struct viac3_softc	*sc = vc3_sc;
	struct viac3_session	*ses = NULL;
	const struct auth_hash	*axf;
	struct swcr_data	*swd;
	int			 sesn, i, cw0;

	if (sc == NULL || sidp == NULL || cri == NULL)
		return (EINVAL);

	if (sc->sc_sessions == NULL) {
		ses = sc->sc_sessions = malloc(sizeof(*ses), M_DEVBUF,
		    M_NOWAIT);
		if (ses == NULL)
			return (ENOMEM);
		sesn = 0;
		sc->sc_nsessions = 1;
	} else {
		for (sesn = 0; sesn < sc->sc_nsessions; sesn++) {
			if (sc->sc_sessions[sesn].ses_used == 0) {
				ses = &sc->sc_sessions[sesn];
				break;
			}
		}

		if (ses == NULL) {
			sesn = sc->sc_nsessions;
			ses = mallocarray(sesn + 1, sizeof(*ses), M_DEVBUF,
			    M_NOWAIT);
			if (ses == NULL)
				return (ENOMEM);
			memcpy(ses, sc->sc_sessions, sesn * sizeof(*ses));
			explicit_bzero(sc->sc_sessions, sesn * sizeof(*ses));
			free(sc->sc_sessions, M_DEVBUF, sesn * sizeof(*ses));
			sc->sc_sessions = ses;
			ses = &sc->sc_sessions[sesn];
			sc->sc_nsessions++;
		}
	}

	bzero(ses, sizeof(*ses));
	ses->ses_used = 1;

	for (c = cri; c != NULL; c = c->cri_next) {
		switch (c->cri_alg) {
		case CRYPTO_AES_CBC:
			switch (c->cri_klen) {
			case 128:
				cw0 = C3_CRYPT_CWLO_KEY128;
				break;
			case 192:
				cw0 = C3_CRYPT_CWLO_KEY192;
				break;
			case 256:
				cw0 = C3_CRYPT_CWLO_KEY256;
				break;
			default:
				viac3_crypto_freesession(sesn);
				return (EINVAL);
			}
			cw0 |= C3_CRYPT_CWLO_ALG_AES | C3_CRYPT_CWLO_KEYGEN_SW |
			    C3_CRYPT_CWLO_NORMAL;

			ses->ses_klen = c->cri_klen;
			ses->ses_cw0 = cw0;

			/* Build expanded keys for both directions */
			AES_KeySetup_Encrypt(ses->ses_ekey, c->cri_key,
			    c->cri_klen / 8);
			AES_KeySetup_Decrypt(ses->ses_dkey, c->cri_key,
			    c->cri_klen / 8);
			for (i = 0; i < 4 * (AES_MAXROUNDS + 1); i++) {
				ses->ses_ekey[i] = ntohl(ses->ses_ekey[i]);
				ses->ses_dkey[i] = ntohl(ses->ses_dkey[i]);
			}

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
				viac3_crypto_freesession(sesn);
				return (ENOMEM);
			}
			ses->swd = swd;

			swd->sw_ictx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_ictx == NULL) {
				viac3_crypto_freesession(sesn);
				return (ENOMEM);
			}

			swd->sw_octx = malloc(axf->ctxsize, M_CRYPTO_DATA,
			    M_NOWAIT);
			if (swd->sw_octx == NULL) {
				viac3_crypto_freesession(sesn);
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
			viac3_crypto_freesession(sesn);
			return (EINVAL);
		}
	}

	*sidp = VIAC3_SID(0, sesn);
	return (0);
}

int
viac3_crypto_freesession(u_int64_t tid)
{
	struct viac3_softc *sc = vc3_sc;
	struct swcr_data *swd;
	const struct auth_hash *axf;
	int sesn;
	u_int32_t sid = ((u_int32_t)tid) & 0xffffffff;

	if (sc == NULL)
		return (EINVAL);
	sesn = VIAC3_SESSION(sid);
	if (sesn >= sc->sc_nsessions)
		return (EINVAL);

	if (sc->sc_sessions[sesn].swd) {
		swd = sc->sc_sessions[sesn].swd;
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

	explicit_bzero(&sc->sc_sessions[sesn], sizeof(sc->sc_sessions[sesn]));
	return (0);
}

static __inline void
viac3_cbc(void *cw, void *src, void *dst, void *key, int rep,
    void *iv)
{
	/* Do the deed */
	__asm volatile("pushfq; popfq");
	__asm volatile("rep xcryptcbc" :
	    : "b" (key), "a" (iv), "c" (rep), "d" (cw), "S" (src), "D" (dst)
	    : "memory", "cc");
}

int
viac3_crypto_swauth(struct cryptop *crp, struct cryptodesc *crd,
    struct swcr_data *sw, caddr_t buf)
{
	int	type;

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		type = CRYPTO_BUF_MBUF;
	else
		type= CRYPTO_BUF_IOV;
		
	return (swcr_authcompute(crp, crd, sw, buf, type));
}

int
viac3_crypto_encdec(struct cryptop *crp, struct cryptodesc *crd,
    struct viac3_session *ses, struct viac3_softc *sc, caddr_t buf)
{
	u_int32_t *key;
	int	err = 0;

	if ((crd->crd_len % 16) != 0)
		return (EINVAL);

	sc->op_buf = malloc(crd->crd_len, M_DEVBUF, M_NOWAIT);
	if (sc->op_buf == NULL)
		return (ENOMEM);

	if (crd->crd_flags & CRD_F_ENCRYPT) {
		sc->op_cw[0] = ses->ses_cw0 | C3_CRYPT_CWLO_ENCRYPT;
		key = ses->ses_ekey;
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(sc->op_iv, crd->crd_iv, 16);
		else
			arc4random_buf(sc->op_iv, 16);

		if ((crd->crd_flags & CRD_F_IV_PRESENT) == 0) {
			if (crp->crp_flags & CRYPTO_F_IMBUF)
				err = m_copyback((struct mbuf *)crp->crp_buf,
				    crd->crd_inject, 16, sc->op_iv, M_NOWAIT);
			else if (crp->crp_flags & CRYPTO_F_IOV)
				cuio_copyback((struct uio *)crp->crp_buf,
				    crd->crd_inject, 16, sc->op_iv);
			else
				memcpy(crp->crp_buf + crd->crd_inject,
				    sc->op_iv, 16);
			if (err)
				goto errout;
		}
	} else {
		sc->op_cw[0] = ses->ses_cw0 | C3_CRYPT_CWLO_DECRYPT;
		key = ses->ses_dkey;
		if (crd->crd_flags & CRD_F_IV_EXPLICIT)
			memcpy(sc->op_iv, crd->crd_iv, 16);
		else {
			if (crp->crp_flags & CRYPTO_F_IMBUF)
				m_copydata((struct mbuf *)crp->crp_buf,
				    crd->crd_inject, 16, sc->op_iv);
			else if (crp->crp_flags & CRYPTO_F_IOV)
				cuio_copydata((struct uio *)crp->crp_buf,
				    crd->crd_inject, 16, sc->op_iv);
			else
				memcpy(sc->op_iv,
				    crp->crp_buf + crd->crd_inject, 16);
		}
	}

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		m_copydata((struct mbuf *)crp->crp_buf,
		    crd->crd_skip, crd->crd_len, sc->op_buf);
	else if (crp->crp_flags & CRYPTO_F_IOV)
		cuio_copydata((struct uio *)crp->crp_buf,
		    crd->crd_skip, crd->crd_len, sc->op_buf);
	else
		memcpy(sc->op_buf, crp->crp_buf + crd->crd_skip, crd->crd_len);

	sc->op_cw[1] = sc->op_cw[2] = sc->op_cw[3] = 0;
	viac3_cbc(&sc->op_cw, sc->op_buf, sc->op_buf, key,
	    crd->crd_len / 16, sc->op_iv);

	if (crp->crp_flags & CRYPTO_F_IMBUF)
		err = m_copyback((struct mbuf *)crp->crp_buf,
		    crd->crd_skip, crd->crd_len, sc->op_buf, M_NOWAIT);
	else if (crp->crp_flags & CRYPTO_F_IOV)
		cuio_copyback((struct uio *)crp->crp_buf,
		    crd->crd_skip, crd->crd_len, sc->op_buf);
	else
		memcpy(crp->crp_buf + crd->crd_skip, sc->op_buf,
		    crd->crd_len);

 errout:
	if (sc->op_buf != NULL) {
		explicit_bzero(sc->op_buf, crd->crd_len);
		free(sc->op_buf, M_DEVBUF, crd->crd_len);
		sc->op_buf = NULL;
	}

	return (err);
}

int
viac3_crypto_process(struct cryptop *crp)
{
	struct viac3_softc *sc = vc3_sc;
	struct viac3_session *ses;
	struct cryptodesc *crd;
	int sesn, err = 0;
	int i;

	KASSERT(crp->crp_ndesc >= 1);

	sesn = VIAC3_SESSION(crp->crp_sid);
	if (sesn >= sc->sc_nsessions) {
		err = EINVAL;
		goto out;
	}
	ses = &sc->sc_sessions[sesn];
	if (ses->ses_used == 0) {
		err = EINVAL;
		goto out;
	}

	for (i = 0; i < crp->crp_ndesc; i++) {
		crd = &crp->crp_desc[i];
		switch (crd->crd_alg) {
		case CRYPTO_AES_CBC:
			if ((err = viac3_crypto_encdec(crp, crd, ses, sc,
			    crp->crp_buf)) != 0)
				goto out;
			break;

		case CRYPTO_MD5_HMAC:
		case CRYPTO_SHA1_HMAC:
		case CRYPTO_RIPEMD160_HMAC:
		case CRYPTO_SHA2_256_HMAC:
		case CRYPTO_SHA2_384_HMAC:
		case CRYPTO_SHA2_512_HMAC:
			if ((err = viac3_crypto_swauth(crp, crd, ses->swd,
			    crp->crp_buf)) != 0)
				goto out;
			break;

		default:
			err = EINVAL;
			goto out;
		}
	}
out:
	return (err);
}

#endif /* CRYPTO */

/*
 * Note, the VIA C3 Nehemiah provides 4 internal 8-byte buffers, which
 * store random data, and can be accessed a lot quicker than waiting
 * for new data to be generated.  As we are using every 8th bit only
 * due to whitening. Since the RNG generates in excess of 21KB/s at
 * its worst, collecting 64 bytes worth of entropy should not affect
 * things significantly.
 *
 * Note, due to some weirdness in the RNG, we need at least 7 bytes
 * extra on the end of our buffer.  Also, there is an outside chance
 * that the VIA RNG can "wedge", as the generated bit-rate is variable.
 * We could do all sorts of startup testing and things, but
 * frankly, I don't really see the point.  If the RNG wedges, then the
 * chances of you having a defective CPU are very high.  Let it wedge.
 *
 * Adding to the whole confusion, in order to access the RNG, we need
 * to have FXSR support enabled, and the correct FPU enable bits must
 * be there to enable the FPU in kernel.  It would be nice if all this
 * mumbo-jumbo was not needed in order to use the RNG.  Oh well, life
 * does go on...
 */
#define VIAC3_RNG_BUFSIZ	16		/* 32bit words */
struct timeout viac3_rnd_tmo;
int viac3_rnd_present;

void
viac3_rnd(void *v)
{
	struct timeout *tmo = v;
	unsigned int *p, i, rv, len = VIAC3_RNG_BUFSIZ;
	static int buffer[VIAC3_RNG_BUFSIZ + 2];	/* XXX why + 2? */

	/*
	 * Here we collect the random data from the VIA C3 RNG.  We make
	 * sure that we turn on maximum whitening (%edx[0,1] == "11"), so
	 * that we get the best random data possible.
	 */
	__asm volatile("rep xstorerng"
	    : "=a" (rv) : "d" (3), "D" (buffer), "c" (len*sizeof(int))
	    : "memory", "cc");

	for (i = 0, p = buffer; i < VIAC3_RNG_BUFSIZ; i++, p++)
		enqueue_randomness(*p);

	timeout_add_msec(tmo, 10);
}
