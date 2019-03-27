/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Cedric Berger
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
 *	$OpenBSD: pf_table.c,v 1.79 2008/10/08 06:24:50 mcbride Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/refcount.h>
#include <sys/socket.h>
#include <vm/uma.h>

#include <net/if.h>
#include <net/vnet.h>
#include <net/pfvar.h>

#define DPFPRINTF(n, x) if (V_pf_status.debug >= (n)) printf x

#define	ACCEPT_FLAGS(flags, oklist)		\
	do {					\
		if ((flags & ~(oklist)) &	\
		    PFR_FLAG_ALLMASK)		\
			return (EINVAL);	\
	} while (0)

#define	FILLIN_SIN(sin, addr)			\
	do {					\
		(sin).sin_len = sizeof(sin);	\
		(sin).sin_family = AF_INET;	\
		(sin).sin_addr = (addr);	\
	} while (0)

#define	FILLIN_SIN6(sin6, addr)			\
	do {					\
		(sin6).sin6_len = sizeof(sin6);	\
		(sin6).sin6_family = AF_INET6;	\
		(sin6).sin6_addr = (addr);	\
	} while (0)

#define	SWAP(type, a1, a2)			\
	do {					\
		type tmp = a1;			\
		a1 = a2;			\
		a2 = tmp;			\
	} while (0)

#define	SUNION2PF(su, af) (((af)==AF_INET) ?	\
    (struct pf_addr *)&(su)->sin.sin_addr :	\
    (struct pf_addr *)&(su)->sin6.sin6_addr)

#define	AF_BITS(af)		(((af)==AF_INET)?32:128)
#define	ADDR_NETWORK(ad)	((ad)->pfra_net < AF_BITS((ad)->pfra_af))
#define	KENTRY_NETWORK(ke)	((ke)->pfrke_net < AF_BITS((ke)->pfrke_af))
#define	KENTRY_RNF_ROOT(ke) \
		((((struct radix_node *)(ke))->rn_flags & RNF_ROOT) != 0)

#define	NO_ADDRESSES		(-1)
#define	ENQUEUE_UNMARKED_ONLY	(1)
#define	INVERT_NEG_FLAG		(1)

struct pfr_walktree {
	enum pfrw_op {
		PFRW_MARK,
		PFRW_SWEEP,
		PFRW_ENQUEUE,
		PFRW_GET_ADDRS,
		PFRW_GET_ASTATS,
		PFRW_POOL_GET,
		PFRW_DYNADDR_UPDATE
	}	 pfrw_op;
	union {
		struct pfr_addr		*pfrw1_addr;
		struct pfr_astats	*pfrw1_astats;
		struct pfr_kentryworkq	*pfrw1_workq;
		struct pfr_kentry	*pfrw1_kentry;
		struct pfi_dynaddr	*pfrw1_dyn;
	}	 pfrw_1;
	int	 pfrw_free;
	int	 pfrw_flags;
};
#define	pfrw_addr	pfrw_1.pfrw1_addr
#define	pfrw_astats	pfrw_1.pfrw1_astats
#define	pfrw_workq	pfrw_1.pfrw1_workq
#define	pfrw_kentry	pfrw_1.pfrw1_kentry
#define	pfrw_dyn	pfrw_1.pfrw1_dyn
#define	pfrw_cnt	pfrw_free

#define	senderr(e)	do { rv = (e); goto _bad; } while (0)

static MALLOC_DEFINE(M_PFTABLE, "pf_table", "pf(4) tables structures");
VNET_DEFINE_STATIC(uma_zone_t, pfr_kentry_z);
#define	V_pfr_kentry_z		VNET(pfr_kentry_z)

static struct pf_addr	 pfr_ffaddr = {
	.addr32 = { 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff }
};

static void		 pfr_copyout_astats(struct pfr_astats *,
			    const struct pfr_kentry *,
			    const struct pfr_walktree *);
static void		 pfr_copyout_addr(struct pfr_addr *,
			    const struct pfr_kentry *ke);
static int		 pfr_validate_addr(struct pfr_addr *);
static void		 pfr_enqueue_addrs(struct pfr_ktable *,
			    struct pfr_kentryworkq *, int *, int);
static void		 pfr_mark_addrs(struct pfr_ktable *);
static struct pfr_kentry
			*pfr_lookup_addr(struct pfr_ktable *,
			    struct pfr_addr *, int);
static bool		 pfr_create_kentry_counter(struct pfr_kcounters *,
			    int, int);
static struct pfr_kentry *pfr_create_kentry(struct pfr_addr *);
static void		 pfr_destroy_kentries(struct pfr_kentryworkq *);
static void		 pfr_destroy_kentry_counter(struct pfr_kcounters *,
			    int, int);
static void		 pfr_destroy_kentry(struct pfr_kentry *);
static void		 pfr_insert_kentries(struct pfr_ktable *,
			    struct pfr_kentryworkq *, long);
static void		 pfr_remove_kentries(struct pfr_ktable *,
			    struct pfr_kentryworkq *);
static void		 pfr_clstats_kentries(struct pfr_kentryworkq *, long,
			    int);
static void		 pfr_reset_feedback(struct pfr_addr *, int);
static void		 pfr_prepare_network(union sockaddr_union *, int, int);
static int		 pfr_route_kentry(struct pfr_ktable *,
			    struct pfr_kentry *);
static int		 pfr_unroute_kentry(struct pfr_ktable *,
			    struct pfr_kentry *);
static int		 pfr_walktree(struct radix_node *, void *);
static int		 pfr_validate_table(struct pfr_table *, int, int);
static int		 pfr_fix_anchor(char *);
static void		 pfr_commit_ktable(struct pfr_ktable *, long);
static void		 pfr_insert_ktables(struct pfr_ktableworkq *);
static void		 pfr_insert_ktable(struct pfr_ktable *);
static void		 pfr_setflags_ktables(struct pfr_ktableworkq *);
static void		 pfr_setflags_ktable(struct pfr_ktable *, int);
static void		 pfr_clstats_ktables(struct pfr_ktableworkq *, long,
			    int);
static void		 pfr_clstats_ktable(struct pfr_ktable *, long, int);
static struct pfr_ktable
			*pfr_create_ktable(struct pfr_table *, long, int);
static void		 pfr_destroy_ktables(struct pfr_ktableworkq *, int);
static void		 pfr_destroy_ktable(struct pfr_ktable *, int);
static int		 pfr_ktable_compare(struct pfr_ktable *,
			    struct pfr_ktable *);
static struct pfr_ktable
			*pfr_lookup_table(struct pfr_table *);
static void		 pfr_clean_node_mask(struct pfr_ktable *,
			    struct pfr_kentryworkq *);
static int		 pfr_skip_table(struct pfr_table *,
			    struct pfr_ktable *, int);
static struct pfr_kentry
			*pfr_kentry_byidx(struct pfr_ktable *, int, int);

static RB_PROTOTYPE(pfr_ktablehead, pfr_ktable, pfrkt_tree, pfr_ktable_compare);
static RB_GENERATE(pfr_ktablehead, pfr_ktable, pfrkt_tree, pfr_ktable_compare);

VNET_DEFINE_STATIC(struct pfr_ktablehead, pfr_ktables);
#define	V_pfr_ktables	VNET(pfr_ktables)

VNET_DEFINE_STATIC(struct pfr_table, pfr_nulltable);
#define	V_pfr_nulltable	VNET(pfr_nulltable)

VNET_DEFINE_STATIC(int, pfr_ktable_cnt);
#define V_pfr_ktable_cnt	VNET(pfr_ktable_cnt)

void
pfr_initialize(void)
{

	V_pfr_kentry_z = uma_zcreate("pf table entries",
	    sizeof(struct pfr_kentry), NULL, NULL, NULL, NULL, UMA_ALIGN_PTR,
	    0);
	V_pf_limits[PF_LIMIT_TABLE_ENTRIES].zone = V_pfr_kentry_z;
	V_pf_limits[PF_LIMIT_TABLE_ENTRIES].limit = PFR_KENTRY_HIWAT;
}

void
pfr_cleanup(void)
{

	uma_zdestroy(V_pfr_kentry_z);
}

int
pfr_clr_addrs(struct pfr_table *tbl, int *ndel, int flags)
{
	struct pfr_ktable	*kt;
	struct pfr_kentryworkq	 workq;

	PF_RULES_WASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	if (pfr_validate_table(tbl, 0, flags & PFR_FLAG_USERIOCTL))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);
	if (kt->pfrkt_flags & PFR_TFLAG_CONST)
		return (EPERM);
	pfr_enqueue_addrs(kt, &workq, ndel, 0);

	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_remove_kentries(kt, &workq);
		KASSERT(kt->pfrkt_cnt == 0, ("%s: non-null pfrkt_cnt", __func__));
	}
	return (0);
}

int
pfr_add_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *nadd, int flags)
{
	struct pfr_ktable	*kt, *tmpkt;
	struct pfr_kentryworkq	 workq;
	struct pfr_kentry	*p, *q;
	struct pfr_addr		*ad;
	int			 i, rv, xadd = 0;
	long			 tzero = time_second;

	PF_RULES_WASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_FEEDBACK);
	if (pfr_validate_table(tbl, 0, flags & PFR_FLAG_USERIOCTL))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);
	if (kt->pfrkt_flags & PFR_TFLAG_CONST)
		return (EPERM);
	tmpkt = pfr_create_ktable(&V_pfr_nulltable, 0, 0);
	if (tmpkt == NULL)
		return (ENOMEM);
	SLIST_INIT(&workq);
	for (i = 0, ad = addr; i < size; i++, ad++) {
		if (pfr_validate_addr(ad))
			senderr(EINVAL);
		p = pfr_lookup_addr(kt, ad, 1);
		q = pfr_lookup_addr(tmpkt, ad, 1);
		if (flags & PFR_FLAG_FEEDBACK) {
			if (q != NULL)
				ad->pfra_fback = PFR_FB_DUPLICATE;
			else if (p == NULL)
				ad->pfra_fback = PFR_FB_ADDED;
			else if (p->pfrke_not != ad->pfra_not)
				ad->pfra_fback = PFR_FB_CONFLICT;
			else
				ad->pfra_fback = PFR_FB_NONE;
		}
		if (p == NULL && q == NULL) {
			p = pfr_create_kentry(ad);
			if (p == NULL)
				senderr(ENOMEM);
			if (pfr_route_kentry(tmpkt, p)) {
				pfr_destroy_kentry(p);
				ad->pfra_fback = PFR_FB_NONE;
			} else {
				SLIST_INSERT_HEAD(&workq, p, pfrke_workq);
				xadd++;
			}
		}
	}
	pfr_clean_node_mask(tmpkt, &workq);
	if (!(flags & PFR_FLAG_DUMMY))
		pfr_insert_kentries(kt, &workq, tzero);
	else
		pfr_destroy_kentries(&workq);
	if (nadd != NULL)
		*nadd = xadd;
	pfr_destroy_ktable(tmpkt, 0);
	return (0);
_bad:
	pfr_clean_node_mask(tmpkt, &workq);
	pfr_destroy_kentries(&workq);
	if (flags & PFR_FLAG_FEEDBACK)
		pfr_reset_feedback(addr, size);
	pfr_destroy_ktable(tmpkt, 0);
	return (rv);
}

int
pfr_del_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *ndel, int flags)
{
	struct pfr_ktable	*kt;
	struct pfr_kentryworkq	 workq;
	struct pfr_kentry	*p;
	struct pfr_addr		*ad;
	int			 i, rv, xdel = 0, log = 1;

	PF_RULES_WASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_FEEDBACK);
	if (pfr_validate_table(tbl, 0, flags & PFR_FLAG_USERIOCTL))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);
	if (kt->pfrkt_flags & PFR_TFLAG_CONST)
		return (EPERM);
	/*
	 * there are two algorithms to choose from here.
	 * with:
	 *   n: number of addresses to delete
	 *   N: number of addresses in the table
	 *
	 * one is O(N) and is better for large 'n'
	 * one is O(n*LOG(N)) and is better for small 'n'
	 *
	 * following code try to decide which one is best.
	 */
	for (i = kt->pfrkt_cnt; i > 0; i >>= 1)
		log++;
	if (size > kt->pfrkt_cnt/log) {
		/* full table scan */
		pfr_mark_addrs(kt);
	} else {
		/* iterate over addresses to delete */
		for (i = 0, ad = addr; i < size; i++, ad++) {
			if (pfr_validate_addr(ad))
				return (EINVAL);
			p = pfr_lookup_addr(kt, ad, 1);
			if (p != NULL)
				p->pfrke_mark = 0;
		}
	}
	SLIST_INIT(&workq);
	for (i = 0, ad = addr; i < size; i++, ad++) {
		if (pfr_validate_addr(ad))
			senderr(EINVAL);
		p = pfr_lookup_addr(kt, ad, 1);
		if (flags & PFR_FLAG_FEEDBACK) {
			if (p == NULL)
				ad->pfra_fback = PFR_FB_NONE;
			else if (p->pfrke_not != ad->pfra_not)
				ad->pfra_fback = PFR_FB_CONFLICT;
			else if (p->pfrke_mark)
				ad->pfra_fback = PFR_FB_DUPLICATE;
			else
				ad->pfra_fback = PFR_FB_DELETED;
		}
		if (p != NULL && p->pfrke_not == ad->pfra_not &&
		    !p->pfrke_mark) {
			p->pfrke_mark = 1;
			SLIST_INSERT_HEAD(&workq, p, pfrke_workq);
			xdel++;
		}
	}
	if (!(flags & PFR_FLAG_DUMMY))
		pfr_remove_kentries(kt, &workq);
	if (ndel != NULL)
		*ndel = xdel;
	return (0);
_bad:
	if (flags & PFR_FLAG_FEEDBACK)
		pfr_reset_feedback(addr, size);
	return (rv);
}

int
pfr_set_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *size2, int *nadd, int *ndel, int *nchange, int flags,
    u_int32_t ignore_pfrt_flags)
{
	struct pfr_ktable	*kt, *tmpkt;
	struct pfr_kentryworkq	 addq, delq, changeq;
	struct pfr_kentry	*p, *q;
	struct pfr_addr		 ad;
	int			 i, rv, xadd = 0, xdel = 0, xchange = 0;
	long			 tzero = time_second;

	PF_RULES_WASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_FEEDBACK);
	if (pfr_validate_table(tbl, ignore_pfrt_flags, flags &
	    PFR_FLAG_USERIOCTL))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);
	if (kt->pfrkt_flags & PFR_TFLAG_CONST)
		return (EPERM);
	tmpkt = pfr_create_ktable(&V_pfr_nulltable, 0, 0);
	if (tmpkt == NULL)
		return (ENOMEM);
	pfr_mark_addrs(kt);
	SLIST_INIT(&addq);
	SLIST_INIT(&delq);
	SLIST_INIT(&changeq);
	for (i = 0; i < size; i++) {
		/*
		 * XXXGL: undertand pf_if usage of this function
		 * and make ad a moving pointer
		 */
		bcopy(addr + i, &ad, sizeof(ad));
		if (pfr_validate_addr(&ad))
			senderr(EINVAL);
		ad.pfra_fback = PFR_FB_NONE;
		p = pfr_lookup_addr(kt, &ad, 1);
		if (p != NULL) {
			if (p->pfrke_mark) {
				ad.pfra_fback = PFR_FB_DUPLICATE;
				goto _skip;
			}
			p->pfrke_mark = 1;
			if (p->pfrke_not != ad.pfra_not) {
				SLIST_INSERT_HEAD(&changeq, p, pfrke_workq);
				ad.pfra_fback = PFR_FB_CHANGED;
				xchange++;
			}
		} else {
			q = pfr_lookup_addr(tmpkt, &ad, 1);
			if (q != NULL) {
				ad.pfra_fback = PFR_FB_DUPLICATE;
				goto _skip;
			}
			p = pfr_create_kentry(&ad);
			if (p == NULL)
				senderr(ENOMEM);
			if (pfr_route_kentry(tmpkt, p)) {
				pfr_destroy_kentry(p);
				ad.pfra_fback = PFR_FB_NONE;
			} else {
				SLIST_INSERT_HEAD(&addq, p, pfrke_workq);
				ad.pfra_fback = PFR_FB_ADDED;
				xadd++;
			}
		}
_skip:
		if (flags & PFR_FLAG_FEEDBACK)
			bcopy(&ad, addr + i, sizeof(ad));
	}
	pfr_enqueue_addrs(kt, &delq, &xdel, ENQUEUE_UNMARKED_ONLY);
	if ((flags & PFR_FLAG_FEEDBACK) && *size2) {
		if (*size2 < size+xdel) {
			*size2 = size+xdel;
			senderr(0);
		}
		i = 0;
		SLIST_FOREACH(p, &delq, pfrke_workq) {
			pfr_copyout_addr(&ad, p);
			ad.pfra_fback = PFR_FB_DELETED;
			bcopy(&ad, addr + size + i, sizeof(ad));
			i++;
		}
	}
	pfr_clean_node_mask(tmpkt, &addq);
	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_insert_kentries(kt, &addq, tzero);
		pfr_remove_kentries(kt, &delq);
		pfr_clstats_kentries(&changeq, tzero, INVERT_NEG_FLAG);
	} else
		pfr_destroy_kentries(&addq);
	if (nadd != NULL)
		*nadd = xadd;
	if (ndel != NULL)
		*ndel = xdel;
	if (nchange != NULL)
		*nchange = xchange;
	if ((flags & PFR_FLAG_FEEDBACK) && size2)
		*size2 = size+xdel;
	pfr_destroy_ktable(tmpkt, 0);
	return (0);
_bad:
	pfr_clean_node_mask(tmpkt, &addq);
	pfr_destroy_kentries(&addq);
	if (flags & PFR_FLAG_FEEDBACK)
		pfr_reset_feedback(addr, size);
	pfr_destroy_ktable(tmpkt, 0);
	return (rv);
}

int
pfr_tst_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
	int *nmatch, int flags)
{
	struct pfr_ktable	*kt;
	struct pfr_kentry	*p;
	struct pfr_addr		*ad;
	int			 i, xmatch = 0;

	PF_RULES_RASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_REPLACE);
	if (pfr_validate_table(tbl, 0, 0))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);

	for (i = 0, ad = addr; i < size; i++, ad++) {
		if (pfr_validate_addr(ad))
			return (EINVAL);
		if (ADDR_NETWORK(ad))
			return (EINVAL);
		p = pfr_lookup_addr(kt, ad, 0);
		if (flags & PFR_FLAG_REPLACE)
			pfr_copyout_addr(ad, p);
		ad->pfra_fback = (p == NULL) ? PFR_FB_NONE :
		    (p->pfrke_not ? PFR_FB_NOTMATCH : PFR_FB_MATCH);
		if (p != NULL && !p->pfrke_not)
			xmatch++;
	}
	if (nmatch != NULL)
		*nmatch = xmatch;
	return (0);
}

int
pfr_get_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int *size,
	int flags)
{
	struct pfr_ktable	*kt;
	struct pfr_walktree	 w;
	int			 rv;

	PF_RULES_RASSERT();

	ACCEPT_FLAGS(flags, 0);
	if (pfr_validate_table(tbl, 0, 0))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);
	if (kt->pfrkt_cnt > *size) {
		*size = kt->pfrkt_cnt;
		return (0);
	}

	bzero(&w, sizeof(w));
	w.pfrw_op = PFRW_GET_ADDRS;
	w.pfrw_addr = addr;
	w.pfrw_free = kt->pfrkt_cnt;
	rv = kt->pfrkt_ip4->rnh_walktree(&kt->pfrkt_ip4->rh, pfr_walktree, &w);
	if (!rv)
		rv = kt->pfrkt_ip6->rnh_walktree(&kt->pfrkt_ip6->rh,
		    pfr_walktree, &w);
	if (rv)
		return (rv);

	KASSERT(w.pfrw_free == 0, ("%s: corruption detected (%d)", __func__,
	    w.pfrw_free));

	*size = kt->pfrkt_cnt;
	return (0);
}

int
pfr_get_astats(struct pfr_table *tbl, struct pfr_astats *addr, int *size,
	int flags)
{
	struct pfr_ktable	*kt;
	struct pfr_walktree	 w;
	struct pfr_kentryworkq	 workq;
	int			 rv;
	long			 tzero = time_second;

	PF_RULES_RASSERT();

	/* XXX PFR_FLAG_CLSTATS disabled */
	ACCEPT_FLAGS(flags, 0);
	if (pfr_validate_table(tbl, 0, 0))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);
	if (kt->pfrkt_cnt > *size) {
		*size = kt->pfrkt_cnt;
		return (0);
	}

	bzero(&w, sizeof(w));
	w.pfrw_op = PFRW_GET_ASTATS;
	w.pfrw_astats = addr;
	w.pfrw_free = kt->pfrkt_cnt;
	/*
	 * Flags below are for backward compatibility. It was possible to have
	 * a table without per-entry counters. Now they are always allocated,
	 * we just discard data when reading it if table is not configured to
	 * have counters.
	 */
	w.pfrw_flags = kt->pfrkt_flags;
	rv = kt->pfrkt_ip4->rnh_walktree(&kt->pfrkt_ip4->rh, pfr_walktree, &w);
	if (!rv)
		rv = kt->pfrkt_ip6->rnh_walktree(&kt->pfrkt_ip6->rh,
		    pfr_walktree, &w);
	if (!rv && (flags & PFR_FLAG_CLSTATS)) {
		pfr_enqueue_addrs(kt, &workq, NULL, 0);
		pfr_clstats_kentries(&workq, tzero, 0);
	}
	if (rv)
		return (rv);

	if (w.pfrw_free) {
		printf("pfr_get_astats: corruption detected (%d).\n",
		    w.pfrw_free);
		return (ENOTTY);
	}
	*size = kt->pfrkt_cnt;
	return (0);
}

int
pfr_clr_astats(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *nzero, int flags)
{
	struct pfr_ktable	*kt;
	struct pfr_kentryworkq	 workq;
	struct pfr_kentry	*p;
	struct pfr_addr		*ad;
	int			 i, rv, xzero = 0;

	PF_RULES_WASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_FEEDBACK);
	if (pfr_validate_table(tbl, 0, 0))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);
	SLIST_INIT(&workq);
	for (i = 0, ad = addr; i < size; i++, ad++) {
		if (pfr_validate_addr(ad))
			senderr(EINVAL);
		p = pfr_lookup_addr(kt, ad, 1);
		if (flags & PFR_FLAG_FEEDBACK) {
			ad->pfra_fback = (p != NULL) ?
			    PFR_FB_CLEARED : PFR_FB_NONE;
		}
		if (p != NULL) {
			SLIST_INSERT_HEAD(&workq, p, pfrke_workq);
			xzero++;
		}
	}

	if (!(flags & PFR_FLAG_DUMMY))
		pfr_clstats_kentries(&workq, 0, 0);
	if (nzero != NULL)
		*nzero = xzero;
	return (0);
_bad:
	if (flags & PFR_FLAG_FEEDBACK)
		pfr_reset_feedback(addr, size);
	return (rv);
}

static int
pfr_validate_addr(struct pfr_addr *ad)
{
	int i;

	switch (ad->pfra_af) {
#ifdef INET
	case AF_INET:
		if (ad->pfra_net > 32)
			return (-1);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		if (ad->pfra_net > 128)
			return (-1);
		break;
#endif /* INET6 */
	default:
		return (-1);
	}
	if (ad->pfra_net < 128 &&
		(((caddr_t)ad)[ad->pfra_net/8] & (0xFF >> (ad->pfra_net%8))))
			return (-1);
	for (i = (ad->pfra_net+7)/8; i < sizeof(ad->pfra_u); i++)
		if (((caddr_t)ad)[i])
			return (-1);
	if (ad->pfra_not && ad->pfra_not != 1)
		return (-1);
	if (ad->pfra_fback)
		return (-1);
	return (0);
}

static void
pfr_enqueue_addrs(struct pfr_ktable *kt, struct pfr_kentryworkq *workq,
	int *naddr, int sweep)
{
	struct pfr_walktree	w;

	SLIST_INIT(workq);
	bzero(&w, sizeof(w));
	w.pfrw_op = sweep ? PFRW_SWEEP : PFRW_ENQUEUE;
	w.pfrw_workq = workq;
	if (kt->pfrkt_ip4 != NULL)
		if (kt->pfrkt_ip4->rnh_walktree(&kt->pfrkt_ip4->rh,
		    pfr_walktree, &w))
			printf("pfr_enqueue_addrs: IPv4 walktree failed.\n");
	if (kt->pfrkt_ip6 != NULL)
		if (kt->pfrkt_ip6->rnh_walktree(&kt->pfrkt_ip6->rh,
		    pfr_walktree, &w))
			printf("pfr_enqueue_addrs: IPv6 walktree failed.\n");
	if (naddr != NULL)
		*naddr = w.pfrw_cnt;
}

static void
pfr_mark_addrs(struct pfr_ktable *kt)
{
	struct pfr_walktree	w;

	bzero(&w, sizeof(w));
	w.pfrw_op = PFRW_MARK;
	if (kt->pfrkt_ip4->rnh_walktree(&kt->pfrkt_ip4->rh, pfr_walktree, &w))
		printf("pfr_mark_addrs: IPv4 walktree failed.\n");
	if (kt->pfrkt_ip6->rnh_walktree(&kt->pfrkt_ip6->rh, pfr_walktree, &w))
		printf("pfr_mark_addrs: IPv6 walktree failed.\n");
}


static struct pfr_kentry *
pfr_lookup_addr(struct pfr_ktable *kt, struct pfr_addr *ad, int exact)
{
	union sockaddr_union	 sa, mask;
	struct radix_head	*head = NULL;
	struct pfr_kentry	*ke;

	PF_RULES_ASSERT();

	bzero(&sa, sizeof(sa));
	if (ad->pfra_af == AF_INET) {
		FILLIN_SIN(sa.sin, ad->pfra_ip4addr);
		head = &kt->pfrkt_ip4->rh;
	} else if ( ad->pfra_af == AF_INET6 ) {
		FILLIN_SIN6(sa.sin6, ad->pfra_ip6addr);
		head = &kt->pfrkt_ip6->rh;
	}
	if (ADDR_NETWORK(ad)) {
		pfr_prepare_network(&mask, ad->pfra_af, ad->pfra_net);
		ke = (struct pfr_kentry *)rn_lookup(&sa, &mask, head);
		if (ke && KENTRY_RNF_ROOT(ke))
			ke = NULL;
	} else {
		ke = (struct pfr_kentry *)rn_match(&sa, head);
		if (ke && KENTRY_RNF_ROOT(ke))
			ke = NULL;
		if (exact && ke && KENTRY_NETWORK(ke))
			ke = NULL;
	}
	return (ke);
}

static bool
pfr_create_kentry_counter(struct pfr_kcounters *kc, int pfr_dir, int pfr_op)
{
	kc->pfrkc_packets[pfr_dir][pfr_op] = counter_u64_alloc(M_NOWAIT);
	if (! kc->pfrkc_packets[pfr_dir][pfr_op])
		return (false);

	kc->pfrkc_bytes[pfr_dir][pfr_op] = counter_u64_alloc(M_NOWAIT);
	if (! kc->pfrkc_bytes[pfr_dir][pfr_op]) {
		/* Previous allocation will be freed through
		 * pfr_destroy_kentry() */
		return (false);
	}

	kc->pfrkc_tzero = 0;

	return (true);
}

static struct pfr_kentry *
pfr_create_kentry(struct pfr_addr *ad)
{
	struct pfr_kentry	*ke;
	int pfr_dir, pfr_op;

	ke =  uma_zalloc(V_pfr_kentry_z, M_NOWAIT | M_ZERO);
	if (ke == NULL)
		return (NULL);

	if (ad->pfra_af == AF_INET)
		FILLIN_SIN(ke->pfrke_sa.sin, ad->pfra_ip4addr);
	else if (ad->pfra_af == AF_INET6)
		FILLIN_SIN6(ke->pfrke_sa.sin6, ad->pfra_ip6addr);
	ke->pfrke_af = ad->pfra_af;
	ke->pfrke_net = ad->pfra_net;
	ke->pfrke_not = ad->pfra_not;
	for (pfr_dir = 0; pfr_dir < PFR_DIR_MAX; pfr_dir ++)
		for (pfr_op = 0; pfr_op < PFR_OP_ADDR_MAX; pfr_op ++) {
			if (! pfr_create_kentry_counter(&ke->pfrke_counters,
			    pfr_dir, pfr_op)) {
				pfr_destroy_kentry(ke);
				return (NULL);
			}
		}
	return (ke);
}

static void
pfr_destroy_kentries(struct pfr_kentryworkq *workq)
{
	struct pfr_kentry	*p, *q;

	for (p = SLIST_FIRST(workq); p != NULL; p = q) {
		q = SLIST_NEXT(p, pfrke_workq);
		pfr_destroy_kentry(p);
	}
}

static void
pfr_destroy_kentry_counter(struct pfr_kcounters *kc, int pfr_dir, int pfr_op)
{
	counter_u64_free(kc->pfrkc_packets[pfr_dir][pfr_op]);
	counter_u64_free(kc->pfrkc_bytes[pfr_dir][pfr_op]);
}

static void
pfr_destroy_kentry(struct pfr_kentry *ke)
{
	int pfr_dir, pfr_op;

	for (pfr_dir = 0; pfr_dir < PFR_DIR_MAX; pfr_dir ++)
		for (pfr_op = 0; pfr_op < PFR_OP_ADDR_MAX; pfr_op ++)
			pfr_destroy_kentry_counter(&ke->pfrke_counters,
			    pfr_dir, pfr_op);

	uma_zfree(V_pfr_kentry_z, ke);
}

static void
pfr_insert_kentries(struct pfr_ktable *kt,
    struct pfr_kentryworkq *workq, long tzero)
{
	struct pfr_kentry	*p;
	int			 rv, n = 0;

	SLIST_FOREACH(p, workq, pfrke_workq) {
		rv = pfr_route_kentry(kt, p);
		if (rv) {
			printf("pfr_insert_kentries: cannot route entry "
			    "(code=%d).\n", rv);
			break;
		}
		p->pfrke_counters.pfrkc_tzero = tzero;
		n++;
	}
	kt->pfrkt_cnt += n;
}

int
pfr_insert_kentry(struct pfr_ktable *kt, struct pfr_addr *ad, long tzero)
{
	struct pfr_kentry	*p;
	int			 rv;

	p = pfr_lookup_addr(kt, ad, 1);
	if (p != NULL)
		return (0);
	p = pfr_create_kentry(ad);
	if (p == NULL)
		return (ENOMEM);

	rv = pfr_route_kentry(kt, p);
	if (rv)
		return (rv);

	p->pfrke_counters.pfrkc_tzero = tzero;
	kt->pfrkt_cnt++;

	return (0);
}

static void
pfr_remove_kentries(struct pfr_ktable *kt,
    struct pfr_kentryworkq *workq)
{
	struct pfr_kentry	*p;
	int			 n = 0;

	SLIST_FOREACH(p, workq, pfrke_workq) {
		pfr_unroute_kentry(kt, p);
		n++;
	}
	kt->pfrkt_cnt -= n;
	pfr_destroy_kentries(workq);
}

static void
pfr_clean_node_mask(struct pfr_ktable *kt,
    struct pfr_kentryworkq *workq)
{
	struct pfr_kentry	*p;

	SLIST_FOREACH(p, workq, pfrke_workq)
		pfr_unroute_kentry(kt, p);
}

static void
pfr_clstats_kentries(struct pfr_kentryworkq *workq, long tzero, int negchange)
{
	struct pfr_kentry	*p;
	int			 pfr_dir, pfr_op;

	SLIST_FOREACH(p, workq, pfrke_workq) {
		if (negchange)
			p->pfrke_not = !p->pfrke_not;
		for (pfr_dir = 0; pfr_dir < PFR_DIR_MAX; pfr_dir ++) {
			for (pfr_op = 0; pfr_op < PFR_OP_ADDR_MAX; pfr_op ++) {
				counter_u64_zero(p->pfrke_counters.
					pfrkc_packets[pfr_dir][pfr_op]);
				counter_u64_zero(p->pfrke_counters.
					pfrkc_bytes[pfr_dir][pfr_op]);
			}
		}
		p->pfrke_counters.pfrkc_tzero = tzero;
	}
}

static void
pfr_reset_feedback(struct pfr_addr *addr, int size)
{
	struct pfr_addr	*ad;
	int		i;

	for (i = 0, ad = addr; i < size; i++, ad++)
		ad->pfra_fback = PFR_FB_NONE;
}

static void
pfr_prepare_network(union sockaddr_union *sa, int af, int net)
{
	int	i;

	bzero(sa, sizeof(*sa));
	if (af == AF_INET) {
		sa->sin.sin_len = sizeof(sa->sin);
		sa->sin.sin_family = AF_INET;
		sa->sin.sin_addr.s_addr = net ? htonl(-1 << (32-net)) : 0;
	} else if (af == AF_INET6) {
		sa->sin6.sin6_len = sizeof(sa->sin6);
		sa->sin6.sin6_family = AF_INET6;
		for (i = 0; i < 4; i++) {
			if (net <= 32) {
				sa->sin6.sin6_addr.s6_addr32[i] =
				    net ? htonl(-1 << (32-net)) : 0;
				break;
			}
			sa->sin6.sin6_addr.s6_addr32[i] = 0xFFFFFFFF;
			net -= 32;
		}
	}
}

static int
pfr_route_kentry(struct pfr_ktable *kt, struct pfr_kentry *ke)
{
	union sockaddr_union	 mask;
	struct radix_node	*rn;
	struct radix_head	*head = NULL;

	PF_RULES_WASSERT();

	bzero(ke->pfrke_node, sizeof(ke->pfrke_node));
	if (ke->pfrke_af == AF_INET)
		head = &kt->pfrkt_ip4->rh;
	else if (ke->pfrke_af == AF_INET6)
		head = &kt->pfrkt_ip6->rh;

	if (KENTRY_NETWORK(ke)) {
		pfr_prepare_network(&mask, ke->pfrke_af, ke->pfrke_net);
		rn = rn_addroute(&ke->pfrke_sa, &mask, head, ke->pfrke_node);
	} else
		rn = rn_addroute(&ke->pfrke_sa, NULL, head, ke->pfrke_node);

	return (rn == NULL ? -1 : 0);
}

static int
pfr_unroute_kentry(struct pfr_ktable *kt, struct pfr_kentry *ke)
{
	union sockaddr_union	 mask;
	struct radix_node	*rn;
	struct radix_head	*head = NULL;

	if (ke->pfrke_af == AF_INET)
		head = &kt->pfrkt_ip4->rh;
	else if (ke->pfrke_af == AF_INET6)
		head = &kt->pfrkt_ip6->rh;

	if (KENTRY_NETWORK(ke)) {
		pfr_prepare_network(&mask, ke->pfrke_af, ke->pfrke_net);
		rn = rn_delete(&ke->pfrke_sa, &mask, head);
	} else
		rn = rn_delete(&ke->pfrke_sa, NULL, head);

	if (rn == NULL) {
		printf("pfr_unroute_kentry: delete failed.\n");
		return (-1);
	}
	return (0);
}

static void
pfr_copyout_addr(struct pfr_addr *ad, const struct pfr_kentry *ke)
{
	bzero(ad, sizeof(*ad));
	if (ke == NULL)
		return;
	ad->pfra_af = ke->pfrke_af;
	ad->pfra_net = ke->pfrke_net;
	ad->pfra_not = ke->pfrke_not;
	if (ad->pfra_af == AF_INET)
		ad->pfra_ip4addr = ke->pfrke_sa.sin.sin_addr;
	else if (ad->pfra_af == AF_INET6)
		ad->pfra_ip6addr = ke->pfrke_sa.sin6.sin6_addr;
}

static void
pfr_copyout_astats(struct pfr_astats *as, const struct pfr_kentry *ke,
    const struct pfr_walktree *w)
{
	int dir, op;
	const struct pfr_kcounters *kc = &ke->pfrke_counters;

	pfr_copyout_addr(&as->pfras_a, ke);
	as->pfras_tzero = kc->pfrkc_tzero;

	if (! (w->pfrw_flags & PFR_TFLAG_COUNTERS)) {
		bzero(as->pfras_packets, sizeof(as->pfras_packets));
		bzero(as->pfras_bytes, sizeof(as->pfras_bytes));
		as->pfras_a.pfra_fback = PFR_FB_NOCOUNT;
		return;
	}

	for (dir = 0; dir < PFR_DIR_MAX; dir ++) {
		for (op = 0; op < PFR_OP_ADDR_MAX; op ++) {
			as->pfras_packets[dir][op] =
			    counter_u64_fetch(kc->pfrkc_packets[dir][op]);
			as->pfras_bytes[dir][op] =
			    counter_u64_fetch(kc->pfrkc_bytes[dir][op]);
		}
	}
}

static int
pfr_walktree(struct radix_node *rn, void *arg)
{
	struct pfr_kentry	*ke = (struct pfr_kentry *)rn;
	struct pfr_walktree	*w = arg;

	switch (w->pfrw_op) {
	case PFRW_MARK:
		ke->pfrke_mark = 0;
		break;
	case PFRW_SWEEP:
		if (ke->pfrke_mark)
			break;
		/* FALLTHROUGH */
	case PFRW_ENQUEUE:
		SLIST_INSERT_HEAD(w->pfrw_workq, ke, pfrke_workq);
		w->pfrw_cnt++;
		break;
	case PFRW_GET_ADDRS:
		if (w->pfrw_free-- > 0) {
			pfr_copyout_addr(w->pfrw_addr, ke);
			w->pfrw_addr++;
		}
		break;
	case PFRW_GET_ASTATS:
		if (w->pfrw_free-- > 0) {
			struct pfr_astats as;

			pfr_copyout_astats(&as, ke, w);

			bcopy(&as, w->pfrw_astats, sizeof(as));
			w->pfrw_astats++;
		}
		break;
	case PFRW_POOL_GET:
		if (ke->pfrke_not)
			break; /* negative entries are ignored */
		if (!w->pfrw_cnt--) {
			w->pfrw_kentry = ke;
			return (1); /* finish search */
		}
		break;
	case PFRW_DYNADDR_UPDATE:
	    {
		union sockaddr_union	pfr_mask;

		if (ke->pfrke_af == AF_INET) {
			if (w->pfrw_dyn->pfid_acnt4++ > 0)
				break;
			pfr_prepare_network(&pfr_mask, AF_INET, ke->pfrke_net);
			w->pfrw_dyn->pfid_addr4 = *SUNION2PF(&ke->pfrke_sa,
			    AF_INET);
			w->pfrw_dyn->pfid_mask4 = *SUNION2PF(&pfr_mask,
			    AF_INET);
		} else if (ke->pfrke_af == AF_INET6){
			if (w->pfrw_dyn->pfid_acnt6++ > 0)
				break;
			pfr_prepare_network(&pfr_mask, AF_INET6, ke->pfrke_net);
			w->pfrw_dyn->pfid_addr6 = *SUNION2PF(&ke->pfrke_sa,
			    AF_INET6);
			w->pfrw_dyn->pfid_mask6 = *SUNION2PF(&pfr_mask,
			    AF_INET6);
		}
		break;
	    }
	}
	return (0);
}

int
pfr_clr_tables(struct pfr_table *filter, int *ndel, int flags)
{
	struct pfr_ktableworkq	 workq;
	struct pfr_ktable	*p;
	int			 xdel = 0;

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_ALLRSETS);
	if (pfr_fix_anchor(filter->pfrt_anchor))
		return (EINVAL);
	if (pfr_table_count(filter, flags) < 0)
		return (ENOENT);

	SLIST_INIT(&workq);
	RB_FOREACH(p, pfr_ktablehead, &V_pfr_ktables) {
		if (pfr_skip_table(filter, p, flags))
			continue;
		if (!strcmp(p->pfrkt_anchor, PF_RESERVED_ANCHOR))
			continue;
		if (!(p->pfrkt_flags & PFR_TFLAG_ACTIVE))
			continue;
		p->pfrkt_nflags = p->pfrkt_flags & ~PFR_TFLAG_ACTIVE;
		SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
		xdel++;
	}
	if (!(flags & PFR_FLAG_DUMMY))
		pfr_setflags_ktables(&workq);
	if (ndel != NULL)
		*ndel = xdel;
	return (0);
}

int
pfr_add_tables(struct pfr_table *tbl, int size, int *nadd, int flags)
{
	struct pfr_ktableworkq	 addq, changeq;
	struct pfr_ktable	*p, *q, *r, key;
	int			 i, rv, xadd = 0;
	long			 tzero = time_second;

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	SLIST_INIT(&addq);
	SLIST_INIT(&changeq);
	for (i = 0; i < size; i++) {
		bcopy(tbl+i, &key.pfrkt_t, sizeof(key.pfrkt_t));
		if (pfr_validate_table(&key.pfrkt_t, PFR_TFLAG_USRMASK,
		    flags & PFR_FLAG_USERIOCTL))
			senderr(EINVAL);
		key.pfrkt_flags |= PFR_TFLAG_ACTIVE;
		p = RB_FIND(pfr_ktablehead, &V_pfr_ktables, &key);
		if (p == NULL) {
			p = pfr_create_ktable(&key.pfrkt_t, tzero, 1);
			if (p == NULL)
				senderr(ENOMEM);
			SLIST_FOREACH(q, &addq, pfrkt_workq) {
				if (!pfr_ktable_compare(p, q)) {
					pfr_destroy_ktable(p, 0);
					goto _skip;
				}
			}
			SLIST_INSERT_HEAD(&addq, p, pfrkt_workq);
			xadd++;
			if (!key.pfrkt_anchor[0])
				goto _skip;

			/* find or create root table */
			bzero(key.pfrkt_anchor, sizeof(key.pfrkt_anchor));
			r = RB_FIND(pfr_ktablehead, &V_pfr_ktables, &key);
			if (r != NULL) {
				p->pfrkt_root = r;
				goto _skip;
			}
			SLIST_FOREACH(q, &addq, pfrkt_workq) {
				if (!pfr_ktable_compare(&key, q)) {
					p->pfrkt_root = q;
					goto _skip;
				}
			}
			key.pfrkt_flags = 0;
			r = pfr_create_ktable(&key.pfrkt_t, 0, 1);
			if (r == NULL)
				senderr(ENOMEM);
			SLIST_INSERT_HEAD(&addq, r, pfrkt_workq);
			p->pfrkt_root = r;
		} else if (!(p->pfrkt_flags & PFR_TFLAG_ACTIVE)) {
			SLIST_FOREACH(q, &changeq, pfrkt_workq)
				if (!pfr_ktable_compare(&key, q))
					goto _skip;
			p->pfrkt_nflags = (p->pfrkt_flags &
			    ~PFR_TFLAG_USRMASK) | key.pfrkt_flags;
			SLIST_INSERT_HEAD(&changeq, p, pfrkt_workq);
			xadd++;
		}
_skip:
	;
	}
	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_insert_ktables(&addq);
		pfr_setflags_ktables(&changeq);
	} else
		 pfr_destroy_ktables(&addq, 0);
	if (nadd != NULL)
		*nadd = xadd;
	return (0);
_bad:
	pfr_destroy_ktables(&addq, 0);
	return (rv);
}

int
pfr_del_tables(struct pfr_table *tbl, int size, int *ndel, int flags)
{
	struct pfr_ktableworkq	 workq;
	struct pfr_ktable	*p, *q, key;
	int			 i, xdel = 0;

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	SLIST_INIT(&workq);
	for (i = 0; i < size; i++) {
		bcopy(tbl+i, &key.pfrkt_t, sizeof(key.pfrkt_t));
		if (pfr_validate_table(&key.pfrkt_t, 0,
		    flags & PFR_FLAG_USERIOCTL))
			return (EINVAL);
		p = RB_FIND(pfr_ktablehead, &V_pfr_ktables, &key);
		if (p != NULL && (p->pfrkt_flags & PFR_TFLAG_ACTIVE)) {
			SLIST_FOREACH(q, &workq, pfrkt_workq)
				if (!pfr_ktable_compare(p, q))
					goto _skip;
			p->pfrkt_nflags = p->pfrkt_flags & ~PFR_TFLAG_ACTIVE;
			SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
			xdel++;
		}
_skip:
	;
	}

	if (!(flags & PFR_FLAG_DUMMY))
		pfr_setflags_ktables(&workq);
	if (ndel != NULL)
		*ndel = xdel;
	return (0);
}

int
pfr_get_tables(struct pfr_table *filter, struct pfr_table *tbl, int *size,
	int flags)
{
	struct pfr_ktable	*p;
	int			 n, nn;

	PF_RULES_RASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_ALLRSETS);
	if (pfr_fix_anchor(filter->pfrt_anchor))
		return (EINVAL);
	n = nn = pfr_table_count(filter, flags);
	if (n < 0)
		return (ENOENT);
	if (n > *size) {
		*size = n;
		return (0);
	}
	RB_FOREACH(p, pfr_ktablehead, &V_pfr_ktables) {
		if (pfr_skip_table(filter, p, flags))
			continue;
		if (n-- <= 0)
			continue;
		bcopy(&p->pfrkt_t, tbl++, sizeof(*tbl));
	}

	KASSERT(n == 0, ("%s: corruption detected (%d)", __func__, n));

	*size = nn;
	return (0);
}

int
pfr_get_tstats(struct pfr_table *filter, struct pfr_tstats *tbl, int *size,
	int flags)
{
	struct pfr_ktable	*p;
	struct pfr_ktableworkq	 workq;
	int			 n, nn;
	long			 tzero = time_second;
	int			 pfr_dir, pfr_op;

	/* XXX PFR_FLAG_CLSTATS disabled */
	ACCEPT_FLAGS(flags, PFR_FLAG_ALLRSETS);
	if (pfr_fix_anchor(filter->pfrt_anchor))
		return (EINVAL);
	n = nn = pfr_table_count(filter, flags);
	if (n < 0)
		return (ENOENT);
	if (n > *size) {
		*size = n;
		return (0);
	}
	SLIST_INIT(&workq);
	RB_FOREACH(p, pfr_ktablehead, &V_pfr_ktables) {
		if (pfr_skip_table(filter, p, flags))
			continue;
		if (n-- <= 0)
			continue;
		bcopy(&p->pfrkt_kts.pfrts_t, &tbl->pfrts_t,
		    sizeof(struct pfr_table));
		for (pfr_dir = 0; pfr_dir < PFR_DIR_MAX; pfr_dir ++) {
			for (pfr_op = 0; pfr_op < PFR_OP_TABLE_MAX; pfr_op ++) {
				tbl->pfrts_packets[pfr_dir][pfr_op] =
				    counter_u64_fetch(
					p->pfrkt_packets[pfr_dir][pfr_op]);
				tbl->pfrts_bytes[pfr_dir][pfr_op] =
				    counter_u64_fetch(
					p->pfrkt_bytes[pfr_dir][pfr_op]);
			}
		}
		tbl->pfrts_match = counter_u64_fetch(p->pfrkt_match);
		tbl->pfrts_nomatch = counter_u64_fetch(p->pfrkt_nomatch);
		tbl->pfrts_tzero = p->pfrkt_tzero;
		tbl->pfrts_cnt = p->pfrkt_cnt;
		for (pfr_op = 0; pfr_op < PFR_REFCNT_MAX; pfr_op++)
			tbl->pfrts_refcnt[pfr_op] = p->pfrkt_refcnt[pfr_op];
		tbl++;
		SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
	}
	if (flags & PFR_FLAG_CLSTATS)
		pfr_clstats_ktables(&workq, tzero,
		    flags & PFR_FLAG_ADDRSTOO);

	KASSERT(n == 0, ("%s: corruption detected (%d)", __func__, n));

	*size = nn;
	return (0);
}

int
pfr_clr_tstats(struct pfr_table *tbl, int size, int *nzero, int flags)
{
	struct pfr_ktableworkq	 workq;
	struct pfr_ktable	*p, key;
	int			 i, xzero = 0;
	long			 tzero = time_second;

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_ADDRSTOO);
	SLIST_INIT(&workq);
	for (i = 0; i < size; i++) {
		bcopy(tbl + i, &key.pfrkt_t, sizeof(key.pfrkt_t));
		if (pfr_validate_table(&key.pfrkt_t, 0, 0))
			return (EINVAL);
		p = RB_FIND(pfr_ktablehead, &V_pfr_ktables, &key);
		if (p != NULL) {
			SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
			xzero++;
		}
	}
	if (!(flags & PFR_FLAG_DUMMY))
		pfr_clstats_ktables(&workq, tzero, flags & PFR_FLAG_ADDRSTOO);
	if (nzero != NULL)
		*nzero = xzero;
	return (0);
}

int
pfr_set_tflags(struct pfr_table *tbl, int size, int setflag, int clrflag,
	int *nchange, int *ndel, int flags)
{
	struct pfr_ktableworkq	 workq;
	struct pfr_ktable	*p, *q, key;
	int			 i, xchange = 0, xdel = 0;

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	if ((setflag & ~PFR_TFLAG_USRMASK) ||
	    (clrflag & ~PFR_TFLAG_USRMASK) ||
	    (setflag & clrflag))
		return (EINVAL);
	SLIST_INIT(&workq);
	for (i = 0; i < size; i++) {
		bcopy(tbl + i, &key.pfrkt_t, sizeof(key.pfrkt_t));
		if (pfr_validate_table(&key.pfrkt_t, 0,
		    flags & PFR_FLAG_USERIOCTL))
			return (EINVAL);
		p = RB_FIND(pfr_ktablehead, &V_pfr_ktables, &key);
		if (p != NULL && (p->pfrkt_flags & PFR_TFLAG_ACTIVE)) {
			p->pfrkt_nflags = (p->pfrkt_flags | setflag) &
			    ~clrflag;
			if (p->pfrkt_nflags == p->pfrkt_flags)
				goto _skip;
			SLIST_FOREACH(q, &workq, pfrkt_workq)
				if (!pfr_ktable_compare(p, q))
					goto _skip;
			SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
			if ((p->pfrkt_flags & PFR_TFLAG_PERSIST) &&
			    (clrflag & PFR_TFLAG_PERSIST) &&
			    !(p->pfrkt_flags & PFR_TFLAG_REFERENCED))
				xdel++;
			else
				xchange++;
		}
_skip:
	;
	}
	if (!(flags & PFR_FLAG_DUMMY))
		pfr_setflags_ktables(&workq);
	if (nchange != NULL)
		*nchange = xchange;
	if (ndel != NULL)
		*ndel = xdel;
	return (0);
}

int
pfr_ina_begin(struct pfr_table *trs, u_int32_t *ticket, int *ndel, int flags)
{
	struct pfr_ktableworkq	 workq;
	struct pfr_ktable	*p;
	struct pf_ruleset	*rs;
	int			 xdel = 0;

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	rs = pf_find_or_create_ruleset(trs->pfrt_anchor);
	if (rs == NULL)
		return (ENOMEM);
	SLIST_INIT(&workq);
	RB_FOREACH(p, pfr_ktablehead, &V_pfr_ktables) {
		if (!(p->pfrkt_flags & PFR_TFLAG_INACTIVE) ||
		    pfr_skip_table(trs, p, 0))
			continue;
		p->pfrkt_nflags = p->pfrkt_flags & ~PFR_TFLAG_INACTIVE;
		SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
		xdel++;
	}
	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_setflags_ktables(&workq);
		if (ticket != NULL)
			*ticket = ++rs->tticket;
		rs->topen = 1;
	} else
		pf_remove_if_empty_ruleset(rs);
	if (ndel != NULL)
		*ndel = xdel;
	return (0);
}

int
pfr_ina_define(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *nadd, int *naddr, u_int32_t ticket, int flags)
{
	struct pfr_ktableworkq	 tableq;
	struct pfr_kentryworkq	 addrq;
	struct pfr_ktable	*kt, *rt, *shadow, key;
	struct pfr_kentry	*p;
	struct pfr_addr		*ad;
	struct pf_ruleset	*rs;
	int			 i, rv, xadd = 0, xaddr = 0;

	PF_RULES_WASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_ADDRSTOO);
	if (size && !(flags & PFR_FLAG_ADDRSTOO))
		return (EINVAL);
	if (pfr_validate_table(tbl, PFR_TFLAG_USRMASK,
	    flags & PFR_FLAG_USERIOCTL))
		return (EINVAL);
	rs = pf_find_ruleset(tbl->pfrt_anchor);
	if (rs == NULL || !rs->topen || ticket != rs->tticket)
		return (EBUSY);
	tbl->pfrt_flags |= PFR_TFLAG_INACTIVE;
	SLIST_INIT(&tableq);
	kt = RB_FIND(pfr_ktablehead, &V_pfr_ktables, (struct pfr_ktable *)tbl);
	if (kt == NULL) {
		kt = pfr_create_ktable(tbl, 0, 1);
		if (kt == NULL)
			return (ENOMEM);
		SLIST_INSERT_HEAD(&tableq, kt, pfrkt_workq);
		xadd++;
		if (!tbl->pfrt_anchor[0])
			goto _skip;

		/* find or create root table */
		bzero(&key, sizeof(key));
		strlcpy(key.pfrkt_name, tbl->pfrt_name, sizeof(key.pfrkt_name));
		rt = RB_FIND(pfr_ktablehead, &V_pfr_ktables, &key);
		if (rt != NULL) {
			kt->pfrkt_root = rt;
			goto _skip;
		}
		rt = pfr_create_ktable(&key.pfrkt_t, 0, 1);
		if (rt == NULL) {
			pfr_destroy_ktables(&tableq, 0);
			return (ENOMEM);
		}
		SLIST_INSERT_HEAD(&tableq, rt, pfrkt_workq);
		kt->pfrkt_root = rt;
	} else if (!(kt->pfrkt_flags & PFR_TFLAG_INACTIVE))
		xadd++;
_skip:
	shadow = pfr_create_ktable(tbl, 0, 0);
	if (shadow == NULL) {
		pfr_destroy_ktables(&tableq, 0);
		return (ENOMEM);
	}
	SLIST_INIT(&addrq);
	for (i = 0, ad = addr; i < size; i++, ad++) {
		if (pfr_validate_addr(ad))
			senderr(EINVAL);
		if (pfr_lookup_addr(shadow, ad, 1) != NULL)
			continue;
		p = pfr_create_kentry(ad);
		if (p == NULL)
			senderr(ENOMEM);
		if (pfr_route_kentry(shadow, p)) {
			pfr_destroy_kentry(p);
			continue;
		}
		SLIST_INSERT_HEAD(&addrq, p, pfrke_workq);
		xaddr++;
	}
	if (!(flags & PFR_FLAG_DUMMY)) {
		if (kt->pfrkt_shadow != NULL)
			pfr_destroy_ktable(kt->pfrkt_shadow, 1);
		kt->pfrkt_flags |= PFR_TFLAG_INACTIVE;
		pfr_insert_ktables(&tableq);
		shadow->pfrkt_cnt = (flags & PFR_FLAG_ADDRSTOO) ?
		    xaddr : NO_ADDRESSES;
		kt->pfrkt_shadow = shadow;
	} else {
		pfr_clean_node_mask(shadow, &addrq);
		pfr_destroy_ktable(shadow, 0);
		pfr_destroy_ktables(&tableq, 0);
		pfr_destroy_kentries(&addrq);
	}
	if (nadd != NULL)
		*nadd = xadd;
	if (naddr != NULL)
		*naddr = xaddr;
	return (0);
_bad:
	pfr_destroy_ktable(shadow, 0);
	pfr_destroy_ktables(&tableq, 0);
	pfr_destroy_kentries(&addrq);
	return (rv);
}

int
pfr_ina_rollback(struct pfr_table *trs, u_int32_t ticket, int *ndel, int flags)
{
	struct pfr_ktableworkq	 workq;
	struct pfr_ktable	*p;
	struct pf_ruleset	*rs;
	int			 xdel = 0;

	PF_RULES_WASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	rs = pf_find_ruleset(trs->pfrt_anchor);
	if (rs == NULL || !rs->topen || ticket != rs->tticket)
		return (0);
	SLIST_INIT(&workq);
	RB_FOREACH(p, pfr_ktablehead, &V_pfr_ktables) {
		if (!(p->pfrkt_flags & PFR_TFLAG_INACTIVE) ||
		    pfr_skip_table(trs, p, 0))
			continue;
		p->pfrkt_nflags = p->pfrkt_flags & ~PFR_TFLAG_INACTIVE;
		SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
		xdel++;
	}
	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_setflags_ktables(&workq);
		rs->topen = 0;
		pf_remove_if_empty_ruleset(rs);
	}
	if (ndel != NULL)
		*ndel = xdel;
	return (0);
}

int
pfr_ina_commit(struct pfr_table *trs, u_int32_t ticket, int *nadd,
    int *nchange, int flags)
{
	struct pfr_ktable	*p, *q;
	struct pfr_ktableworkq	 workq;
	struct pf_ruleset	*rs;
	int			 xadd = 0, xchange = 0;
	long			 tzero = time_second;

	PF_RULES_WASSERT();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	rs = pf_find_ruleset(trs->pfrt_anchor);
	if (rs == NULL || !rs->topen || ticket != rs->tticket)
		return (EBUSY);

	SLIST_INIT(&workq);
	RB_FOREACH(p, pfr_ktablehead, &V_pfr_ktables) {
		if (!(p->pfrkt_flags & PFR_TFLAG_INACTIVE) ||
		    pfr_skip_table(trs, p, 0))
			continue;
		SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
		if (p->pfrkt_flags & PFR_TFLAG_ACTIVE)
			xchange++;
		else
			xadd++;
	}

	if (!(flags & PFR_FLAG_DUMMY)) {
		for (p = SLIST_FIRST(&workq); p != NULL; p = q) {
			q = SLIST_NEXT(p, pfrkt_workq);
			pfr_commit_ktable(p, tzero);
		}
		rs->topen = 0;
		pf_remove_if_empty_ruleset(rs);
	}
	if (nadd != NULL)
		*nadd = xadd;
	if (nchange != NULL)
		*nchange = xchange;

	return (0);
}

static void
pfr_commit_ktable(struct pfr_ktable *kt, long tzero)
{
	struct pfr_ktable	*shadow = kt->pfrkt_shadow;
	int			 nflags;

	PF_RULES_WASSERT();

	if (shadow->pfrkt_cnt == NO_ADDRESSES) {
		if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
			pfr_clstats_ktable(kt, tzero, 1);
	} else if (kt->pfrkt_flags & PFR_TFLAG_ACTIVE) {
		/* kt might contain addresses */
		struct pfr_kentryworkq	 addrq, addq, changeq, delq, garbageq;
		struct pfr_kentry	*p, *q, *next;
		struct pfr_addr		 ad;

		pfr_enqueue_addrs(shadow, &addrq, NULL, 0);
		pfr_mark_addrs(kt);
		SLIST_INIT(&addq);
		SLIST_INIT(&changeq);
		SLIST_INIT(&delq);
		SLIST_INIT(&garbageq);
		pfr_clean_node_mask(shadow, &addrq);
		for (p = SLIST_FIRST(&addrq); p != NULL; p = next) {
			next = SLIST_NEXT(p, pfrke_workq);	/* XXX */
			pfr_copyout_addr(&ad, p);
			q = pfr_lookup_addr(kt, &ad, 1);
			if (q != NULL) {
				if (q->pfrke_not != p->pfrke_not)
					SLIST_INSERT_HEAD(&changeq, q,
					    pfrke_workq);
				q->pfrke_mark = 1;
				SLIST_INSERT_HEAD(&garbageq, p, pfrke_workq);
			} else {
				p->pfrke_counters.pfrkc_tzero = tzero;
				SLIST_INSERT_HEAD(&addq, p, pfrke_workq);
			}
		}
		pfr_enqueue_addrs(kt, &delq, NULL, ENQUEUE_UNMARKED_ONLY);
		pfr_insert_kentries(kt, &addq, tzero);
		pfr_remove_kentries(kt, &delq);
		pfr_clstats_kentries(&changeq, tzero, INVERT_NEG_FLAG);
		pfr_destroy_kentries(&garbageq);
	} else {
		/* kt cannot contain addresses */
		SWAP(struct radix_node_head *, kt->pfrkt_ip4,
		    shadow->pfrkt_ip4);
		SWAP(struct radix_node_head *, kt->pfrkt_ip6,
		    shadow->pfrkt_ip6);
		SWAP(int, kt->pfrkt_cnt, shadow->pfrkt_cnt);
		pfr_clstats_ktable(kt, tzero, 1);
	}
	nflags = ((shadow->pfrkt_flags & PFR_TFLAG_USRMASK) |
	    (kt->pfrkt_flags & PFR_TFLAG_SETMASK) | PFR_TFLAG_ACTIVE)
		& ~PFR_TFLAG_INACTIVE;
	pfr_destroy_ktable(shadow, 0);
	kt->pfrkt_shadow = NULL;
	pfr_setflags_ktable(kt, nflags);
}

static int
pfr_validate_table(struct pfr_table *tbl, int allowedflags, int no_reserved)
{
	int i;

	if (!tbl->pfrt_name[0])
		return (-1);
	if (no_reserved && !strcmp(tbl->pfrt_anchor, PF_RESERVED_ANCHOR))
		 return (-1);
	if (tbl->pfrt_name[PF_TABLE_NAME_SIZE-1])
		return (-1);
	for (i = strlen(tbl->pfrt_name); i < PF_TABLE_NAME_SIZE; i++)
		if (tbl->pfrt_name[i])
			return (-1);
	if (pfr_fix_anchor(tbl->pfrt_anchor))
		return (-1);
	if (tbl->pfrt_flags & ~allowedflags)
		return (-1);
	return (0);
}

/*
 * Rewrite anchors referenced by tables to remove slashes
 * and check for validity.
 */
static int
pfr_fix_anchor(char *anchor)
{
	size_t siz = MAXPATHLEN;
	int i;

	if (anchor[0] == '/') {
		char *path;
		int off;

		path = anchor;
		off = 1;
		while (*++path == '/')
			off++;
		bcopy(path, anchor, siz - off);
		memset(anchor + siz - off, 0, off);
	}
	if (anchor[siz - 1])
		return (-1);
	for (i = strlen(anchor); i < siz; i++)
		if (anchor[i])
			return (-1);
	return (0);
}

int
pfr_table_count(struct pfr_table *filter, int flags)
{
	struct pf_ruleset *rs;

	PF_RULES_ASSERT();

	if (flags & PFR_FLAG_ALLRSETS)
		return (V_pfr_ktable_cnt);
	if (filter->pfrt_anchor[0]) {
		rs = pf_find_ruleset(filter->pfrt_anchor);
		return ((rs != NULL) ? rs->tables : -1);
	}
	return (pf_main_ruleset.tables);
}

static int
pfr_skip_table(struct pfr_table *filter, struct pfr_ktable *kt, int flags)
{
	if (flags & PFR_FLAG_ALLRSETS)
		return (0);
	if (strcmp(filter->pfrt_anchor, kt->pfrkt_anchor))
		return (1);
	return (0);
}

static void
pfr_insert_ktables(struct pfr_ktableworkq *workq)
{
	struct pfr_ktable	*p;

	SLIST_FOREACH(p, workq, pfrkt_workq)
		pfr_insert_ktable(p);
}

static void
pfr_insert_ktable(struct pfr_ktable *kt)
{

	PF_RULES_WASSERT();

	RB_INSERT(pfr_ktablehead, &V_pfr_ktables, kt);
	V_pfr_ktable_cnt++;
	if (kt->pfrkt_root != NULL)
		if (!kt->pfrkt_root->pfrkt_refcnt[PFR_REFCNT_ANCHOR]++)
			pfr_setflags_ktable(kt->pfrkt_root,
			    kt->pfrkt_root->pfrkt_flags|PFR_TFLAG_REFDANCHOR);
}

static void
pfr_setflags_ktables(struct pfr_ktableworkq *workq)
{
	struct pfr_ktable	*p, *q;

	for (p = SLIST_FIRST(workq); p; p = q) {
		q = SLIST_NEXT(p, pfrkt_workq);
		pfr_setflags_ktable(p, p->pfrkt_nflags);
	}
}

static void
pfr_setflags_ktable(struct pfr_ktable *kt, int newf)
{
	struct pfr_kentryworkq	addrq;

	PF_RULES_WASSERT();

	if (!(newf & PFR_TFLAG_REFERENCED) &&
	    !(newf & PFR_TFLAG_REFDANCHOR) &&
	    !(newf & PFR_TFLAG_PERSIST))
		newf &= ~PFR_TFLAG_ACTIVE;
	if (!(newf & PFR_TFLAG_ACTIVE))
		newf &= ~PFR_TFLAG_USRMASK;
	if (!(newf & PFR_TFLAG_SETMASK)) {
		RB_REMOVE(pfr_ktablehead, &V_pfr_ktables, kt);
		if (kt->pfrkt_root != NULL)
			if (!--kt->pfrkt_root->pfrkt_refcnt[PFR_REFCNT_ANCHOR])
				pfr_setflags_ktable(kt->pfrkt_root,
				    kt->pfrkt_root->pfrkt_flags &
					~PFR_TFLAG_REFDANCHOR);
		pfr_destroy_ktable(kt, 1);
		V_pfr_ktable_cnt--;
		return;
	}
	if (!(newf & PFR_TFLAG_ACTIVE) && kt->pfrkt_cnt) {
		pfr_enqueue_addrs(kt, &addrq, NULL, 0);
		pfr_remove_kentries(kt, &addrq);
	}
	if (!(newf & PFR_TFLAG_INACTIVE) && kt->pfrkt_shadow != NULL) {
		pfr_destroy_ktable(kt->pfrkt_shadow, 1);
		kt->pfrkt_shadow = NULL;
	}
	kt->pfrkt_flags = newf;
}

static void
pfr_clstats_ktables(struct pfr_ktableworkq *workq, long tzero, int recurse)
{
	struct pfr_ktable	*p;

	SLIST_FOREACH(p, workq, pfrkt_workq)
		pfr_clstats_ktable(p, tzero, recurse);
}

static void
pfr_clstats_ktable(struct pfr_ktable *kt, long tzero, int recurse)
{
	struct pfr_kentryworkq	 addrq;
	int			 pfr_dir, pfr_op;

	if (recurse) {
		pfr_enqueue_addrs(kt, &addrq, NULL, 0);
		pfr_clstats_kentries(&addrq, tzero, 0);
	}
	for (pfr_dir = 0; pfr_dir < PFR_DIR_MAX; pfr_dir ++) {
		for (pfr_op = 0; pfr_op < PFR_OP_TABLE_MAX; pfr_op ++) {
			counter_u64_zero(kt->pfrkt_packets[pfr_dir][pfr_op]);
			counter_u64_zero(kt->pfrkt_bytes[pfr_dir][pfr_op]);
		}
	}
	counter_u64_zero(kt->pfrkt_match);
	counter_u64_zero(kt->pfrkt_nomatch);
	kt->pfrkt_tzero = tzero;
}

static struct pfr_ktable *
pfr_create_ktable(struct pfr_table *tbl, long tzero, int attachruleset)
{
	struct pfr_ktable	*kt;
	struct pf_ruleset	*rs;
	int			 pfr_dir, pfr_op;

	PF_RULES_WASSERT();

	kt = malloc(sizeof(*kt), M_PFTABLE, M_NOWAIT|M_ZERO);
	if (kt == NULL)
		return (NULL);
	kt->pfrkt_t = *tbl;

	if (attachruleset) {
		rs = pf_find_or_create_ruleset(tbl->pfrt_anchor);
		if (!rs) {
			pfr_destroy_ktable(kt, 0);
			return (NULL);
		}
		kt->pfrkt_rs = rs;
		rs->tables++;
	}

	for (pfr_dir = 0; pfr_dir < PFR_DIR_MAX; pfr_dir ++) {
		for (pfr_op = 0; pfr_op < PFR_OP_TABLE_MAX; pfr_op ++) {
			kt->pfrkt_packets[pfr_dir][pfr_op] =
			    counter_u64_alloc(M_NOWAIT);
			if (! kt->pfrkt_packets[pfr_dir][pfr_op]) {
				pfr_destroy_ktable(kt, 0);
				return (NULL);
			}
			kt->pfrkt_bytes[pfr_dir][pfr_op] =
			    counter_u64_alloc(M_NOWAIT);
			if (! kt->pfrkt_bytes[pfr_dir][pfr_op]) {
				pfr_destroy_ktable(kt, 0);
				return (NULL);
			}
		}
	}
	kt->pfrkt_match = counter_u64_alloc(M_NOWAIT);
	if (! kt->pfrkt_match) {
		pfr_destroy_ktable(kt, 0);
		return (NULL);
	}

	kt->pfrkt_nomatch = counter_u64_alloc(M_NOWAIT);
	if (! kt->pfrkt_nomatch) {
		pfr_destroy_ktable(kt, 0);
		return (NULL);
	}

	if (!rn_inithead((void **)&kt->pfrkt_ip4,
	    offsetof(struct sockaddr_in, sin_addr) * 8) ||
	    !rn_inithead((void **)&kt->pfrkt_ip6,
	    offsetof(struct sockaddr_in6, sin6_addr) * 8)) {
		pfr_destroy_ktable(kt, 0);
		return (NULL);
	}
	kt->pfrkt_tzero = tzero;

	return (kt);
}

static void
pfr_destroy_ktables(struct pfr_ktableworkq *workq, int flushaddr)
{
	struct pfr_ktable	*p, *q;

	for (p = SLIST_FIRST(workq); p; p = q) {
		q = SLIST_NEXT(p, pfrkt_workq);
		pfr_destroy_ktable(p, flushaddr);
	}
}

static void
pfr_destroy_ktable(struct pfr_ktable *kt, int flushaddr)
{
	struct pfr_kentryworkq	 addrq;
	int			 pfr_dir, pfr_op;

	if (flushaddr) {
		pfr_enqueue_addrs(kt, &addrq, NULL, 0);
		pfr_clean_node_mask(kt, &addrq);
		pfr_destroy_kentries(&addrq);
	}
	if (kt->pfrkt_ip4 != NULL)
		rn_detachhead((void **)&kt->pfrkt_ip4);
	if (kt->pfrkt_ip6 != NULL)
		rn_detachhead((void **)&kt->pfrkt_ip6);
	if (kt->pfrkt_shadow != NULL)
		pfr_destroy_ktable(kt->pfrkt_shadow, flushaddr);
	if (kt->pfrkt_rs != NULL) {
		kt->pfrkt_rs->tables--;
		pf_remove_if_empty_ruleset(kt->pfrkt_rs);
	}
	for (pfr_dir = 0; pfr_dir < PFR_DIR_MAX; pfr_dir ++) {
		for (pfr_op = 0; pfr_op < PFR_OP_TABLE_MAX; pfr_op ++) {
			counter_u64_free(kt->pfrkt_packets[pfr_dir][pfr_op]);
			counter_u64_free(kt->pfrkt_bytes[pfr_dir][pfr_op]);
		}
	}
	counter_u64_free(kt->pfrkt_match);
	counter_u64_free(kt->pfrkt_nomatch);

	free(kt, M_PFTABLE);
}

static int
pfr_ktable_compare(struct pfr_ktable *p, struct pfr_ktable *q)
{
	int d;

	if ((d = strncmp(p->pfrkt_name, q->pfrkt_name, PF_TABLE_NAME_SIZE)))
		return (d);
	return (strcmp(p->pfrkt_anchor, q->pfrkt_anchor));
}

static struct pfr_ktable *
pfr_lookup_table(struct pfr_table *tbl)
{
	/* struct pfr_ktable start like a struct pfr_table */
	return (RB_FIND(pfr_ktablehead, &V_pfr_ktables,
	    (struct pfr_ktable *)tbl));
}

int
pfr_match_addr(struct pfr_ktable *kt, struct pf_addr *a, sa_family_t af)
{
	struct pfr_kentry	*ke = NULL;
	int			 match;

	PF_RULES_RASSERT();

	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE) && kt->pfrkt_root != NULL)
		kt = kt->pfrkt_root;
	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (0);

	switch (af) {
#ifdef INET
	case AF_INET:
	    {
		struct sockaddr_in sin;

		bzero(&sin, sizeof(sin));
		sin.sin_len = sizeof(sin);
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = a->addr32[0];
		ke = (struct pfr_kentry *)rn_match(&sin, &kt->pfrkt_ip4->rh);
		if (ke && KENTRY_RNF_ROOT(ke))
			ke = NULL;
		break;
	    }
#endif /* INET */
#ifdef INET6
	case AF_INET6:
	    {
		struct sockaddr_in6 sin6;

		bzero(&sin6, sizeof(sin6));
		sin6.sin6_len = sizeof(sin6);
		sin6.sin6_family = AF_INET6;
		bcopy(a, &sin6.sin6_addr, sizeof(sin6.sin6_addr));
		ke = (struct pfr_kentry *)rn_match(&sin6, &kt->pfrkt_ip6->rh);
		if (ke && KENTRY_RNF_ROOT(ke))
			ke = NULL;
		break;
	    }
#endif /* INET6 */
	}
	match = (ke && !ke->pfrke_not);
	if (match)
		counter_u64_add(kt->pfrkt_match, 1);
	else
		counter_u64_add(kt->pfrkt_nomatch, 1);
	return (match);
}

void
pfr_update_stats(struct pfr_ktable *kt, struct pf_addr *a, sa_family_t af,
    u_int64_t len, int dir_out, int op_pass, int notrule)
{
	struct pfr_kentry	*ke = NULL;

	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE) && kt->pfrkt_root != NULL)
		kt = kt->pfrkt_root;
	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return;

	switch (af) {
#ifdef INET
	case AF_INET:
	    {
		struct sockaddr_in sin;

		bzero(&sin, sizeof(sin));
		sin.sin_len = sizeof(sin);
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = a->addr32[0];
		ke = (struct pfr_kentry *)rn_match(&sin, &kt->pfrkt_ip4->rh);
		if (ke && KENTRY_RNF_ROOT(ke))
			ke = NULL;
		break;
	    }
#endif /* INET */
#ifdef INET6
	case AF_INET6:
	    {
		struct sockaddr_in6 sin6;

		bzero(&sin6, sizeof(sin6));
		sin6.sin6_len = sizeof(sin6);
		sin6.sin6_family = AF_INET6;
		bcopy(a, &sin6.sin6_addr, sizeof(sin6.sin6_addr));
		ke = (struct pfr_kentry *)rn_match(&sin6, &kt->pfrkt_ip6->rh);
		if (ke && KENTRY_RNF_ROOT(ke))
			ke = NULL;
		break;
	    }
#endif /* INET6 */
	default:
		panic("%s: unknown address family %u", __func__, af);
	}
	if ((ke == NULL || ke->pfrke_not) != notrule) {
		if (op_pass != PFR_OP_PASS)
			DPFPRINTF(PF_DEBUG_URGENT,
			    ("pfr_update_stats: assertion failed.\n"));
		op_pass = PFR_OP_XPASS;
	}
	counter_u64_add(kt->pfrkt_packets[dir_out][op_pass], 1);
	counter_u64_add(kt->pfrkt_bytes[dir_out][op_pass], len);
	if (ke != NULL && op_pass != PFR_OP_XPASS &&
	    (kt->pfrkt_flags & PFR_TFLAG_COUNTERS)) {
		counter_u64_add(ke->pfrke_counters.
		    pfrkc_packets[dir_out][op_pass], 1);
		counter_u64_add(ke->pfrke_counters.
		    pfrkc_bytes[dir_out][op_pass], len);
	}
}

struct pfr_ktable *
pfr_attach_table(struct pf_ruleset *rs, char *name)
{
	struct pfr_ktable	*kt, *rt;
	struct pfr_table	 tbl;
	struct pf_anchor	*ac = rs->anchor;

	PF_RULES_WASSERT();

	bzero(&tbl, sizeof(tbl));
	strlcpy(tbl.pfrt_name, name, sizeof(tbl.pfrt_name));
	if (ac != NULL)
		strlcpy(tbl.pfrt_anchor, ac->path, sizeof(tbl.pfrt_anchor));
	kt = pfr_lookup_table(&tbl);
	if (kt == NULL) {
		kt = pfr_create_ktable(&tbl, time_second, 1);
		if (kt == NULL)
			return (NULL);
		if (ac != NULL) {
			bzero(tbl.pfrt_anchor, sizeof(tbl.pfrt_anchor));
			rt = pfr_lookup_table(&tbl);
			if (rt == NULL) {
				rt = pfr_create_ktable(&tbl, 0, 1);
				if (rt == NULL) {
					pfr_destroy_ktable(kt, 0);
					return (NULL);
				}
				pfr_insert_ktable(rt);
			}
			kt->pfrkt_root = rt;
		}
		pfr_insert_ktable(kt);
	}
	if (!kt->pfrkt_refcnt[PFR_REFCNT_RULE]++)
		pfr_setflags_ktable(kt, kt->pfrkt_flags|PFR_TFLAG_REFERENCED);
	return (kt);
}

void
pfr_detach_table(struct pfr_ktable *kt)
{

	PF_RULES_WASSERT();
	KASSERT(kt->pfrkt_refcnt[PFR_REFCNT_RULE] > 0, ("%s: refcount %d\n",
	    __func__, kt->pfrkt_refcnt[PFR_REFCNT_RULE]));

	if (!--kt->pfrkt_refcnt[PFR_REFCNT_RULE])
		pfr_setflags_ktable(kt, kt->pfrkt_flags&~PFR_TFLAG_REFERENCED);
}

int
pfr_pool_get(struct pfr_ktable *kt, int *pidx, struct pf_addr *counter,
    sa_family_t af)
{
	struct pf_addr		 *addr, *cur, *mask;
	union sockaddr_union	 uaddr, umask;
	struct pfr_kentry	*ke, *ke2 = NULL;
	int			 idx = -1, use_counter = 0;

	switch (af) {
	case AF_INET:
		uaddr.sin.sin_len = sizeof(struct sockaddr_in);
		uaddr.sin.sin_family = AF_INET;
		break;
	case AF_INET6:
		uaddr.sin6.sin6_len = sizeof(struct sockaddr_in6);
		uaddr.sin6.sin6_family = AF_INET6;
		break;
	}
	addr = SUNION2PF(&uaddr, af);

	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE) && kt->pfrkt_root != NULL)
		kt = kt->pfrkt_root;
	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (-1);

	if (pidx != NULL)
		idx = *pidx;
	if (counter != NULL && idx >= 0)
		use_counter = 1;
	if (idx < 0)
		idx = 0;

_next_block:
	ke = pfr_kentry_byidx(kt, idx, af);
	if (ke == NULL) {
		counter_u64_add(kt->pfrkt_nomatch, 1);
		return (1);
	}
	pfr_prepare_network(&umask, af, ke->pfrke_net);
	cur = SUNION2PF(&ke->pfrke_sa, af);
	mask = SUNION2PF(&umask, af);

	if (use_counter) {
		/* is supplied address within block? */
		if (!PF_MATCHA(0, cur, mask, counter, af)) {
			/* no, go to next block in table */
			idx++;
			use_counter = 0;
			goto _next_block;
		}
		PF_ACPY(addr, counter, af);
	} else {
		/* use first address of block */
		PF_ACPY(addr, cur, af);
	}

	if (!KENTRY_NETWORK(ke)) {
		/* this is a single IP address - no possible nested block */
		PF_ACPY(counter, addr, af);
		*pidx = idx;
		counter_u64_add(kt->pfrkt_match, 1);
		return (0);
	}
	for (;;) {
		/* we don't want to use a nested block */
		switch (af) {
		case AF_INET:
			ke2 = (struct pfr_kentry *)rn_match(&uaddr,
			    &kt->pfrkt_ip4->rh);
			break;
		case AF_INET6:
			ke2 = (struct pfr_kentry *)rn_match(&uaddr,
			    &kt->pfrkt_ip6->rh);
			break;
		}
		/* no need to check KENTRY_RNF_ROOT() here */
		if (ke2 == ke) {
			/* lookup return the same block - perfect */
			PF_ACPY(counter, addr, af);
			*pidx = idx;
			counter_u64_add(kt->pfrkt_match, 1);
			return (0);
		}

		/* we need to increase the counter past the nested block */
		pfr_prepare_network(&umask, AF_INET, ke2->pfrke_net);
		PF_POOLMASK(addr, addr, SUNION2PF(&umask, af), &pfr_ffaddr, af);
		PF_AINC(addr, af);
		if (!PF_MATCHA(0, cur, mask, addr, af)) {
			/* ok, we reached the end of our main block */
			/* go to next block in table */
			idx++;
			use_counter = 0;
			goto _next_block;
		}
	}
}

static struct pfr_kentry *
pfr_kentry_byidx(struct pfr_ktable *kt, int idx, int af)
{
	struct pfr_walktree	w;

	bzero(&w, sizeof(w));
	w.pfrw_op = PFRW_POOL_GET;
	w.pfrw_cnt = idx;

	switch (af) {
#ifdef INET
	case AF_INET:
		kt->pfrkt_ip4->rnh_walktree(&kt->pfrkt_ip4->rh, pfr_walktree, &w);
		return (w.pfrw_kentry);
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		kt->pfrkt_ip6->rnh_walktree(&kt->pfrkt_ip6->rh, pfr_walktree, &w);
		return (w.pfrw_kentry);
#endif /* INET6 */
	default:
		return (NULL);
	}
}

void
pfr_dynaddr_update(struct pfr_ktable *kt, struct pfi_dynaddr *dyn)
{
	struct pfr_walktree	w;

	bzero(&w, sizeof(w));
	w.pfrw_op = PFRW_DYNADDR_UPDATE;
	w.pfrw_dyn = dyn;

	dyn->pfid_acnt4 = 0;
	dyn->pfid_acnt6 = 0;
	if (!dyn->pfid_af || dyn->pfid_af == AF_INET)
		kt->pfrkt_ip4->rnh_walktree(&kt->pfrkt_ip4->rh, pfr_walktree, &w);
	if (!dyn->pfid_af || dyn->pfid_af == AF_INET6)
		kt->pfrkt_ip6->rnh_walktree(&kt->pfrkt_ip6->rh, pfr_walktree, &w);
}
