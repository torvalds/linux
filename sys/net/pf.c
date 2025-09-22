/*	$OpenBSD: pf.c,v 1.1218 2025/07/07 02:28:50 jsg Exp $ */

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2013 Henning Brauer <henning@openbsd.org>
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
 */

#include "carp.h"
#include "pflog.h"
#include "pfsync.h"
#include "pflow.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/time.h>
#include <sys/pool.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/syslog.h>

#include <crypto/sha2.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/route.h>
#include <net/toeplitz.h>

#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_fsm.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/ip_divert.h>

#ifdef INET6
#include <netinet6/in6_var.h>
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#include <netinet/icmp6.h>
#endif /* INET6 */

#include <net/pfvar.h>
#include <net/pfvar_priv.h>

#if NPFLOW > 0
#include <net/if_pflow.h>
#endif	/* NPFLOW > 0 */

#if NPFSYNC > 0
#include <net/if_pfsync.h>
#endif /* NPFSYNC > 0 */

/*
 * Global variables
 */
struct pf_state_tree	 pf_statetbl;
struct pf_queuehead	 pf_queues[2];
struct pf_queuehead	*pf_queues_active;
struct pf_queuehead	*pf_queues_inactive;

struct pf_status	 pf_status;

struct mutex		 pf_inp_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);

int			 pf_hdr_limit = 20;  /* arbitrary limit, tune in ddb */

SHA2_CTX		 pf_tcp_secret_ctx;
u_char			 pf_tcp_secret[16];
int			 pf_tcp_secret_init;
int			 pf_tcp_iss_off;

enum pf_test_status {
	PF_TEST_FAIL = -1,
	PF_TEST_OK,
	PF_TEST_QUICK
};

struct pf_test_ctx {
	struct pf_pdesc		 *pd;
	struct pf_rule_actions	  act;
	u_int8_t		  icmpcode;
	u_int8_t		  icmptype;
	int			  icmp_dir;
	int			  state_icmp;
	int			  tag;
	u_short			  reason;
	struct pf_rule_item	 *ri;
	struct pf_src_node	 *sns[PF_SN_MAX];
	struct pf_rule_slist	  rules;
	struct pf_rule		 *nr;
	struct pf_rule		**rm;
	struct pf_rule		 *a;
	struct pf_rule		**am;
	struct pf_ruleset	**rsm;
	struct pf_ruleset	 *arsm;
	struct pf_ruleset	 *aruleset;
	struct tcphdr		 *th;
};

struct pool		 pf_src_tree_pl, pf_rule_pl, pf_queue_pl;
struct pool		 pf_state_pl, pf_state_key_pl, pf_state_item_pl;
struct pool		 pf_rule_item_pl, pf_sn_item_pl, pf_pktdelay_pl;

void			 pf_add_threshold(struct pf_threshold *);
int			 pf_check_threshold(struct pf_threshold *);
int			 pf_check_tcp_cksum(struct mbuf *, int, int,
			    sa_family_t);
__inline void		 pf_cksum_fixup(u_int16_t *, u_int16_t, u_int16_t,
			    u_int8_t);
void			 pf_cksum_fixup_a(u_int16_t *, const struct pf_addr *,
			    const struct pf_addr *, sa_family_t, u_int8_t);
int			 pf_modulate_sack(struct pf_pdesc *,
			    struct pf_state_peer *);
int			 pf_icmp_mapping(struct pf_pdesc *, u_int8_t, int *,
			    u_int16_t *, u_int16_t *);
int			 pf_change_icmp_af(struct mbuf *, int,
			    struct pf_pdesc *, struct pf_pdesc *,
			    struct pf_addr *, struct pf_addr *, sa_family_t,
			    sa_family_t);
int			 pf_translate_a(struct pf_pdesc *, struct pf_addr *,
			    struct pf_addr *);
void			 pf_translate_icmp(struct pf_pdesc *, struct pf_addr *,
			    u_int16_t *, struct pf_addr *, struct pf_addr *,
			    u_int16_t);
int			 pf_translate_icmp_af(struct pf_pdesc*, int, void *);
void			 pf_send_icmp(struct mbuf *, u_int8_t, u_int8_t, int,
			    sa_family_t, struct pf_rule *, u_int);
void			 pf_detach_state(struct pf_state *);
struct pf_state_key	*pf_state_key_attach(struct pf_state_key *,
			     struct pf_state *, int);
void			 pf_state_key_detach(struct pf_state *, int);
u_int32_t		 pf_tcp_iss(struct pf_pdesc *);
void			 pf_rule_to_actions(struct pf_rule *,
			    struct pf_rule_actions *);
int			 pf_test_rule(struct pf_pdesc *, struct pf_rule **,
			    struct pf_state **, struct pf_rule **,
			    struct pf_ruleset **, u_short *);
static __inline int	 pf_create_state(struct pf_pdesc *, struct pf_rule *,
			    struct pf_rule *, struct pf_rule *,
			    struct pf_state_key **, struct pf_state_key **,
			    int *, struct pf_state **, int,
			    struct pf_rule_slist *, struct pf_rule_actions *,
			    struct pf_src_node **);
static __inline int	 pf_state_key_addr_setup(struct pf_pdesc *, void *,
			    int, struct pf_addr *, int, struct pf_addr *,
			    int, int);
int			 pf_state_key_setup(struct pf_pdesc *, struct
			    pf_state_key **, struct pf_state_key **, int);
int			 pf_tcp_track_full(struct pf_pdesc *,
			    struct pf_state **, u_short *, int *, int);
int			 pf_tcp_track_sloppy(struct pf_pdesc *,
			    struct pf_state **, u_short *);
static __inline int	 pf_synproxy(struct pf_pdesc *, struct pf_state **,
			    u_short *);
int			 pf_test_state(struct pf_pdesc *, struct pf_state **,
			    u_short *);
int			 pf_icmp_state_lookup(struct pf_pdesc *,
			    struct pf_state_key_cmp *, struct pf_state **,
			    u_int16_t, u_int16_t, int, int *, int, int);
int			 pf_test_state_icmp(struct pf_pdesc *,
			    struct pf_state **, u_short *);
u_int16_t		 pf_calc_mss(struct pf_addr *, sa_family_t, int,
			    uint16_t, uint16_t);
static __inline int	 pf_set_rt_ifp(struct pf_state *, struct pf_addr *,
			    sa_family_t, struct pf_src_node **);
struct pf_divert	*pf_get_divert(struct mbuf *);
int			 pf_walk_option(struct pf_pdesc *, struct ip *,
			    int, int, u_short *);
int			 pf_walk_header(struct pf_pdesc *, struct ip *,
			    u_short *);
int			 pf_walk_option6(struct pf_pdesc *, struct ip6_hdr *,
			    int, int, u_short *);
int			 pf_walk_header6(struct pf_pdesc *, struct ip6_hdr *,
			    u_short *);
void			 pf_print_state_parts(struct pf_state *,
			    struct pf_state_key *, struct pf_state_key *);
int			 pf_addr_wrap_neq(struct pf_addr_wrap *,
			    struct pf_addr_wrap *);
int			 pf_compare_state_keys(struct pf_state_key *,
			    struct pf_state_key *, struct pfi_kif *, u_int);
u_int16_t		 pf_pkt_hash(sa_family_t, uint8_t,
			     const struct pf_addr *, const struct pf_addr *,
			     uint16_t, uint16_t);
int			 pf_find_state(struct pf_pdesc *,
			    struct pf_state_key_cmp *, struct pf_state **);
int			 pf_src_connlimit(struct pf_state **);
int			 pf_match_rcvif(struct mbuf *, struct pf_rule *);
enum pf_test_status	 pf_match_rule(struct pf_test_ctx *,
			    struct pf_ruleset *);
void			 pf_counters_inc(int, struct pf_pdesc *,
			    struct pf_state *, struct pf_rule *,
			    struct pf_rule *);

int			 pf_state_insert(struct pfi_kif *,
			    struct pf_state_key **, struct pf_state_key **,
			    struct pf_state *);

int			 pf_state_key_isvalid(struct pf_state_key *);
struct pf_state_key	*pf_state_key_ref(struct pf_state_key *);
void			 pf_state_key_unref(struct pf_state_key *);
void			 pf_state_key_link_reverse(struct pf_state_key *,
			    struct pf_state_key *);
void			 pf_state_key_unlink_reverse(struct pf_state_key *);
void			 pf_state_key_link_inpcb(struct pf_state_key *,
			    struct inpcb *);
void			 pf_state_key_unlink_inpcb(struct pf_state_key *);
void			 pf_pktenqueue_delayed(void *);
int32_t			 pf_state_expires(const struct pf_state *, uint8_t);

#if NPFLOG > 0
void			 pf_log_matches(struct pf_pdesc *, struct pf_rule *,
			    struct pf_rule *, struct pf_ruleset *,
			    struct pf_rule_slist *);
#endif	/* NPFLOG > 0 */

extern struct pool pfr_ktable_pl;
extern struct pool pfr_kentry_pl;

struct pf_pool_limit pf_pool_limits[PF_LIMIT_MAX] = {
	{ &pf_state_pl, PFSTATE_HIWAT, PFSTATE_HIWAT },
	{ &pf_src_tree_pl, PFSNODE_HIWAT, PFSNODE_HIWAT },
	{ &pf_frent_pl, PFFRAG_FRENT_HIWAT, PFFRAG_FRENT_HIWAT },
	{ &pfr_ktable_pl, PFR_KTABLE_HIWAT, PFR_KTABLE_HIWAT },
	{ &pfr_kentry_pl, PFR_KENTRY_HIWAT, PFR_KENTRY_HIWAT },
	{ &pf_pktdelay_pl, PF_PKTDELAY_MAXPKTS, PF_PKTDELAY_MAXPKTS },
	{ &pf_anchor_pl, PF_ANCHOR_HIWAT, PF_ANCHOR_HIWAT }
};

#define BOUND_IFACE(r, k) \
	((r)->rule_flag & PFRULE_IFBOUND) ? (k) : pfi_all

#define STATE_INC_COUNTERS(s)					\
	do {							\
		struct pf_rule_item *mrm;			\
		s->rule.ptr->states_cur++;			\
		s->rule.ptr->states_tot++;			\
		if (s->anchor.ptr != NULL) {			\
			s->anchor.ptr->states_cur++;		\
			s->anchor.ptr->states_tot++;		\
		}						\
		SLIST_FOREACH(mrm, &s->match_rules, entry)	\
			mrm->r->states_cur++;			\
	} while (0)

static __inline int pf_src_compare(struct pf_src_node *, struct pf_src_node *);
static inline int pf_state_compare_key(const struct pf_state_key *,
	const struct pf_state_key *);
static inline int pf_state_compare_id(const struct pf_state *,
	const struct pf_state *);
#ifdef INET6
static __inline void pf_cksum_uncover(u_int16_t *, u_int16_t, u_int8_t);
static __inline void pf_cksum_cover(u_int16_t *, u_int16_t, u_int8_t);
#endif /* INET6 */
static __inline void pf_set_protostate(struct pf_state *, int, u_int8_t);

struct pf_src_tree tree_src_tracking;

struct pf_state_tree_id tree_id;
struct pf_state_list pf_state_list = PF_STATE_LIST_INITIALIZER(pf_state_list);

RB_GENERATE(pf_src_tree, pf_src_node, entry, pf_src_compare);
RBT_GENERATE(pf_state_tree, pf_state_key, sk_entry, pf_state_compare_key);
RBT_GENERATE(pf_state_tree_id, pf_state, entry_id, pf_state_compare_id);

int
pf_addr_compare(const struct pf_addr *a, const struct pf_addr *b,
    sa_family_t af)
{
	switch (af) {
	case AF_INET:
		if (a->addr32[0] > b->addr32[0])
			return (1);
		if (a->addr32[0] < b->addr32[0])
			return (-1);
		break;
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
	}
	return (0);
}

static __inline int
pf_src_compare(struct pf_src_node *a, struct pf_src_node *b)
{
	int	diff;

	if (a->rule.ptr > b->rule.ptr)
		return (1);
	if (a->rule.ptr < b->rule.ptr)
		return (-1);
	if ((diff = a->type - b->type) != 0)
		return (diff);
	if ((diff = a->af - b->af) != 0)
		return (diff);
	if ((diff = pf_addr_compare(&a->addr, &b->addr, a->af)) != 0)
		return (diff);
	return (0);
}

static __inline void
pf_set_protostate(struct pf_state *st, int which, u_int8_t newstate)
{
	if (which == PF_PEER_DST || which == PF_PEER_BOTH)
		st->dst.state = newstate;
	if (which == PF_PEER_DST)
		return;

	if (st->src.state == newstate)
		return;
	if (st->creatorid == pf_status.hostid &&
	    st->key[PF_SK_STACK]->proto == IPPROTO_TCP &&
	    !(TCPS_HAVEESTABLISHED(st->src.state) ||
	    st->src.state == TCPS_CLOSED) &&
	    (TCPS_HAVEESTABLISHED(newstate) || newstate == TCPS_CLOSED))
		atomic_dec_int(&pf_status.states_halfopen);

	st->src.state = newstate;
}

void
pf_addrcpy(struct pf_addr *dst, struct pf_addr *src, sa_family_t af)
{
	switch (af) {
	case AF_INET:
		dst->addr32[0] = src->addr32[0];
		break;
#ifdef INET6
	case AF_INET6:
		dst->addr32[0] = src->addr32[0];
		dst->addr32[1] = src->addr32[1];
		dst->addr32[2] = src->addr32[2];
		dst->addr32[3] = src->addr32[3];
		break;
#endif /* INET6 */
	default:
		unhandled_af(af);
	}
}

void
pf_init_threshold(struct pf_threshold *threshold,
    u_int32_t limit, u_int32_t seconds)
{
	threshold->limit = limit * PF_THRESHOLD_MULT;
	threshold->seconds = seconds;
	threshold->count = 0;
	threshold->last = getuptime();
}

void
pf_add_threshold(struct pf_threshold *threshold)
{
	u_int32_t t = getuptime(), diff = t - threshold->last;

	if (diff >= threshold->seconds)
		threshold->count = 0;
	else
		threshold->count -= threshold->count * diff /
		    threshold->seconds;
	threshold->count += PF_THRESHOLD_MULT;
	threshold->last = t;
}

int
pf_check_threshold(struct pf_threshold *threshold)
{
	return (threshold->count > threshold->limit);
}

void
pf_state_list_insert(struct pf_state_list *pfs, struct pf_state *st)
{
	/*
	 * we can always put states on the end of the list.
	 *
	 * things reading the list should take a read lock, then
	 * the mutex, get the head and tail pointers, release the
	 * mutex, and then they can iterate between the head and tail.
	 */

	pf_state_ref(st); /* get a ref for the list */

	mtx_enter(&pfs->pfs_mtx);
	TAILQ_INSERT_TAIL(&pfs->pfs_list, st, entry_list);
	mtx_leave(&pfs->pfs_mtx);
}

void
pf_state_list_remove(struct pf_state_list *pfs, struct pf_state *st)
{
	/* states can only be removed when the write lock is held */
	rw_assert_wrlock(&pfs->pfs_rwl);

	mtx_enter(&pfs->pfs_mtx);
	TAILQ_REMOVE(&pfs->pfs_list, st, entry_list);
	mtx_leave(&pfs->pfs_mtx);

	pf_state_unref(st); /* list no longer references the state */
}

void
pf_update_state_timeout(struct pf_state *st, int to)
{
	mtx_enter(&st->mtx);
	if (st->timeout != PFTM_UNLINKED)
		st->timeout = to;
	mtx_leave(&st->mtx);
}

int
pf_src_connlimit(struct pf_state **stp)
{
	int			 bad = 0;
	struct pf_src_node	*sn;

	if ((sn = pf_get_src_node((*stp), PF_SN_NONE)) == NULL)
		return (0);

	sn->conn++;
	(*stp)->src.tcp_est = 1;
	pf_add_threshold(&sn->conn_rate);

	if ((*stp)->rule.ptr->max_src_conn &&
	    (*stp)->rule.ptr->max_src_conn < sn->conn) {
		pf_status.lcounters[LCNT_SRCCONN]++;
		bad++;
	}

	if ((*stp)->rule.ptr->max_src_conn_rate.limit &&
	    pf_check_threshold(&sn->conn_rate)) {
		pf_status.lcounters[LCNT_SRCCONNRATE]++;
		bad++;
	}

	if (!bad)
		return (0);

	if ((*stp)->rule.ptr->overload_tbl) {
		struct pfr_addr p;
		u_int32_t	killed = 0;

		pf_status.lcounters[LCNT_OVERLOAD_TABLE]++;
		if (pf_status.debug >= LOG_NOTICE) {
			log(LOG_NOTICE,
			    "pf: pf_src_connlimit: blocking address ");
			pf_print_host(&sn->addr, 0,
			    (*stp)->key[PF_SK_WIRE]->af);
		}

		memset(&p, 0, sizeof(p));
		p.pfra_af = (*stp)->key[PF_SK_WIRE]->af;
		switch ((*stp)->key[PF_SK_WIRE]->af) {
		case AF_INET:
			p.pfra_net = 32;
			p.pfra_ip4addr = sn->addr.v4;
			break;
#ifdef INET6
		case AF_INET6:
			p.pfra_net = 128;
			p.pfra_ip6addr = sn->addr.v6;
			break;
#endif /* INET6 */
		}

		pfr_insert_kentry((*stp)->rule.ptr->overload_tbl,
		    &p, gettime());

		/* kill existing states if that's required. */
		if ((*stp)->rule.ptr->flush) {
			struct pf_state_key *sk;
			struct pf_state *st;

			pf_status.lcounters[LCNT_OVERLOAD_FLUSH]++;
			RBT_FOREACH(st, pf_state_tree_id, &tree_id) {
				sk = st->key[PF_SK_WIRE];
				/*
				 * Kill states from this source.  (Only those
				 * from the same rule if PF_FLUSH_GLOBAL is not
				 * set)
				 */
				if (sk->af ==
				    (*stp)->key[PF_SK_WIRE]->af &&
				    (((*stp)->direction == PF_OUT &&
				    PF_AEQ(&sn->addr, &sk->addr[1], sk->af)) ||
				    ((*stp)->direction == PF_IN &&
				    PF_AEQ(&sn->addr, &sk->addr[0], sk->af))) &&
				    ((*stp)->rule.ptr->flush &
				    PF_FLUSH_GLOBAL ||
				    (*stp)->rule.ptr == st->rule.ptr)) {
					pf_update_state_timeout(st, PFTM_PURGE);
					pf_set_protostate(st, PF_PEER_BOTH,
					    TCPS_CLOSED);
					killed++;
				}
			}
			if (pf_status.debug >= LOG_NOTICE)
				addlog(", %u states killed", killed);
		}
		if (pf_status.debug >= LOG_NOTICE)
			addlog("\n");
	}

	/* kill this state */
	pf_update_state_timeout(*stp, PFTM_PURGE);
	pf_set_protostate(*stp, PF_PEER_BOTH, TCPS_CLOSED);
	return (1);
}

int
pf_insert_src_node(struct pf_src_node **sn, struct pf_rule *rule,
    enum pf_sn_types type, sa_family_t af, struct pf_addr *src,
    struct pf_addr *raddr, struct pfi_kif *kif)
{
	struct pf_src_node	k;

	if (*sn == NULL) {
		k.af = af;
		k.type = type;
		pf_addrcpy(&k.addr, src, af);
		k.rule.ptr = rule;
		pf_status.scounters[SCNT_SRC_NODE_SEARCH]++;
		*sn = RB_FIND(pf_src_tree, &tree_src_tracking, &k);
	}
	if (*sn == NULL) {
		if (!rule->max_src_nodes ||
		    rule->src_nodes < rule->max_src_nodes)
			(*sn) = pool_get(&pf_src_tree_pl, PR_NOWAIT | PR_ZERO);
		else
			pf_status.lcounters[LCNT_SRCNODES]++;
		if ((*sn) == NULL)
			return (-1);

		pf_init_threshold(&(*sn)->conn_rate,
		    rule->max_src_conn_rate.limit,
		    rule->max_src_conn_rate.seconds);

		(*sn)->type = type;
		(*sn)->af = af;
		(*sn)->rule.ptr = rule;
		pf_addrcpy(&(*sn)->addr, src, af);
		if (raddr)
			pf_addrcpy(&(*sn)->raddr, raddr, af);
		if (RB_INSERT(pf_src_tree,
		    &tree_src_tracking, *sn) != NULL) {
			if (pf_status.debug >= LOG_NOTICE) {
				log(LOG_NOTICE,
				    "pf: src_tree insert failed: ");
				pf_print_host(&(*sn)->addr, 0, af);
				addlog("\n");
			}
			pool_put(&pf_src_tree_pl, *sn);
			return (-1);
		}
		(*sn)->creation = getuptime();
		(*sn)->rule.ptr->src_nodes++;
		if (kif != NULL) {
			(*sn)->kif = kif;
			pfi_kif_ref(kif, PFI_KIF_REF_SRCNODE);
		}
		pf_status.scounters[SCNT_SRC_NODE_INSERT]++;
		pf_status.src_nodes++;
	} else {
		if (rule->max_src_states &&
		    (*sn)->states >= rule->max_src_states) {
			pf_status.lcounters[LCNT_SRCSTATES]++;
			return (-1);
		}
	}
	return (0);
}

void
pf_remove_src_node(struct pf_src_node *sn)
{
	if (sn->states > 0 || sn->expire > getuptime())
		return;

	sn->rule.ptr->src_nodes--;
	if (sn->rule.ptr->states_cur == 0 &&
	    sn->rule.ptr->src_nodes == 0)
		pf_rm_rule(NULL, sn->rule.ptr);
	RB_REMOVE(pf_src_tree, &tree_src_tracking, sn);
	pf_status.scounters[SCNT_SRC_NODE_REMOVALS]++;
	pf_status.src_nodes--;
	pfi_kif_unref(sn->kif, PFI_KIF_REF_SRCNODE);
	pool_put(&pf_src_tree_pl, sn);
}

struct pf_src_node *
pf_get_src_node(struct pf_state *st, enum pf_sn_types type)
{
	struct pf_sn_item	*sni;

	SLIST_FOREACH(sni, &st->src_nodes, next)
		if (sni->sn->type == type)
			return (sni->sn);
	return (NULL);
}

void
pf_state_rm_src_node(struct pf_state *st, struct pf_src_node *sn)
{
	struct pf_sn_item	*sni, *snin, *snip = NULL;

	for (sni = SLIST_FIRST(&st->src_nodes); sni; sni = snin) {
		snin = SLIST_NEXT(sni, next);
		if (sni->sn == sn) {
			if (snip)
				SLIST_REMOVE_AFTER(snip, next);
			else
				SLIST_REMOVE_HEAD(&st->src_nodes, next);
			pool_put(&pf_sn_item_pl, sni);
			sni = NULL;
			sn->states--;
		}
		if (sni != NULL)
			snip = sni;
	}
}

/* state table stuff */

static inline int
pf_state_compare_key(const struct pf_state_key *a,
    const struct pf_state_key *b)
{
	int	diff;

	if ((diff = a->hash - b->hash) != 0)
		return (diff);
	if ((diff = a->proto - b->proto) != 0)
		return (diff);
	if ((diff = a->af - b->af) != 0)
		return (diff);
	if ((diff = pf_addr_compare(&a->addr[0], &b->addr[0], a->af)) != 0)
		return (diff);
	if ((diff = pf_addr_compare(&a->addr[1], &b->addr[1], a->af)) != 0)
		return (diff);
	if ((diff = a->port[0] - b->port[0]) != 0)
		return (diff);
	if ((diff = a->port[1] - b->port[1]) != 0)
		return (diff);
	if ((diff = a->rdomain - b->rdomain) != 0)
		return (diff);
	return (0);
}

static inline int
pf_state_compare_id(const struct pf_state *a, const struct pf_state *b)
{
	if (a->id > b->id)
		return (1);
	if (a->id < b->id)
		return (-1);
	if (a->creatorid > b->creatorid)
		return (1);
	if (a->creatorid < b->creatorid)
		return (-1);

	return (0);
}

/*
 * on failure, pf_state_key_attach() releases the pf_state_key
 * reference and returns NULL.
 */
struct pf_state_key *
pf_state_key_attach(struct pf_state_key *sk, struct pf_state *st, int idx)
{
	struct pf_state_item	*si;
	struct pf_state_key     *cur;
	struct pf_state		*oldst = NULL;

	PF_ASSERT_LOCKED();

	KASSERT(st->key[idx] == NULL);
	sk->sk_removed = 0;
	cur = RBT_INSERT(pf_state_tree, &pf_statetbl, sk);
	if (cur != NULL) {
		sk->sk_removed = 1;
		/* key exists. check for same kif, if none, add to key */
		TAILQ_FOREACH(si, &cur->sk_states, si_entry) {
			struct pf_state *sist = si->si_st;
			if (sist->kif == st->kif &&
			    ((sist->key[PF_SK_WIRE]->af == sk->af &&
			     sist->direction == st->direction) ||
			    (sist->key[PF_SK_WIRE]->af !=
			     sist->key[PF_SK_STACK]->af &&
			     sk->af == sist->key[PF_SK_STACK]->af &&
			     sist->direction != st->direction))) {
				int reuse = 0;

				if (sk->proto == IPPROTO_TCP &&
				    sist->src.state >= TCPS_FIN_WAIT_2 &&
				    sist->dst.state >= TCPS_FIN_WAIT_2)
					reuse = 1;
				if (pf_status.debug >= LOG_NOTICE) {
					log(LOG_NOTICE,
					    "pf: %s key attach %s on %s: ",
					    (idx == PF_SK_WIRE) ?
					    "wire" : "stack",
					    reuse ? "reuse" : "failed",
					    st->kif->pfik_name);
					pf_print_state_parts(st,
					    (idx == PF_SK_WIRE) ?  sk : NULL,
					    (idx == PF_SK_STACK) ?  sk : NULL);
					addlog(", existing: ");
					pf_print_state_parts(sist,
					    (idx == PF_SK_WIRE) ?  sk : NULL,
					    (idx == PF_SK_STACK) ?  sk : NULL);
					addlog("\n");
				}
				if (reuse) {
					pf_set_protostate(sist, PF_PEER_BOTH,
					    TCPS_CLOSED);
					/* remove late or sks can go away */
					oldst = sist;
				} else {
					pf_state_key_unref(sk);
					return (NULL);	/* collision! */
				}
			}
		}

		/* reuse the existing state key */
		pf_state_key_unref(sk);
		sk = cur;
	}

	if ((si = pool_get(&pf_state_item_pl, PR_NOWAIT)) == NULL) {
		if (TAILQ_EMPTY(&sk->sk_states)) {
			KASSERT(cur == NULL);
			RBT_REMOVE(pf_state_tree, &pf_statetbl, sk);
			sk->sk_removed = 1;
			pf_state_key_unref(sk);
		}

		return (NULL);
	}

	st->key[idx] = pf_state_key_ref(sk); /* give a ref to state */
	si->si_st = pf_state_ref(st);

	/* list is sorted, if-bound states before floating */
	if (st->kif == pfi_all)
		TAILQ_INSERT_TAIL(&sk->sk_states, si, si_entry);
	else
		TAILQ_INSERT_HEAD(&sk->sk_states, si, si_entry);

	if (oldst)
		pf_remove_state(oldst);

	/* caller owns the pf_state ref, which owns a pf_state_key ref now */
	return (sk);
}

void
pf_detach_state(struct pf_state *st)
{
	KASSERT(st->key[PF_SK_WIRE] != NULL);
	pf_state_key_detach(st, PF_SK_WIRE);

	KASSERT(st->key[PF_SK_STACK] != NULL);
	if (st->key[PF_SK_STACK] != st->key[PF_SK_WIRE])
		pf_state_key_detach(st, PF_SK_STACK);
}

void
pf_state_key_detach(struct pf_state *st, int idx)
{
	struct pf_state_item	*si;
	struct pf_state_key	*sk;

	PF_ASSERT_LOCKED();

	sk = st->key[idx];
	if (sk == NULL)
		return;

	TAILQ_FOREACH(si, &sk->sk_states, si_entry) {
		if (si->si_st == st)
			break;
	}
	if (si == NULL)
		return;

	TAILQ_REMOVE(&sk->sk_states, si, si_entry);
	pool_put(&pf_state_item_pl, si);

	if (TAILQ_EMPTY(&sk->sk_states)) {
		RBT_REMOVE(pf_state_tree, &pf_statetbl, sk);
		sk->sk_removed = 1;
		pf_state_key_unlink_reverse(sk);
		pf_state_key_unlink_inpcb(sk);
		pf_state_key_unref(sk);
	}

	pf_state_unref(st);
}

struct pf_state_key *
pf_alloc_state_key(int pool_flags)
{
	struct pf_state_key	*sk;

	if ((sk = pool_get(&pf_state_key_pl, pool_flags)) == NULL)
		return (NULL);

	PF_REF_INIT(sk->sk_refcnt);
	TAILQ_INIT(&sk->sk_states);
	sk->sk_removed = 1;

	return (sk);
}

static __inline int
pf_state_key_addr_setup(struct pf_pdesc *pd, void *arg, int sidx,
    struct pf_addr *saddr, int didx, struct pf_addr *daddr, int af, int multi)
{
	struct pf_state_key_cmp *key = arg;
#ifdef INET6
	struct pf_addr *target;

	if (af == AF_INET || pd->proto != IPPROTO_ICMPV6)
		goto copy;

	switch (pd->hdr.icmp6.icmp6_type) {
	case ND_NEIGHBOR_SOLICIT:
		if (multi)
			return (-1);
		target = (struct pf_addr *)&pd->hdr.nd_ns.nd_ns_target;
		daddr = target;
		break;
	case ND_NEIGHBOR_ADVERT:
		if (multi)
			return (-1);
		target = (struct pf_addr *)&pd->hdr.nd_ns.nd_ns_target;
		saddr = target;
		if (IN6_IS_ADDR_MULTICAST(&pd->dst->v6)) {
			key->addr[didx].addr32[0] = 0;
			key->addr[didx].addr32[1] = 0;
			key->addr[didx].addr32[2] = 0;
			key->addr[didx].addr32[3] = 0;
			daddr = NULL; /* overwritten */
		}
		break;
	default:
		if (multi) {
			key->addr[sidx].addr32[0] = __IPV6_ADDR_INT32_MLL;
			key->addr[sidx].addr32[1] = 0;
			key->addr[sidx].addr32[2] = 0;
			key->addr[sidx].addr32[3] = __IPV6_ADDR_INT32_ONE;
			saddr = NULL; /* overwritten */
		}
	}
 copy:
#endif	/* INET6 */
	if (saddr)
		pf_addrcpy(&key->addr[sidx], saddr, af);
	if (daddr)
		pf_addrcpy(&key->addr[didx], daddr, af);

	return (0);
}

int
pf_state_key_setup(struct pf_pdesc *pd, struct pf_state_key **skw,
    struct pf_state_key **sks, int rtableid)
{
	/* if returning error we MUST pool_put state keys ourselves */
	struct pf_state_key *sk1, *sk2;
	u_int wrdom = pd->rdomain;
	int afto = pd->af != pd->naf;

	if ((sk1 = pf_alloc_state_key(PR_NOWAIT | PR_ZERO)) == NULL)
		return (ENOMEM);

	pf_state_key_addr_setup(pd, sk1, pd->sidx, pd->src, pd->didx, pd->dst,
	    pd->af, 0);
	sk1->port[pd->sidx] = pd->osport;
	sk1->port[pd->didx] = pd->odport;
	sk1->proto = pd->proto;
	sk1->af = pd->af;
	sk1->rdomain = pd->rdomain;
	sk1->hash = pf_pkt_hash(sk1->af, sk1->proto,
	    &sk1->addr[0], &sk1->addr[1], sk1->port[0], sk1->port[1]);
	if (rtableid >= 0)
		wrdom = rtable_l2(rtableid);

	if (PF_ANEQ(&pd->nsaddr, pd->src, pd->af) ||
	    PF_ANEQ(&pd->ndaddr, pd->dst, pd->af) ||
	    pd->nsport != pd->osport || pd->ndport != pd->odport ||
	    wrdom != pd->rdomain || afto) {	/* NAT/NAT64 */
		if ((sk2 = pf_alloc_state_key(PR_NOWAIT | PR_ZERO)) == NULL) {
			pf_state_key_unref(sk1);
			return (ENOMEM);
		}
		pf_state_key_addr_setup(pd, sk2, afto ? pd->didx : pd->sidx,
		    &pd->nsaddr, afto ? pd->sidx : pd->didx, &pd->ndaddr,
		    pd->naf, 0);
		sk2->port[afto ? pd->didx : pd->sidx] = pd->nsport;
		sk2->port[afto ? pd->sidx : pd->didx] = pd->ndport;
		if (afto) {
			switch (pd->proto) {
			case IPPROTO_ICMP:
				sk2->proto = IPPROTO_ICMPV6;
				break;
			case IPPROTO_ICMPV6:
				sk2->proto = IPPROTO_ICMP;
				break;
			default:
				sk2->proto = pd->proto;
			}
		} else
			sk2->proto = pd->proto;
		sk2->af = pd->naf;
		sk2->rdomain = wrdom;
		sk2->hash = pf_pkt_hash(sk2->af, sk2->proto,
		    &sk2->addr[0], &sk2->addr[1], sk2->port[0], sk2->port[1]);
	} else
		sk2 = pf_state_key_ref(sk1);

	if (pd->dir == PF_IN) {
		*skw = sk1;
		*sks = sk2;
	} else {
		*sks = sk1;
		*skw = sk2;
	}

	if (pf_status.debug >= LOG_DEBUG) {
		log(LOG_DEBUG, "pf: key setup: ");
		pf_print_state_parts(NULL, *skw, *sks);
		addlog("\n");
	}

	return (0);
}

/*
 * pf_state_insert() does the following:
 * - links the pf_state up with pf_state_key(s).
 * - inserts the pf_state_keys into pf_state_tree.
 * - inserts the pf_state into the into pf_state_tree_id.
 * - tells pfsync about the state.
 *
 * pf_state_insert() owns the references to the pf_state_key structs
 * it is given. on failure to insert, these references are released.
 * on success, the caller owns a pf_state reference that allows it
 * to access the state keys.
 */

int
pf_state_insert(struct pfi_kif *kif, struct pf_state_key **skwp,
    struct pf_state_key **sksp, struct pf_state *st)
{
	struct pf_state_key *skw = *skwp;
	struct pf_state_key *sks = *sksp;
	int same = (skw == sks);

	PF_ASSERT_LOCKED();

	st->kif = kif;
	PF_STATE_ENTER_WRITE();

	skw = pf_state_key_attach(skw, st, PF_SK_WIRE);
	if (skw == NULL) {
		pf_state_key_unref(sks);
		PF_STATE_EXIT_WRITE();
		return (-1);
	}

	if (same) {
		/* pf_state_key_attach might have swapped skw */
		pf_state_key_unref(sks);
		st->key[PF_SK_STACK] = sks = pf_state_key_ref(skw);
	} else if (pf_state_key_attach(sks, st, PF_SK_STACK) == NULL) {
		pf_state_key_detach(st, PF_SK_WIRE);
		PF_STATE_EXIT_WRITE();
		return (-1);
	}

	if (st->id == 0 && st->creatorid == 0) {
		st->id = htobe64(pf_status.stateid++);
		st->creatorid = pf_status.hostid;
	}
	if (RBT_INSERT(pf_state_tree_id, &tree_id, st) != NULL) {
		if (pf_status.debug >= LOG_NOTICE) {
			log(LOG_NOTICE, "pf: state insert failed: "
			    "id: %016llx creatorid: %08x",
			    betoh64(st->id), ntohl(st->creatorid));
			addlog("\n");
		}
		pf_detach_state(st);
		PF_STATE_EXIT_WRITE();
		return (-1);
	}
	pf_state_list_insert(&pf_state_list, st);
	pf_status.fcounters[FCNT_STATE_INSERT]++;
	pf_status.states++;
	pfi_kif_ref(kif, PFI_KIF_REF_STATE);
	PF_STATE_EXIT_WRITE();

#if NPFSYNC > 0
	pfsync_insert_state(st);
#endif	/* NPFSYNC > 0 */

	*skwp = skw;
	*sksp = sks;

	return (0);
}

struct pf_state *
pf_find_state_byid(struct pf_state_cmp *key)
{
	pf_status.fcounters[FCNT_STATE_SEARCH]++;

	return (RBT_FIND(pf_state_tree_id, &tree_id, (struct pf_state *)key));
}

int
pf_compare_state_keys(struct pf_state_key *a, struct pf_state_key *b,
    struct pfi_kif *kif, u_int dir)
{
	/* a (from hdr) and b (new) must be exact opposites of each other */
	if (a->af == b->af && a->proto == b->proto &&
	    PF_AEQ(&a->addr[0], &b->addr[1], a->af) &&
	    PF_AEQ(&a->addr[1], &b->addr[0], a->af) &&
	    a->port[0] == b->port[1] &&
	    a->port[1] == b->port[0] && a->rdomain == b->rdomain)
		return (0);
	else {
		/* mismatch. must not happen. */
		if (pf_status.debug >= LOG_ERR) {
			log(LOG_ERR,
			    "pf: state key linking mismatch! dir=%s, "
			    "if=%s, stored af=%u, a0: ",
			    dir == PF_OUT ? "OUT" : "IN",
			    kif->pfik_name, a->af);
			pf_print_host(&a->addr[0], a->port[0], a->af);
			addlog(", a1: ");
			pf_print_host(&a->addr[1], a->port[1], a->af);
			addlog(", proto=%u", a->proto);
			addlog(", found af=%u, a0: ", b->af);
			pf_print_host(&b->addr[0], b->port[0], b->af);
			addlog(", a1: ");
			pf_print_host(&b->addr[1], b->port[1], b->af);
			addlog(", proto=%u", b->proto);
			addlog("\n");
		}
		return (-1);
	}
}

int
pf_find_state(struct pf_pdesc *pd, struct pf_state_key_cmp *key,
    struct pf_state **stp)
{
	struct pf_state_key	*sk, *pkt_sk;
	struct pf_state_item	*si;
	struct pf_state		*st = NULL;

	pf_status.fcounters[FCNT_STATE_SEARCH]++;
	if (pf_status.debug >= LOG_DEBUG) {
		log(LOG_DEBUG, "pf: key search, %s on %s: ",
		    pd->dir == PF_OUT ? "out" : "in", pd->kif->pfik_name);
		pf_print_state_parts(NULL, (struct pf_state_key *)key, NULL);
		addlog("\n");
	}

	pkt_sk = NULL;
	sk = NULL;
	if (pd->dir == PF_OUT) {
		/* first if block deals with outbound forwarded packet */
		pkt_sk = pd->m->m_pkthdr.pf.statekey;

		if (!pf_state_key_isvalid(pkt_sk)) {
			pf_mbuf_unlink_state_key(pd->m);
			pkt_sk = NULL;
		}

		if (pkt_sk && pf_state_key_isvalid(pkt_sk->sk_reverse))
			sk = pkt_sk->sk_reverse;

		if (pkt_sk == NULL) {
			struct inpcb *inp = pd->m->m_pkthdr.pf.inp;

			/* here we deal with local outbound packet */
			if (inp != NULL) {
				struct pf_state_key	*inp_sk;

				mtx_enter(&pf_inp_mtx);
				inp_sk = inp->inp_pf_sk;
				if (pf_state_key_isvalid(inp_sk)) {
					sk = inp_sk;
					mtx_leave(&pf_inp_mtx);
				} else if (inp_sk != NULL) {
					KASSERT(inp_sk->sk_inp == inp);
					inp_sk->sk_inp = NULL;
					inp->inp_pf_sk = NULL;
					mtx_leave(&pf_inp_mtx);

					pf_state_key_unref(inp_sk);
					in_pcbunref(inp);
				} else
					mtx_leave(&pf_inp_mtx);
			}
		}
	}

	if (sk == NULL) {
		if ((sk = RBT_FIND(pf_state_tree, &pf_statetbl,
		    (struct pf_state_key *)key)) == NULL)
			return (PF_DROP);
		if (pd->dir == PF_OUT && pkt_sk &&
		    pf_compare_state_keys(pkt_sk, sk, pd->kif, pd->dir) == 0)
			pf_state_key_link_reverse(sk, pkt_sk);
		else if (pd->dir == PF_OUT)
			pf_state_key_link_inpcb(sk, pd->m->m_pkthdr.pf.inp);
	}

	/* remove firewall data from outbound packet */
	if (pd->dir == PF_OUT)
		pf_pkt_addr_changed(pd->m);

	/* list is sorted, if-bound states before floating ones */
	TAILQ_FOREACH(si, &sk->sk_states, si_entry) {
		struct pf_state *sist = si->si_st;
		if (sist->timeout != PFTM_PURGE &&
		    (sist->kif == pfi_all || sist->kif == pd->kif) &&
		    ((sist->key[PF_SK_WIRE]->af == sist->key[PF_SK_STACK]->af &&
		      sk == (pd->dir == PF_IN ? sist->key[PF_SK_WIRE] :
		    sist->key[PF_SK_STACK])) ||
		    (sist->key[PF_SK_WIRE]->af != sist->key[PF_SK_STACK]->af
		    && pd->dir == PF_IN && (sk == sist->key[PF_SK_STACK] ||
		    sk == sist->key[PF_SK_WIRE])))) {
			st = sist;
			break;
		}
	}

	if (st == NULL)
		return (PF_DROP);
	if (ISSET(st->state_flags, PFSTATE_INP_UNLINKED))
		return (PF_DROP);

	if (st->rule.ptr->pktrate.limit && pd->dir == st->direction) {
		pf_add_threshold(&st->rule.ptr->pktrate);
		if (pf_check_threshold(&st->rule.ptr->pktrate))
			return (PF_DROP);
	}

	*stp = st;

	return (PF_MATCH);
}

struct pf_state *
pf_find_state_all(struct pf_state_key_cmp *key, u_int dir, int *more)
{
	struct pf_state_key	*sk;
	struct pf_state_item	*si, *ret = NULL;

	pf_status.fcounters[FCNT_STATE_SEARCH]++;

	sk = RBT_FIND(pf_state_tree, &pf_statetbl, (struct pf_state_key *)key);

	if (sk != NULL) {
		TAILQ_FOREACH(si, &sk->sk_states, si_entry) {
			struct pf_state *sist = si->si_st;
			if (dir == PF_INOUT ||
			    (sk == (dir == PF_IN ? sist->key[PF_SK_WIRE] :
			    sist->key[PF_SK_STACK]))) {
				if (more == NULL)
					return (sist);

				if (ret)
					(*more)++;
				else
					ret = si;
			}
		}
	}
	return (ret ? ret->si_st : NULL);
}

void
pf_state_peer_hton(const struct pf_state_peer *s, struct pfsync_state_peer *d)
{
	d->seqlo = htonl(s->seqlo);
	d->seqhi = htonl(s->seqhi);
	d->seqdiff = htonl(s->seqdiff);
	d->max_win = htons(s->max_win);
	d->mss = htons(s->mss);
	d->state = s->state;
	d->wscale = s->wscale;
	if (s->scrub) {
		d->scrub.pfss_flags =
		    htons(s->scrub->pfss_flags & PFSS_TIMESTAMP);
		d->scrub.pfss_ttl = (s)->scrub->pfss_ttl;
		d->scrub.pfss_ts_mod = htonl((s)->scrub->pfss_ts_mod);
		d->scrub.scrub_flag = PFSYNC_SCRUB_FLAG_VALID;
	}
}

void
pf_state_peer_ntoh(const struct pfsync_state_peer *s, struct pf_state_peer *d)
{
	d->seqlo = ntohl(s->seqlo);
	d->seqhi = ntohl(s->seqhi);
	d->seqdiff = ntohl(s->seqdiff);
	d->max_win = ntohs(s->max_win);
	d->mss = ntohs(s->mss);
	d->state = s->state;
	d->wscale = s->wscale;
	if (s->scrub.scrub_flag == PFSYNC_SCRUB_FLAG_VALID &&
	    d->scrub != NULL) {
		d->scrub->pfss_flags =
		    ntohs(s->scrub.pfss_flags) & PFSS_TIMESTAMP;
		d->scrub->pfss_ttl = s->scrub.pfss_ttl;
		d->scrub->pfss_ts_mod = ntohl(s->scrub.pfss_ts_mod);
	}
}

void
pf_state_export(struct pfsync_state *sp, struct pf_state *st)
{
	int32_t expire;

	memset(sp, 0, sizeof(struct pfsync_state));

	/* copy from state key */
	sp->key[PF_SK_WIRE].addr[0] = st->key[PF_SK_WIRE]->addr[0];
	sp->key[PF_SK_WIRE].addr[1] = st->key[PF_SK_WIRE]->addr[1];
	sp->key[PF_SK_WIRE].port[0] = st->key[PF_SK_WIRE]->port[0];
	sp->key[PF_SK_WIRE].port[1] = st->key[PF_SK_WIRE]->port[1];
	sp->key[PF_SK_WIRE].rdomain = htons(st->key[PF_SK_WIRE]->rdomain);
	sp->key[PF_SK_WIRE].af = st->key[PF_SK_WIRE]->af;
	sp->key[PF_SK_STACK].addr[0] = st->key[PF_SK_STACK]->addr[0];
	sp->key[PF_SK_STACK].addr[1] = st->key[PF_SK_STACK]->addr[1];
	sp->key[PF_SK_STACK].port[0] = st->key[PF_SK_STACK]->port[0];
	sp->key[PF_SK_STACK].port[1] = st->key[PF_SK_STACK]->port[1];
	sp->key[PF_SK_STACK].rdomain = htons(st->key[PF_SK_STACK]->rdomain);
	sp->key[PF_SK_STACK].af = st->key[PF_SK_STACK]->af;
	sp->rtableid[PF_SK_WIRE] = htonl(st->rtableid[PF_SK_WIRE]);
	sp->rtableid[PF_SK_STACK] = htonl(st->rtableid[PF_SK_STACK]);
	sp->proto = st->key[PF_SK_WIRE]->proto;
	sp->af = st->key[PF_SK_WIRE]->af;

	/* copy from state */
	strlcpy(sp->ifname, st->kif->pfik_name, sizeof(sp->ifname));
	sp->rt = st->rt;
	sp->rt_addr = st->rt_addr;
	sp->creation = htonl(getuptime() - st->creation);
	expire = pf_state_expires(st, st->timeout);
	if (expire <= getuptime())
		sp->expire = htonl(0);
	else
		sp->expire = htonl(expire - getuptime());

	sp->direction = st->direction;
#if NPFLOG > 0
	sp->log = st->log;
#endif	/* NPFLOG > 0 */
	sp->timeout = st->timeout;
	sp->state_flags = htons(st->state_flags);
	if (READ_ONCE(st->sync_defer) != NULL)
		sp->state_flags |= htons(PFSTATE_ACK);
	if (!SLIST_EMPTY(&st->src_nodes))
		sp->sync_flags |= PFSYNC_FLAG_SRCNODE;

	sp->id = st->id;
	sp->creatorid = st->creatorid;
	pf_state_peer_hton(&st->src, &sp->src);
	pf_state_peer_hton(&st->dst, &sp->dst);

	if (st->rule.ptr == NULL)
		sp->rule = htonl(-1);
	else
		sp->rule = htonl(st->rule.ptr->nr);
	if (st->anchor.ptr == NULL)
		sp->anchor = htonl(-1);
	else
		sp->anchor = htonl(st->anchor.ptr->nr);
	sp->nat_rule = htonl(-1);	/* left for compat, nat_rule is gone */

	pf_state_counter_hton(st->packets[0], sp->packets[0]);
	pf_state_counter_hton(st->packets[1], sp->packets[1]);
	pf_state_counter_hton(st->bytes[0], sp->bytes[0]);
	pf_state_counter_hton(st->bytes[1], sp->bytes[1]);

	sp->max_mss = htons(st->max_mss);
	sp->min_ttl = st->min_ttl;
	sp->set_tos = st->set_tos;
	sp->set_prio[0] = st->set_prio[0];
	sp->set_prio[1] = st->set_prio[1];
}

int
pf_state_alloc_scrub_memory(const struct pfsync_state_peer *s,
    struct pf_state_peer *d)
{
	if (s->scrub.scrub_flag && d->scrub == NULL)
		return (pf_normalize_tcp_alloc(d));

	return (0);
}

#if NPFSYNC > 0
int
pf_state_import(const struct pfsync_state *sp, int flags)
{
	struct pf_state *st = NULL;
	struct pf_state_key *skw = NULL, *sks = NULL;
	struct pf_rule *r = NULL;
	struct pfi_kif  *kif;
	int pool_flags;
	int error = ENOMEM;
	int n = 0;

	PF_ASSERT_LOCKED();

	if (sp->creatorid == 0) {
		DPFPRINTF(LOG_NOTICE, "%s: invalid creator id: %08x", __func__,
		    ntohl(sp->creatorid));
		return (EINVAL);
	}

	if ((kif = pfi_kif_get(sp->ifname, NULL)) == NULL) {
		DPFPRINTF(LOG_NOTICE, "%s: unknown interface: %s", __func__,
		    sp->ifname);
		if (flags & PFSYNC_SI_IOCTL)
			return (EINVAL);
		return (0);	/* skip this state */
	}

	if (sp->af == 0)
		return (0);	/* skip this state */

	/*
	 * If the ruleset checksums match or the state is coming from the ioctl,
	 * it's safe to associate the state with the rule of that number.
	 */
	if (sp->rule != htonl(-1) && sp->anchor == htonl(-1) &&
	    (flags & (PFSYNC_SI_IOCTL | PFSYNC_SI_CKSUM)) &&
	    ntohl(sp->rule) < pf_main_ruleset.rules.active.rcount) {
		TAILQ_FOREACH(r, pf_main_ruleset.rules.active.ptr, entries)
			if (ntohl(sp->rule) == n++)
				break;
	} else
		r = &pf_default_rule;

	if ((r->max_states && r->states_cur >= r->max_states))
		goto cleanup;

	if (flags & PFSYNC_SI_IOCTL)
		pool_flags = PR_WAITOK | PR_LIMITFAIL | PR_ZERO;
	else
		pool_flags = PR_NOWAIT | PR_LIMITFAIL | PR_ZERO;

	if ((st = pool_get(&pf_state_pl, pool_flags)) == NULL)
		goto cleanup;

	if ((skw = pf_alloc_state_key(pool_flags)) == NULL)
		goto cleanup;

	if ((sp->key[PF_SK_WIRE].af &&
	    (sp->key[PF_SK_WIRE].af != sp->key[PF_SK_STACK].af)) ||
	    PF_ANEQ(&sp->key[PF_SK_WIRE].addr[0],
	    &sp->key[PF_SK_STACK].addr[0], sp->af) ||
	    PF_ANEQ(&sp->key[PF_SK_WIRE].addr[1],
	    &sp->key[PF_SK_STACK].addr[1], sp->af) ||
	    sp->key[PF_SK_WIRE].port[0] != sp->key[PF_SK_STACK].port[0] ||
	    sp->key[PF_SK_WIRE].port[1] != sp->key[PF_SK_STACK].port[1] ||
	    sp->key[PF_SK_WIRE].rdomain != sp->key[PF_SK_STACK].rdomain) {
		if ((sks = pf_alloc_state_key(pool_flags)) == NULL)
			goto cleanup;
	} else
		sks = pf_state_key_ref(skw);

	/* allocate memory for scrub info */
	if (pf_state_alloc_scrub_memory(&sp->src, &st->src) ||
	    pf_state_alloc_scrub_memory(&sp->dst, &st->dst))
		goto cleanup;

	/* copy to state key(s) */
	skw->addr[0] = sp->key[PF_SK_WIRE].addr[0];
	skw->addr[1] = sp->key[PF_SK_WIRE].addr[1];
	skw->port[0] = sp->key[PF_SK_WIRE].port[0];
	skw->port[1] = sp->key[PF_SK_WIRE].port[1];
	skw->rdomain = ntohs(sp->key[PF_SK_WIRE].rdomain);
	skw->proto = sp->proto;
	if (!(skw->af = sp->key[PF_SK_WIRE].af))
		skw->af = sp->af;
	skw->hash = pf_pkt_hash(skw->af, skw->proto,
	    &skw->addr[0], &skw->addr[1], skw->port[0], skw->port[1]);

	if (sks != skw) {
		sks->addr[0] = sp->key[PF_SK_STACK].addr[0];
		sks->addr[1] = sp->key[PF_SK_STACK].addr[1];
		sks->port[0] = sp->key[PF_SK_STACK].port[0];
		sks->port[1] = sp->key[PF_SK_STACK].port[1];
		sks->rdomain = ntohs(sp->key[PF_SK_STACK].rdomain);
		if (!(sks->af = sp->key[PF_SK_STACK].af))
			sks->af = sp->af;
		if (sks->af != skw->af) {
			switch (sp->proto) {
			case IPPROTO_ICMP:
				sks->proto = IPPROTO_ICMPV6;
				break;
			case IPPROTO_ICMPV6:
				sks->proto = IPPROTO_ICMP;
				break;
			default:
				sks->proto = sp->proto;
			}
		} else
			sks->proto = sp->proto;

		if (((sks->af != AF_INET) && (sks->af != AF_INET6)) ||
		    ((skw->af != AF_INET) && (skw->af != AF_INET6))) {
			error = EINVAL;
			goto cleanup;
		}

		sks->hash = pf_pkt_hash(sks->af, sks->proto,
		    &sks->addr[0], &sks->addr[1], sks->port[0], sks->port[1]);

	} else if ((sks->af != AF_INET) && (sks->af != AF_INET6)) {
		error = EINVAL;
		goto cleanup;
	}
	st->rtableid[PF_SK_WIRE] = ntohl(sp->rtableid[PF_SK_WIRE]);
	st->rtableid[PF_SK_STACK] = ntohl(sp->rtableid[PF_SK_STACK]);

	/* copy to state */
	st->rt_addr = sp->rt_addr;
	st->rt = sp->rt;
	st->creation = getuptime() - ntohl(sp->creation);
	st->expire = getuptime();
	if (ntohl(sp->expire)) {
		u_int32_t timeout;

		timeout = r->timeout[sp->timeout];
		if (!timeout)
			timeout = pf_default_rule.timeout[sp->timeout];

		/* sp->expire may have been adaptively scaled by export. */
		st->expire -= timeout - ntohl(sp->expire);
	}

	st->direction = sp->direction;
	st->log = sp->log;
	st->timeout = sp->timeout;
	st->state_flags = ntohs(sp->state_flags);
	st->max_mss = ntohs(sp->max_mss);
	st->min_ttl = sp->min_ttl;
	st->set_tos = sp->set_tos;
	st->set_prio[0] = sp->set_prio[0];
	st->set_prio[1] = sp->set_prio[1];

	st->id = sp->id;
	st->creatorid = sp->creatorid;
	pf_state_peer_ntoh(&sp->src, &st->src);
	pf_state_peer_ntoh(&sp->dst, &st->dst);

	st->rule.ptr = r;
	st->anchor.ptr = NULL;

	PF_REF_INIT(st->refcnt);
	mtx_init(&st->mtx, IPL_NET);

	/* XXX when we have anchors, use STATE_INC_COUNTERS */
	r->states_cur++;
	r->states_tot++;

	st->sync_state = PFSYNC_S_NONE;
	st->pfsync_time = getuptime();
#if NPFSYNC > 0
	pfsync_init_state(st, skw, sks, flags);
#endif

	if (pf_state_insert(kif, &skw, &sks, st) != 0) {
		/* XXX when we have anchors, use STATE_DEC_COUNTERS */
		r->states_cur--;
		error = EEXIST;
		goto cleanup_state;
	}

	return (0);

 cleanup:
	if (skw != NULL)
		pf_state_key_unref(skw);
	if (sks != NULL)
		pf_state_key_unref(sks);

 cleanup_state: /* pf_state_insert frees the state keys */
	if (st) {
		if (st->dst.scrub)
			pool_put(&pf_state_scrub_pl, st->dst.scrub);
		if (st->src.scrub)
			pool_put(&pf_state_scrub_pl, st->src.scrub);
		pool_put(&pf_state_pl, st);
	}
	return (error);
}
#endif /* NPFSYNC > 0 */

/* END state table stuff */

void		 pf_purge_states(void *);
struct task	 pf_purge_states_task =
		     TASK_INITIALIZER(pf_purge_states, NULL);

void		 pf_purge_states_tick(void *);
struct timeout	 pf_purge_states_to =
		     TIMEOUT_INITIALIZER(pf_purge_states_tick, NULL);

unsigned int	 pf_purge_expired_states(unsigned int, unsigned int);

/*
 * how many states to scan this interval.
 *
 * this is set when the timeout fires, and reduced by the task. the
 * task will reschedule itself until the limit is reduced to zero,
 * and then it adds the timeout again.
 */
unsigned int pf_purge_states_limit;

/*
 * limit how many states are processed with locks held per run of
 * the state purge task.
 */
unsigned int pf_purge_states_collect = 64;

 void
pf_purge_states_tick(void *null)
 {
	unsigned int limit = pf_status.states;
	unsigned int interval = pf_default_rule.timeout[PFTM_INTERVAL];

	if (limit == 0) {
		timeout_add_sec(&pf_purge_states_to, 1);
		return;
	}

	/*
	 * process a fraction of the state table every second
	 */

	if (interval > 1)
		limit /= interval;

	pf_purge_states_limit = limit;
	task_add(systqmp, &pf_purge_states_task);
}

void
pf_purge_states(void *null)
{
	unsigned int limit;
	unsigned int scanned;

	limit = pf_purge_states_limit;
	if (limit < pf_purge_states_collect)
		limit = pf_purge_states_collect;

	scanned = pf_purge_expired_states(limit, pf_purge_states_collect);
	if (scanned >= pf_purge_states_limit) {
		/* we've run out of states to scan this "interval" */
		timeout_add_sec(&pf_purge_states_to, 1);
		return;
	}

	pf_purge_states_limit -= scanned;
	task_add(systqmp, &pf_purge_states_task);
}

void		 pf_purge_tick(void *);
struct timeout	 pf_purge_to =
		     TIMEOUT_INITIALIZER(pf_purge_tick, NULL);

void		 pf_purge(void *);
struct task	 pf_purge_task =
		     TASK_INITIALIZER(pf_purge, NULL);

void
pf_purge_tick(void *null)
{
	task_add(systqmp, &pf_purge_task);
}

void
pf_purge(void *null)
{
	unsigned int interval = max(1, pf_default_rule.timeout[PFTM_INTERVAL]);

	PF_LOCK();

	pf_purge_expired_src_nodes();

	PF_UNLOCK();

	/*
	 * Fragments don't require PF_LOCK(), they use their own lock.
	 */
	pf_purge_expired_fragments();

	/* interpret the interval as idle time between runs */
	timeout_add_sec(&pf_purge_to, interval);
}

int32_t
pf_state_expires(const struct pf_state *st, uint8_t stimeout)
{
	u_int32_t	timeout;
	u_int32_t	start;
	u_int32_t	end;
	u_int32_t	states;

	/*
	 * pf_state_expires is used by the state purge task to
	 * decide if a state is a candidate for cleanup, and by the
	 * pfsync state export code to populate an expiry time.
	 *
	 * this function may be called by the state purge task while
	 * the state is being modified. avoid inconsistent reads of
	 * state->timeout by having the caller do the read (and any
	 * checks it needs to do on the same variable) and then pass
	 * their view of the timeout in here for this function to use.
	 * the only consequence of using a stale timeout value is
	 * that the state won't be a candidate for purging until the
	 * next pass of the purge task.
	 */

	/* handle all PFTM_* >= PFTM_MAX here */
	if (stimeout >= PFTM_MAX)
		return (0);

	KASSERT(stimeout < PFTM_MAX);

	timeout = st->rule.ptr->timeout[stimeout];
	if (!timeout)
		timeout = pf_default_rule.timeout[stimeout];

	start = st->rule.ptr->timeout[PFTM_ADAPTIVE_START];
	if (start) {
		end = st->rule.ptr->timeout[PFTM_ADAPTIVE_END];
		states = st->rule.ptr->states_cur;
	} else {
		start = pf_default_rule.timeout[PFTM_ADAPTIVE_START];
		end = pf_default_rule.timeout[PFTM_ADAPTIVE_END];
		states = pf_status.states;
	}
	if (end && states > start && start < end) {
		if (states >= end)
			return (0);

		timeout = (u_int64_t)timeout * (end - states) / (end - start);
	}

	return (st->expire + timeout);
}

void
pf_purge_expired_src_nodes(void)
{
	struct pf_src_node		*cur, *next;

	PF_ASSERT_LOCKED();

	RB_FOREACH_SAFE(cur, pf_src_tree, &tree_src_tracking, next) {
		if (cur->states == 0 && cur->expire <= getuptime()) {
			pf_remove_src_node(cur);
		}
	}
}

void
pf_src_tree_remove_state(struct pf_state *st)
{
	u_int32_t		 timeout;
	struct pf_sn_item	*sni;

	while ((sni = SLIST_FIRST(&st->src_nodes)) != NULL) {
		SLIST_REMOVE_HEAD(&st->src_nodes, next);
		if (st->src.tcp_est)
			--sni->sn->conn;
		if (--sni->sn->states == 0) {
			timeout = st->rule.ptr->timeout[PFTM_SRC_NODE];
			if (!timeout)
				timeout =
				    pf_default_rule.timeout[PFTM_SRC_NODE];
			sni->sn->expire = getuptime() + timeout;
		}
		pool_put(&pf_sn_item_pl, sni);
	}
}

void
pf_remove_state(struct pf_state *st)
{
	PF_ASSERT_LOCKED();

	mtx_enter(&st->mtx);
	if (st->timeout == PFTM_UNLINKED) {
		mtx_leave(&st->mtx);
		return;
	}
	st->timeout = PFTM_UNLINKED;
	mtx_leave(&st->mtx);

	/* handle load balancing related tasks */
	pf_postprocess_addr(st);

	if (st->src.state == PF_TCPS_PROXY_DST) {
		pf_send_tcp(st->rule.ptr, st->key[PF_SK_WIRE]->af,
		    &st->key[PF_SK_WIRE]->addr[1],
		    &st->key[PF_SK_WIRE]->addr[0],
		    st->key[PF_SK_WIRE]->port[1],
		    st->key[PF_SK_WIRE]->port[0],
		    st->src.seqhi, st->src.seqlo + 1,
		    TH_RST|TH_ACK, 0, 0, 0, 1, st->tag,
		    st->key[PF_SK_WIRE]->rdomain, NULL);
	}
	if (st->key[PF_SK_STACK]->proto == IPPROTO_TCP)
		pf_set_protostate(st, PF_PEER_BOTH, TCPS_CLOSED);

	RBT_REMOVE(pf_state_tree_id, &tree_id, st);
#if NPFLOW > 0
	if (st->state_flags & PFSTATE_PFLOW)
		export_pflow(st);
#endif	/* NPFLOW > 0 */
#if NPFSYNC > 0
	pfsync_delete_state(st);
#endif	/* NPFSYNC > 0 */
	pf_src_tree_remove_state(st);
	pf_detach_state(st);
}

void
pf_remove_divert_state(struct inpcb *inp)
{
	struct pf_state_key	*sk;
	struct pf_state_item	*si;

	PF_ASSERT_UNLOCKED();

	if (READ_ONCE(inp->inp_pf_sk) == NULL)
		return;

	mtx_enter(&pf_inp_mtx);
	sk = pf_state_key_ref(inp->inp_pf_sk);
	mtx_leave(&pf_inp_mtx);
	if (sk == NULL)
		return;

	PF_LOCK();
	PF_STATE_ENTER_WRITE();
	TAILQ_FOREACH(si, &sk->sk_states, si_entry) {
		struct pf_state *sist = si->si_st;
		if (sk == sist->key[PF_SK_STACK] && sist->rule.ptr &&
		    (sist->rule.ptr->divert.type == PF_DIVERT_TO ||
		     sist->rule.ptr->divert.type == PF_DIVERT_REPLY)) {
			if (sist->key[PF_SK_STACK]->proto == IPPROTO_TCP &&
			    sist->key[PF_SK_WIRE] != sist->key[PF_SK_STACK]) {
				/*
				 * If the local address is translated, keep
				 * the state for "tcp.closed" seconds to
				 * prevent its source port from being reused.
				 */
				if (sist->src.state < TCPS_FIN_WAIT_2 ||
				    sist->dst.state < TCPS_FIN_WAIT_2) {
					pf_set_protostate(sist, PF_PEER_BOTH,
					    TCPS_TIME_WAIT);
					pf_update_state_timeout(sist,
					    PFTM_TCP_CLOSED);
					sist->expire = getuptime();
				}
				sist->state_flags |= PFSTATE_INP_UNLINKED;
			} else
				pf_remove_state(sist);
			break;
		}
	}
	PF_STATE_EXIT_WRITE();
	PF_UNLOCK();

	pf_state_key_unref(sk);
}

void
pf_free_state(struct pf_state *st)
{
	struct pf_rule_item *ri;

	PF_ASSERT_LOCKED();

#if NPFSYNC > 0
	if (pfsync_state_in_use(st))
		return;
#endif	/* NPFSYNC > 0 */

	KASSERT(st->timeout == PFTM_UNLINKED);
	if (--st->rule.ptr->states_cur == 0 &&
	    st->rule.ptr->src_nodes == 0)
		pf_rm_rule(NULL, st->rule.ptr);
	if (st->anchor.ptr != NULL)
		if (--st->anchor.ptr->states_cur == 0)
			pf_rm_rule(NULL, st->anchor.ptr);
	while ((ri = SLIST_FIRST(&st->match_rules))) {
		SLIST_REMOVE_HEAD(&st->match_rules, entry);
		if (--ri->r->states_cur == 0 &&
		    ri->r->src_nodes == 0)
			pf_rm_rule(NULL, ri->r);
		pool_put(&pf_rule_item_pl, ri);
	}
	pf_normalize_tcp_cleanup(st);
	pfi_kif_unref(st->kif, PFI_KIF_REF_STATE);
	pf_state_list_remove(&pf_state_list, st);
	if (st->tag)
		pf_tag_unref(st->tag);
	pf_state_unref(st);
	pf_status.fcounters[FCNT_STATE_REMOVALS]++;
	pf_status.states--;
}

unsigned int
pf_purge_expired_states(const unsigned int limit, const unsigned int collect)
{
	/*
	 * this task/thread/context/whatever is the only thing that
	 * removes states from the pf_state_list, so the cur reference
	 * it holds between calls is guaranteed to still be in the
	 * list.
	 */
	static struct pf_state	*cur = NULL;

	struct pf_state		*head, *tail;
	struct pf_state		*st;
	SLIST_HEAD(pf_state_gcl, pf_state) gcl = SLIST_HEAD_INITIALIZER(gcl);
	time_t			 now;
	unsigned int		 scanned;
	unsigned int		 collected = 0;

	PF_ASSERT_UNLOCKED();

	rw_enter_read(&pf_state_list.pfs_rwl);

	mtx_enter(&pf_state_list.pfs_mtx);
	head = TAILQ_FIRST(&pf_state_list.pfs_list);
	tail = TAILQ_LAST(&pf_state_list.pfs_list, pf_state_queue);
	mtx_leave(&pf_state_list.pfs_mtx);

	if (head == NULL) {
		/* the list is empty */
		rw_exit_read(&pf_state_list.pfs_rwl);
		return (limit);
	}

	/* (re)start at the front of the list */
	if (cur == NULL)
		cur = head;

	now = getuptime();

	for (scanned = 0; scanned < limit; scanned++) {
		uint8_t stimeout = cur->timeout;
		unsigned int limited = 0;

		if ((stimeout == PFTM_UNLINKED) ||
		    (pf_state_expires(cur, stimeout) <= now)) {
			st = pf_state_ref(cur);
			SLIST_INSERT_HEAD(&gcl, st, gc_list);

			if (++collected >= collect)
				limited = 1;
		}

		/* don't iterate past the end of our view of the list */
		if (cur == tail) {
			cur = NULL;
			break;
		}

		cur = TAILQ_NEXT(cur, entry_list);

		/* don't spend too much time here. */
		if (ISSET(READ_ONCE(curcpu()->ci_schedstate.spc_schedflags),
		     SPCF_SHOULDYIELD) || limited)
			break;
	}

	rw_exit_read(&pf_state_list.pfs_rwl);

	if (SLIST_EMPTY(&gcl))
		return (scanned);

	rw_enter_write(&pf_state_list.pfs_rwl);
	PF_LOCK();
	PF_STATE_ENTER_WRITE();
	SLIST_FOREACH(st, &gcl, gc_list) {
		if (st->timeout != PFTM_UNLINKED)
			pf_remove_state(st);

		pf_free_state(st);
	}
	PF_STATE_EXIT_WRITE();
	PF_UNLOCK();
	rw_exit_write(&pf_state_list.pfs_rwl);

	while ((st = SLIST_FIRST(&gcl)) != NULL) {
		SLIST_REMOVE_HEAD(&gcl, gc_list);
		pf_state_unref(st);
	}

	return (scanned);
}

int
pf_tbladdr_setup(struct pf_ruleset *rs, struct pf_addr_wrap *aw, int wait)
{
	if (aw->type != PF_ADDR_TABLE)
		return (0);
	if ((aw->p.tbl = pfr_attach_table(rs, aw->v.tblname, wait)) == NULL)
		return (1);
	return (0);
}

void
pf_tbladdr_remove(struct pf_addr_wrap *aw)
{
	if (aw->type != PF_ADDR_TABLE || aw->p.tbl == NULL)
		return;
	pfr_detach_table(aw->p.tbl);
	aw->p.tbl = NULL;
}

void
pf_tbladdr_copyout(struct pf_addr_wrap *aw)
{
	struct pfr_ktable *kt = aw->p.tbl;

	if (aw->type != PF_ADDR_TABLE || kt == NULL)
		return;
	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE) && kt->pfrkt_root != NULL)
		kt = kt->pfrkt_root;
	aw->p.tbl = NULL;
	aw->p.tblcnt = (kt->pfrkt_flags & PFR_TFLAG_ACTIVE) ?
		kt->pfrkt_cnt : -1;
}

void
pf_print_host(struct pf_addr *addr, u_int16_t p, sa_family_t af)
{
	switch (af) {
	case AF_INET: {
		u_int32_t a = ntohl(addr->addr32[0]);
		addlog("%u.%u.%u.%u", (a>>24)&255, (a>>16)&255,
		    (a>>8)&255, a&255);
		if (p) {
			p = ntohs(p);
			addlog(":%u", p);
		}
		break;
	}
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
					addlog(":");
				if (i == maxend)
					addlog(":");
			} else {
				b = ntohs(addr->addr16[i]);
				addlog("%x", b);
				if (i < 7)
					addlog(":");
			}
		}
		if (p) {
			p = ntohs(p);
			addlog("[%u]", p);
		}
		break;
	}
#endif /* INET6 */
	}
}

void
pf_print_state(struct pf_state *st)
{
	pf_print_state_parts(st, NULL, NULL);
}

void
pf_print_state_parts(struct pf_state *st,
    struct pf_state_key *skwp, struct pf_state_key *sksp)
{
	struct pf_state_key *skw, *sks;
	u_int8_t proto, dir;

	/* Do our best to fill these, but they're skipped if NULL */
	skw = skwp ? skwp : (st ? st->key[PF_SK_WIRE] : NULL);
	sks = sksp ? sksp : (st ? st->key[PF_SK_STACK] : NULL);
	proto = skw ? skw->proto : (sks ? sks->proto : 0);
	dir = st ? st->direction : 0;

	switch (proto) {
	case IPPROTO_IPV4:
		addlog("IPv4");
		break;
	case IPPROTO_IPV6:
		addlog("IPv6");
		break;
	case IPPROTO_TCP:
		addlog("TCP");
		break;
	case IPPROTO_UDP:
		addlog("UDP");
		break;
	case IPPROTO_ICMP:
		addlog("ICMP");
		break;
	case IPPROTO_ICMPV6:
		addlog("ICMPv6");
		break;
	default:
		addlog("%u", proto);
		break;
	}
	switch (dir) {
	case PF_IN:
		addlog(" in");
		break;
	case PF_OUT:
		addlog(" out");
		break;
	}
	if (skw) {
		addlog(" wire: (%d) ", skw->rdomain);
		pf_print_host(&skw->addr[0], skw->port[0], skw->af);
		addlog(" ");
		pf_print_host(&skw->addr[1], skw->port[1], skw->af);
	}
	if (sks) {
		addlog(" stack: (%d) ", sks->rdomain);
		if (sks != skw) {
			pf_print_host(&sks->addr[0], sks->port[0], sks->af);
			addlog(" ");
			pf_print_host(&sks->addr[1], sks->port[1], sks->af);
		} else
			addlog("-");
	}
	if (st) {
		if (proto == IPPROTO_TCP) {
			addlog(" [lo=%u high=%u win=%u modulator=%u",
			    st->src.seqlo, st->src.seqhi,
			    st->src.max_win, st->src.seqdiff);
			if (st->src.wscale && st->dst.wscale)
				addlog(" wscale=%u",
				    st->src.wscale & PF_WSCALE_MASK);
			addlog("]");
			addlog(" [lo=%u high=%u win=%u modulator=%u",
			    st->dst.seqlo, st->dst.seqhi,
			    st->dst.max_win, st->dst.seqdiff);
			if (st->src.wscale && st->dst.wscale)
				addlog(" wscale=%u",
				st->dst.wscale & PF_WSCALE_MASK);
			addlog("]");
		}
		addlog(" %u:%u", st->src.state, st->dst.state);
		if (st->rule.ptr)
			addlog(" @%d", st->rule.ptr->nr);
	}
}

void
pf_print_flags(u_int8_t f)
{
	if (f)
		addlog(" ");
	if (f & TH_FIN)
		addlog("F");
	if (f & TH_SYN)
		addlog("S");
	if (f & TH_RST)
		addlog("R");
	if (f & TH_PUSH)
		addlog("P");
	if (f & TH_ACK)
		addlog("A");
	if (f & TH_URG)
		addlog("U");
	if (f & TH_ECE)
		addlog("E");
	if (f & TH_CWR)
		addlog("W");
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
		if (cur->onrdomain != prev->onrdomain ||
		    cur->ifnot != prev->ifnot)
			PF_SET_SKIP_STEPS(PF_SKIP_RDOM);
		if (cur->af != prev->af)
			PF_SET_SKIP_STEPS(PF_SKIP_AF);
		if (cur->proto != prev->proto)
			PF_SET_SKIP_STEPS(PF_SKIP_PROTO);
		if (cur->src.neg != prev->src.neg ||
		    pf_addr_wrap_neq(&cur->src.addr, &prev->src.addr))
			PF_SET_SKIP_STEPS(PF_SKIP_SRC_ADDR);
		if (cur->dst.neg != prev->dst.neg ||
		    pf_addr_wrap_neq(&cur->dst.addr, &prev->dst.addr))
			PF_SET_SKIP_STEPS(PF_SKIP_DST_ADDR);
		if (cur->src.port[0] != prev->src.port[0] ||
		    cur->src.port[1] != prev->src.port[1] ||
		    cur->src.port_op != prev->src.port_op)
			PF_SET_SKIP_STEPS(PF_SKIP_SRC_PORT);
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

int
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
	case PF_ADDR_NONE:
	case PF_ADDR_NOROUTE:
	case PF_ADDR_URPFFAILED:
		return (0);
	case PF_ADDR_TABLE:
		return (aw1->p.tbl != aw2->p.tbl);
	case PF_ADDR_RTLABEL:
		return (aw1->v.rtlabel != aw2->v.rtlabel);
	default:
		addlog("invalid address type: %d\n", aw1->type);
		return (1);
	}
}

/* This algorithm computes 'a + b - c' in ones-complement using a trick to
 * emulate at most one ones-complement subtraction. This thereby limits net
 * carries/borrows to at most one, eliminating a reduction step and saving one
 * each of +, >>, & and ~.
 *
 * def. x mod y = x - (x//y)*y for integer x,y
 * def. sum = x mod 2^16
 * def. accumulator = (x >> 16) mod 2^16
 *
 * The trick works as follows: subtracting exactly one u_int16_t from the
 * u_int32_t x incurs at most one underflow, wrapping its upper 16-bits, the
 * accumulator, to 2^16 - 1. Adding this to the 16-bit sum preserves the
 * ones-complement borrow:
 *
 *  (sum + accumulator) mod 2^16
 * =	{ assume underflow: accumulator := 2^16 - 1 }
 *  (sum + 2^16 - 1) mod 2^16
 * =	{ mod }
 *  (sum - 1) mod 2^16
 *
 * Although this breaks for sum = 0, giving 0xffff, which is ones-complement's
 * other zero, not -1, that cannot occur: the 16-bit sum cannot be underflown
 * to zero as that requires subtraction of at least 2^16, which exceeds a
 * single u_int16_t's range.
 *
 * We use the following theorem to derive the implementation:
 *
 * th. (x + (y mod z)) mod z  =  (x + y) mod z   (0)
 * proof.
 *     (x + (y mod z)) mod z
 *    =  { def mod }
 *     (x + y - (y//z)*z) mod z
 *    =  { (a + b*c) mod c = a mod c }
 *     (x + y) mod z			[end of proof]
 *
 * ... and thereby obtain:
 *
 *  (sum + accumulator) mod 2^16
 * =	{ def. accumulator, def. sum }
 *  (x mod 2^16 + (x >> 16) mod 2^16) mod 2^16
 * =	{ (0), twice }
 *  (x + (x >> 16)) mod 2^16
 * =	{ x mod 2^n = x & (2^n - 1) }
 *  (x + (x >> 16)) & 0xffff
 *
 * Note: this serves also as a reduction step for at most one add (as the
 * trailing mod 2^16 prevents further reductions by destroying carries).
 */
__inline void
pf_cksum_fixup(u_int16_t *cksum, u_int16_t was, u_int16_t now,
    u_int8_t proto)
{
	u_int32_t x;
	const int udp = proto == IPPROTO_UDP;

	x = *cksum + was - now;
	x = (x + (x >> 16)) & 0xffff;

	/* optimise: eliminate a branch when not udp */
	if (udp && *cksum == 0x0000)
		return;
	if (udp && x == 0x0000)
		x = 0xffff;

	*cksum = (u_int16_t)(x);
}

#ifdef INET6
/* pre: coverage(cksum) is superset of coverage(covered_cksum) */
static __inline void
pf_cksum_uncover(u_int16_t *cksum, u_int16_t covered_cksum, u_int8_t proto)
{
	pf_cksum_fixup(cksum, ~covered_cksum, 0x0, proto);
}

/* pre: disjoint(coverage(cksum), coverage(uncovered_cksum)) */
static __inline void
pf_cksum_cover(u_int16_t *cksum, u_int16_t uncovered_cksum, u_int8_t proto)
{
	pf_cksum_fixup(cksum, 0x0, ~uncovered_cksum, proto);
}
#endif /* INET6 */

/* pre: *a is 16-bit aligned within its packet
 *
 * This algorithm emulates 16-bit ones-complement sums on a twos-complement
 * machine by conserving ones-complement's otherwise discarded carries in the
 * upper bits of x. These accumulated carries when added to the lower 16-bits
 * over at least zero 'reduction' steps then complete the ones-complement sum.
 *
 * def. sum = x mod 2^16
 * def. accumulator = (x >> 16)
 *
 * At most two reduction steps
 *
 *   x := sum + accumulator
 * =    { def sum, def accumulator }
 *   x := x mod 2^16 + (x >> 16)
 * =    { x mod 2^n = x & (2^n - 1) }
 *   x := (x & 0xffff) + (x >> 16)
 *
 * are necessary to incorporate the accumulated carries (at most one per add)
 * i.e. to reduce x < 2^16 from at most 16 carries in the upper 16 bits.
 *
 * The function is also invariant over the endian of the host. Why?
 *
 * Define the unary transpose operator ~ on a bitstring in python slice
 * notation as lambda m: m[P:] + m[:P] , for some constant pivot P.
 *
 * th. ~ distributes over ones-complement addition, denoted by +_1, i.e.
 *
 *     ~m +_1 ~n  =  ~(m +_1 n)    (for all bitstrings m,n of equal length)
 *
 * proof. Regard the bitstrings in m +_1 n as split at P, forming at most two
 * 'half-adds'. Under ones-complement addition, each half-add carries to the
 * other, so the sum of each half-add is unaffected by their relative
 * order. Therefore:
 *
 *     ~m +_1 ~n
 *   =    { half-adds invariant under transposition }
 *     ~s
 *   =    { substitute }
 *     ~(m +_1 n)                   [end of proof]
 *
 * th. Summing two in-memory ones-complement 16-bit variables m,n on a machine
 * with the converse endian does not alter the result.
 *
 * proof.
 *        { converse machine endian: load/store transposes, P := 8 }
 *     ~(~m +_1 ~n)
 *   =    { ~ over +_1 }
 *     ~~m +_1 ~~n
 *   =    { ~ is an involution }
 *      m +_1 n                     [end of proof]
 *
 */
#define NEG(x) ((u_int16_t)~(x))
void
pf_cksum_fixup_a(u_int16_t *cksum, const struct pf_addr *a,
    const struct pf_addr *an, sa_family_t af, u_int8_t proto)
{
	u_int32_t	 x;
	const u_int16_t	*n = an->addr16;
	const u_int16_t *o = a->addr16;
	const int	 udp = proto == IPPROTO_UDP;

	switch (af) {
	case AF_INET:
		x = *cksum + o[0] + NEG(n[0]) + o[1] + NEG(n[1]);
		break;
#ifdef INET6
	case AF_INET6:
		x = *cksum + o[0] + NEG(n[0]) + o[1] + NEG(n[1]) +\
			     o[2] + NEG(n[2]) + o[3] + NEG(n[3]) +\
			     o[4] + NEG(n[4]) + o[5] + NEG(n[5]) +\
			     o[6] + NEG(n[6]) + o[7] + NEG(n[7]);
		break;
#endif /* INET6 */
	default:
		unhandled_af(af);
	}

	x = (x & 0xffff) + (x >> 16);
	x = (x & 0xffff) + (x >> 16);

	/* optimise: eliminate a branch when not udp */
	if (udp && *cksum == 0x0000)
		return;
	if (udp && x == 0x0000)
		x = 0xffff;

	*cksum = (u_int16_t)(x);
}

int
pf_patch_8(struct pf_pdesc *pd, u_int8_t *f, u_int8_t v, bool hi)
{
	int	rewrite = 0;

	if (*f != v) {
		u_int16_t old = htons(hi ? (*f << 8) : *f);
		u_int16_t new = htons(hi ? ( v << 8) :  v);

		pf_cksum_fixup(pd->pcksum, old, new, pd->proto);
		*f = v;
		rewrite = 1;
	}

	return (rewrite);
}

/* pre: *f is 16-bit aligned within its packet */
int
pf_patch_16(struct pf_pdesc *pd, u_int16_t *f, u_int16_t v)
{
	int	rewrite = 0;

	if (*f != v) {
		pf_cksum_fixup(pd->pcksum, *f, v, pd->proto);
		*f = v;
		rewrite = 1;
	}

	return (rewrite);
}

int
pf_patch_16_unaligned(struct pf_pdesc *pd, void *f, u_int16_t v, bool hi)
{
	int		rewrite = 0;
	u_int8_t       *fb = (u_int8_t*)f;
	u_int8_t       *vb = (u_int8_t*)&v;

	if (hi && ALIGNED_POINTER(f, u_int16_t)) {
		return (pf_patch_16(pd, f, v)); /* optimise */
	}

	rewrite += pf_patch_8(pd, fb++, *vb++, hi);
	rewrite += pf_patch_8(pd, fb++, *vb++,!hi);

	return (rewrite);
}

/* pre: *f is 16-bit aligned within its packet */
/* pre: pd->proto != IPPROTO_UDP */
int
pf_patch_32(struct pf_pdesc *pd, u_int32_t *f, u_int32_t v)
{
	int		rewrite = 0;
	u_int16_t      *pc = pd->pcksum;
	u_int8_t        proto = pd->proto;

	/* optimise: inline udp fixup code is unused; let compiler scrub it */
	if (proto == IPPROTO_UDP)
		panic("%s: udp", __func__);

	/* optimise: skip *f != v guard; true for all use-cases */
	pf_cksum_fixup(pc, *f / (1 << 16), v / (1 << 16), proto);
	pf_cksum_fixup(pc, *f % (1 << 16), v % (1 << 16), proto);

	*f = v;
	rewrite = 1;

	return (rewrite);
}

int
pf_patch_32_unaligned(struct pf_pdesc *pd, void *f, u_int32_t v, bool hi)
{
	int		rewrite = 0;
	u_int8_t       *fb = (u_int8_t*)f;
	u_int8_t       *vb = (u_int8_t*)&v;

	if (hi && ALIGNED_POINTER(f, u_int32_t)) {
		return (pf_patch_32(pd, f, v)); /* optimise */
	}

	rewrite += pf_patch_8(pd, fb++, *vb++, hi);
	rewrite += pf_patch_8(pd, fb++, *vb++,!hi);
	rewrite += pf_patch_8(pd, fb++, *vb++, hi);
	rewrite += pf_patch_8(pd, fb++, *vb++,!hi);

	return (rewrite);
}

int
pf_icmp_mapping(struct pf_pdesc *pd, u_int8_t type, int *icmp_dir,
    u_int16_t *virtual_id, u_int16_t *virtual_type)
{
	/*
	 * ICMP types marked with PF_OUT are typically responses to
	 * PF_IN, and will match states in the opposite direction.
	 * PF_IN ICMP types need to match a state with that type.
	 */
	*icmp_dir = PF_OUT;

	/* Queries (and responses) */
	switch (pd->af) {
	case AF_INET:
		switch (type) {
		case ICMP_ECHO:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ICMP_ECHOREPLY:
			*virtual_type = ICMP_ECHO;
			*virtual_id = pd->hdr.icmp.icmp_id;
			break;

		case ICMP_TSTAMP:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ICMP_TSTAMPREPLY:
			*virtual_type = ICMP_TSTAMP;
			*virtual_id = pd->hdr.icmp.icmp_id;
			break;

		case ICMP_IREQ:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ICMP_IREQREPLY:
			*virtual_type = ICMP_IREQ;
			*virtual_id = pd->hdr.icmp.icmp_id;
			break;

		case ICMP_MASKREQ:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ICMP_MASKREPLY:
			*virtual_type = ICMP_MASKREQ;
			*virtual_id = pd->hdr.icmp.icmp_id;
			break;

		case ICMP_IPV6_WHEREAREYOU:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ICMP_IPV6_IAMHERE:
			*virtual_type = ICMP_IPV6_WHEREAREYOU;
			*virtual_id = 0; /* Nothing sane to match on! */
			break;

		case ICMP_MOBILE_REGREQUEST:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ICMP_MOBILE_REGREPLY:
			*virtual_type = ICMP_MOBILE_REGREQUEST;
			*virtual_id = 0; /* Nothing sane to match on! */
			break;

		case ICMP_ROUTERSOLICIT:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ICMP_ROUTERADVERT:
			*virtual_type = ICMP_ROUTERSOLICIT;
			*virtual_id = 0; /* Nothing sane to match on! */
			break;

		/* These ICMP types map to other connections */
		case ICMP_UNREACH:
		case ICMP_SOURCEQUENCH:
		case ICMP_REDIRECT:
		case ICMP_TIMXCEED:
		case ICMP_PARAMPROB:
			/* These will not be used, but set them anyway */
			*icmp_dir = PF_IN;
			*virtual_type = htons(type);
			*virtual_id = 0;
			return (1);  /* These types match to another state */

		/*
		 * All remaining ICMP types get their own states,
		 * and will only match in one direction.
		 */
		default:
			*icmp_dir = PF_IN;
			*virtual_type = type;
			*virtual_id = 0;
			break;
		}
		break;
#ifdef INET6
	case AF_INET6:
		switch (type) {
		case ICMP6_ECHO_REQUEST:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ICMP6_ECHO_REPLY:
			*virtual_type = ICMP6_ECHO_REQUEST;
			*virtual_id = pd->hdr.icmp6.icmp6_id;
			break;

		case MLD_LISTENER_QUERY:
		case MLD_LISTENER_REPORT: {
			struct mld_hdr *mld = &pd->hdr.mld;
			u_int32_t h;

			/*
			 * Listener Report can be sent by clients
			 * without an associated Listener Query.
			 * In addition to that, when Report is sent as a
			 * reply to a Query its source and destination
			 * address are different.
			 */
			*icmp_dir = PF_IN;
			*virtual_type = MLD_LISTENER_QUERY;
			/* generate fake id for these messages */
			h = mld->mld_addr.s6_addr32[0] ^
			    mld->mld_addr.s6_addr32[1] ^
			    mld->mld_addr.s6_addr32[2] ^
			    mld->mld_addr.s6_addr32[3];
			*virtual_id = (h >> 16) ^ (h & 0xffff);
			break;
		}

		/*
		 * ICMP6_FQDN and ICMP6_NI query/reply are the same type as
		 * ICMP6_WRU
		 */
		case ICMP6_WRUREQUEST:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ICMP6_WRUREPLY:
			*virtual_type = ICMP6_WRUREQUEST;
			*virtual_id = 0; /* Nothing sane to match on! */
			break;

		case MLD_MTRACE:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case MLD_MTRACE_RESP:
			*virtual_type = MLD_MTRACE;
			*virtual_id = 0; /* Nothing sane to match on! */
			break;

		case ND_NEIGHBOR_SOLICIT:
			*icmp_dir = PF_IN;
			/* FALLTHROUGH */
		case ND_NEIGHBOR_ADVERT: {
			struct nd_neighbor_solicit *nd = &pd->hdr.nd_ns;
			u_int32_t h;

			*virtual_type = ND_NEIGHBOR_SOLICIT;
			/* generate fake id for these messages */
			h = nd->nd_ns_target.s6_addr32[0] ^
			    nd->nd_ns_target.s6_addr32[1] ^
			    nd->nd_ns_target.s6_addr32[2] ^
			    nd->nd_ns_target.s6_addr32[3];
			*virtual_id = (h >> 16) ^ (h & 0xffff);
			/*
			 * the extra work here deals with 'keep state' option
			 * at pass rule  for unsolicited advertisement.  By
			 * returning 1 (state_icmp = 1) we override 'keep
			 * state' to 'no state' so we don't create state for
			 * unsolicited advertisements. No one expects answer to
			 * unsolicited advertisements so we should be good.
			 */
			if (type == ND_NEIGHBOR_ADVERT) {
				*virtual_type = htons(*virtual_type);
				return (1);
			}
			break;
		}

		/*
		 * These ICMP types map to other connections.
		 * ND_REDIRECT can't be in this list because the triggering
		 * packet header is optional.
		 */
		case ICMP6_DST_UNREACH:
		case ICMP6_PACKET_TOO_BIG:
		case ICMP6_TIME_EXCEEDED:
		case ICMP6_PARAM_PROB:
			/* These will not be used, but set them anyway */
			*icmp_dir = PF_IN;
			*virtual_type = htons(type);
			*virtual_id = 0;
			return (1);  /* These types match to another state */
		/*
		 * All remaining ICMP6 types get their own states,
		 * and will only match in one direction.
		 */
		default:
			*icmp_dir = PF_IN;
			*virtual_type = type;
			*virtual_id = 0;
			break;
		}
		break;
#endif /* INET6 */
	}
	*virtual_type = htons(*virtual_type);
	return (0);  /* These types match to their own state */
}

void
pf_translate_icmp(struct pf_pdesc *pd, struct pf_addr *qa, u_int16_t *qp,
    struct pf_addr *oa, struct pf_addr *na, u_int16_t np)
{
	/* note: doesn't trouble to fixup quoted checksums, if any */

	/* change quoted protocol port */
	if (qp != NULL)
		pf_patch_16(pd, qp, np);

	/* change quoted ip address */
	pf_cksum_fixup_a(pd->pcksum, qa, na, pd->af, pd->proto);
	pf_addrcpy(qa, na, pd->af);

	/* change network-header's ip address */
	if (oa)
		pf_translate_a(pd, oa, na);
}

/* pre: *a is 16-bit aligned within its packet */
/*      *a is a network header src/dst address */
int
pf_translate_a(struct pf_pdesc *pd, struct pf_addr *a, struct pf_addr *an)
{
	int	rewrite = 0;

	/* warning: !PF_ANEQ != PF_AEQ */
	if (!PF_ANEQ(a, an, pd->af))
		return (0);

	/* fixup transport pseudo-header, if any */
	switch (pd->proto) {
	case IPPROTO_TCP:       /* FALLTHROUGH */
	case IPPROTO_UDP:	/* FALLTHROUGH */
	case IPPROTO_ICMPV6:
		pf_cksum_fixup_a(pd->pcksum, a, an, pd->af, pd->proto);
		break;
	default:
		break;  /* assume no pseudo-header */
	}

	pf_addrcpy(a, an, pd->af);
	rewrite = 1;

	return (rewrite);
}

#ifdef INET6
/* pf_translate_af() may change pd->m, adjust local copies after calling */
int
pf_translate_af(struct pf_pdesc *pd)
{
	static const struct pf_addr	zero;
	struct ip		       *ip4;
	struct ip6_hdr		       *ip6;
	int				copyback = 0;
	u_int				hlen, ohlen, dlen;
	u_int16_t		       *pc;
	u_int8_t			af_proto, naf_proto;

	hlen = (pd->naf == AF_INET) ? sizeof(*ip4) : sizeof(*ip6);
	ohlen = pd->off;
	dlen = pd->tot_len - pd->off;
	pc = pd->pcksum;

	af_proto = naf_proto = pd->proto;
	if (naf_proto == IPPROTO_ICMP)
		af_proto = IPPROTO_ICMPV6;
	if (naf_proto == IPPROTO_ICMPV6)
		af_proto = IPPROTO_ICMP;

	/* uncover stale pseudo-header */
	switch (af_proto) {
	case IPPROTO_ICMPV6:
		/* optimise: unchanged for TCP/UDP */
		pf_cksum_fixup(pc, htons(af_proto), 0x0, af_proto);
		pf_cksum_fixup(pc, htons(dlen),     0x0, af_proto);
				/* FALLTHROUGH */
	case IPPROTO_UDP:	/* FALLTHROUGH */
	case IPPROTO_TCP:
		pf_cksum_fixup_a(pc, pd->src, &zero, pd->af, af_proto);
		pf_cksum_fixup_a(pc, pd->dst, &zero, pd->af, af_proto);
		copyback = 1;
		break;
	default:
		break;	/* assume no pseudo-header */
	}

	/* replace the network header */
	m_adj(pd->m, pd->off);
	pd->src = NULL;
	pd->dst = NULL;

	if ((M_PREPEND(pd->m, hlen, M_DONTWAIT)) == NULL) {
		pd->m = NULL;
		return (-1);
	}

	pd->off = hlen;
	pd->tot_len += hlen - ohlen;

	switch (pd->naf) {
	case AF_INET:
		ip4 = mtod(pd->m, struct ip *);
		memset(ip4, 0, hlen);
		ip4->ip_v   = IPVERSION;
		ip4->ip_hl  = hlen >> 2;
		ip4->ip_tos = pd->tos;
		ip4->ip_len = htons(hlen + dlen);
		ip4->ip_id  = htons(ip_randomid());
		ip4->ip_off = htons(IP_DF);
		ip4->ip_ttl = pd->ttl;
		ip4->ip_p   = pd->proto;
		ip4->ip_src = pd->nsaddr.v4;
		ip4->ip_dst = pd->ndaddr.v4;
		break;
	case AF_INET6:
		ip6 = mtod(pd->m, struct ip6_hdr *);
		memset(ip6, 0, hlen);
		ip6->ip6_vfc  = IPV6_VERSION;
		ip6->ip6_flow |= htonl((u_int32_t)pd->tos << 20);
		ip6->ip6_plen = htons(dlen);
		ip6->ip6_nxt  = pd->proto;
		if (!pd->ttl || pd->ttl > IPV6_DEFHLIM)
			ip6->ip6_hlim = IPV6_DEFHLIM;
		else
			ip6->ip6_hlim = pd->ttl;
		ip6->ip6_src  = pd->nsaddr.v6;
		ip6->ip6_dst  = pd->ndaddr.v6;
		break;
	default:
		unhandled_af(pd->naf);
	}

	/* UDP over IPv6 must be checksummed per rfc2460 p27 */
	if (naf_proto == IPPROTO_UDP && *pc == 0x0000 &&
	    pd->naf == AF_INET6) {
		pd->m->m_pkthdr.csum_flags |= M_UDP_CSUM_OUT;
	}

	/* cover fresh pseudo-header */
	switch (naf_proto) {
	case IPPROTO_ICMPV6:
		/* optimise: unchanged for TCP/UDP */
		pf_cksum_fixup(pc, 0x0, htons(naf_proto), naf_proto);
		pf_cksum_fixup(pc, 0x0, htons(dlen),      naf_proto);
				/* FALLTHROUGH */
	case IPPROTO_UDP:	/* FALLTHROUGH */
	case IPPROTO_TCP:
		pf_cksum_fixup_a(pc, &zero, &pd->nsaddr, pd->naf, naf_proto);
		pf_cksum_fixup_a(pc, &zero, &pd->ndaddr, pd->naf, naf_proto);
		copyback = 1;
		break;
	default:
		break;	/* assume no pseudo-header */
	}

	/* flush pd->pcksum */
	if (copyback)
		m_copyback(pd->m, pd->off, pd->hdrlen, &pd->hdr, M_NOWAIT);

	return (0);
}

int
pf_change_icmp_af(struct mbuf *m, int ipoff2, struct pf_pdesc *pd,
    struct pf_pdesc *pd2, struct pf_addr *src, struct pf_addr *dst,
    sa_family_t af, sa_family_t naf)
{
	struct mbuf		*n = NULL;
	struct ip		*ip4;
	struct ip6_hdr		*ip6;
	u_int			 hlen, ohlen, dlen;
	int			 d;

	if (af == naf || (af != AF_INET && af != AF_INET6) ||
	    (naf != AF_INET && naf != AF_INET6))
		return (-1);

	/* split the mbuf chain on the quoted ip/ip6 header boundary */
	if ((n = m_split(m, ipoff2, M_DONTWAIT)) == NULL)
		return (-1);

	/* new quoted header */
	hlen = naf == AF_INET ? sizeof(*ip4) : sizeof(*ip6);
	/* old quoted header */
	ohlen = pd2->off - ipoff2;

	/* trim old quoted header */
	pf_cksum_uncover(pd->pcksum, in_cksum(n, ohlen), pd->proto);
	m_adj(n, ohlen);

	/* prepend a new, translated, quoted header */
	if ((M_PREPEND(n, hlen, M_DONTWAIT)) == NULL)
		return (-1);

	switch (naf) {
	case AF_INET:
		ip4 = mtod(n, struct ip *);
		memset(ip4, 0, sizeof(*ip4));
		ip4->ip_v   = IPVERSION;
		ip4->ip_hl  = sizeof(*ip4) >> 2;
		ip4->ip_len = htons(sizeof(*ip4) + pd2->tot_len - ohlen);
		ip4->ip_id  = htons(ip_randomid());
		ip4->ip_off = htons(IP_DF);
		ip4->ip_ttl = pd2->ttl;
		if (pd2->proto == IPPROTO_ICMPV6)
			ip4->ip_p = IPPROTO_ICMP;
		else
			ip4->ip_p = pd2->proto;
		ip4->ip_src = src->v4;
		ip4->ip_dst = dst->v4;
		in_hdr_cksum_out(n, NULL);
		break;
	case AF_INET6:
		ip6 = mtod(n, struct ip6_hdr *);
		memset(ip6, 0, sizeof(*ip6));
		ip6->ip6_vfc  = IPV6_VERSION;
		ip6->ip6_plen = htons(pd2->tot_len - ohlen);
		if (pd2->proto == IPPROTO_ICMP)
			ip6->ip6_nxt = IPPROTO_ICMPV6;
		else
			ip6->ip6_nxt = pd2->proto;
		if (!pd2->ttl || pd2->ttl > IPV6_DEFHLIM)
			ip6->ip6_hlim = IPV6_DEFHLIM;
		else
			ip6->ip6_hlim = pd2->ttl;
		ip6->ip6_src  = src->v6;
		ip6->ip6_dst  = dst->v6;
		break;
	}

	/* cover new quoted header */
	/* optimise: any new AF_INET header of ours sums to zero */
	if (naf != AF_INET) {
		pf_cksum_cover(pd->pcksum, in_cksum(n, hlen), pd->proto);
	}

	/* reattach modified quoted packet to outer header */
	{
		int nlen = n->m_pkthdr.len;
		m_cat(m, n);
		m->m_pkthdr.len += nlen;
	}

	/* account for altered length */
	d = hlen - ohlen;

	if (pd->proto == IPPROTO_ICMPV6) {
		/* fixup pseudo-header */
		dlen = pd->tot_len - pd->off;
		pf_cksum_fixup(pd->pcksum,
		    htons(dlen), htons(dlen + d), pd->proto);
	}

	pd->tot_len  += d;
	pd2->tot_len += d;
	pd2->off     += d;

	/* note: not bothering to update network headers as
	   these due for rewrite by pf_translate_af() */

	return (0);
}


#define PTR_IP(field)	(offsetof(struct ip, field))
#define PTR_IP6(field)	(offsetof(struct ip6_hdr, field))

int
pf_translate_icmp_af(struct pf_pdesc *pd, int af, void *arg)
{
	struct icmp		*icmp4;
	struct icmp6_hdr	*icmp6;
	u_int32_t		 mtu;
	int32_t			 ptr = -1;
	u_int8_t		 type;
	u_int8_t		 code;

	switch (af) {
	case AF_INET:
		icmp6 = arg;
		type  = icmp6->icmp6_type;
		code  = icmp6->icmp6_code;
		mtu   = ntohl(icmp6->icmp6_mtu);

		switch (type) {
		case ICMP6_ECHO_REQUEST:
			type = ICMP_ECHO;
			break;
		case ICMP6_ECHO_REPLY:
			type = ICMP_ECHOREPLY;
			break;
		case ICMP6_DST_UNREACH:
			type = ICMP_UNREACH;
			switch (code) {
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
				return (-1);
			}
			break;
		case ICMP6_PACKET_TOO_BIG:
			type = ICMP_UNREACH;
			code = ICMP_UNREACH_NEEDFRAG;
			mtu -= 20;
			break;
		case ICMP6_TIME_EXCEEDED:
			type = ICMP_TIMXCEED;
			break;
		case ICMP6_PARAM_PROB:
			switch (code) {
			case ICMP6_PARAMPROB_HEADER:
				type = ICMP_PARAMPROB;
				code = ICMP_PARAMPROB_ERRATPTR;
				ptr  = ntohl(icmp6->icmp6_pptr);

				if (ptr == PTR_IP6(ip6_vfc))
					; /* preserve */
				else if (ptr == PTR_IP6(ip6_vfc) + 1)
					ptr = PTR_IP(ip_tos);
				else if (ptr == PTR_IP6(ip6_plen) ||
				    ptr == PTR_IP6(ip6_plen) + 1)
					ptr = PTR_IP(ip_len);
				else if (ptr == PTR_IP6(ip6_nxt))
					ptr = PTR_IP(ip_p);
				else if (ptr == PTR_IP6(ip6_hlim))
					ptr = PTR_IP(ip_ttl);
				else if (ptr >= PTR_IP6(ip6_src) &&
				    ptr < PTR_IP6(ip6_dst))
					ptr = PTR_IP(ip_src);
				else if (ptr >= PTR_IP6(ip6_dst) &&
				    ptr < sizeof(struct ip6_hdr))
					ptr = PTR_IP(ip_dst);
				else {
					return (-1);
				}
				break;
			case ICMP6_PARAMPROB_NEXTHEADER:
				type = ICMP_UNREACH;
				code = ICMP_UNREACH_PROTOCOL;
				break;
			default:
				return (-1);
			}
			break;
		default:
			return (-1);
		}

		pf_patch_8(pd, &icmp6->icmp6_type, type, PF_HI);
		pf_patch_8(pd, &icmp6->icmp6_code, code, PF_LO);

		/* aligns well with a icmpv4 nextmtu */
		pf_patch_32(pd, &icmp6->icmp6_mtu, htonl(mtu));

		/* icmpv4 pptr is a one most significant byte */
		if (ptr >= 0)
			pf_patch_32(pd, &icmp6->icmp6_pptr, htonl(ptr << 24));
		break;
	case AF_INET6:
		icmp4 = arg;
		type  = icmp4->icmp_type;
		code  = icmp4->icmp_code;
		mtu   = ntohs(icmp4->icmp_nextmtu);

		switch (type) {
		case ICMP_ECHO:
			type = ICMP6_ECHO_REQUEST;
			break;
		case ICMP_ECHOREPLY:
			type = ICMP6_ECHO_REPLY;
			break;
		case ICMP_UNREACH:
			type = ICMP6_DST_UNREACH;
			switch (code) {
			case ICMP_UNREACH_NET:
			case ICMP_UNREACH_HOST:
			case ICMP_UNREACH_NET_UNKNOWN:
			case ICMP_UNREACH_HOST_UNKNOWN:
			case ICMP_UNREACH_ISOLATED:
			case ICMP_UNREACH_TOSNET:
			case ICMP_UNREACH_TOSHOST:
				code = ICMP6_DST_UNREACH_NOROUTE;
				break;
			case ICMP_UNREACH_PORT:
				code = ICMP6_DST_UNREACH_NOPORT;
				break;
			case ICMP_UNREACH_NET_PROHIB:
			case ICMP_UNREACH_HOST_PROHIB:
			case ICMP_UNREACH_FILTER_PROHIB:
			case ICMP_UNREACH_PRECEDENCE_CUTOFF:
				code = ICMP6_DST_UNREACH_ADMIN;
				break;
			case ICMP_UNREACH_PROTOCOL:
				type = ICMP6_PARAM_PROB;
				code = ICMP6_PARAMPROB_NEXTHEADER;
				ptr  = offsetof(struct ip6_hdr, ip6_nxt);
				break;
			case ICMP_UNREACH_NEEDFRAG:
				type = ICMP6_PACKET_TOO_BIG;
				code = 0;
				mtu += 20;
				break;
			default:
				return (-1);
			}
			break;
		case ICMP_TIMXCEED:
			type = ICMP6_TIME_EXCEEDED;
			break;
		case ICMP_PARAMPROB:
			type = ICMP6_PARAM_PROB;
			switch (code) {
			case ICMP_PARAMPROB_ERRATPTR:
				code = ICMP6_PARAMPROB_HEADER;
				break;
			case ICMP_PARAMPROB_LENGTH:
				code = ICMP6_PARAMPROB_HEADER;
				break;
			default:
				return (-1);
			}

			ptr = icmp4->icmp_pptr;
			if (ptr == 0 || ptr == PTR_IP(ip_tos))
				; /* preserve */
			else if (ptr == PTR_IP(ip_len) ||
			    ptr == PTR_IP(ip_len) + 1)
				ptr = PTR_IP6(ip6_plen);
			else if (ptr == PTR_IP(ip_ttl))
				ptr = PTR_IP6(ip6_hlim);
			else if (ptr == PTR_IP(ip_p))
				ptr = PTR_IP6(ip6_nxt);
			else if (ptr >= PTR_IP(ip_src) &&
			    ptr < PTR_IP(ip_dst))
				ptr = PTR_IP6(ip6_src);
			else if (ptr >= PTR_IP(ip_dst) &&
			    ptr < sizeof(struct ip))
				ptr = PTR_IP6(ip6_dst);
			else {
				return (-1);
			}
			break;
		default:
			return (-1);
		}

		pf_patch_8(pd, &icmp4->icmp_type, type, PF_HI);
		pf_patch_8(pd, &icmp4->icmp_code, code, PF_LO);
		pf_patch_16(pd, &icmp4->icmp_nextmtu, htons(mtu));
		if (ptr >= 0)
			pf_patch_32(pd, &icmp4->icmp_void, htonl(ptr));
		break;
	}

	return (0);
}
#endif /* INET6 */

/*
 * Need to modulate the sequence numbers in the TCP SACK option
 * (credits to Krzysztof Pfaff for report and patch)
 */
int
pf_modulate_sack(struct pf_pdesc *pd, struct pf_state_peer *dst)
{
	struct sackblk	 sack;
	int		 copyback = 0, i;
	int		 olen, optsoff;
	u_int8_t	 opts[MAX_TCPOPTLEN], *opt, *eoh;

	olen = (pd->hdr.tcp.th_off << 2) - sizeof(struct tcphdr);
	optsoff = pd->off + sizeof(struct tcphdr);
#define TCPOLEN_MINSACK	(TCPOLEN_SACK + 2)
	if (olen < TCPOLEN_MINSACK ||
	    !pf_pull_hdr(pd->m, optsoff, opts, olen, NULL, pd->af))
		return (0);

	eoh = opts + olen;
	opt = opts;
	while ((opt = pf_find_tcpopt(opt, opts, olen,
		    TCPOPT_SACK, TCPOLEN_MINSACK)) != NULL)
	{
		size_t safelen = MIN(opt[1], (eoh - opt));
		for (i = 2; i + TCPOLEN_SACK <= safelen; i += TCPOLEN_SACK) {
			size_t startoff = (opt + i) - opts;
			memcpy(&sack, &opt[i], sizeof(sack));
			pf_patch_32_unaligned(pd, &sack.start,
			    htonl(ntohl(sack.start) - dst->seqdiff),
			    PF_ALGNMNT(startoff));
			pf_patch_32_unaligned(pd, &sack.end,
			    htonl(ntohl(sack.end) - dst->seqdiff),
			    PF_ALGNMNT(startoff + sizeof(sack.start)));
			memcpy(&opt[i], &sack, sizeof(sack));
		}
		copyback = 1;
		opt += opt[1];
	}

	if (copyback)
		m_copyback(pd->m, optsoff, olen, opts, M_NOWAIT);
	return (copyback);
}

struct mbuf *
pf_build_tcp(const struct pf_rule *r, sa_family_t af,
    const struct pf_addr *saddr, const struct pf_addr *daddr,
    u_int16_t sport, u_int16_t dport, u_int32_t seq, u_int32_t ack,
    u_int8_t flags, u_int16_t win, u_int16_t mss, u_int8_t ttl, int tag,
    u_int16_t rtag, u_int sack, u_int rdom, u_short *reason)
{
	struct mbuf	*m;
	int		 len, tlen;
	struct ip	*h;
#ifdef INET6
	struct ip6_hdr	*h6;
#endif /* INET6 */
	struct tcphdr	*th;
	char		*opt;

	/* maximum segment size tcp option */
	tlen = sizeof(struct tcphdr);
	if (mss)
		tlen += 4;
	if (sack)
		tlen += 2;

	switch (af) {
	case AF_INET:
		len = sizeof(struct ip) + tlen;
		break;
#ifdef INET6
	case AF_INET6:
		len = sizeof(struct ip6_hdr) + tlen;
		break;
#endif /* INET6 */
	default:
		unhandled_af(af);
	}

	/* create outgoing mbuf */
	m = m_gethdr(M_DONTWAIT, MT_HEADER);
	if (m == NULL) {
		REASON_SET(reason, PFRES_MEMORY);
		return (NULL);
	}
	if (tag)
		m->m_pkthdr.pf.flags |= PF_TAG_GENERATED;
	m->m_pkthdr.pf.tag = rtag;
	m->m_pkthdr.ph_rtableid = rdom;
	if (r && (r->scrub_flags & PFSTATE_SETPRIO))
		m->m_pkthdr.pf.prio = r->set_prio[0];
	if (r && r->qid)
		m->m_pkthdr.pf.qid = r->qid;
	m->m_data += max_linkhdr;
	m->m_pkthdr.len = m->m_len = len;
	m->m_pkthdr.ph_ifidx = 0;
	m->m_pkthdr.csum_flags |= M_TCP_CSUM_OUT;
	memset(m->m_data, 0, len);
	switch (af) {
	case AF_INET:
		h = mtod(m, struct ip *);
		h->ip_p = IPPROTO_TCP;
		h->ip_len = htons(tlen);
		h->ip_v = 4;
		h->ip_hl = sizeof(*h) >> 2;
		h->ip_tos = IPTOS_LOWDELAY;
		h->ip_len = htons(len);
		h->ip_off = htons(atomic_load_int(&ip_mtudisc) ? IP_DF : 0);
		h->ip_ttl = ttl ? ttl : atomic_load_int(&ip_defttl);
		h->ip_sum = 0;
		h->ip_src.s_addr = saddr->v4.s_addr;
		h->ip_dst.s_addr = daddr->v4.s_addr;

		th = (struct tcphdr *)((caddr_t)h + sizeof(struct ip));
		break;
#ifdef INET6
	case AF_INET6:
		h6 = mtod(m, struct ip6_hdr *);
		h6->ip6_nxt = IPPROTO_TCP;
		h6->ip6_plen = htons(tlen);
		h6->ip6_vfc |= IPV6_VERSION;
		h6->ip6_hlim = IPV6_DEFHLIM;
		memcpy(&h6->ip6_src, &saddr->v6, sizeof(struct in6_addr));
		memcpy(&h6->ip6_dst, &daddr->v6, sizeof(struct in6_addr));

		th = (struct tcphdr *)((caddr_t)h6 + sizeof(struct ip6_hdr));
		break;
#endif /* INET6 */
	default:
		unhandled_af(af);
	}

	/* TCP header */
	th->th_sport = sport;
	th->th_dport = dport;
	th->th_seq = htonl(seq);
	th->th_ack = htonl(ack);
	th->th_off = tlen >> 2;
	th->th_flags = flags;
	th->th_win = htons(win);

	opt = (char *)(th + 1);
	if (mss) {
		opt[0] = TCPOPT_MAXSEG;
		opt[1] = 4;
		mss = htons(mss);
		memcpy((opt + 2), &mss, 2);
		opt += 4;
	}
	if (sack) {
		opt[0] = TCPOPT_SACK_PERMITTED;
		opt[1] = 2;
		opt += 2;
	}

	return (m);
}

void
pf_send_tcp(const struct pf_rule *r, sa_family_t af,
    const struct pf_addr *saddr, const struct pf_addr *daddr,
    u_int16_t sport, u_int16_t dport, u_int32_t seq, u_int32_t ack,
    u_int8_t flags, u_int16_t win, u_int16_t mss, u_int8_t ttl, int tag,
    u_int16_t rtag, u_int rdom, u_short *reason)
{
	struct mbuf	*m;

	if ((m = pf_build_tcp(r, af, saddr, daddr, sport, dport, seq, ack,
	    flags, win, mss, ttl, tag, rtag, 0, rdom, reason)) == NULL)
		return;

	switch (af) {
	case AF_INET:
		ip_send(m);
		break;
#ifdef INET6
	case AF_INET6:
		ip6_send(m);
		break;
#endif /* INET6 */
	}
}

static void
pf_send_challenge_ack(struct pf_pdesc *pd, struct pf_state *st,
    struct pf_state_peer *src, struct pf_state_peer *dst, u_short *reason)
{
	/*
	 * We are sending challenge ACK as a response to SYN packet, which
	 * matches existing state (modulo TCP window check). Therefore packet
	 * must be sent on behalf of destination.
	 *
	 * We expect sender to remain either silent, or send RST packet
	 * so both, firewall and remote peer, can purge dead state from
	 * memory.
	 */
	pf_send_tcp(st->rule.ptr, pd->af, pd->dst, pd->src,
	    pd->hdr.tcp.th_dport, pd->hdr.tcp.th_sport, dst->seqlo,
	    src->seqlo, TH_ACK, 0, 0, st->rule.ptr->return_ttl, 1, 0,
	    pd->rdomain, reason);
}

void
pf_send_icmp(struct mbuf *m, u_int8_t type, u_int8_t code, int param,
    sa_family_t af, struct pf_rule *r, u_int rdomain)
{
	struct mbuf	*m0;

	if ((m0 = m_copym(m, 0, M_COPYALL, M_NOWAIT)) == NULL)
		return;

	m0->m_pkthdr.pf.flags |= PF_TAG_GENERATED;
	m0->m_pkthdr.ph_rtableid = rdomain;
	if (r && (r->scrub_flags & PFSTATE_SETPRIO))
		m0->m_pkthdr.pf.prio = r->set_prio[0];
	if (r && r->qid)
		m0->m_pkthdr.pf.qid = r->qid;

	switch (af) {
	case AF_INET:
		icmp_error(m0, type, code, 0, param);
		break;
#ifdef INET6
	case AF_INET6:
		icmp6_error(m0, type, code, param);
		break;
#endif /* INET6 */
	}
}

/*
 * Return ((n = 0) == (a = b [with mask m]))
 * Note: n != 0 => returns (a != b [with mask m])
 */
int
pf_match_addr(u_int8_t n, struct pf_addr *a, struct pf_addr *m,
    struct pf_addr *b, sa_family_t af)
{
	switch (af) {
	case AF_INET:
		if ((a->addr32[0] & m->addr32[0]) ==
		    (b->addr32[0] & m->addr32[0]))
			return (n == 0);
		break;
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
			return (n == 0);
		break;
#endif /* INET6 */
	}

	return (n != 0);
}

/*
 * Return 1 if b <= a <= e, otherwise return 0.
 */
int
pf_match_addr_range(struct pf_addr *b, struct pf_addr *e,
    struct pf_addr *a, sa_family_t af)
{
	switch (af) {
	case AF_INET:
		if ((ntohl(a->addr32[0]) < ntohl(b->addr32[0])) ||
		    (ntohl(a->addr32[0]) > ntohl(e->addr32[0])))
			return (0);
		break;
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

int
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
	return (pf_match(op, ntohs(a1), ntohs(a2), ntohs(p)));
}

int
pf_match_uid(u_int8_t op, uid_t a1, uid_t a2, uid_t u)
{
	if (u == -1 && op != PF_OP_EQ && op != PF_OP_NE)
		return (0);
	return (pf_match(op, a1, a2, u));
}

int
pf_match_gid(u_int8_t op, gid_t a1, gid_t a2, gid_t g)
{
	if (g == -1 && op != PF_OP_EQ && op != PF_OP_NE)
		return (0);
	return (pf_match(op, a1, a2, g));
}

int
pf_match_tag(struct mbuf *m, struct pf_rule *r, int *tag)
{
	if (*tag == -1)
		*tag = m->m_pkthdr.pf.tag;

	return ((!r->match_tag_not && r->match_tag == *tag) ||
	    (r->match_tag_not && r->match_tag != *tag));
}

int
pf_match_rcvif(struct mbuf *m, struct pf_rule *r)
{
	struct ifnet *ifp;
#if NCARP > 0
	struct ifnet *ifp0;
#endif
	struct pfi_kif *kif;

	ifp = if_get(m->m_pkthdr.ph_ifidx);
	if (ifp == NULL)
		return (0);

#if NCARP > 0
	if (ifp->if_type == IFT_CARP &&
	    (ifp0 = if_get(ifp->if_carpdevidx)) != NULL) {
		kif = (struct pfi_kif *)ifp0->if_pf_kif;
		if_put(ifp0);
	} else
#endif /* NCARP */
		kif = (struct pfi_kif *)ifp->if_pf_kif;

	if_put(ifp);

	if (kif == NULL) {
		DPFPRINTF(LOG_ERR,
		    "%s: kif == NULL, @%d via %s", __func__,
		    r->nr, r->rcv_ifname);
		return (0);
	}

	return (pfi_kif_match(r->rcv_kif, kif));
}

void
pf_tag_packet(struct mbuf *m, int tag, int rtableid)
{
	if (tag > 0)
		m->m_pkthdr.pf.tag = tag;
	if (rtableid >= 0)
		m->m_pkthdr.ph_rtableid = (u_int)rtableid;
}

void
pf_anchor_stack_init(void)
{
	struct pf_anchor_stackframe *stack;

	stack = (struct pf_anchor_stackframe *)cpumem_enter(pf_anchor_stack);
	stack[PF_ANCHOR_STACK_MAX].sf_stack_top = &stack[0];
	cpumem_leave(pf_anchor_stack, stack);
}

int
pf_anchor_stack_is_full(struct pf_anchor_stackframe *sf)
{
	struct pf_anchor_stackframe *stack;
	int rv;

	stack = (struct pf_anchor_stackframe *)cpumem_enter(pf_anchor_stack);
	rv = (sf == &stack[PF_ANCHOR_STACK_MAX]);
	cpumem_leave(pf_anchor_stack, stack);

	return (rv);
}

int
pf_anchor_stack_is_empty(struct pf_anchor_stackframe *sf)
{
	struct pf_anchor_stackframe *stack;
	int rv;

	stack = (struct pf_anchor_stackframe *)cpumem_enter(pf_anchor_stack);
	rv = (sf == &stack[0]);
	cpumem_leave(pf_anchor_stack, stack);

	return (rv);
}

struct pf_anchor_stackframe *
pf_anchor_stack_top(void)
{
	struct pf_anchor_stackframe *stack;
	struct pf_anchor_stackframe *top_sf;

	stack = (struct pf_anchor_stackframe *)cpumem_enter(pf_anchor_stack);
	top_sf = stack[PF_ANCHOR_STACK_MAX].sf_stack_top;
	cpumem_leave(pf_anchor_stack, stack);

	return (top_sf);
}

int
pf_anchor_stack_push(struct pf_ruleset *rs, struct pf_rule *anchor,
    struct pf_rule *r, struct pf_anchor *child, int jump_target)
{
	struct pf_anchor_stackframe *stack;
	struct pf_anchor_stackframe *top_sf = pf_anchor_stack_top();

	top_sf++;
	if (pf_anchor_stack_is_full(top_sf))
		return (-1);

	top_sf->sf_rs = rs;
	top_sf->sf_anchor = anchor;
	top_sf->sf_r = r;
	top_sf->sf_child = child;
	top_sf->sf_jump_target = jump_target;

	stack = (struct pf_anchor_stackframe *)cpumem_enter(pf_anchor_stack);

	if ((top_sf <= &stack[0]) || (top_sf >= &stack[PF_ANCHOR_STACK_MAX]))
		panic("%s: top frame outside of anchor stack range", __func__);

	stack[PF_ANCHOR_STACK_MAX].sf_stack_top = top_sf;
	cpumem_leave(pf_anchor_stack, stack);

	return (0);
}

int
pf_anchor_stack_pop(struct pf_ruleset **rs, struct pf_rule **anchor,
    struct pf_rule **r, struct pf_anchor **child, int *jump_target)
{
	struct pf_anchor_stackframe *top_sf = pf_anchor_stack_top();
	struct pf_anchor_stackframe *stack;
	int on_top;

	stack = (struct pf_anchor_stackframe *)cpumem_enter(pf_anchor_stack);
	if (pf_anchor_stack_is_empty(top_sf)) {
		on_top = -1;
	} else {
		if ((top_sf <= &stack[0]) ||
		    (top_sf >= &stack[PF_ANCHOR_STACK_MAX]))
			panic("%s: top frame outside of anchor stack range",
			    __func__);

		*rs = top_sf->sf_rs;
		*anchor = top_sf->sf_anchor;
		*r = top_sf->sf_r;
		*child = top_sf->sf_child;
		*jump_target = top_sf->sf_jump_target;
		top_sf--;
		stack[PF_ANCHOR_STACK_MAX].sf_stack_top = top_sf;
		on_top = 0;
	}
	cpumem_leave(pf_anchor_stack, stack);

	return (on_top);
}

void
pf_poolmask(struct pf_addr *naddr, struct pf_addr *raddr,
    struct pf_addr *rmask, struct pf_addr *saddr, sa_family_t af)
{
	switch (af) {
	case AF_INET:
		naddr->addr32[0] = (raddr->addr32[0] & rmask->addr32[0]) |
		((rmask->addr32[0] ^ 0xffffffff ) & saddr->addr32[0]);
		break;
#ifdef INET6
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
#endif /* INET6 */
	default:
		unhandled_af(af);
	}
}

void
pf_addr_inc(struct pf_addr *addr, sa_family_t af)
{
	switch (af) {
	case AF_INET:
		addr->addr32[0] = htonl(ntohl(addr->addr32[0]) + 1);
		break;
#ifdef INET6
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
#endif /* INET6 */
	default:
		unhandled_af(af);
	}
}

int
pf_socket_lookup(struct pf_pdesc *pd)
{
	struct pf_addr		*saddr, *daddr;
	u_int16_t		 sport, dport;
	struct inpcbtable	*table;
	struct inpcb		*inp;

	pd->lookup.uid = -1;
	pd->lookup.gid = -1;
	pd->lookup.pid = NO_PID;
	switch (pd->virtual_proto) {
	case IPPROTO_TCP:
		sport = pd->hdr.tcp.th_sport;
		dport = pd->hdr.tcp.th_dport;
		PF_ASSERT_LOCKED();
		NET_ASSERT_LOCKED();
		table = &tcbtable;
		break;
	case IPPROTO_UDP:
		sport = pd->hdr.udp.uh_sport;
		dport = pd->hdr.udp.uh_dport;
		PF_ASSERT_LOCKED();
		NET_ASSERT_LOCKED();
		table = &udbtable;
		break;
	default:
		return (-1);
	}
	if (pd->dir == PF_IN) {
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
	case AF_INET:
		/*
		 * Fails when rtable is changed while evaluating the ruleset
		 * The socket looked up will not match the one hit in the end.
		 */
		inp = in_pcblookup(table, saddr->v4, sport, daddr->v4, dport,
		    pd->rdomain);
		if (inp == NULL) {
			inp = in_pcblookup_listen(table, daddr->v4, dport,
			    NULL, pd->rdomain);
			if (inp == NULL)
				return (-1);
		}
		break;
#ifdef INET6
	case AF_INET6:
		if (pd->virtual_proto == IPPROTO_UDP)
			table = &udb6table;
		if (pd->virtual_proto == IPPROTO_TCP)
			table = &tcb6table;
		inp = in6_pcblookup(table, &saddr->v6, sport, &daddr->v6,
		    dport, pd->rdomain);
		if (inp == NULL) {
			inp = in6_pcblookup_listen(table, &daddr->v6, dport,
			    NULL, pd->rdomain);
			if (inp == NULL)
				return (-1);
		}
		break;
#endif /* INET6 */
	default:
		unhandled_af(pd->af);
	}
	pd->lookup.uid = inp->inp_socket->so_euid;
	pd->lookup.gid = inp->inp_socket->so_egid;
	pd->lookup.pid = inp->inp_socket->so_cpid;
	in_pcbunref(inp);
	return (1);
}

/* post: r  => (r[0] == type /\ r[1] >= min_typelen >= 2  "validity"
 *                      /\ (eoh - r) >= min_typelen >= 2  "safety"  )
 *
 * warning: r + r[1] may exceed opts bounds for r[1] > min_typelen
 */
u_int8_t*
pf_find_tcpopt(u_int8_t *opt, u_int8_t *opts, size_t hlen, u_int8_t type,
    u_int8_t min_typelen)
{
	u_int8_t *eoh = opts + hlen;

	if (min_typelen < 2)
		return (NULL);

	while ((eoh - opt) >= min_typelen) {
		switch (*opt) {
		case TCPOPT_EOL:
			/* FALLTHROUGH - Workaround the failure of some
			   systems to NOP-pad their bzero'd option buffers,
			   producing spurious EOLs */
		case TCPOPT_NOP:
			opt++;
			continue;
		default:
			if (opt[0] == type &&
			    opt[1] >= min_typelen)
				return (opt);
		}

		opt += MAX(opt[1], 2); /* evade infinite loops */
	}

	return (NULL);
}

u_int8_t
pf_get_wscale(struct pf_pdesc *pd)
{
	int		 olen;
	u_int8_t	 opts[MAX_TCPOPTLEN], *opt;
	u_int8_t	 wscale = 0;

	olen = (pd->hdr.tcp.th_off << 2) - sizeof(struct tcphdr);
	if (olen < TCPOLEN_WINDOW || !pf_pull_hdr(pd->m,
	    pd->off + sizeof(struct tcphdr), opts, olen, NULL, pd->af))
		return (0);

	opt = opts;
	while ((opt = pf_find_tcpopt(opt, opts, olen,
		    TCPOPT_WINDOW, TCPOLEN_WINDOW)) != NULL) {
		wscale = opt[2];
		wscale = MIN(wscale, TCP_MAX_WINSHIFT);
		wscale |= PF_WSCALE_FLAG;

		opt += opt[1];
	}

	return (wscale);
}

u_int16_t
pf_get_mss(struct pf_pdesc *pd, uint16_t mssdflt)
{
	int		 olen;
	u_int8_t	 opts[MAX_TCPOPTLEN], *opt;
	u_int16_t	 mss;

	olen = (pd->hdr.tcp.th_off << 2) - sizeof(struct tcphdr);
	if (olen < TCPOLEN_MAXSEG || !pf_pull_hdr(pd->m,
	    pd->off + sizeof(struct tcphdr), opts, olen, NULL, pd->af))
		return (0);

	mss = mssdflt;
	opt = opts;
	while ((opt = pf_find_tcpopt(opt, opts, olen,
		    TCPOPT_MAXSEG, TCPOLEN_MAXSEG)) != NULL) {
			memcpy(&mss, (opt + 2), 2);
			mss = ntohs(mss);

			opt += opt[1];
	}
	return (mss);
}

u_int16_t
pf_calc_mss(struct pf_addr *addr, sa_family_t af, int rtableid, uint16_t offer,
    uint16_t mssdflt)
{
	struct ifnet		*ifp;
	struct sockaddr_in	*dst;
#ifdef INET6
	struct sockaddr_in6	*dst6;
#endif /* INET6 */
	struct rtentry		*rt = NULL;
	struct sockaddr_storage	 ss;
	int			 hlen, mss;

	memset(&ss, 0, sizeof(ss));

	switch (af) {
	case AF_INET:
		hlen = sizeof(struct ip);
		dst = (struct sockaddr_in *)&ss;
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = addr->v4;
		rt = rtalloc(sintosa(dst), 0, rtableid);
		break;
#ifdef INET6
	case AF_INET6:
		hlen = sizeof(struct ip6_hdr);
		dst6 = (struct sockaddr_in6 *)&ss;
		dst6->sin6_family = AF_INET6;
		dst6->sin6_len = sizeof(*dst6);
		dst6->sin6_addr = addr->v6;
		rt = rtalloc(sin6tosa(dst6), 0, rtableid);
		break;
#endif /* INET6 */
	}

	mss = mssdflt;
	if (rt != NULL && (ifp = if_get(rt->rt_ifidx)) != NULL) {
		mss = ifp->if_mtu - hlen - sizeof(struct tcphdr);
		mss = imax(mss, mssdflt);
		if_put(ifp);
	}
	rtfree(rt);
	mss = imin(mss, offer);
	mss = imax(mss, 64);		/* sanity - at least max opt space */
	return (mss);
}

static __inline int
pf_set_rt_ifp(struct pf_state *st, struct pf_addr *saddr, sa_family_t af,
    struct pf_src_node **sns)
{
	struct pf_rule *r = st->rule.ptr;
	int	rv;

	if (!r->rt)
		return (0);

	rv = pf_map_addr(af, r, saddr, &st->rt_addr, NULL, sns,
	    &r->route, PF_SN_ROUTE);
	if (rv == 0)
		st->rt = r->rt;

	return (rv);
}

u_int32_t
pf_tcp_iss(struct pf_pdesc *pd)
{
	SHA2_CTX ctx;
	union {
		uint8_t bytes[SHA512_DIGEST_LENGTH];
		uint32_t words[1];
	} digest;

	if (pf_tcp_secret_init == 0) {
		arc4random_buf(pf_tcp_secret, sizeof(pf_tcp_secret));
		SHA512Init(&pf_tcp_secret_ctx);
		SHA512Update(&pf_tcp_secret_ctx, pf_tcp_secret,
		    sizeof(pf_tcp_secret));
		pf_tcp_secret_init = 1;
	}
	ctx = pf_tcp_secret_ctx;

	SHA512Update(&ctx, &pd->rdomain, sizeof(pd->rdomain));
	SHA512Update(&ctx, &pd->hdr.tcp.th_sport, sizeof(u_short));
	SHA512Update(&ctx, &pd->hdr.tcp.th_dport, sizeof(u_short));
	switch (pd->af) {
	case AF_INET:
		SHA512Update(&ctx, &pd->src->v4, sizeof(struct in_addr));
		SHA512Update(&ctx, &pd->dst->v4, sizeof(struct in_addr));
		break;
#ifdef INET6
	case AF_INET6:
		SHA512Update(&ctx, &pd->src->v6, sizeof(struct in6_addr));
		SHA512Update(&ctx, &pd->dst->v6, sizeof(struct in6_addr));
		break;
#endif /* INET6 */
	}
	SHA512Final(digest.bytes, &ctx);
	pf_tcp_iss_off += 4096;
	return (digest.words[0] + READ_ONCE(tcp_iss) + pf_tcp_iss_off);
}

void
pf_rule_to_actions(struct pf_rule *r, struct pf_rule_actions *a)
{
	if (r->qid)
		a->qid = r->qid;
	if (r->pqid)
		a->pqid = r->pqid;
	if (r->rtableid >= 0)
		a->rtableid = r->rtableid;
#if NPFLOG > 0
	a->log |= r->log;
#endif	/* NPFLOG > 0 */
	if (r->scrub_flags & PFSTATE_SETTOS)
		a->set_tos = r->set_tos;
	if (r->min_ttl)
		a->min_ttl = r->min_ttl;
	if (r->max_mss)
		a->max_mss = r->max_mss;
	a->flags |= (r->scrub_flags & (PFSTATE_NODF|PFSTATE_RANDOMID|
	    PFSTATE_SETTOS|PFSTATE_SCRUB_TCP|PFSTATE_SETPRIO));
	if (r->scrub_flags & PFSTATE_SETPRIO) {
		a->set_prio[0] = r->set_prio[0];
		a->set_prio[1] = r->set_prio[1];
	}
	if (r->rule_flag & PFRULE_SETDELAY)
		a->delay = r->delay;
}

#define PF_TEST_ATTRIB(t, a)			\
	if (t) {				\
		r = a;				\
		continue;			\
	} else do {				\
	} while (0)

enum pf_test_status
pf_match_rule(struct pf_test_ctx *ctx, struct pf_ruleset *ruleset)
{
	struct pf_rule *r;
	struct pf_anchor *child = NULL;
	int target;

	pf_anchor_stack_init();
enter_ruleset:
	r = TAILQ_FIRST(ruleset->rules.active.ptr);
	while (r != NULL) {
		PF_TEST_ATTRIB(r->rule_flag & PFRULE_EXPIRED,
		    TAILQ_NEXT(r, entries));
		r->evaluations++;
		PF_TEST_ATTRIB(
		    (pfi_kif_match(r->kif, ctx->pd->kif) == r->ifnot),
			r->skip[PF_SKIP_IFP].ptr);
		PF_TEST_ATTRIB((r->direction && r->direction != ctx->pd->dir),
			r->skip[PF_SKIP_DIR].ptr);
		PF_TEST_ATTRIB((r->onrdomain >= 0  &&
		    (r->onrdomain == ctx->pd->rdomain) == r->ifnot),
			r->skip[PF_SKIP_RDOM].ptr);
		PF_TEST_ATTRIB((r->af && r->af != ctx->pd->af),
			r->skip[PF_SKIP_AF].ptr);
		PF_TEST_ATTRIB((r->proto && r->proto != ctx->pd->proto),
			r->skip[PF_SKIP_PROTO].ptr);
		PF_TEST_ATTRIB((PF_MISMATCHAW(&r->src.addr, &ctx->pd->nsaddr,
		    ctx->pd->naf, r->src.neg, ctx->pd->kif,
		    ctx->act.rtableid)),
			r->skip[PF_SKIP_SRC_ADDR].ptr);
		PF_TEST_ATTRIB((PF_MISMATCHAW(&r->dst.addr, &ctx->pd->ndaddr,
		    ctx->pd->af, r->dst.neg, NULL, ctx->act.rtableid)),
			r->skip[PF_SKIP_DST_ADDR].ptr);

		switch (ctx->pd->virtual_proto) {
		case PF_VPROTO_FRAGMENT:
			/* tcp/udp only. port_op always 0 in other cases */
			PF_TEST_ATTRIB((r->src.port_op || r->dst.port_op),
				TAILQ_NEXT(r, entries));
			PF_TEST_ATTRIB((ctx->pd->proto == IPPROTO_TCP &&
			    r->flagset),
				TAILQ_NEXT(r, entries));
			/* icmp only. type/code always 0 in other cases */
			PF_TEST_ATTRIB((r->type || r->code),
				TAILQ_NEXT(r, entries));
			/* tcp/udp only. {uid|gid}.op always 0 in other cases */
			PF_TEST_ATTRIB((r->gid.op || r->uid.op),
				TAILQ_NEXT(r, entries));
			break;

		case IPPROTO_TCP:
			PF_TEST_ATTRIB(((r->flagset & ctx->th->th_flags) !=
			    r->flags),
				TAILQ_NEXT(r, entries));
			PF_TEST_ATTRIB((r->os_fingerprint != PF_OSFP_ANY &&
			    !pf_osfp_match(pf_osfp_fingerprint(ctx->pd),
			    r->os_fingerprint)),
				TAILQ_NEXT(r, entries));
			/* FALLTHROUGH */

		case IPPROTO_UDP:
			/* tcp/udp only. port_op always 0 in other cases */
			PF_TEST_ATTRIB((r->src.port_op &&
			    !pf_match_port(r->src.port_op, r->src.port[0],
			    r->src.port[1], ctx->pd->nsport)),
				r->skip[PF_SKIP_SRC_PORT].ptr);
			PF_TEST_ATTRIB((r->dst.port_op &&
			    !pf_match_port(r->dst.port_op, r->dst.port[0],
			    r->dst.port[1], ctx->pd->ndport)),
				r->skip[PF_SKIP_DST_PORT].ptr);
			/* tcp/udp only. uid.op always 0 in other cases */
			PF_TEST_ATTRIB((r->uid.op && (ctx->pd->lookup.done ||
			    (ctx->pd->lookup.done =
			    pf_socket_lookup(ctx->pd), 1)) &&
			    !pf_match_uid(r->uid.op, r->uid.uid[0],
			    r->uid.uid[1], ctx->pd->lookup.uid)),
				TAILQ_NEXT(r, entries));
			/* tcp/udp only. gid.op always 0 in other cases */
			PF_TEST_ATTRIB((r->gid.op && (ctx->pd->lookup.done ||
			    (ctx->pd->lookup.done =
			    pf_socket_lookup(ctx->pd), 1)) &&
			    !pf_match_gid(r->gid.op, r->gid.gid[0],
			    r->gid.gid[1], ctx->pd->lookup.gid)),
				TAILQ_NEXT(r, entries));
			break;

		case IPPROTO_ICMP:
			/* icmp only. type always 0 in other cases */
			PF_TEST_ATTRIB((r->type &&
			    r->type != ctx->icmptype + 1),
				TAILQ_NEXT(r, entries));
			/* icmp only. type always 0 in other cases */
			PF_TEST_ATTRIB((r->code &&
			    r->code != ctx->icmpcode + 1),
				TAILQ_NEXT(r, entries));
			/* icmp only. don't create states on replies */
			PF_TEST_ATTRIB((r->keep_state && !ctx->state_icmp &&
			    (r->rule_flag & PFRULE_STATESLOPPY) == 0 &&
			    ctx->icmp_dir != PF_IN),
				TAILQ_NEXT(r, entries));
			break;

		case IPPROTO_ICMPV6:
			/* icmp only. type always 0 in other cases */
			PF_TEST_ATTRIB((r->type &&
			    r->type != ctx->icmptype + 1),
				TAILQ_NEXT(r, entries));
			/* icmp only. type always 0 in other cases */
			PF_TEST_ATTRIB((r->code &&
			    r->code != ctx->icmpcode + 1),
				TAILQ_NEXT(r, entries));
			/* icmp only. don't create states on replies */
			PF_TEST_ATTRIB((r->keep_state && !ctx->state_icmp &&
			    (r->rule_flag & PFRULE_STATESLOPPY) == 0 &&
			    ctx->icmp_dir != PF_IN &&
			    ctx->icmptype != ND_NEIGHBOR_ADVERT),
				TAILQ_NEXT(r, entries));
			break;

		default:
			break;
		}

		PF_TEST_ATTRIB((r->rule_flag & PFRULE_FRAGMENT &&
		    ctx->pd->virtual_proto != PF_VPROTO_FRAGMENT),
			TAILQ_NEXT(r, entries));
		PF_TEST_ATTRIB((r->tos && !(r->tos == ctx->pd->tos)),
			TAILQ_NEXT(r, entries));
		PF_TEST_ATTRIB((r->prob &&
		    r->prob <= arc4random_uniform(UINT_MAX - 1) + 1),
			TAILQ_NEXT(r, entries));
		PF_TEST_ATTRIB((r->match_tag &&
		    !pf_match_tag(ctx->pd->m, r, &ctx->tag)),
			TAILQ_NEXT(r, entries));
		PF_TEST_ATTRIB((r->rcv_kif && pf_match_rcvif(ctx->pd->m, r) ==
		    r->rcvifnot),
			TAILQ_NEXT(r, entries));
		PF_TEST_ATTRIB((r->prio &&
		    (r->prio == PF_PRIO_ZERO ? 0 : r->prio) !=
		    ctx->pd->m->m_pkthdr.pf.prio),
			TAILQ_NEXT(r, entries));

		/* must be last! */
		if (r->pktrate.limit) {
			pf_add_threshold(&r->pktrate);
			PF_TEST_ATTRIB((pf_check_threshold(&r->pktrate)),
				TAILQ_NEXT(r, entries));
		}

		/* FALLTHROUGH */
		if (r->tag)
			ctx->tag = r->tag;
		if (r->anchor == NULL) {

			if (r->rule_flag & PFRULE_ONCE) {
				u_int32_t	rule_flag;

				rule_flag = r->rule_flag;
				if (((rule_flag & PFRULE_EXPIRED) == 0) &&
				    atomic_cas_uint(&r->rule_flag, rule_flag,
				    rule_flag | PFRULE_EXPIRED) == rule_flag) {
					r->exptime = gettime();
				} else {
					r = TAILQ_NEXT(r, entries);
					continue;
				}
			}

			if (r->action == PF_MATCH) {
				if ((ctx->ri = pool_get(&pf_rule_item_pl,
				    PR_NOWAIT)) == NULL) {
					REASON_SET(&ctx->reason, PFRES_MEMORY);
					return (PF_TEST_FAIL);
				}
				ctx->ri->r = r;
				/* order is irrelevant */
				SLIST_INSERT_HEAD(&ctx->rules, ctx->ri, entry);
				ctx->ri = NULL;
				pf_rule_to_actions(r, &ctx->act);
				if (r->rule_flag & PFRULE_AFTO)
					ctx->pd->naf = r->naf;
				if (pf_get_transaddr(r, ctx->pd, ctx->sns,
				    &ctx->nr) == -1) {
					REASON_SET(&ctx->reason,
					    PFRES_TRANSLATE);
					return (PF_TEST_FAIL);
				}
#if NPFLOG > 0
				if (r->log) {
					REASON_SET(&ctx->reason, PFRES_MATCH);
					pflog_packet(ctx->pd, ctx->reason, r,
					    ctx->a, ruleset, NULL);
				}
#endif	/* NPFLOG > 0 */
			} else {
				/*
				 * found matching r
				 */
				*ctx->rm = r;
				/*
				 * anchor, with ruleset, where r belongs to
				 */
				*ctx->am = ctx->a;
				/*
				 * ruleset where r belongs to
				 */
				*ctx->rsm = ruleset;
				/*
				 * ruleset, where anchor belongs to.
				 */
				ctx->arsm = ctx->aruleset;
			}

#if NPFLOG > 0
			if (ctx->act.log & PF_LOG_MATCHES)
				pf_log_matches(ctx->pd, r, ctx->a, ruleset,
				    &ctx->rules);
#endif	/* NPFLOG > 0 */

			if (r->quick)
				return (PF_TEST_QUICK);
		} else {
			ctx->aruleset = &r->anchor->ruleset;
			if (r->anchor_wildcard) {
				RB_FOREACH(child, pf_anchor_node,
				    &r->anchor->children) {
					if (pf_anchor_stack_push(ruleset,
					    ctx->a, r, child,
					    PF_NEXT_CHILD) != 0)
						return (PF_TEST_FAIL);

					ctx->a = r;
					ruleset = &child->ruleset;
					goto enter_ruleset;
next_child:
					continue;	/* with RB_FOREACH() */
				}
			} else {
				if (pf_anchor_stack_push(ruleset, ctx->a,
				    r, child, PF_NEXT_RULE) != 0)
					return (PF_TEST_FAIL);

				ctx->a = r;
				ruleset = &r->anchor->ruleset;
				child = NULL;
				goto enter_ruleset;
next_rule:
				;
			}
		}
		r = TAILQ_NEXT(r, entries);
	}

	if (pf_anchor_stack_pop(&ruleset, &ctx->a, &r, &child,
	    &target) == 0) {

		/* stop if any rule matched within quick anchors. */
		if (r->quick == PF_TEST_QUICK && *ctx->am == r)
			return (PF_TEST_QUICK);

		switch (target) {
		case PF_NEXT_CHILD:
			goto next_child;
		case PF_NEXT_RULE:
			goto next_rule;
		default:
			panic("%s: unknown jump target", __func__);
		}
	}

	return (PF_TEST_OK);
}

int
pf_test_rule(struct pf_pdesc *pd, struct pf_rule **rm, struct pf_state **sm,
    struct pf_rule **am, struct pf_ruleset **rsm, u_short *reason)
{
	struct pf_rule		*r = NULL;
	struct pf_rule		*a = NULL;
	struct pf_ruleset	*ruleset = NULL;
	struct pf_state_key	*skw = NULL, *sks = NULL;
	int			 rewrite = 0;
	u_int16_t		 virtual_type, virtual_id;
	int			 action = PF_DROP;
	struct pf_test_ctx	 ctx;
	int			 rv;

	PF_ASSERT_LOCKED();

	memset(&ctx, 0, sizeof(ctx));
	ctx.pd = pd;
	ctx.rm = rm;
	ctx.am = am;
	ctx.rsm = rsm;
	ctx.th = &pd->hdr.tcp;
	ctx.act.rtableid = pd->rdomain;
	ctx.tag = -1;
	SLIST_INIT(&ctx.rules);

	if (pd->dir == PF_IN && if_congested()) {
		REASON_SET(&ctx.reason, PFRES_CONGEST);
		return (PF_DROP);
	}

	switch (pd->virtual_proto) {
	case IPPROTO_ICMP:
		ctx.icmptype = pd->hdr.icmp.icmp_type;
		ctx.icmpcode = pd->hdr.icmp.icmp_code;
		ctx.state_icmp = pf_icmp_mapping(pd, ctx.icmptype,
		    &ctx.icmp_dir, &virtual_id, &virtual_type);
		if (ctx.icmp_dir == PF_IN) {
			pd->osport = pd->nsport = virtual_id;
			pd->odport = pd->ndport = virtual_type;
		} else {
			pd->osport = pd->nsport = virtual_type;
			pd->odport = pd->ndport = virtual_id;
		}
		break;
#ifdef INET6
	case IPPROTO_ICMPV6:
		ctx.icmptype = pd->hdr.icmp6.icmp6_type;
		ctx.icmpcode = pd->hdr.icmp6.icmp6_code;
		ctx.state_icmp = pf_icmp_mapping(pd, ctx.icmptype,
		    &ctx.icmp_dir, &virtual_id, &virtual_type);
		if (ctx.icmp_dir == PF_IN) {
			pd->osport = pd->nsport = virtual_id;
			pd->odport = pd->ndport = virtual_type;
		} else {
			pd->osport = pd->nsport = virtual_type;
			pd->odport = pd->ndport = virtual_id;
		}
		break;
#endif /* INET6 */
	}

	ruleset = &pf_main_ruleset;
	rv = pf_match_rule(&ctx, ruleset);
	if (rv == PF_TEST_FAIL) {
		/*
		 * Reason has been set in pf_match_rule() already.
		 */
		goto cleanup;
	}

	r = *ctx.rm;	/* matching rule */
	a = *ctx.am;	/* rule that defines an anchor containing 'r' */
	ruleset = *ctx.rsm;/* ruleset of the anchor defined by the rule 'a' */
	ctx.aruleset = ctx.arsm;/* ruleset of the 'a' rule itself */

	/* apply actions for last matching pass/block rule */
	pf_rule_to_actions(r, &ctx.act);
	if (r->rule_flag & PFRULE_AFTO)
		pd->naf = r->naf;
	if (pf_get_transaddr(r, pd, ctx.sns, &ctx.nr) == -1) {
		REASON_SET(&ctx.reason, PFRES_TRANSLATE);
		goto cleanup;
	}
	REASON_SET(&ctx.reason, PFRES_MATCH);

#if NPFLOG > 0
	if (r->log)
		pflog_packet(pd, ctx.reason, r, a, ruleset, NULL);
	if (ctx.act.log & PF_LOG_MATCHES)
		pf_log_matches(pd, r, a, ruleset, &ctx.rules);
#endif	/* NPFLOG > 0 */

	if (pd->virtual_proto != PF_VPROTO_FRAGMENT &&
	    (r->action == PF_DROP) &&
	    ((r->rule_flag & PFRULE_RETURNRST) ||
	    (r->rule_flag & PFRULE_RETURNICMP) ||
	    (r->rule_flag & PFRULE_RETURN))) {
		if (pd->proto == IPPROTO_TCP &&
		    ((r->rule_flag & PFRULE_RETURNRST) ||
		    (r->rule_flag & PFRULE_RETURN)) &&
		    !(ctx.th->th_flags & TH_RST)) {
			u_int32_t	 ack =
			    ntohl(ctx.th->th_seq) + pd->p_len;

			if (pf_check_tcp_cksum(pd->m, pd->off,
			    pd->tot_len - pd->off, pd->af))
				REASON_SET(&ctx.reason, PFRES_PROTCKSUM);
			else {
				if (ctx.th->th_flags & TH_SYN)
					ack++;
				if (ctx.th->th_flags & TH_FIN)
					ack++;
				pf_send_tcp(r, pd->af, pd->dst,
				    pd->src, ctx.th->th_dport,
				    ctx.th->th_sport, ntohl(ctx.th->th_ack),
				    ack, TH_RST|TH_ACK, 0, 0, r->return_ttl,
				    1, 0, pd->rdomain, &ctx.reason);
			}
		} else if ((pd->proto != IPPROTO_ICMP ||
		    ICMP_INFOTYPE(ctx.icmptype)) && pd->af == AF_INET &&
		    r->return_icmp)
			pf_send_icmp(pd->m, r->return_icmp >> 8,
			    r->return_icmp & 255, 0, pd->af, r, pd->rdomain);
		else if ((pd->proto != IPPROTO_ICMPV6 ||
		    (ctx.icmptype >= ICMP6_ECHO_REQUEST &&
		    ctx.icmptype != ND_REDIRECT)) && pd->af == AF_INET6 &&
		    r->return_icmp6)
			pf_send_icmp(pd->m, r->return_icmp6 >> 8,
			    r->return_icmp6 & 255, 0, pd->af, r, pd->rdomain);
	}

	if (r->action == PF_DROP)
		goto cleanup;

	pf_tag_packet(pd->m, ctx.tag, ctx.act.rtableid);
	if (ctx.act.rtableid >= 0 &&
	    rtable_l2(ctx.act.rtableid) != pd->rdomain)
		pd->destchg = 1;

	if (r->action == PF_PASS && pd->badopts != 0 && ! r->allow_opts) {
		REASON_SET(&ctx.reason, PFRES_IPOPTIONS);
#if NPFLOG > 0
		pd->pflog |= PF_LOG_FORCE;
#endif	/* NPFLOG > 0 */
		DPFPRINTF(LOG_NOTICE, "dropping packet with "
		    "ip/ipv6 options in pf_test_rule()");
		goto cleanup;
	}

	if (pd->virtual_proto != PF_VPROTO_FRAGMENT
	    && !ctx.state_icmp && r->keep_state) {

		if (r->rule_flag & PFRULE_SRCTRACK &&
		    pf_insert_src_node(&ctx.sns[PF_SN_NONE], r, PF_SN_NONE,
		    pd->af, pd->src, NULL, NULL) != 0) {
			REASON_SET(&ctx.reason, PFRES_SRCLIMIT);
			goto cleanup;
		}

		if (r->max_states && (r->states_cur >= r->max_states)) {
			pf_status.lcounters[LCNT_STATES]++;
			REASON_SET(&ctx.reason, PFRES_MAXSTATES);
			goto cleanup;
		}

		action = pf_create_state(pd, r, a, ctx.nr, &skw, &sks,
		    &rewrite, sm, ctx.tag, &ctx.rules, &ctx.act, ctx.sns);

		if (action != PF_PASS)
			goto cleanup;
		if (sks != skw) {
			struct pf_state_key	*sk;

			if (pd->dir == PF_IN)
				sk = sks;
			else
				sk = skw;
			rewrite += pf_translate(pd,
			    &sk->addr[pd->af == pd->naf ? pd->sidx : pd->didx],
			    sk->port[pd->af == pd->naf ? pd->sidx : pd->didx],
			    &sk->addr[pd->af == pd->naf ? pd->didx : pd->sidx],
			    sk->port[pd->af == pd->naf ? pd->didx : pd->sidx],
			    virtual_type, ctx.icmp_dir);
		}

#ifdef INET6
		if (rewrite && skw->af != sks->af)
			action = PF_AFRT;
#endif /* INET6 */

	} else {
		action = PF_PASS;

		while ((ctx.ri = SLIST_FIRST(&ctx.rules))) {
			SLIST_REMOVE_HEAD(&ctx.rules, entry);
			pool_put(&pf_rule_item_pl, ctx.ri);
		}
	}

	/* copy back packet headers if needed */
	if (rewrite && pd->hdrlen) {
		m_copyback(pd->m, pd->off, pd->hdrlen, &pd->hdr, M_NOWAIT);
	}

#if NPFSYNC > 0
	if (*sm != NULL && !ISSET((*sm)->state_flags, PFSTATE_NOSYNC) &&
	    pd->dir == PF_OUT && pfsync_is_up()) {
		/*
		 * We want the state created, but we dont
		 * want to send this in case a partner
		 * firewall has to know about it to allow
		 * replies through it.
		 */
		if (pfsync_defer(*sm, pd->m))
			return (PF_DEFER);
	}
#endif	/* NPFSYNC > 0 */

	return (action);

cleanup:
	while ((ctx.ri = SLIST_FIRST(&ctx.rules))) {
		SLIST_REMOVE_HEAD(&ctx.rules, entry);
		pool_put(&pf_rule_item_pl, ctx.ri);
	}

	return (action);
}

static __inline int
pf_create_state(struct pf_pdesc *pd, struct pf_rule *r, struct pf_rule *a,
    struct pf_rule *nr, struct pf_state_key **skw, struct pf_state_key **sks,
    int *rewrite, struct pf_state **sm, int tag, struct pf_rule_slist *rules,
    struct pf_rule_actions *act, struct pf_src_node *sns[PF_SN_MAX])
{
	struct pf_state		*st = NULL;
	struct tcphdr		*th = &pd->hdr.tcp;
	u_short			 reason;
	u_int			 i;

	st = pool_get(&pf_state_pl, PR_NOWAIT | PR_ZERO);
	if (st == NULL) {
		REASON_SET(&reason, PFRES_MEMORY);
		goto csfailed;
	}
	st->rule.ptr = r;
	st->anchor.ptr = a;
	st->natrule.ptr = nr;
	if (r->allow_opts)
		st->state_flags |= PFSTATE_ALLOWOPTS;
	if (r->rule_flag & PFRULE_STATESLOPPY)
		st->state_flags |= PFSTATE_SLOPPY;
	if (r->rule_flag & PFRULE_PFLOW)
		st->state_flags |= PFSTATE_PFLOW;
	if (r->rule_flag & PFRULE_NOSYNC)
		st->state_flags |= PFSTATE_NOSYNC;
#if NPFLOG > 0
	st->log = act->log & PF_LOG_ALL;
#endif	/* NPFLOG > 0 */
	st->qid = act->qid;
	st->pqid = act->pqid;
	st->rtableid[pd->didx] = act->rtableid;
	st->rtableid[pd->sidx] = -1;	/* return traffic is routed normally */
	st->min_ttl = act->min_ttl;
	st->set_tos = act->set_tos;
	st->max_mss = act->max_mss;
	st->state_flags |= act->flags;
#if NPFSYNC > 0
	st->sync_state = PFSYNC_S_NONE;
#endif	/* NPFSYNC > 0 */
	st->set_prio[0] = act->set_prio[0];
	st->set_prio[1] = act->set_prio[1];
	st->delay = act->delay;
	SLIST_INIT(&st->src_nodes);

	/*
	 * must initialize refcnt, before pf_state_insert() gets called.
	 * pf_state_inserts() grabs reference for pfsync!
	 */
	PF_REF_INIT(st->refcnt);
	mtx_init(&st->mtx, IPL_NET);

	switch (pd->proto) {
	case IPPROTO_TCP:
		st->src.seqlo = ntohl(th->th_seq);
		st->src.seqhi = st->src.seqlo + pd->p_len + 1;
		if ((th->th_flags & (TH_SYN|TH_ACK)) == TH_SYN &&
		    r->keep_state == PF_STATE_MODULATE) {
			/* Generate sequence number modulator */
			st->src.seqdiff = pf_tcp_iss(pd) - st->src.seqlo;
			if (st->src.seqdiff == 0)
				st->src.seqdiff = 1;
			pf_patch_32(pd, &th->th_seq,
			    htonl(st->src.seqlo + st->src.seqdiff));
			*rewrite = 1;
		} else
			st->src.seqdiff = 0;
		if (th->th_flags & TH_SYN) {
			st->src.seqhi++;
			st->src.wscale = pf_get_wscale(pd);
		}
		st->src.max_win = MAX(ntohs(th->th_win), 1);
		if (st->src.wscale & PF_WSCALE_MASK) {
			/* Remove scale factor from initial window */
			int win = st->src.max_win;
			win += 1 << (st->src.wscale & PF_WSCALE_MASK);
			st->src.max_win = (win - 1) >>
			    (st->src.wscale & PF_WSCALE_MASK);
		}
		if (th->th_flags & TH_FIN)
			st->src.seqhi++;
		st->dst.seqhi = 1;
		st->dst.max_win = 1;
		pf_set_protostate(st, PF_PEER_SRC, TCPS_SYN_SENT);
		pf_set_protostate(st, PF_PEER_DST, TCPS_CLOSED);
		st->timeout = PFTM_TCP_FIRST_PACKET;
		atomic_inc_int(&pf_status.states_halfopen);
		break;
	case IPPROTO_UDP:
		pf_set_protostate(st, PF_PEER_SRC, PFUDPS_SINGLE);
		pf_set_protostate(st, PF_PEER_DST, PFUDPS_NO_TRAFFIC);
		st->timeout = PFTM_UDP_FIRST_PACKET;
		break;
	case IPPROTO_ICMP:
#ifdef INET6
	case IPPROTO_ICMPV6:
#endif	/* INET6 */
		st->timeout = PFTM_ICMP_FIRST_PACKET;
		break;
	default:
		pf_set_protostate(st, PF_PEER_SRC, PFOTHERS_SINGLE);
		pf_set_protostate(st, PF_PEER_DST, PFOTHERS_NO_TRAFFIC);
		st->timeout = PFTM_OTHER_FIRST_PACKET;
	}

	st->creation = getuptime();
	st->expire = getuptime();

	if (pd->proto == IPPROTO_TCP) {
		if (st->state_flags & PFSTATE_SCRUB_TCP &&
		    pf_normalize_tcp_init(pd, &st->src)) {
			REASON_SET(&reason, PFRES_MEMORY);
			goto csfailed;
		}
		if (st->state_flags & PFSTATE_SCRUB_TCP && st->src.scrub &&
		    pf_normalize_tcp_stateful(pd, &reason, st,
		    &st->src, &st->dst, rewrite)) {
			/* This really shouldn't happen!!! */
			DPFPRINTF(LOG_ERR,
			    "%s: tcp normalize failed on first pkt", __func__);
			goto csfailed;
		}
	}
	st->direction = pd->dir;

	if (pf_state_key_setup(pd, skw, sks, act->rtableid)) {
		REASON_SET(&reason, PFRES_MEMORY);
		goto csfailed;
	}

	if (pf_set_rt_ifp(st, pd->src, (*skw)->af, sns) != 0) {
		REASON_SET(&reason, PFRES_NOROUTE);
		goto csfailed;
	}

	for (i = 0; i < PF_SN_MAX; i++)
		if (sns[i] != NULL) {
			struct pf_sn_item	*sni;

			sni = pool_get(&pf_sn_item_pl, PR_NOWAIT);
			if (sni == NULL) {
				REASON_SET(&reason, PFRES_MEMORY);
				goto csfailed;
			}
			sni->sn = sns[i];
			SLIST_INSERT_HEAD(&st->src_nodes, sni, next);
			sni->sn->states++;
		}

#if NPFSYNC > 0
	pfsync_init_state(st, *skw, *sks, 0);
#endif

	if (pf_state_insert(BOUND_IFACE(r, pd->kif), skw, sks, st)) {
		*sks = *skw = NULL;
		REASON_SET(&reason, PFRES_STATEINS);
		goto csfailed;
	} else
		*sm = st;

	/*
	 * Make state responsible for rules it binds here.
	 */
	memcpy(&st->match_rules, rules, sizeof(st->match_rules));
	memset(rules, 0, sizeof(*rules));
	STATE_INC_COUNTERS(st);

	if (tag > 0) {
		pf_tag_ref(tag);
		st->tag = tag;
	}
	if (pd->proto == IPPROTO_TCP && (th->th_flags & (TH_SYN|TH_ACK)) ==
	    TH_SYN && r->keep_state == PF_STATE_SYNPROXY && pd->dir == PF_IN) {
		int		rtid;
		uint16_t	mss, mssdflt;

		rtid = (act->rtableid >= 0) ? act->rtableid : pd->rdomain;
		pf_set_protostate(st, PF_PEER_SRC, PF_TCPS_PROXY_SRC);
		st->src.seqhi = arc4random();
		/* Find mss option */
		mssdflt = atomic_load_int(&tcp_mssdflt);
		mss = pf_get_mss(pd, mssdflt);
		mss = pf_calc_mss(pd->src, pd->af, rtid, mss, mssdflt);
		mss = pf_calc_mss(pd->dst, pd->af, rtid, mss, mssdflt);
		st->src.mss = mss;
		pf_send_tcp(r, pd->af, pd->dst, pd->src, th->th_dport,
		    th->th_sport, st->src.seqhi, ntohl(th->th_seq) + 1,
		    TH_SYN|TH_ACK, 0, st->src.mss, 0, 1, 0, pd->rdomain,
		    &reason);
		REASON_SET(&reason, PFRES_SYNPROXY);
		return (PF_SYNPROXY_DROP);
	}

	return (PF_PASS);

csfailed:
	if (st) {
		pf_normalize_tcp_cleanup(st);	/* safe even w/o init */
		pf_src_tree_remove_state(st);
		pool_put(&pf_state_pl, st);
	}

	for (i = 0; i < PF_SN_MAX; i++)
		if (sns[i] != NULL)
			pf_remove_src_node(sns[i]);

	return (PF_DROP);
}

int
pf_translate(struct pf_pdesc *pd, struct pf_addr *saddr, u_int16_t sport,
    struct pf_addr *daddr, u_int16_t dport, u_int16_t virtual_type,
    int icmp_dir)
{
	int	rewrite = 0;
	int	afto = pd->af != pd->naf;

	if (afto || PF_ANEQ(daddr, pd->dst, pd->af))
		pd->destchg = 1;

	switch (pd->proto) {
	case IPPROTO_TCP:	/* FALLTHROUGH */
	case IPPROTO_UDP:
		rewrite += pf_patch_16(pd, pd->sport, sport);
		rewrite += pf_patch_16(pd, pd->dport, dport);
		break;

	case IPPROTO_ICMP:
		if (pd->af != AF_INET)
			return (0);

#ifdef INET6
		if (afto) {
			if (pf_translate_icmp_af(pd, AF_INET6, &pd->hdr.icmp))
				return (0);
			pd->proto = IPPROTO_ICMPV6;
			rewrite = 1;
		}
#endif /* INET6 */
		if (virtual_type == htons(ICMP_ECHO)) {
			u_int16_t icmpid = (icmp_dir == PF_IN) ? sport : dport;
			rewrite += pf_patch_16(pd,
			    &pd->hdr.icmp.icmp_id, icmpid);
		}
		break;

#ifdef INET6
	case IPPROTO_ICMPV6:
		if (pd->af != AF_INET6)
			return (0);

		if (afto) {
			if (pf_translate_icmp_af(pd, AF_INET, &pd->hdr.icmp6))
				return (0);
			pd->proto = IPPROTO_ICMP;
			rewrite = 1;
		}
		if (virtual_type == htons(ICMP6_ECHO_REQUEST)) {
			u_int16_t icmpid = (icmp_dir == PF_IN) ? sport : dport;
			rewrite += pf_patch_16(pd,
			    &pd->hdr.icmp6.icmp6_id, icmpid);
		}
		break;
#endif /* INET6 */
	}

	if (!afto) {
		rewrite += pf_translate_a(pd, pd->src, saddr);
		rewrite += pf_translate_a(pd, pd->dst, daddr);
	}

	return (rewrite);
}

int
pf_tcp_track_full(struct pf_pdesc *pd, struct pf_state **stp, u_short *reason,
    int *copyback, int reverse)
{
	struct tcphdr		*th = &pd->hdr.tcp;
	struct pf_state_peer	*src, *dst;
	u_int16_t		 win = ntohs(th->th_win);
	u_int32_t		 ack, end, data_end, seq, orig_seq;
	u_int8_t		 sws, dws, psrc, pdst;
	int			 ackskew;

	if ((pd->dir == (*stp)->direction && !reverse) ||
	    (pd->dir != (*stp)->direction && reverse)) {
		src = &(*stp)->src;
		dst = &(*stp)->dst;
		psrc = PF_PEER_SRC;
		pdst = PF_PEER_DST;
	} else {
		src = &(*stp)->dst;
		dst = &(*stp)->src;
		psrc = PF_PEER_DST;
		pdst = PF_PEER_SRC;
	}

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

		if (((*stp)->state_flags & PFSTATE_SCRUB_TCP || dst->scrub) &&
		    src->scrub == NULL) {
			if (pf_normalize_tcp_init(pd, src)) {
				REASON_SET(reason, PFRES_MEMORY);
				return (PF_DROP);
			}
		}

		/* Deferred generation of sequence number modulator */
		if (dst->seqdiff && !src->seqdiff) {
			/* use random iss for the TCP server */
			while ((src->seqdiff = arc4random() - seq) == 0)
				continue;
			ack = ntohl(th->th_ack) - dst->seqdiff;
			pf_patch_32(pd, &th->th_seq, htonl(seq + src->seqdiff));
			pf_patch_32(pd, &th->th_ack, htonl(ack));
			*copyback = 1;
		} else {
			ack = ntohl(th->th_ack);
		}

		end = seq + pd->p_len;
		if (th->th_flags & TH_SYN) {
			end++;
			if (dst->wscale & PF_WSCALE_FLAG) {
				src->wscale = pf_get_wscale(pd);
				if (src->wscale & PF_WSCALE_FLAG) {
					/* Remove scale factor from initial
					 * window */
					sws = src->wscale & PF_WSCALE_MASK;
					win = ((u_int32_t)win + (1 << sws) - 1)
					    >> sws;
					dws = dst->wscale & PF_WSCALE_MASK;
				} else {
					/* fixup other window */
					dst->max_win = MIN(TCP_MAXWIN,
					    (u_int32_t)dst->max_win <<
					    (dst->wscale & PF_WSCALE_MASK));
					/* in case of a retrans SYN|ACK */
					dst->wscale = 0;
				}
			}
		}
		data_end = end;
		if (th->th_flags & TH_FIN)
			end++;

		src->seqlo = seq;
		if (src->state < TCPS_SYN_SENT)
			pf_set_protostate(*stp, psrc, TCPS_SYN_SENT);

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
			pf_patch_32(pd, &th->th_seq, htonl(seq + src->seqdiff));
			pf_patch_32(pd, &th->th_ack, htonl(ack));
			*copyback = 1;
		}
		end = seq + pd->p_len;
		if (th->th_flags & TH_SYN)
			end++;
		data_end = end;
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
		data_end = end = seq;
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
		if (pf_modulate_sack(pd, dst))
			*copyback = 1;
	}


#define MAXACKWINDOW (0xffff + 1500)	/* 1500 is an arbitrary fudge factor */
	if (SEQ_GEQ(src->seqhi, data_end) &&
	    /* Last octet inside other's window space */
	    SEQ_GEQ(seq, src->seqlo - (dst->max_win << dws)) &&
	    /* Retrans: not more than one window back */
	    (ackskew >= -MAXACKWINDOW) &&
	    /* Acking not more than one reassembled fragment backwards */
	    (ackskew <= (MAXACKWINDOW << sws)) &&
	    /* Acking not more than one window forward */
	    ((th->th_flags & TH_RST) == 0 || orig_seq == src->seqlo ||
	    (orig_seq == src->seqlo + 1) || (orig_seq + 1 == src->seqlo) ||
	    /* Require an exact/+1 sequence match on resets when possible */
	    (SEQ_GEQ(orig_seq, src->seqlo - (dst->max_win << dws)) &&
	    SEQ_LEQ(orig_seq, src->seqlo + 1) && ackskew == 0 &&
	    (th->th_flags & (TH_ACK|TH_RST)) == (TH_ACK|TH_RST)))) {
	    /* Allow resets to match sequence window if ack is perfect match */

		if (dst->scrub || src->scrub) {
			if (pf_normalize_tcp_stateful(pd, reason, *stp, src,
			    dst, copyback))
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
				pf_set_protostate(*stp, psrc, TCPS_SYN_SENT);
		if (th->th_flags & TH_FIN)
			if (src->state < TCPS_CLOSING)
				pf_set_protostate(*stp, psrc, TCPS_CLOSING);
		if (th->th_flags & TH_ACK) {
			if (dst->state == TCPS_SYN_SENT) {
				pf_set_protostate(*stp, pdst,
				    TCPS_ESTABLISHED);
				if (src->state == TCPS_ESTABLISHED &&
				    !SLIST_EMPTY(&(*stp)->src_nodes) &&
				    pf_src_connlimit(stp)) {
					REASON_SET(reason, PFRES_SRCLIMIT);
					return (PF_DROP);
				}
			} else if (dst->state == TCPS_CLOSING)
				pf_set_protostate(*stp, pdst,
				    TCPS_FIN_WAIT_2);
		}
		if (th->th_flags & TH_RST)
			pf_set_protostate(*stp, PF_PEER_BOTH, TCPS_TIME_WAIT);

		/* update expire time */
		(*stp)->expire = getuptime();
		if (src->state >= TCPS_FIN_WAIT_2 &&
		    dst->state >= TCPS_FIN_WAIT_2)
			pf_update_state_timeout(*stp, PFTM_TCP_CLOSED);
		else if (src->state >= TCPS_CLOSING &&
		    dst->state >= TCPS_CLOSING)
			pf_update_state_timeout(*stp, PFTM_TCP_FIN_WAIT);
		else if (src->state < TCPS_ESTABLISHED ||
		    dst->state < TCPS_ESTABLISHED)
			pf_update_state_timeout(*stp, PFTM_TCP_OPENING);
		else if (src->state >= TCPS_CLOSING ||
		    dst->state >= TCPS_CLOSING)
			pf_update_state_timeout(*stp, PFTM_TCP_CLOSING);
		else
			pf_update_state_timeout(*stp, PFTM_TCP_ESTABLISHED);

		/* Fall through to PASS packet */
	} else if ((dst->state < TCPS_SYN_SENT ||
		dst->state >= TCPS_FIN_WAIT_2 ||
		src->state >= TCPS_FIN_WAIT_2) &&
	    SEQ_GEQ(src->seqhi + MAXACKWINDOW, data_end) &&
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

		if (pf_status.debug >= LOG_NOTICE) {
			log(LOG_NOTICE, "pf: loose state match: ");
			pf_print_state(*stp);
			pf_print_flags(th->th_flags);
			addlog(" seq=%u (%u) ack=%u len=%u ackskew=%d "
			    "pkts=%llu:%llu dir=%s,%s\n", seq, orig_seq, ack,
			    pd->p_len, ackskew, (*stp)->packets[0],
			    (*stp)->packets[1],
			    pd->dir == PF_IN ? "in" : "out",
			    pd->dir == (*stp)->direction ? "fwd" : "rev");
		}

		if (dst->scrub || src->scrub) {
			if (pf_normalize_tcp_stateful(pd, reason, *stp, src,
			    dst, copyback))
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
				pf_set_protostate(*stp, psrc, TCPS_CLOSING);
		if (th->th_flags & TH_RST)
			pf_set_protostate(*stp, PF_PEER_BOTH, TCPS_TIME_WAIT);

		/* Fall through to PASS packet */
	} else {
		if ((*stp)->dst.state == TCPS_SYN_SENT &&
		    (*stp)->src.state == TCPS_SYN_SENT) {
			/* Send RST for state mismatches during handshake */
			if (!(th->th_flags & TH_RST))
				pf_send_tcp((*stp)->rule.ptr, pd->af,
				    pd->dst, pd->src, th->th_dport,
				    th->th_sport, ntohl(th->th_ack), 0,
				    TH_RST, 0, 0,
				    (*stp)->rule.ptr->return_ttl, 1, 0,
				    pd->rdomain, reason);
			src->seqlo = 0;
			src->seqhi = 1;
			src->max_win = 1;
		} else if (pf_status.debug >= LOG_NOTICE) {
			log(LOG_NOTICE, "pf: BAD state: ");
			pf_print_state(*stp);
			pf_print_flags(th->th_flags);
			addlog(" seq=%u (%u) ack=%u len=%u ackskew=%d "
			    "pkts=%llu:%llu dir=%s,%s\n",
			    seq, orig_seq, ack, pd->p_len, ackskew,
			    (*stp)->packets[0], (*stp)->packets[1],
			    pd->dir == PF_IN ? "in" : "out",
			    pd->dir == (*stp)->direction ? "fwd" : "rev");
			addlog("pf: State failure on: %c %c %c %c | %c %c\n",
			    SEQ_GEQ(src->seqhi, data_end) ? ' ' : '1',
			    SEQ_GEQ(seq, src->seqlo - (dst->max_win << dws)) ?
			    ' ': '2',
			    (ackskew >= -MAXACKWINDOW) ? ' ' : '3',
			    (ackskew <= (MAXACKWINDOW << sws)) ? ' ' : '4',
			    SEQ_GEQ(src->seqhi + MAXACKWINDOW, data_end) ?
			    ' ' :'5',
			    SEQ_GEQ(seq, src->seqlo - MAXACKWINDOW) ?' ' :'6');
		}
		REASON_SET(reason, PFRES_BADSTATE);
		return (PF_DROP);
	}

	return (PF_PASS);
}

int
pf_tcp_track_sloppy(struct pf_pdesc *pd, struct pf_state **stp,
    u_short *reason)
{
	struct tcphdr		*th = &pd->hdr.tcp;
	struct pf_state_peer	*src, *dst;
	u_int8_t		 psrc, pdst;

	if (pd->dir == (*stp)->direction) {
		src = &(*stp)->src;
		dst = &(*stp)->dst;
		psrc = PF_PEER_SRC;
		pdst = PF_PEER_DST;
	} else {
		src = &(*stp)->dst;
		dst = &(*stp)->src;
		psrc = PF_PEER_DST;
		pdst = PF_PEER_SRC;
	}

	if (th->th_flags & TH_SYN)
		if (src->state < TCPS_SYN_SENT)
			pf_set_protostate(*stp, psrc, TCPS_SYN_SENT);
	if (th->th_flags & TH_FIN)
		if (src->state < TCPS_CLOSING)
			pf_set_protostate(*stp, psrc, TCPS_CLOSING);
	if (th->th_flags & TH_ACK) {
		if (dst->state == TCPS_SYN_SENT) {
			pf_set_protostate(*stp, pdst, TCPS_ESTABLISHED);
			if (src->state == TCPS_ESTABLISHED &&
			    !SLIST_EMPTY(&(*stp)->src_nodes) &&
			    pf_src_connlimit(stp)) {
				REASON_SET(reason, PFRES_SRCLIMIT);
				return (PF_DROP);
			}
		} else if (dst->state == TCPS_CLOSING) {
			pf_set_protostate(*stp, pdst, TCPS_FIN_WAIT_2);
		} else if (src->state == TCPS_SYN_SENT &&
		    dst->state < TCPS_SYN_SENT) {
			/*
			 * Handle a special sloppy case where we only see one
			 * half of the connection. If there is a ACK after
			 * the initial SYN without ever seeing a packet from
			 * the destination, set the connection to established.
			 */
			pf_set_protostate(*stp, PF_PEER_BOTH,
			    TCPS_ESTABLISHED);
			if (!SLIST_EMPTY(&(*stp)->src_nodes) &&
			    pf_src_connlimit(stp)) {
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
			pf_set_protostate(*stp, pdst, TCPS_CLOSING);
		}
	}
	if (th->th_flags & TH_RST)
		pf_set_protostate(*stp, PF_PEER_BOTH, TCPS_TIME_WAIT);

	/* update expire time */
	(*stp)->expire = getuptime();
	if (src->state >= TCPS_FIN_WAIT_2 &&
	    dst->state >= TCPS_FIN_WAIT_2)
		pf_update_state_timeout(*stp, PFTM_TCP_CLOSED);
	else if (src->state >= TCPS_CLOSING &&
	    dst->state >= TCPS_CLOSING)
		pf_update_state_timeout(*stp, PFTM_TCP_FIN_WAIT);
	else if (src->state < TCPS_ESTABLISHED ||
	    dst->state < TCPS_ESTABLISHED)
		pf_update_state_timeout(*stp, PFTM_TCP_OPENING);
	else if (src->state >= TCPS_CLOSING ||
	    dst->state >= TCPS_CLOSING)
		pf_update_state_timeout(*stp, PFTM_TCP_CLOSING);
	else
		pf_update_state_timeout(*stp, PFTM_TCP_ESTABLISHED);

	return (PF_PASS);
}

static __inline int
pf_synproxy(struct pf_pdesc *pd, struct pf_state **stp, u_short *reason)
{
	struct pf_state_key	*sk = (*stp)->key[pd->didx];

	if ((*stp)->src.state == PF_TCPS_PROXY_SRC) {
		struct tcphdr	*th = &pd->hdr.tcp;

		if (pd->dir != (*stp)->direction) {
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_SYNPROXY_DROP);
		}
		if (th->th_flags & TH_SYN) {
			if (ntohl(th->th_seq) != (*stp)->src.seqlo) {
				REASON_SET(reason, PFRES_SYNPROXY);
				return (PF_DROP);
			}
			pf_send_tcp((*stp)->rule.ptr, pd->af, pd->dst,
			    pd->src, th->th_dport, th->th_sport,
			    (*stp)->src.seqhi, ntohl(th->th_seq) + 1,
			    TH_SYN|TH_ACK, 0, (*stp)->src.mss, 0, 1,
			    0, pd->rdomain, reason);
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_SYNPROXY_DROP);
		} else if ((th->th_flags & (TH_ACK|TH_RST|TH_FIN)) != TH_ACK ||
		    (ntohl(th->th_ack) != (*stp)->src.seqhi + 1) ||
		    (ntohl(th->th_seq) != (*stp)->src.seqlo + 1)) {
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_DROP);
		} else if (!SLIST_EMPTY(&(*stp)->src_nodes) &&
		    pf_src_connlimit(stp)) {
			REASON_SET(reason, PFRES_SRCLIMIT);
			return (PF_DROP);
		} else
			pf_set_protostate(*stp, PF_PEER_SRC,
			    PF_TCPS_PROXY_DST);
	}
	if ((*stp)->src.state == PF_TCPS_PROXY_DST) {
		struct tcphdr	*th = &pd->hdr.tcp;

		if (pd->dir == (*stp)->direction) {
			if (((th->th_flags & (TH_SYN|TH_ACK)) != TH_ACK) ||
			    (ntohl(th->th_ack) != (*stp)->src.seqhi + 1) ||
			    (ntohl(th->th_seq) != (*stp)->src.seqlo + 1)) {
				REASON_SET(reason, PFRES_SYNPROXY);
				return (PF_DROP);
			}
			(*stp)->src.max_win = MAX(ntohs(th->th_win), 1);
			if ((*stp)->dst.seqhi == 1)
				(*stp)->dst.seqhi = arc4random();
			pf_send_tcp((*stp)->rule.ptr, pd->af,
			    &sk->addr[pd->sidx], &sk->addr[pd->didx],
			    sk->port[pd->sidx], sk->port[pd->didx],
			    (*stp)->dst.seqhi, 0, TH_SYN, 0,
			    (*stp)->src.mss, 0, 0, (*stp)->tag,
			    sk->rdomain, reason);
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_SYNPROXY_DROP);
		} else if (((th->th_flags & (TH_SYN|TH_ACK)) !=
		    (TH_SYN|TH_ACK)) ||
		    (ntohl(th->th_ack) != (*stp)->dst.seqhi + 1)) {
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_DROP);
		} else {
			(*stp)->dst.max_win = MAX(ntohs(th->th_win), 1);
			(*stp)->dst.seqlo = ntohl(th->th_seq);
			pf_send_tcp((*stp)->rule.ptr, pd->af, pd->dst,
			    pd->src, th->th_dport, th->th_sport,
			    ntohl(th->th_ack), ntohl(th->th_seq) + 1,
			    TH_ACK, (*stp)->src.max_win, 0, 0, 0,
			    (*stp)->tag, pd->rdomain, reason);
			pf_send_tcp((*stp)->rule.ptr, pd->af,
			    &sk->addr[pd->sidx], &sk->addr[pd->didx],
			    sk->port[pd->sidx], sk->port[pd->didx],
			    (*stp)->src.seqhi + 1, (*stp)->src.seqlo + 1,
			    TH_ACK, (*stp)->dst.max_win, 0, 0, 1,
			    0, sk->rdomain, reason);
			(*stp)->src.seqdiff = (*stp)->dst.seqhi -
			    (*stp)->src.seqlo;
			(*stp)->dst.seqdiff = (*stp)->src.seqhi -
			    (*stp)->dst.seqlo;
			(*stp)->src.seqhi = (*stp)->src.seqlo +
			    (*stp)->dst.max_win;
			(*stp)->dst.seqhi = (*stp)->dst.seqlo +
			    (*stp)->src.max_win;
			(*stp)->src.wscale = (*stp)->dst.wscale = 0;
			pf_set_protostate(*stp, PF_PEER_BOTH,
			    TCPS_ESTABLISHED);
			REASON_SET(reason, PFRES_SYNPROXY);
			return (PF_SYNPROXY_DROP);
		}
	}
	return (PF_PASS);
}

int
pf_test_state(struct pf_pdesc *pd, struct pf_state **stp, u_short *reason)
{
	int			 copyback = 0;
	struct pf_state_peer	*src, *dst;
	int			 action;
	struct inpcb		*inp = pd->m->m_pkthdr.pf.inp;
	u_int8_t		 psrc, pdst;

	action = PF_PASS;
	if (pd->dir == (*stp)->direction) {
		src = &(*stp)->src;
		dst = &(*stp)->dst;
		psrc = PF_PEER_SRC;
		pdst = PF_PEER_DST;
	} else {
		src = &(*stp)->dst;
		dst = &(*stp)->src;
		psrc = PF_PEER_DST;
		pdst = PF_PEER_SRC;
	}

	switch (pd->virtual_proto) {
	case IPPROTO_TCP:
		if ((action = pf_synproxy(pd, stp, reason)) != PF_PASS)
			return (action);
		if ((pd->hdr.tcp.th_flags & (TH_SYN|TH_ACK)) == TH_SYN) {

			if (dst->state >= TCPS_FIN_WAIT_2 &&
			    src->state >= TCPS_FIN_WAIT_2) {
				if (pf_status.debug >= LOG_NOTICE) {
					log(LOG_NOTICE, "pf: state reuse ");
					pf_print_state(*stp);
					pf_print_flags(pd->hdr.tcp.th_flags);
					addlog("\n");
				}
				/* XXX make sure it's the same direction ?? */
				pf_update_state_timeout(*stp, PFTM_PURGE);
				pf_state_unref(*stp);
				*stp = NULL;
				pf_mbuf_link_inpcb(pd->m, inp);
				return (PF_DROP);
			} else if (dst->state >= TCPS_ESTABLISHED &&
			    src->state >= TCPS_ESTABLISHED) {
				/*
				 * SYN matches existing state???
				 * Typically happens when sender boots up after
				 * sudden panic. Certain protocols (NFSv3) are
				 * always using same port numbers. Challenge
				 * ACK enables all parties (firewall and peers)
				 * to get in sync again.
				 */
				pf_send_challenge_ack(pd, *stp, src, dst,
				    reason);
				return (PF_DROP);
			}
		}

		if ((*stp)->state_flags & PFSTATE_SLOPPY) {
			if (pf_tcp_track_sloppy(pd, stp, reason) == PF_DROP)
				return (PF_DROP);
		} else {
			if (pf_tcp_track_full(pd, stp, reason, &copyback,
			    PF_REVERSED_KEY((*stp)->key, pd->af)) == PF_DROP)
				return (PF_DROP);
		}
		break;
	case IPPROTO_UDP:
		/* update states */
		if (src->state < PFUDPS_SINGLE)
			pf_set_protostate(*stp, psrc, PFUDPS_SINGLE);
		if (dst->state == PFUDPS_SINGLE)
			pf_set_protostate(*stp, pdst, PFUDPS_MULTIPLE);

		/* update expire time */
		(*stp)->expire = getuptime();
		if (src->state == PFUDPS_MULTIPLE &&
		    dst->state == PFUDPS_MULTIPLE)
			pf_update_state_timeout(*stp, PFTM_UDP_MULTIPLE);
		else
			pf_update_state_timeout(*stp, PFTM_UDP_SINGLE);
		break;
	default:
		/* update states */
		if (src->state < PFOTHERS_SINGLE)
			pf_set_protostate(*stp, psrc, PFOTHERS_SINGLE);
		if (dst->state == PFOTHERS_SINGLE)
			pf_set_protostate(*stp, pdst, PFOTHERS_MULTIPLE);

		/* update expire time */
		(*stp)->expire = getuptime();
		if (src->state == PFOTHERS_MULTIPLE &&
		    dst->state == PFOTHERS_MULTIPLE)
			pf_update_state_timeout(*stp, PFTM_OTHER_MULTIPLE);
		else
			pf_update_state_timeout(*stp, PFTM_OTHER_SINGLE);
		break;
	}

	/* translate source/destination address, if necessary */
	if ((*stp)->key[PF_SK_WIRE] != (*stp)->key[PF_SK_STACK]) {
		struct pf_state_key	*nk;
		int			 afto, sidx, didx;

		if (PF_REVERSED_KEY((*stp)->key, pd->af))
			nk = (*stp)->key[pd->sidx];
		else
			nk = (*stp)->key[pd->didx];

		afto = pd->af != nk->af;
		sidx = afto ? pd->didx : pd->sidx;
		didx = afto ? pd->sidx : pd->didx;

#ifdef INET6
		if (afto) {
			pf_addrcpy(&pd->nsaddr, &nk->addr[sidx], nk->af);
			pf_addrcpy(&pd->ndaddr, &nk->addr[didx], nk->af);
			pd->naf = nk->af;
			action = PF_AFRT;
		}
#endif /* INET6 */

		if (!afto)
			pf_translate_a(pd, pd->src, &nk->addr[sidx]);

		if (pd->sport != NULL)
			pf_patch_16(pd, pd->sport, nk->port[sidx]);

		if (afto || PF_ANEQ(pd->dst, &nk->addr[didx], pd->af) ||
		    pd->rdomain != nk->rdomain)
			pd->destchg = 1;

		if (!afto)
			pf_translate_a(pd, pd->dst, &nk->addr[didx]);

		if (pd->dport != NULL)
			pf_patch_16(pd, pd->dport, nk->port[didx]);

		pd->m->m_pkthdr.ph_rtableid = nk->rdomain;
		copyback = 1;
	}

	if (copyback && pd->hdrlen > 0) {
		m_copyback(pd->m, pd->off, pd->hdrlen, &pd->hdr, M_NOWAIT);
	}

	return (action);
}

int
pf_icmp_state_lookup(struct pf_pdesc *pd, struct pf_state_key_cmp *key,
    struct pf_state **stp, u_int16_t icmpid, u_int16_t type,
    int icmp_dir, int *iidx, int multi, int inner)
{
	int direction, action;

	key->af = pd->af;
	key->proto = pd->proto;
	key->rdomain = pd->rdomain;
	if (icmp_dir == PF_IN) {
		*iidx = pd->sidx;
		key->port[pd->sidx] = icmpid;
		key->port[pd->didx] = type;
	} else {
		*iidx = pd->didx;
		key->port[pd->sidx] = type;
		key->port[pd->didx] = icmpid;
	}

	if (pf_state_key_addr_setup(pd, key, pd->sidx, pd->src, pd->didx,
	    pd->dst, pd->af, multi))
		return (PF_DROP);

	key->hash = pf_pkt_hash(key->af, key->proto,
	    &key->addr[0], &key->addr[1], 0, 0);

	action = pf_find_state(pd, key, stp);
	if (action != PF_MATCH)
		return (action);

	if ((*stp)->state_flags & PFSTATE_SLOPPY)
		return (-1);

	/* Is this ICMP message flowing in right direction? */
	if ((*stp)->key[PF_SK_WIRE]->af != (*stp)->key[PF_SK_STACK]->af)
		direction = (pd->af == (*stp)->key[PF_SK_WIRE]->af) ?
		    PF_IN : PF_OUT;
	else
		direction = (*stp)->direction;
	if ((((!inner && direction == pd->dir) ||
	    (inner && direction != pd->dir)) ?
	    PF_IN : PF_OUT) != icmp_dir) {
		if (pf_status.debug >= LOG_NOTICE) {
			log(LOG_NOTICE,
			    "pf: icmp type %d in wrong direction (%d): ",
			    ntohs(type), icmp_dir);
			pf_print_state(*stp);
			addlog("\n");
		}
		return (PF_DROP);
	}
	return (-1);
}

int
pf_test_state_icmp(struct pf_pdesc *pd, struct pf_state **stp,
    u_short *reason)
{
	u_int16_t	 virtual_id, virtual_type;
	u_int8_t	 icmptype, icmpcode;
	int		 icmp_dir, iidx, ret, copyback = 0;

	struct pf_state_key_cmp key;

	switch (pd->proto) {
	case IPPROTO_ICMP:
		icmptype = pd->hdr.icmp.icmp_type;
		icmpcode = pd->hdr.icmp.icmp_code;
		break;
#ifdef INET6
	case IPPROTO_ICMPV6:
		icmptype = pd->hdr.icmp6.icmp6_type;
		icmpcode = pd->hdr.icmp6.icmp6_code;
		break;
#endif /* INET6 */
	default:
		panic("unhandled proto %d", pd->proto);
	}

	if (pf_icmp_mapping(pd, icmptype, &icmp_dir, &virtual_id,
	    &virtual_type) == 0) {
		/*
		 * ICMP query/reply message not related to a TCP/UDP packet.
		 * Search for an ICMP state.
		 */
		ret = pf_icmp_state_lookup(pd, &key, stp,
		    virtual_id, virtual_type, icmp_dir, &iidx,
		    0, 0);
		/* IPv6? try matching a multicast address */
		if (ret == PF_DROP && pd->af == AF_INET6 && icmp_dir == PF_OUT)
			ret = pf_icmp_state_lookup(pd, &key, stp, virtual_id,
			    virtual_type, icmp_dir, &iidx, 1, 0);
		if (ret >= 0)
			return (ret);

		(*stp)->expire = getuptime();
		pf_update_state_timeout(*stp, PFTM_ICMP_ERROR_REPLY);

		/* translate source/destination address, if necessary */
		if ((*stp)->key[PF_SK_WIRE] != (*stp)->key[PF_SK_STACK]) {
			struct pf_state_key	*nk;
			int			 afto, sidx, didx;

			if (PF_REVERSED_KEY((*stp)->key, pd->af))
				nk = (*stp)->key[pd->sidx];
			else
				nk = (*stp)->key[pd->didx];

			afto = pd->af != nk->af;
			sidx = afto ? pd->didx : pd->sidx;
			didx = afto ? pd->sidx : pd->didx;
			iidx = afto ? !iidx : iidx;
#ifdef	INET6
			if (afto) {
				pf_addrcpy(&pd->nsaddr, &nk->addr[sidx],
				    nk->af);
				pf_addrcpy(&pd->ndaddr, &nk->addr[didx],
				    nk->af);
				pd->naf = nk->af;
			}
#endif /* INET6 */
			if (!afto) {
				pf_translate_a(pd, pd->src, &nk->addr[sidx]);
				pf_translate_a(pd, pd->dst, &nk->addr[didx]);
			}

			if (pd->rdomain != nk->rdomain)
				pd->destchg = 1;
			if (!afto && PF_ANEQ(pd->dst,
				&nk->addr[didx], pd->af))
				pd->destchg = 1;
			pd->m->m_pkthdr.ph_rtableid = nk->rdomain;

			switch (pd->af) {
			case AF_INET:
#ifdef INET6
				if (afto) {
					if (pf_translate_icmp_af(pd, AF_INET6,
					    &pd->hdr.icmp))
						return (PF_DROP);
					pd->proto = IPPROTO_ICMPV6;
				}
#endif /* INET6 */
				pf_patch_16(pd,
				    &pd->hdr.icmp.icmp_id, nk->port[iidx]);

				m_copyback(pd->m, pd->off, ICMP_MINLEN,
				    &pd->hdr.icmp, M_NOWAIT);
				copyback = 1;
				break;
#ifdef INET6
			case AF_INET6:
				if (afto) {
					if (pf_translate_icmp_af(pd, AF_INET,
					    &pd->hdr.icmp6))
						return (PF_DROP);
					pd->proto = IPPROTO_ICMP;
				}

				pf_patch_16(pd,
				    &pd->hdr.icmp6.icmp6_id, nk->port[iidx]);

				m_copyback(pd->m, pd->off,
				    sizeof(struct icmp6_hdr), &pd->hdr.icmp6,
				    M_NOWAIT);
				copyback = 1;
				break;
#endif /* INET6 */
			}
#ifdef	INET6
			if (afto)
				return (PF_AFRT);
#endif /* INET6 */
		}
	} else {
		/*
		 * ICMP error message in response to a TCP/UDP packet.
		 * Extract the inner TCP/UDP header and search for that state.
		 */
		struct pf_pdesc	 pd2;
		struct ip	 h2;
#ifdef INET6
		struct ip6_hdr	 h2_6;
#endif /* INET6 */
		int		 ipoff2;

		/* Initialize pd2 fields valid for both packets with pd. */
		memset(&pd2, 0, sizeof(pd2));
		pd2.af = pd->af;
		pd2.dir = pd->dir;
		pd2.kif = pd->kif;
		pd2.m = pd->m;
		pd2.rdomain = pd->rdomain;
		/* Payload packet is from the opposite direction. */
		pd2.sidx = (pd2.dir == PF_IN) ? 1 : 0;
		pd2.didx = (pd2.dir == PF_IN) ? 0 : 1;
		switch (pd->af) {
		case AF_INET:
			/* offset of h2 in mbuf chain */
			ipoff2 = pd->off + ICMP_MINLEN;

			if (!pf_pull_hdr(pd2.m, ipoff2, &h2, sizeof(h2),
			    reason, pd2.af)) {
				DPFPRINTF(LOG_NOTICE,
				    "ICMP error message too short (ip)");
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
			pd2.off = ipoff2;
			if (pf_walk_header(&pd2, &h2, reason) != PF_PASS)
				return (PF_DROP);

			pd2.tot_len = ntohs(h2.ip_len);
			pd2.ttl = h2.ip_ttl;
			pd2.src = (struct pf_addr *)&h2.ip_src;
			pd2.dst = (struct pf_addr *)&h2.ip_dst;
			break;
#ifdef INET6
		case AF_INET6:
			ipoff2 = pd->off + sizeof(struct icmp6_hdr);

			if (!pf_pull_hdr(pd2.m, ipoff2, &h2_6, sizeof(h2_6),
			    reason, pd2.af)) {
				DPFPRINTF(LOG_NOTICE,
				    "ICMP error message too short (ip6)");
				return (PF_DROP);
			}

			pd2.off = ipoff2;
			if (pf_walk_header6(&pd2, &h2_6, reason) != PF_PASS)
				return (PF_DROP);

			pd2.tot_len = ntohs(h2_6.ip6_plen) +
			    sizeof(struct ip6_hdr);
			pd2.ttl = h2_6.ip6_hlim;
			pd2.src = (struct pf_addr *)&h2_6.ip6_src;
			pd2.dst = (struct pf_addr *)&h2_6.ip6_dst;
			break;
#endif /* INET6 */
		default:
			unhandled_af(pd->af);
		}

		if (PF_ANEQ(pd->dst, pd2.src, pd->af)) {
			if (pf_status.debug >= LOG_NOTICE) {
				log(LOG_NOTICE,
				    "pf: BAD ICMP %d:%d outer dst: ",
				    icmptype, icmpcode);
				pf_print_host(pd->src, 0, pd->af);
				addlog(" -> ");
				pf_print_host(pd->dst, 0, pd->af);
				addlog(" inner src: ");
				pf_print_host(pd2.src, 0, pd2.af);
				addlog(" -> ");
				pf_print_host(pd2.dst, 0, pd2.af);
				addlog("\n");
			}
			REASON_SET(reason, PFRES_BADSTATE);
			return (PF_DROP);
		}

		switch (pd2.proto) {
		case IPPROTO_TCP: {
			struct tcphdr		*th = &pd2.hdr.tcp;
			u_int32_t		 seq;
			struct pf_state_peer	*src, *dst;
			u_int8_t		 dws;
			int			 action;

			/*
			 * Only the first 8 bytes of the TCP header can be
			 * expected. Don't access any TCP header fields after
			 * th_seq, an ackskew test is not possible.
			 */
			if (!pf_pull_hdr(pd2.m, pd2.off, th, 8, reason,
			    pd2.af)) {
				DPFPRINTF(LOG_NOTICE,
				    "ICMP error message too short (tcp)");
				return (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_TCP;
			key.rdomain = pd2.rdomain;
			pf_addrcpy(&key.addr[pd2.sidx], pd2.src, key.af);
			pf_addrcpy(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[pd2.sidx] = th->th_sport;
			key.port[pd2.didx] = th->th_dport;
			key.hash = pf_pkt_hash(pd2.af, pd2.proto,
			    pd2.src, pd2.dst, th->th_sport, th->th_dport);

			action = pf_find_state(&pd2, &key, stp);
			if (action != PF_MATCH)
				return (action);

			if (pd2.dir == (*stp)->direction) {
				if (PF_REVERSED_KEY((*stp)->key, pd->af)) {
					src = &(*stp)->src;
					dst = &(*stp)->dst;
				} else {
					src = &(*stp)->dst;
					dst = &(*stp)->src;
				}
			} else {
				if (PF_REVERSED_KEY((*stp)->key, pd->af)) {
					src = &(*stp)->dst;
					dst = &(*stp)->src;
				} else {
					src = &(*stp)->src;
					dst = &(*stp)->dst;
				}
			}

			if (src->wscale && dst->wscale)
				dws = dst->wscale & PF_WSCALE_MASK;
			else
				dws = 0;

			/* Demodulate sequence number */
			seq = ntohl(th->th_seq) - src->seqdiff;
			if (src->seqdiff) {
				pf_patch_32(pd, &th->th_seq, htonl(seq));
				copyback = 1;
			}

			if (!((*stp)->state_flags & PFSTATE_SLOPPY) &&
			    (!SEQ_GEQ(src->seqhi, seq) || !SEQ_GEQ(seq,
			    src->seqlo - (dst->max_win << dws)))) {
				if (pf_status.debug >= LOG_NOTICE) {
					log(LOG_NOTICE,
					    "pf: BAD ICMP %d:%d ",
					    icmptype, icmpcode);
					pf_print_host(pd->src, 0, pd->af);
					addlog(" -> ");
					pf_print_host(pd->dst, 0, pd->af);
					addlog(" state: ");
					pf_print_state(*stp);
					addlog(" seq=%u\n", seq);
				}
				REASON_SET(reason, PFRES_BADSTATE);
				return (PF_DROP);
			} else {
				if (pf_status.debug >= LOG_DEBUG) {
					log(LOG_DEBUG,
					    "pf: OK ICMP %d:%d ",
					    icmptype, icmpcode);
					pf_print_host(pd->src, 0, pd->af);
					addlog(" -> ");
					pf_print_host(pd->dst, 0, pd->af);
					addlog(" state: ");
					pf_print_state(*stp);
					addlog(" seq=%u\n", seq);
				}
			}

			/* translate source/destination address, if necessary */
			if ((*stp)->key[PF_SK_WIRE] !=
			    (*stp)->key[PF_SK_STACK]) {
				struct pf_state_key	*nk;
				int			 afto, sidx, didx;

				if (PF_REVERSED_KEY((*stp)->key, pd->af))
					nk = (*stp)->key[pd->sidx];
				else
					nk = (*stp)->key[pd->didx];

				afto = pd->af != nk->af;
				sidx = afto ? pd2.didx : pd2.sidx;
				didx = afto ? pd2.sidx : pd2.didx;

#ifdef INET6
				if (afto) {
					if (pf_translate_icmp_af(pd, nk->af,
					    &pd->hdr.icmp))
						return (PF_DROP);
					m_copyback(pd->m, pd->off,
					    sizeof(struct icmp6_hdr),
					    &pd->hdr.icmp6, M_NOWAIT);
					if (pf_change_icmp_af(pd->m, ipoff2,
					    pd, &pd2, &nk->addr[sidx],
					    &nk->addr[didx], pd->af, nk->af))
						return (PF_DROP);
					pd->m->m_pkthdr.ph_rtableid =
					    nk->rdomain;
					pd->destchg = 1;
					pf_addrcpy(&pd->nsaddr,
					    &nk->addr[pd2.sidx], nk->af);
					pf_addrcpy(&pd->ndaddr,
					    &nk->addr[pd2.didx], nk->af);
					if (nk->af == AF_INET) {
						pd->proto = IPPROTO_ICMP;
					} else {
						pd->proto = IPPROTO_ICMPV6;
						/*
						 * IPv4 becomes IPv6 so we must
						 * copy IPv4 src addr to least
						 * 32bits in IPv6 address to
						 * keep traceroute/icmp
						 * working.
						 */
						pd->nsaddr.addr32[3] =
						    pd->src->addr32[0];
					}
					pd->naf = nk->af;

					pf_patch_16(pd,
					    &th->th_sport, nk->port[sidx]);
					pf_patch_16(pd,
					    &th->th_dport, nk->port[didx]);

					m_copyback(pd2.m, pd2.off, 8, th,
					    M_NOWAIT);
					return (PF_AFRT);
				}
#endif	/* INET6 */
				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != th->th_sport)
					pf_translate_icmp(pd, pd2.src,
					    &th->th_sport, pd->dst,
					    &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx]);

				if (PF_ANEQ(pd2.dst, &nk->addr[pd2.didx],
				    pd2.af) || pd2.rdomain != nk->rdomain)
					pd->destchg = 1;
				pd->m->m_pkthdr.ph_rtableid = nk->rdomain;

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != th->th_dport)
					pf_translate_icmp(pd, pd2.dst,
					    &th->th_dport, pd->src,
					    &nk->addr[pd2.didx],
					    nk->port[pd2.didx]);
				copyback = 1;
			}

			if (copyback) {
				switch (pd2.af) {
				case AF_INET:
					m_copyback(pd->m, pd->off, ICMP_MINLEN,
					    &pd->hdr.icmp, M_NOWAIT);
					m_copyback(pd2.m, ipoff2, sizeof(h2),
					    &h2, M_NOWAIT);
					break;
#ifdef INET6
				case AF_INET6:
					m_copyback(pd->m, pd->off,
					    sizeof(struct icmp6_hdr),
					    &pd->hdr.icmp6, M_NOWAIT);
					m_copyback(pd2.m, ipoff2, sizeof(h2_6),
					    &h2_6, M_NOWAIT);
					break;
#endif /* INET6 */
				}
				m_copyback(pd2.m, pd2.off, 8, th, M_NOWAIT);
			}
			break;
		}
		case IPPROTO_UDP: {
			struct udphdr	*uh = &pd2.hdr.udp;
			int		 action;

			if (!pf_pull_hdr(pd2.m, pd2.off, uh, sizeof(*uh),
			    reason, pd2.af)) {
				DPFPRINTF(LOG_NOTICE,
				    "ICMP error message too short (udp)");
				return (PF_DROP);
			}

			key.af = pd2.af;
			key.proto = IPPROTO_UDP;
			key.rdomain = pd2.rdomain;
			pf_addrcpy(&key.addr[pd2.sidx], pd2.src, key.af);
			pf_addrcpy(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[pd2.sidx] = uh->uh_sport;
			key.port[pd2.didx] = uh->uh_dport;
			key.hash = pf_pkt_hash(pd2.af, pd2.proto,
			    pd2.src, pd2.dst, uh->uh_sport, uh->uh_dport);

			action = pf_find_state(&pd2, &key, stp);
			if (action != PF_MATCH)
				return (action);

			/* translate source/destination address, if necessary */
			if ((*stp)->key[PF_SK_WIRE] !=
			    (*stp)->key[PF_SK_STACK]) {
				struct pf_state_key	*nk;
				int			 afto, sidx, didx;

				if (PF_REVERSED_KEY((*stp)->key, pd->af))
					nk = (*stp)->key[pd->sidx];
				else
					nk = (*stp)->key[pd->didx];

				afto = pd->af != nk->af;
				sidx = afto ? pd2.didx : pd2.sidx;
				didx = afto ? pd2.sidx : pd2.didx;

#ifdef INET6
				if (afto) {
					if (pf_translate_icmp_af(pd, nk->af,
					    &pd->hdr.icmp))
						return (PF_DROP);
					m_copyback(pd->m, pd->off,
					    sizeof(struct icmp6_hdr),
					    &pd->hdr.icmp6, M_NOWAIT);
					if (pf_change_icmp_af(pd->m, ipoff2,
					    pd, &pd2, &nk->addr[sidx],
					    &nk->addr[didx], pd->af, nk->af))
						return (PF_DROP);
					pd->m->m_pkthdr.ph_rtableid =
					    nk->rdomain;
					pd->destchg = 1;
					pf_addrcpy(&pd->nsaddr,
					    &nk->addr[pd2.sidx], nk->af);
					pf_addrcpy(&pd->ndaddr,
					    &nk->addr[pd2.didx], nk->af);
					if (nk->af == AF_INET) {
						pd->proto = IPPROTO_ICMP;
					} else {
						pd->proto = IPPROTO_ICMPV6;
						/*
						 * IPv4 becomes IPv6 so we must
						 * copy IPv4 src addr to least
						 * 32bits in IPv6 address to
						 * keep traceroute/icmp
						 * working.
						 */
						pd->nsaddr.addr32[3] =
						    pd->src->addr32[0];
					}
					pd->naf = nk->af;

					pf_patch_16(pd,
					    &uh->uh_sport, nk->port[sidx]);
					pf_patch_16(pd,
					    &uh->uh_dport, nk->port[didx]);

					m_copyback(pd2.m, pd2.off, sizeof(*uh),
					    uh, M_NOWAIT);
					return (PF_AFRT);
				}
#endif /* INET6 */

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    nk->port[pd2.sidx] != uh->uh_sport)
					pf_translate_icmp(pd, pd2.src,
					    &uh->uh_sport, pd->dst,
					    &nk->addr[pd2.sidx],
					    nk->port[pd2.sidx]);

				if (PF_ANEQ(pd2.dst, &nk->addr[pd2.didx],
				    pd2.af) || pd2.rdomain != nk->rdomain)
					pd->destchg = 1;
				pd->m->m_pkthdr.ph_rtableid = nk->rdomain;

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af) ||
				    nk->port[pd2.didx] != uh->uh_dport)
					pf_translate_icmp(pd, pd2.dst,
					    &uh->uh_dport, pd->src,
					    &nk->addr[pd2.didx],
					    nk->port[pd2.didx]);

				switch (pd2.af) {
				case AF_INET:
					m_copyback(pd->m, pd->off, ICMP_MINLEN,
					    &pd->hdr.icmp, M_NOWAIT);
					m_copyback(pd2.m, ipoff2, sizeof(h2),
					    &h2, M_NOWAIT);
					break;
#ifdef INET6
				case AF_INET6:
					m_copyback(pd->m, pd->off,
					    sizeof(struct icmp6_hdr),
					    &pd->hdr.icmp6, M_NOWAIT);
					m_copyback(pd2.m, ipoff2, sizeof(h2_6),
					    &h2_6, M_NOWAIT);
					break;
#endif /* INET6 */
				}
				/* Avoid recomputing quoted UDP checksum.
				 * note: udp6 0 csum invalid per rfc2460 p27.
				 * but presumed nothing cares in this context */
				pf_patch_16(pd, &uh->uh_sum, 0);
				m_copyback(pd2.m, pd2.off, sizeof(*uh), uh,
				    M_NOWAIT);
				copyback = 1;
			}
			break;
		}
		case IPPROTO_ICMP: {
			struct icmp	*iih = &pd2.hdr.icmp;

			if (pd2.af != AF_INET) {
				REASON_SET(reason, PFRES_NORM);
				return (PF_DROP);
			}

			if (!pf_pull_hdr(pd2.m, pd2.off, iih, ICMP_MINLEN,
			    reason, pd2.af)) {
				DPFPRINTF(LOG_NOTICE,
				    "ICMP error message too short (icmp)");
				return (PF_DROP);
			}

			pf_icmp_mapping(&pd2, iih->icmp_type,
			    &icmp_dir, &virtual_id, &virtual_type);

			ret = pf_icmp_state_lookup(&pd2, &key, stp,
			    virtual_id, virtual_type, icmp_dir, &iidx, 0, 1);
			if (ret >= 0)
				return (ret);

			/* translate source/destination address, if necessary */
			if ((*stp)->key[PF_SK_WIRE] !=
			    (*stp)->key[PF_SK_STACK]) {
				struct pf_state_key	*nk;
				int			 afto, sidx, didx;

				if (PF_REVERSED_KEY((*stp)->key, pd->af))
					nk = (*stp)->key[pd->sidx];
				else
					nk = (*stp)->key[pd->didx];

				afto = pd->af != nk->af;
				sidx = afto ? pd2.didx : pd2.sidx;
				didx = afto ? pd2.sidx : pd2.didx;
				iidx = afto ? !iidx : iidx;

#ifdef INET6
				if (afto) {
					if (nk->af != AF_INET6)
						return (PF_DROP);
					if (pf_translate_icmp_af(pd, nk->af,
					    &pd->hdr.icmp))
						return (PF_DROP);
					m_copyback(pd->m, pd->off,
					    sizeof(struct icmp6_hdr),
					    &pd->hdr.icmp6, M_NOWAIT);
					if (pf_change_icmp_af(pd->m, ipoff2,
					    pd, &pd2, &nk->addr[sidx],
					    &nk->addr[didx], pd->af, nk->af))
						return (PF_DROP);
					pd->proto = IPPROTO_ICMPV6;
					if (pf_translate_icmp_af(pd,
						nk->af, iih))
						return (PF_DROP);
					if (virtual_type == htons(ICMP_ECHO))
						pf_patch_16(pd, &iih->icmp_id,
						    nk->port[iidx]);
					m_copyback(pd2.m, pd2.off, ICMP_MINLEN,
					    iih, M_NOWAIT);
					pd->m->m_pkthdr.ph_rtableid =
					    nk->rdomain;
					pd->destchg = 1;
					pf_addrcpy(&pd->nsaddr,
					    &nk->addr[pd2.sidx], nk->af);
					pf_addrcpy(&pd->ndaddr,
					    &nk->addr[pd2.didx], nk->af);
					/*
					 * IPv4 becomes IPv6 so we must copy
					 * IPv4 src addr to least 32bits in
					 * IPv6 address to keep traceroute
					 * working.
					 */
					pd->nsaddr.addr32[3] =
					    pd->src->addr32[0];
					pd->naf = nk->af;
					return (PF_AFRT);
				}
#endif /* INET6 */

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    (virtual_type == htons(ICMP_ECHO) &&
				    nk->port[iidx] != iih->icmp_id))
					pf_translate_icmp(pd, pd2.src,
					    (virtual_type == htons(ICMP_ECHO)) ?
					    &iih->icmp_id : NULL,
					    pd->dst, &nk->addr[pd2.sidx],
					    (virtual_type == htons(ICMP_ECHO)) ?
					    nk->port[iidx] : 0);

				if (PF_ANEQ(pd2.dst, &nk->addr[pd2.didx],
				    pd2.af) || pd2.rdomain != nk->rdomain)
					pd->destchg = 1;
				pd->m->m_pkthdr.ph_rtableid = nk->rdomain;

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af))
					pf_translate_icmp(pd, pd2.dst, NULL,
					    pd->src, &nk->addr[pd2.didx], 0);

				m_copyback(pd->m, pd->off, ICMP_MINLEN,
				    &pd->hdr.icmp, M_NOWAIT);
				m_copyback(pd2.m, ipoff2, sizeof(h2), &h2,
				    M_NOWAIT);
				m_copyback(pd2.m, pd2.off, ICMP_MINLEN, iih,
				    M_NOWAIT);
				copyback = 1;
			}
			break;
		}
#ifdef INET6
		case IPPROTO_ICMPV6: {
			struct icmp6_hdr	*iih = &pd2.hdr.icmp6;

			if (pd2.af != AF_INET6) {
				REASON_SET(reason, PFRES_NORM);
				return (PF_DROP);
			}

			if (!pf_pull_hdr(pd2.m, pd2.off, iih,
			    sizeof(struct icmp6_hdr), reason, pd2.af)) {
				DPFPRINTF(LOG_NOTICE,
				    "ICMP error message too short (icmp6)");
				return (PF_DROP);
			}

			pf_icmp_mapping(&pd2, iih->icmp6_type,
			    &icmp_dir, &virtual_id, &virtual_type);
			ret = pf_icmp_state_lookup(&pd2, &key, stp,
			    virtual_id, virtual_type, icmp_dir, &iidx, 0, 1);
			/* IPv6? try matching a multicast address */
			if (ret == PF_DROP && pd2.af == AF_INET6 &&
			    icmp_dir == PF_OUT)
				ret = pf_icmp_state_lookup(&pd2, &key, stp,
				    virtual_id, virtual_type, icmp_dir, &iidx,
				    1, 1);
			if (ret >= 0)
				return (ret);

			/* translate source/destination address, if necessary */
			if ((*stp)->key[PF_SK_WIRE] !=
			    (*stp)->key[PF_SK_STACK]) {
				struct pf_state_key	*nk;
				int			 afto, sidx, didx;

				if (PF_REVERSED_KEY((*stp)->key, pd->af))
					nk = (*stp)->key[pd->sidx];
				else
					nk = (*stp)->key[pd->didx];

				afto = pd->af != nk->af;
				sidx = afto ? pd2.didx : pd2.sidx;
				didx = afto ? pd2.sidx : pd2.didx;
				iidx = afto ? !iidx : iidx;

				if (afto) {
					if (nk->af != AF_INET)
						return (PF_DROP);
					if (pf_translate_icmp_af(pd, nk->af,
					    &pd->hdr.icmp))
						return (PF_DROP);
					m_copyback(pd->m, pd->off,
					    sizeof(struct icmp6_hdr),
					    &pd->hdr.icmp6, M_NOWAIT);
					if (pf_change_icmp_af(pd->m, ipoff2,
					    pd, &pd2, &nk->addr[sidx],
					    &nk->addr[didx], pd->af, nk->af))
						return (PF_DROP);
					pd->proto = IPPROTO_ICMP;
					if (pf_translate_icmp_af(pd,
						nk->af, iih))
						return (PF_DROP);
					if (virtual_type ==
					    htons(ICMP6_ECHO_REQUEST))
						pf_patch_16(pd, &iih->icmp6_id,
						    nk->port[iidx]);
					m_copyback(pd2.m, pd2.off,
					    sizeof(struct icmp6_hdr), iih,
					    M_NOWAIT);
					pd->m->m_pkthdr.ph_rtableid =
					    nk->rdomain;
					pd->destchg = 1;
					pf_addrcpy(&pd->nsaddr,
					    &nk->addr[pd2.sidx], nk->af);
					pf_addrcpy(&pd->ndaddr,
					    &nk->addr[pd2.didx], nk->af);
					pd->naf = nk->af;
					return (PF_AFRT);
				}

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af) ||
				    ((virtual_type ==
				    htons(ICMP6_ECHO_REQUEST)) &&
				    nk->port[pd2.sidx] != iih->icmp6_id))
					pf_translate_icmp(pd, pd2.src,
					    (virtual_type ==
					    htons(ICMP6_ECHO_REQUEST))
					    ? &iih->icmp6_id : NULL,
					    pd->dst, &nk->addr[pd2.sidx],
					    (virtual_type ==
					    htons(ICMP6_ECHO_REQUEST))
					    ? nk->port[iidx] : 0);

				if (PF_ANEQ(pd2.dst, &nk->addr[pd2.didx],
				    pd2.af) || pd2.rdomain != nk->rdomain)
					pd->destchg = 1;
				pd->m->m_pkthdr.ph_rtableid = nk->rdomain;

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af))
					pf_translate_icmp(pd, pd2.dst, NULL,
					    pd->src, &nk->addr[pd2.didx], 0);

				m_copyback(pd->m, pd->off,
				    sizeof(struct icmp6_hdr), &pd->hdr.icmp6,
				    M_NOWAIT);
				m_copyback(pd2.m, ipoff2, sizeof(h2_6), &h2_6,
				    M_NOWAIT);
				m_copyback(pd2.m, pd2.off,
				    sizeof(struct icmp6_hdr), iih, M_NOWAIT);
				copyback = 1;
			}
			break;
		}
#endif /* INET6 */
		default: {
			int	action;

			key.af = pd2.af;
			key.proto = pd2.proto;
			key.rdomain = pd2.rdomain;
			pf_addrcpy(&key.addr[pd2.sidx], pd2.src, key.af);
			pf_addrcpy(&key.addr[pd2.didx], pd2.dst, key.af);
			key.port[0] = key.port[1] = 0;
			key.hash = pf_pkt_hash(pd2.af, pd2.proto,
			    pd2.src, pd2.dst, 0, 0);

			action = pf_find_state(&pd2, &key, stp);
			if (action != PF_MATCH)
				return (action);

			/* translate source/destination address, if necessary */
			if ((*stp)->key[PF_SK_WIRE] !=
			    (*stp)->key[PF_SK_STACK]) {
				struct pf_state_key *nk =
				    (*stp)->key[pd->didx];

				if (PF_ANEQ(pd2.src,
				    &nk->addr[pd2.sidx], pd2.af))
					pf_translate_icmp(pd, pd2.src, NULL,
					    pd->dst, &nk->addr[pd2.sidx], 0);

				if (PF_ANEQ(pd2.dst, &nk->addr[pd2.didx],
				    pd2.af) || pd2.rdomain != nk->rdomain)
					pd->destchg = 1;
				pd->m->m_pkthdr.ph_rtableid = nk->rdomain;

				if (PF_ANEQ(pd2.dst,
				    &nk->addr[pd2.didx], pd2.af))
					pf_translate_icmp(pd, pd2.dst, NULL,
					    pd->src, &nk->addr[pd2.didx], 0);

				switch (pd2.af) {
				case AF_INET:
					m_copyback(pd->m, pd->off, ICMP_MINLEN,
					    &pd->hdr.icmp, M_NOWAIT);
					m_copyback(pd2.m, ipoff2, sizeof(h2),
					    &h2, M_NOWAIT);
					break;
#ifdef INET6
				case AF_INET6:
					m_copyback(pd->m, pd->off,
					    sizeof(struct icmp6_hdr),
					    &pd->hdr.icmp6, M_NOWAIT);
					m_copyback(pd2.m, ipoff2, sizeof(h2_6),
					    &h2_6, M_NOWAIT);
					break;
#endif /* INET6 */
				}
				copyback = 1;
			}
			break;
		}
		}
	}
	if (copyback) {
		m_copyback(pd->m, pd->off, pd->hdrlen, &pd->hdr, M_NOWAIT);
	}

	return (PF_PASS);
}

/*
 * ipoff and off are measured from the start of the mbuf chain.
 * h must be at "ipoff" on the mbuf chain.
 */
void *
pf_pull_hdr(struct mbuf *m, int off, void *p, int len,
    u_short *reasonp, sa_family_t af)
{
	int iplen = 0;

	switch (af) {
	case AF_INET: {
		struct ip	*h = mtod(m, struct ip *);
		u_int16_t	 fragoff = (ntohs(h->ip_off) & IP_OFFMASK) << 3;

		if (fragoff) {
			REASON_SET(reasonp, PFRES_FRAG);
			return (NULL);
		}
		iplen = ntohs(h->ip_len);
		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr	*h = mtod(m, struct ip6_hdr *);

		iplen = ntohs(h->ip6_plen) + sizeof(struct ip6_hdr);
		break;
	}
#endif /* INET6 */
	}
	if (m->m_pkthdr.len < off + len || iplen < off + len) {
		REASON_SET(reasonp, PFRES_SHORT);
		return (NULL);
	}
	m_copydata(m, off, len, p);
	return (p);
}

int
pf_routable(struct pf_addr *addr, sa_family_t af, struct pfi_kif *kif,
    int rtableid)
{
	struct sockaddr_storage	 ss;
	struct sockaddr_in	*dst;
	int			 ret = 1;
	int			 check_mpath;
#ifdef INET6
	struct sockaddr_in6	*dst6;
#endif	/* INET6 */
	struct rtentry		*rt = NULL;

	check_mpath = 0;
	memset(&ss, 0, sizeof(ss));
	switch (af) {
	case AF_INET:
		dst = (struct sockaddr_in *)&ss;
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = addr->v4;
		if (atomic_load_int(&ipmultipath))
			check_mpath = 1;
		break;
#ifdef INET6
	case AF_INET6:
		/*
		 * Skip check for addresses with embedded interface scope,
		 * as they would always match anyway.
		 */
		if (IN6_IS_SCOPE_EMBED(&addr->v6))
			goto out;
		dst6 = (struct sockaddr_in6 *)&ss;
		dst6->sin6_family = AF_INET6;
		dst6->sin6_len = sizeof(*dst6);
		dst6->sin6_addr = addr->v6;
		if (atomic_load_int(&ip6_multipath))
			check_mpath = 1;
		break;
#endif /* INET6 */
	}

	/* Skip checks for ipsec interfaces */
	if (kif != NULL && kif->pfik_ifp->if_type == IFT_ENC)
		goto out;

	rt = rtalloc(sstosa(&ss), 0, rtableid);
	if (rt != NULL) {
		/* No interface given, this is a no-route check */
		if (kif == NULL)
			goto out;

		if (kif->pfik_ifp == NULL) {
			ret = 0;
			goto out;
		}

		/* Perform uRPF check if passed input interface */
		ret = 0;
		do {
			if (rt->rt_ifidx == kif->pfik_ifp->if_index) {
				ret = 1;
#if NCARP > 0
			} else {
				struct ifnet	*ifp;

				ifp = if_get(rt->rt_ifidx);
				if (ifp != NULL && ifp->if_type == IFT_CARP &&
				    ifp->if_carpdevidx ==
				    kif->pfik_ifp->if_index)
					ret = 1;
				if_put(ifp);
#endif /* NCARP */
			}

			rt = rtable_iterate(rt);
		} while (check_mpath == 1 && rt != NULL && ret == 0);
	} else
		ret = 0;
out:
	rtfree(rt);
	return (ret);
}

int
pf_rtlabel_match(struct pf_addr *addr, sa_family_t af, struct pf_addr_wrap *aw,
    int rtableid)
{
	struct sockaddr_storage	 ss;
	struct sockaddr_in	*dst;
#ifdef INET6
	struct sockaddr_in6	*dst6;
#endif	/* INET6 */
	struct rtentry		*rt;
	int			 ret = 0;

	memset(&ss, 0, sizeof(ss));
	switch (af) {
	case AF_INET:
		dst = (struct sockaddr_in *)&ss;
		dst->sin_family = AF_INET;
		dst->sin_len = sizeof(*dst);
		dst->sin_addr = addr->v4;
		break;
#ifdef INET6
	case AF_INET6:
		dst6 = (struct sockaddr_in6 *)&ss;
		dst6->sin6_family = AF_INET6;
		dst6->sin6_len = sizeof(*dst6);
		dst6->sin6_addr = addr->v6;
		break;
#endif /* INET6 */
	}

	rt = rtalloc(sstosa(&ss), RT_RESOLVE, rtableid);
	if (rt != NULL) {
		if (rt->rt_labelid == aw->v.rtlabel)
			ret = 1;
		rtfree(rt);
	}

	return (ret);
}

/* pf_route() may change pd->m, adjust local copies after calling */
void
pf_route(struct pf_pdesc *pd, struct pf_state *st)
{
	struct mbuf		*m0;
	struct mbuf_list	 ml;
	struct sockaddr_in	*dst, sin;
	struct rtentry		*rt = NULL;
	struct ip		*ip;
	struct ifnet		*ifp = NULL;
	unsigned int		 rtableid;

	if (pd->m->m_pkthdr.pf.routed++ > 3) {
		m_freem(pd->m);
		pd->m = NULL;
		return;
	}

	if (st->rt == PF_DUPTO) {
		if ((m0 = m_dup_pkt(pd->m, max_linkhdr, M_NOWAIT)) == NULL)
			return;
	} else {
		if ((st->rt == PF_REPLYTO) == (st->direction == pd->dir))
			return;
		m0 = pd->m;
		pd->m = NULL;
	}

	if (m0->m_len < sizeof(struct ip)) {
		DPFPRINTF(LOG_ERR,
		    "%s: m0->m_len < sizeof(struct ip)", __func__);
		goto bad;
	}

	ip = mtod(m0, struct ip *);

	if (pd->dir == PF_IN) {
		if (ip->ip_ttl <= IPTTLDEC) {
			if (st->rt != PF_DUPTO) {
				pf_send_icmp(m0, ICMP_TIMXCEED,
				    ICMP_TIMXCEED_INTRANS, 0,
				    pd->af, st->rule.ptr, pd->rdomain);
			}
			goto bad;
		}
		ip->ip_ttl -= IPTTLDEC;
	}

	memset(&sin, 0, sizeof(sin));
	dst = &sin;
	dst->sin_family = AF_INET;
	dst->sin_len = sizeof(*dst);
	dst->sin_addr = st->rt_addr.v4;
	rtableid = m0->m_pkthdr.ph_rtableid;

	rt = rtalloc_mpath(sintosa(dst), &ip->ip_src.s_addr, rtableid);
	if (!rtisvalid(rt)) {
		if (st->rt != PF_DUPTO) {
			pf_send_icmp(m0, ICMP_UNREACH, ICMP_UNREACH_HOST,
			    0, pd->af, st->rule.ptr, pd->rdomain);
		}
		ipstat_inc(ips_noroute);
		goto bad;
	}

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		goto bad;

	/* A locally generated packet may have invalid source address. */
	if ((ntohl(ip->ip_src.s_addr) >> IN_CLASSA_NSHIFT) == IN_LOOPBACKNET &&
	    (ifp->if_flags & IFF_LOOPBACK) == 0)
		ip->ip_src = ifatoia(rt->rt_ifa)->ia_addr.sin_addr;

	if (st->rt != PF_DUPTO && pd->dir == PF_IN) {
		if (pf_test(AF_INET, PF_OUT, ifp, &m0) != PF_PASS)
			goto bad;
		else if (m0 == NULL)
			goto done;
		if (m0->m_len < sizeof(struct ip)) {
			DPFPRINTF(LOG_ERR,
			    "%s: m0->m_len < sizeof(struct ip)", __func__);
			goto bad;
		}
		ip = mtod(m0, struct ip *);
	}

	if (if_output_tso(ifp, &m0, sintosa(dst), rt, ifp->if_mtu) ||
	    m0 == NULL)
		goto done;

	/*
	 * Too large for interface; fragment if possible.
	 * Must be able to put at least 8 bytes per fragment.
	 */
	if (ip->ip_off & htons(IP_DF)) {
		ipstat_inc(ips_cantfrag);
		if (st->rt != PF_DUPTO)
			pf_send_icmp(m0, ICMP_UNREACH, ICMP_UNREACH_NEEDFRAG,
			    ifp->if_mtu, pd->af, st->rule.ptr, pd->rdomain);
		goto bad;
	}

	if (ip_fragment(m0, &ml, ifp, ifp->if_mtu) ||
	    if_output_ml(ifp, &ml, sintosa(dst), rt))
		goto done;
	ipstat_inc(ips_fragmented);

done:
	if_put(ifp);
	rtfree(rt);
	return;

bad:
	m_freem(m0);
	goto done;
}

#ifdef INET6
/* pf_route6() may change pd->m, adjust local copies after calling */
void
pf_route6(struct pf_pdesc *pd, struct pf_state *st)
{
	struct mbuf		*m0;
	struct sockaddr_in6	*dst, sin6;
	struct rtentry		*rt = NULL;
	struct ip6_hdr		*ip6;
	struct ifnet		*ifp = NULL;
	struct m_tag		*mtag;
	unsigned int		 rtableid;

	if (pd->m->m_pkthdr.pf.routed++ > 3) {
		m_freem(pd->m);
		pd->m = NULL;
		return;
	}

	if (st->rt == PF_DUPTO) {
		if ((m0 = m_dup_pkt(pd->m, max_linkhdr, M_NOWAIT)) == NULL)
			return;
	} else {
		if ((st->rt == PF_REPLYTO) == (st->direction == pd->dir))
			return;
		m0 = pd->m;
		pd->m = NULL;
	}

	if (m0->m_len < sizeof(struct ip6_hdr)) {
		DPFPRINTF(LOG_ERR,
		    "%s: m0->m_len < sizeof(struct ip6_hdr)", __func__);
		goto bad;
	}
	ip6 = mtod(m0, struct ip6_hdr *);

	if (pd->dir == PF_IN) {
		if (ip6->ip6_hlim <= IPV6_HLIMDEC) {
			if (st->rt != PF_DUPTO) {
				pf_send_icmp(m0, ICMP6_TIME_EXCEEDED,
				    ICMP6_TIME_EXCEED_TRANSIT, 0,
				    pd->af, st->rule.ptr, pd->rdomain);
			}
			goto bad;
		}
		ip6->ip6_hlim -= IPV6_HLIMDEC;
	}

	memset(&sin6, 0, sizeof(sin6));
	dst = &sin6;
	dst->sin6_family = AF_INET6;
	dst->sin6_len = sizeof(*dst);
	dst->sin6_addr = st->rt_addr.v6;
	rtableid = m0->m_pkthdr.ph_rtableid;

	rt = rtalloc_mpath(sin6tosa(dst), &ip6->ip6_src.s6_addr32[0],
	    rtableid);
	if (!rtisvalid(rt)) {
		if (st->rt != PF_DUPTO) {
			pf_send_icmp(m0, ICMP6_DST_UNREACH,
			    ICMP6_DST_UNREACH_NOROUTE, 0,
			    pd->af, st->rule.ptr, pd->rdomain);
		}
		ip6stat_inc(ip6s_noroute);
		goto bad;
	}

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL)
		goto bad;

	/* A locally generated packet may have invalid source address. */
	if (IN6_IS_ADDR_LOOPBACK(&ip6->ip6_src) &&
	    (ifp->if_flags & IFF_LOOPBACK) == 0)
		ip6->ip6_src = ifatoia6(rt->rt_ifa)->ia_addr.sin6_addr;

	if (st->rt != PF_DUPTO && pd->dir == PF_IN) {
		if (pf_test(AF_INET6, PF_OUT, ifp, &m0) != PF_PASS)
			goto bad;
		else if (m0 == NULL)
			goto done;
		if (m0->m_len < sizeof(struct ip6_hdr)) {
			DPFPRINTF(LOG_ERR,
			    "%s: m0->m_len < sizeof(struct ip6_hdr)", __func__);
			goto bad;
		}
	}

	/*
	 * If packet has been reassembled by PF earlier, we have to
	 * use pf_refragment6() here to turn it back to fragments.
	 */
	if ((mtag = m_tag_find(m0, PACKET_TAG_PF_REASSEMBLED, NULL))) {
		(void) pf_refragment6(&m0, mtag, dst, ifp, rt);
		goto done;
	}

	if (if_output_tso(ifp, &m0, sin6tosa(dst), rt, ifp->if_mtu) ||
	    m0 == NULL)
		goto done;

	ip6stat_inc(ip6s_cantfrag);
	if (st->rt != PF_DUPTO)
		pf_send_icmp(m0, ICMP6_PACKET_TOO_BIG, 0,
		    ifp->if_mtu, pd->af, st->rule.ptr, pd->rdomain);
	goto bad;

done:
	if_put(ifp);
	rtfree(rt);
	return;

bad:
	m_freem(m0);
	goto done;
}
#endif /* INET6 */

/*
 * check TCP checksum and set mbuf flag
 *   off is the offset where the protocol header starts
 *   len is the total length of protocol header plus payload
 * returns 0 when the checksum is valid, otherwise returns 1.
 * if the _OUT flag is set the checksum isn't done yet, consider these ok
 */
int
pf_check_tcp_cksum(struct mbuf *m, int off, int len, sa_family_t af)
{
	u_int16_t sum;

	if (m->m_pkthdr.csum_flags &
	    (M_TCP_CSUM_IN_OK | M_TCP_CSUM_OUT)) {
		return (0);
	}
	if (m->m_pkthdr.csum_flags & M_TCP_CSUM_IN_BAD ||
	    off < sizeof(struct ip) ||
	    m->m_pkthdr.len < off + len) {
		return (1);
	}

	/* need to do it in software */
	tcpstat_inc(tcps_inswcsum);

	switch (af) {
	case AF_INET:
		if (m->m_len < sizeof(struct ip))
			return (1);

		sum = in4_cksum(m, IPPROTO_TCP, off, len);
		break;
#ifdef INET6
	case AF_INET6:
		if (m->m_len < sizeof(struct ip6_hdr))
			return (1);

		sum = in6_cksum(m, IPPROTO_TCP, off, len);
		break;
#endif /* INET6 */
	default:
		unhandled_af(af);
	}
	if (sum) {
		tcpstat_inc(tcps_rcvbadsum);
		m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_BAD;
		return (1);
	}

	m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK;
	return (0);
}

struct pf_divert *
pf_find_divert(struct mbuf *m)
{
	struct m_tag    *mtag;

	if ((mtag = m_tag_find(m, PACKET_TAG_PF_DIVERT, NULL)) == NULL)
		return (NULL);

	return ((struct pf_divert *)(mtag + 1));
}

struct pf_divert *
pf_get_divert(struct mbuf *m)
{
	struct m_tag    *mtag;

	if ((mtag = m_tag_find(m, PACKET_TAG_PF_DIVERT, NULL)) == NULL) {
		mtag = m_tag_get(PACKET_TAG_PF_DIVERT, sizeof(struct pf_divert),
		    M_NOWAIT);
		if (mtag == NULL)
			return (NULL);
		memset(mtag + 1, 0, sizeof(struct pf_divert));
		m_tag_prepend(m, mtag);
	}

	return ((struct pf_divert *)(mtag + 1));
}

int
pf_walk_option(struct pf_pdesc *pd, struct ip *h, int off, int end,
    u_short *reason)
{
	uint8_t	type, length, opts[15 * 4 - sizeof(struct ip)];

	/* IP header in payload of ICMP packet may be too short */
	if (pd->m->m_pkthdr.len < end) {
		DPFPRINTF(LOG_NOTICE, "IP option too short");
		REASON_SET(reason, PFRES_SHORT);
		return (PF_DROP);
	}

	KASSERT(end - off <= sizeof(opts));
	m_copydata(pd->m, off, end - off, opts);
	end -= off;
	off = 0;

	while (off < end) {
		type = opts[off];
		if (type == IPOPT_EOL)
			break;
		if (type == IPOPT_NOP) {
			off++;
			continue;
		}
		if (off + 2 > end) {
			DPFPRINTF(LOG_NOTICE, "IP length opt");
			REASON_SET(reason, PFRES_IPOPTIONS);
			return (PF_DROP);
		}
		length = opts[off + 1];
		if (length < 2) {
			DPFPRINTF(LOG_NOTICE, "IP short opt");
			REASON_SET(reason, PFRES_IPOPTIONS);
			return (PF_DROP);
		}
		if (off + length > end) {
			DPFPRINTF(LOG_NOTICE, "IP long opt");
			REASON_SET(reason, PFRES_IPOPTIONS);
			return (PF_DROP);
		}
		switch (type) {
		case IPOPT_RA:
			SET(pd->badopts, PF_OPT_ROUTER_ALERT);
			break;
		default:
			SET(pd->badopts, PF_OPT_OTHER);
			break;
		}
		off += length;
	}

	return (PF_PASS);
}

int
pf_walk_header(struct pf_pdesc *pd, struct ip *h, u_short *reason)
{
	struct ip6_ext		 ext;
	u_int32_t		 hlen, end;
	int			 hdr_cnt;

	hlen = h->ip_hl << 2;
	if (hlen < sizeof(struct ip) || hlen > ntohs(h->ip_len)) {
		REASON_SET(reason, PFRES_SHORT);
		return (PF_DROP);
	}
	if (hlen != sizeof(struct ip)) {
		if (pf_walk_option(pd, h, pd->off + sizeof(struct ip),
		    pd->off + hlen, reason) != PF_PASS)
			return (PF_DROP);
		/* header options which contain only padding is fishy */
		if (pd->badopts == 0)
			SET(pd->badopts, PF_OPT_OTHER);
	}
	end = pd->off + ntohs(h->ip_len);
	pd->off += hlen;
	pd->proto = h->ip_p;
	/* IGMP packets have router alert options, allow them */
	if (pd->proto == IPPROTO_IGMP) {
		/*
		 * According to RFC 1112 ttl must be set to 1 in all IGMP
		 * packets sent to 224.0.0.1
		 */
		if ((h->ip_ttl != 1) &&
		    (h->ip_dst.s_addr == INADDR_ALLHOSTS_GROUP)) {
			DPFPRINTF(LOG_NOTICE, "Invalid IGMP");
			REASON_SET(reason, PFRES_IPOPTIONS);
			return (PF_DROP);
		}
		CLR(pd->badopts, PF_OPT_ROUTER_ALERT);
	}
	/* stop walking over non initial fragments */
	if ((h->ip_off & htons(IP_OFFMASK)) != 0)
		return (PF_PASS);

	for (hdr_cnt = 0; hdr_cnt < pf_hdr_limit; hdr_cnt++) {
		switch (pd->proto) {
		case IPPROTO_AH:
			/* fragments may be short */
			if ((h->ip_off & htons(IP_MF | IP_OFFMASK)) != 0 &&
			    end < pd->off + sizeof(ext))
				return (PF_PASS);
			if (!pf_pull_hdr(pd->m, pd->off, &ext, sizeof(ext),
			    reason, AF_INET)) {
				DPFPRINTF(LOG_NOTICE, "IP short exthdr");
				return (PF_DROP);
			}
			pd->off += (ext.ip6e_len + 2) * 4;
			pd->proto = ext.ip6e_nxt;
			break;
		default:
			return (PF_PASS);
		}
	}
	DPFPRINTF(LOG_NOTICE, "IPv4 nested authentication header limit");
	REASON_SET(reason, PFRES_IPOPTIONS);
	return (PF_DROP);
}

#ifdef INET6
int
pf_walk_option6(struct pf_pdesc *pd, struct ip6_hdr *h, int off, int end,
    u_short *reason)
{
	struct ip6_opt		 opt;
	struct ip6_opt_jumbo	 jumbo;

	while (off < end) {
		if (!pf_pull_hdr(pd->m, off, &opt.ip6o_type,
		    sizeof(opt.ip6o_type), reason, AF_INET6)) {
			DPFPRINTF(LOG_NOTICE, "IPv6 short opt type");
			return (PF_DROP);
		}
		if (opt.ip6o_type == IP6OPT_PAD1) {
			off++;
			continue;
		}
		if (!pf_pull_hdr(pd->m, off, &opt, sizeof(opt),
		    reason, AF_INET6)) {
			DPFPRINTF(LOG_NOTICE, "IPv6 short opt");
			return (PF_DROP);
		}
		if (off + sizeof(opt) + opt.ip6o_len > end) {
			DPFPRINTF(LOG_NOTICE, "IPv6 long opt");
			REASON_SET(reason, PFRES_IPOPTIONS);
			return (PF_DROP);
		}
		switch (opt.ip6o_type) {
		case IP6OPT_PADN:
			break;
		case IP6OPT_JUMBO:
			SET(pd->badopts, PF_OPT_JUMBO);
			if (pd->jumbolen != 0) {
				DPFPRINTF(LOG_NOTICE, "IPv6 multiple jumbo");
				REASON_SET(reason, PFRES_IPOPTIONS);
				return (PF_DROP);
			}
			if (ntohs(h->ip6_plen) != 0) {
				DPFPRINTF(LOG_NOTICE, "IPv6 bad jumbo plen");
				REASON_SET(reason, PFRES_IPOPTIONS);
				return (PF_DROP);
			}
			if (!pf_pull_hdr(pd->m, off, &jumbo, sizeof(jumbo),
			    reason, AF_INET6)) {
				DPFPRINTF(LOG_NOTICE, "IPv6 short jumbo");
				return (PF_DROP);
			}
			memcpy(&pd->jumbolen, jumbo.ip6oj_jumbo_len,
			    sizeof(pd->jumbolen));
			pd->jumbolen = ntohl(pd->jumbolen);
			if (pd->jumbolen < IPV6_MAXPACKET) {
				DPFPRINTF(LOG_NOTICE, "IPv6 short jumbolen");
				REASON_SET(reason, PFRES_IPOPTIONS);
				return (PF_DROP);
			}
			break;
		case IP6OPT_ROUTER_ALERT:
			SET(pd->badopts, PF_OPT_ROUTER_ALERT);
			break;
		default:
			SET(pd->badopts, PF_OPT_OTHER);
			break;
		}
		off += sizeof(opt) + opt.ip6o_len;
	}

	return (PF_PASS);
}

int
pf_walk_header6(struct pf_pdesc *pd, struct ip6_hdr *h, u_short *reason)
{
	struct ip6_frag		 frag;
	struct ip6_ext		 ext;
	struct icmp6_hdr	 icmp6;
	struct ip6_rthdr	 rthdr;
	u_int32_t		 end;
	int			 hdr_cnt, fraghdr_cnt = 0, rthdr_cnt = 0;

	pd->off += sizeof(struct ip6_hdr);
	end = pd->off + ntohs(h->ip6_plen);
	pd->fragoff = pd->extoff = pd->jumbolen = 0;
	pd->proto = h->ip6_nxt;

	for (hdr_cnt = 0; hdr_cnt < pf_hdr_limit; hdr_cnt++) {
		switch (pd->proto) {
		case IPPROTO_ROUTING:
		case IPPROTO_DSTOPTS:
			SET(pd->badopts, PF_OPT_OTHER);
			break;
		case IPPROTO_HOPOPTS:
			if (!pf_pull_hdr(pd->m, pd->off, &ext, sizeof(ext),
			    reason, AF_INET6)) {
				DPFPRINTF(LOG_NOTICE, "IPv6 short exthdr");
				return (PF_DROP);
			}
			if (pf_walk_option6(pd, h, pd->off + sizeof(ext),
			    pd->off + (ext.ip6e_len + 1) * 8, reason)
			    != PF_PASS)
				return (PF_DROP);
			/* option header which contains only padding is fishy */
			if (pd->badopts == 0)
				SET(pd->badopts, PF_OPT_OTHER);
			break;
		}
		switch (pd->proto) {
		case IPPROTO_FRAGMENT:
			if (fraghdr_cnt++) {
				DPFPRINTF(LOG_NOTICE, "IPv6 multiple fragment");
				REASON_SET(reason, PFRES_FRAG);
				return (PF_DROP);
			}
			/* jumbo payload packets cannot be fragmented */
			if (pd->jumbolen != 0) {
				DPFPRINTF(LOG_NOTICE, "IPv6 fragmented jumbo");
				REASON_SET(reason, PFRES_FRAG);
				return (PF_DROP);
			}
			if (!pf_pull_hdr(pd->m, pd->off, &frag, sizeof(frag),
			    reason, AF_INET6)) {
				DPFPRINTF(LOG_NOTICE, "IPv6 short fragment");
				return (PF_DROP);
			}
			/* stop walking over non initial fragments */
			if (ntohs((frag.ip6f_offlg & IP6F_OFF_MASK)) != 0) {
				pd->fragoff = pd->off;
				return (PF_PASS);
			}
			/* RFC6946:  reassemble only non atomic fragments */
			if (frag.ip6f_offlg & IP6F_MORE_FRAG)
				pd->fragoff = pd->off;
			pd->off += sizeof(frag);
			pd->proto = frag.ip6f_nxt;
			break;
		case IPPROTO_ROUTING:
			if (rthdr_cnt++) {
				DPFPRINTF(LOG_NOTICE, "IPv6 multiple rthdr");
				REASON_SET(reason, PFRES_IPOPTIONS);
				return (PF_DROP);
			}
			/* fragments may be short */
			if (pd->fragoff != 0 && end < pd->off + sizeof(rthdr)) {
				pd->off = pd->fragoff;
				pd->proto = IPPROTO_FRAGMENT;
				return (PF_PASS);
			}
			if (!pf_pull_hdr(pd->m, pd->off, &rthdr, sizeof(rthdr),
			    reason, AF_INET6)) {
				DPFPRINTF(LOG_NOTICE, "IPv6 short rthdr");
				return (PF_DROP);
			}
			if (rthdr.ip6r_type == IPV6_RTHDR_TYPE_0) {
				DPFPRINTF(LOG_NOTICE, "IPv6 rthdr0");
				REASON_SET(reason, PFRES_IPOPTIONS);
				return (PF_DROP);
			}
			/* FALLTHROUGH */
		case IPPROTO_HOPOPTS:
			/* RFC2460 4.1:  Hop-by-Hop only after IPv6 header */
			if (pd->proto == IPPROTO_HOPOPTS && hdr_cnt > 0) {
				DPFPRINTF(LOG_NOTICE, "IPv6 hopopts not first");
				REASON_SET(reason, PFRES_IPOPTIONS);
				return (PF_DROP);
			}
			/* FALLTHROUGH */
		case IPPROTO_AH:
		case IPPROTO_DSTOPTS:
			/* fragments may be short */
			if (pd->fragoff != 0 && end < pd->off + sizeof(ext)) {
				pd->off = pd->fragoff;
				pd->proto = IPPROTO_FRAGMENT;
				return (PF_PASS);
			}
			if (!pf_pull_hdr(pd->m, pd->off, &ext, sizeof(ext),
			    reason, AF_INET6)) {
				DPFPRINTF(LOG_NOTICE, "IPv6 short exthdr");
				return (PF_DROP);
			}
			/* reassembly needs the ext header before the frag */
			if (pd->fragoff == 0)
				pd->extoff = pd->off;
			if (pd->proto == IPPROTO_HOPOPTS && pd->fragoff == 0 &&
			    ntohs(h->ip6_plen) == 0 && pd->jumbolen != 0) {
				DPFPRINTF(LOG_NOTICE, "IPv6 missing jumbo");
				REASON_SET(reason, PFRES_IPOPTIONS);
				return (PF_DROP);
			}
			if (pd->proto == IPPROTO_AH)
				pd->off += (ext.ip6e_len + 2) * 4;
			else
				pd->off += (ext.ip6e_len + 1) * 8;
			pd->proto = ext.ip6e_nxt;
			break;
		case IPPROTO_ICMPV6:
			/* fragments may be short, ignore inner header then */
			if (pd->fragoff != 0 && end < pd->off + sizeof(icmp6)) {
				pd->off = pd->fragoff;
				pd->proto = IPPROTO_FRAGMENT;
				return (PF_PASS);
			}
			if (!pf_pull_hdr(pd->m, pd->off, &icmp6, sizeof(icmp6),
			    reason, AF_INET6)) {
				DPFPRINTF(LOG_NOTICE, "IPv6 short icmp6hdr");
				return (PF_DROP);
			}
			/* ICMP multicast packets have router alert options */
			switch (icmp6.icmp6_type) {
			case MLD_LISTENER_QUERY:
			case MLD_LISTENER_REPORT:
			case MLD_LISTENER_DONE:
			case MLDV2_LISTENER_REPORT:
				/*
				 * According to RFC 2710 all MLD messages are
				 * sent with hop-limit (ttl) set to 1, and link
				 * local source address.  If either one is
				 * missing then MLD message is invalid and
				 * should be discarded.
				 */
				if ((h->ip6_hlim != 1) ||
				    !IN6_IS_ADDR_LINKLOCAL(&h->ip6_src)) {
					DPFPRINTF(LOG_NOTICE, "Invalid MLD");
					REASON_SET(reason, PFRES_IPOPTIONS);
					return (PF_DROP);
				}
				CLR(pd->badopts, PF_OPT_ROUTER_ALERT);
				break;
			}
			return (PF_PASS);
		case IPPROTO_TCP:
		case IPPROTO_UDP:
			/* fragments may be short, ignore inner header then */
			if (pd->fragoff != 0 && end < pd->off +
			    (pd->proto == IPPROTO_TCP ? sizeof(struct tcphdr) :
			    pd->proto == IPPROTO_UDP ? sizeof(struct udphdr) :
			    sizeof(struct icmp6_hdr))) {
				pd->off = pd->fragoff;
				pd->proto = IPPROTO_FRAGMENT;
			}
			/* FALLTHROUGH */
		default:
			return (PF_PASS);
		}
	}
	DPFPRINTF(LOG_NOTICE, "IPv6 nested extension header limit");
	REASON_SET(reason, PFRES_IPOPTIONS);
	return (PF_DROP);
}
#endif /* INET6 */

u_int16_t
pf_pkt_hash(sa_family_t af, uint8_t proto,
    const struct pf_addr *src, const struct pf_addr *dst,
    uint16_t sport, uint16_t dport)
{
	uint32_t hash;

	hash = src->addr32[0] ^ dst->addr32[0];
#ifdef INET6
	if (af == AF_INET6) {
		hash ^= src->addr32[1] ^ dst->addr32[1];
		hash ^= src->addr32[2] ^ dst->addr32[2];
		hash ^= src->addr32[3] ^ dst->addr32[3];
	}
#endif

	switch (proto) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		hash ^= sport ^ dport;
		break;
	}

	return stoeplitz_n32(hash);
}

int
pf_setup_pdesc(struct pf_pdesc *pd, sa_family_t af, int dir,
    struct pfi_kif *kif, struct mbuf *m, u_short *reason)
{
	memset(pd, 0, sizeof(*pd));
	pd->dir = dir;
	pd->kif = kif;		/* kif is NULL when called by pflog */
	pd->m = m;
	pd->sidx = (dir == PF_IN) ? 0 : 1;
	pd->didx = (dir == PF_IN) ? 1 : 0;
	pd->af = pd->naf = af;
	pd->rdomain = rtable_l2(pd->m->m_pkthdr.ph_rtableid);

	switch (pd->af) {
	case AF_INET: {
		struct ip	*h;

		/* Check for illegal packets */
		if (pd->m->m_pkthdr.len < (int)sizeof(struct ip)) {
			REASON_SET(reason, PFRES_SHORT);
			return (PF_DROP);
		}

		h = mtod(pd->m, struct ip *);
		if (pd->m->m_pkthdr.len < ntohs(h->ip_len)) {
			REASON_SET(reason, PFRES_SHORT);
			return (PF_DROP);
		}

		if (pf_walk_header(pd, h, reason) != PF_PASS)
			return (PF_DROP);

		pd->src = (struct pf_addr *)&h->ip_src;
		pd->dst = (struct pf_addr *)&h->ip_dst;
		pd->tot_len = ntohs(h->ip_len);
		pd->tos = h->ip_tos & ~IPTOS_ECN_MASK;
		pd->ttl = h->ip_ttl;
		pd->virtual_proto = (h->ip_off & htons(IP_MF | IP_OFFMASK)) ?
		     PF_VPROTO_FRAGMENT : pd->proto;

		break;
	}
#ifdef INET6
	case AF_INET6: {
		struct ip6_hdr	*h;

		/* Check for illegal packets */
		if (pd->m->m_pkthdr.len < (int)sizeof(struct ip6_hdr)) {
			REASON_SET(reason, PFRES_SHORT);
			return (PF_DROP);
		}

		h = mtod(pd->m, struct ip6_hdr *);
		if (pd->m->m_pkthdr.len <
		    sizeof(struct ip6_hdr) + ntohs(h->ip6_plen)) {
			REASON_SET(reason, PFRES_SHORT);
			return (PF_DROP);
		}

		if (pf_walk_header6(pd, h, reason) != PF_PASS)
			return (PF_DROP);

#if 1
		/*
		 * we do not support jumbogram yet.  if we keep going, zero
		 * ip6_plen will do something bad, so drop the packet for now.
		 */
		if (pd->jumbolen != 0) {
			REASON_SET(reason, PFRES_NORM);
			return (PF_DROP);
		}
#endif	/* 1 */

		pd->src = (struct pf_addr *)&h->ip6_src;
		pd->dst = (struct pf_addr *)&h->ip6_dst;
		pd->tot_len = ntohs(h->ip6_plen) + sizeof(struct ip6_hdr);
		pd->tos = (ntohl(h->ip6_flow) & 0x0fc00000) >> 20;
		pd->ttl = h->ip6_hlim;
		pd->virtual_proto = (pd->fragoff != 0) ?
			PF_VPROTO_FRAGMENT : pd->proto;

		break;
	}
#endif /* INET6 */
	default:
		panic("pf_setup_pdesc called with illegal af %u", pd->af);

	}

	pf_addrcpy(&pd->nsaddr, pd->src, pd->af);
	pf_addrcpy(&pd->ndaddr, pd->dst, pd->af);

	switch (pd->virtual_proto) {
	case IPPROTO_TCP: {
		struct tcphdr	*th = &pd->hdr.tcp;

		if (!pf_pull_hdr(pd->m, pd->off, th, sizeof(*th),
		    reason, pd->af))
			return (PF_DROP);
		pd->hdrlen = sizeof(*th);
		if (th->th_dport == 0 ||
		    pd->off + (th->th_off << 2) > pd->tot_len ||
		    (th->th_off << 2) < sizeof(struct tcphdr)) {
			REASON_SET(reason, PFRES_SHORT);
			return (PF_DROP);
		}
		pd->p_len = pd->tot_len - pd->off - (th->th_off << 2);
		pd->sport = &th->th_sport;
		pd->dport = &th->th_dport;
		pd->pcksum = &th->th_sum;
		break;
	}
	case IPPROTO_UDP: {
		struct udphdr	*uh = &pd->hdr.udp;

		if (!pf_pull_hdr(pd->m, pd->off, uh, sizeof(*uh),
		    reason, pd->af))
			return (PF_DROP);
		pd->hdrlen = sizeof(*uh);
		if (uh->uh_dport == 0 ||
		    pd->off + ntohs(uh->uh_ulen) > pd->tot_len ||
		    ntohs(uh->uh_ulen) < sizeof(struct udphdr)) {
			REASON_SET(reason, PFRES_SHORT);
			return (PF_DROP);
		}
		pd->sport = &uh->uh_sport;
		pd->dport = &uh->uh_dport;
		pd->pcksum = &uh->uh_sum;
		break;
	}
	case IPPROTO_ICMP: {
		if (!pf_pull_hdr(pd->m, pd->off, &pd->hdr.icmp, ICMP_MINLEN,
		    reason, pd->af))
			return (PF_DROP);
		pd->hdrlen = ICMP_MINLEN;
		if (pd->off + pd->hdrlen > pd->tot_len) {
			REASON_SET(reason, PFRES_SHORT);
			return (PF_DROP);
		}
		pd->pcksum = &pd->hdr.icmp.icmp_cksum;
		break;
	}
#ifdef INET6
	case IPPROTO_ICMPV6: {
		size_t	icmp_hlen = sizeof(struct icmp6_hdr);

		if (!pf_pull_hdr(pd->m, pd->off, &pd->hdr.icmp6, icmp_hlen,
		    reason, pd->af))
			return (PF_DROP);
		/* ICMP headers we look further into to match state */
		switch (pd->hdr.icmp6.icmp6_type) {
		case MLD_LISTENER_QUERY:
		case MLD_LISTENER_REPORT:
			icmp_hlen = sizeof(struct mld_hdr);
			break;
		case ND_NEIGHBOR_SOLICIT:
		case ND_NEIGHBOR_ADVERT:
			icmp_hlen = sizeof(struct nd_neighbor_solicit);
			/* FALLTHROUGH */
		case ND_ROUTER_SOLICIT:
		case ND_ROUTER_ADVERT:
		case ND_REDIRECT:
			if (pd->ttl != 255) {
				REASON_SET(reason, PFRES_NORM);
				return (PF_DROP);
			}
			break;
		}
		if (icmp_hlen > sizeof(struct icmp6_hdr) &&
		    !pf_pull_hdr(pd->m, pd->off, &pd->hdr.icmp6, icmp_hlen,
		    reason, pd->af))
			return (PF_DROP);
		pd->hdrlen = icmp_hlen;
		if (pd->off + pd->hdrlen > pd->tot_len) {
			REASON_SET(reason, PFRES_SHORT);
			return (PF_DROP);
		}
		pd->pcksum = &pd->hdr.icmp6.icmp6_cksum;
		break;
	}
#endif	/* INET6 */
	}

	if (pd->sport)
		pd->osport = pd->nsport = *pd->sport;
	if (pd->dport)
		pd->odport = pd->ndport = *pd->dport;

	pd->hash = pf_pkt_hash(pd->af, pd->proto,
	    pd->src, pd->dst, pd->osport, pd->odport);

	return (PF_PASS);
}

void
pf_counters_inc(int action, struct pf_pdesc *pd, struct pf_state *st,
    struct pf_rule *r, struct pf_rule *a)
{
	int dirndx;
	pd->kif->pfik_bytes[pd->af == AF_INET6][pd->dir == PF_OUT]
	    [action != PF_PASS] += pd->tot_len;
	pd->kif->pfik_packets[pd->af == AF_INET6][pd->dir == PF_OUT]
	    [action != PF_PASS]++;

	if (action == PF_PASS || action == PF_AFRT || r->action == PF_DROP) {
		dirndx = (pd->dir == PF_OUT);
		r->packets[dirndx]++;
		r->bytes[dirndx] += pd->tot_len;
		if (a != NULL) {
			a->packets[dirndx]++;
			a->bytes[dirndx] += pd->tot_len;
		}
		if (st != NULL) {
			struct pf_rule_item	*ri;
			struct pf_sn_item	*sni;

			SLIST_FOREACH(sni, &st->src_nodes, next) {
				sni->sn->packets[dirndx]++;
				sni->sn->bytes[dirndx] += pd->tot_len;
			}
			dirndx = (pd->dir == st->direction) ? 0 : 1;
			st->packets[dirndx]++;
			st->bytes[dirndx] += pd->tot_len;

			SLIST_FOREACH(ri, &st->match_rules, entry) {
				ri->r->packets[dirndx]++;
				ri->r->bytes[dirndx] += pd->tot_len;

				if (ri->r->src.addr.type == PF_ADDR_TABLE)
					pfr_update_stats(ri->r->src.addr.p.tbl,
					    &st->key[(st->direction == PF_IN)]->
						addr[(st->direction == PF_OUT)],
					    pd, ri->r->action, ri->r->src.neg);
				if (ri->r->dst.addr.type == PF_ADDR_TABLE)
					pfr_update_stats(ri->r->dst.addr.p.tbl,
					    &st->key[(st->direction == PF_IN)]->
						addr[(st->direction == PF_IN)],
					    pd, ri->r->action, ri->r->dst.neg);
			}
		}
		if (r->src.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(r->src.addr.p.tbl,
			    (st == NULL) ? pd->src :
			    &st->key[(st->direction == PF_IN)]->
				addr[(st->direction == PF_OUT)],
			    pd, r->action, r->src.neg);
		if (r->dst.addr.type == PF_ADDR_TABLE)
			pfr_update_stats(r->dst.addr.p.tbl,
			    (st == NULL) ? pd->dst :
			    &st->key[(st->direction == PF_IN)]->
				addr[(st->direction == PF_IN)],
			    pd, r->action, r->dst.neg);
	}
}

int
pf_test(sa_family_t af, int fwdir, struct ifnet *ifp, struct mbuf **m0)
{
#if NCARP > 0
	struct ifnet		*ifp0;
#endif
	struct pfi_kif		*kif;
	u_short			 action, reason = 0;
	struct pf_rule		*a = NULL, *r = &pf_default_rule;
	struct pf_state		*st = NULL;
	struct pf_state_key_cmp	 key;
	struct pf_ruleset	*ruleset = NULL;
	struct pf_pdesc		 pd;
	int			 dir = (fwdir == PF_FWD) ? PF_OUT : fwdir;
	u_int32_t		 qid, pqid = 0;
	int			 have_pf_lock = 0;

	if (!pf_status.running)
		return (PF_PASS);

#if NCARP > 0
	if (ifp->if_type == IFT_CARP &&
		(ifp0 = if_get(ifp->if_carpdevidx)) != NULL) {
		kif = (struct pfi_kif *)ifp0->if_pf_kif;
		if_put(ifp0);
	} else
#endif /* NCARP */
		kif = (struct pfi_kif *)ifp->if_pf_kif;

	if (kif == NULL) {
		DPFPRINTF(LOG_ERR,
		    "%s: kif == NULL, if_xname %s", __func__, ifp->if_xname);
		return (PF_DROP);
	}
	if (kif->pfik_flags & PFI_IFLAG_SKIP)
		return (PF_PASS);

#ifdef DIAGNOSTIC
	if (((*m0)->m_flags & M_PKTHDR) == 0)
		panic("non-M_PKTHDR is passed to pf_test");
#endif /* DIAGNOSTIC */

	if ((*m0)->m_pkthdr.pf.flags & PF_TAG_GENERATED)
		return (PF_PASS);

	if ((*m0)->m_pkthdr.pf.flags & PF_TAG_DIVERTED_PACKET) {
		(*m0)->m_pkthdr.pf.flags &= ~PF_TAG_DIVERTED_PACKET;
		return (PF_PASS);
	}

	if ((*m0)->m_pkthdr.pf.flags & PF_TAG_REFRAGMENTED) {
		(*m0)->m_pkthdr.pf.flags &= ~PF_TAG_REFRAGMENTED;
		return (PF_PASS);
	}

	action = pf_setup_pdesc(&pd, af, dir, kif, *m0, &reason);
	if (action != PF_PASS) {
#if NPFLOG > 0
		pd.pflog |= PF_LOG_FORCE;
#endif	/* NPFLOG > 0 */
		goto done;
	}

	/* packet normalization and reassembly */
	switch (pd.af) {
	case AF_INET:
		action = pf_normalize_ip(&pd, &reason);
		break;
#ifdef INET6
	case AF_INET6:
		action = pf_normalize_ip6(&pd, &reason);
		break;
#endif	/* INET6 */
	}
	*m0 = pd.m;
	/* if packet sits in reassembly queue, return without error */
	if (pd.m == NULL)
		return PF_PASS;

	if (action != PF_PASS) {
#if NPFLOG > 0
		pd.pflog |= PF_LOG_FORCE;
#endif	/* NPFLOG > 0 */
		goto done;
	}

	/* if packet has been reassembled, update packet description */
	if (pf_status.reass && pd.virtual_proto == PF_VPROTO_FRAGMENT) {
		action = pf_setup_pdesc(&pd, af, dir, kif, pd.m, &reason);
		if (action != PF_PASS) {
#if NPFLOG > 0
			pd.pflog |= PF_LOG_FORCE;
#endif	/* NPFLOG > 0 */
			goto done;
		}
	}
	pd.m->m_pkthdr.pf.flags |= PF_TAG_PROCESSED;

	/*
	 * Avoid pcb-lookups from the forwarding path.  They should never
	 * match and would cause MP locking problems.
	 */
	if (fwdir == PF_FWD) {
		pd.lookup.done = -1;
		pd.lookup.uid = -1;
		pd.lookup.gid = -1;
		pd.lookup.pid = NO_PID;
	}

	switch (pd.virtual_proto) {

	case PF_VPROTO_FRAGMENT: {
		/*
		 * handle fragments that aren't reassembled by
		 * normalization
		 */
		PF_LOCK();
		have_pf_lock = 1;
		action = pf_test_rule(&pd, &r, &st, &a, &ruleset, &reason);
		st = pf_state_ref(st);
		if (action != PF_PASS)
			REASON_SET(&reason, PFRES_FRAG);
		break;
	}

	case IPPROTO_ICMP: {
		if (pd.af != AF_INET) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_NORM);
			DPFPRINTF(LOG_NOTICE,
			    "dropping IPv6 packet with ICMPv4 payload");
			break;
		}
		PF_STATE_ENTER_READ();
		action = pf_test_state_icmp(&pd, &st, &reason);
		st = pf_state_ref(st);
		PF_STATE_EXIT_READ();
		if (action == PF_PASS || action == PF_AFRT) {
#if NPFSYNC > 0
			pfsync_update_state(st);
#endif /* NPFSYNC > 0 */
			r = st->rule.ptr;
			a = st->anchor.ptr;
#if NPFLOG > 0
			pd.pflog |= st->log;
#endif	/* NPFLOG > 0 */
		} else if (st == NULL) {
			PF_LOCK();
			have_pf_lock = 1;
			action = pf_test_rule(&pd, &r, &st, &a, &ruleset,
			    &reason);
			st = pf_state_ref(st);
		}
		break;
	}

#ifdef INET6
	case IPPROTO_ICMPV6: {
		if (pd.af != AF_INET6) {
			action = PF_DROP;
			REASON_SET(&reason, PFRES_NORM);
			DPFPRINTF(LOG_NOTICE,
			    "dropping IPv4 packet with ICMPv6 payload");
			break;
		}
		PF_STATE_ENTER_READ();
		action = pf_test_state_icmp(&pd, &st, &reason);
		st = pf_state_ref(st);
		PF_STATE_EXIT_READ();
		if (action == PF_PASS || action == PF_AFRT) {
#if NPFSYNC > 0
			pfsync_update_state(st);
#endif /* NPFSYNC > 0 */
			r = st->rule.ptr;
			a = st->anchor.ptr;
#if NPFLOG > 0
			pd.pflog |= st->log;
#endif	/* NPFLOG > 0 */
		} else if (st == NULL) {
			PF_LOCK();
			have_pf_lock = 1;
			action = pf_test_rule(&pd, &r, &st, &a, &ruleset,
			    &reason);
			st = pf_state_ref(st);
		}
		break;
	}
#endif /* INET6 */

	default:
		if (pd.virtual_proto == IPPROTO_TCP) {
			if (pd.dir == PF_IN && (pd.hdr.tcp.th_flags &
			    (TH_SYN|TH_ACK)) == TH_SYN &&
			    pf_synflood_check(&pd)) {
				PF_LOCK();
				have_pf_lock = 1;
				pf_syncookie_send(&pd, &reason);
				action = PF_DROP;
				break;
			}
			if ((pd.hdr.tcp.th_flags & TH_ACK) && pd.p_len == 0)
				pqid = 1;
			action = pf_normalize_tcp(&pd);
			if (action == PF_DROP)
				break;
		}

		key.af = pd.af;
		key.proto = pd.virtual_proto;
		key.rdomain = pd.rdomain;
		pf_addrcpy(&key.addr[pd.sidx], pd.src, key.af);
		pf_addrcpy(&key.addr[pd.didx], pd.dst, key.af);
		key.port[pd.sidx] = pd.osport;
		key.port[pd.didx] = pd.odport;
		key.hash = pd.hash;

		PF_STATE_ENTER_READ();
		action = pf_find_state(&pd, &key, &st);
		st = pf_state_ref(st);
		PF_STATE_EXIT_READ();

		/* check for syncookies if tcp ack and no active state */
		if (pd.dir == PF_IN && pd.virtual_proto == IPPROTO_TCP &&
		    (st == NULL || (st->src.state >= TCPS_FIN_WAIT_2 &&
		    st->dst.state >= TCPS_FIN_WAIT_2)) &&
		    (pd.hdr.tcp.th_flags & (TH_SYN|TH_ACK|TH_RST)) == TH_ACK &&
		    pf_syncookie_validate(&pd)) {
			struct mbuf	*msyn;
			msyn = pf_syncookie_recreate_syn(&pd, &reason);
			if (msyn) {
				action = pf_test(af, fwdir, ifp, &msyn);
				m_freem(msyn);
				if (action == PF_PASS || action == PF_AFRT) {
					PF_STATE_ENTER_READ();
					pf_state_unref(st);
					action = pf_find_state(&pd, &key, &st);
					st = pf_state_ref(st);
					PF_STATE_EXIT_READ();
					if (st == NULL)
						return (PF_DROP);
					st->src.seqhi = st->dst.seqhi =
					    ntohl(pd.hdr.tcp.th_ack) - 1;
					st->src.seqlo =
					    ntohl(pd.hdr.tcp.th_seq) - 1;
					pf_set_protostate(st, PF_PEER_SRC,
					    PF_TCPS_PROXY_DST);
				}
			} else
				action = PF_DROP;
		}

		if (action == PF_MATCH)
			action = pf_test_state(&pd, &st, &reason);

		if (action == PF_PASS || action == PF_AFRT) {
#if NPFSYNC > 0
			pfsync_update_state(st);
#endif /* NPFSYNC > 0 */
			r = st->rule.ptr;
			a = st->anchor.ptr;
#if NPFLOG > 0
			pd.pflog |= st->log;
#endif	/* NPFLOG > 0 */
		} else if (st == NULL) {
			PF_LOCK();
			have_pf_lock = 1;
			action = pf_test_rule(&pd, &r, &st, &a, &ruleset,
			    &reason);
			st = pf_state_ref(st);
		}

		if (pd.virtual_proto == IPPROTO_TCP) {
			if (st) {
				if (st->max_mss)
					pf_normalize_mss(&pd, st->max_mss);
			} else if (r->max_mss)
				pf_normalize_mss(&pd, r->max_mss);
		}

		break;
	}

	if (have_pf_lock != 0)
		PF_UNLOCK();

	/*
	 * At the moment, we rely on NET_LOCK() to prevent removal of items
	 * we've collected above ('r', 'anchor' and 'ruleset').  They'll have
	 * to be refcounted when NET_LOCK() is gone.
	 */

done:
	if (action != PF_DROP) {
		if (st) {
			/* The non-state case is handled in pf_test_rule() */
			if (action == PF_PASS && pd.badopts != 0 &&
			    !(st->state_flags & PFSTATE_ALLOWOPTS)) {
				action = PF_DROP;
				REASON_SET(&reason, PFRES_IPOPTIONS);
#if NPFLOG > 0
				pd.pflog |= PF_LOG_FORCE;
#endif	/* NPFLOG > 0 */
				DPFPRINTF(LOG_NOTICE, "dropping packet with "
				    "ip/ipv6 options in pf_test()");
			}

			pf_scrub(pd.m, st->state_flags, pd.af, st->min_ttl,
			    st->set_tos);
			pf_tag_packet(pd.m, st->tag, st->rtableid[pd.didx]);
			if (pqid || (pd.tos & IPTOS_LOWDELAY)) {
				qid = st->pqid;
				if (st->state_flags & PFSTATE_SETPRIO) {
					pd.m->m_pkthdr.pf.prio =
					    st->set_prio[1];
				}
			} else {
				qid = st->qid;
				if (st->state_flags & PFSTATE_SETPRIO) {
					pd.m->m_pkthdr.pf.prio =
					    st->set_prio[0];
				}
			}
			pd.m->m_pkthdr.pf.delay = st->delay;
		} else {
			pf_scrub(pd.m, r->scrub_flags, pd.af, r->min_ttl,
			    r->set_tos);
			if (pqid || (pd.tos & IPTOS_LOWDELAY)) {
				qid = r->pqid;
				if (r->scrub_flags & PFSTATE_SETPRIO)
					pd.m->m_pkthdr.pf.prio = r->set_prio[1];
			} else {
				qid = r->qid;
				if (r->scrub_flags & PFSTATE_SETPRIO)
					pd.m->m_pkthdr.pf.prio = r->set_prio[0];
			}
			pd.m->m_pkthdr.pf.delay = r->delay;
		}
	}

	if (action == PF_PASS && qid)
		pd.m->m_pkthdr.pf.qid = qid;
	if (st != NULL) {
		struct mbuf *m = pd.m;
		struct inpcb *inp = m->m_pkthdr.pf.inp;

		if (pd.dir == PF_IN) {
			KASSERT(inp == NULL);
			pf_mbuf_link_state_key(m, st->key[PF_SK_STACK]);
		} else if (pd.dir == PF_OUT)
			pf_state_key_link_inpcb(st->key[PF_SK_STACK], inp);

		if (!ISSET(m->m_pkthdr.csum_flags, M_FLOWID)) {
			m->m_pkthdr.ph_flowid = st->key[PF_SK_WIRE]->hash;
			SET(m->m_pkthdr.csum_flags, M_FLOWID);
		}
	}

	/*
	 * connections redirected to loopback should not match sockets
	 * bound specifically to loopback due to security implications,
	 * see in_pcblookup_listen().
	 */
	if (pd.destchg)
		if ((pd.af == AF_INET && (ntohl(pd.dst->v4.s_addr) >>
		    IN_CLASSA_NSHIFT) == IN_LOOPBACKNET) ||
		    (pd.af == AF_INET6 && IN6_IS_ADDR_LOOPBACK(&pd.dst->v6)))
			pd.m->m_pkthdr.pf.flags |= PF_TAG_TRANSLATE_LOCALHOST;
	/* We need to redo the route lookup on outgoing routes. */
	if (pd.destchg && pd.dir == PF_OUT)
		pd.m->m_pkthdr.pf.flags |= PF_TAG_REROUTE;

	if (pd.dir == PF_IN && action == PF_PASS &&
	    (r->divert.type == PF_DIVERT_TO ||
	    r->divert.type == PF_DIVERT_REPLY)) {
		struct pf_divert *divert;

		if ((divert = pf_get_divert(pd.m))) {
			pd.m->m_pkthdr.pf.flags |= PF_TAG_DIVERTED;
			divert->addr = r->divert.addr;
			divert->port = r->divert.port;
			divert->rdomain = pd.rdomain;
			divert->type = r->divert.type;
		}
	}

	if (action == PF_PASS && r->divert.type == PF_DIVERT_PACKET)
		action = PF_DIVERT;

#if NPFLOG > 0
	if (pd.pflog) {
		struct pf_rule_item	*ri;

		if (pd.pflog & PF_LOG_FORCE || r->log & PF_LOG_ALL)
			pflog_packet(&pd, reason, r, a, ruleset, NULL);
		if (st) {
			SLIST_FOREACH(ri, &st->match_rules, entry)
				if (ri->r->log & PF_LOG_ALL)
					pflog_packet(&pd, reason, ri->r, a,
					    ruleset, NULL);
		}
	}
#endif	/* NPFLOG > 0 */

	pf_counters_inc(action, &pd, st, r, a);

	switch (action) {
	case PF_SYNPROXY_DROP:
		m_freem(pd.m);
		/* FALLTHROUGH */
	case PF_DEFER:
		pd.m = NULL;
		action = PF_PASS;
		break;
	case PF_DIVERT:
		switch (pd.af) {
		case AF_INET:
			divert_packet(pd.m, pd.dir, r->divert.port);
			pd.m = NULL;
			break;
#ifdef INET6
		case AF_INET6:
			divert6_packet(pd.m, pd.dir, r->divert.port);
			pd.m = NULL;
			break;
#endif /* INET6 */
		}
		action = PF_PASS;
		break;
#ifdef INET6
	case PF_AFRT:
		if (pf_translate_af(&pd)) {
			action = PF_DROP;
			goto out;
		}
		pd.m->m_pkthdr.pf.flags |= PF_TAG_GENERATED;
		switch (pd.naf) {
		case AF_INET:
			if (pd.dir == PF_IN) {
				int flags = IP_REDIRECT;

				switch (atomic_load_int(&ip_forwarding)) {
				case 2:
					SET(flags, IP_FORWARDING_IPSEC);
					/* FALLTHROUGH */
				case 1:
					SET(flags, IP_FORWARDING);
					break;
				default:
					ipstat_inc(ips_cantforward);
					action = PF_DROP;
					goto out;
				}
				if (atomic_load_int(&ip_directedbcast))
					SET(flags, IP_ALLOWBROADCAST);
				ip_forward(pd.m, ifp, NULL, flags);
			} else
				ip_output(pd.m, NULL, NULL, 0, NULL, NULL, 0);
			break;
		case AF_INET6:
			if (pd.dir == PF_IN) {
				int flags = IPV6_REDIRECT;

				switch (atomic_load_int(&ip6_forwarding)) {
				case 2:
					SET(flags, IPV6_FORWARDING_IPSEC);
					/* FALLTHROUGH */
				case 1:
					SET(flags, IPV6_FORWARDING);
					break;
				default:
					ip6stat_inc(ip6s_cantforward);
					action = PF_DROP;
					goto out;
				}
				ip6_forward(pd.m, NULL, flags);
			} else
				ip6_output(pd.m, NULL, NULL, 0, NULL, NULL);
			break;
		}
		pd.m = NULL;
		action = PF_PASS;
		break;
#endif /* INET6 */
	case PF_DROP:
		m_freem(pd.m);
		pd.m = NULL;
		break;
	default:
		if (st && st->rt) {
			switch (pd.af) {
			case AF_INET:
				pf_route(&pd, st);
				break;
#ifdef INET6
			case AF_INET6:
				pf_route6(&pd, st);
				break;
#endif /* INET6 */
			}
		}
		break;
	}

#ifdef INET6
	/* if reassembled packet passed, create new fragments */
	if (pf_status.reass && action == PF_PASS && pd.m && fwdir == PF_FWD &&
	    pd.af == AF_INET6) {
		struct m_tag	*mtag;

		if ((mtag = m_tag_find(pd.m, PACKET_TAG_PF_REASSEMBLED, NULL)))
			action = pf_refragment6(&pd.m, mtag, NULL, NULL, NULL);
	}
#endif	/* INET6 */
	if (st && action != PF_DROP) {
		if (!st->if_index_in && dir == PF_IN)
			st->if_index_in = ifp->if_index;
		else if (!st->if_index_out && dir == PF_OUT)
			st->if_index_out = ifp->if_index;
	}

#ifdef INET6
out:
#endif /* INET6 */
	*m0 = pd.m;

	pf_state_unref(st);

	return (action);
}

int
pf_ouraddr(struct mbuf *m)
{
	struct pf_state_key	*sk;

	if (m->m_pkthdr.pf.flags & PF_TAG_DIVERTED)
		return (1);

	sk = m->m_pkthdr.pf.statekey;
	if (sk != NULL) {
		if (READ_ONCE(sk->sk_inp) != NULL)
			return (1);
	}

	return (-1);
}

/*
 * must be called whenever any addressing information such as
 * address, port, protocol has changed
 */
void
pf_pkt_addr_changed(struct mbuf *m)
{
	pf_mbuf_unlink_state_key(m);
	pf_mbuf_unlink_inpcb(m);
}

struct inpcb *
pf_inp_lookup(struct mbuf *m)
{
	struct inpcb *inp = NULL;
	struct pf_state_key *sk = m->m_pkthdr.pf.statekey;

	if (!pf_state_key_isvalid(sk))
		pf_mbuf_unlink_state_key(m);
	else if (READ_ONCE(sk->sk_inp) != NULL) {
		mtx_enter(&pf_inp_mtx);
		inp = in_pcbref(sk->sk_inp);
		mtx_leave(&pf_inp_mtx);
	}

	return (inp);
}

void
pf_inp_link(struct mbuf *m, struct inpcb *inp)
{
	struct pf_state_key *sk = m->m_pkthdr.pf.statekey;

	if (!pf_state_key_isvalid(sk)) {
		pf_mbuf_unlink_state_key(m);
		return;
	}

	/*
	 * we don't need to grab PF-lock here. At worst case we link inp to
	 * state, which might be just being marked as deleted by another
	 * thread.
	 */
	pf_state_key_link_inpcb(sk, inp);

	/* The statekey has finished finding the inp, it is no longer needed. */
	pf_mbuf_unlink_state_key(m);
}

void
pf_inp_unlink(struct inpcb *inp)
{
	struct pf_state_key *sk;

	if (READ_ONCE(inp->inp_pf_sk) == NULL)
		return;

	mtx_enter(&pf_inp_mtx);
	sk = inp->inp_pf_sk;
	if (sk == NULL) {
		mtx_leave(&pf_inp_mtx);
		return;
	}
	KASSERT(sk->sk_inp == inp);
	sk->sk_inp = NULL;
	inp->inp_pf_sk = NULL;
	mtx_leave(&pf_inp_mtx);

	pf_state_key_unref(sk);
	in_pcbunref(inp);
}

void
pf_state_key_link_reverse(struct pf_state_key *sk, struct pf_state_key *skrev)
{
	struct pf_state_key *old_reverse;

	old_reverse = atomic_cas_ptr(&sk->sk_reverse, NULL, skrev);
	if (old_reverse != NULL)
		KASSERT(old_reverse == skrev);
	else {
		pf_state_key_ref(skrev);

		/*
		 * NOTE: if sk == skrev, then KASSERT() below holds true, we
		 * still want to grab a reference in such case, because
		 * pf_state_key_unlink_reverse() does not check whether keys
		 * are identical or not.
		 */
		old_reverse = atomic_cas_ptr(&skrev->sk_reverse, NULL, sk);
		if (old_reverse != NULL)
			KASSERT(old_reverse == sk);

		pf_state_key_ref(sk);
	}
}

#if NPFLOG > 0
void
pf_log_matches(struct pf_pdesc *pd, struct pf_rule *rm, struct pf_rule *am,
    struct pf_ruleset *ruleset, struct pf_rule_slist *matchrules)
{
	struct pf_rule_item	*ri;

	/* if this is the log(matches) rule, packet has been logged already */
	if (rm->log & PF_LOG_MATCHES)
		return;

	SLIST_FOREACH(ri, matchrules, entry)
		if (ri->r->log & PF_LOG_MATCHES)
			pflog_packet(pd, PFRES_MATCH, rm, am, ruleset, ri->r);
}
#endif	/* NPFLOG > 0 */

struct pf_state_key *
pf_state_key_ref(struct pf_state_key *sk)
{
	if (sk != NULL)
		PF_REF_TAKE(sk->sk_refcnt);

	return (sk);
}

void
pf_state_key_unref(struct pf_state_key *sk)
{
	if (PF_REF_RELE(sk->sk_refcnt)) {
		/* state key must be removed from tree */
		KASSERT(!pf_state_key_isvalid(sk));
		/* state key must be unlinked from reverse key */
		KASSERT(sk->sk_reverse == NULL);
		/* state key must be unlinked from socket */
		KASSERT(sk->sk_inp == NULL);
		pool_put(&pf_state_key_pl, sk);
	}
}

int
pf_state_key_isvalid(struct pf_state_key *sk)
{
	return ((sk != NULL) && (sk->sk_removed == 0));
}

void
pf_mbuf_link_state_key(struct mbuf *m, struct pf_state_key *sk)
{
	KASSERT(m->m_pkthdr.pf.statekey == NULL);
	m->m_pkthdr.pf.statekey = pf_state_key_ref(sk);
}

void
pf_mbuf_unlink_state_key(struct mbuf *m)
{
	struct pf_state_key *sk = m->m_pkthdr.pf.statekey;

	if (sk != NULL) {
		m->m_pkthdr.pf.statekey = NULL;
		pf_state_key_unref(sk);
	}
}

void
pf_mbuf_link_inpcb(struct mbuf *m, struct inpcb *inp)
{
	KASSERT(m->m_pkthdr.pf.inp == NULL);
	m->m_pkthdr.pf.inp = in_pcbref(inp);
}

void
pf_mbuf_unlink_inpcb(struct mbuf *m)
{
	struct inpcb *inp = m->m_pkthdr.pf.inp;

	if (inp != NULL) {
		m->m_pkthdr.pf.inp = NULL;
		in_pcbunref(inp);
	}
}

void
pf_state_key_link_inpcb(struct pf_state_key *sk, struct inpcb *inp)
{
	if (inp == NULL || READ_ONCE(sk->sk_inp) != NULL)
		return;

	mtx_enter(&pf_inp_mtx);
	if (inp->inp_pf_sk != NULL || sk->sk_inp != NULL) {
		mtx_leave(&pf_inp_mtx);
		return;
	}
	sk->sk_inp = in_pcbref(inp);
	inp->inp_pf_sk = pf_state_key_ref(sk);
	mtx_leave(&pf_inp_mtx);
}

void
pf_state_key_unlink_inpcb(struct pf_state_key *sk)
{
	struct inpcb *inp;

	if (READ_ONCE(sk->sk_inp) == NULL)
		return;

	mtx_enter(&pf_inp_mtx);
	inp = sk->sk_inp;
	if (inp == NULL) {
		mtx_leave(&pf_inp_mtx);
		return;
	}
	KASSERT(inp->inp_pf_sk == sk);
	sk->sk_inp = NULL;
	inp->inp_pf_sk = NULL;
	mtx_leave(&pf_inp_mtx);

	pf_state_key_unref(sk);
	in_pcbunref(inp);
}

void
pf_state_key_unlink_reverse(struct pf_state_key *sk)
{
	struct pf_state_key *skrev = sk->sk_reverse;

	/* Note that sk and skrev may be equal, then we unref twice. */
	if (skrev != NULL) {
		KASSERT(skrev->sk_reverse == sk);
		sk->sk_reverse = NULL;
		skrev->sk_reverse = NULL;
		pf_state_key_unref(skrev);
		pf_state_key_unref(sk);
	}
}

struct pf_state *
pf_state_ref(struct pf_state *st)
{
	if (st != NULL)
		PF_REF_TAKE(st->refcnt);
	return (st);
}

void
pf_state_unref(struct pf_state *st)
{
	if ((st != NULL) && PF_REF_RELE(st->refcnt)) {
		/* never inserted or removed */
#if NPFSYNC > 0
		KASSERT((TAILQ_NEXT(st, sync_list) == NULL) ||
		    ((TAILQ_NEXT(st, sync_list) == _Q_INVALID) &&
		    (st->sync_state >= PFSYNC_S_NONE)));
#endif	/* NPFSYNC */
		KASSERT((TAILQ_NEXT(st, entry_list) == NULL) ||
		    (TAILQ_NEXT(st, entry_list) == _Q_INVALID));

		pf_state_key_unref(st->key[PF_SK_WIRE]);
		pf_state_key_unref(st->key[PF_SK_STACK]);

		pool_put(&pf_state_pl, st);
	}
}

int
pf_delay_pkt(struct mbuf *m, u_int ifidx)
{
	struct pf_pktdelay	*pdy;

	if ((pdy = pool_get(&pf_pktdelay_pl, PR_NOWAIT)) == NULL) {
		m_freem(m);
		return (ENOBUFS);
	}
	pdy->ifidx = ifidx;
	pdy->m = m;
	timeout_set(&pdy->to, pf_pktenqueue_delayed, pdy);
	timeout_add_msec(&pdy->to, m->m_pkthdr.pf.delay);
	m->m_pkthdr.pf.delay = 0;
	return (0);
}

void
pf_pktenqueue_delayed(void *arg)
{
	struct pf_pktdelay	*pdy = arg;
	struct ifnet		*ifp;

	ifp = if_get(pdy->ifidx);
	if (ifp != NULL) {
		if_enqueue(ifp, pdy->m);
		if_put(ifp);
	} else
		m_freem(pdy->m);

	pool_put(&pf_pktdelay_pl, pdy);
}
