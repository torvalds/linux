/*	$OpenBSD: pf_ioctl.c,v 1.423 2025/08/06 14:00:33 mvs Exp $ */

/*
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002 - 2018 Henning Brauer <henning@openbsd.org>
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

#include "pfsync.h"
#include "pflog.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/filio.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/timeout.h>
#include <sys/pool.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/rwlock.h>
#include <sys/syslog.h>
#include <sys/specdev.h>
#include <uvm/uvm_extern.h>

#include <crypto/md5.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/hfsc.h>
#include <net/fq_codel.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#ifdef INET6
#include <netinet/icmp6.h>
#endif /* INET6 */

#include <net/pfvar.h>
#include <net/pfvar_priv.h>

#if NPFSYNC > 0
#include <netinet/ip_ipsp.h>
#include <net/if_pfsync.h>
#endif /* NPFSYNC > 0 */

struct pool		 pf_tag_pl;

void			 pfattach(int);
int			 pfopen(dev_t, int, int, struct proc *);
int			 pfclose(dev_t, int, int, struct proc *);
int			 pfioctl(dev_t, u_long, caddr_t, int, struct proc *);
int			 pf_begin_rules(u_int32_t *, const char *);
void			 pf_rollback_rules(u_int32_t, char *);
void			 pf_remove_queues(void);
int			 pf_commit_queues(void);
void			 pf_free_queues(struct pf_queuehead *);
void			 pf_calc_chksum(struct pf_ruleset *);
void			 pf_hash_rule(MD5_CTX *, struct pf_rule *);
void			 pf_hash_rule_addr(MD5_CTX *, struct pf_rule_addr *);
int			 pf_commit_rules(u_int32_t, char *);
int			 pf_addr_setup(struct pf_ruleset *,
			    struct pf_addr_wrap *, sa_family_t);
struct pfi_kif		*pf_kif_setup(struct pfi_kif *);
void			 pf_addr_copyout(struct pf_addr_wrap *);
void			 pf_trans_set_commit(void);
void			 pf_pool_copyin(struct pf_pool *, struct pf_pool *);
int			 pf_validate_range(u_int8_t, u_int16_t[2], int);
int			 pf_rule_copyin(struct pf_rule *, struct pf_rule *);
int			 pf_rule_checkaf(struct pf_rule *);
u_int16_t		 pf_qname2qid(char *, int);
void			 pf_qid2qname(u_int16_t, char *);
void			 pf_qid_unref(u_int16_t);
int			 pf_states_clr(struct pfioc_state_kill *);
int			 pf_states_get(struct pfioc_states *);

struct pf_trans		*pf_open_trans(uint32_t);
struct pf_trans		*pf_find_trans(uint32_t, uint64_t);
void			 pf_free_trans(struct pf_trans *);
void			 pf_rollback_trans(struct pf_trans *);

void			 pf_init_tgetrule(struct pf_trans *,
			    struct pf_anchor *, uint32_t, struct pf_rule *);
void			 pf_cleanup_tgetrule(struct pf_trans *t);

struct pf_rule		 pf_default_rule, pf_default_rule_new;

struct {
	char		statusif[IFNAMSIZ];
	u_int32_t	debug;
	u_int32_t	hostid;
	u_int32_t	reass;
	u_int32_t	mask;
} pf_trans_set;

#define	PF_ORDER_HOST	0
#define	PF_ORDER_NET	1

#define	PF_TSET_STATUSIF	0x01
#define	PF_TSET_DEBUG		0x02
#define	PF_TSET_HOSTID		0x04
#define	PF_TSET_REASS		0x08

#define	TAGID_MAX	 50000
TAILQ_HEAD(pf_tags, pf_tagname)	pf_tags = TAILQ_HEAD_INITIALIZER(pf_tags),
				pf_qids = TAILQ_HEAD_INITIALIZER(pf_qids);

/*
 * pf_lock protects consistency of PF data structures, which don't have
 * their dedicated lock yet. The pf_lock currently protects:
 *	- rules,
 *	- radix tables,
 *	- source nodes
 * All callers must grab pf_lock exclusively.
 *
 * pf_state_lock protects consistency of state table. Packets, which do state
 * look up grab the lock as readers. If packet must create state, then it must
 * grab the lock as writer. Whenever packet creates state it grabs pf_lock
 * first then it locks pf_state_lock as the writer.
 */
struct rwlock		 pf_lock = RWLOCK_INITIALIZER("pf_lock");
struct rwlock		 pf_state_lock = RWLOCK_INITIALIZER("pf_state_lock");
struct rwlock		 pfioctl_rw = RWLOCK_INITIALIZER("pfioctl_rw");

struct cpumem *pf_anchor_stack;

#if (PF_QNAME_SIZE != PF_TAG_NAME_SIZE)
#error PF_QNAME_SIZE must be equal to PF_TAG_NAME_SIZE
#endif
u_int16_t		 tagname2tag(struct pf_tags *, char *, int);
void			 tag2tagname(struct pf_tags *, u_int16_t, char *);
void			 tag_unref(struct pf_tags *, u_int16_t);
int			 pf_rtlabel_add(struct pf_addr_wrap *);
void			 pf_rtlabel_remove(struct pf_addr_wrap *);
void			 pf_rtlabel_copyout(struct pf_addr_wrap *);

LIST_HEAD(, pf_trans)	pf_ioctl_trans = LIST_HEAD_INITIALIZER(pf_trans);

/* counts transactions opened by a device */
unsigned int pf_tcount[CLONE_MAPSZ * NBBY];
#define pf_unit2idx(_unit_)	((_unit_) >> CLONE_SHIFT)

void
pfattach(int num)
{
	u_int32_t *timeout = pf_default_rule.timeout;
	struct pf_anchor_stackframe *sf;
	struct cpumem_iter cmi;

	pool_init(&pf_rule_pl, sizeof(struct pf_rule), 0,
	    IPL_SOFTNET, 0, "pfrule", NULL);
	pool_init(&pf_src_tree_pl, sizeof(struct pf_src_node), 0,
	    IPL_SOFTNET, 0, "pfsrctr", NULL);
	pool_init(&pf_sn_item_pl, sizeof(struct pf_sn_item), 0,
	    IPL_SOFTNET, 0, "pfsnitem", NULL);
	pool_init(&pf_state_pl, sizeof(struct pf_state), CACHELINESIZE,
	    IPL_SOFTNET, 0, "pfstate", NULL);
	pool_init(&pf_state_key_pl, sizeof(struct pf_state_key), CACHELINESIZE,
	    IPL_SOFTNET, 0, "pfstkey", NULL);
	pool_init(&pf_state_item_pl, sizeof(struct pf_state_item), 0,
	    IPL_SOFTNET, 0, "pfstitem", NULL);
	pool_init(&pf_rule_item_pl, sizeof(struct pf_rule_item), 0,
	    IPL_SOFTNET, 0, "pfruleitem", NULL);
	pool_init(&pf_queue_pl, sizeof(struct pf_queuespec), 0,
	    IPL_SOFTNET, 0, "pfqueue", NULL);
	pool_init(&pf_tag_pl, sizeof(struct pf_tagname), 0,
	    IPL_SOFTNET, 0, "pftag", NULL);
	pool_init(&pf_pktdelay_pl, sizeof(struct pf_pktdelay), 0,
	    IPL_SOFTNET, 0, "pfpktdelay", NULL);
	pool_init(&pf_anchor_pl, sizeof(struct pf_anchor), 0,
	    IPL_SOFTNET, 0, "pfanchor", NULL);

	hfsc_initialize();
	pfr_initialize();
	pfi_initialize();
	pf_osfp_initialize();
	pf_syncookies_init();

	pool_sethardlimit(pf_pool_limits[PF_LIMIT_STATES].pp,
	    pf_pool_limits[PF_LIMIT_STATES].limit);
	pool_sethardlimit(pf_pool_limits[PF_LIMIT_ANCHORS].pp,
	    pf_pool_limits[PF_LIMIT_ANCHORS].limit);

	if (physmem <= atop(100*1024*1024))
		pf_pool_limits[PF_LIMIT_TABLE_ENTRIES].limit =
		    PFR_KENTRY_HIWAT_SMALL;

	RB_INIT(&tree_src_tracking);
	RB_INIT(&pf_anchors);
	pf_init_ruleset(&pf_main_ruleset);
	TAILQ_INIT(&pf_queues[0]);
	TAILQ_INIT(&pf_queues[1]);
	pf_queues_active = &pf_queues[0];
	pf_queues_inactive = &pf_queues[1];

	/* default rule should never be garbage collected */
	pf_default_rule.entries.tqe_prev = &pf_default_rule.entries.tqe_next;
	pf_default_rule.action = PF_PASS;
	pf_default_rule.nr = (u_int32_t)-1;
	pf_default_rule.rtableid = -1;

	/* initialize default timeouts */
	timeout[PFTM_TCP_FIRST_PACKET] = PFTM_TCP_FIRST_PACKET_VAL;
	timeout[PFTM_TCP_OPENING] = PFTM_TCP_OPENING_VAL;
	timeout[PFTM_TCP_ESTABLISHED] = PFTM_TCP_ESTABLISHED_VAL;
	timeout[PFTM_TCP_CLOSING] = PFTM_TCP_CLOSING_VAL;
	timeout[PFTM_TCP_FIN_WAIT] = PFTM_TCP_FIN_WAIT_VAL;
	timeout[PFTM_TCP_CLOSED] = PFTM_TCP_CLOSED_VAL;
	timeout[PFTM_UDP_FIRST_PACKET] = PFTM_UDP_FIRST_PACKET_VAL;
	timeout[PFTM_UDP_SINGLE] = PFTM_UDP_SINGLE_VAL;
	timeout[PFTM_UDP_MULTIPLE] = PFTM_UDP_MULTIPLE_VAL;
	timeout[PFTM_ICMP_FIRST_PACKET] = PFTM_ICMP_FIRST_PACKET_VAL;
	timeout[PFTM_ICMP_ERROR_REPLY] = PFTM_ICMP_ERROR_REPLY_VAL;
	timeout[PFTM_OTHER_FIRST_PACKET] = PFTM_OTHER_FIRST_PACKET_VAL;
	timeout[PFTM_OTHER_SINGLE] = PFTM_OTHER_SINGLE_VAL;
	timeout[PFTM_OTHER_MULTIPLE] = PFTM_OTHER_MULTIPLE_VAL;
	timeout[PFTM_FRAG] = PFTM_FRAG_VAL;
	timeout[PFTM_INTERVAL] = PFTM_INTERVAL_VAL;
	timeout[PFTM_SRC_NODE] = PFTM_SRC_NODE_VAL;
	timeout[PFTM_TS_DIFF] = PFTM_TS_DIFF_VAL;
	timeout[PFTM_ADAPTIVE_START] = PFSTATE_ADAPT_START;
	timeout[PFTM_ADAPTIVE_END] = PFSTATE_ADAPT_END;

	pf_default_rule.src.addr.type =  PF_ADDR_ADDRMASK;
	pf_default_rule.dst.addr.type =  PF_ADDR_ADDRMASK;
	pf_default_rule.rdr.addr.type =  PF_ADDR_NONE;
	pf_default_rule.nat.addr.type =  PF_ADDR_NONE;
	pf_default_rule.route.addr.type =  PF_ADDR_NONE;

	pf_normalize_init();
	memset(&pf_status, 0, sizeof(pf_status));
	pf_status.debug = LOG_ERR;
	pf_status.reass = PF_REASS_ENABLED;

	/* XXX do our best to avoid a conflict */
	pf_status.hostid = arc4random();

	pf_default_rule_new = pf_default_rule;

	/*
	 * we waste two stack frames as meta-data.
	 * frame[0] always presents a top, which can not be used for data
	 * frame[PF_ANCHOR_STACK_MAX] denotes a bottom of the stack and keeps
	 * the pointer to currently used stack frame.
	 */
	pf_anchor_stack = cpumem_malloc(
	    sizeof(struct pf_anchor_stackframe) * (PF_ANCHOR_STACK_MAX + 2),
	    M_PF);
	CPUMEM_FOREACH(sf, &cmi, pf_anchor_stack)
		sf[PF_ANCHOR_STACK_MAX].sf_stack_top = &sf[0];
}

int
pfopen(dev_t dev, int flags, int fmt, struct proc *p)
{
	int unit = minor(dev);

	if (unit & ((1 << CLONE_SHIFT) - 1))
		return (ENXIO);

	return (0);
}

int
pfclose(dev_t dev, int flags, int fmt, struct proc *p)
{
	struct pf_trans *w, *s;
	LIST_HEAD(, pf_trans)	tmp_list;
	uint32_t unit = minor(dev);

	LIST_INIT(&tmp_list);
	rw_enter_write(&pfioctl_rw);
	LIST_FOREACH_SAFE(w, &pf_ioctl_trans, pft_entry, s) {
		if (w->pft_unit == unit) {
			LIST_REMOVE(w, pft_entry);
			LIST_INSERT_HEAD(&tmp_list, w, pft_entry);
		}
	}
	rw_exit_write(&pfioctl_rw);

	while ((w = LIST_FIRST(&tmp_list)) != NULL) {
		LIST_REMOVE(w, pft_entry);
		pf_free_trans(w);
	}

	return (0);
}

void
pf_rule_free(struct pf_rule *rule)
{
	if (rule == NULL)
		return;

	pfi_kif_free(rule->kif);
	pfi_kif_free(rule->rcv_kif);
	pfi_kif_free(rule->rdr.kif);
	pfi_kif_free(rule->nat.kif);
	pfi_kif_free(rule->route.kif);

	pool_put(&pf_rule_pl, rule);
}

void
pf_rm_rule(struct pf_rulequeue *rulequeue, struct pf_rule *rule)
{
	if (rulequeue != NULL) {
		if (rule->states_cur == 0 && rule->src_nodes == 0) {
			/*
			 * XXX - we need to remove the table *before* detaching
			 * the rule to make sure the table code does not delete
			 * the anchor under our feet.
			 */
			pf_tbladdr_remove(&rule->src.addr);
			pf_tbladdr_remove(&rule->dst.addr);
			pf_tbladdr_remove(&rule->rdr.addr);
			pf_tbladdr_remove(&rule->nat.addr);
			pf_tbladdr_remove(&rule->route.addr);
			if (rule->overload_tbl)
				pfr_detach_table(rule->overload_tbl);
		}
		TAILQ_REMOVE(rulequeue, rule, entries);
		rule->entries.tqe_prev = NULL;
		rule->nr = (u_int32_t)-1;
	}

	if (rule->states_cur > 0 || rule->src_nodes > 0 ||
	    rule->entries.tqe_prev != NULL)
		return;
	pf_tag_unref(rule->tag);
	pf_tag_unref(rule->match_tag);
	pf_rtlabel_remove(&rule->src.addr);
	pf_rtlabel_remove(&rule->dst.addr);
	pfi_dynaddr_remove(&rule->src.addr);
	pfi_dynaddr_remove(&rule->dst.addr);
	pfi_dynaddr_remove(&rule->rdr.addr);
	pfi_dynaddr_remove(&rule->nat.addr);
	pfi_dynaddr_remove(&rule->route.addr);
	if (rulequeue == NULL) {
		pf_tbladdr_remove(&rule->src.addr);
		pf_tbladdr_remove(&rule->dst.addr);
		pf_tbladdr_remove(&rule->rdr.addr);
		pf_tbladdr_remove(&rule->nat.addr);
		pf_tbladdr_remove(&rule->route.addr);
		if (rule->overload_tbl)
			pfr_detach_table(rule->overload_tbl);
	}
	pfi_kif_unref(rule->rcv_kif, PFI_KIF_REF_RULE);
	pfi_kif_unref(rule->kif, PFI_KIF_REF_RULE);
	pfi_kif_unref(rule->rdr.kif, PFI_KIF_REF_RULE);
	pfi_kif_unref(rule->nat.kif, PFI_KIF_REF_RULE);
	pfi_kif_unref(rule->route.kif, PFI_KIF_REF_RULE);
	pf_remove_anchor(rule);
	pool_put(&pf_rule_pl, rule);
}

u_int16_t
tagname2tag(struct pf_tags *head, char *tagname, int create)
{
	struct pf_tagname	*tag, *p = NULL;
	u_int16_t		 new_tagid = 1;

	TAILQ_FOREACH(tag, head, entries)
		if (strcmp(tagname, tag->name) == 0) {
			tag->ref++;
			return (tag->tag);
		}

	if (!create)
		return (0);

	/*
	 * to avoid fragmentation, we do a linear search from the beginning
	 * and take the first free slot we find. if there is none or the list
	 * is empty, append a new entry at the end.
	 */

	/* new entry */
	TAILQ_FOREACH(p, head, entries) {
		if (p->tag != new_tagid)
			break;
		new_tagid = p->tag + 1;
	}

	if (new_tagid > TAGID_MAX)
		return (0);

	/* allocate and fill new struct pf_tagname */
	tag = pool_get(&pf_tag_pl, PR_NOWAIT | PR_ZERO);
	if (tag == NULL)
		return (0);
	strlcpy(tag->name, tagname, sizeof(tag->name));
	tag->tag = new_tagid;
	tag->ref++;

	if (p != NULL)	/* insert new entry before p */
		TAILQ_INSERT_BEFORE(p, tag, entries);
	else	/* either list empty or no free slot in between */
		TAILQ_INSERT_TAIL(head, tag, entries);

	return (tag->tag);
}

void
tag2tagname(struct pf_tags *head, u_int16_t tagid, char *p)
{
	struct pf_tagname	*tag;

	TAILQ_FOREACH(tag, head, entries)
		if (tag->tag == tagid) {
			strlcpy(p, tag->name, PF_TAG_NAME_SIZE);
			return;
		}
}

void
tag_unref(struct pf_tags *head, u_int16_t tag)
{
	struct pf_tagname	*p, *next;

	if (tag == 0)
		return;

	TAILQ_FOREACH_SAFE(p, head, entries, next) {
		if (tag == p->tag) {
			if (--p->ref == 0) {
				TAILQ_REMOVE(head, p, entries);
				pool_put(&pf_tag_pl, p);
			}
			break;
		}
	}
}

u_int16_t
pf_tagname2tag(char *tagname, int create)
{
	return (tagname2tag(&pf_tags, tagname, create));
}

void
pf_tag2tagname(u_int16_t tagid, char *p)
{
	tag2tagname(&pf_tags, tagid, p);
}

void
pf_tag_ref(u_int16_t tag)
{
	struct pf_tagname *t;

	TAILQ_FOREACH(t, &pf_tags, entries)
		if (t->tag == tag)
			break;
	if (t != NULL)
		t->ref++;
}

void
pf_tag_unref(u_int16_t tag)
{
	tag_unref(&pf_tags, tag);
}

int
pf_rtlabel_add(struct pf_addr_wrap *a)
{
	if (a->type == PF_ADDR_RTLABEL &&
	    (a->v.rtlabel = rtlabel_name2id(a->v.rtlabelname)) == 0)
		return (-1);
	return (0);
}

void
pf_rtlabel_remove(struct pf_addr_wrap *a)
{
	if (a->type == PF_ADDR_RTLABEL)
		rtlabel_unref(a->v.rtlabel);
}

void
pf_rtlabel_copyout(struct pf_addr_wrap *a)
{
	if (a->type == PF_ADDR_RTLABEL && a->v.rtlabel) {
		if (rtlabel_id2name(a->v.rtlabel, a->v.rtlabelname,
		    sizeof(a->v.rtlabelname)) == NULL)
			strlcpy(a->v.rtlabelname, "?",
			    sizeof(a->v.rtlabelname));
	}
}

u_int16_t
pf_qname2qid(char *qname, int create)
{
	return (tagname2tag(&pf_qids, qname, create));
}

void
pf_qid2qname(u_int16_t qid, char *p)
{
	tag2tagname(&pf_qids, qid, p);
}

void
pf_qid_unref(u_int16_t qid)
{
	tag_unref(&pf_qids, (u_int16_t)qid);
}

int
pf_begin_rules(u_int32_t *version, const char *anchor)
{
	struct pf_ruleset	*rs;
	struct pf_rule		*rule;

	if ((rs = pf_find_or_create_ruleset(anchor)) == NULL)
		return (EINVAL);
	while ((rule = TAILQ_FIRST(rs->rules.inactive.ptr)) != NULL) {
		pf_rm_rule(rs->rules.inactive.ptr, rule);
		rs->rules.inactive.rcount--;
	}
	*version = ++rs->rules.inactive.version;
	rs->rules.inactive.open = 1;
	return (0);
}

void
pf_rollback_rules(u_int32_t version, char *anchor)
{
	struct pf_ruleset	*rs;
	struct pf_rule		*rule;

	rs = pf_find_ruleset(anchor);
	if (rs == NULL || !rs->rules.inactive.open ||
	    rs->rules.inactive.version != version)
		return;
	while ((rule = TAILQ_FIRST(rs->rules.inactive.ptr)) != NULL) {
		pf_rm_rule(rs->rules.inactive.ptr, rule);
		rs->rules.inactive.rcount--;
	}
	rs->rules.inactive.open = 0;

	/* queue defs only in the main ruleset */
	if (anchor[0])
		return;

	pf_free_queues(pf_queues_inactive);
}

void
pf_free_queues(struct pf_queuehead *where)
{
	struct pf_queuespec	*q, *qtmp;

	TAILQ_FOREACH_SAFE(q, where, entries, qtmp) {
		TAILQ_REMOVE(where, q, entries);
		pfi_kif_unref(q->kif, PFI_KIF_REF_RULE);
		pool_put(&pf_queue_pl, q);
	}
}

void
pf_remove_queues(void)
{
	struct pf_queuespec	*q;
	struct ifnet		*ifp;

	/* put back interfaces in normal queueing mode */
	TAILQ_FOREACH(q, pf_queues_active, entries) {
		if (q->parent_qid != 0)
			continue;

		ifp = q->kif->pfik_ifp;
		if (ifp == NULL)
			continue;

		ifq_attach(&ifp->if_snd, ifq_priq_ops, NULL);
	}
}

struct pf_queue_if {
	struct ifnet		*ifp;
	const struct ifq_ops	*ifqops;
	const struct pfq_ops	*pfqops;
	void			*disc;
	struct pf_queue_if	*next;
};

static inline struct pf_queue_if *
pf_ifp2q(struct pf_queue_if *list, struct ifnet *ifp)
{
	struct pf_queue_if *qif = list;

	while (qif != NULL) {
		if (qif->ifp == ifp)
			return (qif);

		qif = qif->next;
	}

	return (qif);
}

int
pf_create_queues(void)
{
	struct pf_queuespec	*q;
	struct ifnet		*ifp;
	struct pf_queue_if		*list = NULL, *qif;
	int			 error;

	/*
	 * Find root queues and allocate traffic conditioner
	 * private data for these interfaces
	 */
	TAILQ_FOREACH(q, pf_queues_active, entries) {
		if (q->parent_qid != 0)
			continue;

		ifp = q->kif->pfik_ifp;
		if (ifp == NULL)
			continue;

		qif = malloc(sizeof(*qif), M_PF, M_WAITOK);
		qif->ifp = ifp;

		if (q->flags & PFQS_ROOTCLASS) {
			qif->ifqops = ifq_hfsc_ops;
			qif->pfqops = pfq_hfsc_ops;
		} else {
			qif->ifqops = ifq_fqcodel_ops;
			qif->pfqops = pfq_fqcodel_ops;
		}

		qif->disc = qif->pfqops->pfq_alloc(ifp);

		qif->next = list;
		list = qif;
	}

	/* and now everything */
	TAILQ_FOREACH(q, pf_queues_active, entries) {
		ifp = q->kif->pfik_ifp;
		if (ifp == NULL)
			continue;

		qif = pf_ifp2q(list, ifp);
		KASSERT(qif != NULL);

		error = qif->pfqops->pfq_addqueue(qif->disc, q);
		if (error != 0)
			goto error;
	}

	/* find root queues in old list to disable them if necessary */
	TAILQ_FOREACH(q, pf_queues_inactive, entries) {
		if (q->parent_qid != 0)
			continue;

		ifp = q->kif->pfik_ifp;
		if (ifp == NULL)
			continue;

		qif = pf_ifp2q(list, ifp);
		if (qif != NULL)
			continue;

		ifq_attach(&ifp->if_snd, ifq_priq_ops, NULL);
	}

	/* commit the new queues */
	while (list != NULL) {
		qif = list;
		list = qif->next;

		ifp = qif->ifp;

		ifq_attach(&ifp->if_snd, qif->ifqops, qif->disc);
		free(qif, M_PF, sizeof(*qif));
	}

	return (0);

error:
	while (list != NULL) {
		qif = list;
		list = qif->next;

		qif->pfqops->pfq_free(qif->disc);
		free(qif, M_PF, sizeof(*qif));
	}

	return (error);
}

int
pf_commit_queues(void)
{
	struct pf_queuehead	*qswap;
	int error;

	/* swap */
	qswap = pf_queues_active;
	pf_queues_active = pf_queues_inactive;
	pf_queues_inactive = qswap;

	error = pf_create_queues();
	if (error != 0) {
		pf_queues_inactive = pf_queues_active;
		pf_queues_active = qswap;
		return (error);
	}

	pf_free_queues(pf_queues_inactive);

	return (0);
}

const struct pfq_ops *
pf_queue_manager(struct pf_queuespec *q)
{
	if (q->flags & PFQS_FLOWQUEUE)
		return pfq_fqcodel_ops;
	return (/* pfq_default_ops */ NULL);
}

#define PF_MD5_UPD(st, elm)						\
		MD5Update(ctx, (u_int8_t *) &(st)->elm, sizeof((st)->elm))

#define PF_MD5_UPD_STR(st, elm)						\
		MD5Update(ctx, (u_int8_t *) (st)->elm, strlen((st)->elm))

#define PF_MD5_UPD_HTONL(st, elm, stor) do {				\
		(stor) = htonl((st)->elm);				\
		MD5Update(ctx, (u_int8_t *) &(stor), sizeof(u_int32_t));\
} while (0)

#define PF_MD5_UPD_HTONS(st, elm, stor) do {				\
		(stor) = htons((st)->elm);				\
		MD5Update(ctx, (u_int8_t *) &(stor), sizeof(u_int16_t));\
} while (0)

void
pf_hash_rule_addr(MD5_CTX *ctx, struct pf_rule_addr *pfr)
{
	PF_MD5_UPD(pfr, addr.type);
	switch (pfr->addr.type) {
		case PF_ADDR_DYNIFTL:
			PF_MD5_UPD(pfr, addr.v.ifname);
			PF_MD5_UPD(pfr, addr.iflags);
			break;
		case PF_ADDR_TABLE:
			if (strncmp(pfr->addr.v.tblname, PF_OPTIMIZER_TABLE_PFX,
			    strlen(PF_OPTIMIZER_TABLE_PFX)))
				PF_MD5_UPD(pfr, addr.v.tblname);
			break;
		case PF_ADDR_ADDRMASK:
			/* XXX ignore af? */
			PF_MD5_UPD(pfr, addr.v.a.addr.addr32);
			PF_MD5_UPD(pfr, addr.v.a.mask.addr32);
			break;
		case PF_ADDR_RTLABEL:
			PF_MD5_UPD(pfr, addr.v.rtlabelname);
			break;
	}

	PF_MD5_UPD(pfr, port[0]);
	PF_MD5_UPD(pfr, port[1]);
	PF_MD5_UPD(pfr, neg);
	PF_MD5_UPD(pfr, port_op);
}

void
pf_hash_rule(MD5_CTX *ctx, struct pf_rule *rule)
{
	u_int16_t x;
	u_int32_t y;

	pf_hash_rule_addr(ctx, &rule->src);
	pf_hash_rule_addr(ctx, &rule->dst);
	PF_MD5_UPD_STR(rule, label);
	PF_MD5_UPD_STR(rule, ifname);
	PF_MD5_UPD_STR(rule, rcv_ifname);
	PF_MD5_UPD_STR(rule, match_tagname);
	PF_MD5_UPD_HTONS(rule, match_tag, x); /* dup? */
	PF_MD5_UPD_HTONL(rule, os_fingerprint, y);
	PF_MD5_UPD_HTONL(rule, prob, y);
	PF_MD5_UPD_HTONL(rule, uid.uid[0], y);
	PF_MD5_UPD_HTONL(rule, uid.uid[1], y);
	PF_MD5_UPD(rule, uid.op);
	PF_MD5_UPD_HTONL(rule, gid.gid[0], y);
	PF_MD5_UPD_HTONL(rule, gid.gid[1], y);
	PF_MD5_UPD(rule, gid.op);
	PF_MD5_UPD_HTONL(rule, rule_flag, y);
	PF_MD5_UPD(rule, action);
	PF_MD5_UPD(rule, direction);
	PF_MD5_UPD(rule, af);
	PF_MD5_UPD(rule, quick);
	PF_MD5_UPD(rule, ifnot);
	PF_MD5_UPD(rule, rcvifnot);
	PF_MD5_UPD(rule, match_tag_not);
	PF_MD5_UPD(rule, keep_state);
	PF_MD5_UPD(rule, proto);
	PF_MD5_UPD(rule, type);
	PF_MD5_UPD(rule, code);
	PF_MD5_UPD(rule, flags);
	PF_MD5_UPD(rule, flagset);
	PF_MD5_UPD(rule, allow_opts);
	PF_MD5_UPD(rule, rt);
	PF_MD5_UPD(rule, tos);
}

int
pf_commit_rules(u_int32_t version, char *anchor)
{
	struct pf_ruleset	*rs;
	struct pf_rule		*rule;
	struct pf_rulequeue	*old_rules;
	u_int32_t		 old_rcount;

	PF_ASSERT_LOCKED();

	rs = pf_find_ruleset(anchor);
	if (rs == NULL || !rs->rules.inactive.open ||
	    version != rs->rules.inactive.version)
		return (EBUSY);

	if (rs == &pf_main_ruleset)
		pf_calc_chksum(rs);

	/* Swap rules, keep the old. */
	old_rules = rs->rules.active.ptr;
	old_rcount = rs->rules.active.rcount;

	rs->rules.active.ptr = rs->rules.inactive.ptr;
	rs->rules.active.rcount = rs->rules.inactive.rcount;
	rs->rules.inactive.ptr = old_rules;
	rs->rules.inactive.rcount = old_rcount;

	rs->rules.active.version = rs->rules.inactive.version;
	pf_calc_skip_steps(rs->rules.active.ptr);


	/* Purge the old rule list. */
	while ((rule = TAILQ_FIRST(old_rules)) != NULL)
		pf_rm_rule(old_rules, rule);
	rs->rules.inactive.rcount = 0;
	rs->rules.inactive.open = 0;
	pf_remove_if_empty_ruleset(rs);

	/* queue defs only in the main ruleset */
	if (anchor[0])
		return (0);
	return (pf_commit_queues());
}

void
pf_calc_chksum(struct pf_ruleset *rs)
{
	MD5_CTX			 ctx;
	struct pf_rule		*rule;
	u_int8_t		 digest[PF_MD5_DIGEST_LENGTH];

	MD5Init(&ctx);

	if (rs->rules.inactive.rcount) {
		TAILQ_FOREACH(rule, rs->rules.inactive.ptr, entries) {
			pf_hash_rule(&ctx, rule);
		}
	}

	MD5Final(digest, &ctx);
	memcpy(pf_status.pf_chksum, digest, sizeof(pf_status.pf_chksum));
}

int
pf_addr_setup(struct pf_ruleset *ruleset, struct pf_addr_wrap *addr,
    sa_family_t af)
{
	if (pfi_dynaddr_setup(addr, af, PR_WAITOK) ||
	    pf_tbladdr_setup(ruleset, addr, PR_WAITOK) ||
	    pf_rtlabel_add(addr))
		return (EINVAL);

	return (0);
}

struct pfi_kif *
pf_kif_setup(struct pfi_kif *kif_buf)
{
	struct pfi_kif *kif;

	if (kif_buf == NULL)
		return (NULL);

	KASSERT(kif_buf->pfik_name[0] != '\0');

	kif = pfi_kif_get(kif_buf->pfik_name, &kif_buf);
	if (kif_buf != NULL)
		pfi_kif_free(kif_buf);
	pfi_kif_ref(kif, PFI_KIF_REF_RULE);

	return (kif);
}

void
pf_addr_copyout(struct pf_addr_wrap *addr)
{
	pfi_dynaddr_copyout(addr);
	pf_tbladdr_copyout(addr);
	pf_rtlabel_copyout(addr);
}

int
pf_states_clr(struct pfioc_state_kill *psk)
{
	struct pf_state		*st, *nextst;
	struct pf_state		*head, *tail;
	u_int			 killed = 0;
	int			 error;

	NET_LOCK();

	/* lock against the gc removing an item from the list */
	error = rw_enter(&pf_state_list.pfs_rwl, RW_READ|RW_INTR);
	if (error != 0)
		goto unlock;

	/* get a snapshot view of the ends of the list to traverse between */
	mtx_enter(&pf_state_list.pfs_mtx);
	head = TAILQ_FIRST(&pf_state_list.pfs_list);
	tail = TAILQ_LAST(&pf_state_list.pfs_list, pf_state_queue);
	mtx_leave(&pf_state_list.pfs_mtx);

	st = NULL;
	nextst = head;

	PF_LOCK();
	PF_STATE_ENTER_WRITE();

	while (st != tail) {
		st = nextst;
		nextst = TAILQ_NEXT(st, entry_list);

		if (st->timeout == PFTM_UNLINKED)
			continue;

		if (!psk->psk_ifname[0] || !strcmp(psk->psk_ifname,
		    st->kif->pfik_name)) {
#if NPFSYNC > 0
			/* don't send out individual delete messages */
			SET(st->state_flags, PFSTATE_NOSYNC);
#endif	/* NPFSYNC > 0 */
			pf_remove_state(st);
			killed++;
		}
	}

	PF_STATE_EXIT_WRITE();
	PF_UNLOCK();
	rw_exit(&pf_state_list.pfs_rwl);

	psk->psk_killed = killed;

#if NPFSYNC > 0
	pfsync_clear_states(pf_status.hostid, psk->psk_ifname);
#endif	/* NPFSYNC > 0 */
unlock:
	NET_UNLOCK();

	return (error);
}

int
pf_states_get(struct pfioc_states *ps)
{
	struct pf_state		*st, *nextst;
	struct pf_state		*head, *tail;
	struct pfsync_state	*p, pstore;
	u_int32_t		 nr = 0;
	int			 error;

	if (ps->ps_len == 0) {
		nr = pf_status.states;
		ps->ps_len = sizeof(struct pfsync_state) * nr;
		return (0);
	}

	p = ps->ps_states;

	/* lock against the gc removing an item from the list */
	error = rw_enter(&pf_state_list.pfs_rwl, RW_READ|RW_INTR);
	if (error != 0)
		return (error);

	/* get a snapshot view of the ends of the list to traverse between */
	mtx_enter(&pf_state_list.pfs_mtx);
	head = TAILQ_FIRST(&pf_state_list.pfs_list);
	tail = TAILQ_LAST(&pf_state_list.pfs_list, pf_state_queue);
	mtx_leave(&pf_state_list.pfs_mtx);

	st = NULL;
	nextst = head;

	while (st != tail) {
		st = nextst;
		nextst = TAILQ_NEXT(st, entry_list);

		if (st->timeout == PFTM_UNLINKED)
			continue;

		if ((nr+1) * sizeof(*p) > ps->ps_len)
			break;

		pf_state_export(&pstore, st);
		error = copyout(&pstore, p, sizeof(*p));
		if (error)
			goto fail;

		p++;
		nr++;
	}
	ps->ps_len = sizeof(struct pfsync_state) * nr;

fail:
	rw_exit(&pf_state_list.pfs_rwl);

	return (error);
}

int
pfioctl(dev_t dev, u_long cmd, caddr_t addr, int flags, struct proc *p)
{
	int			 error = 0;

	/* XXX keep in sync with switch() below */
	if (securelevel > 1)
		switch (cmd) {
		case DIOCGETRULES:
		case DIOCGETRULE:
		case DIOCGETSTATE:
		case DIOCSETSTATUSIF:
		case DIOCGETSTATUS:
		case DIOCCLRSTATUS:
		case DIOCNATLOOK:
		case DIOCSETDEBUG:
		case DIOCGETSTATES:
		case DIOCGETTIMEOUT:
		case DIOCGETLIMIT:
		case DIOCGETRULESETS:
		case DIOCGETRULESET:
		case DIOCGETQUEUES:
		case DIOCGETQUEUE:
		case DIOCGETQSTATS:
		case DIOCRGETTABLES:
		case DIOCRGETTSTATS:
		case DIOCRCLRTSTATS:
		case DIOCRCLRADDRS:
		case DIOCRADDADDRS:
		case DIOCRDELADDRS:
		case DIOCRSETADDRS:
		case DIOCRGETADDRS:
		case DIOCRGETASTATS:
		case DIOCRCLRASTATS:
		case DIOCRTSTADDRS:
		case DIOCOSFPGET:
		case DIOCGETSRCNODES:
		case DIOCCLRSRCNODES:
		case DIOCIGETIFACES:
		case DIOCSETIFFLAG:
		case DIOCCLRIFFLAG:
		case DIOCGETSYNFLWATS:
			break;
		case DIOCRCLRTABLES:
		case DIOCRADDTABLES:
		case DIOCRDELTABLES:
		case DIOCRSETTFLAGS:
			if (((struct pfioc_table *)addr)->pfrio_flags &
			    PFR_FLAG_DUMMY)
				break; /* dummy operation ok */
			return (EPERM);
		default:
			return (EPERM);
		}

	if (!(flags & FWRITE))
		switch (cmd) {
		case DIOCGETRULES:
		case DIOCGETSTATE:
		case DIOCGETSTATUS:
		case DIOCGETSTATES:
		case DIOCGETTIMEOUT:
		case DIOCGETLIMIT:
		case DIOCGETRULESETS:
		case DIOCGETRULESET:
		case DIOCGETQUEUES:
		case DIOCGETQUEUE:
		case DIOCGETQSTATS:
		case DIOCNATLOOK:
		case DIOCRGETTABLES:
		case DIOCRGETTSTATS:
		case DIOCRGETADDRS:
		case DIOCRGETASTATS:
		case DIOCRTSTADDRS:
		case DIOCOSFPGET:
		case DIOCGETSRCNODES:
		case DIOCIGETIFACES:
		case DIOCGETSYNFLWATS:
		case DIOCXEND:
			break;
		case DIOCRCLRTABLES:
		case DIOCRADDTABLES:
		case DIOCRDELTABLES:
		case DIOCRCLRTSTATS:
		case DIOCRCLRADDRS:
		case DIOCRADDADDRS:
		case DIOCRDELADDRS:
		case DIOCRSETADDRS:
		case DIOCRSETTFLAGS:
			if (((struct pfioc_table *)addr)->pfrio_flags &
			    PFR_FLAG_DUMMY) {
				flags |= FWRITE; /* need write lock for dummy */
				break; /* dummy operation ok */
			}
			return (EACCES);
		case DIOCGETRULE:
			if (((struct pfioc_rule *)addr)->action ==
			    PF_GET_CLR_CNTR)
				return (EACCES);
			break;
		default:
			return (EACCES);
		}

	rw_enter_write(&pfioctl_rw);

	switch (cmd) {

	case DIOCSTART:
		NET_LOCK();
		PF_LOCK();
		if (pf_status.running)
			error = EEXIST;
		else {
			pf_status.running = 1;
			pf_status.since = getuptime();
			if (pf_status.stateid == 0) {
				pf_status.stateid = gettime();
				pf_status.stateid = pf_status.stateid << 32;
			}
			timeout_add_sec(&pf_purge_states_to, 1);
			timeout_add_sec(&pf_purge_to, 1);
			pf_create_queues();
			DPFPRINTF(LOG_NOTICE, "pf: started");
		}
		PF_UNLOCK();
		NET_UNLOCK();
		break;

	case DIOCSTOP:
		NET_LOCK();
		PF_LOCK();
		if (!pf_status.running)
			error = ENOENT;
		else {
			pf_status.running = 0;
			pf_status.since = getuptime();
			pf_remove_queues();
			DPFPRINTF(LOG_NOTICE, "pf: stopped");
		}
		PF_UNLOCK();
		NET_UNLOCK();
		break;

	case DIOCGETQUEUES: {
		struct pfioc_queue	*pq = (struct pfioc_queue *)addr;
		struct pf_queuespec	*qs;
		u_int32_t		 nr = 0;

		PF_LOCK();
		pq->ticket = pf_main_ruleset.rules.active.version;

		/* save state to not run over them all each time? */
		qs = TAILQ_FIRST(pf_queues_active);
		while (qs != NULL) {
			qs = TAILQ_NEXT(qs, entries);
			nr++;
		}
		pq->nr = nr;
		PF_UNLOCK();
		break;
	}

	case DIOCGETQUEUE: {
		struct pfioc_queue	*pq = (struct pfioc_queue *)addr;
		struct pf_queuespec	*qs;
		u_int32_t		 nr = 0;

		PF_LOCK();
		if (pq->ticket != pf_main_ruleset.rules.active.version) {
			error = EBUSY;
			PF_UNLOCK();
			goto fail;
		}

		/* save state to not run over them all each time? */
		qs = TAILQ_FIRST(pf_queues_active);
		while ((qs != NULL) && (nr++ < pq->nr))
			qs = TAILQ_NEXT(qs, entries);
		if (qs == NULL) {
			error = EBUSY;
			PF_UNLOCK();
			goto fail;
		}
		memcpy(&pq->queue, qs, sizeof(pq->queue));
		PF_UNLOCK();
		break;
	}

	case DIOCGETQSTATS: {
		struct pfioc_qstats	*pq = (struct pfioc_qstats *)addr;
		struct pf_queuespec	*qs;
		u_int32_t		 nr;
		int			 nbytes;

		NET_LOCK();
		PF_LOCK();
		if (pq->ticket != pf_main_ruleset.rules.active.version) {
			error = EBUSY;
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}
		nbytes = pq->nbytes;
		nr = 0;

		/* save state to not run over them all each time? */
		qs = TAILQ_FIRST(pf_queues_active);
		while ((qs != NULL) && (nr++ < pq->nr))
			qs = TAILQ_NEXT(qs, entries);
		if (qs == NULL) {
			error = EBUSY;
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}
		memcpy(&pq->queue, qs, sizeof(pq->queue));
		/* It's a root flow queue but is not an HFSC root class */
		if ((qs->flags & PFQS_FLOWQUEUE) && qs->parent_qid == 0 &&
		    !(qs->flags & PFQS_ROOTCLASS))
			error = pfq_fqcodel_ops->pfq_qstats(qs, pq->buf,
			    &nbytes);
		else
			error = pfq_hfsc_ops->pfq_qstats(qs, pq->buf,
			    &nbytes);
		if (error == 0)
			pq->nbytes = nbytes;
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCADDQUEUE: {
		struct pfioc_queue	*q = (struct pfioc_queue *)addr;
		struct pf_queuespec	*qs;

		qs = pool_get(&pf_queue_pl, PR_WAITOK|PR_LIMITFAIL|PR_ZERO);
		if (qs == NULL) {
			error = ENOMEM;
			goto fail;
		}

		NET_LOCK();
		PF_LOCK();
		if (q->ticket != pf_main_ruleset.rules.inactive.version) {
			error = EBUSY;
			PF_UNLOCK();
			NET_UNLOCK();
			pool_put(&pf_queue_pl, qs);
			goto fail;
		}
		memcpy(qs, &q->queue, sizeof(*qs));
		qs->qid = pf_qname2qid(qs->qname, 1);
		if (qs->qid == 0) {
			error = EBUSY;
			PF_UNLOCK();
			NET_UNLOCK();
			pool_put(&pf_queue_pl, qs);
			goto fail;
		}
		if (qs->parent[0] && (qs->parent_qid =
		    pf_qname2qid(qs->parent, 0)) == 0) {
			error = ESRCH;
			PF_UNLOCK();
			NET_UNLOCK();
			pool_put(&pf_queue_pl, qs);
			goto fail;
		}
		qs->kif = pfi_kif_get(qs->ifname, NULL);
		if (qs->kif == NULL) {
			error = ESRCH;
			PF_UNLOCK();
			NET_UNLOCK();
			pool_put(&pf_queue_pl, qs);
			goto fail;
		}
		/* XXX resolve bw percentage specs */
		pfi_kif_ref(qs->kif, PFI_KIF_REF_RULE);

		TAILQ_INSERT_TAIL(pf_queues_inactive, qs, entries);
		PF_UNLOCK();
		NET_UNLOCK();

		break;
	}

	case DIOCADDRULE: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule, *tail;

		rule = pool_get(&pf_rule_pl, PR_WAITOK|PR_LIMITFAIL|PR_ZERO);
		if (rule == NULL) {
			error = ENOMEM;
			goto fail;
		}

		if ((error = pf_rule_copyin(&pr->rule, rule))) {
			pf_rule_free(rule);
			rule = NULL;
			goto fail;
		}

		if (pr->rule.return_icmp >> 8 > ICMP_MAXTYPE) {
			error = EINVAL;
			pf_rule_free(rule);
			rule = NULL;
			goto fail;
		}
		if ((error = pf_rule_checkaf(rule))) {
			pf_rule_free(rule);
			rule = NULL;
			goto fail;
		}
		if (rule->src.addr.type == PF_ADDR_NONE ||
		    rule->dst.addr.type == PF_ADDR_NONE) {
			error = EINVAL;
			pf_rule_free(rule);
			rule = NULL;
			goto fail;
		}

		if (rule->rt && !rule->direction) {
			error = EINVAL;
			pf_rule_free(rule);
			rule = NULL;
			goto fail;
		}

		NET_LOCK();
		PF_LOCK();
		pr->anchor[sizeof(pr->anchor) - 1] = '\0';
		ruleset = pf_find_ruleset(pr->anchor);
		if (ruleset == NULL) {
			error = EINVAL;
			PF_UNLOCK();
			NET_UNLOCK();
			pf_rule_free(rule);
			goto fail;
		}
		if (pr->ticket != ruleset->rules.inactive.version) {
			error = EBUSY;
			PF_UNLOCK();
			NET_UNLOCK();
			pf_rule_free(rule);
			goto fail;
		}
		rule->cuid = p->p_ucred->cr_ruid;
		rule->cpid = p->p_p->ps_pid;

		tail = TAILQ_LAST(ruleset->rules.inactive.ptr,
		    pf_rulequeue);
		if (tail)
			rule->nr = tail->nr + 1;
		else
			rule->nr = 0;

		rule->kif = pf_kif_setup(rule->kif);
		rule->rcv_kif = pf_kif_setup(rule->rcv_kif);
		rule->rdr.kif = pf_kif_setup(rule->rdr.kif);
		rule->nat.kif = pf_kif_setup(rule->nat.kif);
		rule->route.kif = pf_kif_setup(rule->route.kif);

		if (rule->overload_tblname[0]) {
			if ((rule->overload_tbl = pfr_attach_table(ruleset,
			    rule->overload_tblname, PR_WAITOK)) == NULL)
				error = EINVAL;
			else
				rule->overload_tbl->pfrkt_flags |= PFR_TFLAG_ACTIVE;
		}

		if (pf_addr_setup(ruleset, &rule->src.addr, rule->af))
			error = EINVAL;
		if (pf_addr_setup(ruleset, &rule->dst.addr, rule->af))
			error = EINVAL;
		if (pf_addr_setup(ruleset, &rule->rdr.addr, rule->af))
			error = EINVAL;
		if (pf_addr_setup(ruleset, &rule->nat.addr, rule->af))
			error = EINVAL;
		if (pf_addr_setup(ruleset, &rule->route.addr, rule->af))
			error = EINVAL;
		if (pf_anchor_setup(rule, ruleset, pr->anchor_call))
			error = EINVAL;

		if (error) {
			pf_rm_rule(NULL, rule);
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}
		TAILQ_INSERT_TAIL(ruleset->rules.inactive.ptr,
		    rule, entries);
		ruleset->rules.inactive.rcount++;
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCGETRULES: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule;
		struct pf_trans		*t;
		u_int32_t		 ruleset_version;

		NET_LOCK();
		PF_LOCK();
		pr->anchor[sizeof(pr->anchor) - 1] = '\0';
		ruleset = pf_find_ruleset(pr->anchor);
		if (ruleset == NULL) {
			error = EINVAL;
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}
		rule = TAILQ_LAST(ruleset->rules.active.ptr, pf_rulequeue);
		if (rule)
			pr->nr = rule->nr + 1;
		else
			pr->nr = 0;
		ruleset_version = ruleset->rules.active.version;
		pf_anchor_take(ruleset->anchor);
		rule = TAILQ_FIRST(ruleset->rules.active.ptr);
		PF_UNLOCK();
		NET_UNLOCK();

		t = pf_open_trans(minor(dev));
		if (t == NULL) {
			error = EBUSY;
			goto fail;
		}
		pf_init_tgetrule(t, ruleset->anchor, ruleset_version, rule);
		pr->ticket = t->pft_ticket;

		break;
	}

	case DIOCGETRULE: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule;
		struct pf_trans		*t;
		int			 i;

		t = pf_find_trans(minor(dev), pr->ticket);
		if (t == NULL) {
			error = ENXIO;
			goto fail;
		}
		KASSERT(t->pft_unit == minor(dev));
		if (t->pft_type != PF_TRANS_GETRULE) {
			error = EINVAL;
			goto fail;
		}

		NET_LOCK();
		PF_LOCK();
		KASSERT(t->pftgr_anchor != NULL);
		ruleset = &t->pftgr_anchor->ruleset;
		if (t->pftgr_version != ruleset->rules.active.version) {
			error = EBUSY;
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}
		rule = t->pftgr_rule;
		if (rule == NULL) {
			error = ENOENT;
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}
		memcpy(&pr->rule, rule, sizeof(struct pf_rule));
		memset(&pr->rule.entries, 0, sizeof(pr->rule.entries));
		pr->rule.kif = NULL;
		pr->rule.nat.kif = NULL;
		pr->rule.rdr.kif = NULL;
		pr->rule.route.kif = NULL;
		pr->rule.rcv_kif = NULL;
		pr->rule.anchor = NULL;
		pr->rule.overload_tbl = NULL;
		pr->rule.pktrate.limit /= PF_THRESHOLD_MULT;
		if (pf_anchor_copyout(ruleset, rule, pr)) {
			error = EBUSY;
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}
		pf_addr_copyout(&pr->rule.src.addr);
		pf_addr_copyout(&pr->rule.dst.addr);
		pf_addr_copyout(&pr->rule.rdr.addr);
		pf_addr_copyout(&pr->rule.nat.addr);
		pf_addr_copyout(&pr->rule.route.addr);
		for (i = 0; i < PF_SKIP_COUNT; ++i)
			if (rule->skip[i].ptr == NULL)
				pr->rule.skip[i].nr = (u_int32_t)-1;
			else
				pr->rule.skip[i].nr =
				    rule->skip[i].ptr->nr;

		if (pr->action == PF_GET_CLR_CNTR) {
			rule->evaluations = 0;
			rule->packets[0] = rule->packets[1] = 0;
			rule->bytes[0] = rule->bytes[1] = 0;
			rule->states_tot = 0;
		}
		pr->nr = rule->nr;
		t->pftgr_rule = TAILQ_NEXT(rule, entries);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCCHANGERULE: {
		struct pfioc_rule	*pcr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*oldrule = NULL, *newrule = NULL;
		u_int32_t		 nr = 0;

		if (pcr->action < PF_CHANGE_ADD_HEAD ||
		    pcr->action > PF_CHANGE_GET_TICKET) {
			error = EINVAL;
			goto fail;
		}

		if (pcr->action == PF_CHANGE_GET_TICKET) {
			NET_LOCK();
			PF_LOCK();

			ruleset = pf_find_ruleset(pcr->anchor);
			if (ruleset == NULL)
				error = EINVAL;
			else
				pcr->ticket = ++ruleset->rules.active.version;

			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}

		if (pcr->action != PF_CHANGE_REMOVE) {
			newrule = pool_get(&pf_rule_pl,
			    PR_WAITOK|PR_LIMITFAIL|PR_ZERO);
			if (newrule == NULL) {
				error = ENOMEM;
				goto fail;
			}

			if (pcr->rule.return_icmp >> 8 > ICMP_MAXTYPE) {
				error = EINVAL;
				pool_put(&pf_rule_pl, newrule);
				goto fail;
			}
			error = pf_rule_copyin(&pcr->rule, newrule);
			if (error != 0) {
				pf_rule_free(newrule);
				newrule = NULL;
				goto fail;
			}
			if ((error = pf_rule_checkaf(newrule))) {
				pf_rule_free(newrule);
				newrule = NULL;
				goto fail;
			}
			if (newrule->rt && !newrule->direction) {
				pf_rule_free(newrule);
				error = EINVAL;
				newrule = NULL;
				goto fail;
			}
		}

		NET_LOCK();
		PF_LOCK();
		ruleset = pf_find_ruleset(pcr->anchor);
		if (ruleset == NULL) {
			error = EINVAL;
			PF_UNLOCK();
			NET_UNLOCK();
			pf_rule_free(newrule);
			goto fail;
		}

		if (pcr->ticket != ruleset->rules.active.version) {
			error = EINVAL;
			PF_UNLOCK();
			NET_UNLOCK();
			pf_rule_free(newrule);
			goto fail;
		}

		if (pcr->action != PF_CHANGE_REMOVE) {
			KASSERT(newrule != NULL);
			newrule->cuid = p->p_ucred->cr_ruid;
			newrule->cpid = p->p_p->ps_pid;

			newrule->kif = pf_kif_setup(newrule->kif);
			newrule->rcv_kif = pf_kif_setup(newrule->rcv_kif);
			newrule->rdr.kif = pf_kif_setup(newrule->rdr.kif);
			newrule->nat.kif = pf_kif_setup(newrule->nat.kif);
			newrule->route.kif = pf_kif_setup(newrule->route.kif);

			if (newrule->overload_tblname[0]) {
				newrule->overload_tbl = pfr_attach_table(
				    ruleset, newrule->overload_tblname,
				    PR_WAITOK);
				if (newrule->overload_tbl == NULL)
					error = EINVAL;
				else
					newrule->overload_tbl->pfrkt_flags |=
					    PFR_TFLAG_ACTIVE;
			}

			if (pf_addr_setup(ruleset, &newrule->src.addr,
			    newrule->af))
				error = EINVAL;
			if (pf_addr_setup(ruleset, &newrule->dst.addr,
			    newrule->af))
				error = EINVAL;
			if (pf_addr_setup(ruleset, &newrule->rdr.addr,
			    newrule->af))
				error = EINVAL;
			if (pf_addr_setup(ruleset, &newrule->nat.addr,
			    newrule->af))
				error = EINVAL;
			if (pf_addr_setup(ruleset, &newrule->route.addr,
			    newrule->af))
				error = EINVAL;
			if (pf_anchor_setup(newrule, ruleset, pcr->anchor_call))
				error = EINVAL;

			if (error) {
				pf_rm_rule(NULL, newrule);
				PF_UNLOCK();
				NET_UNLOCK();
				goto fail;
			}
		}

		if (pcr->action == PF_CHANGE_ADD_HEAD)
			oldrule = TAILQ_FIRST(ruleset->rules.active.ptr);
		else if (pcr->action == PF_CHANGE_ADD_TAIL)
			oldrule = TAILQ_LAST(ruleset->rules.active.ptr,
			    pf_rulequeue);
		else {
			oldrule = TAILQ_FIRST(ruleset->rules.active.ptr);
			while ((oldrule != NULL) && (oldrule->nr != pcr->nr))
				oldrule = TAILQ_NEXT(oldrule, entries);
			if (oldrule == NULL) {
				if (newrule != NULL)
					pf_rm_rule(NULL, newrule);
				error = EINVAL;
				PF_UNLOCK();
				NET_UNLOCK();
				goto fail;
			}
		}

		if (pcr->action == PF_CHANGE_REMOVE) {
			pf_rm_rule(ruleset->rules.active.ptr, oldrule);
			ruleset->rules.active.rcount--;
		} else {
			if (oldrule == NULL)
				TAILQ_INSERT_TAIL(
				    ruleset->rules.active.ptr,
				    newrule, entries);
			else if (pcr->action == PF_CHANGE_ADD_HEAD ||
			    pcr->action == PF_CHANGE_ADD_BEFORE)
				TAILQ_INSERT_BEFORE(oldrule, newrule, entries);
			else
				TAILQ_INSERT_AFTER(
				    ruleset->rules.active.ptr,
				    oldrule, newrule, entries);
			ruleset->rules.active.rcount++;
		}

		nr = 0;
		TAILQ_FOREACH(oldrule, ruleset->rules.active.ptr, entries)
			oldrule->nr = nr++;

		ruleset->rules.active.version++;

		pf_calc_skip_steps(ruleset->rules.active.ptr);
		pf_remove_if_empty_ruleset(ruleset);

		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCCLRSTATES:
		error = pf_states_clr((struct pfioc_state_kill *)addr);
		break;

	case DIOCKILLSTATES: {
		struct pf_state		*st, *nextst;
		struct pf_state_item	*si, *sit;
		struct pf_state_key	*sk, key;
		struct pf_addr		*srcaddr, *dstaddr;
		u_int16_t		 srcport, dstport;
		struct pfioc_state_kill	*psk = (struct pfioc_state_kill *)addr;
		u_int			 i, killed = 0;
		const int		 dirs[] = { PF_IN, PF_OUT };
		int			 sidx, didx;

		if (psk->psk_pfcmp.id) {
			if (psk->psk_pfcmp.creatorid == 0)
				psk->psk_pfcmp.creatorid = pf_status.hostid;
			NET_LOCK();
			PF_LOCK();
			PF_STATE_ENTER_WRITE();
			if ((st = pf_find_state_byid(&psk->psk_pfcmp))) {
				pf_remove_state(st);
				psk->psk_killed = 1;
			}
			PF_STATE_EXIT_WRITE();
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}

		if (psk->psk_af && psk->psk_proto &&
		    psk->psk_src.port_op == PF_OP_EQ &&
		    psk->psk_dst.port_op == PF_OP_EQ) {

			key.af = psk->psk_af;
			key.proto = psk->psk_proto;
			key.rdomain = psk->psk_rdomain;

			NET_LOCK();
			PF_LOCK();
			PF_STATE_ENTER_WRITE();
			for (i = 0; i < nitems(dirs); i++) {
				if (dirs[i] == PF_IN) {
					sidx = 0;
					didx = 1;
				} else {
					sidx = 1;
					didx = 0;
				}
				pf_addrcpy(&key.addr[sidx],
				    &psk->psk_src.addr.v.a.addr, key.af);
				pf_addrcpy(&key.addr[didx],
				    &psk->psk_dst.addr.v.a.addr, key.af);
				key.port[sidx] = psk->psk_src.port[0];
				key.port[didx] = psk->psk_dst.port[0];

				sk = RBT_FIND(pf_state_tree, &pf_statetbl,
				    &key);
				if (sk == NULL)
					continue;

				TAILQ_FOREACH_SAFE(si, &sk->sk_states,
				    si_entry, sit) {
					struct pf_state *sist = si->si_st;
					if (((sist->key[PF_SK_WIRE]->af ==
					    sist->key[PF_SK_STACK]->af &&
					    sk == (dirs[i] == PF_IN ?
					    sist->key[PF_SK_WIRE] :
					    sist->key[PF_SK_STACK])) ||
					    (sist->key[PF_SK_WIRE]->af !=
					    sist->key[PF_SK_STACK]->af &&
					    dirs[i] == PF_IN &&
					    (sk == sist->key[PF_SK_STACK] ||
					    sk == sist->key[PF_SK_WIRE]))) &&
					    (!psk->psk_ifname[0] ||
					    (sist->kif != pfi_all &&
					    !strcmp(psk->psk_ifname,
					    sist->kif->pfik_name)))) {
						pf_remove_state(sist);
						killed++;
					}
				}
			}
			if (killed)
				psk->psk_killed = killed;
			PF_STATE_EXIT_WRITE();
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}

		NET_LOCK();
		PF_LOCK();
		PF_STATE_ENTER_WRITE();
		RBT_FOREACH_SAFE(st, pf_state_tree_id, &tree_id, nextst) {
			if (st->direction == PF_OUT) {
				sk = st->key[PF_SK_STACK];
				srcaddr = &sk->addr[1];
				dstaddr = &sk->addr[0];
				srcport = sk->port[1];
				dstport = sk->port[0];
			} else {
				sk = st->key[PF_SK_WIRE];
				srcaddr = &sk->addr[0];
				dstaddr = &sk->addr[1];
				srcport = sk->port[0];
				dstport = sk->port[1];
			}
			if ((!psk->psk_af || sk->af == psk->psk_af)
			    && (!psk->psk_proto || psk->psk_proto ==
			    sk->proto) && psk->psk_rdomain == sk->rdomain &&
			    pf_match_addr(psk->psk_src.neg,
			    &psk->psk_src.addr.v.a.addr,
			    &psk->psk_src.addr.v.a.mask,
			    srcaddr, sk->af) &&
			    pf_match_addr(psk->psk_dst.neg,
			    &psk->psk_dst.addr.v.a.addr,
			    &psk->psk_dst.addr.v.a.mask,
			    dstaddr, sk->af) &&
			    (psk->psk_src.port_op == 0 ||
			    pf_match_port(psk->psk_src.port_op,
			    psk->psk_src.port[0], psk->psk_src.port[1],
			    srcport)) &&
			    (psk->psk_dst.port_op == 0 ||
			    pf_match_port(psk->psk_dst.port_op,
			    psk->psk_dst.port[0], psk->psk_dst.port[1],
			    dstport)) &&
			    (!psk->psk_label[0] || (st->rule.ptr->label[0] &&
			    !strcmp(psk->psk_label, st->rule.ptr->label))) &&
			    (!psk->psk_ifname[0] || !strcmp(psk->psk_ifname,
			    st->kif->pfik_name))) {
				pf_remove_state(st);
				killed++;
			}
		}
		psk->psk_killed = killed;
		PF_STATE_EXIT_WRITE();
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

#if NPFSYNC > 0
	case DIOCADDSTATE: {
		struct pfioc_state	*ps = (struct pfioc_state *)addr;
		struct pfsync_state	*sp = &ps->state;

		if (sp->timeout >= PFTM_MAX) {
			error = EINVAL;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pf_state_import(sp, PFSYNC_SI_IOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}
#endif	/* NPFSYNC > 0 */

	case DIOCGETSTATE: {
		struct pfioc_state	*ps = (struct pfioc_state *)addr;
		struct pf_state		*st;
		struct pf_state_cmp	 id_key;

		memset(&id_key, 0, sizeof(id_key));
		id_key.id = ps->state.id;
		id_key.creatorid = ps->state.creatorid;

		NET_LOCK();
		PF_STATE_ENTER_READ();
		st = pf_find_state_byid(&id_key);
		st = pf_state_ref(st);
		PF_STATE_EXIT_READ();
		NET_UNLOCK();
		if (st == NULL) {
			error = ENOENT;
			goto fail;
		}

		pf_state_export(&ps->state, st);
		pf_state_unref(st);
		break;
	}

	case DIOCGETSTATES: 
		error = pf_states_get((struct pfioc_states *)addr);
		break;

	case DIOCGETSTATUS: {
		struct pf_status *s = (struct pf_status *)addr;
		NET_LOCK();
		PF_LOCK();
		PF_FRAG_LOCK();
		memcpy(s, &pf_status, sizeof(struct pf_status));
		PF_FRAG_UNLOCK();
		pfi_update_status(s->ifname, s);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCSETSTATUSIF: {
		struct pfioc_iface	*pi = (struct pfioc_iface *)addr;

		NET_LOCK();
		PF_LOCK();
		if (pi->pfiio_name[0] == 0) {
			memset(pf_status.ifname, 0, IFNAMSIZ);
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}
		strlcpy(pf_trans_set.statusif, pi->pfiio_name, IFNAMSIZ);
		pf_trans_set.mask |= PF_TSET_STATUSIF;
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCCLRSTATUS: {
		struct pfioc_iface	*pi = (struct pfioc_iface *)addr;

		NET_LOCK();
		PF_LOCK();
		/* if ifname is specified, clear counters there only */
		if (pi->pfiio_name[0]) {
			pfi_update_status(pi->pfiio_name, NULL);
			PF_UNLOCK();
			NET_UNLOCK();
			goto fail;
		}

		memset(pf_status.counters, 0, sizeof(pf_status.counters));
		memset(pf_status.fcounters, 0, sizeof(pf_status.fcounters));
		memset(pf_status.scounters, 0, sizeof(pf_status.scounters));
		PF_FRAG_LOCK();
		memset(pf_status.ncounters, 0, sizeof(pf_status.ncounters));
		PF_FRAG_UNLOCK();
		pf_status.since = getuptime();

		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCNATLOOK: {
		struct pfioc_natlook	*pnl = (struct pfioc_natlook *)addr;
		struct pf_state_key	*sk;
		struct pf_state		*st;
		struct pf_state_key_cmp	 key;
		int			 m = 0, direction = pnl->direction;
		int			 sidx, didx;

		switch (pnl->af) {
		case AF_INET:
			break;
#ifdef INET6
		case AF_INET6:
			break;
#endif /* INET6 */
		default:
			error = EAFNOSUPPORT;
			goto fail;
		}

		/* NATLOOK src and dst are reversed, so reverse sidx/didx */
		sidx = (direction == PF_IN) ? 1 : 0;
		didx = (direction == PF_IN) ? 0 : 1;

		if (!pnl->proto ||
		    PF_AZERO(&pnl->saddr, pnl->af) ||
		    PF_AZERO(&pnl->daddr, pnl->af) ||
		    ((pnl->proto == IPPROTO_TCP ||
		    pnl->proto == IPPROTO_UDP) &&
		    (!pnl->dport || !pnl->sport)) ||
		    pnl->rdomain > RT_TABLEID_MAX)
			error = EINVAL;
		else {
			key.af = pnl->af;
			key.proto = pnl->proto;
			key.rdomain = pnl->rdomain;
			pf_addrcpy(&key.addr[sidx], &pnl->saddr, pnl->af);
			key.port[sidx] = pnl->sport;
			pf_addrcpy(&key.addr[didx], &pnl->daddr, pnl->af);
			key.port[didx] = pnl->dport;

			NET_LOCK();
			PF_STATE_ENTER_READ();
			st = pf_find_state_all(&key, direction, &m);
			st = pf_state_ref(st);
			PF_STATE_EXIT_READ();
			NET_UNLOCK();

			if (m > 1)
				error = E2BIG;	/* more than one state */
			else if (st != NULL) {
				sk = st->key[sidx];
				pf_addrcpy(&pnl->rsaddr, &sk->addr[sidx],
				    sk->af);
				pnl->rsport = sk->port[sidx];
				pf_addrcpy(&pnl->rdaddr, &sk->addr[didx],
				    sk->af);
				pnl->rdport = sk->port[didx];
				pnl->rrdomain = sk->rdomain;
			} else
				error = ENOENT;
			pf_state_unref(st);
		}
		break;
	}

	case DIOCSETTIMEOUT: {
		struct pfioc_tm	*pt = (struct pfioc_tm *)addr;

		if (pt->timeout < 0 || pt->timeout >= PFTM_MAX ||
		    pt->seconds < 0) {
			error = EINVAL;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		if (pt->timeout == PFTM_INTERVAL && pt->seconds == 0)
			pt->seconds = 1;
		pf_default_rule_new.timeout[pt->timeout] = pt->seconds;
		pt->seconds = pf_default_rule.timeout[pt->timeout];
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCGETTIMEOUT: {
		struct pfioc_tm	*pt = (struct pfioc_tm *)addr;

		if (pt->timeout < 0 || pt->timeout >= PFTM_MAX) {
			error = EINVAL;
			goto fail;
		}
		PF_LOCK();
		pt->seconds = pf_default_rule.timeout[pt->timeout];
		PF_UNLOCK();
		break;
	}

	case DIOCGETLIMIT: {
		struct pfioc_limit	*pl = (struct pfioc_limit *)addr;

		if (pl->index < 0 || pl->index >= PF_LIMIT_MAX) {
			error = EINVAL;
			goto fail;
		}
		PF_LOCK();
		pl->limit = pf_pool_limits[pl->index].limit;
		PF_UNLOCK();
		break;
	}

	case DIOCSETLIMIT: {
		struct pfioc_limit	*pl = (struct pfioc_limit *)addr;

		PF_LOCK();
		if (pl->index < 0 || pl->index >= PF_LIMIT_MAX) {
			error = EINVAL;
			PF_UNLOCK();
			goto fail;
		}
		if (((struct pool *)pf_pool_limits[pl->index].pp)->pr_nout >
		    pl->limit) {
			error = EBUSY;
			PF_UNLOCK();
			goto fail;
		}
		/* Fragments reference mbuf clusters. */
		if (pl->index == PF_LIMIT_FRAGS &&
		    pl->limit > atomic_load_long(&nmbclust)) {
			error = EINVAL;
			PF_UNLOCK();
			goto fail;
		}

		error = pool_sethardlimit(pf_pool_limits[pl->index].pp,
		    pl->limit);
		if (error == 0) {
			pf_pool_limits[pl->index].limit_new = pl->limit;
			pf_pool_limits[pl->index].limit = pl->limit;
		}
		PF_UNLOCK();
		break;
	}

	case DIOCSETDEBUG: {
		u_int32_t	*level = (u_int32_t *)addr;

		NET_LOCK();
		PF_LOCK();
		pf_trans_set.debug = *level;
		pf_trans_set.mask |= PF_TSET_DEBUG;
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCGETRULESETS: {
		struct pfioc_ruleset	*pr = (struct pfioc_ruleset *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_anchor	*anchor;

		PF_LOCK();
		pr->path[sizeof(pr->path) - 1] = '\0';
		if ((ruleset = pf_find_ruleset(pr->path)) == NULL) {
			error = EINVAL;
			PF_UNLOCK();
			goto fail;
		}
		pr->nr = 0;
		if (ruleset == &pf_main_ruleset) {
			/* XXX kludge for pf_main_ruleset */
			RB_FOREACH(anchor, pf_anchor_global, &pf_anchors)
				if (anchor->parent == NULL)
					pr->nr++;
		} else {
			RB_FOREACH(anchor, pf_anchor_node,
			    &ruleset->anchor->children)
				pr->nr++;
		}
		PF_UNLOCK();
		break;
	}

	case DIOCGETRULESET: {
		struct pfioc_ruleset	*pr = (struct pfioc_ruleset *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_anchor	*anchor;
		u_int32_t		 nr = 0;

		PF_LOCK();
		pr->path[sizeof(pr->path) - 1] = '\0';
		if ((ruleset = pf_find_ruleset(pr->path)) == NULL) {
			error = EINVAL;
			PF_UNLOCK();
			goto fail;
		}
		pr->name[0] = '\0';
		if (ruleset == &pf_main_ruleset) {
			/* XXX kludge for pf_main_ruleset */
			RB_FOREACH(anchor, pf_anchor_global, &pf_anchors)
				if (anchor->parent == NULL && nr++ == pr->nr) {
					strlcpy(pr->name, anchor->name,
					    sizeof(pr->name));
					break;
				}
		} else {
			RB_FOREACH(anchor, pf_anchor_node,
			    &ruleset->anchor->children)
				if (nr++ == pr->nr) {
					strlcpy(pr->name, anchor->name,
					    sizeof(pr->name));
					break;
				}
		}
		PF_UNLOCK();
		if (!pr->name[0])
			error = EBUSY;
		break;
	}

	case DIOCRCLRTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_clr_tables(&io->pfrio_table, &io->pfrio_ndel,
		    io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRADDTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			goto fail;
		}
		error = pfr_add_tables(io->pfrio_buffer, io->pfrio_size,
		    &io->pfrio_nadd, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRDELTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_del_tables(io->pfrio_buffer, io->pfrio_size,
		    &io->pfrio_ndel, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRGETTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_get_tables(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRGETTSTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_tstats)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_get_tstats(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRCLRTSTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_clr_tstats(io->pfrio_buffer, io->pfrio_size,
		    &io->pfrio_nzero, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRSETTFLAGS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_set_tflags(io->pfrio_buffer, io->pfrio_size,
		    io->pfrio_setflag, io->pfrio_clrflag, &io->pfrio_nchange,
		    &io->pfrio_ndel, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRCLRADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_clr_addrs(&io->pfrio_table, &io->pfrio_ndel,
		    io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRADDADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			goto fail;
		}
		error = pfr_add_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nadd, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		break;
	}

	case DIOCRDELADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_del_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_ndel, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRSETADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_set_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_size2, &io->pfrio_nadd,
		    &io->pfrio_ndel, &io->pfrio_nchange, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL, 0);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRGETADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_get_addrs(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRGETASTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_astats)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_get_astats(&io->pfrio_table, io->pfrio_buffer,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRCLRASTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_clr_astats(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nzero, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRTSTADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_tst_addrs(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nmatch, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCRINADEFINE: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			goto fail;
		}
		NET_LOCK();
		PF_LOCK();
		error = pfr_ina_define(&io->pfrio_table, io->pfrio_buffer,
		    io->pfrio_size, &io->pfrio_nadd, &io->pfrio_naddr,
		    io->pfrio_ticket, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCOSFPADD: {
		struct pf_osfp_ioctl *io = (struct pf_osfp_ioctl *)addr;
		error = pf_osfp_add(io);
		break;
	}

	case DIOCOSFPGET: {
		struct pf_osfp_ioctl *io = (struct pf_osfp_ioctl *)addr;
		error = pf_osfp_get(io);
		break;
	}

	case DIOCXBEGIN: {
		struct pfioc_trans	*io = (struct pfioc_trans *)addr;
		struct pfioc_trans_e	*ioe;
		struct pfr_table	*table;
		int			 i;

		if (io->esize != sizeof(*ioe)) {
			error = ENODEV;
			goto fail;
		}
		ioe = malloc(sizeof(*ioe), M_PF, M_WAITOK);
		table = malloc(sizeof(*table), M_PF, M_WAITOK);
		NET_LOCK();
		PF_LOCK();
		pf_default_rule_new = pf_default_rule;
		PF_UNLOCK();
		NET_UNLOCK();
		memset(&pf_trans_set, 0, sizeof(pf_trans_set));
		for (i = 0; i < io->size; i++) {
			if (copyin(io->array+i, ioe, sizeof(*ioe))) {
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = EFAULT;
				goto fail;
			}
			if (strnlen(ioe->anchor, sizeof(ioe->anchor)) ==
			    sizeof(ioe->anchor)) {
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = ENAMETOOLONG;
				goto fail;
			}
			NET_LOCK();
			PF_LOCK();
			switch (ioe->type) {
			case PF_TRANS_TABLE:
				memset(table, 0, sizeof(*table));
				strlcpy(table->pfrt_anchor, ioe->anchor,
				    sizeof(table->pfrt_anchor));
				if ((error = pfr_ina_begin(table,
				    &ioe->ticket, NULL, 0))) {
					PF_UNLOCK();
					NET_UNLOCK();
					free(table, M_PF, sizeof(*table));
					free(ioe, M_PF, sizeof(*ioe));
					goto fail;
				}
				break;
			case PF_TRANS_RULESET:
				if ((error = pf_begin_rules(&ioe->ticket,
				    ioe->anchor))) {
					PF_UNLOCK();
					NET_UNLOCK();
					free(table, M_PF, sizeof(*table));
					free(ioe, M_PF, sizeof(*ioe));
					goto fail;
				}
				break;
			default:
				PF_UNLOCK();
				NET_UNLOCK();
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = EINVAL;
				goto fail;
			}
			PF_UNLOCK();
			NET_UNLOCK();
			if (copyout(ioe, io->array+i, sizeof(io->array[i]))) {
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = EFAULT;
				goto fail;
			}
		}
		free(table, M_PF, sizeof(*table));
		free(ioe, M_PF, sizeof(*ioe));
		break;
	}

	case DIOCXROLLBACK: {
		struct pfioc_trans	*io = (struct pfioc_trans *)addr;
		struct pfioc_trans_e	*ioe;
		struct pfr_table	*table;
		int			 i;

		if (io->esize != sizeof(*ioe)) {
			error = ENODEV;
			goto fail;
		}
		ioe = malloc(sizeof(*ioe), M_PF, M_WAITOK);
		table = malloc(sizeof(*table), M_PF, M_WAITOK);
		for (i = 0; i < io->size; i++) {
			if (copyin(io->array+i, ioe, sizeof(*ioe))) {
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = EFAULT;
				goto fail;
			}
			if (strnlen(ioe->anchor, sizeof(ioe->anchor)) ==
			    sizeof(ioe->anchor)) {
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = ENAMETOOLONG;
				goto fail;
			}
			NET_LOCK();
			PF_LOCK();
			switch (ioe->type) {
			case PF_TRANS_TABLE:
				memset(table, 0, sizeof(*table));
				strlcpy(table->pfrt_anchor, ioe->anchor,
				    sizeof(table->pfrt_anchor));
				if ((error = pfr_ina_rollback(table,
				    ioe->ticket, NULL, 0))) {
					PF_UNLOCK();
					NET_UNLOCK();
					free(table, M_PF, sizeof(*table));
					free(ioe, M_PF, sizeof(*ioe));
					goto fail; /* really bad */
				}
				break;
			case PF_TRANS_RULESET:
				pf_rollback_rules(ioe->ticket, ioe->anchor);
				break;
			default:
				PF_UNLOCK();
				NET_UNLOCK();
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = EINVAL;
				goto fail; /* really bad */
			}
			PF_UNLOCK();
			NET_UNLOCK();
		}
		free(table, M_PF, sizeof(*table));
		free(ioe, M_PF, sizeof(*ioe));
		break;
	}

	case DIOCXCOMMIT: {
		struct pfioc_trans	*io = (struct pfioc_trans *)addr;
		struct pfioc_trans_e	*ioe;
		struct pfr_table	*table;
		struct pf_ruleset	*rs;
		int			 i;

		if (io->esize != sizeof(*ioe)) {
			error = ENODEV;
			goto fail;
		}
		ioe = malloc(sizeof(*ioe), M_PF, M_WAITOK);
		table = malloc(sizeof(*table), M_PF, M_WAITOK);
		/* first makes sure everything will succeed */
		for (i = 0; i < io->size; i++) {
			if (copyin(io->array+i, ioe, sizeof(*ioe))) {
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = EFAULT;
				goto fail;
			}
			if (strnlen(ioe->anchor, sizeof(ioe->anchor)) ==
			    sizeof(ioe->anchor)) {
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = ENAMETOOLONG;
				goto fail;
			}
			NET_LOCK();
			PF_LOCK();
			switch (ioe->type) {
			case PF_TRANS_TABLE:
				rs = pf_find_ruleset(ioe->anchor);
				if (rs == NULL || !rs->topen || ioe->ticket !=
				     rs->tticket) {
					PF_UNLOCK();
					NET_UNLOCK();
					free(table, M_PF, sizeof(*table));
					free(ioe, M_PF, sizeof(*ioe));
					error = EBUSY;
					goto fail;
				}
				break;
			case PF_TRANS_RULESET:
				rs = pf_find_ruleset(ioe->anchor);
				if (rs == NULL ||
				    !rs->rules.inactive.open ||
				    rs->rules.inactive.version !=
				    ioe->ticket) {
					PF_UNLOCK();
					NET_UNLOCK();
					free(table, M_PF, sizeof(*table));
					free(ioe, M_PF, sizeof(*ioe));
					error = EBUSY;
					goto fail;
				}
				break;
			default:
				PF_UNLOCK();
				NET_UNLOCK();
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = EINVAL;
				goto fail;
			}
			PF_UNLOCK();
			NET_UNLOCK();
		}
		NET_LOCK();
		PF_LOCK();

		/* now do the commit - no errors should happen here */
		for (i = 0; i < io->size; i++) {
			PF_UNLOCK();
			NET_UNLOCK();
			if (copyin(io->array+i, ioe, sizeof(*ioe))) {
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = EFAULT;
				goto fail;
			}
			if (strnlen(ioe->anchor, sizeof(ioe->anchor)) ==
			    sizeof(ioe->anchor)) {
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = ENAMETOOLONG;
				goto fail;
			}
			NET_LOCK();
			PF_LOCK();
			switch (ioe->type) {
			case PF_TRANS_TABLE:
				memset(table, 0, sizeof(*table));
				strlcpy(table->pfrt_anchor, ioe->anchor,
				    sizeof(table->pfrt_anchor));
				if ((error = pfr_ina_commit(table, ioe->ticket,
				    NULL, NULL, 0))) {
					PF_UNLOCK();
					NET_UNLOCK();
					free(table, M_PF, sizeof(*table));
					free(ioe, M_PF, sizeof(*ioe));
					goto fail; /* really bad */
				}
				break;
			case PF_TRANS_RULESET:
				if ((error = pf_commit_rules(ioe->ticket,
				    ioe->anchor))) {
					PF_UNLOCK();
					NET_UNLOCK();
					free(table, M_PF, sizeof(*table));
					free(ioe, M_PF, sizeof(*ioe));
					goto fail; /* really bad */
				}
				break;
			default:
				PF_UNLOCK();
				NET_UNLOCK();
				free(table, M_PF, sizeof(*table));
				free(ioe, M_PF, sizeof(*ioe));
				error = EINVAL;
				goto fail; /* really bad */
			}
		}
		for (i = 0; i < PFTM_MAX; i++) {
			int old = pf_default_rule.timeout[i];

			pf_default_rule.timeout[i] =
			    pf_default_rule_new.timeout[i];
			if (pf_default_rule.timeout[i] == PFTM_INTERVAL &&
			    pf_default_rule.timeout[i] < old &&
			    timeout_del(&pf_purge_to))
				task_add(systqmp, &pf_purge_task);
		}
		pfi_xcommit();
		pf_trans_set_commit();
		PF_UNLOCK();
		NET_UNLOCK();
		free(table, M_PF, sizeof(*table));
		free(ioe, M_PF, sizeof(*ioe));
		break;
	}

	case DIOCXEND: {
		u_int32_t	*ticket = (u_int32_t *)addr;
		struct pf_trans	*t;

		t = pf_find_trans(minor(dev), *ticket);
		if (t != NULL)
			pf_rollback_trans(t);
		else
			error = ENXIO;
		break;
	}

	case DIOCGETSRCNODES: {
		struct pfioc_src_nodes	*psn = (struct pfioc_src_nodes *)addr;
		struct pf_src_node	*n, *p, *pstore;
		u_int32_t		 nr = 0;
		size_t			 space = psn->psn_len;

		pstore = malloc(sizeof(*pstore), M_PF, M_WAITOK);

		NET_LOCK();
		PF_LOCK();
		if (space == 0) {
			RB_FOREACH(n, pf_src_tree, &tree_src_tracking)
				nr++;
			psn->psn_len = sizeof(struct pf_src_node) * nr;
			PF_UNLOCK();
			NET_UNLOCK();
			free(pstore, M_PF, sizeof(*pstore));
			goto fail;
		}

		p = psn->psn_src_nodes;
		RB_FOREACH(n, pf_src_tree, &tree_src_tracking) {
			int	secs = getuptime(), diff;

			if ((nr + 1) * sizeof(*p) > psn->psn_len)
				break;

			memcpy(pstore, n, sizeof(*pstore));
			memset(&pstore->entry, 0, sizeof(pstore->entry));
			pstore->rule.ptr = NULL;
			pstore->kif = NULL;
			pstore->rule.nr = n->rule.ptr->nr;
			pstore->creation = secs - pstore->creation;
			if (pstore->expire > secs)
				pstore->expire -= secs;
			else
				pstore->expire = 0;

			/* adjust the connection rate estimate */
			diff = secs - n->conn_rate.last;
			if (diff >= n->conn_rate.seconds)
				pstore->conn_rate.count = 0;
			else
				pstore->conn_rate.count -=
				    n->conn_rate.count * diff /
				    n->conn_rate.seconds;

			error = copyout(pstore, p, sizeof(*p));
			if (error) {
				PF_UNLOCK();
				NET_UNLOCK();
				free(pstore, M_PF, sizeof(*pstore));
				goto fail;
			}
			p++;
			nr++;
		}
		psn->psn_len = sizeof(struct pf_src_node) * nr;

		PF_UNLOCK();
		NET_UNLOCK();
		free(pstore, M_PF, sizeof(*pstore));
		break;
	}

	case DIOCCLRSRCNODES: {
		struct pf_src_node	*n;
		struct pf_state		*st;

		NET_LOCK();
		PF_LOCK();
		PF_STATE_ENTER_WRITE();
		RBT_FOREACH(st, pf_state_tree_id, &tree_id)
			pf_src_tree_remove_state(st);
		PF_STATE_EXIT_WRITE();
		RB_FOREACH(n, pf_src_tree, &tree_src_tracking)
			n->expire = 1;
		pf_purge_expired_src_nodes();
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCKILLSRCNODES: {
		struct pf_src_node	*sn;
		struct pf_state		*st;
		struct pfioc_src_node_kill *psnk =
		    (struct pfioc_src_node_kill *)addr;
		u_int			killed = 0;

		NET_LOCK();
		PF_LOCK();
		RB_FOREACH(sn, pf_src_tree, &tree_src_tracking) {
			if (pf_match_addr(psnk->psnk_src.neg,
				&psnk->psnk_src.addr.v.a.addr,
				&psnk->psnk_src.addr.v.a.mask,
				&sn->addr, sn->af) &&
			    pf_match_addr(psnk->psnk_dst.neg,
				&psnk->psnk_dst.addr.v.a.addr,
				&psnk->psnk_dst.addr.v.a.mask,
				&sn->raddr, sn->af)) {
				/* Handle state to src_node linkage */
				if (sn->states != 0) {
					PF_ASSERT_LOCKED();
					PF_STATE_ENTER_WRITE();
					RBT_FOREACH(st, pf_state_tree_id,
					   &tree_id)
						pf_state_rm_src_node(st, sn);
					PF_STATE_EXIT_WRITE();
				}
				sn->expire = 1;
				killed++;
			}
		}

		if (killed > 0)
			pf_purge_expired_src_nodes();

		psnk->psnk_killed = killed;
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCSETHOSTID: {
		u_int32_t	*hostid = (u_int32_t *)addr;

		NET_LOCK();
		PF_LOCK();
		if (*hostid == 0)
			pf_trans_set.hostid = arc4random();
		else
			pf_trans_set.hostid = *hostid;
		pf_trans_set.mask |= PF_TSET_HOSTID;
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCOSFPFLUSH:
		pf_osfp_flush();
		break;

	case DIOCIGETIFACES: {
		struct pfioc_iface	*io = (struct pfioc_iface *)addr;
		struct pfi_kif		*kif_buf;
		int			 apfiio_size = io->pfiio_size;

		if (io->pfiio_esize != sizeof(struct pfi_kif)) {
			error = ENODEV;
			goto fail;
		}

		if ((kif_buf = mallocarray(sizeof(*kif_buf), apfiio_size,
		    M_PF, M_WAITOK|M_CANFAIL)) == NULL) {
			error = EINVAL;
			goto fail;
		}

		NET_LOCK_SHARED();
		PF_LOCK();
		pfi_get_ifaces(io->pfiio_name, kif_buf, &io->pfiio_size);
		PF_UNLOCK();
		NET_UNLOCK_SHARED();
		if (copyout(kif_buf, io->pfiio_buffer, sizeof(*kif_buf) *
		    io->pfiio_size))
			error = EFAULT;
		free(kif_buf, M_PF, sizeof(*kif_buf) * apfiio_size);
		break;
	}

	case DIOCSETIFFLAG: {
		struct pfioc_iface *io = (struct pfioc_iface *)addr;

		if (io == NULL) {
			error = EINVAL;
			goto fail;
		}

		PF_LOCK();
		error = pfi_set_flags(io->pfiio_name, io->pfiio_flags);
		PF_UNLOCK();
		break;
	}

	case DIOCCLRIFFLAG: {
		struct pfioc_iface *io = (struct pfioc_iface *)addr;

		if (io == NULL) {
			error = EINVAL;
			goto fail;
		}

		PF_LOCK();
		error = pfi_clear_flags(io->pfiio_name, io->pfiio_flags);
		PF_UNLOCK();
		break;
	}

	case DIOCSETREASS: {
		u_int32_t	*reass = (u_int32_t *)addr;

		NET_LOCK();
		PF_LOCK();
		pf_trans_set.reass = *reass;
		pf_trans_set.mask |= PF_TSET_REASS;
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCSETSYNFLWATS: {
		struct pfioc_synflwats *io = (struct pfioc_synflwats *)addr;

		NET_LOCK();
		PF_LOCK();
		error = pf_syncookies_setwats(io->hiwat, io->lowat);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCGETSYNFLWATS: {
		struct pfioc_synflwats *io = (struct pfioc_synflwats *)addr;

		NET_LOCK();
		PF_LOCK();
		error = pf_syncookies_getwats(io);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	case DIOCSETSYNCOOKIES: {
		u_int8_t	*mode = (u_int8_t *)addr;

		NET_LOCK();
		PF_LOCK();
		error = pf_syncookies_setmode(*mode);
		PF_UNLOCK();
		NET_UNLOCK();
		break;
	}

	default:
		error = ENODEV;
		break;
	}
fail:
	rw_exit_write(&pfioctl_rw);

	return (error);
}

void
pf_trans_set_commit(void)
{
	if (pf_trans_set.mask & PF_TSET_STATUSIF)
		strlcpy(pf_status.ifname, pf_trans_set.statusif, IFNAMSIZ);
	if (pf_trans_set.mask & PF_TSET_DEBUG)
		pf_status.debug = pf_trans_set.debug;
	if (pf_trans_set.mask & PF_TSET_HOSTID)
		pf_status.hostid = pf_trans_set.hostid;
	if (pf_trans_set.mask & PF_TSET_REASS)
		pf_status.reass = pf_trans_set.reass;
}

void
pf_pool_copyin(struct pf_pool *from, struct pf_pool *to)
{
	memmove(to, from, sizeof(*to));
	to->kif = NULL;
	to->addr.p.tbl = NULL;
}

int
pf_validate_range(u_int8_t op, u_int16_t port[2], int order)
{
	u_int16_t a = (order == PF_ORDER_NET) ? ntohs(port[0]) : port[0];
	u_int16_t b = (order == PF_ORDER_NET) ? ntohs(port[1]) : port[1];

	if ((op == PF_OP_RRG && a > b) ||  /* 34:12,  i.e. none */
	    (op == PF_OP_IRG && a >= b) || /* 34><12, i.e. none */
	    (op == PF_OP_XRG && a > b))    /* 34<>22, i.e. all */
		return 1;
	return 0;
}

int
pf_rule_copyin(struct pf_rule *from, struct pf_rule *to)
{
	int i;

	if (from->scrub_flags & PFSTATE_SETPRIO &&
	    (from->set_prio[0] > IFQ_MAXPRIO ||
	    from->set_prio[1] > IFQ_MAXPRIO))
		return (EINVAL);

	to->src = from->src;
	to->src.addr.p.tbl = NULL;
	to->dst = from->dst;
	to->dst.addr.p.tbl = NULL;

	if (pf_validate_range(to->src.port_op, to->src.port, PF_ORDER_NET))
		return (EINVAL);
	if (pf_validate_range(to->dst.port_op, to->dst.port, PF_ORDER_NET))
		return (EINVAL);

	/* XXX union skip[] */

	strlcpy(to->label, from->label, sizeof(to->label));
	strlcpy(to->ifname, from->ifname, sizeof(to->ifname));
	strlcpy(to->rcv_ifname, from->rcv_ifname, sizeof(to->rcv_ifname));
	strlcpy(to->qname, from->qname, sizeof(to->qname));
	strlcpy(to->pqname, from->pqname, sizeof(to->pqname));
	strlcpy(to->tagname, from->tagname, sizeof(to->tagname));
	strlcpy(to->match_tagname, from->match_tagname,
	    sizeof(to->match_tagname));
	strlcpy(to->overload_tblname, from->overload_tblname,
	    sizeof(to->overload_tblname));

	pf_pool_copyin(&from->nat, &to->nat);
	pf_pool_copyin(&from->rdr, &to->rdr);
	pf_pool_copyin(&from->route, &to->route);

	if (pf_validate_range(to->rdr.port_op, to->rdr.proxy_port,
	    PF_ORDER_HOST))
		return (EINVAL);

	to->kif = (to->ifname[0]) ?
	    pfi_kif_alloc(to->ifname, M_WAITOK) : NULL;
	to->rcv_kif = (to->rcv_ifname[0]) ?
	    pfi_kif_alloc(to->rcv_ifname, M_WAITOK) : NULL;
	to->rdr.kif = (to->rdr.ifname[0]) ?
	    pfi_kif_alloc(to->rdr.ifname, M_WAITOK) : NULL;
	to->nat.kif = (to->nat.ifname[0]) ?
	    pfi_kif_alloc(to->nat.ifname, M_WAITOK) : NULL;
	to->route.kif = (to->route.ifname[0]) ?
	    pfi_kif_alloc(to->route.ifname, M_WAITOK) : NULL;

	to->os_fingerprint = from->os_fingerprint;

	to->rtableid = from->rtableid;
	if (to->rtableid >= 0 && !rtable_exists(to->rtableid))
		return (EBUSY);
	to->onrdomain = from->onrdomain;
	if (to->onrdomain != -1 && (to->onrdomain < 0 ||
	    to->onrdomain > RT_TABLEID_MAX))
		return (EINVAL);

	for (i = 0; i < PFTM_MAX; i++)
		to->timeout[i] = from->timeout[i];
	to->states_tot = from->states_tot;
	to->max_states = from->max_states;
	to->max_src_nodes = from->max_src_nodes;
	to->max_src_states = from->max_src_states;
	to->max_src_conn = from->max_src_conn;
	to->max_src_conn_rate.limit = from->max_src_conn_rate.limit;
	to->max_src_conn_rate.seconds = from->max_src_conn_rate.seconds;
	pf_init_threshold(&to->pktrate, from->pktrate.limit,
	    from->pktrate.seconds);

	if (to->qname[0] != 0) {
		if ((to->qid = pf_qname2qid(to->qname, 0)) == 0)
			return (EBUSY);
		if (to->pqname[0] != 0) {
			if ((to->pqid = pf_qname2qid(to->pqname, 0)) == 0)
				return (EBUSY);
		} else
			to->pqid = to->qid;
	}
	to->rt_listid = from->rt_listid;
	to->prob = from->prob;
	to->return_icmp = from->return_icmp;
	to->return_icmp6 = from->return_icmp6;
	to->max_mss = from->max_mss;
	if (to->tagname[0])
		if ((to->tag = pf_tagname2tag(to->tagname, 1)) == 0)
			return (EBUSY);
	if (to->match_tagname[0])
		if ((to->match_tag = pf_tagname2tag(to->match_tagname, 1)) == 0)
			return (EBUSY);
	to->scrub_flags = from->scrub_flags;
	to->delay = from->delay;
	to->uid = from->uid;
	to->gid = from->gid;
	to->rule_flag = from->rule_flag;
	to->action = from->action;
	to->direction = from->direction;
	to->log = from->log;
	to->logif = from->logif;
#if NPFLOG > 0
	if (!to->log)
		to->logif = 0;
#endif	/* NPFLOG > 0 */
	to->quick = from->quick;
	to->ifnot = from->ifnot;
	to->rcvifnot = from->rcvifnot;
	to->match_tag_not = from->match_tag_not;
	to->keep_state = from->keep_state;
	to->af = from->af;
	to->naf = from->naf;
	to->proto = from->proto;
	to->type = from->type;
	to->code = from->code;
	to->flags = from->flags;
	to->flagset = from->flagset;
	to->min_ttl = from->min_ttl;
	to->allow_opts = from->allow_opts;
	to->rt = from->rt;
	to->return_ttl = from->return_ttl;
	to->tos = from->tos;
	to->set_tos = from->set_tos;
	to->anchor_relative = from->anchor_relative; /* XXX */
	to->anchor_wildcard = from->anchor_wildcard; /* XXX */
	to->flush = from->flush;
	to->divert.addr = from->divert.addr;
	to->divert.port = from->divert.port;
	to->divert.type = from->divert.type;
	to->prio = from->prio;
	to->set_prio[0] = from->set_prio[0];
	to->set_prio[1] = from->set_prio[1];

	return (0);
}

int
pf_rule_checkaf(struct pf_rule *r)
{
	switch (r->af) {
	case 0:
		if (r->rule_flag & PFRULE_AFTO)
			return (EPFNOSUPPORT);
		break;
	case AF_INET:
		if ((r->rule_flag & PFRULE_AFTO) && r->naf != AF_INET6)
			return (EPFNOSUPPORT);
		break;
#ifdef INET6
	case AF_INET6:
		if ((r->rule_flag & PFRULE_AFTO) && r->naf != AF_INET)
			return (EPFNOSUPPORT);
		break;
#endif /* INET6 */
	default:
		return (EPFNOSUPPORT);
	}

	if ((r->rule_flag & PFRULE_AFTO) == 0 && r->naf != 0)
		return (EPFNOSUPPORT);

	return (0);
}

int
pf_sysctl(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	struct pf_status	pfs;

	NET_LOCK_SHARED();
	PF_LOCK();
	PF_FRAG_LOCK();
	memcpy(&pfs, &pf_status, sizeof(struct pf_status));
	PF_FRAG_UNLOCK();
	pfi_update_status(pfs.ifname, &pfs);
	PF_UNLOCK();
	NET_UNLOCK_SHARED();

	return sysctl_rdstruct(oldp, oldlenp, newp, &pfs, sizeof(pfs));
}

struct pf_trans *
pf_open_trans(uint32_t unit)
{
	static uint64_t ticket = 1;
	struct pf_trans *t;

	rw_assert_wrlock(&pfioctl_rw);

	KASSERT(pf_unit2idx(unit) < nitems(pf_tcount));
	if (pf_tcount[pf_unit2idx(unit)] >= (PF_ANCHOR_STACK_MAX * 8))
		return (NULL);

	t = malloc(sizeof(*t), M_PF, M_WAITOK|M_ZERO);
	t->pft_unit = unit;
	t->pft_ticket = ticket++;
	pf_tcount[pf_unit2idx(unit)]++;

	LIST_INSERT_HEAD(&pf_ioctl_trans, t, pft_entry);

	return (t);
}

struct pf_trans *
pf_find_trans(uint32_t unit, uint64_t ticket)
{
	struct pf_trans	*t;

	rw_assert_anylock(&pfioctl_rw);

	LIST_FOREACH(t, &pf_ioctl_trans, pft_entry) {
		if (t->pft_ticket == ticket && t->pft_unit == unit)
			break;
	}

	return (t);
}

void
pf_init_tgetrule(struct pf_trans *t, struct pf_anchor *a,
    uint32_t rs_version, struct pf_rule *r)
{
	t->pft_type = PF_TRANS_GETRULE;
	if (a == NULL)
		t->pftgr_anchor = &pf_main_anchor;
	else
		t->pftgr_anchor = a;

	t->pftgr_version = rs_version;
	t->pftgr_rule = r;
}

void
pf_cleanup_tgetrule(struct pf_trans *t)
{
	KASSERT(t->pft_type == PF_TRANS_GETRULE);
	pf_anchor_rele(t->pftgr_anchor);
}

void
pf_free_trans(struct pf_trans *t)
{
	switch (t->pft_type) {
	case PF_TRANS_GETRULE:
		pf_cleanup_tgetrule(t);
		break;
	default:
		log(LOG_ERR, "%s unknown transaction type: %d\n",
		    __func__, t->pft_type);
	}

	KASSERT(pf_unit2idx(t->pft_unit) < nitems(pf_tcount));
	KASSERT(pf_tcount[pf_unit2idx(t->pft_unit)] >= 1);
	pf_tcount[pf_unit2idx(t->pft_unit)]--;

	free(t, M_PF, sizeof(*t));
}

void
pf_rollback_trans(struct pf_trans *t)
{
	if (t != NULL) {
		rw_assert_wrlock(&pfioctl_rw);
		LIST_REMOVE(t, pft_entry);
		pf_free_trans(t);
	}
}
