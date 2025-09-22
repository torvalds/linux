/*	$OpenBSD: pf_table.c,v 1.146 2025/07/07 02:28:50 jsg Exp $	*/

/*
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/mbuf.h>
#include <sys/pool.h>
#include <sys/syslog.h>
#include <sys/proc.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_ipsp.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#ifdef INET6
#include <netinet/icmp6.h>
#endif /* INET6 */

#include <net/pfvar.h>
#include <net/pfvar_priv.h>

#define ACCEPT_FLAGS(flags, oklist)		\
	do {					\
		if ((flags & ~(oklist)) &	\
		    PFR_FLAG_ALLMASK)		\
			return (EINVAL);	\
	} while (0)

#define COPYIN(from, to, size, flags)		\
	((flags & PFR_FLAG_USERIOCTL) ?		\
	copyin((from), (to), (size)) :		\
	(bcopy((from), (to), (size)), 0))

#define COPYOUT(from, to, size, flags)		\
	((flags & PFR_FLAG_USERIOCTL) ?		\
	copyout((from), (to), (size)) :		\
	(bcopy((from), (to), (size)), 0))

#define YIELD(ok)				\
	do {					\
		if (ok)				\
			sched_pause(preempt);	\
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

#define SWAP(type, a1, a2)			\
	do {					\
		type tmp = a1;			\
		a1 = a2;			\
		a2 = tmp;			\
	} while (0)

#define SUNION2PF(su, af) (((af)==AF_INET) ?	\
    (struct pf_addr *)&(su)->sin.sin_addr :	\
    (struct pf_addr *)&(su)->sin6.sin6_addr)

#define	AF_BITS(af)		(((af)==AF_INET)?32:128)
#define	ADDR_NETWORK(ad)	((ad)->pfra_net < AF_BITS((ad)->pfra_af))
#define	KENTRY_NETWORK(ke)	((ke)->pfrke_net < AF_BITS((ke)->pfrke_af))

#define NO_ADDRESSES		(-1)
#define ENQUEUE_UNMARKED_ONLY	(1)
#define INVERT_NEG_FLAG		(1)

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
#define pfrw_addr	pfrw_1.pfrw1_addr
#define pfrw_astats	pfrw_1.pfrw1_astats
#define pfrw_workq	pfrw_1.pfrw1_workq
#define pfrw_kentry	pfrw_1.pfrw1_kentry
#define pfrw_dyn	pfrw_1.pfrw1_dyn
#define pfrw_cnt	pfrw_free

#define senderr(e)	do { rv = (e); goto _bad; } while (0)

struct pool		 pfr_ktable_pl;
struct pool		 pfr_kentry_pl[PFRKE_MAX];
struct pool		 pfr_kcounters_pl;
union sockaddr_union	 pfr_mask;
struct pf_addr		 pfr_ffaddr;

int			 pfr_gcd(int, int);
void			 pfr_copyout_addr(struct pfr_addr *,
			    struct pfr_kentry *ke);
int			 pfr_validate_addr(struct pfr_addr *);
void			 pfr_enqueue_addrs(struct pfr_ktable *,
			    struct pfr_kentryworkq *, int *, int);
void			 pfr_mark_addrs(struct pfr_ktable *);
struct pfr_kentry	*pfr_lookup_addr(struct pfr_ktable *,
			    struct pfr_addr *, int);
struct pfr_kentry	*pfr_lookup_kentry(struct pfr_ktable *,
			    struct pfr_kentry *, int);
struct pfr_kentry	*pfr_create_kentry(struct pfr_addr *);
struct pfr_kentry 	*pfr_create_kentry_unlocked(struct pfr_addr *, int);
void			 pfr_kentry_kif_ref(struct pfr_kentry *);
void			 pfr_destroy_kentries(struct pfr_kentryworkq *);
void			 pfr_destroy_ioq(struct pfr_kentryworkq *, int);
void			 pfr_destroy_kentry(struct pfr_kentry *);
void			 pfr_insert_kentries(struct pfr_ktable *,
			    struct pfr_kentryworkq *, time_t);
void			 pfr_remove_kentries(struct pfr_ktable *,
			    struct pfr_kentryworkq *);
void			 pfr_clstats_kentries(struct pfr_kentryworkq *, time_t,
			    int);
void			 pfr_reset_feedback(struct pfr_addr *, int, int);
void			 pfr_prepare_network(union sockaddr_union *, int, int);
int			 pfr_route_kentry(struct pfr_ktable *,
			    struct pfr_kentry *);
int			 pfr_unroute_kentry(struct pfr_ktable *,
			    struct pfr_kentry *);
int			 pfr_walktree(struct radix_node *, void *, u_int);
int			 pfr_validate_table(struct pfr_table *, int, int);
int			 pfr_fix_anchor(char *);
void			 pfr_commit_ktable(struct pfr_ktable *, time_t);
void			 pfr_insert_ktables(struct pfr_ktableworkq *);
void			 pfr_insert_ktable(struct pfr_ktable *);
void			 pfr_setflags_ktables(struct pfr_ktableworkq *);
void			 pfr_setflags_ktable(struct pfr_ktable *, int);
void			 pfr_clstats_ktables(struct pfr_ktableworkq *, time_t,
			    int);
void			 pfr_clstats_ktable(struct pfr_ktable *, time_t, int);
struct pfr_ktable	*pfr_create_ktable(struct pfr_table *, time_t, int,
			    int);
void			 pfr_destroy_ktables(struct pfr_ktableworkq *, int);
void			 pfr_destroy_ktables_aux(struct pfr_ktableworkq *);
void			 pfr_destroy_ktable(struct pfr_ktable *, int);
int			 pfr_ktable_compare(struct pfr_ktable *,
			    struct pfr_ktable *);
void			 pfr_ktable_winfo_update(struct pfr_ktable *,
			    struct pfr_kentry *);
struct pfr_ktable	*pfr_lookup_table(struct pfr_table *);
void			 pfr_clean_node_mask(struct pfr_ktable *,
			    struct pfr_kentryworkq *);
int			 pfr_table_count(struct pfr_table *, int);
int			 pfr_skip_table(struct pfr_table *,
			    struct pfr_ktable *, int);
struct pfr_kentry	*pfr_kentry_byidx(struct pfr_ktable *, int, int);
int			 pfr_islinklocal(sa_family_t, struct pf_addr *);

RB_PROTOTYPE(pfr_ktablehead, pfr_ktable, pfrkt_tree, pfr_ktable_compare);
RB_GENERATE(pfr_ktablehead, pfr_ktable, pfrkt_tree, pfr_ktable_compare);

struct pfr_ktablehead	 pfr_ktables;
struct pfr_table	 pfr_nulltable;
int			 pfr_ktable_cnt;

int
pfr_gcd(int m, int n)
{
       int t;

       while (m > 0) {
	       t = n % m;
	       n = m;
	       m = t;
       }
       return (n);
}

void
pfr_initialize(void)
{
	rn_init(sizeof(struct sockaddr_in6));

	pool_init(&pfr_ktable_pl, sizeof(struct pfr_ktable),
	    0, IPL_SOFTNET, 0, "pfrktable", NULL);
	pool_init(&pfr_kentry_pl[PFRKE_PLAIN], sizeof(struct pfr_kentry),
	    0, IPL_SOFTNET, 0, "pfrke_plain", NULL);
	pool_init(&pfr_kentry_pl[PFRKE_ROUTE], sizeof(struct pfr_kentry_route),
	    0, IPL_SOFTNET, 0, "pfrke_route", NULL);
	pool_init(&pfr_kentry_pl[PFRKE_COST], sizeof(struct pfr_kentry_cost),
	    0, IPL_SOFTNET, 0, "pfrke_cost", NULL);
	pool_init(&pfr_kcounters_pl, sizeof(struct pfr_kcounters),
	    0, IPL_SOFTNET, 0, "pfrkcounters", NULL);

	memset(&pfr_ffaddr, 0xff, sizeof(pfr_ffaddr));
}

int
pfr_clr_addrs(struct pfr_table *tbl, int *ndel, int flags)
{
	struct pfr_ktable	*kt;
	struct pfr_kentryworkq	 workq;

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
		if (kt->pfrkt_cnt) {
			DPFPRINTF(LOG_NOTICE,
			    "pfr_clr_addrs: corruption detected (%d).",
			    kt->pfrkt_cnt);
			kt->pfrkt_cnt = 0;
		}
	}
	return (0);
}

void
pfr_fill_feedback(struct pfr_kentry_all *ke, struct pfr_addr *ad)
{
	ad->pfra_type = ke->pfrke_type;

	switch (ke->pfrke_type) {
	case PFRKE_PLAIN:
		break;
	case PFRKE_COST:
		((struct pfr_kentry_cost *)ke)->weight = ad->pfra_weight;
		/* FALLTHROUGH */
	case PFRKE_ROUTE:
		if (ke->pfrke_rifname[0])
			strlcpy(ad->pfra_ifname, ke->pfrke_rifname, IFNAMSIZ);
		break;
	}

	switch (ke->pfrke_af) {
	case AF_INET:
		ad->pfra_ip4addr = ke->pfrke_sa.sin.sin_addr;
		break;
#ifdef	INET6
	case AF_INET6:
		ad->pfra_ip6addr = ke->pfrke_sa.sin6.sin6_addr;
		break;
#endif	/* INET6 */
	default:
		unhandled_af(ke->pfrke_af);
	}
	ad->pfra_weight = ((struct pfr_kentry_cost *)ke)->weight;
	ad->pfra_af = ke->pfrke_af;
	ad->pfra_net = ke->pfrke_net;
	if (ke->pfrke_flags & PFRKE_FLAG_NOT)
		ad->pfra_not = 1;
	ad->pfra_fback = ke->pfrke_fb;
}

int
pfr_add_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
    int *nadd, int flags)
{
	struct pfr_ktable	*kt, *tmpkt;
	struct pfr_kentryworkq	 workq, ioq;
	struct pfr_kentry	*p, *q, *ke;
	struct pfr_addr		 ad;
	int			 i, rv, xadd = 0;
	time_t			 tzero = gettime();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_FEEDBACK);
	if (pfr_validate_table(tbl, 0, flags & PFR_FLAG_USERIOCTL))
		return (EINVAL);
	tmpkt = pfr_create_ktable(&pfr_nulltable, 0, 0,
	    (flags & PFR_FLAG_USERIOCTL? PR_WAITOK : PR_NOWAIT));
	if (tmpkt == NULL)
		return (ENOMEM);
	SLIST_INIT(&workq);
	SLIST_INIT(&ioq);
	for (i = 0; i < size; i++) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(addr+i, &ad, sizeof(ad), flags))
			senderr(EFAULT);
		if (pfr_validate_addr(&ad))
			senderr(EINVAL);

		ke = pfr_create_kentry_unlocked(&ad, flags);
		if (ke == NULL)
			senderr(ENOMEM);
		ke->pfrke_fb = PFR_FB_NONE;
		SLIST_INSERT_HEAD(&ioq, ke, pfrke_ioq);
	}

	NET_LOCK();
	PF_LOCK();
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE)) {
		PF_UNLOCK();
		NET_UNLOCK();
		senderr(ESRCH);
	}
	if (kt->pfrkt_flags & PFR_TFLAG_CONST) {
		PF_UNLOCK();
		NET_UNLOCK();
		senderr(EPERM);
	}
	SLIST_FOREACH(ke, &ioq, pfrke_ioq) {
		pfr_kentry_kif_ref(ke);
		p = pfr_lookup_kentry(kt, ke, 1);
		q = pfr_lookup_kentry(tmpkt, ke, 1);
		if (flags & PFR_FLAG_FEEDBACK) {
			if (q != NULL)
				ke->pfrke_fb = PFR_FB_DUPLICATE;
			else if (p == NULL)
				ke->pfrke_fb = PFR_FB_ADDED;
			else if ((p->pfrke_flags & PFRKE_FLAG_NOT) !=
			    (ke->pfrke_flags & PFRKE_FLAG_NOT))
				ke->pfrke_fb = PFR_FB_CONFLICT;
			else
				ke->pfrke_fb = PFR_FB_NONE;
		}
		if (p == NULL && q == NULL) {
			if (pfr_route_kentry(tmpkt, ke)) {
				/* defer destroy after feedback is processed */
				ke->pfrke_fb = PFR_FB_NONE;
			} else {
				/*
				 * mark entry as added to table, so we won't
				 * kill it with rest of the ioq
				 */
				ke->pfrke_fb = PFR_FB_ADDED;
				SLIST_INSERT_HEAD(&workq, ke, pfrke_workq);
				xadd++;
			}
		}
	}
	/* remove entries, which we will insert from tmpkt */
	pfr_clean_node_mask(tmpkt, &workq);
	if (!(flags & PFR_FLAG_DUMMY))
		pfr_insert_kentries(kt, &workq, tzero);

	PF_UNLOCK();
	NET_UNLOCK();

	if (flags & PFR_FLAG_FEEDBACK) {
		i = 0;
		while ((ke = SLIST_FIRST(&ioq)) != NULL) {
			YIELD(flags & PFR_FLAG_USERIOCTL);
			pfr_fill_feedback((struct pfr_kentry_all *)ke, &ad);
			if (COPYOUT(&ad, addr+i, sizeof(ad), flags))
				senderr(EFAULT);
			i++;
			SLIST_REMOVE_HEAD(&ioq, pfrke_ioq);
			switch (ke->pfrke_fb) {
			case PFR_FB_CONFLICT:
			case PFR_FB_DUPLICATE:
			case PFR_FB_NONE:
				pfr_destroy_kentry(ke);
				break;
			case PFR_FB_ADDED:
				if (flags & PFR_FLAG_DUMMY)
					pfr_destroy_kentry(ke);
			}
		}
	} else
		pfr_destroy_ioq(&ioq, flags);

	if (nadd != NULL)
		*nadd = xadd;

	pfr_destroy_ktable(tmpkt, 0);
	return (0);
_bad:
	pfr_destroy_ioq(&ioq, flags);
	if (flags & PFR_FLAG_FEEDBACK)
		pfr_reset_feedback(addr, size, flags);
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
	struct pfr_addr		 ad;
	int			 i, rv, xdel = 0, log = 1;

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
		for (i = 0; i < size; i++) {
			YIELD(flags & PFR_FLAG_USERIOCTL);
			if (COPYIN(addr+i, &ad, sizeof(ad), flags))
				return (EFAULT);
			if (pfr_validate_addr(&ad))
				return (EINVAL);
			p = pfr_lookup_addr(kt, &ad, 1);
			if (p != NULL)
				p->pfrke_flags &= ~PFRKE_FLAG_MARK;
		}
	}
	SLIST_INIT(&workq);
	for (i = 0; i < size; i++) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(addr+i, &ad, sizeof(ad), flags))
			senderr(EFAULT);
		if (pfr_validate_addr(&ad))
			senderr(EINVAL);
		p = pfr_lookup_addr(kt, &ad, 1);
		if (flags & PFR_FLAG_FEEDBACK) {
			if (p == NULL)
				ad.pfra_fback = PFR_FB_NONE;
			else if ((p->pfrke_flags & PFRKE_FLAG_NOT) !=
			    ad.pfra_not)
				ad.pfra_fback = PFR_FB_CONFLICT;
			else if (p->pfrke_flags & PFRKE_FLAG_MARK)
				ad.pfra_fback = PFR_FB_DUPLICATE;
			else
				ad.pfra_fback = PFR_FB_DELETED;
		}
		if (p != NULL &&
		    (p->pfrke_flags & PFRKE_FLAG_NOT) == ad.pfra_not &&
		    !(p->pfrke_flags & PFRKE_FLAG_MARK)) {
			p->pfrke_flags |= PFRKE_FLAG_MARK;
			SLIST_INSERT_HEAD(&workq, p, pfrke_workq);
			xdel++;
		}
		if (flags & PFR_FLAG_FEEDBACK)
			if (COPYOUT(&ad, addr+i, sizeof(ad), flags))
				senderr(EFAULT);
	}
	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_remove_kentries(kt, &workq);
	}
	if (ndel != NULL)
		*ndel = xdel;
	return (0);
_bad:
	if (flags & PFR_FLAG_FEEDBACK)
		pfr_reset_feedback(addr, size, flags);
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
	time_t			 tzero = gettime();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_FEEDBACK);
	if (pfr_validate_table(tbl, ignore_pfrt_flags, flags &
	    PFR_FLAG_USERIOCTL))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);
	if (kt->pfrkt_flags & PFR_TFLAG_CONST)
		return (EPERM);
	tmpkt = pfr_create_ktable(&pfr_nulltable, 0, 0,
	    (flags & PFR_FLAG_USERIOCTL? PR_WAITOK : PR_NOWAIT));
	if (tmpkt == NULL)
		return (ENOMEM);
	pfr_mark_addrs(kt);
	SLIST_INIT(&addq);
	SLIST_INIT(&delq);
	SLIST_INIT(&changeq);
	for (i = 0; i < size; i++) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(addr+i, &ad, sizeof(ad), flags))
			senderr(EFAULT);
		if (pfr_validate_addr(&ad))
			senderr(EINVAL);
		ad.pfra_fback = PFR_FB_NONE;
		p = pfr_lookup_addr(kt, &ad, 1);
		if (p != NULL) {
			if (p->pfrke_flags & PFRKE_FLAG_MARK) {
				ad.pfra_fback = PFR_FB_DUPLICATE;
				goto _skip;
			}
			p->pfrke_flags |= PFRKE_FLAG_MARK;
			if ((p->pfrke_flags & PFRKE_FLAG_NOT) != ad.pfra_not) {
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
				goto _skip;
			}
			SLIST_INSERT_HEAD(&addq, p, pfrke_workq);
			ad.pfra_fback = PFR_FB_ADDED;
			xadd++;
			if (p->pfrke_type == PFRKE_COST)
				kt->pfrkt_refcntcost++;
			pfr_ktable_winfo_update(kt, p);
		}
_skip:
		if (flags & PFR_FLAG_FEEDBACK)
			if (COPYOUT(&ad, addr+i, sizeof(ad), flags))
				senderr(EFAULT);
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
			if (COPYOUT(&ad, addr+size+i, sizeof(ad), flags))
				senderr(EFAULT);
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
		pfr_reset_feedback(addr, size, flags);
	pfr_destroy_ktable(tmpkt, 0);
	return (rv);
}

int
pfr_tst_addrs(struct pfr_table *tbl, struct pfr_addr *addr, int size,
	int *nmatch, int flags)
{
	struct pfr_ktable	*kt;
	struct pfr_kentry	*p;
	struct pfr_addr		 ad;
	int			 i, xmatch = 0;

	ACCEPT_FLAGS(flags, PFR_FLAG_REPLACE);
	if (pfr_validate_table(tbl, 0, 0))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);

	for (i = 0; i < size; i++) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(addr+i, &ad, sizeof(ad), flags))
			return (EFAULT);
		if (pfr_validate_addr(&ad))
			return (EINVAL);
		if (ADDR_NETWORK(&ad))
			return (EINVAL);
		p = pfr_lookup_addr(kt, &ad, 0);
		if (flags & PFR_FLAG_REPLACE)
			pfr_copyout_addr(&ad, p);
		ad.pfra_fback = (p == NULL) ? PFR_FB_NONE :
		    ((p->pfrke_flags & PFRKE_FLAG_NOT) ?
		    PFR_FB_NOTMATCH : PFR_FB_MATCH);
		if (p != NULL && !(p->pfrke_flags & PFRKE_FLAG_NOT))
			xmatch++;
		if (COPYOUT(&ad, addr+i, sizeof(ad), flags))
			return (EFAULT);
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
	w.pfrw_flags = flags;
	rv = rn_walktree(kt->pfrkt_ip4, pfr_walktree, &w);
	if (!rv)
		rv = rn_walktree(kt->pfrkt_ip6, pfr_walktree, &w);
	if (rv)
		return (rv);

	if (w.pfrw_free) {
		DPFPRINTF(LOG_ERR,
		    "pfr_get_addrs: corruption detected (%d)", w.pfrw_free);
		return (ENOTTY);
	}
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
	time_t			 tzero = gettime();

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
	w.pfrw_flags = flags;
	rv = rn_walktree(kt->pfrkt_ip4, pfr_walktree, &w);
	if (!rv)
		rv = rn_walktree(kt->pfrkt_ip6, pfr_walktree, &w);
	if (!rv && (flags & PFR_FLAG_CLSTATS)) {
		pfr_enqueue_addrs(kt, &workq, NULL, 0);
		pfr_clstats_kentries(&workq, tzero, 0);
	}
	if (rv)
		return (rv);

	if (w.pfrw_free) {
		DPFPRINTF(LOG_ERR,
		    "pfr_get_astats: corruption detected (%d)", w.pfrw_free);
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
	struct pfr_addr		 ad;
	int			 i, rv, xzero = 0;

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_FEEDBACK);
	if (pfr_validate_table(tbl, 0, 0))
		return (EINVAL);
	kt = pfr_lookup_table(tbl);
	if (kt == NULL || !(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (ESRCH);
	SLIST_INIT(&workq);
	for (i = 0; i < size; i++) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(addr+i, &ad, sizeof(ad), flags))
			senderr(EFAULT);
		if (pfr_validate_addr(&ad))
			senderr(EINVAL);
		p = pfr_lookup_addr(kt, &ad, 1);
		if (flags & PFR_FLAG_FEEDBACK) {
			ad.pfra_fback = (p != NULL) ?
			    PFR_FB_CLEARED : PFR_FB_NONE;
			if (COPYOUT(&ad, addr+i, sizeof(ad), flags))
				senderr(EFAULT);
		}
		if (p != NULL) {
			SLIST_INSERT_HEAD(&workq, p, pfrke_workq);
			xzero++;
		}
	}

	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_clstats_kentries(&workq, gettime(), 0);
	}
	if (nzero != NULL)
		*nzero = xzero;
	return (0);
_bad:
	if (flags & PFR_FLAG_FEEDBACK)
		pfr_reset_feedback(addr, size, flags);
	return (rv);
}

int
pfr_validate_addr(struct pfr_addr *ad)
{
	int i;

	switch (ad->pfra_af) {
	case AF_INET:
		if (ad->pfra_net > 32)
			return (-1);
		break;
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
	if (ad->pfra_fback != PFR_FB_NONE)
		return (-1);
	if (ad->pfra_type >= PFRKE_MAX)
		return (-1);
	return (0);
}

void
pfr_enqueue_addrs(struct pfr_ktable *kt, struct pfr_kentryworkq *workq,
	int *naddr, int sweep)
{
	struct pfr_walktree	w;

	SLIST_INIT(workq);
	bzero(&w, sizeof(w));
	w.pfrw_op = sweep ? PFRW_SWEEP : PFRW_ENQUEUE;
	w.pfrw_workq = workq;
	if (kt->pfrkt_ip4 != NULL)
		if (rn_walktree(kt->pfrkt_ip4, pfr_walktree, &w))
			DPFPRINTF(LOG_ERR,
			    "pfr_enqueue_addrs: IPv4 walktree failed.");
	if (kt->pfrkt_ip6 != NULL)
		if (rn_walktree(kt->pfrkt_ip6, pfr_walktree, &w))
			DPFPRINTF(LOG_ERR,
			    "pfr_enqueue_addrs: IPv6 walktree failed.");
	if (naddr != NULL)
		*naddr = w.pfrw_cnt;
}

void
pfr_mark_addrs(struct pfr_ktable *kt)
{
	struct pfr_walktree	w;

	bzero(&w, sizeof(w));
	w.pfrw_op = PFRW_MARK;
	if (rn_walktree(kt->pfrkt_ip4, pfr_walktree, &w))
		DPFPRINTF(LOG_ERR,
		    "pfr_mark_addrs: IPv4 walktree failed.");
	if (rn_walktree(kt->pfrkt_ip6, pfr_walktree, &w))
		DPFPRINTF(LOG_ERR,
		    "pfr_mark_addrs: IPv6 walktree failed.");
}


struct pfr_kentry *
pfr_lookup_addr(struct pfr_ktable *kt, struct pfr_addr *ad, int exact)
{
	union sockaddr_union	 sa, mask;
	struct radix_node_head	*head;
	struct pfr_kentry	*ke;

	bzero(&sa, sizeof(sa));
	switch (ad->pfra_af) {
	case AF_INET:
		FILLIN_SIN(sa.sin, ad->pfra_ip4addr);
		head = kt->pfrkt_ip4;
		break;
#ifdef	INET6
	case AF_INET6:
		FILLIN_SIN6(sa.sin6, ad->pfra_ip6addr);
		head = kt->pfrkt_ip6;
		break;
#endif	/* INET6 */
	default:
		unhandled_af(ad->pfra_af);
	}
	if (ADDR_NETWORK(ad)) {
		pfr_prepare_network(&mask, ad->pfra_af, ad->pfra_net);
		ke = (struct pfr_kentry *)rn_lookup(&sa, &mask, head);
	} else {
		ke = (struct pfr_kentry *)rn_match(&sa, head);
		if (exact && ke && KENTRY_NETWORK(ke))
			ke = NULL;
	}
	return (ke);
}

struct pfr_kentry *
pfr_lookup_kentry(struct pfr_ktable *kt, struct pfr_kentry *key, int exact)
{
	union sockaddr_union	 mask;
	struct radix_node_head	*head;
	struct pfr_kentry	*ke;

	switch (key->pfrke_af) {
	case AF_INET:
		head = kt->pfrkt_ip4;
		break;
#ifdef	INET6
	case AF_INET6:
		head = kt->pfrkt_ip6;
		break;
#endif	/* INET6 */
	default:
		unhandled_af(key->pfrke_af);
	}
	if (KENTRY_NETWORK(key)) {
		pfr_prepare_network(&mask, key->pfrke_af, key->pfrke_net);
		ke = (struct pfr_kentry *)rn_lookup(&key->pfrke_sa, &mask,
		    head);
	} else {
		ke = (struct pfr_kentry *)rn_match(&key->pfrke_sa, head);
		if (exact && ke && KENTRY_NETWORK(ke))
			ke = NULL;
	}
	return (ke);
}

struct pfr_kentry *
pfr_create_kentry(struct pfr_addr *ad)
{
	struct pfr_kentry_all	*ke;

	if (ad->pfra_type >= PFRKE_MAX)
		panic("unknown pfra_type %d", ad->pfra_type);

	ke = pool_get(&pfr_kentry_pl[ad->pfra_type], PR_NOWAIT | PR_ZERO);
	if (ke == NULL)
		return (NULL);

	ke->pfrke_type = ad->pfra_type;

	/* set weight allowing implicit weights */
	if (ad->pfra_weight == 0)
		ad->pfra_weight = 1;

	switch (ke->pfrke_type) {
	case PFRKE_PLAIN:
		break;
	case PFRKE_COST:
		((struct pfr_kentry_cost *)ke)->weight = ad->pfra_weight;
		/* FALLTHROUGH */
	case PFRKE_ROUTE:
		if (ad->pfra_ifname[0])
			ke->pfrke_rkif = pfi_kif_get(ad->pfra_ifname, NULL);
		if (ke->pfrke_rkif)
			pfi_kif_ref(ke->pfrke_rkif, PFI_KIF_REF_ROUTE);
		break;
	}

	switch (ad->pfra_af) {
	case AF_INET:
		FILLIN_SIN(ke->pfrke_sa.sin, ad->pfra_ip4addr);
		break;
#ifdef	INET6
	case AF_INET6:
		FILLIN_SIN6(ke->pfrke_sa.sin6, ad->pfra_ip6addr);
		break;
#endif	/* INET6 */
	default:
		unhandled_af(ad->pfra_af);
	}
	ke->pfrke_af = ad->pfra_af;
	ke->pfrke_net = ad->pfra_net;
	if (ad->pfra_not)
		ke->pfrke_flags |= PFRKE_FLAG_NOT;
	return ((struct pfr_kentry *)ke);
}

struct pfr_kentry *
pfr_create_kentry_unlocked(struct pfr_addr *ad, int flags)
{
	struct pfr_kentry_all	*ke;
	int mflags = PR_ZERO;

	if (ad->pfra_type >= PFRKE_MAX)
		panic("unknown pfra_type %d", ad->pfra_type);

	if (flags & PFR_FLAG_USERIOCTL)
		mflags |= PR_WAITOK;
	else
		mflags |= PR_NOWAIT;

	ke = pool_get(&pfr_kentry_pl[ad->pfra_type], mflags);
	if (ke == NULL)
		return (NULL);

	ke->pfrke_type = ad->pfra_type;

	/* set weight allowing implicit weights */
	if (ad->pfra_weight == 0)
		ad->pfra_weight = 1;

	switch (ke->pfrke_type) {
	case PFRKE_PLAIN:
		break;
	case PFRKE_COST:
		((struct pfr_kentry_cost *)ke)->weight = ad->pfra_weight;
		/* FALLTHROUGH */
	case PFRKE_ROUTE:
		if (ad->pfra_ifname[0])
			(void) strlcpy(ke->pfrke_rifname, ad->pfra_ifname,
			    IFNAMSIZ);
		break;
	}

	switch (ad->pfra_af) {
	case AF_INET:
		FILLIN_SIN(ke->pfrke_sa.sin, ad->pfra_ip4addr);
		break;
#ifdef	INET6
	case AF_INET6:
		FILLIN_SIN6(ke->pfrke_sa.sin6, ad->pfra_ip6addr);
		break;
#endif	/* INET6 */
	default:
		unhandled_af(ad->pfra_af);
	}
	ke->pfrke_af = ad->pfra_af;
	ke->pfrke_net = ad->pfra_net;
	if (ad->pfra_not)
		ke->pfrke_flags |= PFRKE_FLAG_NOT;
	return ((struct pfr_kentry *)ke);
}

void
pfr_kentry_kif_ref(struct pfr_kentry *ke_all)
{
	struct pfr_kentry_all	*ke = (struct pfr_kentry_all *)ke_all;

	NET_ASSERT_LOCKED();
	switch (ke->pfrke_type) {
	case PFRKE_PLAIN:
		break;
	case PFRKE_COST:
	case PFRKE_ROUTE:
		if (ke->pfrke_rifname[0])
			ke->pfrke_rkif = pfi_kif_get(ke->pfrke_rifname, NULL);
		if (ke->pfrke_rkif)
			pfi_kif_ref(ke->pfrke_rkif, PFI_KIF_REF_ROUTE);
		break;
	}
}

void
pfr_destroy_kentries(struct pfr_kentryworkq *workq)
{
	struct pfr_kentry	*p;

	while ((p = SLIST_FIRST(workq)) != NULL) {
		YIELD(1);
		SLIST_REMOVE_HEAD(workq, pfrke_workq);
		pfr_destroy_kentry(p);
	}
}

void
pfr_destroy_ioq(struct pfr_kentryworkq *ioq, int flags)
{
	struct pfr_kentry	*p;

	while ((p = SLIST_FIRST(ioq)) != NULL) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		SLIST_REMOVE_HEAD(ioq, pfrke_ioq);
		/*
		 * we destroy only those entries, which did not make it to
		 * table
		 */
		if ((p->pfrke_fb != PFR_FB_ADDED) || (flags & PFR_FLAG_DUMMY))
			pfr_destroy_kentry(p);
	}
}

void
pfr_destroy_kentry(struct pfr_kentry *ke)
{
	if (ke->pfrke_counters)
		pool_put(&pfr_kcounters_pl, ke->pfrke_counters);
	if (ke->pfrke_type == PFRKE_COST || ke->pfrke_type == PFRKE_ROUTE)
		pfi_kif_unref(((struct pfr_kentry_all *)ke)->pfrke_rkif,
		    PFI_KIF_REF_ROUTE);
	pool_put(&pfr_kentry_pl[ke->pfrke_type], ke);
}

void
pfr_insert_kentries(struct pfr_ktable *kt,
    struct pfr_kentryworkq *workq, time_t tzero)
{
	struct pfr_kentry	*p;
	int			 rv, n = 0;

	SLIST_FOREACH(p, workq, pfrke_workq) {
		rv = pfr_route_kentry(kt, p);
		if (rv) {
			DPFPRINTF(LOG_ERR,
			    "pfr_insert_kentries: cannot route entry "
			    "(code=%d).", rv);
			break;
		}
		p->pfrke_tzero = tzero;
		++n;
		if (p->pfrke_type == PFRKE_COST)
			kt->pfrkt_refcntcost++;
		pfr_ktable_winfo_update(kt, p);
		YIELD(1);
	}
	kt->pfrkt_cnt += n;
}

int
pfr_insert_kentry(struct pfr_ktable *kt, struct pfr_addr *ad, time_t tzero)
{
	struct pfr_kentry	*p;
	int			 rv;

	p = pfr_lookup_addr(kt, ad, 1);
	if (p != NULL)
		return (0);
	p = pfr_create_kentry(ad);
	if (p == NULL)
		return (EINVAL);

	rv = pfr_route_kentry(kt, p);
	if (rv)
		return (rv);

	p->pfrke_tzero = tzero;
	if (p->pfrke_type == PFRKE_COST)
		kt->pfrkt_refcntcost++;
	kt->pfrkt_cnt++;
	pfr_ktable_winfo_update(kt, p);

	return (0);
}

void
pfr_remove_kentries(struct pfr_ktable *kt,
    struct pfr_kentryworkq *workq)
{
	struct pfr_kentry	*p;
	struct pfr_kentryworkq   addrq;
	int			 n = 0;

	SLIST_FOREACH(p, workq, pfrke_workq) {
		pfr_unroute_kentry(kt, p);
		++n;
		YIELD(1);
		if (p->pfrke_type == PFRKE_COST)
			kt->pfrkt_refcntcost--;
	}
	kt->pfrkt_cnt -= n;
	pfr_destroy_kentries(workq);

	/* update maxweight and gcd for load balancing */
	if (kt->pfrkt_refcntcost > 0) {
		kt->pfrkt_gcdweight = 0;
		kt->pfrkt_maxweight = 1;
		pfr_enqueue_addrs(kt, &addrq, NULL, 0);
		SLIST_FOREACH(p, &addrq, pfrke_workq)
			pfr_ktable_winfo_update(kt, p);
	}
}

void
pfr_clean_node_mask(struct pfr_ktable *kt,
    struct pfr_kentryworkq *workq)
{
	struct pfr_kentry	*p;

	SLIST_FOREACH(p, workq, pfrke_workq) {
		pfr_unroute_kentry(kt, p);
	}
}

void
pfr_clstats_kentries(struct pfr_kentryworkq *workq, time_t tzero, int negchange)
{
	struct pfr_kentry	*p;

	SLIST_FOREACH(p, workq, pfrke_workq) {
		if (negchange)
			p->pfrke_flags ^= PFRKE_FLAG_NOT;
		if (p->pfrke_counters) {
			pool_put(&pfr_kcounters_pl, p->pfrke_counters);
			p->pfrke_counters = NULL;
		}
		p->pfrke_tzero = tzero;
	}
}

void
pfr_reset_feedback(struct pfr_addr *addr, int size, int flags)
{
	struct pfr_addr	ad;
	int		i;

	for (i = 0; i < size; i++) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(addr+i, &ad, sizeof(ad), flags))
			break;
		ad.pfra_fback = PFR_FB_NONE;
		if (COPYOUT(&ad, addr+i, sizeof(ad), flags))
			break;
	}
}

void
pfr_prepare_network(union sockaddr_union *sa, int af, int net)
{
#ifdef	INET6
	int	i;
#endif	/* INET6 */

	bzero(sa, sizeof(*sa));
	switch (af) {
	case AF_INET:
		sa->sin.sin_len = sizeof(sa->sin);
		sa->sin.sin_family = AF_INET;
		sa->sin.sin_addr.s_addr = net ? htonl(-1 << (32-net)) : 0;
		break;
#ifdef	INET6
	case AF_INET6:
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
		break;
#endif	/* INET6 */
	default:
		unhandled_af(af);
	}
}

int
pfr_route_kentry(struct pfr_ktable *kt, struct pfr_kentry *ke)
{
	union sockaddr_union	 mask;
	struct radix_node	*rn;
	struct radix_node_head	*head;

	bzero(ke->pfrke_node, sizeof(ke->pfrke_node));
	switch (ke->pfrke_af) {
	case AF_INET:
		head = kt->pfrkt_ip4;
		break;
#ifdef	INET6
	case AF_INET6:
		head = kt->pfrkt_ip6;
		break;
#endif	/* INET6 */
	default:
		unhandled_af(ke->pfrke_af);
	}

	if (KENTRY_NETWORK(ke)) {
		pfr_prepare_network(&mask, ke->pfrke_af, ke->pfrke_net);
		rn = rn_addroute(&ke->pfrke_sa, &mask, head, ke->pfrke_node, 0);
	} else
		rn = rn_addroute(&ke->pfrke_sa, NULL, head, ke->pfrke_node, 0);

	return (rn == NULL ? -1 : 0);
}

int
pfr_unroute_kentry(struct pfr_ktable *kt, struct pfr_kentry *ke)
{
	union sockaddr_union	 mask;
	struct radix_node	*rn;
	struct radix_node_head	*head;

	switch (ke->pfrke_af) {
	case AF_INET:
		head = kt->pfrkt_ip4;
		break;
#ifdef	INET6
	case AF_INET6:
		head = kt->pfrkt_ip6;
		break;
#endif	/* INET6 */
	default:
		unhandled_af(ke->pfrke_af);
	}

	if (KENTRY_NETWORK(ke)) {
		pfr_prepare_network(&mask, ke->pfrke_af, ke->pfrke_net);
		rn = rn_delete(&ke->pfrke_sa, &mask, head, NULL);
	} else
		rn = rn_delete(&ke->pfrke_sa, NULL, head, NULL);

	if (rn == NULL) {
		DPFPRINTF(LOG_ERR, "pfr_unroute_kentry: delete failed.\n");
		return (-1);
	}
	return (0);
}

void
pfr_copyout_addr(struct pfr_addr *ad, struct pfr_kentry *ke)
{
	bzero(ad, sizeof(*ad));
	if (ke == NULL)
		return;
	ad->pfra_af = ke->pfrke_af;
	ad->pfra_net = ke->pfrke_net;
	ad->pfra_type = ke->pfrke_type;
	if (ke->pfrke_flags & PFRKE_FLAG_NOT)
		ad->pfra_not = 1;

	switch (ad->pfra_af) {
	case AF_INET:
		ad->pfra_ip4addr = ke->pfrke_sa.sin.sin_addr;
		break;
#ifdef	INET6
	case AF_INET6:
		ad->pfra_ip6addr = ke->pfrke_sa.sin6.sin6_addr;
		break;
#endif	/* INET6 */
	default:
		unhandled_af(ad->pfra_af);
	}
	if (ke->pfrke_counters != NULL)
		ad->pfra_states = ke->pfrke_counters->states;
	switch (ke->pfrke_type) {
	case PFRKE_COST:
		ad->pfra_weight = ((struct pfr_kentry_cost *)ke)->weight;
		/* FALLTHROUGH */
	case PFRKE_ROUTE:
		if (((struct pfr_kentry_route *)ke)->kif != NULL)
			strlcpy(ad->pfra_ifname,
			    ((struct pfr_kentry_route *)ke)->kif->pfik_name,
			    IFNAMSIZ);
		break;
	default:
		break;
	}
}

int
pfr_walktree(struct radix_node *rn, void *arg, u_int id)
{
	struct pfr_kentry	*ke = (struct pfr_kentry *)rn;
	struct pfr_walktree	*w = arg;
	union sockaddr_union	 mask;
	int			 flags = w->pfrw_flags;

	switch (w->pfrw_op) {
	case PFRW_MARK:
		ke->pfrke_flags &= ~PFRKE_FLAG_MARK;
		break;
	case PFRW_SWEEP:
		if (ke->pfrke_flags & PFRKE_FLAG_MARK)
			break;
		/* FALLTHROUGH */
	case PFRW_ENQUEUE:
		SLIST_INSERT_HEAD(w->pfrw_workq, ke, pfrke_workq);
		w->pfrw_cnt++;
		break;
	case PFRW_GET_ADDRS:
		if (w->pfrw_free-- > 0) {
			struct pfr_addr ad;

			pfr_copyout_addr(&ad, ke);
			if (copyout(&ad, w->pfrw_addr, sizeof(ad)))
				return (EFAULT);
			w->pfrw_addr++;
		}
		break;
	case PFRW_GET_ASTATS:
		if (w->pfrw_free-- > 0) {
			struct pfr_astats as;

			pfr_copyout_addr(&as.pfras_a, ke);

			if (ke->pfrke_counters) {
				bcopy(ke->pfrke_counters->pfrkc_packets,
				    as.pfras_packets, sizeof(as.pfras_packets));
				bcopy(ke->pfrke_counters->pfrkc_bytes,
				    as.pfras_bytes, sizeof(as.pfras_bytes));
			} else {
				bzero(as.pfras_packets,
				    sizeof(as.pfras_packets));
				bzero(as.pfras_bytes, sizeof(as.pfras_bytes));
				as.pfras_a.pfra_fback = PFR_FB_NOCOUNT;
			}
			as.pfras_tzero = ke->pfrke_tzero;

			if (COPYOUT(&as, w->pfrw_astats, sizeof(as), flags))
				return (EFAULT);
			w->pfrw_astats++;
		}
		break;
	case PFRW_POOL_GET:
		if (ke->pfrke_flags & PFRKE_FLAG_NOT)
			break; /* negative entries are ignored */
		if (!w->pfrw_cnt--) {
			w->pfrw_kentry = ke;
			return (1); /* finish search */
		}
		break;
	case PFRW_DYNADDR_UPDATE:
		switch (ke->pfrke_af) {
		case AF_INET:
			if (w->pfrw_dyn->pfid_acnt4++ > 0)
				break;
			pfr_prepare_network(&mask, AF_INET, ke->pfrke_net);
			w->pfrw_dyn->pfid_addr4 = *SUNION2PF(
			    &ke->pfrke_sa, AF_INET);
			w->pfrw_dyn->pfid_mask4 = *SUNION2PF(
			    &mask, AF_INET);
			break;
#ifdef	INET6
		case AF_INET6:
			if (w->pfrw_dyn->pfid_acnt6++ > 0)
				break;
			pfr_prepare_network(&mask, AF_INET6, ke->pfrke_net);
			w->pfrw_dyn->pfid_addr6 = *SUNION2PF(
			    &ke->pfrke_sa, AF_INET6);
			w->pfrw_dyn->pfid_mask6 = *SUNION2PF(
			    &mask, AF_INET6);
			break;
#endif	/* INET6 */
		default:
			unhandled_af(ke->pfrke_af);
		}
		break;
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
	RB_FOREACH(p, pfr_ktablehead, &pfr_ktables) {
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
	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_setflags_ktables(&workq);
	}
	if (ndel != NULL)
		*ndel = xdel;
	return (0);
}

int
pfr_add_tables(struct pfr_table *tbl, int size, int *nadd, int flags)
{
	struct pfr_ktableworkq	 addq, changeq, auxq;
	struct pfr_ktable	*p, *q, *r, *n, *w, key;
	int			 i, rv, xadd = 0;
	time_t			 tzero = gettime();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	SLIST_INIT(&addq);
	SLIST_INIT(&changeq);
	SLIST_INIT(&auxq);
	/* pre-allocate all memory outside of locks */
	for (i = 0; i < size; i++) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(tbl+i, &key.pfrkt_t, sizeof(key.pfrkt_t), flags))
			senderr(EFAULT);
		if (pfr_validate_table(&key.pfrkt_t, PFR_TFLAG_USRMASK,
		    flags & PFR_FLAG_USERIOCTL))
			senderr(EINVAL);
		key.pfrkt_flags |= PFR_TFLAG_ACTIVE;
		p = pfr_create_ktable(&key.pfrkt_t, tzero, 0,
		    (flags & PFR_FLAG_USERIOCTL? PR_WAITOK : PR_NOWAIT));
		if (p == NULL)
			senderr(ENOMEM);

		/*
		 * Note: we also pre-allocate a root table here. We keep it
		 * at ->pfrkt_root, which we must not forget about.
		 */
		key.pfrkt_flags = 0;
		memset(key.pfrkt_anchor, 0, sizeof(key.pfrkt_anchor));
		p->pfrkt_root = pfr_create_ktable(&key.pfrkt_t, 0, 0,
		    (flags & PFR_FLAG_USERIOCTL? PR_WAITOK : PR_NOWAIT));
		if (p->pfrkt_root == NULL) {
			pfr_destroy_ktable(p, 0);
			senderr(ENOMEM);
		}

		SLIST_FOREACH(q, &auxq, pfrkt_workq) {
			if (!pfr_ktable_compare(p, q)) {
				/*
				 * We need no lock here, because `p` is empty,
				 * there are no rules or shadow tables
				 * attached.
				 */
				pfr_destroy_ktable(p->pfrkt_root, 0);
				p->pfrkt_root = NULL;
				pfr_destroy_ktable(p, 0);
				p = NULL;
				break;
			}
		}
		if (q != NULL)
			continue;

		SLIST_INSERT_HEAD(&auxq, p, pfrkt_workq);
	}

	/*
	 * auxq contains freshly allocated tables with no dups.
	 * also note there are no rulesets attached, because
	 * the attach operation requires PF_LOCK().
	 */
	NET_LOCK();
	PF_LOCK();
	SLIST_FOREACH_SAFE(n, &auxq, pfrkt_workq, w) {
		p = RB_FIND(pfr_ktablehead, &pfr_ktables, n);
		if (p == NULL) {
			SLIST_REMOVE(&auxq, n, pfr_ktable, pfrkt_workq);
			SLIST_INSERT_HEAD(&addq, n, pfrkt_workq);
			xadd++;
		} else if (!(flags & PFR_FLAG_DUMMY) &&
		    !(p->pfrkt_flags & PFR_TFLAG_ACTIVE)) {
			p->pfrkt_nflags =
			    (p->pfrkt_flags & ~PFR_TFLAG_USRMASK) |
			    (n->pfrkt_flags & PFR_TFLAG_USRMASK) |
			    PFR_TFLAG_ACTIVE;
			SLIST_INSERT_HEAD(&changeq, p, pfrkt_workq);
		}
	}

	if (!(flags & PFR_FLAG_DUMMY)) {
		/*
		 * addq contains tables we have to insert and attach rules to
		 * them
		 *
		 * changeq contains tables we need to update
		 *
		 * auxq contains pre-allocated tables, we won't use and we must
		 * free them
		 */
		SLIST_FOREACH_SAFE(p, &addq, pfrkt_workq, w) {
			p->pfrkt_rs = pf_find_or_create_ruleset(
			    p->pfrkt_anchor);
			if (p->pfrkt_rs == NULL) {
				xadd--;
				SLIST_REMOVE(&addq, p, pfr_ktable, pfrkt_workq);
				SLIST_INSERT_HEAD(&auxq, p, pfrkt_workq);
				continue;
			}
			p->pfrkt_rs->tables++;

			if (!p->pfrkt_anchor[0]) {
				q = p->pfrkt_root;
				p->pfrkt_root = NULL;
				SLIST_INSERT_HEAD(&auxq, q, pfrkt_workq);
				continue;
			}

			/* use pre-allocated root table as a key */
			q = p->pfrkt_root;
			p->pfrkt_root = NULL;
			r = RB_FIND(pfr_ktablehead, &pfr_ktables, q);
			if (r != NULL) {
				p->pfrkt_root = r;
				SLIST_INSERT_HEAD(&auxq, q, pfrkt_workq);
				continue;
			}
			/*
			 * there is a chance we could create root table in
			 * earlier iteration. such table may exist in addq only
			 * then.
			 */
			SLIST_FOREACH(r, &addq, pfrkt_workq) {
				if (!pfr_ktable_compare(r, q)) {
					/*
					 * `r` is our root table we've found
					 * earlier, `q` can get dropped.
					 */
					p->pfrkt_root = r;
					SLIST_INSERT_HEAD(&auxq, q,
					    pfrkt_workq);
					break;
				}
			}
			if (r != NULL)
				continue;

			q->pfrkt_rs = pf_find_or_create_ruleset(q->pfrkt_anchor);
			/*
			 * root tables are attached to main ruleset,
			 * because ->pfrkt_anchor[0] == '\0'
			 */
			KASSERT(q->pfrkt_rs == &pf_main_ruleset);
			q->pfrkt_rs->tables++;
			p->pfrkt_root = q;
			SLIST_INSERT_HEAD(&addq, q, pfrkt_workq);
		}

		pfr_insert_ktables(&addq);
		pfr_setflags_ktables(&changeq);
	}
	PF_UNLOCK();
	NET_UNLOCK();

	pfr_destroy_ktables_aux(&auxq);
	if (flags & PFR_FLAG_DUMMY)
		pfr_destroy_ktables_aux(&addq);

	if (nadd != NULL)
		*nadd = xadd;
	return (0);
_bad:
	pfr_destroy_ktables_aux(&auxq);
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
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(tbl+i, &key.pfrkt_t, sizeof(key.pfrkt_t), flags))
			return (EFAULT);
		if (pfr_validate_table(&key.pfrkt_t, 0,
		    flags & PFR_FLAG_USERIOCTL))
			return (EINVAL);
		p = RB_FIND(pfr_ktablehead, &pfr_ktables, &key);
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

	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_setflags_ktables(&workq);
	}
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
	RB_FOREACH(p, pfr_ktablehead, &pfr_ktables) {
		if (pfr_skip_table(filter, p, flags))
			continue;
		if (n-- <= 0)
			continue;
		if (COPYOUT(&p->pfrkt_t, tbl++, sizeof(*tbl), flags))
			return (EFAULT);
	}
	if (n) {
		DPFPRINTF(LOG_ERR,
		    "pfr_get_tables: corruption detected (%d).", n);
		return (ENOTTY);
	}
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
	time_t			 tzero = gettime();

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
	RB_FOREACH(p, pfr_ktablehead, &pfr_ktables) {
		if (pfr_skip_table(filter, p, flags))
			continue;
		if (n-- <= 0)
			continue;
		if (COPYOUT(&p->pfrkt_ts, tbl++, sizeof(*tbl), flags))
			return (EFAULT);
		SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
	}
	if (flags & PFR_FLAG_CLSTATS)
		pfr_clstats_ktables(&workq, tzero,
		    flags & PFR_FLAG_ADDRSTOO);
	if (n) {
		DPFPRINTF(LOG_ERR,
		    "pfr_get_tstats: corruption detected (%d).", n);
		return (ENOTTY);
	}
	*size = nn;
	return (0);
}

int
pfr_clr_tstats(struct pfr_table *tbl, int size, int *nzero, int flags)
{
	struct pfr_ktableworkq	 workq;
	struct pfr_ktable	*p, key;
	int			 i, xzero = 0;
	time_t			 tzero = gettime();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY | PFR_FLAG_ADDRSTOO);
	SLIST_INIT(&workq);
	for (i = 0; i < size; i++) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(tbl+i, &key.pfrkt_t, sizeof(key.pfrkt_t), flags))
			return (EFAULT);
		if (pfr_validate_table(&key.pfrkt_t, 0, 0))
			return (EINVAL);
		p = RB_FIND(pfr_ktablehead, &pfr_ktables, &key);
		if (p != NULL) {
			SLIST_INSERT_HEAD(&workq, p, pfrkt_workq);
			xzero++;
		}
	}
	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_clstats_ktables(&workq, tzero, flags & PFR_FLAG_ADDRSTOO);
	}
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
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(tbl+i, &key.pfrkt_t, sizeof(key.pfrkt_t), flags))
			return (EFAULT);
		if (pfr_validate_table(&key.pfrkt_t, 0,
		    flags & PFR_FLAG_USERIOCTL))
			return (EINVAL);
		p = RB_FIND(pfr_ktablehead, &pfr_ktables, &key);
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
	if (!(flags & PFR_FLAG_DUMMY)) {
		pfr_setflags_ktables(&workq);
	}
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
	RB_FOREACH(p, pfr_ktablehead, &pfr_ktables) {
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
	struct pfr_addr		 ad;
	struct pf_ruleset	*rs;
	int			 i, rv, xadd = 0, xaddr = 0;

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
	kt = RB_FIND(pfr_ktablehead, &pfr_ktables, (struct pfr_ktable *)tbl);
	if (kt == NULL) {
		kt = pfr_create_ktable(tbl, 0, 1,
		    (flags & PFR_FLAG_USERIOCTL? PR_WAITOK : PR_NOWAIT));
		if (kt == NULL)
			return (ENOMEM);
		SLIST_INSERT_HEAD(&tableq, kt, pfrkt_workq);
		xadd++;
		if (!tbl->pfrt_anchor[0])
			goto _skip;

		/* find or create root table */
		bzero(&key, sizeof(key));
		strlcpy(key.pfrkt_name, tbl->pfrt_name, sizeof(key.pfrkt_name));
		rt = RB_FIND(pfr_ktablehead, &pfr_ktables, &key);
		if (rt != NULL) {
			kt->pfrkt_root = rt;
			goto _skip;
		}
		rt = pfr_create_ktable(&key.pfrkt_t, 0, 1,
		    (flags & PFR_FLAG_USERIOCTL? PR_WAITOK : PR_NOWAIT));
		if (rt == NULL) {
			pfr_destroy_ktables(&tableq, 0);
			return (ENOMEM);
		}
		SLIST_INSERT_HEAD(&tableq, rt, pfrkt_workq);
		kt->pfrkt_root = rt;
	} else if (!(kt->pfrkt_flags & PFR_TFLAG_INACTIVE))
		xadd++;
_skip:
	shadow = pfr_create_ktable(tbl, 0, 0,
	    (flags & PFR_FLAG_USERIOCTL? PR_WAITOK : PR_NOWAIT));
	if (shadow == NULL) {
		pfr_destroy_ktables(&tableq, 0);
		return (ENOMEM);
	}
	SLIST_INIT(&addrq);
	for (i = 0; i < size; i++) {
		YIELD(flags & PFR_FLAG_USERIOCTL);
		if (COPYIN(addr+i, &ad, sizeof(ad), flags))
			senderr(EFAULT);
		if (pfr_validate_addr(&ad))
			senderr(EINVAL);
		if (pfr_lookup_addr(shadow, &ad, 1) != NULL)
			continue;
		p = pfr_create_kentry(&ad);
		if (p == NULL)
			senderr(ENOMEM);
		if (pfr_route_kentry(shadow, p)) {
			pfr_destroy_kentry(p);
			continue;
		}
		SLIST_INSERT_HEAD(&addrq, p, pfrke_workq);
		xaddr++;
		if (p->pfrke_type == PFRKE_COST)
			kt->pfrkt_refcntcost++;
		pfr_ktable_winfo_update(kt, p);
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

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	rs = pf_find_ruleset(trs->pfrt_anchor);
	if (rs == NULL || !rs->topen || ticket != rs->tticket)
		return (0);
	SLIST_INIT(&workq);
	RB_FOREACH(p, pfr_ktablehead, &pfr_ktables) {
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
	time_t			 tzero = gettime();

	ACCEPT_FLAGS(flags, PFR_FLAG_DUMMY);
	rs = pf_find_ruleset(trs->pfrt_anchor);
	if (rs == NULL || !rs->topen || ticket != rs->tticket)
		return (EBUSY);

	SLIST_INIT(&workq);
	RB_FOREACH(p, pfr_ktablehead, &pfr_ktables) {
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
		SLIST_FOREACH_SAFE(p, &workq, pfrkt_workq, q) {
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

void
pfr_commit_ktable(struct pfr_ktable *kt, time_t tzero)
{
	struct pfr_ktable	*shadow = kt->pfrkt_shadow;
	int			 nflags;

	if (shadow->pfrkt_cnt == NO_ADDRESSES) {
		if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
			pfr_clstats_ktable(kt, tzero, 1);
	} else if (kt->pfrkt_flags & PFR_TFLAG_ACTIVE) {
		/* kt might contain addresses */
		struct pfr_kentryworkq	 addrq, addq, changeq, delq, garbageq;
		struct pfr_kentry	*p, *q;
		struct pfr_addr		 ad;

		pfr_enqueue_addrs(shadow, &addrq, NULL, 0);
		pfr_mark_addrs(kt);
		SLIST_INIT(&addq);
		SLIST_INIT(&changeq);
		SLIST_INIT(&delq);
		SLIST_INIT(&garbageq);
		pfr_clean_node_mask(shadow, &addrq);
		while ((p = SLIST_FIRST(&addrq)) != NULL) {
			SLIST_REMOVE_HEAD(&addrq, pfrke_workq);
			pfr_copyout_addr(&ad, p);
			q = pfr_lookup_addr(kt, &ad, 1);
			if (q != NULL) {
				if ((q->pfrke_flags & PFRKE_FLAG_NOT) !=
				    (p->pfrke_flags & PFRKE_FLAG_NOT))
					SLIST_INSERT_HEAD(&changeq, q,
					    pfrke_workq);
				q->pfrke_flags |= PFRKE_FLAG_MARK;
				SLIST_INSERT_HEAD(&garbageq, p, pfrke_workq);
			} else {
				p->pfrke_tzero = tzero;
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

int
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
int
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

	if (flags & PFR_FLAG_ALLRSETS)
		return (pfr_ktable_cnt);
	if (filter->pfrt_anchor[0]) {
		rs = pf_find_ruleset(filter->pfrt_anchor);
		return ((rs != NULL) ? rs->tables : -1);
	}
	return (pf_main_ruleset.tables);
}

int
pfr_skip_table(struct pfr_table *filter, struct pfr_ktable *kt, int flags)
{
	if (flags & PFR_FLAG_ALLRSETS)
		return (0);
	if (strcmp(filter->pfrt_anchor, kt->pfrkt_anchor))
		return (1);
	return (0);
}

void
pfr_insert_ktables(struct pfr_ktableworkq *workq)
{
	struct pfr_ktable	*p;

	SLIST_FOREACH(p, workq, pfrkt_workq)
		pfr_insert_ktable(p);
}

void
pfr_insert_ktable(struct pfr_ktable *kt)
{
	RB_INSERT(pfr_ktablehead, &pfr_ktables, kt);
	pfr_ktable_cnt++;
	if (kt->pfrkt_root != NULL)
		if (!kt->pfrkt_root->pfrkt_refcnt[PFR_REFCNT_ANCHOR]++)
			pfr_setflags_ktable(kt->pfrkt_root,
			    kt->pfrkt_root->pfrkt_flags|PFR_TFLAG_REFDANCHOR);
}

void
pfr_setflags_ktables(struct pfr_ktableworkq *workq)
{
	struct pfr_ktable	*p, *q;

	SLIST_FOREACH_SAFE(p, workq, pfrkt_workq, q) {
		pfr_setflags_ktable(p, p->pfrkt_nflags);
	}
}

void
pfr_setflags_ktable(struct pfr_ktable *kt, int newf)
{
	struct pfr_kentryworkq	addrq;

	if (!(newf & PFR_TFLAG_REFERENCED) &&
	    !(newf & PFR_TFLAG_REFDANCHOR) &&
	    !(newf & PFR_TFLAG_PERSIST))
		newf &= ~PFR_TFLAG_ACTIVE;
	if (!(newf & PFR_TFLAG_ACTIVE))
		newf &= ~PFR_TFLAG_USRMASK;
	if (!(newf & PFR_TFLAG_SETMASK)) {
		RB_REMOVE(pfr_ktablehead, &pfr_ktables, kt);
		if (kt->pfrkt_root != NULL)
			if (!--kt->pfrkt_root->pfrkt_refcnt[PFR_REFCNT_ANCHOR])
				pfr_setflags_ktable(kt->pfrkt_root,
				    kt->pfrkt_root->pfrkt_flags &
					~PFR_TFLAG_REFDANCHOR);
		pfr_destroy_ktable(kt, 1);
		pfr_ktable_cnt--;
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

void
pfr_clstats_ktables(struct pfr_ktableworkq *workq, time_t tzero, int recurse)
{
	struct pfr_ktable	*p;

	SLIST_FOREACH(p, workq, pfrkt_workq)
		pfr_clstats_ktable(p, tzero, recurse);
}

void
pfr_clstats_ktable(struct pfr_ktable *kt, time_t tzero, int recurse)
{
	struct pfr_kentryworkq	 addrq;

	if (recurse) {
		pfr_enqueue_addrs(kt, &addrq, NULL, 0);
		pfr_clstats_kentries(&addrq, tzero, 0);
	}
	bzero(kt->pfrkt_packets, sizeof(kt->pfrkt_packets));
	bzero(kt->pfrkt_bytes, sizeof(kt->pfrkt_bytes));
	kt->pfrkt_match = kt->pfrkt_nomatch = 0;
	kt->pfrkt_tzero = tzero;
}

struct pfr_ktable *
pfr_create_ktable(struct pfr_table *tbl, time_t tzero, int attachruleset,
    int wait)
{
	struct pfr_ktable	*kt;
	struct pf_ruleset	*rs;

	kt = pool_get(&pfr_ktable_pl, wait|PR_ZERO|PR_LIMITFAIL);
	if (kt == NULL)
		return (NULL);
	kt->pfrkt_t = *tbl;

	if (attachruleset) {
		PF_ASSERT_LOCKED();
		rs = pf_find_or_create_ruleset(tbl->pfrt_anchor);
		if (!rs) {
			pfr_destroy_ktable(kt, 0);
			return (NULL);
		}
		kt->pfrkt_rs = rs;
		rs->tables++;
	}

	if (!rn_inithead((void **)&kt->pfrkt_ip4,
	    offsetof(struct sockaddr_in, sin_addr)) ||
	    !rn_inithead((void **)&kt->pfrkt_ip6,
	    offsetof(struct sockaddr_in6, sin6_addr))) {
		pfr_destroy_ktable(kt, 0);
		return (NULL);
	}
	kt->pfrkt_tzero = tzero;
	kt->pfrkt_refcntcost = 0;
	kt->pfrkt_gcdweight = 0;
	kt->pfrkt_maxweight = 1;

	return (kt);
}

void
pfr_destroy_ktables(struct pfr_ktableworkq *workq, int flushaddr)
{
	struct pfr_ktable	*p;

	while ((p = SLIST_FIRST(workq)) != NULL) {
		SLIST_REMOVE_HEAD(workq, pfrkt_workq);
		pfr_destroy_ktable(p, flushaddr);
	}
}

void
pfr_destroy_ktables_aux(struct pfr_ktableworkq *auxq)
{
	struct pfr_ktable	*p;

	while ((p = SLIST_FIRST(auxq)) != NULL) {
		SLIST_REMOVE_HEAD(auxq, pfrkt_workq);
		/*
		 * There must be no extra data (rules, shadow tables, ...)
		 * attached, because auxq holds just empty memory to be
		 * initialized. Therefore we can also be called with no lock.
		 */
		if (p->pfrkt_root != NULL) {
			KASSERT(p->pfrkt_root->pfrkt_rs == NULL);
			KASSERT(p->pfrkt_root->pfrkt_shadow == NULL);
			KASSERT(p->pfrkt_root->pfrkt_root == NULL);
			pfr_destroy_ktable(p->pfrkt_root, 0);
			p->pfrkt_root = NULL;
		}
		KASSERT(p->pfrkt_rs == NULL);
		KASSERT(p->pfrkt_shadow == NULL);
		pfr_destroy_ktable(p, 0);
	}
}

void
pfr_destroy_ktable(struct pfr_ktable *kt, int flushaddr)
{
	struct pfr_kentryworkq	 addrq;

	if (flushaddr) {
		pfr_enqueue_addrs(kt, &addrq, NULL, 0);
		pfr_clean_node_mask(kt, &addrq);
		pfr_destroy_kentries(&addrq);
	}
	if (kt->pfrkt_ip4 != NULL)
		free(kt->pfrkt_ip4, M_RTABLE, sizeof(*kt->pfrkt_ip4));
	if (kt->pfrkt_ip6 != NULL)
		free(kt->pfrkt_ip6, M_RTABLE, sizeof(*kt->pfrkt_ip6));
	if (kt->pfrkt_shadow != NULL)
		pfr_destroy_ktable(kt->pfrkt_shadow, flushaddr);
	if (kt->pfrkt_rs != NULL) {
		kt->pfrkt_rs->tables--;
		pf_remove_if_empty_ruleset(kt->pfrkt_rs);
	}
	pool_put(&pfr_ktable_pl, kt);
}

int
pfr_ktable_compare(struct pfr_ktable *p, struct pfr_ktable *q)
{
	int d;

	if ((d = strncmp(p->pfrkt_name, q->pfrkt_name, PF_TABLE_NAME_SIZE)))
		return (d);
	return (strcmp(p->pfrkt_anchor, q->pfrkt_anchor));
}

struct pfr_ktable *
pfr_lookup_table(struct pfr_table *tbl)
{
	/* struct pfr_ktable start like a struct pfr_table */
	return (RB_FIND(pfr_ktablehead, &pfr_ktables,
	    (struct pfr_ktable *)tbl));
}

int
pfr_match_addr(struct pfr_ktable *kt, struct pf_addr *a, sa_family_t af)
{
	struct pfr_kentry	*ke = NULL;
	int			 match;

	ke = pfr_kentry_byaddr(kt, a, af, 0);

	match = (ke && !(ke->pfrke_flags & PFRKE_FLAG_NOT));
	if (match)
		kt->pfrkt_match++;
	else
		kt->pfrkt_nomatch++;

	return (match);
}

struct pfr_kentry *
pfr_kentry_byaddr(struct pfr_ktable *kt, struct pf_addr *a, sa_family_t af,
    int exact)
{
	struct pfr_kentry	*ke = NULL;
	struct sockaddr_in	 tmp4;
#ifdef INET6
	struct sockaddr_in6	 tmp6;
#endif /* INET6 */

	kt = pfr_ktable_select_active(kt);
	if (kt == NULL)
		return (0);

	switch (af) {
	case AF_INET:
		bzero(&tmp4, sizeof(tmp4));
		tmp4.sin_len = sizeof(tmp4);
		tmp4.sin_family = AF_INET;
		tmp4.sin_addr.s_addr = a->addr32[0];
		ke = (struct pfr_kentry *)rn_match(&tmp4, kt->pfrkt_ip4);
		break;
#ifdef INET6
	case AF_INET6:
		bzero(&tmp6, sizeof(tmp6));
		tmp6.sin6_len = sizeof(tmp6);
		tmp6.sin6_family = AF_INET6;
		bcopy(a, &tmp6.sin6_addr, sizeof(tmp6.sin6_addr));
		ke = (struct pfr_kentry *)rn_match(&tmp6, kt->pfrkt_ip6);
		break;
#endif /* INET6 */
	default:
		unhandled_af(af);
	}
	if (exact && ke && KENTRY_NETWORK(ke))
		ke = NULL;

	return (ke);
}

void
pfr_update_stats(struct pfr_ktable *kt, struct pf_addr *a, struct pf_pdesc *pd,
    int op, int notrule)
{
	struct pfr_kentry	*ke = NULL;
	struct sockaddr_in	 tmp4;
#ifdef INET6
	struct sockaddr_in6	 tmp6;
#endif /* INET6 */
	sa_family_t		 af = pd->af;
	u_int64_t		 len = pd->tot_len;
	int			 dir_idx = (pd->dir == PF_OUT);
	int			 op_idx;

	kt = pfr_ktable_select_active(kt);
	if (kt == NULL)
		return;

	switch (af) {
	case AF_INET:
		bzero(&tmp4, sizeof(tmp4));
		tmp4.sin_len = sizeof(tmp4);
		tmp4.sin_family = AF_INET;
		tmp4.sin_addr.s_addr = a->addr32[0];
		ke = (struct pfr_kentry *)rn_match(&tmp4, kt->pfrkt_ip4);
		break;
#ifdef INET6
	case AF_INET6:
		bzero(&tmp6, sizeof(tmp6));
		tmp6.sin6_len = sizeof(tmp6);
		tmp6.sin6_family = AF_INET6;
		bcopy(a, &tmp6.sin6_addr, sizeof(tmp6.sin6_addr));
		ke = (struct pfr_kentry *)rn_match(&tmp6, kt->pfrkt_ip6);
		break;
#endif /* INET6 */
	default:
		unhandled_af(af);
	}

	switch (op) {
	case PF_PASS:
		op_idx = PFR_OP_PASS;
		break;
	case PF_MATCH:
		op_idx = PFR_OP_MATCH;
		break;
	case PF_DROP:
		op_idx = PFR_OP_BLOCK;
		break;
	default:
		panic("unhandled op");
	}

	if ((ke == NULL || (ke->pfrke_flags & PFRKE_FLAG_NOT)) != notrule) {
		if (op_idx != PFR_OP_PASS)
			DPFPRINTF(LOG_DEBUG,
			    "pfr_update_stats: assertion failed.");
		op_idx = PFR_OP_XPASS;
	}
	kt->pfrkt_packets[dir_idx][op_idx]++;
	kt->pfrkt_bytes[dir_idx][op_idx] += len;
	if (ke != NULL && op_idx != PFR_OP_XPASS &&
	    (kt->pfrkt_flags & PFR_TFLAG_COUNTERS)) {
		if (ke->pfrke_counters == NULL)
			ke->pfrke_counters = pool_get(&pfr_kcounters_pl,
			    PR_NOWAIT | PR_ZERO);
		if (ke->pfrke_counters != NULL) {
			ke->pfrke_counters->pfrkc_packets[dir_idx][op_idx]++;
			ke->pfrke_counters->pfrkc_bytes[dir_idx][op_idx] += len;
		}
	}
}

struct pfr_ktable *
pfr_attach_table(struct pf_ruleset *rs, char *name, int wait)
{
	struct pfr_ktable	*kt, *rt;
	struct pfr_table	 tbl;
	struct pf_anchor	*ac = rs->anchor;

	bzero(&tbl, sizeof(tbl));
	strlcpy(tbl.pfrt_name, name, sizeof(tbl.pfrt_name));
	if (ac != NULL)
		strlcpy(tbl.pfrt_anchor, ac->path, sizeof(tbl.pfrt_anchor));
	kt = pfr_lookup_table(&tbl);
	if (kt == NULL) {
		kt = pfr_create_ktable(&tbl, gettime(), 1, wait);
		if (kt == NULL)
			return (NULL);
		if (ac != NULL) {
			bzero(tbl.pfrt_anchor, sizeof(tbl.pfrt_anchor));
			rt = pfr_lookup_table(&tbl);
			if (rt == NULL) {
				rt = pfr_create_ktable(&tbl, 0, 1, wait);
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
	if (kt->pfrkt_refcnt[PFR_REFCNT_RULE] <= 0)
		DPFPRINTF(LOG_NOTICE, "pfr_detach_table: refcount = %d.",
		    kt->pfrkt_refcnt[PFR_REFCNT_RULE]);
	else if (!--kt->pfrkt_refcnt[PFR_REFCNT_RULE])
		pfr_setflags_ktable(kt, kt->pfrkt_flags&~PFR_TFLAG_REFERENCED);
}

int
pfr_islinklocal(sa_family_t af, struct pf_addr *addr)
{
#ifdef	INET6
	if (af == AF_INET6 && IN6_IS_ADDR_LINKLOCAL(&addr->v6))
		return (1);
#endif	/* INET6 */
	return (0);
}

int
pfr_pool_get(struct pf_pool *rpool, struct pf_addr **raddr,
    struct pf_addr **rmask, sa_family_t af)
{
	struct pfr_ktable	*kt;
	struct pfr_kentry	*ke, *ke2;
	struct pf_addr		*addr, *counter;
	union sockaddr_union	 mask;
	struct sockaddr_in	 tmp4;
#ifdef INET6
	struct sockaddr_in6	 tmp6;
#endif
	int			 startidx, idx = -1, loop = 0, use_counter = 0;

	switch (af) {
	case AF_INET:
		bzero(&tmp4, sizeof(tmp4));
		tmp4.sin_len = sizeof(tmp4);
		tmp4.sin_family = AF_INET;
		addr = (struct pf_addr *)&tmp4.sin_addr;
		break;
#ifdef	INET6
	case AF_INET6:
		bzero(&tmp6, sizeof(tmp6));
		tmp6.sin6_len = sizeof(tmp6);
		tmp6.sin6_family = AF_INET6;
		addr = (struct pf_addr *)&tmp6.sin6_addr;
		break;
#endif	/* INET6 */
	default:
		unhandled_af(af);
	}

	if (rpool->addr.type == PF_ADDR_TABLE)
		kt = rpool->addr.p.tbl;
	else if (rpool->addr.type == PF_ADDR_DYNIFTL)
		kt = rpool->addr.p.dyn->pfid_kt;
	else
		return (-1);
	kt = pfr_ktable_select_active(kt);
	if (kt == NULL)
		return (-1);

	counter = &rpool->counter;
	idx = rpool->tblidx;
	if (idx < 0 || idx >= kt->pfrkt_cnt)
		idx = 0;
	else
		use_counter = 1;
	startidx = idx;

 _next_block:
	if (loop && startidx == idx) {
		kt->pfrkt_nomatch++;
		return (1);
	}

	ke = pfr_kentry_byidx(kt, idx, af);
	if (ke == NULL) {
		/* we don't have this idx, try looping */
		if (loop || (ke = pfr_kentry_byidx(kt, 0, af)) == NULL) {
			kt->pfrkt_nomatch++;
			return (1);
		}
		idx = 0;
		loop++;
	}

	/* Get current weight for weighted round-robin */
	if (idx == 0 && use_counter == 1 && kt->pfrkt_refcntcost > 0) {
		rpool->curweight = rpool->curweight - kt->pfrkt_gcdweight;

		if (rpool->curweight < 1)
			rpool->curweight = kt->pfrkt_maxweight;
	}

	pfr_prepare_network(&pfr_mask, af, ke->pfrke_net);
	*raddr = SUNION2PF(&ke->pfrke_sa, af);
	*rmask = SUNION2PF(&pfr_mask, af);

	if (use_counter && !PF_AZERO(counter, af)) {
		/* is supplied address within block? */
		if (!pf_match_addr(0, *raddr, *rmask, counter, af)) {
			/* no, go to next block in table */
			idx++;
			use_counter = 0;
			goto _next_block;
		}
		pf_addrcpy(addr, counter, af);
	} else {
		/* use first address of block */
		pf_addrcpy(addr, *raddr, af);
	}

	if (!KENTRY_NETWORK(ke)) {
		/* this is a single IP address - no possible nested block */
		if (rpool->addr.type == PF_ADDR_DYNIFTL &&
		    pfr_islinklocal(af, addr)) {
			idx++;
			goto _next_block;
		}
		pf_addrcpy(counter, addr, af);
		rpool->tblidx = idx;
		kt->pfrkt_match++;
		rpool->states = 0;
		if (ke->pfrke_counters != NULL)
			rpool->states = ke->pfrke_counters->states;
		switch (ke->pfrke_type) {
		case PFRKE_COST:
			rpool->weight = ((struct pfr_kentry_cost *)ke)->weight;
			/* FALLTHROUGH */
		case PFRKE_ROUTE:
			rpool->kif = ((struct pfr_kentry_route *)ke)->kif;
			break;
		default:
			rpool->weight = 1;
			break;
		}
		return (0);
	}
	for (;;) {
		/* we don't want to use a nested block */
		switch (af) {
		case AF_INET:
			ke2 = (struct pfr_kentry *)rn_match(&tmp4,
			    kt->pfrkt_ip4);
			break;
#ifdef	INET6
		case AF_INET6:
			ke2 = (struct pfr_kentry *)rn_match(&tmp6,
			    kt->pfrkt_ip6);
			break;
#endif	/* INET6 */
		default:
			unhandled_af(af);
		}
		if (ke2 == ke) {
			/* lookup return the same block - perfect */
			if (rpool->addr.type == PF_ADDR_DYNIFTL &&
			    pfr_islinklocal(af, addr))
				goto _next_entry;
			pf_addrcpy(counter, addr, af);
			rpool->tblidx = idx;
			kt->pfrkt_match++;
			rpool->states = 0;
			if (ke->pfrke_counters != NULL)
				rpool->states = ke->pfrke_counters->states;
			switch (ke->pfrke_type) {
			case PFRKE_COST:
				rpool->weight =
				    ((struct pfr_kentry_cost *)ke)->weight;
				/* FALLTHROUGH */
			case PFRKE_ROUTE:
				rpool->kif = ((struct pfr_kentry_route *)ke)->kif;
				break;
			default:
				rpool->weight = 1;
				break;
			}
			return (0);
		}
_next_entry:
		/* we need to increase the counter past the nested block */
		pfr_prepare_network(&mask, AF_INET, ke2->pfrke_net);
		pf_poolmask(addr, addr, SUNION2PF(&mask, af), &pfr_ffaddr, af);
		pf_addr_inc(addr, af);
		if (!pf_match_addr(0, *raddr, *rmask, addr, af)) {
			/* ok, we reached the end of our main block */
			/* go to next block in table */
			idx++;
			use_counter = 0;
			goto _next_block;
		}
	}
}

struct pfr_kentry *
pfr_kentry_byidx(struct pfr_ktable *kt, int idx, int af)
{
	struct pfr_walktree	w;

	bzero(&w, sizeof(w));
	w.pfrw_op = PFRW_POOL_GET;
	w.pfrw_cnt = idx;

	switch (af) {
	case AF_INET:
		rn_walktree(kt->pfrkt_ip4, pfr_walktree, &w);
		return (w.pfrw_kentry);
#ifdef INET6
	case AF_INET6:
		rn_walktree(kt->pfrkt_ip6, pfr_walktree, &w);
		return (w.pfrw_kentry);
#endif /* INET6 */
	default:
		return (NULL);
	}
}

/* Added for load balancing state counter use. */
int
pfr_states_increase(struct pfr_ktable *kt, struct pf_addr *addr, int af)
{
	struct pfr_kentry *ke;

	ke = pfr_kentry_byaddr(kt, addr, af, 1);
	if (ke == NULL)
		return (-1);

	if (ke->pfrke_counters == NULL)
		ke->pfrke_counters = pool_get(&pfr_kcounters_pl,
		    PR_NOWAIT | PR_ZERO);
	if (ke->pfrke_counters == NULL)
		return (-1);

	ke->pfrke_counters->states++;
	return ke->pfrke_counters->states;
}

/* Added for load balancing state counter use. */
int
pfr_states_decrease(struct pfr_ktable *kt, struct pf_addr *addr, int af)
{
	struct pfr_kentry *ke;

	ke = pfr_kentry_byaddr(kt, addr, af, 1);
	if (ke == NULL)
		return (-1);

	if (ke->pfrke_counters == NULL)
		ke->pfrke_counters = pool_get(&pfr_kcounters_pl,
		    PR_NOWAIT | PR_ZERO);
	if (ke->pfrke_counters == NULL)
		return (-1);

	if (ke->pfrke_counters->states > 0)
		ke->pfrke_counters->states--;
	else
		DPFPRINTF(LOG_DEBUG,
		    "pfr_states_decrease: states-- when states <= 0");

	return ke->pfrke_counters->states;
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
	switch (dyn->pfid_af) {
	case AF_UNSPEC:	/* look up all both addresses IPv4 + IPv6 */
		rn_walktree(kt->pfrkt_ip4, pfr_walktree, &w);
		rn_walktree(kt->pfrkt_ip6, pfr_walktree, &w);
		break;
	case AF_INET:
		rn_walktree(kt->pfrkt_ip4, pfr_walktree, &w);
		break;
#ifdef	INET6
	case AF_INET6:
		rn_walktree(kt->pfrkt_ip6, pfr_walktree, &w);
		break;
#endif	/* INET6 */
	default:
		unhandled_af(dyn->pfid_af);
	}
}

void
pfr_ktable_winfo_update(struct pfr_ktable *kt, struct pfr_kentry *p) {
	/*
	 * If cost flag is set,
	 * gcdweight is needed for round-robin.
	 */
	if (kt->pfrkt_refcntcost > 0) {
		u_int16_t weight;

		weight = (p->pfrke_type == PFRKE_COST) ?
		    ((struct pfr_kentry_cost *)p)->weight : 1;

		if (kt->pfrkt_gcdweight == 0)
			kt->pfrkt_gcdweight = weight;

		kt->pfrkt_gcdweight =
			pfr_gcd(weight, kt->pfrkt_gcdweight);

		if (kt->pfrkt_maxweight < weight)
			kt->pfrkt_maxweight = weight;
	}
}

struct pfr_ktable *
pfr_ktable_select_active(struct pfr_ktable *kt)
{
	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE) && kt->pfrkt_root != NULL)
		kt = kt->pfrkt_root;
	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE))
		return (NULL);

	return (kt);
}
