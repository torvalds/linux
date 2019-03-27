/*	$FreeBSD$	*/
/* $OpenBSD: ip_ipcomp.c,v 1.1 2001/07/05 12:08:52 jjbg Exp $ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Jean-Jacques Bernard-Gundol (jj@wabbitt.org)
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
 */

/* IP payload compression protocol (IPComp), see RFC 2393 */
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/protosw.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_encap.h>

#include <net/netisr.h>
#include <net/vnet.h>

#include <netipsec/ipsec.h>
#include <netipsec/xform.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netipsec/ipsec6.h>
#endif

#include <netipsec/ipcomp.h>
#include <netipsec/ipcomp_var.h>

#include <netipsec/key.h>
#include <netipsec/key_debug.h>

#include <opencrypto/cryptodev.h>
#include <opencrypto/deflate.h>
#include <opencrypto/xform.h>

VNET_DEFINE(int, ipcomp_enable) = 1;
VNET_PCPUSTAT_DEFINE(struct ipcompstat, ipcompstat);
VNET_PCPUSTAT_SYSINIT(ipcompstat);

#ifdef VIMAGE
VNET_PCPUSTAT_SYSUNINIT(ipcompstat);
#endif /* VIMAGE */

SYSCTL_DECL(_net_inet_ipcomp);
SYSCTL_INT(_net_inet_ipcomp, OID_AUTO, ipcomp_enable,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ipcomp_enable), 0, "");
SYSCTL_VNET_PCPUSTAT(_net_inet_ipcomp, IPSECCTL_STATS, stats,
    struct ipcompstat, ipcompstat,
    "IPCOMP statistics (struct ipcompstat, netipsec/ipcomp_var.h");

static int ipcomp_input_cb(struct cryptop *crp);
static int ipcomp_output_cb(struct cryptop *crp);

/*
 * RFC 3173 p 2.2. Non-Expansion Policy:
 * If the total size of a compressed payload and the IPComp header, as
 * defined in section 3, is not smaller than the size of the original
 * payload, the IP datagram MUST be sent in the original non-compressed
 * form.
 *
 * When we use IPComp in tunnel mode, for small packets we will receive
 * encapsulated IP-IP datagrams without any compression and without IPComp
 * header.
 */
static int
ipcomp_encapcheck(union sockaddr_union *src, union sockaddr_union *dst)
{
	struct secasvar *sav;

	sav = key_allocsa_tunnel(src, dst, IPPROTO_IPCOMP);
	if (sav == NULL)
		return (0);
	key_freesav(&sav);

	if (src->sa.sa_family == AF_INET)
		return (sizeof(struct in_addr) << 4);
	else
		return (sizeof(struct in6_addr) << 4);
}

static int
ipcomp_nonexp_input(struct mbuf *m, int off, int proto, void *arg __unused)
{
	int isr;

	switch (proto) {
#ifdef INET
	case IPPROTO_IPV4:
		isr = NETISR_IP;
		break;
#endif
#ifdef INET6
	case IPPROTO_IPV6:
		isr = NETISR_IPV6;
		break;
#endif
	default:
		IPCOMPSTAT_INC(ipcomps_nopf);
		m_freem(m);
		return (IPPROTO_DONE);
	}
	m_adj(m, off);
	IPCOMPSTAT_ADD(ipcomps_ibytes, m->m_pkthdr.len);
	IPCOMPSTAT_INC(ipcomps_input);
	netisr_dispatch(isr, m);
	return (IPPROTO_DONE);
}

/*
 * ipcomp_init() is called when an CPI is being set up.
 */
static int
ipcomp_init(struct secasvar *sav, struct xformsw *xsp)
{
	const struct comp_algo *tcomp;
	struct cryptoini cric;

	/* NB: algorithm really comes in alg_enc and not alg_comp! */
	tcomp = comp_algorithm_lookup(sav->alg_enc);
	if (tcomp == NULL) {
		DPRINTF(("%s: unsupported compression algorithm %d\n", __func__,
			 sav->alg_comp));
		return EINVAL;
	}
	sav->alg_comp = sav->alg_enc;		/* set for doing histogram */
	sav->tdb_xform = xsp;
	sav->tdb_compalgxform = tcomp;

	/* Initialize crypto session */
	bzero(&cric, sizeof (cric));
	cric.cri_alg = sav->tdb_compalgxform->type;

	return crypto_newsession(&sav->tdb_cryptoid, &cric, V_crypto_support);
}

/*
 * ipcomp_zeroize() used when IPCA is deleted
 */
static int
ipcomp_zeroize(struct secasvar *sav)
{

	crypto_freesession(sav->tdb_cryptoid);
	sav->tdb_cryptoid = NULL;
	return 0;
}

/*
 * ipcomp_input() gets called to uncompress an input packet
 */
static int
ipcomp_input(struct mbuf *m, struct secasvar *sav, int skip, int protoff)
{
	struct xform_data *xd;
	struct cryptodesc *crdc;
	struct cryptop *crp;
	struct ipcomp *ipcomp;
	caddr_t addr;
	int error, hlen = IPCOMP_HLENGTH;

	/*
	 * Check that the next header of the IPComp is not IPComp again, before
	 * doing any real work.  Given it is not possible to do double
	 * compression it means someone is playing tricks on us.
	 */
	error = ENOBUFS;
	if (m->m_len < skip + hlen && (m = m_pullup(m, skip + hlen)) == NULL) {
		IPCOMPSTAT_INC(ipcomps_hdrops);		/*XXX*/
		DPRINTF(("%s: m_pullup failed\n", __func__));
		key_freesav(&sav);
		return (error);
	}
	addr = (caddr_t) mtod(m, struct ip *) + skip;
	ipcomp = (struct ipcomp *)addr;
	if (ipcomp->comp_nxt == IPPROTO_IPCOMP) {
		IPCOMPSTAT_INC(ipcomps_pdrops);	/* XXX have our own stats? */
		DPRINTF(("%s: recursive compression detected\n", __func__));
		error = EINVAL;
		goto bad;
	}

	/* Get crypto descriptors */
	crp = crypto_getreq(1);
	if (crp == NULL) {
		DPRINTF(("%s: no crypto descriptors\n", __func__));
		IPCOMPSTAT_INC(ipcomps_crypto);
		goto bad;
	}
	/* Get IPsec-specific opaque pointer */
	xd = malloc(sizeof(*xd), M_XDATA, M_NOWAIT | M_ZERO);
	if (xd == NULL) {
		DPRINTF(("%s: cannot allocate xform_data\n", __func__));
		IPCOMPSTAT_INC(ipcomps_crypto);
		crypto_freereq(crp);
		goto bad;
	}
	crdc = crp->crp_desc;

	crdc->crd_skip = skip + hlen;
	crdc->crd_len = m->m_pkthdr.len - (skip + hlen);
	crdc->crd_inject = skip;

	/* Decompression operation */
	crdc->crd_alg = sav->tdb_compalgxform->type;


	/* Crypto operation descriptor */
	crp->crp_ilen = m->m_pkthdr.len - (skip + hlen);
	crp->crp_flags = CRYPTO_F_IMBUF | CRYPTO_F_CBIFSYNC;
	crp->crp_buf = (caddr_t) m;
	crp->crp_callback = ipcomp_input_cb;
	crp->crp_opaque = (caddr_t) xd;

	/* These are passed as-is to the callback */
	xd->sav = sav;
	xd->protoff = protoff;
	xd->skip = skip;
	xd->vnet = curvnet;

	SECASVAR_LOCK(sav);
	crp->crp_session = xd->cryptoid = sav->tdb_cryptoid;
	SECASVAR_UNLOCK(sav);

	return crypto_dispatch(crp);
bad:
	m_freem(m);
	key_freesav(&sav);
	return (error);
}

/*
 * IPComp input callback from the crypto driver.
 */
static int
ipcomp_input_cb(struct cryptop *crp)
{
	IPSEC_DEBUG_DECLARE(char buf[IPSEC_ADDRSTRLEN]);
	struct xform_data *xd;
	struct mbuf *m;
	struct secasvar *sav;
	struct secasindex *saidx;
	caddr_t addr;
	crypto_session_t cryptoid;
	int hlen = IPCOMP_HLENGTH, error, clen;
	int skip, protoff;
	uint8_t nproto;

	m = (struct mbuf *) crp->crp_buf;
	xd = (struct xform_data *) crp->crp_opaque;
	CURVNET_SET(xd->vnet);
	sav = xd->sav;
	skip = xd->skip;
	protoff = xd->protoff;
	cryptoid = xd->cryptoid;
	saidx = &sav->sah->saidx;
	IPSEC_ASSERT(saidx->dst.sa.sa_family == AF_INET ||
		saidx->dst.sa.sa_family == AF_INET6,
		("unexpected protocol family %u", saidx->dst.sa.sa_family));

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
		IPCOMPSTAT_INC(ipcomps_noxform);
		DPRINTF(("%s: crypto error %d\n", __func__, crp->crp_etype));
		error = crp->crp_etype;
		goto bad;
	}
	/* Shouldn't happen... */
	if (m == NULL) {
		IPCOMPSTAT_INC(ipcomps_crypto);
		DPRINTF(("%s: null mbuf returned from crypto\n", __func__));
		error = EINVAL;
		goto bad;
	}
	IPCOMPSTAT_INC(ipcomps_hist[sav->alg_comp]);

	clen = crp->crp_olen;		/* Length of data after processing */

	/* Release the crypto descriptors */
	free(xd, M_XDATA), xd = NULL;
	crypto_freereq(crp), crp = NULL;

	/* In case it's not done already, adjust the size of the mbuf chain */
	m->m_pkthdr.len = clen + hlen + skip;

	if (m->m_len < skip + hlen && (m = m_pullup(m, skip + hlen)) == NULL) {
		IPCOMPSTAT_INC(ipcomps_hdrops);		/*XXX*/
		DPRINTF(("%s: m_pullup failed\n", __func__));
		error = EINVAL;				/*XXX*/
		goto bad;
	}

	/* Keep the next protocol field */
	addr = (caddr_t) mtod(m, struct ip *) + skip;
	nproto = ((struct ipcomp *) addr)->comp_nxt;

	/* Remove the IPCOMP header */
	error = m_striphdr(m, skip, hlen);
	if (error) {
		IPCOMPSTAT_INC(ipcomps_hdrops);
		DPRINTF(("%s: bad mbuf chain, IPCA %s/%08lx\n", __func__,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		goto bad;
	}

	/* Restore the Next Protocol field */
	m_copyback(m, protoff, sizeof (u_int8_t), (u_int8_t *) &nproto);

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
 * IPComp output routine, called by ipsec[46]_perform_request()
 */
static int
ipcomp_output(struct mbuf *m, struct secpolicy *sp, struct secasvar *sav,
    u_int idx, int skip, int protoff)
{
	IPSEC_DEBUG_DECLARE(char buf[IPSEC_ADDRSTRLEN]);
	const struct comp_algo *ipcompx;
	struct cryptodesc *crdc;
	struct cryptop *crp;
	struct xform_data *xd;
	int error, ralen, maxpacketsize;

	IPSEC_ASSERT(sav != NULL, ("null SA"));
	ipcompx = sav->tdb_compalgxform;
	IPSEC_ASSERT(ipcompx != NULL, ("null compression xform"));

	/*
	 * Do not touch the packet in case our payload to compress
	 * is lower than the minimal threshold of the compression
	 * alogrithm.  We will just send out the data uncompressed.
	 * See RFC 3173, 2.2. Non-Expansion Policy.
	 */
	if (m->m_pkthdr.len <= ipcompx->minlen) {
		IPCOMPSTAT_INC(ipcomps_threshold);
		return ipsec_process_done(m, sp, sav, idx);
	}

	ralen = m->m_pkthdr.len - skip;	/* Raw payload length before comp. */
	IPCOMPSTAT_INC(ipcomps_output);

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
		IPCOMPSTAT_INC(ipcomps_nopf);
		DPRINTF(("%s: unknown/unsupported protocol family %d, "
		    "IPCA %s/%08lx\n", __func__,
		    sav->sah->saidx.dst.sa.sa_family,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi)));
		error = EPFNOSUPPORT;
		goto bad;
	}
	if (ralen + skip + IPCOMP_HLENGTH > maxpacketsize) {
		IPCOMPSTAT_INC(ipcomps_toobig);
		DPRINTF(("%s: packet in IPCA %s/%08lx got too big "
		    "(len %u, max len %u)\n", __func__,
		    ipsec_address(&sav->sah->saidx.dst, buf, sizeof(buf)),
		    (u_long) ntohl(sav->spi),
		    ralen + skip + IPCOMP_HLENGTH, maxpacketsize));
		error = EMSGSIZE;
		goto bad;
	}

	/* Update the counters */
	IPCOMPSTAT_ADD(ipcomps_obytes, m->m_pkthdr.len - skip);

	m = m_unshare(m, M_NOWAIT);
	if (m == NULL) {
		IPCOMPSTAT_INC(ipcomps_hdrops);
		DPRINTF(("%s: cannot clone mbuf chain, IPCA %s/%08lx\n",
		    __func__, ipsec_address(&sav->sah->saidx.dst, buf,
		    sizeof(buf)), (u_long) ntohl(sav->spi)));
		error = ENOBUFS;
		goto bad;
	}

	/* Ok now, we can pass to the crypto processing. */

	/* Get crypto descriptors */
	crp = crypto_getreq(1);
	if (crp == NULL) {
		IPCOMPSTAT_INC(ipcomps_crypto);
		DPRINTF(("%s: failed to acquire crypto descriptor\n",__func__));
		error = ENOBUFS;
		goto bad;
	}
	crdc = crp->crp_desc;

	/* Compression descriptor */
	crdc->crd_skip = skip;
	crdc->crd_len = ralen;
	crdc->crd_flags = CRD_F_COMP;
	crdc->crd_inject = skip;

	/* Compression operation */
	crdc->crd_alg = ipcompx->type;

	/* IPsec-specific opaque crypto info */
	xd =  malloc(sizeof(struct xform_data), M_XDATA, M_NOWAIT | M_ZERO);
	if (xd == NULL) {
		IPCOMPSTAT_INC(ipcomps_crypto);
		DPRINTF(("%s: failed to allocate xform_data\n", __func__));
		crypto_freereq(crp);
		error = ENOBUFS;
		goto bad;
	}

	xd->sp = sp;
	xd->sav = sav;
	xd->idx = idx;
	xd->skip = skip;
	xd->protoff = protoff;
	xd->vnet = curvnet;

	/* Crypto operation descriptor */
	crp->crp_ilen = m->m_pkthdr.len;	/* Total input length */
	crp->crp_flags = CRYPTO_F_IMBUF | CRYPTO_F_CBIFSYNC;
	crp->crp_buf = (caddr_t) m;
	crp->crp_callback = ipcomp_output_cb;
	crp->crp_opaque = (caddr_t) xd;

	SECASVAR_LOCK(sav);
	crp->crp_session = xd->cryptoid = sav->tdb_cryptoid;
	SECASVAR_UNLOCK(sav);

	return crypto_dispatch(crp);
bad:
	if (m)
		m_freem(m);
	key_freesav(&sav);
	key_freesp(&sp);
	return (error);
}

/*
 * IPComp output callback from the crypto driver.
 */
static int
ipcomp_output_cb(struct cryptop *crp)
{
	IPSEC_DEBUG_DECLARE(char buf[IPSEC_ADDRSTRLEN]);
	struct xform_data *xd;
	struct secpolicy *sp;
	struct secasvar *sav;
	struct mbuf *m;
	crypto_session_t cryptoid;
	u_int idx;
	int error, skip, protoff;

	m = (struct mbuf *) crp->crp_buf;
	xd = (struct xform_data *) crp->crp_opaque;
	CURVNET_SET(xd->vnet);
	idx = xd->idx;
	sp = xd->sp;
	sav = xd->sav;
	skip = xd->skip;
	protoff = xd->protoff;
	cryptoid = xd->cryptoid;

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
		IPCOMPSTAT_INC(ipcomps_noxform);
		DPRINTF(("%s: crypto error %d\n", __func__, crp->crp_etype));
		error = crp->crp_etype;
		goto bad;
	}
	/* Shouldn't happen... */
	if (m == NULL) {
		IPCOMPSTAT_INC(ipcomps_crypto);
		DPRINTF(("%s: bogus return buffer from crypto\n", __func__));
		error = EINVAL;
		goto bad;
	}
	IPCOMPSTAT_INC(ipcomps_hist[sav->alg_comp]);

	if (crp->crp_ilen - skip > crp->crp_olen) {
		struct mbuf *mo;
		struct ipcomp *ipcomp;
		int roff;
		uint8_t prot;

		/* Compression helped, inject IPCOMP header. */
		mo = m_makespace(m, skip, IPCOMP_HLENGTH, &roff);
		if (mo == NULL) {
			IPCOMPSTAT_INC(ipcomps_wrap);
			DPRINTF(("%s: IPCOMP header inject failed "
			    "for IPCA %s/%08lx\n",
			    __func__, ipsec_address(&sav->sah->saidx.dst, buf,
			    sizeof(buf)), (u_long) ntohl(sav->spi)));
			error = ENOBUFS;
			goto bad;
		}
		ipcomp = (struct ipcomp *)(mtod(mo, caddr_t) + roff);

		/* Initialize the IPCOMP header */
		/* XXX alignment always correct? */
		switch (sav->sah->saidx.dst.sa.sa_family) {
#ifdef INET
		case AF_INET:
			ipcomp->comp_nxt = mtod(m, struct ip *)->ip_p;
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			ipcomp->comp_nxt = mtod(m, struct ip6_hdr *)->ip6_nxt;
			break;
#endif
		}
		ipcomp->comp_flags = 0;
		ipcomp->comp_cpi = htons((u_int16_t) ntohl(sav->spi));

		/* Fix Next Protocol in IPv4/IPv6 header */
		prot = IPPROTO_IPCOMP;
		m_copyback(m, protoff, sizeof(u_int8_t),
		    (u_char *)&prot);

		/* Adjust the length in the IP header */
		switch (sav->sah->saidx.dst.sa.sa_family) {
#ifdef INET
		case AF_INET:
			mtod(m, struct ip *)->ip_len = htons(m->m_pkthdr.len);
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			mtod(m, struct ip6_hdr *)->ip6_plen =
				htons(m->m_pkthdr.len) - sizeof(struct ip6_hdr);
			break;
#endif /* INET6 */
		default:
			IPCOMPSTAT_INC(ipcomps_nopf);
			DPRINTF(("%s: unknown/unsupported protocol "
			    "family %d, IPCA %s/%08lx\n", __func__,
			    sav->sah->saidx.dst.sa.sa_family,
			    ipsec_address(&sav->sah->saidx.dst, buf,
				sizeof(buf)), (u_long) ntohl(sav->spi)));
			error = EPFNOSUPPORT;
			goto bad;
		}
	} else {
		/* Compression was useless, we have lost time. */
		IPCOMPSTAT_INC(ipcomps_uncompr);
		DPRINTF(("%s: compressions was useless %d - %d <= %d\n",
		    __func__, crp->crp_ilen, skip, crp->crp_olen));
		/* XXX remember state to not compress the next couple
		 *     of packets, RFC 3173, 2.2. Non-Expansion Policy */
	}

	/* Release the crypto descriptor */
	free(xd, M_XDATA);
	crypto_freereq(crp);

	/* NB: m is reclaimed by ipsec_process_done. */
	error = ipsec_process_done(m, sp, sav, idx);
	CURVNET_RESTORE();
	return (error);
bad:
	if (m)
		m_freem(m);
	CURVNET_RESTORE();
	free(xd, M_XDATA);
	crypto_freereq(crp);
	key_freesav(&sav);
	key_freesp(&sp);
	return (error);
}

#ifdef INET
static int
ipcomp4_nonexp_encapcheck(const struct mbuf *m, int off, int proto,
    void *arg __unused)
{
	union sockaddr_union src, dst;
	const struct ip *ip;

	if (V_ipcomp_enable == 0)
		return (0);
	if (proto != IPPROTO_IPV4 && proto != IPPROTO_IPV6)
		return (0);
	bzero(&src, sizeof(src));
	bzero(&dst, sizeof(dst));
	src.sa.sa_family = dst.sa.sa_family = AF_INET;
	src.sin.sin_len = dst.sin.sin_len = sizeof(struct sockaddr_in);
	ip = mtod(m, const struct ip *);
	src.sin.sin_addr = ip->ip_src;
	dst.sin.sin_addr = ip->ip_dst;
	return (ipcomp_encapcheck(&src, &dst));
}

static const struct encaptab *ipe4_cookie = NULL;
static const struct encap_config ipv4_encap_cfg = {
	.proto = -1,
	.min_length = sizeof(struct ip),
	.exact_match = sizeof(in_addr_t) << 4,
	.check = ipcomp4_nonexp_encapcheck,
	.input = ipcomp_nonexp_input
};
#endif
#ifdef INET6
static int
ipcomp6_nonexp_encapcheck(const struct mbuf *m, int off, int proto,
    void *arg __unused)
{
	union sockaddr_union src, dst;
	const struct ip6_hdr *ip6;

	if (V_ipcomp_enable == 0)
		return (0);
	if (proto != IPPROTO_IPV4 && proto != IPPROTO_IPV6)
		return (0);
	bzero(&src, sizeof(src));
	bzero(&dst, sizeof(dst));
	src.sa.sa_family = dst.sa.sa_family = AF_INET;
	src.sin6.sin6_len = dst.sin6.sin6_len = sizeof(struct sockaddr_in6);
	ip6 = mtod(m, const struct ip6_hdr *);
	src.sin6.sin6_addr = ip6->ip6_src;
	dst.sin6.sin6_addr = ip6->ip6_dst;
	if (IN6_IS_SCOPE_LINKLOCAL(&src.sin6.sin6_addr)) {
		/* XXX: sa6_recoverscope() */
		src.sin6.sin6_scope_id =
		    ntohs(src.sin6.sin6_addr.s6_addr16[1]);
		src.sin6.sin6_addr.s6_addr16[1] = 0;
	}
	if (IN6_IS_SCOPE_LINKLOCAL(&dst.sin6.sin6_addr)) {
		/* XXX: sa6_recoverscope() */
		dst.sin6.sin6_scope_id =
		    ntohs(dst.sin6.sin6_addr.s6_addr16[1]);
		dst.sin6.sin6_addr.s6_addr16[1] = 0;
	}
	return (ipcomp_encapcheck(&src, &dst));
}

static const struct encaptab *ipe6_cookie = NULL;
static const struct encap_config ipv6_encap_cfg = {
	.proto = -1,
	.min_length = sizeof(struct ip6_hdr),
	.exact_match = sizeof(struct in6_addr) << 4,
	.check = ipcomp6_nonexp_encapcheck,
	.input = ipcomp_nonexp_input
};
#endif

static struct xformsw ipcomp_xformsw = {
	.xf_type =	XF_IPCOMP,
	.xf_name =	"IPcomp",
	.xf_init =	ipcomp_init,
	.xf_zeroize =	ipcomp_zeroize,
	.xf_input =	ipcomp_input,
	.xf_output =	ipcomp_output,
};

static void
ipcomp_attach(void)
{

#ifdef INET
	ipe4_cookie = ip_encap_attach(&ipv4_encap_cfg, NULL, M_WAITOK);
#endif
#ifdef INET6
	ipe6_cookie = ip6_encap_attach(&ipv6_encap_cfg, NULL, M_WAITOK);
#endif
	xform_attach(&ipcomp_xformsw);
}

static void
ipcomp_detach(void)
{

#ifdef INET
	ip_encap_detach(ipe4_cookie);
#endif
#ifdef INET6
	ip6_encap_detach(ipe6_cookie);
#endif
	xform_detach(&ipcomp_xformsw);
}

SYSINIT(ipcomp_xform_init, SI_SUB_PROTO_DOMAIN, SI_ORDER_MIDDLE,
    ipcomp_attach, NULL);
SYSUNINIT(ipcomp_xform_uninit, SI_SUB_PROTO_DOMAIN, SI_ORDER_MIDDLE,
    ipcomp_detach, NULL);
