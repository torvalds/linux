/*	$OpenBSD: pf_if.c,v 1.112 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright 2005 Henning Brauer <henning@openbsd.org>
 * Copyright 2005 Ryan McBride <mcbride@openbsd.org>
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2003 Cedric Berger
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/pool.h>
#include <sys/syslog.h>

#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include <net/pfvar.h>

#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#endif /* INET6 */

#include <net/pfvar_priv.h>

#define isupper(c)	((c) >= 'A' && (c) <= 'Z')
#define islower(c)	((c) >= 'a' && (c) <= 'z')
#define isalpha(c)	(isupper(c)||islower(c))

struct pfi_kif		 *pfi_all = NULL;
struct pool		  pfi_addr_pl;
struct pfi_ifhead	  pfi_ifs;
long			  pfi_update = 1;
struct pfr_addr		 *pfi_buffer;
int			  pfi_buffer_cnt;
int			  pfi_buffer_max;

void		 pfi_kif_update(struct pfi_kif *);
void		 pfi_dynaddr_update(struct pfi_dynaddr *dyn);
void		 pfi_table_update(struct pfr_ktable *, struct pfi_kif *,
		    u_int8_t, int);
void		 pfi_kifaddr_update(void *);
void		 pfi_instance_add(struct ifnet *, u_int8_t, int);
void		 pfi_address_add(struct sockaddr *, sa_family_t, u_int8_t);
int		 pfi_if_compare(struct pfi_kif *, struct pfi_kif *);
int		 pfi_skip_if(const char *, struct pfi_kif *);
int		 pfi_unmask(void *);
void		 pfi_group_change(const char *);

RB_PROTOTYPE(pfi_ifhead, pfi_kif, pfik_tree, pfi_if_compare);
RB_GENERATE(pfi_ifhead, pfi_kif, pfik_tree, pfi_if_compare);

#define PFI_BUFFER_MAX		0x10000
#define PFI_MTYPE		M_PF

struct pfi_kif *
pfi_kif_alloc(const char *kif_name, int mflags)
{
	struct pfi_kif *kif;

	kif = malloc(sizeof(*pfi_all), PFI_MTYPE, mflags|M_ZERO);
	if (kif == NULL)
		return (NULL);
	strlcpy(kif->pfik_name, kif_name, sizeof(kif->pfik_name));
	kif->pfik_tzero = gettime();
	TAILQ_INIT(&kif->pfik_dynaddrs);

	if (!strcmp(kif->pfik_name, "any")) {
		/* both so it works in the ioctl and the regular case */
		kif->pfik_flags |= PFI_IFLAG_ANY;
		kif->pfik_flags_new |= PFI_IFLAG_ANY;
	}

	return (kif);
}

void
pfi_kif_free(struct pfi_kif *kif)
{
	if (kif == NULL)
		return;

	if (kif->pfik_rules || kif->pfik_states || kif->pfik_routes ||
	     kif->pfik_srcnodes || kif->pfik_flagrefs)
		panic("kif is still alive");

	free(kif, PFI_MTYPE, sizeof(*kif));
}

void
pfi_initialize(void)
{
	/*
	 * The first time we arrive here is during kernel boot,
	 * when if_attachsetup() for the first time. No locking
	 * is needed in this case, because it's granted there
	 * is a single thread, which sets pfi_all global var.
	 */
	if (pfi_all != NULL)	/* already initialized */
		return;

	pool_init(&pfi_addr_pl, sizeof(struct pfi_dynaddr), 0, IPL_SOFTNET, 0,
	    "pfiaddrpl", NULL);
	pfi_buffer_max = 64;
	pfi_buffer = mallocarray(pfi_buffer_max, sizeof(*pfi_buffer),
	    PFI_MTYPE, M_WAITOK);

	pfi_all = pfi_kif_alloc(IFG_ALL, M_WAITOK);

	if (RB_INSERT(pfi_ifhead, &pfi_ifs, pfi_all) != NULL)
		panic("IFG_ALL kif found already");
}

struct pfi_kif *
pfi_kif_find(const char *kif_name)
{
	struct pfi_kif_cmp	 s;

	PF_ASSERT_LOCKED();

	memset(&s, 0, sizeof(s));
	strlcpy(s.pfik_name, kif_name, sizeof(s.pfik_name));
	return (RB_FIND(pfi_ifhead, &pfi_ifs, (struct pfi_kif *)&s));
}

struct pfi_kif *
pfi_kif_get(const char *kif_name, struct pfi_kif **prealloc)
{
	struct pfi_kif		*kif;

	PF_ASSERT_LOCKED();

	if ((kif = pfi_kif_find(kif_name)))
		return (kif);

	/* create new one */
	if ((prealloc == NULL) || (*prealloc == NULL)) {
		kif = pfi_kif_alloc(kif_name, M_NOWAIT);
		if (kif == NULL)
			return (NULL);
	} else {
		kif = *prealloc;
		*prealloc = NULL;
	}

	RB_INSERT(pfi_ifhead, &pfi_ifs, kif);
	return (kif);
}

void
pfi_kif_ref(struct pfi_kif *kif, enum pfi_kif_refs what)
{
	PF_ASSERT_LOCKED();

	switch (what) {
	case PFI_KIF_REF_RULE:
		kif->pfik_rules++;
		break;
	case PFI_KIF_REF_STATE:
		kif->pfik_states++;
		break;
	case PFI_KIF_REF_ROUTE:
		kif->pfik_routes++;
		break;
	case PFI_KIF_REF_SRCNODE:
		kif->pfik_srcnodes++;
		break;
	case PFI_KIF_REF_FLAG:
		kif->pfik_flagrefs++;
		break;
	default:
		panic("pfi_kif_ref with unknown type");
	}
}

void
pfi_kif_unref(struct pfi_kif *kif, enum pfi_kif_refs what)
{
	if (kif == NULL)
		return;

	PF_ASSERT_LOCKED();

	switch (what) {
	case PFI_KIF_REF_NONE:
		break;
	case PFI_KIF_REF_RULE:
		if (kif->pfik_rules <= 0) {
			DPFPRINTF(LOG_ERR,
			    "pfi_kif_unref (%s): rules refcount <= 0",
			    kif->pfik_name);
			return;
		}
		kif->pfik_rules--;
		break;
	case PFI_KIF_REF_STATE:
		if (kif->pfik_states <= 0) {
			DPFPRINTF(LOG_ERR,
			    "pfi_kif_unref (%s): state refcount <= 0",
			    kif->pfik_name);
			return;
		}
		kif->pfik_states--;
		break;
	case PFI_KIF_REF_ROUTE:
		if (kif->pfik_routes <= 0) {
			DPFPRINTF(LOG_ERR,
			    "pfi_kif_unref (%s): route refcount <= 0",
			    kif->pfik_name);
			return;
		}
		kif->pfik_routes--;
		break;
	case PFI_KIF_REF_SRCNODE:
		if (kif->pfik_srcnodes <= 0) {
			DPFPRINTF(LOG_ERR,
			    "pfi_kif_unref (%s): src-node refcount <= 0",
			    kif->pfik_name);
			return;
		}
		kif->pfik_srcnodes--;
		break;
	case PFI_KIF_REF_FLAG:
		if (kif->pfik_flagrefs <= 0) {
			DPFPRINTF(LOG_ERR,
			    "pfi_kif_unref (%s): flags refcount <= 0",
			    kif->pfik_name);
			return;
		}
		kif->pfik_flagrefs--;
		break;
	default:
		panic("pfi_kif_unref (%s) with unknown type", kif->pfik_name);
	}

	if (kif->pfik_ifp != NULL || kif->pfik_group != NULL || kif == pfi_all)
		return;

	if (kif->pfik_rules || kif->pfik_states || kif->pfik_routes ||
	    kif->pfik_srcnodes || kif->pfik_flagrefs)
		return;

	RB_REMOVE(pfi_ifhead, &pfi_ifs, kif);
	free(kif, PFI_MTYPE, sizeof(*kif));
}

int
pfi_kif_match(struct pfi_kif *rule_kif, struct pfi_kif *packet_kif)
{
	struct ifg_list	*p;

	if (rule_kif == NULL || rule_kif == packet_kif)
		return (1);

	if (rule_kif->pfik_group != NULL)
		TAILQ_FOREACH(p, &packet_kif->pfik_ifp->if_groups, ifgl_next)
			if (p->ifgl_group == rule_kif->pfik_group)
				return (1);

	if (rule_kif->pfik_flags & PFI_IFLAG_ANY && packet_kif->pfik_ifp &&
	    !(packet_kif->pfik_ifp->if_flags & IFF_LOOPBACK))
		return (1);

	return (0);
}

void
pfi_attach_ifnet(struct ifnet *ifp)
{
	struct pfi_kif		*kif;
	struct task		*t;

	PF_LOCK();
	pfi_initialize();
	pfi_update++;
	if ((kif = pfi_kif_get(ifp->if_xname, NULL)) == NULL)
		panic("%s: pfi_kif_get failed", __func__);

	kif->pfik_ifp = ifp;
	ifp->if_pf_kif = (caddr_t)kif;

	t = malloc(sizeof(*t), PFI_MTYPE, M_WAITOK);
	task_set(t, pfi_kifaddr_update, kif);
	if_addrhook_add(ifp, t);
	kif->pfik_ah_cookie = t;

	pfi_kif_update(kif);
	PF_UNLOCK();
}

void
pfi_detach_ifnet(struct ifnet *ifp)
{
	struct pfi_kif		*kif;
	struct task		*t;

	if ((kif = (struct pfi_kif *)ifp->if_pf_kif) == NULL)
		return;

	PF_LOCK();
	pfi_update++;
	t = kif->pfik_ah_cookie;
	kif->pfik_ah_cookie = NULL;
	if_addrhook_del(ifp, t);
	free(t, PFI_MTYPE, sizeof(*t));

	pfi_kif_update(kif);

	kif->pfik_ifp = NULL;
	ifp->if_pf_kif = NULL;
	pfi_kif_unref(kif, PFI_KIF_REF_NONE);
	PF_UNLOCK();
}

void
pfi_attach_ifgroup(struct ifg_group *ifg)
{
	struct pfi_kif	*kif;

	PF_LOCK();
	pfi_initialize();
	pfi_update++;
	if ((kif = pfi_kif_get(ifg->ifg_group, NULL)) == NULL)
		panic("%s: pfi_kif_get failed", __func__);

	kif->pfik_group = ifg;
	ifg->ifg_pf_kif = (caddr_t)kif;
	PF_UNLOCK();
}

void
pfi_detach_ifgroup(struct ifg_group *ifg)
{
	struct pfi_kif	*kif;

	if ((kif = (struct pfi_kif *)ifg->ifg_pf_kif) == NULL)
		return;

	PF_LOCK();
	pfi_update++;

	kif->pfik_group = NULL;
	ifg->ifg_pf_kif = NULL;
	pfi_kif_unref(kif, PFI_KIF_REF_NONE);
	PF_UNLOCK();
}

void
pfi_group_change(const char *group)
{
	struct pfi_kif		*kif;

	pfi_update++;
	if ((kif = pfi_kif_get(group, NULL)) == NULL)
		panic("%s: pfi_kif_get failed", __func__);

	pfi_kif_update(kif);
}

void
pfi_group_delmember(const char *group)
{
	PF_LOCK();
	pfi_group_change(group);
	pfi_xcommit();
	PF_UNLOCK();
}

void
pfi_group_addmember(const char *group)
{
	PF_LOCK();
	pfi_group_change(group);	
	pfi_xcommit();
	PF_UNLOCK();
}

int
pfi_match_addr(struct pfi_dynaddr *dyn, struct pf_addr *a, sa_family_t af)
{
	switch (af) {
	case AF_INET:
		switch (dyn->pfid_acnt4) {
		case 0:
			return (0);
		case 1:
			return (pf_match_addr(0, &dyn->pfid_addr4,
			    &dyn->pfid_mask4, a, AF_INET));
		default:
			return (pfr_match_addr(dyn->pfid_kt, a, AF_INET));
		}
		break;
#ifdef INET6
	case AF_INET6:
		switch (dyn->pfid_acnt6) {
		case 0:
			return (0);
		case 1:
			return (pf_match_addr(0, &dyn->pfid_addr6,
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
pfi_dynaddr_setup(struct pf_addr_wrap *aw, sa_family_t af, int wait)
{
	struct pfi_dynaddr	*dyn;
	char			 tblname[PF_TABLE_NAME_SIZE];
	struct pf_ruleset	*ruleset = NULL;
	int			 rv = 0;

	if (aw->type != PF_ADDR_DYNIFTL)
		return (0);
	if ((dyn = pool_get(&pfi_addr_pl, wait|PR_LIMITFAIL|PR_ZERO)) == NULL)
		return (1);

	if (!strcmp(aw->v.ifname, "self"))
		dyn->pfid_kif = pfi_kif_get(IFG_ALL, NULL);
	else
		dyn->pfid_kif = pfi_kif_get(aw->v.ifname, NULL);
	if (dyn->pfid_kif == NULL) {
		rv = 1;
		goto _bad;
	}
	pfi_kif_ref(dyn->pfid_kif, PFI_KIF_REF_RULE);

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
		rv = 1;
		goto _bad;
	}

	if ((dyn->pfid_kt = pfr_attach_table(ruleset, tblname, wait)) == NULL) {
		rv = 1;
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
		pfi_kif_unref(dyn->pfid_kif, PFI_KIF_REF_RULE);
	pool_put(&pfi_addr_pl, dyn);
	return (rv);
}

void
pfi_kif_update(struct pfi_kif *kif)
{
	struct ifg_list		*ifgl;
	struct pfi_dynaddr	*p;

	/* update all dynaddr */
	TAILQ_FOREACH(p, &kif->pfik_dynaddrs, entry)
		pfi_dynaddr_update(p);

	/* again for all groups kif is member of */
	if (kif->pfik_ifp != NULL)
		TAILQ_FOREACH(ifgl, &kif->pfik_ifp->if_groups, ifgl_next)
			pfi_kif_update((struct pfi_kif *)
			    ifgl->ifgl_group->ifg_pf_kif);
}

void
pfi_dynaddr_update(struct pfi_dynaddr *dyn)
{
	struct pfi_kif		*kif;
	struct pfr_ktable	*kt;

	if (dyn == NULL || dyn->pfid_kif == NULL || dyn->pfid_kt == NULL)
		panic("pfi_dynaddr_update");

	kif = dyn->pfid_kif;
	kt = dyn->pfid_kt;

	if (kt->pfrkt_larg != pfi_update) {
		/* this table needs to be brought up-to-date */
		pfi_table_update(kt, kif, dyn->pfid_net, dyn->pfid_iflags);
		kt->pfrkt_larg = pfi_update;
	}
	pfr_dynaddr_update(kt, dyn);
}

void
pfi_table_update(struct pfr_ktable *kt, struct pfi_kif *kif, u_int8_t net, int flags)
{
	int			 e, size2 = 0;
	struct ifg_member	*ifgm;

	pfi_buffer_cnt = 0;

	if (kif->pfik_ifp != NULL)
		pfi_instance_add(kif->pfik_ifp, net, flags);
	else if (kif->pfik_group != NULL)
		TAILQ_FOREACH(ifgm, &kif->pfik_group->ifg_members, ifgm_next)
			pfi_instance_add(ifgm->ifgm_ifp, net, flags);

	if ((e = pfr_set_addrs(&kt->pfrkt_t, pfi_buffer, pfi_buffer_cnt, &size2,
	    NULL, NULL, NULL, 0, PFR_TFLAG_ALLMASK)))
		DPFPRINTF(LOG_ERR,
		    "pfi_table_update: cannot set %d new addresses "
		    "into table %s: %d", pfi_buffer_cnt, kt->pfrkt_name, e);
}

void
pfi_instance_add(struct ifnet *ifp, u_int8_t net, int flags)
{
	struct ifaddr	*ifa;
	int		 got4 = 0, got6 = 0;
	int		 net2, af;

	if (ifp == NULL)
		return;
	TAILQ_FOREACH(ifa, &ifp->if_addrlist, ifa_list) {
		if (ifa->ifa_addr == NULL)
			continue;
		af = ifa->ifa_addr->sa_family;
		if (af != AF_INET && af != AF_INET6)
			continue;
		if ((flags & PFI_AFLAG_BROADCAST) && af == AF_INET6)
			continue;
		if ((flags & PFI_AFLAG_BROADCAST) &&
		    !(ifp->if_flags & IFF_BROADCAST))
			continue;
		if ((flags & PFI_AFLAG_PEER) &&
		    !(ifp->if_flags & IFF_POINTOPOINT))
			continue;
		if ((flags & PFI_AFLAG_NETWORK) && af == AF_INET6 &&
		    IN6_IS_ADDR_LINKLOCAL(
		    &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr))
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
				    ifa->ifa_netmask)->sin_addr);
			else if (af == AF_INET6)
				net2 = pfi_unmask(&((struct sockaddr_in6 *)
				    ifa->ifa_netmask)->sin6_addr);
		}
		if (af == AF_INET && net2 > 32)
			net2 = 32;
		if (flags & PFI_AFLAG_BROADCAST)
			pfi_address_add(ifa->ifa_broadaddr, af, net2);
		else if (flags & PFI_AFLAG_PEER)
			pfi_address_add(ifa->ifa_dstaddr, af, net2);
		else
			pfi_address_add(ifa->ifa_addr, af, net2);
	}
}

void
pfi_address_add(struct sockaddr *sa, sa_family_t af, u_int8_t net)
{
	struct pfr_addr	*p;
	int		 i;

	if (pfi_buffer_cnt >= pfi_buffer_max) {
		int		 new_max = pfi_buffer_max * 2;

		if (new_max > PFI_BUFFER_MAX) {
			DPFPRINTF(LOG_ERR,
			    "pfi_address_add: address buffer full (%d/%d)",
			    pfi_buffer_cnt, PFI_BUFFER_MAX);
			return;
		}
		p = mallocarray(new_max, sizeof(*pfi_buffer), PFI_MTYPE,
		    M_DONTWAIT);
		if (p == NULL) {
			DPFPRINTF(LOG_ERR,
			    "pfi_address_add: no memory to grow buffer "
			    "(%d/%d)", pfi_buffer_cnt, PFI_BUFFER_MAX);
			return;
		}
		memcpy(p, pfi_buffer, pfi_buffer_max * sizeof(*pfi_buffer));
		/* no need to zero buffer */
		free(pfi_buffer, PFI_MTYPE, pfi_buffer_max * sizeof(*pfi_buffer));
		pfi_buffer = p;
		pfi_buffer_max = new_max;
	}
	if (af == AF_INET && net > 32)
		net = 128;
	p = pfi_buffer + pfi_buffer_cnt++;
	memset(p, 0, sizeof(*p));
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
pfi_dynaddr_remove(struct pf_addr_wrap *aw)
{
	if (aw->type != PF_ADDR_DYNIFTL || aw->p.dyn == NULL ||
	    aw->p.dyn->pfid_kif == NULL || aw->p.dyn->pfid_kt == NULL)
		return;

	TAILQ_REMOVE(&aw->p.dyn->pfid_kif->pfik_dynaddrs, aw->p.dyn, entry);
	pfi_kif_unref(aw->p.dyn->pfid_kif, PFI_KIF_REF_RULE);
	aw->p.dyn->pfid_kif = NULL;
	pfr_detach_table(aw->p.dyn->pfid_kt);
	aw->p.dyn->pfid_kt = NULL;
	pool_put(&pfi_addr_pl, aw->p.dyn);
	aw->p.dyn = NULL;
}

void
pfi_dynaddr_copyout(struct pf_addr_wrap *aw)
{
	if (aw->type != PF_ADDR_DYNIFTL || aw->p.dyn == NULL ||
	    aw->p.dyn->pfid_kif == NULL)
		return;
	aw->p.dyncnt = aw->p.dyn->pfid_acnt4 + aw->p.dyn->pfid_acnt6;
}

void
pfi_kifaddr_update(void *v)
{
	struct pfi_kif		*kif = (struct pfi_kif *)v;

	NET_ASSERT_LOCKED();

	PF_LOCK();
	pfi_update++;
	pfi_kif_update(kif);
	PF_UNLOCK();
}

int
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
	TAILQ_HEAD(, ifg_member) ifg_members;
	int			 i, j, k;

	if (*name == '\0' && pfs == NULL) {
		RB_FOREACH(p, pfi_ifhead, &pfi_ifs) {
			memset(p->pfik_packets, 0, sizeof(p->pfik_packets));
			memset(p->pfik_bytes, 0, sizeof(p->pfik_bytes));
			p->pfik_tzero = gettime();
		}
		return;
	}

	strlcpy(key.pfik_name, name, sizeof(key.pfik_name));
	p = RB_FIND(pfi_ifhead, &pfi_ifs, (struct pfi_kif *)&key);
	if (p == NULL) {
		return;
	}
	if (p->pfik_group != NULL) {
		memcpy(&ifg_members, &p->pfik_group->ifg_members,
		    sizeof(ifg_members));
	} else {
		/* build a temporary list for p only */
		memset(&p_member, 0, sizeof(p_member));
		p_member.ifgm_ifp = p->pfik_ifp;
		TAILQ_INIT(&ifg_members);
		TAILQ_INSERT_TAIL(&ifg_members, &p_member, ifgm_next);
	}
	if (pfs) {
		memset(pfs->pcounters, 0, sizeof(pfs->pcounters));
		memset(pfs->bcounters, 0, sizeof(pfs->bcounters));
	}
	TAILQ_FOREACH(ifgm, &ifg_members, ifgm_next) {
		if (ifgm->ifgm_ifp == NULL)
			continue;
		p = (struct pfi_kif *)ifgm->ifgm_ifp->if_pf_kif;

		/* just clear statistics */
		if (pfs == NULL) {
			memset(p->pfik_packets, 0, sizeof(p->pfik_packets));
			memset(p->pfik_bytes, 0, sizeof(p->pfik_bytes));
			p->pfik_tzero = gettime();
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
	struct pfi_kif	*p;
	int		 n = 0;

	RB_FOREACH(p, pfi_ifhead, &pfi_ifs) {
		if (pfi_skip_if(name, p))
			continue;
		if (*size <= ++n)
			break;
		if (!p->pfik_tzero)
			p->pfik_tzero = gettime();
		memcpy(buf++, p, sizeof(*buf));
	}
	*size = n;
}

int
pfi_skip_if(const char *filter, struct pfi_kif *p)
{
	struct ifg_list	*i;
	int		 n;

	PF_ASSERT_LOCKED();

	if (filter == NULL || !*filter)
		return (0);
	if (!strcmp(p->pfik_name, filter))
		return (0);	/* exact match */
	n = strlen(filter);
	if (n < 1 || n >= IFNAMSIZ)
		return (1);	/* sanity check */
	if (filter[n-1] >= '0' && filter[n-1] <= '9')
		return (1);     /* group names may not end in a digit */
	if (p->pfik_ifp != NULL)
		TAILQ_FOREACH(i, &p->pfik_ifp->if_groups, ifgl_next)
			if (!strncmp(i->ifgl_group->ifg_group, filter, IFNAMSIZ))
				return (0);	/* iface is in group "filter" */
	return (1);
}

int
pfi_set_flags(const char *name, int flags)
{
	struct pfi_kif	*p;
	size_t	n;

	PF_ASSERT_LOCKED();

	if (name != NULL && name[0] != '\0') {
		p = pfi_kif_find(name);
		if (p == NULL) {
			n = strlen(name);
			if (n < 1 || n >= IFNAMSIZ)
				return (EINVAL);

			if (!isalpha(name[0]))
				return (EINVAL);

			p = pfi_kif_get(name, NULL);
			if (p != NULL) {
				p->pfik_flags_new = p->pfik_flags | flags;
				/*
				 * We use pfik_flagrefs counter as an
				 * indication whether the kif has been created
				 * on behalf of 'pfi_set_flags()' or not.
				 */
				KASSERT(p->pfik_flagrefs == 0);
				if (ISSET(p->pfik_flags_new, PFI_IFLAG_SKIP))
					pfi_kif_ref(p, PFI_KIF_REF_FLAG);
			} else
				panic("%s pfi_kif_get() returned NULL\n",
				    __func__);
		} else
			p->pfik_flags_new = p->pfik_flags | flags;
	} else {
		RB_FOREACH(p, pfi_ifhead, &pfi_ifs)
			p->pfik_flags_new = p->pfik_flags | flags;
	}

	return (0);
}

int
pfi_clear_flags(const char *name, int flags)
{
	struct pfi_kif	*p, *w;

	PF_ASSERT_LOCKED();

	if (name != NULL && name[0] != '\0') {
		p = pfi_kif_find(name);
		if (p != NULL) {
			p->pfik_flags_new = p->pfik_flags & ~flags;

			KASSERT((p->pfik_flagrefs == 0) ||
			    (p->pfik_flagrefs == 1));

			if (!ISSET(p->pfik_flags_new, PFI_IFLAG_SKIP) &&
			    (p->pfik_flagrefs == 1))
				pfi_kif_unref(p, PFI_KIF_REF_FLAG);
		} else
			return (ESRCH);

	} else
		RB_FOREACH_SAFE(p, pfi_ifhead, &pfi_ifs, w) {
			p->pfik_flags_new = p->pfik_flags & ~flags;

			KASSERT((p->pfik_flagrefs == 0) ||
			    (p->pfik_flagrefs == 1));

			if (!ISSET(p->pfik_flags_new, PFI_IFLAG_SKIP) &&
			    (p->pfik_flagrefs == 1))
				pfi_kif_unref(p, PFI_KIF_REF_FLAG);
		}

	return (0);
}

void
pfi_xcommit(void)
{
	struct pfi_kif	*p, *gkif;
	struct ifg_list	*g;
	struct ifnet	*ifp;
	size_t n;

	PF_ASSERT_LOCKED();

	RB_FOREACH(p, pfi_ifhead, &pfi_ifs) {
		p->pfik_flags = p->pfik_flags_new;
		n = strlen(p->pfik_name);
		ifp = p->pfik_ifp;
		/*
		 * if kif is backed by existing interface, then we must use
		 * skip flags found in groups. We use pfik_flags_new, otherwise
		 * we would need to do two RB_FOREACH() passes: the first to
		 * commit group changes the second to commit flag changes for
		 * interfaces.
		 */
		if (ifp != NULL)
			TAILQ_FOREACH(g, &ifp->if_groups, ifgl_next) {
				gkif =
				    (struct pfi_kif *)g->ifgl_group->ifg_pf_kif;
				KASSERT(gkif != NULL);
				p->pfik_flags |= gkif->pfik_flags_new;
			}
	}
}

/* from pf_print_state.c */
int
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

