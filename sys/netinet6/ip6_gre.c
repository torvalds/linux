/*-
 * Copyright (c) 2014, 2018 Andrey V. Elsukov <ae@FreeBSD.org>
 * All rights reserved.
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

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/jail.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>
#include <sys/proc.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>

#include <netinet/in.h>
#ifdef INET
#include <net/ethernet.h>
#include <netinet/ip.h>
#endif
#include <netinet/ip_encap.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_var.h>
#include <netinet6/scope6_var.h>
#include <net/if_gre.h>

VNET_DEFINE(int, ip6_gre_hlim) = IPV6_DEFHLIM;
#define	V_ip6_gre_hlim		VNET(ip6_gre_hlim)

SYSCTL_DECL(_net_inet6_ip6);
SYSCTL_INT(_net_inet6_ip6, OID_AUTO, grehlim, CTLFLAG_VNET | CTLFLAG_RW,
    &VNET_NAME(ip6_gre_hlim), 0, "Default hop limit for encapsulated packets");

VNET_DEFINE_STATIC(struct gre_list *, ipv6_hashtbl) = NULL;
VNET_DEFINE_STATIC(struct gre_list *, ipv6_srchashtbl) = NULL;
#define	V_ipv6_hashtbl		VNET(ipv6_hashtbl)
#define	V_ipv6_srchashtbl	VNET(ipv6_srchashtbl)
#define	GRE_HASH(src, dst)	(V_ipv6_hashtbl[\
    in6_gre_hashval((src), (dst)) & (GRE_HASH_SIZE - 1)])
#define	GRE_SRCHASH(src)	(V_ipv6_srchashtbl[\
    fnv_32_buf((src), sizeof(*src), FNV1_32_INIT) & (GRE_HASH_SIZE - 1)])
#define	GRE_HASH_SC(sc)		GRE_HASH(&(sc)->gre_oip6.ip6_src,\
    &(sc)->gre_oip6.ip6_dst)

static uint32_t
in6_gre_hashval(const struct in6_addr *src, const struct in6_addr *dst)
{
	uint32_t ret;

	ret = fnv_32_buf(src, sizeof(*src), FNV1_32_INIT);
	return (fnv_32_buf(dst, sizeof(*dst), ret));
}

static int
in6_gre_checkdup(const struct gre_softc *sc, const struct in6_addr *src,
    const struct in6_addr *dst)
{
	struct gre_softc *tmp;

	if (sc->gre_family == AF_INET6 &&
	    IN6_ARE_ADDR_EQUAL(&sc->gre_oip6.ip6_src, src) &&
	    IN6_ARE_ADDR_EQUAL(&sc->gre_oip6.ip6_dst, dst))
		return (EEXIST);

	CK_LIST_FOREACH(tmp, &GRE_HASH(src, dst), chain) {
		if (tmp == sc)
			continue;
		if (IN6_ARE_ADDR_EQUAL(&tmp->gre_oip6.ip6_src, src) &&
		    IN6_ARE_ADDR_EQUAL(&tmp->gre_oip6.ip6_dst, dst))
			return (EADDRNOTAVAIL);
	}
	return (0);
}

static int
in6_gre_lookup(const struct mbuf *m, int off, int proto, void **arg)
{
	const struct ip6_hdr *ip6;
	struct gre_softc *sc;

	if (V_ipv6_hashtbl == NULL)
		return (0);

	MPASS(in_epoch(net_epoch_preempt));
	ip6 = mtod(m, const struct ip6_hdr *);
	CK_LIST_FOREACH(sc, &GRE_HASH(&ip6->ip6_dst, &ip6->ip6_src), chain) {
		/*
		 * This is an inbound packet, its ip6_dst is source address
		 * in softc.
		 */
		if (IN6_ARE_ADDR_EQUAL(&sc->gre_oip6.ip6_src,
		    &ip6->ip6_dst) &&
		    IN6_ARE_ADDR_EQUAL(&sc->gre_oip6.ip6_dst,
		    &ip6->ip6_src)) {
			if ((GRE2IFP(sc)->if_flags & IFF_UP) == 0)
				return (0);
			*arg = sc;
			return (ENCAP_DRV_LOOKUP);
		}
	}
	return (0);
}

/*
 * Check that ingress address belongs to local host.
 */
static void
in6_gre_set_running(struct gre_softc *sc)
{

	if (in6_localip(&sc->gre_oip6.ip6_src))
		GRE2IFP(sc)->if_drv_flags |= IFF_DRV_RUNNING;
	else
		GRE2IFP(sc)->if_drv_flags &= ~IFF_DRV_RUNNING;
}

/*
 * ifaddr_event handler.
 * Clear IFF_DRV_RUNNING flag when ingress address disappears to prevent
 * source address spoofing.
 */
static void
in6_gre_srcaddr(void *arg __unused, const struct sockaddr *sa,
    int event __unused)
{
	const struct sockaddr_in6 *sin;
	struct gre_softc *sc;

	/* Check that VNET is ready */
	if (V_ipv6_hashtbl == NULL)
		return;

	MPASS(in_epoch(net_epoch_preempt));
	sin = (const struct sockaddr_in6 *)sa;
	CK_LIST_FOREACH(sc, &GRE_SRCHASH(&sin->sin6_addr), srchash) {
		if (IN6_ARE_ADDR_EQUAL(&sc->gre_oip6.ip6_src,
		    &sin->sin6_addr) == 0)
			continue;
		in6_gre_set_running(sc);
	}
}

static void
in6_gre_attach(struct gre_softc *sc)
{

	sc->gre_hlen = sizeof(struct greip6);
	sc->gre_oip6.ip6_vfc = IPV6_VERSION;
	sc->gre_oip6.ip6_nxt = IPPROTO_GRE;
	gre_updatehdr(sc, &sc->gre_gi6hdr->gi6_gre);
	CK_LIST_INSERT_HEAD(&GRE_HASH_SC(sc), sc, chain);
	CK_LIST_INSERT_HEAD(&GRE_SRCHASH(&sc->gre_oip6.ip6_src), sc, srchash);
}

void
in6_gre_setopts(struct gre_softc *sc, u_long cmd, uint32_t value)
{

	MPASS(cmd == GRESKEY || cmd == GRESOPTS);

	/* NOTE: we are protected with gre_ioctl_sx lock */
	MPASS(sc->gre_family == AF_INET6);
	CK_LIST_REMOVE(sc, chain);
	CK_LIST_REMOVE(sc, srchash);
	GRE_WAIT();
	if (cmd == GRESKEY)
		sc->gre_key = value;
	else
		sc->gre_options = value;
	in6_gre_attach(sc);
}

int
in6_gre_ioctl(struct gre_softc *sc, u_long cmd, caddr_t data)
{
	struct in6_ifreq *ifr = (struct in6_ifreq *)data;
	struct sockaddr_in6 *dst, *src;
	struct ip6_hdr *ip6;
	int error;

	/* NOTE: we are protected with gre_ioctl_sx lock */
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
			V_ipv6_hashtbl = gre_hashinit();
			V_ipv6_srchashtbl = gre_hashinit();
		}
		error = in6_gre_checkdup(sc, &src->sin6_addr,
		    &dst->sin6_addr);
		if (error == EADDRNOTAVAIL)
			break;
		if (error == EEXIST) {
			/* Addresses are the same. Just return. */
			error = 0;
			break;
		}
		ip6 = malloc(sizeof(struct greip6) + 3 * sizeof(uint32_t),
		    M_GRE, M_WAITOK | M_ZERO);
		ip6->ip6_src = src->sin6_addr;
		ip6->ip6_dst = dst->sin6_addr;
		if (sc->gre_family != 0) {
			/* Detach existing tunnel first */
			CK_LIST_REMOVE(sc, chain);
			CK_LIST_REMOVE(sc, srchash);
			GRE_WAIT();
			free(sc->gre_hdr, M_GRE);
			/* XXX: should we notify about link state change? */
		}
		sc->gre_family = AF_INET6;
		sc->gre_hdr = ip6;
		sc->gre_oseq = 0;
		sc->gre_iseq = UINT32_MAX;
		in6_gre_attach(sc);
		in6_gre_set_running(sc);
		break;
	case SIOCGIFPSRCADDR_IN6:
	case SIOCGIFPDSTADDR_IN6:
		if (sc->gre_family != AF_INET6) {
			error = EADDRNOTAVAIL;
			break;
		}
		src = (struct sockaddr_in6 *)&ifr->ifr_addr;
		memset(src, 0, sizeof(*src));
		src->sin6_family = AF_INET6;
		src->sin6_len = sizeof(*src);
		src->sin6_addr = (cmd == SIOCGIFPSRCADDR_IN6) ?
		    sc->gre_oip6.ip6_src: sc->gre_oip6.ip6_dst;
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
in6_gre_output(struct mbuf *m, int af __unused, int hlen __unused)
{
	struct greip6 *gi6;

	gi6 = mtod(m, struct greip6 *);
	gi6->gi6_ip6.ip6_hlim = V_ip6_gre_hlim;
	return (ip6_output(m, NULL, NULL, IPV6_MINMTU, NULL, NULL, NULL));
}

static const struct srcaddrtab *ipv6_srcaddrtab = NULL;
static const struct encaptab *ecookie = NULL;
static const struct encap_config ipv6_encap_cfg = {
	.proto = IPPROTO_GRE,
	.min_length = sizeof(struct greip6) +
#ifdef INET
	    sizeof(struct ip),
#else
	    sizeof(struct ip6_hdr),
#endif
	.exact_match = ENCAP_DRV_LOOKUP,
	.lookup = in6_gre_lookup,
	.input = gre_input
};

void
in6_gre_init(void)
{

	if (!IS_DEFAULT_VNET(curvnet))
		return;
	ipv6_srcaddrtab = ip6_encap_register_srcaddr(in6_gre_srcaddr,
	    NULL, M_WAITOK);
	ecookie = ip6_encap_attach(&ipv6_encap_cfg, NULL, M_WAITOK);
}

void
in6_gre_uninit(void)
{

	if (IS_DEFAULT_VNET(curvnet)) {
		ip6_encap_detach(ecookie);
		ip6_encap_unregister_srcaddr(ipv6_srcaddrtab);
	}
	if (V_ipv6_hashtbl != NULL) {
		gre_hashdestroy(V_ipv6_hashtbl);
		V_ipv6_hashtbl = NULL;
		GRE_WAIT();
		gre_hashdestroy(V_ipv6_srchashtbl);
	}
}
