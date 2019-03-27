/*	$FreeBSD$	*/
/*	$OpenBSD: ip_esp.c,v 1.69 2001/06/26 06:18:59 angelos Exp $ */
/*-
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr) and
 * Niels Provos (provos@physnet.uni-hamburg.de).
 *
 * The original version of this code was written by John Ioannidis
 * for BSD/OS in Athens, Greece, in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Additional features in 1999 by Angelos D. Keromytis.
 *
 * Copyright (C) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
 * Copyright (c) 2001 Angelos D. Keromytis.
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/random.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/mutex.h>
#include <machine/atomic.h>

#include <net/if.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_ecn.h>
#include <netinet/ip6.h>

#include <netipsec/ipsec.h>
#include <netipsec/ah.h>
#include <netipsec/ah_var.h>
#include <netipsec/esp.h>
#include <netipsec/esp_var.h>
#include <netipsec/xform.h>

#ifdef INET6
#include <netinet6/ip6_var.h>
#include <netipsec/ipsec6.h>
#include <netinet6/ip6_ecn.h>
#endif

#include <netipsec/key.h>
#include <netipsec/key_debug.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/xform.h>

VNET_DEFINE(int, esp_enable) = 1;
VNET_PCPUSTAT_DEFINE(struct espstat, espstat);
VNET_PCPUSTAT_SYSINIT(espstat);

#ifdef VIMAGE
VNET_PCPUSTAT_SYSUNINIT(espstat);
#endif /* VIMAGE */

SYSCTL_DECL(_net_inet_esp);
SYSCTL_INT(_net_inet_esp, OID_AUTO, esp_enable,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(esp_enable), 0, "");
SYSCTL_VNET_PCPUSTAT(_net_inet_esp, IPSECCTL_STATS, stats,
    struct espstat, espstat,
    "ESP statistics (struct espstat, netipsec/esp_var.h");

static int esp_input_cb(struct cryptop *op);
static int esp_output_cb(struct cryptop *crp);

size_t
esp_hdrsiz(struct secasvar *sav)
{
	size_t size;

	if (sav != NULL) {
		/*XXX not right for null algorithm--does it matter??*/
		IPSEC_ASSERT(sav->tdb_encalgxform != NULL,
			("SA with null xform"));
		if (sav->flags & SADB_X_EXT_OLD)
			size = sizeof (struct esp);
		else
			size = sizeof (struct newesp);
		size += sav->tdb_encalgxform->blocksize + 9;
		/*XXX need alg check???*/
		if (sav->tdb_authalgxform != NULL && sav->replay)
			size += ah_hdrsiz(sav);
	} else {
		/*
		 *   base header size
		 * + max iv length for CBC mode
		 * + max pad length
		 * + sizeof (pad length field)
		 * + sizeof (next header field)
		 * + max icv supported.
		 */
		size = sizeof (struct newesp) + EALG_MAX_BLOCK_LEN + 9 + 16;
	}
	return size;
}

/*
 * esp_init() is called when an SPI is being set up.
 */
static int
esp_init(struct secasvar *sav, struct xformsw *xsp)
{
	const struct enc_xform *txform;
	struct cryptoini cria, crie;
	int keylen;
	int error;

	txform = enc_algorithm_lookup(sav->alg_enc);
	if (txform == NULL) {
		DPRINTF(("%s: unsupported encryption algorithm %d\n",
			__func__, sav->alg_enc));
		return EINVAL;
	}
	if (sav->key_enc == NULL) {
		DPRINTF(("%s: no encoding key for %s algorithm\n",
			 __func__, txform->name));
		return EINVAL;
	}
	if ((sav->flags & (SADB_X_EXT_OLD | SADB_X_EXT_IV4B)) ==
	    SADB_X_EXT_IV4B) {
		DPRINTF(("%s: 4-byte IV not supported with protocol\n",
			__func__));
		return EINVAL;
	}
	/* subtract off the salt, RFC4106, 8.1 and RFC3686, 5.1 */
	keylen = _KEYLEN(sav->key_enc) - SAV_ISCTRORGCM(sav) * 4;
	if (txform->minkey > keylen || keylen > txform->maxkey) {
		DPRINTF(("%s: invalid key length %u, must be in the range "
			"[%u..%u] for algorithm %s\n", __func__,
			keylen, txform->minkey, txform->maxkey,
			txform->name));
		return EINVAL;
	}

	if (SAV_ISCTRORGCM(sav))
		sav->ivlen = 8;	/* RFC4106 3.1 and RFC3686 3.1 */
	else
		sav->ivlen = txform->ivsize;

	/*
	 * Setup AH-related state.
	 */
	if (sav->alg_auth != 0) {
		error = ah_init0(sav, xsp, &cria);
		if (error)
			return error;
	}

	/* NB: override anything set in ah_init0 */
	sav->tdb_xform = xsp;
	sav->tdb_encalgxform = txform;

	/*
	 * Whenever AES-GCM is used for encryption, one
	 * of the AES authentication algorithms is chosen
	 * as well, based on the key size.
	 */
	if (sav->alg_enc == SADB_X_EALG_AESGCM16) {
		switch (keylen) {
		case AES_128_GMAC_KEY_LEN:
			sav->alg_auth = SADB_X_AALG_AES128GMAC;
			sav->tdb_authalgxform = &auth_hash_nist_gmac_aes_128;
			break;
		case AES_192_GMAC_KEY_LEN:
			sav->alg_auth = SADB_X_AALG_AES192GMAC;
			sav->tdb_authalgxform = &auth_hash_nist_gmac_aes_192;
			break;
		case AES_256_GMAC_KEY_LEN:
			sav->alg_auth = SADB_X_AALG_AES256GMAC;
			sav->tdb_authalgxform = &auth_hash_nist_gmac_aes_256;
			break;
		default:
			DPRINTF(("%s: invalid key length %u"
				 "for algorithm %s\n", __func__,
				 keylen, txform->name));
			return EINVAL;
		}
		bzero(&cria, sizeof(cria));
		cria.cri_alg = sav->tdb_authalgxform->type;
		cria.cri_key = sav->key_enc->key_data;
		cria.cri_klen = _KEYBITS(sav->key_enc) - SAV_ISGCM(sav) * 32;
	}

	/* Initialize crypto session. */
	bzero(&crie, sizeof(crie));
	crie.cri_alg = sav->tdb_encalgxform->type;
	crie.cri_key = sav->key_enc->key_data;
	crie.cri_klen = _KEYBITS(sav->key_enc) - SAV_ISCTRORGCM(sav) * 32;

	if (sav->tdb_authalgxform && sav->tdb_encalgxform) {
		/* init both auth & enc */
		crie.cri_next = &cria;
		error = crypto_newsession(&sav->tdb_cryptoid,
					  &crie, V_crypto_support);
	} else if (sav->tdb_encalgxform) {
		error = crypto_newsession(&sav->tdb_cryptoid,
					  &crie, V_crypto_support);
	} else if (sav->tdb_authalgxform) {
		error = crypto_newsession(&sav->tdb_cryptoid,
					  &cria, V_crypto_support);
	} else {
		/* XXX cannot happen? */
		DPRINTF(("%s: no encoding OR authentication xform!\n",
			__func__));
		error = EINVAL;
	}
	return error;
}

/*
 * Paranoia.
 */
static int
esp_zeroize(struct secasvar *sav)
{
	/* NB: ah_zerorize free's the crypto session state */
	int error = ah_zeroize(sav);

	if (sav->key_enc)
		bzero(sav->key_enc->key_data, _KEYLEN(sav->key_enc));
	sav->tdb_encalgxform = NULL;
	sav->tdb_xform = NULL;
	return error;
}

/*
 * ESP input processing, called (eventually) through the protocol switch.
 */
static int
esp_input(struct mbuf *m, struct secasvar *sav, int skip, int protoff)
{
	IPSEC_DEBUG_DECLARE(char buf[128]);
	const struct auth_hash *esph;
	const struct enc_xform *espx;
	struct xform_data *xd;
	struct cryptodesc *crde;
	struct cryptop *crp;
	struct newesp *esp;
	uint8_t *ivp;
	crypto_session_t cryptoid;
	int alen, error, hlen, plen;

	IPSEC_ASSERT(sav != NULL, ("null SA"));
	IPSEC_ASSERT(sav->tdb_encalgxform != NULL, ("null encoding xform"));

	error = EINVAL;
	/* Valid IP Packet length ? */
	if ( (skip&3) || (m->m_pkthdr.len&3) ){
		DPRINTF(("%s: misaligned packet, skip %u pkt len %u",
				__func__, skip, m->m_pkthdr.len));
		ESPSTAT_INC(esps_badilen);
		goto bad;
	}
	/* XXX don't pullup, just copy header */
	IP6_EXTHDR_GET(esp, struct newesp *, m, skip, sizeof (struct newesp));

	esph = sav->tdb_authalgxform;
	espx = sav->tdb_encalgxform;

	/* Determine the ESP header and auth length */
	if (sav->flags & SADB_X_EXT_OLD)
		hlen = sizeof (struct esp) + sav->ivlen;
	else
		hlen = sizeof (struct newesp) + sav->ivlen;

	alen = xform_ah_authsize(esph);

	/*
	 * Verify payload length is multiple of encryption algorithm
	 * block size.
	 *
	 * NB: This works for the null algorithm because the blocksize
	 *     is 4 and all packets must be 4-byte aligned regardless
	 *     of the algorithm.
	 */
	plen = m->m_pkthdr.len - (skip + hlen + alen);
	if ((plen & (espx->blocksize - 1)) || (plen <= 0)) {
		DPRINTF(("%s: payload of %d octets not a multiple of %d octets,"
		    "  SA %s/%08lx\n", __func__, plen, espx->blocksize,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long)ntohl(sav->spi)));
		ESPSTAT_INC(esps_badilen);
		goto bad;
	}

	/*
	 * Check sequence number.
	 */
	SECASVAR_LOCK(sav);
	if (esph != NULL && sav->replay != NULL && sav->replay->wsize != 0) {
		if (ipsec_chkreplay(ntohl(esp->esp_seq), sav) == 0) {
			SECASVAR_UNLOCK(sav);
			DPRINTF(("%s: packet replay check for %s\n", __func__,
			    ipsec_sa2str(sav, buf, sizeof(buf))));
			ESPSTAT_INC(esps_replay);
			error = EACCES;
			goto bad;
		}
	}
	cryptoid = sav->tdb_cryptoid;
	SECASVAR_UNLOCK(sav);

	/* Update the counters */
	ESPSTAT_ADD(esps_ibytes, m->m_pkthdr.len - (skip + hlen + alen));

	/* Get crypto descriptors */
	crp = crypto_getreq(esph && espx ? 2 : 1);
	if (crp == NULL) {
		DPRINTF(("%s: failed to acquire crypto descriptors\n",
			__func__));
		ESPSTAT_INC(esps_crypto);
		error = ENOBUFS;
		goto bad;
	}

	/* Get IPsec-specific opaque pointer */
	xd = malloc(sizeof(*xd) + alen, M_XDATA, M_NOWAIT | M_ZERO);
	if (xd == NULL) {
		DPRINTF(("%s: failed to allocate xform_data\n", __func__));
		ESPSTAT_INC(esps_crypto);
		crypto_freereq(crp);
		error = ENOBUFS;
		goto bad;
	}

	if (esph != NULL) {
		struct cryptodesc *crda = crp->crp_desc;

		IPSEC_ASSERT(crda != NULL, ("null ah crypto descriptor"));

		/* Authentication descriptor */
		crda->crd_skip = skip;
		if (SAV_ISGCM(sav))
			crda->crd_len = 8;	/* RFC4106 5, SPI + SN */
		else
			crda->crd_len = m->m_pkthdr.len - (skip + alen);
		crda->crd_inject = m->m_pkthdr.len - alen;

		crda->crd_alg = esph->type;

		/* Copy the authenticator */
		m_copydata(m, m->m_pkthdr.len - alen, alen,
		    (caddr_t) (xd + 1));

		/* Chain authentication request */
		crde = crda->crd_next;
	} else {
		crde = crp->crp_desc;
	}

	/* Crypto operation descriptor */
	crp->crp_ilen = m->m_pkthdr.len; /* Total input length */
	crp->crp_flags = CRYPTO_F_IMBUF | CRYPTO_F_CBIFSYNC;
	if (V_async_crypto)
		crp->crp_flags |= CRYPTO_F_ASYNC | CRYPTO_F_ASYNC_KEEPORDER;
	crp->crp_buf = (caddr_t) m;
	crp->crp_callback = esp_input_cb;
	crp->crp_session = cryptoid;
	crp->crp_opaque = (caddr_t) xd;

	/* These are passed as-is to the callback */
	xd->sav = sav;
	xd->protoff = protoff;
	xd->skip = skip;
	xd->cryptoid = cryptoid;
	xd->vnet = curvnet;

	/* Decryption descriptor */
	IPSEC_ASSERT(crde != NULL, ("null esp crypto descriptor"));
	crde->crd_skip = skip + hlen;
	crde->crd_len = m->m_pkthdr.len - (skip + hlen + alen);
	crde->crd_inject = skip + hlen - sav->ivlen;

	if (SAV_ISCTRORGCM(sav)) {
		ivp = &crde->crd_iv[0];

		/* GCM IV Format: RFC4106 4 */
		/* CTR IV Format: RFC3686 4 */
		/* Salt is last four bytes of key, RFC4106 8.1 */
		/* Nonce is last four bytes of key, RFC3686 5.1 */
		memcpy(ivp, sav->key_enc->key_data +
		    _KEYLEN(sav->key_enc) - 4, 4);

		if (SAV_ISCTR(sav)) {
			/* Initial block counter is 1, RFC3686 4 */
			be32enc(&ivp[sav->ivlen + 4], 1);
		}

		m_copydata(m, skip + hlen - sav->ivlen, sav->ivlen, &ivp[4]);
		crde->crd_flags |= CRD_F_IV_EXPLICIT;
	}

	crde->crd_alg = espx->type;

	return (crypto_dispatch(crp));
bad:
	m_freem(m);
	key_freesav(&sav);
	return (error);
}

/*
 * ESP input callback from the crypto driver.
 */
static int
esp_input_cb(struct cryptop *crp)
{
	IPSEC_DEBUG_DECLARE(char buf[128]);
	u_int8_t lastthree[3], aalg[AH_HMAC_MAXHASHLEN];
	const struct auth_hash *esph;
	struct mbuf *m;
	struct cryptodesc *crd;
	struct xform_data *xd;
	struct secasvar *sav;
	struct secasindex *saidx;
	caddr_t ptr;
	crypto_session_t cryptoid;
	int hlen, skip, protoff, error, alen;

	crd = crp->crp_desc;
	IPSEC_ASSERT(crd != NULL, ("null crypto descriptor!"));

	m = (struct mbuf *) crp->crp_buf;
	xd = (struct xform_data *) crp->crp_opaque;
	CURVNET_SET(xd->vnet);
	sav = xd->sav;
	skip = xd->skip;
	protoff = xd->protoff;
	cryptoid = xd->cryptoid;
	saidx = &sav->sah->saidx;
	esph = sav->tdb_authalgxform;

	/* Check for crypto errors */
	if (crp->crp_etype) {
		if (crp->crp_etype == EAGAIN) {
			/* Reset the session ID */
			if (ipsec_updateid(sav, &crp->crp_session, &cryptoid) != 0)
				crypto_freesession(cryptoid);
			xd->cryptoid = crp->crp_session;
			CURVNET_RESTORE();
			return (crypto_dispatch(crp));
		}
		ESPSTAT_INC(esps_noxform);
		DPRINTF(("%s: crypto error %d\n", __func__, crp->crp_etype));
		error = crp->crp_etype;
		goto bad;
	}

	/* Shouldn't happen... */
	if (m == NULL) {
		ESPSTAT_INC(esps_crypto);
		DPRINTF(("%s: bogus returned buffer from crypto\n", __func__));
		error = EINVAL;
		goto bad;
	}
	ESPSTAT_INC(esps_hist[sav->alg_enc]);

	/* If authentication was performed, check now. */
	if (esph != NULL) {
		alen = xform_ah_authsize(esph);
		AHSTAT_INC(ahs_hist[sav->alg_auth]);
		/* Copy the authenticator from the packet */
		m_copydata(m, m->m_pkthdr.len - alen, alen, aalg);
		ptr = (caddr_t) (xd + 1);

		/* Verify authenticator */
		if (timingsafe_bcmp(ptr, aalg, alen) != 0) {
			DPRINTF(("%s: authentication hash mismatch for "
			    "packet in SA %s/%08lx\n", __func__,
			    ipsec_address(&saidx->dst, buf, sizeof(buf)),
			    (u_long) ntohl(sav->spi)));
			ESPSTAT_INC(esps_badauth);
			error = EACCES;
			goto bad;
		}
		m->m_flags |= M_AUTHIPDGM;
		/* Remove trailing authenticator */
		m_adj(m, -alen);
	}

	/* Release the crypto descriptors */
	free(xd, M_XDATA), xd = NULL;
	crypto_freereq(crp), crp = NULL;

	/*
	 * Packet is now decrypted.
	 */
	m->m_flags |= M_DECRYPTED;

	/*
	 * Update replay sequence number, if appropriate.
	 */
	if (sav->replay) {
		u_int32_t seq;

		m_copydata(m, skip + offsetof(struct newesp, esp_seq),
			   sizeof (seq), (caddr_t) &seq);
		SECASVAR_LOCK(sav);
		if (ipsec_updatereplay(ntohl(seq), sav)) {
			SECASVAR_UNLOCK(sav);
			DPRINTF(("%s: packet replay check for %s\n", __func__,
			    ipsec_sa2str(sav, buf, sizeof(buf))));
			ESPSTAT_INC(esps_replay);
			error = EACCES;
			goto bad;
		}
		SECASVAR_UNLOCK(sav);
	}

	/* Determine the ESP header length */
	if (sav->flags & SADB_X_EXT_OLD)
		hlen = sizeof (struct esp) + sav->ivlen;
	else
		hlen = sizeof (struct newesp) + sav->ivlen;

	/* Remove the ESP header and IV from the mbuf. */
	error = m_striphdr(m, skip, hlen);
	if (error) {
		ESPSTAT_INC(esps_hdrops);
		DPRINTF(("%s: bad mbuf chain, SA %s/%08lx\n", __func__,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		goto bad;
	}

	/* Save the last three bytes of decrypted data */
	m_copydata(m, m->m_pkthdr.len - 3, 3, lastthree);

	/* Verify pad length */
	if (lastthree[1] + 2 > m->m_pkthdr.len - skip) {
		ESPSTAT_INC(esps_badilen);
		DPRINTF(("%s: invalid padding length %d for %u byte packet "
		    "in SA %s/%08lx\n", __func__, lastthree[1],
		    m->m_pkthdr.len - skip,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		error = EINVAL;
		goto bad;
	}

	/* Verify correct decryption by checking the last padding bytes */
	if ((sav->flags & SADB_X_EXT_PMASK) != SADB_X_EXT_PRAND) {
		if (lastthree[1] != lastthree[0] && lastthree[1] != 0) {
			ESPSTAT_INC(esps_badenc);
			DPRINTF(("%s: decryption failed for packet in "
			    "SA %s/%08lx\n", __func__, ipsec_address(
			    &sav->sah->saidx.dst, buf, sizeof(buf)),
			    (u_long) ntohl(sav->spi)));
			error = EINVAL;
			goto bad;
		}
	}

	/* Trim the mbuf chain to remove trailing authenticator and padding */
	m_adj(m, -(lastthree[1] + 2));

	/* Restore the Next Protocol field */
	m_copyback(m, protoff, sizeof (u_int8_t), lastthree + 2);

	switch (saidx->dst.sa.sa_family) {
#ifdef INET6
	case AF_INET6:
		error = ipsec6_common_input_cb(m, sav, skip, protoff);
		break;
#endif
#ifdef INET
	case AF_INET:
		error = ipsec4_common_input_cb(m, sav, skip, protoff);
		break;
#endif
	default:
		panic("%s: Unexpected address family: %d saidx=%p", __func__,
		    saidx->dst.sa.sa_family, saidx);
	}
	CURVNET_RESTORE();
	return error;
bad:
	CURVNET_RESTORE();
	if (sav != NULL)
		key_freesav(&sav);
	if (m != NULL)
		m_freem(m);
	if (xd != NULL)
		free(xd, M_XDATA);
	if (crp != NULL)
		crypto_freereq(crp);
	return error;
}
/*
 * ESP output routine, called by ipsec[46]_perform_request().
 */
static int
esp_output(struct mbuf *m, struct secpolicy *sp, struct secasvar *sav,
    u_int idx, int skip, int protoff)
{
	IPSEC_DEBUG_DECLARE(char buf[IPSEC_ADDRSTRLEN]);
	struct cryptodesc *crde = NULL, *crda = NULL;
	struct cryptop *crp;
	const struct auth_hash *esph;
	const struct enc_xform *espx;
	struct mbuf *mo = NULL;
	struct xform_data *xd;
	struct secasindex *saidx;
	unsigned char *pad;
	uint8_t *ivp;
	uint64_t cntr;
	crypto_session_t cryptoid;
	int hlen, rlen, padding, blks, alen, i, roff;
	int error, maxpacketsize;
	uint8_t prot;

	IPSEC_ASSERT(sav != NULL, ("null SA"));
	esph = sav->tdb_authalgxform;
	espx = sav->tdb_encalgxform;
	IPSEC_ASSERT(espx != NULL, ("null encoding xform"));

	if (sav->flags & SADB_X_EXT_OLD)
		hlen = sizeof (struct esp) + sav->ivlen;
	else
		hlen = sizeof (struct newesp) + sav->ivlen;

	rlen = m->m_pkthdr.len - skip;	/* Raw payload length. */
	/*
	 * RFC4303 2.4 Requires 4 byte alignment.
	 */
	blks = MAX(4, espx->blocksize);		/* Cipher blocksize */

	/* XXX clamp padding length a la KAME??? */
	padding = ((blks - ((rlen + 2) % blks)) % blks) + 2;

	alen = xform_ah_authsize(esph);

	ESPSTAT_INC(esps_output);

	saidx = &sav->sah->saidx;
	/* Check for maximum packet size violations. */
	switch (saidx->dst.sa.sa_family) {
#ifdef INET
	case AF_INET:
		maxpacketsize = IP_MAXPACKET;
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		maxpacketsize = IPV6_MAXPACKET;
		break;
#endif /* INET6 */
	default:
		DPRINTF(("%s: unknown/unsupported protocol "
		    "family %d, SA %s/%08lx\n", __func__,
		    saidx->dst.sa.sa_family, ipsec_address(&saidx->dst,
			buf, sizeof(buf)), (u_long) ntohl(sav->spi)));
		ESPSTAT_INC(esps_nopf);
		error = EPFNOSUPPORT;
		goto bad;
	}
	/*
	DPRINTF(("%s: skip %d hlen %d rlen %d padding %d alen %d blksd %d\n",
		__func__, skip, hlen, rlen, padding, alen, blks)); */
	if (skip + hlen + rlen + padding + alen > maxpacketsize) {
		DPRINTF(("%s: packet in SA %s/%08lx got too big "
		    "(len %u, max len %u)\n", __func__,
		    ipsec_address(&saidx->dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi),
		    skip + hlen + rlen + padding + alen, maxpacketsize));
		ESPSTAT_INC(esps_toobig);
		error = EMSGSIZE;
		goto bad;
	}

	/* Update the counters. */
	ESPSTAT_ADD(esps_obytes, m->m_pkthdr.len - skip);

	m = m_unshare(m, M_NOWAIT);
	if (m == NULL) {
		DPRINTF(("%s: cannot clone mbuf chain, SA %s/%08lx\n", __func__,
		    ipsec_address(&saidx->dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		ESPSTAT_INC(esps_hdrops);
		error = ENOBUFS;
		goto bad;
	}

	/* Inject ESP header. */
	mo = m_makespace(m, skip, hlen, &roff);
	if (mo == NULL) {
		DPRINTF(("%s: %u byte ESP hdr inject failed for SA %s/%08lx\n",
		    __func__, hlen, ipsec_address(&saidx->dst, buf,
		    sizeof(buf)), (u_long) ntohl(sav->spi)));
		ESPSTAT_INC(esps_hdrops);	/* XXX diffs from openbsd */
		error = ENOBUFS;
		goto bad;
	}

	/* Initialize ESP header. */
	bcopy((caddr_t) &sav->spi, mtod(mo, caddr_t) + roff,
	    sizeof(uint32_t));
	SECASVAR_LOCK(sav);
	if (sav->replay) {
		uint32_t replay;

#ifdef REGRESSION
		/* Emulate replay attack when ipsec_replay is TRUE. */
		if (!V_ipsec_replay)
#endif
			sav->replay->count++;
		replay = htonl(sav->replay->count);

		bcopy((caddr_t) &replay, mtod(mo, caddr_t) + roff +
		    sizeof(uint32_t), sizeof(uint32_t));
	}
	cryptoid = sav->tdb_cryptoid;
	if (SAV_ISCTRORGCM(sav))
		cntr = sav->cntr++;
	SECASVAR_UNLOCK(sav);

	/*
	 * Add padding -- better to do it ourselves than use the crypto engine,
	 * although if/when we support compression, we'd have to do that.
	 */
	pad = (u_char *) m_pad(m, padding + alen);
	if (pad == NULL) {
		DPRINTF(("%s: m_pad failed for SA %s/%08lx\n", __func__,
		    ipsec_address(&saidx->dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		m = NULL;		/* NB: free'd by m_pad */
		error = ENOBUFS;
		goto bad;
	}

	/*
	 * Add padding: random, zero, or self-describing.
	 * XXX catch unexpected setting
	 */
	switch (sav->flags & SADB_X_EXT_PMASK) {
	case SADB_X_EXT_PRAND:
		(void) read_random(pad, padding - 2);
		break;
	case SADB_X_EXT_PZERO:
		bzero(pad, padding - 2);
		break;
	case SADB_X_EXT_PSEQ:
		for (i = 0; i < padding - 2; i++)
			pad[i] = i+1;
		break;
	}

	/* Fix padding length and Next Protocol in padding itself. */
	pad[padding - 2] = padding - 2;
	m_copydata(m, protoff, sizeof(u_int8_t), pad + padding - 1);

	/* Fix Next Protocol in IPv4/IPv6 header. */
	prot = IPPROTO_ESP;
	m_copyback(m, protoff, sizeof(u_int8_t), (u_char *) &prot);

	/* Get crypto descriptors. */
	crp = crypto_getreq(esph != NULL ? 2 : 1);
	if (crp == NULL) {
		DPRINTF(("%s: failed to acquire crypto descriptors\n",
			__func__));
		ESPSTAT_INC(esps_crypto);
		error = ENOBUFS;
		goto bad;
	}

	/* IPsec-specific opaque crypto info. */
	xd =  malloc(sizeof(struct xform_data), M_XDATA, M_NOWAIT | M_ZERO);
	if (xd == NULL) {
		crypto_freereq(crp);
		DPRINTF(("%s: failed to allocate xform_data\n", __func__));
		ESPSTAT_INC(esps_crypto);
		error = ENOBUFS;
		goto bad;
	}

	crde = crp->crp_desc;
	crda = crde->crd_next;

	/* Encryption descriptor. */
	crde->crd_skip = skip + hlen;
	crde->crd_len = m->m_pkthdr.len - (skip + hlen + alen);
	crde->crd_flags = CRD_F_ENCRYPT;
	crde->crd_inject = skip + hlen - sav->ivlen;

	/* Encryption operation. */
	crde->crd_alg = espx->type;
	if (SAV_ISCTRORGCM(sav)) {
		ivp = &crde->crd_iv[0];

		/* GCM IV Format: RFC4106 4 */
		/* CTR IV Format: RFC3686 4 */
		/* Salt is last four bytes of key, RFC4106 8.1 */
		/* Nonce is last four bytes of key, RFC3686 5.1 */
		memcpy(ivp, sav->key_enc->key_data +
		    _KEYLEN(sav->key_enc) - 4, 4);
		be64enc(&ivp[4], cntr);
		if (SAV_ISCTR(sav)) {
			/* Initial block counter is 1, RFC3686 4 */
			/* XXXAE: should we use this only for first packet? */
			be32enc(&ivp[sav->ivlen + 4], 1);
		}

		m_copyback(m, skip + hlen - sav->ivlen, sav->ivlen, &ivp[4]);
		crde->crd_flags |= CRD_F_IV_EXPLICIT|CRD_F_IV_PRESENT;
	}

	/* Callback parameters */
	xd->sp = sp;
	xd->sav = sav;
	xd->idx = idx;
	xd->cryptoid = cryptoid;
	xd->vnet = curvnet;

	/* Crypto operation descriptor. */
	crp->crp_ilen = m->m_pkthdr.len; /* Total input length. */
	crp->crp_flags = CRYPTO_F_IMBUF | CRYPTO_F_CBIFSYNC;
	if (V_async_crypto)
		crp->crp_flags |= CRYPTO_F_ASYNC | CRYPTO_F_ASYNC_KEEPORDER;
	crp->crp_buf = (caddr_t) m;
	crp->crp_callback = esp_output_cb;
	crp->crp_opaque = (caddr_t) xd;
	crp->crp_session = cryptoid;

	if (esph) {
		/* Authentication descriptor. */
		crda->crd_alg = esph->type;
		crda->crd_skip = skip;
		if (SAV_ISGCM(sav))
			crda->crd_len = 8;	/* RFC4106 5, SPI + SN */
		else
			crda->crd_len = m->m_pkthdr.len - (skip + alen);
		crda->crd_inject = m->m_pkthdr.len - alen;
	}

	return crypto_dispatch(crp);
bad:
	if (m)
		m_freem(m);
	key_freesav(&sav);
	key_freesp(&sp);
	return (error);
}
/*
 * ESP output callback from the crypto driver.
 */
static int
esp_output_cb(struct cryptop *crp)
{
	struct xform_data *xd;
	struct secpolicy *sp;
	struct secasvar *sav;
	struct mbuf *m;
	crypto_session_t cryptoid;
	u_int idx;
	int error;

	xd = (struct xform_data *) crp->crp_opaque;
	CURVNET_SET(xd->vnet);
	m = (struct mbuf *) crp->crp_buf;
	sp = xd->sp;
	sav = xd->sav;
	idx = xd->idx;
	cryptoid = xd->cryptoid;

	/* Check for crypto errors. */
	if (crp->crp_etype) {
		if (crp->crp_etype == EAGAIN) {
			/* Reset the session ID */
			if (ipsec_updateid(sav, &crp->crp_session, &cryptoid) != 0)
				crypto_freesession(cryptoid);
			xd->cryptoid = crp->crp_session;
			CURVNET_RESTORE();
			return (crypto_dispatch(crp));
		}
		ESPSTAT_INC(esps_noxform);
		DPRINTF(("%s: crypto error %d\n", __func__, crp->crp_etype));
		error = crp->crp_etype;
		m_freem(m);
		goto bad;
	}

	/* Shouldn't happen... */
	if (m == NULL) {
		ESPSTAT_INC(esps_crypto);
		DPRINTF(("%s: bogus returned buffer from crypto\n", __func__));
		error = EINVAL;
		goto bad;
	}
	free(xd, M_XDATA);
	crypto_freereq(crp);
	ESPSTAT_INC(esps_hist[sav->alg_enc]);
	if (sav->tdb_authalgxform != NULL)
		AHSTAT_INC(ahs_hist[sav->alg_auth]);

#ifdef REGRESSION
	/* Emulate man-in-the-middle attack when ipsec_integrity is TRUE. */
	if (V_ipsec_integrity) {
		static unsigned char ipseczeroes[AH_HMAC_MAXHASHLEN];
		const struct auth_hash *esph;

		/*
		 * Corrupt HMAC if we want to test integrity verification of
		 * the other side.
		 */
		esph = sav->tdb_authalgxform;
		if (esph !=  NULL) {
			int alen;

			alen = xform_ah_authsize(esph);
			m_copyback(m, m->m_pkthdr.len - alen,
			    alen, ipseczeroes);
		}
	}
#endif

	/* NB: m is reclaimed by ipsec_process_done. */
	error = ipsec_process_done(m, sp, sav, idx);
	CURVNET_RESTORE();
	return (error);
bad:
	CURVNET_RESTORE();
	free(xd, M_XDATA);
	crypto_freereq(crp);
	key_freesav(&sav);
	key_freesp(&sp);
	return (error);
}

static struct xformsw esp_xformsw = {
	.xf_type =	XF_ESP,
	.xf_name =	"IPsec ESP",
	.xf_init =	esp_init,
	.xf_zeroize =	esp_zeroize,
	.xf_input =	esp_input,
	.xf_output =	esp_output,
};

SYSINIT(esp_xform_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_MIDDLE,
    xform_attach, &esp_xformsw);
SYSUNINIT(esp_xform_uninit, SI_SUB_PROTO_DOMAIN, SI_ORDER_MIDDLE,
    xform_detach, &esp_xformsw);
