/*	$OpenBSD: cryptodev.c,v 1.52 2002/06/19 07:22:46 deraadt Exp $	*/

/*-
 * Copyright (c) 2001 Theo de Raadt
 * Copyright (c) 2002-2006 Sam Leffler, Errno Consulting
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by John-Mark Gurney
 * under sponsorship of the FreeBSD Foundation and
 * Rubicon Communications, LLC (Netgate).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/random.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/fcntl.h>
#include <sys/bus.h>
#include <sys/user.h>
#include <sys/sdt.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/xform.h>

SDT_PROVIDER_DECLARE(opencrypto);

SDT_PROBE_DEFINE1(opencrypto, dev, ioctl, error, "int"/*line number*/);

#ifdef COMPAT_FREEBSD32
#include <sys/mount.h>
#include <compat/freebsd32/freebsd32.h>

struct session_op32 {
	u_int32_t	cipher;
	u_int32_t	mac;
	u_int32_t	keylen;
	u_int32_t	key;
	int		mackeylen;
	u_int32_t	mackey;
	u_int32_t	ses;
};

struct session2_op32 {
	u_int32_t	cipher;
	u_int32_t	mac;
	u_int32_t	keylen;
	u_int32_t	key;
	int		mackeylen;
	u_int32_t	mackey;
	u_int32_t	ses;
	int		crid;
	int		pad[4];
};

struct crypt_op32 {
	u_int32_t	ses;
	u_int16_t	op;
	u_int16_t	flags;
	u_int		len;
	u_int32_t	src, dst;
	u_int32_t	mac;
	u_int32_t	iv;
};

struct crparam32 {
	u_int32_t	crp_p;
	u_int		crp_nbits;
};

struct crypt_kop32 {
	u_int		crk_op;
	u_int		crk_status;
	u_short		crk_iparams;
	u_short		crk_oparams;
	u_int		crk_crid;
	struct crparam32	crk_param[CRK_MAXPARAM];
};

struct cryptotstat32 {
	struct timespec32	acc;
	struct timespec32	min;
	struct timespec32	max;
	u_int32_t	count;
};

struct cryptostats32 {
	u_int32_t	cs_ops;
	u_int32_t	cs_errs;
	u_int32_t	cs_kops;
	u_int32_t	cs_kerrs;
	u_int32_t	cs_intrs;
	u_int32_t	cs_rets;
	u_int32_t	cs_blocks;
	u_int32_t	cs_kblocks;
	struct cryptotstat32 cs_invoke;
	struct cryptotstat32 cs_done;
	struct cryptotstat32 cs_cb;
	struct cryptotstat32 cs_finis;
};

#define	CIOCGSESSION32	_IOWR('c', 101, struct session_op32)
#define	CIOCCRYPT32	_IOWR('c', 103, struct crypt_op32)
#define	CIOCKEY32	_IOWR('c', 104, struct crypt_kop32)
#define	CIOCGSESSION232	_IOWR('c', 106, struct session2_op32)
#define	CIOCKEY232	_IOWR('c', 107, struct crypt_kop32)

static void
session_op_from_32(const struct session_op32 *from, struct session_op *to)
{

	CP(*from, *to, cipher);
	CP(*from, *to, mac);
	CP(*from, *to, keylen);
	PTRIN_CP(*from, *to, key);
	CP(*from, *to, mackeylen);
	PTRIN_CP(*from, *to, mackey);
	CP(*from, *to, ses);
}

static void
session2_op_from_32(const struct session2_op32 *from, struct session2_op *to)
{

	session_op_from_32((const struct session_op32 *)from,
	    (struct session_op *)to);
	CP(*from, *to, crid);
}

static void
session_op_to_32(const struct session_op *from, struct session_op32 *to)
{

	CP(*from, *to, cipher);
	CP(*from, *to, mac);
	CP(*from, *to, keylen);
	PTROUT_CP(*from, *to, key);
	CP(*from, *to, mackeylen);
	PTROUT_CP(*from, *to, mackey);
	CP(*from, *to, ses);
}

static void
session2_op_to_32(const struct session2_op *from, struct session2_op32 *to)
{

	session_op_to_32((const struct session_op *)from,
	    (struct session_op32 *)to);
	CP(*from, *to, crid);
}

static void
crypt_op_from_32(const struct crypt_op32 *from, struct crypt_op *to)
{

	CP(*from, *to, ses);
	CP(*from, *to, op);
	CP(*from, *to, flags);
	CP(*from, *to, len);
	PTRIN_CP(*from, *to, src);
	PTRIN_CP(*from, *to, dst);
	PTRIN_CP(*from, *to, mac);
	PTRIN_CP(*from, *to, iv);
}

static void
crypt_op_to_32(const struct crypt_op *from, struct crypt_op32 *to)
{

	CP(*from, *to, ses);
	CP(*from, *to, op);
	CP(*from, *to, flags);
	CP(*from, *to, len);
	PTROUT_CP(*from, *to, src);
	PTROUT_CP(*from, *to, dst);
	PTROUT_CP(*from, *to, mac);
	PTROUT_CP(*from, *to, iv);
}

static void
crparam_from_32(const struct crparam32 *from, struct crparam *to)
{

	PTRIN_CP(*from, *to, crp_p);
	CP(*from, *to, crp_nbits);
}

static void
crparam_to_32(const struct crparam *from, struct crparam32 *to)
{

	PTROUT_CP(*from, *to, crp_p);
	CP(*from, *to, crp_nbits);
}

static void
crypt_kop_from_32(const struct crypt_kop32 *from, struct crypt_kop *to)
{
	int i;

	CP(*from, *to, crk_op);
	CP(*from, *to, crk_status);
	CP(*from, *to, crk_iparams);
	CP(*from, *to, crk_oparams);
	CP(*from, *to, crk_crid);
	for (i = 0; i < CRK_MAXPARAM; i++)
		crparam_from_32(&from->crk_param[i], &to->crk_param[i]);
}

static void
crypt_kop_to_32(const struct crypt_kop *from, struct crypt_kop32 *to)
{
	int i;

	CP(*from, *to, crk_op);
	CP(*from, *to, crk_status);
	CP(*from, *to, crk_iparams);
	CP(*from, *to, crk_oparams);
	CP(*from, *to, crk_crid);
	for (i = 0; i < CRK_MAXPARAM; i++)
		crparam_to_32(&from->crk_param[i], &to->crk_param[i]);
}
#endif

struct csession {
	TAILQ_ENTRY(csession) next;
	crypto_session_t cses;
	u_int32_t	ses;
	struct mtx	lock;		/* for op submission */

	u_int32_t	cipher;
	struct enc_xform *txform;
	u_int32_t	mac;
	struct auth_hash *thash;

	caddr_t		key;
	int		keylen;

	caddr_t		mackey;
	int		mackeylen;
};

struct cryptop_data {
	struct csession *cse;

	struct iovec	iovec[1];
	struct uio	uio;
	bool		done;
};

struct fcrypt {
	TAILQ_HEAD(csessionlist, csession) csessions;
	int		sesn;
};

static	int cryptof_ioctl(struct file *, u_long, void *,
		    struct ucred *, struct thread *);
static	int cryptof_stat(struct file *, struct stat *,
		    struct ucred *, struct thread *);
static	int cryptof_close(struct file *, struct thread *);
static	int cryptof_fill_kinfo(struct file *, struct kinfo_file *,
		    struct filedesc *);

static struct fileops cryptofops = {
    .fo_read = invfo_rdwr,
    .fo_write = invfo_rdwr,
    .fo_truncate = invfo_truncate,
    .fo_ioctl = cryptof_ioctl,
    .fo_poll = invfo_poll,
    .fo_kqfilter = invfo_kqfilter,
    .fo_stat = cryptof_stat,
    .fo_close = cryptof_close,
    .fo_chmod = invfo_chmod,
    .fo_chown = invfo_chown,
    .fo_sendfile = invfo_sendfile,
    .fo_fill_kinfo = cryptof_fill_kinfo,
};

static struct csession *csefind(struct fcrypt *, u_int);
static int csedelete(struct fcrypt *, struct csession *);
static struct csession *cseadd(struct fcrypt *, struct csession *);
static struct csession *csecreate(struct fcrypt *, crypto_session_t, caddr_t,
    u_int64_t, caddr_t, u_int64_t, u_int32_t, u_int32_t, struct enc_xform *,
    struct auth_hash *);
static void csefree(struct csession *);

static	int cryptodev_op(struct csession *, struct crypt_op *,
			struct ucred *, struct thread *td);
static	int cryptodev_aead(struct csession *, struct crypt_aead *,
			struct ucred *, struct thread *);
static	int cryptodev_key(struct crypt_kop *);
static	int cryptodev_find(struct crypt_find_op *);

/*
 * Check a crypto identifier to see if it requested
 * a software device/driver.  This can be done either
 * by device name/class or through search constraints.
 */
static int
checkforsoftware(int *cridp)
{
	int crid;

	crid = *cridp;

	if (!crypto_devallowsoft) {
		if (crid & CRYPTOCAP_F_SOFTWARE) {
			if (crid & CRYPTOCAP_F_HARDWARE) {
				*cridp = CRYPTOCAP_F_HARDWARE;
				return 0;
			}
			return EINVAL;
		}
		if ((crid & CRYPTOCAP_F_HARDWARE) == 0 &&
		    (crypto_getcaps(crid) & CRYPTOCAP_F_HARDWARE) == 0)
			return EINVAL;
	}
	return 0;
}

/* ARGSUSED */
static int
cryptof_ioctl(
	struct file *fp,
	u_long cmd,
	void *data,
	struct ucred *active_cred,
	struct thread *td)
{
#define	SES2(p)	((struct session2_op *)p)
	struct cryptoini cria, crie;
	struct fcrypt *fcr = fp->f_data;
	struct csession *cse;
	struct session_op *sop;
	struct crypt_op *cop;
	struct crypt_aead *caead;
	struct enc_xform *txform = NULL;
	struct auth_hash *thash = NULL;
	struct crypt_kop *kop;
	crypto_session_t cses;
	u_int32_t ses;
	int error = 0, crid;
#ifdef COMPAT_FREEBSD32
	struct session2_op sopc;
	struct crypt_op copc;
	struct crypt_kop kopc;
#endif

	switch (cmd) {
	case CIOCGSESSION:
	case CIOCGSESSION2:
#ifdef COMPAT_FREEBSD32
	case CIOCGSESSION32:
	case CIOCGSESSION232:
		if (cmd == CIOCGSESSION32) {
			session_op_from_32(data, (struct session_op *)&sopc);
			sop = (struct session_op *)&sopc;
		} else if (cmd == CIOCGSESSION232) {
			session2_op_from_32(data, &sopc);
			sop = (struct session_op *)&sopc;
		} else
#endif
			sop = (struct session_op *)data;
		switch (sop->cipher) {
		case 0:
			break;
		case CRYPTO_DES_CBC:
			txform = &enc_xform_des;
			break;
		case CRYPTO_3DES_CBC:
			txform = &enc_xform_3des;
			break;
		case CRYPTO_BLF_CBC:
			txform = &enc_xform_blf;
			break;
		case CRYPTO_CAST_CBC:
			txform = &enc_xform_cast5;
			break;
		case CRYPTO_SKIPJACK_CBC:
			txform = &enc_xform_skipjack;
			break;
		case CRYPTO_AES_CBC:
			txform = &enc_xform_rijndael128;
			break;
		case CRYPTO_AES_XTS:
			txform = &enc_xform_aes_xts;
			break;
		case CRYPTO_NULL_CBC:
			txform = &enc_xform_null;
			break;
		case CRYPTO_ARC4:
			txform = &enc_xform_arc4;
			break;
 		case CRYPTO_CAMELLIA_CBC:
 			txform = &enc_xform_camellia;
 			break;
		case CRYPTO_AES_ICM:
			txform = &enc_xform_aes_icm;
 			break;
		case CRYPTO_AES_NIST_GCM_16:
			txform = &enc_xform_aes_nist_gcm;
 			break;
		case CRYPTO_CHACHA20:
			txform = &enc_xform_chacha20;
			break;
		case CRYPTO_AES_CCM_16:
			txform = &enc_xform_ccm;
			break;

		default:
			CRYPTDEB("invalid cipher");
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			return (EINVAL);
		}

		switch (sop->mac) {
		case 0:
			break;
		case CRYPTO_MD5_HMAC:
			thash = &auth_hash_hmac_md5;
			break;
		case CRYPTO_POLY1305:
			thash = &auth_hash_poly1305;
			break;
		case CRYPTO_SHA1_HMAC:
			thash = &auth_hash_hmac_sha1;
			break;
		case CRYPTO_SHA2_224_HMAC:
			thash = &auth_hash_hmac_sha2_224;
			break;
		case CRYPTO_SHA2_256_HMAC:
			thash = &auth_hash_hmac_sha2_256;
			break;
		case CRYPTO_SHA2_384_HMAC:
			thash = &auth_hash_hmac_sha2_384;
			break;
		case CRYPTO_SHA2_512_HMAC:
			thash = &auth_hash_hmac_sha2_512;
			break;
		case CRYPTO_RIPEMD160_HMAC:
			thash = &auth_hash_hmac_ripemd_160;
			break;
		case CRYPTO_AES_128_NIST_GMAC:
			thash = &auth_hash_nist_gmac_aes_128;
			break;
		case CRYPTO_AES_192_NIST_GMAC:
			thash = &auth_hash_nist_gmac_aes_192;
			break;
		case CRYPTO_AES_256_NIST_GMAC:
			thash = &auth_hash_nist_gmac_aes_256;
			break;

		case CRYPTO_AES_CCM_CBC_MAC:
			switch (sop->keylen) {
			case 16:
				thash = &auth_hash_ccm_cbc_mac_128;
				break;
			case 24:
				thash = &auth_hash_ccm_cbc_mac_192;
				break;
			case 32:
				thash = &auth_hash_ccm_cbc_mac_256;
				break;
			default:
				CRYPTDEB("Invalid CBC MAC key size %d",
				    sop->keylen);
				SDT_PROBE1(opencrypto, dev, ioctl,
				    error, __LINE__);
				return (EINVAL);
			}
			break;
#ifdef notdef
		case CRYPTO_MD5:
			thash = &auth_hash_md5;
			break;
#endif
		case CRYPTO_SHA1:
			thash = &auth_hash_sha1;
			break;
		case CRYPTO_SHA2_224:
			thash = &auth_hash_sha2_224;
			break;
		case CRYPTO_SHA2_256:
			thash = &auth_hash_sha2_256;
			break;
		case CRYPTO_SHA2_384:
			thash = &auth_hash_sha2_384;
			break;
		case CRYPTO_SHA2_512:
			thash = &auth_hash_sha2_512;
			break;

		case CRYPTO_NULL_HMAC:
			thash = &auth_hash_null;
			break;

		case CRYPTO_BLAKE2B:
			thash = &auth_hash_blake2b;
			break;
		case CRYPTO_BLAKE2S:
			thash = &auth_hash_blake2s;
			break;

		default:
			CRYPTDEB("invalid mac");
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			return (EINVAL);
		}

		bzero(&crie, sizeof(crie));
		bzero(&cria, sizeof(cria));

		if (txform) {
			crie.cri_alg = txform->type;
			crie.cri_klen = sop->keylen * 8;
			if (sop->keylen > txform->maxkey ||
			    sop->keylen < txform->minkey) {
				CRYPTDEB("invalid cipher parameters");
				error = EINVAL;
				SDT_PROBE1(opencrypto, dev, ioctl, error,
				    __LINE__);
				goto bail;
			}

			crie.cri_key = malloc(crie.cri_klen / 8,
			    M_XDATA, M_WAITOK);
			if ((error = copyin(sop->key, crie.cri_key,
			    crie.cri_klen / 8))) {
				CRYPTDEB("invalid key");
				SDT_PROBE1(opencrypto, dev, ioctl, error,
				    __LINE__);
				goto bail;
			}
			if (thash)
				crie.cri_next = &cria;
		}

		if (thash) {
			cria.cri_alg = thash->type;
			cria.cri_klen = sop->mackeylen * 8;
			if (thash->keysize != 0 &&
			    sop->mackeylen > thash->keysize) {
				CRYPTDEB("invalid mac key length");
				error = EINVAL;
				SDT_PROBE1(opencrypto, dev, ioctl, error,
				    __LINE__);
				goto bail;
			}

			if (cria.cri_klen) {
				cria.cri_key = malloc(cria.cri_klen / 8,
				    M_XDATA, M_WAITOK);
				if ((error = copyin(sop->mackey, cria.cri_key,
				    cria.cri_klen / 8))) {
					CRYPTDEB("invalid mac key");
					SDT_PROBE1(opencrypto, dev, ioctl,
					    error, __LINE__);
					goto bail;
				}
			}
		}

		/* NB: CIOCGSESSION2 has the crid */
		if (cmd == CIOCGSESSION2
#ifdef COMPAT_FREEBSD32
		    || cmd == CIOCGSESSION232
#endif
			) {
			crid = SES2(sop)->crid;
			error = checkforsoftware(&crid);
			if (error) {
				CRYPTDEB("checkforsoftware");
				SDT_PROBE1(opencrypto, dev, ioctl, error,
				    __LINE__);
				goto bail;
			}
		} else
			crid = CRYPTOCAP_F_HARDWARE;
		error = crypto_newsession(&cses, (txform ? &crie : &cria), crid);
		if (error) {
			CRYPTDEB("crypto_newsession");
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			goto bail;
		}

		cse = csecreate(fcr, cses, crie.cri_key, crie.cri_klen,
		    cria.cri_key, cria.cri_klen, sop->cipher, sop->mac, txform,
		    thash);

		if (cse == NULL) {
			crypto_freesession(cses);
			error = EINVAL;
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			CRYPTDEB("csecreate");
			goto bail;
		}
		sop->ses = cse->ses;
		if (cmd == CIOCGSESSION2
#ifdef COMPAT_FREEBSD32
		    || cmd == CIOCGSESSION232
#endif
		    ) {
			/* return hardware/driver id */
			SES2(sop)->crid = crypto_ses2hid(cse->cses);
		}
bail:
		if (error) {
			if (crie.cri_key)
				free(crie.cri_key, M_XDATA);
			if (cria.cri_key)
				free(cria.cri_key, M_XDATA);
		}
#ifdef COMPAT_FREEBSD32
		else {
			if (cmd == CIOCGSESSION32)
				session_op_to_32(sop, data);
			else if (cmd == CIOCGSESSION232)
				session2_op_to_32((struct session2_op *)sop,
				    data);
		}
#endif
		break;
	case CIOCFSESSION:
		ses = *(u_int32_t *)data;
		cse = csefind(fcr, ses);
		if (cse == NULL) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			return (EINVAL);
		}
		csedelete(fcr, cse);
		csefree(cse);
		break;
	case CIOCCRYPT:
#ifdef COMPAT_FREEBSD32
	case CIOCCRYPT32:
		if (cmd == CIOCCRYPT32) {
			cop = &copc;
			crypt_op_from_32(data, cop);
		} else
#endif
			cop = (struct crypt_op *)data;
		cse = csefind(fcr, cop->ses);
		if (cse == NULL) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			return (EINVAL);
		}
		error = cryptodev_op(cse, cop, active_cred, td);
#ifdef COMPAT_FREEBSD32
		if (error == 0 && cmd == CIOCCRYPT32)
			crypt_op_to_32(cop, data);
#endif
		break;
	case CIOCKEY:
	case CIOCKEY2:
#ifdef COMPAT_FREEBSD32
	case CIOCKEY32:
	case CIOCKEY232:
#endif
		if (!crypto_userasymcrypto) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			return (EPERM);		/* XXX compat? */
		}
#ifdef COMPAT_FREEBSD32
		if (cmd == CIOCKEY32 || cmd == CIOCKEY232) {
			kop = &kopc;
			crypt_kop_from_32(data, kop);
		} else
#endif
			kop = (struct crypt_kop *)data;
		if (cmd == CIOCKEY
#ifdef COMPAT_FREEBSD32
		    || cmd == CIOCKEY32
#endif
		    ) {
			/* NB: crypto core enforces s/w driver use */
			kop->crk_crid =
			    CRYPTOCAP_F_HARDWARE | CRYPTOCAP_F_SOFTWARE;
		}
		mtx_lock(&Giant);
		error = cryptodev_key(kop);
		mtx_unlock(&Giant);
#ifdef COMPAT_FREEBSD32
		if (cmd == CIOCKEY32 || cmd == CIOCKEY232)
			crypt_kop_to_32(kop, data);
#endif
		break;
	case CIOCASYMFEAT:
		if (!crypto_userasymcrypto) {
			/*
			 * NB: if user asym crypto operations are
			 * not permitted return "no algorithms"
			 * so well-behaved applications will just
			 * fallback to doing them in software.
			 */
			*(int *)data = 0;
		} else {
			error = crypto_getfeat((int *)data);
			if (error)
				SDT_PROBE1(opencrypto, dev, ioctl, error,
				    __LINE__);
		}
		break;
	case CIOCFINDDEV:
		error = cryptodev_find((struct crypt_find_op *)data);
		break;
	case CIOCCRYPTAEAD:
		caead = (struct crypt_aead *)data;
		cse = csefind(fcr, caead->ses);
		if (cse == NULL) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			return (EINVAL);
		}
		error = cryptodev_aead(cse, caead, active_cred, td);
		break;
	default:
		error = EINVAL;
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		break;
	}
	return (error);
#undef SES2
}

static int cryptodev_cb(struct cryptop *);

static struct cryptop_data *
cod_alloc(struct csession *cse, size_t len, struct thread *td)
{
	struct cryptop_data *cod;
	struct uio *uio;

	cod = malloc(sizeof(struct cryptop_data), M_XDATA, M_WAITOK | M_ZERO);

	cod->cse = cse;
	uio = &cod->uio;
	uio->uio_iov = cod->iovec;
	uio->uio_iovcnt = 1;
	uio->uio_resid = len;
	uio->uio_segflg = UIO_SYSSPACE;
	uio->uio_rw = UIO_WRITE;
	uio->uio_td = td;
	uio->uio_iov[0].iov_len = len;
	uio->uio_iov[0].iov_base = malloc(len, M_XDATA, M_WAITOK);
	return (cod);
}

static void
cod_free(struct cryptop_data *cod)
{

	free(cod->uio.uio_iov[0].iov_base, M_XDATA);
	free(cod, M_XDATA);
}

static int
cryptodev_op(
	struct csession *cse,
	struct crypt_op *cop,
	struct ucred *active_cred,
	struct thread *td)
{
	struct cryptop_data *cod = NULL;
	struct cryptop *crp = NULL;
	struct cryptodesc *crde = NULL, *crda = NULL;
	int error;

	if (cop->len > 256*1024-4) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (E2BIG);
	}

	if (cse->txform) {
		if (cop->len == 0 || (cop->len % cse->txform->blocksize) != 0) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			return (EINVAL);
		}
	}

	if (cse->thash)
		cod = cod_alloc(cse, cop->len + cse->thash->hashsize, td);
	else
		cod = cod_alloc(cse, cop->len, td);

	crp = crypto_getreq((cse->txform != NULL) + (cse->thash != NULL));
	if (crp == NULL) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		error = ENOMEM;
		goto bail;
	}

	if (cse->thash && cse->txform) {
		if (cop->flags & COP_F_CIPHER_FIRST) {
			crde = crp->crp_desc;
			crda = crde->crd_next;
		} else {
			crda = crp->crp_desc;
			crde = crda->crd_next;
		}
	} else if (cse->thash) {
		crda = crp->crp_desc;
	} else if (cse->txform) {
		crde = crp->crp_desc;
	} else {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		error = EINVAL;
		goto bail;
	}

	if ((error = copyin(cop->src, cod->uio.uio_iov[0].iov_base,
	    cop->len))) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

	if (crda) {
		crda->crd_skip = 0;
		crda->crd_len = cop->len;
		crda->crd_inject = cop->len;

		crda->crd_alg = cse->mac;
		crda->crd_key = cse->mackey;
		crda->crd_klen = cse->mackeylen * 8;
	}

	if (crde) {
		if (cop->op == COP_ENCRYPT)
			crde->crd_flags |= CRD_F_ENCRYPT;
		else
			crde->crd_flags &= ~CRD_F_ENCRYPT;
		crde->crd_len = cop->len;
		crde->crd_inject = 0;

		crde->crd_alg = cse->cipher;
		crde->crd_key = cse->key;
		crde->crd_klen = cse->keylen * 8;
	}

	crp->crp_ilen = cop->len;
	crp->crp_flags = CRYPTO_F_IOV | CRYPTO_F_CBIMM
		       | (cop->flags & COP_F_BATCH);
	crp->crp_uio = &cod->uio;
	crp->crp_callback = cryptodev_cb;
	crp->crp_session = cse->cses;
	crp->crp_opaque = cod;

	if (cop->iv) {
		if (crde == NULL) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			error = EINVAL;
			goto bail;
		}
		if (cse->cipher == CRYPTO_ARC4) { /* XXX use flag? */
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			error = EINVAL;
			goto bail;
		}
		if ((error = copyin(cop->iv, crde->crd_iv,
		    cse->txform->ivsize))) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			goto bail;
		}
		crde->crd_flags |= CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
		crde->crd_skip = 0;
	} else if (cse->cipher == CRYPTO_ARC4) { /* XXX use flag? */
		crde->crd_skip = 0;
	} else if (crde) {
		crde->crd_flags |= CRD_F_IV_PRESENT;
		crde->crd_skip = cse->txform->ivsize;
		crde->crd_len -= cse->txform->ivsize;
	}

	if (cop->mac && crda == NULL) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		error = EINVAL;
		goto bail;
	}

again:
	/*
	 * Let the dispatch run unlocked, then, interlock against the
	 * callback before checking if the operation completed and going
	 * to sleep.  This insures drivers don't inherit our lock which
	 * results in a lock order reversal between crypto_dispatch forced
	 * entry and the crypto_done callback into us.
	 */
	error = crypto_dispatch(crp);
	if (error != 0) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

	mtx_lock(&cse->lock);
	while (!cod->done)
		mtx_sleep(cod, &cse->lock, PWAIT, "crydev", 0);
	mtx_unlock(&cse->lock);

	if (crp->crp_etype == EAGAIN) {
		crp->crp_etype = 0;
		crp->crp_flags &= ~CRYPTO_F_DONE;
		cod->done = false;
		goto again;
	}

	if (crp->crp_etype != 0) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		error = crp->crp_etype;
		goto bail;
	}

	if (cop->dst &&
	    (error = copyout(cod->uio.uio_iov[0].iov_base, cop->dst,
	    cop->len))) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

	if (cop->mac &&
	    (error = copyout((caddr_t)cod->uio.uio_iov[0].iov_base + cop->len,
	    cop->mac, cse->thash->hashsize))) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

bail:
	if (crp)
		crypto_freereq(crp);
	if (cod)
		cod_free(cod);

	return (error);
}

static int
cryptodev_aead(
	struct csession *cse,
	struct crypt_aead *caead,
	struct ucred *active_cred,
	struct thread *td)
{
	struct cryptop_data *cod = NULL;
	struct cryptop *crp = NULL;
	struct cryptodesc *crde = NULL, *crda = NULL;
	int error;

	if (caead->len > 256*1024-4 || caead->aadlen > 256*1024-4) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (E2BIG);
	}

	if (cse->txform == NULL || cse->thash == NULL || caead->tag == NULL ||
	    (caead->len % cse->txform->blocksize) != 0) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (EINVAL);
	}

	cod = cod_alloc(cse, caead->aadlen + caead->len + cse->thash->hashsize,
	    td);

	crp = crypto_getreq(2);
	if (crp == NULL) {
		error = ENOMEM;
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

	if (caead->flags & COP_F_CIPHER_FIRST) {
		crde = crp->crp_desc;
		crda = crde->crd_next;
	} else {
		crda = crp->crp_desc;
		crde = crda->crd_next;
	}

	if ((error = copyin(caead->aad, cod->uio.uio_iov[0].iov_base,
	    caead->aadlen))) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

	if ((error = copyin(caead->src, (char *)cod->uio.uio_iov[0].iov_base +
	    caead->aadlen, caead->len))) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

	/*
	 * For GCM/CCM, crd_len covers only the AAD.  For other ciphers
	 * chained with an HMAC, crd_len covers both the AAD and the
	 * cipher text.
	 */
	crda->crd_skip = 0;
	if (cse->cipher == CRYPTO_AES_NIST_GCM_16 ||
	    cse->cipher == CRYPTO_AES_CCM_16)
		crda->crd_len = caead->aadlen;
	else
		crda->crd_len = caead->aadlen + caead->len;
	crda->crd_inject = caead->aadlen + caead->len;

	crda->crd_alg = cse->mac;
	crda->crd_key = cse->mackey;
	crda->crd_klen = cse->mackeylen * 8;

	if (caead->op == COP_ENCRYPT)
		crde->crd_flags |= CRD_F_ENCRYPT;
	else
		crde->crd_flags &= ~CRD_F_ENCRYPT;
	crde->crd_skip = caead->aadlen;
	crde->crd_len = caead->len;
	crde->crd_inject = caead->aadlen;

	crde->crd_alg = cse->cipher;
	crde->crd_key = cse->key;
	crde->crd_klen = cse->keylen * 8;

	crp->crp_ilen = caead->aadlen + caead->len;
	crp->crp_flags = CRYPTO_F_IOV | CRYPTO_F_CBIMM
		       | (caead->flags & COP_F_BATCH);
	crp->crp_uio = &cod->uio;
	crp->crp_callback = cryptodev_cb;
	crp->crp_session = cse->cses;
	crp->crp_opaque = cod;

	if (caead->iv) {
		if (caead->ivlen > sizeof(crde->crd_iv)) {
			error = EINVAL;
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			goto bail;
		}

		if ((error = copyin(caead->iv, crde->crd_iv, caead->ivlen))) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			goto bail;
		}
		crde->crd_flags |= CRD_F_IV_EXPLICIT | CRD_F_IV_PRESENT;
	} else {
		crde->crd_flags |= CRD_F_IV_PRESENT;
		crde->crd_skip += cse->txform->ivsize;
		crde->crd_len -= cse->txform->ivsize;
	}

	if ((error = copyin(caead->tag, (caddr_t)cod->uio.uio_iov[0].iov_base +
	    caead->len + caead->aadlen, cse->thash->hashsize))) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}
again:
	/*
	 * Let the dispatch run unlocked, then, interlock against the
	 * callback before checking if the operation completed and going
	 * to sleep.  This insures drivers don't inherit our lock which
	 * results in a lock order reversal between crypto_dispatch forced
	 * entry and the crypto_done callback into us.
	 */
	error = crypto_dispatch(crp);
	if (error != 0) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

	mtx_lock(&cse->lock);
	while (!cod->done)
		mtx_sleep(cod, &cse->lock, PWAIT, "crydev", 0);
	mtx_unlock(&cse->lock);

	if (crp->crp_etype == EAGAIN) {
		crp->crp_etype = 0;
		crp->crp_flags &= ~CRYPTO_F_DONE;
		cod->done = false;
		goto again;
	}

	if (crp->crp_etype != 0) {
		error = crp->crp_etype;
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

	if (caead->dst && (error = copyout(
	    (caddr_t)cod->uio.uio_iov[0].iov_base + caead->aadlen, caead->dst,
	    caead->len))) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

	if ((error = copyout((caddr_t)cod->uio.uio_iov[0].iov_base +
	    caead->aadlen + caead->len, caead->tag, cse->thash->hashsize))) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto bail;
	}

bail:
	crypto_freereq(crp);
	if (cod)
		cod_free(cod);

	return (error);
}

static int
cryptodev_cb(struct cryptop *crp)
{
	struct cryptop_data *cod = crp->crp_opaque;

	/*
	 * Lock to ensure the wakeup() is not missed by the loops
	 * waiting on cod->done in cryptodev_op() and
	 * cryptodev_aead().
	 */
	mtx_lock(&cod->cse->lock);
	cod->done = true;
	mtx_unlock(&cod->cse->lock);
	wakeup(cod);
	return (0);
}

static int
cryptodevkey_cb(void *op)
{
	struct cryptkop *krp = (struct cryptkop *) op;

	wakeup_one(krp);
	return (0);
}

static int
cryptodev_key(struct crypt_kop *kop)
{
	struct cryptkop *krp = NULL;
	int error = EINVAL;
	int in, out, size, i;

	if (kop->crk_iparams + kop->crk_oparams > CRK_MAXPARAM) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (EFBIG);
	}

	in = kop->crk_iparams;
	out = kop->crk_oparams;
	switch (kop->crk_op) {
	case CRK_MOD_EXP:
		if (in == 3 && out == 1)
			break;
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (EINVAL);
	case CRK_MOD_EXP_CRT:
		if (in == 6 && out == 1)
			break;
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (EINVAL);
	case CRK_DSA_SIGN:
		if (in == 5 && out == 2)
			break;
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (EINVAL);
	case CRK_DSA_VERIFY:
		if (in == 7 && out == 0)
			break;
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (EINVAL);
	case CRK_DH_COMPUTE_KEY:
		if (in == 3 && out == 1)
			break;
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (EINVAL);
	default:
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (EINVAL);
	}

	krp = (struct cryptkop *)malloc(sizeof *krp, M_XDATA, M_WAITOK|M_ZERO);
	if (!krp) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		return (ENOMEM);
	}
	krp->krp_op = kop->crk_op;
	krp->krp_status = kop->crk_status;
	krp->krp_iparams = kop->crk_iparams;
	krp->krp_oparams = kop->crk_oparams;
	krp->krp_crid = kop->crk_crid;
	krp->krp_status = 0;
	krp->krp_callback = (int (*) (struct cryptkop *)) cryptodevkey_cb;

	for (i = 0; i < CRK_MAXPARAM; i++) {
		if (kop->crk_param[i].crp_nbits > 65536) {
			/* Limit is the same as in OpenBSD */
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			goto fail;
		}
		krp->krp_param[i].crp_nbits = kop->crk_param[i].crp_nbits;
	}
	for (i = 0; i < krp->krp_iparams + krp->krp_oparams; i++) {
		size = (krp->krp_param[i].crp_nbits + 7) / 8;
		if (size == 0)
			continue;
		krp->krp_param[i].crp_p = malloc(size, M_XDATA, M_WAITOK);
		if (i >= krp->krp_iparams)
			continue;
		error = copyin(kop->crk_param[i].crp_p, krp->krp_param[i].crp_p, size);
		if (error) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			goto fail;
		}
	}

	error = crypto_kdispatch(krp);
	if (error) {
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto fail;
	}
	error = tsleep(krp, PSOCK, "crydev", 0);
	if (error) {
		/* XXX can this happen?  if so, how do we recover? */
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto fail;
	}
	
	kop->crk_crid = krp->krp_crid;		/* device that did the work */
	if (krp->krp_status != 0) {
		error = krp->krp_status;
		SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
		goto fail;
	}

	for (i = krp->krp_iparams; i < krp->krp_iparams + krp->krp_oparams; i++) {
		size = (krp->krp_param[i].crp_nbits + 7) / 8;
		if (size == 0)
			continue;
		error = copyout(krp->krp_param[i].crp_p, kop->crk_param[i].crp_p, size);
		if (error) {
			SDT_PROBE1(opencrypto, dev, ioctl, error, __LINE__);
			goto fail;
		}
	}

fail:
	if (krp) {
		kop->crk_status = krp->krp_status;
		for (i = 0; i < CRK_MAXPARAM; i++) {
			if (krp->krp_param[i].crp_p)
				free(krp->krp_param[i].crp_p, M_XDATA);
		}
		free(krp, M_XDATA);
	}
	return (error);
}

static int
cryptodev_find(struct crypt_find_op *find)
{
	device_t dev;
	size_t fnlen = sizeof find->name;

	if (find->crid != -1) {
		dev = crypto_find_device_byhid(find->crid);
		if (dev == NULL)
			return (ENOENT);
		strncpy(find->name, device_get_nameunit(dev), fnlen);
		find->name[fnlen - 1] = '\x0';
	} else {
		find->name[fnlen - 1] = '\x0';
		find->crid = crypto_find_driver(find->name);
		if (find->crid == -1)
			return (ENOENT);
	}
	return (0);
}

/* ARGSUSED */
static int
cryptof_stat(
	struct file *fp,
	struct stat *sb,
	struct ucred *active_cred,
	struct thread *td)
{

	return (EOPNOTSUPP);
}

/* ARGSUSED */
static int
cryptof_close(struct file *fp, struct thread *td)
{
	struct fcrypt *fcr = fp->f_data;
	struct csession *cse;

	while ((cse = TAILQ_FIRST(&fcr->csessions))) {
		TAILQ_REMOVE(&fcr->csessions, cse, next);
		csefree(cse);
	}
	free(fcr, M_XDATA);
	fp->f_data = NULL;
	return 0;
}

static int
cryptof_fill_kinfo(struct file *fp, struct kinfo_file *kif, struct filedesc *fdp)
{

	kif->kf_type = KF_TYPE_CRYPTO;
	return (0);
}

static struct csession *
csefind(struct fcrypt *fcr, u_int ses)
{
	struct csession *cse;

	TAILQ_FOREACH(cse, &fcr->csessions, next)
		if (cse->ses == ses)
			return (cse);
	return (NULL);
}

static int
csedelete(struct fcrypt *fcr, struct csession *cse_del)
{
	struct csession *cse;

	TAILQ_FOREACH(cse, &fcr->csessions, next) {
		if (cse == cse_del) {
			TAILQ_REMOVE(&fcr->csessions, cse, next);
			return (1);
		}
	}
	return (0);
}
	
static struct csession *
cseadd(struct fcrypt *fcr, struct csession *cse)
{
	TAILQ_INSERT_TAIL(&fcr->csessions, cse, next);
	cse->ses = fcr->sesn++;
	return (cse);
}

struct csession *
csecreate(struct fcrypt *fcr, crypto_session_t cses, caddr_t key, u_int64_t keylen,
    caddr_t mackey, u_int64_t mackeylen, u_int32_t cipher, u_int32_t mac,
    struct enc_xform *txform, struct auth_hash *thash)
{
	struct csession *cse;

	cse = malloc(sizeof(struct csession), M_XDATA, M_NOWAIT | M_ZERO);
	if (cse == NULL)
		return NULL;
	mtx_init(&cse->lock, "cryptodev", "crypto session lock", MTX_DEF);
	cse->key = key;
	cse->keylen = keylen/8;
	cse->mackey = mackey;
	cse->mackeylen = mackeylen/8;
	cse->cses = cses;
	cse->cipher = cipher;
	cse->mac = mac;
	cse->txform = txform;
	cse->thash = thash;
	cseadd(fcr, cse);
	return (cse);
}

static void
csefree(struct csession *cse)
{

	crypto_freesession(cse->cses);
	mtx_destroy(&cse->lock);
	if (cse->key)
		free(cse->key, M_XDATA);
	if (cse->mackey)
		free(cse->mackey, M_XDATA);
	free(cse, M_XDATA);
}

static int
cryptoopen(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	return (0);
}

static int
cryptoread(struct cdev *dev, struct uio *uio, int ioflag)
{
	return (EIO);
}

static int
cryptowrite(struct cdev *dev, struct uio *uio, int ioflag)
{
	return (EIO);
}

static int
cryptoioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	struct file *f;
	struct fcrypt *fcr;
	int fd, error;

	switch (cmd) {
	case CRIOGET:
		fcr = malloc(sizeof(struct fcrypt), M_XDATA, M_WAITOK);
		TAILQ_INIT(&fcr->csessions);
		fcr->sesn = 0;

		error = falloc(td, &f, &fd, 0);

		if (error) {
			free(fcr, M_XDATA);
			return (error);
		}
		/* falloc automatically provides an extra reference to 'f'. */
		finit(f, FREAD | FWRITE, DTYPE_CRYPTO, fcr, &cryptofops);
		*(u_int32_t *)data = fd;
		fdrop(f, td);
		break;
	case CRIOFINDDEV:
		error = cryptodev_find((struct crypt_find_op *)data);
		break;
	case CRIOASYMFEAT:
		error = crypto_getfeat((int *)data);
		break;
	default:
		error = EINVAL;
		break;
	}
	return (error);
}

static struct cdevsw crypto_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	cryptoopen,
	.d_read =	cryptoread,
	.d_write =	cryptowrite,
	.d_ioctl =	cryptoioctl,
	.d_name =	"crypto",
};
static struct cdev *crypto_dev;

/*
 * Initialization code, both for static and dynamic loading.
 */
static int
cryptodev_modevent(module_t mod, int type, void *unused)
{
	switch (type) {
	case MOD_LOAD:
		if (bootverbose)
			printf("crypto: <crypto device>\n");
		crypto_dev = make_dev(&crypto_cdevsw, 0, 
				      UID_ROOT, GID_WHEEL, 0666,
				      "crypto");
		return 0;
	case MOD_UNLOAD:
		/*XXX disallow if active sessions */
		destroy_dev(crypto_dev);
		return 0;
	}
	return EINVAL;
}

static moduledata_t cryptodev_mod = {
	"cryptodev",
	cryptodev_modevent,
	0
};
MODULE_VERSION(cryptodev, 1);
DECLARE_MODULE(cryptodev, cryptodev_mod, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_DEPEND(cryptodev, crypto, 1, 1, 1);
MODULE_DEPEND(cryptodev, zlib, 1, 1, 1);
