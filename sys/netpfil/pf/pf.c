/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2008 Henning Brauer
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
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 *	$OpenBSD: pf.c,v 1.634 2009/02/27 12:37:45 henning Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_bpf.h"
#include "opt_pf.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/hash.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/limits.h>
#include <sys/mbuf.h>
#include <sys/md5.h>
#include <sys/random.h>
#include <sys/refcount.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>
#include <sys/ucred.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>
#include <net/route.h>
#include <net/radix_mpath.h>
#include <net/vnet.h>

#include <net/pfil.h>
#include <net/pfvar.h>
#include <net/if_pflog.h>
#include <net/if_pfsync.h>

#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/in_fib.h>
#include <netinet/ip.h>
#include <netinet/ip_fw.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/nd6.h>
#include <netinet6/ip6_var.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/in6_fib.h>
#include <netinet6/scope6_var.h>
#endif /* INET6 */

#include <machine/in_cksum.h>
#include <security/mac/mac_framework.h>

#define	DPFPRINTF(n, x)	if (V_pf_status.debug >= (n)) printf x

/*
 * Global variables
 */

/* state tables */
VNET_DEFINE(struct pf_altqqueue,	 pf_altqs[4]);
VNET_DEFINE(struct pf_palist,		 pf_pabuf);
VNET_DEFINE(struct pf_altqqueue *,	 pf_altqs_active);
VNET_DEFINE(struct pf_altqqueue *,	 pf_altq_ifs_active);
VNET_DEFINE(struct pf_altqqueue *,	 pf_altqs_inactive);
VNET_DEFINE(struct pf_altqqueue *,	 pf_altq_ifs_inactive);
VNET_DEFINE(struct pf_kstatus,		 pf_status);

VNET_DEFINE(u_int32_t,			 ticket_altqs_active);
VNET_DEFINE(u_int32_t,			 ticket_altqs_inactive);
VNET_DEFINE(int,			 altqs_inactive_open);
VNET_DEFINE(u_int32_t,			 ticket_pabuf);

VNET_DEFINE(MD5_CTX,			 pf_tcp_secret_ctx);
#define	V_pf_tcp_secret_ctx		 VNET(pf_tcp_secret_ctx)
VNET_DEFINE(u_char,			 pf_tcp_secret[16]);
#define	V_pf_tcp_secret			 VNET(pf_tcp_secret)
VNET_DEFINE(int,			 pf_tcp_secret_init);
#define	V_pf_tcp_secret_init		 VNET(pf_tcp_secret_init)
VNET_DEFINE(int,			 pf_tcp_iss_off);
#define	V_pf_tcp_iss_off		 VNET(pf_tcp_iss_off)
VNET_DECLARE(int,			 pf_vnet_active);
#define	V_pf_vnet_active		 VNET(pf_vnet_active)

VNET_DEFINE_STATIC(uint32_t, pf_purge_idx);
#define V_pf_purge_idx	VNET(pf_purge_idx)

/*
 * Queue for pf_intr() sends.
 */
static MALLOC_DEFINE(M_PFTEMP, "pf_temp", "pf(4) temporary allocations");
struct pf_send_entry {
	STAILQ_ENTRY(pf_send_entry)	pfse_next;
	struct mbuf			*pfse_m;
	enum {
		PFSE_IP,
		PFSE_IP6,
		PFSE_ICMP,
		PFSE_ICMP6,
	}				pfse_type;
	struct {
		int		type;
		int		code;
		int		mtu;
	} icmpopts;
};

STAILQ_HEAD(pf_send_head, pf_send_entry);
VNET_DEFINE_STATIC(struct pf_send_head, pf_sendqueue);
#define	V_pf_sendqueue	VNET(pf_sendqueue)

static struct mtx pf_sendqueue_mtx;
MTX_SYSINIT(pf_sendqueue_mtx, &pf_sendqueue_mtx, "pf send queue", MTX_DEF);
#define	PF_SENDQ_LOCK()		mtx_lock(&pf_sendqueue_mtx)
#define	PF_SENDQ_UNLOCK()	mtx_unlock(&pf_sendqueue_mtx)

/*
 * Queue for pf_overload_task() tasks.
 */
struct pf_overload_entry {
	SLIST_ENTRY(pf_overload_entry)	next;
	struct pf_addr  		addr;
	sa_family_t			af;
	uint8_t				dir;
	struct pf_rule  		*rule;
};

SLIST_HEAD(pf_overload_head, pf_overload_entry);
VNET_DEFINE_STATIC(struct pf_overload_head, pf_overloadqueue);
#define V_pf_overloadqueue	VNET(pf_overloadqueue)
VNET_DEFINE_STATIC(struct task, pf_overloadtask);
#define	V_pf_overloadtask	VNET(pf_overloadtask)

static struct mtx pf_overloadqueue_mtx;
MTX_SYSINIT(pf_overloadqueue_mtx, &pf_overloadqueue_mtx,
    "pf overload/flush queue", MTX_DEF);
#define	PF_OVERLOADQ_LOCK()	mtx_lock(&pf_overloadqueue_mtx)
#define	PF_OVERLOADQ_UNLOCK()	mtx_unlock(&pf_overloadqueue_mtx)

VNET_DEFINE(struct pf_rulequeue, pf_unlinked_rules);
struct mtx pf_unlnkdrules_mtx;
MTX_SYSINIT(pf_unlnkdrules_mtx, &pf_unlnkdrules_mtx, "pf unlinked rules",
    MTX_DEF);

VNET_DEFINE_STATIC(uma_zone_t,	pf_sources_z);
#define	V_pf_sources_z	VNET(pf_sources_z)
uma_zone_t		pf_mtag_z;
VNET_DEFINE(uma_zone_t,	 pf_state_z);
VNET_DEFINE(uma_zone_t,	 pf_state_key_z);

VNET_DEFINE(uint64_t, pf_stateid[MAXCPU]);
#define	PFID_CPUBITS	8
#define	PFID_CPUSHIFT	(sizeof(uint64_t) * NBBY - PFID_CPUBITS)
#define	PFID_CPUMASK	((uint64_t)((1 << PFID_CPUBITS) - 1) <<	PFID_CPUSHIFT)
#define	PFID_MAXID	(~PFID_CPUMASK)
CTASSERT((1 << PFID_CPUBITS) >= MAXCPU);

static void		 pf_src_tree_remove_state(struct pf_state *);
static void		 pf_init_threshold(struct pf_threshold *, u_int32_t,
			    u_int32_t);
static void		 pf_add_threshold(struct pf_threshold *);
static int		 pf_check_threshold(struct pf_threshold *);

static void		 pf_change_ap(struct mbuf *, struct pf_addr *, u_int16_t *,
			    u_int16_t *, u_int16_t *, struct pf_addr *,
			    u_int16_t, u_int8_t, sa_family_t);
static int		 pf_modulate_sack(struct mbuf *, int, struct pf_pdesc *,
			    struct tcphdr *, struct pf_state_peer *);
static void		 pf_change_icmp(struct pf_addr *, u_int16_t *,
			    struct pf_addr *, struct pf_addr *, u_int16_t,
			    u_int16_t *, u_int16_t *, u_int16_t *,
			    u_int16_t *, u_int8_t, sa_family_t);
static void		 pf_send_tcp(struct mbuf *,
			    const struct pf_rule *, sa_family_t,
			    const struct pf_addr *, const struct pf_addr *,
			    u_int16_t, u_int16_t, u_int32_t, u_int32_t,
			    u_int8_t, u_int16_t, u_int16_t, u_int8_t, int,
			    u_int16_t, struct ifnet *);
static void		 pf_send_icmp(struct mbuf *, u_int8_t, u_int8_t,
			    sa_family_t, struct pf_rule *);
static void		 pf_detach_state(struct pf_state *);
static int		 pf_state_key_attach(struct pf_state_key *,
			    struct pf_state_key *, struct pf_state *);
static void		 pf_state_key_detach(struct pf_state *, int);
static int		 pf_state_key_ctor(void *, int, void *, int);
static u_int32_t	 pf_tcp_iss(struct pf_pdesc *);
static int		 pf_test_rule(struct pf_rule **, struct pf_state **,
			    int, struct pfi_kif *, struct mbuf *, int,
			    struct pf_pdesc *, struct pf_rule **,
			    struct pf_ruleset **, struct inpcb *);
static int		 pf_create_state(struct pf_rule *, struct pf_rule *,
			    struct pf_rule *, struct pf_pdesc *,
			    struct pf_src_node *, struct pf_state_key *,
			    struct pf_state_key *, struct mbuf *, int,
			    u_int16_t, u_int16_t, int *, struct pfi_kif *,
			    struct pf_state **, int, u_int16_t, u_int16_t,
			    int);
static int		 pf_test_fragment(struct pf_rule **, int,
			    struct pfi_kif *, struct mbuf *, void *,
			    struct pf_pdesc *, struct pf_rule **,
			    struct pf_ruleset **);
static int		 pf_tcp_track_full(struct pf_state_peer *,
			    struct pf_state_peer *, struct pf_state **,
			    struct pfi_kif *, struct mbuf *, int,
			    struct pf_pdesc *, u_short *, int *);
static int		 pf_tcp_track_sloppy(struct pf_state_peer *,
			    struct pf_state_peer *, struct pf_state **,
			    struct pf_pdesc *, u_short *);
static int		 pf_test_state_tcp(struct pf_state **, int,
			    struct pfi_kif *, struct mbuf *, int,
			    void *, struct pf_pdesc *, u_short *);
static int		 pf_test_state_udp(struct pf_state **, int,
			    struct pfi_kif *, struct mbuf *, int,
			    void *, struct pf_pdesc *);
static int		 pf_test_state_icmp(struct pf_state **, int,
			    struct pfi_kif *, struct mbuf *, int,
			    void *, struct pf_pdesc *, u_short *);
static int		 pf_test_state_other(struct pf_state **, int,
			    struct pfi_kif *, struct mbuf *, struct pf_pdesc *);
static u_int8_t		 pf_get_wscale(struct mbuf *, int, u_int16_t,
			    sa_family_t);
static u_int16_t	 pf_get_mss(struct mbuf *, int, u_int16_t,
			    sa_family_t);
static u_int16_t	 pf_calc_mss(struct pf_addr *, sa_family_t,
				int, u_int16_t);
static int		 pf_check_proto_cksum(struct mbuf *, int, int,
			    u_int8_t, sa_family_t);
static void		 pf_print_state_parts(struct pf_state *,
			    struct pf_state_key *, struct pf_state_key *);
static int		 pf_addr_wrap_neq(struct pf_addr_wrap *,
			    struct pf_addr_wrap *);
static struct pf_state	*pf_find_state(struct pfi_kif *,
			    struct pf_state_key_cmp *, u_int);
static int		 pf_src_connlimit(struct pf_state **);
static void		 pf_overload_task(void *v, int pending);
static int		 pf_insert_src_node(struct pf_src_node **,
			    struct pf_rule *, struct pf_addr *, sa_family_t);
static u_int		 pf_purge_expired_states(u_int, int);
static void		 pf_purge_unlinked_rules(void);
static int		 pf_mtag_uminit(void *, int, int);
static void		 pf_mtag_free(struct m_tag *);
#ifdef INET
static void		 pf_route(struct mbuf **, struct pf_rule *, int,
			    struct ifnet *, struct pf_state *,
			    struct pf_pdesc *, struct inpcb *);
#endif /* INET */
#ifdef INET6
static void		 pf_change_a6(struct pf_addr *, u_int16_t *,
			    struct pf_addr *, u_int8_t);
static void		 pf_route6(struct mbuf **, struct pf_rule *, int,
			    struct ifnet *, struct pf_state *,
			    struct pf_pdesc *, struct inpcb *);
#endif /* INET6 */

int in4_cksum(struct mbuf *m, u_int8_t nxt, int off, int len);

extern int pf_end_threads;
extern struct proc *pf_purge_proc;

VNET_DEFINE(struct pf_limit, pf_limits[PF_LIMIT_MAX]);

#define	PACKET_LOOPED(pd)	((pd)->pf_mtag &&			\
				 (pd)->pf_mtag->flags & PF_PACKET_LOOPED)

#define	STATE_LOOKUP(i, k, d, s, pd)					\
	do {								\
		(s) = pf_find_state((i), (k), (d));			\
		if ((s) == NULL)					\
			return (PF_DROP);				\
		if (PACKET_LOOPED(pd))					\
			return (PF_PASS);				\
		if ((d) == PF_OUT &&					\
		    (((s)->rule.ptr->rt == PF_ROUTETO &&		\
		    (s)->rule.ptr->direction == PF_OUT) ||		\
		    ((s)->rule.ptr->rt == PF_REPLYTO &&			\
		    (s)->rule.ptr->direction == PF_IN)) &&		\
		    (s)->rt_kif != NULL &&				\
		    (s)->rt_kif != (i))					\
			return (PF_PASS);				\
	} while (0)

#define	BOUND_IFACE(r, k) \
	((r)->rule_flag & PFRULE_IFBOUND) ? (k) : V_pfi_all

#define	STATE_INC_COUNTERS(s)						\
	do {								\
		counter_u64_add(s->rule.ptr->states_cur, 1);		\
		counter_u64_add(s->rule.ptr->states_tot, 1);		\
		if (s->anchor.ptr != NULL) {				\
			counter_u64_add(s->anchor.ptr->states_cur, 1);	\
			counter_u64_add(s->anchor.ptr->states_tot, 1);	\
		}							\
		if (s->nat_rule.ptr != NULL) {				\
			counter_u64_add(s->nat_rule.ptr->states_cur, 1);\
			counter_u64_add(s->nat_rule.ptr->states_tot, 1);\
		}							\
	} while (0)

#define	STATE_DEC_COUNTERS(s)						\
	do {								\
		if (s->nat_rule.ptr != NULL)				\
			counter_u64_add(s->nat_rule.ptr->states_cur, -1);\
		if (s->anchor.ptr != NULL)				\
			counter_u64_add(s->anchor.ptr->states_cur, -1);	\
		counter_u64_add(s->rule.ptr->states_cur, -1);		\
	} while (0)

MALLOC_DEFINE(M_PFHASH, "pf_hash", "pf(4) hash header structures");
VNET_DEFINE(struct pf_keyhash *, pf_keyhash);
VNET_DEFINE(struct pf_idhash *, pf_idhash);
VNET_DEFINE(struct pf_srchash *, pf_srchash);

SYSCTL_NODE(_net, OID_AUTO, pf, CTLFLAG_RW, 0, "pf(4)");

u_long	pf_hashmask;
u_long	pf_srchashmask;
static u_long	pf_hashsize;
static u_long	pf_srchashsize;
u_long	pf_ioctl_maxcount = 65535;

SYSCTL_ULONG(_net_pf, OID_AUTO, states_hashsize, CTLFLAG_RDTUN,
    &pf_hashsize, 0, "Size of pf(4) states hashtable");
SYSCTL_ULONG(_net_pf, OID_AUTO, source_nodes_hashsize, CTLFLAG_RDTUN,
    &pf_srchashsize, 0, "Size of pf(4) source nodes hashtable");
SYSCTL_ULONG(_net_pf, OID_AUTO, request_maxcount, CTLFLAG_RDTUN,
    &pf_ioctl_maxcount, 0, "Maximum number of tables, addresses, ... in a single ioctl() call");

VNET_DEFINE(void *, pf_swi_cookie);

VNET_DEFINE(uint32_t, pf_hashseed);
#define	V_pf_hashseed	VNET(pf_hashseed)

int
pf_addr_cmp(struct pf_addr *a, struct pf_addr *b, sa_family_t af)
{

	switch (af) {
#ifdef INET
	case AF_INET:
		if (a->addr32[0] > b->addr32[0])
			return (1);
		if (a->addr32[0] < b->addr32[0])
			return (-1);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		if (a->addr32[3] > b->addr32[3])
			return (1);
		if (a->addr32[3] < b->addr32[3])
			return (-1);
		if (a->addr32[2] > b->addr32[2])
			return (1);
		if (a->addr32[2] < b->addr32[2])
			return (-1);
		if (a->addr32[1] > b->addr32[1])
			return (1);
		if (a->addr32[1] < b->addr32[1])
			return (-1);
		if (a->addr32[0] > b->addr32[0])
			return (1);
		if (a->addr32[0] < b->addr32[0])
			return (-1);
		break;
#endif /* INET6 */
	default:
		panic("%s: unknown address family %u", __func__, af);
	}
	return (0);
}

static __inline uint32_t
pf_hashkey(struct pf_state_key *sk)
{
	uint32_t h;

	h = murmur3_32_hash32((uint32_t *)sk,
	    sizeof(struct pf_state_key_cmp)/sizeof(uint32_t),
	    V_pf_hashseed);

	return (h & pf_hashmask);
}

static __inline uint32_t
pf_hashsrc(struct pf_addr *addr, sa_family_t af)
{
	uint32_t h;

	switch (af) {
	case AF_INET:
		h = murmur3_32_hash32((uint32_t *)&addr->v4,
		    sizeof(addr->v4)/sizeof(uint32_t), V_pf_hashseed);
		break;
	case AF_INET6:
		h = murmur3_32_hash32((uint32_t *)&addr->v6,
		    sizeof(addr->v6)/sizeof(uint32_t), V_pf_hashseed);
		break;
	default:
		panic("%s: unknown address family %u", __func__, af);
	}

	return (h & pf_srchashmask);
}

#ifdef ALTQ
static int
pf_state_hash(struct pf_state *s)
{
	u_int32_t hv = (intptr_t)s / sizeof(*s);

	hv ^= crc32(&s->src, sizeof(s->src));
	hv ^= crc32(&s->dst, sizeof(s->dst));
	if (hv == 0)
		hv = 1;
	return (hv);
}
#endif

#ifdef INET6
void
pf_addrcpy(struct pf_addr *dst, struct pf_addr *src, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		dst->addr32[0] = src->addr32[0];
		break;
#endif /* INET */
	case AF_INET6:
		dst->addr32[0] = src->addr32[0];
		dst->addr32[1] = src->addr32[1];
		dst->addr32[2] = src->addr32[2];
		dst->addr32[3] = src->addr32[3];
		break;
	}
}
#endif /* INET6 */

static void
pf_init_threshold(struct pf_threshold *threshold,
    u_int32_t limit, u_int32_t seconds)
{
	threshold->limit = limit * PF_THRESHOLD_MULT;
	threshold->seconds = seconds;
	threshold->count = 0;
	threshold->last = time_uptime;
}

static void
pf_add_threshold(struct pf_threshold *threshold)
{
	u_int32_t t = time_uptime, diff = t - threshold->last;

	if (diff >= threshold->seconds)
		threshold->count = 0;
	else
		threshold->count -= threshold->count * diff /
		    threshold->seconds;
	threshold->count += PF_THRESHOLD_MULT;
	threshold->last = t;
}

static int
pf_check_threshold(struct pf_threshold *threshold)
{
	return (threshold->count > threshold->limit);
}

static int
pf_src_connlimit(struct pf_state **state)
{
	struct pf_overload_entry *pfoe;
	int bad = 0;

	PF_STATE_LOCK_ASSERT(*state);

	(*state)->src_node->conn++;
	(*state)->src.tcp_est = 1;
	pf_add_threshold(&(*state)->src_node->conn_rate);

	if ((*state)->rule.ptr->max_src_conn &&
	    (*state)->rule.ptr->max_src_conn <
	    (*state)->src_node->conn) {
		counter_u64_add(V_pf_status.lcounters[LCNT_SRCCONN], 1);
		bad++;
	}

	if ((*state)->rule.ptr->max_src_conn_rate.limit &&
	    pf_check_threshold(&(*state)->src_node->conn_rate)) {
		counter_u64_add(V_pf_status.lcounters[LCNT_SRCCONNRATE], 1);
		bad++;
	}

	if (!bad)
		return (0);

	/* Kill this state. */
	(*state)->timeout = PFTM_PURGE;
	(*state)->src.state = (*state)->dst.state = TCPS_CLOSED;

	if ((*state)->rule.ptr->overload_tbl == NULL)
		return (1);

	/* Schedule overloading and flushing task. */
	pfoe = malloc(sizeof(*pfoe), M_PFTEMP, M_NOWAIT);
	if (pfoe == NULL)
		return (1);	/* too bad :( */

	bcopy(&(*state)->src_node->addr, &pfoe->addr, sizeof(pfoe->addr));
	pfoe->af = (*state)->key[PF_SK_WIRE]->af;
	pfoe->rule = (*state)->rule.ptr;
	pfoe->dir = (*state)->direction;
	PF_OVERLOADQ_LOCK();
	SLIST_INSERT_HEAD(&V_pf_overloadqueue, pfoe, next);
	PF_OVERLOADQ_UNLOCK();
	taskqueue_enqueue(taskqueue_swi, &V_pf_overloadtask);

	return (1);
}

static void
pf_overload_task(void *v, int pending)
{
	struct pf_overload_head queue;
	struct pfr_addr p;
	struct pf_overload_entry *pfoe, *pfoe1;
	uint32_t killed = 0;

	CURVNET_SET((struct vnet *)v);

	PF_OVERLOADQ_LOCK();
	queue = V_pf_overloadqueue;
	SLIST_INIT(&V_pf_overloadqueue);
	PF_OVERLOADQ_UNLOCK();

	bzero(&p, sizeof(p));
	SLIST_FOREACH(pfoe, &queue, next) {
		counter_u64_add(V_pf_status.lcounters[LCNT_OVERLOAD_TABLE], 1);
		if (V_pf_status.debug >= PF_DEBUG_MISC) {
			printf("%s: blocking address ", __func__);
			pf_print_host(&pfoe->addr, 0, pfoe->af);
			printf("\n");
		}

		p.pfra_af = pfoe->af;
		switch (pfoe->af) {
#ifdef INET
		case AF_INET:
			p.pfra_net = 32;
			p.pfra_ip4addr = pfoe->addr.v4;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			p.pfra_net = 128;
			p.pfra_ip6addr = pfoe->addr.v6;
			break;
#endif
		}

		PF_RULES_WLOCK();
		pfr_insert_kentry(pfoe->rule->overload_tbl, &p, time_second);
		PF_RULES_WUNLOCK();
	}

	/*
	 * Remove those entries, that don't need flushing.
	 */
	SLIST_FOREACH_SAFE(pfoe, &queue, next, pfoe1)
		if (pfoe->rule->flush == 0) {
			SLIST_REMOVE(&queue, pfoe, pf_overload_entry, next);
			free(pfoe, M_PFTEMP);
		} else
			counter_u64_add(
			    V_pf_status.lcounters[LCNT_OVERLOAD_FLUSH], 1);

	/* If nothing to flush, return. */
	if (SLIST_EMPTY(&queue)) {
		CURVNET_RESTORE();
		return;
	}

	for (int i = 0; i <= pf_hashmask; i++) {
		struct pf_idhash *ih = &V_pf_idhash[i];
		struct pf_state_key *sk;
		struct pf_state *s;

		PF_HASHROW_LOCK(ih);
		LIST_FOREACH(s, &ih->states, entry) {
		    sk = s->key[PF_SK_WIRE];
		    SLIST_FOREACH(pfoe, &queue, next)
			if (sk->af == pfoe->af &&
			    ((pfoe->rule->flush & PF_FLUSH_GLOBAL) ||
			    pfoe->rule == s->rule.ptr) &&
			    ((pfoe->dir == PF_OUT &&
			    PF_AEQ(&pfoe->addr, &sk->addr[1], sk->af)) ||
			    (pfoe->dir == PF_IN &&
			    PF_AEQ(&pfoe->addr, &sk->addr[0], sk->af)))) {
				s->timeout = PFTM_PURGE;
				s->src.state = s->dst.state = TCPS_CLOSED;
				killed++;
			}
		}
		PF_HASHROW_UNLOCK(ih);
	}
	SLIST_FOREACH_SAFE(pfoe, &queue, next, pfoe1)
		free(pfoe, M_PFTEMP);
	if (V_pf_status.debug >= PF_DEBUG_MISC)
		printf("%s: %u states killed", __func__, killed);

	CURVNET_RESTORE();
}

/*
 * Can return locked on failure, so that we can consistently
 * allocate and insert a new one.
 */
struct pf_src_node *
pf_find_src_node(struct pf_addr *src, struct pf_rule *rule, sa_family_t af,
	int returnlocked)
{
	struct pf_srchash *sh;
	struct pf_src_node *n;

	counter_u64_add(V_pf_status.scounters[SCNT_SRC_NODE_SEARCH], 1);

	sh = &V_pf_srchash[pf_hashsrc(src, af)];
	PF_HASHROW_LOCK(sh);
	LIST_FOREACH(n, &sh->nodes, entry)
		if (n->rule.ptr == rule && n->af == af &&
		    ((af == AF_INET && n->addr.v4.s_addr == src->v4.s_addr) ||
		    (af == AF_INET6 && bcmp(&n->addr, src, sizeof(*src)) == 0)))
			break;
	if (n != NULL) {
		n->states++;
		PF_HASHROW_UNLOCK(sh);
	} else if (returnlocked == 0)
		PF_HASHROW_UNLOCK(sh);

	return (n);
}

static int
pf_insert_src_node(struct pf_src_node **sn, struct pf_rule *rule,
    struct pf_addr *src, sa_family_t af)
{

	KASSERT((rule->rule_flag & PFRULE_RULESRCTRACK ||
	    rule->rpool.opts & PF_POOL_STICKYADDR),
	    ("%s for non-tracking rule %p", __func__, rule));

	if (*sn == NULL)
		*sn = pf_find_src_node(src, rule, af, 1);

	if (*sn == NULL) {
		struct pf_srchash *sh = &V_pf_srchash[pf_hashsrc(src, af)];

		PF_HASHROW_ASSERT(sh);

		if (!rule->max_src_nodes ||
		    counter_u64_fetch(rule->src_nodes) < rule->max_src_nodes)
			(*sn) = uma_zalloc(V_pf_sources_z, M_NOWAIT | M_ZERO);
		else
			counter_u64_add(V_pf_status.lcounters[LCNT_SRCNODES],
			    1);
		if ((*sn) == NULL) {
			PF_HASHROW_UNLOCK(sh);
			return (-1);
		}

		pf_init_threshold(&(*sn)->conn_rate,
		    rule->max_src_conn_rate.limit,
		    rule->max_src_conn_rate.seconds);

		(*sn)->af = af;
		(*sn)->rule.ptr = rule;
		PF_ACPY(&(*sn)->addr, src, af);
		LIST_INSERT_HEAD(&sh->nodes, *sn, entry);
		(*sn)->creation = time_uptime;
		(*sn)->ruletype = rule->action;
		(*sn)->states = 1;
		if ((*sn)->rule.ptr != NULL)
			counter_u64_add((*sn)->rule.ptr->src_nodes, 1);
		PF_HASHROW_UNLOCK(sh);
		counter_u64_add(V_pf_status.scounters[SCNT_SRC_NODE_INSERT], 1);
	} else {
		if (rule->max_src_states &&
		    (*sn)->states >= rule->max_src_states) {
			counter_u64_add(V_pf_status.lcounters[LCNT_SRCSTATES],
			    1);
			return (-1);
		}
	}
	return (0);
}

void
pf_unlink_src_node(struct pf_src_node *src)
{

	PF_HASHROW_ASSERT(&V_pf_srchash[pf_hashsrc(&src->addr, src->af)]);
	LIST_REMOVE(src, entry);
	if (src->rule.ptr)
		counter_u64_add(src->rule.ptr->src_nodes, -1);
}

u_int
pf_free_src_nodes(struct pf_src_node_list *head)
{
	struct pf_src_node *sn, *tmp;
	u_int count = 0;

	LIST_FOREACH_SAFE(sn, head, entry, tmp) {
		uma_zfree(V_pf_sources_z, sn);
		count++;
	}

	counter_u64_add(V_pf_status.scounters[SCNT_SRC_NODE_REMOVALS], count);

	return (count);
}

void
pf_mtag_initialize()
{

	pf_mtag_z = uma_zcreate("pf mtags", sizeof(struct m_tag) +
	    sizeof(struct pf_mtag), NULL, NULL, pf_mtag_uminit, NULL,
	    UMA_ALIGN_PTR, 0);
}

/* Per-vnet data storage structures initialization. */
void
pf_initialize()
{
	struct pf_keyhash	*kh;
	struct pf_idhash	*ih;
	struct pf_srchash	*sh;
	u_int i;

	if (pf_hashsize == 0 || !powerof2(pf_hashsize))
		pf_hashsize = PF_HASHSIZ;
	if (pf_srchashsize == 0 || !powerof2(pf_srchashsize))
		pf_srchashsize = PF_SRCHASHSIZ;

	V_pf_hashseed = arc4random();

	/* States and state keys storage. */
	V_pf_state_z = uma_zcreate("pf states", sizeof(struct pf_state),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	V_pf_limits[PF_LIMIT_STATES].zone = V_pf_state_z;
	uma_zone_set_max(V_pf_state_z, PFSTATE_HIWAT);
	uma_zone_set_warning(V_pf_state_z, "PF states limit reached");

	V_pf_state_key_z = uma_zcreate("pf state keys",
	    sizeof(struct pf_state_key), pf_state_key_ctor, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, 0);

	V_pf_keyhash = mallocarray(pf_hashsize, sizeof(struct pf_keyhash),
	    M_PFHASH, M_NOWAIT | M_ZERO);
	V_pf_idhash = mallocarray(pf_hashsize, sizeof(struct pf_idhash),
	    M_PFHASH, M_NOWAIT | M_ZERO);
	if (V_pf_keyhash == NULL || V_pf_idhash == NULL) {
		printf("pf: Unable to allocate memory for "
		    "state_hashsize %lu.\n", pf_hashsize);

		free(V_pf_keyhash, M_PFHASH);
		free(V_pf_idhash, M_PFHASH);

		pf_hashsize = PF_HASHSIZ;
		V_pf_keyhash = mallocarray(pf_hashsize,
		    sizeof(struct pf_keyhash), M_PFHASH, M_WAITOK | M_ZERO);
		V_pf_idhash = mallocarray(pf_hashsize,
		    sizeof(struct pf_idhash), M_PFHASH, M_WAITOK | M_ZERO);
	}

	pf_hashmask = pf_hashsize - 1;
	for (i = 0, kh = V_pf_keyhash, ih = V_pf_idhash; i <= pf_hashmask;
	    i++, kh++, ih++) {
		mtx_init(&kh->lock, "pf_keyhash", NULL, MTX_DEF | MTX_DUPOK);
		mtx_init(&ih->lock, "pf_idhash", NULL, MTX_DEF);
	}

	/* Source nodes. */
	V_pf_sources_z = uma_zcreate("pf source nodes",
	    sizeof(struct pf_src_node), NULL, NULL, NULL, NULL, UMA_ALIGN_PTR,
	    0);
	V_pf_limits[PF_LIMIT_SRC_NODES].zone = V_pf_sources_z;
	uma_zone_set_max(V_pf_sources_z, PFSNODE_HIWAT);
	uma_zone_set_warning(V_pf_sources_z, "PF source nodes limit reached");

	V_pf_srchash = mallocarray(pf_srchashsize,
	    sizeof(struct pf_srchash), M_PFHASH, M_NOWAIT | M_ZERO);
	if (V_pf_srchash == NULL) {
		printf("pf: Unable to allocate memory for "
		    "source_hashsize %lu.\n", pf_srchashsize);

		pf_srchashsize = PF_SRCHASHSIZ;
		V_pf_srchash = mallocarray(pf_srchashsize,
		    sizeof(struct pf_srchash), M_PFHASH, M_WAITOK | M_ZERO);
	}

	pf_srchashmask = pf_srchashsize - 1;
	for (i = 0, sh = V_pf_srchash; i <= pf_srchashmask; i++, sh++)
		mtx_init(&sh->lock, "pf_srchash", NULL, MTX_DEF);

	/* ALTQ */
	TAILQ_INIT(&V_pf_altqs[0]);
	TAILQ_INIT(&V_pf_altqs[1]);
	TAILQ_INIT(&V_pf_altqs[2]);
	TAILQ_INIT(&V_pf_altqs[3]);
	TAILQ_INIT(&V_pf_pabuf);
	V_pf_altqs_active = &V_pf_altqs[0];
	V_pf_altq_ifs_active = &V_pf_altqs[1];
	V_pf_altqs_inactive = &V_pf_altqs[2];
	V_pf_altq_ifs_inactive = &V_pf_altqs[3];

	/* Send & overload+flush queues. */
	STAILQ_INIT(&V_pf_sendqueue);
	SLIST_INIT(&V_pf_overloadqueue);
	TASK_INIT(&V_pf_overloadtask, 0, pf_overload_task, curvnet);

	/* Unlinked, but may be referenced rules. */
	TAILQ_INIT(&V_pf_unlinked_rules);
}

void
pf_mtag_cleanup()
{

	uma_zdestroy(pf_mtag_z);
}

void
pf_cleanup()
{
	struct pf_keyhash	*kh;
	struct pf_idhash	*ih;
	struct pf_srchash	*sh;
	struct pf_send_entry	*pfse, *next;
	u_int i;

	for (i = 0, kh = V_pf_keyhash, ih = V_pf_idhash; i <= pf_hashmask;
	    i++, kh++, ih++) {
		KASSERT(LIST_EMPTY(&kh->keys), ("%s: key hash not empty",
		    __func__));
		KASSERT(LIST_EMPTY(&ih->states), ("%s: id hash not empty",
		    __func__));
		mtx_destroy(&kh->lock);
		mtx_destroy(&ih->lock);
	}
	free(V_pf_keyhash, M_PFHASH);
	free(V_pf_idhash, M_PFHASH);

	for (i = 0, sh = V_pf_srchash; i <= pf_srchashmask; i++, sh++) {
		KASSERT(LIST_EMPTY(&sh->nodes),
		    ("%s: source node hash not empty", __func__));
		mtx_destroy(&sh->lock);
	}
	free(V_pf_srchash, M_PFHASH);

	STAILQ_FOREACH_SAFE(pfse, &V_pf_sendqueue, pfse_next, next) {
		m_freem(pfse->pfse_m);
		free(pfse, M_PFTEMP);
	}

	uma_zdestroy(V_pf_sources_z);
	uma_zdestroy(V_pf_state_z);
	uma_zdestroy(V_pf_state_key_z);
}

static int
pf_mtag_uminit(void *mem, int size, int how)
{
	struct m_tag *t;

	t = (struct m_tag *)mem;
	t->m_tag_cookie = MTAG_ABI_COMPAT;
	t->m_tag_id = PACKET_TAG_PF;
	t->m_tag_len = sizeof(struct pf_mtag);
	t->m_tag_free = pf_mtag_free;

	return (0);
}

static void
pf_mtag_free(struct m_tag *t)
{

	uma_zfree(pf_mtag_z, t);
}

struct pf_mtag *
pf_get_mtag(struct mbuf *m)
{
	struct m_tag *mtag;

	if ((mtag = m_tag_find(m, PACKET_TAG_PF, NULL)) != NULL)
		return ((struct pf_mtag *)(mtag + 1));

	mtag = uma_zalloc(pf_mtag_z, M_NOWAIT);
	if (mtag == NULL)
		return (NULL);
	bzero(mtag + 1, sizeof(struct pf_mtag));
	m_tag_prepend(m, mtag);

	return ((struct pf_mtag *)(mtag + 1));
}

static int
pf_state_key_attach(struct pf_state_key *skw, struct pf_state_key *sks,
    struct pf_state *s)
{
	struct pf_keyhash	*khs, *khw, *kh;
	struct pf_state_key	*sk, *cur;
	struct pf_state		*si, *olds = NULL;
	int idx;

	KASSERT(s->refs == 0, ("%s: state not pristine", __func__));
	KASSERT(s->key[PF_SK_WIRE] == NULL, ("%s: state has key", __func__));
	KASSERT(s->key[PF_SK_STACK] == NULL, ("%s: state has key", __func__));

	/*
	 * We need to lock hash slots of both keys. To avoid deadlock
	 * we always lock the slot with lower address first. Unlock order
	 * isn't important.
	 *
	 * We also need to lock ID hash slot before dropping key
	 * locks. On success we return with ID hash slot locked.
	 */

	if (skw == sks) {
		khs = khw = &V_pf_keyhash[pf_hashkey(skw)];
		PF_HASHROW_LOCK(khs);
	} else {
		khs = &V_pf_keyhash[pf_hashkey(sks)];
		khw = &V_pf_keyhash[pf_hashkey(skw)];
		if (khs == khw) {
			PF_HASHROW_LOCK(khs);
		} else if (khs < khw) {
			PF_HASHROW_LOCK(khs);
			PF_HASHROW_LOCK(khw);
		} else {
			PF_HASHROW_LOCK(khw);
			PF_HASHROW_LOCK(khs);
		}
	}

#define	KEYS_UNLOCK()	do {			\
	if (khs != khw) {			\
		PF_HASHROW_UNLOCK(khs);		\
		PF_HASHROW_UNLOCK(khw);		\
	} else					\
		PF_HASHROW_UNLOCK(khs);		\
} while (0)

	/*
	 * First run: start with wire key.
	 */
	sk = skw;
	kh = khw;
	idx = PF_SK_WIRE;

keyattach:
	LIST_FOREACH(cur, &kh->keys, entry)
		if (bcmp(cur, sk, sizeof(struct pf_state_key_cmp)) == 0)
			break;

	if (cur != NULL) {
		/* Key exists. Check for same kif, if none, add to key. */
		TAILQ_FOREACH(si, &cur->states[idx], key_list[idx]) {
			struct pf_idhash *ih = &V_pf_idhash[PF_IDHASH(si)];

			PF_HASHROW_LOCK(ih);
			if (si->kif == s->kif &&
			    si->direction == s->direction) {
				if (sk->proto == IPPROTO_TCP &&
				    si->src.state >= TCPS_FIN_WAIT_2 &&
				    si->dst.state >= TCPS_FIN_WAIT_2) {
					/*
					 * New state matches an old >FIN_WAIT_2
					 * state. We can't drop key hash locks,
					 * thus we can't unlink it properly.
					 *
					 * As a workaround we drop it into
					 * TCPS_CLOSED state, schedule purge
					 * ASAP and push it into the very end
					 * of the slot TAILQ, so that it won't
					 * conflict with our new state.
					 */
					si->src.state = si->dst.state =
					    TCPS_CLOSED;
					si->timeout = PFTM_PURGE;
					olds = si;
				} else {
					if (V_pf_status.debug >= PF_DEBUG_MISC) {
						printf("pf: %s key attach "
						    "failed on %s: ",
						    (idx == PF_SK_WIRE) ?
						    "wire" : "stack",
						    s->kif->pfik_name);
						pf_print_state_parts(s,
						    (idx == PF_SK_WIRE) ?
						    sk : NULL,
						    (idx == PF_SK_STACK) ?
						    sk : NULL);
						printf(", existing: ");
						pf_print_state_parts(si,
						    (idx == PF_SK_WIRE) ?
						    sk : NULL,
						    (idx == PF_SK_STACK) ?
						    sk : NULL);
						printf("\n");
					}
					PF_HASHROW_UNLOCK(ih);
					KEYS_UNLOCK();
					uma_zfree(V_pf_state_key_z, sk);
					if (idx == PF_SK_STACK)
						pf_detach_state(s);
					return (EEXIST); /* collision! */
				}
			}
			PF_HASHROW_UNLOCK(ih);
		}
		uma_zfree(V_pf_state_key_z, sk);
		s->key[idx] = cur;
	} else {
		LIST_INSERT_HEAD(&kh->keys, sk, entry);
		s->key[idx] = sk;
	}

stateattach:
	/* List is sorted, if-bound states before floating. */
	if (s->kif == V_pfi_all)
		TAILQ_INSERT_TAIL(&s->key[idx]->states[idx], s, key_list[idx]);
	else
		TAILQ_INSERT_HEAD(&s->key[idx]->states[idx], s, key_list[idx]);

	if (olds) {
		TAILQ_REMOVE(&s->key[idx]->states[idx], olds, key_list[idx]);
		TAILQ_INSERT_TAIL(&s->key[idx]->states[idx], olds,
		    key_list[idx]);
		olds = NULL;
	}

	/*
	 * Attach done. See how should we (or should not?)
	 * attach a second key.
	 */
	if (sks == skw) {
		s->key[PF_SK_STACK] = s->key[PF_SK_WIRE];
		idx = PF_SK_STACK;
		sks = NULL;
		goto stateattach;
	} else if (sks != NULL) {
		/*
		 * Continue attaching with stack key.
		 */
		sk = sks;
		kh = khs;
		idx = PF_SK_STACK;
		sks = NULL;
		goto keyattach;
	}

	PF_STATE_LOCK(s);
	KEYS_UNLOCK();

	KASSERT(s->key[PF_SK_WIRE] != NULL && s->key[PF_SK_STACK] != NULL,
	    ("%s failure", __func__));

	return (0);
#undef	KEYS_UNLOCK
}

static void
pf_detach_state(struct pf_state *s)
{
	struct pf_state_key *sks = s->key[PF_SK_STACK];
	struct pf_keyhash *kh;

	if (sks != NULL) {
		kh = &V_pf_keyhash[pf_hashkey(sks)];
		PF_HASHROW_LOCK(kh);
		if (s->key[PF_SK_STACK] != NULL)
			pf_state_key_detach(s, PF_SK_STACK);
		/*
		 * If both point to same key, then we are done.
		 */
		if (sks == s->key[PF_SK_WIRE]) {
			pf_state_key_detach(s, PF_SK_WIRE);
			PF_HASHROW_UNLOCK(kh);
			return;
		}
		PF_HASHROW_UNLOCK(kh);
	}

	if (s->key[PF_SK_WIRE] != NULL) {
		kh = &V_pf_keyhash[pf_hashkey(s->key[PF_SK_WIRE])];
		PF_HASHROW_LOCK(kh);
		if (s->key[PF_SK_WIRE] != NULL)
			pf_state_key_detach(s, PF_SK_WIRE);
		PF_HASHROW_UNLOCK(kh);
	}
}

static void
pf_state_key_detach(struct pf_state *s, int idx)
{
	struct pf_state_key *sk = s->key[idx];
#ifdef INVARIANTS
	struct pf_keyhash *kh = &V_pf_keyhash[pf_hashkey(sk)];

	PF_HASHROW_ASSERT(kh);
#endif
	TAILQ_REMOVE(&sk->states[idx], s, key_list[idx]);
	s->key[idx] = NULL;

	if (TAILQ_EMPTY(&sk->states[0]) && TAILQ_EMPTY(&sk->states[1])) {
		LIST_REMOVE(sk, entry);
		uma_zfree(V_pf_state_key_z, sk);
	}
}

static int
pf_state_key_ctor(void *mem, int size, void *arg, int flags)
{
	struct pf_state_key *sk = mem;

	bzero(sk, sizeof(struct pf_state_key_cmp));
	TAILQ_INIT(&sk->states[PF_SK_WIRE]);
	TAILQ_INIT(&sk->states[PF_SK_STACK]);

	return (0);
}

struct pf_state_key *
pf_state_key_setup(struct pf_pdesc *pd, struct pf_addr *saddr,
	struct pf_addr *daddr, u_int16_t sport, u_int16_t dport)
{
	struct pf_state_key *sk;

	sk = uma_zalloc(V_pf_state_key_z, M_NOWAIT);
	if (sk == NULL)
		return (NULL);

	PF_ACPY(&sk->addr[pd->sidx], saddr, pd->af);
	PF_ACPY(&sk->addr[pd->didx], daddr, pd->af);
	sk->port[pd->sidx] = sport;
	sk->port[pd->didx] = dport;
	sk->proto = pd->proto;
	sk->af = pd->af;

	return (sk);
}

struct pf_state_key *
pf_state_key_clone(struct pf_state_key *orig)
{
	struct pf_state_key *sk;

	sk = uma_zalloc(V_pf_state_key_z, M_NOWAIT);
	if (sk == NULL)
		return (NULL);

	bcopy(orig, sk, sizeof(struct pf_state_key_cmp));

	return (sk);
}

int
pf_state_insert(struct pfi_kif *kif, struct pf_state_key *skw,
    struct pf_state_key *sks, struct pf_state *s)
{
	struct pf_idhash *ih;
	struct pf_state *cur;
	int error;

	KASSERT(TAILQ_EMPTY(&sks->states[0]) && TAILQ_EMPTY(&sks->states[1]),
	    ("%s: sks not pristine", __func__));
	KASSERT(TAILQ_EMPTY(&skw->states[0]) && TAILQ_EMPTY(&skw->states[1]),
	    ("%s: skw not pristine", __func__));
	KASSERT(s->refs == 0, ("%s: state not pristine", __func__));

	s->kif = kif;

	if (s->id == 0 && s->creatorid == 0) {
		/* XXX: should be atomic, but probability of collision low */
		if ((s->id = V_pf_stateid[curcpu]++) == PFID_MAXID)
			V_pf_stateid[curcpu] = 1;
		s->id |= (uint64_t )curcpu << PFID_CPUSHIFT;
		s->id = htobe64(s->id);
		s->creatorid = V_pf_status.hostid;
	}

	/* Returns with ID locked on success. */
	if ((error = pf_state_key_attach(skw, sks, s)) != 0)
		return (error);

	ih = &V_pf_idhash[PF_IDHASH(s)];
	PF_HASHROW_ASSERT(ih);
	LIST_FOREACH(cur, &ih->states, entry)
		if (cur->id == s->id && cur->creatorid == s->creatorid)
			break;

	if (cur != NULL) {
		PF_HASHROW_UNLOCK(ih);
		if (V_pf_status.debug >= PF_DEBUG_MISC) {
			printf("pf: state ID collision: "
			    "id: %016llx creatorid: %08x\n",
			    (unsigned long long)be64toh(s->id),
			    ntohl(s->creatorid));
		}
		pf_detach_state(s);
		return (EEXIST);
	}
	LIST_INSERT_HEAD(&ih->states, s, entry);
	/* One for keys, one for ID hash. */
	refcount_init(&s->refs, 2);

	counter_u64_add(V_pf_status.fcounters[FCNT_STATE_INSERT], 1);
	if (V_pfsync_insert_state_ptr != NULL)
		V_pfsync_insert_state_ptr(s);

	/* Returns locked. */
	return (0);
}

/*
 * Find state by ID: returns with locked row on success.
 */
struct pf_state *
pf_find_state_byid(uint64_t id, uint32_t creatorid)
{
	struct pf_idhash *ih;
	struct pf_state *s;

	counter_u64_add(V_pf_status.fcounters[FCNT_STATE_SEARCH], 1);

	ih = &V_pf_idhash[(be64toh(id) % (pf_hashmask + 1))];

	PF_HASHROW_LOCK(ih);
	LIST_FOREACH(s, &ih->states, entry)
		if (s->id == id && s->creatorid == creatorid)
			break;

	if (s == NULL)
		PF_HASHROW_UNLOCK(ih);

	return (s);
}

/*
 * Find state by key.
 * Returns with ID hash slot locked on success.
 */
static struct pf_state *
pf_find_state(struct pfi_kif *kif, struct pf_state_key_cmp *key, u_int dir)
{
	struct pf_keyhash	*kh;
	struct pf_state_key	*sk;
	struct pf_state		*s;
	int idx;

	counter_u64_add(V_pf_status.fcounters[FCNT_STATE_SEARCH], 1);

	kh = &V_pf_keyhash[pf_hashkey((struct pf_state_key *)key)];

	PF_HASHROW_LOCK(kh);
	LIST_FOREACH(sk, &kh->keys, entry)
		if (bcmp(sk, key, sizeof(struct pf_state_key_cmp)) == 0)
			break;
	if (sk == NULL) {
		PF_HASHROW_UNLOCK(kh);
		return (NULL);
	}

	idx = (dir == PF_IN ? PF_SK_WIRE : PF_SK_STACK);

	/* List is sorted, if-bound states before floating ones. */
	TAILQ_FOREACH(s, &sk->states[idx], key_list[idx])
		if (s->kif == V_pfi_all || s->kif == kif) {
			PF_STATE_LOCK(s);
			PF_HASHROW_UNLOCK(kh);
			if (s->timeout >= PFTM_MAX) {
				/*
				 * State is either being processed by
				 * pf_unlink_state() in an other thread, or
				 * is scheduled for immediate expiry.
				 */
				PF_STATE_UNLOCK(s);
				return (NULL);
			}
			return (s);
		}
	PF_HASHROW_UNLOCK(kh);

	return (NULL);
}

struct pf_state *
pf_find_state_all(struct pf_state_key_cmp *key, u_int dir, int *more)
{
	struct pf_keyhash	*kh;
	struct pf_state_key	*sk;
	struct pf_state		*s, *ret = NULL;
	int			 idx, inout = 0;

	counter_u64_add(V_pf_status.fcounters[FCNT_STATE_SEARCH], 1);

	kh = &V_pf_keyhash[pf_hashkey((struct pf_state_key *)key)];

	PF_HASHROW_LOCK(kh);
	LIST_FOREACH(sk, &kh->keys, entry)
		if (bcmp(sk, key, sizeof(struct pf_state_key_cmp)) == 0)
			break;
	if (sk == NULL) {
		PF_HASHROW_UNLOCK(kh);
		return (NULL);
	}
	switch (dir) {
	case PF_IN:
		idx = PF_SK_WIRE;
		break;
	case PF_OUT:
		idx = PF_SK_STACK;
		break;
	case PF_INOUT:
		idx = PF_SK_WIRE;
		inout = 1;
		break;
	default:
		panic("%s: dir %u", __func__, dir);
	}
second_run:
	TAILQ_FOREACH(s, &sk->states[idx], key_list[idx]) {
		if (more == NULL) {
			PF_HASHROW_UNLOCK(kh);
			return (s);
		}

		if (ret)
			(*more)++;
		else
			ret = s;
	}
	if (inout == 1) {
		inout = 0;
		idx = PF_SK_STACK;
		goto second_run;
	}
	PF_HASHROW_UNLOCK(kh);

	return (ret);
}

/* END state table stuff */

static void
pf_send(struct pf_send_entry *pfse)
{

	PF_SENDQ_LOCK();
	STAILQ_INSERT_TAIL(&V_pf_sendqueue, pfse, pfse_next);
	PF_SENDQ_UNLOCK();
	swi_sched(V_pf_swi_cookie, 0);
}

void
pf_intr(void *v)
{
	struct pf_send_head queue;
	struct pf_send_entry *pfse, *next;

	CURVNET_SET((struct vnet *)v);

	PF_SENDQ_LOCK();
	queue = V_pf_sendqueue;
	STAILQ_INIT(&V_pf_sendqueue);
	PF_SENDQ_UNLOCK();

	STAILQ_FOREACH_SAFE(pfse, &queue, pfse_next, next) {
		switch (pfse->pfse_type) {
#ifdef INET
		case PFSE_IP:
			ip_output(pfse->pfse_m, NULL, NULL, 0, NULL, NULL);
			break;
		case PFSE_ICMP:
			icmp_error(pfse->pfse_m, pfse->icmpopts.type,
			    pfse->icmpopts.code, 0, pfse->icmpopts.mtu);
			break;
#endif /* INET */
#ifdef INET6
		case PFSE_IP6:
			ip6_output(pfse->pfse_m, NULL, NULL, 0, NULL, NULL,
			    NULL);
			break;
		case PFSE_ICMP6:
			icmp6_error(pfse->pfse_m, pfse->icmpopts.type,
			    pfse->icmpopts.code, pfse->icmpopts.mtu);
			break;
#endif /* INET6 */
		default:
			panic("%s: unknown type", __func__);
		}
		free(pfse, M_PFTEMP);
	}
	CURVNET_RESTORE();
}

void
pf_purge_thread(void *unused __unused)
{
	VNET_ITERATOR_DECL(vnet_iter);

	sx_xlock(&pf_end_lock);
	while (pf_end_threads == 0) {
		sx_sleep(pf_purge_thread, &pf_end_lock, 0, "pftm", hz / 10);

		VNET_LIST_RLOCK();
		VNET_FOREACH(vnet_iter) {
			CURVNET_SET(vnet_iter);


			/* Wait until V_pf_default_rule is initialized. */
			if (V_pf_vnet_active == 0) {
				CURVNET_RESTORE();
				continue;
			}

			/*
			 *  Process 1/interval fraction of the state
			 * table every run.
			 */
			V_pf_purge_idx =
			    pf_purge_expired_states(V_pf_purge_idx, pf_hashmask /
			    (V_pf_default_rule.timeout[PFTM_INTERVAL] * 10));

			/*
			 * Purge other expired types every
			 * PFTM_INTERVAL seconds.
			 */
			if (V_pf_purge_idx == 0) {
				/*
				 * Order is important:
				 * - states and src nodes reference rules
				 * - states and rules reference kifs
				 */
				pf_purge_expired_fragments();
				pf_purge_expired_src_nodes();
				pf_purge_unlinked_rules();
				pfi_kif_purge();
			}
			CURVNET_RESTORE();
		}
		VNET_LIST_RUNLOCK();
	}

	pf_end_threads++;
	sx_xunlock(&pf_end_lock);
	kproc_exit(0);
}

void
pf_unload_vnet_purge(void)
{

	/*
	 * To cleanse up all kifs and rules we need
	 * two runs: first one clears reference flags,
	 * then pf_purge_expired_states() doesn't
	 * raise them, and then second run frees.
	 */
	pf_purge_unlinked_rules();
	pfi_kif_purge();

	/*
	 * Now purge everything.
	 */
	pf_purge_expired_states(0, pf_hashmask);
	pf_purge_fragments(UINT_MAX);
	pf_purge_expired_src_nodes();

	/*
	 * Now all kifs & rules should be unreferenced,
	 * thus should be successfully freed.
	 */
	pf_purge_unlinked_rules();
	pfi_kif_purge();
}


u_int32_t
pf_state_expires(const struct pf_state *state)
{
	u_int32_t	timeout;
	u_int32_t	start;
	u_int32_t	end;
	u_int32_t	states;

	/* handle all PFTM_* > PFTM_MAX here */
	if (state->timeout == PFTM_PURGE)
		return (time_uptime);
	KASSERT(state->timeout != PFTM_UNLINKED,
	    ("pf_state_expires: timeout == PFTM_UNLINKED"));
	KASSERT((state->timeout < PFTM_MAX),
	    ("pf_state_expires: timeout > PFTM_MAX"));
	timeout = state->rule.ptr->timeout[state->timeout];
	if (!timeout)
		timeout = V_pf_default_rule.timeout[state->timeout];
	start = state->rule.ptr->timeout[PFTM_ADAPTIVE_START];
	if (start && state->rule.ptr != &V_pf_default_rule) {
		end = state->rule.ptr->timeout[PFTM_ADAPTIVE_END];
		states = counter_u64_fetch(state->rule.ptr->states_cur);
	} else {
		start = V_pf_default_rule.timeout[PFTM_ADAPTIVE_START];
		end = V_pf_default_rule.timeout[PFTM_ADAPTIVE_END];
		states = V_pf_status.states;
	}
	if (end && states > start && start < end) {
		if (states < end) {
			timeout = (u_int64_t)timeout * (end - states) /
			    (end - start);
			return (state->expire + timeout);
		}
		else
			return (time_uptime);
	}
	return (state->expire + timeout);
}

void
pf_purge_expired_src_nodes()
{
	struct pf_src_node_list	 freelist;
	struct pf_srchash	*sh;
	struct pf_src_node	*cur, *next;
	int i;

	LIST_INIT(&freelist);
	for (i = 0, sh = V_pf_srchash; i <= pf_srchashmask; i++, sh++) {
	    PF_HASHROW_LOCK(sh);
	    LIST_FOREACH_SAFE(cur, &sh->nodes, entry, next)
		if (cur->states == 0 && cur->expire <= time_uptime) {
			pf_unlink_src_node(cur);
			LIST_INSERT_HEAD(&freelist, cur, entry);
		} else if (cur->rule.ptr != NULL)
			cur->rule.ptr->rule_flag |= PFRULE_REFS;
	    PF_HASHROW_UNLOCK(sh);
	}

	pf_free_src_nodes(&freelist);

	V_pf_status.src_nodes = uma_zone_get_cur(V_pf_sources_z);
}

static void
pf_src_tree_remove_state(struct pf_state *s)
{
	struct pf_src_node *sn;
	struct pf_srchash *sh;
	uint32_t timeout;

	timeout = s->rule.ptr->timeout[PFTM_SRC_NODE] ?
	    s->rule.ptr->timeout[PFTM_SRC_NODE] :
	    V_pf_default_rule.timeout[PFTM_SRC_NODE];

	if (s->src_node != NULL) {
		sn = s->src_node;
		sh = &V_pf_srchash[pf_hashsrc(&sn->addr, sn->af)];
	    	PF_HASHROW_LOCK(sh);
		if (s->src.tcp_est)
			--sn->conn;
		if (--sn->states == 0)
			sn->expire = time_uptime + timeout;
	    	PF_HASHROW_UNLOCK(sh);
	}
	if (s->nat_src_node != s->src_node && s->nat_src_node != NULL) {
		sn = s->nat_src_node;
		sh = &V_pf_srchash[pf_hashsrc(&sn->addr, sn->af)];
	    	PF_HASHROW_LOCK(sh);
		if (--sn->states == 0)
			sn->expire = time_uptime + timeout;
	    	PF_HASHROW_UNLOCK(sh);
	}
	s->src_node = s->nat_src_node = NULL;
}

/*
 * Unlink and potentilly free a state. Function may be
 * called with ID hash row locked, but always returns
 * unlocked, since it needs to go through key hash locking.
 */
int
pf_unlink_state(struct pf_state *s, u_int flags)
{
	struct pf_idhash *ih = &V_pf_idhash[PF_IDHASH(s)];

	if ((flags & PF_ENTER_LOCKED) == 0)
		PF_HASHROW_LOCK(ih);
	else
		PF_HASHROW_ASSERT(ih);

	if (s->timeout == PFTM_UNLINKED) {
		/*
		 * State is being processed
		 * by pf_unlink_state() in
		 * an other thread.
		 */
		PF_HASHROW_UNLOCK(ih);
		return (0);	/* XXXGL: undefined actually */
	}

	if (s->src.state == PF_TCPS_PROXY_DST) {
		/* XXX wire key the right one? */
		pf_send_tcp(NULL, s->rule.ptr, s->key[PF_SK_WIRE]->af,
		    &s->key[PF_SK_WIRE]->addr[1],
		    &s->key[PF_SK_WIRE]->addr[0],
		    s->key[PF_SK_WIRE]->port[1],
		    s->key[PF_SK_WIRE]->port[0],
		    s->src.seqhi, s->src.seqlo + 1,
		    TH_RST|TH_ACK, 0, 0, 0, 1, s->tag, NULL);
	}

	LIST_REMOVE(s, entry);
	pf_src_tree_remove_state(s);

	if (V_pfsync_delete_state_ptr != NULL)
		V_pfsync_delete_state_ptr(s);

	STATE_DEC_COUNTERS(s);

	s->timeout = PFTM_UNLINKED;

	PF_HASHROW_UNLOCK(ih);

	pf_detach_state(s);
	/* pf_state_insert() initialises refs to 2, so we can never release the
	 * last reference here, only in pf_release_state(). */
	(void)refcount_release(&s->refs);

	return (pf_release_state(s));
}

void
pf_free_state(struct pf_state *cur)
{

	KASSERT(cur->refs == 0, ("%s: %p has refs", __func__, cur));
	KASSERT(cur->timeout == PFTM_UNLINKED, ("%s: timeout %u", __func__,
	    cur->timeout));

	pf_normalize_tcp_cleanup(cur);
	uma_zfree(V_pf_state_z, cur);
	counter_u64_add(V_pf_status.fcounters[FCNT_STATE_REMOVALS], 1);
}

/*
 * Called only from pf_purge_thread(), thus serialized.
 */
static u_int
pf_purge_expired_states(u_int i, int maxcheck)
{
	struct pf_idhash *ih;
	struct pf_state *s;

	V_pf_status.states = uma_zone_get_cur(V_pf_state_z);

	/*
	 * Go through hash and unlink states that expire now.
	 */
	while (maxcheck > 0) {

		ih = &V_pf_idhash[i];

		/* only take the lock if we expect to do work */
		if (!LIST_EMPTY(&ih->states)) {
relock:
			PF_HASHROW_LOCK(ih);
			LIST_FOREACH(s, &ih->states, entry) {
				if (pf_state_expires(s) <= time_uptime) {
					V_pf_status.states -=
					    pf_unlink_state(s, PF_ENTER_LOCKED);
					goto relock;
				}
				s->rule.ptr->rule_flag |= PFRULE_REFS;
				if (s->nat_rule.ptr != NULL)
					s->nat_rule.ptr->rule_flag |= PFRULE_REFS;
				if (s->anchor.ptr != NULL)
					s->anchor.ptr->rule_flag |= PFRULE_REFS;
				s->kif->pfik_flags |= PFI_IFLAG_REFS;
				if (s->rt_kif)
					s->rt_kif->pfik_flags |= PFI_IFLAG_REFS;
			}
			PF_HASHROW_UNLOCK(ih);
		}

		/* Return when we hit end of hash. */
		if (++i > pf_hashmask) {
			V_pf_status.states = uma_zone_get_cur(V_pf_state_z);
			return (0);
		}

		maxcheck--;
	}

	V_pf_status.states = uma_zone_get_cur(V_pf_state_z);

	return (i);
}

static void
pf_purge_unlinked_rules()
{
	struct pf_rulequeue tmpq;
	struct pf_rule *r, *r1;

	/*
	 * If we have overloading task pending, then we'd
	 * better skip purging this time. There is a tiny
	 * probability that overloading task references
	 * an already unlinked rule.
	 */
	PF_OVERLOADQ_LOCK();
	if (!SLIST_EMPTY(&V_pf_overloadqueue)) {
		PF_OVERLOADQ_UNLOCK();
		return;
	}
	PF_OVERLOADQ_UNLOCK();

	/*
	 * Do naive mark-and-sweep garbage collecting of old rules.
	 * Reference flag is raised by pf_purge_expired_states()
	 * and pf_purge_expired_src_nodes().
	 *
	 * To avoid LOR between PF_UNLNKDRULES_LOCK/PF_RULES_WLOCK,
	 * use a temporary queue.
	 */
	TAILQ_INIT(&tmpq);
	PF_UNLNKDRULES_LOCK();
	TAILQ_FOREACH_SAFE(r, &V_pf_unlinked_rules, entries, r1) {
		if (!(r->rule_flag & PFRULE_REFS)) {
			TAILQ_REMOVE(&V_pf_unlinked_rules, r, entries);
			TAILQ_INSERT_TAIL(&tmpq, r, entries);
		} else
			r->rule_flag &= ~PFRULE_REFS;
	}
	PF_UNLNKDRULES_UNLOCK();

	if (!TAILQ_EMPTY(&tmpq)) {
		PF_RULES_WLOCK();
		TAILQ_FOREACH_SAFE(r, &tmpq, entries, r1) {
			TAILQ_REMOVE(&tmpq, r, entries);
			pf_free_rule(r);
		}
		PF_RULES_WUNLOCK();
	}
}

void
pf_print_host(struct pf_addr *addr, u_int16_t p, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET: {
		u_int32_t a = ntohl(addr->addr32[0]);
		printf("%u.%u.%u.%u", (a>>24)&255, (a>>16)&255,
		    (a>>8)&255, a&255);
		if (p) {
			p = ntohs(p);
			printf(":%u", p);
		}
		break;
	}
#endif /* INET */
#ifdef INET6
	case AF_INET6: {
		u_int16_t b;
		u_int8_t i, curstart, curend, maxstart, maxend;
		curstart = curend = maxstart = maxend = 255;
		for (i = 0; i < 8; i++) {
			if (!addr->addr16[i]) {
				if (curstart == 255)
					curstart = i;
				curend = i;
			} else {
				if ((curend - curstart) >
				    (maxend - maxstart)) {
					maxstart = curstart;
					maxend = curend;
				}
				curstart = curend = 255;
			}
		}
		if ((curend - curstart) >
		    (maxend - maxstart)) {
			maxstart = curstart;
			maxend = curend;
		}
		for (i = 0; i < 8; i++) {
			if (i >= maxstart && i <= maxend) {
				if (i == 0)
					printf(":");
				if (i == maxend)
					printf(":");
			} else {
				b = ntohs(addr->addr16[i]);
				printf("%x", b);
				if (i < 7)
					printf(":");
			}
		}
		if (p) {
			p = ntohs(p);
			printf("[%u]", p);
		}
		break;
	}
#endif /* INET6 */
	}
}

void
pf_print_state(struct pf_state *s)
{
	pf_print_state_parts(s, NULL, NULL);
}

static void
pf_print_state_parts(struct pf_state *s,
    struct pf_state_key *skwp, struct pf_state_key *sksp)
{
	struct pf_state_key *skw, *sks;
	u_int8_t proto, dir;

	/* Do our best to fill these, but they're skipped if NULL */
	skw = skwp ? skwp : (s ? s->key[PF_SK_WIRE] : NULL);
	sks = sksp ? sksp : (s ? s->key[PF_SK_STACK] : NULL);
	proto = skw ? skw->proto : (sks ? sks->proto : 0);
	dir = s ? s->direction : 0;

	switch (proto) {
	case IPPROTO_IPV4:
		printf("IPv4");
		break;
	case IPPROTO_IPV6:
		printf("IPv6");
		break;
	case IPPROTO_TCP:
		printf("TCP");
		break;
	case IPPROTO_UDP:
		printf("UDP");
		break;
	case IPPROTO_ICMP:
		printf("ICMP");
		break;
	case IPPROTO_ICMPV6:
		printf("ICMPv6");
		break;
	default:
		printf("%u", proto);
		break;
	}
	switch (dir) {
	case PF_IN:
		printf(" in");
		break;
	case PF_OUT:
		printf(" out");
		break;
	}
	if (skw) {
		printf(" wire: ");
		pf_print_host(&skw->addr[0], skw->port[0], skw->af);
		printf(" ");
		pf_print_host(&skw->addr[1], skw->port[1], skw->af);
	}
	if (sks) {
		printf(" stack: ");
		if (sks != skw) {
			pf_print_host(&sks->addr[0], sks->port[0], sks->af);
			printf(" ");
			pf_print_host(&sks->addr[1], sks->port[1], sks->af);
		} else
			printf("-");
	}
	if (s) {
		if (proto == IPPROTO_TCP) {
			printf(" [lo=%u high=%u win=%u modulator=%u",
			    s->src.seqlo, s->src.seqhi,
			    s->src.max_win, s->src.seqdiff);
			if (s->src.wscale && s->dst.wscale)
				printf(" wscale=%u",
				    s->src.wscale & PF_WSCALE_MASK);
			printf("]");
			printf(" [lo=%u high=%u win=%u modulator=%u",
			    s->dst.seqlo, s->dst.seqhi,
			    s->dst.max_win, s->dst.seqdiff);
			if (s->src.wscale && s->dst.wscale)
				printf(" wscale=%u",
				s->dst.wscale & PF_WSCALE_MASK);
			printf("]");
		}
		printf(" %u:%u", s->src.state, s->dst.state);
	}
}

void
pf_print_flags(u_int8_t f)
{
	if (f)
		printf(" ");
	if (f & TH_FIN)
		printf("F");
	if (f & TH_SYN)
		printf("S");
	if (f & TH_RST)
		printf("R");
	if (f & TH_PUSH)
		printf("P");
	if (f & TH_ACK)
		printf("A");
	if (f & TH_URG)
		printf("U");
	if (f & TH_ECE)
		printf("E");
	if (f & TH_CWR)
		printf("W");
}

#define	PF_SET_SKIP_STEPS(i)					\
	do {							\
		while (head[i] != cur) {			\
			head[i]->skip[i].ptr = cur;		\
			head[i] = TAILQ_NEXT(head[i], entries);	\
		}						\
	} while (0)

void
pf_calc_skip_steps(struct pf_rulequeue *rules)
{
	struct pf_rule *cur, *prev, *head[PF_SKIP_COUNT];
	int i;

	cur = TAILQ_FIRST(rules);
	prev = cur;
	for (i = 0; i < PF_SKIP_COUNT; ++i)
		head[i] = cur;
	while (cur != NULL) {

		if (cur->kif != prev->kif || cur->ifnot != prev->ifnot)
			PF_SET_SKIP_STEPS(PF_SKIP_IFP);
		if (cur->direction != prev->direction)
			PF_SET_SKIP_STEPS(PF_SKIP_DIR);
		if (cur->af != prev->af)
			PF_SET_SKIP_STEPS(PF_SKIP_AF);
		if (cur->proto != prev->proto)
			PF_SET_SKIP_STEPS(PF_SKIP_PROTO);
		if (cur->src.neg != prev->src.neg ||
		    pf_addr_wrap_neq(&cur->src.addr, &prev->src.addr))
			PF_SET_SKIP_STEPS(PF_SKIP_SRC_ADDR);
		if (cur->src.port[0] != prev->src.port[0] ||
		    cur->src.port[1] != prev->src.port[1] ||
		    cur->src.port_op != prev->src.port_op)
			PF_SET_SKIP_STEPS(PF_SKIP_SRC_PORT);
		if (cur->dst.neg != prev->dst.neg ||
		    pf_addr_wrap_neq(&cur->dst.addr, &prev->dst.addr))
			PF_SET_SKIP_STEPS(PF_SKIP_DST_ADDR);
		if (cur->dst.port[0] != prev->dst.port[0] ||
		    cur->dst.port[1] != prev->dst.port[1] ||
		    cur->dst.port_op != prev->dst.port_op)
			PF_SET_SKIP_STEPS(PF_SKIP_DST_PORT);

		prev = cur;
		cur = TAILQ_NEXT(cur, entries);
	}
	for (i = 0; i < PF_SKIP_COUNT; ++i)
		PF_SET_SKIP_STEPS(i);
}

static int
pf_addr_wrap_neq(struct pf_addr_wrap *aw1, struct pf_addr_wrap *aw2)
{
	if (aw1->type != aw2->type)
		return (1);
	switch (aw1->type) {
	case PF_ADDR_ADDRMASK:
	case PF_ADDR_RANGE:
		if (PF_ANEQ(&aw1->v.a.addr, &aw2->v.a.addr, AF_INET6))
			return (1);
		if (PF_ANEQ(&aw1->v.a.mask, &aw2->v.a.mask, AF_INET6))
			return (1);
		return (0);
	case PF_ADDR_DYNIFTL:
		return (aw1->p.dyn->pfid_kt != aw2->p.dyn->pfid_kt);
	case PF_ADDR_NOROUTE:
	case PF_ADDR_URPFFAILED:
		return (0);
	case PF_ADDR_TABLE:
		return (aw1->p.tbl != aw2->p.tbl);
	default:
		printf("invalid address type: %d\n", aw1->type);
		return (1);
	}
}

/**
 * Checksum updates are a little complicated because the checksum in the TCP/UDP
 * header isn't always a full checksum. In some cases (i.e. output) it's a
 * pseudo-header checksum, which is a partial checksum over src/dst IP
 * addresses, protocol number and length.
 *
 * That means we have the following cases:
 *  * Input or forwarding: we don't have TSO, the checksum fields are full
 *  	checksums, we need to update the checksum whenever we change anything.
 *  * Output (i.e. the checksum is a pseudo-header checksum):
 *  	x The field being updated is src/dst address or affects the length of
 *  	the packet. We need to update the pseudo-header checksum (note that this
 *  	checksum is not ones' complement).
 *  	x Some other field is being modified (e.g. src/dst port numbers): We
 *  	don't have to update anything.
 **/
u_int16_t
pf_cksum_fixup(u_int16_t cksum, u_int16_t old, u_int16_t new, u_int8_t udp)
{
	u_int32_t	l;

	if (udp && !cksum)
		return (0x0000);
	l = cksum + old - new;
	l = (l >> 16) + (l & 65535);
	l = l & 65535;
	if (udp && !l)
		return (0xFFFF);
	return (l);
}

u_int16_t
pf_proto_cksum_fixup(struct mbuf *m, u_int16_t cksum, u_int16_t old,
        u_int16_t new, u_int8_t udp)
{
	if (m->m_pkthdr.csum_flags & (CSUM_DELAY_DATA | CSUM_DELAY_DATA_IPV6))
		return (cksum);

	return (pf_cksum_fixup(cksum, old, new, udp));
}

static void
pf_change_ap(struct mbuf *m, struct pf_addr *a, u_int16_t *p, u_int16_t *ic,
        u_int16_t *pc, struct pf_addr *an, u_int16_t pn, u_int8_t u,
        sa_family_t af)
{
	struct pf_addr	ao;
	u_int16_t	po = *p;

	PF_ACPY(&ao, a, af);
	PF_ACPY(a, an, af);

	if (m->m_pkthdr.csum_flags & (CSUM_DELAY_DATA | CSUM_DELAY_DATA_IPV6))
		*pc = ~*pc;

	*p = pn;

	switch (af) {
#ifdef INET
	case AF_INET:
		*ic = pf_cksum_fixup(pf_cksum_fixup(*ic,
		    ao.addr16[0], an->addr16[0], 0),
		    ao.addr16[1], an->addr16[1], 0);
		*p = pn;

		*pc = pf_cksum_fixup(pf_cksum_fixup(*pc,
		    ao.addr16[0], an->addr16[0], u),
		    ao.addr16[1], an->addr16[1], u);

		*pc = pf_proto_cksum_fixup(m, *pc, po, pn, u);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		*pc = pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
		    pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
		    pf_cksum_fixup(pf_cksum_fixup(*pc,
		    ao.addr16[0], an->addr16[0], u),
		    ao.addr16[1], an->addr16[1], u),
		    ao.addr16[2], an->addr16[2], u),
		    ao.addr16[3], an->addr16[3], u),
		    ao.addr16[4], an->addr16[4], u),
		    ao.addr16[5], an->addr16[5], u),
		    ao.addr16[6], an->addr16[6], u),
		    ao.addr16[7], an->addr16[7], u);

		*pc = pf_proto_cksum_fixup(m, *pc, po, pn, u);
		break;
#endif /* INET6 */
	}

	if (m->m_pkthdr.csum_flags & (CSUM_DELAY_DATA | 
	    CSUM_DELAY_DATA_IPV6)) {
		*pc = ~*pc;
		if (! *pc)
			*pc = 0xffff;
	}
}

/* Changes a u_int32_t.  Uses a void * so there are no align restrictions */
void
pf_change_a(void *a, u_int16_t *c, u_int32_t an, u_int8_t u)
{
	u_int32_t	ao;

	memcpy(&ao, a, sizeof(ao));
	memcpy(a, &an, sizeof(u_int32_t));
	*c = pf_cksum_fixup(pf_cksum_fixup(*c, ao / 65536, an / 65536, u),
	    ao % 65536, an % 65536, u);
}

void
pf_change_proto_a(struct mbuf *m, void *a, u_int16_t *c, u_int32_t an, u_int8_t udp)
{
	u_int32_t	ao;

	memcpy(&ao, a, sizeof(ao));
	memcpy(a, &an, sizeof(u_int32_t));

	*c = pf_proto_cksum_fixup(m,
	    pf_proto_cksum_fixup(m, *c, ao / 65536, an / 65536, udp),
	    ao % 65536, an % 65536, udp);
}

#ifdef INET6
static void
pf_change_a6(struct pf_addr *a, u_int16_t *c, struct pf_addr *an, u_int8_t u)
{
	struct pf_addr	ao;

	PF_ACPY(&ao, a, AF_INET6);
	PF_ACPY(a, an, AF_INET6);

	*c = pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
	    pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
	    pf_cksum_fixup(pf_cksum_fixup(*c,
	    ao.addr16[0], an->addr16[0], u),
	    ao.addr16[1], an->addr16[1], u),
	    ao.addr16[2], an->addr16[2], u),
	    ao.addr16[3], an->addr16[3], u),
	    ao.addr16[4], an->addr16[4], u),
	    ao.addr16[5], an->addr16[5], u),
	    ao.addr16[6], an->addr16[6], u),
	    ao.addr16[7], an->addr16[7], u);
}
#endif /* INET6 */

static void
pf_change_icmp(struct pf_addr *ia, u_int16_t *ip, struct pf_addr *oa,
    struct pf_addr *na, u_int16_t np, u_int16_t *pc, u_int16_t *h2c,
    u_int16_t *ic, u_int16_t *hc, u_int8_t u, sa_family_t af)
{
	struct pf_addr	oia, ooa;

	PF_ACPY(&oia, ia, af);
	if (oa)
		PF_ACPY(&ooa, oa, af);

	/* Change inner protocol port, fix inner protocol checksum. */
	if (ip != NULL) {
		u_int16_t	oip = *ip;
		u_int32_t	opc;

		if (pc != NULL)
			opc = *pc;
		*ip = np;
		if (pc != NULL)
			*pc = pf_cksum_fixup(*pc, oip, *ip, u);
		*ic = pf_cksum_fixup(*ic, oip, *ip, 0);
		if (pc != NULL)
			*ic = pf_cksum_fixup(*ic, opc, *pc, 0);
	}
	/* Change inner ip address, fix inner ip and icmp checksums. */
	PF_ACPY(ia, na, af);
	switch (af) {
#ifdef INET
	case AF_INET: {
		u_int32_t	 oh2c = *h2c;

		*h2c = pf_cksum_fixup(pf_cksum_fixup(*h2c,
		    oia.addr16[0], ia->addr16[0], 0),
		    oia.addr16[1], ia->addr16[1], 0);
		*ic = pf_cksum_fixup(pf_cksum_fixup(*ic,
		    oia.addr16[0], ia->addr16[0], 0),
		    oia.addr16[1], ia->addr16[1], 0);
		*ic = pf_cksum_fixup(*ic, oh2c, *h2c, 0);
		break;
	}
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		*ic = pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
		    pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
		    pf_cksum_fixup(pf_cksum_fixup(*ic,
		    oia.addr16[0], ia->addr16[0], u),
		    oia.addr16[1], ia->addr16[1], u),
		    oia.addr16[2], ia->addr16[2], u),
		    oia.addr16[3], ia->addr16[3], u),
		    oia.addr16[4], ia->addr16[4], u),
		    oia.addr16[5], ia->addr16[5], u),
		    oia.addr16[6], ia->addr16[6], u),
		    oia.addr16[7], ia->addr16[7], u);
		break;
#endif /* INET6 */
	}
	/* Outer ip address, fix outer ip or icmpv6 checksum, if necessary. */
	if (oa) {
		PF_ACPY(oa, na, af);
		switch (af) {
#ifdef INET
		case AF_INET:
			*hc = pf_cksum_fixup(pf_cksum_fixup(*hc,
			    ooa.addr16[0], oa->addr16[0], 0),
			    ooa.addr16[1], oa->addr16[1], 0);
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			*ic = pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
			    pf_cksum_fixup(pf_cksum_fixup(pf_cksum_fixup(
			    pf_cksum_fixup(pf_cksum_fixup(*ic,
			    ooa.addr16[0], oa->addr16[0], u),
			    ooa.addr16[1], oa->addr16[1], u),
			    ooa.addr16[2], oa->addr16[2], u),
			    ooa.addr16[3], oa->addr16[3], u),
			    ooa.addr16[4], oa->addr16[4], u),
			    ooa.addr16[5], oa->addr16[5], u),
			    ooa.addr16[6], oa->addr16[6], u),
			    ooa.addr16[7], oa->addr16[7], u);
			break;
#endif /* INET6 */
		}
	}
}


/*
 * Need to modulate the sequence numbers in the TCP SACK option
 * (credits to Krzysztof Pfaff for report and patch)
 */
static int
pf_modulate_sack(struct mbuf *m, int off, struct pf_pdesc *pd,
    struct tcphdr *th, struct pf_state_peer *dst)
{
	int hlen = (th->th_off << 2) - sizeof(*th), thoptlen = hlen;
	u_int8_t opts[TCP_MAXOLEN], *opt = opts;
	int copyback = 0, i, olen;
	struct sackblk sack;

#define	TCPOLEN_SACKLEN	(TCPOLEN_SACK + 2)
	if (hlen < TCPOLEN_SACKLEN ||
	    !pf_pull_hdr(m, off + sizeof(*th), opts, hlen, NULL, NULL, pd->af))
		return 0;

	while (hlen >= TCPOLEN_SACKLEN) {
		olen = opt[1];
		switch (*opt) {
		case TCPOPT_EOL:	/* FALLTHROUGH */
		case TCPOPT_NOP:
			opt++;
			hlen--;
			break;
		case TCPOPT_SACK:
			if (olen > hlen)
				olen = hlen;
			if (olen >= TCPOLEN_SACKLEN) {
				for (i = 2; i + TCPOLEN_SACK <= olen;
				    i += TCPOLEN_SACK) {
					memcpy(&sack, &opt[i], sizeof(sack));
					pf_change_proto_a(m, &sack.start, &th->th_sum,
					    htonl(ntohl(sack.start) - dst->seqdiff), 0);
					pf_change_proto_a(m, &sack.end, &th->th_sum,
					    htonl(ntohl(sack.end) - dst->seqdiff), 0);
					memcpy(&opt[i], &sack, sizeof(sack));
				}
				copyback = 1;
			}
			/* FALLTHROUGH */
		default:
			if (olen < 2)
				olen = 2;
			hlen -= olen;
			opt += olen;
		}
	}

	if (copyback)
		m_copyback(m, off + sizeof(*th), thoptlen, (caddr_t)opts);
	return (copyback);
}

static void
pf_send_tcp(struct mbuf *replyto, const struct pf_rule *r, sa_family_t af,
    const struct pf_addr *saddr, const struct pf_addr *daddr,
    u_int16_t sport, u_int16_t dport, u_int32_t seq, u_int32_t ack,
    u_int8_t flags, u_int16_t win, u_int16_t mss, u_int8_t ttl, int tag,
    u_int16_t rtag, struct ifnet *ifp)
{
	struct pf_send_entry *pfse;
	struct mbuf	*m;
	int		 len, tlen;
#ifdef INET
	struct ip	*h = NULL;
#endif /* INET */
#ifdef INET6
	struct ip6_hdr	*h6 = NULL;
#endif /* INET6 */
	struct tcphdr	*th;
	char		*opt;
	struct pf_mtag  *pf_mtag;

	len = 0;
	th = NULL;

	/* maximum segment size tcp option */
	tlen = sizeof(struct tcphdr);
	if (mss)
		tlen += 4;

	switch (af) {
#ifdef INET
	case AF_INET:
		len = sizeof(struct ip) + tlen;
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		len = sizeof(struct ip6_hdr) + tlen;
		break;
#endif /* INET6 */
	default:
		panic("%s: unsupported af %d", __func__, af);
	}

	/* Allocate outgoing queue entry, mbuf and mbuf tag. */
	pfse = malloc(sizeof(*pfse), M_PFTEMP, M_NOWAIT);
	if (pfse == NULL)
		return;
	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL) {
		free(pfse, M_PFTEMP);
		return;
	}
#ifdef MAC
	mac_netinet_firewall_send(m);
#endif
	if ((pf_mtag = pf_get_mtag(m)) == NULL) {
		free(pfse, M_PFTEMP);
		m_freem(m);
		return;
	}
	if (tag)
		m->m_flags |= M_SKIP_FIREWALL;
	pf_mtag->tag = rtag;

	if (r != NULL && r->rtableid >= 0)
		M_SETFIB(m, r->rtableid);

#ifdef ALTQ
	if (r != NULL && r->qid) {
		pf_mtag->qid = r->qid;

		/* add hints for ecn */
		pf_mtag->hdr = mtod(m, struct ip *);
	}
#endif /* ALTQ */
	m->m_data += max_linkhdr;
	m->m_pkthdr.len = m->m_len = len;
	m->m_pkthdr.rcvif = NULL;
	bzero(m->m_data, len);
	switch (af) {
#ifdef INET
	case AF_INET:
		h = mtod(m, struct ip *);

		/* IP header fields included in the TCP checksum */
		h->ip_p = IPPROTO_TCP;
		h->ip_len = htons(tlen);
		h->ip_src.s_addr = saddr->v4.s_addr;
		h->ip_dst.s_addr = daddr->v4.s_addr;

		th = (struct tcphdr *)((caddr_t)h + sizeof(struct ip));
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		h6 = mtod(m, struct ip6_hdr *);

		/* IP header fields included in the TCP checksum */
		h6->ip6_nxt = IPPROTO_TCP;
		h6->ip6_plen = htons(tlen);
		memcpy(&h6->ip6_src, &saddr->v6, sizeof(struct in6_addr));
		memcpy(&h6->ip6_dst, &daddr->v6, sizeof(struct in6_addr));

		th = (struct tcphdr *)((caddr_t)h6 + sizeof(struct ip6_hdr));
		break;
#endif /* INET6 */
	}

	/* TCP header */
	th->th_sport = sport;
	th->th_dport = dport;
	th->th_seq = htonl(seq);
	th->th_ack = htonl(ack);
	th->th_off = tlen >> 2;
	th->th_flags = flags;
	th->th_win = htons(win);

	if (mss) {
		opt = (char *)(th + 1);
		opt[0] = TCPOPT_MAXSEG;
		opt[1] = 4;
		HTONS(mss);
		bcopy((caddr_t)&mss, (caddr_t)(opt + 2), 2);
	}

	switch (af) {
#ifdef INET
	case AF_INET:
		/* TCP checksum */
		th->th_sum = in_cksum(m, len);

		/* Finish the IP header */
		h->ip_v = 4;
		h->ip_hl = sizeof(*h) >> 2;
		h->ip_tos = IPTOS_LOWDELAY;
		h->ip_off = htons(V_path_mtu_discovery ? IP_DF : 0);
		h->ip_len = htons(len);
		h->ip_ttl = ttl ? ttl : V_ip_defttl;
		h->ip_sum = 0;

		pfse->pfse_type = PFSE_IP;
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		/* TCP checksum */
		th->th_sum = in6_cksum(m, IPPROTO_TCP,
		    sizeof(struct ip6_hdr), tlen);

		h6->ip6_vfc |= IPV6_VERSION;
		h6->ip6_hlim = IPV6_DEFHLIM;

		pfse->pfse_type = PFSE_IP6;
		break;
#endif /* INET6 */
	}
	pfse->pfse_m = m;
	pf_send(pfse);
}

static void
pf_return(struct pf_rule *r, struct pf_rule *nr, struct pf_pdesc *pd,
    struct pf_state_key *sk, int off, struct mbuf *m, struct tcphdr *th,
    struct pfi_kif *kif, u_int16_t bproto_sum, u_int16_t bip_sum, int hdrlen,
    u_short *reason)
{
	struct pf_addr	* const saddr = pd->src;
	struct pf_addr	* const daddr = pd->dst;
	sa_family_t	 af = pd->af;

	/* undo NAT changes, if they have taken place */
	if (nr != NULL) {
		PF_ACPY(saddr, &sk->addr[pd->sidx], af);
		PF_ACPY(daddr, &sk->addr[pd->didx], af);
		if (pd->sport)
			*pd->sport = sk->port[pd->sidx];
		if (pd->dport)
			*pd->dport = sk->port[pd->didx];
		if (pd->proto_sum)
			*pd->proto_sum = bproto_sum;
		if (pd->ip_sum)
			*pd->ip_sum = bip_sum;
		m_copyback(m, off, hdrlen, pd->hdr.any);
	}
	if (pd->proto == IPPROTO_TCP &&
	    ((r->rule_flag & PFRULE_RETURNRST) ||
	    (r->rule_flag & PFRULE_RETURN)) &&
	    !(th->th_flags & TH_RST)) {
		u_int32_t	 ack = ntohl(th->th_seq) + pd->p_len;
		int		 len = 0;
#ifdef INET
		struct ip	*h4;
#endif
#ifdef INET6
		struct ip6_hdr	*h6;
#endif

		switch (af) {
#ifdef INET
		case AF_INET:
			h4 = mtod(m, struct ip *);
			len = ntohs(h4->ip_len) - off;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			h6 = mtod(m, struct ip6_hdr *);
			len = ntohs(h6->ip6_plen) - (off - sizeof(*h6));
			break;
#endif
		}

		if (pf_check_proto_cksum(m, off, len, IPPROTO_TCP, af))
			REASON_SET(reason, PFRES_PROTCKSUM);
		else {
			if (th->th_flags & TH_SYN)
				ack++;
			if (th->th_flags & TH_FIN)
				ack++;
			pf_send_tcp(m, r, af, pd->dst,
				pd->src, th->th_dport, th->th_sport,
				ntohl(th->th_ack), ack, TH_RST|TH_ACK, 0, 0,
				r->return_ttl, 1, 0, kif->pfik_ifp);
		}
	} else if (pd->proto != IPPROTO_ICMP && af == AF_INET &&
		r->return_icmp)
		pf_send_icmp(m, r->return_icmp >> 8,
			r->return_icmp & 255, af, r);
	else if (pd->proto != IPPROTO_ICMPV6 && af == AF_INET6 &&
		r->return_icmp6)
		pf_send_icmp(m, r->return_icmp6 >> 8,
			r->return_icmp6 & 255, af, r);
}


static int
pf_ieee8021q_setpcp(struct mbuf *m, u_int8_t prio)
{
	struct m_tag *mtag;

	KASSERT(prio <= PF_PRIO_MAX,
	    ("%s with invalid pcp", __func__));

	mtag = m_tag_locate(m, MTAG_8021Q, MTAG_8021Q_PCP_OUT, NULL);
	if (mtag == NULL) {
		mtag = m_tag_alloc(MTAG_8021Q, MTAG_8021Q_PCP_OUT,
		    sizeof(uint8_t), M_NOWAIT);
		if (mtag == NULL)
			return (ENOMEM);
		m_tag_prepend(m, mtag);
	}

	*(uint8_t *)(mtag + 1) = prio;
	return (0);
}

static int
pf_match_ieee8021q_pcp(u_int8_t prio, struct mbuf *m)
{
	struct m_tag *mtag;
	u_int8_t mpcp;

	mtag = m_tag_locate(m, MTAG_8021Q, MTAG_8021Q_PCP_IN, NULL);
	if (mtag == NULL)
		return (0);

	if (prio == PF_PRIO_ZERO)
		prio = 0;

	mpcp = *(uint8_t *)(mtag + 1);

	return (mpcp == prio);
}

static void
pf_send_icmp(struct mbuf *m, u_int8_t type, u_int8_t code, sa_family_t af,
    struct pf_rule *r)
{
	struct pf_send_entry *pfse;
	struct mbuf *m0;
	struct pf_mtag *pf_mtag;

	/* Allocate outgoing queue entry, mbuf and mbuf tag. */
	pfse = malloc(sizeof(*pfse), M_PFTEMP, M_NOWAIT);
	if (pfse == NULL)
		return;

	if ((m0 = m_copypacket(m, M_NOWAIT)) == NULL) {
		free(pfse, M_PFTEMP);
		return;
	}

	if ((pf_mtag = pf_get_mtag(m0)) == NULL) {
		free(pfse, M_PFTEMP);
		return;
	}
	/* XXX: revisit */
	m0->m_flags |= M_SKIP_FIREWALL;

	if (r->rtableid >= 0)
		M_SETFIB(m0, r->rtableid);

#ifdef ALTQ
	if (r->qid) {
		pf_mtag->qid = r->qid;
		/* add hints for ecn */
		pf_mtag->hdr = mtod(m0, struct ip *);
	}
#endif /* ALTQ */

	switch (af) {
#ifdef INET
	case AF_INET:
		pfse->pfse_type = PFSE_ICMP;
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		pfse->pfse_type = PFSE_ICMP6;
		break;
#endif /* INET6 */
	}
	pfse->pfse_m = m0;
	pfse->icmpopts.type = type;
	pfse->icmpopts.code = code;
	pf_send(pfse);
}

/*
 * Return 1 if the addresses a and b match (with mask m), otherwise return 0.
 * If n is 0, they match if they are equal. If n is != 0, they match if they
 * are different.
 */
int
pf_match_addr(u_int8_t n, struct pf_addr *a, struct pf_addr *m,
    struct pf_addr *b, sa_family_t af)
{
	int	match = 0;

	switch (af) {
#ifdef INET
	case AF_INET:
		if ((a->addr32[0] & m->addr32[0]) ==
		    (b->addr32[0] & m->addr32[0]))
			match++;
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		if (((a->addr32[0] & m->addr32[0]) ==
		     (b->addr32[0] & m->addr32[0])) &&
		    ((a->addr32[1] & m->addr32[1]) ==
		     (b->addr32[1] & m->addr32[1])) &&
		    ((a->addr32[2] & m->addr32[2]) ==
		     (b->addr32[2] & m->addr32[2])) &&
		    ((a->addr32[3] & m->addr32[3]) ==
		     (b->addr32[3] & m->addr32[3])))
			match++;
		break;
#endif /* INET6 */
	}
	if (match) {
		if (n)
			return (0);
		else
			return (1);
	} else {
		if (n)
			return (1);
		else
			return (0);
	}
}

/*
 * Return 1 if b <= a <= e, otherwise return 0.
 */
int
pf_match_addr_range(struct pf_addr *b, struct pf_addr *e,
    struct pf_addr *a, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		if ((ntohl(a->addr32[0]) < ntohl(b->addr32[0])) ||
		    (ntohl(a->addr32[0]) > ntohl(e->addr32[0])))
			return (0);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6: {
		int	i;

		/* check a >= b */
		for (i = 0; i < 4; ++i)
			if (ntohl(a->addr32[i]) > ntohl(b->addr32[i]))
				break;
			else if (ntohl(a->addr32[i]) < ntohl(b->addr32[i]))
				return (0);
		/* check a <= e */
		for (i = 0; i < 4; ++i)
			if (ntohl(a->addr32[i]) < ntohl(e->addr32[i]))
				break;
			else if (ntohl(a->addr32[i]) > ntohl(e->addr32[i]))
				return (0);
		break;
	}
#endif /* INET6 */
	}
	return (1);
}

static int
pf_match(u_int8_t op, u_int32_t a1, u_int32_t a2, u_int32_t p)
{
	switch (op) {
	case PF_OP_IRG:
		return ((p > a1) && (p < a2));
	case PF_OP_XRG:
		return ((p < a1) || (p > a2));
	case PF_OP_RRG:
		return ((p >= a1) && (p <= a2));
	case PF_OP_EQ:
		return (p == a1);
	case PF_OP_NE:
		return (p != a1);
	case PF_OP_LT:
		return (p < a1);
	case PF_OP_LE:
		return (p <= a1);
	case PF_OP_GT:
		return (p > a1);
	case PF_OP_GE:
		return (p >= a1);
	}
	return (0); /* never reached */
}

int
pf_match_port(u_int8_t op, u_int16_t a1, u_int16_t a2, u_int16_t p)
{
	NTOHS(a1);
	NTOHS(a2);
	NTOHS(p);
	return (pf_match(op, a1, a2, p));
}

static int
pf_match_uid(u_int8_t op, uid_t a1, uid_t a2, uid_t u)
{
	if (u == UID_MAX && op != PF_OP_EQ && op != PF_OP_NE)
		return (0);
	return (pf_match(op, a1, a2, u));
}

static int
pf_match_gid(u_int8_t op, gid_t a1, gid_t a2, gid_t g)
{
	if (g == GID_MAX && op != PF_OP_EQ && op != PF_OP_NE)
		return (0);
	return (pf_match(op, a1, a2, g));
}

int
pf_match_tag(struct mbuf *m, struct pf_rule *r, int *tag, int mtag)
{
	if (*tag == -1)
		*tag = mtag;

	return ((!r->match_tag_not && r->match_tag == *tag) ||
	    (r->match_tag_not && r->match_tag != *tag));
}

int
pf_tag_packet(struct mbuf *m, struct pf_pdesc *pd, int tag)
{

	KASSERT(tag > 0, ("%s: tag %d", __func__, tag));

	if (pd->pf_mtag == NULL && ((pd->pf_mtag = pf_get_mtag(m)) == NULL))
		return (ENOMEM);

	pd->pf_mtag->tag = tag;

	return (0);
}

#define	PF_ANCHOR_STACKSIZE	32
struct pf_anchor_stackframe {
	struct pf_ruleset	*rs;
	struct pf_rule		*r;	/* XXX: + match bit */
	struct pf_anchor	*child;
};

/*
 * XXX: We rely on malloc(9) returning pointer aligned addresses.
 */
#define	PF_ANCHORSTACK_MATCH	0x00000001
#define	PF_ANCHORSTACK_MASK	(PF_ANCHORSTACK_MATCH)

#define	PF_ANCHOR_MATCH(f)	((uintptr_t)(f)->r & PF_ANCHORSTACK_MATCH)
#define	PF_ANCHOR_RULE(f)	(struct pf_rule *)			\
				((uintptr_t)(f)->r & ~PF_ANCHORSTACK_MASK)
#define	PF_ANCHOR_SET_MATCH(f)	do { (f)->r = (void *) 			\
				((uintptr_t)(f)->r | PF_ANCHORSTACK_MATCH);  \
} while (0)

void
pf_step_into_anchor(struct pf_anchor_stackframe *stack, int *depth,
    struct pf_ruleset **rs, int n, struct pf_rule **r, struct pf_rule **a,
    int *match)
{
	struct pf_anchor_stackframe	*f;

	PF_RULES_RASSERT();

	if (match)
		*match = 0;
	if (*depth >= PF_ANCHOR_STACKSIZE) {
		printf("%s: anchor stack overflow on %s\n",
		    __func__, (*r)->anchor->name);
		*r = TAILQ_NEXT(*r, entries);
		return;
	} else if (*depth == 0 && a != NULL)
		*a = *r;
	f = stack + (*depth)++;
	f->rs = *rs;
	f->r = *r;
	if ((*r)->anchor_wildcard) {
		struct pf_anchor_node *parent = &(*r)->anchor->children;

		if ((f->child = RB_MIN(pf_anchor_node, parent)) == NULL) {
			*r = NULL;
			return;
		}
		*rs = &f->child->ruleset;
	} else {
		f->child = NULL;
		*rs = &(*r)->anchor->ruleset;
	}
	*r = TAILQ_FIRST((*rs)->rules[n].active.ptr);
}

int
pf_step_out_of_anchor(struct pf_anchor_stackframe *stack, int *depth,
    struct pf_ruleset **rs, int n, struct pf_rule **r, struct pf_rule **a,
    int *match)
{
	struct pf_anchor_stackframe	*f;
	struct pf_rule *fr;
	int quick = 0;

	PF_RULES_RASSERT();

	do {
		if (*depth <= 0)
			break;
		f = stack + *depth - 1;
		fr = PF_ANCHOR_RULE(f);
		if (f->child != NULL) {
			struct pf_anchor_node *parent;

			/*
			 * This block traverses through
			 * a wildcard anchor.
			 */
			parent = &fr->anchor->children;
			if (match != NULL && *match) {
				/*
				 * If any of "*" matched, then
				 * "foo/ *" matched, mark frame
				 * appropriately.
				 */
				PF_ANCHOR_SET_MATCH(f);
				*match = 0;
			}
			f->child = RB_NEXT(pf_anchor_node, parent, f->child);
			if (f->child != NULL) {
				*rs = &f->child->ruleset;
				*r = TAILQ_FIRST((*rs)->rules[n].active.ptr);
				if (*r == NULL)
					continue;
				else
					break;
			}
		}
		(*depth)--;
		if (*depth == 0 && a != NULL)
			*a = NULL;
		*rs = f->rs;
		if (PF_ANCHOR_MATCH(f) || (match != NULL && *match))
			quick = fr->quick;
		*r = TAILQ_NEXT(fr, entries);
	} while (*r == NULL);

	return (quick);
}

#ifdef INET6
void
pf_poolmask(struct pf_addr *naddr, struct pf_addr *raddr,
    struct pf_addr *rmask, struct pf_addr *saddr, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		naddr->addr32[0] = (raddr->addr32[0] & rmask->addr32[0]) |
		((rmask->addr32[0] ^ 0xffffffff ) & saddr->addr32[0]);
		break;
#endif /* INET */
	case AF_INET6:
		naddr->addr32[0] = (raddr->addr32[0] & rmask->addr32[0]) |
		((rmask->addr32[0] ^ 0xffffffff ) & saddr->addr32[0]);
		naddr->addr32[1] = (raddr->addr32[1] & rmask->addr32[1]) |
		((rmask->addr32[1] ^ 0xffffffff ) & saddr->addr32[1]);
		naddr->addr32[2] = (raddr->addr32[2] & rmask->addr32[2]) |
		((rmask->addr32[2] ^ 0xffffffff ) & saddr->addr32[2]);
		naddr->addr32[3] = (raddr->addr32[3] & rmask->addr32[3]) |
		((rmask->addr32[3] ^ 0xffffffff ) & saddr->addr32[3]);
		break;
	}
}

void
pf_addr_inc(struct pf_addr *addr, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET:
		addr->addr32[0] = htonl(ntohl(addr->addr32[0]) + 1);
		break;
#endif /* INET */
	case AF_INET6:
		if (addr->addr32[3] == 0xffffffff) {
			addr->addr32[3] = 0;
			if (addr->addr32[2] == 0xffffffff) {
				addr->addr32[2] = 0;
				if (addr->addr32[1] == 0xffffffff) {
					addr->addr32[1] = 0;
					addr->addr32[0] =
					    htonl(ntohl(addr->addr32[0]) + 1);
				} else
					addr->addr32[1] =
					    htonl(ntohl(addr->addr32[1]) + 1);
			} else
				addr->addr32[2] =
				    htonl(ntohl(addr->addr32[2]) + 1);
		} else
			addr->addr32[3] =
			    htonl(ntohl(addr->addr32[3]) + 1);
		break;
	}
}
#endif /* INET6 */

int
pf_socket_lookup(int direction, struct pf_pdesc *pd, struct mbuf *m)
{
	struct pf_addr		*saddr, *daddr;
	u_int16_t		 sport, dport;
	struct inpcbinfo	*pi;
	struct inpcb		*inp;

	pd->lookup.uid = UID_MAX;
	pd->lookup.gid = GID_MAX;

	switch (pd->proto) {
	case IPPROTO_TCP:
		if (pd->hdr.tcp == NULL)
			return (-1);
		sport = pd->hdr.tcp->th_sport;
		dport = pd->hdr.tcp->th_dport;
		pi = &V_tcbinfo;
		break;
	case IPPROTO_UDP:
		if (pd->hdr.udp == NULL)
			return (-1);
		sport = pd->hdr.udp->uh_sport;
		dport = pd->hdr.udp->uh_dport;
		pi = &V_udbinfo;
		break;
	default:
		return (-1);
	}
	if (direction == PF_IN) {
		saddr = pd->src;
		daddr = pd->dst;
	} else {
		u_int16_t	p;

		p = sport;
		sport = dport;
		dport = p;
		saddr = pd->dst;
		daddr = pd->src;
	}
	switch (pd->af) {
#ifdef INET
	case AF_INET:
		inp = in_pcblookup_mbuf(pi, saddr->v4, sport, daddr->v4,
		    dport, INPLOOKUP_RLOCKPCB, NULL, m);
		if (inp == NULL) {
			inp = in_pcblookup_mbuf(pi, saddr->v4, sport,
			   daddr->v4, dport, INPLOOKUP_WILDCARD |
			   INPLOOKUP_RLOCKPCB, NULL, m);
			if (inp == NULL)
				return (-1);
		}
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		inp = in6_pcblookup_mbuf(pi, &saddr->v6, sport, &daddr->v6,
		    dport, INPLOOKUP_RLOCKPCB, NULL, m);
		if (inp == NULL) {
			inp = in6_pcblookup_mbuf(pi, &saddr->v6, sport,
			    &daddr->v6, dport, INPLOOKUP_WILDCARD |
			    INPLOOKUP_RLOCKPCB, NULL, m);
			if (inp == NULL)
				return (-1);
		}
		break;
#endif /* INET6 */

	default:
		return (-1);
	}
	INP_RLOCK_ASSERT(inp);
	pd->lookup.uid = inp->inp_cred->cr_uid;
	pd->lookup.gid = inp->inp_cred->cr_groups[0];
	INP_RUNLOCK(inp);

	return (1);
}

static u_int8_t
pf_get_wscale(struct mbuf *m, int off, u_int16_t th_off, sa_family_t af)
{
	int		 hlen;
	u_int8_t	 hdr[60];
	u_int8_t	*opt, optlen;
	u_int8_t	 wscale = 0;

	hlen = th_off << 2;		/* hlen <= sizeof(hdr) */
	if (hlen <= sizeof(struct tcphdr))
		return (0);
	if (!pf_pull_hdr(m, off, hdr, hlen, NULL, NULL, af))
		return (0);
	opt = hdr + sizeof(struct tcphdr);
	hlen -= sizeof(struct tcphdr);
	while (hlen >= 3) {
		switch (*opt) {
		case TCPOPT_EOL:
		case TCPOPT_NOP:
			++opt;
			--hlen;
			break;
		case TCPOPT_WINDOW:
			wscale = opt[2];
			if (wscale > TCP_MAX_WINSHIFT)
				wscale = TCP_MAX_WINSHIFT;
			wscale |= PF_WSCALE_FLAG;
			/* FALLTHROUGH */
		default:
			optlen = opt[1];
			if (optlen < 2)
				optlen = 2;
			hlen -= optlen;
			opt += optlen;
			break;
		}
	}
	return (wscale);
}

static u_int16_t
pf_get_mss(struct mbuf *m, int off, u_int16_t th_off, sa_family_t af)
{
	int		 hlen;
	u_int8_t	 hdr[60];
	u_int8_t	*opt, optlen;
	u_int16_t	 mss = V_tcp_mssdflt;

	hlen = th_off << 2;	/* hlen <= sizeof(hdr) */
	if (hlen <= sizeof(struct tcphdr))
		return (0);
	if (!pf_pull_hdr(m, off, hdr, hlen, NULL, NULL, af))
		return (0);
	opt = hdr + sizeof(struct tcphdr);
	hlen -= sizeof(struct tcphdr);
	while (hlen >= TCPOLEN_MAXSEG) {
		switch (*opt) {
		case TCPOPT_EOL:
		case TCPOPT_NOP:
			++opt;
			--hlen;
			break;
		case TCPOPT_MAXSEG:
			bcopy((caddr_t)(opt + 2), (caddr_t)&mss, 2);
			NTOHS(mss);
			/* FALLTHROUGH */
		default:
			optlen = opt[1];
			if (optlen < 2)
				optlen = 2;
			hlen -= optlen;
			opt += optlen;
			break;
		}
	}
	return (mss);
}

static u_int16_t
pf_calc_mss(struct pf_addr *addr, sa_family_t af, int rtableid, u_int16_t offer)
{
#ifdef INET
	struct nhop4_basic	nh4;
#endif /* INET */
#ifdef INET6
	struct nhop6_basic	nh6;
	struct in6_addr		dst6;
	uint32_t		scopeid;
#endif /* INET6 */
	int			 hlen = 0;
	uint16_t		 mss = 0;

	switch (af) {
#ifdef INET
	case AF_INET:
		hlen = sizeof(struct ip);
		if (fib4_lookup_nh_basic(rtableid, addr->v4, 0, 0, &nh4) == 0)
			mss = nh4.nh_mtu - hlen - sizeof(struct tcphdr);
		break;
#endif /* INET */
#ifdef INET6
	case AF_INET6:
		hlen = sizeof(struct ip6_hdr);
		in6_splitscope(&addr->v6, &dst6, &scopeid);
		if (fib6_lookup_nh_basic(rtableid, &dst6, scopeid, 0,0,&nh6)==0)
			mss = nh6.nh_mtu - hlen - sizeof(struct tcphdr);
		break;
#endif /* INET6 */
	}

	mss = max(V_tcp_mssdflt, mss);
	mss = min(mss, offer);
	mss = max(mss, 64);		/* sanity - at least max opt space */
	return (mss);
}

static u_int32_t
pf_tcp_iss(struct pf_pdesc *pd)
{
	MD5_CTX ctx;
	u_int32_t digest[4];

	if (V_pf_tcp_secret_init == 0) {
		read_random(&V_pf_tcp_secret, sizeof(V_pf_tcp_secret));
		MD5Init(&V_pf_tcp_secret_ctx);
		MD5Update(&V_pf_tcp_secret_ctx, V_pf_tcp_secret,
		    sizeof(V_pf_tcp_secret));
		V_pf_tcp_secret_init = 1;
	}

	ctx = V_pf_tcp_secret_ctx;

	MD5Update(&ctx, (char *)&pd->hdr.tcp->th_sport, sizeof(u_short));
	MD5Update(&ctx, (char *)&pd->hdr.tcp->th_dport, sizeof(u_short));
	if (pd->af == AF_INET6) {
		MD5Update(&ctx, (char *)&pd->src->v6, sizeof(struct in6_addr));
		MD5Update(&ctx, (char *)&pd->dst->v6, sizeof(struct in6_addr));
	} else {
		MD5Update(&ctx, (char *)&pd->src->v4, sizeof(struct in_addr));
		MD5Update(&ctx, (char *)&pd->dst->v4, sizeof(struct in_addr));
	}
	MD5Final((u_char *)digest, &ctx);
	V_pf_tcp_iss_off += 4096;
#define	ISN_RANDOM_INCREMENT (4096 - 1)
	return (digest[0] + (arc4random() & ISN_RANDOM_INCREMENT) +
	    V_pf_tcp_iss_off);
#undef	ISN_RANDOM_INCREMENT
}

static int
pf_test_rule(struct pf_rule **rm, struct pf_state **sm, int direction,
    struct pfi_kif *kif, struct mbuf *m, int off, struct pf_pdesc *pd,
    struct pf_rule **am, struct pf_ruleset **rsm, struct inpcb *inp)
{
	struct pf_rule		*nr = NULL;
	struct pf_addr		* const saddr = pd->src;
	struct pf_addr		* const daddr = pd->dst;
	sa_family_t		 af = pd->af;
	struct pf_rule		*r, *a = NULL;
	struct pf_ruleset	*ruleset = NULL;
	struct pf_src_node	*nsn = NULL;
	struct tcphdr		*th = pd->hdr.tcp;
	struct pf_state_key	*sk = NULL, *nk = NULL;
	u_short			 reason;
	int			 rewrite = 0, hdrlen = 0;
	int			 tag = -1, rtableid = -1;
	int			 asd = 0;
	int			 match = 0;
	int			 state_icmp = 0;
	u_int16_t		 sport = 0, dport = 0;
	u_int16_t		 bproto_sum = 0, bip_sum = 0;
	u_int8_t		 icmptype = 0, icmpcode = 0;
	struct pf_anchor_stackframe	anchor_stack[PF_ANCHOR_STACKSIZE];

	PF_RULES_RASSERT();

	if (inp != NULL) {
		INP_LOCK_ASSERT(inp);
		pd->lookup.uid = inp->inp_cred->cr_uid;
		pd->lookup.gid = inp->inp_cred->cr_groups[0];
		pd->lookup.done = 1;
	}

	switch (pd->proto) {
	case IPPROTO_TCP:
		sport = th->th_sport;
		dport = th->th_dport;
		hdrlen = sizeof(*th);
		break;
	case IPPROTO_UDP:
		sport = pd->hdr.udp->uh_sport;
		dport = pd->hdr.udp->uh_dport;
		hdrlen = sizeof(*pd->hdr.udp);
		break;
#ifdef INET
	case IPPROTO_ICMP:
		if (pd->af != AF_INET)
			break;
		sport = dport = pd->hdr.icmp->icmp_id;
		hdrlen = sizeof(*pd->hdr.icmp);
		icmptype = pd->hdr.icmp->icmp_type;
		icmpcode = pd->hdr.icmp->icmp_code;

		if (icmptype == ICMP_UNREACH ||
		    icmptype == ICMP_SOURCEQUENCH ||
		    icmptype == ICMP_REDIRECT ||
		    icmptype == ICMP_TIMXCEED ||
		    icmptype == ICMP_PARAMPROB)
			state_icmp++;
		break;
#endif /* INET */
#ifdef INET6
	case IPPROTO_ICMPV6:
		if (af != AF_INET6)
			break;
		sport = dport = pd->hdr.icmp6->icmp6_id;
		hdrlen = sizeof(*pd->hdr.icmp6);
		icmptype = pd->hdr.icmp6->icmp6_type;
		icmpcode = pd->hdr.icmp6->icmp6_code;

		if (icmptype == ICMP6_DST_UNREACH ||
		    icmptype == ICMP6_PACKET_TOO_BIG ||
		    icmptype == ICMP6_TIME_EXCEEDED ||
		    icmptype == ICMP6_PARAM_PROB)
			state_icmp++;
		break;
#endif /* INET6 */
	default:
		sport = dport = hdrlen = 0;
		break;
	}

	r = TAILQ_FIRST(pf_main_ruleset.rules[PF_RULESET_FILTER].active.ptr);

	/* check packet for BINAT/NAT/RDR */
	if ((nr = pf_get_translation(pd, m, off, direction, kif, &nsn, &sk,
	    &nk, saddr, daddr, sport, dport, anchor_stack)) != NULL) {
		KASSERT(sk != NULL, ("%s: null sk", __func__));
		KASSERT(nk != NULL, ("%s: null nk", __func__));

		if (pd->ip_sum)
			bip_sum = *pd->ip_sum;

		switch (pd->proto) {
		case IPPROTO_TCP:
			bproto_sum = th->th_sum;
			pd->proto_sum = &th->th_sum;

			if (PF_ANEQ(saddr, &nk->addr[pd->sidx], af) ||
			    nk->port[pd->sidx] != sport) {
				pf_change_ap(m, saddr, &th->th_sport, pd->ip_sum,
				    &th->th_sum, &nk->addr[pd->sidx],
				    nk->port[pd->sidx], 0, af);
				pd->sport = &th->th_sport;
				sport = th->th_sport;
			}

			if (PF_ANEQ(daddr, &nk->addr[pd->didx], af) ||
			    nk->port[pd->didx] != dport) {
				pf_change_ap(m, daddr, &th->th_dport, pd->ip_sum,
				    &th->th_sum, &nk->addr[pd->didx],
				    nk->port[pd->didx], 0, af);
				dport = th->th_dport;
				pd->dport = &th->th_dport;
			}
			rewrite++;
			break;
		case IPPROTO_UDP:
			bproto_sum = pd->hdr.udp->uh_sum;
			pd->proto_sum = &pd->hdr.udp->uh_sum;

			if (PF_ANEQ(saddr, &nk->addr[pd->sidx], af) ||
			    nk->port[pd->sidx] != sport) {
				pf_change_ap(m, saddr, &pd->hdr.udp->uh_sport,
				    pd->ip_sum, &pd->hdr.udp->uh_sum,
				    &nk->addr[pd->sidx],
				    nk->port[pd->sidx], 1, af);
				sport = pd->hdr.udp->uh_sport;
				pd->sport = &pd->hdr.udp->uh_sport;
			}

			if (PF_ANEQ(daddr, &nk->addr[pd->didx], af) ||
			    nk->port[pd->didx] != dport) {
				pf_change_ap(m, daddr, &pd->hdr.udp->uh_dport,
				    pd->ip_sum, &pd->hdr.udp->uh_sum,
				    &nk->addr[pd->didx],
				    nk->port[pd->didx], 1, af);
				dport = pd->hdr.udp->uh_dport;
				pd->dport = &pd->hdr.udp->uh_dport;
			}
			rewrite++;
			break;
#ifdef INET
		case IPPROTO_ICMP:
			nk->port[0] = nk->port[1];
			if (PF_ANEQ(saddr, &nk->addr[pd->sidx], AF_INET))
				pf_change_a(&saddr->v4.s_addr, pd->ip_sum,
				    nk->addr[pd->sidx].v4.s_addr, 0);

			if (PF_ANEQ(daddr, &nk->addr[pd->didx], AF_INET))
				pf_change_a(&daddr->v4.s_addr, pd->ip_sum,
				    nk->addr[pd->didx].v4.s_addr, 0);

			if (nk->port[1] != pd->hdr.icmp->icmp_id) {
				pd->hdr.icmp->icmp_cksum = pf_cksum_fixup(
				    pd->hdr.icmp->icmp_cksum, sport,
				    nk->port[1], 0);
				pd->hdr.icmp->icmp_id = nk->port[1];
				pd->sport = &pd->hdr.icmp->icmp_id;
			}
			m_copyback(m, off, ICMP_MINLEN, (caddr_t)pd->hdr.icmp);
			break;
#endif /* INET */
#ifdef INET6
		case IPPROTO_ICMPV6:
			nk->port[0] = nk->port[1];
			if (PF_ANEQ(saddr, &nk->addr[pd->sidx], AF_INET6))
				pf_change_a6(saddr, &pd->hdr.icmp6->icmp6_cksum,
				    &nk->addr[pd->sidx], 0);

			if (PF_ANEQ(daddr, &nk->addr[pd->didx], AF_INET6))
				pf_change_a6(daddr, &pd->hdr.icmp6->icmp6_cksum,
				    &nk->addr[pd->didx], 0);
			rewrite++;
			break;
#endif /* INET */
		default:
			switch (af) {
#ifdef INET
			case AF_INET:
				if (PF_ANEQ(saddr,
				    &nk->addr[pd->sidx], AF_INET))
					pf_change_a(&saddr->v4.s_addr,
					    pd->ip_sum,
					    nk->addr[pd->sidx].v4.s_addr, 0);

				if (PF_ANEQ(daddr,
				    &nk->addr[pd->didx], AF_INET))
					pf_change_a(&daddr->v4.s_addr,
					    pd->ip_sum,
					    nk->addr[pd->didx].v4.s_addr, 0);
				break;
#endif /* INET */
#ifdef INET6
			case AF_INET6:
				if (PF_ANEQ(saddr,
				    &nk->addr[pd->sidx], AF_INET6))
					PF_ACPY(saddr, &nk->addr[pd->sidx], af);

				if (PF_ANEQ(daddr,
				    &nk->addr[pd->didx], AF_INET6))
					PF_ACPY(daddr, &nk->addr[pd->didx], af);
				break;
#endif /* INET */
			}
			break;
		}
		if (nr->natpass)
			r = NULL;
		pd->nat_rule = nr;
	}

	while (r != NULL) {
		r->evaluations++;
		if (pfi_kif_match(r->kif, kif) == r->ifnot)
			r = r->skip[PF_SKIP_IFP].ptr;
		else if (r->direction && r->direction != direction)
			r = r->skip[PF_SKIP_DIR].ptr;
		else if (r->af && r->af != af)
			r = r->skip[PF_SKIP_AF].ptr;
		else if (r->proto && r->proto != pd->proto)
			r = r->skip[PF_SKIP_PROTO].ptr;
		else if (PF_MISMATCHAW(&r->src.addr, saddr, af,
		    r->src.neg, kif, M_GETFIB(m)))
			r = r->skip[PF_SKIP_SRC_ADDR].ptr;
		/* tcp/udp only. port_op always 0 in other cases */
		else if (r->src.port_op && !pf_match_port(r->src.port_op,
		    r->src.port[0], r->src.port[1], sport))
			r = r->skip[PF_SKIP_SRC_PORT].ptr;
		else if (PF_MISMATCHAW(&r->dst.addr, daddr, af,
		    r->dst.neg, NULL, M_GETFIB(m)))
			r = r->skip[PF_SKIP_DST_ADDR].ptr;
		/* tcp/udp only. port_op always 0 in other cases */
		else if (r->dst.port_op && !pf_match_port(r->dst.port_op,
		    r->dst.port[0], r->dst.port[1], dport))
			r = r->skip[PF_SKIP_DST_PORT].ptr;
		/* icmp only. type always 0 in other cases */
		else if (r->type && r->type != icmptype + 1)
			r = TAILQ_NEXT(r, entries);
		/* icmp only. type always 0 in other cases */
		else if (r->code && r->code != icmpcode + 1)
			r = TAILQ_NEXT(r, entries);
		else if (r->tos && !(r->tos == pd->tos))
			r = TAILQ_NEXT(r, entries);
		else if (r->rule_flag & PFRULE_FRAGMENT)
			r = TAILQ_NEXT(r, entries);
		else if (pd->proto == IPPROTO_TCP &&
		    (r->flagset & th->th_flags) != r->flags)
			r = TAILQ_NEXT(r, entries);
		/* tcp/udp only. uid.op always 0 in other cases */
		else if (r->uid.op && (pd->lookup.done || (pd->lookup.done =
		    pf_socket_lookup(direction, pd, m), 1)) &&
		    !pf_match_uid(r->uid.op, r->uid.uid[0], r->uid.uid[1],
		    pd->lookup.uid))
			r = TAILQ_NEXT(r, entries);
		/* tcp/udp only. gid.op always 0 in other cases */
		else if (r->gid.op && (pd->lookup.done || (pd->lookup.done =
		    pf_socket_lookup(direction, pd, m), 1)) &&
		    !pf_match_gid(r->gid.op, r->gid.gid[0], r->gid.gid[1],
		    pd->lookup.gid))
			r = TAILQ_NEXT(r, entries);
		else if (r->prio &&
		    !pf_match_ieee8021q_pcp(r->prio, m))
			r = TAILQ_NEXT(r, entries);
		else if (r->prob &&
		    r->prob <= arc4random())
			r = TAILQ_NEXT(r, entries);
		else if (r->match_tag && !pf_match_tag(m, r, &tag,
		    pd->pf_mtag ? pd->pf_mtag->tag : 0))
			r = TAILQ_NEXT(r, entries);
		else if (r->os_fingerprint != PF_OSFP_ANY &&
		    (pd->proto != IPPROTO_TCP || !pf_osfp_match(
		    pf_osfp_fingerprint(pd, m, off, th),
		    r->os_fingerprint)))
			r = TAILQ_NEXT(r, entries);
		else {
			if (r->tag)
				tag = r->tag;
			if (r->rtableid >= 0)
				rtableid = r->rtableid;
			if (r->anchor == NULL) {
				match = 1;
				*rm = r;
				*am = a;
				*rsm = ruleset;
				if ((*rm)->quick)
					break;
				r = TAILQ_NEXT(r, entries);
			} else
				pf_step_into_anchor(anchor_stack, &asd,
				    &ruleset, PF_RULESET_FILTER, &r, &a,
				    &match);
		}
		if (r == NULL && pf_step_out_of_anchor(anchor_stack, &asd,
		    &ruleset, PF_RULESET_FILTER, &r, &a, &match))
			break;
	}
	r = *rm;
	a = *am;
	ruleset = *rsm;

	REASON_SET(&reason, PFRES_MATCH);

	if (r->log || (nr != NULL && nr->log)) {
		if (rewrite)
			m_copyback(m, off, hdrlen, pd->hdr.any);
		PFLOG_PACKET(kif, m, af, direction, reason, r->log ? r : nr, a,
		    ruleset, pd, 1);
	}

	if ((r->action == PF_DROP) &&
	    ((r->rule_flag & PFRULE_RETURNRST) ||
	    (r->rule_flag & PFRULE_RETURNICMP) ||
	    (r->rule_flag & PFRULE_RETURN))) {
		pf_return(r, nr, pd, sk, off, m, th, kif, bproto_sum,
		    bip_sum, hdrlen, &reason);
	}

	if (r->action == PF_DROP)
		goto cleanup;

	if (tag > 0 && pf_tag_packet(m, pd, tag)) {
		REASON_SET(&reason, PFRES_MEMORY);
		goto cleanup;
	}
	if (rtableid >= 0)
		M_SETFIB(m, rtableid);

	if (!state_icmp && (r->keep_state || nr != NULL ||
	    (pd->flags & PFDESC_TCP_NORM))) {
		int action;
		action = pf_create_state(r, nr, a, pd, nsn, nk, sk, m, off,
		    sport, dport, &rewrite, kif, sm, tag, bproto_sum, bip_sum,
		    hdrlen);
		if (action != PF_PASS) {
			if (action == PF_DROP &&
			    (r->rule_flag & PFRULE_RETURN))
				pf_return(r, nr, pd, sk, off, m, th, kif,
				    bproto_sum, bip_sum, hdrlen, &reason);
			return (action);
		}
	} else {
		if (sk != NULL)
			uma_zfree(V_pf_state_key_z, sk);
		if (nk != NULL)
			uma_zfree(V_pf_state_key_z, nk);
	}

	/* copy back packet headers if we performed NAT operations */
	if (rewrite)
		m_copyback(m, off, hdrlen, pd->hdr.any);

	if (*sm != NULL && !((*sm)->state_flags & PFSTATE_NOSYNC) &&
	    direction == PF_OUT &&
	    V_pfsync_defer_ptr != NULL && V_pfsync_defer_ptr(*sm, m))
		/*
		 * We want the state created, but we dont
		 * want to send this in case a partner
		 * firewall has to know about it to allow
		 * replies through it.
		 */
		return (PF_DEFER);

	return (PF_PASS);

cleanup:
	if (sk != NULL)
		uma_zfree(V_pf_state_key_z, sk);
	if (nk != NULL)
		uma_zfree(V_pf_state_key_z, nk);
	return (PF_DROP);
}

static int
pf_create_state(struct pf_rule *r, struct pf_rule *nr, struct pf_rule *a,
    struct pf_pdesc *pd, struct pf_src_node *nsn, struct pf_state_key *nk,
    struct pf_state_key *sk, struct mbuf *m, int off, u_int16_t sport,
    u_int16_t dport, int *rewrite, struct pfi_kif *kif, struct pf_state **sm,
    int tag, u_int16_t bproto_sum, u_int16_t bip_sum, int hdrlen)
{
	struct pf_state		*s = NULL;
	struct pf_src_node	*sn = NULL;
	struct tcphdr		*th = pd->hdr.tcp;
	u_int16_t		 mss = V_tcp_mssdflt;
	u_short			 reason;

	/* check maximums */
	if (r->max_states &&
	    (counter_u64_fetch(r->states_cur) >= r->max_states)) {
		counter_u64_add(V_pf_status.lcounters[LCNT_STATES], 1);
		REASON_SET(&reason, PFRES_MAXSTATES);
		goto csfailed;
	}
	/* src node for filter rule */
	if ((r->rule_flag & PFRULE_SRCTRACK ||
	    r->rpool.opts & PF_POOL_STICKYADDR) &&
	    pf_insert_src_node(&sn, r, pd->src, pd->af) != 0) {
		REASON_SET(&reason, PFRES_SRCLIMIT);
		goto csfailed;
	}
	/* src node for translation rule */
	if (nr != NULL && (nr->rpool.opts & PF_POOL_STICKYADDR) &&
	    pf_insert_src_node(&nsn, nr, &sk->addr[pd->sidx], pd->af)) {
		REASON_SET(&reason, PFRES_SRCLIMIT);
		goto csfailed;
	}
	s = uma_zalloc(V_pf_state_z, M_NOWAIT | M_ZERO);
	if (s == NULL) {
		REASON_SET(&reason, PFRES_MEMORY);
		goto csfailed;
	}
	s->rule.ptr = r;
	s->nat_rule.ptr = nr;
	s->anchor.ptr = a;
	STATE_INC_COUNTERS(s);
	if (r->allow_opts)
		s->state_flags |= PFSTATE_ALLOWOPTS;
	if (r->rule_flag & PFRULE_STATESLOPPY)
		s->state_flags |= PFSTATE_SLOPPY;
	s->log = r->log & PF_LOG_ALL;
	s->sync_state = PFSYNC_S_NONE;
	if (nr != NULL)
		s->log |= nr->log & PF_LOG_ALL;
	switch (pd->proto) {
	case IPPROTO_TCP:
		s->src.seqlo = ntohl(th->th_seq);
		s->src.seqhi = s->src.seqlo + pd->p_len + 1;
		if ((th->th_flags & (TH_SYN|TH_ACK)) == TH_SYN &&
		    r->keep_state == PF_STATE_MODULATE) {
			/* Generate sequence number modulator */
			if ((s->src.seqdiff = pf_tcp_iss(pd) - s->src.seqlo) ==
			    0)
				s->src.seqdiff = 1;
			pf_change_proto_a(m, &th->th_seq, &th->th_sum,
			    htonl(s->src.seqlo + s->src.seqdiff), 0);
			*rewrite = 1;
		} else
			s->src.seqdiff = 0;
		if (th->th_flags & TH_SYN) {
			s->src.seqhi++;
			s->src.wscale = pf_get_wscale(m, off,
			    th->th_off, pd->af);
		}
		s->src.max_win = MAX(ntohs(th->th_win), 1);
		if (s->src.wscale & PF_WSCALE_MASK) {
			/* Remove scale factor from initial window */
			int win = s->src.max_win;
			win += 1 << (s->src.wscale & PF_WSCALE_MASK);
			s->src.max_win = (win - 1) >>
			    (s->src.wscale & PF_WSCALE_MASK);
		}
		if (th->th_flags & TH_FIN)
			s->src.seqhi++;
		s->dst.seqhi = 1;
		s->dst.max_win = 1;
		s->src.state = TCPS_SYN_SENT;
		s->dst.state = TCPS_CLOSED;
		s->timeout = PFTM_TCP_FIRST_PACKET;
		break;
	case IPPROTO_UDP:
		s->src.state = PFUDPS_SINGLE;
		s->dst.state = PFUDPS_NO_TRAFFIC;
		s->timeout = PFTM_UDP_FIRST_PACKET;
		break;
	case IPPROTO_ICMP:
#ifdef INET6
	case IPPROTO_ICMPV6:
#endif
		s->timeout = PFTM_ICMP_FIRST_PACKET;
		break;
	default:
		s->src.state = PFOTHERS_SINGLE;
		s->dst.state = PFOTHERS_NO_TRAFFIC;
		s->timeout = PFTM_OTHER_FIRST_PACKET;
	}

	if (r->rt) {
		if (pf_map_addr(pd->af, r, pd->src, &s->rt_addr, NULL, &sn)) {
			REASON_SET(&reason, PFRES_MAPFAILED);
			pf_src_tree_remove_state(s);
			STATE_DEC_COUNTERS(s);
			uma_zfree(V_pf_state_z, s);
			goto csfailed;
		}
		s->rt_kif = r->rpool.cur->kif;
	}

	s->creation = time_uptime;
	s->expire = time_uptime;

	if (sn != NULL)
		s->src_node = sn;
	if (nsn != NULL) {
		/* XXX We only modify one side for now. */
		PF_ACPY(&nsn->raddr, &nk->addr[1], pd->af);
		s->nat_src_node = nsn;
	}
	if (pd->proto == IPPROTO_TCP) {
		if ((pd->flags & PFDESC_TCP_NORM) && pf_normalize_tcp_init(m,
		    off, pd, th, &s->src, &s->dst)) {
			REASON_SET(&reason, PFRES_MEMORY);
			pf_src_tree_remove_state(s);
			STATE_DEC_COUNTERS(s);
			uma_zfree(V_pf_state_z, s);
			return (PF_DROP);
		}
		if ((pd->flags & PFDESC_TCP_NORM) && s->src.scrub &&
		    pf_normalize_tcp_stateful(m, off, pd, &reason, th, s,
		    &s->src, &s->dst, rewrite)) {
			/* This really shouldn't happen!!! */
			DPFPRINTF(PF_DEBUG_URGENT,
			    ("pf_normalize_tcp_stateful failed on first pkt"));
			pf_normalize_tcp_cleanup(s);
			pf_src_tree_remove_state(s);
			STATE_DEC_COUNTERS(s);
			uma_zfree(V_pf_state_z, s);
			return (PF_DROP);
		}
	}
	s->direction = pd->dir;

	/*
	 * sk/nk could already been setup by pf_get_translation().
	 */
	if (nr == NULL) {
		KASSERT((sk == NULL && nk == NULL), ("%s: nr %p sk %p, nk %p",
		    __func__, nr, sk, nk));
		sk = pf_state_key_setup(pd, pd->src, pd->dst, sport, dport);
		if (sk == NULL)
			goto csfailed;
		nk = sk;
	} else
		KASSERT((sk != NULL && nk != NULL), ("%s: nr %p sk %p, nk %p",
		    __func__, nr, sk, nk));

	/* Swap sk/nk for PF_OUT. */
	if (pf_state_insert(BOUND_IFACE(r, kif),
	    (pd->dir == PF_IN) ? sk : nk,
	    (pd->dir == PF_IN) ? nk : sk, s)) {
		if (pd->proto == IPPROTO_TCP)
			pf_normalize_tcp_cleanup(s);
		REASON_SET(&reason, PFRES_STATEINS);
		pf_src_tree_remove_state(s);
		STATE_DEC_COUNTERS(s);
		uma_zfree(V_pf_state_z, s);
		return (PF_DROP);
	} else
		*sm = s;

	if (tag > 0)
		s->tag = tag;
	if (pd->proto == IPPROTO_TCP && (th->th_flags & (TH_SYN|TH_ACK)) ==
	    TH_SYN && r->keep_state == PF_STATE_SYNPROXY) {
		s->src.state = PF_TCPS_PROXY_SRC;
		/* undo NAT changes, if they have taken place */
		if (nr != NULL) {
			struct pf_state_key *skt = s->key[PF_SK_WIRE];
			if (pd->dir == PF_OUT)
				skt = s->key[PF_SK_STACK];
			PF_ACPY(pd->src, &skt->addr[pd->sidx], pd->af);
			PF_ACPY(pd->dst, &skt->addr[pd->didx], pd->af);
			if (pd->sport)
				*pd->sport = skt->port[pd->sidx];
			if (pd->dport)
				*pd->dport = skt->port[pd->didx];
			if (pd->proto_sum)
				*pd->proto_sum = bproto_sum;
			if (pd->ip_sum)
				*pd->ip_sum = bip_sum;
			m_copyback(m, off, hdrlen, pd->hdr.any);
		}
		s->src.seqhi = htonl(arc4random());
		/* Find mss option */
		int rtid = M_GETFIB(m);
		mss = pf_get_mss(m, off, th->th_off, pd->af);
		mss = pf_calc_mss(pd->src, pd->af, rtid, mss);
		mss = pf_calc_mss(pd->dst, pd->af, rtid, mss);
		s->src.mss = mss;
		pf_send_tcp(NULL, r, pd->af, pd->dst, pd->src, th->th_dport,
		    th->th_sport, s->src.seqhi, ntohl(th->th_seq) + 1,
		    TH_SYN|TH_ACK, 0, s->src.mss, 0, 1, 0, NULL);
		REASON_SET(&reason, PFRES_SYNPROXY);
		return (PF_SYNPROXY_DROP);
	}

	return (PF_PASS);

csfailed:
	if (sk != NULL)
		uma_zfree(V_pf_state_key_z, sk);
	if (nk != NULL)
		uma_zfree(V_pf_state_key_z, nk);

	if (sn != NULL) {
		struct pf_srchash *sh;

		sh = &V_pf_srchash[pf_hashsrc(&sn->addr, sn->af)];
		PF_HASHROW_LOCK(sh);
		if (--sn->states == 0 && sn->expire == 0) {
			pf_unlink_src_node(sn);
			uma_zfree(V_pf_sources_z, sn);
			counter_u64_add(
			    V_pf_status.scounters[SCNT_SRC_NODE_REMOVALS], 1);
		}
		PF_HASHROW_UNLOCK(sh);
	}

	if (nsn != sn && nsn != NULL) {
		struct pf_srchash *sh;

		sh = &V_pf_srchash[pf_hashsrc(&nsn->addr, nsn->af)];
		PF_HASHROW_LOCK(sh);
		if (--nsn->states == 0 && nsn->expire == 0) {
			pf_unlink_src_node(nsn);
			uma_zfree(V_pf_sources_z, nsn);
			counter_u64_add(
			    V_pf_status.scounters[SCNT_SRC_NODE_REMOVALS], 1);
		}
		PF_HASHROW_UNLOCK(sh);
	}

	return (PF_DROP);
}

static int
pf_test_fragment(struct pf_rule **rm, int direction, struct pfi_kif *kif,
    struct mbuf *m, void *h, struct pf_pdesc *pd, struct pf_rule **am,
    struct pf_ruleset **rsm)
{
	struct pf_rule		*r, *a = NULL;
	struct pf_ruleset	*ruleset = NULL;
	sa_family_t		 af = pd->af;
	u_short			 reason;
	int			 tag = -1;
	int			 asd = 0;
	int			 match = 0;
	struct pf_anchor_stackframe	anchor_stack[PF_ANCHOR_STACKSIZE];

	PF_RULES_RASSERT();

	r = TAILQ_FIRST(pf_main_ruleset.rules[PF_RULESET_FILTER].active.ptr);
	while (r != NULL) {
		r->evaluations++;
		if (pfi_kif_match(r->kif, kif) == r->ifnot)
			r = r->skip[PF_SKIP_IFP].ptr;
		else if (r->direction && r->direction != direction)
			r = r->skip[PF_SKIP_DIR].ptr;
		else if (r->af && r->af != af)
			r = r->skip[PF_SKIP_AF].ptr;
		else if (r->proto && r->proto != pd->proto)
			r = r->skip[PF_SKIP_PROTO].ptr;
		else if (PF_MISMATCHAW(&r->src.addr, pd->src, af,
		    r->src.neg, kif, M_GETFIB(m)))
			r = r->skip[PF_SKIP_SRC_ADDR].ptr;
		else if (PF_MISMATCHAW(&r->dst.addr, pd->dst, af,
		    r->dst.neg, NULL, M_GETFIB(m)))
			r = r->skip[PF_SKIP_DST_ADDR].ptr;
		else if (r->tos && !(r->tos == pd->tos))
			r = TAILQ_NEXT(r, entries);
		else if (r->os_fingerprint != PF_OSFP_ANY)
			r = TAILQ_NEXT(r, entries);
		else if (pd->proto == IPPROTO_UDP &&
		    (r->src.port_op || r->dst.port_op))
			r = TAILQ_NEXT(r, entries);
		else if (pd->proto == IPPROTO_TCP &&
		    (r->src.port_op || r->dst.port_op || r->flagset))
			r = TAILQ_NEXT(r, entries);
		else if ((pd->proto == IPPROTO_ICMP ||
		    pd->proto == IPPROTO_ICMPV6) &&
		    (r->type || r->code))
			r = TAILQ_NEXT(r, entries);
		else if (r->prio &&
		    !pf_match_ieee8021q_pcp(r->prio, m))
			r = TAILQ_NEXT(r, entries);
		else if (r->prob && r->prob <=
		    (arc4random() % (UINT_MAX - 1) + 1))
			r = TAILQ_NEXT(r, entries);
		else if (r->match_tag && !pf_match_tag(m, r, &tag,
		    pd->pf_mtag ? pd->pf_mtag->tag : 0))
			r = TAILQ_NEXT(r, entries);
		else {
			if (r->anchor == NULL) {
				match = 1;
				*rm = r;
				*am = a;
				*rsm = ruleset;
				if ((*rm)->quick)
					break;
				r = TAILQ_NEXT(r, entries);
			} else
				pf_step_into_anchor(anchor_stack, &asd,
				    &ruleset, PF_RULESET_FILTER, &r, &a,
				    &match);
		}
		if (r == NULL && pf_step_out_of_anchor(anchor_stack, &asd,
		    &ruleset, PF_RULESET_FILTER, &r, &a, &match))
			break;
	}
	r = *rm;
	a = *am;
	ruleset = *rsm;

	REASON_SET(&reason, PFRES_MATCH);

	if (r->log)
		PFLOG_PACKET(kif, m, af, direction, reason, r, a, ruleset, pd,
		    1);

	if (r->action != PF_PASS)
		return (PF_DROP);

	if (tag > 0 && pf_tag_packet(m, pd, tag)) {
		REASON_SET(&reason, PFRES_MEMORY);
		return (PF_DROP);
	}

	return (PF_PASS);
}

static int
pf_tcp_track_full(struct pf_state_peer *src, struct pf_state_peer *dst,
	struct pf_state **state, struct pfi_kif *kif, struct mbuf *m, int off,
	struct pf_pdesc *pd, u_short *reason, int *copyback)
{
	struct tcphdr		*th = pd->hdr.tcp;
	u_int16_t		 win = ntohs(th->th_win);
	u_int32_t		 ack, end, seq, orig_seq;
	u_int8_t		 sws, dws;
	int			 ackskew;

	if (src->wscale && dst->wscale && !(th->th_flags & TH_SYN)) {
		sws = src->wscale & PF_WSCALE_MASK;
		dws = dst->wscale & PF_WSCALE_MASK;
	} else
		sws = dws = 0;

	/*
	 * Sequence tracking algorithm from Guido van Rooij's paper:
	 *   http://www.madison-gurkha.com/publications/tcp_filtering/
	 *	tcp_filtering.ps
	 */

	orig_seq = seq = ntohl(th->th_seq);
	if (src->seqlo == 0) {
		/* First packet from this end. Set its state */

		if ((pd->flags & PFDESC_TCP_NORM || dst->scrub) &&
		    src->scrub == NULL) {
			if (pf_normalize_tcp_init(m, off, pd, th, src, dst)) {
				REASON_SET(reason, PFRES_MEMORY);
				return (PF_DROP);
			}
		}

		/* Deferred generation of sequence number modulator */
		if (dst->seqdiff && !src->seqdiff) {
			/* use random iss for the TCP server */
			while ((src->seqdiff = arc4random() - seq) == 0)
				;
			ack = ntohl(th->th_ack) - dst->seqdiff;
			pf_change_proto_a(m, &th->th_seq, &th->th_sum, htonl(seq +
			    src->seqdiff), 0);
			pf_change_proto_a(m, &th->th_ack, &th->th_sum, htonl(ack), 0);
			*copyback = 1;
		} else {
			ack = ntohl(th->th_ack);
		}

		end = seq + pd->p_len;
		if (th->th_flags & TH_SYN) {
			end++;
			if (dst->wscale & PF_WSCALE_FLAG) {
				src->wscale = pf_get_wscale(m, off, th->th_off,
				    pd->af);
				if (src->wscale & PF_WSCALE_FLAG) {
					/* Remove scale factor from initial
					 * window */
					sws = src->wscale & PF_WSCALE_MASK;
					win = ((u_int32_t)win + (1 << sws) - 1)
					    >> sws;
					dws = dst->wscale & PF_WSCALE_MASK;
				} else {
					/* fixup other window */
					dst->max_win <<= dst->wscale &
					    PF_WSCALE_MASK;
					/* in case of a retrans SYN|ACK */
					dst->wscale = 0;
				}
			}
		}
		if (th->th_flags & TH_FIN)
			end++;

		src->seqlo = seq;
		if (src->state < TCPS_SYN_SENT)
			src->state = TCPS_SYN_SENT;

		/*
		 * May need to slide the window (seqhi may have been set by
		 * the crappy stack check or if we picked up the connection
		 * after establishment)
		 */
		if (src->seqhi == 1 ||
		    SEQ_GEQ(end + MAX(1, dst->max_win << dws), src->seqhi))
			src->seqhi = end + MAX(1, dst->max_win << dws);
		if (win > src->max_win)
			src->max_win = win;

	} else {
		ack = ntohl(th->th_ack) - dst->seqdiff;
		if (src->seqdiff) {
			/* Modulate sequence numbers */
			pf_change_proto_a(m, &th->th_seq, &th->th_sum, htonl(seq +
			    src->seqdiff), 0);
			pf_change_proto_a(m, &th->th_ack, &th->th_sum, htonl(ack), 0);
			*copyback = 1;
		}
		end = seq + pd->p_len;
		if (th->th_flags & TH_SYN)
			end++;
		if (th->th_flags & TH_FIN)
			end++;
	}

	if ((th->th_flags & TH_ACK) == 0) {
		/* Let it pass through the ack skew check */
		ack = dst->seqlo;
	} else if ((ack == 0 &&
	    (th->th_flags & (TH_ACK|TH_RST)) == (TH_ACK|TH_RST)) ||
	    /* broken tcp stacks do not set ack */
	    (dst->state < TCPS_SYN_SENT)) {
		/*
		 * Many stacks (ours included) will set the ACK number in an
		 * FIN|ACK if the SYN times out -- no sequence to ACK.
		 */
		ack = dst->seqlo;
	}

	if (seq == end) {
		/* Ease sequencing restrictions on no data packets */
		seq = src->seqlo;
		end = seq;
	}

	ackskew = dst->seqlo - ack;


	/*
	 * Need to demodulate the sequence numbers in any TCP SACK options
	 * (Selective ACK). We could optionally validate the SACK values
	 * against the current ACK window, either forwards or backwards, but
	 * I'm not confident that SACK has been implemented properly
	 * everywhere. It wouldn't surprise me if several stacks accidentally
	 * SACK too far backwards of previously ACKed data. There really aren't
	 * any security implications of bad SACKing unless the target stack
	 * doesn't validate the option length correctly. Someone trying to
	 * spoof into a TCP connection won't bother blindly sending SACK
	 * options anyway.
	 */
	if (dst->seqdiff && (th->th_off << 2) > sizeof(struct tcphdr)) {
		if (pf_modulate_sack(m, off, pd, th, dst))
			*copyback = 1;
	}


#define	MAXACKWINDOW (0xffff + 1500)	/* 1500 is an arbitrary fudge factor */
	if (SEQ_GEQ(src->seqhi, end) &&
	    /* Last octet inside other's window space */
	    SEQ_GEQ(seq, src->seqlo - (dst->max_win << dws)) &&
	    /* Retrans: not more than one window back */
	    (ackskew >= -MAXACKWINDOW) &&
	    /* Acking not more than one reassembled fragment backwards */
	    (ackskew <= (MAXACKWINDOW << sws)) &&
	    /* Acking not more than one window forward */
	    ((th->th_flags & TH_RST) == 0 || orig_seq == src->seqlo ||
	    (orig_seq == src->seqlo + 1) || (orig_seq + 1 == src->seqlo) ||
	    (pd->flags & PFDESC_IP_REAS) == 0)) {
	    /* Require an exact/+1 sequence match on resets when possible */

		if (dst->scrub || src->scrub) {
			if (pf_normalize_tcp_stateful(m, off, pd, reason, th,
			    *state, src, dst, copyback))
				return (PF_DROP);
		}

		/* update max window */
		if (src->max_win < win)
			src->max_win = win;
		/* synchronize sequencing */
		if (SEQ_GT(end, src->seqlo))
			src->seqlo = end;
		/* slide the window of what the other end can send */
		if (SEQ_GEQ(ack + (win << sws), dst->seqhi))
			dst->seqhi = ack + MAX((win << sws), 1);


		/* update states */
		if (th->th_flags & TH_SYN)
			if (src->state < TCPS_SYN_SENT)
				src->state = TCPS_SYN_SENT;
		if (th->th_flags & TH_FIN)
			if (src->state < TCPS_CLOSING)
				src->state = TCPS_CLOSING;
		if (th->th_flags & TH_ACK) {
			if (dst->state == TCPS_SYN_SENT) {
				dst->state = TCPS_ESTABLISHED;
				if (src->state == TCPS_ESTABLISHED &&
				    (*state)->src_node != NULL &&
				    pf_src_connlimit(state)) {
					REASON_SET(reason, PFRES_SRCLIMIT);
					return (PF_DROP);
				}
			} else if (dst->state == TCPS_CLOSING)
				dst->state = TCPS_FIN_WAIT_2;
		}
		if (th->th_flags & TH_RST)
			src->state = dst->state = TCPS_TIME_WAIT;

		/* update expire time */
		(*state)->expire = time_uptime;
		if (src->state >= TCPS_FIN_WAIT_2 &&
		    dst->state >= TCPS_FIN_WAIT_2)
			(*state)->timeout = PFTM_TCP_CLOSED;
		else if (src->state >= TCPS_CLOSING &&
		    dst->state >= TCPS_CLOSING)
			(*state)->timeout = PFTM_TCP_FIN_WAIT;
		else if (src->state < TCPS_ESTABLISHED ||
		    dst->state < TCPS_ESTABLISHED)
			(*state)->timeout = PFTM_TCP_OPENING;
		else if (src->state >= TCPS_CLOSING ||
		    dst->state >= TCPS_CLOSING)
			(*state)->timeout = PFTM_TCP_CLOSING;
		else
			(*state)->timeout = PFTM_TCP_ESTABLISHED;

		/* Fall through to PASS packet */

	} else if ((dst->state < TCPS_SYN_SENT ||
		dst->state >= TCPS_FIN_WAIT_2 ||
		src->state >= TCPS_FIN_WAIT_2) &&
	    SEQ_GEQ(src->seqhi + MAXACKWINDOW, end) &&
	    /* Within a window forward of the originating packet */
	    SEQ_GEQ(seq, src->seqlo - MAXACKWINDOW)) {
	    /* Within a window backward of the originating packet */

		/*
		 * This currently handles three situations:
		 *  1) Stupid stacks will shotgun SYNs before their peer
		 *     replies.
		 *  2) When PF catches an already established stream (the
		 *     firewall rebooted, the state table was flushed, routes
		 *     changed...)
		 *  3) Packets get funky immediately after the connection
		 *     closes (this should catch Solaris spurious ACK|FINs
		 *     that web servers like to spew after a close)
		 *
		 * This must be a little more careful than the above code
		 * since packet floods will also be caught here. We don't
		 * update the TTL here to mitigate the damage of a packet
		 * flood and so the same code can handle awkward establishment
		 * and a loosened connection close.
		 * In the establishment case, a correct peer response will
		 * validate the connection, go through the normal state code
		 * and keep updating the state TTL.
		 */

		if (V_pf_status.debug >= PF_DEBUG_MISC) {
			printf("pf: loose state match: ");
			pf_print_state(*state);
			pf_print_flags(th->th_flags);
			printf(" seq=%u (%u) ack=%u len=%u ackskew=%d "
			    "pkts=%llu:%llu dir=%s,%s\n", seq, orig_seq, ack,
			    pd->p_len, ackskew, (unsigned long long)(*state)->packets[0],
			    (unsigned long long)(*state)->packets[1],
			    pd->dir == PF_IN ? "in" : "out",
			    pd->dir == (*state)->direction ? "fwd" : "rev");
		}

		if (dst->scrub || src->scrub) {
			if (pf_normalize_tcp_stateful(m, off, pd, reason, th,
			    *state, src, dst, copyback))
				return (PF_DROP);
		}

		/* update max window */
		if (src->max_win < win)
			src->max_win = win;
		/* synchronize sequencing */
		if (SEQ_GT(end, src->seqlo))
			src->seqlo = end;
		/* slide the window of what the other end can send */
		if (SEQ_GEQ(ack + (win << sws), dst->seqhi))
			dst->seqhi = ack + MAX((win << sws), 1);

		/*
		 * Cannot set dst->seqhi here since this could be a shotgunned
		 * SYN and not an already established connection.
		 */

		if (th->th_flags & TH_FIN)
			if (src->state < TCPS_CLOSING)
				src->state = TCPS_CLOSING;
		if (th->th_flags & TH_RST)
			src->state = dst->state = TCPS_TIME_WAIT;

		/* Fall through to PASS packet */

	} else {
		if ((*state)->dst.state == TCPS_SYN_SENT &&
		    (*state)->src.state == TCPS_SYN_SENT) {
			/* Send RST for state mismatches during handshake */
			if (!(th->th_flags & TH_RST))
				pf_send_tcp(NULL, (*state)->rule.ptr, pd->af,
				    pd->dst, pd->src, th->th_dport,
				    th->th_sport, ntohl(th->th_ack), 0,
				    TH_RST, 0, 0,
				    (*state)->rule.ptr->return_ttl, 1, 0,
				    kif->pfik_ifp);
			src->seqlo = 0;
			src->seqhi = 1;
			src->max_win = 1;
		} else if (V_pf_status.debug >= PF_DEBUG_MISC) {
			printf("pf: BAD state: ");
			pf_print_state(*state);
			pf_print_flags(th->th_flags);
			printf(" seq=%u (%u) ack=%u len=%u ackskew=%d "
			    "pkts=%llu:%llu dir=%s,%s\n",
			    seq, orig_seq, ack, pd->p_len, ackskew,
			    (unsigned long long)(*state)->packets[0],
			    (unsigned long long)(*state)->packets[1],
			    pd->dir == PF_IN ? "in" : "out",
			    pd->dir == (*state)->direction ? "fwd" : "rev");
			printf("pf: State failure on: %c %c %c %c | %c %c\n",
			    SEQ_GEQ(src->seqhi, end) ? ' ' : '1',
			    SEQ_GEQ(seq, src->seqlo - (dst->max_win << dws)) ?
			    ' ': '2',
			    (ackskew >= -MAXACKWINDOW) ? ' ' : '3',
			    (ackskew <= (MAXACKWINDOW << sws)) ? ' ' : '4',
			    SEQ_GEQ(src->seqhi + MAXACKWINDOW, end) ?' ' :'5',
			    SEQ_GEQ(seq, src->seqlo - MAXACKWINDOW) ?' ' :'6');
		}
		REASON_SET(reason, PFRES_BADSTATE);
		return (PF_DROP);
	}

	return (PF_PASS);
}

static int
pf_tcp_track_sloppy(struct pf_state_peer *src, struct pf_state_peer *dst,
	struct pf_state **state, struct pf_pdesc *pd, u_short *reason)
{
	struct tcphdr		*th = pd->hdr.tcp;

	if (th->th_flags & TH_SYN)
		if (src->state < TCPS_SYN_SENT)
			src->state = TCPS_SYN_SENT;
	if (th->th_flags & TH_FIN)
		if (src->state < TCPS_CLOSING)
			src->state = TCPS_CLOSING;
	if (th->th_flags & TH_ACK) {
		if (dst->state == TCPS_SYN_SENT) {
			dst->state = TCPS_ESTABLISHED;
			if (src->state == TCPS_ESTABLISHED &&
			    (*state)->src_node != NULL &&
			    pf_src_connlimit(state)) {
				REASON_SET(reason, PFRES_SRCLIMIT);
				return (PF_DROP);
			}
		} else if (dst->state == TCPS_CLOSING) {
			dst->state = TCPS_FIN_WAIT_2;
		} else if (src->state == TCPS_SYN_SENT &&
		    dst->state < TCPS_SYN_SENT) {
			/*
			 * Handle a special sloppy case where we only see one
			 * half of the connection. If there is a ACK after
			 * the initial SYN without ever seeing a packet from
			 * the destination, set the connection to established.
			 */
			dst->state = src->state = TCPS_ESTABLISHED;
			if ((*state)->src_node != NULL &&
			    pf_src_connlimit(state)) {
				REASON_SET(reason, PFRES_SRCLIMIT);
				return (PF_DROP);
			}
		} else if (src->state == TCPS_CLOSING &&
		    dst->state == TCPS_ESTABLISHED &&
		    dst->seqlo == 0) {
			/*
			 * Handle the closing of half connections where we
			 * don't see the full bidirectional FIN/ACK+ACK
			 * handshake.
			 */
			dst->state = TCPS_CLOSING;
		}
	}
	if (th->th_flags & TH_RST)
		src->state = dst->state = TCPS_TIME_WAIT;

	/* update expire time */
	(*state)->expire = time_uptime;
	if (src->state >= TCPS_FIN_WAIT_2 &&
	    dst->state >= TCPS_FIN_WAIT_2)
		(*state)->timeout = PFTM_TCP_CLOSED;
	else if (src->state >= TCPS_CLOSING &&
	    dst->state >= TCPS_CLOSING)
		(*state)->timeout = PFTM_TCP_FIN_WAIT;
	else if (src->state < TCPS_ESTABLISHED ||
	    dst->state < TCPS_ESTABLISHED)
		(*state)->timeout = PFTM_TCP_OPENING;
	else if (src->state >= TCPS_CLOSING ||
	    dst->state >= TCPS_CLOSING)
		(*state)->timeout = PFTM_TCP_CLOSING;
	else
		(*state)->timeout = PFTM_TCP_ESTABLISHED;

	return (PF_PASS);
}

static int
pf_test_state_tcp(struct pf_state **state, int direction, struct pfi_kif *kif,
    struct mbuf *m, int off, void *h, struct pf_pdesc *pd,
    u_short *reason)
{
	struct pf_state_key_cmp	 key;
	struct tcphdr		*th = pd->hdr.tcp;
	int			 copyback = 0;
	struct pf_state_peer	*src, *dst;
	struct pf_state_key	*sk;

	bzero(&key, sizeof(key));
	key.af = pd->af;
	key.proto = IPPROTO_TCP;
	if (direction == PF_IN)	{	/* wire side, straight */
		PF_ACPY(&key.addr[0], pd->src, key.af);
		PF_ACPY(&key.addr[1], pd->dst, key.af);
		key.port[0] = th->th_sport;
		key.port[1] = th->th_dport;
	} else {			/* stack side, reverse */
		PF_ACPY(&key.addr[1], pd->src, key.af);
		PF_ACPY(&key.addr[0], pd->dst, key.af);
		key.port[1] = th->th_sport;
		key.port[0] = th->th_dport;
	}

	STATE_LOOKUP(kif, &key, direction, *state, pd);

	if (direction == (*state)->direction) {
		src = &(*state)->src;
		dst = &(*state)->dst;
	} else {
		src = &(*state)->dst;
		dst = &(*state)->src;
	}

	sk = (*state)->key[pd->didx];

	if ((*state)->src.state == PF_TCPS_PROXY_SRC) {
		if (direction != (*state)->direction) {
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_SYNPROXY_DROP);
		}
		if (th->th_flags & TH_SYN) {
			if (ntohl(th->th_seq) != (*state)->src.seqlo) {
				REASON_SET(reason, PFRES_SYNPROXY);
				return (PF_DROP);
			}
			pf_send_tcp(NULL, (*state)->rule.ptr, pd->af, pd->dst,
			    pd->src, th->th_dport, th->th_sport,
			    (*state)->src.seqhi, ntohl(th->th_seq) + 1,
			    TH_SYN|TH_ACK, 0, (*state)->src.mss, 0, 1, 0, NULL);
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_SYNPROXY_DROP);
		} else if ((th->th_flags & (TH_ACK|TH_RST|TH_FIN)) != TH_ACK ||
		    (ntohl(th->th_ack) != (*state)->src.seqhi + 1) ||
		    (ntohl(th->th_seq) != (*state)->src.seqlo + 1)) {
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_DROP);
		} else if ((*state)->src_node != NULL &&
		    pf_src_connlimit(state)) {
			REASON_SET(reason, PFRES_SRCLIMIT);
			return (PF_DROP);
		} else
			(*state)->src.state = PF_TCPS_PROXY_DST;
	}
	if ((*state)->src.state == PF_TCPS_PROXY_DST) {
		if (direction == (*state)->direction) {
			if (((th->th_flags & (TH_SYN|TH_ACK)) != TH_ACK) ||
			    (ntohl(th->th_ack) != (*state)->src.seqhi + 1) ||
			    (ntohl(th->th_seq) != (*state)->src.seqlo + 1)) {
				REASON_SET(reason, PFRES_SYNPROXY);
				return (PF_DROP);
			}
			(*state)->src.max_win = MAX(ntohs(th->th_win), 1);
			if ((*state)->dst.seqhi == 1)
				(*state)->dst.seqhi = htonl(arc4random());
			pf_send_tcp(NULL, (*state)->rule.ptr, pd->af,
			    &sk->addr[pd->sidx], &sk->addr[pd->didx],
			    sk->port[pd->sidx], sk->port[pd->didx],
			    (*state)->dst.seqhi, 0, TH_SYN, 0,
			    (*state)->src.mss, 0, 0, (*state)->tag, NULL);
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_SYNPROXY_DROP);
		} else if (((th->th_flags & (TH_SYN|TH_ACK)) !=
		    (TH_SYN|TH_ACK)) ||
		    (ntohl(th->th_ack) != (*state)->dst.seqhi + 1)) {
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_DROP);
		} else {
			(*state)->dst.max_win = MAX(ntohs(th->th_win), 1);
			(*state)->dst.seqlo = ntohl(th->th_seq);
			pf_send_tcp(NULL, (*state)->rule.ptr, pd->af, pd->dst,
			    pd->src, th->th_dport, th->th_sport,
			    ntohl(th->th_ack), ntohl(th->th_seq) + 1,
			    TH_ACK, (*state)->src.max_win, 0, 0, 0,
			    (*state)->tag, NULL);
			pf_send_tcp(NULL, (*state)->rule.ptr, pd->af,
			    &sk->addr[pd->sidx], &sk->addr[pd->didx],
			    sk->port[pd->sidx], sk->port[pd->didx],
			    (*state)->src.seqhi + 1, (*state)->src.seqlo + 1,
			    TH_ACK, (*state)->dst.max_win, 0, 0, 1, 0, NULL);
			(*state)->src.seqdiff = (*state)->dst.seqhi -
			    (*state)->src.seqlo;
			(*state)->dst.seqdiff = (*state)->src.seqhi -
			    (*state)->dst.seqlo;
			(*state)->src.seqhi = (*state)->src.seqlo +
			    (*state)->dst.max_win;
			(*state)->dst.seqhi = (*state)->dst.seqlo +
			    (*state)->src.max_win;
			(*state)->src.wscale = (*state)->dst.wscale = 0;
			(*state)->src.state = (*state)->dst.state =
			    TCPS_ESTABLISHED;
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_SYNPROXY_DROP);
		}
	}

	if (((th->th_flags & (TH_SYN|TH_ACK)) == TH_SYN) &&
	    dst->state >= TCPS_FIN_WAIT_2 &&
	    src->state >= TCPS_FIN_WAIT_2) {
		if (V_pf_status.debug >= PF_DEBUG_MISC) {
			printf("pf: state reuse ");
			pf_print_state(*state);
			pf_print_flags(th->th_flags);
			printf("\n");
		}
		/* XXX make sure it's the same direction ?? */
		(*state)->src.state = (*state)->dst.state = TCPS_CLOSED;
		pf_unlink_state(*state, PF_ENTER_LOCKED);
		*state = NULL;
		return (PF_DROP);
	}

	if ((*state)->state_flags & PFSTATE_SLOPPY) {
		if (pf_tcp_track_sloppy(src, dst, state, pd, reason) == PF_DROP)
			return (PF_DROP);
	} else {
		if (pf_tcp_track_full(src, dst, state, kif, m, off, pd, reason,
		    &copyback) == PF_DROP)
			return (PF_DROP);
	}

	/* translate source/destination address, if necessary */
	if ((*state)->key[PF_SK_WIRE] != (*state)->key[PF_SK_STACK]) {
		struct pf_state_key *nk = (*state)->key[pd->didx];

		if (PF_ANEQ(pd->src, &nk->addr[pd->sidx], pd->af) ||
		    nk->port[pd->sidx] != th->th_sport)
			pf_change_ap(m, pd->src, &th->th_sport,
			    pd->ip_sum, &th->th_sum, &nk->addr[pd->sidx],
			    nk->port[pd->sidx], 0, pd->af);

		if (PF_ANEQ(pd->dst, &nk->addr[pd->didx], pd->af) ||
		    nk->port[pd->didx] != th->th_dport)
			pf_change_ap(m, pd->dst, &th->th_dport,
			    pd->ip_sum, &th->th_sum, &nk->addr[pd->didx],
			    nk->port[pd->didx], 0, pd->af);
		copyback = 1;
	}

	/* Copyback sequence modulation or stateful scrub changes if needed */
	if (copyback)
		m_copyback(m, off, sizeof(*th), (caddr_t)th);

	return (PF_PASS);
}

static int
pf_test_state_udp(struct pf_state **state, int direction, struct pfi_kif *kif,
    struct mbuf *m, int off, void *h, struct pf_pdesc *pd)
{
	struct pf_state_peer	*src, *dst;
	struct pf_state_key_cmp	 key;
	struct udphdr		*uh = pd->hdr.udp;

	bzero(&key, sizeof(key));
	key.af = pd->af;
	key.proto = IPPROTO_UDP;
	if (direction == PF_IN)	{	/* wire side, straight */
		PF_ACPY(&key.addr[0], pd->src, key.af);
		PF_ACPY(&key.addr[1], pd->dst, key.af);
		key.port[0] = uh->uh_sport;
		key.port[1] = uh->uh_dport;
	} else {			/* stack side, reverse */
		PF_ACPY(&key.addr[1], pd->src, key.af);
		PF_ACPY(&key.addr[0], pd->dst, key.af);
		key.port[1] = uh->uh_sport;
		key.port[0] = uh->uh_dport;
	}

	STATE_LOOKUP(kif, &key, direction, *state, pd);

	if (direction == (*state)->direction) {
		src = &(*state)->src;
		dst = &(*state)->dst;
	} else {
		src = &(*state)->dst;
		dst = &(*state)->src;
	}

	/* update states */
	if (src->state < PFUDPS_SINGLE)
		src->state = PFUDPS_SINGLE;
	if (dst->state == PFUDPS_SINGLE)
		dst->state = PFUDPS_MULTIPLE;

	/* update expire time */
	(*state)->expire = time_uptime;
	if (src->state == PFUDPS_MULTIPLE && dst->state == PFUDPS_MULTIPLE)
		(*state)->timeout = PFTM_UDP_MULTIPLE;
	else
		(*state)->timeout = PFTM_UDP_SINGLE;

	/* translate source/destination address, if necessary */
	if ((*state)->key[PF_SK_WIRE] != (*state)->key[PF_SK_STACK]) {
		struct pf_state_key *nk = (*state)->key[pd->didx];

		if (PF_ANEQ(pd->src, &nk->addr[pd->sidx], pd->af) ||
		    nk->port[pd->sidx] != uh->uh_sport)
			pf_change_ap(m, pd->src, &uh->uh_sport, pd->ip_sum,
			    &uh->uh_sum, &nk->addr[pd->sidx],
			    nk->port[pd->sidx], 1, pd->af);

		if (PF_ANEQ(pd->dst, &nk->addr[pd->didx], pd->af) ||
		    nk->port[pd->didx] != uh->uh_dport)
			pf_change_ap(m, pd->dst, &uh->uh_dport, pd->ip_sum,
			    &uh->uh_sum, &nk->addr[pd->didx],
			    nk->port[pd->didx], 1, pd->af);
		m_copyback(m, off, sizeof(*uh), (caddr_t)uh);
	}

	return (PF_PASS);
}

static int
pf_test_state_icmp(struct pf_state **state, int direction, struct pfi_kif *kif,
    struct mbuf *m, int off, void *h, struct pf_pdesc *pd, u_short *reason)
{
	struct pf_addr  *saddr = pd->src, *daddr = pd->dst;
	u_int16_t	 icmpid = 0, *icmpsum;
	u_int8_t	 icmptype, icmpcode;
	int		 state_icmp = 0;
	struct pf_state_key_cmp key;

	bzero(&key, sizeof(key));
	switch (pd->proto) {
#ifdef INET
	case IPPROTO_ICMP:
		icmptype = pd->hdr.icmp->icmp_type;
		icmpcode = pd->hdr.icmp->icmp_code;
		icmpid = pd->hdr.icmp->icmp_id;
		icmpsum = &pd->hdr.icmp->icmp_cksum;

		if (icmptype == ICMP_UNREACH ||
		    icmptype == ICMP_SOURCEQUENCH ||
		    icmptype == ICMP_REDIRECT ||
		    icmptype == ICMP_TIMXCEED ||
		    icmptype == ICMP_PARAMPROB)
			state_icmp++;
		break;
#endif /* INET */
#ifdef INET6
	case IPPROTO_ICMPV6:
		icmptype = pd->hdr.icmp6->icmp6_type;
		icmpcode = pd->hdr.icmp6->icmp6_code;
		icmpid = pd->hdr.icmp6->icmp6_id;
		icmpsum = &pd->hdr.icmp6->icmp6_cksum;

		if (icmptype == ICMP6_DST_UNREACH ||
		    icmptype == ICMP6_PACKET_TOO_BIG ||
		    icmptype == ICMP6_TIME_EXCEEDED ||
		    icmptype == ICMP6_PARAM_PROB)
			state_icmp++;
		break;
#endif /* INET6 */
	}

	if (!state_icmp) {

		/*
		 * ICMP query/reply message not related to a TCP/UDP packet.
		 * Search for an ICMP state.
		 */
		key.af = pd->af;
		key.proto = pd->proto;
		key.port[0] = key.port[1] = icmpid;
		if (direction == PF_IN)	{	/* wire side, straight */
			PF_ACPY(&key.addr[0], pd->src, key.af);
			PF_ACPY(&key.addr[1], pd->dst, key.af);
		} else {			/* stack side, reverse */
			PF_ACPY(&key.addr[1], pd->src, key.af);
			PF_ACPY(&key.addr[0], pd->dst, key.af);
		}

		STATE_LOOKUP(kif, &key, direction, *state, pd);

		(*state)->expire = time_uptime;
		(*state)->timeout = PFTM_ICMP_ERROR_REPLY;

		/* translate source/destination address, if necessary */
		if ((*state)->key[PF_SK_WIRE] != (*state)->key[PF_SK_STACK]) {
			struct pf_state_key *nk = (*state)->key[pd->didx];

			switch (pd->af) {
#ifdef INET
			case AF_INET:
				if (PF_ANEQ(pd->src,
				    &nk->addr[pd->sidx], AF_INET))
					pf_change_a(&saddr->v4.s_addr,
					    pd->ip_sum,
					    nk->addr[pd->sidx].v4.s_addr, 0);

				if (PF_ANEQ(pd->dst, &nk->addr[pd->didx],
				    AF_INET))
					pf_change_a(&daddr->v4.s_addr,
					    pd->ip_sum,
					    nk->addr[pd->didx].v4.s_addr, 0);

				if (nk->port[0] !=
				    pd->hdr.icmp->icmp_id) {
					pd->hdr.icmp->icmp_cksum =
					    pf_cksum_fixup(
					    pd->hdr.icmp->icmp_cksum, icmpid,
					    nk->port[pd->sidx], 0);
					pd->hdr.icmp->icmp_id =
					    nk->port[pd->sidx];
				}

				m_copyback(m, off, ICMP_MINLEN,
				    (caddr_t )pd->hdr.icmp);
				break;
#endif /* INET */
#ifdef INET6
			case AF_INET6:
				if (PF_ANEQ(pd->src,
				    &nk->addr[pd->sidx], AF_INET6))
					pf_change_a6(saddr,
					    &pd->hdr.icmp6->icmp6_cksum,
					    &nk->addr[pd->sidx], 0);

				if (PF_ANEQ(pd->dst,
				    &nk->addr[pd->didx], AF_INET6))
					pf_change_a6(daddr,
					    &pd->hdr.icmp6->icmp6_cksum,
					    &nk->addr[pd->didx], 0);

				m_copyback(m, off, sizeof(struct icmp6_hdr),
				    (caddr_t )pd->hdr.icmp6);
				break;
#endif /* INET6 */
			}
		}
		return (PF_PASS);

	} else {
		/*
		 * ICMP error message in response to a TCP/UDP packet.
		 * Extract the inner TCP/UDP header and search for that state.
		 */

		struct pf_pdesc	pd2;
		bzero(&pd2, sizeof pd2);
#ifdef INET
		struct ip	h2;
#endif /* INET */
#ifdef INET6
		struct ip6_hdr	h2_6;
		int		terminal = 0;
#endif /* INET6 */
		int		ipoff2 = 0;
		int		off2 = 0;

		pd2.af = pd->af;
		/* Payload packet is from the opposite direction. */
		pd2.sidx = (direction == PF_IN) ? 1 : 0;
		pd2.didx = (direction == PF_IN) ? 0 : 1;
		switch (pd->af) {
#ifdef INET
		case AF_INET:
			/* offset of h2 in mbuf chain */
			ipoff2 = off + ICMP_MINLEN;

			if (!pf_pull_hdr(m, ipoff2, &h2, sizeof(h2),
			    NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(ip)\n"));
				return (PF_DROP);
			}
			/*
			 * ICMP error messages don't refer to non-first
			 * fragments
			 */
			if (h2.ip_off & htons(IP_OFFMASK)) {
				REASON_SET(reason, PFRES_FRAG);
				return (PF_DROP);
			}

			/* offset of protocol header that follows h2 */
			off2 = ipoff2 + (h2.ip_hl << 2);

			pd2.proto = h2.ip_p;
			pd2.src = (struct pf_addr *)&h2.ip_src;
			pd2.dst = (struct pf_addr *)&h2.ip_dst;
			pd2.ip_sum = &h2.ip_sum;
			break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			ipoff2 = off + sizeof(struct icmp6_hdr);

			if (!pf_pull_hdr(m, ipoff2, &h2_6, sizeof(h2_6),
			    NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(ip6)\n"));
				return (PF_DROP);
			}
			pd2.proto = h2_6.ip6_nxt;
			pd2.src = (struct pf_addr *)&h2_6.ip6_src;
			pd2.dst = (struct pf_addr *)&h2_6.ip6_dst;
			pd2.ip_sum = NULL;
			off2 = ipoff2 + sizeof(h2_6);
			do {
				switch (pd2.proto) {
				case IPPROTO_FRAGMENT:
					/*
					 * ICMPv6 error messages for
					 * non-first fragments
					 */
					REASON_SET(reason, PFRES_FRAG);
					return (PF_DROP);
				case IPPROTO_AH:
				case IPPROTO_HOPOPTS:
				case IPPROTO_ROUTING:
				case IPPROTO_DSTOPTS: {
					/* get next header and header length */
					struct ip6_ext opt6;

					if (!pf_pull_hdr(m, off2, &opt6,
					    sizeof(opt6), NULL, reason,
					    pd2.af)) {
						DPFPRINTF(PF_DEBUG_MISC,
						    ("pf: ICMPv6 short opt\n"));
						return (PF_DROP);
					}
					if (pd2.proto == IPPROTO_AH)
						off2 += (opt6.ip6e_len + 2) * 4;
					else
						off2 += (opt6.ip6e_len + 1) * 8;
					pd2.proto = opt6.ip6e_nxt;
					/* goto the next header */
					break;
				}
				default:
					terminal++;
					break;
				}
			} while (!terminal);
			break;
#endif /* INET6 */
		}

		if (PF_ANEQ(pd->dst, pd2.src, pd->af)) {
			if (V_pf_status.debug >= PF_DEBUG_MISC) {
				printf("pf: BAD ICMP %d:%d outer dst: ",
				    icmptype, icmpcode);
				pf_print_host(pd->src, 0, pd->af);
				printf(" -> ");
				pf_print_host(pd->dst, 0, pd->af);
				printf(" inner src: ");
				pf_print_host(pd2.src, 0, pd2.af);
				printf(" -> ");
				pf_print_host(pd2.dst, 0, pd2.af);
				printf("\n");
			}
			REASON_SET(reason, PFRES_BADSTATE);
			return (PF_DROP);
		}

		switch (pd2.proto) {
		case IPPROTO_TCP: {
			struct tcphdr		 th;
			u_int32_t		 seq;
			struct pf_state_peer	*src, *dst;
			u_int8_t		 dws;
			int			 copyback = 0;

			/*
			 * Only the first 8 bytes of the TCP header can be
			 * expected. Don't access any TCP header fields after
			 * th_seq, an ackskew test is not possible.
			 */
			if (!pf_pull_hdr(m, off2, &th, 8, NULL, reason,
			    pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(tcp)\n"));
				return (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_TCP;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[pd2.sidx] = th.th_sport;
			key.port[pd2.didx] = th.th_dport;

			STATE_LOOKUP(kif, &key, direction, *state, pd);

			if (direction == (*state)->direction) {
				src = &(*state)->dst;
				dst = &(*state)->src;
			} else {
				src = &(*state)->src;
				dst = &(*state)->dst;
			}

			if (src->wscale && dst->wscale)
				dws = dst->wscale & PF_WSCALE_MASK;
			else
				dws = 0;

			/* Demodulate sequence number */
			seq = ntohl(th.th_seq) - src->seqdiff;
			if (src->seqdiff) {
				pf_change_a(&th.th_seq, icmpsum,
				    htonl(seq), 0);
				copyback = 1;
			}

			if (!((*state)->state_flags & PFSTATE_SLOPPY) &&
			    (!SEQ_GEQ(src->seqhi, seq) ||
			    !SEQ_GEQ(seq, src->seqlo - (dst->max_win << dws)))) {
				if (V_pf_status.debug >= PF_DEBUG_MISC) {
					printf("pf: BAD ICMP %d:%d ",
					    icmptype, icmpcode);
					pf_print_host(pd->src, 0, pd->af);
					printf(" -> ");
					pf_print_host(pd->dst, 0, pd->af);
					printf(" state: ");
					pf_print_state(*state);
					printf(" seq=%u\n", seq);
				}
				REASON_SET(reason, PFRES_BADSTATE);
				return (PF_DROP);
			} else {
				if (V_pf_status.debug >= PF_DEBUG_MISC) {
					printf("pf: OK ICMP %d:%d ",
					    icmptype, icmpcode);
					pf_print_host(pd->src, 0, pd->af);
					printf(" -> ");
					pf_print_host(pd->dst, 0, pd->af);
					printf(" state: ");
					pf_print_state(*state);
					printf(" seq=%u\n", seq);
				}
			}

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != th.th_sport)
					pf_change_icmp(pd2.src, &th.th_sport,
					    daddr, &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, pd2.af);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != th.th_dport)
					pf_change_icmp(pd2.dst, &th.th_dport,
					    saddr, &nk->addr[pd2.didx],
					    nk->port[pd2.didx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, pd2.af);
				copyback = 1;
			}

			if (copyback) {
				switch (pd2.af) {
#ifdef INET
				case AF_INET:
					m_copyback(m, off, ICMP_MINLEN,
					    (caddr_t )pd->hdr.icmp);
					m_copyback(m, ipoff2, sizeof(h2),
					    (caddr_t )&h2);
					break;
#endif /* INET */
#ifdef INET6
				case AF_INET6:
					m_copyback(m, off,
					    sizeof(struct icmp6_hdr),
					    (caddr_t )pd->hdr.icmp6);
					m_copyback(m, ipoff2, sizeof(h2_6),
					    (caddr_t )&h2_6);
					break;
#endif /* INET6 */
				}
				m_copyback(m, off2, 8, (caddr_t)&th);
			}

			return (PF_PASS);
			break;
		}
		case IPPROTO_UDP: {
			struct udphdr		uh;

			if (!pf_pull_hdr(m, off2, &uh, sizeof(uh),
			    NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(udp)\n"));
				return (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_UDP;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[pd2.sidx] = uh.uh_sport;
			key.port[pd2.didx] = uh.uh_dport;

			STATE_LOOKUP(kif, &key, direction, *state, pd);

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != uh.uh_sport)
					pf_change_icmp(pd2.src, &uh.uh_sport,
					    daddr, &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx], &uh.uh_sum,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 1, pd2.af);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != uh.uh_dport)
					pf_change_icmp(pd2.dst, &uh.uh_dport,
					    saddr, &nk->addr[pd2.didx],
					    nk->port[pd2.didx], &uh.uh_sum,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 1, pd2.af);

				switch (pd2.af) {
#ifdef INET
				case AF_INET:
					m_copyback(m, off, ICMP_MINLEN,
					    (caddr_t )pd->hdr.icmp);
					m_copyback(m, ipoff2, sizeof(h2), (caddr_t)&h2);
					break;
#endif /* INET */
#ifdef INET6
				case AF_INET6:
					m_copyback(m, off,
					    sizeof(struct icmp6_hdr),
					    (caddr_t )pd->hdr.icmp6);
					m_copyback(m, ipoff2, sizeof(h2_6),
					    (caddr_t )&h2_6);
					break;
#endif /* INET6 */
				}
				m_copyback(m, off2, sizeof(uh), (caddr_t)&uh);
			}
			return (PF_PASS);
			break;
		}
#ifdef INET
		case IPPROTO_ICMP: {
			struct icmp		iih;

			if (!pf_pull_hdr(m, off2, &iih, ICMP_MINLEN,
			    NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short i"
				    "(icmp)\n"));
				return (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_ICMP;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[0] = key.port[1] = iih.icmp_id;

			STATE_LOOKUP(kif, &key, direction, *state, pd);

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != iih.icmp_id)
					pf_change_icmp(pd2.src, &iih.icmp_id,
					    daddr, &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, AF_INET);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != iih.icmp_id)
					pf_change_icmp(pd2.dst, &iih.icmp_id,
					    saddr, &nk->addr[pd2.didx],
					    nk->port[pd2.didx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, AF_INET);

				m_copyback(m, off, ICMP_MINLEN, (caddr_t)pd->hdr.icmp);
				m_copyback(m, ipoff2, sizeof(h2), (caddr_t)&h2);
				m_copyback(m, off2, ICMP_MINLEN, (caddr_t)&iih);
			}
			return (PF_PASS);
			break;
		}
#endif /* INET */
#ifdef INET6
		case IPPROTO_ICMPV6: {
			struct icmp6_hdr	iih;

			if (!pf_pull_hdr(m, off2, &iih,
			    sizeof(struct icmp6_hdr), NULL, reason, pd2.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: ICMP error message too short "
				    "(icmp6)\n"));
				return (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_ICMPV6;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[0] = key.port[1] = iih.icmp6_id;

			STATE_LOOKUP(kif, &key, direction, *state, pd);

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != iih.icmp6_id)
					pf_change_icmp(pd2.src, &iih.icmp6_id,
					    daddr, &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, AF_INET6);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != iih.icmp6_id)
					pf_change_icmp(pd2.dst, &iih.icmp6_id,
					    saddr, &nk->addr[pd2.didx],
					    nk->port[pd2.didx], NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, AF_INET6);

				m_copyback(m, off, sizeof(struct icmp6_hdr),
				    (caddr_t)pd->hdr.icmp6);
				m_copyback(m, ipoff2, sizeof(h2_6), (caddr_t)&h2_6);
				m_copyback(m, off2, sizeof(struct icmp6_hdr),
				    (caddr_t)&iih);
			}
			return (PF_PASS);
			break;
		}
#endif /* INET6 */
		default: {
			key.af = pd2.af;
			key.proto = pd2.proto;
			PF_ACPY(&key.addr[pd2.sidx], pd2.src, key.af);
			PF_ACPY(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[0] = key.port[1] = 0;

			STATE_LOOKUP(kif, &key, direction, *state, pd);

			/* translate source/destination address, if necessary */
			if ((*state)->key[PF_SK_WIRE] !=
			    (*state)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*state)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af))
					pf_change_icmp(pd2.src, NULL, daddr,
					    &nk->addr[pd2.sidx], 0, NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, pd2.af);

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af))
					pf_change_icmp(pd2.dst, NULL, saddr,
					    &nk->addr[pd2.didx], 0, NULL,
					    pd2.ip_sum, icmpsum,
					    pd->ip_sum, 0, pd2.af);

				switch (pd2.af) {
#ifdef INET
				case AF_INET:
					m_copyback(m, off, ICMP_MINLEN,
					    (caddr_t)pd->hdr.icmp);
					m_copyback(m, ipoff2, sizeof(h2), (caddr_t)&h2);
					break;
#endif /* INET */
#ifdef INET6
				case AF_INET6:
					m_copyback(m, off,
					    sizeof(struct icmp6_hdr),
					    (caddr_t )pd->hdr.icmp6);
					m_copyback(m, ipoff2, sizeof(h2_6),
					    (caddr_t )&h2_6);
					break;
#endif /* INET6 */
				}
			}
			return (PF_PASS);
			break;
		}
		}
	}
}

static int
pf_test_state_other(struct pf_state **state, int direction, struct pfi_kif *kif,
    struct mbuf *m, struct pf_pdesc *pd)
{
	struct pf_state_peer	*src, *dst;
	struct pf_state_key_cmp	 key;

	bzero(&key, sizeof(key));
	key.af = pd->af;
	key.proto = pd->proto;
	if (direction == PF_IN)	{
		PF_ACPY(&key.addr[0], pd->src, key.af);
		PF_ACPY(&key.addr[1], pd->dst, key.af);
		key.port[0] = key.port[1] = 0;
	} else {
		PF_ACPY(&key.addr[1], pd->src, key.af);
		PF_ACPY(&key.addr[0], pd->dst, key.af);
		key.port[1] = key.port[0] = 0;
	}

	STATE_LOOKUP(kif, &key, direction, *state, pd);

	if (direction == (*state)->direction) {
		src = &(*state)->src;
		dst = &(*state)->dst;
	} else {
		src = &(*state)->dst;
		dst = &(*state)->src;
	}

	/* update states */
	if (src->state < PFOTHERS_SINGLE)
		src->state = PFOTHERS_SINGLE;
	if (dst->state == PFOTHERS_SINGLE)
		dst->state = PFOTHERS_MULTIPLE;

	/* update expire time */
	(*state)->expire = time_uptime;
	if (src->state == PFOTHERS_MULTIPLE && dst->state == PFOTHERS_MULTIPLE)
		(*state)->timeout = PFTM_OTHER_MULTIPLE;
	else
		(*state)->timeout = PFTM_OTHER_SINGLE;

	/* translate source/destination address, if necessary */
	if ((*state)->key[PF_SK_WIRE] != (*state)->key[PF_SK_STACK]) {
		struct pf_state_key *nk = (*state)->key[pd->didx];

		KASSERT(nk, ("%s: nk is null", __func__));
		KASSERT(pd, ("%s: pd is null", __func__));
		KASSERT(pd->src, ("%s: pd->src is null", __func__));
		KASSERT(pd->dst, ("%s: pd->dst is null", __func__));
		switch (pd->af) {
#ifdef INET
		case AF_INET:
			if (PF_ANEQ(pd->src, &nk->addr[pd->sidx], AF_INET))
				pf_change_a(&pd->src->v4.s_addr,
				    pd->ip_sum,
				    nk->addr[pd->sidx].v4.s_addr,
				    0);


			if (PF_ANEQ(pd->dst, &nk->addr[pd->didx], AF_INET))
				pf_change_a(&pd->dst->v4.s_addr,
				    pd->ip_sum,
				    nk->addr[pd->didx].v4.s_addr,
				    0);

				break;
#endif /* INET */
#ifdef INET6
		case AF_INET6:
			if (PF_ANEQ(pd->src, &nk->addr[pd->sidx], AF_INET))
				PF_ACPY(pd->src, &nk->addr[pd->sidx], pd->af);

			if (PF_ANEQ(pd->dst, &nk->addr[pd->didx], AF_INET))
				PF_ACPY(pd->dst, &nk->addr[pd->didx], pd->af);
#endif /* INET6 */
		}
	}
	return (PF_PASS);
}

/*
 * ipoff and off are measured from the start of the mbuf chain.
 * h must be at "ipoff" on the mbuf chain.
 */
void *
pf_pull_hdr(struct mbuf *m, int off, void *p, int len,
    u_short *actionp, u_short *reasonp, sa_family_t af)
{
	switch (af) {
#ifdef INET
	case AF_INET: {
		struct ip	*h = mtod(m, struct ip *);
		u_int16_t	 fragoff = (ntohs(h->ip_off) & IP_OFFMASK) << 3;

		if (fragoff) {
			if (fragoff >= len)
				ACTION_SET(actionp, PF_PASS);
			else {
				ACTION_SET(actionp, PF_DROP);
				REASON_SET(reasonp, PFRES_FRAG);
			}
			return (NULL);
		}
		if (m->m_pkthdr.len < off + len ||
		    ntohs(h->ip_len) < off + len) {
			ACTION_SET(actionp, PF_DROP);
			REASON_SET(reasonp, PFRES_SHORT);
			return (NULL);
		}
		break;
	}
#endif /* INET */
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr	*h = mtod(m, struct ip6_hdr *);

		if (m->m_pkthdr.len < off + len ||
		    (ntohs(h->ip6_plen) + sizeof(struct ip6_hdr)) <
		    (unsigned)(off + len)) {
			ACTION_SET(actionp, PF_DROP);
			REASON_SET(reasonp, PFRES_SHORT);
			return (NULL);
		}
		break;
	}
#endif /* INET6 */
	}
	m_copydata(m, off, len, p);
	return (p);
}

#ifdef RADIX_MPATH
static int
pf_routable_oldmpath(struct pf_addr *addr, sa_family_t af, struct pfi_kif *kif,
    int rtableid)
{
	struct radix_node_head	*rnh;
	struct sockaddr_in	*dst;
	int			 ret = 1;
	int			 check_mpath;
#ifdef INET6
	struct sockaddr_in6	*dst6;
	struct route_in6	 ro;
#else
	struct route		 ro;
#endif
	struct radix_node	*rn;
	struct rtentry		*rt;
	struct ifnet		*ifp;

	check_mpath = 0;
	/* XXX: stick to table 0 for now */
	rnh = rt_tables_get_rnh(0, af);
	if (rnh != NULL && rn_mpath_capable(rnh))
		check_mpath = 1;
	bzero(&ro, sizeof(ro));
	switch (af) {
	case AF_INET:
		dst = satosin(&ro.ro_dst);
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = addr->v4;
		break;
#ifdef INET6
	case AF_INET6:
		/*
		 * Skip check for addresses with embedded interface scope,
		 * as they would always match anyway.
		 */
		if (IN6_IS_SCOPE_EMBED(&addr->v6))
			goto out;
		dst6 = (struct sockaddr_in6 *)&ro.ro_dst;
		dst6->sin6_family = AF_INET6;
		dst6->sin6_len = sizeof(*dst6);
		dst6->sin6_addr = addr->v6;
		break;
#endif /* INET6 */
	default:
		return (0);
	}

	/* Skip checks for ipsec interfaces */
	if (kif != NULL && kif->pfik_ifp->if_type == IFT_ENC)
		goto out;

	switch (af) {
#ifdef INET6
	case AF_INET6:
		in6_rtalloc_ign(&ro, 0, rtableid);
		break;
#endif
#ifdef INET
	case AF_INET:
		in_rtalloc_ign((struct route *)&ro, 0, rtableid);
		break;
#endif
	}

	if (ro.ro_rt != NULL) {
		/* No interface given, this is a no-route check */
		if (kif == NULL)
			goto out;

		if (kif->pfik_ifp == NULL) {
			ret = 0;
			goto out;
		}

		/* Perform uRPF check if passed input interface */
		ret = 0;
		rn = (struct radix_node *)ro.ro_rt;
		do {
			rt = (struct rtentry *)rn;
			ifp = rt->rt_ifp;

			if (kif->pfik_ifp == ifp)
				ret = 1;
			rn = rn_mpath_next(rn);
		} while (check_mpath == 1 && rn != NULL && ret == 0);
	} else
		ret = 0;
out:
	if (ro.ro_rt != NULL)
		RTFREE(ro.ro_rt);
	return (ret);
}
#endif

int
pf_routable(struct pf_addr *addr, sa_family_t af, struct pfi_kif *kif,
    int rtableid)
{
#ifdef INET
	struct nhop4_basic	nh4;
#endif
#ifdef INET6
	struct nhop6_basic	nh6;
#endif
	struct ifnet		*ifp;
#ifdef RADIX_MPATH
	struct radix_node_head	*rnh;

	/* XXX: stick to table 0 for now */
	rnh = rt_tables_get_rnh(0, af);
	if (rnh != NULL && rn_mpath_capable(rnh))
		return (pf_routable_oldmpath(addr, af, kif, rtableid));
#endif
	/*
	 * Skip check for addresses with embedded interface scope,
	 * as they would always match anyway.
	 */
	if (af == AF_INET6 && IN6_IS_SCOPE_EMBED(&addr->v6))
		return (1);

	if (af != AF_INET && af != AF_INET6)
		return (0);

	/* Skip checks for ipsec interfaces */
	if (kif != NULL && kif->pfik_ifp->if_type == IFT_ENC)
		return (1);

	ifp = NULL;

	switch (af) {
#ifdef INET6
	case AF_INET6:
		if (fib6_lookup_nh_basic(rtableid, &addr->v6, 0, 0, 0, &nh6)!=0)
			return (0);
		ifp = nh6.nh_ifp;
		break;
#endif
#ifdef INET
	case AF_INET:
		if (fib4_lookup_nh_basic(rtableid, addr->v4, 0, 0, &nh4) != 0)
			return (0);
		ifp = nh4.nh_ifp;
		break;
#endif
	}

	/* No interface given, this is a no-route check */
	if (kif == NULL)
		return (1);

	if (kif->pfik_ifp == NULL)
		return (0);

	/* Perform uRPF check if passed input interface */
	if (kif->pfik_ifp == ifp)
		return (1);
	return (0);
}

#ifdef INET
static void
pf_route(struct mbuf **m, struct pf_rule *r, int dir, struct ifnet *oifp,
    struct pf_state *s, struct pf_pdesc *pd, struct inpcb *inp)
{
	struct mbuf		*m0, *m1;
	struct sockaddr_in	dst;
	struct ip		*ip;
	struct ifnet		*ifp = NULL;
	struct pf_addr		 naddr;
	struct pf_src_node	*sn = NULL;
	int			 error = 0;
	uint16_t		 ip_len, ip_off;

	KASSERT(m && *m && r && oifp, ("%s: invalid parameters", __func__));
	KASSERT(dir == PF_IN || dir == PF_OUT, ("%s: invalid direction",
	    __func__));

	if ((pd->pf_mtag == NULL &&
	    ((pd->pf_mtag = pf_get_mtag(*m)) == NULL)) ||
	    pd->pf_mtag->routed++ > 3) {
		m0 = *m;
		*m = NULL;
		goto bad_locked;
	}

	if (r->rt == PF_DUPTO) {
		if ((m0 = m_dup(*m, M_NOWAIT)) == NULL) {
			if (s)
				PF_STATE_UNLOCK(s);
			return;
		}
	} else {
		if ((r->rt == PF_REPLYTO) == (r->direction == dir)) {
			if (s)
				PF_STATE_UNLOCK(s);
			return;
		}
		m0 = *m;
	}

	ip = mtod(m0, struct ip *);

	bzero(&dst, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(dst);
	dst.sin_addr = ip->ip_dst;

	bzero(&naddr, sizeof(naddr));

	if (TAILQ_EMPTY(&r->rpool.list)) {
		DPFPRINTF(PF_DEBUG_URGENT,
		    ("%s: TAILQ_EMPTY(&r->rpool.list)\n", __func__));
		goto bad_locked;
	}
	if (s == NULL) {
		pf_map_addr(AF_INET, r, (struct pf_addr *)&ip->ip_src,
		    &naddr, NULL, &sn);
		if (!PF_AZERO(&naddr, AF_INET))
			dst.sin_addr.s_addr = naddr.v4.s_addr;
		ifp = r->rpool.cur->kif ?
		    r->rpool.cur->kif->pfik_ifp : NULL;
	} else {
		if (!PF_AZERO(&s->rt_addr, AF_INET))
			dst.sin_addr.s_addr =
			    s->rt_addr.v4.s_addr;
		ifp = s->rt_kif ? s->rt_kif->pfik_ifp : NULL;
		PF_STATE_UNLOCK(s);
	}
	if (ifp == NULL)
		goto bad;

	if (oifp != ifp) {
		if (pf_test(PF_OUT, 0, ifp, &m0, inp) != PF_PASS)
			goto bad;
		else if (m0 == NULL)
			goto done;
		if (m0->m_len < sizeof(struct ip)) {
			DPFPRINTF(PF_DEBUG_URGENT,
			    ("%s: m0->m_len < sizeof(struct ip)\n", __func__));
			goto bad;
		}
		ip = mtod(m0, struct ip *);
	}

	if (ifp->if_flags & IFF_LOOPBACK)
		m0->m_flags |= M_SKIP_FIREWALL;

	ip_len = ntohs(ip->ip_len);
	ip_off = ntohs(ip->ip_off);

	/* Copied from FreeBSD 10.0-CURRENT ip_output. */
	m0->m_pkthdr.csum_flags |= CSUM_IP;
	if (m0->m_pkthdr.csum_flags & CSUM_DELAY_DATA & ~ifp->if_hwassist) {
		in_delayed_cksum(m0);
		m0->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA;
	}
#ifdef SCTP
	if (m0->m_pkthdr.csum_flags & CSUM_SCTP & ~ifp->if_hwassist) {
		sctp_delayed_cksum(m, (uint32_t)(ip->ip_hl << 2));
		m0->m_pkthdr.csum_flags &= ~CSUM_SCTP;
	}
#endif

	/*
	 * If small enough for interface, or the interface will take
	 * care of the fragmentation for us, we can just send directly.
	 */
	if (ip_len <= ifp->if_mtu ||
	    (m0->m_pkthdr.csum_flags & ifp->if_hwassist & CSUM_TSO) != 0) {
		ip->ip_sum = 0;
		if (m0->m_pkthdr.csum_flags & CSUM_IP & ~ifp->if_hwassist) {
			ip->ip_sum = in_cksum(m0, ip->ip_hl << 2);
			m0->m_pkthdr.csum_flags &= ~CSUM_IP;
		}
		m_clrprotoflags(m0);	/* Avoid confusing lower layers. */
		error = (*ifp->if_output)(ifp, m0, sintosa(&dst), NULL);
		goto done;
	}

	/* Balk when DF bit is set or the interface didn't support TSO. */
	if ((ip_off & IP_DF) || (m0->m_pkthdr.csum_flags & CSUM_TSO)) {
		error = EMSGSIZE;
		KMOD_IPSTAT_INC(ips_cantfrag);
		if (r->rt != PF_DUPTO) {
			icmp_error(m0, ICMP_UNREACH, ICMP_UNREACH_NEEDFRAG, 0,
			    ifp->if_mtu);
			goto done;
		} else
			goto bad;
	}

	error = ip_fragment(ip, &m0, ifp->if_mtu, ifp->if_hwassist);
	if (error)
		goto bad;

	for (; m0; m0 = m1) {
		m1 = m0->m_nextpkt;
		m0->m_nextpkt = NULL;
		if (error == 0) {
			m_clrprotoflags(m0);
			error = (*ifp->if_output)(ifp, m0, sintosa(&dst), NULL);
		} else
			m_freem(m0);
	}

	if (error == 0)
		KMOD_IPSTAT_INC(ips_fragmented);

done:
	if (r->rt != PF_DUPTO)
		*m = NULL;
	return;

bad_locked:
	if (s)
		PF_STATE_UNLOCK(s);
bad:
	m_freem(m0);
	goto done;
}
#endif /* INET */

#ifdef INET6
static void
pf_route6(struct mbuf **m, struct pf_rule *r, int dir, struct ifnet *oifp,
    struct pf_state *s, struct pf_pdesc *pd, struct inpcb *inp)
{
	struct mbuf		*m0;
	struct sockaddr_in6	dst;
	struct ip6_hdr		*ip6;
	struct ifnet		*ifp = NULL;
	struct pf_addr		 naddr;
	struct pf_src_node	*sn = NULL;

	KASSERT(m && *m && r && oifp, ("%s: invalid parameters", __func__));
	KASSERT(dir == PF_IN || dir == PF_OUT, ("%s: invalid direction",
	    __func__));

	if ((pd->pf_mtag == NULL &&
	    ((pd->pf_mtag = pf_get_mtag(*m)) == NULL)) ||
	    pd->pf_mtag->routed++ > 3) {
		m0 = *m;
		*m = NULL;
		goto bad_locked;
	}

	if (r->rt == PF_DUPTO) {
		if ((m0 = m_dup(*m, M_NOWAIT)) == NULL) {
			if (s)
				PF_STATE_UNLOCK(s);
			return;
		}
	} else {
		if ((r->rt == PF_REPLYTO) == (r->direction == dir)) {
			if (s)
				PF_STATE_UNLOCK(s);
			return;
		}
		m0 = *m;
	}

	ip6 = mtod(m0, struct ip6_hdr *);

	bzero(&dst, sizeof(dst));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(dst);
	dst.sin6_addr = ip6->ip6_dst;

	bzero(&naddr, sizeof(naddr));

	if (TAILQ_EMPTY(&r->rpool.list)) {
		DPFPRINTF(PF_DEBUG_URGENT,
		    ("%s: TAILQ_EMPTY(&r->rpool.list)\n", __func__));
		goto bad_locked;
	}
	if (s == NULL) {
		pf_map_addr(AF_INET6, r, (struct pf_addr *)&ip6->ip6_src,
		    &naddr, NULL, &sn);
		if (!PF_AZERO(&naddr, AF_INET6))
			PF_ACPY((struct pf_addr *)&dst.sin6_addr,
			    &naddr, AF_INET6);
		ifp = r->rpool.cur->kif ? r->rpool.cur->kif->pfik_ifp : NULL;
	} else {
		if (!PF_AZERO(&s->rt_addr, AF_INET6))
			PF_ACPY((struct pf_addr *)&dst.sin6_addr,
			    &s->rt_addr, AF_INET6);
		ifp = s->rt_kif ? s->rt_kif->pfik_ifp : NULL;
	}

	if (s)
		PF_STATE_UNLOCK(s);

	if (ifp == NULL)
		goto bad;

	if (oifp != ifp) {
		if (pf_test6(PF_OUT, PFIL_FWD, ifp, &m0, inp) != PF_PASS)
			goto bad;
		else if (m0 == NULL)
			goto done;
		if (m0->m_len < sizeof(struct ip6_hdr)) {
			DPFPRINTF(PF_DEBUG_URGENT,
			    ("%s: m0->m_len < sizeof(struct ip6_hdr)\n",
			    __func__));
			goto bad;
		}
		ip6 = mtod(m0, struct ip6_hdr *);
	}

	if (ifp->if_flags & IFF_LOOPBACK)
		m0->m_flags |= M_SKIP_FIREWALL;

	if (m0->m_pkthdr.csum_flags & CSUM_DELAY_DATA_IPV6 &
	    ~ifp->if_hwassist) {
		uint32_t plen = m0->m_pkthdr.len - sizeof(*ip6);
		in6_delayed_cksum(m0, plen, sizeof(struct ip6_hdr));
		m0->m_pkthdr.csum_flags &= ~CSUM_DELAY_DATA_IPV6;
	}

	/*
	 * If the packet is too large for the outgoing interface,
	 * send back an icmp6 error.
	 */
	if (IN6_IS_SCOPE_EMBED(&dst.sin6_addr))
		dst.sin6_addr.s6_addr16[1] = htons(ifp->if_index);
	if ((u_long)m0->m_pkthdr.len <= ifp->if_mtu)
		nd6_output_ifp(ifp, ifp, m0, &dst, NULL);
	else {
		in6_ifstat_inc(ifp, ifs6_in_toobig);
		if (r->rt != PF_DUPTO)
			icmp6_error(m0, ICMP6_PACKET_TOO_BIG, 0, ifp->if_mtu);
		else
			goto bad;
	}

done:
	if (r->rt != PF_DUPTO)
		*m = NULL;
	return;

bad_locked:
	if (s)
		PF_STATE_UNLOCK(s);
bad:
	m_freem(m0);
	goto done;
}
#endif /* INET6 */

/*
 * FreeBSD supports cksum offloads for the following drivers.
 *  em(4), fxp(4), lge(4), ndis(4), nge(4), re(4), ti(4), txp(4), xl(4)
 *
 * CSUM_DATA_VALID | CSUM_PSEUDO_HDR :
 *  network driver performed cksum including pseudo header, need to verify
 *   csum_data
 * CSUM_DATA_VALID :
 *  network driver performed cksum, needs to additional pseudo header
 *  cksum computation with partial csum_data(i.e. lack of H/W support for
 *  pseudo header, for instance hme(4), sk(4) and possibly gem(4))
 *
 * After validating the cksum of packet, set both flag CSUM_DATA_VALID and
 * CSUM_PSEUDO_HDR in order to avoid recomputation of the cksum in upper
 * TCP/UDP layer.
 * Also, set csum_data to 0xffff to force cksum validation.
 */
static int
pf_check_proto_cksum(struct mbuf *m, int off, int len, u_int8_t p, sa_family_t af)
{
	u_int16_t sum = 0;
	int hw_assist = 0;
	struct ip *ip;

	if (off < sizeof(struct ip) || len < sizeof(struct udphdr))
		return (1);
	if (m->m_pkthdr.len < off + len)
		return (1);

	switch (p) {
	case IPPROTO_TCP:
		if (m->m_pkthdr.csum_flags & CSUM_DATA_VALID) {
			if (m->m_pkthdr.csum_flags & CSUM_PSEUDO_HDR) {
				sum = m->m_pkthdr.csum_data;
			} else {
				ip = mtod(m, struct ip *);
				sum = in_pseudo(ip->ip_src.s_addr,
				ip->ip_dst.s_addr, htonl((u_short)len +
				m->m_pkthdr.csum_data + IPPROTO_TCP));
			}
			sum ^= 0xffff;
			++hw_assist;
		}
		break;
	case IPPROTO_UDP:
		if (m->m_pkthdr.csum_flags & CSUM_DATA_VALID) {
			if (m->m_pkthdr.csum_flags & CSUM_PSEUDO_HDR) {
				sum = m->m_pkthdr.csum_data;
			} else {
				ip = mtod(m, struct ip *);
				sum = in_pseudo(ip->ip_src.s_addr,
				ip->ip_dst.s_addr, htonl((u_short)len +
				m->m_pkthdr.csum_data + IPPROTO_UDP));
			}
			sum ^= 0xffff;
			++hw_assist;
		}
		break;
	case IPPROTO_ICMP:
#ifdef INET6
	case IPPROTO_ICMPV6:
#endif /* INET6 */
		break;
	default:
		return (1);
	}

	if (!hw_assist) {
		switch (af) {
		case AF_INET:
			if (p == IPPROTO_ICMP) {
				if (m->m_len < off)
					return (1);
				m->m_data += off;
				m->m_len -= off;
				sum = in_cksum(m, len);
				m->m_data -= off;
				m->m_len += off;
			} else {
				if (m->m_len < sizeof(struct ip))
					return (1);
				sum = in4_cksum(m, p, off, len);
			}
			break;
#ifdef INET6
		case AF_INET6:
			if (m->m_len < sizeof(struct ip6_hdr))
				return (1);
			sum = in6_cksum(m, p, off, len);
			break;
#endif /* INET6 */
		default:
			return (1);
		}
	}
	if (sum) {
		switch (p) {
		case IPPROTO_TCP:
		    {
			KMOD_TCPSTAT_INC(tcps_rcvbadsum);
			break;
		    }
		case IPPROTO_UDP:
		    {
			KMOD_UDPSTAT_INC(udps_badsum);
			break;
		    }
#ifdef INET
		case IPPROTO_ICMP:
		    {
			KMOD_ICMPSTAT_INC(icps_checksum);
			break;
		    }
#endif
#ifdef INET6
		case IPPROTO_ICMPV6:
		    {
			KMOD_ICMP6STAT_INC(icp6s_checksum);
			break;
		    }
#endif /* INET6 */
		}
		return (1);
	} else {
		if (p == IPPROTO_TCP || p == IPPROTO_UDP) {
			m->m_pkthdr.csum_flags |=
			    (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
			m->m_pkthdr.csum_data = 0xffff;
		}
	}
	return (0);
}


#ifdef INET
int
pf_test(int dir, int pflags, struct ifnet *ifp, struct mbuf **m0, struct inpcb *inp)
{
	struct pfi_kif		*kif;
	u_short			 action, reason = 0, log = 0;
	struct mbuf		*m = *m0;
	struct ip		*h = NULL;
	struct m_tag		*ipfwtag;
	struct pf_rule		*a = NULL, *r = &V_pf_default_rule, *tr, *nr;
	struct pf_state		*s = NULL;
	struct pf_ruleset	*ruleset = NULL;
	struct pf_pdesc		 pd;
	int			 off, dirndx, pqid = 0;

	PF_RULES_RLOCK_TRACKER;

	M_ASSERTPKTHDR(m);

	if (!V_pf_status.running)
		return (PF_PASS);

	memset(&pd, 0, sizeof(pd));

	kif = (struct pfi_kif *)ifp->if_pf_kif;

	if (kif == NULL) {
		DPFPRINTF(PF_DEBUG_URGENT,
		    ("pf_test: kif == NULL, if_xname %s\n", ifp->if_xname));
		return (PF_DROP);
	}
	if (kif->pfik_flags & PFI_IFLAG_SKIP)
		return (PF_PASS);

	if (m->m_flags & M_SKIP_FIREWALL)
		return (PF_PASS);

	pd.pf_mtag = pf_find_mtag(m);

	PF_RULES_RLOCK();

	if (ip_divert_ptr != NULL &&
	    ((ipfwtag = m_tag_locate(m, MTAG_IPFW_RULE, 0, NULL)) != NULL)) {
		struct ipfw_rule_ref *rr = (struct ipfw_rule_ref *)(ipfwtag+1);
		if (rr->info & IPFW_IS_DIVERT && rr->rulenum == 0) {
			if (pd.pf_mtag == NULL &&
			    ((pd.pf_mtag = pf_get_mtag(m)) == NULL)) {
				action = PF_DROP;
				goto done;
			}
			pd.pf_mtag->flags |= PF_PACKET_LOOPED;
			m_tag_delete(m, ipfwtag);
		}
		if (pd.pf_mtag && pd.pf_mtag->flags & PF_FASTFWD_OURS_PRESENT) {
			m->m_flags |= M_FASTFWD_OURS;
			pd.pf_mtag->flags &= ~PF_FASTFWD_OURS_PRESENT;
		}
	} else if (pf_normalize_ip(m0, dir, kif, &reason, &pd) != PF_PASS) {
		/* We do IP header normalization and packet reassembly here */
		action = PF_DROP;
		goto done;
	}
	m = *m0;	/* pf_normalize messes with m0 */
	h = mtod(m, struct ip *);

	off = h->ip_hl << 2;
	if (off < (int)sizeof(struct ip)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_SHORT);
		log = 1;
		goto done;
	}

	pd.src = (struct pf_addr *)&h->ip_src;
	pd.dst = (struct pf_addr *)&h->ip_dst;
	pd.sport = pd.dport = NULL;
	pd.ip_sum = &h->ip_sum;
	pd.proto_sum = NULL;
	pd.proto = h->ip_p;
	pd.dir = dir;
	pd.sidx = (dir == PF_IN) ? 0 : 1;
	pd.didx = (dir == PF_IN) ? 1 : 0;
	pd.af = AF_INET;
	pd.tos = h->ip_tos & ~IPTOS_ECN_MASK;
	pd.tot_len = ntohs(h->ip_len);

	/* handle fragments that didn't get reassembled by normalization */
	if (h->ip_off & htons(IP_MF | IP_OFFMASK)) {
		action = pf_test_fragment(&r, dir, kif, m, h,
		    &pd, &a, &ruleset);
		goto done;
	}

	switch (h->ip_p) {

	case IPPROTO_TCP: {
		struct tcphdr	th;

		pd.hdr.tcp = &th;
		if (!pf_pull_hdr(m, off, &th, sizeof(th),
		    &action, &reason, AF_INET)) {
			log = action != PF_PASS;
			goto done;
		}
		pd.p_len = pd.tot_len - off - (th.th_off << 2);
		if ((th.th_flags & TH_ACK) && pd.p_len == 0)
			pqid = 1;
		action = pf_normalize_tcp(dir, kif, m, 0, off, h, &pd);
		if (action == PF_DROP)
			goto done;
		action = pf_test_state_tcp(&s, dir, kif, m, off, h, &pd,
		    &reason);
		if (action == PF_PASS) {
			if (V_pfsync_update_state_ptr != NULL)
				V_pfsync_update_state_ptr(s);
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL)
			action = pf_test_rule(&r, &s, dir, kif, m, off, &pd,
			    &a, &ruleset, inp);
		break;
	}

	case IPPROTO_UDP: {
		struct udphdr	uh;

		pd.hdr.udp = &uh;
		if (!pf_pull_hdr(m, off, &uh, sizeof(uh),
		    &action, &reason, AF_INET)) {
			log = action != PF_PASS;
			goto done;
		}
		if (uh.uh_dport == 0 ||
		    ntohs(uh.uh_ulen) > m->m_pkthdr.len - off ||
		    ntohs(uh.uh_ulen) < sizeof(struct udphdr)) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_SHORT);
			goto done;
		}
		action = pf_test_state_udp(&s, dir, kif, m, off, h, &pd);
		if (action == PF_PASS) {
			if (V_pfsync_update_state_ptr != NULL)
				V_pfsync_update_state_ptr(s);
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL)
			action = pf_test_rule(&r, &s, dir, kif, m, off, &pd,
			    &a, &ruleset, inp);
		break;
	}

	case IPPROTO_ICMP: {
		struct icmp	ih;

		pd.hdr.icmp = &ih;
		if (!pf_pull_hdr(m, off, &ih, ICMP_MINLEN,
		    &action, &reason, AF_INET)) {
			log = action != PF_PASS;
			goto done;
		}
		action = pf_test_state_icmp(&s, dir, kif, m, off, h, &pd,
		    &reason);
		if (action == PF_PASS) {
			if (V_pfsync_update_state_ptr != NULL)
				V_pfsync_update_state_ptr(s);
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL)
			action = pf_test_rule(&r, &s, dir, kif, m, off, &pd,
			    &a, &ruleset, inp);
		break;
	}

#ifdef INET6
	case IPPROTO_ICMPV6: {
		action = PF_DROP;
		DPFPRINTF(PF_DEBUG_MISC,
		    ("pf: dropping IPv4 packet with ICMPv6 payload\n"));
		goto done;
	}
#endif

	default:
		action = pf_test_state_other(&s, dir, kif, m, &pd);
		if (action == PF_PASS) {
			if (V_pfsync_update_state_ptr != NULL)
				V_pfsync_update_state_ptr(s);
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL)
			action = pf_test_rule(&r, &s, dir, kif, m, off, &pd,
			    &a, &ruleset, inp);
		break;
	}

done:
	PF_RULES_RUNLOCK();
	if (action == PF_PASS && h->ip_hl > 5 &&
	    !((s && s->state_flags & PFSTATE_ALLOWOPTS) || r->allow_opts)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_IPOPTIONS);
		log = r->log;
		DPFPRINTF(PF_DEBUG_MISC,
		    ("pf: dropping packet with ip options\n"));
	}

	if (s && s->tag > 0 && pf_tag_packet(m, &pd, s->tag)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_MEMORY);
	}
	if (r->rtableid >= 0)
		M_SETFIB(m, r->rtableid);

	if (r->scrub_flags & PFSTATE_SETPRIO) {
		if (pd.tos & IPTOS_LOWDELAY)
			pqid = 1;
		if (pf_ieee8021q_setpcp(m, r->set_prio[pqid])) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_MEMORY);
			log = 1;
			DPFPRINTF(PF_DEBUG_MISC,
			    ("pf: failed to allocate 802.1q mtag\n"));
		}
	}

#ifdef ALTQ
	if (action == PF_PASS && r->qid) {
		if (pd.pf_mtag == NULL &&
		    ((pd.pf_mtag = pf_get_mtag(m)) == NULL)) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_MEMORY);
		} else {
			if (s != NULL)
				pd.pf_mtag->qid_hash = pf_state_hash(s);
			if (pqid || (pd.tos & IPTOS_LOWDELAY))
				pd.pf_mtag->qid = r->pqid;
			else
				pd.pf_mtag->qid = r->qid;
			/* Add hints for ecn. */
			pd.pf_mtag->hdr = h;
		}

	}
#endif /* ALTQ */

	/*
	 * connections redirected to loopback should not match sockets
	 * bound specifically to loopback due to security implications,
	 * see tcp_input() and in_pcblookup_listen().
	 */
	if (dir == PF_IN && action == PF_PASS && (pd.proto == IPPROTO_TCP ||
	    pd.proto == IPPROTO_UDP) && s != NULL && s->nat_rule.ptr != NULL &&
	    (s->nat_rule.ptr->action == PF_RDR ||
	    s->nat_rule.ptr->action == PF_BINAT) &&
	    (ntohl(pd.dst->v4.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET)
		m->m_flags |= M_SKIP_FIREWALL;

	if (action == PF_PASS && r->divert.port && ip_divert_ptr != NULL &&
	    !PACKET_LOOPED(&pd)) {

		ipfwtag = m_tag_alloc(MTAG_IPFW_RULE, 0,
		    sizeof(struct ipfw_rule_ref), M_NOWAIT | M_ZERO);
		if (ipfwtag != NULL) {
			((struct ipfw_rule_ref *)(ipfwtag+1))->info =
			    ntohs(r->divert.port);
			((struct ipfw_rule_ref *)(ipfwtag+1))->rulenum = dir;

			if (s)
				PF_STATE_UNLOCK(s);

			m_tag_prepend(m, ipfwtag);
			if (m->m_flags & M_FASTFWD_OURS) {
				if (pd.pf_mtag == NULL &&
				    ((pd.pf_mtag = pf_get_mtag(m)) == NULL)) {
					action = PF_DROP;
					REASON_SET(&reason, PFRES_MEMORY);
					log = 1;
					DPFPRINTF(PF_DEBUG_MISC,
					    ("pf: failed to allocate tag\n"));
				} else {
					pd.pf_mtag->flags |=
					    PF_FASTFWD_OURS_PRESENT;
					m->m_flags &= ~M_FASTFWD_OURS;
				}
			}
			ip_divert_ptr(*m0, dir == PF_IN);
			*m0 = NULL;

			return (action);
		} else {
			/* XXX: ipfw has the same behaviour! */
			action = PF_DROP;
			REASON_SET(&reason, PFRES_MEMORY);
			log = 1;
			DPFPRINTF(PF_DEBUG_MISC,
			    ("pf: failed to allocate divert tag\n"));
		}
	}

	if (log) {
		struct pf_rule *lr;

		if (s != NULL && s->nat_rule.ptr != NULL &&
		    s->nat_rule.ptr->log & PF_LOG_ALL)
			lr = s->nat_rule.ptr;
		else
			lr = r;
		PFLOG_PACKET(kif, m, AF_INET, dir, reason, lr, a, ruleset, &pd,
		    (s == NULL));
	}

	kif->pfik_bytes[0][dir == PF_OUT][action != PF_PASS] += pd.tot_len;
	kif->pfik_packets[0][dir == PF_OUT][action != PF_PASS]++;

	if (action == PF_PASS || r->action == PF_DROP) {
		dirndx = (dir == PF_OUT);
		r->packets[dirndx]++;
		r->bytes[dirndx] += pd.tot_len;
		if (a != NULL) {
			a->packets[dirndx]++;
			a->bytes[dirndx] += pd.tot_len;
		}
		if (s != NULL) {
			if (s->nat_rule.ptr != NULL) {
				s->nat_rule.ptr->packets[dirndx]++;
				s->nat_rule.ptr->bytes[dirndx] += pd.tot_len;
			}
			if (s->src_node != NULL) {
				s->src_node->packets[dirndx]++;
				s->src_node->bytes[dirndx] += pd.tot_len;
			}
			if (s->nat_src_node != NULL) {
				s->nat_src_node->packets[dirndx]++;
				s->nat_src_node->bytes[dirndx] += pd.tot_len;
			}
			dirndx = (dir == s->direction) ? 0 : 1;
			s->packets[dirndx]++;
			s->bytes[dirndx] += pd.tot_len;
		}
		tr = r;
		nr = (s != NULL) ? s->nat_rule.ptr : pd.nat_rule;
		if (nr != NULL && r == &V_pf_default_rule)
			tr = nr;
		if (tr->src.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(tr->src.addr.p.tbl,
			    (s == NULL) ? pd.src :
			    &s->key[(s->direction == PF_IN)]->
				addr[(s->direction == PF_OUT)],
			    pd.af, pd.tot_len, dir == PF_OUT,
			    r->action == PF_PASS, tr->src.neg);
		if (tr->dst.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(tr->dst.addr.p.tbl,
			    (s == NULL) ? pd.dst :
			    &s->key[(s->direction == PF_IN)]->
				addr[(s->direction == PF_IN)],
			    pd.af, pd.tot_len, dir == PF_OUT,
			    r->action == PF_PASS, tr->dst.neg);
	}

	switch (action) {
	case PF_SYNPROXY_DROP:
		m_freem(*m0);
	case PF_DEFER:
		*m0 = NULL;
		action = PF_PASS;
		break;
	case PF_DROP:
		m_freem(*m0);
		*m0 = NULL;
		break;
	default:
		/* pf_route() returns unlocked. */
		if (r->rt) {
			pf_route(m0, r, dir, kif->pfik_ifp, s, &pd, inp);
			return (action);
		}
		break;
	}
	if (s)
		PF_STATE_UNLOCK(s);

	return (action);
}
#endif /* INET */

#ifdef INET6
int
pf_test6(int dir, int pflags, struct ifnet *ifp, struct mbuf **m0, struct inpcb *inp)
{
	struct pfi_kif		*kif;
	u_short			 action, reason = 0, log = 0;
	struct mbuf		*m = *m0, *n = NULL;
	struct m_tag		*mtag;
	struct ip6_hdr		*h = NULL;
	struct pf_rule		*a = NULL, *r = &V_pf_default_rule, *tr, *nr;
	struct pf_state		*s = NULL;
	struct pf_ruleset	*ruleset = NULL;
	struct pf_pdesc		 pd;
	int			 off, terminal = 0, dirndx, rh_cnt = 0, pqid = 0;

	PF_RULES_RLOCK_TRACKER;
	M_ASSERTPKTHDR(m);

	if (!V_pf_status.running)
		return (PF_PASS);

	memset(&pd, 0, sizeof(pd));
	pd.pf_mtag = pf_find_mtag(m);

	if (pd.pf_mtag && pd.pf_mtag->flags & PF_TAG_GENERATED)
		return (PF_PASS);

	kif = (struct pfi_kif *)ifp->if_pf_kif;
	if (kif == NULL) {
		DPFPRINTF(PF_DEBUG_URGENT,
		    ("pf_test6: kif == NULL, if_xname %s\n", ifp->if_xname));
		return (PF_DROP);
	}
	if (kif->pfik_flags & PFI_IFLAG_SKIP)
		return (PF_PASS);

	if (m->m_flags & M_SKIP_FIREWALL)
		return (PF_PASS);

	PF_RULES_RLOCK();

	/* We do IP header normalization and packet reassembly here */
	if (pf_normalize_ip6(m0, dir, kif, &reason, &pd) != PF_PASS) {
		action = PF_DROP;
		goto done;
	}
	m = *m0;	/* pf_normalize messes with m0 */
	h = mtod(m, struct ip6_hdr *);

#if 1
	/*
	 * we do not support jumbogram yet.  if we keep going, zero ip6_plen
	 * will do something bad, so drop the packet for now.
	 */
	if (htons(h->ip6_plen) == 0) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_NORM);	/*XXX*/
		goto done;
	}
#endif

	pd.src = (struct pf_addr *)&h->ip6_src;
	pd.dst = (struct pf_addr *)&h->ip6_dst;
	pd.sport = pd.dport = NULL;
	pd.ip_sum = NULL;
	pd.proto_sum = NULL;
	pd.dir = dir;
	pd.sidx = (dir == PF_IN) ? 0 : 1;
	pd.didx = (dir == PF_IN) ? 1 : 0;
	pd.af = AF_INET6;
	pd.tos = 0;
	pd.tot_len = ntohs(h->ip6_plen) + sizeof(struct ip6_hdr);

	off = ((caddr_t)h - m->m_data) + sizeof(struct ip6_hdr);
	pd.proto = h->ip6_nxt;
	do {
		switch (pd.proto) {
		case IPPROTO_FRAGMENT:
			action = pf_test_fragment(&r, dir, kif, m, h,
			    &pd, &a, &ruleset);
			if (action == PF_DROP)
				REASON_SET(&reason, PFRES_FRAG);
			goto done;
		case IPPROTO_ROUTING: {
			struct ip6_rthdr rthdr;

			if (rh_cnt++) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: IPv6 more than one rthdr\n"));
				action = PF_DROP;
				REASON_SET(&reason, PFRES_IPOPTIONS);
				log = 1;
				goto done;
			}
			if (!pf_pull_hdr(m, off, &rthdr, sizeof(rthdr), NULL,
			    &reason, pd.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: IPv6 short rthdr\n"));
				action = PF_DROP;
				REASON_SET(&reason, PFRES_SHORT);
				log = 1;
				goto done;
			}
			if (rthdr.ip6r_type == IPV6_RTHDR_TYPE_0) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: IPv6 rthdr0\n"));
				action = PF_DROP;
				REASON_SET(&reason, PFRES_IPOPTIONS);
				log = 1;
				goto done;
			}
			/* FALLTHROUGH */
		}
		case IPPROTO_AH:
		case IPPROTO_HOPOPTS:
		case IPPROTO_DSTOPTS: {
			/* get next header and header length */
			struct ip6_ext	opt6;

			if (!pf_pull_hdr(m, off, &opt6, sizeof(opt6),
			    NULL, &reason, pd.af)) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: IPv6 short opt\n"));
				action = PF_DROP;
				log = 1;
				goto done;
			}
			if (pd.proto == IPPROTO_AH)
				off += (opt6.ip6e_len + 2) * 4;
			else
				off += (opt6.ip6e_len + 1) * 8;
			pd.proto = opt6.ip6e_nxt;
			/* goto the next header */
			break;
		}
		default:
			terminal++;
			break;
		}
	} while (!terminal);

	/* if there's no routing header, use unmodified mbuf for checksumming */
	if (!n)
		n = m;

	switch (pd.proto) {

	case IPPROTO_TCP: {
		struct tcphdr	th;

		pd.hdr.tcp = &th;
		if (!pf_pull_hdr(m, off, &th, sizeof(th),
		    &action, &reason, AF_INET6)) {
			log = action != PF_PASS;
			goto done;
		}
		pd.p_len = pd.tot_len - off - (th.th_off << 2);
		action = pf_normalize_tcp(dir, kif, m, 0, off, h, &pd);
		if (action == PF_DROP)
			goto done;
		action = pf_test_state_tcp(&s, dir, kif, m, off, h, &pd,
		    &reason);
		if (action == PF_PASS) {
			if (V_pfsync_update_state_ptr != NULL)
				V_pfsync_update_state_ptr(s);
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL)
			action = pf_test_rule(&r, &s, dir, kif, m, off, &pd,
			    &a, &ruleset, inp);
		break;
	}

	case IPPROTO_UDP: {
		struct udphdr	uh;

		pd.hdr.udp = &uh;
		if (!pf_pull_hdr(m, off, &uh, sizeof(uh),
		    &action, &reason, AF_INET6)) {
			log = action != PF_PASS;
			goto done;
		}
		if (uh.uh_dport == 0 ||
		    ntohs(uh.uh_ulen) > m->m_pkthdr.len - off ||
		    ntohs(uh.uh_ulen) < sizeof(struct udphdr)) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_SHORT);
			goto done;
		}
		action = pf_test_state_udp(&s, dir, kif, m, off, h, &pd);
		if (action == PF_PASS) {
			if (V_pfsync_update_state_ptr != NULL)
				V_pfsync_update_state_ptr(s);
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL)
			action = pf_test_rule(&r, &s, dir, kif, m, off, &pd,
			    &a, &ruleset, inp);
		break;
	}

	case IPPROTO_ICMP: {
		action = PF_DROP;
		DPFPRINTF(PF_DEBUG_MISC,
		    ("pf: dropping IPv6 packet with ICMPv4 payload\n"));
		goto done;
	}

	case IPPROTO_ICMPV6: {
		struct icmp6_hdr	ih;

		pd.hdr.icmp6 = &ih;
		if (!pf_pull_hdr(m, off, &ih, sizeof(ih),
		    &action, &reason, AF_INET6)) {
			log = action != PF_PASS;
			goto done;
		}
		action = pf_test_state_icmp(&s, dir, kif,
		    m, off, h, &pd, &reason);
		if (action == PF_PASS) {
			if (V_pfsync_update_state_ptr != NULL)
				V_pfsync_update_state_ptr(s);
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL)
			action = pf_test_rule(&r, &s, dir, kif, m, off, &pd,
			    &a, &ruleset, inp);
		break;
	}

	default:
		action = pf_test_state_other(&s, dir, kif, m, &pd);
		if (action == PF_PASS) {
			if (V_pfsync_update_state_ptr != NULL)
				V_pfsync_update_state_ptr(s);
			r = s->rule.ptr;
			a = s->anchor.ptr;
			log = s->log;
		} else if (s == NULL)
			action = pf_test_rule(&r, &s, dir, kif, m, off, &pd,
			    &a, &ruleset, inp);
		break;
	}

done:
	PF_RULES_RUNLOCK();
	if (n != m) {
		m_freem(n);
		n = NULL;
	}

	/* handle dangerous IPv6 extension headers. */
	if (action == PF_PASS && rh_cnt &&
	    !((s && s->state_flags & PFSTATE_ALLOWOPTS) || r->allow_opts)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_IPOPTIONS);
		log = r->log;
		DPFPRINTF(PF_DEBUG_MISC,
		    ("pf: dropping packet with dangerous v6 headers\n"));
	}

	if (s && s->tag > 0 && pf_tag_packet(m, &pd, s->tag)) {
		action = PF_DROP;
		REASON_SET(&reason, PFRES_MEMORY);
	}
	if (r->rtableid >= 0)
		M_SETFIB(m, r->rtableid);

	if (r->scrub_flags & PFSTATE_SETPRIO) {
		if (pd.tos & IPTOS_LOWDELAY)
			pqid = 1;
		if (pf_ieee8021q_setpcp(m, r->set_prio[pqid])) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_MEMORY);
			log = 1;
			DPFPRINTF(PF_DEBUG_MISC,
			    ("pf: failed to allocate 802.1q mtag\n"));
		}
	}

#ifdef ALTQ
	if (action == PF_PASS && r->qid) {
		if (pd.pf_mtag == NULL &&
		    ((pd.pf_mtag = pf_get_mtag(m)) == NULL)) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_MEMORY);
		} else {
			if (s != NULL)
				pd.pf_mtag->qid_hash = pf_state_hash(s);
			if (pd.tos & IPTOS_LOWDELAY)
				pd.pf_mtag->qid = r->pqid;
			else
				pd.pf_mtag->qid = r->qid;
			/* Add hints for ecn. */
			pd.pf_mtag->hdr = h;
		}
	}
#endif /* ALTQ */

	if (dir == PF_IN && action == PF_PASS && (pd.proto == IPPROTO_TCP ||
	    pd.proto == IPPROTO_UDP) && s != NULL && s->nat_rule.ptr != NULL &&
	    (s->nat_rule.ptr->action == PF_RDR ||
	    s->nat_rule.ptr->action == PF_BINAT) &&
	    IN6_IS_ADDR_LOOPBACK(&pd.dst->v6))
		m->m_flags |= M_SKIP_FIREWALL;

	/* XXX: Anybody working on it?! */
	if (r->divert.port)
		printf("pf: divert(9) is not supported for IPv6\n");

	if (log) {
		struct pf_rule *lr;

		if (s != NULL && s->nat_rule.ptr != NULL &&
		    s->nat_rule.ptr->log & PF_LOG_ALL)
			lr = s->nat_rule.ptr;
		else
			lr = r;
		PFLOG_PACKET(kif, m, AF_INET6, dir, reason, lr, a, ruleset,
		    &pd, (s == NULL));
	}

	kif->pfik_bytes[1][dir == PF_OUT][action != PF_PASS] += pd.tot_len;
	kif->pfik_packets[1][dir == PF_OUT][action != PF_PASS]++;

	if (action == PF_PASS || r->action == PF_DROP) {
		dirndx = (dir == PF_OUT);
		r->packets[dirndx]++;
		r->bytes[dirndx] += pd.tot_len;
		if (a != NULL) {
			a->packets[dirndx]++;
			a->bytes[dirndx] += pd.tot_len;
		}
		if (s != NULL) {
			if (s->nat_rule.ptr != NULL) {
				s->nat_rule.ptr->packets[dirndx]++;
				s->nat_rule.ptr->bytes[dirndx] += pd.tot_len;
			}
			if (s->src_node != NULL) {
				s->src_node->packets[dirndx]++;
				s->src_node->bytes[dirndx] += pd.tot_len;
			}
			if (s->nat_src_node != NULL) {
				s->nat_src_node->packets[dirndx]++;
				s->nat_src_node->bytes[dirndx] += pd.tot_len;
			}
			dirndx = (dir == s->direction) ? 0 : 1;
			s->packets[dirndx]++;
			s->bytes[dirndx] += pd.tot_len;
		}
		tr = r;
		nr = (s != NULL) ? s->nat_rule.ptr : pd.nat_rule;
		if (nr != NULL && r == &V_pf_default_rule)
			tr = nr;
		if (tr->src.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(tr->src.addr.p.tbl,
			    (s == NULL) ? pd.src :
			    &s->key[(s->direction == PF_IN)]->addr[0],
			    pd.af, pd.tot_len, dir == PF_OUT,
			    r->action == PF_PASS, tr->src.neg);
		if (tr->dst.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(tr->dst.addr.p.tbl,
			    (s == NULL) ? pd.dst :
			    &s->key[(s->direction == PF_IN)]->addr[1],
			    pd.af, pd.tot_len, dir == PF_OUT,
			    r->action == PF_PASS, tr->dst.neg);
	}

	switch (action) {
	case PF_SYNPROXY_DROP:
		m_freem(*m0);
	case PF_DEFER:
		*m0 = NULL;
		action = PF_PASS;
		break;
	case PF_DROP:
		m_freem(*m0);
		*m0 = NULL;
		break;
	default:
		/* pf_route6() returns unlocked. */
		if (r->rt) {
			pf_route6(m0, r, dir, kif->pfik_ifp, s, &pd, inp);
			return (action);
		}
		break;
	}

	if (s)
		PF_STATE_UNLOCK(s);

	/* If reassembled packet passed, create new fragments. */
	if (action == PF_PASS && *m0 && (pflags & PFIL_FWD) &&
	    (mtag = m_tag_find(m, PF_REASSEMBLED, NULL)) != NULL)
		action = pf_refragment6(ifp, m0, mtag);

	return (action);
}
#endif /* INET6 */
