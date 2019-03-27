/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2003 Cedric Berger
 * Copyright (c) 2005 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 2005 Ryan McBride <mcbride@openbsd.org>
 * Copyright (c) 2012 Gleb Smirnoff <glebius@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *	$OpenBSD: pf_if.c,v 1.54 2008/06/14 16:55:28 mk Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/eventhandler.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>
#include <net/pfvar.h>
#include <net/route.h>

VNET_DEFINE(struct pfi_kif *,	 pfi_all);
VNET_DEFINE_STATIC(long, pfi_update);
#define	V_pfi_update	VNET(pfi_update)
#define PFI_BUFFER_MAX	0x10000

VNET_DECLARE(int, pf_vnet_active);
#define V_pf_vnet_active	VNET(pf_vnet_active)

VNET_DEFINE_STATIC(struct pfr_addr *, pfi_buffer);
VNET_DEFINE_STATIC(int, pfi_buffer_cnt);
VNET_DEFINE_STATIC(int,	pfi_buffer_max);
#define	V_pfi_buffer		 VNET(pfi_buffer)
#define	V_pfi_buffer_cnt	 VNET(pfi_buffer_cnt)
#define	V_pfi_buffer_max	 VNET(pfi_buffer_max)

eventhandler_tag	 pfi_attach_cookie;
eventhandler_tag	 pfi_detach_cookie;
eventhandler_tag	 pfi_attach_group_cookie;
eventhandler_tag	 pfi_change_group_cookie;
eventhandler_tag	 pfi_detach_group_cookie;
eventhandler_tag	 pfi_ifaddr_event_cookie;

static void	 pfi_attach_ifnet(struct ifnet *);
static void	 pfi_attach_ifgroup(struct ifg_group *);

static void	 pfi_kif_update(struct pfi_kif *);
static void	 pfi_dynaddr_update(struct pfi_dynaddr *dyn);
static void	 pfi_table_update(struct pfr_ktable *, struct pfi_kif *, int,
		    int);
static void	 pfi_instance_add(struct ifnet *, int, int);
static void	 pfi_address_add(struct sockaddr *, int, int);
static int	 pfi_if_compare(struct pfi_kif *, struct pfi_kif *);
static int	 pfi_skip_if(const char *, struct pfi_kif *);
static int	 pfi_unmask(void *);
static void	 pfi_attach_ifnet_event(void * __unused, struct ifnet *);
static void	 pfi_detach_ifnet_event(void * __unused, struct ifnet *);
static void	 pfi_attach_group_event(void * __unused, struct ifg_group *);
static void	 pfi_change_group_event(void * __unused, char *);
static void	 pfi_detach_group_event(void * __unused, struct ifg_group *);
static void	 pfi_ifaddr_event(void * __unused, struct ifnet *);

RB_HEAD(pfi_ifhead, pfi_kif);
static RB_PROTOTYPE(pfi_ifhead, pfi_kif, pfik_tree, pfi_if_compare);
static RB_GENERATE(pfi_ifhead, pfi_kif, pfik_tree, pfi_if_compare);
VNET_DEFINE_STATIC(struct pfi_ifhead, pfi_ifs);
#define	V_pfi_ifs	VNET(pfi_ifs)

#define	PFI_BUFFER_MAX		0x10000
MALLOC_DEFINE(PFI_MTYPE, "pf_ifnet", "pf(4) interface database");

LIST_HEAD(pfi_list, pfi_kif);
VNET_DEFINE_STATIC(struct pfi_list, pfi_unlinked_kifs);
#define	V_pfi_unlinked_kifs	VNET(pfi_unlinked_kifs)
static struct mtx pfi_unlnkdkifs_mtx;
MTX_SYSINIT(pfi_unlnkdkifs_mtx, &pfi_unlnkdkifs_mtx, "pf unlinked interfaces",
    MTX_DEF);

void
pfi_initialize_vnet(void)
{
	struct ifg_group *ifg;
	struct ifnet *ifp;
	struct pfi_kif *kif;

	V_pfi_buffer_max = 64;
	V_pfi_buffer = malloc(V_pfi_buffer_max * sizeof(*V_pfi_buffer),
	    PFI_MTYPE, M_WAITOK);

	kif = malloc(sizeof(*kif), PFI_MTYPE, M_WAITOK);
	PF_RULES_WLOCK();
	V_pfi_all = pfi_kif_attach(kif, IFG_ALL);
	PF_RULES_WUNLOCK();

	IFNET_RLOCK();
	CK_STAILQ_FOREACH(ifg, &V_ifg_head, ifg_next)
		pfi_attach_ifgroup(ifg);
	CK_STAILQ_FOREACH(ifp, &V_ifnet, if_link)
		pfi_attach_ifnet(ifp);
	IFNET_RUNLOCK();
}

void
pfi_initialize(void)
{

	pfi_attach_cookie = EVENTHANDLER_REGISTER(ifnet_arrival_event,
	    pfi_attach_ifnet_event, NULL, EVENTHANDLER_PRI_ANY);
	pfi_detach_cookie = EVENTHANDLER_REGISTER(ifnet_departure_event,
	    pfi_detach_ifnet_event, NULL, EVENTHANDLER_PRI_ANY);
	pfi_attach_group_cookie = EVENTHANDLER_REGISTER(group_attach_event,
	    pfi_attach_group_event, NULL, EVENTHANDLER_PRI_ANY);
	pfi_change_group_cookie = EVENTHANDLER_REGISTER(group_change_event,
	    pfi_change_group_event, NULL, EVENTHANDLER_PRI_ANY);
	pfi_detach_group_cookie = EVENTHANDLER_REGISTER(group_detach_event,
	    pfi_detach_group_event, NULL, EVENTHANDLER_PRI_ANY);
	pfi_ifaddr_event_cookie = EVENTHANDLER_REGISTER(ifaddr_event,
	    pfi_ifaddr_event, NULL, EVENTHANDLER_PRI_ANY);
}

void
pfi_cleanup_vnet(void)
{
	struct pfi_kif *kif;

	PF_RULES_WASSERT();

	V_pfi_all = NULL;
	while ((kif = RB_MIN(pfi_ifhead, &V_pfi_ifs))) {
		RB_REMOVE(pfi_ifhead, &V_pfi_ifs, kif);
		if (kif->pfik_group)
			kif->pfik_group->ifg_pf_kif = NULL;
		if (kif->pfik_ifp) {
			if_rele(kif->pfik_ifp);
			kif->pfik_ifp->if_pf_kif = NULL;
		}
		free(kif, PFI_MTYPE);
	}

	mtx_lock(&pfi_unlnkdkifs_mtx);
	while ((kif = LIST_FIRST(&V_pfi_unlinked_kifs))) {
		LIST_REMOVE(kif, pfik_list);
		free(kif, PFI_MTYPE);
	}
	mtx_unlock(&pfi_unlnkdkifs_mtx);

	free(V_pfi_buffer, PFI_MTYPE);
}

void
pfi_cleanup(void)
{

	EVENTHANDLER_DEREGISTER(ifnet_arrival_event, pfi_attach_cookie);
	EVENTHANDLER_DEREGISTER(ifnet_departure_event, pfi_detach_cookie);
	EVENTHANDLER_DEREGISTER(group_attach_event, pfi_attach_group_cookie);
	EVENTHANDLER_DEREGISTER(group_change_event, pfi_change_group_cookie);
	EVENTHANDLER_DEREGISTER(group_detach_event, pfi_detach_group_cookie);
	EVENTHANDLER_DEREGISTER(ifaddr_event, pfi_ifaddr_event_cookie);
}

struct pfi_kif *
pfi_kif_find(const char *kif_name)
{
	struct pfi_kif_cmp s;

	PF_RULES_ASSERT();

	bzero(&s, sizeof(s));
	strlcpy(s.pfik_name, kif_name, sizeof(s.pfik_name));

	return (RB_FIND(pfi_ifhead, &V_pfi_ifs, (struct pfi_kif *)&s));
}

struct pfi_kif *
pfi_kif_attach(struct pfi_kif *kif, const char *kif_name)
{
	struct pfi_kif *kif1;

	PF_RULES_WASSERT();
	KASSERT(kif != NULL, ("%s: null kif", __func__));

	kif1 = pfi_kif_find(kif_name);
	if (kif1 != NULL) {
		free(kif, PFI_MTYPE);
		return (kif1);
	}

	bzero(kif, sizeof(*kif));
	strlcpy(kif->pfik_name, kif_name, sizeof(kif->pfik_name));
	/*
	 * It seems that the value of time_second is in unintialzied state
	 * when pf sets interface statistics clear time in boot phase if pf
	 * was statically linked to kernel. Instead of setting the bogus
	 * time value have pfi_get_ifaces handle this case. In
	 * pfi_get_ifaces it uses time_second if it sees the time is 0.
	 */
	kif->pfik_tzero = time_second > 1 ? time_second : 0;
	TAILQ_INIT(&kif->pfik_dynaddrs);

	RB_INSERT(pfi_ifhead, &V_pfi_ifs, kif);

	return (kif);
}

void
pfi_kif_ref(struct pfi_kif *kif)
{

	PF_RULES_WASSERT();
	kif->pfik_rulerefs++;
}

void
pfi_kif_unref(struct pfi_kif *kif)
{

	PF_RULES_WASSERT();
	KASSERT(kif->pfik_rulerefs > 0, ("%s: %p has zero refs", __func__, kif));

	kif->pfik_rulerefs--;

	if (kif->pfik_rulerefs > 0)
		return;

	/* kif referencing an existing ifnet or group should exist. */
	if (kif->pfik_ifp != NULL || kif->pfik_group != NULL || kif == V_pfi_all)
		return;

	RB_REMOVE(pfi_ifhead, &V_pfi_ifs, kif);

	kif->pfik_flags |= PFI_IFLAG_REFS;

	mtx_lock(&pfi_unlnkdkifs_mtx);
	LIST_INSERT_HEAD(&V_pfi_unlinked_kifs, kif, pfik_list);
	mtx_unlock(&pfi_unlnkdkifs_mtx);
}

void
pfi_kif_purge(void)
{
	struct pfi_kif *kif, *kif1;

	/*
	 * Do naive mark-and-sweep garbage collecting of old kifs.
	 * Reference flag is raised by pf_purge_expired_states().
	 */
	mtx_lock(&pfi_unlnkdkifs_mtx);
	LIST_FOREACH_SAFE(kif, &V_pfi_unlinked_kifs, pfik_list, kif1) {
		if (!(kif->pfik_flags & PFI_IFLAG_REFS)) {
			LIST_REMOVE(kif, pfik_list);
			free(kif, PFI_MTYPE);
		} else
			kif->pfik_flags &= ~PFI_IFLAG_REFS;
	}
	mtx_unlock(&pfi_unlnkdkifs_mtx);
}

int
pfi_kif_match(struct pfi_kif *rule_kif, struct pfi_kif *packet_kif)
{
	struct ifg_list	*p;

	if (rule_kif == NULL || rule_kif == packet_kif)
		return (1);

	if (rule_kif->pfik_group != NULL) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(p, &packet_kif->pfik_ifp->if_groups, ifgl_next)
			if (p->ifgl_group == rule_kif->pfik_group) {
				NET_EPOCH_EXIT(et);
				return (1);
			}
		NET_EPOCH_EXIT(et);
	}


	return (0);
}

static void
pfi_attach_ifnet(struct ifnet *ifp)
{
	struct pfi_kif *kif;

	kif = malloc(sizeof(*kif), PFI_MTYPE, M_WAITOK);

	PF_RULES_WLOCK();
	V_pfi_update++;
	kif = pfi_kif_attach(kif, ifp->if_xname);

	if_ref(ifp);

	kif->pfik_ifp = ifp;
	ifp->if_pf_kif = kif;

	pfi_kif_update(kif);
	PF_RULES_WUNLOCK();
}

static void
pfi_attach_ifgroup(struct ifg_group *ifg)
{
	struct pfi_kif *kif;

	kif = malloc(sizeof(*kif), PFI_MTYPE, M_WAITOK);

	PF_RULES_WLOCK();
	V_pfi_update++;
	kif = pfi_kif_attach(kif, ifg->ifg_group);

	kif->pfik_group = ifg;
	ifg->ifg_pf_kif = kif;
	PF_RULES_WUNLOCK();
}

int
pfi_match_addr(struct pfi_dynaddr *dyn, struct pf_addr *a, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		switch (dyn->pfid_acnt4) {
		case 0:
			return (0);
		case 1:
			return (PF_MATCHA(0, &dyn->pfid_addr4,
			    &dyn->pfid_mask4, a, AF_INET));
		default:
			return (pfr_match_addr(dyn->pfid_kt, a, AF_INET));
		}
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		switch (dyn->pfid_acnt6) {
		case 0:
			return (0);
		case 1:
			return (PF_MATCHA(0, &dyn->pfid_addr6,
			    &dyn->pfid_mask6, a, AF_INET6));
		default:
			return (pfr_match_addr(dyn->pfid_kt, a, AF_INET6));
		}
		break;
#endif /* INET6 */
	default:
		return (0);
	}
}

int
pfi_dynaddr_setup(struct pf_addr_wrap *aw, sa_family_t af)
{
	struct pfi_dynaddr	*dyn;
	char			 tblname[PF_TABLE_NAME_SIZE];
	struct pf_ruleset	*ruleset = NULL;
	struct pfi_kif		*kif;
	int			 rv = 0;

	PF_RULES_WASSERT();
	KASSERT(aw->type == PF_ADDR_DYNIFTL, ("%s: type %u",
	    __func__, aw->type));
	KASSERT(aw->p.dyn == NULL, ("%s: dyn is %p", __func__, aw->p.dyn));

	if ((dyn = malloc(sizeof(*dyn), PFI_MTYPE, M_NOWAIT | M_ZERO)) == NULL)
		return (ENOMEM);

	if ((kif = malloc(sizeof(*kif), PFI_MTYPE, M_NOWAIT)) == NULL) {
		free(dyn, PFI_MTYPE);
		return (ENOMEM);
	}

	if (!strcmp(aw->v.ifname, "self"))
		dyn->pfid_kif = pfi_kif_attach(kif, IFG_ALL);
	else
		dyn->pfid_kif = pfi_kif_attach(kif, aw->v.ifname);
	pfi_kif_ref(dyn->pfid_kif);

	dyn->pfid_net = pfi_unmask(&aw->v.a.mask);
	if (af == AF_INET && dyn->pfid_net == 32)
		dyn->pfid_net = 128;
	strlcpy(tblname, aw->v.ifname, sizeof(tblname));
	if (aw->iflags & PFI_AFLAG_NETWORK)
		strlcat(tblname, ":network", sizeof(tblname));
	if (aw->iflags & PFI_AFLAG_BROADCAST)
		strlcat(tblname, ":broadcast", sizeof(tblname));
	if (aw->iflags & PFI_AFLAG_PEER)
		strlcat(tblname, ":peer", sizeof(tblname));
	if (aw->iflags & PFI_AFLAG_NOALIAS)
		strlcat(tblname, ":0", sizeof(tblname));
	if (dyn->pfid_net != 128)
		snprintf(tblname + strlen(tblname),
		    sizeof(tblname) - strlen(tblname), "/%d", dyn->pfid_net);
	if ((ruleset = pf_find_or_create_ruleset(PF_RESERVED_ANCHOR)) == NULL) {
		rv = ENOMEM;
		goto _bad;
	}

	if ((dyn->pfid_kt = pfr_attach_table(ruleset, tblname)) == NULL) {
		rv = ENOMEM;
		goto _bad;
	}

	dyn->pfid_kt->pfrkt_flags |= PFR_TFLAG_ACTIVE;
	dyn->pfid_iflags = aw->iflags;
	dyn->pfid_af = af;

	TAILQ_INSERT_TAIL(&dyn->pfid_kif->pfik_dynaddrs, dyn, entry);
	aw->p.dyn = dyn;
	pfi_kif_update(dyn->pfid_kif);

	return (0);

_bad:
	if (dyn->pfid_kt != NULL)
		pfr_detach_table(dyn->pfid_kt);
	if (ruleset != NULL)
		pf_remove_if_empty_ruleset(ruleset);
	if (dyn->pfid_kif != NULL)
		pfi_kif_unref(dyn->pfid_kif);
	free(dyn, PFI_MTYPE);

	return (rv);
}

static void
pfi_kif_update(struct pfi_kif *kif)
{
	struct ifg_list		*ifgl;
	struct pfi_dynaddr	*p;

	PF_RULES_WASSERT();

	/* update all dynaddr */
	TAILQ_FOREACH(p, &kif->pfik_dynaddrs, entry)
		pfi_dynaddr_update(p);

	/* again for all groups kif is member of */
	if (kif->pfik_ifp != NULL) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifgl, &kif->pfik_ifp->if_groups, ifgl_next)
			pfi_kif_update((struct pfi_kif *)
			    ifgl->ifgl_group->ifg_pf_kif);
		NET_EPOCH_EXIT(et);
	}
}

static void
pfi_dynaddr_update(struct pfi_dynaddr *dyn)
{
	struct pfi_kif		*kif;
	struct pfr_ktable	*kt;

	PF_RULES_WASSERT();
	KASSERT(dyn && dyn->pfid_kif && dyn->pfid_kt,
	    ("%s: bad argument", __func__));

	kif = dyn->pfid_kif;
	kt = dyn->pfid_kt;

	if (kt->pfrkt_larg != V_pfi_update) {
		/* this table needs to be brought up-to-date */
		pfi_table_update(kt, kif, dyn->pfid_net, dyn->pfid_iflags);
		kt->pfrkt_larg = V_pfi_update;
	}
	pfr_dynaddr_update(kt, dyn);
}

static void
pfi_table_update(struct pfr_ktable *kt, struct pfi_kif *kif, int net, int flags)
{
	int			 e, size2 = 0;
	struct ifg_member	*ifgm;

	V_pfi_buffer_cnt = 0;

	if (kif->pfik_ifp != NULL)
		pfi_instance_add(kif->pfik_ifp, net, flags);
	else if (kif->pfik_group != NULL) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(ifgm, &kif->pfik_group->ifg_members, ifgm_next)
			pfi_instance_add(ifgm->ifgm_ifp, net, flags);
		NET_EPOCH_EXIT(et);
	}

	if ((e = pfr_set_addrs(&kt->pfrkt_t, V_pfi_buffer, V_pfi_buffer_cnt, &size2,
	    NULL, NULL, NULL, 0, PFR_TFLAG_ALLMASK)))
		printf("%s: cannot set %d new addresses into table %s: %d\n",
		    __func__, V_pfi_buffer_cnt, kt->pfrkt_name, e);
}

static void
pfi_instance_add(struct ifnet *ifp, int net, int flags)
{
	struct epoch_tracker et;
	struct ifaddr	*ia;
	int		 got4 = 0, got6 = 0;
	int		 net2, af;

	NET_EPOCH_ENTER(et);
	CK_STAILQ_FOREACH(ia, &ifp->if_addrhead, ifa_link) {
		if (ia->ifa_addr == NULL)
			continue;
		af = ia->ifa_addr->sa_family;
		if (af != AF_INET && af != AF_INET6)
			continue;
		/*
		 * XXX: For point-to-point interfaces, (ifname:0) and IPv4,
		 *      jump over addresses without a proper route to work
		 *      around a problem with ppp not fully removing the
		 *      address used during IPCP.
		 */
		if ((ifp->if_flags & IFF_POINTOPOINT) &&
		    !(ia->ifa_flags & IFA_ROUTE) &&
		    (flags & PFI_AFLAG_NOALIAS) && (af == AF_INET))
			continue;
		if ((flags & PFI_AFLAG_BROADCAST) && af == AF_INET6)
			continue;
		if ((flags & PFI_AFLAG_BROADCAST) &&
		    !(ifp->if_flags & IFF_BROADCAST))
			continue;
		if ((flags & PFI_AFLAG_PEER) &&
		    !(ifp->if_flags & IFF_POINTOPOINT))
			continue;
		if ((flags & (PFI_AFLAG_NETWORK | PFI_AFLAG_NOALIAS)) &&
		    af == AF_INET6 &&
		    IN6_IS_ADDR_LINKLOCAL(
		    &((struct sockaddr_in6 *)ia->ifa_addr)->sin6_addr))
			continue;
		if (flags & PFI_AFLAG_NOALIAS) {
			if (af == AF_INET && got4)
				continue;
			if (af == AF_INET6 && got6)
				continue;
		}
		if (af == AF_INET)
			got4 = 1;
		else if (af == AF_INET6)
			got6 = 1;
		net2 = net;
		if (net2 == 128 && (flags & PFI_AFLAG_NETWORK)) {
			if (af == AF_INET)
				net2 = pfi_unmask(&((struct sockaddr_in *)
				    ia->ifa_netmask)->sin_addr);
			else if (af == AF_INET6)
				net2 = pfi_unmask(&((struct sockaddr_in6 *)
				    ia->ifa_netmask)->sin6_addr);
		}
		if (af == AF_INET && net2 > 32)
			net2 = 32;
		if (flags & PFI_AFLAG_BROADCAST)
			pfi_address_add(ia->ifa_broadaddr, af, net2);
		else if (flags & PFI_AFLAG_PEER)
			pfi_address_add(ia->ifa_dstaddr, af, net2);
		else
			pfi_address_add(ia->ifa_addr, af, net2);
	}
	NET_EPOCH_EXIT(et);
}

static void
pfi_address_add(struct sockaddr *sa, int af, int net)
{
	struct pfr_addr	*p;
	int		 i;

	if (V_pfi_buffer_cnt >= V_pfi_buffer_max) {
		int		 new_max = V_pfi_buffer_max * 2;

		if (new_max > PFI_BUFFER_MAX) {
			printf("%s: address buffer full (%d/%d)\n", __func__,
			    V_pfi_buffer_cnt, PFI_BUFFER_MAX);
			return;
		}
		p = malloc(new_max * sizeof(*V_pfi_buffer), PFI_MTYPE,
		    M_NOWAIT);
		if (p == NULL) {
			printf("%s: no memory to grow buffer (%d/%d)\n",
			    __func__, V_pfi_buffer_cnt, PFI_BUFFER_MAX);
			return;
		}
		memcpy(p, V_pfi_buffer, V_pfi_buffer_max * sizeof(*V_pfi_buffer));
		/* no need to zero buffer */
		free(V_pfi_buffer, PFI_MTYPE);
		V_pfi_buffer = p;
		V_pfi_buffer_max = new_max;
	}
	if (af == AF_INET && net > 32)
		net = 128;
	p = V_pfi_buffer + V_pfi_buffer_cnt++;
	bzero(p, sizeof(*p));
	p->pfra_af = af;
	p->pfra_net = net;
	if (af == AF_INET)
		p->pfra_ip4addr = ((struct sockaddr_in *)sa)->sin_addr;
	else if (af == AF_INET6) {
		p->pfra_ip6addr = ((struct sockaddr_in6 *)sa)->sin6_addr;
		if (IN6_IS_SCOPE_EMBED(&p->pfra_ip6addr))
			p->pfra_ip6addr.s6_addr16[1] = 0;
	}
	/* mask network address bits */
	if (net < 128)
		((caddr_t)p)[p->pfra_net/8] &= ~(0xFF >> (p->pfra_net%8));
	for (i = (p->pfra_net+7)/8; i < sizeof(p->pfra_u); i++)
		((caddr_t)p)[i] = 0;
}

void
pfi_dynaddr_remove(struct pfi_dynaddr *dyn)
{

	KASSERT(dyn->pfid_kif != NULL, ("%s: null pfid_kif", __func__));
	KASSERT(dyn->pfid_kt != NULL, ("%s: null pfid_kt", __func__));

	TAILQ_REMOVE(&dyn->pfid_kif->pfik_dynaddrs, dyn, entry);
	pfi_kif_unref(dyn->pfid_kif);
	pfr_detach_table(dyn->pfid_kt);
	free(dyn, PFI_MTYPE);
}

void
pfi_dynaddr_copyout(struct pf_addr_wrap *aw)
{

	KASSERT(aw->type == PF_ADDR_DYNIFTL,
	    ("%s: type %u", __func__, aw->type));

	if (aw->p.dyn == NULL || aw->p.dyn->pfid_kif == NULL)
		return;
	aw->p.dyncnt = aw->p.dyn->pfid_acnt4 + aw->p.dyn->pfid_acnt6;
}

static int
pfi_if_compare(struct pfi_kif *p, struct pfi_kif *q)
{
	return (strncmp(p->pfik_name, q->pfik_name, IFNAMSIZ));
}

void
pfi_update_status(const char *name, struct pf_status *pfs)
{
	struct pfi_kif		*p;
	struct pfi_kif_cmp	 key;
	struct ifg_member	 p_member, *ifgm;
	CK_STAILQ_HEAD(, ifg_member) ifg_members;
	int			 i, j, k;

	strlcpy(key.pfik_name, name, sizeof(key.pfik_name));
	p = RB_FIND(pfi_ifhead, &V_pfi_ifs, (struct pfi_kif *)&key);
	if (p == NULL)
		return;

	if (p->pfik_group != NULL) {
		bcopy(&p->pfik_group->ifg_members, &ifg_members,
		    sizeof(ifg_members));
	} else {
		/* build a temporary list for p only */
		bzero(&p_member, sizeof(p_member));
		p_member.ifgm_ifp = p->pfik_ifp;
		CK_STAILQ_INIT(&ifg_members);
		CK_STAILQ_INSERT_TAIL(&ifg_members, &p_member, ifgm_next);
	}
	if (pfs) {
		bzero(pfs->pcounters, sizeof(pfs->pcounters));
		bzero(pfs->bcounters, sizeof(pfs->bcounters));
	}
	CK_STAILQ_FOREACH(ifgm, &ifg_members, ifgm_next) {
		if (ifgm->ifgm_ifp == NULL || ifgm->ifgm_ifp->if_pf_kif == NULL)
			continue;
		p = (struct pfi_kif *)ifgm->ifgm_ifp->if_pf_kif;

		/* just clear statistics */
		if (pfs == NULL) {
			bzero(p->pfik_packets, sizeof(p->pfik_packets));
			bzero(p->pfik_bytes, sizeof(p->pfik_bytes));
			p->pfik_tzero = time_second;
			continue;
		}
		for (i = 0; i < 2; i++)
			for (j = 0; j < 2; j++)
				for (k = 0; k < 2; k++) {
					pfs->pcounters[i][j][k] +=
						p->pfik_packets[i][j][k];
					pfs->bcounters[i][j] +=
						p->pfik_bytes[i][j][k];
				}
	}
}

void
pfi_get_ifaces(const char *name, struct pfi_kif *buf, int *size)
{
	struct pfi_kif	*p, *nextp;
	int		 n = 0;

	for (p = RB_MIN(pfi_ifhead, &V_pfi_ifs); p; p = nextp) {
		nextp = RB_NEXT(pfi_ifhead, &V_pfi_ifs, p);
		if (pfi_skip_if(name, p))
			continue;
		if (*size <= n++)
			break;
		if (!p->pfik_tzero)
			p->pfik_tzero = time_second;
		bcopy(p, buf++, sizeof(*buf));
		nextp = RB_NEXT(pfi_ifhead, &V_pfi_ifs, p);
	}
	*size = n;
}

static int
pfi_skip_if(const char *filter, struct pfi_kif *p)
{
	struct ifg_list *i;
	int	n;

	if (filter == NULL || !*filter)
		return (0);
	if (!strcmp(p->pfik_name, filter))
		return (0);	/* exact match */
	n = strlen(filter);
	if (n < 1 || n >= IFNAMSIZ)
		return (1);	/* sanity check */
	if (filter[n-1] >= '0' && filter[n-1] <= '9')
		return (1);	/* group names may not end in a digit */
	if (p->pfik_ifp != NULL) {
		struct epoch_tracker et;

		NET_EPOCH_ENTER(et);
		CK_STAILQ_FOREACH(i, &p->pfik_ifp->if_groups, ifgl_next) {
			if (!strncmp(i->ifgl_group->ifg_group, filter,
			      IFNAMSIZ)) {
				NET_EPOCH_EXIT(et);
				return (0); /* iface is in group "filter" */
			}
		}
		NET_EPOCH_EXIT(et);
	}
	return (1);
}

int
pfi_set_flags(const char *name, int flags)
{
	struct pfi_kif	*p;

	RB_FOREACH(p, pfi_ifhead, &V_pfi_ifs) {
		if (pfi_skip_if(name, p))
			continue;
		p->pfik_flags |= flags;
	}
	return (0);
}

int
pfi_clear_flags(const char *name, int flags)
{
	struct pfi_kif	*p;

	RB_FOREACH(p, pfi_ifhead, &V_pfi_ifs) {
		if (pfi_skip_if(name, p))
			continue;
		p->pfik_flags &= ~flags;
	}
	return (0);
}

/* from pf_print_state.c */
static int
pfi_unmask(void *addr)
{
	struct pf_addr *m = addr;
	int i = 31, j = 0, b = 0;
	u_int32_t tmp;

	while (j < 4 && m->addr32[j] == 0xffffffff) {
		b += 32;
		j++;
	}
	if (j < 4) {
		tmp = ntohl(m->addr32[j]);
		for (i = 31; tmp & (1 << i); --i)
			b++;
	}
	return (b);
}

static void
pfi_attach_ifnet_event(void *arg __unused, struct ifnet *ifp)
{

	if (V_pf_vnet_active == 0) {
		/* Avoid teardown race in the least expensive way. */
		return;
	}
	pfi_attach_ifnet(ifp);
#ifdef ALTQ
	PF_RULES_WLOCK();
	pf_altq_ifnet_event(ifp, 0);
	PF_RULES_WUNLOCK();
#endif
}

static void
pfi_detach_ifnet_event(void *arg __unused, struct ifnet *ifp)
{
	struct pfi_kif *kif = (struct pfi_kif *)ifp->if_pf_kif;

	if (pfsync_detach_ifnet_ptr)
		pfsync_detach_ifnet_ptr(ifp);

	if (kif == NULL)
		return;

	if (V_pf_vnet_active == 0) {
		/* Avoid teardown race in the least expensive way. */
		return;
	}

	PF_RULES_WLOCK();
	V_pfi_update++;
	pfi_kif_update(kif);

	if (kif->pfik_ifp)
		if_rele(kif->pfik_ifp);

	kif->pfik_ifp = NULL;
	ifp->if_pf_kif = NULL;
#ifdef ALTQ
	pf_altq_ifnet_event(ifp, 1);
#endif
	PF_RULES_WUNLOCK();
}

static void
pfi_attach_group_event(void *arg __unused, struct ifg_group *ifg)
{

	if (V_pf_vnet_active == 0) {
		/* Avoid teardown race in the least expensive way. */
		return;
	}
	pfi_attach_ifgroup(ifg);
}

static void
pfi_change_group_event(void *arg __unused, char *gname)
{
	struct pfi_kif *kif;

	if (V_pf_vnet_active == 0) {
		/* Avoid teardown race in the least expensive way. */
		return;
	}

	kif = malloc(sizeof(*kif), PFI_MTYPE, M_WAITOK);
	PF_RULES_WLOCK();
	V_pfi_update++;
	kif = pfi_kif_attach(kif, gname);
	pfi_kif_update(kif);
	PF_RULES_WUNLOCK();
}

static void
pfi_detach_group_event(void *arg __unused, struct ifg_group *ifg)
{
	struct pfi_kif *kif = (struct pfi_kif *)ifg->ifg_pf_kif;

	if (kif == NULL)
		return;

	if (V_pf_vnet_active == 0) {
		/* Avoid teardown race in the least expensive way. */
		return;
	}
	PF_RULES_WLOCK();
	V_pfi_update++;

	kif->pfik_group = NULL;
	ifg->ifg_pf_kif = NULL;
	PF_RULES_WUNLOCK();
}

static void
pfi_ifaddr_event(void *arg __unused, struct ifnet *ifp)
{

	KASSERT(ifp, ("ifp == NULL"));

	if (ifp->if_pf_kif == NULL)
		return;

	if (V_pf_vnet_active == 0) {
		/* Avoid teardown race in the least expensive way. */
		return;
	}
	PF_RULES_WLOCK();
	if (ifp->if_pf_kif) {
		V_pfi_update++;
		pfi_kif_update(ifp->if_pf_kif);
	}
	PF_RULES_WUNLOCK();
}
