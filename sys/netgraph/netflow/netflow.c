/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Alexander V. Chernikov <melifaro@ipfw.ru>
 * Copyright (c) 2004-2005 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 2001-2003 Roman V. Palagin <romanp@unshadow.net>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $SourceForge: netflow.c,v 1.41 2004/09/05 11:41:10 glebius Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet6.h"
#include "opt_route.h"
#include <sys/param.h>
#include <sys/bitstring.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/limits.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <vm/uma.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>

#include <netgraph/netflow/netflow.h>
#include <netgraph/netflow/netflow_v9.h>
#include <netgraph/netflow/ng_netflow.h>

#define	NBUCKETS	(65536)		/* must be power of 2 */

/* This hash is for TCP or UDP packets. */
#define FULL_HASH(addr1, addr2, port1, port2)	\
	(((addr1 ^ (addr1 >> 16) ^ 		\
	htons(addr2 ^ (addr2 >> 16))) ^ 	\
	port1 ^ htons(port2)) &			\
	(NBUCKETS - 1))

/* This hash is for all other IP packets. */
#define ADDR_HASH(addr1, addr2)			\
	((addr1 ^ (addr1 >> 16) ^ 		\
	htons(addr2 ^ (addr2 >> 16))) &		\
	(NBUCKETS - 1))

/* Macros to shorten logical constructions */
/* XXX: priv must exist in namespace */
#define	INACTIVE(fle)	(time_uptime - fle->f.last > priv->nfinfo_inact_t)
#define	AGED(fle)	(time_uptime - fle->f.first > priv->nfinfo_act_t)
#define	ISFREE(fle)	(fle->f.packets == 0)

/*
 * 4 is a magical number: statistically number of 4-packet flows is
 * bigger than 5,6,7...-packet flows by an order of magnitude. Most UDP/ICMP
 * scans are 1 packet (~ 90% of flow cache). TCP scans are 2-packet in case
 * of reachable host and 4-packet otherwise.
 */
#define	SMALL(fle)	(fle->f.packets <= 4)

MALLOC_DEFINE(M_NETFLOW_HASH, "netflow_hash", "NetFlow hash");

static int export_add(item_p, struct flow_entry *);
static int export_send(priv_p, fib_export_p, item_p, int);

static int hash_insert(priv_p, struct flow_hash_entry *, struct flow_rec *,
    int, uint8_t, uint8_t);
#ifdef INET6
static int hash6_insert(priv_p, struct flow_hash_entry *, struct flow6_rec *,
    int, uint8_t, uint8_t);
#endif

static void expire_flow(priv_p, fib_export_p, struct flow_entry *, int);

/*
 * Generate hash for a given flow record.
 *
 * FIB is not used here, because:
 * most VRFS will carry public IPv4 addresses which are unique even
 * without FIB private addresses can overlap, but this is worked out
 * via flow_rec bcmp() containing fib id. In IPv6 world addresses are
 * all globally unique (it's not fully true, there is FC00::/7 for example,
 * but chances of address overlap are MUCH smaller)
 */
static inline uint32_t
ip_hash(struct flow_rec *r)
{

	switch (r->r_ip_p) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		return FULL_HASH(r->r_src.s_addr, r->r_dst.s_addr,
		    r->r_sport, r->r_dport);
	default:
		return ADDR_HASH(r->r_src.s_addr, r->r_dst.s_addr);
	}
}

#ifdef INET6
/* Generate hash for a given flow6 record. Use lower 4 octets from v6 addresses */
static inline uint32_t
ip6_hash(struct flow6_rec *r)
{

	switch (r->r_ip_p) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		return FULL_HASH(r->src.r_src6.__u6_addr.__u6_addr32[3],
		    r->dst.r_dst6.__u6_addr.__u6_addr32[3], r->r_sport,
		    r->r_dport);
	default:
		return ADDR_HASH(r->src.r_src6.__u6_addr.__u6_addr32[3],
		    r->dst.r_dst6.__u6_addr.__u6_addr32[3]);
 	}
}

static inline int
ip6_masklen(struct in6_addr *saddr, struct rt_addrinfo *info)
{
	const int nbits = sizeof(*saddr) * NBBY;
	int mlen;

	if (info->rti_addrs & RTA_NETMASK)
		bit_count((bitstr_t *)saddr, 0, nbits, &mlen);
	else
		mlen = nbits;
	return (mlen);
}
#endif

/*
 * Detach export datagram from priv, if there is any.
 * If there is no, allocate a new one.
 */
static item_p
get_export_dgram(priv_p priv, fib_export_p fe)
{
	item_p	item = NULL;

	mtx_lock(&fe->export_mtx);
	if (fe->exp.item != NULL) {
		item = fe->exp.item;
		fe->exp.item = NULL;
	}
	mtx_unlock(&fe->export_mtx);

	if (item == NULL) {
		struct netflow_v5_export_dgram *dgram;
		struct mbuf *m;

		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL)
			return (NULL);
		item = ng_package_data(m, NG_NOFLAGS);
		if (item == NULL)
			return (NULL);
		dgram = mtod(m, struct netflow_v5_export_dgram *);
		dgram->header.count = 0;
		dgram->header.version = htons(NETFLOW_V5);
		dgram->header.pad = 0;
	}

	return (item);
}

/*
 * Re-attach incomplete datagram back to priv.
 * If there is already another one, then send incomplete. */
static void
return_export_dgram(priv_p priv, fib_export_p fe, item_p item, int flags)
{

	/*
	 * It may happen on SMP, that some thread has already
	 * put its item there, in this case we bail out and
	 * send what we have to collector.
	 */
	mtx_lock(&fe->export_mtx);
	if (fe->exp.item == NULL) {
		fe->exp.item = item;
		mtx_unlock(&fe->export_mtx);
	} else {
		mtx_unlock(&fe->export_mtx);
		export_send(priv, fe, item, flags);
	}
}

/*
 * The flow is over. Call export_add() and free it. If datagram is
 * full, then call export_send().
 */
static void
expire_flow(priv_p priv, fib_export_p fe, struct flow_entry *fle, int flags)
{
	struct netflow_export_item exp;
	uint16_t version = fle->f.version;

	if ((priv->export != NULL) && (version == IPVERSION)) {
		exp.item = get_export_dgram(priv, fe);
		if (exp.item == NULL) {
			priv->nfinfo_export_failed++;
			if (priv->export9 != NULL)
				priv->nfinfo_export9_failed++;
			/* fle definitely contains IPv4 flow. */
			uma_zfree_arg(priv->zone, fle, priv);
			return;
		}

		if (export_add(exp.item, fle) > 0)
			export_send(priv, fe, exp.item, flags);
		else
			return_export_dgram(priv, fe, exp.item, NG_QUEUE);
	}

	if (priv->export9 != NULL) {
		exp.item9 = get_export9_dgram(priv, fe, &exp.item9_opt);
		if (exp.item9 == NULL) {
			priv->nfinfo_export9_failed++;
			if (version == IPVERSION)
				uma_zfree_arg(priv->zone, fle, priv);
#ifdef INET6
			else if (version == IP6VERSION)
				uma_zfree_arg(priv->zone6, fle, priv);
#endif
			else
				panic("ng_netflow: Unknown IP proto: %d",
				    version);
			return;
		}

		if (export9_add(exp.item9, exp.item9_opt, fle) > 0)
			export9_send(priv, fe, exp.item9, exp.item9_opt, flags);
		else
			return_export9_dgram(priv, fe, exp.item9,
			    exp.item9_opt, NG_QUEUE);
	}

	if (version == IPVERSION)
		uma_zfree_arg(priv->zone, fle, priv);
#ifdef INET6
	else if (version == IP6VERSION)
		uma_zfree_arg(priv->zone6, fle, priv);
#endif
}

/* Get a snapshot of node statistics */
void
ng_netflow_copyinfo(priv_p priv, struct ng_netflow_info *i)
{

	i->nfinfo_bytes = counter_u64_fetch(priv->nfinfo_bytes);
	i->nfinfo_packets = counter_u64_fetch(priv->nfinfo_packets);
	i->nfinfo_bytes6 = counter_u64_fetch(priv->nfinfo_bytes6);
	i->nfinfo_packets6 = counter_u64_fetch(priv->nfinfo_packets6);
	i->nfinfo_sbytes = counter_u64_fetch(priv->nfinfo_sbytes);
	i->nfinfo_spackets = counter_u64_fetch(priv->nfinfo_spackets);
	i->nfinfo_sbytes6 = counter_u64_fetch(priv->nfinfo_sbytes6);
	i->nfinfo_spackets6 = counter_u64_fetch(priv->nfinfo_spackets6);
	i->nfinfo_act_exp = counter_u64_fetch(priv->nfinfo_act_exp);
	i->nfinfo_inact_exp = counter_u64_fetch(priv->nfinfo_inact_exp);

	i->nfinfo_used = uma_zone_get_cur(priv->zone);
#ifdef INET6
	i->nfinfo_used6 = uma_zone_get_cur(priv->zone6);
#endif

	i->nfinfo_alloc_failed = priv->nfinfo_alloc_failed;
	i->nfinfo_export_failed = priv->nfinfo_export_failed;
	i->nfinfo_export9_failed = priv->nfinfo_export9_failed;
	i->nfinfo_realloc_mbuf = priv->nfinfo_realloc_mbuf;
	i->nfinfo_alloc_fibs = priv->nfinfo_alloc_fibs;
	i->nfinfo_inact_t = priv->nfinfo_inact_t;
	i->nfinfo_act_t = priv->nfinfo_act_t;
}

/*
 * Insert a record into defined slot.
 *
 * First we get for us a free flow entry, then fill in all
 * possible fields in it.
 *
 * TODO: consider dropping hash mutex while filling in datagram,
 * as this was done in previous version. Need to test & profile
 * to be sure.
 */
static int
hash_insert(priv_p priv, struct flow_hash_entry *hsh, struct flow_rec *r,
	int plen, uint8_t flags, uint8_t tcp_flags)
{
	struct flow_entry *fle;
	struct sockaddr_in sin, sin_mask;
	struct sockaddr_dl rt_gateway;
	struct rt_addrinfo info;

	mtx_assert(&hsh->mtx, MA_OWNED);

	fle = uma_zalloc_arg(priv->zone, priv, M_NOWAIT);
	if (fle == NULL) {
		priv->nfinfo_alloc_failed++;
		return (ENOMEM);
	}

	/*
	 * Now fle is totally ours. It is detached from all lists,
	 * we can safely edit it.
	 */
	fle->f.version = IPVERSION;
	bcopy(r, &fle->f.r, sizeof(struct flow_rec));
	fle->f.bytes = plen;
	fle->f.packets = 1;
	fle->f.tcp_flags = tcp_flags;

	fle->f.first = fle->f.last = time_uptime;

	/*
	 * First we do route table lookup on destination address. So we can
	 * fill in out_ifx, dst_mask, nexthop, and dst_as in future releases.
	 */
	if ((flags & NG_NETFLOW_CONF_NODSTLOOKUP) == 0) {
		bzero(&sin, sizeof(sin));
		sin.sin_len = sizeof(struct sockaddr_in);
		sin.sin_family = AF_INET;
		sin.sin_addr = fle->f.r.r_dst;

		rt_gateway.sdl_len = sizeof(rt_gateway);
		sin_mask.sin_len = sizeof(struct sockaddr_in);
		bzero(&info, sizeof(info));

		info.rti_info[RTAX_GATEWAY] = (struct sockaddr *)&rt_gateway;
		info.rti_info[RTAX_NETMASK] = (struct sockaddr *)&sin_mask;

		if (rib_lookup_info(r->fib, (struct sockaddr *)&sin, NHR_REF, 0,
		    &info) == 0) {
			fle->f.fle_o_ifx = info.rti_ifp->if_index;

			if (info.rti_flags & RTF_GATEWAY &&
			    rt_gateway.sdl_family == AF_INET)
				fle->f.next_hop =
				    ((struct sockaddr_in *)&rt_gateway)->sin_addr;

			if (info.rti_addrs & RTA_NETMASK)
				fle->f.dst_mask = bitcount32(sin_mask.sin_addr.s_addr);
			else if (info.rti_flags & RTF_HOST)
				/* Give up. We can't determine mask :( */
				fle->f.dst_mask = 32;

			rib_free_info(&info);
		}
	}

	/* Do route lookup on source address, to fill in src_mask. */
	if ((flags & NG_NETFLOW_CONF_NOSRCLOOKUP) == 0) {
		bzero(&sin, sizeof(sin));
		sin.sin_len = sizeof(struct sockaddr_in);
		sin.sin_family = AF_INET;
		sin.sin_addr = fle->f.r.r_src;

		sin_mask.sin_len = sizeof(struct sockaddr_in);
		bzero(&info, sizeof(info));

		info.rti_info[RTAX_NETMASK] = (struct sockaddr *)&sin_mask;

		if (rib_lookup_info(r->fib, (struct sockaddr *)&sin, 0, 0,
		    &info) == 0) {
			if (info.rti_addrs & RTA_NETMASK)
				fle->f.src_mask =
				    bitcount32(sin_mask.sin_addr.s_addr);
			else if (info.rti_flags & RTF_HOST)
				/* Give up. We can't determine mask :( */
				fle->f.src_mask = 32;
		}
	}

	/* Push new flow at the and of hash. */
	TAILQ_INSERT_TAIL(&hsh->head, fle, fle_hash);

	return (0);
}

#ifdef INET6
static int
hash6_insert(priv_p priv, struct flow_hash_entry *hsh6, struct flow6_rec *r,
	int plen, uint8_t flags, uint8_t tcp_flags)
{
	struct flow6_entry *fle6;
	struct sockaddr_in6 sin6, sin6_mask;
	struct sockaddr_dl rt_gateway;
	struct rt_addrinfo info;

	mtx_assert(&hsh6->mtx, MA_OWNED);

	fle6 = uma_zalloc_arg(priv->zone6, priv, M_NOWAIT);
	if (fle6 == NULL) {
		priv->nfinfo_alloc_failed++;
		return (ENOMEM);
	}

	/*
	 * Now fle is totally ours. It is detached from all lists,
	 * we can safely edit it.
	 */

	fle6->f.version = IP6VERSION;
	bcopy(r, &fle6->f.r, sizeof(struct flow6_rec));
	fle6->f.bytes = plen;
	fle6->f.packets = 1;
	fle6->f.tcp_flags = tcp_flags;

	fle6->f.first = fle6->f.last = time_uptime;

	/*
	 * First we do route table lookup on destination address. So we can
	 * fill in out_ifx, dst_mask, nexthop, and dst_as in future releases.
	 */
	if ((flags & NG_NETFLOW_CONF_NODSTLOOKUP) == 0) {
		bzero(&sin6, sizeof(struct sockaddr_in6));
		sin6.sin6_len = sizeof(struct sockaddr_in6);
		sin6.sin6_family = AF_INET6;
		sin6.sin6_addr = r->dst.r_dst6;

		rt_gateway.sdl_len = sizeof(rt_gateway);
		sin6_mask.sin6_len = sizeof(struct sockaddr_in6);
		bzero(&info, sizeof(info));

		info.rti_info[RTAX_GATEWAY] = (struct sockaddr *)&rt_gateway;
		info.rti_info[RTAX_NETMASK] = (struct sockaddr *)&sin6_mask;

		if (rib_lookup_info(r->fib, (struct sockaddr *)&sin6, NHR_REF,
		    0, &info) == 0) {
			fle6->f.fle_o_ifx = info.rti_ifp->if_index;

			if (info.rti_flags & RTF_GATEWAY &&
			    rt_gateway.sdl_family == AF_INET6)
				fle6->f.n.next_hop6 =
				    ((struct sockaddr_in6 *)&rt_gateway)->sin6_addr;

			fle6->f.dst_mask =
			    ip6_masklen(&sin6_mask.sin6_addr, &info);

			rib_free_info(&info);
		}
	}

	if ((flags & NG_NETFLOW_CONF_NOSRCLOOKUP) == 0) {
		/* Do route lookup on source address, to fill in src_mask. */
		bzero(&sin6, sizeof(struct sockaddr_in6));
		sin6.sin6_len = sizeof(struct sockaddr_in6);
		sin6.sin6_family = AF_INET6;
		sin6.sin6_addr = r->src.r_src6;

		sin6_mask.sin6_len = sizeof(struct sockaddr_in6);
		bzero(&info, sizeof(info));

		info.rti_info[RTAX_NETMASK] = (struct sockaddr *)&sin6_mask;

		if (rib_lookup_info(r->fib, (struct sockaddr *)&sin6, 0, 0,
		    &info) == 0)
			fle6->f.src_mask =
			    ip6_masklen(&sin6_mask.sin6_addr, &info);
	}

	/* Push new flow at the and of hash. */
	TAILQ_INSERT_TAIL(&hsh6->head, (struct flow_entry *)fle6, fle_hash);

	return (0);
}
#endif


/*
 * Non-static functions called from ng_netflow.c
 */

/* Allocate memory and set up flow cache */
void
ng_netflow_cache_init(priv_p priv)
{
	struct flow_hash_entry *hsh;
	int i;

	/* Initialize cache UMA zone. */
	priv->zone = uma_zcreate("NetFlow IPv4 cache",
	    sizeof(struct flow_entry), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_CACHE, 0);
	uma_zone_set_max(priv->zone, CACHESIZE);
#ifdef INET6	
	priv->zone6 = uma_zcreate("NetFlow IPv6 cache",
	    sizeof(struct flow6_entry), NULL, NULL, NULL, NULL,
	    UMA_ALIGN_CACHE, 0);
	uma_zone_set_max(priv->zone6, CACHESIZE);
#endif	

	/* Allocate hash. */
	priv->hash = malloc(NBUCKETS * sizeof(struct flow_hash_entry),
	    M_NETFLOW_HASH, M_WAITOK | M_ZERO);

	/* Initialize hash. */
	for (i = 0, hsh = priv->hash; i < NBUCKETS; i++, hsh++) {
		mtx_init(&hsh->mtx, "hash mutex", NULL, MTX_DEF);
		TAILQ_INIT(&hsh->head);
	}

#ifdef INET6
	/* Allocate hash. */
	priv->hash6 = malloc(NBUCKETS * sizeof(struct flow_hash_entry),
	    M_NETFLOW_HASH, M_WAITOK | M_ZERO);

	/* Initialize hash. */
	for (i = 0, hsh = priv->hash6; i < NBUCKETS; i++, hsh++) {
		mtx_init(&hsh->mtx, "hash mutex", NULL, MTX_DEF);
		TAILQ_INIT(&hsh->head);
	}
#endif

	priv->nfinfo_bytes = counter_u64_alloc(M_WAITOK);
	priv->nfinfo_packets = counter_u64_alloc(M_WAITOK);
	priv->nfinfo_bytes6 = counter_u64_alloc(M_WAITOK);
	priv->nfinfo_packets6 = counter_u64_alloc(M_WAITOK);
	priv->nfinfo_sbytes = counter_u64_alloc(M_WAITOK);
	priv->nfinfo_spackets = counter_u64_alloc(M_WAITOK);
	priv->nfinfo_sbytes6 = counter_u64_alloc(M_WAITOK);
	priv->nfinfo_spackets6 = counter_u64_alloc(M_WAITOK);
	priv->nfinfo_act_exp = counter_u64_alloc(M_WAITOK);
	priv->nfinfo_inact_exp = counter_u64_alloc(M_WAITOK);

	ng_netflow_v9_cache_init(priv);
	CTR0(KTR_NET, "ng_netflow startup()");
}

/* Initialize new FIB table for v5 and v9 */
int
ng_netflow_fib_init(priv_p priv, int fib)
{
	fib_export_p	fe = priv_to_fib(priv, fib);

	CTR1(KTR_NET, "ng_netflow(): fib init: %d", fib);

	if (fe != NULL)
		return (0);

	if ((fe = malloc(sizeof(struct fib_export), M_NETGRAPH,
	    M_NOWAIT | M_ZERO)) == NULL)
		return (ENOMEM);

	mtx_init(&fe->export_mtx, "export dgram lock", NULL, MTX_DEF);
	mtx_init(&fe->export9_mtx, "export9 dgram lock", NULL, MTX_DEF);
	fe->fib = fib;
	fe->domain_id = fib;

	if (atomic_cmpset_ptr((volatile uintptr_t *)&priv->fib_data[fib],
	    (uintptr_t)NULL, (uintptr_t)fe) == 0) {
		/* FIB already set up by other ISR */
		CTR3(KTR_NET, "ng_netflow(): fib init: %d setup %p but got %p",
		    fib, fe, priv_to_fib(priv, fib));
		mtx_destroy(&fe->export_mtx);
		mtx_destroy(&fe->export9_mtx);
		free(fe, M_NETGRAPH);
	} else {
		/* Increase counter for statistics */
		CTR3(KTR_NET, "ng_netflow(): fib %d setup to %p (%p)",
		    fib, fe, priv_to_fib(priv, fib));
		priv->nfinfo_alloc_fibs++;
	}
	
	return (0);
}

/* Free all flow cache memory. Called from node close method. */
void
ng_netflow_cache_flush(priv_p priv)
{
	struct flow_entry	*fle, *fle1;
	struct flow_hash_entry	*hsh;
	struct netflow_export_item exp;
	fib_export_p fe;
	int i;

	bzero(&exp, sizeof(exp));

	/*
	 * We are going to free probably billable data.
	 * Expire everything before freeing it.
	 * No locking is required since callout is already drained.
	 */
	for (hsh = priv->hash, i = 0; i < NBUCKETS; hsh++, i++)
		TAILQ_FOREACH_SAFE(fle, &hsh->head, fle_hash, fle1) {
			TAILQ_REMOVE(&hsh->head, fle, fle_hash);
			fe = priv_to_fib(priv, fle->f.r.fib);
			expire_flow(priv, fe, fle, NG_QUEUE);
		}
#ifdef INET6
	for (hsh = priv->hash6, i = 0; i < NBUCKETS; hsh++, i++)
		TAILQ_FOREACH_SAFE(fle, &hsh->head, fle_hash, fle1) {
			TAILQ_REMOVE(&hsh->head, fle, fle_hash);
			fe = priv_to_fib(priv, fle->f.r.fib);
			expire_flow(priv, fe, fle, NG_QUEUE);
		}
#endif

	uma_zdestroy(priv->zone);
	/* Destroy hash mutexes. */
	for (i = 0, hsh = priv->hash; i < NBUCKETS; i++, hsh++)
		mtx_destroy(&hsh->mtx);

	/* Free hash memory. */
	if (priv->hash != NULL)
		free(priv->hash, M_NETFLOW_HASH);
#ifdef INET6
	uma_zdestroy(priv->zone6);
	/* Destroy hash mutexes. */
	for (i = 0, hsh = priv->hash6; i < NBUCKETS; i++, hsh++)
		mtx_destroy(&hsh->mtx);

	/* Free hash memory. */
	if (priv->hash6 != NULL)
		free(priv->hash6, M_NETFLOW_HASH);
#endif

	for (i = 0; i < priv->maxfibs; i++) {
		if ((fe = priv_to_fib(priv, i)) == NULL)
			continue;

		if (fe->exp.item != NULL)
			export_send(priv, fe, fe->exp.item, NG_QUEUE);

		if (fe->exp.item9 != NULL)
			export9_send(priv, fe, fe->exp.item9,
			    fe->exp.item9_opt, NG_QUEUE);

		mtx_destroy(&fe->export_mtx);
		mtx_destroy(&fe->export9_mtx);
		free(fe, M_NETGRAPH);
	}

	counter_u64_free(priv->nfinfo_bytes);
	counter_u64_free(priv->nfinfo_packets);
	counter_u64_free(priv->nfinfo_bytes6);
	counter_u64_free(priv->nfinfo_packets6);
	counter_u64_free(priv->nfinfo_sbytes);
	counter_u64_free(priv->nfinfo_spackets);
	counter_u64_free(priv->nfinfo_sbytes6);
	counter_u64_free(priv->nfinfo_spackets6);
	counter_u64_free(priv->nfinfo_act_exp);
	counter_u64_free(priv->nfinfo_inact_exp);

	ng_netflow_v9_cache_flush(priv);
}

/* Insert packet from into flow cache. */
int
ng_netflow_flow_add(priv_p priv, fib_export_p fe, struct ip *ip,
    caddr_t upper_ptr, uint8_t upper_proto, uint8_t flags,
    unsigned int src_if_index)
{
	struct flow_entry	*fle, *fle1;
	struct flow_hash_entry	*hsh;
	struct flow_rec		r;
	int			hlen, plen;
	int			error = 0;
	uint16_t		eproto;
	uint8_t			tcp_flags = 0;

	bzero(&r, sizeof(r));

	if (ip->ip_v != IPVERSION)
		return (EINVAL);

	hlen = ip->ip_hl << 2;
	if (hlen < sizeof(struct ip))
		return (EINVAL);

	eproto = ETHERTYPE_IP;
	/* Assume L4 template by default */
	r.flow_type = NETFLOW_V9_FLOW_V4_L4;

	r.r_src = ip->ip_src;
	r.r_dst = ip->ip_dst;
	r.fib = fe->fib;

	plen = ntohs(ip->ip_len);

	r.r_ip_p = ip->ip_p;
	r.r_tos = ip->ip_tos;

	r.r_i_ifx = src_if_index;

	/*
	 * XXX NOTE: only first fragment of fragmented TCP, UDP and
	 * ICMP packet will be recorded with proper s_port and d_port.
	 * Following fragments will be recorded simply as IP packet with
	 * ip_proto = ip->ip_p and s_port, d_port set to zero.
	 * I know, it looks like bug. But I don't want to re-implement
	 * ip packet assebmling here. Anyway, (in)famous trafd works this way -
	 * and nobody complains yet :)
	 */
	if ((ip->ip_off & htons(IP_OFFMASK)) == 0)
		switch(r.r_ip_p) {
		case IPPROTO_TCP:
		    {
			struct tcphdr *tcp;

			tcp = (struct tcphdr *)((caddr_t )ip + hlen);
			r.r_sport = tcp->th_sport;
			r.r_dport = tcp->th_dport;
			tcp_flags = tcp->th_flags;
			break;
		    }
		case IPPROTO_UDP:
			r.r_ports = *(uint32_t *)((caddr_t )ip + hlen);
			break;
		}

	counter_u64_add(priv->nfinfo_packets, 1);
	counter_u64_add(priv->nfinfo_bytes, plen);

	/* Find hash slot. */
	hsh = &priv->hash[ip_hash(&r)];

	mtx_lock(&hsh->mtx);

	/*
	 * Go through hash and find our entry. If we encounter an
	 * entry, that should be expired, purge it. We do a reverse
	 * search since most active entries are first, and most
	 * searches are done on most active entries.
	 */
	TAILQ_FOREACH_REVERSE_SAFE(fle, &hsh->head, fhead, fle_hash, fle1) {
		if (bcmp(&r, &fle->f.r, sizeof(struct flow_rec)) == 0)
			break;
		if ((INACTIVE(fle) && SMALL(fle)) || AGED(fle)) {
			TAILQ_REMOVE(&hsh->head, fle, fle_hash);
			expire_flow(priv, priv_to_fib(priv, fle->f.r.fib),
			    fle, NG_QUEUE);
			counter_u64_add(priv->nfinfo_act_exp, 1);
		}
	}

	if (fle) {			/* An existent entry. */

		fle->f.bytes += plen;
		fle->f.packets ++;
		fle->f.tcp_flags |= tcp_flags;
		fle->f.last = time_uptime;

		/*
		 * We have the following reasons to expire flow in active way:
		 * - it hit active timeout
		 * - a TCP connection closed
		 * - it is going to overflow counter
		 */
		if (tcp_flags & TH_FIN || tcp_flags & TH_RST || AGED(fle) ||
		    (fle->f.bytes >= (CNTR_MAX - IF_MAXMTU)) ) {
			TAILQ_REMOVE(&hsh->head, fle, fle_hash);
			expire_flow(priv, priv_to_fib(priv, fle->f.r.fib),
			    fle, NG_QUEUE);
			counter_u64_add(priv->nfinfo_act_exp, 1);
		} else {
			/*
			 * It is the newest, move it to the tail,
			 * if it isn't there already. Next search will
			 * locate it quicker.
			 */
			if (fle != TAILQ_LAST(&hsh->head, fhead)) {
				TAILQ_REMOVE(&hsh->head, fle, fle_hash);
				TAILQ_INSERT_TAIL(&hsh->head, fle, fle_hash);
			}
		}
	} else				/* A new flow entry. */
		error = hash_insert(priv, hsh, &r, plen, flags, tcp_flags);

	mtx_unlock(&hsh->mtx);

	return (error);
}

#ifdef INET6
/* Insert IPv6 packet from into flow cache. */
int
ng_netflow_flow6_add(priv_p priv, fib_export_p fe, struct ip6_hdr *ip6,
    caddr_t upper_ptr, uint8_t upper_proto, uint8_t flags,
    unsigned int src_if_index)
{
	struct flow_entry	*fle = NULL, *fle1;
	struct flow6_entry	*fle6;
	struct flow_hash_entry	*hsh;
	struct flow6_rec	r;
	int			plen;
	int			error = 0;
	uint8_t			tcp_flags = 0;

	/* check version */
	if ((ip6->ip6_vfc & IPV6_VERSION_MASK) != IPV6_VERSION)
		return (EINVAL);

	bzero(&r, sizeof(r));

	r.src.r_src6 = ip6->ip6_src;
	r.dst.r_dst6 = ip6->ip6_dst;
	r.fib = fe->fib;

	/* Assume L4 template by default */
	r.flow_type = NETFLOW_V9_FLOW_V6_L4;

	plen = ntohs(ip6->ip6_plen) + sizeof(struct ip6_hdr);

#if 0
	/* XXX: set DSCP/CoS value */
	r.r_tos = ip->ip_tos;
#endif
	if ((flags & NG_NETFLOW_IS_FRAG) == 0) {
		switch(upper_proto) {
		case IPPROTO_TCP:
		    {
			struct tcphdr *tcp;

			tcp = (struct tcphdr *)upper_ptr;
			r.r_ports = *(uint32_t *)upper_ptr;
			tcp_flags = tcp->th_flags;
			break;
		    }
 		case IPPROTO_UDP:
		case IPPROTO_SCTP:
			r.r_ports = *(uint32_t *)upper_ptr;
			break;
		}
	}	

	r.r_ip_p = upper_proto;
	r.r_i_ifx = src_if_index;
 
	counter_u64_add(priv->nfinfo_packets6, 1);
	counter_u64_add(priv->nfinfo_bytes6, plen);

	/* Find hash slot. */
	hsh = &priv->hash6[ip6_hash(&r)];

	mtx_lock(&hsh->mtx);

	/*
	 * Go through hash and find our entry. If we encounter an
	 * entry, that should be expired, purge it. We do a reverse
	 * search since most active entries are first, and most
	 * searches are done on most active entries.
	 */
	TAILQ_FOREACH_REVERSE_SAFE(fle, &hsh->head, fhead, fle_hash, fle1) {
		if (fle->f.version != IP6VERSION)
			continue;
		fle6 = (struct flow6_entry *)fle;
		if (bcmp(&r, &fle6->f.r, sizeof(struct flow6_rec)) == 0)
			break;
		if ((INACTIVE(fle6) && SMALL(fle6)) || AGED(fle6)) {
			TAILQ_REMOVE(&hsh->head, fle, fle_hash);
			expire_flow(priv, priv_to_fib(priv, fle->f.r.fib), fle,
			    NG_QUEUE);
			counter_u64_add(priv->nfinfo_act_exp, 1);
		}
	}

	if (fle != NULL) {			/* An existent entry. */
		fle6 = (struct flow6_entry *)fle;

		fle6->f.bytes += plen;
		fle6->f.packets ++;
		fle6->f.tcp_flags |= tcp_flags;
		fle6->f.last = time_uptime;

		/*
		 * We have the following reasons to expire flow in active way:
		 * - it hit active timeout
		 * - a TCP connection closed
		 * - it is going to overflow counter
		 */
		if (tcp_flags & TH_FIN || tcp_flags & TH_RST || AGED(fle6) ||
		    (fle6->f.bytes >= (CNTR_MAX - IF_MAXMTU)) ) {
			TAILQ_REMOVE(&hsh->head, fle, fle_hash);
			expire_flow(priv, priv_to_fib(priv, fle->f.r.fib), fle,
			    NG_QUEUE);
			counter_u64_add(priv->nfinfo_act_exp, 1);
		} else {
			/*
			 * It is the newest, move it to the tail,
			 * if it isn't there already. Next search will
			 * locate it quicker.
			 */
			if (fle != TAILQ_LAST(&hsh->head, fhead)) {
				TAILQ_REMOVE(&hsh->head, fle, fle_hash);
				TAILQ_INSERT_TAIL(&hsh->head, fle, fle_hash);
			}
		}
	} else				/* A new flow entry. */
		error = hash6_insert(priv, hsh, &r, plen, flags, tcp_flags);

	mtx_unlock(&hsh->mtx);

	return (error);
}
#endif

/*
 * Return records from cache to userland.
 *
 * TODO: matching particular IP should be done in kernel, here.
 */
int
ng_netflow_flow_show(priv_p priv, struct ngnf_show_header *req,
struct ngnf_show_header *resp)
{
	struct flow_hash_entry	*hsh;
	struct flow_entry	*fle;
	struct flow_entry_data	*data = (struct flow_entry_data *)(resp + 1);
#ifdef INET6
	struct flow6_entry_data	*data6 = (struct flow6_entry_data *)(resp + 1);
#endif
	int	i, max;

	i = req->hash_id;
	if (i > NBUCKETS-1)
		return (EINVAL);

#ifdef INET6
	if (req->version == 6) {
		resp->version = 6;
		hsh = priv->hash6 + i;
		max = NREC6_AT_ONCE;
	} else
#endif
	if (req->version == 4) {
		resp->version = 4;
		hsh = priv->hash + i;
		max = NREC_AT_ONCE;
	} else
		return (EINVAL);

	/*
	 * We will transfer not more than NREC_AT_ONCE. More data
	 * will come in next message.
	 * We send current hash index and current record number in list 
	 * to userland, and userland should return it back to us. 
	 * Then, we will restart with new entry.
	 *
	 * The resulting cache snapshot can be inaccurate if flow expiration
	 * is taking place on hash item between userland data requests for 
	 * this hash item id.
	 */
	resp->nentries = 0;
	for (; i < NBUCKETS; hsh++, i++) {
		int list_id;

		if (mtx_trylock(&hsh->mtx) == 0) {
			/* 
			 * Requested hash index is not available,
			 * relay decision to skip or re-request data
			 * to userland.
			 */
			resp->hash_id = i;
			resp->list_id = 0;
			return (0);
		}

		list_id = 0;
		TAILQ_FOREACH(fle, &hsh->head, fle_hash) {
			if (hsh->mtx.mtx_lock & MTX_CONTESTED) {
				resp->hash_id = i;
				resp->list_id = list_id;
				mtx_unlock(&hsh->mtx);
				return (0);
			}

			list_id++;
			/* Search for particular record in list. */
			if (req->list_id > 0) {
				if (list_id < req->list_id)
					continue;

				/* Requested list position found. */
				req->list_id = 0;
			}
#ifdef INET6
			if (req->version == 6) {
				struct flow6_entry *fle6;

				fle6 = (struct flow6_entry *)fle;
				bcopy(&fle6->f, data6 + resp->nentries,
				    sizeof(fle6->f));
			} else
#endif
				bcopy(&fle->f, data + resp->nentries,
				    sizeof(fle->f));
			resp->nentries++;
			if (resp->nentries == max) {
				resp->hash_id = i;
				/* 
				 * If it was the last item in list
				 * we simply skip to next hash_id.
				 */
				resp->list_id = list_id + 1;
				mtx_unlock(&hsh->mtx);
				return (0);
			}
		}
		mtx_unlock(&hsh->mtx);
	}

	resp->hash_id = resp->list_id = 0;

	return (0);
}

/* We have full datagram in privdata. Send it to export hook. */
static int
export_send(priv_p priv, fib_export_p fe, item_p item, int flags)
{
	struct mbuf *m = NGI_M(item);
	struct netflow_v5_export_dgram *dgram = mtod(m,
					struct netflow_v5_export_dgram *);
	struct netflow_v5_header *header = &dgram->header;
	struct timespec ts;
	int error = 0;

	/* Fill mbuf header. */
	m->m_len = m->m_pkthdr.len = sizeof(struct netflow_v5_record) *
	   header->count + sizeof(struct netflow_v5_header);

	/* Fill export header. */
	header->sys_uptime = htonl(MILLIUPTIME(time_uptime));
	getnanotime(&ts);
	header->unix_secs  = htonl(ts.tv_sec);
	header->unix_nsecs = htonl(ts.tv_nsec);
	header->engine_type = 0;
	header->engine_id = fe->domain_id;
	header->pad = 0;
	header->flow_seq = htonl(atomic_fetchadd_32(&fe->flow_seq,
	    header->count));
	header->count = htons(header->count);

	if (priv->export != NULL)
		NG_FWD_ITEM_HOOK_FLAGS(error, item, priv->export, flags);
	else
		NG_FREE_ITEM(item);

	return (error);
}


/* Add export record to dgram. */
static int
export_add(item_p item, struct flow_entry *fle)
{
	struct netflow_v5_export_dgram *dgram = mtod(NGI_M(item),
					struct netflow_v5_export_dgram *);
	struct netflow_v5_header *header = &dgram->header;
	struct netflow_v5_record *rec;

	rec = &dgram->r[header->count];
	header->count ++;

	KASSERT(header->count <= NETFLOW_V5_MAX_RECORDS,
	    ("ng_netflow: export too big"));

	/* Fill in export record. */
	rec->src_addr = fle->f.r.r_src.s_addr;
	rec->dst_addr = fle->f.r.r_dst.s_addr;
	rec->next_hop = fle->f.next_hop.s_addr;
	rec->i_ifx    = htons(fle->f.fle_i_ifx);
	rec->o_ifx    = htons(fle->f.fle_o_ifx);
	rec->packets  = htonl(fle->f.packets);
	rec->octets   = htonl(fle->f.bytes);
	rec->first    = htonl(MILLIUPTIME(fle->f.first));
	rec->last     = htonl(MILLIUPTIME(fle->f.last));
	rec->s_port   = fle->f.r.r_sport;
	rec->d_port   = fle->f.r.r_dport;
	rec->flags    = fle->f.tcp_flags;
	rec->prot     = fle->f.r.r_ip_p;
	rec->tos      = fle->f.r.r_tos;
	rec->dst_mask = fle->f.dst_mask;
	rec->src_mask = fle->f.src_mask;
	rec->pad1     = 0;
	rec->pad2     = 0;

	/* Not supported fields. */
	rec->src_as = rec->dst_as = 0;

	if (header->count == NETFLOW_V5_MAX_RECORDS)
		return (1); /* end of datagram */
	else
		return (0);	
}

/* Periodic flow expiry run. */
void
ng_netflow_expire(void *arg)
{
	struct flow_entry	*fle, *fle1;
	struct flow_hash_entry	*hsh;
	priv_p			priv = (priv_p )arg;
	int			used, i;

	/*
	 * Going through all the cache.
	 */
	used = uma_zone_get_cur(priv->zone);
	for (hsh = priv->hash, i = 0; i < NBUCKETS; hsh++, i++) {
		/*
		 * Skip entries, that are already being worked on.
		 */
		if (mtx_trylock(&hsh->mtx) == 0)
			continue;

		TAILQ_FOREACH_SAFE(fle, &hsh->head, fle_hash, fle1) {
			/*
			 * Interrupt thread wants this entry!
			 * Quick! Quick! Bail out!
			 */
			if (hsh->mtx.mtx_lock & MTX_CONTESTED)
				break;

			/*
			 * Don't expire aggressively while hash collision
			 * ratio is predicted small.
			 */
			if (used <= (NBUCKETS*2) && !INACTIVE(fle))
				break;

			if ((INACTIVE(fle) && (SMALL(fle) ||
			    (used > (NBUCKETS*2)))) || AGED(fle)) {
				TAILQ_REMOVE(&hsh->head, fle, fle_hash);
				expire_flow(priv, priv_to_fib(priv,
				    fle->f.r.fib), fle, NG_NOFLAGS);
				used--;
				counter_u64_add(priv->nfinfo_inact_exp, 1);
			}
		}
		mtx_unlock(&hsh->mtx);
	}

#ifdef INET6
	used = uma_zone_get_cur(priv->zone6);
	for (hsh = priv->hash6, i = 0; i < NBUCKETS; hsh++, i++) {
		struct flow6_entry	*fle6;

		/*
		 * Skip entries, that are already being worked on.
		 */
		if (mtx_trylock(&hsh->mtx) == 0)
			continue;

		TAILQ_FOREACH_SAFE(fle, &hsh->head, fle_hash, fle1) {
			fle6 = (struct flow6_entry *)fle;
			/*
			 * Interrupt thread wants this entry!
			 * Quick! Quick! Bail out!
			 */
			if (hsh->mtx.mtx_lock & MTX_CONTESTED)
				break;

			/*
			 * Don't expire aggressively while hash collision
			 * ratio is predicted small.
			 */
			if (used <= (NBUCKETS*2) && !INACTIVE(fle6))
				break;

			if ((INACTIVE(fle6) && (SMALL(fle6) ||
			    (used > (NBUCKETS*2)))) || AGED(fle6)) {
				TAILQ_REMOVE(&hsh->head, fle, fle_hash);
				expire_flow(priv, priv_to_fib(priv,
				    fle->f.r.fib), fle, NG_NOFLAGS);
				used--;
				counter_u64_add(priv->nfinfo_inact_exp, 1);
			}
		}
		mtx_unlock(&hsh->mtx);
	}
#endif

	/* Schedule next expire. */
	callout_reset(&priv->exp_callout, (1*hz), &ng_netflow_expire,
	    (void *)priv);
}
