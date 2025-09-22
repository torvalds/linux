/*	$OpenBSD: ip_ipip.c,v 1.111 2025/07/18 08:39:14 mvs Exp $ */
/*
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
 * Copyright (c) 2001, Angelos D. Keromytis.
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

/*
 * IP-inside-IP processing
 */

#include "bpfilter.h"
#include "gif.h"
#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_ipsp.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6_var.h>
#include <netinet/ip_ecn.h>
#include <netinet/ip_ipip.h>

#if NPF > 0
#include <net/pfvar.h>
#endif

/*
 * Locks used to protect data:
 *	a	atomic
 */

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
 * We can control the acceptance of IP4 packets by altering the sysctl
 * net.inet.ipip.allow value.  Zero means drop them, all else is acceptance.
 */
int ipip_allow = 0;	/* [a] */

struct cpumem *ipipcounters;

void
ipip_init(void)
{
	ipipcounters = counters_alloc(ipips_ncounters);
}

/*
 * Really only a wrapper for ipip_input_if(), for use with pr_input.
 */
int
ipip_input(struct mbuf **mp, int *offp, int nxt, int af, struct netstack *ns)
{
	struct ifnet *ifp;
	int ipip_allow_local = atomic_load_int(&ipip_allow);

	/* If we do not accept IP-in-IP explicitly, drop.  */
	if (ipip_allow_local == 0 && ((*mp)->m_flags & (M_AUTH|M_CONF)) == 0) {
		DPRINTF("dropped due to policy");
		ipipstat_inc(ipips_pdrops);
		m_freemp(mp);
		return IPPROTO_DONE;
	}

	ifp = if_get((*mp)->m_pkthdr.ph_ifidx);
	if (ifp == NULL) {
		m_freemp(mp);
		return IPPROTO_DONE;
	}
	nxt = ipip_input_if(mp, offp, nxt, af, ipip_allow_local, ifp, ns);
	if_put(ifp);

	return nxt;
}

/*
 * ipip_input gets called when we receive an IP{46} encapsulated packet,
 * either because we got it at a real interface, or because AH or ESP
 * were being used in tunnel mode (in which case the ph_ifidx element
 * will contain the index of the encX interface associated with the
 * tunnel.
 */

int
ipip_input_if(struct mbuf **mp, int *offp, int proto, int oaf, int allow,
    struct ifnet *ifp, struct netstack *ns)
{
	struct mbuf *m = *mp;
	struct sockaddr_in *sin;
	struct ip *ip;
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct ip6_hdr *ip6;
#endif
	int mode, hlen;
	u_int8_t itos, otos;
	sa_family_t iaf;

	ipipstat_inc(ipips_ipackets);

	switch (oaf) {
	case AF_INET:
		hlen = sizeof(struct ip);
		break;
#ifdef INET6
	case AF_INET6:
		hlen = sizeof(struct ip6_hdr);
		break;
#endif
	default:
		unhandled_af(oaf);
	}

	/* Bring the IP header in the first mbuf, if not there already */
	if (m->m_len < hlen) {
		if ((m = *mp = m_pullup(m, hlen)) == NULL) {
			DPRINTF("m_pullup() failed");
			ipipstat_inc(ipips_hdrops);
			goto bad;
		}
	}

	/* Keep outer ecn field. */
	switch (oaf) {
	case AF_INET:
		ip = mtod(m, struct ip *);
		otos = ip->ip_tos;
		break;
#ifdef INET6
	case AF_INET6:
		ip6 = mtod(m, struct ip6_hdr *);
		otos = (ntohl(ip6->ip6_flow) >> 20) & 0xff;
		break;
#endif
	}

	/* Remove outer IP header */
	KASSERT(*offp > 0);
	m_adj(m, *offp);
	*offp = 0;
	ip = NULL;
#ifdef INET6
	ip6 = NULL;
#endif

	switch (proto) {
	case IPPROTO_IPV4:
		hlen = sizeof(struct ip);
		break;

#ifdef INET6
	case IPPROTO_IPV6:
		hlen = sizeof(struct ip6_hdr);
		break;
#endif
	default:
		ipipstat_inc(ipips_family);
		goto bad;
	}

	/* Sanity check */
	if (m->m_pkthdr.len < hlen) {
		ipipstat_inc(ipips_hdrops);
		goto bad;
	}

	/*
	 * Bring the inner header into the first mbuf, if not there already.
	 */
	if (m->m_len < hlen) {
		if ((m = *mp = m_pullup(m, hlen)) == NULL) {
			DPRINTF("m_pullup() failed");
			ipipstat_inc(ipips_hdrops);
			goto bad;
		}
	}

	/*
	 * RFC 1853 specifies that the inner TTL should not be touched on
	 * decapsulation. There's no reason this comment should be here, but
	 * this is as good as any a position.
	 */

	/* Some sanity checks in the inner IP header */
	switch (proto) {
	case IPPROTO_IPV4:
		iaf = AF_INET;
		ip = mtod(m, struct ip *);
		hlen = ip->ip_hl << 2;
		if (m->m_pkthdr.len < hlen) {
			ipipstat_inc(ipips_hdrops);
			goto bad;
		}
		itos = ip->ip_tos;
		mode = m->m_flags & (M_AUTH|M_CONF) ?
		    ECN_ALLOWED_IPSEC : ECN_ALLOWED;
		if (!ip_ecn_egress(mode, &otos, &itos)) {
			DPRINTF("ip_ecn_egress() failed");
			ipipstat_inc(ipips_pdrops);
			goto bad;
		}
		/* re-calculate the checksum if ip_tos was changed */
		if (itos != ip->ip_tos)
			ip_tos_patch(ip, itos);
		break;
#ifdef INET6
	case IPPROTO_IPV6:
		iaf = AF_INET6;
		ip6 = mtod(m, struct ip6_hdr *);
		itos = (ntohl(ip6->ip6_flow) >> 20) & 0xff;
		if (!ip_ecn_egress(ECN_ALLOWED, &otos, &itos)) {
			DPRINTF("ip_ecn_egress() failed");
			ipipstat_inc(ipips_pdrops);
			goto bad;
		}
		ip6->ip6_flow &= ~htonl(0xff << 20);
		ip6->ip6_flow |= htonl((u_int32_t) itos << 20);
		break;
#endif
	}

	/* Check for local address spoofing. */
	if (!(ifp->if_flags & IFF_LOOPBACK) && allow != 2) {
		struct sockaddr_storage ss;
		struct rtentry *rt;

		memset(&ss, 0, sizeof(ss));

		if (ip) {
			sin = (struct sockaddr_in *)&ss;
			sin->sin_family = AF_INET;
			sin->sin_len = sizeof(*sin);
			sin->sin_addr = ip->ip_src;
#ifdef INET6
		} else if (ip6) {
			sin6 = (struct sockaddr_in6 *)&ss;
			sin6->sin6_family = AF_INET6;
			sin6->sin6_len = sizeof(*sin6);
			sin6->sin6_addr = ip6->ip6_src;
#endif /* INET6 */
		}
		rt = rtalloc(sstosa(&ss), 0, m->m_pkthdr.ph_rtableid);
		if ((rt != NULL) && (rt->rt_flags & RTF_LOCAL)) {
			ipipstat_inc(ipips_spoof);
			rtfree(rt);
			goto bad;
		}
		rtfree(rt);
	}

	/* Statistics */
	ipipstat_add(ipips_ibytes, m->m_pkthdr.len - hlen);

#if NBPFILTER > 0 && NGIF > 0
	if (ifp->if_type == IFT_GIF && ifp->if_bpf != NULL)
		bpf_mtap_af(ifp->if_bpf, iaf, m, BPF_DIRECTION_IN);
#endif
#if NPF > 0
	pf_pkt_addr_changed(m);
#endif

	/*
	 * Interface pointer stays the same; if no IPsec processing has
	 * been done (or will be done), this will point to a normal
	 * interface. Otherwise, it'll point to an enc interface, which
	 * will allow a packet filter to distinguish between secure and
	 * untrusted packets.
	 */

	switch (proto) {
	case IPPROTO_IPV4:
		return ip_input_if(mp, offp, proto, oaf, ifp, ns);
#ifdef INET6
	case IPPROTO_IPV6:
		return ip6_input_if(mp, offp, proto, oaf, ifp, ns);
#endif
	}
 bad:
	m_freemp(mp);
	return IPPROTO_DONE;
}

int
ipip_output(struct mbuf **mp, struct tdb *tdb)
{
	struct mbuf *m = *mp;
	u_int8_t tp, otos, itos;
	u_int64_t obytes;
	struct ip *ipo;
#ifdef INET6
	struct ip6_hdr *ip6, *ip6o;
#endif /* INET6 */
#ifdef ENCDEBUG
	char buf[INET6_ADDRSTRLEN];
#endif
	int error;

	/* XXX Deal with empty TDB source/destination addresses. */

	m_copydata(m, 0, 1, &tp);
	tp = (tp >> 4) & 0xff;  /* Get the IP version number. */

	switch (tdb->tdb_dst.sa.sa_family) {
	case AF_INET:
		if (tdb->tdb_src.sa.sa_family != AF_INET ||
		    tdb->tdb_src.sin.sin_addr.s_addr == INADDR_ANY ||
		    tdb->tdb_dst.sin.sin_addr.s_addr == INADDR_ANY) {

			DPRINTF("unspecified tunnel endpoint address "
			    "in SA %s/%08x",
			    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
			    ntohl(tdb->tdb_spi));

			ipipstat_inc(ipips_unspec);
			error = EINVAL;
			goto drop;
		}

		M_PREPEND(*mp, sizeof(struct ip), M_DONTWAIT);
		if (*mp == NULL) {
			DPRINTF("M_PREPEND failed");
			ipipstat_inc(ipips_hdrops);
			error = ENOBUFS;
			goto drop;
		}
		m = *mp;

		ipo = mtod(m, struct ip *);

		ipo->ip_v = IPVERSION;
		ipo->ip_hl = 5;
		ipo->ip_len = htons(m->m_pkthdr.len);
		ipo->ip_ttl = atomic_load_int(&ip_defttl);
		ipo->ip_sum = 0;
		ipo->ip_src = tdb->tdb_src.sin.sin_addr;
		ipo->ip_dst = tdb->tdb_dst.sin.sin_addr;

		/*
		 * We do the htons() to prevent snoopers from determining our
		 * endianness.
		 */
		ipo->ip_id = htons(ip_randomid());

		/* If the inner protocol is IP... */
		if (tp == IPVERSION) {
			/* Save ECN notification */
			m_copydata(m, sizeof(struct ip) +
			    offsetof(struct ip, ip_tos),
			    sizeof(u_int8_t), (caddr_t) &itos);

			ipo->ip_p = IPPROTO_IPIP;

			/*
			 * We should be keeping tunnel soft-state and
			 * send back ICMPs if needed.
			 */
			m_copydata(m, sizeof(struct ip) +
			    offsetof(struct ip, ip_off),
			    sizeof(u_int16_t), (caddr_t) &ipo->ip_off);
			ipo->ip_off = ntohs(ipo->ip_off);
			ipo->ip_off &= ~(IP_DF | IP_MF | IP_OFFMASK);
			ipo->ip_off = htons(ipo->ip_off);
		}
#ifdef INET6
		else if (tp == (IPV6_VERSION >> 4)) {
			u_int32_t itos32;

			/* Save ECN notification. */
			m_copydata(m, sizeof(struct ip) +
			    offsetof(struct ip6_hdr, ip6_flow),
			    sizeof(u_int32_t), (caddr_t) &itos32);
			itos = ntohl(itos32) >> 20;
			ipo->ip_p = IPPROTO_IPV6;
			ipo->ip_off = 0;
		}
#endif /* INET6 */
		else {
			ipipstat_inc(ipips_family);
			error = EAFNOSUPPORT;
			goto drop;
		}

		otos = 0;
		ip_ecn_ingress(ECN_ALLOWED, &otos, &itos);
		ipo->ip_tos = otos;

		obytes = m->m_pkthdr.len - sizeof(struct ip);
		if (tdb->tdb_xform->xf_type == XF_IP4)
			tdb->tdb_cur_bytes += obytes;
		break;

#ifdef INET6
	case AF_INET6:
		if (IN6_IS_ADDR_UNSPECIFIED(&tdb->tdb_dst.sin6.sin6_addr) ||
		    tdb->tdb_src.sa.sa_family != AF_INET6 ||
		    IN6_IS_ADDR_UNSPECIFIED(&tdb->tdb_src.sin6.sin6_addr)) {

			DPRINTF("unspecified tunnel endpoint address "
			    "in SA %s/%08x",
			    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
			    ntohl(tdb->tdb_spi));

			ipipstat_inc(ipips_unspec);
			error = EINVAL;
			goto drop;
		}

		/* If the inner protocol is IPv6, clear link local scope */
		if (tp == (IPV6_VERSION >> 4)) {
			/* scoped address handling */
			ip6 = mtod(m, struct ip6_hdr *);
			if (IN6_IS_SCOPE_EMBED(&ip6->ip6_src))
				ip6->ip6_src.s6_addr16[1] = 0;
			if (IN6_IS_SCOPE_EMBED(&ip6->ip6_dst))
				ip6->ip6_dst.s6_addr16[1] = 0;
		}

		M_PREPEND(*mp, sizeof(struct ip6_hdr), M_DONTWAIT);
		if (*mp == NULL) {
			DPRINTF("M_PREPEND failed");
			ipipstat_inc(ipips_hdrops);
			error = ENOBUFS;
			goto drop;
		}
		m = *mp;

		/* Initialize IPv6 header */
		ip6o = mtod(m, struct ip6_hdr *);
		ip6o->ip6_flow = 0;
		ip6o->ip6_vfc &= ~IPV6_VERSION_MASK;
		ip6o->ip6_vfc |= IPV6_VERSION;
		ip6o->ip6_plen = htons(m->m_pkthdr.len - sizeof(*ip6o));
		ip6o->ip6_hlim = atomic_load_int(&ip6_defhlim);
		in6_embedscope(&ip6o->ip6_src, &tdb->tdb_src.sin6, NULL, NULL);
		in6_embedscope(&ip6o->ip6_dst, &tdb->tdb_dst.sin6, NULL, NULL);

		if (tp == IPVERSION) {
			/* Save ECN notification */
			m_copydata(m, sizeof(struct ip6_hdr) +
			    offsetof(struct ip, ip_tos), sizeof(u_int8_t),
			    (caddr_t) &itos);

			/* This is really IPVERSION. */
			ip6o->ip6_nxt = IPPROTO_IPIP;
		}
		else
			if (tp == (IPV6_VERSION >> 4)) {
				u_int32_t itos32;

				/* Save ECN notification. */
				m_copydata(m, sizeof(struct ip6_hdr) +
				    offsetof(struct ip6_hdr, ip6_flow),
				    sizeof(u_int32_t), (caddr_t) &itos32);
				itos = ntohl(itos32) >> 20;

				ip6o->ip6_nxt = IPPROTO_IPV6;
			} else {
				ipipstat_inc(ipips_family);
				error = EAFNOSUPPORT;
				goto drop;
			}

		otos = 0;
		ip_ecn_ingress(ECN_ALLOWED, &otos, &itos);
		ip6o->ip6_flow |= htonl((u_int32_t) otos << 20);

		obytes = m->m_pkthdr.len - sizeof(struct ip6_hdr);
		if (tdb->tdb_xform->xf_type == XF_IP4)
			tdb->tdb_cur_bytes += obytes;
		break;
#endif /* INET6 */

	default:
		DPRINTF("unsupported protocol family %d",
		    tdb->tdb_dst.sa.sa_family);
		ipipstat_inc(ipips_family);
		error = EPFNOSUPPORT;
		goto drop;
	}

	ipipstat_pkt(ipips_opackets, ipips_obytes, obytes);
	return 0;

 drop:
	m_freemp(mp);
	return error;
}

#ifdef IPSEC
int
ipe4_attach(void)
{
	return 0;
}

int
ipe4_init(struct tdb *tdbp, const struct xformsw *xsp, struct ipsecinit *ii)
{
	tdbp->tdb_xform = xsp;
	return 0;
}

int
ipe4_zeroize(struct tdb *tdbp)
{
	return 0;
}

int
ipe4_input(struct mbuf **mp, struct tdb *tdb, int hlen, int proto,
    struct netstack *ns)
{
	/* This is a rather serious mistake, so no conditional printing. */
	printf("%s: should never be called\n", __func__);
	m_freemp(mp);
	return EINVAL;
}
#endif	/* IPSEC */

#ifndef SMALL_KERNEL
int
ipip_sysctl_ipipstat(void *oldp, size_t *oldlenp, void *newp)
{
	struct ipipstat ipipstat;

	CTASSERT(sizeof(ipipstat) == (ipips_ncounters * sizeof(uint64_t)));
	memset(&ipipstat, 0, sizeof ipipstat);
	counters_read(ipipcounters, (uint64_t *)&ipipstat, ipips_ncounters,
	    NULL);
	return (sysctl_rdstruct(oldp, oldlenp, newp,
	    &ipipstat, sizeof(ipipstat)));
}

int
ipip_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case IPIPCTL_ALLOW:
		return (sysctl_int_bounded(oldp, oldlenp, newp, newlen,
		    &ipip_allow, 0, 2));
	case IPIPCTL_STATS:
		return (ipip_sysctl_ipipstat(oldp, oldlenp, newp));
	default:
		return (ENOPROTOOPT);
	}
	/* NOTREACHED */
}
#endif /* SMALL_KERNEL */
