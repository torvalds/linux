/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2019 Yandex LLC
 * Copyright (c) 2015-2019 Andrey V. Elsukov <ae@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rmlock.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_pflog.h>
#include <net/pfil.h>
#include <net/netisr.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_fib.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_fw.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet6/in6_var.h>
#include <netinet6/in6_fib.h>
#include <netinet6/ip6_var.h>
#include <netinet6/ip_fw_nat64.h>

#include <netpfil/pf/pf.h>
#include <netpfil/ipfw/ip_fw_private.h>
#include <machine/in_cksum.h>

#include "ip_fw_nat64.h"
#include "nat64_translate.h"


typedef int (*nat64_output_t)(struct ifnet *, struct mbuf *,
    struct sockaddr *, struct nat64_counters *, void *);
typedef int (*nat64_output_one_t)(struct mbuf *, struct nat64_counters *,
    void *);

static int nat64_find_route4(struct nhop4_basic *, struct sockaddr_in *,
    struct mbuf *);
static int nat64_find_route6(struct nhop6_basic *, struct sockaddr_in6 *,
    struct mbuf *);
static int nat64_output_one(struct mbuf *, struct nat64_counters *, void *);
static int nat64_output(struct ifnet *, struct mbuf *, struct sockaddr *,
    struct nat64_counters *, void *);
static int nat64_direct_output_one(struct mbuf *, struct nat64_counters *,
    void *);
static int nat64_direct_output(struct ifnet *, struct mbuf *,
    struct sockaddr *, struct nat64_counters *, void *);

struct nat64_methods {
	nat64_output_t		output;
	nat64_output_one_t	output_one;
};
static const struct nat64_methods nat64_netisr = {
	.output = nat64_output,
	.output_one = nat64_output_one
};
static const struct nat64_methods nat64_direct = {
	.output = nat64_direct_output,
	.output_one = nat64_direct_output_one
};
VNET_DEFINE_STATIC(const struct nat64_methods *, nat64out) = &nat64_netisr;
#define	V_nat64out	VNET(nat64out)

void
nat64_set_output_method(int direct)
{

	V_nat64out = direct != 0 ? &nat64_direct: &nat64_netisr;
}

int
nat64_get_output_method(void)
{

	return (V_nat64out == &nat64_direct ? 1: 0);
}

static void
nat64_log(struct pfloghdr *logdata, struct mbuf *m, sa_family_t family)
{

	logdata->dir = PF_OUT;
	logdata->af = family;
	ipfw_bpf_mtap2(logdata, PFLOG_HDRLEN, m);
}

static int
nat64_direct_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct nat64_counters *stats, void *logdata)
{
	int error;

	if (logdata != NULL)
		nat64_log(logdata, m, dst->sa_family);
	error = (*ifp->if_output)(ifp, m, dst, NULL);
	if (error != 0)
		NAT64STAT_INC(stats, oerrors);
	return (error);
}

static int
nat64_direct_output_one(struct mbuf *m, struct nat64_counters *stats,
    void *logdata)
{
	struct nhop6_basic nh6;
	struct nhop4_basic nh4;
	struct sockaddr_in6 dst6;
	struct sockaddr_in dst4;
	struct sockaddr *dst;
	struct ip6_hdr *ip6;
	struct ip *ip4;
	struct ifnet *ifp;
	int error;

	ip4 = mtod(m, struct ip *);
	switch (ip4->ip_v) {
	case IPVERSION:
		dst4.sin_addr = ip4->ip_dst;
		error = nat64_find_route4(&nh4, &dst4, m);
		if (error != 0)
			NAT64STAT_INC(stats, noroute4);
		else {
			ifp = nh4.nh_ifp;
			dst = (struct sockaddr *)&dst4;
		}
		break;
	case (IPV6_VERSION >> 4):
		ip6 = mtod(m, struct ip6_hdr *);
		dst6.sin6_addr = ip6->ip6_dst;
		error = nat64_find_route6(&nh6, &dst6, m);
		if (error != 0)
			NAT64STAT_INC(stats, noroute6);
		else {
			ifp = nh6.nh_ifp;
			dst = (struct sockaddr *)&dst6;
		}
		break;
	default:
		m_freem(m);
		NAT64STAT_INC(stats, dropped);
		DPRINTF(DP_DROPS, "dropped due to unknown IP version");
		return (EAFNOSUPPORT);
	}
	if (error != 0) {
		m_freem(m);
		return (EHOSTUNREACH);
	}
	if (logdata != NULL)
		nat64_log(logdata, m, dst->sa_family);
	error = (*ifp->if_output)(ifp, m, dst, NULL);
	if (error != 0)
		NAT64STAT_INC(stats, oerrors);
	return (error);
}

static int
nat64_output(struct ifnet *ifp, struct mbuf *m, struct sockaddr *dst,
    struct nat64_counters *stats, void *logdata)
{
	struct ip *ip4;
	int ret, af;

	ip4 = mtod(m, struct ip *);
	switch (ip4->ip_v) {
	case IPVERSION:
		af = AF_INET;
		ret = NETISR_IP;
		break;
	case (IPV6_VERSION >> 4):
		af = AF_INET6;
		ret = NETISR_IPV6;
		break;
	default:
		m_freem(m);
		NAT64STAT_INC(stats, dropped);
		DPRINTF(DP_DROPS, "unknown IP version");
		return (EAFNOSUPPORT);
	}
	if (logdata != NULL)
		nat64_log(logdata, m, af);
	if (m->m_pkthdr.rcvif == NULL)
		m->m_pkthdr.rcvif = V_loif;
	ret = netisr_queue(ret, m);
	if (ret != 0)
		NAT64STAT_INC(stats, oerrors);
	return (ret);
}

static int
nat64_output_one(struct mbuf *m, struct nat64_counters *stats, void *logdata)
{

	return (nat64_output(NULL, m, NULL, stats, logdata));
}

/*
 * Check the given IPv6 prefix and length according to RFC6052:
 *   The prefixes can only have one of the following lengths:
 *   32, 40, 48, 56, 64, or 96 (The Well-Known Prefix is 96 bits long).
 * Returns zero on success, otherwise EINVAL.
 */
int
nat64_check_prefixlen(int length)
{

	switch (length) {
	case 32:
	case 40:
	case 48:
	case 56:
	case 64:
	case 96:
		return (0);
	}
	return (EINVAL);
}

int
nat64_check_prefix6(const struct in6_addr *prefix, int length)
{

	if (nat64_check_prefixlen(length) != 0)
		return (EINVAL);

	/* Well-known prefix has 96 prefix length */
	if (IN6_IS_ADDR_WKPFX(prefix) && length != 96)
		return (EINVAL);

	/* Bits 64 to 71 must be set to zero */
	if (prefix->__u6_addr.__u6_addr8[8] != 0)
		return (EINVAL);

	/* Some extra checks */
	if (IN6_IS_ADDR_MULTICAST(prefix) ||
	    IN6_IS_ADDR_UNSPECIFIED(prefix) ||
	    IN6_IS_ADDR_LOOPBACK(prefix))
		return (EINVAL);
	return (0);
}

int
nat64_check_private_ip4(const struct nat64_config *cfg, in_addr_t ia)
{

	if (cfg->flags & NAT64_ALLOW_PRIVATE)
		return (0);

	/* WKPFX must not be used to represent non-global IPv4 addresses */
	if (cfg->flags & NAT64_WKPFX) {
		/* IN_PRIVATE */
		if ((ia & htonl(0xff000000)) == htonl(0x0a000000) ||
		    (ia & htonl(0xfff00000)) == htonl(0xac100000) ||
		    (ia & htonl(0xffff0000)) == htonl(0xc0a80000))
			return (1);
		/*
		 * RFC 5735:
		 *  192.0.0.0/24 - reserved for IETF protocol assignments
		 *  192.88.99.0/24 - for use as 6to4 relay anycast addresses
		 *  198.18.0.0/15 - for use in benchmark tests
		 *  192.0.2.0/24, 198.51.100.0/24, 203.0.113.0/24 - for use
		 *   in documentation and example code
		 */
		if ((ia & htonl(0xffffff00)) == htonl(0xc0000000) ||
		    (ia & htonl(0xffffff00)) == htonl(0xc0586300) ||
		    (ia & htonl(0xfffffe00)) == htonl(0xc6120000) ||
		    (ia & htonl(0xffffff00)) == htonl(0xc0000200) ||
		    (ia & htonl(0xfffffe00)) == htonl(0xc6336400) ||
		    (ia & htonl(0xffffff00)) == htonl(0xcb007100))
			return (1);
	}
	return (0);
}

/*
 * Embed @ia IPv4 address into @ip6 IPv6 address.
 * Place to embedding determined from prefix length @plen.
 */
void
nat64_embed_ip4(struct in6_addr *ip6, int plen, in_addr_t ia)
{

	switch (plen) {
	case 32:
	case 96:
		ip6->s6_addr32[plen / 32] = ia;
		break;
	case 40:
	case 48:
	case 56:
		/*
		 * Preserve prefix bits.
		 * Since suffix bits should be zero and reserved for future
		 * use, we just overwrite the whole word, where they are.
		 */
		ip6->s6_addr32[1] &= 0xffffffff << (32 - plen % 32);
#if BYTE_ORDER == BIG_ENDIAN
		ip6->s6_addr32[1] |= ia >> (plen % 32);
		ip6->s6_addr32[2] = ia << (24 - plen % 32);
#elif BYTE_ORDER == LITTLE_ENDIAN
		ip6->s6_addr32[1] |= ia << (plen % 32);
		ip6->s6_addr32[2] = ia >> (24 - plen % 32);
#endif
		break;
	case 64:
#if BYTE_ORDER == BIG_ENDIAN
		ip6->s6_addr32[2] = ia >> 8;
		ip6->s6_addr32[3] = ia << 24;
#elif BYTE_ORDER == LITTLE_ENDIAN
		ip6->s6_addr32[2] = ia << 8;
		ip6->s6_addr32[3] = ia >> 24;
#endif
		break;
	default:
		panic("Wrong plen: %d", plen);
	};
	/*
	 * Bits 64 to 71 of the address are reserved for compatibility
	 * with the host identifier format defined in the IPv6 addressing
	 * architecture [RFC4291]. These bits MUST be set to zero.
	 */
	ip6->s6_addr8[8] = 0;
}

in_addr_t
nat64_extract_ip4(const struct in6_addr *ip6, int plen)
{
	in_addr_t ia;

	/*
	 * According to RFC 6052 p2.2:
	 * IPv4-embedded IPv6 addresses are composed of a variable-length
	 * prefix, the embedded IPv4 address, and a variable length suffix.
	 * The suffix bits are reserved for future extensions and SHOULD
	 * be set to zero.
	 */
	switch (plen) {
	case 32:
		if (ip6->s6_addr32[3] != 0 || ip6->s6_addr32[2] != 0)
			goto badip6;
		break;
	case 40:
		if (ip6->s6_addr32[3] != 0 ||
		    (ip6->s6_addr32[2] & htonl(0xff00ffff)) != 0)
			goto badip6;
		break;
	case 48:
		if (ip6->s6_addr32[3] != 0 ||
		    (ip6->s6_addr32[2] & htonl(0xff0000ff)) != 0)
			goto badip6;
		break;
	case 56:
		if (ip6->s6_addr32[3] != 0 || ip6->s6_addr8[8] != 0)
			goto badip6;
		break;
	case 64:
		if (ip6->s6_addr8[8] != 0 ||
		    (ip6->s6_addr32[3] & htonl(0x00ffffff)) != 0)
			goto badip6;
	};
	switch (plen) {
	case 32:
	case 96:
		ia = ip6->s6_addr32[plen / 32];
		break;
	case 40:
	case 48:
	case 56:
#if BYTE_ORDER == BIG_ENDIAN
		ia = (ip6->s6_addr32[1] << (plen % 32)) |
		    (ip6->s6_addr32[2] >> (24 - plen % 32));
#elif BYTE_ORDER == LITTLE_ENDIAN
		ia = (ip6->s6_addr32[1] >> (plen % 32)) |
		    (ip6->s6_addr32[2] << (24 - plen % 32));
#endif
		break;
	case 64:
#if BYTE_ORDER == BIG_ENDIAN
		ia = (ip6->s6_addr32[2] << 8) | (ip6->s6_addr32[3] >> 24);
#elif BYTE_ORDER == LITTLE_ENDIAN
		ia = (ip6->s6_addr32[2] >> 8) | (ip6->s6_addr32[3] << 24);
#endif
		break;
	default:
		return (0);
	};
	if (nat64_check_ip4(ia) == 0)
		return (ia);

	DPRINTF(DP_GENERIC | DP_DROPS,
	    "invalid destination address: %08x", ia);
	return (0);
badip6:
	DPRINTF(DP_GENERIC | DP_DROPS, "invalid IPv4-embedded IPv6 address");
	return (0);
}

/*
 * According to RFC 1624 the equation for incremental checksum update is:
 *	HC' = ~(~HC + ~m + m')	--	[Eqn. 3]
 *	HC' = HC - ~m - m'	--	[Eqn. 4]
 * So, when we are replacing IPv4 addresses to IPv6, we
 * can assume, that new bytes previously were zeros, and vise versa -
 * when we replacing IPv6 addresses to IPv4, now unused bytes become
 * zeros. The payload length in pseudo header has bigger size, but one
 * half of it should be zero. Using the equation 4 we get:
 *	HC' = HC - (~m0 + m0')	-- m0 is first changed word
 *	HC' = (HC - (~m0 + m0')) - (~m1 + m1')	-- m1 is second changed word
 *	HC' = HC - ~m0 - m0' - ~m1 - m1' - ... =
 *	  = HC - sum(~m[i] + m'[i])
 *
 * The function result should be used as follows:
 *	IPv6 to IPv4:	HC' = cksum_add(HC, result)
 *	IPv4 to IPv6:	HC' = cksum_add(HC, ~result)
 */
static uint16_t
nat64_cksum_convert(struct ip6_hdr *ip6, struct ip *ip)
{
	uint32_t sum;
	uint16_t *p;

	sum = ~ip->ip_src.s_addr >> 16;
	sum += ~ip->ip_src.s_addr & 0xffff;
	sum += ~ip->ip_dst.s_addr >> 16;
	sum += ~ip->ip_dst.s_addr & 0xffff;

	for (p = (uint16_t *)&ip6->ip6_src;
	    p < (uint16_t *)(&ip6->ip6_src + 2); p++)
		sum += *p;

	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (sum);
}

static void
nat64_init_ip4hdr(const struct ip6_hdr *ip6, const struct ip6_frag *frag,
    uint16_t plen, uint8_t proto, struct ip *ip)
{

	/* assume addresses are already initialized */
	ip->ip_v = IPVERSION;
	ip->ip_hl = sizeof(*ip) >> 2;
	ip->ip_tos = (ntohl(ip6->ip6_flow) >> 20) & 0xff;
	ip->ip_len = htons(sizeof(*ip) + plen);
	ip->ip_ttl = ip6->ip6_hlim;
	/* Forwarding code will decrement TTL for netisr based output. */
	if (V_nat64out == &nat64_direct)
		ip->ip_ttl -= IPV6_HLIMDEC;
	ip->ip_sum = 0;
	ip->ip_p = (proto == IPPROTO_ICMPV6) ? IPPROTO_ICMP: proto;
	ip_fillid(ip);
	if (frag != NULL) {
		ip->ip_off = htons(ntohs(frag->ip6f_offlg) >> 3);
		if (frag->ip6f_offlg & IP6F_MORE_FRAG)
			ip->ip_off |= htons(IP_MF);
	} else {
		ip->ip_off = htons(IP_DF);
	}
	ip->ip_sum = in_cksum_hdr(ip);
}

#define	FRAGSZ(mtu) ((mtu) - sizeof(struct ip6_hdr) - sizeof(struct ip6_frag))
static NAT64NOINLINE int
nat64_fragment6(struct nat64_counters *stats, struct ip6_hdr *ip6,
    struct mbufq *mq, struct mbuf *m, uint32_t mtu, uint16_t ip_id,
    uint16_t ip_off)
{
	struct ip6_frag ip6f;
	struct mbuf *n;
	uint16_t hlen, len, offset;
	int plen;

	plen = ntohs(ip6->ip6_plen);
	hlen = sizeof(struct ip6_hdr);

	/* Fragmentation isn't needed */
	if (ip_off == 0 && plen <= mtu - hlen) {
		M_PREPEND(m, hlen, M_NOWAIT);
		if (m == NULL) {
			NAT64STAT_INC(stats, nomem);
			return (ENOMEM);
		}
		bcopy(ip6, mtod(m, void *), hlen);
		if (mbufq_enqueue(mq, m) != 0) {
			m_freem(m);
			NAT64STAT_INC(stats, dropped);
			DPRINTF(DP_DROPS, "dropped due to mbufq overflow");
			return (ENOBUFS);
		}
		return (0);
	}

	hlen += sizeof(struct ip6_frag);
	ip6f.ip6f_reserved = 0;
	ip6f.ip6f_nxt = ip6->ip6_nxt;
	ip6->ip6_nxt = IPPROTO_FRAGMENT;
	if (ip_off != 0) {
		/*
		 * We have got an IPv4 fragment.
		 * Use offset value and ip_id from original fragment.
		 */
		ip6f.ip6f_ident = htonl(ntohs(ip_id));
		offset = (ntohs(ip_off) & IP_OFFMASK) << 3;
		NAT64STAT_INC(stats, ifrags);
	} else {
		/* The packet size exceeds interface MTU */
		ip6f.ip6f_ident = htonl(ip6_randomid());
		offset = 0; /* First fragment*/
	}
	while (plen > 0 && m != NULL) {
		n = NULL;
		len = FRAGSZ(mtu) & ~7;
		if (len > plen)
			len = plen;
		ip6->ip6_plen = htons(len + sizeof(ip6f));
		ip6f.ip6f_offlg = ntohs(offset);
		if (len < plen || (ip_off & htons(IP_MF)) != 0)
			ip6f.ip6f_offlg |= IP6F_MORE_FRAG;
		offset += len;
		plen -= len;
		if (plen > 0) {
			n = m_split(m, len, M_NOWAIT);
			if (n == NULL)
				goto fail;
		}
		M_PREPEND(m, hlen, M_NOWAIT);
		if (m == NULL)
			goto fail;
		bcopy(ip6, mtod(m, void *), sizeof(struct ip6_hdr));
		bcopy(&ip6f, mtodo(m, sizeof(struct ip6_hdr)),
		    sizeof(struct ip6_frag));
		if (mbufq_enqueue(mq, m) != 0)
			goto fail;
		m = n;
	}
	NAT64STAT_ADD(stats, ofrags, mbufq_len(mq));
	return (0);
fail:
	if (m != NULL)
		m_freem(m);
	if (n != NULL)
		m_freem(n);
	mbufq_drain(mq);
	NAT64STAT_INC(stats, nomem);
	return (ENOMEM);
}

static NAT64NOINLINE int
nat64_find_route6(struct nhop6_basic *pnh, struct sockaddr_in6 *dst,
    struct mbuf *m)
{

	if (fib6_lookup_nh_basic(M_GETFIB(m), &dst->sin6_addr, 0, 0, 0,
	    pnh) != 0)
		return (EHOSTUNREACH);
	if (pnh->nh_flags & (NHF_BLACKHOLE | NHF_REJECT))
		return (EHOSTUNREACH);
	/*
	 * XXX: we need to use destination address with embedded scope
	 * zone id, because LLTABLE uses such form of addresses for lookup.
	 */
	dst->sin6_family = AF_INET6;
	dst->sin6_len = sizeof(*dst);
	dst->sin6_addr = pnh->nh_addr;
	if (IN6_IS_SCOPE_LINKLOCAL(&dst->sin6_addr))
		dst->sin6_addr.s6_addr16[1] =
		    htons(pnh->nh_ifp->if_index & 0xffff);
	dst->sin6_port = 0;
	dst->sin6_scope_id = 0;
	dst->sin6_flowinfo = 0;

	return (0);
}

#define	NAT64_ICMP6_PLEN	64
static NAT64NOINLINE void
nat64_icmp6_reflect(struct mbuf *m, uint8_t type, uint8_t code, uint32_t mtu,
    struct nat64_counters *stats, void *logdata)
{
	struct icmp6_hdr *icmp6;
	struct ip6_hdr *ip6, *oip6;
	struct mbuf *n;
	int len, plen;

	len = 0;
	plen = nat64_getlasthdr(m, &len);
	if (plen < 0) {
		DPRINTF(DP_DROPS, "mbuf isn't contigious");
		goto freeit;
	}
	/*
	 * Do not send ICMPv6 in reply to ICMPv6 errors.
	 */
	if (plen == IPPROTO_ICMPV6) {
		if (m->m_len < len + sizeof(*icmp6)) {
			DPRINTF(DP_DROPS, "mbuf isn't contigious");
			goto freeit;
		}
		icmp6 = mtodo(m, len);
		if (icmp6->icmp6_type < ICMP6_ECHO_REQUEST ||
		    icmp6->icmp6_type == ND_REDIRECT) {
			DPRINTF(DP_DROPS, "do not send ICMPv6 in reply to "
			    "ICMPv6 errors");
			goto freeit;
		}
	}
	/*
	if (icmp6_ratelimit(&ip6->ip6_src, type, code))
		goto freeit;
		*/
	ip6 = mtod(m, struct ip6_hdr *);
	switch (type) {
	case ICMP6_DST_UNREACH:
	case ICMP6_PACKET_TOO_BIG:
	case ICMP6_TIME_EXCEEDED:
	case ICMP6_PARAM_PROB:
		break;
	default:
		goto freeit;
	}
	/* Calculate length of ICMPv6 payload */
	len = (m->m_pkthdr.len > NAT64_ICMP6_PLEN) ? NAT64_ICMP6_PLEN:
	    m->m_pkthdr.len;

	/* Create new ICMPv6 datagram */
	plen = len + sizeof(struct icmp6_hdr);
	n = m_get2(sizeof(struct ip6_hdr) + plen + max_hdr, M_NOWAIT,
	    MT_HEADER, M_PKTHDR);
	if (n == NULL) {
		NAT64STAT_INC(stats, nomem);
		m_freem(m);
		return;
	}
	/*
	 * Move pkthdr from original mbuf. We should have initialized some
	 * fields, because we can reinject this mbuf to netisr and it will
	 * go trough input path (it requires at least rcvif should be set).
	 * Also do M_ALIGN() to reduce chances of need to allocate new mbuf
	 * in the chain, when we will do M_PREPEND() or make some type of
	 * tunneling.
	 */
	m_move_pkthdr(n, m);
	M_ALIGN(n, sizeof(struct ip6_hdr) + plen + max_hdr);

	n->m_len = n->m_pkthdr.len = sizeof(struct ip6_hdr) + plen;
	oip6 = mtod(n, struct ip6_hdr *);
	oip6->ip6_src = ip6->ip6_dst;
	oip6->ip6_dst = ip6->ip6_src;
	oip6->ip6_nxt = IPPROTO_ICMPV6;
	oip6->ip6_flow = 0;
	oip6->ip6_vfc |= IPV6_VERSION;
	oip6->ip6_hlim = V_ip6_defhlim;
	oip6->ip6_plen = htons(plen);

	icmp6 = mtodo(n, sizeof(struct ip6_hdr));
	icmp6->icmp6_cksum = 0;
	icmp6->icmp6_type = type;
	icmp6->icmp6_code = code;
	icmp6->icmp6_mtu = htonl(mtu);

	m_copydata(m, 0, len, mtodo(n, sizeof(struct ip6_hdr) +
	    sizeof(struct icmp6_hdr)));
	icmp6->icmp6_cksum = in6_cksum(n, IPPROTO_ICMPV6,
	    sizeof(struct ip6_hdr), plen);
	m_freem(m);
	V_nat64out->output_one(n, stats, logdata);
	return;
freeit:
	NAT64STAT_INC(stats, dropped);
	m_freem(m);
}

static NAT64NOINLINE int
nat64_find_route4(struct nhop4_basic *pnh, struct sockaddr_in *dst,
    struct mbuf *m)
{

	if (fib4_lookup_nh_basic(M_GETFIB(m), dst->sin_addr, 0, 0, pnh) != 0)
		return (EHOSTUNREACH);
	if (pnh->nh_flags & (NHF_BLACKHOLE | NHF_BROADCAST | NHF_REJECT))
		return (EHOSTUNREACH);

	dst->sin_family = AF_INET;
	dst->sin_len = sizeof(*dst);
	dst->sin_addr = pnh->nh_addr;
	dst->sin_port = 0;
	return (0);
}

#define	NAT64_ICMP_PLEN	64
static NAT64NOINLINE void
nat64_icmp_reflect(struct mbuf *m, uint8_t type,
    uint8_t code, uint16_t mtu, struct nat64_counters *stats, void *logdata)
{
	struct icmp *icmp;
	struct ip *ip, *oip;
	struct mbuf *n;
	int len, plen;

	ip = mtod(m, struct ip *);
	/* Do not send ICMP error if packet is not the first fragment */
	if (ip->ip_off & ~ntohs(IP_MF|IP_DF)) {
		DPRINTF(DP_DROPS, "not first fragment");
		goto freeit;
	}
	/* Do not send ICMP in reply to ICMP errors */
	if (ip->ip_p == IPPROTO_ICMP) {
		if (m->m_len < (ip->ip_hl << 2)) {
			DPRINTF(DP_DROPS, "mbuf isn't contigious");
			goto freeit;
		}
		icmp = mtodo(m, ip->ip_hl << 2);
		if (!ICMP_INFOTYPE(icmp->icmp_type)) {
			DPRINTF(DP_DROPS, "do not send ICMP in reply to "
			    "ICMP errors");
			goto freeit;
		}
	}
	switch (type) {
	case ICMP_UNREACH:
	case ICMP_TIMXCEED:
	case ICMP_PARAMPROB:
		break;
	default:
		goto freeit;
	}
	/* Calculate length of ICMP payload */
	len = (m->m_pkthdr.len > NAT64_ICMP_PLEN) ? (ip->ip_hl << 2) + 8:
	    m->m_pkthdr.len;

	/* Create new ICMPv4 datagram */
	plen = len + sizeof(struct icmphdr) + sizeof(uint32_t);
	n = m_get2(sizeof(struct ip) + plen + max_hdr, M_NOWAIT,
	    MT_HEADER, M_PKTHDR);
	if (n == NULL) {
		NAT64STAT_INC(stats, nomem);
		m_freem(m);
		return;
	}
	m_move_pkthdr(n, m);
	M_ALIGN(n, sizeof(struct ip) + plen + max_hdr);

	n->m_len = n->m_pkthdr.len = sizeof(struct ip) + plen;
	oip = mtod(n, struct ip *);
	oip->ip_v = IPVERSION;
	oip->ip_hl = sizeof(struct ip) >> 2;
	oip->ip_tos = 0;
	oip->ip_len = htons(n->m_pkthdr.len);
	oip->ip_ttl = V_ip_defttl;
	oip->ip_p = IPPROTO_ICMP;
	ip_fillid(oip);
	oip->ip_off = htons(IP_DF);
	oip->ip_src = ip->ip_dst;
	oip->ip_dst = ip->ip_src;
	oip->ip_sum = 0;
	oip->ip_sum = in_cksum_hdr(oip);

	icmp = mtodo(n, sizeof(struct ip));
	icmp->icmp_type = type;
	icmp->icmp_code = code;
	icmp->icmp_cksum = 0;
	icmp->icmp_pmvoid = 0;
	icmp->icmp_nextmtu = htons(mtu);
	m_copydata(m, 0, len, mtodo(n, sizeof(struct ip) +
	    sizeof(struct icmphdr) + sizeof(uint32_t)));
	icmp->icmp_cksum = in_cksum_skip(n, sizeof(struct ip) + plen,
	    sizeof(struct ip));
	m_freem(m);
	V_nat64out->output_one(n, stats, logdata);
	return;
freeit:
	NAT64STAT_INC(stats, dropped);
	m_freem(m);
}

/* Translate ICMP echo request/reply into ICMPv6 */
static void
nat64_icmp_handle_echo(struct ip6_hdr *ip6, struct icmp6_hdr *icmp6,
    uint16_t id, uint8_t type)
{
	uint16_t old;

	old = *(uint16_t *)icmp6;	/* save type+code in one word */
	icmp6->icmp6_type = type;
	/* Reflect ICMPv6 -> ICMPv4 type translation in the cksum */
	icmp6->icmp6_cksum = cksum_adjust(icmp6->icmp6_cksum,
	    old, *(uint16_t *)icmp6);
	if (id != 0) {
		old = icmp6->icmp6_id;
		icmp6->icmp6_id = id;
		/* Reflect ICMP id translation in the cksum */
		icmp6->icmp6_cksum = cksum_adjust(icmp6->icmp6_cksum,
		    old, id);
	}
	/* Reflect IPv6 pseudo header in the cksum */
	icmp6->icmp6_cksum = ~in6_cksum_pseudo(ip6, ntohs(ip6->ip6_plen),
	    IPPROTO_ICMPV6, ~icmp6->icmp6_cksum);
}

static NAT64NOINLINE struct mbuf *
nat64_icmp_translate(struct mbuf *m, struct ip6_hdr *ip6, uint16_t icmpid,
    int offset, struct nat64_config *cfg)
{
	struct ip ip;
	struct icmp *icmp;
	struct tcphdr *tcp;
	struct udphdr *udp;
	struct ip6_hdr *eip6;
	struct mbuf *n;
	uint32_t mtu;
	int len, hlen, plen;
	uint8_t type, code;

	if (m->m_len < offset + ICMP_MINLEN)
		m = m_pullup(m, offset + ICMP_MINLEN);
	if (m == NULL) {
		NAT64STAT_INC(&cfg->stats, nomem);
		return (m);
	}
	mtu = 0;
	icmp = mtodo(m, offset);
	/* RFC 7915 p4.2 */
	switch (icmp->icmp_type) {
	case ICMP_ECHOREPLY:
		type = ICMP6_ECHO_REPLY;
		code = 0;
		break;
	case ICMP_UNREACH:
		type = ICMP6_DST_UNREACH;
		switch (icmp->icmp_code) {
		case ICMP_UNREACH_NET:
		case ICMP_UNREACH_HOST:
		case ICMP_UNREACH_SRCFAIL:
		case ICMP_UNREACH_NET_UNKNOWN:
		case ICMP_UNREACH_HOST_UNKNOWN:
		case ICMP_UNREACH_TOSNET:
		case ICMP_UNREACH_TOSHOST:
			code = ICMP6_DST_UNREACH_NOROUTE;
			break;
		case ICMP_UNREACH_PROTOCOL:
			type = ICMP6_PARAM_PROB;
			code = ICMP6_PARAMPROB_NEXTHEADER;
			break;
		case ICMP_UNREACH_PORT:
			code = ICMP6_DST_UNREACH_NOPORT;
			break;
		case ICMP_UNREACH_NEEDFRAG:
			type = ICMP6_PACKET_TOO_BIG;
			code = 0;
			/* XXX: needs an additional look */
			mtu = max(IPV6_MMTU, ntohs(icmp->icmp_nextmtu) + 20);
			break;
		case ICMP_UNREACH_NET_PROHIB:
		case ICMP_UNREACH_HOST_PROHIB:
		case ICMP_UNREACH_FILTER_PROHIB:
		case ICMP_UNREACH_PRECEDENCE_CUTOFF:
			code = ICMP6_DST_UNREACH_ADMIN;
			break;
		default:
			DPRINTF(DP_DROPS, "Unsupported ICMP type %d, code %d",
			    icmp->icmp_type, icmp->icmp_code);
			goto freeit;
		}
		break;
	case ICMP_TIMXCEED:
		type = ICMP6_TIME_EXCEEDED;
		code = icmp->icmp_code;
		break;
	case ICMP_ECHO:
		type = ICMP6_ECHO_REQUEST;
		code = 0;
		break;
	case ICMP_PARAMPROB:
		type = ICMP6_PARAM_PROB;
		switch (icmp->icmp_code) {
		case ICMP_PARAMPROB_ERRATPTR:
		case ICMP_PARAMPROB_LENGTH:
			code = ICMP6_PARAMPROB_HEADER;
			switch (icmp->icmp_pptr) {
			case 0: /* Version/IHL */
			case 1: /* Type Of Service */
				mtu = icmp->icmp_pptr;
				break;
			case 2: /* Total Length */
			case 3: mtu = 4; /* Payload Length */
				break;
			case 8: /* Time to Live */
				mtu = 7; /* Hop Limit */
				break;
			case 9: /* Protocol */
				mtu = 6; /* Next Header */
				break;
			case 12: /* Source address */
			case 13:
			case 14:
			case 15:
				mtu = 8;
				break;
			case 16: /* Destination address */
			case 17:
			case 18:
			case 19:
				mtu = 24;
				break;
			default: /* Silently drop */
				DPRINTF(DP_DROPS, "Unsupported ICMP type %d,"
				    " code %d, pptr %d", icmp->icmp_type,
				    icmp->icmp_code, icmp->icmp_pptr);
				goto freeit;
			}
			break;
		default:
			DPRINTF(DP_DROPS, "Unsupported ICMP type %d,"
			    " code %d, pptr %d", icmp->icmp_type,
			    icmp->icmp_code, icmp->icmp_pptr);
			goto freeit;
		}
		break;
	default:
		DPRINTF(DP_DROPS, "Unsupported ICMP type %d, code %d",
		    icmp->icmp_type, icmp->icmp_code);
		goto freeit;
	}
	/*
	 * For echo request/reply we can use original payload,
	 * but we need adjust icmp_cksum, because ICMPv6 cksum covers
	 * IPv6 pseudo header and ICMPv6 types differs from ICMPv4.
	 */
	if (type == ICMP6_ECHO_REQUEST || type == ICMP6_ECHO_REPLY) {
		nat64_icmp_handle_echo(ip6, ICMP6(icmp), icmpid, type);
		return (m);
	}
	/*
	 * For other types of ICMP messages we need to translate inner
	 * IPv4 header to IPv6 header.
	 * Assume ICMP src is the same as payload dst
	 * E.g. we have ( GWsrc1 , NATIP1 ) in outer header
	 * and          ( NATIP1, Hostdst1 ) in ICMP copy header.
	 * In that case, we already have map for NATIP1 and GWsrc1.
	 * The only thing we need is to copy IPv6 map prefix to
	 * Hostdst1.
	 */
	hlen = offset + ICMP_MINLEN;
	if (m->m_pkthdr.len < hlen + sizeof(struct ip) + ICMP_MINLEN) {
		DPRINTF(DP_DROPS, "Message is too short %d",
		    m->m_pkthdr.len);
		goto freeit;
	}
	m_copydata(m, hlen, sizeof(struct ip), (char *)&ip);
	if (ip.ip_v != IPVERSION) {
		DPRINTF(DP_DROPS, "Wrong IP version %d", ip.ip_v);
		goto freeit;
	}
	hlen += ip.ip_hl << 2; /* Skip inner IP header */
	if (nat64_check_ip4(ip.ip_src.s_addr) != 0 ||
	    nat64_check_ip4(ip.ip_dst.s_addr) != 0 ||
	    nat64_check_private_ip4(cfg, ip.ip_src.s_addr) != 0 ||
	    nat64_check_private_ip4(cfg, ip.ip_dst.s_addr) != 0) {
		DPRINTF(DP_DROPS, "IP addresses checks failed %04x -> %04x",
		    ntohl(ip.ip_src.s_addr), ntohl(ip.ip_dst.s_addr));
		goto freeit;
	}
	if (m->m_pkthdr.len < hlen + ICMP_MINLEN) {
		DPRINTF(DP_DROPS, "Message is too short %d",
		    m->m_pkthdr.len);
		goto freeit;
	}
#if 0
	/*
	 * Check that inner source matches the outer destination.
	 * XXX: We need some method to convert IPv4 into IPv6 address here,
	 *	and compare IPv6 addresses.
	 */
	if (ip.ip_src.s_addr != nat64_get_ip4(&ip6->ip6_dst)) {
		DPRINTF(DP_GENERIC, "Inner source doesn't match destination ",
		    "%04x vs %04x", ip.ip_src.s_addr,
		    nat64_get_ip4(&ip6->ip6_dst));
		goto freeit;
	}
#endif
	/*
	 * Create new mbuf for ICMPv6 datagram.
	 * NOTE: len is data length just after inner IP header.
	 */
	len = m->m_pkthdr.len - hlen;
	if (sizeof(struct ip6_hdr) +
	    sizeof(struct icmp6_hdr) + len > NAT64_ICMP6_PLEN)
		len = NAT64_ICMP6_PLEN - sizeof(struct icmp6_hdr) -
		    sizeof(struct ip6_hdr);
	plen = sizeof(struct icmp6_hdr) + sizeof(struct ip6_hdr) + len;
	n = m_get2(offset + plen + max_hdr, M_NOWAIT, MT_HEADER, M_PKTHDR);
	if (n == NULL) {
		NAT64STAT_INC(&cfg->stats, nomem);
		m_freem(m);
		return (NULL);
	}
	m_move_pkthdr(n, m);
	M_ALIGN(n, offset + plen + max_hdr);
	n->m_len = n->m_pkthdr.len = offset + plen;
	/* Adjust ip6_plen in outer header */
	ip6->ip6_plen = htons(plen);
	/* Construct new inner IPv6 header */
	eip6 = mtodo(n, offset + sizeof(struct icmp6_hdr));
	eip6->ip6_src = ip6->ip6_dst;

	/* Use the same prefix that we have in outer header */
	eip6->ip6_dst = ip6->ip6_src;
	MPASS(cfg->flags & NAT64_PLATPFX);
	nat64_embed_ip4(&eip6->ip6_dst, cfg->plat_plen, ip.ip_dst.s_addr);

	eip6->ip6_flow = htonl(ip.ip_tos << 20);
	eip6->ip6_vfc |= IPV6_VERSION;
	eip6->ip6_hlim = ip.ip_ttl;
	eip6->ip6_plen = htons(ntohs(ip.ip_len) - (ip.ip_hl << 2));
	eip6->ip6_nxt = (ip.ip_p == IPPROTO_ICMP) ? IPPROTO_ICMPV6: ip.ip_p;
	m_copydata(m, hlen, len, (char *)(eip6 + 1));
	/*
	 * We need to translate source port in the inner ULP header,
	 * and adjust ULP checksum.
	 */
	switch (ip.ip_p) {
	case IPPROTO_TCP:
		if (len < offsetof(struct tcphdr, th_sum))
			break;
		tcp = TCP(eip6 + 1);
		if (icmpid != 0) {
			tcp->th_sum = cksum_adjust(tcp->th_sum,
			    tcp->th_sport, icmpid);
			tcp->th_sport = icmpid;
		}
		tcp->th_sum = cksum_add(tcp->th_sum,
		    ~nat64_cksum_convert(eip6, &ip));
		break;
	case IPPROTO_UDP:
		if (len < offsetof(struct udphdr, uh_sum))
			break;
		udp = UDP(eip6 + 1);
		if (icmpid != 0) {
			udp->uh_sum = cksum_adjust(udp->uh_sum,
			    udp->uh_sport, icmpid);
			udp->uh_sport = icmpid;
		}
		udp->uh_sum = cksum_add(udp->uh_sum,
		    ~nat64_cksum_convert(eip6, &ip));
		break;
	case IPPROTO_ICMP:
		/*
		 * Check if this is an ICMP error message for echo request
		 * that we sent. I.e. ULP in the data containing invoking
		 * packet is IPPROTO_ICMP and its type is ICMP_ECHO.
		 */
		icmp = (struct icmp *)(eip6 + 1);
		if (icmp->icmp_type != ICMP_ECHO) {
			m_freem(n);
			goto freeit;
		}
		/*
		 * For our client this original datagram should looks
		 * like it was ICMPv6 datagram with type ICMP6_ECHO_REQUEST.
		 * Thus we need adjust icmp_cksum and convert type from
		 * ICMP_ECHO to ICMP6_ECHO_REQUEST.
		 */
		nat64_icmp_handle_echo(eip6, ICMP6(icmp), icmpid,
		    ICMP6_ECHO_REQUEST);
	}
	m_freem(m);
	/* Convert ICMPv4 into ICMPv6 header */
	icmp = mtodo(n, offset);
	ICMP6(icmp)->icmp6_type = type;
	ICMP6(icmp)->icmp6_code = code;
	ICMP6(icmp)->icmp6_mtu = htonl(mtu);
	ICMP6(icmp)->icmp6_cksum = 0;
	ICMP6(icmp)->icmp6_cksum = cksum_add(
	    ~in6_cksum_pseudo(ip6, plen, IPPROTO_ICMPV6, 0),
	    in_cksum_skip(n, n->m_pkthdr.len, offset));
	return (n);
freeit:
	m_freem(m);
	NAT64STAT_INC(&cfg->stats, dropped);
	return (NULL);
}

int
nat64_getlasthdr(struct mbuf *m, int *offset)
{
	struct ip6_hdr *ip6;
	struct ip6_hbh *hbh;
	int proto, hlen;

	if (offset != NULL)
		hlen = *offset;
	else
		hlen = 0;

	if (m->m_len < hlen + sizeof(*ip6))
		return (-1);

	ip6 = mtodo(m, hlen);
	hlen += sizeof(*ip6);
	proto = ip6->ip6_nxt;
	/* Skip extension headers */
	while (proto == IPPROTO_HOPOPTS || proto == IPPROTO_ROUTING ||
	    proto == IPPROTO_DSTOPTS) {
		hbh = mtodo(m, hlen);
		/*
		 * We expect mbuf has contigious data up to
		 * upper level header.
		 */
		if (m->m_len < hlen)
			return (-1);
		/*
		 * We doesn't support Jumbo payload option,
		 * so return error.
		 */
		if (proto == IPPROTO_HOPOPTS && ip6->ip6_plen == 0)
			return (-1);
		proto = hbh->ip6h_nxt;
		hlen += (hbh->ip6h_len + 1) << 3;
	}
	if (offset != NULL)
		*offset = hlen;
	return (proto);
}

int
nat64_do_handle_ip4(struct mbuf *m, struct in6_addr *saddr,
    struct in6_addr *daddr, uint16_t lport, struct nat64_config *cfg,
    void *logdata)
{
	struct nhop6_basic nh;
	struct ip6_hdr ip6;
	struct sockaddr_in6 dst;
	struct ip *ip;
	struct mbufq mq;
	uint16_t ip_id, ip_off;
	uint16_t *csum;
	int plen, hlen;
	uint8_t proto;

	ip = mtod(m, struct ip*);

	if (ip->ip_ttl <= IPTTLDEC) {
		nat64_icmp_reflect(m, ICMP_TIMXCEED,
		    ICMP_TIMXCEED_INTRANS, 0, &cfg->stats, logdata);
		return (NAT64RETURN);
	}

	ip6.ip6_dst = *daddr;
	ip6.ip6_src = *saddr;

	hlen = ip->ip_hl << 2;
	plen = ntohs(ip->ip_len) - hlen;
	proto = ip->ip_p;

	/* Save ip_id and ip_off, both are in network byte order */
	ip_id = ip->ip_id;
	ip_off = ip->ip_off & htons(IP_OFFMASK | IP_MF);

	/* Fragment length must be multiple of 8 octets */
	if ((ip->ip_off & htons(IP_MF)) != 0 && (plen & 0x7) != 0) {
		nat64_icmp_reflect(m, ICMP_PARAMPROB,
		    ICMP_PARAMPROB_LENGTH, 0, &cfg->stats, logdata);
		return (NAT64RETURN);
	}
	/* Fragmented ICMP is unsupported */
	if (proto == IPPROTO_ICMP && ip_off != 0) {
		DPRINTF(DP_DROPS, "dropped due to fragmented ICMP");
		NAT64STAT_INC(&cfg->stats, dropped);
		return (NAT64MFREE);
	}

	dst.sin6_addr = ip6.ip6_dst;
	if (nat64_find_route6(&nh, &dst, m) != 0) {
		NAT64STAT_INC(&cfg->stats, noroute6);
		nat64_icmp_reflect(m, ICMP_UNREACH, ICMP_UNREACH_HOST, 0,
		    &cfg->stats, logdata);
		return (NAT64RETURN);
	}
	if (nh.nh_mtu < plen + sizeof(ip6) &&
	    (ip->ip_off & htons(IP_DF)) != 0) {
		nat64_icmp_reflect(m, ICMP_UNREACH, ICMP_UNREACH_NEEDFRAG,
		    FRAGSZ(nh.nh_mtu) + sizeof(struct ip), &cfg->stats, logdata);
		return (NAT64RETURN);
	}

	ip6.ip6_flow = htonl(ip->ip_tos << 20);
	ip6.ip6_vfc |= IPV6_VERSION;
	ip6.ip6_hlim = ip->ip_ttl;
	/* Forwarding code will decrement TTL for netisr based output. */
	if (V_nat64out == &nat64_direct)
		ip6.ip6_hlim -= IPTTLDEC;
	ip6.ip6_plen = htons(plen);
	ip6.ip6_nxt = (proto == IPPROTO_ICMP) ? IPPROTO_ICMPV6: proto;
	/* Convert checksums. */
	switch (proto) {
	case IPPROTO_TCP:
		csum = &TCP(mtodo(m, hlen))->th_sum;
		if (lport != 0) {
			struct tcphdr *tcp = TCP(mtodo(m, hlen));
			*csum = cksum_adjust(*csum, tcp->th_dport, lport);
			tcp->th_dport = lport;
		}
		*csum = cksum_add(*csum, ~nat64_cksum_convert(&ip6, ip));
		break;
	case IPPROTO_UDP:
		csum = &UDP(mtodo(m, hlen))->uh_sum;
		if (lport != 0) {
			struct udphdr *udp = UDP(mtodo(m, hlen));
			*csum = cksum_adjust(*csum, udp->uh_dport, lport);
			udp->uh_dport = lport;
		}
		*csum = cksum_add(*csum, ~nat64_cksum_convert(&ip6, ip));
		break;
	case IPPROTO_ICMP:
		m = nat64_icmp_translate(m, &ip6, lport, hlen, cfg);
		if (m == NULL)	/* stats already accounted */
			return (NAT64RETURN);
	}

	m_adj(m, hlen);
	mbufq_init(&mq, 255);
	nat64_fragment6(&cfg->stats, &ip6, &mq, m, nh.nh_mtu, ip_id, ip_off);
	while ((m = mbufq_dequeue(&mq)) != NULL) {
		if (V_nat64out->output(nh.nh_ifp, m, (struct sockaddr *)&dst,
		    &cfg->stats, logdata) != 0)
			break;
		NAT64STAT_INC(&cfg->stats, opcnt46);
	}
	mbufq_drain(&mq);
	return (NAT64RETURN);
}

int
nat64_handle_icmp6(struct mbuf *m, int hlen, uint32_t aaddr, uint16_t aport,
    struct nat64_config *cfg, void *logdata)
{
	struct ip ip;
	struct icmp6_hdr *icmp6;
	struct ip6_frag *ip6f;
	struct ip6_hdr *ip6, *ip6i;
	uint32_t mtu;
	int plen, proto;
	uint8_t type, code;

	if (hlen == 0) {
		ip6 = mtod(m, struct ip6_hdr *);
		if (nat64_check_ip6(&ip6->ip6_src) != 0 ||
		    nat64_check_ip6(&ip6->ip6_dst) != 0)
			return (NAT64SKIP);

		proto = nat64_getlasthdr(m, &hlen);
		if (proto != IPPROTO_ICMPV6) {
			DPRINTF(DP_DROPS,
			    "dropped due to mbuf isn't contigious");
			NAT64STAT_INC(&cfg->stats, dropped);
			return (NAT64MFREE);
		}
	}

	/*
	 * Translate ICMPv6 type and code to ICMPv4 (RFC7915).
	 * NOTE: ICMPv6 echo handled by nat64_do_handle_ip6().
	 */
	icmp6 = mtodo(m, hlen);
	mtu = 0;
	switch (icmp6->icmp6_type) {
	case ICMP6_DST_UNREACH:
		type = ICMP_UNREACH;
		switch (icmp6->icmp6_code) {
		case ICMP6_DST_UNREACH_NOROUTE:
		case ICMP6_DST_UNREACH_BEYONDSCOPE:
		case ICMP6_DST_UNREACH_ADDR:
			code = ICMP_UNREACH_HOST;
			break;
		case ICMP6_DST_UNREACH_ADMIN:
			code = ICMP_UNREACH_HOST_PROHIB;
			break;
		case ICMP6_DST_UNREACH_NOPORT:
			code = ICMP_UNREACH_PORT;
			break;
		default:
			DPRINTF(DP_DROPS, "Unsupported ICMPv6 type %d,"
			    " code %d", icmp6->icmp6_type,
			    icmp6->icmp6_code);
			NAT64STAT_INC(&cfg->stats, dropped);
			return (NAT64MFREE);
		}
		break;
	case ICMP6_PACKET_TOO_BIG:
		type = ICMP_UNREACH;
		code = ICMP_UNREACH_NEEDFRAG;
		mtu = ntohl(icmp6->icmp6_mtu);
		if (mtu < IPV6_MMTU) {
			DPRINTF(DP_DROPS, "Wrong MTU %d in ICMPv6 type %d,"
			    " code %d", mtu, icmp6->icmp6_type,
			    icmp6->icmp6_code);
			NAT64STAT_INC(&cfg->stats, dropped);
			return (NAT64MFREE);
		}
		/*
		 * Adjust MTU to reflect difference between
		 * IPv6 an IPv4 headers.
		 */
		mtu -= sizeof(struct ip6_hdr) - sizeof(struct ip);
		break;
	case ICMP6_TIME_EXCEEDED:
		type = ICMP_TIMXCEED;
		code = icmp6->icmp6_code;
		break;
	case ICMP6_PARAM_PROB:
		switch (icmp6->icmp6_code) {
		case ICMP6_PARAMPROB_HEADER:
			type = ICMP_PARAMPROB;
			code = ICMP_PARAMPROB_ERRATPTR;
			mtu = ntohl(icmp6->icmp6_pptr);
			switch (mtu) {
			case 0: /* Version/Traffic Class */
			case 1: /* Traffic Class/Flow Label */
				break;
			case 4: /* Payload Length */
			case 5:
				mtu = 2;
				break;
			case 6: /* Next Header */
				mtu = 9;
				break;
			case 7: /* Hop Limit */
				mtu = 8;
				break;
			default:
				if (mtu >= 8 && mtu <= 23) {
					mtu = 12; /* Source address */
					break;
				}
				if (mtu >= 24 && mtu <= 39) {
					mtu = 16; /* Destination address */
					break;
				}
				DPRINTF(DP_DROPS, "Unsupported ICMPv6 type %d,"
				    " code %d, pptr %d", icmp6->icmp6_type,
				    icmp6->icmp6_code, mtu);
				NAT64STAT_INC(&cfg->stats, dropped);
				return (NAT64MFREE);
			}
		case ICMP6_PARAMPROB_NEXTHEADER:
			type = ICMP_UNREACH;
			code = ICMP_UNREACH_PROTOCOL;
			break;
		default:
			DPRINTF(DP_DROPS, "Unsupported ICMPv6 type %d,"
			    " code %d, pptr %d", icmp6->icmp6_type,
			    icmp6->icmp6_code, ntohl(icmp6->icmp6_pptr));
			NAT64STAT_INC(&cfg->stats, dropped);
			return (NAT64MFREE);
		}
		break;
	default:
		DPRINTF(DP_DROPS, "Unsupported ICMPv6 type %d, code %d",
		    icmp6->icmp6_type, icmp6->icmp6_code);
		NAT64STAT_INC(&cfg->stats, dropped);
		return (NAT64MFREE);
	}

	hlen += sizeof(struct icmp6_hdr);
	if (m->m_pkthdr.len < hlen + sizeof(struct ip6_hdr) + ICMP_MINLEN) {
		NAT64STAT_INC(&cfg->stats, dropped);
		DPRINTF(DP_DROPS, "Message is too short %d",
		    m->m_pkthdr.len);
		return (NAT64MFREE);
	}
	/*
	 * We need at least ICMP_MINLEN bytes of original datagram payload
	 * to generate ICMP message. It is nice that ICMP_MINLEN is equal
	 * to sizeof(struct ip6_frag). So, if embedded datagram had a fragment
	 * header we will not have to do m_pullup() again.
	 *
	 * What we have here:
	 * Outer header: (IPv6iGW, v4mapPRefix+v4exthost)
	 * Inner header: (v4mapPRefix+v4host, IPv6iHost) [sport, dport]
	 * We need to translate it to:
	 *
	 * Outer header: (alias_host, v4exthost)
	 * Inner header: (v4exthost, alias_host) [sport, alias_port]
	 *
	 * Assume caller function has checked if v4mapPRefix+v4host
	 * matches configured prefix.
	 * The only two things we should be provided with are mapping between
	 * IPv6iHost <> alias_host and between dport and alias_port.
	 */
	if (m->m_len < hlen + sizeof(struct ip6_hdr) + ICMP_MINLEN)
		m = m_pullup(m, hlen + sizeof(struct ip6_hdr) + ICMP_MINLEN);
	if (m == NULL) {
		NAT64STAT_INC(&cfg->stats, nomem);
		return (NAT64RETURN);
	}
	ip6 = mtod(m, struct ip6_hdr *);
	ip6i = mtodo(m, hlen);
	ip6f = NULL;
	proto = ip6i->ip6_nxt;
	plen = ntohs(ip6i->ip6_plen);
	hlen += sizeof(struct ip6_hdr);
	if (proto == IPPROTO_FRAGMENT) {
		if (m->m_pkthdr.len < hlen + sizeof(struct ip6_frag) +
		    ICMP_MINLEN)
			goto fail;
		ip6f = mtodo(m, hlen);
		proto = ip6f->ip6f_nxt;
		plen -= sizeof(struct ip6_frag);
		hlen += sizeof(struct ip6_frag);
		/* Ajust MTU to reflect frag header size */
		if (type == ICMP_UNREACH && code == ICMP_UNREACH_NEEDFRAG)
			mtu -= sizeof(struct ip6_frag);
	}
	if (proto != IPPROTO_TCP && proto != IPPROTO_UDP) {
		DPRINTF(DP_DROPS, "Unsupported proto %d in the inner header",
		    proto);
		goto fail;
	}
	if (nat64_check_ip6(&ip6i->ip6_src) != 0 ||
	    nat64_check_ip6(&ip6i->ip6_dst) != 0) {
		DPRINTF(DP_DROPS, "Inner addresses do not passes the check");
		goto fail;
	}
	/* Check if outer dst is the same as inner src */
	if (!IN6_ARE_ADDR_EQUAL(&ip6->ip6_dst, &ip6i->ip6_src)) {
		DPRINTF(DP_DROPS, "Inner src doesn't match outer dst");
		goto fail;
	}

	/* Now we need to make a fake IPv4 packet to generate ICMP message */
	ip.ip_dst.s_addr = aaddr;
	ip.ip_src.s_addr = nat64_extract_ip4(&ip6i->ip6_src, cfg->plat_plen);
	if (ip.ip_src.s_addr == 0)
		goto fail;
	/* XXX: Make fake ulp header */
	if (V_nat64out == &nat64_direct) /* init_ip4hdr will decrement it */
		ip6i->ip6_hlim += IPV6_HLIMDEC;
	nat64_init_ip4hdr(ip6i, ip6f, plen, proto, &ip);
	m_adj(m, hlen - sizeof(struct ip));
	bcopy(&ip, mtod(m, void *), sizeof(ip));
	nat64_icmp_reflect(m, type, code, (uint16_t)mtu, &cfg->stats,
	    logdata);
	return (NAT64RETURN);
fail:
	/*
	 * We must call m_freem() because mbuf pointer could be
	 * changed with m_pullup().
	 */
	m_freem(m);
	NAT64STAT_INC(&cfg->stats, dropped);
	return (NAT64RETURN);
}

int
nat64_do_handle_ip6(struct mbuf *m, uint32_t aaddr, uint16_t aport,
    struct nat64_config *cfg, void *logdata)
{
	struct ip ip;
	struct nhop4_basic nh;
	struct sockaddr_in dst;
	struct ip6_frag *frag;
	struct ip6_hdr *ip6;
	struct icmp6_hdr *icmp6;
	uint16_t *csum;
	int plen, hlen, proto;

	/*
	 * XXX: we expect ipfw_chk() did m_pullup() up to upper level
	 * protocol's headers. Also we skip some checks, that ip6_input(),
	 * ip6_forward(), ip6_fastfwd() and ipfw_chk() already did.
	 */
	ip6 = mtod(m, struct ip6_hdr *);
	if (nat64_check_ip6(&ip6->ip6_src) != 0 ||
	    nat64_check_ip6(&ip6->ip6_dst) != 0) {
		return (NAT64SKIP);
	}

	/* Starting from this point we must not return zero */
	ip.ip_src.s_addr = aaddr;
	if (nat64_check_ip4(ip.ip_src.s_addr) != 0) {
		DPRINTF(DP_GENERIC | DP_DROPS, "invalid source address: %08x",
		    ip.ip_src.s_addr);
		NAT64STAT_INC(&cfg->stats, dropped);
		return (NAT64MFREE);
	}

	ip.ip_dst.s_addr = nat64_extract_ip4(&ip6->ip6_dst, cfg->plat_plen);
	if (ip.ip_dst.s_addr == 0) {
		NAT64STAT_INC(&cfg->stats, dropped);
		return (NAT64MFREE);
	}

	if (ip6->ip6_hlim <= IPV6_HLIMDEC) {
		nat64_icmp6_reflect(m, ICMP6_TIME_EXCEEDED,
		    ICMP6_TIME_EXCEED_TRANSIT, 0, &cfg->stats, logdata);
		return (NAT64RETURN);
	}

	hlen = 0;
	plen = ntohs(ip6->ip6_plen);
	proto = nat64_getlasthdr(m, &hlen);
	if (proto < 0) {
		DPRINTF(DP_DROPS, "dropped due to mbuf isn't contigious");
		NAT64STAT_INC(&cfg->stats, dropped);
		return (NAT64MFREE);
	}
	frag = NULL;
	if (proto == IPPROTO_FRAGMENT) {
		/* ipfw_chk should m_pullup up to frag header */
		if (m->m_len < hlen + sizeof(*frag)) {
			DPRINTF(DP_DROPS,
			    "dropped due to mbuf isn't contigious");
			NAT64STAT_INC(&cfg->stats, dropped);
			return (NAT64MFREE);
		}
		frag = mtodo(m, hlen);
		proto = frag->ip6f_nxt;
		hlen += sizeof(*frag);
		/* Fragmented ICMPv6 is unsupported */
		if (proto == IPPROTO_ICMPV6) {
			DPRINTF(DP_DROPS, "dropped due to fragmented ICMPv6");
			NAT64STAT_INC(&cfg->stats, dropped);
			return (NAT64MFREE);
		}
		/* Fragment length must be multiple of 8 octets */
		if ((frag->ip6f_offlg & IP6F_MORE_FRAG) != 0 &&
		    ((plen + sizeof(struct ip6_hdr) - hlen) & 0x7) != 0) {
			nat64_icmp6_reflect(m, ICMP6_PARAM_PROB,
			    ICMP6_PARAMPROB_HEADER,
			    offsetof(struct ip6_hdr, ip6_plen), &cfg->stats,
			    logdata);
			return (NAT64RETURN);
		}
	}
	plen -= hlen - sizeof(struct ip6_hdr);
	if (plen < 0 || m->m_pkthdr.len < plen + hlen) {
		DPRINTF(DP_DROPS, "plen %d, pkthdr.len %d, hlen %d",
		    plen, m->m_pkthdr.len, hlen);
		NAT64STAT_INC(&cfg->stats, dropped);
		return (NAT64MFREE);
	}

	icmp6 = NULL;	/* Make gcc happy */
	if (proto == IPPROTO_ICMPV6) {
		icmp6 = mtodo(m, hlen);
		if (icmp6->icmp6_type != ICMP6_ECHO_REQUEST &&
		    icmp6->icmp6_type != ICMP6_ECHO_REPLY)
			return (nat64_handle_icmp6(m, hlen, aaddr, aport,
			    cfg, logdata));
	}
	dst.sin_addr.s_addr = ip.ip_dst.s_addr;
	if (nat64_find_route4(&nh, &dst, m) != 0) {
		NAT64STAT_INC(&cfg->stats, noroute4);
		nat64_icmp6_reflect(m, ICMP6_DST_UNREACH,
		    ICMP6_DST_UNREACH_NOROUTE, 0, &cfg->stats, logdata);
		return (NAT64RETURN);
	}
	if (nh.nh_mtu < plen + sizeof(ip)) {
		nat64_icmp6_reflect(m, ICMP6_PACKET_TOO_BIG, 0, nh.nh_mtu,
		    &cfg->stats, logdata);
		return (NAT64RETURN);
	}
	nat64_init_ip4hdr(ip6, frag, plen, proto, &ip);
	/* Convert checksums. */
	switch (proto) {
	case IPPROTO_TCP:
		csum = &TCP(mtodo(m, hlen))->th_sum;
		if (aport != 0) {
			struct tcphdr *tcp = TCP(mtodo(m, hlen));
			*csum = cksum_adjust(*csum, tcp->th_sport, aport);
			tcp->th_sport = aport;
		}
		*csum = cksum_add(*csum, nat64_cksum_convert(ip6, &ip));
		break;
	case IPPROTO_UDP:
		csum = &UDP(mtodo(m, hlen))->uh_sum;
		if (aport != 0) {
			struct udphdr *udp = UDP(mtodo(m, hlen));
			*csum = cksum_adjust(*csum, udp->uh_sport, aport);
			udp->uh_sport = aport;
		}
		*csum = cksum_add(*csum, nat64_cksum_convert(ip6, &ip));
		break;
	case IPPROTO_ICMPV6:
		/* Checksum in ICMPv6 covers pseudo header */
		csum = &icmp6->icmp6_cksum;
		*csum = cksum_add(*csum, in6_cksum_pseudo(ip6, plen,
		    IPPROTO_ICMPV6, 0));
		/* Convert ICMPv6 types to ICMP */
		proto = *(uint16_t *)icmp6; /* save old word for cksum_adjust */
		if (icmp6->icmp6_type == ICMP6_ECHO_REQUEST)
			icmp6->icmp6_type = ICMP_ECHO;
		else /* ICMP6_ECHO_REPLY */
			icmp6->icmp6_type = ICMP_ECHOREPLY;
		*csum = cksum_adjust(*csum, (uint16_t)proto,
		    *(uint16_t *)icmp6);
		if (aport != 0) {
			uint16_t old_id = icmp6->icmp6_id;
			icmp6->icmp6_id = aport;
			*csum = cksum_adjust(*csum, old_id, aport);
		}
		break;
	};

	m_adj(m, hlen - sizeof(ip));
	bcopy(&ip, mtod(m, void *), sizeof(ip));
	if (V_nat64out->output(nh.nh_ifp, m, (struct sockaddr *)&dst,
	    &cfg->stats, logdata) == 0)
		NAT64STAT_INC(&cfg->stats, opcnt64);
	return (NAT64RETURN);
}

