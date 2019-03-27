/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * Copyright (c) 2018 Andrey V. Elsukov <ae@FreeBSD.org>
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
 *	$KAME: in6_gif.c,v 1.49 2001/05/14 14:02:17 itojun Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/jail.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/vnet.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#ifdef INET
#include <netinet/ip.h>
#include <netinet/ip_ecn.h>
#endif
#include <netinet/ip_encap.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_var.h>
#include <netinet6/scope6_var.h>
#include <netinet6/ip6_ecn.h>
#include <netinet6/in6_fib.h>

#include <net/if_gif.h>

#define GIF_HLIM	30
VNET_DEFINE_STATIC(int, ip6_gif_hlim) = GIF_HLIM;
#define	V_ip6_gif_hlim			VNET(ip6_gif_hlim)

SYSCTL_DECL(_net_inet6_ip6);
SYSCTL_INT(_net_inet6_ip6, IPV6CTL_GIF_HLIM, gifhlim,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ip6_gif_hlim), 0,
    "Default hop limit for encapsulated packets");

/*
 * We keep interfaces in a hash table using src+dst as key.
 * Interfaces with GIF_IGNORE_SOURCE flag are linked into plain list.
 */
VNET_DEFINE_STATIC(struct gif_list *, ipv6_hashtbl) = NULL;
VNET_DEFINE_STATIC(struct gif_list *, ipv6_srchashtbl) = NULL;
VNET_DEFINE_STATIC(struct gif_list, ipv6_list) = CK_LIST_HEAD_INITIALIZER();
#define	V_ipv6_hashtbl		VNET(ipv6_hashtbl)
#define	V_ipv6_srchashtbl	VNET(ipv6_srchashtbl)
#define	V_ipv6_list		VNET(ipv6_list)

#define	GIF_HASH(src, dst)	(V_ipv6_hashtbl[\
    in6_gif_hashval((src), (dst)) & (GIF_HASH_SIZE - 1)])
#define	GIF_SRCHASH(src)	(V_ipv6_srchashtbl[\
    fnv_32_buf((src), sizeof(*src), FNV1_32_INIT) & (GIF_HASH_SIZE - 1)])
#define	GIF_HASH_SC(sc)		GIF_HASH(&(sc)->gif_ip6hdr->ip6_src,\
    &(sc)->gif_ip6hdr->ip6_dst)
static uint32_t
in6_gif_hashval(const struct in6_addr *src, const struct in6_addr *dst)
{
	uint32_t ret;

	ret = fnv_32_buf(src, sizeof(*src), FNV1_32_INIT);
	return (fnv_32_buf(dst, sizeof(*dst), ret));
}

static int
in6_gif_checkdup(const struct gif_softc *sc, const struct in6_addr *src,
    const struct in6_addr *dst)
{
	struct gif_softc *tmp;

	if (sc->gif_family == AF_INET6 &&
	    IN6_ARE_ADDR_EQUAL(&sc->gif_ip6hdr->ip6_src, src) &&
	    IN6_ARE_ADDR_EQUAL(&sc->gif_ip6hdr->ip6_dst, dst))
		return (EEXIST);

	CK_LIST_FOREACH(tmp, &GIF_HASH(src, dst), chain) {
		if (tmp == sc)
			continue;
		if (IN6_ARE_ADDR_EQUAL(&tmp->gif_ip6hdr->ip6_src, src) &&
		    IN6_ARE_ADDR_EQUAL(&tmp->gif_ip6hdr->ip6_dst, dst))
			return (EADDRNOTAVAIL);
	}
	return (0);
}

/*
 * Check that ingress address belongs to local host.
 */
static void
in6_gif_set_running(struct gif_softc *sc)
{

	if (in6_localip(&sc->gif_ip6hdr->ip6_src))
		GIF2IFP(sc)->if_drv_flags |= IFF_DRV_RUNNING;
	else
		GIF2IFP(sc)->if_drv_flags &= ~IFF_DRV_RUNNING;
}

/*
 * ifaddr_event handler.
 * Clear IFF_DRV_RUNNING flag when ingress address disappears to prevent
 * source address spoofing.
 */
static void
in6_gif_srcaddr(void *arg __unused, const struct sockaddr *sa, int event)
{
	const struct sockaddr_in6 *sin;
	struct gif_softc *sc;

	/* Check that VNET is ready */
	if (V_ipv6_hashtbl == NULL)
		return;

	MPASS(in_epoch(net_epoch_preempt));
	sin = (const struct sockaddr_in6 *)sa;
	CK_LIST_FOREACH(sc, &GIF_SRCHASH(&sin->sin6_addr), srchash) {
		if (IN6_ARE_ADDR_EQUAL(&sc->gif_ip6hdr->ip6_src,
		    &sin->sin6_addr) == 0)
			continue;
		in6_gif_set_running(sc);
	}
}

static void
in6_gif_attach(struct gif_softc *sc)
{

	if (sc->gif_options & GIF_IGNORE_SOURCE)
		CK_LIST_INSERT_HEAD(&V_ipv6_list, sc, chain);
	else
		CK_LIST_INSERT_HEAD(&GIF_HASH_SC(sc), sc, chain);

	CK_LIST_INSERT_HEAD(&GIF_SRCHASH(&sc->gif_ip6hdr->ip6_src),
	    sc, srchash);
}

int
in6_gif_setopts(struct gif_softc *sc, u_int options)
{

	/* NOTE: we are protected with gif_ioctl_sx lock */
	MPASS(sc->gif_family == AF_INET6);
	MPASS(sc->gif_options != options);

	if ((options & GIF_IGNORE_SOURCE) !=
	    (sc->gif_options & GIF_IGNORE_SOURCE)) {
		CK_LIST_REMOVE(sc, srchash);
		CK_LIST_REMOVE(sc, chain);
		sc->gif_options = options;
		in6_gif_attach(sc);
	}
	return (0);
}

int
in6_gif_ioctl(struct gif_softc *sc, u_long cmd, caddr_t data)
{
	struct in6_ifreq *ifr = (struct in6_ifreq *)data;
	struct sockaddr_in6 *dst, *src;
	struct ip6_hdr *ip6;
	int error;

	/* NOTE: we are protected with gif_ioctl_sx lock */
	error = EINVAL;
	switch (cmd) {
	case SIOCSIFPHYADDR_IN6:
		src = &((struct in6_aliasreq *)data)->ifra_addr;
		dst = &((struct in6_aliasreq *)data)->ifra_dstaddr;

		/* sanity checks */
		if (src->sin6_family != dst->sin6_family ||
		    src->sin6_family != AF_INET6 ||
		    src->sin6_len != dst->sin6_len ||
		    src->sin6_len != sizeof(*src))
			break;
		if (IN6_IS_ADDR_UNSPECIFIED(&src->sin6_addr) ||
		    IN6_IS_ADDR_UNSPECIFIED(&dst->sin6_addr)) {
			error = EADDRNOTAVAIL;
			break;
		}
		/*
		 * Check validity of the scope zone ID of the
		 * addresses, and convert it into the kernel
		 * internal form if necessary.
		 */
		if ((error = sa6_embedscope(src, 0)) != 0 ||
		    (error = sa6_embedscope(dst, 0)) != 0)
			break;

		if (V_ipv6_hashtbl == NULL) {
			V_ipv6_hashtbl = gif_hashinit();
			V_ipv6_srchashtbl = gif_hashinit();
		}
		error = in6_gif_checkdup(sc, &src->sin6_addr,
		    &dst->sin6_addr);
		if (error == EADDRNOTAVAIL)
			break;
		if (error == EEXIST) {
			/* Addresses are the same. Just return. */
			error = 0;
			break;
		}
		ip6 = malloc(sizeof(*ip6), M_GIF, M_WAITOK | M_ZERO);
		ip6->ip6_src = src->sin6_addr;
		ip6->ip6_dst = dst->sin6_addr;
		ip6->ip6_vfc = IPV6_VERSION;
		if (sc->gif_family != 0) {
			/* Detach existing tunnel first */
			CK_LIST_REMOVE(sc, srchash);
			CK_LIST_REMOVE(sc, chain);
			GIF_WAIT();
			free(sc->gif_hdr, M_GIF);
			/* XXX: should we notify about link state change? */
		}
		sc->gif_family = AF_INET6;
		sc->gif_ip6hdr = ip6;
		in6_gif_attach(sc);
		in6_gif_set_running(sc);
		break;
	case SIOCGIFPSRCADDR_IN6:
	case SIOCGIFPDSTADDR_IN6:
		if (sc->gif_family != AF_INET6) {
			error = EADDRNOTAVAIL;
			break;
		}
		src = (struct sockaddr_in6 *)&ifr->ifr_addr;
		memset(src, 0, sizeof(*src));
		src->sin6_family = AF_INET6;
		src->sin6_len = sizeof(*src);
		src->sin6_addr = (cmd == SIOCGIFPSRCADDR_IN6) ?
		    sc->gif_ip6hdr->ip6_src: sc->gif_ip6hdr->ip6_dst;
		error = prison_if(curthread->td_ucred, (struct sockaddr *)src);
		if (error == 0)
			error = sa6_recoverscope(src);
		if (error != 0)
			memset(src, 0, sizeof(*src));
		break;
	}
	return (error);
}

int
in6_gif_output(struct ifnet *ifp, struct mbuf *m, int proto, uint8_t ecn)
{
	struct gif_softc *sc = ifp->if_softc;
	struct ip6_hdr *ip6;
	int len;

	/* prepend new IP header */
	MPASS(in_epoch(net_epoch_preempt));
	len = sizeof(struct ip6_hdr);
#ifndef __NO_STRICT_ALIGNMENT
	if (proto == IPPROTO_ETHERIP)
		len += ETHERIP_ALIGN;
#endif
	M_PREPEND(m, len, M_NOWAIT);
	if (m == NULL)
		return (ENOBUFS);
#ifndef __NO_STRICT_ALIGNMENT
	if (proto == IPPROTO_ETHERIP) {
		len = mtod(m, vm_offset_t) & 3;
		KASSERT(len == 0 || len == ETHERIP_ALIGN,
		    ("in6_gif_output: unexpected misalignment"));
		m->m_data += len;
		m->m_len -= ETHERIP_ALIGN;
	}
#endif

	ip6 = mtod(m, struct ip6_hdr *);
	MPASS(sc->gif_family == AF_INET6);
	bcopy(sc->gif_ip6hdr, ip6, sizeof(struct ip6_hdr));

	ip6->ip6_flow  |= htonl((uint32_t)ecn << 20);
	ip6->ip6_nxt	= proto;
	ip6->ip6_hlim	= V_ip6_gif_hlim;
	/*
	 * force fragmentation to minimum MTU, to avoid path MTU discovery.
	 * it is too painful to ask for resend of inner packet, to achieve
	 * path MTU discovery for encapsulated packets.
	 */
	return (ip6_output(m, 0, NULL, IPV6_MINMTU, 0, NULL, NULL));
}

static int
in6_gif_input(struct mbuf *m, int off, int proto, void *arg)
{
	struct gif_softc *sc = arg;
	struct ifnet *gifp;
	struct ip6_hdr *ip6;
	uint8_t ecn;

	MPASS(in_epoch(net_epoch_preempt));
	if (sc == NULL) {
		m_freem(m);
		IP6STAT_INC(ip6s_nogif);
		return (IPPROTO_DONE);
	}
	gifp = GIF2IFP(sc);
	if ((gifp->if_flags & IFF_UP) != 0) {
		ip6 = mtod(m, struct ip6_hdr *);
		ecn = (ntohl(ip6->ip6_flow) >> 20) & 0xff;
		m_adj(m, off);
		gif_input(m, gifp, proto, ecn);
	} else {
		m_freem(m);
		IP6STAT_INC(ip6s_nogif);
	}
	return (IPPROTO_DONE);
}

static int
in6_gif_lookup(const struct mbuf *m, int off, int proto, void **arg)
{
	const struct ip6_hdr *ip6;
	struct gif_softc *sc;
	int ret;

	if (V_ipv6_hashtbl == NULL)
		return (0);

	MPASS(in_epoch(net_epoch_preempt));
	/*
	 * NOTE: it is safe to iterate without any locking here, because softc
	 * can be reclaimed only when we are not within net_epoch_preempt
	 * section, but ip_encap lookup+input are executed in epoch section.
	 */
	ip6 = mtod(m, const struct ip6_hdr *);
	ret = 0;
	CK_LIST_FOREACH(sc, &GIF_HASH(&ip6->ip6_dst, &ip6->ip6_src), chain) {
		/*
		 * This is an inbound packet, its ip6_dst is source address
		 * in softc.
		 */
		if (IN6_ARE_ADDR_EQUAL(&sc->gif_ip6hdr->ip6_src,
		    &ip6->ip6_dst) &&
		    IN6_ARE_ADDR_EQUAL(&sc->gif_ip6hdr->ip6_dst,
		    &ip6->ip6_src)) {
			ret = ENCAP_DRV_LOOKUP;
			goto done;
		}
	}
	/*
	 * No exact match.
	 * Check the list of interfaces with GIF_IGNORE_SOURCE flag.
	 */
	CK_LIST_FOREACH(sc, &V_ipv6_list, chain) {
		if (IN6_ARE_ADDR_EQUAL(&sc->gif_ip6hdr->ip6_src,
		    &ip6->ip6_dst)) {
			ret = 128 + 8; /* src + proto */
			goto done;
		}
	}
	return (0);
done:
	if ((GIF2IFP(sc)->if_flags & IFF_UP) == 0)
		return (0);
	/* ingress filters on outer source */
	if ((GIF2IFP(sc)->if_flags & IFF_LINK2) == 0) {
		struct nhop6_basic nh6;

		if (fib6_lookup_nh_basic(sc->gif_fibnum, &ip6->ip6_src,
		    ntohs(in6_getscope(&ip6->ip6_src)), 0, 0, &nh6) != 0)
			return (0);

		if (nh6.nh_ifp != m->m_pkthdr.rcvif)
			return (0);
	}
	*arg = sc;
	return (ret);
}

static const struct srcaddrtab *ipv6_srcaddrtab;
static struct {
	const struct encap_config encap;
	const struct encaptab *cookie;
} ipv6_encap_cfg[] = {
#ifdef INET
	{
		.encap = {
			.proto = IPPROTO_IPV4,
			.min_length = sizeof(struct ip6_hdr) +
			    sizeof(struct ip),
			.exact_match = ENCAP_DRV_LOOKUP,
			.lookup = in6_gif_lookup,
			.input = in6_gif_input
		},
	},
#endif
	{
		.encap = {
			.proto = IPPROTO_IPV6,
			.min_length = 2 * sizeof(struct ip6_hdr),
			.exact_match = ENCAP_DRV_LOOKUP,
			.lookup = in6_gif_lookup,
			.input = in6_gif_input
		},
	},
	{
		.encap = {
			.proto = IPPROTO_ETHERIP,
			.min_length = sizeof(struct ip6_hdr) +
			    sizeof(struct etherip_header) +
			    sizeof(struct ether_header),
			.exact_match = ENCAP_DRV_LOOKUP,
			.lookup = in6_gif_lookup,
			.input = in6_gif_input
		},
	}
};

void
in6_gif_init(void)
{
	int i;

	if (!IS_DEFAULT_VNET(curvnet))
		return;

	ipv6_srcaddrtab = ip6_encap_register_srcaddr(in6_gif_srcaddr,
	    NULL, M_WAITOK);
	for (i = 0; i < nitems(ipv6_encap_cfg); i++)
		ipv6_encap_cfg[i].cookie = ip6_encap_attach(
		    &ipv6_encap_cfg[i].encap, NULL, M_WAITOK);
}

void
in6_gif_uninit(void)
{
	int i;

	if (IS_DEFAULT_VNET(curvnet)) {
		for (i = 0; i < nitems(ipv6_encap_cfg); i++)
			ip6_encap_detach(ipv6_encap_cfg[i].cookie);
		ip6_encap_unregister_srcaddr(ipv6_srcaddrtab);
	}
	if (V_ipv6_hashtbl != NULL) {
		gif_hashdestroy(V_ipv6_hashtbl);
		V_ipv6_hashtbl = NULL;
		GIF_WAIT();
		gif_hashdestroy(V_ipv6_srchashtbl);
	}
}
