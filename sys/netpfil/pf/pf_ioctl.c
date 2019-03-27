/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Daniel Hartmeier
 * Copyright (c) 2002,2003 Henning Brauer
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
 *	$OpenBSD: pf_ioctl.c,v 1.213 2009/02/15 21:46:12 mbalmer Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_bpf.h"
#include "opt_pf.h"

#include <sys/param.h>
#include <sys/_bitset.h>
#include <sys/bitset.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/hash.h>
#include <sys/interrupt.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/md5.h>
#include <sys/ucred.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>
#include <net/route.h>
#include <net/pfil.h>
#include <net/pfvar.h>
#include <net/if_pfsync.h>
#include <net/if_pflog.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6_var.h>
#include <netinet/ip_icmp.h>

#ifdef INET6
#include <netinet/ip6.h>
#endif /* INET6 */

#ifdef ALTQ
#include <net/altq/altq.h>
#endif

static struct pf_pool	*pf_get_pool(char *, u_int32_t, u_int8_t, u_int32_t,
			    u_int8_t, u_int8_t, u_int8_t);

static void		 pf_mv_pool(struct pf_palist *, struct pf_palist *);
static void		 pf_empty_pool(struct pf_palist *);
static int		 pfioctl(struct cdev *, u_long, caddr_t, int,
			    struct thread *);
#ifdef ALTQ
static int		 pf_begin_altq(u_int32_t *);
static int		 pf_rollback_altq(u_int32_t);
static int		 pf_commit_altq(u_int32_t);
static int		 pf_enable_altq(struct pf_altq *);
static int		 pf_disable_altq(struct pf_altq *);
static u_int32_t	 pf_qname2qid(char *);
static void		 pf_qid_unref(u_int32_t);
#endif /* ALTQ */
static int		 pf_begin_rules(u_int32_t *, int, const char *);
static int		 pf_rollback_rules(u_int32_t, int, char *);
static int		 pf_setup_pfsync_matching(struct pf_ruleset *);
static void		 pf_hash_rule(MD5_CTX *, struct pf_rule *);
static void		 pf_hash_rule_addr(MD5_CTX *, struct pf_rule_addr *);
static int		 pf_commit_rules(u_int32_t, int, char *);
static int		 pf_addr_setup(struct pf_ruleset *,
			    struct pf_addr_wrap *, sa_family_t);
static void		 pf_addr_copyout(struct pf_addr_wrap *);
#ifdef ALTQ
static int		 pf_export_kaltq(struct pf_altq *,
			    struct pfioc_altq_v1 *, size_t);
static int		 pf_import_kaltq(struct pfioc_altq_v1 *,
			    struct pf_altq *, size_t);
#endif /* ALTQ */

VNET_DEFINE(struct pf_rule,	pf_default_rule);

#ifdef ALTQ
VNET_DEFINE_STATIC(int,		pf_altq_running);
#define	V_pf_altq_running	VNET(pf_altq_running)
#endif

#define	TAGID_MAX	 50000
struct pf_tagname {
	TAILQ_ENTRY(pf_tagname)	namehash_entries;
	TAILQ_ENTRY(pf_tagname)	taghash_entries;
	char			name[PF_TAG_NAME_SIZE];
	uint16_t		tag;
	int			ref;
};

struct pf_tagset {
	TAILQ_HEAD(, pf_tagname)	*namehash;
	TAILQ_HEAD(, pf_tagname)	*taghash;
	unsigned int			 mask;
	uint32_t			 seed;
	BITSET_DEFINE(, TAGID_MAX)	 avail;
};

VNET_DEFINE(struct pf_tagset, pf_tags);
#define	V_pf_tags	VNET(pf_tags)
static unsigned int	pf_rule_tag_hashsize;
#define	PF_RULE_TAG_HASH_SIZE_DEFAULT	128
SYSCTL_UINT(_net_pf, OID_AUTO, rule_tag_hashsize, CTLFLAG_RDTUN,
    &pf_rule_tag_hashsize, PF_RULE_TAG_HASH_SIZE_DEFAULT,
    "Size of pf(4) rule tag hashtable");

#ifdef ALTQ
VNET_DEFINE(struct pf_tagset, pf_qids);
#define	V_pf_qids	VNET(pf_qids)
static unsigned int	pf_queue_tag_hashsize;
#define	PF_QUEUE_TAG_HASH_SIZE_DEFAULT	128
SYSCTL_UINT(_net_pf, OID_AUTO, queue_tag_hashsize, CTLFLAG_RDTUN,
    &pf_queue_tag_hashsize, PF_QUEUE_TAG_HASH_SIZE_DEFAULT,
    "Size of pf(4) queue tag hashtable");
#endif
VNET_DEFINE(uma_zone_t,	 pf_tag_z);
#define	V_pf_tag_z		 VNET(pf_tag_z)
static MALLOC_DEFINE(M_PFALTQ, "pf_altq", "pf(4) altq configuration db");
static MALLOC_DEFINE(M_PFRULE, "pf_rule", "pf(4) rules");

#if (PF_QNAME_SIZE != PF_TAG_NAME_SIZE)
#error PF_QNAME_SIZE must be equal to PF_TAG_NAME_SIZE
#endif

static void		 pf_init_tagset(struct pf_tagset *, unsigned int *,
			    unsigned int);
static void		 pf_cleanup_tagset(struct pf_tagset *);
static uint16_t		 tagname2hashindex(const struct pf_tagset *, const char *);
static uint16_t		 tag2hashindex(const struct pf_tagset *, uint16_t);
static u_int16_t	 tagname2tag(struct pf_tagset *, char *);
static u_int16_t	 pf_tagname2tag(char *);
static void		 tag_unref(struct pf_tagset *, u_int16_t);

#define DPFPRINTF(n, x) if (V_pf_status.debug >= (n)) printf x

struct cdev *pf_dev;

/*
 * XXX - These are new and need to be checked when moveing to a new version
 */
static void		 pf_clear_states(void);
static int		 pf_clear_tables(void);
static void		 pf_clear_srcnodes(struct pf_src_node *);
static void		 pf_kill_srcnodes(struct pfioc_src_node_kill *);
static void		 pf_tbladdr_copyout(struct pf_addr_wrap *);

/*
 * Wrapper functions for pfil(9) hooks
 */
#ifdef INET
static pfil_return_t pf_check_in(struct mbuf **m, struct ifnet *ifp,
    int flags, void *ruleset __unused, struct inpcb *inp);
static pfil_return_t pf_check_out(struct mbuf **m, struct ifnet *ifp,
    int flags, void *ruleset __unused, struct inpcb *inp);
#endif
#ifdef INET6
static pfil_return_t pf_check6_in(struct mbuf **m, struct ifnet *ifp,
    int flags, void *ruleset __unused, struct inpcb *inp);
static pfil_return_t pf_check6_out(struct mbuf **m, struct ifnet *ifp,
    int flags, void *ruleset __unused, struct inpcb *inp);
#endif

static int		hook_pf(void);
static int		dehook_pf(void);
static int		shutdown_pf(void);
static int		pf_load(void);
static void		pf_unload(void);

static struct cdevsw pf_cdevsw = {
	.d_ioctl =	pfioctl,
	.d_name =	PF_NAME,
	.d_version =	D_VERSION,
};

volatile VNET_DEFINE_STATIC(int, pf_pfil_hooked);
#define V_pf_pfil_hooked	VNET(pf_pfil_hooked)

/*
 * We need a flag that is neither hooked nor running to know when
 * the VNET is "valid".  We primarily need this to control (global)
 * external event, e.g., eventhandlers.
 */
VNET_DEFINE(int, pf_vnet_active);
#define V_pf_vnet_active	VNET(pf_vnet_active)

int pf_end_threads;
struct proc *pf_purge_proc;

struct rmlock			pf_rules_lock;
struct sx			pf_ioctl_lock;
struct sx			pf_end_lock;

/* pfsync */
VNET_DEFINE(pfsync_state_import_t *, pfsync_state_import_ptr);
VNET_DEFINE(pfsync_insert_state_t *, pfsync_insert_state_ptr);
VNET_DEFINE(pfsync_update_state_t *, pfsync_update_state_ptr);
VNET_DEFINE(pfsync_delete_state_t *, pfsync_delete_state_ptr);
VNET_DEFINE(pfsync_clear_states_t *, pfsync_clear_states_ptr);
VNET_DEFINE(pfsync_defer_t *, pfsync_defer_ptr);
pfsync_detach_ifnet_t *pfsync_detach_ifnet_ptr;

/* pflog */
pflog_packet_t			*pflog_packet_ptr = NULL;

extern u_long	pf_ioctl_maxcount;

static void
pfattach_vnet(void)
{
	u_int32_t *my_timeout = V_pf_default_rule.timeout;

	pf_initialize();
	pfr_initialize();
	pfi_initialize_vnet();
	pf_normalize_init();

	V_pf_limits[PF_LIMIT_STATES].limit = PFSTATE_HIWAT;
	V_pf_limits[PF_LIMIT_SRC_NODES].limit = PFSNODE_HIWAT;

	RB_INIT(&V_pf_anchors);
	pf_init_ruleset(&pf_main_ruleset);

	/* default rule should never be garbage collected */
	V_pf_default_rule.entries.tqe_prev = &V_pf_default_rule.entries.tqe_next;
#ifdef PF_DEFAULT_TO_DROP
	V_pf_default_rule.action = PF_DROP;
#else
	V_pf_default_rule.action = PF_PASS;
#endif
	V_pf_default_rule.nr = -1;
	V_pf_default_rule.rtableid = -1;

	V_pf_default_rule.states_cur = counter_u64_alloc(M_WAITOK);
	V_pf_default_rule.states_tot = counter_u64_alloc(M_WAITOK);
	V_pf_default_rule.src_nodes = counter_u64_alloc(M_WAITOK);

	/* initialize default timeouts */
	my_timeout[PFTM_TCP_FIRST_PACKET] = PFTM_TCP_FIRST_PACKET_VAL;
	my_timeout[PFTM_TCP_OPENING] = PFTM_TCP_OPENING_VAL;
	my_timeout[PFTM_TCP_ESTABLISHED] = PFTM_TCP_ESTABLISHED_VAL;
	my_timeout[PFTM_TCP_CLOSING] = PFTM_TCP_CLOSING_VAL;
	my_timeout[PFTM_TCP_FIN_WAIT] = PFTM_TCP_FIN_WAIT_VAL;
	my_timeout[PFTM_TCP_CLOSED] = PFTM_TCP_CLOSED_VAL;
	my_timeout[PFTM_UDP_FIRST_PACKET] = PFTM_UDP_FIRST_PACKET_VAL;
	my_timeout[PFTM_UDP_SINGLE] = PFTM_UDP_SINGLE_VAL;
	my_timeout[PFTM_UDP_MULTIPLE] = PFTM_UDP_MULTIPLE_VAL;
	my_timeout[PFTM_ICMP_FIRST_PACKET] = PFTM_ICMP_FIRST_PACKET_VAL;
	my_timeout[PFTM_ICMP_ERROR_REPLY] = PFTM_ICMP_ERROR_REPLY_VAL;
	my_timeout[PFTM_OTHER_FIRST_PACKET] = PFTM_OTHER_FIRST_PACKET_VAL;
	my_timeout[PFTM_OTHER_SINGLE] = PFTM_OTHER_SINGLE_VAL;
	my_timeout[PFTM_OTHER_MULTIPLE] = PFTM_OTHER_MULTIPLE_VAL;
	my_timeout[PFTM_FRAG] = PFTM_FRAG_VAL;
	my_timeout[PFTM_INTERVAL] = PFTM_INTERVAL_VAL;
	my_timeout[PFTM_SRC_NODE] = PFTM_SRC_NODE_VAL;
	my_timeout[PFTM_TS_DIFF] = PFTM_TS_DIFF_VAL;
	my_timeout[PFTM_ADAPTIVE_START] = PFSTATE_ADAPT_START;
	my_timeout[PFTM_ADAPTIVE_END] = PFSTATE_ADAPT_END;

	bzero(&V_pf_status, sizeof(V_pf_status));
	V_pf_status.debug = PF_DEBUG_URGENT;

	V_pf_pfil_hooked = 0;

	/* XXX do our best to avoid a conflict */
	V_pf_status.hostid = arc4random();

	for (int i = 0; i < PFRES_MAX; i++)
		V_pf_status.counters[i] = counter_u64_alloc(M_WAITOK);
	for (int i = 0; i < LCNT_MAX; i++)
		V_pf_status.lcounters[i] = counter_u64_alloc(M_WAITOK);
	for (int i = 0; i < FCNT_MAX; i++)
		V_pf_status.fcounters[i] = counter_u64_alloc(M_WAITOK);
	for (int i = 0; i < SCNT_MAX; i++)
		V_pf_status.scounters[i] = counter_u64_alloc(M_WAITOK);

	if (swi_add(NULL, "pf send", pf_intr, curvnet, SWI_NET,
	    INTR_MPSAFE, &V_pf_swi_cookie) != 0)
		/* XXXGL: leaked all above. */
		return;
}


static struct pf_pool *
pf_get_pool(char *anchor, u_int32_t ticket, u_int8_t rule_action,
    u_int32_t rule_number, u_int8_t r_last, u_int8_t active,
    u_int8_t check_ticket)
{
	struct pf_ruleset	*ruleset;
	struct pf_rule		*rule;
	int			 rs_num;

	ruleset = pf_find_ruleset(anchor);
	if (ruleset == NULL)
		return (NULL);
	rs_num = pf_get_ruleset_number(rule_action);
	if (rs_num >= PF_RULESET_MAX)
		return (NULL);
	if (active) {
		if (check_ticket && ticket !=
		    ruleset->rules[rs_num].active.ticket)
			return (NULL);
		if (r_last)
			rule = TAILQ_LAST(ruleset->rules[rs_num].active.ptr,
			    pf_rulequeue);
		else
			rule = TAILQ_FIRST(ruleset->rules[rs_num].active.ptr);
	} else {
		if (check_ticket && ticket !=
		    ruleset->rules[rs_num].inactive.ticket)
			return (NULL);
		if (r_last)
			rule = TAILQ_LAST(ruleset->rules[rs_num].inactive.ptr,
			    pf_rulequeue);
		else
			rule = TAILQ_FIRST(ruleset->rules[rs_num].inactive.ptr);
	}
	if (!r_last) {
		while ((rule != NULL) && (rule->nr != rule_number))
			rule = TAILQ_NEXT(rule, entries);
	}
	if (rule == NULL)
		return (NULL);

	return (&rule->rpool);
}

static void
pf_mv_pool(struct pf_palist *poola, struct pf_palist *poolb)
{
	struct pf_pooladdr	*mv_pool_pa;

	while ((mv_pool_pa = TAILQ_FIRST(poola)) != NULL) {
		TAILQ_REMOVE(poola, mv_pool_pa, entries);
		TAILQ_INSERT_TAIL(poolb, mv_pool_pa, entries);
	}
}

static void
pf_empty_pool(struct pf_palist *poola)
{
	struct pf_pooladdr *pa;

	while ((pa = TAILQ_FIRST(poola)) != NULL) {
		switch (pa->addr.type) {
		case PF_ADDR_DYNIFTL:
			pfi_dynaddr_remove(pa->addr.p.dyn);
			break;
		case PF_ADDR_TABLE:
			/* XXX: this could be unfinished pooladdr on pabuf */
			if (pa->addr.p.tbl != NULL)
				pfr_detach_table(pa->addr.p.tbl);
			break;
		}
		if (pa->kif)
			pfi_kif_unref(pa->kif);
		TAILQ_REMOVE(poola, pa, entries);
		free(pa, M_PFRULE);
	}
}

static void
pf_unlink_rule(struct pf_rulequeue *rulequeue, struct pf_rule *rule)
{

	PF_RULES_WASSERT();

	TAILQ_REMOVE(rulequeue, rule, entries);

	PF_UNLNKDRULES_LOCK();
	rule->rule_flag |= PFRULE_REFS;
	TAILQ_INSERT_TAIL(&V_pf_unlinked_rules, rule, entries);
	PF_UNLNKDRULES_UNLOCK();
}

void
pf_free_rule(struct pf_rule *rule)
{

	PF_RULES_WASSERT();

	if (rule->tag)
		tag_unref(&V_pf_tags, rule->tag);
	if (rule->match_tag)
		tag_unref(&V_pf_tags, rule->match_tag);
#ifdef ALTQ
	if (rule->pqid != rule->qid)
		pf_qid_unref(rule->pqid);
	pf_qid_unref(rule->qid);
#endif
	switch (rule->src.addr.type) {
	case PF_ADDR_DYNIFTL:
		pfi_dynaddr_remove(rule->src.addr.p.dyn);
		break;
	case PF_ADDR_TABLE:
		pfr_detach_table(rule->src.addr.p.tbl);
		break;
	}
	switch (rule->dst.addr.type) {
	case PF_ADDR_DYNIFTL:
		pfi_dynaddr_remove(rule->dst.addr.p.dyn);
		break;
	case PF_ADDR_TABLE:
		pfr_detach_table(rule->dst.addr.p.tbl);
		break;
	}
	if (rule->overload_tbl)
		pfr_detach_table(rule->overload_tbl);
	if (rule->kif)
		pfi_kif_unref(rule->kif);
	pf_anchor_remove(rule);
	pf_empty_pool(&rule->rpool.list);
	counter_u64_free(rule->states_cur);
	counter_u64_free(rule->states_tot);
	counter_u64_free(rule->src_nodes);
	free(rule, M_PFRULE);
}

static void
pf_init_tagset(struct pf_tagset *ts, unsigned int *tunable_size,
    unsigned int default_size)
{
	unsigned int i;
	unsigned int hashsize;
	
	if (*tunable_size == 0 || !powerof2(*tunable_size))
		*tunable_size = default_size;

	hashsize = *tunable_size;
	ts->namehash = mallocarray(hashsize, sizeof(*ts->namehash), M_PFHASH,
	    M_WAITOK);
	ts->taghash = mallocarray(hashsize, sizeof(*ts->taghash), M_PFHASH,
	    M_WAITOK);
	ts->mask = hashsize - 1;
	ts->seed = arc4random();
	for (i = 0; i < hashsize; i++) {
		TAILQ_INIT(&ts->namehash[i]);
		TAILQ_INIT(&ts->taghash[i]);
	}
	BIT_FILL(TAGID_MAX, &ts->avail);
}

static void
pf_cleanup_tagset(struct pf_tagset *ts)
{
	unsigned int i;
	unsigned int hashsize;
	struct pf_tagname *t, *tmp;

	/*
	 * Only need to clean up one of the hashes as each tag is hashed
	 * into each table.
	 */
	hashsize = ts->mask + 1;
	for (i = 0; i < hashsize; i++)
		TAILQ_FOREACH_SAFE(t, &ts->namehash[i], namehash_entries, tmp)
			uma_zfree(V_pf_tag_z, t);

	free(ts->namehash, M_PFHASH);
	free(ts->taghash, M_PFHASH);
}

static uint16_t
tagname2hashindex(const struct pf_tagset *ts, const char *tagname)
{

	return (murmur3_32_hash(tagname, strlen(tagname), ts->seed) & ts->mask);
}

static uint16_t
tag2hashindex(const struct pf_tagset *ts, uint16_t tag)
{

	return (tag & ts->mask);
}

static u_int16_t
tagname2tag(struct pf_tagset *ts, char *tagname)
{
	struct pf_tagname	*tag;
	u_int32_t		 index;
	u_int16_t		 new_tagid;

	PF_RULES_WASSERT();

	index = tagname2hashindex(ts, tagname);
	TAILQ_FOREACH(tag, &ts->namehash[index], namehash_entries)
		if (strcmp(tagname, tag->name) == 0) {
			tag->ref++;
			return (tag->tag);
		}

	/*
	 * new entry
	 *
	 * to avoid fragmentation, we do a linear search from the beginning
	 * and take the first free slot we find.
	 */
	new_tagid = BIT_FFS(TAGID_MAX, &ts->avail);
	/*
	 * Tags are 1-based, with valid tags in the range [1..TAGID_MAX].
	 * BIT_FFS() returns a 1-based bit number, with 0 indicating no bits
	 * set.  It may also return a bit number greater than TAGID_MAX due
	 * to rounding of the number of bits in the vector up to a multiple
	 * of the vector word size at declaration/allocation time.
	 */
	if ((new_tagid == 0) || (new_tagid > TAGID_MAX))
		return (0);

	/* Mark the tag as in use.  Bits are 0-based for BIT_CLR() */
	BIT_CLR(TAGID_MAX, new_tagid - 1, &ts->avail);
	
	/* allocate and fill new struct pf_tagname */
	tag = uma_zalloc(V_pf_tag_z, M_NOWAIT);
	if (tag == NULL)
		return (0);
	strlcpy(tag->name, tagname, sizeof(tag->name));
	tag->tag = new_tagid;
	tag->ref = 1;

	/* Insert into namehash */
	TAILQ_INSERT_TAIL(&ts->namehash[index], tag, namehash_entries);

	/* Insert into taghash */
	index = tag2hashindex(ts, new_tagid);
	TAILQ_INSERT_TAIL(&ts->taghash[index], tag, taghash_entries);
	
	return (tag->tag);
}

static void
tag_unref(struct pf_tagset *ts, u_int16_t tag)
{
	struct pf_tagname	*t;
	uint16_t		 index;
	
	PF_RULES_WASSERT();

	index = tag2hashindex(ts, tag);
	TAILQ_FOREACH(t, &ts->taghash[index], taghash_entries)
		if (tag == t->tag) {
			if (--t->ref == 0) {
				TAILQ_REMOVE(&ts->taghash[index], t,
				    taghash_entries);
				index = tagname2hashindex(ts, t->name);
				TAILQ_REMOVE(&ts->namehash[index], t,
				    namehash_entries);
				/* Bits are 0-based for BIT_SET() */
				BIT_SET(TAGID_MAX, tag - 1, &ts->avail);
				uma_zfree(V_pf_tag_z, t);
			}
			break;
		}
}

static u_int16_t
pf_tagname2tag(char *tagname)
{
	return (tagname2tag(&V_pf_tags, tagname));
}

#ifdef ALTQ
static u_int32_t
pf_qname2qid(char *qname)
{
	return ((u_int32_t)tagname2tag(&V_pf_qids, qname));
}

static void
pf_qid_unref(u_int32_t qid)
{
	tag_unref(&V_pf_qids, (u_int16_t)qid);
}

static int
pf_begin_altq(u_int32_t *ticket)
{
	struct pf_altq	*altq, *tmp;
	int		 error = 0;

	PF_RULES_WASSERT();

	/* Purge the old altq lists */
	TAILQ_FOREACH_SAFE(altq, V_pf_altq_ifs_inactive, entries, tmp) {
		if ((altq->local_flags & PFALTQ_FLAG_IF_REMOVED) == 0) {
			/* detach and destroy the discipline */
			error = altq_remove(altq);
		}
		free(altq, M_PFALTQ);
	}
	TAILQ_INIT(V_pf_altq_ifs_inactive);
	TAILQ_FOREACH_SAFE(altq, V_pf_altqs_inactive, entries, tmp) {
		pf_qid_unref(altq->qid);
		free(altq, M_PFALTQ);
	}
	TAILQ_INIT(V_pf_altqs_inactive);
	if (error)
		return (error);
	*ticket = ++V_ticket_altqs_inactive;
	V_altqs_inactive_open = 1;
	return (0);
}

static int
pf_rollback_altq(u_int32_t ticket)
{
	struct pf_altq	*altq, *tmp;
	int		 error = 0;

	PF_RULES_WASSERT();

	if (!V_altqs_inactive_open || ticket != V_ticket_altqs_inactive)
		return (0);
	/* Purge the old altq lists */
	TAILQ_FOREACH_SAFE(altq, V_pf_altq_ifs_inactive, entries, tmp) {
		if ((altq->local_flags & PFALTQ_FLAG_IF_REMOVED) == 0) {
			/* detach and destroy the discipline */
			error = altq_remove(altq);
		}
		free(altq, M_PFALTQ);
	}
	TAILQ_INIT(V_pf_altq_ifs_inactive);
	TAILQ_FOREACH_SAFE(altq, V_pf_altqs_inactive, entries, tmp) {
		pf_qid_unref(altq->qid);
		free(altq, M_PFALTQ);
	}
	TAILQ_INIT(V_pf_altqs_inactive);
	V_altqs_inactive_open = 0;
	return (error);
}

static int
pf_commit_altq(u_int32_t ticket)
{
	struct pf_altqqueue	*old_altqs, *old_altq_ifs;
	struct pf_altq		*altq, *tmp;
	int			 err, error = 0;

	PF_RULES_WASSERT();

	if (!V_altqs_inactive_open || ticket != V_ticket_altqs_inactive)
		return (EBUSY);

	/* swap altqs, keep the old. */
	old_altqs = V_pf_altqs_active;
	old_altq_ifs = V_pf_altq_ifs_active;
	V_pf_altqs_active = V_pf_altqs_inactive;
	V_pf_altq_ifs_active = V_pf_altq_ifs_inactive;
	V_pf_altqs_inactive = old_altqs;
	V_pf_altq_ifs_inactive = old_altq_ifs;
	V_ticket_altqs_active = V_ticket_altqs_inactive;

	/* Attach new disciplines */
	TAILQ_FOREACH(altq, V_pf_altq_ifs_active, entries) {
		if ((altq->local_flags & PFALTQ_FLAG_IF_REMOVED) == 0) {
			/* attach the discipline */
			error = altq_pfattach(altq);
			if (error == 0 && V_pf_altq_running)
				error = pf_enable_altq(altq);
			if (error != 0)
				return (error);
		}
	}

	/* Purge the old altq lists */
	TAILQ_FOREACH_SAFE(altq, V_pf_altq_ifs_inactive, entries, tmp) {
		if ((altq->local_flags & PFALTQ_FLAG_IF_REMOVED) == 0) {
			/* detach and destroy the discipline */
			if (V_pf_altq_running)
				error = pf_disable_altq(altq);
			err = altq_pfdetach(altq);
			if (err != 0 && error == 0)
				error = err;
			err = altq_remove(altq);
			if (err != 0 && error == 0)
				error = err;
		}
		free(altq, M_PFALTQ);
	}
	TAILQ_INIT(V_pf_altq_ifs_inactive);
	TAILQ_FOREACH_SAFE(altq, V_pf_altqs_inactive, entries, tmp) {
		pf_qid_unref(altq->qid);
		free(altq, M_PFALTQ);
	}
	TAILQ_INIT(V_pf_altqs_inactive);

	V_altqs_inactive_open = 0;
	return (error);
}

static int
pf_enable_altq(struct pf_altq *altq)
{
	struct ifnet		*ifp;
	struct tb_profile	 tb;
	int			 error = 0;

	if ((ifp = ifunit(altq->ifname)) == NULL)
		return (EINVAL);

	if (ifp->if_snd.altq_type != ALTQT_NONE)
		error = altq_enable(&ifp->if_snd);

	/* set tokenbucket regulator */
	if (error == 0 && ifp != NULL && ALTQ_IS_ENABLED(&ifp->if_snd)) {
		tb.rate = altq->ifbandwidth;
		tb.depth = altq->tbrsize;
		error = tbr_set(&ifp->if_snd, &tb);
	}

	return (error);
}

static int
pf_disable_altq(struct pf_altq *altq)
{
	struct ifnet		*ifp;
	struct tb_profile	 tb;
	int			 error;

	if ((ifp = ifunit(altq->ifname)) == NULL)
		return (EINVAL);

	/*
	 * when the discipline is no longer referenced, it was overridden
	 * by a new one.  if so, just return.
	 */
	if (altq->altq_disc != ifp->if_snd.altq_disc)
		return (0);

	error = altq_disable(&ifp->if_snd);

	if (error == 0) {
		/* clear tokenbucket regulator */
		tb.rate = 0;
		error = tbr_set(&ifp->if_snd, &tb);
	}

	return (error);
}

static int
pf_altq_ifnet_event_add(struct ifnet *ifp, int remove, u_int32_t ticket,
    struct pf_altq *altq)
{
	struct ifnet	*ifp1;
	int		 error = 0;
	
	/* Deactivate the interface in question */
	altq->local_flags &= ~PFALTQ_FLAG_IF_REMOVED;
	if ((ifp1 = ifunit(altq->ifname)) == NULL ||
	    (remove && ifp1 == ifp)) {
		altq->local_flags |= PFALTQ_FLAG_IF_REMOVED;
	} else {
		error = altq_add(ifp1, altq);

		if (ticket != V_ticket_altqs_inactive)
			error = EBUSY;

		if (error)
			free(altq, M_PFALTQ);
	}

	return (error);
}

void
pf_altq_ifnet_event(struct ifnet *ifp, int remove)
{
	struct pf_altq	*a1, *a2, *a3;
	u_int32_t	 ticket;
	int		 error = 0;

	/*
	 * No need to re-evaluate the configuration for events on interfaces
	 * that do not support ALTQ, as it's not possible for such
	 * interfaces to be part of the configuration.
	 */
	if (!ALTQ_IS_READY(&ifp->if_snd))
		return;

	/* Interrupt userland queue modifications */
	if (V_altqs_inactive_open)
		pf_rollback_altq(V_ticket_altqs_inactive);

	/* Start new altq ruleset */
	if (pf_begin_altq(&ticket))
		return;

	/* Copy the current active set */
	TAILQ_FOREACH(a1, V_pf_altq_ifs_active, entries) {
		a2 = malloc(sizeof(*a2), M_PFALTQ, M_NOWAIT);
		if (a2 == NULL) {
			error = ENOMEM;
			break;
		}
		bcopy(a1, a2, sizeof(struct pf_altq));

		error = pf_altq_ifnet_event_add(ifp, remove, ticket, a2);
		if (error)
			break;

		TAILQ_INSERT_TAIL(V_pf_altq_ifs_inactive, a2, entries);
	}
	if (error)
		goto out;
	TAILQ_FOREACH(a1, V_pf_altqs_active, entries) {
		a2 = malloc(sizeof(*a2), M_PFALTQ, M_NOWAIT);
		if (a2 == NULL) {
			error = ENOMEM;
			break;
		}
		bcopy(a1, a2, sizeof(struct pf_altq));

		if ((a2->qid = pf_qname2qid(a2->qname)) == 0) {
			error = EBUSY;
			free(a2, M_PFALTQ);
			break;
		}
		a2->altq_disc = NULL;
		TAILQ_FOREACH(a3, V_pf_altq_ifs_inactive, entries) {
			if (strncmp(a3->ifname, a2->ifname,
				IFNAMSIZ) == 0) {
				a2->altq_disc = a3->altq_disc;
				break;
			}
		}
		error = pf_altq_ifnet_event_add(ifp, remove, ticket, a2);
		if (error)
			break;

		TAILQ_INSERT_TAIL(V_pf_altqs_inactive, a2, entries);
	}

out:
	if (error != 0)
		pf_rollback_altq(ticket);
	else
		pf_commit_altq(ticket);
}
#endif /* ALTQ */

static int
pf_begin_rules(u_int32_t *ticket, int rs_num, const char *anchor)
{
	struct pf_ruleset	*rs;
	struct pf_rule		*rule;

	PF_RULES_WASSERT();

	if (rs_num < 0 || rs_num >= PF_RULESET_MAX)
		return (EINVAL);
	rs = pf_find_or_create_ruleset(anchor);
	if (rs == NULL)
		return (EINVAL);
	while ((rule = TAILQ_FIRST(rs->rules[rs_num].inactive.ptr)) != NULL) {
		pf_unlink_rule(rs->rules[rs_num].inactive.ptr, rule);
		rs->rules[rs_num].inactive.rcount--;
	}
	*ticket = ++rs->rules[rs_num].inactive.ticket;
	rs->rules[rs_num].inactive.open = 1;
	return (0);
}

static int
pf_rollback_rules(u_int32_t ticket, int rs_num, char *anchor)
{
	struct pf_ruleset	*rs;
	struct pf_rule		*rule;

	PF_RULES_WASSERT();

	if (rs_num < 0 || rs_num >= PF_RULESET_MAX)
		return (EINVAL);
	rs = pf_find_ruleset(anchor);
	if (rs == NULL || !rs->rules[rs_num].inactive.open ||
	    rs->rules[rs_num].inactive.ticket != ticket)
		return (0);
	while ((rule = TAILQ_FIRST(rs->rules[rs_num].inactive.ptr)) != NULL) {
		pf_unlink_rule(rs->rules[rs_num].inactive.ptr, rule);
		rs->rules[rs_num].inactive.rcount--;
	}
	rs->rules[rs_num].inactive.open = 0;
	return (0);
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

static void
pf_hash_rule_addr(MD5_CTX *ctx, struct pf_rule_addr *pfr)
{
	PF_MD5_UPD(pfr, addr.type);
	switch (pfr->addr.type) {
		case PF_ADDR_DYNIFTL:
			PF_MD5_UPD(pfr, addr.v.ifname);
			PF_MD5_UPD(pfr, addr.iflags);
			break;
		case PF_ADDR_TABLE:
			PF_MD5_UPD(pfr, addr.v.tblname);
			break;
		case PF_ADDR_ADDRMASK:
			/* XXX ignore af? */
			PF_MD5_UPD(pfr, addr.v.a.addr.addr32);
			PF_MD5_UPD(pfr, addr.v.a.mask.addr32);
			break;
	}

	PF_MD5_UPD(pfr, port[0]);
	PF_MD5_UPD(pfr, port[1]);
	PF_MD5_UPD(pfr, neg);
	PF_MD5_UPD(pfr, port_op);
}

static void
pf_hash_rule(MD5_CTX *ctx, struct pf_rule *rule)
{
	u_int16_t x;
	u_int32_t y;

	pf_hash_rule_addr(ctx, &rule->src);
	pf_hash_rule_addr(ctx, &rule->dst);
	PF_MD5_UPD_STR(rule, label);
	PF_MD5_UPD_STR(rule, ifname);
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
	PF_MD5_UPD(rule, match_tag_not);
	PF_MD5_UPD(rule, natpass);
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

static int
pf_commit_rules(u_int32_t ticket, int rs_num, char *anchor)
{
	struct pf_ruleset	*rs;
	struct pf_rule		*rule, **old_array;
	struct pf_rulequeue	*old_rules;
	int			 error;
	u_int32_t		 old_rcount;

	PF_RULES_WASSERT();

	if (rs_num < 0 || rs_num >= PF_RULESET_MAX)
		return (EINVAL);
	rs = pf_find_ruleset(anchor);
	if (rs == NULL || !rs->rules[rs_num].inactive.open ||
	    ticket != rs->rules[rs_num].inactive.ticket)
		return (EBUSY);

	/* Calculate checksum for the main ruleset */
	if (rs == &pf_main_ruleset) {
		error = pf_setup_pfsync_matching(rs);
		if (error != 0)
			return (error);
	}

	/* Swap rules, keep the old. */
	old_rules = rs->rules[rs_num].active.ptr;
	old_rcount = rs->rules[rs_num].active.rcount;
	old_array = rs->rules[rs_num].active.ptr_array;

	rs->rules[rs_num].active.ptr =
	    rs->rules[rs_num].inactive.ptr;
	rs->rules[rs_num].active.ptr_array =
	    rs->rules[rs_num].inactive.ptr_array;
	rs->rules[rs_num].active.rcount =
	    rs->rules[rs_num].inactive.rcount;
	rs->rules[rs_num].inactive.ptr = old_rules;
	rs->rules[rs_num].inactive.ptr_array = old_array;
	rs->rules[rs_num].inactive.rcount = old_rcount;

	rs->rules[rs_num].active.ticket =
	    rs->rules[rs_num].inactive.ticket;
	pf_calc_skip_steps(rs->rules[rs_num].active.ptr);


	/* Purge the old rule list. */
	while ((rule = TAILQ_FIRST(old_rules)) != NULL)
		pf_unlink_rule(old_rules, rule);
	if (rs->rules[rs_num].inactive.ptr_array)
		free(rs->rules[rs_num].inactive.ptr_array, M_TEMP);
	rs->rules[rs_num].inactive.ptr_array = NULL;
	rs->rules[rs_num].inactive.rcount = 0;
	rs->rules[rs_num].inactive.open = 0;
	pf_remove_if_empty_ruleset(rs);

	return (0);
}

static int
pf_setup_pfsync_matching(struct pf_ruleset *rs)
{
	MD5_CTX			 ctx;
	struct pf_rule		*rule;
	int			 rs_cnt;
	u_int8_t		 digest[PF_MD5_DIGEST_LENGTH];

	MD5Init(&ctx);
	for (rs_cnt = 0; rs_cnt < PF_RULESET_MAX; rs_cnt++) {
		/* XXX PF_RULESET_SCRUB as well? */
		if (rs_cnt == PF_RULESET_SCRUB)
			continue;

		if (rs->rules[rs_cnt].inactive.ptr_array)
			free(rs->rules[rs_cnt].inactive.ptr_array, M_TEMP);
		rs->rules[rs_cnt].inactive.ptr_array = NULL;

		if (rs->rules[rs_cnt].inactive.rcount) {
			rs->rules[rs_cnt].inactive.ptr_array =
			    malloc(sizeof(caddr_t) *
			    rs->rules[rs_cnt].inactive.rcount,
			    M_TEMP, M_NOWAIT);

			if (!rs->rules[rs_cnt].inactive.ptr_array)
				return (ENOMEM);
		}

		TAILQ_FOREACH(rule, rs->rules[rs_cnt].inactive.ptr,
		    entries) {
			pf_hash_rule(&ctx, rule);
			(rs->rules[rs_cnt].inactive.ptr_array)[rule->nr] = rule;
		}
	}

	MD5Final(digest, &ctx);
	memcpy(V_pf_status.pf_chksum, digest, sizeof(V_pf_status.pf_chksum));
	return (0);
}

static int
pf_addr_setup(struct pf_ruleset *ruleset, struct pf_addr_wrap *addr,
    sa_family_t af)
{
	int error = 0;

	switch (addr->type) {
	case PF_ADDR_TABLE:
		addr->p.tbl = pfr_attach_table(ruleset, addr->v.tblname);
		if (addr->p.tbl == NULL)
			error = ENOMEM;
		break;
	case PF_ADDR_DYNIFTL:
		error = pfi_dynaddr_setup(addr, af);
		break;
	}

	return (error);
}

static void
pf_addr_copyout(struct pf_addr_wrap *addr)
{

	switch (addr->type) {
	case PF_ADDR_DYNIFTL:
		pfi_dynaddr_copyout(addr);
		break;
	case PF_ADDR_TABLE:
		pf_tbladdr_copyout(addr);
		break;
	}
}

#ifdef ALTQ
/*
 * Handle export of struct pf_kaltq to user binaries that may be using any
 * version of struct pf_altq.
 */
static int
pf_export_kaltq(struct pf_altq *q, struct pfioc_altq_v1 *pa, size_t ioc_size)
{
	u_int32_t version;
	
	if (ioc_size == sizeof(struct pfioc_altq_v0))
		version = 0;
	else
		version = pa->version;

	if (version > PFIOC_ALTQ_VERSION)
		return (EINVAL);

#define ASSIGN(x) exported_q->x = q->x
#define COPY(x) \
	bcopy(&q->x, &exported_q->x, min(sizeof(q->x), sizeof(exported_q->x)))
#define SATU16(x) (u_int32_t)uqmin((x), USHRT_MAX)
#define SATU32(x) (u_int32_t)uqmin((x), UINT_MAX)

	switch (version) {
	case 0: {
		struct pf_altq_v0 *exported_q =
		    &((struct pfioc_altq_v0 *)pa)->altq;

		COPY(ifname);

		ASSIGN(scheduler);
		ASSIGN(tbrsize);
		exported_q->tbrsize = SATU16(q->tbrsize);
		exported_q->ifbandwidth = SATU32(q->ifbandwidth);

		COPY(qname);
		COPY(parent);
		ASSIGN(parent_qid);
		exported_q->bandwidth = SATU32(q->bandwidth);
		ASSIGN(priority);
		ASSIGN(local_flags);

		ASSIGN(qlimit);
		ASSIGN(flags);

		if (q->scheduler == ALTQT_HFSC) {
#define ASSIGN_OPT(x) exported_q->pq_u.hfsc_opts.x = q->pq_u.hfsc_opts.x
#define ASSIGN_OPT_SATU32(x) exported_q->pq_u.hfsc_opts.x = \
			    SATU32(q->pq_u.hfsc_opts.x)
			
			ASSIGN_OPT_SATU32(rtsc_m1);
			ASSIGN_OPT(rtsc_d);
			ASSIGN_OPT_SATU32(rtsc_m2);

			ASSIGN_OPT_SATU32(lssc_m1);
			ASSIGN_OPT(lssc_d);
			ASSIGN_OPT_SATU32(lssc_m2);

			ASSIGN_OPT_SATU32(ulsc_m1);
			ASSIGN_OPT(ulsc_d);
			ASSIGN_OPT_SATU32(ulsc_m2);

			ASSIGN_OPT(flags);
			
#undef ASSIGN_OPT
#undef ASSIGN_OPT_SATU32
		} else
			COPY(pq_u);

		ASSIGN(qid);
		break;
	}
	case 1:	{
		struct pf_altq_v1 *exported_q =
		    &((struct pfioc_altq_v1 *)pa)->altq;

		COPY(ifname);

		ASSIGN(scheduler);
		ASSIGN(tbrsize);
		ASSIGN(ifbandwidth);

		COPY(qname);
		COPY(parent);
		ASSIGN(parent_qid);
		ASSIGN(bandwidth);
		ASSIGN(priority);
		ASSIGN(local_flags);

		ASSIGN(qlimit);
		ASSIGN(flags);
		COPY(pq_u);

		ASSIGN(qid);
		break;
	}
	default:
		panic("%s: unhandled struct pfioc_altq version", __func__);
		break;
	}

#undef ASSIGN
#undef COPY
#undef SATU16
#undef SATU32

	return (0);
}

/*
 * Handle import to struct pf_kaltq of struct pf_altq from user binaries
 * that may be using any version of it.
 */
static int
pf_import_kaltq(struct pfioc_altq_v1 *pa, struct pf_altq *q, size_t ioc_size)
{
	u_int32_t version;
	
	if (ioc_size == sizeof(struct pfioc_altq_v0))
		version = 0;
	else
		version = pa->version;

	if (version > PFIOC_ALTQ_VERSION)
		return (EINVAL);
	
#define ASSIGN(x) q->x = imported_q->x
#define COPY(x) \
	bcopy(&imported_q->x, &q->x, min(sizeof(imported_q->x), sizeof(q->x)))

	switch (version) {
	case 0: {
		struct pf_altq_v0 *imported_q =
		    &((struct pfioc_altq_v0 *)pa)->altq;

		COPY(ifname);

		ASSIGN(scheduler);
		ASSIGN(tbrsize); /* 16-bit -> 32-bit */
		ASSIGN(ifbandwidth); /* 32-bit -> 64-bit */

		COPY(qname);
		COPY(parent);
		ASSIGN(parent_qid);
		ASSIGN(bandwidth); /* 32-bit -> 64-bit */
		ASSIGN(priority);
		ASSIGN(local_flags);

		ASSIGN(qlimit);
		ASSIGN(flags);

		if (imported_q->scheduler == ALTQT_HFSC) {
#define ASSIGN_OPT(x) q->pq_u.hfsc_opts.x = imported_q->pq_u.hfsc_opts.x

			/*
			 * The m1 and m2 parameters are being copied from
			 * 32-bit to 64-bit.
			 */
			ASSIGN_OPT(rtsc_m1);
			ASSIGN_OPT(rtsc_d);
			ASSIGN_OPT(rtsc_m2);

			ASSIGN_OPT(lssc_m1);
			ASSIGN_OPT(lssc_d);
			ASSIGN_OPT(lssc_m2);

			ASSIGN_OPT(ulsc_m1);
			ASSIGN_OPT(ulsc_d);
			ASSIGN_OPT(ulsc_m2);

			ASSIGN_OPT(flags);
			
#undef ASSIGN_OPT
		} else
			COPY(pq_u);

		ASSIGN(qid);
		break;
	}
	case 1: {
		struct pf_altq_v1 *imported_q =
		    &((struct pfioc_altq_v1 *)pa)->altq;

		COPY(ifname);

		ASSIGN(scheduler);
		ASSIGN(tbrsize);
		ASSIGN(ifbandwidth);

		COPY(qname);
		COPY(parent);
		ASSIGN(parent_qid);
		ASSIGN(bandwidth);
		ASSIGN(priority);
		ASSIGN(local_flags);

		ASSIGN(qlimit);
		ASSIGN(flags);
		COPY(pq_u);

		ASSIGN(qid);
		break;
	}
	default:	
		panic("%s: unhandled struct pfioc_altq version", __func__);
		break;
	}

#undef ASSIGN
#undef COPY
	
	return (0);
}

static struct pf_altq *
pf_altq_get_nth_active(u_int32_t n)
{
	struct pf_altq		*altq;
	u_int32_t		 nr;

	nr = 0;
	TAILQ_FOREACH(altq, V_pf_altq_ifs_active, entries) {
		if (nr == n)
			return (altq);
		nr++;
	}

	TAILQ_FOREACH(altq, V_pf_altqs_active, entries) {
		if (nr == n)
			return (altq);
		nr++;
	}

	return (NULL);
}
#endif /* ALTQ */

static int
pfioctl(struct cdev *dev, u_long cmd, caddr_t addr, int flags, struct thread *td)
{
	int			 error = 0;
	PF_RULES_RLOCK_TRACKER;

	/* XXX keep in sync with switch() below */
	if (securelevel_gt(td->td_ucred, 2))
		switch (cmd) {
		case DIOCGETRULES:
		case DIOCGETRULE:
		case DIOCGETADDRS:
		case DIOCGETADDR:
		case DIOCGETSTATE:
		case DIOCSETSTATUSIF:
		case DIOCGETSTATUS:
		case DIOCCLRSTATUS:
		case DIOCNATLOOK:
		case DIOCSETDEBUG:
		case DIOCGETSTATES:
		case DIOCGETTIMEOUT:
		case DIOCCLRRULECTRS:
		case DIOCGETLIMIT:
		case DIOCGETALTQSV0:
		case DIOCGETALTQSV1:
		case DIOCGETALTQV0:
		case DIOCGETALTQV1:
		case DIOCGETQSTATSV0:
		case DIOCGETQSTATSV1:
		case DIOCGETRULESETS:
		case DIOCGETRULESET:
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
		case DIOCGIFSPEEDV0:
		case DIOCGIFSPEEDV1:
		case DIOCSETIFFLAG:
		case DIOCCLRIFFLAG:
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
		case DIOCGETADDRS:
		case DIOCGETADDR:
		case DIOCGETSTATE:
		case DIOCGETSTATUS:
		case DIOCGETSTATES:
		case DIOCGETTIMEOUT:
		case DIOCGETLIMIT:
		case DIOCGETALTQSV0:
		case DIOCGETALTQSV1:
		case DIOCGETALTQV0:
		case DIOCGETALTQV1:
		case DIOCGETQSTATSV0:
		case DIOCGETQSTATSV1:
		case DIOCGETRULESETS:
		case DIOCGETRULESET:
		case DIOCNATLOOK:
		case DIOCRGETTABLES:
		case DIOCRGETTSTATS:
		case DIOCRGETADDRS:
		case DIOCRGETASTATS:
		case DIOCRTSTADDRS:
		case DIOCOSFPGET:
		case DIOCGETSRCNODES:
		case DIOCIGETIFACES:
		case DIOCGIFSPEEDV1:
		case DIOCGIFSPEEDV0:
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

	CURVNET_SET(TD_TO_VNET(td));

	switch (cmd) {
	case DIOCSTART:
		sx_xlock(&pf_ioctl_lock);
		if (V_pf_status.running)
			error = EEXIST;
		else {
			int cpu;

			error = hook_pf();
			if (error) {
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: pfil registration failed\n"));
				break;
			}
			V_pf_status.running = 1;
			V_pf_status.since = time_second;

			CPU_FOREACH(cpu)
				V_pf_stateid[cpu] = time_second;

			DPFPRINTF(PF_DEBUG_MISC, ("pf: started\n"));
		}
		break;

	case DIOCSTOP:
		sx_xlock(&pf_ioctl_lock);
		if (!V_pf_status.running)
			error = ENOENT;
		else {
			V_pf_status.running = 0;
			error = dehook_pf();
			if (error) {
				V_pf_status.running = 1;
				DPFPRINTF(PF_DEBUG_MISC,
				    ("pf: pfil unregistration failed\n"));
			}
			V_pf_status.since = time_second;
			DPFPRINTF(PF_DEBUG_MISC, ("pf: stopped\n"));
		}
		break;

	case DIOCADDRULE: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule, *tail;
		struct pf_pooladdr	*pa;
		struct pfi_kif		*kif = NULL;
		int			 rs_num;

		if (pr->rule.return_icmp >> 8 > ICMP_MAXTYPE) {
			error = EINVAL;
			break;
		}
#ifndef INET
		if (pr->rule.af == AF_INET) {
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET */
#ifndef INET6
		if (pr->rule.af == AF_INET6) {
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET6 */

		rule = malloc(sizeof(*rule), M_PFRULE, M_WAITOK);
		bcopy(&pr->rule, rule, sizeof(struct pf_rule));
		if (rule->ifname[0])
			kif = malloc(sizeof(*kif), PFI_MTYPE, M_WAITOK);
		rule->states_cur = counter_u64_alloc(M_WAITOK);
		rule->states_tot = counter_u64_alloc(M_WAITOK);
		rule->src_nodes = counter_u64_alloc(M_WAITOK);
		rule->cuid = td->td_ucred->cr_ruid;
		rule->cpid = td->td_proc ? td->td_proc->p_pid : 0;
		TAILQ_INIT(&rule->rpool.list);

#define	ERROUT(x)	{ error = (x); goto DIOCADDRULE_error; }

		PF_RULES_WLOCK();
		pr->anchor[sizeof(pr->anchor) - 1] = 0;
		ruleset = pf_find_ruleset(pr->anchor);
		if (ruleset == NULL)
			ERROUT(EINVAL);
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX)
			ERROUT(EINVAL);
		if (pr->ticket != ruleset->rules[rs_num].inactive.ticket) {
			DPFPRINTF(PF_DEBUG_MISC,
			    ("ticket: %d != [%d]%d\n", pr->ticket, rs_num,
			    ruleset->rules[rs_num].inactive.ticket));
			ERROUT(EBUSY);
		}
		if (pr->pool_ticket != V_ticket_pabuf) {
			DPFPRINTF(PF_DEBUG_MISC,
			    ("pool_ticket: %d != %d\n", pr->pool_ticket,
			    V_ticket_pabuf));
			ERROUT(EBUSY);
		}

		tail = TAILQ_LAST(ruleset->rules[rs_num].inactive.ptr,
		    pf_rulequeue);
		if (tail)
			rule->nr = tail->nr + 1;
		else
			rule->nr = 0;
		if (rule->ifname[0]) {
			rule->kif = pfi_kif_attach(kif, rule->ifname);
			pfi_kif_ref(rule->kif);
		} else
			rule->kif = NULL;

		if (rule->rtableid > 0 && rule->rtableid >= rt_numfibs)
			error = EBUSY;

#ifdef ALTQ
		/* set queue IDs */
		if (rule->qname[0] != 0) {
			if ((rule->qid = pf_qname2qid(rule->qname)) == 0)
				error = EBUSY;
			else if (rule->pqname[0] != 0) {
				if ((rule->pqid =
				    pf_qname2qid(rule->pqname)) == 0)
					error = EBUSY;
			} else
				rule->pqid = rule->qid;
		}
#endif
		if (rule->tagname[0])
			if ((rule->tag = pf_tagname2tag(rule->tagname)) == 0)
				error = EBUSY;
		if (rule->match_tagname[0])
			if ((rule->match_tag =
			    pf_tagname2tag(rule->match_tagname)) == 0)
				error = EBUSY;
		if (rule->rt && !rule->direction)
			error = EINVAL;
		if (!rule->log)
			rule->logif = 0;
		if (rule->logif >= PFLOGIFS_MAX)
			error = EINVAL;
		if (pf_addr_setup(ruleset, &rule->src.addr, rule->af))
			error = ENOMEM;
		if (pf_addr_setup(ruleset, &rule->dst.addr, rule->af))
			error = ENOMEM;
		if (pf_anchor_setup(rule, ruleset, pr->anchor_call))
			error = EINVAL;
		if (rule->scrub_flags & PFSTATE_SETPRIO &&
		    (rule->set_prio[0] > PF_PRIO_MAX ||
		    rule->set_prio[1] > PF_PRIO_MAX))
			error = EINVAL;
		TAILQ_FOREACH(pa, &V_pf_pabuf, entries)
			if (pa->addr.type == PF_ADDR_TABLE) {
				pa->addr.p.tbl = pfr_attach_table(ruleset,
				    pa->addr.v.tblname);
				if (pa->addr.p.tbl == NULL)
					error = ENOMEM;
			}

		rule->overload_tbl = NULL;
		if (rule->overload_tblname[0]) {
			if ((rule->overload_tbl = pfr_attach_table(ruleset,
			    rule->overload_tblname)) == NULL)
				error = EINVAL;
			else
				rule->overload_tbl->pfrkt_flags |=
				    PFR_TFLAG_ACTIVE;
		}

		pf_mv_pool(&V_pf_pabuf, &rule->rpool.list);
		if (((((rule->action == PF_NAT) || (rule->action == PF_RDR) ||
		    (rule->action == PF_BINAT)) && rule->anchor == NULL) ||
		    (rule->rt > PF_NOPFROUTE)) &&
		    (TAILQ_FIRST(&rule->rpool.list) == NULL))
			error = EINVAL;

		if (error) {
			pf_free_rule(rule);
			PF_RULES_WUNLOCK();
			break;
		}

		rule->rpool.cur = TAILQ_FIRST(&rule->rpool.list);
		rule->evaluations = rule->packets[0] = rule->packets[1] =
		    rule->bytes[0] = rule->bytes[1] = 0;
		TAILQ_INSERT_TAIL(ruleset->rules[rs_num].inactive.ptr,
		    rule, entries);
		ruleset->rules[rs_num].inactive.rcount++;
		PF_RULES_WUNLOCK();
		break;

#undef ERROUT
DIOCADDRULE_error:
		PF_RULES_WUNLOCK();
		counter_u64_free(rule->states_cur);
		counter_u64_free(rule->states_tot);
		counter_u64_free(rule->src_nodes);
		free(rule, M_PFRULE);
		if (kif)
			free(kif, PFI_MTYPE);
		break;
	}

	case DIOCGETRULES: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*tail;
		int			 rs_num;

		PF_RULES_WLOCK();
		pr->anchor[sizeof(pr->anchor) - 1] = 0;
		ruleset = pf_find_ruleset(pr->anchor);
		if (ruleset == NULL) {
			PF_RULES_WUNLOCK();
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			PF_RULES_WUNLOCK();
			error = EINVAL;
			break;
		}
		tail = TAILQ_LAST(ruleset->rules[rs_num].active.ptr,
		    pf_rulequeue);
		if (tail)
			pr->nr = tail->nr + 1;
		else
			pr->nr = 0;
		pr->ticket = ruleset->rules[rs_num].active.ticket;
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCGETRULE: {
		struct pfioc_rule	*pr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*rule;
		int			 rs_num, i;

		PF_RULES_WLOCK();
		pr->anchor[sizeof(pr->anchor) - 1] = 0;
		ruleset = pf_find_ruleset(pr->anchor);
		if (ruleset == NULL) {
			PF_RULES_WUNLOCK();
			error = EINVAL;
			break;
		}
		rs_num = pf_get_ruleset_number(pr->rule.action);
		if (rs_num >= PF_RULESET_MAX) {
			PF_RULES_WUNLOCK();
			error = EINVAL;
			break;
		}
		if (pr->ticket != ruleset->rules[rs_num].active.ticket) {
			PF_RULES_WUNLOCK();
			error = EBUSY;
			break;
		}
		rule = TAILQ_FIRST(ruleset->rules[rs_num].active.ptr);
		while ((rule != NULL) && (rule->nr != pr->nr))
			rule = TAILQ_NEXT(rule, entries);
		if (rule == NULL) {
			PF_RULES_WUNLOCK();
			error = EBUSY;
			break;
		}
		bcopy(rule, &pr->rule, sizeof(struct pf_rule));
		pr->rule.u_states_cur = counter_u64_fetch(rule->states_cur);
		pr->rule.u_states_tot = counter_u64_fetch(rule->states_tot);
		pr->rule.u_src_nodes = counter_u64_fetch(rule->src_nodes);
		if (pf_anchor_copyout(ruleset, rule, pr)) {
			PF_RULES_WUNLOCK();
			error = EBUSY;
			break;
		}
		pf_addr_copyout(&pr->rule.src.addr);
		pf_addr_copyout(&pr->rule.dst.addr);
		for (i = 0; i < PF_SKIP_COUNT; ++i)
			if (rule->skip[i].ptr == NULL)
				pr->rule.skip[i].nr = -1;
			else
				pr->rule.skip[i].nr =
				    rule->skip[i].ptr->nr;

		if (pr->action == PF_GET_CLR_CNTR) {
			rule->evaluations = 0;
			rule->packets[0] = rule->packets[1] = 0;
			rule->bytes[0] = rule->bytes[1] = 0;
			counter_u64_zero(rule->states_tot);
		}
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCCHANGERULE: {
		struct pfioc_rule	*pcr = (struct pfioc_rule *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_rule		*oldrule = NULL, *newrule = NULL;
		struct pfi_kif		*kif = NULL;
		struct pf_pooladdr	*pa;
		u_int32_t		 nr = 0;
		int			 rs_num;

		if (pcr->action < PF_CHANGE_ADD_HEAD ||
		    pcr->action > PF_CHANGE_GET_TICKET) {
			error = EINVAL;
			break;
		}
		if (pcr->rule.return_icmp >> 8 > ICMP_MAXTYPE) {
			error = EINVAL;
			break;
		}

		if (pcr->action != PF_CHANGE_REMOVE) {
#ifndef INET
			if (pcr->rule.af == AF_INET) {
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET */
#ifndef INET6
			if (pcr->rule.af == AF_INET6) {
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET6 */
			newrule = malloc(sizeof(*newrule), M_PFRULE, M_WAITOK);
			bcopy(&pcr->rule, newrule, sizeof(struct pf_rule));
			if (newrule->ifname[0])
				kif = malloc(sizeof(*kif), PFI_MTYPE, M_WAITOK);
			newrule->states_cur = counter_u64_alloc(M_WAITOK);
			newrule->states_tot = counter_u64_alloc(M_WAITOK);
			newrule->src_nodes = counter_u64_alloc(M_WAITOK);
			newrule->cuid = td->td_ucred->cr_ruid;
			newrule->cpid = td->td_proc ? td->td_proc->p_pid : 0;
			TAILQ_INIT(&newrule->rpool.list);
		}

#define	ERROUT(x)	{ error = (x); goto DIOCCHANGERULE_error; }

		PF_RULES_WLOCK();
		if (!(pcr->action == PF_CHANGE_REMOVE ||
		    pcr->action == PF_CHANGE_GET_TICKET) &&
		    pcr->pool_ticket != V_ticket_pabuf)
			ERROUT(EBUSY);

		ruleset = pf_find_ruleset(pcr->anchor);
		if (ruleset == NULL)
			ERROUT(EINVAL);

		rs_num = pf_get_ruleset_number(pcr->rule.action);
		if (rs_num >= PF_RULESET_MAX)
			ERROUT(EINVAL);

		if (pcr->action == PF_CHANGE_GET_TICKET) {
			pcr->ticket = ++ruleset->rules[rs_num].active.ticket;
			ERROUT(0);
		} else if (pcr->ticket !=
			    ruleset->rules[rs_num].active.ticket)
				ERROUT(EINVAL);

		if (pcr->action != PF_CHANGE_REMOVE) {
			if (newrule->ifname[0]) {
				newrule->kif = pfi_kif_attach(kif,
				    newrule->ifname);
				pfi_kif_ref(newrule->kif);
			} else
				newrule->kif = NULL;

			if (newrule->rtableid > 0 &&
			    newrule->rtableid >= rt_numfibs)
				error = EBUSY;

#ifdef ALTQ
			/* set queue IDs */
			if (newrule->qname[0] != 0) {
				if ((newrule->qid =
				    pf_qname2qid(newrule->qname)) == 0)
					error = EBUSY;
				else if (newrule->pqname[0] != 0) {
					if ((newrule->pqid =
					    pf_qname2qid(newrule->pqname)) == 0)
						error = EBUSY;
				} else
					newrule->pqid = newrule->qid;
			}
#endif /* ALTQ */
			if (newrule->tagname[0])
				if ((newrule->tag =
				    pf_tagname2tag(newrule->tagname)) == 0)
					error = EBUSY;
			if (newrule->match_tagname[0])
				if ((newrule->match_tag = pf_tagname2tag(
				    newrule->match_tagname)) == 0)
					error = EBUSY;
			if (newrule->rt && !newrule->direction)
				error = EINVAL;
			if (!newrule->log)
				newrule->logif = 0;
			if (newrule->logif >= PFLOGIFS_MAX)
				error = EINVAL;
			if (pf_addr_setup(ruleset, &newrule->src.addr, newrule->af))
				error = ENOMEM;
			if (pf_addr_setup(ruleset, &newrule->dst.addr, newrule->af))
				error = ENOMEM;
			if (pf_anchor_setup(newrule, ruleset, pcr->anchor_call))
				error = EINVAL;
			TAILQ_FOREACH(pa, &V_pf_pabuf, entries)
				if (pa->addr.type == PF_ADDR_TABLE) {
					pa->addr.p.tbl =
					    pfr_attach_table(ruleset,
					    pa->addr.v.tblname);
					if (pa->addr.p.tbl == NULL)
						error = ENOMEM;
				}

			newrule->overload_tbl = NULL;
			if (newrule->overload_tblname[0]) {
				if ((newrule->overload_tbl = pfr_attach_table(
				    ruleset, newrule->overload_tblname)) ==
				    NULL)
					error = EINVAL;
				else
					newrule->overload_tbl->pfrkt_flags |=
					    PFR_TFLAG_ACTIVE;
			}

			pf_mv_pool(&V_pf_pabuf, &newrule->rpool.list);
			if (((((newrule->action == PF_NAT) ||
			    (newrule->action == PF_RDR) ||
			    (newrule->action == PF_BINAT) ||
			    (newrule->rt > PF_NOPFROUTE)) &&
			    !newrule->anchor)) &&
			    (TAILQ_FIRST(&newrule->rpool.list) == NULL))
				error = EINVAL;

			if (error) {
				pf_free_rule(newrule);
				PF_RULES_WUNLOCK();
				break;
			}

			newrule->rpool.cur = TAILQ_FIRST(&newrule->rpool.list);
			newrule->evaluations = 0;
			newrule->packets[0] = newrule->packets[1] = 0;
			newrule->bytes[0] = newrule->bytes[1] = 0;
		}
		pf_empty_pool(&V_pf_pabuf);

		if (pcr->action == PF_CHANGE_ADD_HEAD)
			oldrule = TAILQ_FIRST(
			    ruleset->rules[rs_num].active.ptr);
		else if (pcr->action == PF_CHANGE_ADD_TAIL)
			oldrule = TAILQ_LAST(
			    ruleset->rules[rs_num].active.ptr, pf_rulequeue);
		else {
			oldrule = TAILQ_FIRST(
			    ruleset->rules[rs_num].active.ptr);
			while ((oldrule != NULL) && (oldrule->nr != pcr->nr))
				oldrule = TAILQ_NEXT(oldrule, entries);
			if (oldrule == NULL) {
				if (newrule != NULL)
					pf_free_rule(newrule);
				PF_RULES_WUNLOCK();
				error = EINVAL;
				break;
			}
		}

		if (pcr->action == PF_CHANGE_REMOVE) {
			pf_unlink_rule(ruleset->rules[rs_num].active.ptr,
			    oldrule);
			ruleset->rules[rs_num].active.rcount--;
		} else {
			if (oldrule == NULL)
				TAILQ_INSERT_TAIL(
				    ruleset->rules[rs_num].active.ptr,
				    newrule, entries);
			else if (pcr->action == PF_CHANGE_ADD_HEAD ||
			    pcr->action == PF_CHANGE_ADD_BEFORE)
				TAILQ_INSERT_BEFORE(oldrule, newrule, entries);
			else
				TAILQ_INSERT_AFTER(
				    ruleset->rules[rs_num].active.ptr,
				    oldrule, newrule, entries);
			ruleset->rules[rs_num].active.rcount++;
		}

		nr = 0;
		TAILQ_FOREACH(oldrule,
		    ruleset->rules[rs_num].active.ptr, entries)
			oldrule->nr = nr++;

		ruleset->rules[rs_num].active.ticket++;

		pf_calc_skip_steps(ruleset->rules[rs_num].active.ptr);
		pf_remove_if_empty_ruleset(ruleset);

		PF_RULES_WUNLOCK();
		break;

#undef ERROUT
DIOCCHANGERULE_error:
		PF_RULES_WUNLOCK();
		if (newrule != NULL) {
			counter_u64_free(newrule->states_cur);
			counter_u64_free(newrule->states_tot);
			counter_u64_free(newrule->src_nodes);
			free(newrule, M_PFRULE);
		}
		if (kif != NULL)
			free(kif, PFI_MTYPE);
		break;
	}

	case DIOCCLRSTATES: {
		struct pf_state		*s;
		struct pfioc_state_kill *psk = (struct pfioc_state_kill *)addr;
		u_int			 i, killed = 0;

		for (i = 0; i <= pf_hashmask; i++) {
			struct pf_idhash *ih = &V_pf_idhash[i];

relock_DIOCCLRSTATES:
			PF_HASHROW_LOCK(ih);
			LIST_FOREACH(s, &ih->states, entry)
				if (!psk->psk_ifname[0] ||
				    !strcmp(psk->psk_ifname,
				    s->kif->pfik_name)) {
					/*
					 * Don't send out individual
					 * delete messages.
					 */
					s->state_flags |= PFSTATE_NOSYNC;
					pf_unlink_state(s, PF_ENTER_LOCKED);
					killed++;
					goto relock_DIOCCLRSTATES;
				}
			PF_HASHROW_UNLOCK(ih);
		}
		psk->psk_killed = killed;
		if (V_pfsync_clear_states_ptr != NULL)
			V_pfsync_clear_states_ptr(V_pf_status.hostid, psk->psk_ifname);
		break;
	}

	case DIOCKILLSTATES: {
		struct pf_state		*s;
		struct pf_state_key	*sk;
		struct pf_addr		*srcaddr, *dstaddr;
		u_int16_t		 srcport, dstport;
		struct pfioc_state_kill	*psk = (struct pfioc_state_kill *)addr;
		u_int			 i, killed = 0;

		if (psk->psk_pfcmp.id) {
			if (psk->psk_pfcmp.creatorid == 0)
				psk->psk_pfcmp.creatorid = V_pf_status.hostid;
			if ((s = pf_find_state_byid(psk->psk_pfcmp.id,
			    psk->psk_pfcmp.creatorid))) {
				pf_unlink_state(s, PF_ENTER_LOCKED);
				psk->psk_killed = 1;
			}
			break;
		}

		for (i = 0; i <= pf_hashmask; i++) {
			struct pf_idhash *ih = &V_pf_idhash[i];

relock_DIOCKILLSTATES:
			PF_HASHROW_LOCK(ih);
			LIST_FOREACH(s, &ih->states, entry) {
				sk = s->key[PF_SK_WIRE];
				if (s->direction == PF_OUT) {
					srcaddr = &sk->addr[1];
					dstaddr = &sk->addr[0];
					srcport = sk->port[1];
					dstport = sk->port[0];
				} else {
					srcaddr = &sk->addr[0];
					dstaddr = &sk->addr[1];
					srcport = sk->port[0];
					dstport = sk->port[1];
				}

				if ((!psk->psk_af || sk->af == psk->psk_af)
				    && (!psk->psk_proto || psk->psk_proto ==
				    sk->proto) &&
				    PF_MATCHA(psk->psk_src.neg,
				    &psk->psk_src.addr.v.a.addr,
				    &psk->psk_src.addr.v.a.mask,
				    srcaddr, sk->af) &&
				    PF_MATCHA(psk->psk_dst.neg,
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
				    (!psk->psk_label[0] ||
				    (s->rule.ptr->label[0] &&
				    !strcmp(psk->psk_label,
				    s->rule.ptr->label))) &&
				    (!psk->psk_ifname[0] ||
				    !strcmp(psk->psk_ifname,
				    s->kif->pfik_name))) {
					pf_unlink_state(s, PF_ENTER_LOCKED);
					killed++;
					goto relock_DIOCKILLSTATES;
				}
			}
			PF_HASHROW_UNLOCK(ih);
		}
		psk->psk_killed = killed;
		break;
	}

	case DIOCADDSTATE: {
		struct pfioc_state	*ps = (struct pfioc_state *)addr;
		struct pfsync_state	*sp = &ps->state;

		if (sp->timeout >= PFTM_MAX) {
			error = EINVAL;
			break;
		}
		if (V_pfsync_state_import_ptr != NULL) {
			PF_RULES_RLOCK();
			error = V_pfsync_state_import_ptr(sp, PFSYNC_SI_IOCTL);
			PF_RULES_RUNLOCK();
		} else
			error = EOPNOTSUPP;
		break;
	}

	case DIOCGETSTATE: {
		struct pfioc_state	*ps = (struct pfioc_state *)addr;
		struct pf_state		*s;

		s = pf_find_state_byid(ps->state.id, ps->state.creatorid);
		if (s == NULL) {
			error = ENOENT;
			break;
		}

		pfsync_state_export(&ps->state, s);
		PF_STATE_UNLOCK(s);
		break;
	}

	case DIOCGETSTATES: {
		struct pfioc_states	*ps = (struct pfioc_states *)addr;
		struct pf_state		*s;
		struct pfsync_state	*pstore, *p;
		int i, nr;

		if (ps->ps_len == 0) {
			nr = uma_zone_get_cur(V_pf_state_z);
			ps->ps_len = sizeof(struct pfsync_state) * nr;
			break;
		}

		p = pstore = malloc(ps->ps_len, M_TEMP, M_WAITOK);
		nr = 0;

		for (i = 0; i <= pf_hashmask; i++) {
			struct pf_idhash *ih = &V_pf_idhash[i];

			PF_HASHROW_LOCK(ih);
			LIST_FOREACH(s, &ih->states, entry) {

				if (s->timeout == PFTM_UNLINKED)
					continue;

				if ((nr+1) * sizeof(*p) > ps->ps_len) {
					PF_HASHROW_UNLOCK(ih);
					goto DIOCGETSTATES_full;
				}
				pfsync_state_export(p, s);
				p++;
				nr++;
			}
			PF_HASHROW_UNLOCK(ih);
		}
DIOCGETSTATES_full:
		error = copyout(pstore, ps->ps_states,
		    sizeof(struct pfsync_state) * nr);
		if (error) {
			free(pstore, M_TEMP);
			break;
		}
		ps->ps_len = sizeof(struct pfsync_state) * nr;
		free(pstore, M_TEMP);

		break;
	}

	case DIOCGETSTATUS: {
		struct pf_status *s = (struct pf_status *)addr;

		PF_RULES_RLOCK();
		s->running = V_pf_status.running;
		s->since   = V_pf_status.since;
		s->debug   = V_pf_status.debug;
		s->hostid  = V_pf_status.hostid;
		s->states  = V_pf_status.states;
		s->src_nodes = V_pf_status.src_nodes;

		for (int i = 0; i < PFRES_MAX; i++)
			s->counters[i] =
			    counter_u64_fetch(V_pf_status.counters[i]);
		for (int i = 0; i < LCNT_MAX; i++)
			s->lcounters[i] =
			    counter_u64_fetch(V_pf_status.lcounters[i]);
		for (int i = 0; i < FCNT_MAX; i++)
			s->fcounters[i] =
			    counter_u64_fetch(V_pf_status.fcounters[i]);
		for (int i = 0; i < SCNT_MAX; i++)
			s->scounters[i] =
			    counter_u64_fetch(V_pf_status.scounters[i]);

		bcopy(V_pf_status.ifname, s->ifname, IFNAMSIZ);
		bcopy(V_pf_status.pf_chksum, s->pf_chksum,
		    PF_MD5_DIGEST_LENGTH);

		pfi_update_status(s->ifname, s);
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCSETSTATUSIF: {
		struct pfioc_if	*pi = (struct pfioc_if *)addr;

		if (pi->ifname[0] == 0) {
			bzero(V_pf_status.ifname, IFNAMSIZ);
			break;
		}
		PF_RULES_WLOCK();
		strlcpy(V_pf_status.ifname, pi->ifname, IFNAMSIZ);
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCCLRSTATUS: {
		PF_RULES_WLOCK();
		for (int i = 0; i < PFRES_MAX; i++)
			counter_u64_zero(V_pf_status.counters[i]);
		for (int i = 0; i < FCNT_MAX; i++)
			counter_u64_zero(V_pf_status.fcounters[i]);
		for (int i = 0; i < SCNT_MAX; i++)
			counter_u64_zero(V_pf_status.scounters[i]);
		for (int i = 0; i < LCNT_MAX; i++)
			counter_u64_zero(V_pf_status.lcounters[i]);
		V_pf_status.since = time_second;
		if (*V_pf_status.ifname)
			pfi_update_status(V_pf_status.ifname, NULL);
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCNATLOOK: {
		struct pfioc_natlook	*pnl = (struct pfioc_natlook *)addr;
		struct pf_state_key	*sk;
		struct pf_state		*state;
		struct pf_state_key_cmp	 key;
		int			 m = 0, direction = pnl->direction;
		int			 sidx, didx;

		/* NATLOOK src and dst are reversed, so reverse sidx/didx */
		sidx = (direction == PF_IN) ? 1 : 0;
		didx = (direction == PF_IN) ? 0 : 1;

		if (!pnl->proto ||
		    PF_AZERO(&pnl->saddr, pnl->af) ||
		    PF_AZERO(&pnl->daddr, pnl->af) ||
		    ((pnl->proto == IPPROTO_TCP ||
		    pnl->proto == IPPROTO_UDP) &&
		    (!pnl->dport || !pnl->sport)))
			error = EINVAL;
		else {
			bzero(&key, sizeof(key));
			key.af = pnl->af;
			key.proto = pnl->proto;
			PF_ACPY(&key.addr[sidx], &pnl->saddr, pnl->af);
			key.port[sidx] = pnl->sport;
			PF_ACPY(&key.addr[didx], &pnl->daddr, pnl->af);
			key.port[didx] = pnl->dport;

			state = pf_find_state_all(&key, direction, &m);

			if (m > 1)
				error = E2BIG;	/* more than one state */
			else if (state != NULL) {
				/* XXXGL: not locked read */
				sk = state->key[sidx];
				PF_ACPY(&pnl->rsaddr, &sk->addr[sidx], sk->af);
				pnl->rsport = sk->port[sidx];
				PF_ACPY(&pnl->rdaddr, &sk->addr[didx], sk->af);
				pnl->rdport = sk->port[didx];
			} else
				error = ENOENT;
		}
		break;
	}

	case DIOCSETTIMEOUT: {
		struct pfioc_tm	*pt = (struct pfioc_tm *)addr;
		int		 old;

		if (pt->timeout < 0 || pt->timeout >= PFTM_MAX ||
		    pt->seconds < 0) {
			error = EINVAL;
			break;
		}
		PF_RULES_WLOCK();
		old = V_pf_default_rule.timeout[pt->timeout];
		if (pt->timeout == PFTM_INTERVAL && pt->seconds == 0)
			pt->seconds = 1;
		V_pf_default_rule.timeout[pt->timeout] = pt->seconds;
		if (pt->timeout == PFTM_INTERVAL && pt->seconds < old)
			wakeup(pf_purge_thread);
		pt->seconds = old;
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCGETTIMEOUT: {
		struct pfioc_tm	*pt = (struct pfioc_tm *)addr;

		if (pt->timeout < 0 || pt->timeout >= PFTM_MAX) {
			error = EINVAL;
			break;
		}
		PF_RULES_RLOCK();
		pt->seconds = V_pf_default_rule.timeout[pt->timeout];
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCGETLIMIT: {
		struct pfioc_limit	*pl = (struct pfioc_limit *)addr;

		if (pl->index < 0 || pl->index >= PF_LIMIT_MAX) {
			error = EINVAL;
			break;
		}
		PF_RULES_RLOCK();
		pl->limit = V_pf_limits[pl->index].limit;
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCSETLIMIT: {
		struct pfioc_limit	*pl = (struct pfioc_limit *)addr;
		int			 old_limit;

		PF_RULES_WLOCK();
		if (pl->index < 0 || pl->index >= PF_LIMIT_MAX ||
		    V_pf_limits[pl->index].zone == NULL) {
			PF_RULES_WUNLOCK();
			error = EINVAL;
			break;
		}
		uma_zone_set_max(V_pf_limits[pl->index].zone, pl->limit);
		old_limit = V_pf_limits[pl->index].limit;
		V_pf_limits[pl->index].limit = pl->limit;
		pl->limit = old_limit;
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCSETDEBUG: {
		u_int32_t	*level = (u_int32_t *)addr;

		PF_RULES_WLOCK();
		V_pf_status.debug = *level;
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCCLRRULECTRS: {
		/* obsoleted by DIOCGETRULE with action=PF_GET_CLR_CNTR */
		struct pf_ruleset	*ruleset = &pf_main_ruleset;
		struct pf_rule		*rule;

		PF_RULES_WLOCK();
		TAILQ_FOREACH(rule,
		    ruleset->rules[PF_RULESET_FILTER].active.ptr, entries) {
			rule->evaluations = 0;
			rule->packets[0] = rule->packets[1] = 0;
			rule->bytes[0] = rule->bytes[1] = 0;
		}
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCGIFSPEEDV0:
	case DIOCGIFSPEEDV1: {
		struct pf_ifspeed_v1	*psp = (struct pf_ifspeed_v1 *)addr;
		struct pf_ifspeed_v1	ps;
		struct ifnet		*ifp;

		if (psp->ifname[0] != 0) {
			/* Can we completely trust user-land? */
			strlcpy(ps.ifname, psp->ifname, IFNAMSIZ);
			ifp = ifunit(ps.ifname);
			if (ifp != NULL) {
				psp->baudrate32 =
				    (u_int32_t)uqmin(ifp->if_baudrate, UINT_MAX);
				if (cmd == DIOCGIFSPEEDV1)
					psp->baudrate = ifp->if_baudrate;
			} else
				error = EINVAL;
		} else
			error = EINVAL;
		break;
	}

#ifdef ALTQ
	case DIOCSTARTALTQ: {
		struct pf_altq		*altq;

		PF_RULES_WLOCK();
		/* enable all altq interfaces on active list */
		TAILQ_FOREACH(altq, V_pf_altq_ifs_active, entries) {
			if ((altq->local_flags & PFALTQ_FLAG_IF_REMOVED) == 0) {
				error = pf_enable_altq(altq);
				if (error != 0)
					break;
			}
		}
		if (error == 0)
			V_pf_altq_running = 1;
		PF_RULES_WUNLOCK();
		DPFPRINTF(PF_DEBUG_MISC, ("altq: started\n"));
		break;
	}

	case DIOCSTOPALTQ: {
		struct pf_altq		*altq;

		PF_RULES_WLOCK();
		/* disable all altq interfaces on active list */
		TAILQ_FOREACH(altq, V_pf_altq_ifs_active, entries) {
			if ((altq->local_flags & PFALTQ_FLAG_IF_REMOVED) == 0) {
				error = pf_disable_altq(altq);
				if (error != 0)
					break;
			}
		}
		if (error == 0)
			V_pf_altq_running = 0;
		PF_RULES_WUNLOCK();
		DPFPRINTF(PF_DEBUG_MISC, ("altq: stopped\n"));
		break;
	}

	case DIOCADDALTQV0:
	case DIOCADDALTQV1: {
		struct pfioc_altq_v1	*pa = (struct pfioc_altq_v1 *)addr;
		struct pf_altq		*altq, *a;
		struct ifnet		*ifp;

		altq = malloc(sizeof(*altq), M_PFALTQ, M_WAITOK | M_ZERO);
		error = pf_import_kaltq(pa, altq, IOCPARM_LEN(cmd));
		if (error)
			break;
		altq->local_flags = 0;

		PF_RULES_WLOCK();
		if (pa->ticket != V_ticket_altqs_inactive) {
			PF_RULES_WUNLOCK();
			free(altq, M_PFALTQ);
			error = EBUSY;
			break;
		}

		/*
		 * if this is for a queue, find the discipline and
		 * copy the necessary fields
		 */
		if (altq->qname[0] != 0) {
			if ((altq->qid = pf_qname2qid(altq->qname)) == 0) {
				PF_RULES_WUNLOCK();
				error = EBUSY;
				free(altq, M_PFALTQ);
				break;
			}
			altq->altq_disc = NULL;
			TAILQ_FOREACH(a, V_pf_altq_ifs_inactive, entries) {
				if (strncmp(a->ifname, altq->ifname,
				    IFNAMSIZ) == 0) {
					altq->altq_disc = a->altq_disc;
					break;
				}
			}
		}

		if ((ifp = ifunit(altq->ifname)) == NULL)
			altq->local_flags |= PFALTQ_FLAG_IF_REMOVED;
		else
			error = altq_add(ifp, altq);

		if (error) {
			PF_RULES_WUNLOCK();
			free(altq, M_PFALTQ);
			break;
		}

		if (altq->qname[0] != 0)
			TAILQ_INSERT_TAIL(V_pf_altqs_inactive, altq, entries);
		else
			TAILQ_INSERT_TAIL(V_pf_altq_ifs_inactive, altq, entries);
		/* version error check done on import above */
		pf_export_kaltq(altq, pa, IOCPARM_LEN(cmd));
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCGETALTQSV0:
	case DIOCGETALTQSV1: {
		struct pfioc_altq_v1	*pa = (struct pfioc_altq_v1 *)addr;
		struct pf_altq		*altq;

		PF_RULES_RLOCK();
		pa->nr = 0;
		TAILQ_FOREACH(altq, V_pf_altq_ifs_active, entries)
			pa->nr++;
		TAILQ_FOREACH(altq, V_pf_altqs_active, entries)
			pa->nr++;
		pa->ticket = V_ticket_altqs_active;
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCGETALTQV0:
	case DIOCGETALTQV1: {
		struct pfioc_altq_v1	*pa = (struct pfioc_altq_v1 *)addr;
		struct pf_altq		*altq;

		PF_RULES_RLOCK();
		if (pa->ticket != V_ticket_altqs_active) {
			PF_RULES_RUNLOCK();
			error = EBUSY;
			break;
		}
		altq = pf_altq_get_nth_active(pa->nr);
		if (altq == NULL) {
			PF_RULES_RUNLOCK();
			error = EBUSY;
			break;
		}
		pf_export_kaltq(altq, pa, IOCPARM_LEN(cmd));
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCCHANGEALTQV0:
	case DIOCCHANGEALTQV1:
		/* CHANGEALTQ not supported yet! */
		error = ENODEV;
		break;

	case DIOCGETQSTATSV0:
	case DIOCGETQSTATSV1: {
		struct pfioc_qstats_v1	*pq = (struct pfioc_qstats_v1 *)addr;
		struct pf_altq		*altq;
		int			 nbytes;
		u_int32_t		 version;

		PF_RULES_RLOCK();
		if (pq->ticket != V_ticket_altqs_active) {
			PF_RULES_RUNLOCK();
			error = EBUSY;
			break;
		}
		nbytes = pq->nbytes;
		altq = pf_altq_get_nth_active(pq->nr);
		if (altq == NULL) {
			PF_RULES_RUNLOCK();
			error = EBUSY;
			break;
		}

		if ((altq->local_flags & PFALTQ_FLAG_IF_REMOVED) != 0) {
			PF_RULES_RUNLOCK();
			error = ENXIO;
			break;
		}
		PF_RULES_RUNLOCK();
		if (cmd == DIOCGETQSTATSV0)
			version = 0;  /* DIOCGETQSTATSV0 means stats struct v0 */
		else
			version = pq->version;
		error = altq_getqstats(altq, pq->buf, &nbytes, version);
		if (error == 0) {
			pq->scheduler = altq->scheduler;
			pq->nbytes = nbytes;
		}
		break;
	}
#endif /* ALTQ */

	case DIOCBEGINADDRS: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;

		PF_RULES_WLOCK();
		pf_empty_pool(&V_pf_pabuf);
		pp->ticket = ++V_ticket_pabuf;
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCADDADDR: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;
		struct pf_pooladdr	*pa;
		struct pfi_kif		*kif = NULL;

#ifndef INET
		if (pp->af == AF_INET) {
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET */
#ifndef INET6
		if (pp->af == AF_INET6) {
			error = EAFNOSUPPORT;
			break;
		}
#endif /* INET6 */
		if (pp->addr.addr.type != PF_ADDR_ADDRMASK &&
		    pp->addr.addr.type != PF_ADDR_DYNIFTL &&
		    pp->addr.addr.type != PF_ADDR_TABLE) {
			error = EINVAL;
			break;
		}
		pa = malloc(sizeof(*pa), M_PFRULE, M_WAITOK);
		bcopy(&pp->addr, pa, sizeof(struct pf_pooladdr));
		if (pa->ifname[0])
			kif = malloc(sizeof(*kif), PFI_MTYPE, M_WAITOK);
		PF_RULES_WLOCK();
		if (pp->ticket != V_ticket_pabuf) {
			PF_RULES_WUNLOCK();
			if (pa->ifname[0])
				free(kif, PFI_MTYPE);
			free(pa, M_PFRULE);
			error = EBUSY;
			break;
		}
		if (pa->ifname[0]) {
			pa->kif = pfi_kif_attach(kif, pa->ifname);
			pfi_kif_ref(pa->kif);
		} else
			pa->kif = NULL;
		if (pa->addr.type == PF_ADDR_DYNIFTL && ((error =
		    pfi_dynaddr_setup(&pa->addr, pp->af)) != 0)) {
			if (pa->ifname[0])
				pfi_kif_unref(pa->kif);
			PF_RULES_WUNLOCK();
			free(pa, M_PFRULE);
			break;
		}
		TAILQ_INSERT_TAIL(&V_pf_pabuf, pa, entries);
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCGETADDRS: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;
		struct pf_pool		*pool;
		struct pf_pooladdr	*pa;

		PF_RULES_RLOCK();
		pp->nr = 0;
		pool = pf_get_pool(pp->anchor, pp->ticket, pp->r_action,
		    pp->r_num, 0, 1, 0);
		if (pool == NULL) {
			PF_RULES_RUNLOCK();
			error = EBUSY;
			break;
		}
		TAILQ_FOREACH(pa, &pool->list, entries)
			pp->nr++;
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCGETADDR: {
		struct pfioc_pooladdr	*pp = (struct pfioc_pooladdr *)addr;
		struct pf_pool		*pool;
		struct pf_pooladdr	*pa;
		u_int32_t		 nr = 0;

		PF_RULES_RLOCK();
		pool = pf_get_pool(pp->anchor, pp->ticket, pp->r_action,
		    pp->r_num, 0, 1, 1);
		if (pool == NULL) {
			PF_RULES_RUNLOCK();
			error = EBUSY;
			break;
		}
		pa = TAILQ_FIRST(&pool->list);
		while ((pa != NULL) && (nr < pp->nr)) {
			pa = TAILQ_NEXT(pa, entries);
			nr++;
		}
		if (pa == NULL) {
			PF_RULES_RUNLOCK();
			error = EBUSY;
			break;
		}
		bcopy(pa, &pp->addr, sizeof(struct pf_pooladdr));
		pf_addr_copyout(&pp->addr.addr);
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCCHANGEADDR: {
		struct pfioc_pooladdr	*pca = (struct pfioc_pooladdr *)addr;
		struct pf_pool		*pool;
		struct pf_pooladdr	*oldpa = NULL, *newpa = NULL;
		struct pf_ruleset	*ruleset;
		struct pfi_kif		*kif = NULL;

		if (pca->action < PF_CHANGE_ADD_HEAD ||
		    pca->action > PF_CHANGE_REMOVE) {
			error = EINVAL;
			break;
		}
		if (pca->addr.addr.type != PF_ADDR_ADDRMASK &&
		    pca->addr.addr.type != PF_ADDR_DYNIFTL &&
		    pca->addr.addr.type != PF_ADDR_TABLE) {
			error = EINVAL;
			break;
		}

		if (pca->action != PF_CHANGE_REMOVE) {
#ifndef INET
			if (pca->af == AF_INET) {
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET */
#ifndef INET6
			if (pca->af == AF_INET6) {
				error = EAFNOSUPPORT;
				break;
			}
#endif /* INET6 */
			newpa = malloc(sizeof(*newpa), M_PFRULE, M_WAITOK);
			bcopy(&pca->addr, newpa, sizeof(struct pf_pooladdr));
			if (newpa->ifname[0])
				kif = malloc(sizeof(*kif), PFI_MTYPE, M_WAITOK);
			newpa->kif = NULL;
		}

#define	ERROUT(x)	{ error = (x); goto DIOCCHANGEADDR_error; }
		PF_RULES_WLOCK();
		ruleset = pf_find_ruleset(pca->anchor);
		if (ruleset == NULL)
			ERROUT(EBUSY);

		pool = pf_get_pool(pca->anchor, pca->ticket, pca->r_action,
		    pca->r_num, pca->r_last, 1, 1);
		if (pool == NULL)
			ERROUT(EBUSY);

		if (pca->action != PF_CHANGE_REMOVE) {
			if (newpa->ifname[0]) {
				newpa->kif = pfi_kif_attach(kif, newpa->ifname);
				pfi_kif_ref(newpa->kif);
				kif = NULL;
			}

			switch (newpa->addr.type) {
			case PF_ADDR_DYNIFTL:
				error = pfi_dynaddr_setup(&newpa->addr,
				    pca->af);
				break;
			case PF_ADDR_TABLE:
				newpa->addr.p.tbl = pfr_attach_table(ruleset,
				    newpa->addr.v.tblname);
				if (newpa->addr.p.tbl == NULL)
					error = ENOMEM;
				break;
			}
			if (error)
				goto DIOCCHANGEADDR_error;
		}

		switch (pca->action) {
		case PF_CHANGE_ADD_HEAD:
			oldpa = TAILQ_FIRST(&pool->list);
			break;
		case PF_CHANGE_ADD_TAIL:
			oldpa = TAILQ_LAST(&pool->list, pf_palist);
			break;
		default:
			oldpa = TAILQ_FIRST(&pool->list);
			for (int i = 0; oldpa && i < pca->nr; i++)
				oldpa = TAILQ_NEXT(oldpa, entries);

			if (oldpa == NULL)
				ERROUT(EINVAL);
		}

		if (pca->action == PF_CHANGE_REMOVE) {
			TAILQ_REMOVE(&pool->list, oldpa, entries);
			switch (oldpa->addr.type) {
			case PF_ADDR_DYNIFTL:
				pfi_dynaddr_remove(oldpa->addr.p.dyn);
				break;
			case PF_ADDR_TABLE:
				pfr_detach_table(oldpa->addr.p.tbl);
				break;
			}
			if (oldpa->kif)
				pfi_kif_unref(oldpa->kif);
			free(oldpa, M_PFRULE);
		} else {
			if (oldpa == NULL)
				TAILQ_INSERT_TAIL(&pool->list, newpa, entries);
			else if (pca->action == PF_CHANGE_ADD_HEAD ||
			    pca->action == PF_CHANGE_ADD_BEFORE)
				TAILQ_INSERT_BEFORE(oldpa, newpa, entries);
			else
				TAILQ_INSERT_AFTER(&pool->list, oldpa,
				    newpa, entries);
		}

		pool->cur = TAILQ_FIRST(&pool->list);
		PF_ACPY(&pool->counter, &pool->cur->addr.v.a.addr, pca->af);
		PF_RULES_WUNLOCK();
		break;

#undef ERROUT
DIOCCHANGEADDR_error:
		if (newpa != NULL) {
			if (newpa->kif)
				pfi_kif_unref(newpa->kif);
			free(newpa, M_PFRULE);
		}
		PF_RULES_WUNLOCK();
		if (kif != NULL)
			free(kif, PFI_MTYPE);
		break;
	}

	case DIOCGETRULESETS: {
		struct pfioc_ruleset	*pr = (struct pfioc_ruleset *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_anchor	*anchor;

		PF_RULES_RLOCK();
		pr->path[sizeof(pr->path) - 1] = 0;
		if ((ruleset = pf_find_ruleset(pr->path)) == NULL) {
			PF_RULES_RUNLOCK();
			error = ENOENT;
			break;
		}
		pr->nr = 0;
		if (ruleset->anchor == NULL) {
			/* XXX kludge for pf_main_ruleset */
			RB_FOREACH(anchor, pf_anchor_global, &V_pf_anchors)
				if (anchor->parent == NULL)
					pr->nr++;
		} else {
			RB_FOREACH(anchor, pf_anchor_node,
			    &ruleset->anchor->children)
				pr->nr++;
		}
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCGETRULESET: {
		struct pfioc_ruleset	*pr = (struct pfioc_ruleset *)addr;
		struct pf_ruleset	*ruleset;
		struct pf_anchor	*anchor;
		u_int32_t		 nr = 0;

		PF_RULES_RLOCK();
		pr->path[sizeof(pr->path) - 1] = 0;
		if ((ruleset = pf_find_ruleset(pr->path)) == NULL) {
			PF_RULES_RUNLOCK();
			error = ENOENT;
			break;
		}
		pr->name[0] = 0;
		if (ruleset->anchor == NULL) {
			/* XXX kludge for pf_main_ruleset */
			RB_FOREACH(anchor, pf_anchor_global, &V_pf_anchors)
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
		if (!pr->name[0])
			error = EBUSY;
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCRCLRTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			break;
		}
		PF_RULES_WLOCK();
		error = pfr_clr_tables(&io->pfrio_table, &io->pfrio_ndel,
		    io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCRADDTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_table *pfrts;
		size_t totlen;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}

		if (io->pfrio_size < 0 || io->pfrio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfrio_size, sizeof(struct pfr_table))) {
			error = ENOMEM;
			break;
		}

		totlen = io->pfrio_size * sizeof(struct pfr_table);
		pfrts = mallocarray(io->pfrio_size, sizeof(struct pfr_table),
		    M_TEMP, M_WAITOK);
		error = copyin(io->pfrio_buffer, pfrts, totlen);
		if (error) {
			free(pfrts, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		error = pfr_add_tables(pfrts, io->pfrio_size,
		    &io->pfrio_nadd, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		free(pfrts, M_TEMP);
		break;
	}

	case DIOCRDELTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_table *pfrts;
		size_t totlen;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}

		if (io->pfrio_size < 0 || io->pfrio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfrio_size, sizeof(struct pfr_table))) {
			error = ENOMEM;
			break;
		}

		totlen = io->pfrio_size * sizeof(struct pfr_table);
		pfrts = mallocarray(io->pfrio_size, sizeof(struct pfr_table),
		    M_TEMP, M_WAITOK);
		error = copyin(io->pfrio_buffer, pfrts, totlen);
		if (error) {
			free(pfrts, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		error = pfr_del_tables(pfrts, io->pfrio_size,
		    &io->pfrio_ndel, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		free(pfrts, M_TEMP);
		break;
	}

	case DIOCRGETTABLES: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_table *pfrts;
		size_t totlen, n;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}
		PF_RULES_RLOCK();
		n = pfr_table_count(&io->pfrio_table, io->pfrio_flags);
		io->pfrio_size = min(io->pfrio_size, n);

		totlen = io->pfrio_size * sizeof(struct pfr_table);

		pfrts = mallocarray(io->pfrio_size, sizeof(struct pfr_table),
		    M_TEMP, M_NOWAIT);
		if (pfrts == NULL) {
			error = ENOMEM;
			PF_RULES_RUNLOCK();
			break;
		}
		error = pfr_get_tables(&io->pfrio_table, pfrts,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_RUNLOCK();
		if (error == 0)
			error = copyout(pfrts, io->pfrio_buffer, totlen);
		free(pfrts, M_TEMP);
		break;
	}

	case DIOCRGETTSTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_tstats *pfrtstats;
		size_t totlen, n;

		if (io->pfrio_esize != sizeof(struct pfr_tstats)) {
			error = ENODEV;
			break;
		}
		PF_RULES_WLOCK();
		n = pfr_table_count(&io->pfrio_table, io->pfrio_flags);
		io->pfrio_size = min(io->pfrio_size, n);

		totlen = io->pfrio_size * sizeof(struct pfr_tstats);
		pfrtstats = mallocarray(io->pfrio_size,
		    sizeof(struct pfr_tstats), M_TEMP, M_NOWAIT);
		if (pfrtstats == NULL) {
			error = ENOMEM;
			PF_RULES_WUNLOCK();
			break;
		}
		error = pfr_get_tstats(&io->pfrio_table, pfrtstats,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		if (error == 0)
			error = copyout(pfrtstats, io->pfrio_buffer, totlen);
		free(pfrtstats, M_TEMP);
		break;
	}

	case DIOCRCLRTSTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_table *pfrts;
		size_t totlen, n;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}

		PF_RULES_WLOCK();
		n = pfr_table_count(&io->pfrio_table, io->pfrio_flags);
		io->pfrio_size = min(io->pfrio_size, n);

		totlen = io->pfrio_size * sizeof(struct pfr_table);
		pfrts = mallocarray(io->pfrio_size, sizeof(struct pfr_table),
		    M_TEMP, M_NOWAIT);
		if (pfrts == NULL) {
			error = ENOMEM;
			PF_RULES_WUNLOCK();
			break;
		}
		error = copyin(io->pfrio_buffer, pfrts, totlen);
		if (error) {
			free(pfrts, M_TEMP);
			PF_RULES_WUNLOCK();
			break;
		}
		error = pfr_clr_tstats(pfrts, io->pfrio_size,
		    &io->pfrio_nzero, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		free(pfrts, M_TEMP);
		break;
	}

	case DIOCRSETTFLAGS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_table *pfrts;
		size_t totlen, n;

		if (io->pfrio_esize != sizeof(struct pfr_table)) {
			error = ENODEV;
			break;
		}

		PF_RULES_WLOCK();
		n = pfr_table_count(&io->pfrio_table, io->pfrio_flags);
		io->pfrio_size = min(io->pfrio_size, n);

		totlen = io->pfrio_size * sizeof(struct pfr_table);
		pfrts = mallocarray(io->pfrio_size, sizeof(struct pfr_table),
		    M_TEMP, M_NOWAIT);
		if (pfrts == NULL) {
			error = ENOMEM;
			PF_RULES_WUNLOCK();
			break;
		}
		error = copyin(io->pfrio_buffer, pfrts, totlen);
		if (error) {
			free(pfrts, M_TEMP);
			PF_RULES_WUNLOCK();
			break;
		}
		error = pfr_set_tflags(pfrts, io->pfrio_size,
		    io->pfrio_setflag, io->pfrio_clrflag, &io->pfrio_nchange,
		    &io->pfrio_ndel, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		free(pfrts, M_TEMP);
		break;
	}

	case DIOCRCLRADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;

		if (io->pfrio_esize != 0) {
			error = ENODEV;
			break;
		}
		PF_RULES_WLOCK();
		error = pfr_clr_addrs(&io->pfrio_table, &io->pfrio_ndel,
		    io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCRADDADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_addr *pfras;
		size_t totlen;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		if (io->pfrio_size < 0 ||
		    io->pfrio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfrio_size, sizeof(struct pfr_addr))) {
			error = EINVAL;
			break;
		}
		totlen = io->pfrio_size * sizeof(struct pfr_addr);
		pfras = mallocarray(io->pfrio_size, sizeof(struct pfr_addr),
		    M_TEMP, M_NOWAIT);
		if (! pfras) {
			error = ENOMEM;
			break;
		}
		error = copyin(io->pfrio_buffer, pfras, totlen);
		if (error) {
			free(pfras, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		error = pfr_add_addrs(&io->pfrio_table, pfras,
		    io->pfrio_size, &io->pfrio_nadd, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		if (error == 0 && io->pfrio_flags & PFR_FLAG_FEEDBACK)
			error = copyout(pfras, io->pfrio_buffer, totlen);
		free(pfras, M_TEMP);
		break;
	}

	case DIOCRDELADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_addr *pfras;
		size_t totlen;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		if (io->pfrio_size < 0 ||
		    io->pfrio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfrio_size, sizeof(struct pfr_addr))) {
			error = EINVAL;
			break;
		}
		totlen = io->pfrio_size * sizeof(struct pfr_addr);
		pfras = mallocarray(io->pfrio_size, sizeof(struct pfr_addr),
		    M_TEMP, M_NOWAIT);
		if (! pfras) {
			error = ENOMEM;
			break;
		}
		error = copyin(io->pfrio_buffer, pfras, totlen);
		if (error) {
			free(pfras, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		error = pfr_del_addrs(&io->pfrio_table, pfras,
		    io->pfrio_size, &io->pfrio_ndel, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		if (error == 0 && io->pfrio_flags & PFR_FLAG_FEEDBACK)
			error = copyout(pfras, io->pfrio_buffer, totlen);
		free(pfras, M_TEMP);
		break;
	}

	case DIOCRSETADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_addr *pfras;
		size_t totlen, count;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		if (io->pfrio_size < 0 || io->pfrio_size2 < 0) {
			error = EINVAL;
			break;
		}
		count = max(io->pfrio_size, io->pfrio_size2);
		if (count > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(count, sizeof(struct pfr_addr))) {
			error = EINVAL;
			break;
		}
		totlen = count * sizeof(struct pfr_addr);
		pfras = mallocarray(count, sizeof(struct pfr_addr), M_TEMP,
		    M_NOWAIT);
		if (! pfras) {
			error = ENOMEM;
			break;
		}
		error = copyin(io->pfrio_buffer, pfras, totlen);
		if (error) {
			free(pfras, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		error = pfr_set_addrs(&io->pfrio_table, pfras,
		    io->pfrio_size, &io->pfrio_size2, &io->pfrio_nadd,
		    &io->pfrio_ndel, &io->pfrio_nchange, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL, 0);
		PF_RULES_WUNLOCK();
		if (error == 0 && io->pfrio_flags & PFR_FLAG_FEEDBACK)
			error = copyout(pfras, io->pfrio_buffer, totlen);
		free(pfras, M_TEMP);
		break;
	}

	case DIOCRGETADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_addr *pfras;
		size_t totlen;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		if (io->pfrio_size < 0 ||
		    io->pfrio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfrio_size, sizeof(struct pfr_addr))) {
			error = EINVAL;
			break;
		}
		totlen = io->pfrio_size * sizeof(struct pfr_addr);
		pfras = mallocarray(io->pfrio_size, sizeof(struct pfr_addr),
		    M_TEMP, M_NOWAIT);
		if (! pfras) {
			error = ENOMEM;
			break;
		}
		PF_RULES_RLOCK();
		error = pfr_get_addrs(&io->pfrio_table, pfras,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_RUNLOCK();
		if (error == 0)
			error = copyout(pfras, io->pfrio_buffer, totlen);
		free(pfras, M_TEMP);
		break;
	}

	case DIOCRGETASTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_astats *pfrastats;
		size_t totlen;

		if (io->pfrio_esize != sizeof(struct pfr_astats)) {
			error = ENODEV;
			break;
		}
		if (io->pfrio_size < 0 ||
		    io->pfrio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfrio_size, sizeof(struct pfr_astats))) {
			error = EINVAL;
			break;
		}
		totlen = io->pfrio_size * sizeof(struct pfr_astats);
		pfrastats = mallocarray(io->pfrio_size,
		    sizeof(struct pfr_astats), M_TEMP, M_NOWAIT);
		if (! pfrastats) {
			error = ENOMEM;
			break;
		}
		PF_RULES_RLOCK();
		error = pfr_get_astats(&io->pfrio_table, pfrastats,
		    &io->pfrio_size, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_RUNLOCK();
		if (error == 0)
			error = copyout(pfrastats, io->pfrio_buffer, totlen);
		free(pfrastats, M_TEMP);
		break;
	}

	case DIOCRCLRASTATS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_addr *pfras;
		size_t totlen;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		if (io->pfrio_size < 0 ||
		    io->pfrio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfrio_size, sizeof(struct pfr_addr))) {
			error = EINVAL;
			break;
		}
		totlen = io->pfrio_size * sizeof(struct pfr_addr);
		pfras = mallocarray(io->pfrio_size, sizeof(struct pfr_addr),
		    M_TEMP, M_NOWAIT);
		if (! pfras) {
			error = ENOMEM;
			break;
		}
		error = copyin(io->pfrio_buffer, pfras, totlen);
		if (error) {
			free(pfras, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		error = pfr_clr_astats(&io->pfrio_table, pfras,
		    io->pfrio_size, &io->pfrio_nzero, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		if (error == 0 && io->pfrio_flags & PFR_FLAG_FEEDBACK)
			error = copyout(pfras, io->pfrio_buffer, totlen);
		free(pfras, M_TEMP);
		break;
	}

	case DIOCRTSTADDRS: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_addr *pfras;
		size_t totlen;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		if (io->pfrio_size < 0 ||
		    io->pfrio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfrio_size, sizeof(struct pfr_addr))) {
			error = EINVAL;
			break;
		}
		totlen = io->pfrio_size * sizeof(struct pfr_addr);
		pfras = mallocarray(io->pfrio_size, sizeof(struct pfr_addr),
		    M_TEMP, M_NOWAIT);
		if (! pfras) {
			error = ENOMEM;
			break;
		}
		error = copyin(io->pfrio_buffer, pfras, totlen);
		if (error) {
			free(pfras, M_TEMP);
			break;
		}
		PF_RULES_RLOCK();
		error = pfr_tst_addrs(&io->pfrio_table, pfras,
		    io->pfrio_size, &io->pfrio_nmatch, io->pfrio_flags |
		    PFR_FLAG_USERIOCTL);
		PF_RULES_RUNLOCK();
		if (error == 0)
			error = copyout(pfras, io->pfrio_buffer, totlen);
		free(pfras, M_TEMP);
		break;
	}

	case DIOCRINADEFINE: {
		struct pfioc_table *io = (struct pfioc_table *)addr;
		struct pfr_addr *pfras;
		size_t totlen;

		if (io->pfrio_esize != sizeof(struct pfr_addr)) {
			error = ENODEV;
			break;
		}
		if (io->pfrio_size < 0 ||
		    io->pfrio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfrio_size, sizeof(struct pfr_addr))) {
			error = EINVAL;
			break;
		}
		totlen = io->pfrio_size * sizeof(struct pfr_addr);
		pfras = mallocarray(io->pfrio_size, sizeof(struct pfr_addr),
		    M_TEMP, M_NOWAIT);
		if (! pfras) {
			error = ENOMEM;
			break;
		}
		error = copyin(io->pfrio_buffer, pfras, totlen);
		if (error) {
			free(pfras, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		error = pfr_ina_define(&io->pfrio_table, pfras,
		    io->pfrio_size, &io->pfrio_nadd, &io->pfrio_naddr,
		    io->pfrio_ticket, io->pfrio_flags | PFR_FLAG_USERIOCTL);
		PF_RULES_WUNLOCK();
		free(pfras, M_TEMP);
		break;
	}

	case DIOCOSFPADD: {
		struct pf_osfp_ioctl *io = (struct pf_osfp_ioctl *)addr;
		PF_RULES_WLOCK();
		error = pf_osfp_add(io);
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCOSFPGET: {
		struct pf_osfp_ioctl *io = (struct pf_osfp_ioctl *)addr;
		PF_RULES_RLOCK();
		error = pf_osfp_get(io);
		PF_RULES_RUNLOCK();
		break;
	}

	case DIOCXBEGIN: {
		struct pfioc_trans	*io = (struct pfioc_trans *)addr;
		struct pfioc_trans_e	*ioes, *ioe;
		size_t			 totlen;
		int			 i;

		if (io->esize != sizeof(*ioe)) {
			error = ENODEV;
			break;
		}
		if (io->size < 0 ||
		    io->size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->size, sizeof(struct pfioc_trans_e))) {
			error = EINVAL;
			break;
		}
		totlen = sizeof(struct pfioc_trans_e) * io->size;
		ioes = mallocarray(io->size, sizeof(struct pfioc_trans_e),
		    M_TEMP, M_NOWAIT);
		if (! ioes) {
			error = ENOMEM;
			break;
		}
		error = copyin(io->array, ioes, totlen);
		if (error) {
			free(ioes, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		for (i = 0, ioe = ioes; i < io->size; i++, ioe++) {
			switch (ioe->rs_num) {
#ifdef ALTQ
			case PF_RULESET_ALTQ:
				if (ioe->anchor[0]) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					error = EINVAL;
					goto fail;
				}
				if ((error = pf_begin_altq(&ioe->ticket))) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					goto fail;
				}
				break;
#endif /* ALTQ */
			case PF_RULESET_TABLE:
			    {
				struct pfr_table table;

				bzero(&table, sizeof(table));
				strlcpy(table.pfrt_anchor, ioe->anchor,
				    sizeof(table.pfrt_anchor));
				if ((error = pfr_ina_begin(&table,
				    &ioe->ticket, NULL, 0))) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					goto fail;
				}
				break;
			    }
			default:
				if ((error = pf_begin_rules(&ioe->ticket,
				    ioe->rs_num, ioe->anchor))) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					goto fail;
				}
				break;
			}
		}
		PF_RULES_WUNLOCK();
		error = copyout(ioes, io->array, totlen);
		free(ioes, M_TEMP);
		break;
	}

	case DIOCXROLLBACK: {
		struct pfioc_trans	*io = (struct pfioc_trans *)addr;
		struct pfioc_trans_e	*ioe, *ioes;
		size_t			 totlen;
		int			 i;

		if (io->esize != sizeof(*ioe)) {
			error = ENODEV;
			break;
		}
		if (io->size < 0 ||
		    io->size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->size, sizeof(struct pfioc_trans_e))) {
			error = EINVAL;
			break;
		}
		totlen = sizeof(struct pfioc_trans_e) * io->size;
		ioes = mallocarray(io->size, sizeof(struct pfioc_trans_e),
		    M_TEMP, M_NOWAIT);
		if (! ioes) {
			error = ENOMEM;
			break;
		}
		error = copyin(io->array, ioes, totlen);
		if (error) {
			free(ioes, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		for (i = 0, ioe = ioes; i < io->size; i++, ioe++) {
			switch (ioe->rs_num) {
#ifdef ALTQ
			case PF_RULESET_ALTQ:
				if (ioe->anchor[0]) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					error = EINVAL;
					goto fail;
				}
				if ((error = pf_rollback_altq(ioe->ticket))) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					goto fail; /* really bad */
				}
				break;
#endif /* ALTQ */
			case PF_RULESET_TABLE:
			    {
				struct pfr_table table;

				bzero(&table, sizeof(table));
				strlcpy(table.pfrt_anchor, ioe->anchor,
				    sizeof(table.pfrt_anchor));
				if ((error = pfr_ina_rollback(&table,
				    ioe->ticket, NULL, 0))) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					goto fail; /* really bad */
				}
				break;
			    }
			default:
				if ((error = pf_rollback_rules(ioe->ticket,
				    ioe->rs_num, ioe->anchor))) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					goto fail; /* really bad */
				}
				break;
			}
		}
		PF_RULES_WUNLOCK();
		free(ioes, M_TEMP);
		break;
	}

	case DIOCXCOMMIT: {
		struct pfioc_trans	*io = (struct pfioc_trans *)addr;
		struct pfioc_trans_e	*ioe, *ioes;
		struct pf_ruleset	*rs;
		size_t			 totlen;
		int			 i;

		if (io->esize != sizeof(*ioe)) {
			error = ENODEV;
			break;
		}

		if (io->size < 0 ||
		    io->size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->size, sizeof(struct pfioc_trans_e))) {
			error = EINVAL;
			break;
		}

		totlen = sizeof(struct pfioc_trans_e) * io->size;
		ioes = mallocarray(io->size, sizeof(struct pfioc_trans_e),
		    M_TEMP, M_NOWAIT);
		if (ioes == NULL) {
			error = ENOMEM;
			break;
		}
		error = copyin(io->array, ioes, totlen);
		if (error) {
			free(ioes, M_TEMP);
			break;
		}
		PF_RULES_WLOCK();
		/* First makes sure everything will succeed. */
		for (i = 0, ioe = ioes; i < io->size; i++, ioe++) {
			switch (ioe->rs_num) {
#ifdef ALTQ
			case PF_RULESET_ALTQ:
				if (ioe->anchor[0]) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					error = EINVAL;
					goto fail;
				}
				if (!V_altqs_inactive_open || ioe->ticket !=
				    V_ticket_altqs_inactive) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					error = EBUSY;
					goto fail;
				}
				break;
#endif /* ALTQ */
			case PF_RULESET_TABLE:
				rs = pf_find_ruleset(ioe->anchor);
				if (rs == NULL || !rs->topen || ioe->ticket !=
				    rs->tticket) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					error = EBUSY;
					goto fail;
				}
				break;
			default:
				if (ioe->rs_num < 0 || ioe->rs_num >=
				    PF_RULESET_MAX) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					error = EINVAL;
					goto fail;
				}
				rs = pf_find_ruleset(ioe->anchor);
				if (rs == NULL ||
				    !rs->rules[ioe->rs_num].inactive.open ||
				    rs->rules[ioe->rs_num].inactive.ticket !=
				    ioe->ticket) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					error = EBUSY;
					goto fail;
				}
				break;
			}
		}
		/* Now do the commit - no errors should happen here. */
		for (i = 0, ioe = ioes; i < io->size; i++, ioe++) {
			switch (ioe->rs_num) {
#ifdef ALTQ
			case PF_RULESET_ALTQ:
				if ((error = pf_commit_altq(ioe->ticket))) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					goto fail; /* really bad */
				}
				break;
#endif /* ALTQ */
			case PF_RULESET_TABLE:
			    {
				struct pfr_table table;

				bzero(&table, sizeof(table));
				strlcpy(table.pfrt_anchor, ioe->anchor,
				    sizeof(table.pfrt_anchor));
				if ((error = pfr_ina_commit(&table,
				    ioe->ticket, NULL, NULL, 0))) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					goto fail; /* really bad */
				}
				break;
			    }
			default:
				if ((error = pf_commit_rules(ioe->ticket,
				    ioe->rs_num, ioe->anchor))) {
					PF_RULES_WUNLOCK();
					free(ioes, M_TEMP);
					goto fail; /* really bad */
				}
				break;
			}
		}
		PF_RULES_WUNLOCK();
		free(ioes, M_TEMP);
		break;
	}

	case DIOCGETSRCNODES: {
		struct pfioc_src_nodes	*psn = (struct pfioc_src_nodes *)addr;
		struct pf_srchash	*sh;
		struct pf_src_node	*n, *p, *pstore;
		uint32_t		 i, nr = 0;

		for (i = 0, sh = V_pf_srchash; i <= pf_srchashmask;
				i++, sh++) {
			PF_HASHROW_LOCK(sh);
			LIST_FOREACH(n, &sh->nodes, entry)
				nr++;
			PF_HASHROW_UNLOCK(sh);
		}

		psn->psn_len = min(psn->psn_len,
		    sizeof(struct pf_src_node) * nr);

		if (psn->psn_len == 0) {
			psn->psn_len = sizeof(struct pf_src_node) * nr;
			break;
		}

		nr = 0;

		p = pstore = malloc(psn->psn_len, M_TEMP, M_WAITOK);
		for (i = 0, sh = V_pf_srchash; i <= pf_srchashmask;
		    i++, sh++) {
		    PF_HASHROW_LOCK(sh);
		    LIST_FOREACH(n, &sh->nodes, entry) {
			int	secs = time_uptime, diff;

			if ((nr + 1) * sizeof(*p) > (unsigned)psn->psn_len)
				break;

			bcopy(n, p, sizeof(struct pf_src_node));
			if (n->rule.ptr != NULL)
				p->rule.nr = n->rule.ptr->nr;
			p->creation = secs - p->creation;
			if (p->expire > secs)
				p->expire -= secs;
			else
				p->expire = 0;

			/* Adjust the connection rate estimate. */
			diff = secs - n->conn_rate.last;
			if (diff >= n->conn_rate.seconds)
				p->conn_rate.count = 0;
			else
				p->conn_rate.count -=
				    n->conn_rate.count * diff /
				    n->conn_rate.seconds;
			p++;
			nr++;
		    }
		    PF_HASHROW_UNLOCK(sh);
		}
		error = copyout(pstore, psn->psn_src_nodes,
		    sizeof(struct pf_src_node) * nr);
		if (error) {
			free(pstore, M_TEMP);
			break;
		}
		psn->psn_len = sizeof(struct pf_src_node) * nr;
		free(pstore, M_TEMP);
		break;
	}

	case DIOCCLRSRCNODES: {

		pf_clear_srcnodes(NULL);
		pf_purge_expired_src_nodes();
		break;
	}

	case DIOCKILLSRCNODES:
		pf_kill_srcnodes((struct pfioc_src_node_kill *)addr);
		break;

	case DIOCSETHOSTID: {
		u_int32_t	*hostid = (u_int32_t *)addr;

		PF_RULES_WLOCK();
		if (*hostid == 0)
			V_pf_status.hostid = arc4random();
		else
			V_pf_status.hostid = *hostid;
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCOSFPFLUSH:
		PF_RULES_WLOCK();
		pf_osfp_flush();
		PF_RULES_WUNLOCK();
		break;

	case DIOCIGETIFACES: {
		struct pfioc_iface *io = (struct pfioc_iface *)addr;
		struct pfi_kif *ifstore;
		size_t bufsiz;

		if (io->pfiio_esize != sizeof(struct pfi_kif)) {
			error = ENODEV;
			break;
		}

		if (io->pfiio_size < 0 ||
		    io->pfiio_size > pf_ioctl_maxcount ||
		    WOULD_OVERFLOW(io->pfiio_size, sizeof(struct pfi_kif))) {
			error = EINVAL;
			break;
		}

		bufsiz = io->pfiio_size * sizeof(struct pfi_kif);
		ifstore = mallocarray(io->pfiio_size, sizeof(struct pfi_kif),
		    M_TEMP, M_NOWAIT);
		if (ifstore == NULL) {
			error = ENOMEM;
			break;
		}

		PF_RULES_RLOCK();
		pfi_get_ifaces(io->pfiio_name, ifstore, &io->pfiio_size);
		PF_RULES_RUNLOCK();
		error = copyout(ifstore, io->pfiio_buffer, bufsiz);
		free(ifstore, M_TEMP);
		break;
	}

	case DIOCSETIFFLAG: {
		struct pfioc_iface *io = (struct pfioc_iface *)addr;

		PF_RULES_WLOCK();
		error = pfi_set_flags(io->pfiio_name, io->pfiio_flags);
		PF_RULES_WUNLOCK();
		break;
	}

	case DIOCCLRIFFLAG: {
		struct pfioc_iface *io = (struct pfioc_iface *)addr;

		PF_RULES_WLOCK();
		error = pfi_clear_flags(io->pfiio_name, io->pfiio_flags);
		PF_RULES_WUNLOCK();
		break;
	}

	default:
		error = ENODEV;
		break;
	}
fail:
	if (sx_xlocked(&pf_ioctl_lock))
		sx_xunlock(&pf_ioctl_lock);
	CURVNET_RESTORE();

	return (error);
}

void
pfsync_state_export(struct pfsync_state *sp, struct pf_state *st)
{
	bzero(sp, sizeof(struct pfsync_state));

	/* copy from state key */
	sp->key[PF_SK_WIRE].addr[0] = st->key[PF_SK_WIRE]->addr[0];
	sp->key[PF_SK_WIRE].addr[1] = st->key[PF_SK_WIRE]->addr[1];
	sp->key[PF_SK_WIRE].port[0] = st->key[PF_SK_WIRE]->port[0];
	sp->key[PF_SK_WIRE].port[1] = st->key[PF_SK_WIRE]->port[1];
	sp->key[PF_SK_STACK].addr[0] = st->key[PF_SK_STACK]->addr[0];
	sp->key[PF_SK_STACK].addr[1] = st->key[PF_SK_STACK]->addr[1];
	sp->key[PF_SK_STACK].port[0] = st->key[PF_SK_STACK]->port[0];
	sp->key[PF_SK_STACK].port[1] = st->key[PF_SK_STACK]->port[1];
	sp->proto = st->key[PF_SK_WIRE]->proto;
	sp->af = st->key[PF_SK_WIRE]->af;

	/* copy from state */
	strlcpy(sp->ifname, st->kif->pfik_name, sizeof(sp->ifname));
	bcopy(&st->rt_addr, &sp->rt_addr, sizeof(sp->rt_addr));
	sp->creation = htonl(time_uptime - st->creation);
	sp->expire = pf_state_expires(st);
	if (sp->expire <= time_uptime)
		sp->expire = htonl(0);
	else
		sp->expire = htonl(sp->expire - time_uptime);

	sp->direction = st->direction;
	sp->log = st->log;
	sp->timeout = st->timeout;
	sp->state_flags = st->state_flags;
	if (st->src_node)
		sp->sync_flags |= PFSYNC_FLAG_SRCNODE;
	if (st->nat_src_node)
		sp->sync_flags |= PFSYNC_FLAG_NATSRCNODE;

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
	if (st->nat_rule.ptr == NULL)
		sp->nat_rule = htonl(-1);
	else
		sp->nat_rule = htonl(st->nat_rule.ptr->nr);

	pf_state_counter_hton(st->packets[0], sp->packets[0]);
	pf_state_counter_hton(st->packets[1], sp->packets[1]);
	pf_state_counter_hton(st->bytes[0], sp->bytes[0]);
	pf_state_counter_hton(st->bytes[1], sp->bytes[1]);

}

static void
pf_tbladdr_copyout(struct pf_addr_wrap *aw)
{
	struct pfr_ktable *kt;

	KASSERT(aw->type == PF_ADDR_TABLE, ("%s: type %u", __func__, aw->type));

	kt = aw->p.tbl;
	if (!(kt->pfrkt_flags & PFR_TFLAG_ACTIVE) && kt->pfrkt_root != NULL)
		kt = kt->pfrkt_root;
	aw->p.tbl = NULL;
	aw->p.tblcnt = (kt->pfrkt_flags & PFR_TFLAG_ACTIVE) ?
		kt->pfrkt_cnt : -1;
}

/*
 * XXX - Check for version missmatch!!!
 */
static void
pf_clear_states(void)
{
	struct pf_state	*s;
	u_int i;

	for (i = 0; i <= pf_hashmask; i++) {
		struct pf_idhash *ih = &V_pf_idhash[i];
relock:
		PF_HASHROW_LOCK(ih);
		LIST_FOREACH(s, &ih->states, entry) {
			s->timeout = PFTM_PURGE;
			/* Don't send out individual delete messages. */
			s->state_flags |= PFSTATE_NOSYNC;
			pf_unlink_state(s, PF_ENTER_LOCKED);
			goto relock;
		}
		PF_HASHROW_UNLOCK(ih);
	}
}

static int
pf_clear_tables(void)
{
	struct pfioc_table io;
	int error;

	bzero(&io, sizeof(io));

	error = pfr_clr_tables(&io.pfrio_table, &io.pfrio_ndel,
	    io.pfrio_flags);

	return (error);
}

static void
pf_clear_srcnodes(struct pf_src_node *n)
{
	struct pf_state *s;
	int i;

	for (i = 0; i <= pf_hashmask; i++) {
		struct pf_idhash *ih = &V_pf_idhash[i];

		PF_HASHROW_LOCK(ih);
		LIST_FOREACH(s, &ih->states, entry) {
			if (n == NULL || n == s->src_node)
				s->src_node = NULL;
			if (n == NULL || n == s->nat_src_node)
				s->nat_src_node = NULL;
		}
		PF_HASHROW_UNLOCK(ih);
	}

	if (n == NULL) {
		struct pf_srchash *sh;

		for (i = 0, sh = V_pf_srchash; i <= pf_srchashmask;
		    i++, sh++) {
			PF_HASHROW_LOCK(sh);
			LIST_FOREACH(n, &sh->nodes, entry) {
				n->expire = 1;
				n->states = 0;
			}
			PF_HASHROW_UNLOCK(sh);
		}
	} else {
		/* XXX: hash slot should already be locked here. */
		n->expire = 1;
		n->states = 0;
	}
}

static void
pf_kill_srcnodes(struct pfioc_src_node_kill *psnk)
{
	struct pf_src_node_list	 kill;

	LIST_INIT(&kill);
	for (int i = 0; i <= pf_srchashmask; i++) {
		struct pf_srchash *sh = &V_pf_srchash[i];
		struct pf_src_node *sn, *tmp;

		PF_HASHROW_LOCK(sh);
		LIST_FOREACH_SAFE(sn, &sh->nodes, entry, tmp)
			if (PF_MATCHA(psnk->psnk_src.neg,
			      &psnk->psnk_src.addr.v.a.addr,
			      &psnk->psnk_src.addr.v.a.mask,
			      &sn->addr, sn->af) &&
			    PF_MATCHA(psnk->psnk_dst.neg,
			      &psnk->psnk_dst.addr.v.a.addr,
			      &psnk->psnk_dst.addr.v.a.mask,
			      &sn->raddr, sn->af)) {
				pf_unlink_src_node(sn);
				LIST_INSERT_HEAD(&kill, sn, entry);
				sn->expire = 1;
			}
		PF_HASHROW_UNLOCK(sh);
	}

	for (int i = 0; i <= pf_hashmask; i++) {
		struct pf_idhash *ih = &V_pf_idhash[i];
		struct pf_state *s;

		PF_HASHROW_LOCK(ih);
		LIST_FOREACH(s, &ih->states, entry) {
			if (s->src_node && s->src_node->expire == 1)
				s->src_node = NULL;
			if (s->nat_src_node && s->nat_src_node->expire == 1)
				s->nat_src_node = NULL;
		}
		PF_HASHROW_UNLOCK(ih);
	}

	psnk->psnk_killed = pf_free_src_nodes(&kill);
}

/*
 * XXX - Check for version missmatch!!!
 */

/*
 * Duplicate pfctl -Fa operation to get rid of as much as we can.
 */
static int
shutdown_pf(void)
{
	int error = 0;
	u_int32_t t[5];
	char nn = '\0';

	do {
		if ((error = pf_begin_rules(&t[0], PF_RULESET_SCRUB, &nn))
		    != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: SCRUB\n"));
			break;
		}
		if ((error = pf_begin_rules(&t[1], PF_RULESET_FILTER, &nn))
		    != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: FILTER\n"));
			break;		/* XXX: rollback? */
		}
		if ((error = pf_begin_rules(&t[2], PF_RULESET_NAT, &nn))
		    != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: NAT\n"));
			break;		/* XXX: rollback? */
		}
		if ((error = pf_begin_rules(&t[3], PF_RULESET_BINAT, &nn))
		    != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: BINAT\n"));
			break;		/* XXX: rollback? */
		}
		if ((error = pf_begin_rules(&t[4], PF_RULESET_RDR, &nn))
		    != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: RDR\n"));
			break;		/* XXX: rollback? */
		}

		/* XXX: these should always succeed here */
		pf_commit_rules(t[0], PF_RULESET_SCRUB, &nn);
		pf_commit_rules(t[1], PF_RULESET_FILTER, &nn);
		pf_commit_rules(t[2], PF_RULESET_NAT, &nn);
		pf_commit_rules(t[3], PF_RULESET_BINAT, &nn);
		pf_commit_rules(t[4], PF_RULESET_RDR, &nn);

		if ((error = pf_clear_tables()) != 0)
			break;

#ifdef ALTQ
		if ((error = pf_begin_altq(&t[0])) != 0) {
			DPFPRINTF(PF_DEBUG_MISC, ("shutdown_pf: ALTQ\n"));
			break;
		}
		pf_commit_altq(t[0]);
#endif

		pf_clear_states();

		pf_clear_srcnodes(NULL);

		/* status does not use malloced mem so no need to cleanup */
		/* fingerprints and interfaces have their own cleanup code */
	} while(0);

	return (error);
}

static pfil_return_t
pf_check_return(int chk, struct mbuf **m)
{

	switch (chk) {
	case PF_PASS:
		if (*m == NULL)
			return (PFIL_CONSUMED);
		else
			return (PFIL_PASS);
		break;
	default:
		if (*m != NULL) {
			m_freem(*m);
			*m = NULL;
		}
		return (PFIL_DROPPED);
	}
}

#ifdef INET
static pfil_return_t
pf_check_in(struct mbuf **m, struct ifnet *ifp, int flags,
    void *ruleset __unused, struct inpcb *inp)
{
	int chk;

	chk = pf_test(PF_IN, flags, ifp, m, inp);

	return (pf_check_return(chk, m));
}

static pfil_return_t
pf_check_out(struct mbuf **m, struct ifnet *ifp, int flags,
    void *ruleset __unused,  struct inpcb *inp)
{
	int chk;

	chk = pf_test(PF_OUT, flags, ifp, m, inp);

	return (pf_check_return(chk, m));
}
#endif

#ifdef INET6
static pfil_return_t
pf_check6_in(struct mbuf **m, struct ifnet *ifp, int flags,
    void *ruleset __unused,  struct inpcb *inp)
{
	int chk;

	/*
	 * In case of loopback traffic IPv6 uses the real interface in
	 * order to support scoped addresses. In order to support stateful
	 * filtering we have change this to lo0 as it is the case in IPv4.
	 */
	CURVNET_SET(ifp->if_vnet);
	chk = pf_test6(PF_IN, flags, (*m)->m_flags & M_LOOP ? V_loif : ifp, m, inp);
	CURVNET_RESTORE();

	return (pf_check_return(chk, m));
}

static pfil_return_t
pf_check6_out(struct mbuf **m, struct ifnet *ifp, int flags,
    void *ruleset __unused,  struct inpcb *inp)
{
	int chk;

	CURVNET_SET(ifp->if_vnet);
	chk = pf_test6(PF_OUT, flags, ifp, m, inp);
	CURVNET_RESTORE();

	return (pf_check_return(chk, m));
}
#endif /* INET6 */

#ifdef INET
VNET_DEFINE_STATIC(pfil_hook_t, pf_ip4_in_hook);
VNET_DEFINE_STATIC(pfil_hook_t, pf_ip4_out_hook);
#define	V_pf_ip4_in_hook	VNET(pf_ip4_in_hook)
#define	V_pf_ip4_out_hook	VNET(pf_ip4_out_hook)
#endif
#ifdef INET6
VNET_DEFINE_STATIC(pfil_hook_t, pf_ip6_in_hook);
VNET_DEFINE_STATIC(pfil_hook_t, pf_ip6_out_hook);
#define	V_pf_ip6_in_hook	VNET(pf_ip6_in_hook)
#define	V_pf_ip6_out_hook	VNET(pf_ip6_out_hook)
#endif

static int
hook_pf(void)
{
	struct pfil_hook_args pha;
	struct pfil_link_args pla;

	if (V_pf_pfil_hooked)
		return (0);

	pha.pa_version = PFIL_VERSION;
	pha.pa_modname = "pf";
	pha.pa_ruleset = NULL;

	pla.pa_version = PFIL_VERSION;

#ifdef INET
	pha.pa_type = PFIL_TYPE_IP4;
	pha.pa_func = pf_check_in;
	pha.pa_flags = PFIL_IN;
	pha.pa_rulname = "default-in";
	V_pf_ip4_in_hook = pfil_add_hook(&pha);
	pla.pa_flags = PFIL_IN | PFIL_HEADPTR | PFIL_HOOKPTR;
	pla.pa_head = V_inet_pfil_head;
	pla.pa_hook = V_pf_ip4_in_hook;
	(void)pfil_link(&pla);
	pha.pa_func = pf_check_out;
	pha.pa_flags = PFIL_OUT;
	pha.pa_rulname = "default-out";
	V_pf_ip4_out_hook = pfil_add_hook(&pha);
	pla.pa_flags = PFIL_OUT | PFIL_HEADPTR | PFIL_HOOKPTR;
	pla.pa_head = V_inet_pfil_head;
	pla.pa_hook = V_pf_ip4_out_hook;
	(void)pfil_link(&pla);
#endif
#ifdef INET6
	pha.pa_type = PFIL_TYPE_IP6;
	pha.pa_func = pf_check6_in;
	pha.pa_flags = PFIL_IN;
	pha.pa_rulname = "default-in6";
	V_pf_ip6_in_hook = pfil_add_hook(&pha);
	pla.pa_flags = PFIL_IN | PFIL_HEADPTR | PFIL_HOOKPTR;
	pla.pa_head = V_inet6_pfil_head;
	pla.pa_hook = V_pf_ip6_in_hook;
	(void)pfil_link(&pla);
	pha.pa_func = pf_check6_out;
	pha.pa_rulname = "default-out6";
	pha.pa_flags = PFIL_OUT;
	V_pf_ip6_out_hook = pfil_add_hook(&pha);
	pla.pa_flags = PFIL_OUT | PFIL_HEADPTR | PFIL_HOOKPTR;
	pla.pa_head = V_inet6_pfil_head;
	pla.pa_hook = V_pf_ip6_out_hook;
	(void)pfil_link(&pla);
#endif

	V_pf_pfil_hooked = 1;
	return (0);
}

static int
dehook_pf(void)
{

	if (V_pf_pfil_hooked == 0)
		return (0);

#ifdef INET
	pfil_remove_hook(V_pf_ip4_in_hook);
	pfil_remove_hook(V_pf_ip4_out_hook);
#endif
#ifdef INET6
	pfil_remove_hook(V_pf_ip6_in_hook);
	pfil_remove_hook(V_pf_ip6_out_hook);
#endif

	V_pf_pfil_hooked = 0;
	return (0);
}

static void
pf_load_vnet(void)
{
	V_pf_tag_z = uma_zcreate("pf tags", sizeof(struct pf_tagname),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);

	pf_init_tagset(&V_pf_tags, &pf_rule_tag_hashsize,
	    PF_RULE_TAG_HASH_SIZE_DEFAULT);
#ifdef ALTQ
	pf_init_tagset(&V_pf_qids, &pf_queue_tag_hashsize,
	    PF_QUEUE_TAG_HASH_SIZE_DEFAULT);
#endif

	pfattach_vnet();
	V_pf_vnet_active = 1;
}

static int
pf_load(void)
{
	int error;

	rm_init(&pf_rules_lock, "pf rulesets");
	sx_init(&pf_ioctl_lock, "pf ioctl");
	sx_init(&pf_end_lock, "pf end thread");

	pf_mtag_initialize();

	pf_dev = make_dev(&pf_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, PF_NAME);
	if (pf_dev == NULL)
		return (ENOMEM);

	pf_end_threads = 0;
	error = kproc_create(pf_purge_thread, NULL, &pf_purge_proc, 0, 0, "pf purge");
	if (error != 0)
		return (error);

	pfi_initialize();

	return (0);
}

static void
pf_unload_vnet(void)
{
	int error;

	V_pf_vnet_active = 0;
	V_pf_status.running = 0;
	error = dehook_pf();
	if (error) {
		/*
		 * Should not happen!
		 * XXX Due to error code ESRCH, kldunload will show
		 * a message like 'No such process'.
		 */
		printf("%s : pfil unregisteration fail\n", __FUNCTION__);
		return;
	}

	PF_RULES_WLOCK();
	shutdown_pf();
	PF_RULES_WUNLOCK();

	swi_remove(V_pf_swi_cookie);

	pf_unload_vnet_purge();

	pf_normalize_cleanup();
	PF_RULES_WLOCK();
	pfi_cleanup_vnet();
	PF_RULES_WUNLOCK();
	pfr_cleanup();
	pf_osfp_flush();
	pf_cleanup();
	if (IS_DEFAULT_VNET(curvnet))
		pf_mtag_cleanup();

	pf_cleanup_tagset(&V_pf_tags);
#ifdef ALTQ
	pf_cleanup_tagset(&V_pf_qids);
#endif
	uma_zdestroy(V_pf_tag_z);

	/* Free counters last as we updated them during shutdown. */
	counter_u64_free(V_pf_default_rule.states_cur);
	counter_u64_free(V_pf_default_rule.states_tot);
	counter_u64_free(V_pf_default_rule.src_nodes);

	for (int i = 0; i < PFRES_MAX; i++)
		counter_u64_free(V_pf_status.counters[i]);
	for (int i = 0; i < LCNT_MAX; i++)
		counter_u64_free(V_pf_status.lcounters[i]);
	for (int i = 0; i < FCNT_MAX; i++)
		counter_u64_free(V_pf_status.fcounters[i]);
	for (int i = 0; i < SCNT_MAX; i++)
		counter_u64_free(V_pf_status.scounters[i]);
}

static void
pf_unload(void)
{

	sx_xlock(&pf_end_lock);
	pf_end_threads = 1;
	while (pf_end_threads < 2) {
		wakeup_one(pf_purge_thread);
		sx_sleep(pf_purge_proc, &pf_end_lock, 0, "pftmo", 0);
	}
	sx_xunlock(&pf_end_lock);

	if (pf_dev != NULL)
		destroy_dev(pf_dev);

	pfi_cleanup();

	rm_destroy(&pf_rules_lock);
	sx_destroy(&pf_ioctl_lock);
	sx_destroy(&pf_end_lock);
}

static void
vnet_pf_init(void *unused __unused)
{

	pf_load_vnet();
}
VNET_SYSINIT(vnet_pf_init, SI_SUB_PROTO_FIREWALL, SI_ORDER_THIRD, 
    vnet_pf_init, NULL);

static void
vnet_pf_uninit(const void *unused __unused)
{

	pf_unload_vnet();
} 
SYSUNINIT(pf_unload, SI_SUB_PROTO_FIREWALL, SI_ORDER_SECOND, pf_unload, NULL);
VNET_SYSUNINIT(vnet_pf_uninit, SI_SUB_PROTO_FIREWALL, SI_ORDER_THIRD,
    vnet_pf_uninit, NULL);


static int
pf_modevent(module_t mod, int type, void *data)
{
	int error = 0;

	switch(type) {
	case MOD_LOAD:
		error = pf_load();
		break;
	case MOD_UNLOAD:
		/* Handled in SYSUNINIT(pf_unload) to ensure it's done after
		 * the vnet_pf_uninit()s */
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

static moduledata_t pf_mod = {
	"pf",
	pf_modevent,
	0
};

DECLARE_MODULE(pf, pf_mod, SI_SUB_PROTO_FIREWALL, SI_ORDER_SECOND);
MODULE_VERSION(pf, PF_MODVER);
