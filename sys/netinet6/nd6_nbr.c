/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 *
 *	$KAME: nd6_nbr.c,v 1.86 2002/01/21 02:33:04 jinmei Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"
#include "opt_mpath.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/libkern.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>
#include <sys/queue.h>
#include <sys/callout.h>
#include <sys/refcount.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/route.h>
#ifdef RADIX_MPATH
#include <net/radix_mpath.h>
#endif
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <net/if_llatbl.h>
#include <netinet6/in6_var.h>
#include <netinet6/in6_ifattach.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/scope6_var.h>
#include <netinet6/nd6.h>
#include <netinet/icmp6.h>
#include <netinet/ip_carp.h>
#include <netinet6/send.h>

#define SDL(s) ((struct sockaddr_dl *)s)

struct dadq;
static struct dadq *nd6_dad_find(struct ifaddr *, struct nd_opt_nonce *);
static void nd6_dad_add(struct dadq *dp);
static void nd6_dad_del(struct dadq *dp);
static void nd6_dad_rele(struct dadq *);
static void nd6_dad_starttimer(struct dadq *, int, int);
static void nd6_dad_stoptimer(struct dadq *);
static void nd6_dad_timer(struct dadq *);
static void nd6_dad_duplicated(struct ifaddr *, struct dadq *);
static void nd6_dad_ns_output(struct dadq *);
static void nd6_dad_ns_input(struct ifaddr *, struct nd_opt_nonce *);
static void nd6_dad_na_input(struct ifaddr *);
static void nd6_na_output_fib(struct ifnet *, const struct in6_addr *,
    const struct in6_addr *, u_long, int, struct sockaddr *, u_int);
static void nd6_ns_output_fib(struct ifnet *, const struct in6_addr *,
    const struct in6_addr *, const struct in6_addr *, uint8_t *, u_int);

VNET_DEFINE_STATIC(int, dad_enhanced) = 1;
#define	V_dad_enhanced			VNET(dad_enhanced)

SYSCTL_DECL(_net_inet6_ip6);
SYSCTL_INT(_net_inet6_ip6, OID_AUTO, dad_enhanced, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(dad_enhanced), 0,
    "Enable Enhanced DAD, which adds a random nonce to NS messages for DAD.");

VNET_DEFINE_STATIC(int, dad_maxtry) = 15;	/* max # of *tries* to
						   transmit DAD packet */
#define	V_dad_maxtry			VNET(dad_maxtry)

/*
 * Input a Neighbor Solicitation Message.
 *
 * Based on RFC 2461
 * Based on RFC 2462 (duplicate address detection)
 */
void
nd6_ns_input(struct mbuf *m, int off, int icmp6len)
{
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct nd_neighbor_solicit *nd_ns;
	struct in6_addr saddr6 = ip6->ip6_src;
	struct in6_addr daddr6 = ip6->ip6_dst;
	struct in6_addr taddr6;
	struct in6_addr myaddr6;
	char *lladdr = NULL;
	struct ifaddr *ifa = NULL;
	int lladdrlen = 0;
	int anycast = 0, proxy = 0, tentative = 0;
	int tlladdr;
	int rflag;
	union nd_opts ndopts;
	struct sockaddr_dl proxydl;
	char ip6bufs[INET6_ADDRSTRLEN], ip6bufd[INET6_ADDRSTRLEN];

	/* RFC 6980: Nodes MUST silently ignore fragments */
	if(m->m_flags & M_FRAGMENTED)
		goto freeit;

	rflag = (V_ip6_forwarding) ? ND_NA_FLAG_ROUTER : 0;
	if (ND_IFINFO(ifp)->flags & ND6_IFF_ACCEPT_RTADV && V_ip6_norbit_raif)
		rflag = 0;
#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, icmp6len,);
	nd_ns = (struct nd_neighbor_solicit *)((caddr_t)ip6 + off);
#else
	IP6_EXTHDR_GET(nd_ns, struct nd_neighbor_solicit *, m, off, icmp6len);
	if (nd_ns == NULL) {
		ICMP6STAT_INC(icp6s_tooshort);
		return;
	}
#endif
	ip6 = mtod(m, struct ip6_hdr *); /* adjust pointer for safety */
	taddr6 = nd_ns->nd_ns_target;
	if (in6_setscope(&taddr6, ifp, NULL) != 0)
		goto bad;

	if (ip6->ip6_hlim != 255) {
		nd6log((LOG_ERR,
		    "nd6_ns_input: invalid hlim (%d) from %s to %s on %s\n",
		    ip6->ip6_hlim, ip6_sprintf(ip6bufs, &ip6->ip6_src),
		    ip6_sprintf(ip6bufd, &ip6->ip6_dst), if_name(ifp)));
		goto bad;
	}

	if (IN6_IS_ADDR_UNSPECIFIED(&saddr6)) {
		/* dst has to be a solicited node multicast address. */
		if (daddr6.s6_addr16[0] == IPV6_ADDR_INT16_MLL &&
		    /* don't check ifindex portion */
		    daddr6.s6_addr32[1] == 0 &&
		    daddr6.s6_addr32[2] == IPV6_ADDR_INT32_ONE &&
		    daddr6.s6_addr8[12] == 0xff) {
			; /* good */
		} else {
			nd6log((LOG_INFO, "nd6_ns_input: bad DAD packet "
			    "(wrong ip6 dst)\n"));
			goto bad;
		}
	} else if (!V_nd6_onlink_ns_rfc4861) {
		struct sockaddr_in6 src_sa6;

		/*
		 * According to recent IETF discussions, it is not a good idea
		 * to accept a NS from an address which would not be deemed
		 * to be a neighbor otherwise.  This point is expected to be
		 * clarified in future revisions of the specification.
		 */
		bzero(&src_sa6, sizeof(src_sa6));
		src_sa6.sin6_family = AF_INET6;
		src_sa6.sin6_len = sizeof(src_sa6);
		src_sa6.sin6_addr = saddr6;
		if (nd6_is_addr_neighbor(&src_sa6, ifp) == 0) {
			nd6log((LOG_INFO, "nd6_ns_input: "
				"NS packet from non-neighbor\n"));
			goto bad;
		}
	}

	if (IN6_IS_ADDR_MULTICAST(&taddr6)) {
		nd6log((LOG_INFO, "nd6_ns_input: bad NS target (multicast)\n"));
		goto bad;
	}

	icmp6len -= sizeof(*nd_ns);
	nd6_option_init(nd_ns + 1, icmp6len, &ndopts);
	if (nd6_options(&ndopts) < 0) {
		nd6log((LOG_INFO,
		    "nd6_ns_input: invalid ND option, ignored\n"));
		/* nd6_options have incremented stats */
		goto freeit;
	}

	if (ndopts.nd_opts_src_lladdr) {
		lladdr = (char *)(ndopts.nd_opts_src_lladdr + 1);
		lladdrlen = ndopts.nd_opts_src_lladdr->nd_opt_len << 3;
	}

	if (IN6_IS_ADDR_UNSPECIFIED(&ip6->ip6_src) && lladdr) {
		nd6log((LOG_INFO, "nd6_ns_input: bad DAD packet "
		    "(link-layer address option)\n"));
		goto bad;
	}

	/*
	 * Attaching target link-layer address to the NA?
	 * (RFC 2461 7.2.4)
	 *
	 * NS IP dst is unicast/anycast			MUST NOT add
	 * NS IP dst is solicited-node multicast	MUST add
	 *
	 * In implementation, we add target link-layer address by default.
	 * We do not add one in MUST NOT cases.
	 */
	if (!IN6_IS_ADDR_MULTICAST(&daddr6))
		tlladdr = 0;
	else
		tlladdr = 1;

	/*
	 * Target address (taddr6) must be either:
	 * (1) Valid unicast/anycast address for my receiving interface,
	 * (2) Unicast address for which I'm offering proxy service, or
	 * (3) "tentative" address on which DAD is being performed.
	 */
	/* (1) and (3) check. */
	if (ifp->if_carp)
		ifa = (*carp_iamatch6_p)(ifp, &taddr6);
	else
		ifa = (struct ifaddr *)in6ifa_ifpwithaddr(ifp, &taddr6);

	/* (2) check. */
	if (ifa == NULL) {
		struct sockaddr_dl rt_gateway;
		struct rt_addrinfo info;
		struct sockaddr_in6 dst6;

		bzero(&dst6, sizeof(dst6));
		dst6.sin6_len = sizeof(struct sockaddr_in6);
		dst6.sin6_family = AF_INET6;
		dst6.sin6_addr = taddr6;

		bzero(&rt_gateway, sizeof(rt_gateway));
		rt_gateway.sdl_len = sizeof(rt_gateway);
		bzero(&info, sizeof(info));
		info.rti_info[RTAX_GATEWAY] = (struct sockaddr *)&rt_gateway;

		if (rib_lookup_info(ifp->if_fib, (struct sockaddr *)&dst6,
		    0, 0, &info) == 0) {
			if ((info.rti_flags & RTF_ANNOUNCE) != 0 &&
			    rt_gateway.sdl_family == AF_LINK) {

				/*
				 * proxy NDP for single entry
				 */
				proxydl = *SDL(&rt_gateway);
				ifa = (struct ifaddr *)in6ifa_ifpforlinklocal(
				    ifp, IN6_IFF_NOTREADY|IN6_IFF_ANYCAST);
				if (ifa)
					proxy = 1;
			}
		}
	}
	if (ifa == NULL) {
		/*
		 * We've got an NS packet, and we don't have that adddress
		 * assigned for us.  We MUST silently ignore it.
		 * See RFC2461 7.2.3.
		 */
		goto freeit;
	}
	myaddr6 = *IFA_IN6(ifa);
	anycast = ((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_ANYCAST;
	tentative = ((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_TENTATIVE;
	if (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_DUPLICATED)
		goto freeit;

	if (lladdr && ((ifp->if_addrlen + 2 + 7) & ~7) != lladdrlen) {
		nd6log((LOG_INFO, "nd6_ns_input: lladdrlen mismatch for %s "
		    "(if %d, NS packet %d)\n",
		    ip6_sprintf(ip6bufs, &taddr6),
		    ifp->if_addrlen, lladdrlen - 2));
		goto bad;
	}

	if (IN6_ARE_ADDR_EQUAL(&myaddr6, &saddr6)) {
		nd6log((LOG_INFO, "nd6_ns_input: duplicate IP6 address %s\n",
		    ip6_sprintf(ip6bufs, &saddr6)));
		goto freeit;
	}

	/*
	 * We have neighbor solicitation packet, with target address equals to
	 * one of my tentative address.
	 *
	 * src addr	how to process?
	 * ---		---
	 * multicast	of course, invalid (rejected in ip6_input)
	 * unicast	somebody is doing address resolution -> ignore
	 * unspec	dup address detection
	 *
	 * The processing is defined in RFC 2462.
	 */
	if (tentative) {
		/*
		 * If source address is unspecified address, it is for
		 * duplicate address detection.
		 *
		 * If not, the packet is for addess resolution;
		 * silently ignore it.
		 */
		if (IN6_IS_ADDR_UNSPECIFIED(&saddr6))
			nd6_dad_ns_input(ifa, ndopts.nd_opts_nonce);

		goto freeit;
	}

	/*
	 * If the source address is unspecified address, entries must not
	 * be created or updated.
	 * It looks that sender is performing DAD.  Output NA toward
	 * all-node multicast address, to tell the sender that I'm using
	 * the address.
	 * S bit ("solicited") must be zero.
	 */
	if (IN6_IS_ADDR_UNSPECIFIED(&saddr6)) {
		struct in6_addr in6_all;

		in6_all = in6addr_linklocal_allnodes;
		if (in6_setscope(&in6_all, ifp, NULL) != 0)
			goto bad;
		nd6_na_output_fib(ifp, &in6_all, &taddr6,
		    ((anycast || proxy || !tlladdr) ? 0 : ND_NA_FLAG_OVERRIDE) |
		    rflag, tlladdr, proxy ? (struct sockaddr *)&proxydl : NULL,
		    M_GETFIB(m));
		goto freeit;
	}

	nd6_cache_lladdr(ifp, &saddr6, lladdr, lladdrlen,
	    ND_NEIGHBOR_SOLICIT, 0);

	nd6_na_output_fib(ifp, &saddr6, &taddr6,
	    ((anycast || proxy || !tlladdr) ? 0 : ND_NA_FLAG_OVERRIDE) |
	    rflag | ND_NA_FLAG_SOLICITED, tlladdr,
	    proxy ? (struct sockaddr *)&proxydl : NULL, M_GETFIB(m));
 freeit:
	if (ifa != NULL)
		ifa_free(ifa);
	m_freem(m);
	return;

 bad:
	nd6log((LOG_ERR, "nd6_ns_input: src=%s\n",
		ip6_sprintf(ip6bufs, &saddr6)));
	nd6log((LOG_ERR, "nd6_ns_input: dst=%s\n",
		ip6_sprintf(ip6bufs, &daddr6)));
	nd6log((LOG_ERR, "nd6_ns_input: tgt=%s\n",
		ip6_sprintf(ip6bufs, &taddr6)));
	ICMP6STAT_INC(icp6s_badns);
	if (ifa != NULL)
		ifa_free(ifa);
	m_freem(m);
}

/*
 * Output a Neighbor Solicitation Message. Caller specifies:
 *	- ICMP6 header source IP6 address
 *	- ND6 header target IP6 address
 *	- ND6 header source datalink address
 *
 * Based on RFC 2461
 * Based on RFC 2462 (duplicate address detection)
 *
 *    ln - for source address determination
 * nonce - If non-NULL, NS is used for duplicate address detection and
 *         the value (length is ND_OPT_NONCE_LEN) is used as a random nonce.
 */
static void
nd6_ns_output_fib(struct ifnet *ifp, const struct in6_addr *saddr6,
    const struct in6_addr *daddr6, const struct in6_addr *taddr6,
    uint8_t *nonce, u_int fibnum)
{
	struct mbuf *m;
	struct m_tag *mtag;
	struct ip6_hdr *ip6;
	struct nd_neighbor_solicit *nd_ns;
	struct ip6_moptions im6o;
	int icmp6len;
	int maxlen;
	caddr_t mac;

	if (IN6_IS_ADDR_MULTICAST(taddr6))
		return;

	/* estimate the size of message */
	maxlen = sizeof(*ip6) + sizeof(*nd_ns);
	maxlen += (sizeof(struct nd_opt_hdr) + ifp->if_addrlen + 7) & ~7;
	KASSERT(max_linkhdr + maxlen <= MCLBYTES, (
	    "%s: max_linkhdr + maxlen > MCLBYTES (%d + %d > %d)",
	    __func__, max_linkhdr, maxlen, MCLBYTES));

	if (max_linkhdr + maxlen > MHLEN)
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	else
		m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return;
	M_SETFIB(m, fibnum);

	if (daddr6 == NULL || IN6_IS_ADDR_MULTICAST(daddr6)) {
		m->m_flags |= M_MCAST;
		im6o.im6o_multicast_ifp = ifp;
		im6o.im6o_multicast_hlim = 255;
		im6o.im6o_multicast_loop = 0;
	}

	icmp6len = sizeof(*nd_ns);
	m->m_pkthdr.len = m->m_len = sizeof(*ip6) + icmp6len;
	m->m_data += max_linkhdr;	/* or M_ALIGN() equivalent? */

	/* fill neighbor solicitation packet */
	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	/* ip6->ip6_plen will be set later */
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	ip6->ip6_hlim = 255;
	if (daddr6)
		ip6->ip6_dst = *daddr6;
	else {
		ip6->ip6_dst.s6_addr16[0] = IPV6_ADDR_INT16_MLL;
		ip6->ip6_dst.s6_addr16[1] = 0;
		ip6->ip6_dst.s6_addr32[1] = 0;
		ip6->ip6_dst.s6_addr32[2] = IPV6_ADDR_INT32_ONE;
		ip6->ip6_dst.s6_addr32[3] = taddr6->s6_addr32[3];
		ip6->ip6_dst.s6_addr8[12] = 0xff;
		if (in6_setscope(&ip6->ip6_dst, ifp, NULL) != 0)
			goto bad;
	}
	if (nonce == NULL) {
		struct ifaddr *ifa = NULL;

		/*
		 * RFC2461 7.2.2:
		 * "If the source address of the packet prompting the
		 * solicitation is the same as one of the addresses assigned
		 * to the outgoing interface, that address SHOULD be placed
		 * in the IP Source Address of the outgoing solicitation.
		 * Otherwise, any one of the addresses assigned to the
		 * interface should be used."
		 *
		 * We use the source address for the prompting packet
		 * (saddr6), if saddr6 belongs to the outgoing interface.
		 * Otherwise, we perform the source address selection as usual.
		 */

		if (saddr6 != NULL)
			ifa = (struct ifaddr *)in6ifa_ifpwithaddr(ifp, saddr6);
		if (ifa != NULL) {
			/* ip6_src set already. */
			ip6->ip6_src = *saddr6;
			ifa_free(ifa);
		} else {
			int error;
			struct in6_addr dst6, src6;
			uint32_t scopeid;

			in6_splitscope(&ip6->ip6_dst, &dst6, &scopeid);
			error = in6_selectsrc_addr(fibnum, &dst6,
			    scopeid, ifp, &src6, NULL);
			if (error) {
				char ip6buf[INET6_ADDRSTRLEN];
				nd6log((LOG_DEBUG, "%s: source can't be "
				    "determined: dst=%s, error=%d\n", __func__,
				    ip6_sprintf(ip6buf, &dst6),
				    error));
				goto bad;
			}
			ip6->ip6_src = src6;
		}
	} else {
		/*
		 * Source address for DAD packet must always be IPv6
		 * unspecified address. (0::0)
		 * We actually don't have to 0-clear the address (we did it
		 * above), but we do so here explicitly to make the intention
		 * clearer.
		 */
		bzero(&ip6->ip6_src, sizeof(ip6->ip6_src));
	}
	nd_ns = (struct nd_neighbor_solicit *)(ip6 + 1);
	nd_ns->nd_ns_type = ND_NEIGHBOR_SOLICIT;
	nd_ns->nd_ns_code = 0;
	nd_ns->nd_ns_reserved = 0;
	nd_ns->nd_ns_target = *taddr6;
	in6_clearscope(&nd_ns->nd_ns_target); /* XXX */

	/*
	 * Add source link-layer address option.
	 *
	 *				spec		implementation
	 *				---		---
	 * DAD packet			MUST NOT	do not add the option
	 * there's no link layer address:
	 *				impossible	do not add the option
	 * there's link layer address:
	 *	Multicast NS		MUST add one	add the option
	 *	Unicast NS		SHOULD add one	add the option
	 */
	if (nonce == NULL && (mac = nd6_ifptomac(ifp))) {
		int optlen = sizeof(struct nd_opt_hdr) + ifp->if_addrlen;
		struct nd_opt_hdr *nd_opt = (struct nd_opt_hdr *)(nd_ns + 1);
		/* 8 byte alignments... */
		optlen = (optlen + 7) & ~7;

		m->m_pkthdr.len += optlen;
		m->m_len += optlen;
		icmp6len += optlen;
		bzero((caddr_t)nd_opt, optlen);
		nd_opt->nd_opt_type = ND_OPT_SOURCE_LINKADDR;
		nd_opt->nd_opt_len = optlen >> 3;
		bcopy(mac, (caddr_t)(nd_opt + 1), ifp->if_addrlen);
	}
	/*
	 * Add a Nonce option (RFC 3971) to detect looped back NS messages.
	 * This behavior is documented as Enhanced Duplicate Address
	 * Detection in RFC 7527.
	 * net.inet6.ip6.dad_enhanced=0 disables this.
	 */
	if (V_dad_enhanced != 0 && nonce != NULL) {
		int optlen = sizeof(struct nd_opt_hdr) + ND_OPT_NONCE_LEN;
		struct nd_opt_hdr *nd_opt = (struct nd_opt_hdr *)(nd_ns + 1);
		/* 8-byte alignment is required. */
		optlen = (optlen + 7) & ~7;

		m->m_pkthdr.len += optlen;
		m->m_len += optlen;
		icmp6len += optlen;
		bzero((caddr_t)nd_opt, optlen);
		nd_opt->nd_opt_type = ND_OPT_NONCE;
		nd_opt->nd_opt_len = optlen >> 3;
		bcopy(nonce, (caddr_t)(nd_opt + 1), ND_OPT_NONCE_LEN);
	}
	ip6->ip6_plen = htons((u_short)icmp6len);
	nd_ns->nd_ns_cksum = 0;
	nd_ns->nd_ns_cksum =
	    in6_cksum(m, IPPROTO_ICMPV6, sizeof(*ip6), icmp6len);

	if (send_sendso_input_hook != NULL) {
		mtag = m_tag_get(PACKET_TAG_ND_OUTGOING,
			sizeof(unsigned short), M_NOWAIT);
		if (mtag == NULL)
			goto bad;
		*(unsigned short *)(mtag + 1) = nd_ns->nd_ns_type;
		m_tag_prepend(m, mtag);
	}

	ip6_output(m, NULL, NULL, (nonce != NULL) ? IPV6_UNSPECSRC : 0,
	    &im6o, NULL, NULL);
	icmp6_ifstat_inc(ifp, ifs6_out_msg);
	icmp6_ifstat_inc(ifp, ifs6_out_neighborsolicit);
	ICMP6STAT_INC(icp6s_outhist[ND_NEIGHBOR_SOLICIT]);

	return;

  bad:
	m_freem(m);
}

#ifndef BURN_BRIDGES
void
nd6_ns_output(struct ifnet *ifp, const struct in6_addr *saddr6,
    const struct in6_addr *daddr6, const struct in6_addr *taddr6,uint8_t *nonce)
{

	nd6_ns_output_fib(ifp, saddr6, daddr6, taddr6, nonce, RT_DEFAULT_FIB);
}
#endif
/*
 * Neighbor advertisement input handling.
 *
 * Based on RFC 2461
 * Based on RFC 2462 (duplicate address detection)
 *
 * the following items are not implemented yet:
 * - proxy advertisement delay rule (RFC2461 7.2.8, last paragraph, SHOULD)
 * - anycast advertisement delay rule (RFC2461 7.2.7, SHOULD)
 */
void
nd6_na_input(struct mbuf *m, int off, int icmp6len)
{
	struct epoch_tracker et;
	struct ifnet *ifp = m->m_pkthdr.rcvif;
	struct ip6_hdr *ip6 = mtod(m, struct ip6_hdr *);
	struct nd_neighbor_advert *nd_na;
	struct in6_addr daddr6 = ip6->ip6_dst;
	struct in6_addr taddr6;
	int flags;
	int is_router;
	int is_solicited;
	int is_override;
	char *lladdr = NULL;
	int lladdrlen = 0;
	int checklink = 0;
	struct ifaddr *ifa;
	struct llentry *ln = NULL;
	union nd_opts ndopts;
	struct mbuf *chain = NULL;
	struct sockaddr_in6 sin6;
	u_char linkhdr[LLE_MAX_LINKHDR];
	size_t linkhdrsize;
	int lladdr_off;
	char ip6bufs[INET6_ADDRSTRLEN], ip6bufd[INET6_ADDRSTRLEN];

	/* RFC 6980: Nodes MUST silently ignore fragments */
	if(m->m_flags & M_FRAGMENTED)
		goto freeit;

	if (ip6->ip6_hlim != 255) {
		nd6log((LOG_ERR,
		    "nd6_na_input: invalid hlim (%d) from %s to %s on %s\n",
		    ip6->ip6_hlim, ip6_sprintf(ip6bufs, &ip6->ip6_src),
		    ip6_sprintf(ip6bufd, &ip6->ip6_dst), if_name(ifp)));
		goto bad;
	}

#ifndef PULLDOWN_TEST
	IP6_EXTHDR_CHECK(m, off, icmp6len,);
	nd_na = (struct nd_neighbor_advert *)((caddr_t)ip6 + off);
#else
	IP6_EXTHDR_GET(nd_na, struct nd_neighbor_advert *, m, off, icmp6len);
	if (nd_na == NULL) {
		ICMP6STAT_INC(icp6s_tooshort);
		return;
	}
#endif

	flags = nd_na->nd_na_flags_reserved;
	is_router = ((flags & ND_NA_FLAG_ROUTER) != 0);
	is_solicited = ((flags & ND_NA_FLAG_SOLICITED) != 0);
	is_override = ((flags & ND_NA_FLAG_OVERRIDE) != 0);
	memset(&sin6, 0, sizeof(sin6));

	taddr6 = nd_na->nd_na_target;
	if (in6_setscope(&taddr6, ifp, NULL))
		goto bad;	/* XXX: impossible */

	if (IN6_IS_ADDR_MULTICAST(&taddr6)) {
		nd6log((LOG_ERR,
		    "nd6_na_input: invalid target address %s\n",
		    ip6_sprintf(ip6bufs, &taddr6)));
		goto bad;
	}
	if (IN6_IS_ADDR_MULTICAST(&daddr6))
		if (is_solicited) {
			nd6log((LOG_ERR,
			    "nd6_na_input: a solicited adv is multicasted\n"));
			goto bad;
		}

	icmp6len -= sizeof(*nd_na);
	nd6_option_init(nd_na + 1, icmp6len, &ndopts);
	if (nd6_options(&ndopts) < 0) {
		nd6log((LOG_INFO,
		    "nd6_na_input: invalid ND option, ignored\n"));
		/* nd6_options have incremented stats */
		goto freeit;
	}

	if (ndopts.nd_opts_tgt_lladdr) {
		lladdr = (char *)(ndopts.nd_opts_tgt_lladdr + 1);
		lladdrlen = ndopts.nd_opts_tgt_lladdr->nd_opt_len << 3;
	}

	/*
	 * This effectively disables the DAD check on a non-master CARP
	 * address.
	 */
	if (ifp->if_carp)
		ifa = (*carp_iamatch6_p)(ifp, &taddr6);
	else
		ifa = (struct ifaddr *)in6ifa_ifpwithaddr(ifp, &taddr6);

	/*
	 * Target address matches one of my interface address.
	 *
	 * If my address is tentative, this means that there's somebody
	 * already using the same address as mine.  This indicates DAD failure.
	 * This is defined in RFC 2462.
	 *
	 * Otherwise, process as defined in RFC 2461.
	 */
	if (ifa
	 && (((struct in6_ifaddr *)ifa)->ia6_flags & IN6_IFF_TENTATIVE)) {
		nd6_dad_na_input(ifa);
		ifa_free(ifa);
		goto freeit;
	}

	/* Just for safety, maybe unnecessary. */
	if (ifa) {
		ifa_free(ifa);
		log(LOG_ERR,
		    "nd6_na_input: duplicate IP6 address %s\n",
		    ip6_sprintf(ip6bufs, &taddr6));
		goto freeit;
	}

	if (lladdr && ((ifp->if_addrlen + 2 + 7) & ~7) != lladdrlen) {
		nd6log((LOG_INFO, "nd6_na_input: lladdrlen mismatch for %s "
		    "(if %d, NA packet %d)\n", ip6_sprintf(ip6bufs, &taddr6),
		    ifp->if_addrlen, lladdrlen - 2));
		goto bad;
	}

	/*
	 * If no neighbor cache entry is found, NA SHOULD silently be
	 * discarded.
	 */
	NET_EPOCH_ENTER(et);
	ln = nd6_lookup(&taddr6, LLE_EXCLUSIVE, ifp);
	NET_EPOCH_EXIT(et);
	if (ln == NULL) {
		goto freeit;
	}

	if (ln->ln_state == ND6_LLINFO_INCOMPLETE) {
		/*
		 * If the link-layer has address, and no lladdr option came,
		 * discard the packet.
		 */
		if (ifp->if_addrlen && lladdr == NULL) {
			goto freeit;
		}

		/*
		 * Record link-layer address, and update the state.
		 */
		linkhdrsize = sizeof(linkhdr);
		if (lltable_calc_llheader(ifp, AF_INET6, lladdr,
		    linkhdr, &linkhdrsize, &lladdr_off) != 0)
			return;

		if (lltable_try_set_entry_addr(ifp, ln, linkhdr, linkhdrsize,
		    lladdr_off) == 0) {
			ln = NULL;
			goto freeit;
		}
		EVENTHANDLER_INVOKE(lle_event, ln, LLENTRY_RESOLVED);
		if (is_solicited)
			nd6_llinfo_setstate(ln, ND6_LLINFO_REACHABLE);
		else
			nd6_llinfo_setstate(ln, ND6_LLINFO_STALE);
		if ((ln->ln_router = is_router) != 0) {
			/*
			 * This means a router's state has changed from
			 * non-reachable to probably reachable, and might
			 * affect the status of associated prefixes..
			 */
			checklink = 1;
		}
	} else {
		int llchange;

		/*
		 * Check if the link-layer address has changed or not.
		 */
		if (lladdr == NULL)
			llchange = 0;
		else {
			if (ln->la_flags & LLE_VALID) {
				if (bcmp(lladdr, ln->ll_addr, ifp->if_addrlen))
					llchange = 1;
				else
					llchange = 0;
			} else
				llchange = 1;
		}

		/*
		 * This is VERY complex.  Look at it with care.
		 *
		 * override solicit lladdr llchange	action
		 *					(L: record lladdr)
		 *
		 *	0	0	n	--	(2c)
		 *	0	0	y	n	(2b) L
		 *	0	0	y	y	(1)    REACHABLE->STALE
		 *	0	1	n	--	(2c)   *->REACHABLE
		 *	0	1	y	n	(2b) L *->REACHABLE
		 *	0	1	y	y	(1)    REACHABLE->STALE
		 *	1	0	n	--	(2a)
		 *	1	0	y	n	(2a) L
		 *	1	0	y	y	(2a) L *->STALE
		 *	1	1	n	--	(2a)   *->REACHABLE
		 *	1	1	y	n	(2a) L *->REACHABLE
		 *	1	1	y	y	(2a) L *->REACHABLE
		 */
		if (!is_override && (lladdr != NULL && llchange)) {  /* (1) */
			/*
			 * If state is REACHABLE, make it STALE.
			 * no other updates should be done.
			 */
			if (ln->ln_state == ND6_LLINFO_REACHABLE)
				nd6_llinfo_setstate(ln, ND6_LLINFO_STALE);
			goto freeit;
		} else if (is_override				   /* (2a) */
			|| (!is_override && (lladdr != NULL && !llchange)) /* (2b) */
			|| lladdr == NULL) {			   /* (2c) */
			/*
			 * Update link-local address, if any.
			 */
			if (lladdr != NULL) {
				linkhdrsize = sizeof(linkhdr);
				if (lltable_calc_llheader(ifp, AF_INET6, lladdr,
				    linkhdr, &linkhdrsize, &lladdr_off) != 0)
					goto freeit;
				if (lltable_try_set_entry_addr(ifp, ln, linkhdr,
				    linkhdrsize, lladdr_off) == 0) {
					ln = NULL;
					goto freeit;
				}
				EVENTHANDLER_INVOKE(lle_event, ln,
				    LLENTRY_RESOLVED);
			}

			/*
			 * If solicited, make the state REACHABLE.
			 * If not solicited and the link-layer address was
			 * changed, make it STALE.
			 */
			if (is_solicited)
				nd6_llinfo_setstate(ln, ND6_LLINFO_REACHABLE);
			else {
				if (lladdr != NULL && llchange)
					nd6_llinfo_setstate(ln, ND6_LLINFO_STALE);
			}
		}

		if (ln->ln_router && !is_router) {
			/*
			 * The peer dropped the router flag.
			 * Remove the sender from the Default Router List and
			 * update the Destination Cache entries.
			 */
			struct ifnet *nd6_ifp;

			nd6_ifp = lltable_get_ifp(ln->lle_tbl);
			if (!defrouter_remove(&ln->r_l3addr.addr6, nd6_ifp) &&
			    (ND_IFINFO(nd6_ifp)->flags &
			     ND6_IFF_ACCEPT_RTADV) != 0)
				/*
				 * Even if the neighbor is not in the default
				 * router list, the neighbor may be used as a
				 * next hop for some destinations (e.g. redirect
				 * case). So we must call rt6_flush explicitly.
				 */
				rt6_flush(&ip6->ip6_src, ifp);
		}
		ln->ln_router = is_router;
	}
        /* XXX - QL
	 *  Does this matter?
	 *  rt->rt_flags &= ~RTF_REJECT;
	 */
	ln->la_asked = 0;
	if (ln->la_hold != NULL)
		nd6_grab_holdchain(ln, &chain, &sin6);
 freeit:
	if (ln != NULL)
		LLE_WUNLOCK(ln);

	if (chain != NULL)
		nd6_flush_holdchain(ifp, chain, &sin6);

	if (checklink)
		pfxlist_onlink_check();

	m_freem(m);
	return;

 bad:
	if (ln != NULL)
		LLE_WUNLOCK(ln);

	ICMP6STAT_INC(icp6s_badna);
	m_freem(m);
}

/*
 * Neighbor advertisement output handling.
 *
 * Based on RFC 2461
 *
 * the following items are not implemented yet:
 * - proxy advertisement delay rule (RFC2461 7.2.8, last paragraph, SHOULD)
 * - anycast advertisement delay rule (RFC2461 7.2.7, SHOULD)
 *
 * tlladdr - 1 if include target link-layer address
 * sdl0 - sockaddr_dl (= proxy NA) or NULL
 */
static void
nd6_na_output_fib(struct ifnet *ifp, const struct in6_addr *daddr6_0,
    const struct in6_addr *taddr6, u_long flags, int tlladdr,
    struct sockaddr *sdl0, u_int fibnum)
{
	struct mbuf *m;
	struct m_tag *mtag;
	struct ip6_hdr *ip6;
	struct nd_neighbor_advert *nd_na;
	struct ip6_moptions im6o;
	struct in6_addr daddr6, dst6, src6;
	uint32_t scopeid;

	int icmp6len, maxlen, error;
	caddr_t mac = NULL;

	daddr6 = *daddr6_0;	/* make a local copy for modification */

	/* estimate the size of message */
	maxlen = sizeof(*ip6) + sizeof(*nd_na);
	maxlen += (sizeof(struct nd_opt_hdr) + ifp->if_addrlen + 7) & ~7;
	KASSERT(max_linkhdr + maxlen <= MCLBYTES, (
	    "%s: max_linkhdr + maxlen > MCLBYTES (%d + %d > %d)",
	    __func__, max_linkhdr, maxlen, MCLBYTES));

	if (max_linkhdr + maxlen > MHLEN)
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	else
		m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return;
	M_SETFIB(m, fibnum);

	if (IN6_IS_ADDR_MULTICAST(&daddr6)) {
		m->m_flags |= M_MCAST;
		im6o.im6o_multicast_ifp = ifp;
		im6o.im6o_multicast_hlim = 255;
		im6o.im6o_multicast_loop = 0;
	}

	icmp6len = sizeof(*nd_na);
	m->m_pkthdr.len = m->m_len = sizeof(struct ip6_hdr) + icmp6len;
	m->m_data += max_linkhdr;	/* or M_ALIGN() equivalent? */

	/* fill neighbor advertisement packet */
	ip6 = mtod(m, struct ip6_hdr *);
	ip6->ip6_flow = 0;
	ip6->ip6_vfc &= ~IPV6_VERSION_MASK;
	ip6->ip6_vfc |= IPV6_VERSION;
	ip6->ip6_nxt = IPPROTO_ICMPV6;
	ip6->ip6_hlim = 255;
	if (IN6_IS_ADDR_UNSPECIFIED(&daddr6)) {
		/* reply to DAD */
		daddr6.s6_addr16[0] = IPV6_ADDR_INT16_MLL;
		daddr6.s6_addr16[1] = 0;
		daddr6.s6_addr32[1] = 0;
		daddr6.s6_addr32[2] = 0;
		daddr6.s6_addr32[3] = IPV6_ADDR_INT32_ONE;
		if (in6_setscope(&daddr6, ifp, NULL))
			goto bad;

		flags &= ~ND_NA_FLAG_SOLICITED;
	}
	ip6->ip6_dst = daddr6;

	/*
	 * Select a source whose scope is the same as that of the dest.
	 */
	in6_splitscope(&daddr6, &dst6, &scopeid);
	error = in6_selectsrc_addr(fibnum, &dst6,
	    scopeid, ifp, &src6, NULL);
	if (error) {
		char ip6buf[INET6_ADDRSTRLEN];
		nd6log((LOG_DEBUG, "nd6_na_output: source can't be "
		    "determined: dst=%s, error=%d\n",
		    ip6_sprintf(ip6buf, &daddr6), error));
		goto bad;
	}
	ip6->ip6_src = src6;
	nd_na = (struct nd_neighbor_advert *)(ip6 + 1);
	nd_na->nd_na_type = ND_NEIGHBOR_ADVERT;
	nd_na->nd_na_code = 0;
	nd_na->nd_na_target = *taddr6;
	in6_clearscope(&nd_na->nd_na_target); /* XXX */

	/*
	 * "tlladdr" indicates NS's condition for adding tlladdr or not.
	 * see nd6_ns_input() for details.
	 * Basically, if NS packet is sent to unicast/anycast addr,
	 * target lladdr option SHOULD NOT be included.
	 */
	if (tlladdr) {
		/*
		 * sdl0 != NULL indicates proxy NA.  If we do proxy, use
		 * lladdr in sdl0.  If we are not proxying (sending NA for
		 * my address) use lladdr configured for the interface.
		 */
		if (sdl0 == NULL) {
			if (ifp->if_carp)
				mac = (*carp_macmatch6_p)(ifp, m, taddr6);
			if (mac == NULL)
				mac = nd6_ifptomac(ifp);
		} else if (sdl0->sa_family == AF_LINK) {
			struct sockaddr_dl *sdl;
			sdl = (struct sockaddr_dl *)sdl0;
			if (sdl->sdl_alen == ifp->if_addrlen)
				mac = LLADDR(sdl);
		}
	}
	if (tlladdr && mac) {
		int optlen = sizeof(struct nd_opt_hdr) + ifp->if_addrlen;
		struct nd_opt_hdr *nd_opt = (struct nd_opt_hdr *)(nd_na + 1);

		/* roundup to 8 bytes alignment! */
		optlen = (optlen + 7) & ~7;

		m->m_pkthdr.len += optlen;
		m->m_len += optlen;
		icmp6len += optlen;
		bzero((caddr_t)nd_opt, optlen);
		nd_opt->nd_opt_type = ND_OPT_TARGET_LINKADDR;
		nd_opt->nd_opt_len = optlen >> 3;
		bcopy(mac, (caddr_t)(nd_opt + 1), ifp->if_addrlen);
	} else
		flags &= ~ND_NA_FLAG_OVERRIDE;

	ip6->ip6_plen = htons((u_short)icmp6len);
	nd_na->nd_na_flags_reserved = flags;
	nd_na->nd_na_cksum = 0;
	nd_na->nd_na_cksum =
	    in6_cksum(m, IPPROTO_ICMPV6, sizeof(struct ip6_hdr), icmp6len);

	if (send_sendso_input_hook != NULL) {
		mtag = m_tag_get(PACKET_TAG_ND_OUTGOING,
		    sizeof(unsigned short), M_NOWAIT);
		if (mtag == NULL)
			goto bad;
		*(unsigned short *)(mtag + 1) = nd_na->nd_na_type;
		m_tag_prepend(m, mtag);
	}

	ip6_output(m, NULL, NULL, 0, &im6o, NULL, NULL);
	icmp6_ifstat_inc(ifp, ifs6_out_msg);
	icmp6_ifstat_inc(ifp, ifs6_out_neighboradvert);
	ICMP6STAT_INC(icp6s_outhist[ND_NEIGHBOR_ADVERT]);

	return;

  bad:
	m_freem(m);
}

#ifndef BURN_BRIDGES
void
nd6_na_output(struct ifnet *ifp, const struct in6_addr *daddr6_0,
    const struct in6_addr *taddr6, u_long flags, int tlladdr,
    struct sockaddr *sdl0)
{

	nd6_na_output_fib(ifp, daddr6_0, taddr6, flags, tlladdr, sdl0,
	    RT_DEFAULT_FIB);
}
#endif

caddr_t
nd6_ifptomac(struct ifnet *ifp)
{
	switch (ifp->if_type) {
	case IFT_ETHER:
	case IFT_IEEE1394:
	case IFT_L2VLAN:
	case IFT_INFINIBAND:
	case IFT_BRIDGE:
		return IF_LLADDR(ifp);
	default:
		return NULL;
	}
}

struct dadq {
	TAILQ_ENTRY(dadq) dad_list;
	struct ifaddr *dad_ifa;
	int dad_count;		/* max NS to send */
	int dad_ns_tcount;	/* # of trials to send NS */
	int dad_ns_ocount;	/* NS sent so far */
	int dad_ns_icount;
	int dad_na_icount;
	int dad_ns_lcount;	/* looped back NS */
	int dad_loopbackprobe;	/* probing state for loopback detection */
	struct callout dad_timer_ch;
	struct vnet *dad_vnet;
	u_int dad_refcnt;
#define	ND_OPT_NONCE_LEN32 \
		((ND_OPT_NONCE_LEN + sizeof(uint32_t) - 1)/sizeof(uint32_t))
	uint32_t dad_nonce[ND_OPT_NONCE_LEN32];
	bool dad_ondadq;	/* on dadq? Protected by DADQ_WLOCK. */
};

VNET_DEFINE_STATIC(TAILQ_HEAD(, dadq), dadq);
VNET_DEFINE_STATIC(struct rwlock, dad_rwlock);
#define	V_dadq			VNET(dadq)
#define	V_dad_rwlock		VNET(dad_rwlock)

#define	DADQ_RLOCK()		rw_rlock(&V_dad_rwlock)	
#define	DADQ_RUNLOCK()		rw_runlock(&V_dad_rwlock)	
#define	DADQ_WLOCK()		rw_wlock(&V_dad_rwlock)	
#define	DADQ_WUNLOCK()		rw_wunlock(&V_dad_rwlock)	

static void
nd6_dad_add(struct dadq *dp)
{

	DADQ_WLOCK();
	TAILQ_INSERT_TAIL(&V_dadq, dp, dad_list);
	dp->dad_ondadq = true;
	DADQ_WUNLOCK();
}

static void
nd6_dad_del(struct dadq *dp)
{

	DADQ_WLOCK();
	if (dp->dad_ondadq) {
		/*
		 * Remove dp from the dadq and release the dadq's
		 * reference.
		 */
		TAILQ_REMOVE(&V_dadq, dp, dad_list);
		dp->dad_ondadq = false;
		DADQ_WUNLOCK();
		nd6_dad_rele(dp);
	} else
		DADQ_WUNLOCK();
}

static struct dadq *
nd6_dad_find(struct ifaddr *ifa, struct nd_opt_nonce *n)
{
	struct dadq *dp;

	DADQ_RLOCK();
	TAILQ_FOREACH(dp, &V_dadq, dad_list) {
		if (dp->dad_ifa != ifa)
			continue;
		/*
		 * Skip if the nonce matches the received one.
		 * +2 in the length is required because of type and
		 * length fields are included in a header.
		 */
		if (n != NULL &&
		    n->nd_opt_nonce_len == (ND_OPT_NONCE_LEN + 2) / 8 &&
		    memcmp(&n->nd_opt_nonce[0], &dp->dad_nonce[0],
		        ND_OPT_NONCE_LEN) == 0) {
			dp->dad_ns_lcount++;
			continue;
		}
		refcount_acquire(&dp->dad_refcnt);
		break;
	}
	DADQ_RUNLOCK();

	return (dp);
}

static void
nd6_dad_starttimer(struct dadq *dp, int ticks, int send_ns)
{

	if (send_ns != 0)
		nd6_dad_ns_output(dp);
	callout_reset(&dp->dad_timer_ch, ticks,
	    (void (*)(void *))nd6_dad_timer, (void *)dp);
}

static void
nd6_dad_stoptimer(struct dadq *dp)
{

	callout_drain(&dp->dad_timer_ch);
}

static void
nd6_dad_rele(struct dadq *dp)
{

	if (refcount_release(&dp->dad_refcnt)) {
		ifa_free(dp->dad_ifa);
		free(dp, M_IP6NDP);
	}
}

void
nd6_dad_init(void)
{

	rw_init(&V_dad_rwlock, "nd6 DAD queue");
	TAILQ_INIT(&V_dadq);
}

/*
 * Start Duplicate Address Detection (DAD) for specified interface address.
 */
void
nd6_dad_start(struct ifaddr *ifa, int delay)
{
	struct in6_ifaddr *ia = (struct in6_ifaddr *)ifa;
	struct dadq *dp;
	char ip6buf[INET6_ADDRSTRLEN];

	KASSERT((ia->ia6_flags & IN6_IFF_TENTATIVE) != 0,
	    ("starting DAD on non-tentative address %p", ifa));

	/*
	 * If we don't need DAD, don't do it.
	 * There are several cases:
	 * - DAD is disabled globally or on the interface
	 * - the interface address is anycast
	 */
	if ((ia->ia6_flags & IN6_IFF_ANYCAST) != 0 ||
	    V_ip6_dad_count == 0 ||
	    (ND_IFINFO(ifa->ifa_ifp)->flags & ND6_IFF_NO_DAD) != 0) {
		ia->ia6_flags &= ~IN6_IFF_TENTATIVE;
		return;
	}
	if ((ifa->ifa_ifp->if_flags & IFF_UP) == 0 ||
	    (ifa->ifa_ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    (ND_IFINFO(ifa->ifa_ifp)->flags & ND6_IFF_IFDISABLED) != 0)
		return;

	if ((dp = nd6_dad_find(ifa, NULL)) != NULL) {
		/*
		 * DAD is already in progress.  Let the existing entry
		 * finish it.
		 */
		nd6_dad_rele(dp);
		return;
	}

	dp = malloc(sizeof(*dp), M_IP6NDP, M_NOWAIT | M_ZERO);
	if (dp == NULL) {
		log(LOG_ERR, "nd6_dad_start: memory allocation failed for "
			"%s(%s)\n",
			ip6_sprintf(ip6buf, &ia->ia_addr.sin6_addr),
			ifa->ifa_ifp ? if_name(ifa->ifa_ifp) : "???");
		return;
	}
	callout_init(&dp->dad_timer_ch, 0);
#ifdef VIMAGE
	dp->dad_vnet = curvnet;
#endif
	nd6log((LOG_DEBUG, "%s: starting DAD for %s\n", if_name(ifa->ifa_ifp),
	    ip6_sprintf(ip6buf, &ia->ia_addr.sin6_addr)));

	/*
	 * Send NS packet for DAD, ip6_dad_count times.
	 * Note that we must delay the first transmission, if this is the
	 * first packet to be sent from the interface after interface
	 * (re)initialization.
	 */
	dp->dad_ifa = ifa;
	ifa_ref(dp->dad_ifa);
	dp->dad_count = V_ip6_dad_count;
	dp->dad_ns_icount = dp->dad_na_icount = 0;
	dp->dad_ns_ocount = dp->dad_ns_tcount = 0;
	dp->dad_ns_lcount = dp->dad_loopbackprobe = 0;

	/* Add this to the dadq and add a reference for the dadq. */
	refcount_init(&dp->dad_refcnt, 1);
	nd6_dad_add(dp);
	nd6_dad_starttimer(dp, delay, 0);
}

/*
 * terminate DAD unconditionally.  used for address removals.
 */
void
nd6_dad_stop(struct ifaddr *ifa)
{
	struct dadq *dp;

	dp = nd6_dad_find(ifa, NULL);
	if (!dp) {
		/* DAD wasn't started yet */
		return;
	}

	nd6_dad_stoptimer(dp);
	nd6_dad_del(dp);

	/* Release this function's reference, acquired by nd6_dad_find(). */
	nd6_dad_rele(dp);
}

static void
nd6_dad_timer(struct dadq *dp)
{
	CURVNET_SET(dp->dad_vnet);
	struct ifaddr *ifa = dp->dad_ifa;
	struct ifnet *ifp = dp->dad_ifa->ifa_ifp;
	struct in6_ifaddr *ia = (struct in6_ifaddr *)ifa;
	char ip6buf[INET6_ADDRSTRLEN];

	KASSERT(ia != NULL, ("DAD entry %p with no address", dp));

	if (ND_IFINFO(ifp)->flags & ND6_IFF_IFDISABLED) {
		/* Do not need DAD for ifdisabled interface. */
		log(LOG_ERR, "nd6_dad_timer: cancel DAD on %s because of "
		    "ND6_IFF_IFDISABLED.\n", ifp->if_xname);
		goto err;
	}
	if (ia->ia6_flags & IN6_IFF_DUPLICATED) {
		log(LOG_ERR, "nd6_dad_timer: called with duplicated address "
			"%s(%s)\n",
			ip6_sprintf(ip6buf, &ia->ia_addr.sin6_addr),
			ifa->ifa_ifp ? if_name(ifa->ifa_ifp) : "???");
		goto err;
	}
	if ((ia->ia6_flags & IN6_IFF_TENTATIVE) == 0) {
		log(LOG_ERR, "nd6_dad_timer: called with non-tentative address "
			"%s(%s)\n",
			ip6_sprintf(ip6buf, &ia->ia_addr.sin6_addr),
			ifa->ifa_ifp ? if_name(ifa->ifa_ifp) : "???");
		goto err;
	}

	/* Stop DAD if the interface is down even after dad_maxtry attempts. */
	if ((dp->dad_ns_tcount > V_dad_maxtry) &&
	    (((ifp->if_flags & IFF_UP) == 0) ||
	     ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0))) {
		nd6log((LOG_INFO, "%s: could not run DAD "
		    "because the interface was down or not running.\n",
		    if_name(ifa->ifa_ifp)));
		goto err;
	}

	/* Need more checks? */
	if (dp->dad_ns_ocount < dp->dad_count) {
		/*
		 * We have more NS to go.  Send NS packet for DAD.
		 */
		nd6_dad_starttimer(dp,
		    (long)ND_IFINFO(ifa->ifa_ifp)->retrans * hz / 1000, 1);
		goto done;
	} else {
		/*
		 * We have transmitted sufficient number of DAD packets.
		 * See what we've got.
		 */
		if (dp->dad_ns_icount > 0 || dp->dad_na_icount > 0)
			/* We've seen NS or NA, means DAD has failed. */
			nd6_dad_duplicated(ifa, dp);
		else if (V_dad_enhanced != 0 &&
		    dp->dad_ns_lcount > 0 &&
		    dp->dad_ns_lcount > dp->dad_loopbackprobe) {
			/*
			 * Sec. 4.1 in RFC 7527 requires transmission of
			 * additional probes until the loopback condition
			 * becomes clear when a looped back probe is detected.
			 */
			log(LOG_ERR, "%s: a looped back NS message is "
			    "detected during DAD for %s.  "
			    "Another DAD probes are being sent.\n",
			    if_name(ifa->ifa_ifp),
			    ip6_sprintf(ip6buf, IFA_IN6(ifa)));
			dp->dad_loopbackprobe = dp->dad_ns_lcount;
			/*
			 * Send an NS immediately and increase dad_count by
			 * V_nd6_mmaxtries - 1.
			 */
			dp->dad_count =
			    dp->dad_ns_ocount + V_nd6_mmaxtries - 1;
			nd6_dad_starttimer(dp,
			    (long)ND_IFINFO(ifa->ifa_ifp)->retrans * hz / 1000,
			    1);
			goto done;
		} else {
			/*
			 * We are done with DAD.  No NA came, no NS came.
			 * No duplicate address found.  Check IFDISABLED flag
			 * again in case that it is changed between the
			 * beginning of this function and here.
			 */
			if ((ND_IFINFO(ifp)->flags & ND6_IFF_IFDISABLED) == 0)
				ia->ia6_flags &= ~IN6_IFF_TENTATIVE;

			nd6log((LOG_DEBUG,
			    "%s: DAD complete for %s - no duplicates found\n",
			    if_name(ifa->ifa_ifp),
			    ip6_sprintf(ip6buf, &ia->ia_addr.sin6_addr)));
			if (dp->dad_ns_lcount > 0)
				log(LOG_ERR, "%s: DAD completed while "
				    "a looped back NS message is detected "
				    "during DAD for %s.\n",
				    if_name(ifa->ifa_ifp),
				    ip6_sprintf(ip6buf, IFA_IN6(ifa)));
		}
	}
err:
	nd6_dad_del(dp);
done:
	CURVNET_RESTORE();
}

static void
nd6_dad_duplicated(struct ifaddr *ifa, struct dadq *dp)
{
	struct in6_ifaddr *ia = (struct in6_ifaddr *)ifa;
	struct ifnet *ifp;
	char ip6buf[INET6_ADDRSTRLEN];

	log(LOG_ERR, "%s: DAD detected duplicate IPv6 address %s: "
	    "NS in/out/loopback=%d/%d/%d, NA in=%d\n",
	    if_name(ifa->ifa_ifp), ip6_sprintf(ip6buf, &ia->ia_addr.sin6_addr),
	    dp->dad_ns_icount, dp->dad_ns_ocount, dp->dad_ns_lcount,
	    dp->dad_na_icount);

	ia->ia6_flags &= ~IN6_IFF_TENTATIVE;
	ia->ia6_flags |= IN6_IFF_DUPLICATED;

	ifp = ifa->ifa_ifp;
	log(LOG_ERR, "%s: DAD complete for %s - duplicate found\n",
	    if_name(ifp), ip6_sprintf(ip6buf, &ia->ia_addr.sin6_addr));
	log(LOG_ERR, "%s: manual intervention required\n",
	    if_name(ifp));

	/*
	 * If the address is a link-local address formed from an interface
	 * identifier based on the hardware address which is supposed to be
	 * uniquely assigned (e.g., EUI-64 for an Ethernet interface), IP
	 * operation on the interface SHOULD be disabled.
	 * [RFC 4862, Section 5.4.5]
	 */
	if (IN6_IS_ADDR_LINKLOCAL(&ia->ia_addr.sin6_addr)) {
		struct in6_addr in6;

		/*
		 * To avoid over-reaction, we only apply this logic when we are
		 * very sure that hardware addresses are supposed to be unique.
		 */
		switch (ifp->if_type) {
		case IFT_ETHER:
		case IFT_ATM:
		case IFT_IEEE1394:
		case IFT_INFINIBAND:
			in6 = ia->ia_addr.sin6_addr;
			if (in6_get_hw_ifid(ifp, &in6) == 0 &&
			    IN6_ARE_ADDR_EQUAL(&ia->ia_addr.sin6_addr, &in6)) {
				ND_IFINFO(ifp)->flags |= ND6_IFF_IFDISABLED;
				log(LOG_ERR, "%s: possible hardware address "
				    "duplication detected, disable IPv6\n",
				    if_name(ifp));
			}
			break;
		}
	}
}

static void
nd6_dad_ns_output(struct dadq *dp)
{
	struct in6_ifaddr *ia = (struct in6_ifaddr *)dp->dad_ifa;
	struct ifnet *ifp = dp->dad_ifa->ifa_ifp;
	int i;

	dp->dad_ns_tcount++;
	if ((ifp->if_flags & IFF_UP) == 0) {
		return;
	}
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		return;
	}

	dp->dad_ns_ocount++;
	if (V_dad_enhanced != 0) {
		for (i = 0; i < ND_OPT_NONCE_LEN32; i++)
			dp->dad_nonce[i] = arc4random();
		/*
		 * XXXHRS: Note that in the case that
		 * DupAddrDetectTransmits > 1, multiple NS messages with
		 * different nonces can be looped back in an unexpected
		 * order.  The current implementation recognizes only
		 * the latest nonce on the sender side.  Practically it
		 * should work well in almost all cases.
		 */
	}
	nd6_ns_output(ifp, NULL, NULL, &ia->ia_addr.sin6_addr,
	    (uint8_t *)&dp->dad_nonce[0]);
}

static void
nd6_dad_ns_input(struct ifaddr *ifa, struct nd_opt_nonce *ndopt_nonce)
{
	struct dadq *dp;

	if (ifa == NULL)
		panic("ifa == NULL in nd6_dad_ns_input");

	/* Ignore Nonce option when Enhanced DAD is disabled. */
	if (V_dad_enhanced == 0)
		ndopt_nonce = NULL;
	dp = nd6_dad_find(ifa, ndopt_nonce);
	if (dp == NULL)
		return;

	dp->dad_ns_icount++;
	nd6_dad_rele(dp);
}

static void
nd6_dad_na_input(struct ifaddr *ifa)
{
	struct dadq *dp;

	if (ifa == NULL)
		panic("ifa == NULL in nd6_dad_na_input");

	dp = nd6_dad_find(ifa, NULL);
	if (dp != NULL) {
		dp->dad_na_icount++;
		nd6_dad_rele(dp);
	}
}
