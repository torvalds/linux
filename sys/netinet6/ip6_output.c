/*	$OpenBSD: ip6_output.c,v 1.303 2025/07/25 20:04:47 mvs Exp $	*/
/*	$KAME: ip6_output.c,v 1.172 2001/03/25 09:55:56 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982, 1986, 1988, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ip_output.c	8.3 (Berkeley) 1/21/94
 */

#include "pf.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_enc.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>

#include <netinet/ip_var.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp_var.h>

#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/ip6_var.h>

#include <crypto/idgen.h>

#if NPF > 0
#include <net/pfvar.h>
#endif

#ifdef IPSEC
#include <netinet/ip_ipsp.h>

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
#endif /* IPSEC */

struct ip6_exthdrs {
	struct mbuf *ip6e_ip6;
	struct mbuf *ip6e_hbh;
	struct mbuf *ip6e_dest1;
	struct mbuf *ip6e_rthdr;
	struct mbuf *ip6e_dest2;
};

int ip6_pcbopt(int, u_char *, int, struct ip6_pktopts **, int, int);
int ip6_getpcbopt(struct ip6_pktopts *, int, struct mbuf *);
int ip6_setpktopt(int, u_char *, int, struct ip6_pktopts *, int, int, int);
int ip6_setmoptions(int, struct ip6_moptions **, struct mbuf *, unsigned int);
int ip6_getmoptions(int, struct ip6_moptions *, struct mbuf *);
int ip6_copyexthdr(struct mbuf **, caddr_t, int);
int ip6_insertfraghdr(struct mbuf *, struct mbuf *, int,
	struct ip6_frag **);
int ip6_insert_jumboopt(struct ip6_exthdrs *, u_int32_t);
int ip6_splithdr(struct mbuf *, struct ip6_exthdrs *);
int ip6_getpmtu(struct rtentry *, struct ifnet *, u_long *);
int copypktopts(struct ip6_pktopts *, struct ip6_pktopts *);
static __inline u_int16_t __attribute__((__unused__))
    in6_cksum_phdr(const struct in6_addr *, const struct in6_addr *,
    u_int32_t, u_int32_t);
void in6_delayed_cksum(struct mbuf *, u_int8_t);

int ip6_output_ipsec_pmtu_update(struct tdb *, struct route *,
    struct in6_addr *, int, int, int);

/* Context for non-repeating IDs */
struct idgen32_ctx ip6_id_ctx;

/*
 * IP6 output. The packet in mbuf chain m contains a skeletal IP6
 * header (with pri, len, nxt, hlim, src, dst).
 * This function may modify ver and hlim only.
 * The mbuf chain containing the packet will be freed.
 * The mbuf opt, if present, will not be freed.
 *
 * type of "mtu": rt_mtu is u_int, ifnet.ifr_mtu is int.
 * We use u_long to hold largest one.  XXX should be u_int
 */
int
ip6_output(struct mbuf *m, struct ip6_pktopts *opt, struct route *ro,
    int flags, struct ip6_moptions *im6o, const struct ipsec_level *seclevel)
{
	struct ip6_hdr *ip6;
	struct ifnet *ifp = NULL;
	struct mbuf_list ml;
	int hlen, tlen;
	struct route iproute;
	struct rtentry *rt = NULL;
	struct sockaddr_in6 *dst;
	int error = 0;
	u_long mtu;
	u_int orig_rtableid;
	int dontfrag;
	u_int16_t src_scope, dst_scope;
	u_int32_t optlen = 0, plen = 0, unfragpartlen = 0;
	struct ip6_exthdrs exthdrs;
	struct in6_addr finaldst;
	struct route *ro_pmtu = NULL;
	int hdrsplit = 0;
	u_int8_t sproto = 0;
	u_char nextproto;
#ifdef IPSEC
	struct tdb *tdb = NULL;
#endif /* IPSEC */

	ip6 = mtod(m, struct ip6_hdr *);
	finaldst = ip6->ip6_dst;

#define MAKE_EXTHDR(hp, mp)						\
    do {								\
	if (hp) {							\
		struct ip6_ext *eh = (struct ip6_ext *)(hp);		\
		error = ip6_copyexthdr((mp), (caddr_t)(hp),		\
		    ((eh)->ip6e_len + 1) << 3);				\
		if (error)						\
			goto freehdrs;					\
	}								\
    } while (0)

	bzero(&exthdrs, sizeof(exthdrs));

	if (opt) {
		/* Hop-by-Hop options header */
		MAKE_EXTHDR(opt->ip6po_hbh, &exthdrs.ip6e_hbh);
		/* Destination options header(1st part) */
		MAKE_EXTHDR(opt->ip6po_dest1, &exthdrs.ip6e_dest1);
		/* Routing header */
		MAKE_EXTHDR(opt->ip6po_rthdr, &exthdrs.ip6e_rthdr);
		/* Destination options header(2nd part) */
		MAKE_EXTHDR(opt->ip6po_dest2, &exthdrs.ip6e_dest2);
	}

#ifdef IPSEC
	if (ipsec_in_use || seclevel != NULL) {
		error = ip6_output_ipsec_lookup(m, seclevel, &tdb);
		if (error) {
			/*
			 * -EINVAL is used to indicate that the packet should
			 * be silently dropped, typically because we've asked
			 * key management for an SA.
			 */
			if (error == -EINVAL) /* Should silently drop packet */
				error = 0;

			goto freehdrs;
		}
	}
#endif /* IPSEC */

	/*
	 * Calculate the total length of the extension header chain.
	 * Keep the length of the unfragmentable part for fragmentation.
	 */
	optlen = 0;
	if (exthdrs.ip6e_hbh) optlen += exthdrs.ip6e_hbh->m_len;
	if (exthdrs.ip6e_dest1) optlen += exthdrs.ip6e_dest1->m_len;
	if (exthdrs.ip6e_rthdr) optlen += exthdrs.ip6e_rthdr->m_len;
	unfragpartlen = optlen + sizeof(struct ip6_hdr);
	/* NOTE: we don't add AH/ESP length here. do that later. */
	if (exthdrs.ip6e_dest2) optlen += exthdrs.ip6e_dest2->m_len;

	/*
	 * If we need IPsec, or there is at least one extension header,
	 * separate IP6 header from the payload.
	 */
	if ((sproto || optlen) && !hdrsplit) {
		if ((error = ip6_splithdr(m, &exthdrs)) != 0) {
			m = NULL;
			goto freehdrs;
		}
		m = exthdrs.ip6e_ip6;
		hdrsplit++;
	}

	/* adjust pointer */
	ip6 = mtod(m, struct ip6_hdr *);

	/* adjust mbuf packet header length */
	m->m_pkthdr.len += optlen;
	plen = m->m_pkthdr.len - sizeof(*ip6);

	/* If this is a jumbo payload, insert a jumbo payload option. */
	if (plen > IPV6_MAXPACKET) {
		if (!hdrsplit) {
			if ((error = ip6_splithdr(m, &exthdrs)) != 0) {
				m = NULL;
				goto freehdrs;
			}
			m = exthdrs.ip6e_ip6;
			hdrsplit++;
		}
		/* adjust pointer */
		ip6 = mtod(m, struct ip6_hdr *);
		if ((error = ip6_insert_jumboopt(&exthdrs, plen)) != 0)
			goto freehdrs;
		ip6->ip6_plen = 0;
	} else
		ip6->ip6_plen = htons(plen);

	/*
	 * Concatenate headers and fill in next header fields.
	 * Here we have, on "m"
	 *	IPv6 payload
	 * and we insert headers accordingly.  Finally, we should be getting:
	 *	IPv6 hbh dest1 rthdr ah* [esp* dest2 payload]
	 *
	 * during the header composing process, "m" points to IPv6 header.
	 * "mprev" points to an extension header prior to esp.
	 */
	{
		u_char *nexthdrp = &ip6->ip6_nxt;
		struct mbuf *mprev = m;

		/*
		 * we treat dest2 specially.  this makes IPsec processing
		 * much easier.  the goal here is to make mprev point the
		 * mbuf prior to dest2.
		 *
		 * result: IPv6 dest2 payload
		 * m and mprev will point to IPv6 header.
		 */
		if (exthdrs.ip6e_dest2) {
			if (!hdrsplit)
				panic("%s: assumption failed: hdr not split",
				    __func__);
			exthdrs.ip6e_dest2->m_next = m->m_next;
			m->m_next = exthdrs.ip6e_dest2;
			*mtod(exthdrs.ip6e_dest2, u_char *) = ip6->ip6_nxt;
			ip6->ip6_nxt = IPPROTO_DSTOPTS;
		}

#define MAKE_CHAIN(m, mp, p, i)\
    do {\
	if (m) {\
		if (!hdrsplit) \
			panic("assumption failed: hdr not split"); \
		*mtod((m), u_char *) = *(p);\
		*(p) = (i);\
		p = mtod((m), u_char *);\
		(m)->m_next = (mp)->m_next;\
		(mp)->m_next = (m);\
		(mp) = (m);\
	}\
    } while (0)
		/*
		 * result: IPv6 hbh dest1 rthdr dest2 payload
		 * m will point to IPv6 header.  mprev will point to the
		 * extension header prior to dest2 (rthdr in the above case).
		 */
		MAKE_CHAIN(exthdrs.ip6e_hbh, mprev, nexthdrp, IPPROTO_HOPOPTS);
		MAKE_CHAIN(exthdrs.ip6e_dest1, mprev, nexthdrp,
		    IPPROTO_DSTOPTS);
		MAKE_CHAIN(exthdrs.ip6e_rthdr, mprev, nexthdrp,
		    IPPROTO_ROUTING);
	}

	/*
	 * If there is a routing header, replace the destination address field
	 * with the first hop of the routing header.
	 */
	if (exthdrs.ip6e_rthdr) {
		struct ip6_rthdr *rh;
		struct ip6_rthdr0 *rh0;
		struct in6_addr *addr;

		rh = (struct ip6_rthdr *)(mtod(exthdrs.ip6e_rthdr,
		    struct ip6_rthdr *));
		switch (rh->ip6r_type) {
		case IPV6_RTHDR_TYPE_0:
			rh0 = (struct ip6_rthdr0 *)rh;
			addr = (struct in6_addr *)(rh0 + 1);
			ip6->ip6_dst = addr[0];
			bcopy(&addr[1], &addr[0],
			    sizeof(struct in6_addr) * (rh0->ip6r0_segleft - 1));
			addr[rh0->ip6r0_segleft - 1] = finaldst;
			break;
		default:	/* is it possible? */
			error = EINVAL;
			goto bad;
		}
	}

	/* Source address validation */
	if (!(flags & IPV6_UNSPECSRC) &&
	    IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src)) {
		/*
		 * XXX: we can probably assume validation in the caller, but
		 * we explicitly check the address here for safety.
		 */
		error = EOPNOTSUPP;
		ip6stat_inc(ip6s_badscope);
		goto bad;
	}
	if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_src)) {
		error = EOPNOTSUPP;
		ip6stat_inc(ip6s_badscope);
		goto bad;
	}

	ip6stat_inc(ip6s_localout);

	/*
	 * Route packet.
	 */
	orig_rtableid = m->m_pkthdr.ph_rtableid;
#if NPF > 0
reroute:
#endif

	/* initialize cached route */
	if (ro == NULL) {
		ro = &iproute;
		ro->ro_rt = NULL;
	}
	ro_pmtu = ro;
	if (opt && opt->ip6po_rthdr)
		ro = &opt->ip6po_route;
	dst = &ro->ro_dstsin6;

	/*
	 * if specified, try to fill in the traffic class field.
	 * do not override if a non-zero value is already set.
	 * we check the diffserv field and the ecn field separately.
	 */
	if (opt && opt->ip6po_tclass >= 0) {
		int mask = 0;

		if ((ip6->ip6_flow & htonl(0xfc << 20)) == 0)
			mask |= 0xfc;
		if ((ip6->ip6_flow & htonl(0x03 << 20)) == 0)
			mask |= 0x03;
		if (mask != 0)
			ip6->ip6_flow |=
			    htonl((opt->ip6po_tclass & mask) << 20);
	}

	/* fill in or override the hop limit field, if necessary. */
	if (opt && opt->ip6po_hlim != -1)
		ip6->ip6_hlim = opt->ip6po_hlim & 0xff;
	else if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
		if (im6o != NULL)
			ip6->ip6_hlim = im6o->im6o_hlim;
		else
			ip6->ip6_hlim = atomic_load_int(&ip6_defmcasthlim);
	}

#ifdef IPSEC
	if (tdb != NULL) {
		/*
		 * XXX what should we do if ip6_hlim == 0 and the
		 * packet gets tunneled?
		 */
		/*
		 * if we are source-routing, do not attempt to tunnel the
		 * packet just because ip6_dst is different from what tdb has.
		 * XXX
		 */
		error = ip6_output_ipsec_send(tdb, m, ro, orig_rtableid,
		    exthdrs.ip6e_rthdr ? 1 : 0, 0);
		goto done;
	}
#endif /* IPSEC */

	if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
		struct in6_pktinfo *pi = NULL;

		/*
		 * If the caller specify the outgoing interface
		 * explicitly, use it.
		 */
		if (opt != NULL && (pi = opt->ip6po_pktinfo) != NULL)
			ifp = if_get(pi->ipi6_ifindex);

		if (ifp == NULL && im6o != NULL)
			ifp = if_get(im6o->im6o_ifidx);
	}

	if (ifp == NULL) {
		rt = in6_selectroute(&ip6->ip6_dst, opt, ro,
		    m->m_pkthdr.ph_rtableid);
		if (rt == NULL) {
			ip6stat_inc(ip6s_noroute);
			error = EHOSTUNREACH;
			goto bad;
		}
		if (ISSET(rt->rt_flags, RTF_LOCAL))
			ifp = if_get(rtable_loindex(m->m_pkthdr.ph_rtableid));
		else
			ifp = if_get(rt->rt_ifidx);
		/*
		 * We aren't using rtisvalid() here because the UP/DOWN state
		 * machine is broken with some Ethernet drivers like em(4).
		 * As a result we might try to use an invalid cached route
		 * entry while an interface is being detached.
		 */
		if (ifp == NULL) {
			ip6stat_inc(ip6s_noroute);
			error = EHOSTUNREACH;
			goto bad;
		}
	} else {
		route6_cache(ro, &ip6->ip6_dst, NULL, m->m_pkthdr.ph_rtableid);
	}

	if (rt && (rt->rt_flags & RTF_GATEWAY) &&
	    !IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst))
		dst = satosin6(rt->rt_gateway);

	if (!IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst)) {
		/* Unicast */

		m->m_flags &= ~(M_BCAST | M_MCAST);	/* just in case */
	} else {
		/* Multicast */

		m->m_flags = (m->m_flags & ~M_BCAST) | M_MCAST;

		/*
		 * Confirm that the outgoing interface supports multicast.
		 */
		if ((ifp->if_flags & IFF_MULTICAST) == 0) {
			ip6stat_inc(ip6s_noroute);
			error = ENETUNREACH;
			goto bad;
		}

		if ((im6o == NULL || im6o->im6o_loop) &&
		    in6_hasmulti(&ip6->ip6_dst, ifp)) {
			/*
			 * If we belong to the destination multicast group
			 * on the outgoing interface, and the caller did not
			 * forbid loopback, loop back a copy.
			 * Can't defer TCP/UDP checksumming, do the
			 * computation now.
			 */
			in6_proto_cksum_out(m, NULL);
			ip6_mloopback(ifp, m, dst);
		}
#ifdef MROUTING
		else {
			/*
			 * If we are acting as a multicast router, perform
			 * multicast forwarding as if the packet had just
			 * arrived on the interface to which we are about
			 * to send.  The multicast forwarding function
			 * recursively calls this function, using the
			 * IPV6_FORWARDING flag to prevent infinite recursion.
			 *
			 * Multicasts that are looped back by ip6_mloopback(),
			 * above, will be forwarded by the ip6_input() routine,
			 * if necessary.
			 */
			if (atomic_load_int(&ip6_mforwarding) &&
			    ip6_mrouter[ifp->if_rdomain] &&
			    (flags & IPV6_FORWARDING) == 0) {
				if (ip6_mforward(ip6, ifp, m, flags) != 0) {
					m_freem(m);
					goto done;
				}
			}
		}
#endif
		/*
		 * Multicasts with a hoplimit of zero may be looped back,
		 * above, but must not be transmitted on a network.
		 * Also, multicasts addressed to the loopback interface
		 * are not sent -- the above call to ip6_mloopback() will
		 * loop back a copy if this host actually belongs to the
		 * destination group on the loopback interface.
		 */
		if (ip6->ip6_hlim == 0 || (ifp->if_flags & IFF_LOOPBACK) ||
		    IN6_IS_ADDR_MC_INTFACELOCAL(&ip6->ip6_dst)) {
			m_freem(m);
			goto done;
		}
	}

	/*
	 * If this packet is going through a loopback interface we won't
	 * be able to restore its scope ID using the interface index.
	 */
	if (IN6_IS_SCOPE_EMBED(&ip6->ip6_src)) {
		if (ifp->if_flags & IFF_LOOPBACK)
			src_scope = ip6->ip6_src.s6_addr16[1];
		ip6->ip6_src.s6_addr16[1] = 0;
	}
	if (IN6_IS_SCOPE_EMBED(&ip6->ip6_dst)) {
		if (ifp->if_flags & IFF_LOOPBACK)
			dst_scope = ip6->ip6_dst.s6_addr16[1];
		ip6->ip6_dst.s6_addr16[1] = 0;
	}

	/* Determine path MTU. */
	if ((error = ip6_getpmtu(ro_pmtu->ro_rt, ifp, &mtu)) != 0)
		goto bad;

	/*
	 * The caller of this function may specify to use the minimum MTU
	 * in some cases.
	 * An advanced API option (IPV6_USE_MIN_MTU) can also override MTU
	 * setting.  The logic is a bit complicated; by default, unicast
	 * packets will follow path MTU while multicast packets will be sent at
	 * the minimum MTU.  If IP6PO_MINMTU_ALL is specified, all packets
	 * including unicast ones will be sent at the minimum MTU.  Multicast
	 * packets will always be sent at the minimum MTU unless
	 * IP6PO_MINMTU_DISABLE is explicitly specified.
	 * See RFC 3542 for more details.
	 */
	if (mtu > IPV6_MMTU) {
		if ((flags & IPV6_MINMTU))
			mtu = IPV6_MMTU;
		else if (opt && opt->ip6po_minmtu == IP6PO_MINMTU_ALL)
			mtu = IPV6_MMTU;
		else if (IN6_IS_ADDR_MULTICAST(&ip6->ip6_dst) && (opt == NULL ||
		    opt->ip6po_minmtu != IP6PO_MINMTU_DISABLE)) {
			mtu = IPV6_MMTU;
		}
	}

	/*
	 * If the outgoing packet contains a hop-by-hop options header,
	 * it must be examined and processed even by the source node.
	 * (RFC 2460, section 4.)
	 */
	if (exthdrs.ip6e_hbh) {
		struct ip6_hbh *hbh = mtod(exthdrs.ip6e_hbh, struct ip6_hbh *);
		u_int32_t rtalert; /* returned value is ignored */
		u_int32_t plen = 0; /* no more than 1 jumbo payload option! */

		m->m_pkthdr.ph_ifidx = ifp->if_index;
		if (ip6_process_hopopts(&m, (u_int8_t *)(hbh + 1),
		    ((hbh->ip6h_len + 1) << 3) - sizeof(struct ip6_hbh),
		    &rtalert, &plen) < 0) {
			/* m was already freed at this point */
			error = EINVAL;/* better error? */
			goto done;
		}
		m->m_pkthdr.ph_ifidx = 0;
	}

#if NPF > 0
	if (pf_test(AF_INET6, PF_OUT, ifp, &m) != PF_PASS) {
		error = EACCES;
		m_freem(m);
		goto done;
	}
	if (m == NULL)
		goto done;
	ip6 = mtod(m, struct ip6_hdr *);
	if ((m->m_pkthdr.pf.flags & (PF_TAG_REROUTE | PF_TAG_GENERATED)) ==
	    (PF_TAG_REROUTE | PF_TAG_GENERATED)) {
		/* already rerun the route lookup, go on */
		m->m_pkthdr.pf.flags &= ~(PF_TAG_GENERATED | PF_TAG_REROUTE);
	} else if (m->m_pkthdr.pf.flags & PF_TAG_REROUTE) {
		/* tag as generated to skip over pf_test on rerun */
		m->m_pkthdr.pf.flags |= PF_TAG_GENERATED;
		finaldst = ip6->ip6_dst;
		if (ro == &iproute)
			rtfree(ro->ro_rt);
		ro = NULL;
		if_put(ifp); /* drop reference since destination changed */
		ifp = NULL;
		goto reroute;
	}
#endif

#ifdef IPSEC
	if (ISSET(flags, IPV6_FORWARDING) &&
	    ISSET(flags, IPV6_FORWARDING_IPSEC) &&
	    !ISSET(m->m_pkthdr.ph_tagsset, PACKET_TAG_IPSEC_IN_DONE)) {
		error = EHOSTUNREACH;
		goto bad;
	}
#endif

	/*
	 * If the packet is not going on the wire it can be destined
	 * to any local address.  In this case do not clear its scopes
	 * to let ip6_input() find a matching local route.
	 */
	if (ifp->if_flags & IFF_LOOPBACK) {
		if (IN6_IS_SCOPE_EMBED(&ip6->ip6_src))
			ip6->ip6_src.s6_addr16[1] = src_scope;
		if (IN6_IS_SCOPE_EMBED(&ip6->ip6_dst))
			ip6->ip6_dst.s6_addr16[1] = dst_scope;
	}

	/*
	 * Send the packet to the outgoing interface.
	 * If necessary, do IPv6 fragmentation before sending.
	 *
	 * the logic here is rather complex:
	 * 1: normal case (dontfrag == 0)
	 * 1-a: send as is if tlen <= path mtu
	 * 1-b: fragment if tlen > path mtu
	 *
	 * 2: if user asks us not to fragment (dontfrag == 1)
	 * 2-a: send as is if tlen <= interface mtu
	 * 2-b: error if tlen > interface mtu
	 */
	tlen = ISSET(m->m_pkthdr.csum_flags, M_TCP_TSO) ?
	    m->m_pkthdr.ph_mss : m->m_pkthdr.len;

	if (ISSET(m->m_pkthdr.csum_flags, M_IPV6_DF_OUT)) {
		CLR(m->m_pkthdr.csum_flags, M_IPV6_DF_OUT);
		dontfrag = 1;
	} else if (opt && ISSET(opt->ip6po_flags, IP6PO_DONTFRAG))
		dontfrag = 1;
	else
		dontfrag = 0;

	if (dontfrag && tlen > ifp->if_mtu) {		/* case 2-b */
#ifdef IPSEC
		if (atomic_load_int(&ip_mtudisc))
			ipsec_adjust_mtu(m, mtu);
#endif
		error = EMSGSIZE;
		goto bad;
	}

	/*
	 * transmit packet without fragmentation
	 */
	if (dontfrag || tlen <= mtu) {			/* case 1-a and 2-a */
		error = if_output_tso(ifp, &m, sin6tosa(dst), ro->ro_rt,
		    ifp->if_mtu);
		if (error || m == NULL)
			goto done;
		goto bad;				/* should not happen */
	}

	/*
	 * try to fragment the packet.  case 1-b
	 */
	if (mtu < IPV6_MMTU) {
		/* path MTU cannot be less than IPV6_MMTU */
		error = EMSGSIZE;
		goto bad;
	} else if (ip6->ip6_plen == 0) {
		/* jumbo payload cannot be fragmented */
		error = EMSGSIZE;
		goto bad;
	}

	/*
	 * Too large for the destination or interface;
	 * fragment if possible.
	 * Must be able to put at least 8 bytes per fragment.
	 */
	hlen = unfragpartlen;
	if (mtu > IPV6_MAXPACKET)
		mtu = IPV6_MAXPACKET;

	/*
	 * If we are doing fragmentation, we can't defer TCP/UDP
	 * checksumming; compute the checksum and clear the flag.
	 */
	in6_proto_cksum_out(m, NULL);

	/*
	 * Change the next header field of the last header in the
	 * unfragmentable part.
	 */
	if (exthdrs.ip6e_rthdr) {
		nextproto = *mtod(exthdrs.ip6e_rthdr, u_char *);
		*mtod(exthdrs.ip6e_rthdr, u_char *) = IPPROTO_FRAGMENT;
	} else if (exthdrs.ip6e_dest1) {
		nextproto = *mtod(exthdrs.ip6e_dest1, u_char *);
		*mtod(exthdrs.ip6e_dest1, u_char *) = IPPROTO_FRAGMENT;
	} else if (exthdrs.ip6e_hbh) {
		nextproto = *mtod(exthdrs.ip6e_hbh, u_char *);
		*mtod(exthdrs.ip6e_hbh, u_char *) = IPPROTO_FRAGMENT;
	} else {
		nextproto = ip6->ip6_nxt;
		ip6->ip6_nxt = IPPROTO_FRAGMENT;
	}

	if ((error = ip6_fragment(m, &ml, hlen, nextproto, mtu)) ||
	    (error = if_output_ml(ifp, &ml, sin6tosa(dst), ro->ro_rt)))
		goto done;
	ip6stat_inc(ip6s_fragmented);
	goto done;

 freehdrs:
	m_freem(exthdrs.ip6e_hbh);	/* m_freem will check if mbuf is 0 */
	m_freem(exthdrs.ip6e_dest1);
	m_freem(exthdrs.ip6e_rthdr);
	m_freem(exthdrs.ip6e_dest2);
 bad:
	m_freem(m);
 done:
	if (ro == &iproute)
		rtfree(ro->ro_rt);
	else if (ro_pmtu == &iproute)
		rtfree(ro_pmtu->ro_rt);
	if_put(ifp);
#ifdef IPSEC
	tdb_unref(tdb);
#endif /* IPSEC */
	return (error);
}

int
ip6_fragment(struct mbuf *m0, struct mbuf_list *ml, int hlen, u_char nextproto,
    u_long mtu)
{
	struct ip6_hdr *ip6;
	u_int32_t id;
	int tlen, len, off;
	int error;

	ml_init(ml);

	ip6 = mtod(m0, struct ip6_hdr *);
	tlen = m0->m_pkthdr.len;
	len = (mtu - hlen - sizeof(struct ip6_frag)) & ~7;
	if (len < 8) {
		error = EMSGSIZE;
		goto bad;
	}
	id = htonl(ip6_randomid());

	/*
	 * Loop through length of payload,
	 * make new header and copy data of each part and link onto chain.
	 */
	for (off = hlen; off < tlen; off += len) {
		struct mbuf *m;
		struct mbuf *mlast;
		struct ip6_hdr *mhip6;
		struct ip6_frag *ip6f;

		MGETHDR(m, M_DONTWAIT, MT_HEADER);
		if (m == NULL) {
			error = ENOBUFS;
			goto bad;
		}
		ml_enqueue(ml, m);
		if ((error = m_dup_pkthdr(m, m0, M_DONTWAIT)) != 0)
			goto bad;
		m->m_data += max_linkhdr;
		mhip6 = mtod(m, struct ip6_hdr *);
		*mhip6 = *ip6;
		m->m_len = sizeof(struct ip6_hdr);

		if ((error = ip6_insertfraghdr(m0, m, hlen, &ip6f)) != 0)
			goto bad;
		ip6f->ip6f_offlg = htons((off - hlen) & ~7);
		if (off + len >= tlen)
			len = tlen - off;
		else
			ip6f->ip6f_offlg |= IP6F_MORE_FRAG;

		m->m_pkthdr.len = hlen + sizeof(struct ip6_frag) + len;
		mhip6->ip6_plen = htons(m->m_pkthdr.len -
		    sizeof(struct ip6_hdr));
		for (mlast = m; mlast->m_next; mlast = mlast->m_next)
			;
		mlast->m_next = m_copym(m0, off, len, M_DONTWAIT);
		if (mlast->m_next == NULL) {
			error = ENOBUFS;
			goto bad;
		}

		ip6f->ip6f_reserved = 0;
		ip6f->ip6f_ident = id;
		ip6f->ip6f_nxt = nextproto;
	}

	ip6stat_add(ip6s_ofragments, ml_len(ml));
	m_freem(m0);
	return (0);

bad:
	ip6stat_inc(ip6s_odropped);
	ml_purge(ml);
	m_freem(m0);
	return (error);
}

int
ip6_copyexthdr(struct mbuf **mp, caddr_t hdr, int hlen)
{
	struct mbuf *m;

	if (hlen > MCLBYTES)
		return (ENOBUFS); /* XXX */

	MGET(m, M_DONTWAIT, MT_DATA);
	if (!m)
		return (ENOBUFS);

	if (hlen > MLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_free(m);
			return (ENOBUFS);
		}
	}
	m->m_len = hlen;
	if (hdr)
		memcpy(mtod(m, caddr_t), hdr, hlen);

	*mp = m;
	return (0);
}

/*
 * Insert jumbo payload option.
 */
int
ip6_insert_jumboopt(struct ip6_exthdrs *exthdrs, u_int32_t plen)
{
	struct mbuf *mopt;
	u_int8_t *optbuf;
	u_int32_t v;

#define JUMBOOPTLEN	8	/* length of jumbo payload option and padding */

	/*
	 * If there is no hop-by-hop options header, allocate new one.
	 * If there is one but it doesn't have enough space to store the
	 * jumbo payload option, allocate a cluster to store the whole options.
	 * Otherwise, use it to store the options.
	 */
	if (exthdrs->ip6e_hbh == 0) {
		MGET(mopt, M_DONTWAIT, MT_DATA);
		if (mopt == NULL)
			return (ENOBUFS);
		mopt->m_len = JUMBOOPTLEN;
		optbuf = mtod(mopt, u_int8_t *);
		optbuf[1] = 0;	/* = ((JUMBOOPTLEN) >> 3) - 1 */
		exthdrs->ip6e_hbh = mopt;
	} else {
		struct ip6_hbh *hbh;

		mopt = exthdrs->ip6e_hbh;
		if (m_trailingspace(mopt) < JUMBOOPTLEN) {
			/*
			 * XXX assumption:
			 * - exthdrs->ip6e_hbh is not referenced from places
			 *   other than exthdrs.
			 * - exthdrs->ip6e_hbh is not an mbuf chain.
			 */
			int oldoptlen = mopt->m_len;
			struct mbuf *n;

			/*
			 * XXX: give up if the whole (new) hbh header does
			 * not fit even in an mbuf cluster.
			 */
			if (oldoptlen + JUMBOOPTLEN > MCLBYTES)
				return (ENOBUFS);

			/*
			 * As a consequence, we must always prepare a cluster
			 * at this point.
			 */
			MGET(n, M_DONTWAIT, MT_DATA);
			if (n) {
				MCLGET(n, M_DONTWAIT);
				if ((n->m_flags & M_EXT) == 0) {
					m_freem(n);
					n = NULL;
				}
			}
			if (!n)
				return (ENOBUFS);
			n->m_len = oldoptlen + JUMBOOPTLEN;
			memcpy(mtod(n, caddr_t), mtod(mopt, caddr_t),
			      oldoptlen);
			optbuf = mtod(n, u_int8_t *) + oldoptlen;
			m_freem(mopt);
			mopt = exthdrs->ip6e_hbh = n;
		} else {
			optbuf = mtod(mopt, u_int8_t *) + mopt->m_len;
			mopt->m_len += JUMBOOPTLEN;
		}
		optbuf[0] = IP6OPT_PADN;
		optbuf[1] = 0;

		/*
		 * Adjust the header length according to the pad and
		 * the jumbo payload option.
		 */
		hbh = mtod(mopt, struct ip6_hbh *);
		hbh->ip6h_len += (JUMBOOPTLEN >> 3);
	}

	/* fill in the option. */
	optbuf[2] = IP6OPT_JUMBO;
	optbuf[3] = 4;
	v = (u_int32_t)htonl(plen + JUMBOOPTLEN);
	memcpy(&optbuf[4], &v, sizeof(u_int32_t));

	/* finally, adjust the packet header length */
	exthdrs->ip6e_ip6->m_pkthdr.len += JUMBOOPTLEN;

	return (0);
#undef JUMBOOPTLEN
}

/*
 * Insert fragment header and copy unfragmentable header portions.
 */
int
ip6_insertfraghdr(struct mbuf *m0, struct mbuf *m, int hlen,
    struct ip6_frag **frghdrp)
{
	struct mbuf *n, *mlast;

	if (hlen > sizeof(struct ip6_hdr)) {
		n = m_copym(m0, sizeof(struct ip6_hdr),
		    hlen - sizeof(struct ip6_hdr), M_DONTWAIT);
		if (n == NULL)
			return (ENOBUFS);
		m->m_next = n;
	} else
		n = m;

	/* Search for the last mbuf of unfragmentable part. */
	for (mlast = n; mlast->m_next; mlast = mlast->m_next)
		;

	if ((mlast->m_flags & M_EXT) == 0 &&
	    m_trailingspace(mlast) >= sizeof(struct ip6_frag)) {
		/* use the trailing space of the last mbuf for fragment hdr */
		*frghdrp = (struct ip6_frag *)(mtod(mlast, caddr_t) +
		    mlast->m_len);
		mlast->m_len += sizeof(struct ip6_frag);
		m->m_pkthdr.len += sizeof(struct ip6_frag);
	} else {
		/* allocate a new mbuf for the fragment header */
		struct mbuf *mfrg;

		MGET(mfrg, M_DONTWAIT, MT_DATA);
		if (mfrg == NULL)
			return (ENOBUFS);
		mfrg->m_len = sizeof(struct ip6_frag);
		*frghdrp = mtod(mfrg, struct ip6_frag *);
		mlast->m_next = mfrg;
	}

	return (0);
}

int
ip6_getpmtu(struct rtentry *rt, struct ifnet *ifp, u_long *mtup)
{
	u_int mtu, rtmtu;
	int error = 0;

	if (rt != NULL) {
		mtu = rtmtu = atomic_load_int(&rt->rt_mtu);
		if (mtu == 0)
			mtu = ifp->if_mtu;
		else if (mtu < IPV6_MMTU) {
			/* RFC8021 IPv6 Atomic Fragments Considered Harmful */
			mtu = IPV6_MMTU;
		} else if (mtu > ifp->if_mtu) {
			/*
			 * The MTU on the route is larger than the MTU on
			 * the interface!  This shouldn't happen, unless the
			 * MTU of the interface has been changed after the
			 * interface was brought up.  Change the MTU in the
			 * route to match the interface MTU (as long as the
			 * field isn't locked).
			 */
			mtu = ifp->if_mtu;
			if (!(rt->rt_locks & RTV_MTU))
				atomic_cas_uint(&rt->rt_mtu, rtmtu, mtu);
		}
	} else {
		mtu = ifp->if_mtu;
	}

	*mtup = mtu;
	return (error);
}

/*
 * IP6 socket option processing.
 */
int
ip6_ctloutput(int op, struct socket *so, int level, int optname,
    struct mbuf *m)
{
	int privileged, optdatalen, uproto;
	void *optdata;
	struct inpcb *inp = sotoinpcb(so);
	int error, optval;
	struct proc *p = curproc; /* For IPsec and rdomain */
	u_int rtableid, rtid = 0;

	error = optval = 0;

	privileged = (inp->inp_socket->so_state & SS_PRIV);
	uproto = (int)so->so_proto->pr_protocol;

	if (level != IPPROTO_IPV6)
		return (EINVAL);

	rtableid = p->p_p->ps_rtableid;

	switch (op) {
	case PRCO_SETOPT:
		switch (optname) {
		/*
		 * Use of some Hop-by-Hop options or some
		 * Destination options, might require special
		 * privilege.  That is, normal applications
		 * (without special privilege) might be forbidden
		 * from setting certain options in outgoing packets,
		 * and might never see certain options in received
		 * packets. [RFC 2292 Section 6]
		 * KAME specific note:
		 *  KAME prevents non-privileged users from sending or
		 *  receiving ANY hbh/dst options in order to avoid
		 *  overhead of parsing options in the kernel.
		 */
		case IPV6_RECVHOPOPTS:
		case IPV6_RECVDSTOPTS:
			if (!privileged) {
				error = EPERM;
				break;
			}
			/* FALLTHROUGH */
		case IPV6_UNICAST_HOPS:
		case IPV6_MINHOPCOUNT:
		case IPV6_HOPLIMIT:

		case IPV6_RECVPKTINFO:
		case IPV6_RECVHOPLIMIT:
		case IPV6_RECVRTHDR:
		case IPV6_RECVPATHMTU:
		case IPV6_RECVTCLASS:
		case IPV6_V6ONLY:
		case IPV6_AUTOFLOWLABEL:
		case IPV6_RECVDSTPORT:
			if (m == NULL || m->m_len != sizeof(int)) {
				error = EINVAL;
				break;
			}
			optval = *mtod(m, int *);
			switch (optname) {

			case IPV6_UNICAST_HOPS:
				if (optval < -1 || optval >= 256)
					error = EINVAL;
				else {
					/* -1 = kernel default */
					inp->inp_hops = optval;
				}
				break;

			case IPV6_MINHOPCOUNT:
				if (optval < 0 || optval > 255)
					error = EINVAL;
				else
					inp->inp_ip6_minhlim = optval;
				break;

#define OPTSET(bit) \
do { \
	if (optval) \
		inp->inp_flags |= (bit); \
	else \
		inp->inp_flags &= ~(bit); \
} while (/*CONSTCOND*/ 0)
#define OPTBIT(bit) (inp->inp_flags & (bit) ? 1 : 0)

			case IPV6_RECVPKTINFO:
				OPTSET(IN6P_PKTINFO);
				break;

			case IPV6_HOPLIMIT:
			{
				struct ip6_pktopts **optp;

				optp = &inp->inp_outputopts6;
				error = ip6_pcbopt(IPV6_HOPLIMIT,
				    (u_char *)&optval, sizeof(optval), optp,
				    privileged, uproto);
				break;
			}

			case IPV6_RECVHOPLIMIT:
				OPTSET(IN6P_HOPLIMIT);
				break;

			case IPV6_RECVHOPOPTS:
				OPTSET(IN6P_HOPOPTS);
				break;

			case IPV6_RECVDSTOPTS:
				OPTSET(IN6P_DSTOPTS);
				break;

			case IPV6_RECVRTHDR:
				OPTSET(IN6P_RTHDR);
				break;

			case IPV6_RECVPATHMTU:
				/*
				 * We ignore this option for TCP
				 * sockets.
				 * (RFC3542 leaves this case
				 * unspecified.)
				 */
				if (uproto != IPPROTO_TCP)
					OPTSET(IN6P_MTU);
				break;

			case IPV6_V6ONLY:
				/*
				 * make setsockopt(IPV6_V6ONLY)
				 * available only prior to bind(2).
				 * see ipng mailing list, Jun 22 2001.
				 */
				if (inp->inp_lport || !IN6_IS_ADDR_UNSPECIFIED(
				    &inp->inp_laddr6)) {
					error = EINVAL;
					break;
				}
				/* No support for IPv4-mapped addresses. */
				if (!optval)
					error = EINVAL;
				else
					error = 0;
				break;
			case IPV6_RECVTCLASS:
				OPTSET(IN6P_TCLASS);
				break;
			case IPV6_AUTOFLOWLABEL:
				OPTSET(IN6P_AUTOFLOWLABEL);
				break;

			case IPV6_RECVDSTPORT:
				OPTSET(IN6P_RECVDSTPORT);
				break;
			}
			break;

		case IPV6_TCLASS:
		case IPV6_DONTFRAG:
		case IPV6_USE_MIN_MTU:
			if (m == NULL || m->m_len != sizeof(optval)) {
				error = EINVAL;
				break;
			}
			optval = *mtod(m, int *);
			{
				struct ip6_pktopts **optp;
				optp = &inp->inp_outputopts6;
				error = ip6_pcbopt(optname, (u_char *)&optval,
				    sizeof(optval), optp, privileged, uproto);
				break;
			}

		case IPV6_PKTINFO:
		case IPV6_HOPOPTS:
		case IPV6_RTHDR:
		case IPV6_DSTOPTS:
		case IPV6_RTHDRDSTOPTS:
		{
			/* new advanced API (RFC3542) */
			u_char *optbuf;
			int optbuflen;
			struct ip6_pktopts **optp;

			if (m && m->m_next) {
				error = EINVAL;	/* XXX */
				break;
			}
			if (m) {
				optbuf = mtod(m, u_char *);
				optbuflen = m->m_len;
			} else {
				optbuf = NULL;
				optbuflen = 0;
			}
			optp = &inp->inp_outputopts6;
			error = ip6_pcbopt(optname, optbuf, optbuflen, optp,
			    privileged, uproto);
			break;
		}
#undef OPTSET

		case IPV6_MULTICAST_IF:
		case IPV6_MULTICAST_HOPS:
		case IPV6_MULTICAST_LOOP:
		case IPV6_JOIN_GROUP:
		case IPV6_LEAVE_GROUP:
			error =	ip6_setmoptions(optname,
						&inp->inp_moptions6,
						m, inp->inp_rtableid);
			break;

		case IPV6_PORTRANGE:
			if (m == NULL || m->m_len != sizeof(int)) {
				error = EINVAL;
				break;
			}
			optval = *mtod(m, int *);

			switch (optval) {
			case IPV6_PORTRANGE_DEFAULT:
				inp->inp_flags &= ~(IN6P_LOWPORT);
				inp->inp_flags &= ~(IN6P_HIGHPORT);
				break;

			case IPV6_PORTRANGE_HIGH:
				inp->inp_flags &= ~(IN6P_LOWPORT);
				inp->inp_flags |= IN6P_HIGHPORT;
				break;

			case IPV6_PORTRANGE_LOW:
				inp->inp_flags &= ~(IN6P_HIGHPORT);
				inp->inp_flags |= IN6P_LOWPORT;
				break;

			default:
				error = EINVAL;
				break;
			}
			break;

		case IPSEC6_OUTSA:
			error = EINVAL;
			break;

		case IPV6_AUTH_LEVEL:
		case IPV6_ESP_TRANS_LEVEL:
		case IPV6_ESP_NETWORK_LEVEL:
		case IPV6_IPCOMP_LEVEL:
#ifndef IPSEC
			error = EINVAL;
#else
			if (m == NULL || m->m_len != sizeof(int)) {
				error = EINVAL;
				break;
			}
			optval = *mtod(m, int *);

			if (optval < IPSEC_LEVEL_BYPASS ||
			    optval > IPSEC_LEVEL_UNIQUE) {
				error = EINVAL;
				break;
			}

			switch (optname) {
			case IPV6_AUTH_LEVEL:
				if (optval < IPSEC_AUTH_LEVEL_DEFAULT &&
				    suser(p)) {
					error = EACCES;
					break;
				}
				inp->inp_seclevel.sl_auth = optval;
				break;

			case IPV6_ESP_TRANS_LEVEL:
				if (optval < IPSEC_ESP_TRANS_LEVEL_DEFAULT &&
				    suser(p)) {
					error = EACCES;
					break;
				}
				inp->inp_seclevel.sl_esp_trans = optval;
				break;

			case IPV6_ESP_NETWORK_LEVEL:
				if (optval < IPSEC_ESP_NETWORK_LEVEL_DEFAULT &&
				    suser(p)) {
					error = EACCES;
					break;
				}
				inp->inp_seclevel.sl_esp_network = optval;
				break;

			case IPV6_IPCOMP_LEVEL:
				if (optval < IPSEC_IPCOMP_LEVEL_DEFAULT &&
				    suser(p)) {
					error = EACCES;
					break;
				}
				inp->inp_seclevel.sl_ipcomp = optval;
				break;
			}
#endif
			break;
		case SO_RTABLE:
			if (m == NULL || m->m_len < sizeof(u_int)) {
				error = EINVAL;
				break;
			}
			rtid = *mtod(m, u_int *);
			if (inp->inp_rtableid == rtid)
				break;
			/* needs privileges to switch when already set */
			if (rtableid != rtid && rtableid != 0 &&
			    (error = suser(p)) != 0)
				break;
			error = in_pcbset_rtableid(inp, rtid);
			break;
		case IPV6_PIPEX:
			if (m != NULL && m->m_len == sizeof(int))
				inp->inp_pipex = *mtod(m, int *);
			else
				error = EINVAL;
			break;

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;

	case PRCO_GETOPT:
		switch (optname) {

		case IPV6_RECVHOPOPTS:
		case IPV6_RECVDSTOPTS:
		case IPV6_UNICAST_HOPS:
		case IPV6_MINHOPCOUNT:
		case IPV6_RECVPKTINFO:
		case IPV6_RECVHOPLIMIT:
		case IPV6_RECVRTHDR:
		case IPV6_RECVPATHMTU:

		case IPV6_V6ONLY:
		case IPV6_PORTRANGE:
		case IPV6_RECVTCLASS:
		case IPV6_AUTOFLOWLABEL:
		case IPV6_RECVDSTPORT:
			switch (optname) {

			case IPV6_RECVHOPOPTS:
				optval = OPTBIT(IN6P_HOPOPTS);
				break;

			case IPV6_RECVDSTOPTS:
				optval = OPTBIT(IN6P_DSTOPTS);
				break;

			case IPV6_UNICAST_HOPS:
				optval = inp->inp_hops;
				break;

			case IPV6_MINHOPCOUNT:
				optval = inp->inp_ip6_minhlim;
				break;

			case IPV6_RECVPKTINFO:
				optval = OPTBIT(IN6P_PKTINFO);
				break;

			case IPV6_RECVHOPLIMIT:
				optval = OPTBIT(IN6P_HOPLIMIT);
				break;

			case IPV6_RECVRTHDR:
				optval = OPTBIT(IN6P_RTHDR);
				break;

			case IPV6_RECVPATHMTU:
				optval = OPTBIT(IN6P_MTU);
				break;

			case IPV6_V6ONLY:
				optval = 1;
				break;

			case IPV6_PORTRANGE:
			    {
				int flags;
				flags = inp->inp_flags;
				if (flags & IN6P_HIGHPORT)
					optval = IPV6_PORTRANGE_HIGH;
				else if (flags & IN6P_LOWPORT)
					optval = IPV6_PORTRANGE_LOW;
				else
					optval = 0;
				break;
			    }
			case IPV6_RECVTCLASS:
				optval = OPTBIT(IN6P_TCLASS);
				break;

			case IPV6_AUTOFLOWLABEL:
				optval = OPTBIT(IN6P_AUTOFLOWLABEL);
				break;

			case IPV6_RECVDSTPORT:
				optval = OPTBIT(IN6P_RECVDSTPORT);
				break;
			}
			if (error)
				break;
			m->m_len = sizeof(int);
			*mtod(m, int *) = optval;
			break;

		case IPV6_PATHMTU:
		{
			u_long pmtu = 0;
			struct ip6_mtuinfo mtuinfo;
			struct ifnet *ifp;
			struct rtentry *rt;

			if (!(so->so_state & SS_ISCONNECTED))
				return (ENOTCONN);

			rt = in6_pcbrtentry(inp);
			if (!rtisvalid(rt))
				return (EHOSTUNREACH);

			ifp = if_get(rt->rt_ifidx);
			if (ifp == NULL)
				return (EHOSTUNREACH);
			/*
			 * XXX: we dot not consider the case of source
			 * routing, or optional information to specify
			 * the outgoing interface.
			 */
			error = ip6_getpmtu(rt, ifp, &pmtu);
			if_put(ifp);
			if (error)
				break;
			if (pmtu > IPV6_MAXPACKET)
				pmtu = IPV6_MAXPACKET;

			bzero(&mtuinfo, sizeof(mtuinfo));
			mtuinfo.ip6m_mtu = (u_int32_t)pmtu;
			optdata = (void *)&mtuinfo;
			optdatalen = sizeof(mtuinfo);
			if (optdatalen > MCLBYTES)
				return (EMSGSIZE); /* XXX */
			if (optdatalen > MLEN)
				MCLGET(m, M_WAIT);
			m->m_len = optdatalen;
			bcopy(optdata, mtod(m, void *), optdatalen);
			break;
		}

		case IPV6_PKTINFO:
		case IPV6_HOPOPTS:
		case IPV6_RTHDR:
		case IPV6_DSTOPTS:
		case IPV6_RTHDRDSTOPTS:
		case IPV6_TCLASS:
		case IPV6_DONTFRAG:
		case IPV6_USE_MIN_MTU:
			error = ip6_getpcbopt(inp->inp_outputopts6,
			    optname, m);
			break;

		case IPV6_MULTICAST_IF:
		case IPV6_MULTICAST_HOPS:
		case IPV6_MULTICAST_LOOP:
		case IPV6_JOIN_GROUP:
		case IPV6_LEAVE_GROUP:
			error = ip6_getmoptions(optname,
			    inp->inp_moptions6, m);
			break;

		case IPSEC6_OUTSA:
			error = EINVAL;
			break;

		case IPV6_AUTH_LEVEL:
		case IPV6_ESP_TRANS_LEVEL:
		case IPV6_ESP_NETWORK_LEVEL:
		case IPV6_IPCOMP_LEVEL:
#ifndef IPSEC
			m->m_len = sizeof(int);
			*mtod(m, int *) = IPSEC_LEVEL_NONE;
#else
			m->m_len = sizeof(int);
			switch (optname) {
			case IPV6_AUTH_LEVEL:
				optval = inp->inp_seclevel.sl_auth;
				break;

			case IPV6_ESP_TRANS_LEVEL:
				optval =
				    inp->inp_seclevel.sl_esp_trans;
				break;

			case IPV6_ESP_NETWORK_LEVEL:
				optval =
				    inp->inp_seclevel.sl_esp_network;
				break;

			case IPV6_IPCOMP_LEVEL:
				optval = inp->inp_seclevel.sl_ipcomp;
				break;
			}
			*mtod(m, int *) = optval;
#endif
			break;
		case SO_RTABLE:
			m->m_len = sizeof(u_int);
			*mtod(m, u_int *) = inp->inp_rtableid;
			break;
		case IPV6_PIPEX:
			m->m_len = sizeof(int);
			*mtod(m, int *) = inp->inp_pipex;
			break;

		default:
			error = ENOPROTOOPT;
			break;
		}
		break;
	}
	return (error);
}

int
ip6_raw_ctloutput(int op, struct socket *so, int level, int optname,
    struct mbuf *m)
{
	int error = 0, optval;
	const int icmp6off = offsetof(struct icmp6_hdr, icmp6_cksum);
	struct inpcb *inp = sotoinpcb(so);

	if (level != IPPROTO_IPV6)
		return (EINVAL);

	switch (optname) {
	case IPV6_CHECKSUM:
		/*
		 * For ICMPv6 sockets, no modification allowed for checksum
		 * offset, permit "no change" values to help existing apps.
		 *
		 * RFC3542 says: "An attempt to set IPV6_CHECKSUM
		 * for an ICMPv6 socket will fail."
		 * The current behavior does not meet RFC3542.
		 */
		switch (op) {
		case PRCO_SETOPT:
			if (m == NULL || m->m_len != sizeof(int)) {
				error = EINVAL;
				break;
			}
			optval = *mtod(m, int *);
			if (optval < -1 ||
			    (optval > 0 && (optval % 2) != 0)) {
				/*
				 * The API assumes non-negative even offset
				 * values or -1 as a special value.
				 */
				error = EINVAL;
			} else if (so->so_proto->pr_protocol ==
			    IPPROTO_ICMPV6) {
				if (optval != icmp6off)
					error = EINVAL;
			} else
				inp->inp_cksum6 = optval;
			break;

		case PRCO_GETOPT:
			if (so->so_proto->pr_protocol == IPPROTO_ICMPV6)
				optval = icmp6off;
			else
				optval = inp->inp_cksum6;

			m->m_len = sizeof(int);
			*mtod(m, int *) = optval;
			break;

		default:
			error = EINVAL;
			break;
		}
		break;

	default:
		error = ENOPROTOOPT;
		break;
	}

	return (error);
}

/*
 * initialize ip6_pktopts.  beware that there are non-zero default values in
 * the struct.
 */
void
ip6_initpktopts(struct ip6_pktopts *opt)
{
	bzero(opt, sizeof(*opt));
	opt->ip6po_hlim = -1;	/* -1 means default hop limit */
	opt->ip6po_tclass = -1;	/* -1 means default traffic class */
	opt->ip6po_minmtu = IP6PO_MINMTU_MCASTONLY;
}

int
ip6_pcbopt(int optname, u_char *buf, int len, struct ip6_pktopts **pktopt,
    int priv, int uproto)
{
	struct ip6_pktopts *opt;

	if (*pktopt == NULL) {
		*pktopt = malloc(sizeof(struct ip6_pktopts), M_IP6OPT,
		    M_WAITOK);
		ip6_initpktopts(*pktopt);
	}
	opt = *pktopt;

	return (ip6_setpktopt(optname, buf, len, opt, priv, 1, uproto));
}

int
ip6_getpcbopt(struct ip6_pktopts *pktopt, int optname, struct mbuf *m)
{
	void *optdata = NULL;
	int optdatalen = 0;
	struct ip6_ext *ip6e;
	int error = 0;
	struct in6_pktinfo null_pktinfo;
	int deftclass = 0, on;
	int defminmtu = IP6PO_MINMTU_MCASTONLY;

	switch (optname) {
	case IPV6_PKTINFO:
		if (pktopt && pktopt->ip6po_pktinfo)
			optdata = (void *)pktopt->ip6po_pktinfo;
		else {
			/* XXX: we don't have to do this every time... */
			bzero(&null_pktinfo, sizeof(null_pktinfo));
			optdata = (void *)&null_pktinfo;
		}
		optdatalen = sizeof(struct in6_pktinfo);
		break;
	case IPV6_TCLASS:
		if (pktopt && pktopt->ip6po_tclass >= 0)
			optdata = (void *)&pktopt->ip6po_tclass;
		else
			optdata = (void *)&deftclass;
		optdatalen = sizeof(int);
		break;
	case IPV6_HOPOPTS:
		if (pktopt && pktopt->ip6po_hbh) {
			optdata = (void *)pktopt->ip6po_hbh;
			ip6e = (struct ip6_ext *)pktopt->ip6po_hbh;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_RTHDR:
		if (pktopt && pktopt->ip6po_rthdr) {
			optdata = (void *)pktopt->ip6po_rthdr;
			ip6e = (struct ip6_ext *)pktopt->ip6po_rthdr;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_RTHDRDSTOPTS:
		if (pktopt && pktopt->ip6po_dest1) {
			optdata = (void *)pktopt->ip6po_dest1;
			ip6e = (struct ip6_ext *)pktopt->ip6po_dest1;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_DSTOPTS:
		if (pktopt && pktopt->ip6po_dest2) {
			optdata = (void *)pktopt->ip6po_dest2;
			ip6e = (struct ip6_ext *)pktopt->ip6po_dest2;
			optdatalen = (ip6e->ip6e_len + 1) << 3;
		}
		break;
	case IPV6_USE_MIN_MTU:
		if (pktopt)
			optdata = (void *)&pktopt->ip6po_minmtu;
		else
			optdata = (void *)&defminmtu;
		optdatalen = sizeof(int);
		break;
	case IPV6_DONTFRAG:
		if (pktopt && ((pktopt->ip6po_flags) & IP6PO_DONTFRAG))
			on = 1;
		else
			on = 0;
		optdata = (void *)&on;
		optdatalen = sizeof(on);
		break;
	default:		/* should not happen */
#ifdef DIAGNOSTIC
		panic("%s: unexpected option", __func__);
#endif
		return (ENOPROTOOPT);
	}

	if (optdatalen > MCLBYTES)
		return (EMSGSIZE); /* XXX */
	if (optdatalen > MLEN)
		MCLGET(m, M_WAIT);
	m->m_len = optdatalen;
	if (optdatalen)
		bcopy(optdata, mtod(m, void *), optdatalen);

	return (error);
}

void
ip6_clearpktopts(struct ip6_pktopts *pktopt, int optname)
{
	if (optname == -1 || optname == IPV6_PKTINFO) {
		if (pktopt->ip6po_pktinfo)
			free(pktopt->ip6po_pktinfo, M_IP6OPT, 0);
		pktopt->ip6po_pktinfo = NULL;
	}
	if (optname == -1 || optname == IPV6_HOPLIMIT)
		pktopt->ip6po_hlim = -1;
	if (optname == -1 || optname == IPV6_TCLASS)
		pktopt->ip6po_tclass = -1;
	if (optname == -1 || optname == IPV6_HOPOPTS) {
		if (pktopt->ip6po_hbh)
			free(pktopt->ip6po_hbh, M_IP6OPT, 0);
		pktopt->ip6po_hbh = NULL;
	}
	if (optname == -1 || optname == IPV6_RTHDRDSTOPTS) {
		if (pktopt->ip6po_dest1)
			free(pktopt->ip6po_dest1, M_IP6OPT, 0);
		pktopt->ip6po_dest1 = NULL;
	}
	if (optname == -1 || optname == IPV6_RTHDR) {
		if (pktopt->ip6po_rhinfo.ip6po_rhi_rthdr)
			free(pktopt->ip6po_rhinfo.ip6po_rhi_rthdr, M_IP6OPT, 0);
		pktopt->ip6po_rhinfo.ip6po_rhi_rthdr = NULL;
		if (pktopt->ip6po_route.ro_rt) {
			rtfree(pktopt->ip6po_route.ro_rt);
			pktopt->ip6po_route.ro_rt = NULL;
		}
	}
	if (optname == -1 || optname == IPV6_DSTOPTS) {
		if (pktopt->ip6po_dest2)
			free(pktopt->ip6po_dest2, M_IP6OPT, 0);
		pktopt->ip6po_dest2 = NULL;
	}
}

#define PKTOPT_EXTHDRCPY(type) \
do {\
	if (src->type) {\
		size_t hlen;\
		hlen = (((struct ip6_ext *)src->type)->ip6e_len + 1) << 3;\
		dst->type = malloc(hlen, M_IP6OPT, M_NOWAIT);\
		if (dst->type == NULL)\
			goto bad;\
		memcpy(dst->type, src->type, hlen);\
	}\
} while (/*CONSTCOND*/ 0)

int
copypktopts(struct ip6_pktopts *dst, struct ip6_pktopts *src)
{
	dst->ip6po_hlim = src->ip6po_hlim;
	dst->ip6po_tclass = src->ip6po_tclass;
	dst->ip6po_flags = src->ip6po_flags;
	if (src->ip6po_pktinfo) {
		dst->ip6po_pktinfo = malloc(sizeof(*dst->ip6po_pktinfo),
		    M_IP6OPT, M_NOWAIT);
		if (dst->ip6po_pktinfo == NULL)
			goto bad;
		*dst->ip6po_pktinfo = *src->ip6po_pktinfo;
	}
	PKTOPT_EXTHDRCPY(ip6po_hbh);
	PKTOPT_EXTHDRCPY(ip6po_dest1);
	PKTOPT_EXTHDRCPY(ip6po_dest2);
	PKTOPT_EXTHDRCPY(ip6po_rthdr); /* not copy the cached route */
	return (0);

  bad:
	ip6_clearpktopts(dst, -1);
	return (ENOBUFS);
}
#undef PKTOPT_EXTHDRCPY

void
ip6_freepcbopts(struct ip6_pktopts *pktopt)
{
	if (pktopt == NULL)
		return;

	ip6_clearpktopts(pktopt, -1);

	free(pktopt, M_IP6OPT, 0);
}

/*
 * Set the IP6 multicast options in response to user setsockopt().
 */
int
ip6_setmoptions(int optname, struct ip6_moptions **im6op, struct mbuf *m,
    unsigned int rtableid)
{
	int error = 0;
	u_int loop, ifindex;
	struct ipv6_mreq *mreq;
	struct ifnet *ifp;
	struct ip6_moptions *im6o = *im6op;
	struct in6_multi_mship *imm;
	struct proc *p = curproc;	/* XXX */
	int ip6_defmcasthlim_local = atomic_load_int(&ip6_defmcasthlim);

	if (im6o == NULL) {
		/*
		 * No multicast option buffer attached to the pcb;
		 * allocate one and initialize to default values.
		 */
		im6o = malloc(sizeof(*im6o), M_IPMOPTS, M_WAITOK);
		if (im6o == NULL)
			return (ENOBUFS);
		*im6op = im6o;
		im6o->im6o_ifidx = 0;
		im6o->im6o_hlim = ip6_defmcasthlim_local;
		im6o->im6o_loop = IPV6_DEFAULT_MULTICAST_LOOP;
		LIST_INIT(&im6o->im6o_memberships);
	}

	switch (optname) {

	case IPV6_MULTICAST_IF:
		/*
		 * Select the interface for outgoing multicast packets.
		 */
		if (m == NULL || m->m_len != sizeof(u_int)) {
			error = EINVAL;
			break;
		}
		memcpy(&ifindex, mtod(m, u_int *), sizeof(ifindex));
		if (ifindex != 0) {
			ifp = if_get(ifindex);
			if (ifp == NULL) {
				error = ENXIO;	/* XXX EINVAL? */
				break;
			}
			if (ifp->if_rdomain != rtable_l2(rtableid) ||
			    (ifp->if_flags & IFF_MULTICAST) == 0) {
				error = EADDRNOTAVAIL;
				if_put(ifp);
				break;
			}
			if_put(ifp);
		}
		im6o->im6o_ifidx = ifindex;
		break;

	case IPV6_MULTICAST_HOPS:
	    {
		/*
		 * Set the IP6 hoplimit for outgoing multicast packets.
		 */
		int optval;
		if (m == NULL || m->m_len != sizeof(int)) {
			error = EINVAL;
			break;
		}
		memcpy(&optval, mtod(m, u_int *), sizeof(optval));
		if (optval < -1 || optval >= 256)
			error = EINVAL;
		else if (optval == -1)
			im6o->im6o_hlim = ip6_defmcasthlim_local;
		else
			im6o->im6o_hlim = optval;
		break;
	    }

	case IPV6_MULTICAST_LOOP:
		/*
		 * Set the loopback flag for outgoing multicast packets.
		 * Must be zero or one.
		 */
		if (m == NULL || m->m_len != sizeof(u_int)) {
			error = EINVAL;
			break;
		}
		memcpy(&loop, mtod(m, u_int *), sizeof(loop));
		if (loop > 1) {
			error = EINVAL;
			break;
		}
		im6o->im6o_loop = loop;
		break;

	case IPV6_JOIN_GROUP:
		/*
		 * Add a multicast group membership.
		 * Group must be a valid IP6 multicast address.
		 */
		if (m == NULL || m->m_len != sizeof(struct ipv6_mreq)) {
			error = EINVAL;
			break;
		}
		mreq = mtod(m, struct ipv6_mreq *);
		if (IN6_IS_ADDR_UNSPECIFIED(&mreq->ipv6mr_multiaddr)) {
			/*
			 * We use the unspecified address to specify to accept
			 * all multicast addresses. Only super user is allowed
			 * to do this.
			 */
			if (suser(p))
			{
				error = EACCES;
				break;
			}
		} else if (!IN6_IS_ADDR_MULTICAST(&mreq->ipv6mr_multiaddr)) {
			error = EINVAL;
			break;
		}

		/*
		 * If no interface was explicitly specified, choose an
		 * appropriate one according to the given multicast address.
		 */
		if (mreq->ipv6mr_interface == 0) {
			struct rtentry *rt;
			struct sockaddr_in6 dst;

			memset(&dst, 0, sizeof(dst));
			dst.sin6_len = sizeof(dst);
			dst.sin6_family = AF_INET6;
			dst.sin6_addr = mreq->ipv6mr_multiaddr;
			rt = rtalloc(sin6tosa(&dst), RT_RESOLVE, rtableid);
			if (rt == NULL) {
				error = EADDRNOTAVAIL;
				break;
			}
			ifp = if_get(rt->rt_ifidx);
			rtfree(rt);
		} else {
			/*
			 * If the interface is specified, validate it.
			 */
			ifp = if_get(mreq->ipv6mr_interface);
			if (ifp == NULL) {
				error = ENXIO;	/* XXX EINVAL? */
				break;
			}
		}

		/*
		 * See if we found an interface, and confirm that it
		 * supports multicast
		 */
		if (ifp == NULL || ifp->if_rdomain != rtable_l2(rtableid) ||
		    (ifp->if_flags & IFF_MULTICAST) == 0) {
			if_put(ifp);
			error = EADDRNOTAVAIL;
			break;
		}
		/*
		 * Put interface index into the multicast address,
		 * if the address has link/interface-local scope.
		 */
		if (IN6_IS_SCOPE_EMBED(&mreq->ipv6mr_multiaddr)) {
			mreq->ipv6mr_multiaddr.s6_addr16[1] =
			    htons(ifp->if_index);
		}
		/*
		 * See if the membership already exists.
		 */
		LIST_FOREACH(imm, &im6o->im6o_memberships, i6mm_chain)
			if (imm->i6mm_maddr->in6m_ifidx == ifp->if_index &&
			    IN6_ARE_ADDR_EQUAL(&imm->i6mm_maddr->in6m_addr,
			    &mreq->ipv6mr_multiaddr))
				break;
		if (imm != NULL) {
			if_put(ifp);
			error = EADDRINUSE;
			break;
		}
		/*
		 * Everything looks good; add a new record to the multicast
		 * address list for the given interface.
		 */
		imm = in6_joingroup(ifp, &mreq->ipv6mr_multiaddr, &error);
		if_put(ifp);
		if (!imm)
			break;
		LIST_INSERT_HEAD(&im6o->im6o_memberships, imm, i6mm_chain);
		break;

	case IPV6_LEAVE_GROUP:
		/*
		 * Drop a multicast group membership.
		 * Group must be a valid IP6 multicast address.
		 */
		if (m == NULL || m->m_len != sizeof(struct ipv6_mreq)) {
			error = EINVAL;
			break;
		}
		mreq = mtod(m, struct ipv6_mreq *);
		if (IN6_IS_ADDR_UNSPECIFIED(&mreq->ipv6mr_multiaddr)) {
			if (suser(p)) {
				error = EACCES;
				break;
			}
		} else if (!IN6_IS_ADDR_MULTICAST(&mreq->ipv6mr_multiaddr)) {
			error = EINVAL;
			break;
		}

		/*
		 * Put interface index into the multicast address,
		 * if the address has link-local scope.
		 */
		if (IN6_IS_ADDR_MC_LINKLOCAL(&mreq->ipv6mr_multiaddr)) {
			mreq->ipv6mr_multiaddr.s6_addr16[1] =
			    htons(mreq->ipv6mr_interface);
		}

		/*
		 * If an interface address was specified, get a pointer
		 * to its ifnet structure.
		 */
		if (mreq->ipv6mr_interface == 0)
			ifp = NULL;
		else {
			ifp = if_get(mreq->ipv6mr_interface);
			if (ifp == NULL) {
				error = ENXIO;	/* XXX EINVAL? */
				break;
			}
		}

		/*
		 * Find the membership in the membership list.
		 */
		LIST_FOREACH(imm, &im6o->im6o_memberships, i6mm_chain) {
			if ((ifp == NULL ||
			    imm->i6mm_maddr->in6m_ifidx == ifp->if_index) &&
			    IN6_ARE_ADDR_EQUAL(&imm->i6mm_maddr->in6m_addr,
			    &mreq->ipv6mr_multiaddr))
				break;
		}

		if_put(ifp);

		if (imm == NULL) {
			/* Unable to resolve interface */
			error = EADDRNOTAVAIL;
			break;
		}
		/*
		 * Give up the multicast address record to which the
		 * membership points.
		 */
		LIST_REMOVE(imm, i6mm_chain);
		in6_leavegroup(imm);
		break;

	default:
		error = EOPNOTSUPP;
		break;
	}

	/*
	 * If all options have default values, no need to keep the option
	 * structure.
	 */
	if (im6o->im6o_ifidx == 0 &&
	    im6o->im6o_hlim == ip6_defmcasthlim_local &&
	    im6o->im6o_loop == IPV6_DEFAULT_MULTICAST_LOOP &&
	    LIST_EMPTY(&im6o->im6o_memberships)) {
		free(*im6op, M_IPMOPTS, sizeof(**im6op));
		*im6op = NULL;
	}

	return (error);
}

/*
 * Return the IP6 multicast options in response to user getsockopt().
 */
int
ip6_getmoptions(int optname, struct ip6_moptions *im6o, struct mbuf *m)
{
	u_int *hlim, *loop, *ifindex;

	switch (optname) {
	case IPV6_MULTICAST_IF:
		ifindex = mtod(m, u_int *);
		m->m_len = sizeof(u_int);
		if (im6o == NULL || im6o->im6o_ifidx == 0)
			*ifindex = 0;
		else
			*ifindex = im6o->im6o_ifidx;
		return (0);

	case IPV6_MULTICAST_HOPS:
		hlim = mtod(m, u_int *);
		m->m_len = sizeof(u_int);
		if (im6o == NULL)
			*hlim = atomic_load_int(&ip6_defmcasthlim);
		else
			*hlim = im6o->im6o_hlim;
		return (0);

	case IPV6_MULTICAST_LOOP:
		loop = mtod(m, u_int *);
		m->m_len = sizeof(u_int);
		if (im6o == NULL)
			*loop = atomic_load_int(&ip6_defmcasthlim);
		else
			*loop = im6o->im6o_loop;
		return (0);

	default:
		return (EOPNOTSUPP);
	}
}

/*
 * Discard the IP6 multicast options.
 */
void
ip6_freemoptions(struct ip6_moptions *im6o)
{
	struct in6_multi_mship *imm;

	if (im6o == NULL)
		return;

	while (!LIST_EMPTY(&im6o->im6o_memberships)) {
		imm = LIST_FIRST(&im6o->im6o_memberships);
		LIST_REMOVE(imm, i6mm_chain);
		in6_leavegroup(imm);
	}
	free(im6o, M_IPMOPTS, sizeof(*im6o));
}

/*
 * Set IPv6 outgoing packet options based on advanced API.
 */
int
ip6_setpktopts(struct mbuf *control, struct ip6_pktopts *opt,
    struct ip6_pktopts *stickyopt, int priv, int uproto)
{
	u_int clen;
	struct cmsghdr *cm = 0;
	caddr_t cmsgs;
	int error;

	if (control == NULL || opt == NULL)
		return (EINVAL);

	ip6_initpktopts(opt);
	if (stickyopt) {
		int error;

		/*
		 * If stickyopt is provided, make a local copy of the options
		 * for this particular packet, then override them by ancillary
		 * objects.
		 * XXX: copypktopts() does not copy the cached route to a next
		 * hop (if any).  This is not very good in terms of efficiency,
		 * but we can allow this since this option should be rarely
		 * used.
		 */
		if ((error = copypktopts(opt, stickyopt)) != 0)
			return (error);
	}

	/*
	 * XXX: Currently, we assume all the optional information is stored
	 * in a single mbuf.
	 */
	if (control->m_next)
		return (EINVAL);

	clen = control->m_len;
	cmsgs = mtod(control, caddr_t);
	do {
		if (clen < CMSG_LEN(0))
			return (EINVAL);
		cm = (struct cmsghdr *)cmsgs;
		if (cm->cmsg_len < CMSG_LEN(0) || cm->cmsg_len > clen ||
		    CMSG_ALIGN(cm->cmsg_len) > clen)
			return (EINVAL);
		if (cm->cmsg_level == IPPROTO_IPV6) {
			error = ip6_setpktopt(cm->cmsg_type, CMSG_DATA(cm),
			    cm->cmsg_len - CMSG_LEN(0), opt, priv, 0, uproto);
			if (error)
				return (error);
		}

		clen -= CMSG_ALIGN(cm->cmsg_len);
		cmsgs += CMSG_ALIGN(cm->cmsg_len);
	} while (clen);

	return (0);
}

/*
 * Set a particular packet option, as a sticky option or an ancillary data
 * item.  "len" can be 0 only when it's a sticky option.
 */
int
ip6_setpktopt(int optname, u_char *buf, int len, struct ip6_pktopts *opt,
    int priv, int sticky, int uproto)
{
	int minmtupolicy;

	switch (optname) {
	case IPV6_PKTINFO:
	{
		struct ifnet *ifp = NULL;
		struct in6_pktinfo *pktinfo;

		if (len != sizeof(struct in6_pktinfo))
			return (EINVAL);

		pktinfo = (struct in6_pktinfo *)buf;

		/*
		 * An application can clear any sticky IPV6_PKTINFO option by
		 * doing a "regular" setsockopt with ipi6_addr being
		 * in6addr_any and ipi6_ifindex being zero.
		 * [RFC 3542, Section 6]
		 */
		if (opt->ip6po_pktinfo &&
		    pktinfo->ipi6_ifindex == 0 &&
		    IN6_IS_ADDR_UNSPECIFIED(&pktinfo->ipi6_addr)) {
			ip6_clearpktopts(opt, optname);
			break;
		}

		if (uproto == IPPROTO_TCP &&
		    sticky && !IN6_IS_ADDR_UNSPECIFIED(&pktinfo->ipi6_addr)) {
			return (EINVAL);
		}

		if (pktinfo->ipi6_ifindex) {
			ifp = if_get(pktinfo->ipi6_ifindex);
			if (ifp == NULL)
				return (ENXIO);
			if_put(ifp);
		}

		/*
		 * We store the address anyway, and let in6_selectsrc()
		 * validate the specified address.  This is because ipi6_addr
		 * may not have enough information about its scope zone, and
		 * we may need additional information (such as outgoing
		 * interface or the scope zone of a destination address) to
		 * disambiguate the scope.
		 * XXX: the delay of the validation may confuse the
		 * application when it is used as a sticky option.
		 */
		if (opt->ip6po_pktinfo == NULL) {
			opt->ip6po_pktinfo = malloc(sizeof(*pktinfo),
			    M_IP6OPT, M_NOWAIT);
			if (opt->ip6po_pktinfo == NULL)
				return (ENOBUFS);
		}
		bcopy(pktinfo, opt->ip6po_pktinfo, sizeof(*pktinfo));
		break;
	}

	case IPV6_HOPLIMIT:
	{
		int *hlimp;

		/*
		 * RFC 3542 deprecated the usage of sticky IPV6_HOPLIMIT
		 * to simplify the ordering among hoplimit options.
		 */
		if (sticky)
			return (ENOPROTOOPT);

		if (len != sizeof(int))
			return (EINVAL);
		hlimp = (int *)buf;
		if (*hlimp < -1 || *hlimp > 255)
			return (EINVAL);

		opt->ip6po_hlim = *hlimp;
		break;
	}

	case IPV6_TCLASS:
	{
		int tclass;

		if (len != sizeof(int))
			return (EINVAL);
		tclass = *(int *)buf;
		if (tclass < -1 || tclass > 255)
			return (EINVAL);

		opt->ip6po_tclass = tclass;
		break;
	}
	case IPV6_HOPOPTS:
	{
		struct ip6_hbh *hbh;
		int hbhlen;

		/*
		 * XXX: We don't allow a non-privileged user to set ANY HbH
		 * options, since per-option restriction has too much
		 * overhead.
		 */
		if (!priv)
			return (EPERM);

		if (len == 0) {
			ip6_clearpktopts(opt, IPV6_HOPOPTS);
			break;	/* just remove the option */
		}

		/* message length validation */
		if (len < sizeof(struct ip6_hbh))
			return (EINVAL);
		hbh = (struct ip6_hbh *)buf;
		hbhlen = (hbh->ip6h_len + 1) << 3;
		if (len != hbhlen)
			return (EINVAL);

		/* turn off the previous option, then set the new option. */
		ip6_clearpktopts(opt, IPV6_HOPOPTS);
		opt->ip6po_hbh = malloc(hbhlen, M_IP6OPT, M_NOWAIT);
		if (opt->ip6po_hbh == NULL)
			return (ENOBUFS);
		memcpy(opt->ip6po_hbh, hbh, hbhlen);

		break;
	}

	case IPV6_DSTOPTS:
	case IPV6_RTHDRDSTOPTS:
	{
		struct ip6_dest *dest, **newdest = NULL;
		int destlen;

		if (!priv)	/* XXX: see the comment for IPV6_HOPOPTS */
			return (EPERM);

		if (len == 0) {
			ip6_clearpktopts(opt, optname);
			break;	/* just remove the option */
		}

		/* message length validation */
		if (len < sizeof(struct ip6_dest))
			return (EINVAL);
		dest = (struct ip6_dest *)buf;
		destlen = (dest->ip6d_len + 1) << 3;
		if (len != destlen)
			return (EINVAL);
		/*
		 * Determine the position that the destination options header
		 * should be inserted; before or after the routing header.
		 */
		switch (optname) {
		case IPV6_RTHDRDSTOPTS:
			newdest = &opt->ip6po_dest1;
			break;
		case IPV6_DSTOPTS:
			newdest = &opt->ip6po_dest2;
			break;
		}

		/* turn off the previous option, then set the new option. */
		ip6_clearpktopts(opt, optname);
		*newdest = malloc(destlen, M_IP6OPT, M_NOWAIT);
		if (*newdest == NULL)
			return (ENOBUFS);
		memcpy(*newdest, dest, destlen);

		break;
	}

	case IPV6_RTHDR:
	{
		struct ip6_rthdr *rth;
		int rthlen;

		if (len == 0) {
			ip6_clearpktopts(opt, IPV6_RTHDR);
			break;	/* just remove the option */
		}

		/* message length validation */
		if (len < sizeof(struct ip6_rthdr))
			return (EINVAL);
		rth = (struct ip6_rthdr *)buf;
		rthlen = (rth->ip6r_len + 1) << 3;
		if (len != rthlen)
			return (EINVAL);

		switch (rth->ip6r_type) {
		case IPV6_RTHDR_TYPE_0:
			if (rth->ip6r_len == 0)	/* must contain one addr */
				return (EINVAL);
			if (rth->ip6r_len % 2) /* length must be even */
				return (EINVAL);
			if (rth->ip6r_len / 2 != rth->ip6r_segleft)
				return (EINVAL);
			break;
		default:
			return (EINVAL);	/* not supported */
		}
		/* turn off the previous option */
		ip6_clearpktopts(opt, IPV6_RTHDR);
		opt->ip6po_rthdr = malloc(rthlen, M_IP6OPT, M_NOWAIT);
		if (opt->ip6po_rthdr == NULL)
			return (ENOBUFS);
		memcpy(opt->ip6po_rthdr, rth, rthlen);
		break;
	}

	case IPV6_USE_MIN_MTU:
		if (len != sizeof(int))
			return (EINVAL);
		minmtupolicy = *(int *)buf;
		if (minmtupolicy != IP6PO_MINMTU_MCASTONLY &&
		    minmtupolicy != IP6PO_MINMTU_DISABLE &&
		    minmtupolicy != IP6PO_MINMTU_ALL) {
			return (EINVAL);
		}
		opt->ip6po_minmtu = minmtupolicy;
		break;

	case IPV6_DONTFRAG:
		if (len != sizeof(int))
			return (EINVAL);

		if (uproto == IPPROTO_TCP || *(int *)buf == 0) {
			/*
			 * we ignore this option for TCP sockets.
			 * (RFC3542 leaves this case unspecified.)
			 */
			opt->ip6po_flags &= ~IP6PO_DONTFRAG;
		} else
			opt->ip6po_flags |= IP6PO_DONTFRAG;
		break;

	default:
		return (ENOPROTOOPT);
	} /* end of switch */

	return (0);
}

/*
 * Routine called from ip6_output() to loop back a copy of an IP6 multicast
 * packet to the input queue of a specified interface.
 */
void
ip6_mloopback(struct ifnet *ifp, struct mbuf *m, struct sockaddr_in6 *dst)
{
	struct mbuf *copym;
	struct ip6_hdr *ip6;

	/*
	 * Duplicate the packet.
	 */
	copym = m_copym(m, 0, M_COPYALL, M_NOWAIT);
	if (copym == NULL)
		return;

	/*
	 * Make sure to deep-copy IPv6 header portion in case the data
	 * is in an mbuf cluster, so that we can safely override the IPv6
	 * header portion later.
	 */
	if ((copym->m_flags & M_EXT) != 0 ||
	    copym->m_len < sizeof(struct ip6_hdr)) {
		copym = m_pullup(copym, sizeof(struct ip6_hdr));
		if (copym == NULL)
			return;
	}

#ifdef DIAGNOSTIC
	if (copym->m_len < sizeof(*ip6)) {
		m_freem(copym);
		return;
	}
#endif

	ip6 = mtod(copym, struct ip6_hdr *);
	if (IN6_IS_SCOPE_EMBED(&ip6->ip6_src))
		ip6->ip6_src.s6_addr16[1] = 0;
	if (IN6_IS_SCOPE_EMBED(&ip6->ip6_dst))
		ip6->ip6_dst.s6_addr16[1] = 0;

	if_input_local(ifp, copym, dst->sin6_family, NULL);
}

/*
 * Chop IPv6 header off from the payload.
 */
int
ip6_splithdr(struct mbuf *m, struct ip6_exthdrs *exthdrs)
{
	struct mbuf *mh;
	struct ip6_hdr *ip6;

	ip6 = mtod(m, struct ip6_hdr *);
	if (m->m_len > sizeof(*ip6)) {
		MGET(mh, M_DONTWAIT, MT_HEADER);
		if (mh == NULL) {
			m_freem(m);
			return ENOBUFS;
		}
		M_MOVE_PKTHDR(mh, m);
		m_align(mh, sizeof(*ip6));
		m->m_len -= sizeof(*ip6);
		m->m_data += sizeof(*ip6);
		mh->m_next = m;
		m = mh;
		m->m_len = sizeof(*ip6);
		bcopy((caddr_t)ip6, mtod(m, caddr_t), sizeof(*ip6));
	}
	exthdrs->ip6e_ip6 = m;
	return 0;
}

u_int32_t
ip6_randomid(void)
{
	return idgen32(&ip6_id_ctx);
}

void
ip6_randomid_init(void)
{
	idgen32_init(&ip6_id_ctx);
}

/*
 *	Compute significant parts of the IPv6 checksum pseudo-header
 *	for use in a delayed TCP/UDP checksum calculation.
 */
static __inline u_int16_t __attribute__((__unused__))
in6_cksum_phdr(const struct in6_addr *src, const struct in6_addr *dst,
    u_int32_t len, u_int32_t nxt)
{
	u_int32_t sum = 0;
	const u_int16_t *w;

	w = (const u_int16_t *) src;
	sum += w[0];
	if (!IN6_IS_SCOPE_EMBED(src))
		sum += w[1];
	sum += w[2]; sum += w[3]; sum += w[4]; sum += w[5];
	sum += w[6]; sum += w[7];

	w = (const u_int16_t *) dst;
	sum += w[0];
	if (!IN6_IS_SCOPE_EMBED(dst))
		sum += w[1];
	sum += w[2]; sum += w[3]; sum += w[4]; sum += w[5];
	sum += w[6]; sum += w[7];

	sum += (u_int16_t)(len >> 16) + (u_int16_t)(len /*& 0xffff*/);

	sum += (u_int16_t)(nxt >> 16) + (u_int16_t)(nxt /*& 0xffff*/);

	sum = (u_int16_t)(sum >> 16) + (u_int16_t)(sum /*& 0xffff*/);

	if (sum > 0xffff)
		sum -= 0xffff;

	return (sum);
}

/*
 * Process a delayed payload checksum calculation.
 */
void
in6_delayed_cksum(struct mbuf *m, u_int8_t nxt)
{
	int nxtp, offset;
	u_int16_t csum;

	offset = ip6_lasthdr(m, 0, IPPROTO_IPV6, &nxtp);
	if (offset <= 0 || nxtp != nxt)
		/* If the desired next protocol isn't found, punt. */
		return;
	csum = (u_int16_t)(in6_cksum(m, 0, offset, m->m_pkthdr.len - offset));

	switch (nxt) {
	case IPPROTO_TCP:
		offset += offsetof(struct tcphdr, th_sum);
		break;

	case IPPROTO_UDP:
		offset += offsetof(struct udphdr, uh_sum);
		if (csum == 0)
			csum = 0xffff;
		break;

	case IPPROTO_ICMPV6:
		offset += offsetof(struct icmp6_hdr, icmp6_cksum);
		break;
	}

	if ((offset + sizeof(u_int16_t)) > m->m_len)
		m_copyback(m, offset, sizeof(csum), &csum, M_NOWAIT);
	else
		*(u_int16_t *)(mtod(m, caddr_t) + offset) = csum;
}

void
in6_proto_cksum_out(struct mbuf *m, struct ifnet *ifp)
{
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);

	/* some hw and in6_delayed_cksum need the pseudo header cksum */
	if (m->m_pkthdr.csum_flags &
	    (M_TCP_CSUM_OUT|M_UDP_CSUM_OUT|M_ICMP_CSUM_OUT)) {
		int nxt, offset;
		u_int16_t csum;

		offset = ip6_lasthdr(m, 0, IPPROTO_IPV6, &nxt);
		if (ISSET(m->m_pkthdr.csum_flags, M_TCP_TSO) &&
		    in_ifcap_cksum(m, ifp, IFCAP_TSOv6)) {
			csum = in6_cksum_phdr(&ip6->ip6_src, &ip6->ip6_dst,
			    htonl(0), htonl(nxt));
		} else {
			csum = in6_cksum_phdr(&ip6->ip6_src, &ip6->ip6_dst,
			    htonl(m->m_pkthdr.len - offset), htonl(nxt));
		}
		if (nxt == IPPROTO_TCP)
			offset += offsetof(struct tcphdr, th_sum);
		else if (nxt == IPPROTO_UDP)
			offset += offsetof(struct udphdr, uh_sum);
		else if (nxt == IPPROTO_ICMPV6)
			offset += offsetof(struct icmp6_hdr, icmp6_cksum);
		if ((offset + sizeof(u_int16_t)) > m->m_len)
			m_copyback(m, offset, sizeof(csum), &csum, M_NOWAIT);
		else
			*(u_int16_t *)(mtod(m, caddr_t) + offset) = csum;
	}

	if (m->m_pkthdr.csum_flags & M_TCP_CSUM_OUT) {
		if (!ifp || !(ifp->if_capabilities & IFCAP_CSUM_TCPv6) ||
		    ip6->ip6_nxt != IPPROTO_TCP ||
		    ifp->if_bridgeidx != 0) {
			tcpstat_inc(tcps_outswcsum);
			in6_delayed_cksum(m, IPPROTO_TCP);
			m->m_pkthdr.csum_flags &= ~M_TCP_CSUM_OUT; /* Clear */
		}
	} else if (m->m_pkthdr.csum_flags & M_UDP_CSUM_OUT) {
		if (!ifp || !(ifp->if_capabilities & IFCAP_CSUM_UDPv6) ||
		    ip6->ip6_nxt != IPPROTO_UDP ||
		    ifp->if_bridgeidx != 0) {
			udpstat_inc(udps_outswcsum);
			in6_delayed_cksum(m, IPPROTO_UDP);
			m->m_pkthdr.csum_flags &= ~M_UDP_CSUM_OUT; /* Clear */
		}
	} else if (m->m_pkthdr.csum_flags & M_ICMP_CSUM_OUT) {
		in6_delayed_cksum(m, IPPROTO_ICMPV6);
		m->m_pkthdr.csum_flags &= ~M_ICMP_CSUM_OUT; /* Clear */
	}
}

#ifdef IPSEC
int
ip6_output_ipsec_lookup(struct mbuf *m, const struct ipsec_level *seclevel,
    struct tdb **tdbout)
{
	struct tdb *tdb;
	struct m_tag *mtag;
	struct tdb_ident *tdbi;
	int error;

	/*
	 * Check if there was an outgoing SA bound to the flow
	 * from a transport protocol.
	 */

	/* Do we have any pending SAs to apply ? */
	error = ipsp_spd_lookup(m, AF_INET6, sizeof(struct ip6_hdr),
	    IPSP_DIRECTION_OUT, NULL, seclevel, &tdb, NULL);
	if (error || tdb == NULL) {
		*tdbout = NULL;
		return error;
	}
	/* Loop detection */
	for (mtag = m_tag_first(m); mtag != NULL; mtag = m_tag_next(m, mtag)) {
		if (mtag->m_tag_id != PACKET_TAG_IPSEC_OUT_DONE)
			continue;
		tdbi = (struct tdb_ident *)(mtag + 1);
		if (tdbi->spi == tdb->tdb_spi &&
		    tdbi->proto == tdb->tdb_sproto &&
		    tdbi->rdomain == tdb->tdb_rdomain &&
		    !memcmp(&tdbi->dst, &tdb->tdb_dst,
		    sizeof(union sockaddr_union))) {
			/* no IPsec needed */
			tdb_unref(tdb);
			*tdbout = NULL;
			return 0;
		}
	}
	*tdbout = tdb;
	return 0;
}

int
ip6_output_ipsec_pmtu_update(struct tdb *tdb, struct route *ro,
    struct in6_addr *dst, int ifidx, int rtableid, int transportmode)
{
	struct rtentry *rt = NULL;
	int rt_mtucloned = 0;

	/* Find a host route to store the mtu in */
	if (ro != NULL)
		rt = ro->ro_rt;
	/* but don't add a PMTU route for transport mode SAs */
	if (transportmode)
		rt = NULL;
	else if (rt == NULL || (rt->rt_flags & RTF_HOST) == 0) {
		struct sockaddr_in6 sin6;
		int error;

		memset(&sin6, 0, sizeof(sin6));
		sin6.sin6_family = AF_INET6;
		sin6.sin6_len = sizeof(sin6);
		sin6.sin6_addr = *dst;
		sin6.sin6_scope_id = in6_addr2scopeid(ifidx, dst);
		error = in6_embedscope(dst, &sin6, NULL, NULL);
		if (error) {
			/* should be impossible */
			return error;
		}
		rt = icmp6_mtudisc_clone(&sin6, rtableid, 1);
		rt_mtucloned = 1;
	}
	DPRINTF("spi %08x mtu %d rt %p cloned %d",
	    ntohl(tdb->tdb_spi), tdb->tdb_mtu, rt, rt_mtucloned);
	if (rt != NULL) {
		atomic_store_int(&rt->rt_mtu, tdb->tdb_mtu);
		if (ro != NULL && ro->ro_rt != NULL) {
			rtfree(ro->ro_rt);
			ro->ro_tableid = rtableid;
			ro->ro_rt = rtalloc(&ro->ro_dstsa, RT_RESOLVE,
			    rtableid);
		}
		if (rt_mtucloned)
			rtfree(rt);
	}
	return 0;
}

int
ip6_output_ipsec_send(struct tdb *tdb, struct mbuf *m, struct route *ro,
    u_int rtableid, int tunalready, int fwd)
{
	struct mbuf_list ml;
	struct ifnet *encif = NULL;
	struct ip6_hdr *ip6;
	struct in6_addr dst;
	u_int len;
	int ifidx, tso = 0, ip_mtudisc_local =  atomic_load_int(&ip_mtudisc);
	int error;

#if NPF > 0
	/*
	 * Packet filter
	 */
	if ((encif = enc_getif(tdb->tdb_rdomain, tdb->tdb_tap)) == NULL ||
	    pf_test(AF_INET6, fwd ? PF_FWD : PF_OUT, encif, &m) != PF_PASS) {
		m_freem(m);
		return EACCES;
	}
	if (m == NULL)
		return 0;
	/*
	 * PF_TAG_REROUTE handling or not...
	 * Packet is entering IPsec so the routing is
	 * already overruled by the IPsec policy.
	 * Until now the change was not reconsidered.
	 * What's the behaviour?
	 */
#endif

	/* Check if we can chop the TCP packet */
	ip6 = mtod(m, struct ip6_hdr *);
	if (ISSET(m->m_pkthdr.csum_flags, M_TCP_TSO) &&
	    m->m_pkthdr.ph_mss <= tdb->tdb_mtu) {
		tso = 1;
		len = m->m_pkthdr.ph_mss;
	} else
		len = sizeof(struct ip6_hdr) + ntohs(ip6->ip6_plen);

	/* Check if we are allowed to fragment */
	dst = ip6->ip6_dst;
	ifidx = m->m_pkthdr.ph_ifidx;
	if (ip_mtudisc_local && tdb->tdb_mtu &&
	    len > tdb->tdb_mtu && tdb->tdb_mtutimeout > gettime()) {
		int transportmode;

		transportmode = (tdb->tdb_dst.sa.sa_family == AF_INET6) &&
		    (IN6_ARE_ADDR_EQUAL(&tdb->tdb_dst.sin6.sin6_addr, &dst));
		error = ip6_output_ipsec_pmtu_update(tdb, ro, &dst, ifidx,
		    rtableid, transportmode);
		if (error) {
			ipsecstat_inc(ipsec_odrops);
			tdbstat_inc(tdb, tdb_odrops);
			m_freem(m);
			return error;
		}
		ipsec_adjust_mtu(m, tdb->tdb_mtu);
		m_freem(m);
		return EMSGSIZE;
	}
	/* propagate don't fragment for v6-over-v6 */
	if (ip_mtudisc_local)
		SET(m->m_pkthdr.csum_flags, M_IPV6_DF_OUT);

	/*
	 * Clear these -- they'll be set in the recursive invocation
	 * as needed.
	 */
	m->m_flags &= ~(M_BCAST | M_MCAST);

	if (tso) {
		error = tcp_softtso_chop(&ml, m, encif, len);
		if (error)
			goto done;
	} else {
		CLR(m->m_pkthdr.csum_flags, M_TCP_TSO);
		in6_proto_cksum_out(m, encif);
		ml_init(&ml);
		ml_enqueue(&ml, m);
	}

	KERNEL_LOCK();
	while ((m = ml_dequeue(&ml)) != NULL) {
		/* Callee frees mbuf */
		error = ipsp_process_packet(m, tdb, AF_INET6, tunalready,
		    IPSP_DF_INHERIT);
		if (error)
			break;
	}
	KERNEL_UNLOCK();
 done:
	if (error) {
		ml_purge(&ml);
		ipsecstat_inc(ipsec_odrops);
		tdbstat_inc(tdb, tdb_odrops);
	}
	if (!error && tso)
		tcpstat_inc(tcps_outswtso);
	if (ip_mtudisc_local && error == EMSGSIZE)
		ip6_output_ipsec_pmtu_update(tdb, ro, &dst, ifidx, rtableid, 0);
	return error;
}
#endif /* IPSEC */
