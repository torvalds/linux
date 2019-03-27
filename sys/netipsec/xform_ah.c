/*	$FreeBSD$	*/
/*	$OpenBSD: ip_ah.c,v 1.63 2001/06/26 06:18:58 angelos Exp $ */
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
 * Additional features in 1999 by Angelos D. Keromytis and Niklas Hallqvist.
 *
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
 * Copyright (c) 1999 Niklas Hallqvist.
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
#include <sys/mutex.h>
#include <sys/sysctl.h>

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
#include <netipsec/xform.h>

#ifdef INET6
#include <netinet6/ip6_var.h>
#include <netipsec/ipsec6.h>
#include <netinet6/ip6_ecn.h>
#endif

#include <netipsec/key.h>
#include <netipsec/key_debug.h>

#include <opencrypto/cryptodev.h>

/*
 * Return header size in bytes.  The old protocol did not support
 * the replay counter; the new protocol always includes the counter.
 */
#define HDRSIZE(sav) \
	(((sav)->flags & SADB_X_EXT_OLD) ? \
		sizeof (struct ah) : sizeof (struct ah) + sizeof (u_int32_t))
/* 
 * Return authenticator size in bytes, based on a field in the
 * algorithm descriptor.
 */
#define	AUTHSIZE(sav)	((sav->flags & SADB_X_EXT_OLD) ? 16 :	\
			 xform_ah_authsize((sav)->tdb_authalgxform))

VNET_DEFINE(int, ah_enable) = 1;	/* control flow of packets with AH */
VNET_DEFINE(int, ah_cleartos) = 1;	/* clear ip_tos when doing AH calc */
VNET_PCPUSTAT_DEFINE(struct ahstat, ahstat);
VNET_PCPUSTAT_SYSINIT(ahstat);

#ifdef VIMAGE
VNET_PCPUSTAT_SYSUNINIT(ahstat);
#endif /* VIMAGE */

#ifdef INET
SYSCTL_DECL(_net_inet_ah);
SYSCTL_INT(_net_inet_ah, OID_AUTO, ah_enable,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ah_enable), 0, "");
SYSCTL_INT(_net_inet_ah, OID_AUTO, ah_cleartos,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ah_cleartos), 0, "");
SYSCTL_VNET_PCPUSTAT(_net_inet_ah, IPSECCTL_STATS, stats, struct ahstat,
    ahstat, "AH statistics (struct ahstat, netipsec/ah_var.h)");
#endif

static unsigned char ipseczeroes[256];	/* larger than an ip6 extension hdr */

static int ah_input_cb(struct cryptop*);
static int ah_output_cb(struct cryptop*);

int
xform_ah_authsize(const struct auth_hash *esph)
{
	int alen;

	if (esph == NULL)
		return 0;

	switch (esph->type) {
	case CRYPTO_SHA2_256_HMAC:
	case CRYPTO_SHA2_384_HMAC:
	case CRYPTO_SHA2_512_HMAC:
		alen = esph->hashsize / 2;	/* RFC4868 2.3 */
		break;

	case CRYPTO_AES_128_NIST_GMAC:
	case CRYPTO_AES_192_NIST_GMAC:
	case CRYPTO_AES_256_NIST_GMAC:
		alen = esph->hashsize;
		break;

	default:
		alen = AH_HMAC_HASHLEN;
		break;
	}

	return alen;
}

size_t
ah_hdrsiz(struct secasvar *sav)
{
	size_t size;

	if (sav != NULL) {
		int authsize, rplen, align;

		IPSEC_ASSERT(sav->tdb_authalgxform != NULL, ("null xform"));
		/*XXX not right for null algorithm--does it matter??*/

		/* RFC4302: use the correct alignment. */
		align = sizeof(uint32_t);
#ifdef INET6
		if (sav->sah->saidx.dst.sa.sa_family == AF_INET6) {
			align = sizeof(uint64_t);
		}
#endif
		rplen = HDRSIZE(sav);
		authsize = AUTHSIZE(sav);
		size = roundup(rplen + authsize, align);
	} else {
		/* default guess */
		size = sizeof (struct ah) + sizeof (u_int32_t) + 16;
	}
	return size;
}

/*
 * NB: public for use by esp_init.
 */
int
ah_init0(struct secasvar *sav, struct xformsw *xsp, struct cryptoini *cria)
{
	const struct auth_hash *thash;
	int keylen;

	thash = auth_algorithm_lookup(sav->alg_auth);
	if (thash == NULL) {
		DPRINTF(("%s: unsupported authentication algorithm %u\n",
			__func__, sav->alg_auth));
		return EINVAL;
	}
	/*
	 * Verify the replay state block allocation is consistent with
	 * the protocol type.  We check here so we can make assumptions
	 * later during protocol processing.
	 */
	/* NB: replay state is setup elsewhere (sigh) */
	if (((sav->flags&SADB_X_EXT_OLD) == 0) ^ (sav->replay != NULL)) {
		DPRINTF(("%s: replay state block inconsistency, "
			"%s algorithm %s replay state\n", __func__,
			(sav->flags & SADB_X_EXT_OLD) ? "old" : "new",
			sav->replay == NULL ? "without" : "with"));
		return EINVAL;
	}
	if (sav->key_auth == NULL) {
		DPRINTF(("%s: no authentication key for %s algorithm\n",
			__func__, thash->name));
		return EINVAL;
	}
	keylen = _KEYLEN(sav->key_auth);
	if (keylen > thash->keysize && thash->keysize != 0) {
		DPRINTF(("%s: invalid keylength %d, algorithm %s requires "
			"keysize less than %d\n", __func__,
			 keylen, thash->name, thash->keysize));
		return EINVAL;
	}

	sav->tdb_xform = xsp;
	sav->tdb_authalgxform = thash;

	/* Initialize crypto session. */
	bzero(cria, sizeof (*cria));
	cria->cri_alg = sav->tdb_authalgxform->type;
	cria->cri_klen = _KEYBITS(sav->key_auth);
	cria->cri_key = sav->key_auth->key_data;
	cria->cri_mlen = AUTHSIZE(sav);

	return 0;
}

/*
 * ah_init() is called when an SPI is being set up.
 */
static int
ah_init(struct secasvar *sav, struct xformsw *xsp)
{
	struct cryptoini cria;
	int error;

	error = ah_init0(sav, xsp, &cria);
	return error ? error :
		 crypto_newsession(&sav->tdb_cryptoid, &cria, V_crypto_support);
}

/*
 * Paranoia.
 *
 * NB: public for use by esp_zeroize (XXX).
 */
int
ah_zeroize(struct secasvar *sav)
{

	if (sav->key_auth)
		bzero(sav->key_auth->key_data, _KEYLEN(sav->key_auth));

	crypto_freesession(sav->tdb_cryptoid);
	sav->tdb_cryptoid = NULL;
	sav->tdb_authalgxform = NULL;
	sav->tdb_xform = NULL;
	return 0;
}

/*
 * Massage IPv4/IPv6 headers for AH processing.
 */
static int
ah_massage_headers(struct mbuf **m0, int proto, int skip, int alg, int out)
{
	struct mbuf *m = *m0;
	unsigned char *ptr;
	int off, count;

#ifdef INET
	struct ip *ip;
#endif /* INET */

#ifdef INET6
	struct ip6_ext *ip6e;
	struct ip6_hdr ip6;
	int ad, alloc, nxt, noff;
#endif /* INET6 */

	switch (proto) {
#ifdef INET
	case AF_INET:
		/*
		 * This is the least painful way of dealing with IPv4 header
		 * and option processing -- just make sure they're in
		 * contiguous memory.
		 */
		*m0 = m = m_pullup(m, skip);
		if (m == NULL) {
			DPRINTF(("%s: m_pullup failed\n", __func__));
			return ENOBUFS;
		}

		/* Fix the IP header */
		ip = mtod(m, struct ip *);
		if (V_ah_cleartos)
			ip->ip_tos = 0;
		ip->ip_ttl = 0;
		ip->ip_sum = 0;

		if (alg == CRYPTO_MD5_KPDK || alg == CRYPTO_SHA1_KPDK)
			ip->ip_off &= htons(IP_DF);
		else
			ip->ip_off = htons(0);

		ptr = mtod(m, unsigned char *);

		/* IPv4 option processing */
		for (off = sizeof(struct ip); off < skip;) {
			if (ptr[off] == IPOPT_EOL || ptr[off] == IPOPT_NOP ||
			    off + 1 < skip)
				;
			else {
				DPRINTF(("%s: illegal IPv4 option length for "
					"option %d\n", __func__, ptr[off]));

				m_freem(m);
				return EINVAL;
			}

			switch (ptr[off]) {
			case IPOPT_EOL:
				off = skip;  /* End the loop. */
				break;

			case IPOPT_NOP:
				off++;
				break;

			case IPOPT_SECURITY:	/* 0x82 */
			case 0x85:	/* Extended security. */
			case 0x86:	/* Commercial security. */
			case 0x94:	/* Router alert */
			case 0x95:	/* RFC1770 */
				/* Sanity check for option length. */
				if (ptr[off + 1] < 2) {
					DPRINTF(("%s: illegal IPv4 option "
						"length for option %d\n",
						__func__, ptr[off]));

					m_freem(m);
					return EINVAL;
				}

				off += ptr[off + 1];
				break;

			case IPOPT_LSRR:
			case IPOPT_SSRR:
				/* Sanity check for option length. */
				if (ptr[off + 1] < 2) {
					DPRINTF(("%s: illegal IPv4 option "
						"length for option %d\n",
						__func__, ptr[off]));

					m_freem(m);
					return EINVAL;
				}

				/*
				 * On output, if we have either of the
				 * source routing options, we should
				 * swap the destination address of the
				 * IP header with the last address
				 * specified in the option, as that is
				 * what the destination's IP header
				 * will look like.
				 */
				if (out)
					bcopy(ptr + off + ptr[off + 1] -
					    sizeof(struct in_addr),
					    &(ip->ip_dst), sizeof(struct in_addr));

				/* Fall through */
			default:
				/* Sanity check for option length. */
				if (ptr[off + 1] < 2) {
					DPRINTF(("%s: illegal IPv4 option "
						"length for option %d\n",
						__func__, ptr[off]));
					m_freem(m);
					return EINVAL;
				}

				/* Zeroize all other options. */
				count = ptr[off + 1];
				bcopy(ipseczeroes, ptr + off, count);
				off += count;
				break;
			}

			/* Sanity check. */
			if (off > skip)	{
				DPRINTF(("%s: malformed IPv4 options header\n",
					__func__));

				m_freem(m);
				return EINVAL;
			}
		}

		break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:  /* Ugly... */
		/* Copy and "cook" the IPv6 header. */
		m_copydata(m, 0, sizeof(ip6), (caddr_t) &ip6);

		/* We don't do IPv6 Jumbograms. */
		if (ip6.ip6_plen == 0) {
			DPRINTF(("%s: unsupported IPv6 jumbogram\n", __func__));
			m_freem(m);
			return EMSGSIZE;
		}

		ip6.ip6_flow = 0;
		ip6.ip6_hlim = 0;
		ip6.ip6_vfc &= ~IPV6_VERSION_MASK;
		ip6.ip6_vfc |= IPV6_VERSION;

		/* Scoped address handling. */
		if (IN6_IS_SCOPE_LINKLOCAL(&ip6.ip6_src))
			ip6.ip6_src.s6_addr16[1] = 0;
		if (IN6_IS_SCOPE_LINKLOCAL(&ip6.ip6_dst))
			ip6.ip6_dst.s6_addr16[1] = 0;

		/* Done with IPv6 header. */
		m_copyback(m, 0, sizeof(struct ip6_hdr), (caddr_t) &ip6);

		/* Let's deal with the remaining headers (if any). */
		if (skip - sizeof(struct ip6_hdr) > 0) {
			if (m->m_len <= skip) {
				ptr = (unsigned char *) malloc(
				    skip - sizeof(struct ip6_hdr),
				    M_XDATA, M_NOWAIT);
				if (ptr == NULL) {
					DPRINTF(("%s: failed to allocate memory"
						"for IPv6 headers\n",__func__));
					m_freem(m);
					return ENOBUFS;
				}

				/*
				 * Copy all the protocol headers after
				 * the IPv6 header.
				 */
				m_copydata(m, sizeof(struct ip6_hdr),
				    skip - sizeof(struct ip6_hdr), ptr);
				alloc = 1;
			} else {
				/* No need to allocate memory. */
				ptr = mtod(m, unsigned char *) +
				    sizeof(struct ip6_hdr);
				alloc = 0;
			}
		} else
			break;

		nxt = ip6.ip6_nxt & 0xff; /* Next header type. */

		for (off = 0; off < skip - sizeof(struct ip6_hdr);)
			switch (nxt) {
			case IPPROTO_HOPOPTS:
			case IPPROTO_DSTOPTS:
				ip6e = (struct ip6_ext *)(ptr + off);
				noff = off + ((ip6e->ip6e_len + 1) << 3);

				/* Sanity check. */
				if (noff > skip - sizeof(struct ip6_hdr))
					goto error6;

				/*
				 * Zero out mutable options.
				 */
				for (count = off + sizeof(struct ip6_ext);
				     count < noff;) {
					if (ptr[count] == IP6OPT_PAD1) {
						count++;
						continue; /* Skip padding. */
					}

					ad = ptr[count + 1] + 2;
					if (count + ad > noff)
						goto error6;

					if (ptr[count] & IP6OPT_MUTABLE)
						memset(ptr + count, 0, ad);
					count += ad;
				}

				if (count != noff)
					goto error6;

				/* Advance. */
				off += ((ip6e->ip6e_len + 1) << 3);
				nxt = ip6e->ip6e_nxt;
				break;

			case IPPROTO_ROUTING:
				/*
				 * Always include routing headers in
				 * computation.
				 */
				ip6e = (struct ip6_ext *) (ptr + off);
				off += ((ip6e->ip6e_len + 1) << 3);
				nxt = ip6e->ip6e_nxt;
				break;

			default:
				DPRINTF(("%s: unexpected IPv6 header type %d",
					__func__, off));
error6:
				if (alloc)
					free(ptr, M_XDATA);
				m_freem(m);
				return EINVAL;
			}

		/* Copyback and free, if we allocated. */
		if (alloc) {
			m_copyback(m, sizeof(struct ip6_hdr),
			    skip - sizeof(struct ip6_hdr), ptr);
			free(ptr, M_XDATA);
		}

		break;
#endif /* INET6 */
	}

	return 0;
}

/*
 * ah_input() gets called to verify that an input packet
 * passes authentication.
 */
static int
ah_input(struct mbuf *m, struct secasvar *sav, int skip, int protoff)
{
	IPSEC_DEBUG_DECLARE(char buf[128]);
	const struct auth_hash *ahx;
	struct cryptodesc *crda;
	struct cryptop *crp;
	struct xform_data *xd;
	struct newah *ah;
	crypto_session_t cryptoid;
	int hl, rplen, authsize, ahsize, error;

	IPSEC_ASSERT(sav != NULL, ("null SA"));
	IPSEC_ASSERT(sav->key_auth != NULL, ("null authentication key"));
	IPSEC_ASSERT(sav->tdb_authalgxform != NULL,
		("null authentication xform"));

	/* Figure out header size. */
	rplen = HDRSIZE(sav);

	/* XXX don't pullup, just copy header */
	IP6_EXTHDR_GET(ah, struct newah *, m, skip, rplen);
	if (ah == NULL) {
		DPRINTF(("ah_input: cannot pullup header\n"));
		AHSTAT_INC(ahs_hdrops);		/*XXX*/
		error = ENOBUFS;
		goto bad;
	}

	/* Check replay window, if applicable. */
	SECASVAR_LOCK(sav);
	if (sav->replay != NULL && sav->replay->wsize != 0 &&
	    ipsec_chkreplay(ntohl(ah->ah_seq), sav) == 0) {
		SECASVAR_UNLOCK(sav);
		AHSTAT_INC(ahs_replay);
		DPRINTF(("%s: packet replay failure: %s\n", __func__,
		    ipsec_sa2str(sav, buf, sizeof(buf))));
		error = EACCES;
		goto bad;
	}
	cryptoid = sav->tdb_cryptoid;
	SECASVAR_UNLOCK(sav);

	/* Verify AH header length. */
	hl = sizeof(struct ah) + (ah->ah_len * sizeof (u_int32_t));
	ahx = sav->tdb_authalgxform;
	authsize = AUTHSIZE(sav);
	ahsize = ah_hdrsiz(sav);
	if (hl != ahsize) {
		DPRINTF(("%s: bad authenticator length %u (expecting %lu)"
		    " for packet in SA %s/%08lx\n", __func__, hl,
		    (u_long)ahsize,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		AHSTAT_INC(ahs_badauthl);
		error = EACCES;
		goto bad;
	}
	if (skip + ahsize > m->m_pkthdr.len) {
		DPRINTF(("%s: bad mbuf length %u (expecting %lu)"
		    " for packet in SA %s/%08lx\n", __func__,
		    m->m_pkthdr.len, (u_long)(skip + ahsize),
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		AHSTAT_INC(ahs_badauthl);
		error = EACCES;
		goto bad;
	}
	AHSTAT_ADD(ahs_ibytes, m->m_pkthdr.len - skip - hl);

	/* Get crypto descriptors. */
	crp = crypto_getreq(1);
	if (crp == NULL) {
		DPRINTF(("%s: failed to acquire crypto descriptor\n",
		    __func__));
		AHSTAT_INC(ahs_crypto);
		error = ENOBUFS;
		goto bad;
	}

	crda = crp->crp_desc;
	IPSEC_ASSERT(crda != NULL, ("null crypto descriptor"));

	crda->crd_skip = 0;
	crda->crd_len = m->m_pkthdr.len;
	crda->crd_inject = skip + rplen;

	/* Authentication operation. */
	crda->crd_alg = ahx->type;
	crda->crd_klen = _KEYBITS(sav->key_auth);
	crda->crd_key = sav->key_auth->key_data;

	/* Allocate IPsec-specific opaque crypto info. */
	xd = malloc(sizeof(*xd) + skip + rplen + authsize, M_XDATA,
	    M_NOWAIT | M_ZERO);
	if (xd == NULL) {
		DPRINTF(("%s: failed to allocate xform_data\n", __func__));
		AHSTAT_INC(ahs_crypto);
		crypto_freereq(crp);
		error = ENOBUFS;
		goto bad;
	}

	/*
	 * Save the authenticator, the skipped portion of the packet,
	 * and the AH header.
	 */
	m_copydata(m, 0, skip + rplen + authsize, (caddr_t)(xd + 1));

	/* Zeroize the authenticator on the packet. */
	m_copyback(m, skip + rplen, authsize, ipseczeroes);

	/* Save ah_nxt, since ah pointer can become invalid after "massage" */
	hl = ah->ah_nxt;

	/* "Massage" the packet headers for crypto processing. */
	error = ah_massage_headers(&m, sav->sah->saidx.dst.sa.sa_family,
	    skip, ahx->type, 0);
	if (error != 0) {
		/* NB: mbuf is free'd by ah_massage_headers */
		AHSTAT_INC(ahs_hdrops);
		free(xd, M_XDATA);
		crypto_freereq(crp);
		key_freesav(&sav);
		return (error);
	}

	/* Crypto operation descriptor. */
	crp->crp_ilen = m->m_pkthdr.len; /* Total input length. */
	crp->crp_flags = CRYPTO_F_IMBUF | CRYPTO_F_CBIFSYNC;
	if (V_async_crypto)
		crp->crp_flags |= CRYPTO_F_ASYNC | CRYPTO_F_ASYNC_KEEPORDER;
	crp->crp_buf = (caddr_t) m;
	crp->crp_callback = ah_input_cb;
	crp->crp_session = cryptoid;
	crp->crp_opaque = (caddr_t) xd;

	/* These are passed as-is to the callback. */
	xd->sav = sav;
	xd->nxt = hl;
	xd->protoff = protoff;
	xd->skip = skip;
	xd->cryptoid = cryptoid;
	xd->vnet = curvnet;
	return (crypto_dispatch(crp));
bad:
	m_freem(m);
	key_freesav(&sav);
	return (error);
}

/*
 * AH input callback from the crypto driver.
 */
static int
ah_input_cb(struct cryptop *crp)
{
	IPSEC_DEBUG_DECLARE(char buf[IPSEC_ADDRSTRLEN]);
	unsigned char calc[AH_ALEN_MAX];
	struct mbuf *m;
	struct xform_data *xd;
	struct secasvar *sav;
	struct secasindex *saidx;
	caddr_t ptr;
	crypto_session_t cryptoid;
	int authsize, rplen, ahsize, error, skip, protoff;
	uint8_t nxt;

	m = (struct mbuf *) crp->crp_buf;
	xd = (struct xform_data *) crp->crp_opaque;
	CURVNET_SET(xd->vnet);
	sav = xd->sav;
	skip = xd->skip;
	nxt = xd->nxt;
	protoff = xd->protoff;
	cryptoid = xd->cryptoid;
	saidx = &sav->sah->saidx;
	IPSEC_ASSERT(saidx->dst.sa.sa_family == AF_INET ||
		saidx->dst.sa.sa_family == AF_INET6,
		("unexpected protocol family %u", saidx->dst.sa.sa_family));

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
		AHSTAT_INC(ahs_noxform);
		DPRINTF(("%s: crypto error %d\n", __func__, crp->crp_etype));
		error = crp->crp_etype;
		goto bad;
	} else {
		AHSTAT_INC(ahs_hist[sav->alg_auth]);
		crypto_freereq(crp);		/* No longer needed. */
		crp = NULL;
	}

	/* Shouldn't happen... */
	if (m == NULL) {
		AHSTAT_INC(ahs_crypto);
		DPRINTF(("%s: bogus returned buffer from crypto\n", __func__));
		error = EINVAL;
		goto bad;
	}

	/* Figure out header size. */
	rplen = HDRSIZE(sav);
	authsize = AUTHSIZE(sav);
	ahsize = ah_hdrsiz(sav);

	/* Copy authenticator off the packet. */
	m_copydata(m, skip + rplen, authsize, calc);

	/* Verify authenticator. */
	ptr = (caddr_t) (xd + 1);
	if (timingsafe_bcmp(ptr + skip + rplen, calc, authsize)) {
		DPRINTF(("%s: authentication hash mismatch for packet "
		    "in SA %s/%08lx\n", __func__,
		    ipsec_address(&saidx->dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		AHSTAT_INC(ahs_badauth);
		error = EACCES;
		goto bad;
	}
	/* Fix the Next Protocol field. */
	((uint8_t *) ptr)[protoff] = nxt;

	/* Copyback the saved (uncooked) network headers. */
	m_copyback(m, 0, skip, ptr);
	free(xd, M_XDATA), xd = NULL;			/* No longer needed */

	/*
	 * Header is now authenticated.
	 */
	m->m_flags |= M_AUTHIPHDR|M_AUTHIPDGM;

	/*
	 * Update replay sequence number, if appropriate.
	 */
	if (sav->replay) {
		u_int32_t seq;

		m_copydata(m, skip + offsetof(struct newah, ah_seq),
			   sizeof (seq), (caddr_t) &seq);
		SECASVAR_LOCK(sav);
		if (ipsec_updatereplay(ntohl(seq), sav)) {
			SECASVAR_UNLOCK(sav);
			AHSTAT_INC(ahs_replay);
			error = EACCES;
			goto bad;
		}
		SECASVAR_UNLOCK(sav);
	}

	/*
	 * Remove the AH header and authenticator from the mbuf.
	 */
	error = m_striphdr(m, skip, ahsize);
	if (error) {
		DPRINTF(("%s: mangled mbuf chain for SA %s/%08lx\n", __func__,
		    ipsec_address(&saidx->dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		AHSTAT_INC(ahs_hdrops);
		goto bad;
	}

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
	if (sav)
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
 * AH output routine, called by ipsec[46]_perform_request().
 */
static int
ah_output(struct mbuf *m, struct secpolicy *sp, struct secasvar *sav,
    u_int idx, int skip, int protoff)
{
	IPSEC_DEBUG_DECLARE(char buf[IPSEC_ADDRSTRLEN]);
	const struct auth_hash *ahx;
	struct cryptodesc *crda;
	struct xform_data *xd;
	struct mbuf *mi;
	struct cryptop *crp;
	struct newah *ah;
	crypto_session_t cryptoid;
	uint16_t iplen;
	int error, rplen, authsize, ahsize, maxpacketsize, roff;
	uint8_t prot;

	IPSEC_ASSERT(sav != NULL, ("null SA"));
	ahx = sav->tdb_authalgxform;
	IPSEC_ASSERT(ahx != NULL, ("null authentication xform"));

	AHSTAT_INC(ahs_output);

	/* Figure out header size. */
	rplen = HDRSIZE(sav);
	authsize = AUTHSIZE(sav);
	ahsize = ah_hdrsiz(sav);

	/* Check for maximum packet size violations. */
	switch (sav->sah->saidx.dst.sa.sa_family) {
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
		DPRINTF(("%s: unknown/unsupported protocol family %u, "
		    "SA %s/%08lx\n", __func__,
		    sav->sah->saidx.dst.sa.sa_family,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		AHSTAT_INC(ahs_nopf);
		error = EPFNOSUPPORT;
		goto bad;
	}
	if (ahsize + m->m_pkthdr.len > maxpacketsize) {
		DPRINTF(("%s: packet in SA %s/%08lx got too big "
		    "(len %u, max len %u)\n", __func__,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi),
		    ahsize + m->m_pkthdr.len, maxpacketsize));
		AHSTAT_INC(ahs_toobig);
		error = EMSGSIZE;
		goto bad;
	}

	/* Update the counters. */
	AHSTAT_ADD(ahs_obytes, m->m_pkthdr.len - skip);

	m = m_unshare(m, M_NOWAIT);
	if (m == NULL) {
		DPRINTF(("%s: cannot clone mbuf chain, SA %s/%08lx\n", __func__,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		AHSTAT_INC(ahs_hdrops);
		error = ENOBUFS;
		goto bad;
	}

	/* Inject AH header. */
	mi = m_makespace(m, skip, ahsize, &roff);
	if (mi == NULL) {
		DPRINTF(("%s: failed to inject %u byte AH header for SA "
		    "%s/%08lx\n", __func__, ahsize,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		AHSTAT_INC(ahs_hdrops);		/*XXX differs from openbsd */
		error = ENOBUFS;
		goto bad;
	}

	/*
	 * The AH header is guaranteed by m_makespace() to be in
	 * contiguous memory, at roff bytes offset into the returned mbuf.
	 */
	ah = (struct newah *)(mtod(mi, caddr_t) + roff);

	/* Initialize the AH header. */
	m_copydata(m, protoff, sizeof(u_int8_t), (caddr_t) &ah->ah_nxt);
	ah->ah_len = (ahsize - sizeof(struct ah)) / sizeof(u_int32_t);
	ah->ah_reserve = 0;
	ah->ah_spi = sav->spi;

	/* Zeroize authenticator. */
	m_copyback(m, skip + rplen, authsize, ipseczeroes);

	/* Zeroize padding */
	m_copyback(m, skip + rplen + authsize, ahsize - (rplen + authsize),
	    ipseczeroes);

	/* Insert packet replay counter, as requested.  */
	SECASVAR_LOCK(sav);
	if (sav->replay) {
		if (sav->replay->count == ~0 &&
		    (sav->flags & SADB_X_EXT_CYCSEQ) == 0) {
			SECASVAR_UNLOCK(sav);
			DPRINTF(("%s: replay counter wrapped for SA %s/%08lx\n",
			    __func__, ipsec_address(&sav->sah->saidx.dst, buf,
			    sizeof(buf)), (u_long) ntohl(sav->spi)));
			AHSTAT_INC(ahs_wrap);
			error = EACCES;
			goto bad;
		}
#ifdef REGRESSION
		/* Emulate replay attack when ipsec_replay is TRUE. */
		if (!V_ipsec_replay)
#endif
			sav->replay->count++;
		ah->ah_seq = htonl(sav->replay->count);
	}
	cryptoid = sav->tdb_cryptoid;
	SECASVAR_UNLOCK(sav);

	/* Get crypto descriptors. */
	crp = crypto_getreq(1);
	if (crp == NULL) {
		DPRINTF(("%s: failed to acquire crypto descriptors\n",
			__func__));
		AHSTAT_INC(ahs_crypto);
		error = ENOBUFS;
		goto bad;
	}

	crda = crp->crp_desc;
	crda->crd_skip = 0;
	crda->crd_inject = skip + rplen;
	crda->crd_len = m->m_pkthdr.len;

	/* Authentication operation. */
	crda->crd_alg = ahx->type;
	crda->crd_key = sav->key_auth->key_data;
	crda->crd_klen = _KEYBITS(sav->key_auth);

	/* Allocate IPsec-specific opaque crypto info. */
	xd =  malloc(sizeof(struct xform_data) + skip, M_XDATA,
	    M_NOWAIT | M_ZERO);
	if (xd == NULL) {
		crypto_freereq(crp);
		DPRINTF(("%s: failed to allocate xform_data\n", __func__));
		AHSTAT_INC(ahs_crypto);
		error = ENOBUFS;
		goto bad;
	}

	/* Save the skipped portion of the packet. */
	m_copydata(m, 0, skip, (caddr_t) (xd + 1));

	/*
	 * Fix IP header length on the header used for
	 * authentication. We don't need to fix the original
	 * header length as it will be fixed by our caller.
	 */
	switch (sav->sah->saidx.dst.sa.sa_family) {
#ifdef INET
	case AF_INET:
		bcopy(((caddr_t)(xd + 1)) +
		    offsetof(struct ip, ip_len),
		    (caddr_t) &iplen, sizeof(u_int16_t));
		iplen = htons(ntohs(iplen) + ahsize);
		m_copyback(m, offsetof(struct ip, ip_len),
		    sizeof(u_int16_t), (caddr_t) &iplen);
		break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
		bcopy(((caddr_t)(xd + 1)) +
		    offsetof(struct ip6_hdr, ip6_plen),
		    (caddr_t) &iplen, sizeof(uint16_t));
		iplen = htons(ntohs(iplen) + ahsize);
		m_copyback(m, offsetof(struct ip6_hdr, ip6_plen),
		    sizeof(uint16_t), (caddr_t) &iplen);
		break;
#endif /* INET6 */
	}

	/* Fix the Next Header field in saved header. */
	((uint8_t *) (xd + 1))[protoff] = IPPROTO_AH;

	/* Update the Next Protocol field in the IP header. */
	prot = IPPROTO_AH;
	m_copyback(m, protoff, sizeof(uint8_t), (caddr_t) &prot);

	/* "Massage" the packet headers for crypto processing. */
	error = ah_massage_headers(&m, sav->sah->saidx.dst.sa.sa_family,
			skip, ahx->type, 1);
	if (error != 0) {
		m = NULL;	/* mbuf was free'd by ah_massage_headers. */
		free(xd, M_XDATA);
		crypto_freereq(crp);
		goto bad;
	}

	/* Crypto operation descriptor. */
	crp->crp_ilen = m->m_pkthdr.len; /* Total input length. */
	crp->crp_flags = CRYPTO_F_IMBUF | CRYPTO_F_CBIFSYNC;
	if (V_async_crypto)
		crp->crp_flags |= CRYPTO_F_ASYNC | CRYPTO_F_ASYNC_KEEPORDER;
	crp->crp_buf = (caddr_t) m;
	crp->crp_callback = ah_output_cb;
	crp->crp_session = cryptoid;
	crp->crp_opaque = (caddr_t) xd;

	/* These are passed as-is to the callback. */
	xd->sp = sp;
	xd->sav = sav;
	xd->skip = skip;
	xd->idx = idx;
	xd->cryptoid = cryptoid;
	xd->vnet = curvnet;

	return crypto_dispatch(crp);
bad:
	if (m)
		m_freem(m);
	key_freesav(&sav);
	key_freesp(&sp);
	return (error);
}

/*
 * AH output callback from the crypto driver.
 */
static int
ah_output_cb(struct cryptop *crp)
{
	struct xform_data *xd;
	struct secpolicy *sp;
	struct secasvar *sav;
	struct mbuf *m;
	crypto_session_t cryptoid;
	caddr_t ptr;
	u_int idx;
	int skip, error;

	m = (struct mbuf *) crp->crp_buf;
	xd = (struct xform_data *) crp->crp_opaque;
	CURVNET_SET(xd->vnet);
	sp = xd->sp;
	sav = xd->sav;
	skip = xd->skip;
	idx = xd->idx;
	cryptoid = xd->cryptoid;
	ptr = (caddr_t) (xd + 1);

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
		AHSTAT_INC(ahs_noxform);
		DPRINTF(("%s: crypto error %d\n", __func__, crp->crp_etype));
		error = crp->crp_etype;
		m_freem(m);
		goto bad;
	}

	/* Shouldn't happen... */
	if (m == NULL) {
		AHSTAT_INC(ahs_crypto);
		DPRINTF(("%s: bogus returned buffer from crypto\n", __func__));
		error = EINVAL;
		goto bad;
	}
	/*
	 * Copy original headers (with the new protocol number) back
	 * in place.
	 */
	m_copyback(m, 0, skip, ptr);

	free(xd, M_XDATA);
	crypto_freereq(crp);
	AHSTAT_INC(ahs_hist[sav->alg_auth]);
#ifdef REGRESSION
	/* Emulate man-in-the-middle attack when ipsec_integrity is TRUE. */
	if (V_ipsec_integrity) {
		int alen;

		/*
		 * Corrupt HMAC if we want to test integrity verification of
		 * the other side.
		 */
		alen = AUTHSIZE(sav);
		m_copyback(m, m->m_pkthdr.len - alen, alen, ipseczeroes);
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

static struct xformsw ah_xformsw = {
	.xf_type =	XF_AH,
	.xf_name =	"IPsec AH",
	.xf_init =	ah_init,
	.xf_zeroize =	ah_zeroize,
	.xf_input =	ah_input,
	.xf_output =	ah_output,
};

SYSINIT(ah_xform_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_MIDDLE,
    xform_attach, &ah_xformsw);
SYSUNINIT(ah_xform_uninit, SI_SUB_PROTO_DOMAIN, SI_ORDER_MIDDLE,
    xform_detach, &ah_xformsw);
