/* $OpenBSD: ip_ipcomp.c,v 1.95 2025/07/08 00:47:41 jsg Exp $ */

/*
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#ifdef INET6
#include <netinet/ip6.h>
#endif				/* INET6 */

#include <netinet/ip_ipsp.h>
#include <netinet/ip_ipcomp.h>
#include <net/pfkeyv2.h>
#include <net/if_enc.h>

#include <crypto/cryptodev.h>
#include <crypto/xform.h>

#include "bpfilter.h"

#ifdef ENCDEBUG
#define DPRINTF(fmt, args...)						\
	do {								\
		if (atomic_load_int(&encdebug))				\
			printf("%s: " fmt "\n", __func__, ## args);	\
	} while (0)
#else
#define DPRINTF(fmt, args...)						\
	do { } while (0)
#endif

/*
 * ipcomp_attach() is called from the transformation code
 */
int
ipcomp_attach(void)
{
	return 0;
}

/*
 * ipcomp_init() is called when an CPI is being set up.
 */
int
ipcomp_init(struct tdb *tdbp, const struct xformsw *xsp, struct ipsecinit *ii)
{
	const struct comp_algo *tcomp = NULL;
	struct cryptoini cric;
	int error;

	switch (ii->ii_compalg) {
	case SADB_X_CALG_DEFLATE:
		tcomp = &comp_algo_deflate;
		break;
	default:
		DPRINTF("unsupported compression algorithm %d specified",
		    ii->ii_compalg);
		return EINVAL;
	}

	tdbp->tdb_compalgxform = tcomp;

	DPRINTF("initialized TDB with ipcomp algorithm %s", tcomp->name);

	tdbp->tdb_xform = xsp;

	/* Initialize crypto session */
	memset(&cric, 0, sizeof(cric));
	cric.cri_alg = tdbp->tdb_compalgxform->type;

	KERNEL_LOCK();
	error = crypto_newsession(&tdbp->tdb_cryptoid, &cric, 0);
	KERNEL_UNLOCK();
	return error;
}

/*
 * ipcomp_zeroize() used when IPCA is deleted
 */
int
ipcomp_zeroize(struct tdb *tdbp)
{
	int error;

	KERNEL_LOCK();
	error = crypto_freesession(tdbp->tdb_cryptoid);
	KERNEL_UNLOCK();
	tdbp->tdb_cryptoid = 0;
	return error;
}

/*
 * ipcomp_input() gets called to uncompress an input packet
 */
int
ipcomp_input(struct mbuf **mp, struct tdb *tdb, int skip, int protoff,
    struct netstack *ns)
{
	const struct comp_algo *ipcompx = tdb->tdb_compalgxform;
	struct mbuf *m = *mp;
	struct cryptodesc *crdc = NULL;
	struct cryptop *crp;
	int hlen, error, clen, roff;
	u_int8_t nproto;
	u_int64_t ibytes;
	struct mbuf *m1, *mo;
	struct ipcomp  *ipcomp;
	caddr_t addr;
#ifdef ENCDEBUG
	char buf[INET6_ADDRSTRLEN];
#endif

	hlen = IPCOMP_HLENGTH;

	/* Get crypto descriptors */
	crp = crypto_getreq(1);
	if (crp == NULL) {
		DPRINTF("failed to acquire crypto descriptors");
		ipcompstat_inc(ipcomps_crypto);
		goto drop;
	}
	crdc = &crp->crp_desc[0];

	crdc->crd_skip = skip + hlen;
	crdc->crd_len = m->m_pkthdr.len - (skip + hlen);
	crdc->crd_inject = skip;

	/* Decompression operation */
	crdc->crd_alg = ipcompx->type;

	/* Crypto operation descriptor */
	crp->crp_ilen = m->m_pkthdr.len - (skip + hlen);
	crp->crp_flags = CRYPTO_F_IMBUF;
	crp->crp_buf = (caddr_t)m;
	crp->crp_sid = tdb->tdb_cryptoid;

	while ((error = crypto_invoke(crp)) == EAGAIN) {
		/* Reset the session ID */
		if (tdb->tdb_cryptoid != 0)
			tdb->tdb_cryptoid = crp->crp_sid;
	}
	if (error) {
		DPRINTF("crypto error %d", error);
		ipsecstat_inc(ipsec_noxform);
		goto drop;
	}

	clen = crp->crp_olen;

	/* Release the crypto descriptors */
	crypto_freereq(crp);
	crp = NULL;

	/* update the counters */
	ibytes = m->m_pkthdr.len - (skip + hlen);
	tdb->tdb_cur_bytes += ibytes;
	tdbstat_add(tdb, tdb_ibytes, ibytes);
	ipcompstat_add(ipcomps_ibytes, ibytes);

	/* Hard expiration */
	if ((tdb->tdb_flags & TDBF_BYTES) &&
	    (tdb->tdb_cur_bytes >= tdb->tdb_exp_bytes)) {
		ipsecstat_inc(ipsec_exctdb);
		pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_HARD);
		tdb_delete(tdb);
		goto drop;
	}
	/* Notify on soft expiration */
	mtx_enter(&tdb->tdb_mtx);
	if ((tdb->tdb_flags & TDBF_SOFT_BYTES) &&
	    (tdb->tdb_cur_bytes >= tdb->tdb_soft_bytes)) {
		tdb->tdb_flags &= ~TDBF_SOFT_BYTES;  /* Turn off checking */
		mtx_leave(&tdb->tdb_mtx);
		/* may sleep in solock() for the pfkey socket */
		pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_SOFT);
	} else
		mtx_leave(&tdb->tdb_mtx);

	/* In case it's not done already, adjust the size of the mbuf chain */
	m->m_pkthdr.len = clen + hlen + skip;

	if (m->m_len < skip + hlen &&
	    (m = *mp = m_pullup(m, skip + hlen)) == NULL) {
		ipcompstat_inc(ipcomps_hdrops);
		goto drop;
	}

	/* Find the beginning of the IPCOMP header */
	m1 = m_getptr(m, skip, &roff);
	if (m1 == NULL) {
		DPRINTF("bad mbuf chain, IPCA %s/%08x",
		    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
		    ntohl(tdb->tdb_spi));
		ipcompstat_inc(ipcomps_hdrops);
		goto drop;
	}
	/* Keep the next protocol field */
	addr = (caddr_t) mtod(m, struct ip *) + skip;
	ipcomp = (struct ipcomp *) addr;
	nproto = ipcomp->ipcomp_nh;

	/* Remove the IPCOMP header from the mbuf */
	if (roff == 0) {
		/* The IPCOMP header is at the beginning of m1 */
		m_adj(m1, hlen);
		/*
		 * If m1 is the first mbuf, it has set M_PKTHDR and m_adj()
		 * has already adjusted the packet header length for us.
		 */
		if (m1 != m)
			m->m_pkthdr.len -= hlen;
	} else if (roff + hlen >= m1->m_len) {
		int adjlen;

		if (roff + hlen > m1->m_len) {
			adjlen = roff + hlen - m1->m_len;

			/* Adjust the next mbuf by the remainder */
			m_adj(m1->m_next, adjlen);

			/*
			 * The second mbuf is guaranteed not to have a
			 * pkthdr...
			 */
			m->m_pkthdr.len -= adjlen;
		}
		/* Now, let's unlink the mbuf chain for a second... */
		mo = m1->m_next;
		m1->m_next = NULL;

		/* ...and trim the end of the first part of the chain...sick */
		adjlen = m1->m_len - roff;
		m_adj(m1, -adjlen);
		/*
		 * If m1 is the first mbuf, it has set M_PKTHDR and m_adj()
		 * has already adjusted the packet header length for us.
		 */
		if (m1 != m)
			m->m_pkthdr.len -= adjlen;

		/* Finally, let's relink */
		m1->m_next = mo;
	} else {
		memmove(mtod(m1, u_char *) + roff,
		    mtod(m1, u_char *) + roff + hlen,
		    m1->m_len - (roff + hlen));
		m1->m_len -= hlen;
		m->m_pkthdr.len -= hlen;
	}

	/* Restore the Next Protocol field */
	m_copyback(m, protoff, sizeof(u_int8_t), &nproto, M_NOWAIT);

	/* Back to generic IPsec input processing */
	return ipsec_common_input_cb(mp, tdb, skip, protoff, ns);

 drop:
	m_freemp(mp);
	crypto_freereq(crp);
	return IPPROTO_DONE;
}

/*
 * IPComp output routine, called by ipsp_process_packet()
 */
int
ipcomp_output(struct mbuf *m, struct tdb *tdb, int skip, int protoff)
{
	const struct comp_algo *ipcompx = tdb->tdb_compalgxform;
	int error, hlen, ilen, olen, rlen, roff;
	struct cryptodesc *crdc = NULL;
	struct cryptop *crp = NULL;
	struct mbuf *mi, *mo;
	struct ip *ip;
	u_int16_t cpi;
#ifdef INET6
	struct ip6_hdr *ip6;
#endif
#ifdef ENCDEBUG
	char buf[INET6_ADDRSTRLEN];
#endif
#if NBPFILTER > 0
	struct ifnet *encif;
	struct ipcomp  *ipcomp;

	if ((encif = enc_getif(0, tdb->tdb_tap)) != NULL) {
		encif->if_opackets++;
		encif->if_obytes += m->m_pkthdr.len;

		if (encif->if_bpf) {
			struct enchdr hdr;

			memset(&hdr, 0, sizeof(hdr));

			hdr.af = tdb->tdb_dst.sa.sa_family;
			hdr.spi = tdb->tdb_spi;

			bpf_mtap_hdr(encif->if_bpf, (char *)&hdr,
			    ENC_HDRLEN, m, BPF_DIRECTION_OUT);
		}
	}
#endif
	hlen = IPCOMP_HLENGTH;

	ipcompstat_inc(ipcomps_output);

	switch (tdb->tdb_dst.sa.sa_family) {
	case AF_INET:
		/* Check for IPv4 maximum packet size violations */
		/*
		 * Since compression is going to reduce the size, no need to
		 * worry
		 */
		if (m->m_pkthdr.len + hlen > IP_MAXPACKET) {
			DPRINTF("packet in IPCA %s/%08x got too big",
			    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
			    ntohl(tdb->tdb_spi));
			ipcompstat_inc(ipcomps_toobig);
			error = EMSGSIZE;
			goto drop;
		}
		break;

#ifdef INET6
	case AF_INET6:
		/* Check for IPv6 maximum packet size violations */
		if (m->m_pkthdr.len + hlen > IPV6_MAXPACKET) {
			DPRINTF("packet in IPCA %s/%08x got too big",
			    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
			    ntohl(tdb->tdb_spi));
			ipcompstat_inc(ipcomps_toobig);
			error = EMSGSIZE;
			goto drop;
		}
		break;
#endif /* INET6 */

	default:
		DPRINTF("unknown/unsupported protocol family %d, IPCA %s/%08x",
		    tdb->tdb_dst.sa.sa_family,
		    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
		    ntohl(tdb->tdb_spi));
		ipcompstat_inc(ipcomps_nopf);
		error = EPFNOSUPPORT;
		goto drop;
	}

	/* Update the counters */
	tdb->tdb_cur_bytes += m->m_pkthdr.len - skip;
	ipcompstat_add(ipcomps_obytes, m->m_pkthdr.len - skip);

	/* Hard byte expiration */
	if ((tdb->tdb_flags & TDBF_BYTES) &&
	    (tdb->tdb_cur_bytes >= tdb->tdb_exp_bytes)) {
		ipsecstat_inc(ipsec_exctdb);
		pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_HARD);
		tdb_delete(tdb);
		error = EINVAL;
		goto drop;
	}

	/* Soft byte expiration */
	mtx_enter(&tdb->tdb_mtx);
	if ((tdb->tdb_flags & TDBF_SOFT_BYTES) &&
	    (tdb->tdb_cur_bytes >= tdb->tdb_soft_bytes)) {
		tdb->tdb_flags &= ~TDBF_SOFT_BYTES;  /* Turn off checking */
		mtx_leave(&tdb->tdb_mtx);
		/* may sleep in solock() for the pfkey socket */
		pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_SOFT);
	} else
		mtx_leave(&tdb->tdb_mtx);

	/*
	 * Loop through mbuf chain; if we find a readonly mbuf,
	 * copy the packet.
	 */
	mi = m;
	while (mi != NULL && !M_READONLY(mi))
		mi = mi->m_next;

	if (mi != NULL) {
		struct mbuf *n = m_dup_pkt(m, 0, M_DONTWAIT);

		if (n == NULL) {
			DPRINTF("bad mbuf chain, IPCA %s/%08x",
			    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
			    ntohl(tdb->tdb_spi));
			ipcompstat_inc(ipcomps_hdrops);
			error = ENOBUFS;
			goto drop;
		}

		m_freem(m);
		m = n;
	}
	/* Ok now, we can pass to the crypto processing */

	/* Get crypto descriptors */
	crp = crypto_getreq(1);
	if (crp == NULL) {
		DPRINTF("failed to acquire crypto descriptors");
		ipcompstat_inc(ipcomps_crypto);
		error = ENOBUFS;
		goto drop;
	}
	crdc = &crp->crp_desc[0];

	/* Compression descriptor */
	crdc->crd_skip = skip;
	crdc->crd_len = m->m_pkthdr.len - skip;
	crdc->crd_flags = CRD_F_COMP;
	crdc->crd_inject = skip;

	/* Compression operation */
	crdc->crd_alg = ipcompx->type;

	/* Crypto operation descriptor */
	crp->crp_ilen = m->m_pkthdr.len;	/* Total input length */
	crp->crp_flags = CRYPTO_F_IMBUF;
	crp->crp_buf = (caddr_t)m;
	crp->crp_sid = tdb->tdb_cryptoid;

	while ((error = crypto_invoke(crp)) == EAGAIN) {
		/* Reset the session ID */
		if (tdb->tdb_cryptoid != 0)
			tdb->tdb_cryptoid = crp->crp_sid;
	}
	if (error) {
		DPRINTF("crypto error %d", error);
		ipsecstat_inc(ipsec_noxform);
		goto drop;
	}

	ilen = crp->crp_ilen;
	olen = crp->crp_olen;

	/* Release the crypto descriptors */
	crypto_freereq(crp);
	crp = NULL;

	rlen = ilen - skip;

	/* Check sizes. */
	if (rlen <= olen + IPCOMP_HLENGTH) {
		/* Compression was useless, we have lost time. */
		ipcompstat_inc(ipcomps_minlen); /* misnomer, but like to count */
		goto skiphdr;
	}

	/* Inject IPCOMP header */
	mo = m_makespace(m, skip, IPCOMP_HLENGTH, &roff);
	if (mo == NULL) {
		DPRINTF("ailed to inject IPCOMP header for IPCA %s/%08x",
		    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
		    ntohl(tdb->tdb_spi));
		ipcompstat_inc(ipcomps_wrap);
		error = ENOBUFS;
		goto drop;
	}

	/* Initialize the IPCOMP header */
	ipcomp = (struct ipcomp *)(mtod(mo, caddr_t) + roff);
	memset(ipcomp, 0, sizeof(struct ipcomp));
	cpi = (u_int16_t) ntohl(tdb->tdb_spi);
	ipcomp->ipcomp_cpi = htons(cpi);

	/* m_pullup before ? */
	switch (tdb->tdb_dst.sa.sa_family) {
	case AF_INET:
		ip = mtod(m, struct ip *);
		ipcomp->ipcomp_nh = ip->ip_p;
		ip->ip_p = IPPROTO_IPCOMP;
		break;
#ifdef INET6
	case AF_INET6:
		ip6 = mtod(m, struct ip6_hdr *);
		ipcomp->ipcomp_nh = ip6->ip6_nxt;
		ip6->ip6_nxt = IPPROTO_IPCOMP;
		break;
#endif
	default:
		DPRINTF("unsupported protocol family %d, IPCA %s/%08x",
		    tdb->tdb_dst.sa.sa_family,
		    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
		    ntohl(tdb->tdb_spi));
		ipcompstat_inc(ipcomps_nopf);
		error = EPFNOSUPPORT;
		goto drop;
	}

 skiphdr:
	error = ipsp_process_done(m, tdb);
	if (error)
		ipcompstat_inc(ipcomps_outfail);
	return error;

 drop:
	m_freem(m);
	crypto_freereq(crp);
	return error;
}
