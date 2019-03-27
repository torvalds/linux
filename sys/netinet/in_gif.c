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
 *	$KAME: in_gif.c,v 1.54 2001/05/14 14:02:16 itojun Exp $
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
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/in_var.h>
#include <netinet/ip_encap.h>
#include <netinet/ip_ecn.h>
#include <netinet/in_fib.h>

#ifdef INET6
#include <netinet/ip6.h>
#endif

#include <net/if_gif.h>

#define GIF_TTL		30
VNET_DEFINE_STATIC(int, ip_gif_ttl) = GIF_TTL;
#define	V_ip_gif_ttl		VNET(ip_gif_ttl)
SYSCTL_INT(_net_inet_ip, IPCTL_GIF_TTL, gifttl, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ip_gif_ttl), 0, "Default TTL value for encapsulated packets");

/*
 * We keep interfaces in a hash table using src+dst as key.
 * Interfaces with GIF_IGNORE_SOURCE flag are linked into plain list.
 */
VNET_DEFINE_STATIC(struct gif_list *, ipv4_hashtbl) = NULL;
VNET_DEFINE_STATIC(struct gif_list *, ipv4_srchashtbl) = NULL;
VNET_DEFINE_STATIC(struct gif_list, ipv4_list) = CK_LIST_HEAD_INITIALIZER();
#define	V_ipv4_hashtbl		VNET(ipv4_hashtbl)
#define	V_ipv4_srchashtbl	VNET(ipv4_srchashtbl)
#define	V_ipv4_list		VNET(ipv4_list)

#define	GIF_HASH(src, dst)	(V_ipv4_hashtbl[\
    in_gif_hashval((src), (dst)) & (GIF_HASH_SIZE - 1)])
#define	GIF_SRCHASH(src)	(V_ipv4_srchashtbl[\
    fnv_32_buf(&(src), sizeof(src), FNV1_32_INIT) & (GIF_HASH_SIZE - 1)])
#define	GIF_HASH_SC(sc)		GIF_HASH((sc)->gif_iphdr->ip_src.s_addr,\
    (sc)->gif_iphdr->ip_dst.s_addr)
static uint32_t
in_gif_hashval(in_addr_t src, in_addr_t dst)
{
	uint32_t ret;

	ret = fnv_32_buf(&src, sizeof(src), FNV1_32_INIT);
	return (fnv_32_buf(&dst, sizeof(dst), ret));
}

static int
in_gif_checkdup(const struct gif_softc *sc, in_addr_t src, in_addr_t dst)
{
	struct gif_softc *tmp;

	if (sc->gif_family == AF_INET &&
	    sc->gif_iphdr->ip_src.s_addr == src &&
	    sc->gif_iphdr->ip_dst.s_addr == dst)
		return (EEXIST);

	CK_LIST_FOREACH(tmp, &GIF_HASH(src, dst), chain) {
		if (tmp == sc)
			continue;
		if (tmp->gif_iphdr->ip_src.s_addr == src &&
		    tmp->gif_iphdr->ip_dst.s_addr == dst)
			return (EADDRNOTAVAIL);
	}
	return (0);
}

/*
 * Check that ingress address belongs to local host.
 */
static void
in_gif_set_running(struct gif_softc *sc)
{

	if (in_localip(sc->gif_iphdr->ip_src))
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
in_gif_srcaddr(void *arg __unused, const struct sockaddr *sa,
    int event __unused)
{
	const struct sockaddr_in *sin;
	struct gif_softc *sc;

	/* Check that VNET is ready */
	if (V_ipv4_hashtbl == NULL)
		return;

	MPASS(in_epoch(net_epoch_preempt));
	sin = (const struct sockaddr_in *)sa;
	CK_LIST_FOREACH(sc, &GIF_SRCHASH(sin->sin_addr.s_addr), srchash) {
		if (sc->gif_iphdr->ip_src.s_addr != sin->sin_addr.s_addr)
			continue;
		in_gif_set_running(sc);
	}
}

static void
in_gif_attach(struct gif_softc *sc)
{

	if (sc->gif_options & GIF_IGNORE_SOURCE)
		CK_LIST_INSERT_HEAD(&V_ipv4_list, sc, chain);
	else
		CK_LIST_INSERT_HEAD(&GIF_HASH_SC(sc), sc, chain);

	CK_LIST_INSERT_HEAD(&GIF_SRCHASH(sc->gif_iphdr->ip_src.s_addr),
	    sc, srchash);
}

int
in_gif_setopts(struct gif_softc *sc, u_int options)
{

	/* NOTE: we are protected with gif_ioctl_sx lock */
	MPASS(sc->gif_family == AF_INET);
	MPASS(sc->gif_options != options);

	if ((options & GIF_IGNORE_SOURCE) !=
	    (sc->gif_options & GIF_IGNORE_SOURCE)) {
		CK_LIST_REMOVE(sc, srchash);
		CK_LIST_REMOVE(sc, chain);
		sc->gif_options = options;
		in_gif_attach(sc);
	}
	return (0);
}

int
in_gif_ioctl(struct gif_softc *sc, u_long cmd, caddr_t data)
{
	struct ifreq *ifr = (struct ifreq *)data;
	struct sockaddr_in *dst, *src;
	struct ip *ip;
	int error;

	/* NOTE: we are protected with gif_ioctl_sx lock */
	error = EINVAL;
	switch (cmd) {
	case SIOCSIFPHYADDR:
		src = &((struct in_aliasreq *)data)->ifra_addr;
		dst = &((struct in_aliasreq *)data)->ifra_dstaddr;

		/* sanity checks */
		if (src->sin_family != dst->sin_family ||
		    src->sin_family != AF_INET ||
		    src->sin_len != dst->sin_len ||
		    src->sin_len != sizeof(*src))
			break;
		if (src->sin_addr.s_addr == INADDR_ANY ||
		    dst->sin_addr.s_addr == INADDR_ANY) {
			error = EADDRNOTAVAIL;
			break;
		}
		if (V_ipv4_hashtbl == NULL) {
			V_ipv4_hashtbl = gif_hashinit();
			V_ipv4_srchashtbl = gif_hashinit();
		}
		error = in_gif_checkdup(sc, src->sin_addr.s_addr,
		    dst->sin_addr.s_addr);
		if (error == EADDRNOTAVAIL)
			break;
		if (error == EEXIST) {
			/* Addresses are the same. Just return. */
			error = 0;
			break;
		}
		ip = malloc(sizeof(*ip), M_GIF, M_WAITOK | M_ZERO);
		ip->ip_src.s_addr = src->sin_addr.s_addr;
		ip->ip_dst.s_addr = dst->sin_addr.s_addr;
		if (sc->gif_family != 0) {
			/* Detach existing tunnel first */
			CK_LIST_REMOVE(sc, srchash);
			CK_LIST_REMOVE(sc, chain);
			GIF_WAIT();
			free(sc->gif_hdr, M_GIF);
			/* XXX: should we notify about link state change? */
		}
		sc->gif_family = AF_INET;
		sc->gif_iphdr = ip;
		in_gif_attach(sc);
		in_gif_set_running(sc);
		break;
	case SIOCGIFPSRCADDR:
	case SIOCGIFPDSTADDR:
		if (sc->gif_family != AF_INET) {
			error = EADDRNOTAVAIL;
			break;
		}
		src = (struct sockaddr_in *)&ifr->ifr_addr;
		memset(src, 0, sizeof(*src));
		src->sin_family = AF_INET;
		src->sin_len = sizeof(*src);
		src->sin_addr = (cmd == SIOCGIFPSRCADDR) ?
		    sc->gif_iphdr->ip_src: sc->gif_iphdr->ip_dst;
		error = prison_if(curthread->td_ucred, (struct sockaddr *)src);
		if (error != 0)
			memset(src, 0, sizeof(*src));
		break;
	}
	return (error);
}

int
in_gif_output(struct ifnet *ifp, struct mbuf *m, int proto, uint8_t ecn)
{
	struct gif_softc *sc = ifp->if_softc;
	struct ip *ip;
	int len;

	/* prepend new IP header */
	MPASS(in_epoch(net_epoch_preempt));
	len = sizeof(struct ip);
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
		    ("in_gif_output: unexpected misalignment"));
		m->m_data += len;
		m->m_len -= ETHERIP_ALIGN;
	}
#endif
	ip = mtod(m, struct ip *);

	MPASS(sc->gif_family == AF_INET);
	bcopy(sc->gif_iphdr, ip, sizeof(struct ip));
	ip->ip_p = proto;
	/* version will be set in ip_output() */
	ip->ip_ttl = V_ip_gif_ttl;
	ip->ip_len = htons(m->m_pkthdr.len);
	ip->ip_tos = ecn;

	return (ip_output(m, NULL, NULL, 0, NULL, NULL));
}

static int
in_gif_input(struct mbuf *m, int off, int proto, void *arg)
{
	struct gif_softc *sc = arg;
	struct ifnet *gifp;
	struct ip *ip;
	uint8_t ecn;

	MPASS(in_epoch(net_epoch_preempt));
	if (sc == NULL) {
		m_freem(m);
		KMOD_IPSTAT_INC(ips_nogif);
		return (IPPROTO_DONE);
	}
	gifp = GIF2IFP(sc);
	if ((gifp->if_flags & IFF_UP) != 0) {
		ip = mtod(m, struct ip *);
		ecn = ip->ip_tos;
		m_adj(m, off);
		gif_input(m, gifp, proto, ecn);
	} else {
		m_freem(m);
		KMOD_IPSTAT_INC(ips_nogif);
	}
	return (IPPROTO_DONE);
}

static int
in_gif_lookup(const struct mbuf *m, int off, int proto, void **arg)
{
	const struct ip *ip;
	struct gif_softc *sc;
	int ret;

	if (V_ipv4_hashtbl == NULL)
		return (0);

	MPASS(in_epoch(net_epoch_preempt));
	ip = mtod(m, const struct ip *);
	/*
	 * NOTE: it is safe to iterate without any locking here, because softc
	 * can be reclaimed only when we are not within net_epoch_preempt
	 * section, but ip_encap lookup+input are executed in epoch section.
	 */
	ret = 0;
	CK_LIST_FOREACH(sc, &GIF_HASH(ip->ip_dst.s_addr,
	    ip->ip_src.s_addr), chain) {
		/*
		 * This is an inbound packet, its ip_dst is source address
		 * in softc.
		 */
		if (sc->gif_iphdr->ip_src.s_addr == ip->ip_dst.s_addr &&
		    sc->gif_iphdr->ip_dst.s_addr == ip->ip_src.s_addr) {
			ret = ENCAP_DRV_LOOKUP;
			goto done;
		}
	}
	/*
	 * No exact match.
	 * Check the list of interfaces with GIF_IGNORE_SOURCE flag.
	 */
	CK_LIST_FOREACH(sc, &V_ipv4_list, chain) {
		if (sc->gif_iphdr->ip_src.s_addr == ip->ip_dst.s_addr) {
			ret = 32 + 8; /* src + proto */
			goto done;
		}
	}
	return (0);
done:
	if ((GIF2IFP(sc)->if_flags & IFF_UP) == 0)
		return (0);
	/* ingress filters on outer source */
	if ((GIF2IFP(sc)->if_flags & IFF_LINK2) == 0) {
		struct nhop4_basic nh4;
		struct in_addr dst;

		dst = ip->ip_src;
		if (fib4_lookup_nh_basic(sc->gif_fibnum, dst, 0, 0, &nh4) != 0)
			return (0);
		if (nh4.nh_ifp != m->m_pkthdr.rcvif)
			return (0);
	}
	*arg = sc;
	return (ret);
}

static const struct srcaddrtab *ipv4_srcaddrtab;
static struct {
	const struct encap_config encap;
	const struct encaptab *cookie;
} ipv4_encap_cfg[] = {
	{
		.encap = {
			.proto = IPPROTO_IPV4,
			.min_length = 2 * sizeof(struct ip),
			.exact_match = ENCAP_DRV_LOOKUP,
			.lookup = in_gif_lookup,
			.input = in_gif_input
		},
	},
#ifdef INET6
	{
		.encap = {
			.proto = IPPROTO_IPV6,
			.min_length = sizeof(struct ip) +
			    sizeof(struct ip6_hdr),
			.exact_match = ENCAP_DRV_LOOKUP,
			.lookup = in_gif_lookup,
			.input = in_gif_input
		},
	},
#endif
	{
		.encap = {
			.proto = IPPROTO_ETHERIP,
			.min_length = sizeof(struct ip) +
			    sizeof(struct etherip_header) +
			    sizeof(struct ether_header),
			.exact_match = ENCAP_DRV_LOOKUP,
			.lookup = in_gif_lookup,
			.input = in_gif_input
		},
	}
};

void
in_gif_init(void)
{
	int i;

	if (!IS_DEFAULT_VNET(curvnet))
		return;

	ipv4_srcaddrtab = ip_encap_register_srcaddr(in_gif_srcaddr,
	    NULL, M_WAITOK);
	for (i = 0; i < nitems(ipv4_encap_cfg); i++)
		ipv4_encap_cfg[i].cookie = ip_encap_attach(
		    &ipv4_encap_cfg[i].encap, NULL, M_WAITOK);
}

void
in_gif_uninit(void)
{
	int i;

	if (IS_DEFAULT_VNET(curvnet)) {
		for (i = 0; i < nitems(ipv4_encap_cfg); i++)
			ip_encap_detach(ipv4_encap_cfg[i].cookie);
		ip_encap_unregister_srcaddr(ipv4_srcaddrtab);
	}
	if (V_ipv4_hashtbl != NULL) {
		gif_hashdestroy(V_ipv4_hashtbl);
		V_ipv4_hashtbl = NULL;
		GIF_WAIT();
		gif_hashdestroy(V_ipv4_srchashtbl);
	}
}

