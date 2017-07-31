/*
 * Packet matching code.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2005 Netfilter Core Team <coreteam@netfilter.org>
 * Copyright (c) 2006-2010 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/in.h>
#include <linux/skbuff.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/poison.h>
#include <linux/icmpv6.h>
#include <net/ipv6.h>
#include <net/compat.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/err.h>
#include <linux/cpumask.h>

#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_log.h>
#include "../../netfilter/xt_repldata.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("IPv6 packet filter");

#ifdef CONFIG_NETFILTER_DEBUG
#define IP_NF_ASSERT(x)	WARN_ON(!(x))
#else
#define IP_NF_ASSERT(x)
#endif

void *ip6t_alloc_initial_table(const struct xt_table *info)
{
	return xt_alloc_initial_table(ip6t, IP6T);
}
EXPORT_SYMBOL_GPL(ip6t_alloc_initial_table);

/* Returns whether matches rule or not. */
/* Performance critical - called for every packet */
static inline bool
ip6_packet_match(const struct sk_buff *skb,
		 const char *indev,
		 const char *outdev,
		 const struct ip6t_ip6 *ip6info,
		 unsigned int *protoff,
		 int *fragoff, bool *hotdrop)
{
	unsigned long ret;
	const struct ipv6hdr *ipv6 = ipv6_hdr(skb);

	if (NF_INVF(ip6info, IP6T_INV_SRCIP,
		    ipv6_masked_addr_cmp(&ipv6->saddr, &ip6info->smsk,
					 &ip6info->src)) ||
	    NF_INVF(ip6info, IP6T_INV_DSTIP,
		    ipv6_masked_addr_cmp(&ipv6->daddr, &ip6info->dmsk,
					 &ip6info->dst)))
		return false;

	ret = ifname_compare_aligned(indev, ip6info->iniface, ip6info->iniface_mask);

	if (NF_INVF(ip6info, IP6T_INV_VIA_IN, ret != 0))
		return false;

	ret = ifname_compare_aligned(outdev, ip6info->outiface, ip6info->outiface_mask);

	if (NF_INVF(ip6info, IP6T_INV_VIA_OUT, ret != 0))
		return false;

/* ... might want to do something with class and flowlabel here ... */

	/* look for the desired protocol header */
	if (ip6info->flags & IP6T_F_PROTO) {
		int protohdr;
		unsigned short _frag_off;

		protohdr = ipv6_find_hdr(skb, protoff, -1, &_frag_off, NULL);
		if (protohdr < 0) {
			if (_frag_off == 0)
				*hotdrop = true;
			return false;
		}
		*fragoff = _frag_off;

		if (ip6info->proto == protohdr) {
			if (ip6info->invflags & IP6T_INV_PROTO)
				return false;

			return true;
		}

		/* We need match for the '-p all', too! */
		if ((ip6info->proto != 0) &&
			!(ip6info->invflags & IP6T_INV_PROTO))
			return false;
	}
	return true;
}

/* should be ip6 safe */
static bool
ip6_checkentry(const struct ip6t_ip6 *ipv6)
{
	if (ipv6->flags & ~IP6T_F_MASK)
		return false;
	if (ipv6->invflags & ~IP6T_INV_MASK)
		return false;

	return true;
}

static unsigned int
ip6t_error(struct sk_buff *skb, const struct xt_action_param *par)
{
	net_info_ratelimited("error: `%s'\n", (const char *)par->targinfo);

	return NF_DROP;
}

static inline struct ip6t_entry *
get_entry(const void *base, unsigned int offset)
{
	return (struct ip6t_entry *)(base + offset);
}

/* All zeroes == unconditional rule. */
/* Mildly perf critical (only if packet tracing is on) */
static inline bool unconditional(const struct ip6t_entry *e)
{
	static const struct ip6t_ip6 uncond;

	return e->target_offset == sizeof(struct ip6t_entry) &&
	       memcmp(&e->ipv6, &uncond, sizeof(uncond)) == 0;
}

static inline const struct xt_entry_target *
ip6t_get_target_c(const struct ip6t_entry *e)
{
	return ip6t_get_target((struct ip6t_entry *)e);
}

#if IS_ENABLED(CONFIG_NETFILTER_XT_TARGET_TRACE)
/* This cries for unification! */
static const char *const hooknames[] = {
	[NF_INET_PRE_ROUTING]		= "PREROUTING",
	[NF_INET_LOCAL_IN]		= "INPUT",
	[NF_INET_FORWARD]		= "FORWARD",
	[NF_INET_LOCAL_OUT]		= "OUTPUT",
	[NF_INET_POST_ROUTING]		= "POSTROUTING",
};

enum nf_ip_trace_comments {
	NF_IP6_TRACE_COMMENT_RULE,
	NF_IP6_TRACE_COMMENT_RETURN,
	NF_IP6_TRACE_COMMENT_POLICY,
};

static const char *const comments[] = {
	[NF_IP6_TRACE_COMMENT_RULE]	= "rule",
	[NF_IP6_TRACE_COMMENT_RETURN]	= "return",
	[NF_IP6_TRACE_COMMENT_POLICY]	= "policy",
};

static struct nf_loginfo trace_loginfo = {
	.type = NF_LOG_TYPE_LOG,
	.u = {
		.log = {
			.level = LOGLEVEL_WARNING,
			.logflags = NF_LOG_DEFAULT_MASK,
		},
	},
};

/* Mildly perf critical (only if packet tracing is on) */
static inline int
get_chainname_rulenum(const struct ip6t_entry *s, const struct ip6t_entry *e,
		      const char *hookname, const char **chainname,
		      const char **comment, unsigned int *rulenum)
{
	const struct xt_standard_target *t = (void *)ip6t_get_target_c(s);

	if (strcmp(t->target.u.kernel.target->name, XT_ERROR_TARGET) == 0) {
		/* Head of user chain: ERROR target with chainname */
		*chainname = t->target.data;
		(*rulenum) = 0;
	} else if (s == e) {
		(*rulenum)++;

		if (unconditional(s) &&
		    strcmp(t->target.u.kernel.target->name,
			   XT_STANDARD_TARGET) == 0 &&
		    t->verdict < 0) {
			/* Tail of chains: STANDARD target (return/policy) */
			*comment = *chainname == hookname
				? comments[NF_IP6_TRACE_COMMENT_POLICY]
				: comments[NF_IP6_TRACE_COMMENT_RETURN];
		}
		return 1;
	} else
		(*rulenum)++;

	return 0;
}

static void trace_packet(struct net *net,
			 const struct sk_buff *skb,
			 unsigned int hook,
			 const struct net_device *in,
			 const struct net_device *out,
			 const char *tablename,
			 const struct xt_table_info *private,
			 const struct ip6t_entry *e)
{
	const struct ip6t_entry *root;
	const char *hookname, *chainname, *comment;
	const struct ip6t_entry *iter;
	unsigned int rulenum = 0;

	root = get_entry(private->entries, private->hook_entry[hook]);

	hookname = chainname = hooknames[hook];
	comment = comments[NF_IP6_TRACE_COMMENT_RULE];

	xt_entry_foreach(iter, root, private->size - private->hook_entry[hook])
		if (get_chainname_rulenum(iter, e, hookname,
		    &chainname, &comment, &rulenum) != 0)
			break;

	nf_log_trace(net, AF_INET6, hook, skb, in, out, &trace_loginfo,
		     "TRACE: %s:%s:%s:%u ",
		     tablename, chainname, comment, rulenum);
}
#endif

static inline struct ip6t_entry *
ip6t_next_entry(const struct ip6t_entry *entry)
{
	return (void *)entry + entry->next_offset;
}

/* Returns one of the generic firewall policies, like NF_ACCEPT. */
unsigned int
ip6t_do_table(struct sk_buff *skb,
	      const struct nf_hook_state *state,
	      struct xt_table *table)
{
	unsigned int hook = state->hook;
	static const char nulldevname[IFNAMSIZ] __attribute__((aligned(sizeof(long))));
	/* Initializing verdict to NF_DROP keeps gcc happy. */
	unsigned int verdict = NF_DROP;
	const char *indev, *outdev;
	const void *table_base;
	struct ip6t_entry *e, **jumpstack;
	unsigned int stackidx, cpu;
	const struct xt_table_info *private;
	struct xt_action_param acpar;
	unsigned int addend;

	/* Initialization */
	stackidx = 0;
	indev = state->in ? state->in->name : nulldevname;
	outdev = state->out ? state->out->name : nulldevname;
	/* We handle fragments by dealing with the first fragment as
	 * if it was a normal packet.  All other fragments are treated
	 * normally, except that they will NEVER match rules that ask
	 * things we don't know, ie. tcp syn flag or ports).  If the
	 * rule is also a fragment-specific rule, non-fragments won't
	 * match it. */
	acpar.hotdrop = false;
	acpar.state   = state;

	IP_NF_ASSERT(table->valid_hooks & (1 << hook));

	local_bh_disable();
	addend = xt_write_recseq_begin();
	private = table->private;
	/*
	 * Ensure we load private-> members after we've fetched the base
	 * pointer.
	 */
	smp_read_barrier_depends();
	cpu        = smp_processor_id();
	table_base = private->entries;
	jumpstack  = (struct ip6t_entry **)private->jumpstack[cpu];

	/* Switch to alternate jumpstack if we're being invoked via TEE.
	 * TEE issues XT_CONTINUE verdict on original skb so we must not
	 * clobber the jumpstack.
	 *
	 * For recursion via REJECT or SYNPROXY the stack will be clobbered
	 * but it is no problem since absolute verdict is issued by these.
	 */
	if (static_key_false(&xt_tee_enabled))
		jumpstack += private->stacksize * __this_cpu_read(nf_skb_duplicated);

	e = get_entry(table_base, private->hook_entry[hook]);

	do {
		const struct xt_entry_target *t;
		const struct xt_entry_match *ematch;
		struct xt_counters *counter;

		IP_NF_ASSERT(e);
		acpar.thoff = 0;
		if (!ip6_packet_match(skb, indev, outdev, &e->ipv6,
		    &acpar.thoff, &acpar.fragoff, &acpar.hotdrop)) {
 no_match:
			e = ip6t_next_entry(e);
			continue;
		}

		xt_ematch_foreach(ematch, e) {
			acpar.match     = ematch->u.kernel.match;
			acpar.matchinfo = ematch->data;
			if (!acpar.match->match(skb, &acpar))
				goto no_match;
		}

		counter = xt_get_this_cpu_counter(&e->counters);
		ADD_COUNTER(*counter, skb->len, 1);

		t = ip6t_get_target_c(e);
		IP_NF_ASSERT(t->u.kernel.target);

#if IS_ENABLED(CONFIG_NETFILTER_XT_TARGET_TRACE)
		/* The packet is traced: log it */
		if (unlikely(skb->nf_trace))
			trace_packet(state->net, skb, hook, state->in,
				     state->out, table->name, private, e);
#endif
		/* Standard target? */
		if (!t->u.kernel.target->target) {
			int v;

			v = ((struct xt_standard_target *)t)->verdict;
			if (v < 0) {
				/* Pop from stack? */
				if (v != XT_RETURN) {
					verdict = (unsigned int)(-v) - 1;
					break;
				}
				if (stackidx == 0)
					e = get_entry(table_base,
					    private->underflow[hook]);
				else
					e = ip6t_next_entry(jumpstack[--stackidx]);
				continue;
			}
			if (table_base + v != ip6t_next_entry(e) &&
			    !(e->ipv6.flags & IP6T_F_GOTO)) {
				jumpstack[stackidx++] = e;
			}

			e = get_entry(table_base, v);
			continue;
		}

		acpar.target   = t->u.kernel.target;
		acpar.targinfo = t->data;

		verdict = t->u.kernel.target->target(skb, &acpar);
		if (verdict == XT_CONTINUE)
			e = ip6t_next_entry(e);
		else
			/* Verdict */
			break;
	} while (!acpar.hotdrop);

	xt_write_recseq_end(addend);
	local_bh_enable();

	if (acpar.hotdrop)
		return NF_DROP;
	else return verdict;
}

/* Figures out from what hook each rule can be called: returns 0 if
   there are loops.  Puts hook bitmask in comefrom. */
static int
mark_source_chains(const struct xt_table_info *newinfo,
		   unsigned int valid_hooks, void *entry0,
		   unsigned int *offsets)
{
	unsigned int hook;

	/* No recursion; use packet counter to save back ptrs (reset
	   to 0 as we leave), and comefrom to save source hook bitmask */
	for (hook = 0; hook < NF_INET_NUMHOOKS; hook++) {
		unsigned int pos = newinfo->hook_entry[hook];
		struct ip6t_entry *e = entry0 + pos;

		if (!(valid_hooks & (1 << hook)))
			continue;

		/* Set initial back pointer. */
		e->counters.pcnt = pos;

		for (;;) {
			const struct xt_standard_target *t
				= (void *)ip6t_get_target_c(e);
			int visited = e->comefrom & (1 << hook);

			if (e->comefrom & (1 << NF_INET_NUMHOOKS))
				return 0;

			e->comefrom |= ((1 << hook) | (1 << NF_INET_NUMHOOKS));

			/* Unconditional return/END. */
			if ((unconditional(e) &&
			     (strcmp(t->target.u.user.name,
				     XT_STANDARD_TARGET) == 0) &&
			     t->verdict < 0) || visited) {
				unsigned int oldpos, size;

				if ((strcmp(t->target.u.user.name,
					    XT_STANDARD_TARGET) == 0) &&
				    t->verdict < -NF_MAX_VERDICT - 1)
					return 0;

				/* Return: backtrack through the last
				   big jump. */
				do {
					e->comefrom ^= (1<<NF_INET_NUMHOOKS);
					oldpos = pos;
					pos = e->counters.pcnt;
					e->counters.pcnt = 0;

					/* We're at the start. */
					if (pos == oldpos)
						goto next;

					e = entry0 + pos;
				} while (oldpos == pos + e->next_offset);

				/* Move along one */
				size = e->next_offset;
				e = entry0 + pos + size;
				if (pos + size >= newinfo->size)
					return 0;
				e->counters.pcnt = pos;
				pos += size;
			} else {
				int newpos = t->verdict;

				if (strcmp(t->target.u.user.name,
					   XT_STANDARD_TARGET) == 0 &&
				    newpos >= 0) {
					/* This a jump; chase it. */
					if (!xt_find_jump_offset(offsets, newpos,
								 newinfo->number))
						return 0;
					e = entry0 + newpos;
				} else {
					/* ... this is a fallthru */
					newpos = pos + e->next_offset;
					if (newpos >= newinfo->size)
						return 0;
				}
				e = entry0 + newpos;
				e->counters.pcnt = pos;
				pos = newpos;
			}
		}
next:		;
	}
	return 1;
}

static void cleanup_match(struct xt_entry_match *m, struct net *net)
{
	struct xt_mtdtor_param par;

	par.net       = net;
	par.match     = m->u.kernel.match;
	par.matchinfo = m->data;
	par.family    = NFPROTO_IPV6;
	if (par.match->destroy != NULL)
		par.match->destroy(&par);
	module_put(par.match->me);
}

static int check_match(struct xt_entry_match *m, struct xt_mtchk_param *par)
{
	const struct ip6t_ip6 *ipv6 = par->entryinfo;

	par->match     = m->u.kernel.match;
	par->matchinfo = m->data;

	return xt_check_match(par, m->u.match_size - sizeof(*m),
			      ipv6->proto, ipv6->invflags & IP6T_INV_PROTO);
}

static int
find_check_match(struct xt_entry_match *m, struct xt_mtchk_param *par)
{
	struct xt_match *match;
	int ret;

	match = xt_request_find_match(NFPROTO_IPV6, m->u.user.name,
				      m->u.user.revision);
	if (IS_ERR(match))
		return PTR_ERR(match);

	m->u.kernel.match = match;

	ret = check_match(m, par);
	if (ret)
		goto err;

	return 0;
err:
	module_put(m->u.kernel.match->me);
	return ret;
}

static int check_target(struct ip6t_entry *e, struct net *net, const char *name)
{
	struct xt_entry_target *t = ip6t_get_target(e);
	struct xt_tgchk_param par = {
		.net       = net,
		.table     = name,
		.entryinfo = e,
		.target    = t->u.kernel.target,
		.targinfo  = t->data,
		.hook_mask = e->comefrom,
		.family    = NFPROTO_IPV6,
	};

	t = ip6t_get_target(e);
	return xt_check_target(&par, t->u.target_size - sizeof(*t),
			       e->ipv6.proto,
			       e->ipv6.invflags & IP6T_INV_PROTO);
}

static int
find_check_entry(struct ip6t_entry *e, struct net *net, const char *name,
		 unsigned int size,
		 struct xt_percpu_counter_alloc_state *alloc_state)
{
	struct xt_entry_target *t;
	struct xt_target *target;
	int ret;
	unsigned int j;
	struct xt_mtchk_param mtpar;
	struct xt_entry_match *ematch;

	if (!xt_percpu_counter_alloc(alloc_state, &e->counters))
		return -ENOMEM;

	j = 0;
	mtpar.net	= net;
	mtpar.table     = name;
	mtpar.entryinfo = &e->ipv6;
	mtpar.hook_mask = e->comefrom;
	mtpar.family    = NFPROTO_IPV6;
	xt_ematch_foreach(ematch, e) {
		ret = find_check_match(ematch, &mtpar);
		if (ret != 0)
			goto cleanup_matches;
		++j;
	}

	t = ip6t_get_target(e);
	target = xt_request_find_target(NFPROTO_IPV6, t->u.user.name,
					t->u.user.revision);
	if (IS_ERR(target)) {
		ret = PTR_ERR(target);
		goto cleanup_matches;
	}
	t->u.kernel.target = target;

	ret = check_target(e, net, name);
	if (ret)
		goto err;
	return 0;
 err:
	module_put(t->u.kernel.target->me);
 cleanup_matches:
	xt_ematch_foreach(ematch, e) {
		if (j-- == 0)
			break;
		cleanup_match(ematch, net);
	}

	xt_percpu_counter_free(&e->counters);

	return ret;
}

static bool check_underflow(const struct ip6t_entry *e)
{
	const struct xt_entry_target *t;
	unsigned int verdict;

	if (!unconditional(e))
		return false;
	t = ip6t_get_target_c(e);
	if (strcmp(t->u.user.name, XT_STANDARD_TARGET) != 0)
		return false;
	verdict = ((struct xt_standard_target *)t)->verdict;
	verdict = -verdict - 1;
	return verdict == NF_DROP || verdict == NF_ACCEPT;
}

static int
check_entry_size_and_hooks(struct ip6t_entry *e,
			   struct xt_table_info *newinfo,
			   const unsigned char *base,
			   const unsigned char *limit,
			   const unsigned int *hook_entries,
			   const unsigned int *underflows,
			   unsigned int valid_hooks)
{
	unsigned int h;
	int err;

	if ((unsigned long)e % __alignof__(struct ip6t_entry) != 0 ||
	    (unsigned char *)e + sizeof(struct ip6t_entry) >= limit ||
	    (unsigned char *)e + e->next_offset > limit)
		return -EINVAL;

	if (e->next_offset
	    < sizeof(struct ip6t_entry) + sizeof(struct xt_entry_target))
		return -EINVAL;

	if (!ip6_checkentry(&e->ipv6))
		return -EINVAL;

	err = xt_check_entry_offsets(e, e->elems, e->target_offset,
				     e->next_offset);
	if (err)
		return err;

	/* Check hooks & underflows */
	for (h = 0; h < NF_INET_NUMHOOKS; h++) {
		if (!(valid_hooks & (1 << h)))
			continue;
		if ((unsigned char *)e - base == hook_entries[h])
			newinfo->hook_entry[h] = hook_entries[h];
		if ((unsigned char *)e - base == underflows[h]) {
			if (!check_underflow(e))
				return -EINVAL;

			newinfo->underflow[h] = underflows[h];
		}
	}

	/* Clear counters and comefrom */
	e->counters = ((struct xt_counters) { 0, 0 });
	e->comefrom = 0;
	return 0;
}

static void cleanup_entry(struct ip6t_entry *e, struct net *net)
{
	struct xt_tgdtor_param par;
	struct xt_entry_target *t;
	struct xt_entry_match *ematch;

	/* Cleanup all matches */
	xt_ematch_foreach(ematch, e)
		cleanup_match(ematch, net);
	t = ip6t_get_target(e);

	par.net      = net;
	par.target   = t->u.kernel.target;
	par.targinfo = t->data;
	par.family   = NFPROTO_IPV6;
	if (par.target->destroy != NULL)
		par.target->destroy(&par);
	module_put(par.target->me);
	xt_percpu_counter_free(&e->counters);
}

/* Checks and translates the user-supplied table segment (held in
   newinfo) */
static int
translate_table(struct net *net, struct xt_table_info *newinfo, void *entry0,
		const struct ip6t_replace *repl)
{
	struct xt_percpu_counter_alloc_state alloc_state = { 0 };
	struct ip6t_entry *iter;
	unsigned int *offsets;
	unsigned int i;
	int ret = 0;

	newinfo->size = repl->size;
	newinfo->number = repl->num_entries;

	/* Init all hooks to impossible value. */
	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = 0xFFFFFFFF;
		newinfo->underflow[i] = 0xFFFFFFFF;
	}

	offsets = xt_alloc_entry_offsets(newinfo->number);
	if (!offsets)
		return -ENOMEM;
	i = 0;
	/* Walk through entries, checking offsets. */
	xt_entry_foreach(iter, entry0, newinfo->size) {
		ret = check_entry_size_and_hooks(iter, newinfo, entry0,
						 entry0 + repl->size,
						 repl->hook_entry,
						 repl->underflow,
						 repl->valid_hooks);
		if (ret != 0)
			goto out_free;
		if (i < repl->num_entries)
			offsets[i] = (void *)iter - entry0;
		++i;
		if (strcmp(ip6t_get_target(iter)->u.user.name,
		    XT_ERROR_TARGET) == 0)
			++newinfo->stacksize;
	}

	ret = -EINVAL;
	if (i != repl->num_entries)
		goto out_free;

	/* Check hooks all assigned */
	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		/* Only hooks which are valid */
		if (!(repl->valid_hooks & (1 << i)))
			continue;
		if (newinfo->hook_entry[i] == 0xFFFFFFFF)
			goto out_free;
		if (newinfo->underflow[i] == 0xFFFFFFFF)
			goto out_free;
	}

	if (!mark_source_chains(newinfo, repl->valid_hooks, entry0, offsets)) {
		ret = -ELOOP;
		goto out_free;
	}
	kvfree(offsets);

	/* Finally, each sanity check must pass */
	i = 0;
	xt_entry_foreach(iter, entry0, newinfo->size) {
		ret = find_check_entry(iter, net, repl->name, repl->size,
				       &alloc_state);
		if (ret != 0)
			break;
		++i;
	}

	if (ret != 0) {
		xt_entry_foreach(iter, entry0, newinfo->size) {
			if (i-- == 0)
				break;
			cleanup_entry(iter, net);
		}
		return ret;
	}

	return ret;
 out_free:
	kvfree(offsets);
	return ret;
}

static void
get_counters(const struct xt_table_info *t,
	     struct xt_counters counters[])
{
	struct ip6t_entry *iter;
	unsigned int cpu;
	unsigned int i;

	for_each_possible_cpu(cpu) {
		seqcount_t *s = &per_cpu(xt_recseq, cpu);

		i = 0;
		xt_entry_foreach(iter, t->entries, t->size) {
			struct xt_counters *tmp;
			u64 bcnt, pcnt;
			unsigned int start;

			tmp = xt_get_per_cpu_counter(&iter->counters, cpu);
			do {
				start = read_seqcount_begin(s);
				bcnt = tmp->bcnt;
				pcnt = tmp->pcnt;
			} while (read_seqcount_retry(s, start));

			ADD_COUNTER(counters[i], bcnt, pcnt);
			++i;
		}
	}
}

static struct xt_counters *alloc_counters(const struct xt_table *table)
{
	unsigned int countersize;
	struct xt_counters *counters;
	const struct xt_table_info *private = table->private;

	/* We need atomic snapshot of counters: rest doesn't change
	   (other than comefrom, which userspace doesn't care
	   about). */
	countersize = sizeof(struct xt_counters) * private->number;
	counters = vzalloc(countersize);

	if (counters == NULL)
		return ERR_PTR(-ENOMEM);

	get_counters(private, counters);

	return counters;
}

static int
copy_entries_to_user(unsigned int total_size,
		     const struct xt_table *table,
		     void __user *userptr)
{
	unsigned int off, num;
	const struct ip6t_entry *e;
	struct xt_counters *counters;
	const struct xt_table_info *private = table->private;
	int ret = 0;
	const void *loc_cpu_entry;

	counters = alloc_counters(table);
	if (IS_ERR(counters))
		return PTR_ERR(counters);

	loc_cpu_entry = private->entries;

	/* FIXME: use iterator macros --RR */
	/* ... then go back and fix counters and names */
	for (off = 0, num = 0; off < total_size; off += e->next_offset, num++){
		unsigned int i;
		const struct xt_entry_match *m;
		const struct xt_entry_target *t;

		e = loc_cpu_entry + off;
		if (copy_to_user(userptr + off, e, sizeof(*e))) {
			ret = -EFAULT;
			goto free_counters;
		}
		if (copy_to_user(userptr + off
				 + offsetof(struct ip6t_entry, counters),
				 &counters[num],
				 sizeof(counters[num])) != 0) {
			ret = -EFAULT;
			goto free_counters;
		}

		for (i = sizeof(struct ip6t_entry);
		     i < e->target_offset;
		     i += m->u.match_size) {
			m = (void *)e + i;

			if (xt_match_to_user(m, userptr + off + i)) {
				ret = -EFAULT;
				goto free_counters;
			}
		}

		t = ip6t_get_target_c(e);
		if (xt_target_to_user(t, userptr + off + e->target_offset)) {
			ret = -EFAULT;
			goto free_counters;
		}
	}

 free_counters:
	vfree(counters);
	return ret;
}

#ifdef CONFIG_COMPAT
static void compat_standard_from_user(void *dst, const void *src)
{
	int v = *(compat_int_t *)src;

	if (v > 0)
		v += xt_compat_calc_jump(AF_INET6, v);
	memcpy(dst, &v, sizeof(v));
}

static int compat_standard_to_user(void __user *dst, const void *src)
{
	compat_int_t cv = *(int *)src;

	if (cv > 0)
		cv -= xt_compat_calc_jump(AF_INET6, cv);
	return copy_to_user(dst, &cv, sizeof(cv)) ? -EFAULT : 0;
}

static int compat_calc_entry(const struct ip6t_entry *e,
			     const struct xt_table_info *info,
			     const void *base, struct xt_table_info *newinfo)
{
	const struct xt_entry_match *ematch;
	const struct xt_entry_target *t;
	unsigned int entry_offset;
	int off, i, ret;

	off = sizeof(struct ip6t_entry) - sizeof(struct compat_ip6t_entry);
	entry_offset = (void *)e - base;
	xt_ematch_foreach(ematch, e)
		off += xt_compat_match_offset(ematch->u.kernel.match);
	t = ip6t_get_target_c(e);
	off += xt_compat_target_offset(t->u.kernel.target);
	newinfo->size -= off;
	ret = xt_compat_add_offset(AF_INET6, entry_offset, off);
	if (ret)
		return ret;

	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		if (info->hook_entry[i] &&
		    (e < (struct ip6t_entry *)(base + info->hook_entry[i])))
			newinfo->hook_entry[i] -= off;
		if (info->underflow[i] &&
		    (e < (struct ip6t_entry *)(base + info->underflow[i])))
			newinfo->underflow[i] -= off;
	}
	return 0;
}

static int compat_table_info(const struct xt_table_info *info,
			     struct xt_table_info *newinfo)
{
	struct ip6t_entry *iter;
	const void *loc_cpu_entry;
	int ret;

	if (!newinfo || !info)
		return -EINVAL;

	/* we dont care about newinfo->entries */
	memcpy(newinfo, info, offsetof(struct xt_table_info, entries));
	newinfo->initial_entries = 0;
	loc_cpu_entry = info->entries;
	xt_compat_init_offsets(AF_INET6, info->number);
	xt_entry_foreach(iter, loc_cpu_entry, info->size) {
		ret = compat_calc_entry(iter, info, loc_cpu_entry, newinfo);
		if (ret != 0)
			return ret;
	}
	return 0;
}
#endif

static int get_info(struct net *net, void __user *user,
		    const int *len, int compat)
{
	char name[XT_TABLE_MAXNAMELEN];
	struct xt_table *t;
	int ret;

	if (*len != sizeof(struct ip6t_getinfo))
		return -EINVAL;

	if (copy_from_user(name, user, sizeof(name)) != 0)
		return -EFAULT;

	name[XT_TABLE_MAXNAMELEN-1] = '\0';
#ifdef CONFIG_COMPAT
	if (compat)
		xt_compat_lock(AF_INET6);
#endif
	t = try_then_request_module(xt_find_table_lock(net, AF_INET6, name),
				    "ip6table_%s", name);
	if (t) {
		struct ip6t_getinfo info;
		const struct xt_table_info *private = t->private;
#ifdef CONFIG_COMPAT
		struct xt_table_info tmp;

		if (compat) {
			ret = compat_table_info(private, &tmp);
			xt_compat_flush_offsets(AF_INET6);
			private = &tmp;
		}
#endif
		memset(&info, 0, sizeof(info));
		info.valid_hooks = t->valid_hooks;
		memcpy(info.hook_entry, private->hook_entry,
		       sizeof(info.hook_entry));
		memcpy(info.underflow, private->underflow,
		       sizeof(info.underflow));
		info.num_entries = private->number;
		info.size = private->size;
		strcpy(info.name, name);

		if (copy_to_user(user, &info, *len) != 0)
			ret = -EFAULT;
		else
			ret = 0;

		xt_table_unlock(t);
		module_put(t->me);
	} else
		ret = -ENOENT;
#ifdef CONFIG_COMPAT
	if (compat)
		xt_compat_unlock(AF_INET6);
#endif
	return ret;
}

static int
get_entries(struct net *net, struct ip6t_get_entries __user *uptr,
	    const int *len)
{
	int ret;
	struct ip6t_get_entries get;
	struct xt_table *t;

	if (*len < sizeof(get))
		return -EINVAL;
	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;
	if (*len != sizeof(struct ip6t_get_entries) + get.size)
		return -EINVAL;

	get.name[sizeof(get.name) - 1] = '\0';

	t = xt_find_table_lock(net, AF_INET6, get.name);
	if (t) {
		struct xt_table_info *private = t->private;
		if (get.size == private->size)
			ret = copy_entries_to_user(private->size,
						   t, uptr->entrytable);
		else
			ret = -EAGAIN;

		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = -ENOENT;

	return ret;
}

static int
__do_replace(struct net *net, const char *name, unsigned int valid_hooks,
	     struct xt_table_info *newinfo, unsigned int num_counters,
	     void __user *counters_ptr)
{
	int ret;
	struct xt_table *t;
	struct xt_table_info *oldinfo;
	struct xt_counters *counters;
	struct ip6t_entry *iter;

	ret = 0;
	counters = vzalloc(num_counters * sizeof(struct xt_counters));
	if (!counters) {
		ret = -ENOMEM;
		goto out;
	}

	t = try_then_request_module(xt_find_table_lock(net, AF_INET6, name),
				    "ip6table_%s", name);
	if (!t) {
		ret = -ENOENT;
		goto free_newinfo_counters_untrans;
	}

	/* You lied! */
	if (valid_hooks != t->valid_hooks) {
		ret = -EINVAL;
		goto put_module;
	}

	oldinfo = xt_replace_table(t, num_counters, newinfo, &ret);
	if (!oldinfo)
		goto put_module;

	/* Update module usage count based on number of rules */
	if ((oldinfo->number > oldinfo->initial_entries) ||
	    (newinfo->number <= oldinfo->initial_entries))
		module_put(t->me);
	if ((oldinfo->number > oldinfo->initial_entries) &&
	    (newinfo->number <= oldinfo->initial_entries))
		module_put(t->me);

	/* Get the old counters, and synchronize with replace */
	get_counters(oldinfo, counters);

	/* Decrease module usage counts and free resource */
	xt_entry_foreach(iter, oldinfo->entries, oldinfo->size)
		cleanup_entry(iter, net);

	xt_free_table_info(oldinfo);
	if (copy_to_user(counters_ptr, counters,
			 sizeof(struct xt_counters) * num_counters) != 0) {
		/* Silent error, can't fail, new table is already in place */
		net_warn_ratelimited("ip6tables: counters copy to user failed while replacing table\n");
	}
	vfree(counters);
	xt_table_unlock(t);
	return ret;

 put_module:
	module_put(t->me);
	xt_table_unlock(t);
 free_newinfo_counters_untrans:
	vfree(counters);
 out:
	return ret;
}

static int
do_replace(struct net *net, const void __user *user, unsigned int len)
{
	int ret;
	struct ip6t_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;
	struct ip6t_entry *iter;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* overflow check */
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;
	if (tmp.num_counters == 0)
		return -EINVAL;

	tmp.name[sizeof(tmp.name)-1] = 0;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	loc_cpu_entry = newinfo->entries;
	if (copy_from_user(loc_cpu_entry, user + sizeof(tmp),
			   tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	ret = translate_table(net, newinfo, loc_cpu_entry, &tmp);
	if (ret != 0)
		goto free_newinfo;

	ret = __do_replace(net, tmp.name, tmp.valid_hooks, newinfo,
			   tmp.num_counters, tmp.counters);
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	xt_entry_foreach(iter, loc_cpu_entry, newinfo->size)
		cleanup_entry(iter, net);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

static int
do_add_counters(struct net *net, const void __user *user, unsigned int len,
		int compat)
{
	unsigned int i;
	struct xt_counters_info tmp;
	struct xt_counters *paddc;
	struct xt_table *t;
	const struct xt_table_info *private;
	int ret = 0;
	struct ip6t_entry *iter;
	unsigned int addend;

	paddc = xt_copy_counters_from_user(user, len, &tmp, compat);
	if (IS_ERR(paddc))
		return PTR_ERR(paddc);
	t = xt_find_table_lock(net, AF_INET6, tmp.name);
	if (!t) {
		ret = -ENOENT;
		goto free;
	}

	local_bh_disable();
	private = t->private;
	if (private->number != tmp.num_counters) {
		ret = -EINVAL;
		goto unlock_up_free;
	}

	i = 0;
	addend = xt_write_recseq_begin();
	xt_entry_foreach(iter, private->entries, private->size) {
		struct xt_counters *tmp;

		tmp = xt_get_this_cpu_counter(&iter->counters);
		ADD_COUNTER(*tmp, paddc[i].bcnt, paddc[i].pcnt);
		++i;
	}
	xt_write_recseq_end(addend);
 unlock_up_free:
	local_bh_enable();
	xt_table_unlock(t);
	module_put(t->me);
 free:
	vfree(paddc);

	return ret;
}

#ifdef CONFIG_COMPAT
struct compat_ip6t_replace {
	char			name[XT_TABLE_MAXNAMELEN];
	u32			valid_hooks;
	u32			num_entries;
	u32			size;
	u32			hook_entry[NF_INET_NUMHOOKS];
	u32			underflow[NF_INET_NUMHOOKS];
	u32			num_counters;
	compat_uptr_t		counters;	/* struct xt_counters * */
	struct compat_ip6t_entry entries[0];
};

static int
compat_copy_entry_to_user(struct ip6t_entry *e, void __user **dstptr,
			  unsigned int *size, struct xt_counters *counters,
			  unsigned int i)
{
	struct xt_entry_target *t;
	struct compat_ip6t_entry __user *ce;
	u_int16_t target_offset, next_offset;
	compat_uint_t origsize;
	const struct xt_entry_match *ematch;
	int ret = 0;

	origsize = *size;
	ce = *dstptr;
	if (copy_to_user(ce, e, sizeof(struct ip6t_entry)) != 0 ||
	    copy_to_user(&ce->counters, &counters[i],
	    sizeof(counters[i])) != 0)
		return -EFAULT;

	*dstptr += sizeof(struct compat_ip6t_entry);
	*size -= sizeof(struct ip6t_entry) - sizeof(struct compat_ip6t_entry);

	xt_ematch_foreach(ematch, e) {
		ret = xt_compat_match_to_user(ematch, dstptr, size);
		if (ret != 0)
			return ret;
	}
	target_offset = e->target_offset - (origsize - *size);
	t = ip6t_get_target(e);
	ret = xt_compat_target_to_user(t, dstptr, size);
	if (ret)
		return ret;
	next_offset = e->next_offset - (origsize - *size);
	if (put_user(target_offset, &ce->target_offset) != 0 ||
	    put_user(next_offset, &ce->next_offset) != 0)
		return -EFAULT;
	return 0;
}

static int
compat_find_calc_match(struct xt_entry_match *m,
		       const struct ip6t_ip6 *ipv6,
		       int *size)
{
	struct xt_match *match;

	match = xt_request_find_match(NFPROTO_IPV6, m->u.user.name,
				      m->u.user.revision);
	if (IS_ERR(match))
		return PTR_ERR(match);

	m->u.kernel.match = match;
	*size += xt_compat_match_offset(match);
	return 0;
}

static void compat_release_entry(struct compat_ip6t_entry *e)
{
	struct xt_entry_target *t;
	struct xt_entry_match *ematch;

	/* Cleanup all matches */
	xt_ematch_foreach(ematch, e)
		module_put(ematch->u.kernel.match->me);
	t = compat_ip6t_get_target(e);
	module_put(t->u.kernel.target->me);
}

static int
check_compat_entry_size_and_hooks(struct compat_ip6t_entry *e,
				  struct xt_table_info *newinfo,
				  unsigned int *size,
				  const unsigned char *base,
				  const unsigned char *limit)
{
	struct xt_entry_match *ematch;
	struct xt_entry_target *t;
	struct xt_target *target;
	unsigned int entry_offset;
	unsigned int j;
	int ret, off;

	if ((unsigned long)e % __alignof__(struct compat_ip6t_entry) != 0 ||
	    (unsigned char *)e + sizeof(struct compat_ip6t_entry) >= limit ||
	    (unsigned char *)e + e->next_offset > limit)
		return -EINVAL;

	if (e->next_offset < sizeof(struct compat_ip6t_entry) +
			     sizeof(struct compat_xt_entry_target))
		return -EINVAL;

	if (!ip6_checkentry(&e->ipv6))
		return -EINVAL;

	ret = xt_compat_check_entry_offsets(e, e->elems,
					    e->target_offset, e->next_offset);
	if (ret)
		return ret;

	off = sizeof(struct ip6t_entry) - sizeof(struct compat_ip6t_entry);
	entry_offset = (void *)e - (void *)base;
	j = 0;
	xt_ematch_foreach(ematch, e) {
		ret = compat_find_calc_match(ematch, &e->ipv6, &off);
		if (ret != 0)
			goto release_matches;
		++j;
	}

	t = compat_ip6t_get_target(e);
	target = xt_request_find_target(NFPROTO_IPV6, t->u.user.name,
					t->u.user.revision);
	if (IS_ERR(target)) {
		ret = PTR_ERR(target);
		goto release_matches;
	}
	t->u.kernel.target = target;

	off += xt_compat_target_offset(target);
	*size += off;
	ret = xt_compat_add_offset(AF_INET6, entry_offset, off);
	if (ret)
		goto out;

	return 0;

out:
	module_put(t->u.kernel.target->me);
release_matches:
	xt_ematch_foreach(ematch, e) {
		if (j-- == 0)
			break;
		module_put(ematch->u.kernel.match->me);
	}
	return ret;
}

static void
compat_copy_entry_from_user(struct compat_ip6t_entry *e, void **dstptr,
			    unsigned int *size,
			    struct xt_table_info *newinfo, unsigned char *base)
{
	struct xt_entry_target *t;
	struct ip6t_entry *de;
	unsigned int origsize;
	int h;
	struct xt_entry_match *ematch;

	origsize = *size;
	de = *dstptr;
	memcpy(de, e, sizeof(struct ip6t_entry));
	memcpy(&de->counters, &e->counters, sizeof(e->counters));

	*dstptr += sizeof(struct ip6t_entry);
	*size += sizeof(struct ip6t_entry) - sizeof(struct compat_ip6t_entry);

	xt_ematch_foreach(ematch, e)
		xt_compat_match_from_user(ematch, dstptr, size);

	de->target_offset = e->target_offset - (origsize - *size);
	t = compat_ip6t_get_target(e);
	xt_compat_target_from_user(t, dstptr, size);

	de->next_offset = e->next_offset - (origsize - *size);
	for (h = 0; h < NF_INET_NUMHOOKS; h++) {
		if ((unsigned char *)de - base < newinfo->hook_entry[h])
			newinfo->hook_entry[h] -= origsize - *size;
		if ((unsigned char *)de - base < newinfo->underflow[h])
			newinfo->underflow[h] -= origsize - *size;
	}
}

static int
translate_compat_table(struct net *net,
		       struct xt_table_info **pinfo,
		       void **pentry0,
		       const struct compat_ip6t_replace *compatr)
{
	unsigned int i, j;
	struct xt_table_info *newinfo, *info;
	void *pos, *entry0, *entry1;
	struct compat_ip6t_entry *iter0;
	struct ip6t_replace repl;
	unsigned int size;
	int ret = 0;

	info = *pinfo;
	entry0 = *pentry0;
	size = compatr->size;
	info->number = compatr->num_entries;

	j = 0;
	xt_compat_lock(AF_INET6);
	xt_compat_init_offsets(AF_INET6, compatr->num_entries);
	/* Walk through entries, checking offsets. */
	xt_entry_foreach(iter0, entry0, compatr->size) {
		ret = check_compat_entry_size_and_hooks(iter0, info, &size,
							entry0,
							entry0 + compatr->size);
		if (ret != 0)
			goto out_unlock;
		++j;
	}

	ret = -EINVAL;
	if (j != compatr->num_entries)
		goto out_unlock;

	ret = -ENOMEM;
	newinfo = xt_alloc_table_info(size);
	if (!newinfo)
		goto out_unlock;

	newinfo->number = compatr->num_entries;
	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = compatr->hook_entry[i];
		newinfo->underflow[i] = compatr->underflow[i];
	}
	entry1 = newinfo->entries;
	pos = entry1;
	size = compatr->size;
	xt_entry_foreach(iter0, entry0, compatr->size)
		compat_copy_entry_from_user(iter0, &pos, &size,
					    newinfo, entry1);

	/* all module references in entry0 are now gone. */
	xt_compat_flush_offsets(AF_INET6);
	xt_compat_unlock(AF_INET6);

	memcpy(&repl, compatr, sizeof(*compatr));

	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		repl.hook_entry[i] = newinfo->hook_entry[i];
		repl.underflow[i] = newinfo->underflow[i];
	}

	repl.num_counters = 0;
	repl.counters = NULL;
	repl.size = newinfo->size;
	ret = translate_table(net, newinfo, entry1, &repl);
	if (ret)
		goto free_newinfo;

	*pinfo = newinfo;
	*pentry0 = entry1;
	xt_free_table_info(info);
	return 0;

free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
out_unlock:
	xt_compat_flush_offsets(AF_INET6);
	xt_compat_unlock(AF_INET6);
	xt_entry_foreach(iter0, entry0, compatr->size) {
		if (j-- == 0)
			break;
		compat_release_entry(iter0);
	}
	return ret;
}

static int
compat_do_replace(struct net *net, void __user *user, unsigned int len)
{
	int ret;
	struct compat_ip6t_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;
	struct ip6t_entry *iter;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* overflow check */
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;
	if (tmp.num_counters == 0)
		return -EINVAL;

	tmp.name[sizeof(tmp.name)-1] = 0;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	loc_cpu_entry = newinfo->entries;
	if (copy_from_user(loc_cpu_entry, user + sizeof(tmp),
			   tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	ret = translate_compat_table(net, &newinfo, &loc_cpu_entry, &tmp);
	if (ret != 0)
		goto free_newinfo;

	ret = __do_replace(net, tmp.name, tmp.valid_hooks, newinfo,
			   tmp.num_counters, compat_ptr(tmp.counters));
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	xt_entry_foreach(iter, loc_cpu_entry, newinfo->size)
		cleanup_entry(iter, net);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

static int
compat_do_ip6t_set_ctl(struct sock *sk, int cmd, void __user *user,
		       unsigned int len)
{
	int ret;

	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IP6T_SO_SET_REPLACE:
		ret = compat_do_replace(sock_net(sk), user, len);
		break;

	case IP6T_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(sock_net(sk), user, len, 1);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

struct compat_ip6t_get_entries {
	char name[XT_TABLE_MAXNAMELEN];
	compat_uint_t size;
	struct compat_ip6t_entry entrytable[0];
};

static int
compat_copy_entries_to_user(unsigned int total_size, struct xt_table *table,
			    void __user *userptr)
{
	struct xt_counters *counters;
	const struct xt_table_info *private = table->private;
	void __user *pos;
	unsigned int size;
	int ret = 0;
	unsigned int i = 0;
	struct ip6t_entry *iter;

	counters = alloc_counters(table);
	if (IS_ERR(counters))
		return PTR_ERR(counters);

	pos = userptr;
	size = total_size;
	xt_entry_foreach(iter, private->entries, total_size) {
		ret = compat_copy_entry_to_user(iter, &pos,
						&size, counters, i++);
		if (ret != 0)
			break;
	}

	vfree(counters);
	return ret;
}

static int
compat_get_entries(struct net *net, struct compat_ip6t_get_entries __user *uptr,
		   int *len)
{
	int ret;
	struct compat_ip6t_get_entries get;
	struct xt_table *t;

	if (*len < sizeof(get))
		return -EINVAL;

	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;

	if (*len != sizeof(struct compat_ip6t_get_entries) + get.size)
		return -EINVAL;

	get.name[sizeof(get.name) - 1] = '\0';

	xt_compat_lock(AF_INET6);
	t = xt_find_table_lock(net, AF_INET6, get.name);
	if (t) {
		const struct xt_table_info *private = t->private;
		struct xt_table_info info;
		ret = compat_table_info(private, &info);
		if (!ret && get.size == info.size)
			ret = compat_copy_entries_to_user(private->size,
							  t, uptr->entrytable);
		else if (!ret)
			ret = -EAGAIN;

		xt_compat_flush_offsets(AF_INET6);
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = -ENOENT;

	xt_compat_unlock(AF_INET6);
	return ret;
}

static int do_ip6t_get_ctl(struct sock *, int, void __user *, int *);

static int
compat_do_ip6t_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	int ret;

	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IP6T_SO_GET_INFO:
		ret = get_info(sock_net(sk), user, len, 1);
		break;
	case IP6T_SO_GET_ENTRIES:
		ret = compat_get_entries(sock_net(sk), user, len);
		break;
	default:
		ret = do_ip6t_get_ctl(sk, cmd, user, len);
	}
	return ret;
}
#endif

static int
do_ip6t_set_ctl(struct sock *sk, int cmd, void __user *user, unsigned int len)
{
	int ret;

	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IP6T_SO_SET_REPLACE:
		ret = do_replace(sock_net(sk), user, len);
		break;

	case IP6T_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(sock_net(sk), user, len, 0);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int
do_ip6t_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	int ret;

	if (!ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IP6T_SO_GET_INFO:
		ret = get_info(sock_net(sk), user, len, 0);
		break;

	case IP6T_SO_GET_ENTRIES:
		ret = get_entries(sock_net(sk), user, len);
		break;

	case IP6T_SO_GET_REVISION_MATCH:
	case IP6T_SO_GET_REVISION_TARGET: {
		struct xt_get_revision rev;
		int target;

		if (*len != sizeof(rev)) {
			ret = -EINVAL;
			break;
		}
		if (copy_from_user(&rev, user, sizeof(rev)) != 0) {
			ret = -EFAULT;
			break;
		}
		rev.name[sizeof(rev.name)-1] = 0;

		if (cmd == IP6T_SO_GET_REVISION_TARGET)
			target = 1;
		else
			target = 0;

		try_then_request_module(xt_find_revision(AF_INET6, rev.name,
							 rev.revision,
							 target, &ret),
					"ip6t_%s", rev.name);
		break;
	}

	default:
		ret = -EINVAL;
	}

	return ret;
}

static void __ip6t_unregister_table(struct net *net, struct xt_table *table)
{
	struct xt_table_info *private;
	void *loc_cpu_entry;
	struct module *table_owner = table->me;
	struct ip6t_entry *iter;

	private = xt_unregister_table(table);

	/* Decrease module usage counts and free resources */
	loc_cpu_entry = private->entries;
	xt_entry_foreach(iter, loc_cpu_entry, private->size)
		cleanup_entry(iter, net);
	if (private->number > private->initial_entries)
		module_put(table_owner);
	xt_free_table_info(private);
}

int ip6t_register_table(struct net *net, const struct xt_table *table,
			const struct ip6t_replace *repl,
			const struct nf_hook_ops *ops,
			struct xt_table **res)
{
	int ret;
	struct xt_table_info *newinfo;
	struct xt_table_info bootstrap = {0};
	void *loc_cpu_entry;
	struct xt_table *new_table;

	newinfo = xt_alloc_table_info(repl->size);
	if (!newinfo)
		return -ENOMEM;

	loc_cpu_entry = newinfo->entries;
	memcpy(loc_cpu_entry, repl->entries, repl->size);

	ret = translate_table(net, newinfo, loc_cpu_entry, repl);
	if (ret != 0)
		goto out_free;

	new_table = xt_register_table(net, table, &bootstrap, newinfo);
	if (IS_ERR(new_table)) {
		ret = PTR_ERR(new_table);
		goto out_free;
	}

	/* set res now, will see skbs right after nf_register_net_hooks */
	WRITE_ONCE(*res, new_table);

	ret = nf_register_net_hooks(net, ops, hweight32(table->valid_hooks));
	if (ret != 0) {
		__ip6t_unregister_table(net, new_table);
		*res = NULL;
	}

	return ret;

out_free:
	xt_free_table_info(newinfo);
	return ret;
}

void ip6t_unregister_table(struct net *net, struct xt_table *table,
			   const struct nf_hook_ops *ops)
{
	nf_unregister_net_hooks(net, ops, hweight32(table->valid_hooks));
	__ip6t_unregister_table(net, table);
}

/* Returns 1 if the type and code is matched by the range, 0 otherwise */
static inline bool
icmp6_type_code_match(u_int8_t test_type, u_int8_t min_code, u_int8_t max_code,
		     u_int8_t type, u_int8_t code,
		     bool invert)
{
	return (type == test_type && code >= min_code && code <= max_code)
		^ invert;
}

static bool
icmp6_match(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct icmp6hdr *ic;
	struct icmp6hdr _icmph;
	const struct ip6t_icmp *icmpinfo = par->matchinfo;

	/* Must not be a fragment. */
	if (par->fragoff != 0)
		return false;

	ic = skb_header_pointer(skb, par->thoff, sizeof(_icmph), &_icmph);
	if (ic == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		par->hotdrop = true;
		return false;
	}

	return icmp6_type_code_match(icmpinfo->type,
				     icmpinfo->code[0],
				     icmpinfo->code[1],
				     ic->icmp6_type, ic->icmp6_code,
				     !!(icmpinfo->invflags&IP6T_ICMP_INV));
}

/* Called when user tries to insert an entry of this type. */
static int icmp6_checkentry(const struct xt_mtchk_param *par)
{
	const struct ip6t_icmp *icmpinfo = par->matchinfo;

	/* Must specify no unknown invflags */
	return (icmpinfo->invflags & ~IP6T_ICMP_INV) ? -EINVAL : 0;
}

/* The built-in targets: standard (NULL) and error. */
static struct xt_target ip6t_builtin_tg[] __read_mostly = {
	{
		.name             = XT_STANDARD_TARGET,
		.targetsize       = sizeof(int),
		.family           = NFPROTO_IPV6,
#ifdef CONFIG_COMPAT
		.compatsize       = sizeof(compat_int_t),
		.compat_from_user = compat_standard_from_user,
		.compat_to_user   = compat_standard_to_user,
#endif
	},
	{
		.name             = XT_ERROR_TARGET,
		.target           = ip6t_error,
		.targetsize       = XT_FUNCTION_MAXNAMELEN,
		.family           = NFPROTO_IPV6,
	},
};

static struct nf_sockopt_ops ip6t_sockopts = {
	.pf		= PF_INET6,
	.set_optmin	= IP6T_BASE_CTL,
	.set_optmax	= IP6T_SO_SET_MAX+1,
	.set		= do_ip6t_set_ctl,
#ifdef CONFIG_COMPAT
	.compat_set	= compat_do_ip6t_set_ctl,
#endif
	.get_optmin	= IP6T_BASE_CTL,
	.get_optmax	= IP6T_SO_GET_MAX+1,
	.get		= do_ip6t_get_ctl,
#ifdef CONFIG_COMPAT
	.compat_get	= compat_do_ip6t_get_ctl,
#endif
	.owner		= THIS_MODULE,
};

static struct xt_match ip6t_builtin_mt[] __read_mostly = {
	{
		.name       = "icmp6",
		.match      = icmp6_match,
		.matchsize  = sizeof(struct ip6t_icmp),
		.checkentry = icmp6_checkentry,
		.proto      = IPPROTO_ICMPV6,
		.family     = NFPROTO_IPV6,
	},
};

static int __net_init ip6_tables_net_init(struct net *net)
{
	return xt_proto_init(net, NFPROTO_IPV6);
}

static void __net_exit ip6_tables_net_exit(struct net *net)
{
	xt_proto_fini(net, NFPROTO_IPV6);
}

static struct pernet_operations ip6_tables_net_ops = {
	.init = ip6_tables_net_init,
	.exit = ip6_tables_net_exit,
};

static int __init ip6_tables_init(void)
{
	int ret;

	ret = register_pernet_subsys(&ip6_tables_net_ops);
	if (ret < 0)
		goto err1;

	/* No one else will be downing sem now, so we won't sleep */
	ret = xt_register_targets(ip6t_builtin_tg, ARRAY_SIZE(ip6t_builtin_tg));
	if (ret < 0)
		goto err2;
	ret = xt_register_matches(ip6t_builtin_mt, ARRAY_SIZE(ip6t_builtin_mt));
	if (ret < 0)
		goto err4;

	/* Register setsockopt */
	ret = nf_register_sockopt(&ip6t_sockopts);
	if (ret < 0)
		goto err5;

	pr_info("(C) 2000-2006 Netfilter Core Team\n");
	return 0;

err5:
	xt_unregister_matches(ip6t_builtin_mt, ARRAY_SIZE(ip6t_builtin_mt));
err4:
	xt_unregister_targets(ip6t_builtin_tg, ARRAY_SIZE(ip6t_builtin_tg));
err2:
	unregister_pernet_subsys(&ip6_tables_net_ops);
err1:
	return ret;
}

static void __exit ip6_tables_fini(void)
{
	nf_unregister_sockopt(&ip6t_sockopts);

	xt_unregister_matches(ip6t_builtin_mt, ARRAY_SIZE(ip6t_builtin_mt));
	xt_unregister_targets(ip6t_builtin_tg, ARRAY_SIZE(ip6t_builtin_tg));
	unregister_pernet_subsys(&ip6_tables_net_ops);
}

EXPORT_SYMBOL(ip6t_register_table);
EXPORT_SYMBOL(ip6t_unregister_table);
EXPORT_SYMBOL(ip6t_do_table);

module_init(ip6_tables_init);
module_exit(ip6_tables_fini);
