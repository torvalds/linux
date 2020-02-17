/*
 *  ebtables
 *
 *  Author:
 *  Bart De Schuymer		<bdschuym@pandora.be>
 *
 *  ebtables.c,v 2.0, July, 2002
 *
 *  This code is strongly inspired by the iptables code which is
 *  Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/audit.h>
#include <net/sock.h>
/* needed for logical [in,out]-dev filtering */
#include "../br_private.h"

/* Each cpu has its own set of counters, so there is no need for write_lock in
 * the softirq
 * For reading or updating the counters, the user context needs to
 * get a write_lock
 */

/* The size of each set of counters is altered to get cache alignment */
#define SMP_ALIGN(x) (((x) + SMP_CACHE_BYTES-1) & ~(SMP_CACHE_BYTES-1))
#define COUNTER_OFFSET(n) (SMP_ALIGN(n * sizeof(struct ebt_counter)))
#define COUNTER_BASE(c, n, cpu) ((struct ebt_counter *)(((char *)c) + \
				 COUNTER_OFFSET(n) * cpu))



static DEFINE_MUTEX(ebt_mutex);

#ifdef CONFIG_COMPAT
static void ebt_standard_compat_from_user(void *dst, const void *src)
{
	int v = *(compat_int_t *)src;

	if (v >= 0)
		v += xt_compat_calc_jump(NFPROTO_BRIDGE, v);
	memcpy(dst, &v, sizeof(v));
}

static int ebt_standard_compat_to_user(void __user *dst, const void *src)
{
	compat_int_t cv = *(int *)src;

	if (cv >= 0)
		cv -= xt_compat_calc_jump(NFPROTO_BRIDGE, cv);
	return copy_to_user(dst, &cv, sizeof(cv)) ? -EFAULT : 0;
}
#endif


static struct xt_target ebt_standard_target = {
	.name       = "standard",
	.revision   = 0,
	.family     = NFPROTO_BRIDGE,
	.targetsize = sizeof(int),
#ifdef CONFIG_COMPAT
	.compatsize = sizeof(compat_int_t),
	.compat_from_user = ebt_standard_compat_from_user,
	.compat_to_user =  ebt_standard_compat_to_user,
#endif
};

static inline int
ebt_do_watcher(const struct ebt_entry_watcher *w, struct sk_buff *skb,
	       struct xt_action_param *par)
{
	par->target   = w->u.watcher;
	par->targinfo = w->data;
	w->u.watcher->target(skb, par);
	/* watchers don't give a verdict */
	return 0;
}

static inline int
ebt_do_match(struct ebt_entry_match *m, const struct sk_buff *skb,
	     struct xt_action_param *par)
{
	par->match     = m->u.match;
	par->matchinfo = m->data;
	return !m->u.match->match(skb, par);
}

static inline int
ebt_dev_check(const char *entry, const struct net_device *device)
{
	int i = 0;
	const char *devname;

	if (*entry == '\0')
		return 0;
	if (!device)
		return 1;
	devname = device->name;
	/* 1 is the wildcard token */
	while (entry[i] != '\0' && entry[i] != 1 && entry[i] == devname[i])
		i++;
	return devname[i] != entry[i] && entry[i] != 1;
}

/* process standard matches */
static inline int
ebt_basic_match(const struct ebt_entry *e, const struct sk_buff *skb,
		const struct net_device *in, const struct net_device *out)
{
	const struct ethhdr *h = eth_hdr(skb);
	const struct net_bridge_port *p;
	__be16 ethproto;

	if (skb_vlan_tag_present(skb))
		ethproto = htons(ETH_P_8021Q);
	else
		ethproto = h->h_proto;

	if (e->bitmask & EBT_802_3) {
		if (NF_INVF(e, EBT_IPROTO, eth_proto_is_802_3(ethproto)))
			return 1;
	} else if (!(e->bitmask & EBT_NOPROTO) &&
		   NF_INVF(e, EBT_IPROTO, e->ethproto != ethproto))
		return 1;

	if (NF_INVF(e, EBT_IIN, ebt_dev_check(e->in, in)))
		return 1;
	if (NF_INVF(e, EBT_IOUT, ebt_dev_check(e->out, out)))
		return 1;
	/* rcu_read_lock()ed by nf_hook_thresh */
	if (in && (p = br_port_get_rcu(in)) != NULL &&
	    NF_INVF(e, EBT_ILOGICALIN,
		    ebt_dev_check(e->logical_in, p->br->dev)))
		return 1;
	if (out && (p = br_port_get_rcu(out)) != NULL &&
	    NF_INVF(e, EBT_ILOGICALOUT,
		    ebt_dev_check(e->logical_out, p->br->dev)))
		return 1;

	if (e->bitmask & EBT_SOURCEMAC) {
		if (NF_INVF(e, EBT_ISOURCE,
			    !ether_addr_equal_masked(h->h_source, e->sourcemac,
						     e->sourcemsk)))
			return 1;
	}
	if (e->bitmask & EBT_DESTMAC) {
		if (NF_INVF(e, EBT_IDEST,
			    !ether_addr_equal_masked(h->h_dest, e->destmac,
						     e->destmsk)))
			return 1;
	}
	return 0;
}

static inline
struct ebt_entry *ebt_next_entry(const struct ebt_entry *entry)
{
	return (void *)entry + entry->next_offset;
}

static inline const struct ebt_entry_target *
ebt_get_target_c(const struct ebt_entry *e)
{
	return ebt_get_target((struct ebt_entry *)e);
}

/* Do some firewalling */
unsigned int ebt_do_table(struct sk_buff *skb,
			  const struct nf_hook_state *state,
			  struct ebt_table *table)
{
	unsigned int hook = state->hook;
	int i, nentries;
	struct ebt_entry *point;
	struct ebt_counter *counter_base, *cb_base;
	const struct ebt_entry_target *t;
	int verdict, sp = 0;
	struct ebt_chainstack *cs;
	struct ebt_entries *chaininfo;
	const char *base;
	const struct ebt_table_info *private;
	struct xt_action_param acpar;

	acpar.state   = state;
	acpar.hotdrop = false;

	read_lock_bh(&table->lock);
	private = table->private;
	cb_base = COUNTER_BASE(private->counters, private->nentries,
	   smp_processor_id());
	if (private->chainstack)
		cs = private->chainstack[smp_processor_id()];
	else
		cs = NULL;
	chaininfo = private->hook_entry[hook];
	nentries = private->hook_entry[hook]->nentries;
	point = (struct ebt_entry *)(private->hook_entry[hook]->data);
	counter_base = cb_base + private->hook_entry[hook]->counter_offset;
	/* base for chain jumps */
	base = private->entries;
	i = 0;
	while (i < nentries) {
		if (ebt_basic_match(point, skb, state->in, state->out))
			goto letscontinue;

		if (EBT_MATCH_ITERATE(point, ebt_do_match, skb, &acpar) != 0)
			goto letscontinue;
		if (acpar.hotdrop) {
			read_unlock_bh(&table->lock);
			return NF_DROP;
		}

		ADD_COUNTER(*(counter_base + i), skb->len, 1);

		/* these should only watch: not modify, nor tell us
		 * what to do with the packet
		 */
		EBT_WATCHER_ITERATE(point, ebt_do_watcher, skb, &acpar);

		t = ebt_get_target_c(point);
		/* standard target */
		if (!t->u.target->target)
			verdict = ((struct ebt_standard_target *)t)->verdict;
		else {
			acpar.target   = t->u.target;
			acpar.targinfo = t->data;
			verdict = t->u.target->target(skb, &acpar);
		}
		if (verdict == EBT_ACCEPT) {
			read_unlock_bh(&table->lock);
			return NF_ACCEPT;
		}
		if (verdict == EBT_DROP) {
			read_unlock_bh(&table->lock);
			return NF_DROP;
		}
		if (verdict == EBT_RETURN) {
letsreturn:
			if (WARN(sp == 0, "RETURN on base chain")) {
				/* act like this is EBT_CONTINUE */
				goto letscontinue;
			}

			sp--;
			/* put all the local variables right */
			i = cs[sp].n;
			chaininfo = cs[sp].chaininfo;
			nentries = chaininfo->nentries;
			point = cs[sp].e;
			counter_base = cb_base +
			   chaininfo->counter_offset;
			continue;
		}
		if (verdict == EBT_CONTINUE)
			goto letscontinue;

		if (WARN(verdict < 0, "bogus standard verdict\n")) {
			read_unlock_bh(&table->lock);
			return NF_DROP;
		}

		/* jump to a udc */
		cs[sp].n = i + 1;
		cs[sp].chaininfo = chaininfo;
		cs[sp].e = ebt_next_entry(point);
		i = 0;
		chaininfo = (struct ebt_entries *) (base + verdict);

		if (WARN(chaininfo->distinguisher, "jump to non-chain\n")) {
			read_unlock_bh(&table->lock);
			return NF_DROP;
		}

		nentries = chaininfo->nentries;
		point = (struct ebt_entry *)chaininfo->data;
		counter_base = cb_base + chaininfo->counter_offset;
		sp++;
		continue;
letscontinue:
		point = ebt_next_entry(point);
		i++;
	}

	/* I actually like this :) */
	if (chaininfo->policy == EBT_RETURN)
		goto letsreturn;
	if (chaininfo->policy == EBT_ACCEPT) {
		read_unlock_bh(&table->lock);
		return NF_ACCEPT;
	}
	read_unlock_bh(&table->lock);
	return NF_DROP;
}

/* If it succeeds, returns element and locks mutex */
static inline void *
find_inlist_lock_noload(struct list_head *head, const char *name, int *error,
			struct mutex *mutex)
{
	struct {
		struct list_head list;
		char name[EBT_FUNCTION_MAXNAMELEN];
	} *e;

	mutex_lock(mutex);
	list_for_each_entry(e, head, list) {
		if (strcmp(e->name, name) == 0)
			return e;
	}
	*error = -ENOENT;
	mutex_unlock(mutex);
	return NULL;
}

static void *
find_inlist_lock(struct list_head *head, const char *name, const char *prefix,
		 int *error, struct mutex *mutex)
{
	return try_then_request_module(
			find_inlist_lock_noload(head, name, error, mutex),
			"%s%s", prefix, name);
}

static inline struct ebt_table *
find_table_lock(struct net *net, const char *name, int *error,
		struct mutex *mutex)
{
	return find_inlist_lock(&net->xt.tables[NFPROTO_BRIDGE], name,
				"ebtable_", error, mutex);
}

static inline void ebt_free_table_info(struct ebt_table_info *info)
{
	int i;

	if (info->chainstack) {
		for_each_possible_cpu(i)
			vfree(info->chainstack[i]);
		vfree(info->chainstack);
	}
}
static inline int
ebt_check_match(struct ebt_entry_match *m, struct xt_mtchk_param *par,
		unsigned int *cnt)
{
	const struct ebt_entry *e = par->entryinfo;
	struct xt_match *match;
	size_t left = ((char *)e + e->watchers_offset) - (char *)m;
	int ret;

	if (left < sizeof(struct ebt_entry_match) ||
	    left - sizeof(struct ebt_entry_match) < m->match_size)
		return -EINVAL;

	match = xt_find_match(NFPROTO_BRIDGE, m->u.name, m->u.revision);
	if (IS_ERR(match) || match->family != NFPROTO_BRIDGE) {
		if (!IS_ERR(match))
			module_put(match->me);
		request_module("ebt_%s", m->u.name);
		match = xt_find_match(NFPROTO_BRIDGE, m->u.name, m->u.revision);
	}
	if (IS_ERR(match))
		return PTR_ERR(match);
	m->u.match = match;

	par->match     = match;
	par->matchinfo = m->data;
	ret = xt_check_match(par, m->match_size,
	      e->ethproto, e->invflags & EBT_IPROTO);
	if (ret < 0) {
		module_put(match->me);
		return ret;
	}

	(*cnt)++;
	return 0;
}

static inline int
ebt_check_watcher(struct ebt_entry_watcher *w, struct xt_tgchk_param *par,
		  unsigned int *cnt)
{
	const struct ebt_entry *e = par->entryinfo;
	struct xt_target *watcher;
	size_t left = ((char *)e + e->target_offset) - (char *)w;
	int ret;

	if (left < sizeof(struct ebt_entry_watcher) ||
	   left - sizeof(struct ebt_entry_watcher) < w->watcher_size)
		return -EINVAL;

	watcher = xt_request_find_target(NFPROTO_BRIDGE, w->u.name, 0);
	if (IS_ERR(watcher))
		return PTR_ERR(watcher);

	if (watcher->family != NFPROTO_BRIDGE) {
		module_put(watcher->me);
		return -ENOENT;
	}

	w->u.watcher = watcher;

	par->target   = watcher;
	par->targinfo = w->data;
	ret = xt_check_target(par, w->watcher_size,
	      e->ethproto, e->invflags & EBT_IPROTO);
	if (ret < 0) {
		module_put(watcher->me);
		return ret;
	}

	(*cnt)++;
	return 0;
}

static int ebt_verify_pointers(const struct ebt_replace *repl,
			       struct ebt_table_info *newinfo)
{
	unsigned int limit = repl->entries_size;
	unsigned int valid_hooks = repl->valid_hooks;
	unsigned int offset = 0;
	int i;

	for (i = 0; i < NF_BR_NUMHOOKS; i++)
		newinfo->hook_entry[i] = NULL;

	newinfo->entries_size = repl->entries_size;
	newinfo->nentries = repl->nentries;

	while (offset < limit) {
		size_t left = limit - offset;
		struct ebt_entry *e = (void *)newinfo->entries + offset;

		if (left < sizeof(unsigned int))
			break;

		for (i = 0; i < NF_BR_NUMHOOKS; i++) {
			if ((valid_hooks & (1 << i)) == 0)
				continue;
			if ((char __user *)repl->hook_entry[i] ==
			     repl->entries + offset)
				break;
		}

		if (i != NF_BR_NUMHOOKS || !(e->bitmask & EBT_ENTRY_OR_ENTRIES)) {
			if (e->bitmask != 0) {
				/* we make userspace set this right,
				 * so there is no misunderstanding
				 */
				return -EINVAL;
			}
			if (i != NF_BR_NUMHOOKS)
				newinfo->hook_entry[i] = (struct ebt_entries *)e;
			if (left < sizeof(struct ebt_entries))
				break;
			offset += sizeof(struct ebt_entries);
		} else {
			if (left < sizeof(struct ebt_entry))
				break;
			if (left < e->next_offset)
				break;
			if (e->next_offset < sizeof(struct ebt_entry))
				return -EINVAL;
			offset += e->next_offset;
		}
	}
	if (offset != limit)
		return -EINVAL;

	/* check if all valid hooks have a chain */
	for (i = 0; i < NF_BR_NUMHOOKS; i++) {
		if (!newinfo->hook_entry[i] &&
		   (valid_hooks & (1 << i)))
			return -EINVAL;
	}
	return 0;
}

/* this one is very careful, as it is the first function
 * to parse the userspace data
 */
static inline int
ebt_check_entry_size_and_hooks(const struct ebt_entry *e,
			       const struct ebt_table_info *newinfo,
			       unsigned int *n, unsigned int *cnt,
			       unsigned int *totalcnt, unsigned int *udc_cnt)
{
	int i;

	for (i = 0; i < NF_BR_NUMHOOKS; i++) {
		if ((void *)e == (void *)newinfo->hook_entry[i])
			break;
	}
	/* beginning of a new chain
	 * if i == NF_BR_NUMHOOKS it must be a user defined chain
	 */
	if (i != NF_BR_NUMHOOKS || !e->bitmask) {
		/* this checks if the previous chain has as many entries
		 * as it said it has
		 */
		if (*n != *cnt)
			return -EINVAL;

		if (((struct ebt_entries *)e)->policy != EBT_DROP &&
		   ((struct ebt_entries *)e)->policy != EBT_ACCEPT) {
			/* only RETURN from udc */
			if (i != NF_BR_NUMHOOKS ||
			   ((struct ebt_entries *)e)->policy != EBT_RETURN)
				return -EINVAL;
		}
		if (i == NF_BR_NUMHOOKS) /* it's a user defined chain */
			(*udc_cnt)++;
		if (((struct ebt_entries *)e)->counter_offset != *totalcnt)
			return -EINVAL;
		*n = ((struct ebt_entries *)e)->nentries;
		*cnt = 0;
		return 0;
	}
	/* a plain old entry, heh */
	if (sizeof(struct ebt_entry) > e->watchers_offset ||
	   e->watchers_offset > e->target_offset ||
	   e->target_offset >= e->next_offset)
		return -EINVAL;

	/* this is not checked anywhere else */
	if (e->next_offset - e->target_offset < sizeof(struct ebt_entry_target))
		return -EINVAL;

	(*cnt)++;
	(*totalcnt)++;
	return 0;
}

struct ebt_cl_stack {
	struct ebt_chainstack cs;
	int from;
	unsigned int hookmask;
};

/* We need these positions to check that the jumps to a different part of the
 * entries is a jump to the beginning of a new chain.
 */
static inline int
ebt_get_udc_positions(struct ebt_entry *e, struct ebt_table_info *newinfo,
		      unsigned int *n, struct ebt_cl_stack *udc)
{
	int i;

	/* we're only interested in chain starts */
	if (e->bitmask)
		return 0;
	for (i = 0; i < NF_BR_NUMHOOKS; i++) {
		if (newinfo->hook_entry[i] == (struct ebt_entries *)e)
			break;
	}
	/* only care about udc */
	if (i != NF_BR_NUMHOOKS)
		return 0;

	udc[*n].cs.chaininfo = (struct ebt_entries *)e;
	/* these initialisations are depended on later in check_chainloops() */
	udc[*n].cs.n = 0;
	udc[*n].hookmask = 0;

	(*n)++;
	return 0;
}

static inline int
ebt_cleanup_match(struct ebt_entry_match *m, struct net *net, unsigned int *i)
{
	struct xt_mtdtor_param par;

	if (i && (*i)-- == 0)
		return 1;

	par.net       = net;
	par.match     = m->u.match;
	par.matchinfo = m->data;
	par.family    = NFPROTO_BRIDGE;
	if (par.match->destroy != NULL)
		par.match->destroy(&par);
	module_put(par.match->me);
	return 0;
}

static inline int
ebt_cleanup_watcher(struct ebt_entry_watcher *w, struct net *net, unsigned int *i)
{
	struct xt_tgdtor_param par;

	if (i && (*i)-- == 0)
		return 1;

	par.net      = net;
	par.target   = w->u.watcher;
	par.targinfo = w->data;
	par.family   = NFPROTO_BRIDGE;
	if (par.target->destroy != NULL)
		par.target->destroy(&par);
	module_put(par.target->me);
	return 0;
}

static inline int
ebt_cleanup_entry(struct ebt_entry *e, struct net *net, unsigned int *cnt)
{
	struct xt_tgdtor_param par;
	struct ebt_entry_target *t;

	if (e->bitmask == 0)
		return 0;
	/* we're done */
	if (cnt && (*cnt)-- == 0)
		return 1;
	EBT_WATCHER_ITERATE(e, ebt_cleanup_watcher, net, NULL);
	EBT_MATCH_ITERATE(e, ebt_cleanup_match, net, NULL);
	t = ebt_get_target(e);

	par.net      = net;
	par.target   = t->u.target;
	par.targinfo = t->data;
	par.family   = NFPROTO_BRIDGE;
	if (par.target->destroy != NULL)
		par.target->destroy(&par);
	module_put(par.target->me);
	return 0;
}

static inline int
ebt_check_entry(struct ebt_entry *e, struct net *net,
		const struct ebt_table_info *newinfo,
		const char *name, unsigned int *cnt,
		struct ebt_cl_stack *cl_s, unsigned int udc_cnt)
{
	struct ebt_entry_target *t;
	struct xt_target *target;
	unsigned int i, j, hook = 0, hookmask = 0;
	size_t gap;
	int ret;
	struct xt_mtchk_param mtpar;
	struct xt_tgchk_param tgpar;

	/* don't mess with the struct ebt_entries */
	if (e->bitmask == 0)
		return 0;

	if (e->bitmask & ~EBT_F_MASK)
		return -EINVAL;

	if (e->invflags & ~EBT_INV_MASK)
		return -EINVAL;

	if ((e->bitmask & EBT_NOPROTO) && (e->bitmask & EBT_802_3))
		return -EINVAL;

	/* what hook do we belong to? */
	for (i = 0; i < NF_BR_NUMHOOKS; i++) {
		if (!newinfo->hook_entry[i])
			continue;
		if ((char *)newinfo->hook_entry[i] < (char *)e)
			hook = i;
		else
			break;
	}
	/* (1 << NF_BR_NUMHOOKS) tells the check functions the rule is on
	 * a base chain
	 */
	if (i < NF_BR_NUMHOOKS)
		hookmask = (1 << hook) | (1 << NF_BR_NUMHOOKS);
	else {
		for (i = 0; i < udc_cnt; i++)
			if ((char *)(cl_s[i].cs.chaininfo) > (char *)e)
				break;
		if (i == 0)
			hookmask = (1 << hook) | (1 << NF_BR_NUMHOOKS);
		else
			hookmask = cl_s[i - 1].hookmask;
	}
	i = 0;

	memset(&mtpar, 0, sizeof(mtpar));
	memset(&tgpar, 0, sizeof(tgpar));
	mtpar.net	= tgpar.net       = net;
	mtpar.table     = tgpar.table     = name;
	mtpar.entryinfo = tgpar.entryinfo = e;
	mtpar.hook_mask = tgpar.hook_mask = hookmask;
	mtpar.family    = tgpar.family    = NFPROTO_BRIDGE;
	ret = EBT_MATCH_ITERATE(e, ebt_check_match, &mtpar, &i);
	if (ret != 0)
		goto cleanup_matches;
	j = 0;
	ret = EBT_WATCHER_ITERATE(e, ebt_check_watcher, &tgpar, &j);
	if (ret != 0)
		goto cleanup_watchers;
	t = ebt_get_target(e);
	gap = e->next_offset - e->target_offset;

	target = xt_request_find_target(NFPROTO_BRIDGE, t->u.name, 0);
	if (IS_ERR(target)) {
		ret = PTR_ERR(target);
		goto cleanup_watchers;
	}

	/* Reject UNSPEC, xtables verdicts/return values are incompatible */
	if (target->family != NFPROTO_BRIDGE) {
		module_put(target->me);
		ret = -ENOENT;
		goto cleanup_watchers;
	}

	t->u.target = target;
	if (t->u.target == &ebt_standard_target) {
		if (gap < sizeof(struct ebt_standard_target)) {
			ret = -EFAULT;
			goto cleanup_watchers;
		}
		if (((struct ebt_standard_target *)t)->verdict <
		   -NUM_STANDARD_TARGETS) {
			ret = -EFAULT;
			goto cleanup_watchers;
		}
	} else if (t->target_size > gap - sizeof(struct ebt_entry_target)) {
		module_put(t->u.target->me);
		ret = -EFAULT;
		goto cleanup_watchers;
	}

	tgpar.target   = target;
	tgpar.targinfo = t->data;
	ret = xt_check_target(&tgpar, t->target_size,
	      e->ethproto, e->invflags & EBT_IPROTO);
	if (ret < 0) {
		module_put(target->me);
		goto cleanup_watchers;
	}
	(*cnt)++;
	return 0;
cleanup_watchers:
	EBT_WATCHER_ITERATE(e, ebt_cleanup_watcher, net, &j);
cleanup_matches:
	EBT_MATCH_ITERATE(e, ebt_cleanup_match, net, &i);
	return ret;
}

/* checks for loops and sets the hook mask for udc
 * the hook mask for udc tells us from which base chains the udc can be
 * accessed. This mask is a parameter to the check() functions of the extensions
 */
static int check_chainloops(const struct ebt_entries *chain, struct ebt_cl_stack *cl_s,
			    unsigned int udc_cnt, unsigned int hooknr, char *base)
{
	int i, chain_nr = -1, pos = 0, nentries = chain->nentries, verdict;
	const struct ebt_entry *e = (struct ebt_entry *)chain->data;
	const struct ebt_entry_target *t;

	while (pos < nentries || chain_nr != -1) {
		/* end of udc, go back one 'recursion' step */
		if (pos == nentries) {
			/* put back values of the time when this chain was called */
			e = cl_s[chain_nr].cs.e;
			if (cl_s[chain_nr].from != -1)
				nentries =
				cl_s[cl_s[chain_nr].from].cs.chaininfo->nentries;
			else
				nentries = chain->nentries;
			pos = cl_s[chain_nr].cs.n;
			/* make sure we won't see a loop that isn't one */
			cl_s[chain_nr].cs.n = 0;
			chain_nr = cl_s[chain_nr].from;
			if (pos == nentries)
				continue;
		}
		t = ebt_get_target_c(e);
		if (strcmp(t->u.name, EBT_STANDARD_TARGET))
			goto letscontinue;
		if (e->target_offset + sizeof(struct ebt_standard_target) >
		   e->next_offset)
			return -1;

		verdict = ((struct ebt_standard_target *)t)->verdict;
		if (verdict >= 0) { /* jump to another chain */
			struct ebt_entries *hlp2 =
			   (struct ebt_entries *)(base + verdict);
			for (i = 0; i < udc_cnt; i++)
				if (hlp2 == cl_s[i].cs.chaininfo)
					break;
			/* bad destination or loop */
			if (i == udc_cnt)
				return -1;

			if (cl_s[i].cs.n)
				return -1;

			if (cl_s[i].hookmask & (1 << hooknr))
				goto letscontinue;
			/* this can't be 0, so the loop test is correct */
			cl_s[i].cs.n = pos + 1;
			pos = 0;
			cl_s[i].cs.e = ebt_next_entry(e);
			e = (struct ebt_entry *)(hlp2->data);
			nentries = hlp2->nentries;
			cl_s[i].from = chain_nr;
			chain_nr = i;
			/* this udc is accessible from the base chain for hooknr */
			cl_s[i].hookmask |= (1 << hooknr);
			continue;
		}
letscontinue:
		e = ebt_next_entry(e);
		pos++;
	}
	return 0;
}

/* do the parsing of the table/chains/entries/matches/watchers/targets, heh */
static int translate_table(struct net *net, const char *name,
			   struct ebt_table_info *newinfo)
{
	unsigned int i, j, k, udc_cnt;
	int ret;
	struct ebt_cl_stack *cl_s = NULL; /* used in the checking for chain loops */

	i = 0;
	while (i < NF_BR_NUMHOOKS && !newinfo->hook_entry[i])
		i++;
	if (i == NF_BR_NUMHOOKS)
		return -EINVAL;

	if (newinfo->hook_entry[i] != (struct ebt_entries *)newinfo->entries)
		return -EINVAL;

	/* make sure chains are ordered after each other in same order
	 * as their corresponding hooks
	 */
	for (j = i + 1; j < NF_BR_NUMHOOKS; j++) {
		if (!newinfo->hook_entry[j])
			continue;
		if (newinfo->hook_entry[j] <= newinfo->hook_entry[i])
			return -EINVAL;

		i = j;
	}

	/* do some early checkings and initialize some things */
	i = 0; /* holds the expected nr. of entries for the chain */
	j = 0; /* holds the up to now counted entries for the chain */
	k = 0; /* holds the total nr. of entries, should equal
		* newinfo->nentries afterwards
		*/
	udc_cnt = 0; /* will hold the nr. of user defined chains (udc) */
	ret = EBT_ENTRY_ITERATE(newinfo->entries, newinfo->entries_size,
	   ebt_check_entry_size_and_hooks, newinfo,
	   &i, &j, &k, &udc_cnt);

	if (ret != 0)
		return ret;

	if (i != j)
		return -EINVAL;

	if (k != newinfo->nentries)
		return -EINVAL;

	/* get the location of the udc, put them in an array
	 * while we're at it, allocate the chainstack
	 */
	if (udc_cnt) {
		/* this will get free'd in do_replace()/ebt_register_table()
		 * if an error occurs
		 */
		newinfo->chainstack =
			vmalloc(array_size(nr_cpu_ids,
					   sizeof(*(newinfo->chainstack))));
		if (!newinfo->chainstack)
			return -ENOMEM;
		for_each_possible_cpu(i) {
			newinfo->chainstack[i] =
			  vmalloc(array_size(udc_cnt, sizeof(*(newinfo->chainstack[0]))));
			if (!newinfo->chainstack[i]) {
				while (i)
					vfree(newinfo->chainstack[--i]);
				vfree(newinfo->chainstack);
				newinfo->chainstack = NULL;
				return -ENOMEM;
			}
		}

		cl_s = vmalloc(array_size(udc_cnt, sizeof(*cl_s)));
		if (!cl_s)
			return -ENOMEM;
		i = 0; /* the i'th udc */
		EBT_ENTRY_ITERATE(newinfo->entries, newinfo->entries_size,
		   ebt_get_udc_positions, newinfo, &i, cl_s);
		/* sanity check */
		if (i != udc_cnt) {
			vfree(cl_s);
			return -EFAULT;
		}
	}

	/* Check for loops */
	for (i = 0; i < NF_BR_NUMHOOKS; i++)
		if (newinfo->hook_entry[i])
			if (check_chainloops(newinfo->hook_entry[i],
			   cl_s, udc_cnt, i, newinfo->entries)) {
				vfree(cl_s);
				return -EINVAL;
			}

	/* we now know the following (along with E=mc²):
	 *  - the nr of entries in each chain is right
	 *  - the size of the allocated space is right
	 *  - all valid hooks have a corresponding chain
	 *  - there are no loops
	 *  - wrong data can still be on the level of a single entry
	 *  - could be there are jumps to places that are not the
	 *    beginning of a chain. This can only occur in chains that
	 *    are not accessible from any base chains, so we don't care.
	 */

	/* used to know what we need to clean up if something goes wrong */
	i = 0;
	ret = EBT_ENTRY_ITERATE(newinfo->entries, newinfo->entries_size,
	   ebt_check_entry, net, newinfo, name, &i, cl_s, udc_cnt);
	if (ret != 0) {
		EBT_ENTRY_ITERATE(newinfo->entries, newinfo->entries_size,
				  ebt_cleanup_entry, net, &i);
	}
	vfree(cl_s);
	return ret;
}

/* called under write_lock */
static void get_counters(const struct ebt_counter *oldcounters,
			 struct ebt_counter *counters, unsigned int nentries)
{
	int i, cpu;
	struct ebt_counter *counter_base;

	/* counters of cpu 0 */
	memcpy(counters, oldcounters,
	       sizeof(struct ebt_counter) * nentries);

	/* add other counters to those of cpu 0 */
	for_each_possible_cpu(cpu) {
		if (cpu == 0)
			continue;
		counter_base = COUNTER_BASE(oldcounters, nentries, cpu);
		for (i = 0; i < nentries; i++)
			ADD_COUNTER(counters[i], counter_base[i].bcnt,
				    counter_base[i].pcnt);
	}
}

static int do_replace_finish(struct net *net, struct ebt_replace *repl,
			      struct ebt_table_info *newinfo)
{
	int ret;
	struct ebt_counter *counterstmp = NULL;
	/* used to be able to unlock earlier */
	struct ebt_table_info *table;
	struct ebt_table *t;

	/* the user wants counters back
	 * the check on the size is done later, when we have the lock
	 */
	if (repl->num_counters) {
		unsigned long size = repl->num_counters * sizeof(*counterstmp);
		counterstmp = vmalloc(size);
		if (!counterstmp)
			return -ENOMEM;
	}

	newinfo->chainstack = NULL;
	ret = ebt_verify_pointers(repl, newinfo);
	if (ret != 0)
		goto free_counterstmp;

	ret = translate_table(net, repl->name, newinfo);

	if (ret != 0)
		goto free_counterstmp;

	t = find_table_lock(net, repl->name, &ret, &ebt_mutex);
	if (!t) {
		ret = -ENOENT;
		goto free_iterate;
	}

	/* the table doesn't like it */
	if (t->check && (ret = t->check(newinfo, repl->valid_hooks)))
		goto free_unlock;

	if (repl->num_counters && repl->num_counters != t->private->nentries) {
		ret = -EINVAL;
		goto free_unlock;
	}

	/* we have the mutex lock, so no danger in reading this pointer */
	table = t->private;
	/* make sure the table can only be rmmod'ed if it contains no rules */
	if (!table->nentries && newinfo->nentries && !try_module_get(t->me)) {
		ret = -ENOENT;
		goto free_unlock;
	} else if (table->nentries && !newinfo->nentries)
		module_put(t->me);
	/* we need an atomic snapshot of the counters */
	write_lock_bh(&t->lock);
	if (repl->num_counters)
		get_counters(t->private->counters, counterstmp,
		   t->private->nentries);

	t->private = newinfo;
	write_unlock_bh(&t->lock);
	mutex_unlock(&ebt_mutex);
	/* so, a user can change the chains while having messed up her counter
	 * allocation. Only reason why this is done is because this way the lock
	 * is held only once, while this doesn't bring the kernel into a
	 * dangerous state.
	 */
	if (repl->num_counters &&
	   copy_to_user(repl->counters, counterstmp,
	   repl->num_counters * sizeof(struct ebt_counter))) {
		/* Silent error, can't fail, new table is already in place */
		net_warn_ratelimited("ebtables: counters copy to user failed while replacing table\n");
	}

	/* decrease module count and free resources */
	EBT_ENTRY_ITERATE(table->entries, table->entries_size,
			  ebt_cleanup_entry, net, NULL);

	vfree(table->entries);
	ebt_free_table_info(table);
	vfree(table);
	vfree(counterstmp);

#ifdef CONFIG_AUDIT
	if (audit_enabled) {
		audit_log(audit_context(), GFP_KERNEL,
			  AUDIT_NETFILTER_CFG,
			  "table=%s family=%u entries=%u",
			  repl->name, AF_BRIDGE, repl->nentries);
	}
#endif
	return ret;

free_unlock:
	mutex_unlock(&ebt_mutex);
free_iterate:
	EBT_ENTRY_ITERATE(newinfo->entries, newinfo->entries_size,
			  ebt_cleanup_entry, net, NULL);
free_counterstmp:
	vfree(counterstmp);
	/* can be initialized in translate_table() */
	ebt_free_table_info(newinfo);
	return ret;
}

/* replace the table */
static int do_replace(struct net *net, const void __user *user,
		      unsigned int len)
{
	int ret, countersize;
	struct ebt_table_info *newinfo;
	struct ebt_replace tmp;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	if (len != sizeof(tmp) + tmp.entries_size)
		return -EINVAL;

	if (tmp.entries_size == 0)
		return -EINVAL;

	/* overflow check */
	if (tmp.nentries >= ((INT_MAX - sizeof(struct ebt_table_info)) /
			NR_CPUS - SMP_CACHE_BYTES) / sizeof(struct ebt_counter))
		return -ENOMEM;
	if (tmp.num_counters >= INT_MAX / sizeof(struct ebt_counter))
		return -ENOMEM;

	tmp.name[sizeof(tmp.name) - 1] = 0;

	countersize = COUNTER_OFFSET(tmp.nentries) * nr_cpu_ids;
	newinfo = __vmalloc(sizeof(*newinfo) + countersize, GFP_KERNEL_ACCOUNT,
			    PAGE_KERNEL);
	if (!newinfo)
		return -ENOMEM;

	if (countersize)
		memset(newinfo->counters, 0, countersize);

	newinfo->entries = __vmalloc(tmp.entries_size, GFP_KERNEL_ACCOUNT,
				     PAGE_KERNEL);
	if (!newinfo->entries) {
		ret = -ENOMEM;
		goto free_newinfo;
	}
	if (copy_from_user(
	   newinfo->entries, tmp.entries, tmp.entries_size) != 0) {
		ret = -EFAULT;
		goto free_entries;
	}

	ret = do_replace_finish(net, &tmp, newinfo);
	if (ret == 0)
		return ret;
free_entries:
	vfree(newinfo->entries);
free_newinfo:
	vfree(newinfo);
	return ret;
}

static void __ebt_unregister_table(struct net *net, struct ebt_table *table)
{
	mutex_lock(&ebt_mutex);
	list_del(&table->list);
	mutex_unlock(&ebt_mutex);
	EBT_ENTRY_ITERATE(table->private->entries, table->private->entries_size,
			  ebt_cleanup_entry, net, NULL);
	if (table->private->nentries)
		module_put(table->me);
	vfree(table->private->entries);
	ebt_free_table_info(table->private);
	vfree(table->private);
	kfree(table);
}

int ebt_register_table(struct net *net, const struct ebt_table *input_table,
		       const struct nf_hook_ops *ops, struct ebt_table **res)
{
	struct ebt_table_info *newinfo;
	struct ebt_table *t, *table;
	struct ebt_replace_kernel *repl;
	int ret, i, countersize;
	void *p;

	if (input_table == NULL || (repl = input_table->table) == NULL ||
	    repl->entries == NULL || repl->entries_size == 0 ||
	    repl->counters != NULL || input_table->private != NULL)
		return -EINVAL;

	/* Don't add one table to multiple lists. */
	table = kmemdup(input_table, sizeof(struct ebt_table), GFP_KERNEL);
	if (!table) {
		ret = -ENOMEM;
		goto out;
	}

	countersize = COUNTER_OFFSET(repl->nentries) * nr_cpu_ids;
	newinfo = vmalloc(sizeof(*newinfo) + countersize);
	ret = -ENOMEM;
	if (!newinfo)
		goto free_table;

	p = vmalloc(repl->entries_size);
	if (!p)
		goto free_newinfo;

	memcpy(p, repl->entries, repl->entries_size);
	newinfo->entries = p;

	newinfo->entries_size = repl->entries_size;
	newinfo->nentries = repl->nentries;

	if (countersize)
		memset(newinfo->counters, 0, countersize);

	/* fill in newinfo and parse the entries */
	newinfo->chainstack = NULL;
	for (i = 0; i < NF_BR_NUMHOOKS; i++) {
		if ((repl->valid_hooks & (1 << i)) == 0)
			newinfo->hook_entry[i] = NULL;
		else
			newinfo->hook_entry[i] = p +
				((char *)repl->hook_entry[i] - repl->entries);
	}
	ret = translate_table(net, repl->name, newinfo);
	if (ret != 0)
		goto free_chainstack;

	if (table->check && table->check(newinfo, table->valid_hooks)) {
		ret = -EINVAL;
		goto free_chainstack;
	}

	table->private = newinfo;
	rwlock_init(&table->lock);
	mutex_lock(&ebt_mutex);
	list_for_each_entry(t, &net->xt.tables[NFPROTO_BRIDGE], list) {
		if (strcmp(t->name, table->name) == 0) {
			ret = -EEXIST;
			goto free_unlock;
		}
	}

	/* Hold a reference count if the chains aren't empty */
	if (newinfo->nentries && !try_module_get(table->me)) {
		ret = -ENOENT;
		goto free_unlock;
	}
	list_add(&table->list, &net->xt.tables[NFPROTO_BRIDGE]);
	mutex_unlock(&ebt_mutex);

	WRITE_ONCE(*res, table);

	if (!ops)
		return 0;

	ret = nf_register_net_hooks(net, ops, hweight32(table->valid_hooks));
	if (ret) {
		__ebt_unregister_table(net, table);
		*res = NULL;
	}

	return ret;
free_unlock:
	mutex_unlock(&ebt_mutex);
free_chainstack:
	ebt_free_table_info(newinfo);
	vfree(newinfo->entries);
free_newinfo:
	vfree(newinfo);
free_table:
	kfree(table);
out:
	return ret;
}

void ebt_unregister_table(struct net *net, struct ebt_table *table,
			  const struct nf_hook_ops *ops)
{
	if (ops)
		nf_unregister_net_hooks(net, ops, hweight32(table->valid_hooks));
	__ebt_unregister_table(net, table);
}

/* userspace just supplied us with counters */
static int do_update_counters(struct net *net, const char *name,
				struct ebt_counter __user *counters,
				unsigned int num_counters,
				const void __user *user, unsigned int len)
{
	int i, ret;
	struct ebt_counter *tmp;
	struct ebt_table *t;

	if (num_counters == 0)
		return -EINVAL;

	tmp = vmalloc(array_size(num_counters, sizeof(*tmp)));
	if (!tmp)
		return -ENOMEM;

	t = find_table_lock(net, name, &ret, &ebt_mutex);
	if (!t)
		goto free_tmp;

	if (num_counters != t->private->nentries) {
		ret = -EINVAL;
		goto unlock_mutex;
	}

	if (copy_from_user(tmp, counters, num_counters * sizeof(*counters))) {
		ret = -EFAULT;
		goto unlock_mutex;
	}

	/* we want an atomic add of the counters */
	write_lock_bh(&t->lock);

	/* we add to the counters of the first cpu */
	for (i = 0; i < num_counters; i++)
		ADD_COUNTER(t->private->counters[i], tmp[i].bcnt, tmp[i].pcnt);

	write_unlock_bh(&t->lock);
	ret = 0;
unlock_mutex:
	mutex_unlock(&ebt_mutex);
free_tmp:
	vfree(tmp);
	return ret;
}

static int update_counters(struct net *net, const void __user *user,
			    unsigned int len)
{
	struct ebt_replace hlp;

	if (copy_from_user(&hlp, user, sizeof(hlp)))
		return -EFAULT;

	if (len != sizeof(hlp) + hlp.num_counters * sizeof(struct ebt_counter))
		return -EINVAL;

	return do_update_counters(net, hlp.name, hlp.counters,
				hlp.num_counters, user, len);
}

static inline int ebt_obj_to_user(char __user *um, const char *_name,
				  const char *data, int entrysize,
				  int usersize, int datasize, u8 revision)
{
	char name[EBT_EXTENSION_MAXNAMELEN] = {0};

	/* ebtables expects 31 bytes long names but xt_match names are 29 bytes
	 * long. Copy 29 bytes and fill remaining bytes with zeroes.
	 */
	strlcpy(name, _name, sizeof(name));
	if (copy_to_user(um, name, EBT_EXTENSION_MAXNAMELEN) ||
	    put_user(revision, (u8 __user *)(um + EBT_EXTENSION_MAXNAMELEN)) ||
	    put_user(datasize, (int __user *)(um + EBT_EXTENSION_MAXNAMELEN + 1)) ||
	    xt_data_to_user(um + entrysize, data, usersize, datasize,
			    XT_ALIGN(datasize)))
		return -EFAULT;

	return 0;
}

static inline int ebt_match_to_user(const struct ebt_entry_match *m,
				    const char *base, char __user *ubase)
{
	return ebt_obj_to_user(ubase + ((char *)m - base),
			       m->u.match->name, m->data, sizeof(*m),
			       m->u.match->usersize, m->match_size,
			       m->u.match->revision);
}

static inline int ebt_watcher_to_user(const struct ebt_entry_watcher *w,
				      const char *base, char __user *ubase)
{
	return ebt_obj_to_user(ubase + ((char *)w - base),
			       w->u.watcher->name, w->data, sizeof(*w),
			       w->u.watcher->usersize, w->watcher_size,
			       w->u.watcher->revision);
}

static inline int ebt_entry_to_user(struct ebt_entry *e, const char *base,
				    char __user *ubase)
{
	int ret;
	char __user *hlp;
	const struct ebt_entry_target *t;

	if (e->bitmask == 0) {
		/* special case !EBT_ENTRY_OR_ENTRIES */
		if (copy_to_user(ubase + ((char *)e - base), e,
				 sizeof(struct ebt_entries)))
			return -EFAULT;
		return 0;
	}

	if (copy_to_user(ubase + ((char *)e - base), e, sizeof(*e)))
		return -EFAULT;

	hlp = ubase + (((char *)e + e->target_offset) - base);
	t = ebt_get_target_c(e);

	ret = EBT_MATCH_ITERATE(e, ebt_match_to_user, base, ubase);
	if (ret != 0)
		return ret;
	ret = EBT_WATCHER_ITERATE(e, ebt_watcher_to_user, base, ubase);
	if (ret != 0)
		return ret;
	ret = ebt_obj_to_user(hlp, t->u.target->name, t->data, sizeof(*t),
			      t->u.target->usersize, t->target_size,
			      t->u.target->revision);
	if (ret != 0)
		return ret;

	return 0;
}

static int copy_counters_to_user(struct ebt_table *t,
				 const struct ebt_counter *oldcounters,
				 void __user *user, unsigned int num_counters,
				 unsigned int nentries)
{
	struct ebt_counter *counterstmp;
	int ret = 0;

	/* userspace might not need the counters */
	if (num_counters == 0)
		return 0;

	if (num_counters != nentries)
		return -EINVAL;

	counterstmp = vmalloc(array_size(nentries, sizeof(*counterstmp)));
	if (!counterstmp)
		return -ENOMEM;

	write_lock_bh(&t->lock);
	get_counters(oldcounters, counterstmp, nentries);
	write_unlock_bh(&t->lock);

	if (copy_to_user(user, counterstmp,
	   nentries * sizeof(struct ebt_counter)))
		ret = -EFAULT;
	vfree(counterstmp);
	return ret;
}

/* called with ebt_mutex locked */
static int copy_everything_to_user(struct ebt_table *t, void __user *user,
				   const int *len, int cmd)
{
	struct ebt_replace tmp;
	const struct ebt_counter *oldcounters;
	unsigned int entries_size, nentries;
	int ret;
	char *entries;

	if (cmd == EBT_SO_GET_ENTRIES) {
		entries_size = t->private->entries_size;
		nentries = t->private->nentries;
		entries = t->private->entries;
		oldcounters = t->private->counters;
	} else {
		entries_size = t->table->entries_size;
		nentries = t->table->nentries;
		entries = t->table->entries;
		oldcounters = t->table->counters;
	}

	if (copy_from_user(&tmp, user, sizeof(tmp)))
		return -EFAULT;

	if (*len != sizeof(struct ebt_replace) + entries_size +
	   (tmp.num_counters ? nentries * sizeof(struct ebt_counter) : 0))
		return -EINVAL;

	if (tmp.nentries != nentries)
		return -EINVAL;

	if (tmp.entries_size != entries_size)
		return -EINVAL;

	ret = copy_counters_to_user(t, oldcounters, tmp.counters,
					tmp.num_counters, nentries);
	if (ret)
		return ret;

	/* set the match/watcher/target names right */
	return EBT_ENTRY_ITERATE(entries, entries_size,
	   ebt_entry_to_user, entries, tmp.entries);
}

static int do_ebt_set_ctl(struct sock *sk,
	int cmd, void __user *user, unsigned int len)
{
	int ret;
	struct net *net = sock_net(sk);

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case EBT_SO_SET_ENTRIES:
		ret = do_replace(net, user, len);
		break;
	case EBT_SO_SET_COUNTERS:
		ret = update_counters(net, user, len);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int do_ebt_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	int ret;
	struct ebt_replace tmp;
	struct ebt_table *t;
	struct net *net = sock_net(sk);

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	if (copy_from_user(&tmp, user, sizeof(tmp)))
		return -EFAULT;

	tmp.name[sizeof(tmp.name) - 1] = '\0';

	t = find_table_lock(net, tmp.name, &ret, &ebt_mutex);
	if (!t)
		return ret;

	switch (cmd) {
	case EBT_SO_GET_INFO:
	case EBT_SO_GET_INIT_INFO:
		if (*len != sizeof(struct ebt_replace)) {
			ret = -EINVAL;
			mutex_unlock(&ebt_mutex);
			break;
		}
		if (cmd == EBT_SO_GET_INFO) {
			tmp.nentries = t->private->nentries;
			tmp.entries_size = t->private->entries_size;
			tmp.valid_hooks = t->valid_hooks;
		} else {
			tmp.nentries = t->table->nentries;
			tmp.entries_size = t->table->entries_size;
			tmp.valid_hooks = t->table->valid_hooks;
		}
		mutex_unlock(&ebt_mutex);
		if (copy_to_user(user, &tmp, *len) != 0) {
			ret = -EFAULT;
			break;
		}
		ret = 0;
		break;

	case EBT_SO_GET_ENTRIES:
	case EBT_SO_GET_INIT_ENTRIES:
		ret = copy_everything_to_user(t, user, len, cmd);
		mutex_unlock(&ebt_mutex);
		break;

	default:
		mutex_unlock(&ebt_mutex);
		ret = -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
/* 32 bit-userspace compatibility definitions. */
struct compat_ebt_replace {
	char name[EBT_TABLE_MAXNAMELEN];
	compat_uint_t valid_hooks;
	compat_uint_t nentries;
	compat_uint_t entries_size;
	/* start of the chains */
	compat_uptr_t hook_entry[NF_BR_NUMHOOKS];
	/* nr of counters userspace expects back */
	compat_uint_t num_counters;
	/* where the kernel will put the old counters. */
	compat_uptr_t counters;
	compat_uptr_t entries;
};

/* struct ebt_entry_match, _target and _watcher have same layout */
struct compat_ebt_entry_mwt {
	union {
		struct {
			char name[EBT_EXTENSION_MAXNAMELEN];
			u8 revision;
		};
		compat_uptr_t ptr;
	} u;
	compat_uint_t match_size;
	compat_uint_t data[0] __attribute__ ((aligned (__alignof__(struct compat_ebt_replace))));
};

/* account for possible padding between match_size and ->data */
static int ebt_compat_entry_padsize(void)
{
	BUILD_BUG_ON(sizeof(struct ebt_entry_match) <
			sizeof(struct compat_ebt_entry_mwt));
	return (int) sizeof(struct ebt_entry_match) -
			sizeof(struct compat_ebt_entry_mwt);
}

static int ebt_compat_match_offset(const struct xt_match *match,
				   unsigned int userlen)
{
	/* ebt_among needs special handling. The kernel .matchsize is
	 * set to -1 at registration time; at runtime an EBT_ALIGN()ed
	 * value is expected.
	 * Example: userspace sends 4500, ebt_among.c wants 4504.
	 */
	if (unlikely(match->matchsize == -1))
		return XT_ALIGN(userlen) - COMPAT_XT_ALIGN(userlen);
	return xt_compat_match_offset(match);
}

static int compat_match_to_user(struct ebt_entry_match *m, void __user **dstptr,
				unsigned int *size)
{
	const struct xt_match *match = m->u.match;
	struct compat_ebt_entry_mwt __user *cm = *dstptr;
	int off = ebt_compat_match_offset(match, m->match_size);
	compat_uint_t msize = m->match_size - off;

	if (WARN_ON(off >= m->match_size))
		return -EINVAL;

	if (copy_to_user(cm->u.name, match->name, strlen(match->name) + 1) ||
	    put_user(match->revision, &cm->u.revision) ||
	    put_user(msize, &cm->match_size))
		return -EFAULT;

	if (match->compat_to_user) {
		if (match->compat_to_user(cm->data, m->data))
			return -EFAULT;
	} else {
		if (xt_data_to_user(cm->data, m->data, match->usersize, msize,
				    COMPAT_XT_ALIGN(msize)))
			return -EFAULT;
	}

	*size -= ebt_compat_entry_padsize() + off;
	*dstptr = cm->data;
	*dstptr += msize;
	return 0;
}

static int compat_target_to_user(struct ebt_entry_target *t,
				 void __user **dstptr,
				 unsigned int *size)
{
	const struct xt_target *target = t->u.target;
	struct compat_ebt_entry_mwt __user *cm = *dstptr;
	int off = xt_compat_target_offset(target);
	compat_uint_t tsize = t->target_size - off;

	if (WARN_ON(off >= t->target_size))
		return -EINVAL;

	if (copy_to_user(cm->u.name, target->name, strlen(target->name) + 1) ||
	    put_user(target->revision, &cm->u.revision) ||
	    put_user(tsize, &cm->match_size))
		return -EFAULT;

	if (target->compat_to_user) {
		if (target->compat_to_user(cm->data, t->data))
			return -EFAULT;
	} else {
		if (xt_data_to_user(cm->data, t->data, target->usersize, tsize,
				    COMPAT_XT_ALIGN(tsize)))
			return -EFAULT;
	}

	*size -= ebt_compat_entry_padsize() + off;
	*dstptr = cm->data;
	*dstptr += tsize;
	return 0;
}

static int compat_watcher_to_user(struct ebt_entry_watcher *w,
				  void __user **dstptr,
				  unsigned int *size)
{
	return compat_target_to_user((struct ebt_entry_target *)w,
							dstptr, size);
}

static int compat_copy_entry_to_user(struct ebt_entry *e, void __user **dstptr,
				unsigned int *size)
{
	struct ebt_entry_target *t;
	struct ebt_entry __user *ce;
	u32 watchers_offset, target_offset, next_offset;
	compat_uint_t origsize;
	int ret;

	if (e->bitmask == 0) {
		if (*size < sizeof(struct ebt_entries))
			return -EINVAL;
		if (copy_to_user(*dstptr, e, sizeof(struct ebt_entries)))
			return -EFAULT;

		*dstptr += sizeof(struct ebt_entries);
		*size -= sizeof(struct ebt_entries);
		return 0;
	}

	if (*size < sizeof(*ce))
		return -EINVAL;

	ce = *dstptr;
	if (copy_to_user(ce, e, sizeof(*ce)))
		return -EFAULT;

	origsize = *size;
	*dstptr += sizeof(*ce);

	ret = EBT_MATCH_ITERATE(e, compat_match_to_user, dstptr, size);
	if (ret)
		return ret;
	watchers_offset = e->watchers_offset - (origsize - *size);

	ret = EBT_WATCHER_ITERATE(e, compat_watcher_to_user, dstptr, size);
	if (ret)
		return ret;
	target_offset = e->target_offset - (origsize - *size);

	t = ebt_get_target(e);

	ret = compat_target_to_user(t, dstptr, size);
	if (ret)
		return ret;
	next_offset = e->next_offset - (origsize - *size);

	if (put_user(watchers_offset, &ce->watchers_offset) ||
	    put_user(target_offset, &ce->target_offset) ||
	    put_user(next_offset, &ce->next_offset))
		return -EFAULT;

	*size -= sizeof(*ce);
	return 0;
}

static int compat_calc_match(struct ebt_entry_match *m, int *off)
{
	*off += ebt_compat_match_offset(m->u.match, m->match_size);
	*off += ebt_compat_entry_padsize();
	return 0;
}

static int compat_calc_watcher(struct ebt_entry_watcher *w, int *off)
{
	*off += xt_compat_target_offset(w->u.watcher);
	*off += ebt_compat_entry_padsize();
	return 0;
}

static int compat_calc_entry(const struct ebt_entry *e,
			     const struct ebt_table_info *info,
			     const void *base,
			     struct compat_ebt_replace *newinfo)
{
	const struct ebt_entry_target *t;
	unsigned int entry_offset;
	int off, ret, i;

	if (e->bitmask == 0)
		return 0;

	off = 0;
	entry_offset = (void *)e - base;

	EBT_MATCH_ITERATE(e, compat_calc_match, &off);
	EBT_WATCHER_ITERATE(e, compat_calc_watcher, &off);

	t = ebt_get_target_c(e);

	off += xt_compat_target_offset(t->u.target);
	off += ebt_compat_entry_padsize();

	newinfo->entries_size -= off;

	ret = xt_compat_add_offset(NFPROTO_BRIDGE, entry_offset, off);
	if (ret)
		return ret;

	for (i = 0; i < NF_BR_NUMHOOKS; i++) {
		const void *hookptr = info->hook_entry[i];
		if (info->hook_entry[i] &&
		    (e < (struct ebt_entry *)(base - hookptr))) {
			newinfo->hook_entry[i] -= off;
			pr_debug("0x%08X -> 0x%08X\n",
					newinfo->hook_entry[i] + off,
					newinfo->hook_entry[i]);
		}
	}

	return 0;
}

static int ebt_compat_init_offsets(unsigned int number)
{
	if (number > INT_MAX)
		return -EINVAL;

	/* also count the base chain policies */
	number += NF_BR_NUMHOOKS;

	return xt_compat_init_offsets(NFPROTO_BRIDGE, number);
}

static int compat_table_info(const struct ebt_table_info *info,
			     struct compat_ebt_replace *newinfo)
{
	unsigned int size = info->entries_size;
	const void *entries = info->entries;
	int ret;

	newinfo->entries_size = size;
	ret = ebt_compat_init_offsets(info->nentries);
	if (ret)
		return ret;

	return EBT_ENTRY_ITERATE(entries, size, compat_calc_entry, info,
							entries, newinfo);
}

static int compat_copy_everything_to_user(struct ebt_table *t,
					  void __user *user, int *len, int cmd)
{
	struct compat_ebt_replace repl, tmp;
	struct ebt_counter *oldcounters;
	struct ebt_table_info tinfo;
	int ret;
	void __user *pos;

	memset(&tinfo, 0, sizeof(tinfo));

	if (cmd == EBT_SO_GET_ENTRIES) {
		tinfo.entries_size = t->private->entries_size;
		tinfo.nentries = t->private->nentries;
		tinfo.entries = t->private->entries;
		oldcounters = t->private->counters;
	} else {
		tinfo.entries_size = t->table->entries_size;
		tinfo.nentries = t->table->nentries;
		tinfo.entries = t->table->entries;
		oldcounters = t->table->counters;
	}

	if (copy_from_user(&tmp, user, sizeof(tmp)))
		return -EFAULT;

	if (tmp.nentries != tinfo.nentries ||
	   (tmp.num_counters && tmp.num_counters != tinfo.nentries))
		return -EINVAL;

	memcpy(&repl, &tmp, sizeof(repl));
	if (cmd == EBT_SO_GET_ENTRIES)
		ret = compat_table_info(t->private, &repl);
	else
		ret = compat_table_info(&tinfo, &repl);
	if (ret)
		return ret;

	if (*len != sizeof(tmp) + repl.entries_size +
	   (tmp.num_counters? tinfo.nentries * sizeof(struct ebt_counter): 0)) {
		pr_err("wrong size: *len %d, entries_size %u, replsz %d\n",
				*len, tinfo.entries_size, repl.entries_size);
		return -EINVAL;
	}

	/* userspace might not need the counters */
	ret = copy_counters_to_user(t, oldcounters, compat_ptr(tmp.counters),
					tmp.num_counters, tinfo.nentries);
	if (ret)
		return ret;

	pos = compat_ptr(tmp.entries);
	return EBT_ENTRY_ITERATE(tinfo.entries, tinfo.entries_size,
			compat_copy_entry_to_user, &pos, &tmp.entries_size);
}

struct ebt_entries_buf_state {
	char *buf_kern_start;	/* kernel buffer to copy (translated) data to */
	u32 buf_kern_len;	/* total size of kernel buffer */
	u32 buf_kern_offset;	/* amount of data copied so far */
	u32 buf_user_offset;	/* read position in userspace buffer */
};

static int ebt_buf_count(struct ebt_entries_buf_state *state, unsigned int sz)
{
	state->buf_kern_offset += sz;
	return state->buf_kern_offset >= sz ? 0 : -EINVAL;
}

static int ebt_buf_add(struct ebt_entries_buf_state *state,
		       const void *data, unsigned int sz)
{
	if (state->buf_kern_start == NULL)
		goto count_only;

	if (WARN_ON(state->buf_kern_offset + sz > state->buf_kern_len))
		return -EINVAL;

	memcpy(state->buf_kern_start + state->buf_kern_offset, data, sz);

 count_only:
	state->buf_user_offset += sz;
	return ebt_buf_count(state, sz);
}

static int ebt_buf_add_pad(struct ebt_entries_buf_state *state, unsigned int sz)
{
	char *b = state->buf_kern_start;

	if (WARN_ON(b && state->buf_kern_offset > state->buf_kern_len))
		return -EINVAL;

	if (b != NULL && sz > 0)
		memset(b + state->buf_kern_offset, 0, sz);
	/* do not adjust ->buf_user_offset here, we added kernel-side padding */
	return ebt_buf_count(state, sz);
}

enum compat_mwt {
	EBT_COMPAT_MATCH,
	EBT_COMPAT_WATCHER,
	EBT_COMPAT_TARGET,
};

static int compat_mtw_from_user(const struct compat_ebt_entry_mwt *mwt,
				enum compat_mwt compat_mwt,
				struct ebt_entries_buf_state *state,
				const unsigned char *base)
{
	char name[EBT_EXTENSION_MAXNAMELEN];
	struct xt_match *match;
	struct xt_target *wt;
	void *dst = NULL;
	int off, pad = 0;
	unsigned int size_kern, match_size = mwt->match_size;

	if (strscpy(name, mwt->u.name, sizeof(name)) < 0)
		return -EINVAL;

	if (state->buf_kern_start)
		dst = state->buf_kern_start + state->buf_kern_offset;

	switch (compat_mwt) {
	case EBT_COMPAT_MATCH:
		match = xt_request_find_match(NFPROTO_BRIDGE, name,
					      mwt->u.revision);
		if (IS_ERR(match))
			return PTR_ERR(match);

		off = ebt_compat_match_offset(match, match_size);
		if (dst) {
			if (match->compat_from_user)
				match->compat_from_user(dst, mwt->data);
			else
				memcpy(dst, mwt->data, match_size);
		}

		size_kern = match->matchsize;
		if (unlikely(size_kern == -1))
			size_kern = match_size;
		module_put(match->me);
		break;
	case EBT_COMPAT_WATCHER: /* fallthrough */
	case EBT_COMPAT_TARGET:
		wt = xt_request_find_target(NFPROTO_BRIDGE, name,
					    mwt->u.revision);
		if (IS_ERR(wt))
			return PTR_ERR(wt);
		off = xt_compat_target_offset(wt);

		if (dst) {
			if (wt->compat_from_user)
				wt->compat_from_user(dst, mwt->data);
			else
				memcpy(dst, mwt->data, match_size);
		}

		size_kern = wt->targetsize;
		module_put(wt->me);
		break;

	default:
		return -EINVAL;
	}

	state->buf_kern_offset += match_size + off;
	state->buf_user_offset += match_size;
	pad = XT_ALIGN(size_kern) - size_kern;

	if (pad > 0 && dst) {
		if (WARN_ON(state->buf_kern_len <= pad))
			return -EINVAL;
		if (WARN_ON(state->buf_kern_offset - (match_size + off) + size_kern > state->buf_kern_len - pad))
			return -EINVAL;
		memset(dst + size_kern, 0, pad);
	}
	return off + match_size;
}

/* return size of all matches, watchers or target, including necessary
 * alignment and padding.
 */
static int ebt_size_mwt(const struct compat_ebt_entry_mwt *match32,
			unsigned int size_left, enum compat_mwt type,
			struct ebt_entries_buf_state *state, const void *base)
{
	const char *buf = (const char *)match32;
	int growth = 0;

	if (size_left == 0)
		return 0;

	do {
		struct ebt_entry_match *match_kern;
		int ret;

		if (size_left < sizeof(*match32))
			return -EINVAL;

		match_kern = (struct ebt_entry_match *) state->buf_kern_start;
		if (match_kern) {
			char *tmp;
			tmp = state->buf_kern_start + state->buf_kern_offset;
			match_kern = (struct ebt_entry_match *) tmp;
		}
		ret = ebt_buf_add(state, buf, sizeof(*match32));
		if (ret < 0)
			return ret;
		size_left -= sizeof(*match32);

		/* add padding before match->data (if any) */
		ret = ebt_buf_add_pad(state, ebt_compat_entry_padsize());
		if (ret < 0)
			return ret;

		if (match32->match_size > size_left)
			return -EINVAL;

		size_left -= match32->match_size;

		ret = compat_mtw_from_user(match32, type, state, base);
		if (ret < 0)
			return ret;

		if (WARN_ON(ret < match32->match_size))
			return -EINVAL;
		growth += ret - match32->match_size;
		growth += ebt_compat_entry_padsize();

		buf += sizeof(*match32);
		buf += match32->match_size;

		if (match_kern)
			match_kern->match_size = ret;

		match32 = (struct compat_ebt_entry_mwt *) buf;
	} while (size_left);

	return growth;
}

/* called for all ebt_entry structures. */
static int size_entry_mwt(const struct ebt_entry *entry, const unsigned char *base,
			  unsigned int *total,
			  struct ebt_entries_buf_state *state)
{
	unsigned int i, j, startoff, next_expected_off, new_offset = 0;
	/* stores match/watchers/targets & offset of next struct ebt_entry: */
	unsigned int offsets[4];
	unsigned int *offsets_update = NULL;
	int ret;
	char *buf_start;

	if (*total < sizeof(struct ebt_entries))
		return -EINVAL;

	if (!entry->bitmask) {
		*total -= sizeof(struct ebt_entries);
		return ebt_buf_add(state, entry, sizeof(struct ebt_entries));
	}
	if (*total < sizeof(*entry) || entry->next_offset < sizeof(*entry))
		return -EINVAL;

	startoff = state->buf_user_offset;
	/* pull in most part of ebt_entry, it does not need to be changed. */
	ret = ebt_buf_add(state, entry,
			offsetof(struct ebt_entry, watchers_offset));
	if (ret < 0)
		return ret;

	offsets[0] = sizeof(struct ebt_entry); /* matches come first */
	memcpy(&offsets[1], &entry->watchers_offset,
			sizeof(offsets) - sizeof(offsets[0]));

	if (state->buf_kern_start) {
		buf_start = state->buf_kern_start + state->buf_kern_offset;
		offsets_update = (unsigned int *) buf_start;
	}
	ret = ebt_buf_add(state, &offsets[1],
			sizeof(offsets) - sizeof(offsets[0]));
	if (ret < 0)
		return ret;
	buf_start = (char *) entry;
	/* 0: matches offset, always follows ebt_entry.
	 * 1: watchers offset, from ebt_entry structure
	 * 2: target offset, from ebt_entry structure
	 * 3: next ebt_entry offset, from ebt_entry structure
	 *
	 * offsets are relative to beginning of struct ebt_entry (i.e., 0).
	 */
	for (i = 0; i < 4 ; ++i) {
		if (offsets[i] > *total)
			return -EINVAL;

		if (i < 3 && offsets[i] == *total)
			return -EINVAL;

		if (i == 0)
			continue;
		if (offsets[i-1] > offsets[i])
			return -EINVAL;
	}

	for (i = 0, j = 1 ; j < 4 ; j++, i++) {
		struct compat_ebt_entry_mwt *match32;
		unsigned int size;
		char *buf = buf_start + offsets[i];

		if (offsets[i] > offsets[j])
			return -EINVAL;

		match32 = (struct compat_ebt_entry_mwt *) buf;
		size = offsets[j] - offsets[i];
		ret = ebt_size_mwt(match32, size, i, state, base);
		if (ret < 0)
			return ret;
		new_offset += ret;
		if (offsets_update && new_offset) {
			pr_debug("change offset %d to %d\n",
				offsets_update[i], offsets[j] + new_offset);
			offsets_update[i] = offsets[j] + new_offset;
		}
	}

	if (state->buf_kern_start == NULL) {
		unsigned int offset = buf_start - (char *) base;

		ret = xt_compat_add_offset(NFPROTO_BRIDGE, offset, new_offset);
		if (ret < 0)
			return ret;
	}

	next_expected_off = state->buf_user_offset - startoff;
	if (next_expected_off != entry->next_offset)
		return -EINVAL;

	if (*total < entry->next_offset)
		return -EINVAL;
	*total -= entry->next_offset;
	return 0;
}

/* repl->entries_size is the size of the ebt_entry blob in userspace.
 * It might need more memory when copied to a 64 bit kernel in case
 * userspace is 32-bit. So, first task: find out how much memory is needed.
 *
 * Called before validation is performed.
 */
static int compat_copy_entries(unsigned char *data, unsigned int size_user,
				struct ebt_entries_buf_state *state)
{
	unsigned int size_remaining = size_user;
	int ret;

	ret = EBT_ENTRY_ITERATE(data, size_user, size_entry_mwt, data,
					&size_remaining, state);
	if (ret < 0)
		return ret;

	if (size_remaining)
		return -EINVAL;

	return state->buf_kern_offset;
}


static int compat_copy_ebt_replace_from_user(struct ebt_replace *repl,
					    void __user *user, unsigned int len)
{
	struct compat_ebt_replace tmp;
	int i;

	if (len < sizeof(tmp))
		return -EINVAL;

	if (copy_from_user(&tmp, user, sizeof(tmp)))
		return -EFAULT;

	if (len != sizeof(tmp) + tmp.entries_size)
		return -EINVAL;

	if (tmp.entries_size == 0)
		return -EINVAL;

	if (tmp.nentries >= ((INT_MAX - sizeof(struct ebt_table_info)) /
			NR_CPUS - SMP_CACHE_BYTES) / sizeof(struct ebt_counter))
		return -ENOMEM;
	if (tmp.num_counters >= INT_MAX / sizeof(struct ebt_counter))
		return -ENOMEM;

	memcpy(repl, &tmp, offsetof(struct ebt_replace, hook_entry));

	/* starting with hook_entry, 32 vs. 64 bit structures are different */
	for (i = 0; i < NF_BR_NUMHOOKS; i++)
		repl->hook_entry[i] = compat_ptr(tmp.hook_entry[i]);

	repl->num_counters = tmp.num_counters;
	repl->counters = compat_ptr(tmp.counters);
	repl->entries = compat_ptr(tmp.entries);
	return 0;
}

static int compat_do_replace(struct net *net, void __user *user,
			     unsigned int len)
{
	int ret, i, countersize, size64;
	struct ebt_table_info *newinfo;
	struct ebt_replace tmp;
	struct ebt_entries_buf_state state;
	void *entries_tmp;

	ret = compat_copy_ebt_replace_from_user(&tmp, user, len);
	if (ret) {
		/* try real handler in case userland supplied needed padding */
		if (ret == -EINVAL && do_replace(net, user, len) == 0)
			ret = 0;
		return ret;
	}

	countersize = COUNTER_OFFSET(tmp.nentries) * nr_cpu_ids;
	newinfo = vmalloc(sizeof(*newinfo) + countersize);
	if (!newinfo)
		return -ENOMEM;

	if (countersize)
		memset(newinfo->counters, 0, countersize);

	memset(&state, 0, sizeof(state));

	newinfo->entries = vmalloc(tmp.entries_size);
	if (!newinfo->entries) {
		ret = -ENOMEM;
		goto free_newinfo;
	}
	if (copy_from_user(
	   newinfo->entries, tmp.entries, tmp.entries_size) != 0) {
		ret = -EFAULT;
		goto free_entries;
	}

	entries_tmp = newinfo->entries;

	xt_compat_lock(NFPROTO_BRIDGE);

	ret = ebt_compat_init_offsets(tmp.nentries);
	if (ret < 0)
		goto out_unlock;

	ret = compat_copy_entries(entries_tmp, tmp.entries_size, &state);
	if (ret < 0)
		goto out_unlock;

	pr_debug("tmp.entries_size %d, kern off %d, user off %d delta %d\n",
		tmp.entries_size, state.buf_kern_offset, state.buf_user_offset,
		xt_compat_calc_jump(NFPROTO_BRIDGE, tmp.entries_size));

	size64 = ret;
	newinfo->entries = vmalloc(size64);
	if (!newinfo->entries) {
		vfree(entries_tmp);
		ret = -ENOMEM;
		goto out_unlock;
	}

	memset(&state, 0, sizeof(state));
	state.buf_kern_start = newinfo->entries;
	state.buf_kern_len = size64;

	ret = compat_copy_entries(entries_tmp, tmp.entries_size, &state);
	if (WARN_ON(ret < 0)) {
		vfree(entries_tmp);
		goto out_unlock;
	}

	vfree(entries_tmp);
	tmp.entries_size = size64;

	for (i = 0; i < NF_BR_NUMHOOKS; i++) {
		char __user *usrptr;
		if (tmp.hook_entry[i]) {
			unsigned int delta;
			usrptr = (char __user *) tmp.hook_entry[i];
			delta = usrptr - tmp.entries;
			usrptr += xt_compat_calc_jump(NFPROTO_BRIDGE, delta);
			tmp.hook_entry[i] = (struct ebt_entries __user *)usrptr;
		}
	}

	xt_compat_flush_offsets(NFPROTO_BRIDGE);
	xt_compat_unlock(NFPROTO_BRIDGE);

	ret = do_replace_finish(net, &tmp, newinfo);
	if (ret == 0)
		return ret;
free_entries:
	vfree(newinfo->entries);
free_newinfo:
	vfree(newinfo);
	return ret;
out_unlock:
	xt_compat_flush_offsets(NFPROTO_BRIDGE);
	xt_compat_unlock(NFPROTO_BRIDGE);
	goto free_entries;
}

static int compat_update_counters(struct net *net, void __user *user,
				  unsigned int len)
{
	struct compat_ebt_replace hlp;

	if (copy_from_user(&hlp, user, sizeof(hlp)))
		return -EFAULT;

	/* try real handler in case userland supplied needed padding */
	if (len != sizeof(hlp) + hlp.num_counters * sizeof(struct ebt_counter))
		return update_counters(net, user, len);

	return do_update_counters(net, hlp.name, compat_ptr(hlp.counters),
					hlp.num_counters, user, len);
}

static int compat_do_ebt_set_ctl(struct sock *sk,
		int cmd, void __user *user, unsigned int len)
{
	int ret;
	struct net *net = sock_net(sk);

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case EBT_SO_SET_ENTRIES:
		ret = compat_do_replace(net, user, len);
		break;
	case EBT_SO_SET_COUNTERS:
		ret = compat_update_counters(net, user, len);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int compat_do_ebt_get_ctl(struct sock *sk, int cmd,
		void __user *user, int *len)
{
	int ret;
	struct compat_ebt_replace tmp;
	struct ebt_table *t;
	struct net *net = sock_net(sk);

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	/* try real handler in case userland supplied needed padding */
	if ((cmd == EBT_SO_GET_INFO ||
	     cmd == EBT_SO_GET_INIT_INFO) && *len != sizeof(tmp))
			return do_ebt_get_ctl(sk, cmd, user, len);

	if (copy_from_user(&tmp, user, sizeof(tmp)))
		return -EFAULT;

	tmp.name[sizeof(tmp.name) - 1] = '\0';

	t = find_table_lock(net, tmp.name, &ret, &ebt_mutex);
	if (!t)
		return ret;

	xt_compat_lock(NFPROTO_BRIDGE);
	switch (cmd) {
	case EBT_SO_GET_INFO:
		tmp.nentries = t->private->nentries;
		ret = compat_table_info(t->private, &tmp);
		if (ret)
			goto out;
		tmp.valid_hooks = t->valid_hooks;

		if (copy_to_user(user, &tmp, *len) != 0) {
			ret = -EFAULT;
			break;
		}
		ret = 0;
		break;
	case EBT_SO_GET_INIT_INFO:
		tmp.nentries = t->table->nentries;
		tmp.entries_size = t->table->entries_size;
		tmp.valid_hooks = t->table->valid_hooks;

		if (copy_to_user(user, &tmp, *len) != 0) {
			ret = -EFAULT;
			break;
		}
		ret = 0;
		break;
	case EBT_SO_GET_ENTRIES:
	case EBT_SO_GET_INIT_ENTRIES:
		/* try real handler first in case of userland-side padding.
		 * in case we are dealing with an 'ordinary' 32 bit binary
		 * without 64bit compatibility padding, this will fail right
		 * after copy_from_user when the *len argument is validated.
		 *
		 * the compat_ variant needs to do one pass over the kernel
		 * data set to adjust for size differences before it the check.
		 */
		if (copy_everything_to_user(t, user, len, cmd) == 0)
			ret = 0;
		else
			ret = compat_copy_everything_to_user(t, user, len, cmd);
		break;
	default:
		ret = -EINVAL;
	}
 out:
	xt_compat_flush_offsets(NFPROTO_BRIDGE);
	xt_compat_unlock(NFPROTO_BRIDGE);
	mutex_unlock(&ebt_mutex);
	return ret;
}
#endif

static struct nf_sockopt_ops ebt_sockopts = {
	.pf		= PF_INET,
	.set_optmin	= EBT_BASE_CTL,
	.set_optmax	= EBT_SO_SET_MAX + 1,
	.set		= do_ebt_set_ctl,
#ifdef CONFIG_COMPAT
	.compat_set	= compat_do_ebt_set_ctl,
#endif
	.get_optmin	= EBT_BASE_CTL,
	.get_optmax	= EBT_SO_GET_MAX + 1,
	.get		= do_ebt_get_ctl,
#ifdef CONFIG_COMPAT
	.compat_get	= compat_do_ebt_get_ctl,
#endif
	.owner		= THIS_MODULE,
};

static int __init ebtables_init(void)
{
	int ret;

	ret = xt_register_target(&ebt_standard_target);
	if (ret < 0)
		return ret;
	ret = nf_register_sockopt(&ebt_sockopts);
	if (ret < 0) {
		xt_unregister_target(&ebt_standard_target);
		return ret;
	}

	return 0;
}

static void __exit ebtables_fini(void)
{
	nf_unregister_sockopt(&ebt_sockopts);
	xt_unregister_target(&ebt_standard_target);
}

EXPORT_SYMBOL(ebt_register_table);
EXPORT_SYMBOL(ebt_unregister_table);
EXPORT_SYMBOL(ebt_do_table);
module_init(ebtables_init);
module_exit(ebtables_fini);
MODULE_LICENSE("GPL");
