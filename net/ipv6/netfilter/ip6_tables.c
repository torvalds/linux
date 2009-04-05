/*
 * Packet matching code.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2005 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

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
#include <asm/uaccess.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/err.h>
#include <linux/cpumask.h>

#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_log.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("IPv6 packet filter");

/*#define DEBUG_IP_FIREWALL*/
/*#define DEBUG_ALLOW_ALL*/ /* Useful for remote debugging */
/*#define DEBUG_IP_FIREWALL_USER*/

#ifdef DEBUG_IP_FIREWALL
#define dprintf(format, args...)  printk(format , ## args)
#else
#define dprintf(format, args...)
#endif

#ifdef DEBUG_IP_FIREWALL_USER
#define duprintf(format, args...) printk(format , ## args)
#else
#define duprintf(format, args...)
#endif

#ifdef CONFIG_NETFILTER_DEBUG
#define IP_NF_ASSERT(x)						\
do {								\
	if (!(x))						\
		printk("IP_NF_ASSERT: %s:%s:%u\n",		\
		       __func__, __FILE__, __LINE__);	\
} while(0)
#else
#define IP_NF_ASSERT(x)
#endif

#if 0
/* All the better to debug you with... */
#define static
#define inline
#endif

/*
   We keep a set of rules for each CPU, so we can avoid write-locking
   them in the softirq when updating the counters and therefore
   only need to read-lock in the softirq; doing a write_lock_bh() in user
   context stops packets coming through and allows user context to read
   the counters or update the rules.

   Hence the start of any table is given by get_table() below.  */

/* Check for an extension */
int
ip6t_ext_hdr(u8 nexthdr)
{
	return ( (nexthdr == IPPROTO_HOPOPTS)   ||
		 (nexthdr == IPPROTO_ROUTING)   ||
		 (nexthdr == IPPROTO_FRAGMENT)  ||
		 (nexthdr == IPPROTO_ESP)       ||
		 (nexthdr == IPPROTO_AH)        ||
		 (nexthdr == IPPROTO_NONE)      ||
		 (nexthdr == IPPROTO_DSTOPTS) );
}

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

#define FWINV(bool, invflg) ((bool) ^ !!(ip6info->invflags & (invflg)))

	if (FWINV(ipv6_masked_addr_cmp(&ipv6->saddr, &ip6info->smsk,
				       &ip6info->src), IP6T_INV_SRCIP)
	    || FWINV(ipv6_masked_addr_cmp(&ipv6->daddr, &ip6info->dmsk,
					  &ip6info->dst), IP6T_INV_DSTIP)) {
		dprintf("Source or dest mismatch.\n");
/*
		dprintf("SRC: %u. Mask: %u. Target: %u.%s\n", ip->saddr,
			ipinfo->smsk.s_addr, ipinfo->src.s_addr,
			ipinfo->invflags & IP6T_INV_SRCIP ? " (INV)" : "");
		dprintf("DST: %u. Mask: %u. Target: %u.%s\n", ip->daddr,
			ipinfo->dmsk.s_addr, ipinfo->dst.s_addr,
			ipinfo->invflags & IP6T_INV_DSTIP ? " (INV)" : "");*/
		return false;
	}

	ret = ifname_compare_aligned(indev, ip6info->iniface, ip6info->iniface_mask);

	if (FWINV(ret != 0, IP6T_INV_VIA_IN)) {
		dprintf("VIA in mismatch (%s vs %s).%s\n",
			indev, ip6info->iniface,
			ip6info->invflags&IP6T_INV_VIA_IN ?" (INV)":"");
		return false;
	}

	ret = ifname_compare_aligned(outdev, ip6info->outiface, ip6info->outiface_mask);

	if (FWINV(ret != 0, IP6T_INV_VIA_OUT)) {
		dprintf("VIA out mismatch (%s vs %s).%s\n",
			outdev, ip6info->outiface,
			ip6info->invflags&IP6T_INV_VIA_OUT ?" (INV)":"");
		return false;
	}

/* ... might want to do something with class and flowlabel here ... */

	/* look for the desired protocol header */
	if((ip6info->flags & IP6T_F_PROTO)) {
		int protohdr;
		unsigned short _frag_off;

		protohdr = ipv6_find_hdr(skb, protoff, -1, &_frag_off);
		if (protohdr < 0) {
			if (_frag_off == 0)
				*hotdrop = true;
			return false;
		}
		*fragoff = _frag_off;

		dprintf("Packet protocol %hi ?= %s%hi.\n",
				protohdr,
				ip6info->invflags & IP6T_INV_PROTO ? "!":"",
				ip6info->proto);

		if (ip6info->proto == protohdr) {
			if(ip6info->invflags & IP6T_INV_PROTO) {
				return false;
			}
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
	if (ipv6->flags & ~IP6T_F_MASK) {
		duprintf("Unknown flag bits set: %08X\n",
			 ipv6->flags & ~IP6T_F_MASK);
		return false;
	}
	if (ipv6->invflags & ~IP6T_INV_MASK) {
		duprintf("Unknown invflag bits set: %08X\n",
			 ipv6->invflags & ~IP6T_INV_MASK);
		return false;
	}
	return true;
}

static unsigned int
ip6t_error(struct sk_buff *skb, const struct xt_target_param *par)
{
	if (net_ratelimit())
		printk("ip6_tables: error: `%s'\n",
		       (const char *)par->targinfo);

	return NF_DROP;
}

/* Performance critical - called for every packet */
static inline bool
do_match(struct ip6t_entry_match *m, const struct sk_buff *skb,
	 struct xt_match_param *par)
{
	par->match     = m->u.kernel.match;
	par->matchinfo = m->data;

	/* Stop iteration if it doesn't match */
	if (!m->u.kernel.match->match(skb, par))
		return true;
	else
		return false;
}

static inline struct ip6t_entry *
get_entry(void *base, unsigned int offset)
{
	return (struct ip6t_entry *)(base + offset);
}

/* All zeroes == unconditional rule. */
/* Mildly perf critical (only if packet tracing is on) */
static inline int
unconditional(const struct ip6t_ip6 *ipv6)
{
	unsigned int i;

	for (i = 0; i < sizeof(*ipv6); i++)
		if (((char *)ipv6)[i])
			break;

	return (i == sizeof(*ipv6));
}

#if defined(CONFIG_NETFILTER_XT_TARGET_TRACE) || \
    defined(CONFIG_NETFILTER_XT_TARGET_TRACE_MODULE)
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
			.level = 4,
			.logflags = NF_LOG_MASK,
		},
	},
};

/* Mildly perf critical (only if packet tracing is on) */
static inline int
get_chainname_rulenum(struct ip6t_entry *s, struct ip6t_entry *e,
		      char *hookname, char **chainname,
		      char **comment, unsigned int *rulenum)
{
	struct ip6t_standard_target *t = (void *)ip6t_get_target(s);

	if (strcmp(t->target.u.kernel.target->name, IP6T_ERROR_TARGET) == 0) {
		/* Head of user chain: ERROR target with chainname */
		*chainname = t->target.data;
		(*rulenum) = 0;
	} else if (s == e) {
		(*rulenum)++;

		if (s->target_offset == sizeof(struct ip6t_entry)
		   && strcmp(t->target.u.kernel.target->name,
			     IP6T_STANDARD_TARGET) == 0
		   && t->verdict < 0
		   && unconditional(&s->ipv6)) {
			/* Tail of chains: STANDARD target (return/policy) */
			*comment = *chainname == hookname
				? (char *)comments[NF_IP6_TRACE_COMMENT_POLICY]
				: (char *)comments[NF_IP6_TRACE_COMMENT_RETURN];
		}
		return 1;
	} else
		(*rulenum)++;

	return 0;
}

static void trace_packet(struct sk_buff *skb,
			 unsigned int hook,
			 const struct net_device *in,
			 const struct net_device *out,
			 const char *tablename,
			 struct xt_table_info *private,
			 struct ip6t_entry *e)
{
	void *table_base;
	const struct ip6t_entry *root;
	char *hookname, *chainname, *comment;
	unsigned int rulenum = 0;

	table_base = (void *)private->entries[smp_processor_id()];
	root = get_entry(table_base, private->hook_entry[hook]);

	hookname = chainname = (char *)hooknames[hook];
	comment = (char *)comments[NF_IP6_TRACE_COMMENT_RULE];

	IP6T_ENTRY_ITERATE(root,
			   private->size - private->hook_entry[hook],
			   get_chainname_rulenum,
			   e, hookname, &chainname, &comment, &rulenum);

	nf_log_packet(AF_INET6, hook, skb, in, out, &trace_loginfo,
		      "TRACE: %s:%s:%s:%u ",
		      tablename, chainname, comment, rulenum);
}
#endif

/* Returns one of the generic firewall policies, like NF_ACCEPT. */
unsigned int
ip6t_do_table(struct sk_buff *skb,
	      unsigned int hook,
	      const struct net_device *in,
	      const struct net_device *out,
	      struct xt_table *table)
{
	static const char nulldevname[IFNAMSIZ] __attribute__((aligned(sizeof(long))));
	bool hotdrop = false;
	/* Initializing verdict to NF_DROP keeps gcc happy. */
	unsigned int verdict = NF_DROP;
	const char *indev, *outdev;
	void *table_base;
	struct ip6t_entry *e, *back;
	struct xt_table_info *private;
	struct xt_match_param mtpar;
	struct xt_target_param tgpar;

	/* Initialization */
	indev = in ? in->name : nulldevname;
	outdev = out ? out->name : nulldevname;
	/* We handle fragments by dealing with the first fragment as
	 * if it was a normal packet.  All other fragments are treated
	 * normally, except that they will NEVER match rules that ask
	 * things we don't know, ie. tcp syn flag or ports).  If the
	 * rule is also a fragment-specific rule, non-fragments won't
	 * match it. */
	mtpar.hotdrop = &hotdrop;
	mtpar.in      = tgpar.in  = in;
	mtpar.out     = tgpar.out = out;
	mtpar.family  = tgpar.family = NFPROTO_IPV6;
	tgpar.hooknum = hook;

	IP_NF_ASSERT(table->valid_hooks & (1 << hook));

	rcu_read_lock_bh();
	private = rcu_dereference(table->private);
	table_base = rcu_dereference(private->entries[smp_processor_id()]);

	e = get_entry(table_base, private->hook_entry[hook]);

	/* For return from builtin chain */
	back = get_entry(table_base, private->underflow[hook]);

	do {
		IP_NF_ASSERT(e);
		IP_NF_ASSERT(back);
		if (ip6_packet_match(skb, indev, outdev, &e->ipv6,
			&mtpar.thoff, &mtpar.fragoff, &hotdrop)) {
			struct ip6t_entry_target *t;

			if (IP6T_MATCH_ITERATE(e, do_match, skb, &mtpar) != 0)
				goto no_match;

			ADD_COUNTER(e->counters,
				    ntohs(ipv6_hdr(skb)->payload_len) +
				    sizeof(struct ipv6hdr), 1);

			t = ip6t_get_target(e);
			IP_NF_ASSERT(t->u.kernel.target);

#if defined(CONFIG_NETFILTER_XT_TARGET_TRACE) || \
    defined(CONFIG_NETFILTER_XT_TARGET_TRACE_MODULE)
			/* The packet is traced: log it */
			if (unlikely(skb->nf_trace))
				trace_packet(skb, hook, in, out,
					     table->name, private, e);
#endif
			/* Standard target? */
			if (!t->u.kernel.target->target) {
				int v;

				v = ((struct ip6t_standard_target *)t)->verdict;
				if (v < 0) {
					/* Pop from stack? */
					if (v != IP6T_RETURN) {
						verdict = (unsigned)(-v) - 1;
						break;
					}
					e = back;
					back = get_entry(table_base,
							 back->comefrom);
					continue;
				}
				if (table_base + v != (void *)e + e->next_offset
				    && !(e->ipv6.flags & IP6T_F_GOTO)) {
					/* Save old back ptr in next entry */
					struct ip6t_entry *next
						= (void *)e + e->next_offset;
					next->comefrom
						= (void *)back - table_base;
					/* set back pointer to next entry */
					back = next;
				}

				e = get_entry(table_base, v);
			} else {
				/* Targets which reenter must return
				   abs. verdicts */
				tgpar.target   = t->u.kernel.target;
				tgpar.targinfo = t->data;

#ifdef CONFIG_NETFILTER_DEBUG
				((struct ip6t_entry *)table_base)->comefrom
					= 0xeeeeeeec;
#endif
				verdict = t->u.kernel.target->target(skb,
								     &tgpar);

#ifdef CONFIG_NETFILTER_DEBUG
				if (((struct ip6t_entry *)table_base)->comefrom
				    != 0xeeeeeeec
				    && verdict == IP6T_CONTINUE) {
					printk("Target %s reentered!\n",
					       t->u.kernel.target->name);
					verdict = NF_DROP;
				}
				((struct ip6t_entry *)table_base)->comefrom
					= 0x57acc001;
#endif
				if (verdict == IP6T_CONTINUE)
					e = (void *)e + e->next_offset;
				else
					/* Verdict */
					break;
			}
		} else {

		no_match:
			e = (void *)e + e->next_offset;
		}
	} while (!hotdrop);

#ifdef CONFIG_NETFILTER_DEBUG
	((struct ip6t_entry *)table_base)->comefrom = NETFILTER_LINK_POISON;
#endif
	rcu_read_unlock_bh();

#ifdef DEBUG_ALLOW_ALL
	return NF_ACCEPT;
#else
	if (hotdrop)
		return NF_DROP;
	else return verdict;
#endif
}

/* Figures out from what hook each rule can be called: returns 0 if
   there are loops.  Puts hook bitmask in comefrom. */
static int
mark_source_chains(struct xt_table_info *newinfo,
		   unsigned int valid_hooks, void *entry0)
{
	unsigned int hook;

	/* No recursion; use packet counter to save back ptrs (reset
	   to 0 as we leave), and comefrom to save source hook bitmask */
	for (hook = 0; hook < NF_INET_NUMHOOKS; hook++) {
		unsigned int pos = newinfo->hook_entry[hook];
		struct ip6t_entry *e = (struct ip6t_entry *)(entry0 + pos);

		if (!(valid_hooks & (1 << hook)))
			continue;

		/* Set initial back pointer. */
		e->counters.pcnt = pos;

		for (;;) {
			struct ip6t_standard_target *t
				= (void *)ip6t_get_target(e);
			int visited = e->comefrom & (1 << hook);

			if (e->comefrom & (1 << NF_INET_NUMHOOKS)) {
				printk("iptables: loop hook %u pos %u %08X.\n",
				       hook, pos, e->comefrom);
				return 0;
			}
			e->comefrom |= ((1 << hook) | (1 << NF_INET_NUMHOOKS));

			/* Unconditional return/END. */
			if ((e->target_offset == sizeof(struct ip6t_entry)
			    && (strcmp(t->target.u.user.name,
				       IP6T_STANDARD_TARGET) == 0)
			    && t->verdict < 0
			    && unconditional(&e->ipv6)) || visited) {
				unsigned int oldpos, size;

				if ((strcmp(t->target.u.user.name,
					    IP6T_STANDARD_TARGET) == 0) &&
				    t->verdict < -NF_MAX_VERDICT - 1) {
					duprintf("mark_source_chains: bad "
						"negative verdict (%i)\n",
								t->verdict);
					return 0;
				}

				/* Return: backtrack through the last
				   big jump. */
				do {
					e->comefrom ^= (1<<NF_INET_NUMHOOKS);
#ifdef DEBUG_IP_FIREWALL_USER
					if (e->comefrom
					    & (1 << NF_INET_NUMHOOKS)) {
						duprintf("Back unset "
							 "on hook %u "
							 "rule %u\n",
							 hook, pos);
					}
#endif
					oldpos = pos;
					pos = e->counters.pcnt;
					e->counters.pcnt = 0;

					/* We're at the start. */
					if (pos == oldpos)
						goto next;

					e = (struct ip6t_entry *)
						(entry0 + pos);
				} while (oldpos == pos + e->next_offset);

				/* Move along one */
				size = e->next_offset;
				e = (struct ip6t_entry *)
					(entry0 + pos + size);
				e->counters.pcnt = pos;
				pos += size;
			} else {
				int newpos = t->verdict;

				if (strcmp(t->target.u.user.name,
					   IP6T_STANDARD_TARGET) == 0
				    && newpos >= 0) {
					if (newpos > newinfo->size -
						sizeof(struct ip6t_entry)) {
						duprintf("mark_source_chains: "
							"bad verdict (%i)\n",
								newpos);
						return 0;
					}
					/* This a jump; chase it. */
					duprintf("Jump rule %u -> %u\n",
						 pos, newpos);
				} else {
					/* ... this is a fallthru */
					newpos = pos + e->next_offset;
				}
				e = (struct ip6t_entry *)
					(entry0 + newpos);
				e->counters.pcnt = pos;
				pos = newpos;
			}
		}
		next:
		duprintf("Finished chain %u\n", hook);
	}
	return 1;
}

static int
cleanup_match(struct ip6t_entry_match *m, unsigned int *i)
{
	struct xt_mtdtor_param par;

	if (i && (*i)-- == 0)
		return 1;

	par.match     = m->u.kernel.match;
	par.matchinfo = m->data;
	par.family    = NFPROTO_IPV6;
	if (par.match->destroy != NULL)
		par.match->destroy(&par);
	module_put(par.match->me);
	return 0;
}

static int
check_entry(struct ip6t_entry *e, const char *name)
{
	struct ip6t_entry_target *t;

	if (!ip6_checkentry(&e->ipv6)) {
		duprintf("ip_tables: ip check failed %p %s.\n", e, name);
		return -EINVAL;
	}

	if (e->target_offset + sizeof(struct ip6t_entry_target) >
	    e->next_offset)
		return -EINVAL;

	t = ip6t_get_target(e);
	if (e->target_offset + t->u.target_size > e->next_offset)
		return -EINVAL;

	return 0;
}

static int check_match(struct ip6t_entry_match *m, struct xt_mtchk_param *par,
		       unsigned int *i)
{
	const struct ip6t_ip6 *ipv6 = par->entryinfo;
	int ret;

	par->match     = m->u.kernel.match;
	par->matchinfo = m->data;

	ret = xt_check_match(par, m->u.match_size - sizeof(*m),
			     ipv6->proto, ipv6->invflags & IP6T_INV_PROTO);
	if (ret < 0) {
		duprintf("ip_tables: check failed for `%s'.\n",
			 par.match->name);
		return ret;
	}
	++*i;
	return 0;
}

static int
find_check_match(struct ip6t_entry_match *m, struct xt_mtchk_param *par,
		 unsigned int *i)
{
	struct xt_match *match;
	int ret;

	match = try_then_request_module(xt_find_match(AF_INET6, m->u.user.name,
						      m->u.user.revision),
					"ip6t_%s", m->u.user.name);
	if (IS_ERR(match) || !match) {
		duprintf("find_check_match: `%s' not found\n", m->u.user.name);
		return match ? PTR_ERR(match) : -ENOENT;
	}
	m->u.kernel.match = match;

	ret = check_match(m, par, i);
	if (ret)
		goto err;

	return 0;
err:
	module_put(m->u.kernel.match->me);
	return ret;
}

static int check_target(struct ip6t_entry *e, const char *name)
{
	struct ip6t_entry_target *t = ip6t_get_target(e);
	struct xt_tgchk_param par = {
		.table     = name,
		.entryinfo = e,
		.target    = t->u.kernel.target,
		.targinfo  = t->data,
		.hook_mask = e->comefrom,
		.family    = NFPROTO_IPV6,
	};
	int ret;

	t = ip6t_get_target(e);
	ret = xt_check_target(&par, t->u.target_size - sizeof(*t),
	      e->ipv6.proto, e->ipv6.invflags & IP6T_INV_PROTO);
	if (ret < 0) {
		duprintf("ip_tables: check failed for `%s'.\n",
			 t->u.kernel.target->name);
		return ret;
	}
	return 0;
}

static int
find_check_entry(struct ip6t_entry *e, const char *name, unsigned int size,
		 unsigned int *i)
{
	struct ip6t_entry_target *t;
	struct xt_target *target;
	int ret;
	unsigned int j;
	struct xt_mtchk_param mtpar;

	ret = check_entry(e, name);
	if (ret)
		return ret;

	j = 0;
	mtpar.table     = name;
	mtpar.entryinfo = &e->ipv6;
	mtpar.hook_mask = e->comefrom;
	mtpar.family    = NFPROTO_IPV6;
	ret = IP6T_MATCH_ITERATE(e, find_check_match, &mtpar, &j);
	if (ret != 0)
		goto cleanup_matches;

	t = ip6t_get_target(e);
	target = try_then_request_module(xt_find_target(AF_INET6,
							t->u.user.name,
							t->u.user.revision),
					 "ip6t_%s", t->u.user.name);
	if (IS_ERR(target) || !target) {
		duprintf("find_check_entry: `%s' not found\n", t->u.user.name);
		ret = target ? PTR_ERR(target) : -ENOENT;
		goto cleanup_matches;
	}
	t->u.kernel.target = target;

	ret = check_target(e, name);
	if (ret)
		goto err;

	(*i)++;
	return 0;
 err:
	module_put(t->u.kernel.target->me);
 cleanup_matches:
	IP6T_MATCH_ITERATE(e, cleanup_match, &j);
	return ret;
}

static int
check_entry_size_and_hooks(struct ip6t_entry *e,
			   struct xt_table_info *newinfo,
			   unsigned char *base,
			   unsigned char *limit,
			   const unsigned int *hook_entries,
			   const unsigned int *underflows,
			   unsigned int *i)
{
	unsigned int h;

	if ((unsigned long)e % __alignof__(struct ip6t_entry) != 0
	    || (unsigned char *)e + sizeof(struct ip6t_entry) >= limit) {
		duprintf("Bad offset %p\n", e);
		return -EINVAL;
	}

	if (e->next_offset
	    < sizeof(struct ip6t_entry) + sizeof(struct ip6t_entry_target)) {
		duprintf("checking: element %p size %u\n",
			 e, e->next_offset);
		return -EINVAL;
	}

	/* Check hooks & underflows */
	for (h = 0; h < NF_INET_NUMHOOKS; h++) {
		if ((unsigned char *)e - base == hook_entries[h])
			newinfo->hook_entry[h] = hook_entries[h];
		if ((unsigned char *)e - base == underflows[h])
			newinfo->underflow[h] = underflows[h];
	}

	/* FIXME: underflows must be unconditional, standard verdicts
	   < 0 (not IP6T_RETURN). --RR */

	/* Clear counters and comefrom */
	e->counters = ((struct xt_counters) { 0, 0 });
	e->comefrom = 0;

	(*i)++;
	return 0;
}

static int
cleanup_entry(struct ip6t_entry *e, unsigned int *i)
{
	struct xt_tgdtor_param par;
	struct ip6t_entry_target *t;

	if (i && (*i)-- == 0)
		return 1;

	/* Cleanup all matches */
	IP6T_MATCH_ITERATE(e, cleanup_match, NULL);
	t = ip6t_get_target(e);

	par.target   = t->u.kernel.target;
	par.targinfo = t->data;
	par.family   = NFPROTO_IPV6;
	if (par.target->destroy != NULL)
		par.target->destroy(&par);
	module_put(par.target->me);
	return 0;
}

/* Checks and translates the user-supplied table segment (held in
   newinfo) */
static int
translate_table(const char *name,
		unsigned int valid_hooks,
		struct xt_table_info *newinfo,
		void *entry0,
		unsigned int size,
		unsigned int number,
		const unsigned int *hook_entries,
		const unsigned int *underflows)
{
	unsigned int i;
	int ret;

	newinfo->size = size;
	newinfo->number = number;

	/* Init all hooks to impossible value. */
	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = 0xFFFFFFFF;
		newinfo->underflow[i] = 0xFFFFFFFF;
	}

	duprintf("translate_table: size %u\n", newinfo->size);
	i = 0;
	/* Walk through entries, checking offsets. */
	ret = IP6T_ENTRY_ITERATE(entry0, newinfo->size,
				check_entry_size_and_hooks,
				newinfo,
				entry0,
				entry0 + size,
				hook_entries, underflows, &i);
	if (ret != 0)
		return ret;

	if (i != number) {
		duprintf("translate_table: %u not %u entries\n",
			 i, number);
		return -EINVAL;
	}

	/* Check hooks all assigned */
	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		/* Only hooks which are valid */
		if (!(valid_hooks & (1 << i)))
			continue;
		if (newinfo->hook_entry[i] == 0xFFFFFFFF) {
			duprintf("Invalid hook entry %u %u\n",
				 i, hook_entries[i]);
			return -EINVAL;
		}
		if (newinfo->underflow[i] == 0xFFFFFFFF) {
			duprintf("Invalid underflow %u %u\n",
				 i, underflows[i]);
			return -EINVAL;
		}
	}

	if (!mark_source_chains(newinfo, valid_hooks, entry0))
		return -ELOOP;

	/* Finally, each sanity check must pass */
	i = 0;
	ret = IP6T_ENTRY_ITERATE(entry0, newinfo->size,
				find_check_entry, name, size, &i);

	if (ret != 0) {
		IP6T_ENTRY_ITERATE(entry0, newinfo->size,
				   cleanup_entry, &i);
		return ret;
	}

	/* And one copy for every other CPU */
	for_each_possible_cpu(i) {
		if (newinfo->entries[i] && newinfo->entries[i] != entry0)
			memcpy(newinfo->entries[i], entry0, newinfo->size);
	}

	return ret;
}

/* Gets counters. */
static inline int
add_entry_to_counter(const struct ip6t_entry *e,
		     struct xt_counters total[],
		     unsigned int *i)
{
	ADD_COUNTER(total[*i], e->counters.bcnt, e->counters.pcnt);

	(*i)++;
	return 0;
}

static inline int
set_entry_to_counter(const struct ip6t_entry *e,
		     struct ip6t_counters total[],
		     unsigned int *i)
{
	SET_COUNTER(total[*i], e->counters.bcnt, e->counters.pcnt);

	(*i)++;
	return 0;
}

static void
get_counters(const struct xt_table_info *t,
	     struct xt_counters counters[])
{
	unsigned int cpu;
	unsigned int i;
	unsigned int curcpu;

	/* Instead of clearing (by a previous call to memset())
	 * the counters and using adds, we set the counters
	 * with data used by 'current' CPU
	 * We dont care about preemption here.
	 */
	curcpu = raw_smp_processor_id();

	i = 0;
	IP6T_ENTRY_ITERATE(t->entries[curcpu],
			   t->size,
			   set_entry_to_counter,
			   counters,
			   &i);

	for_each_possible_cpu(cpu) {
		if (cpu == curcpu)
			continue;
		i = 0;
		IP6T_ENTRY_ITERATE(t->entries[cpu],
				  t->size,
				  add_entry_to_counter,
				  counters,
				  &i);
	}
}

/* We're lazy, and add to the first CPU; overflow works its fey magic
 * and everything is OK. */
static int
add_counter_to_entry(struct ip6t_entry *e,
		     const struct xt_counters addme[],
		     unsigned int *i)
{
	ADD_COUNTER(e->counters, addme[*i].bcnt, addme[*i].pcnt);

	(*i)++;
	return 0;
}

/* Take values from counters and add them back onto the current cpu */
static void put_counters(struct xt_table_info *t,
			 const struct xt_counters counters[])
{
	unsigned int i, cpu;

	local_bh_disable();
	cpu = smp_processor_id();
	i = 0;
	IP6T_ENTRY_ITERATE(t->entries[cpu],
			   t->size,
			   add_counter_to_entry,
			   counters,
			   &i);
	local_bh_enable();
}

static inline int
zero_entry_counter(struct ip6t_entry *e, void *arg)
{
	e->counters.bcnt = 0;
	e->counters.pcnt = 0;
	return 0;
}

static void
clone_counters(struct xt_table_info *newinfo, const struct xt_table_info *info)
{
	unsigned int cpu;
	const void *loc_cpu_entry = info->entries[raw_smp_processor_id()];

	memcpy(newinfo, info, offsetof(struct xt_table_info, entries));
	for_each_possible_cpu(cpu) {
		memcpy(newinfo->entries[cpu], loc_cpu_entry, info->size);
		IP6T_ENTRY_ITERATE(newinfo->entries[cpu], newinfo->size,
				   zero_entry_counter, NULL);
	}
}

static struct xt_counters *alloc_counters(struct xt_table *table)
{
	unsigned int countersize;
	struct xt_counters *counters;
	struct xt_table_info *private = table->private;
	struct xt_table_info *info;

	/* We need atomic snapshot of counters: rest doesn't change
	   (other than comefrom, which userspace doesn't care
	   about). */
	countersize = sizeof(struct xt_counters) * private->number;
	counters = vmalloc_node(countersize, numa_node_id());

	if (counters == NULL)
		goto nomem;

	info = xt_alloc_table_info(private->size);
	if (!info)
		goto free_counters;

	clone_counters(info, private);

	mutex_lock(&table->lock);
	xt_table_entry_swap_rcu(private, info);
	synchronize_net();	/* Wait until smoke has cleared */

	get_counters(info, counters);
	put_counters(private, counters);
	mutex_unlock(&table->lock);

	xt_free_table_info(info);

 free_counters:
	vfree(counters);
 nomem:
	return ERR_PTR(-ENOMEM);
}

static int
copy_entries_to_user(unsigned int total_size,
		     struct xt_table *table,
		     void __user *userptr)
{
	unsigned int off, num;
	struct ip6t_entry *e;
	struct xt_counters *counters;
	const struct xt_table_info *private = table->private;
	int ret = 0;
	const void *loc_cpu_entry;

	counters = alloc_counters(table);
	if (IS_ERR(counters))
		return PTR_ERR(counters);

	/* choose the copy that is on our node/cpu, ...
	 * This choice is lazy (because current thread is
	 * allowed to migrate to another cpu)
	 */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	if (copy_to_user(userptr, loc_cpu_entry, total_size) != 0) {
		ret = -EFAULT;
		goto free_counters;
	}

	/* FIXME: use iterator macros --RR */
	/* ... then go back and fix counters and names */
	for (off = 0, num = 0; off < total_size; off += e->next_offset, num++){
		unsigned int i;
		const struct ip6t_entry_match *m;
		const struct ip6t_entry_target *t;

		e = (struct ip6t_entry *)(loc_cpu_entry + off);
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

			if (copy_to_user(userptr + off + i
					 + offsetof(struct ip6t_entry_match,
						    u.user.name),
					 m->u.kernel.match->name,
					 strlen(m->u.kernel.match->name)+1)
			    != 0) {
				ret = -EFAULT;
				goto free_counters;
			}
		}

		t = ip6t_get_target(e);
		if (copy_to_user(userptr + off + e->target_offset
				 + offsetof(struct ip6t_entry_target,
					    u.user.name),
				 t->u.kernel.target->name,
				 strlen(t->u.kernel.target->name)+1) != 0) {
			ret = -EFAULT;
			goto free_counters;
		}
	}

 free_counters:
	vfree(counters);
	return ret;
}

#ifdef CONFIG_COMPAT
static void compat_standard_from_user(void *dst, void *src)
{
	int v = *(compat_int_t *)src;

	if (v > 0)
		v += xt_compat_calc_jump(AF_INET6, v);
	memcpy(dst, &v, sizeof(v));
}

static int compat_standard_to_user(void __user *dst, void *src)
{
	compat_int_t cv = *(int *)src;

	if (cv > 0)
		cv -= xt_compat_calc_jump(AF_INET6, cv);
	return copy_to_user(dst, &cv, sizeof(cv)) ? -EFAULT : 0;
}

static inline int
compat_calc_match(struct ip6t_entry_match *m, int *size)
{
	*size += xt_compat_match_offset(m->u.kernel.match);
	return 0;
}

static int compat_calc_entry(struct ip6t_entry *e,
			     const struct xt_table_info *info,
			     void *base, struct xt_table_info *newinfo)
{
	struct ip6t_entry_target *t;
	unsigned int entry_offset;
	int off, i, ret;

	off = sizeof(struct ip6t_entry) - sizeof(struct compat_ip6t_entry);
	entry_offset = (void *)e - base;
	IP6T_MATCH_ITERATE(e, compat_calc_match, &off);
	t = ip6t_get_target(e);
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
	void *loc_cpu_entry;

	if (!newinfo || !info)
		return -EINVAL;

	/* we dont care about newinfo->entries[] */
	memcpy(newinfo, info, offsetof(struct xt_table_info, entries));
	newinfo->initial_entries = 0;
	loc_cpu_entry = info->entries[raw_smp_processor_id()];
	return IP6T_ENTRY_ITERATE(loc_cpu_entry, info->size,
				  compat_calc_entry, info, loc_cpu_entry,
				  newinfo);
}
#endif

static int get_info(struct net *net, void __user *user, int *len, int compat)
{
	char name[IP6T_TABLE_MAXNAMELEN];
	struct xt_table *t;
	int ret;

	if (*len != sizeof(struct ip6t_getinfo)) {
		duprintf("length %u != %zu\n", *len,
			 sizeof(struct ip6t_getinfo));
		return -EINVAL;
	}

	if (copy_from_user(name, user, sizeof(name)) != 0)
		return -EFAULT;

	name[IP6T_TABLE_MAXNAMELEN-1] = '\0';
#ifdef CONFIG_COMPAT
	if (compat)
		xt_compat_lock(AF_INET6);
#endif
	t = try_then_request_module(xt_find_table_lock(net, AF_INET6, name),
				    "ip6table_%s", name);
	if (t && !IS_ERR(t)) {
		struct ip6t_getinfo info;
		const struct xt_table_info *private = t->private;

#ifdef CONFIG_COMPAT
		if (compat) {
			struct xt_table_info tmp;
			ret = compat_table_info(private, &tmp);
			xt_compat_flush_offsets(AF_INET6);
			private = &tmp;
		}
#endif
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
		ret = t ? PTR_ERR(t) : -ENOENT;
#ifdef CONFIG_COMPAT
	if (compat)
		xt_compat_unlock(AF_INET6);
#endif
	return ret;
}

static int
get_entries(struct net *net, struct ip6t_get_entries __user *uptr, int *len)
{
	int ret;
	struct ip6t_get_entries get;
	struct xt_table *t;

	if (*len < sizeof(get)) {
		duprintf("get_entries: %u < %zu\n", *len, sizeof(get));
		return -EINVAL;
	}
	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;
	if (*len != sizeof(struct ip6t_get_entries) + get.size) {
		duprintf("get_entries: %u != %zu\n",
			 *len, sizeof(get) + get.size);
		return -EINVAL;
	}

	t = xt_find_table_lock(net, AF_INET6, get.name);
	if (t && !IS_ERR(t)) {
		struct xt_table_info *private = t->private;
		duprintf("t->private->number = %u\n", private->number);
		if (get.size == private->size)
			ret = copy_entries_to_user(private->size,
						   t, uptr->entrytable);
		else {
			duprintf("get_entries: I've got %u not %u!\n",
				 private->size, get.size);
			ret = -EAGAIN;
		}
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = t ? PTR_ERR(t) : -ENOENT;

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
	const void *loc_cpu_old_entry;

	ret = 0;
	counters = vmalloc_node(num_counters * sizeof(struct xt_counters),
				numa_node_id());
	if (!counters) {
		ret = -ENOMEM;
		goto out;
	}

	t = try_then_request_module(xt_find_table_lock(net, AF_INET6, name),
				    "ip6table_%s", name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free_newinfo_counters_untrans;
	}

	/* You lied! */
	if (valid_hooks != t->valid_hooks) {
		duprintf("Valid hook crap: %08X vs %08X\n",
			 valid_hooks, t->valid_hooks);
		ret = -EINVAL;
		goto put_module;
	}

	oldinfo = xt_replace_table(t, num_counters, newinfo, &ret);
	if (!oldinfo)
		goto put_module;

	/* Update module usage count based on number of rules */
	duprintf("do_replace: oldnum=%u, initnum=%u, newnum=%u\n",
		oldinfo->number, oldinfo->initial_entries, newinfo->number);
	if ((oldinfo->number > oldinfo->initial_entries) ||
	    (newinfo->number <= oldinfo->initial_entries))
		module_put(t->me);
	if ((oldinfo->number > oldinfo->initial_entries) &&
	    (newinfo->number <= oldinfo->initial_entries))
		module_put(t->me);

	/* Get the old counters. */
	get_counters(oldinfo, counters);
	/* Decrease module usage counts and free resource */
	loc_cpu_old_entry = oldinfo->entries[raw_smp_processor_id()];
	IP6T_ENTRY_ITERATE(loc_cpu_old_entry, oldinfo->size, cleanup_entry,
			   NULL);
	xt_free_table_info(oldinfo);
	if (copy_to_user(counters_ptr, counters,
			 sizeof(struct xt_counters) * num_counters) != 0)
		ret = -EFAULT;
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
do_replace(struct net *net, void __user *user, unsigned int len)
{
	int ret;
	struct ip6t_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* overflow check */
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	/* choose the copy that is on our node/cpu */
	loc_cpu_entry = newinfo->entries[raw_smp_processor_id()];
	if (copy_from_user(loc_cpu_entry, user + sizeof(tmp),
			   tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	ret = translate_table(tmp.name, tmp.valid_hooks,
			      newinfo, loc_cpu_entry, tmp.size, tmp.num_entries,
			      tmp.hook_entry, tmp.underflow);
	if (ret != 0)
		goto free_newinfo;

	duprintf("ip_tables: Translated table\n");

	ret = __do_replace(net, tmp.name, tmp.valid_hooks, newinfo,
			   tmp.num_counters, tmp.counters);
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	IP6T_ENTRY_ITERATE(loc_cpu_entry, newinfo->size, cleanup_entry, NULL);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

static int
do_add_counters(struct net *net, void __user *user, unsigned int len,
		int compat)
{
	unsigned int i;
	struct xt_counters_info tmp;
	struct xt_counters *paddc;
	unsigned int num_counters;
	char *name;
	int size;
	void *ptmp;
	struct xt_table *t;
	const struct xt_table_info *private;
	int ret = 0;
	const void *loc_cpu_entry;
#ifdef CONFIG_COMPAT
	struct compat_xt_counters_info compat_tmp;

	if (compat) {
		ptmp = &compat_tmp;
		size = sizeof(struct compat_xt_counters_info);
	} else
#endif
	{
		ptmp = &tmp;
		size = sizeof(struct xt_counters_info);
	}

	if (copy_from_user(ptmp, user, size) != 0)
		return -EFAULT;

#ifdef CONFIG_COMPAT
	if (compat) {
		num_counters = compat_tmp.num_counters;
		name = compat_tmp.name;
	} else
#endif
	{
		num_counters = tmp.num_counters;
		name = tmp.name;
	}

	if (len != size + num_counters * sizeof(struct xt_counters))
		return -EINVAL;

	paddc = vmalloc_node(len - size, numa_node_id());
	if (!paddc)
		return -ENOMEM;

	if (copy_from_user(paddc, user + size, len - size) != 0) {
		ret = -EFAULT;
		goto free;
	}

	t = xt_find_table_lock(net, AF_INET6, name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free;
	}

	mutex_lock(&t->lock);
	private = t->private;
	if (private->number != num_counters) {
		ret = -EINVAL;
		goto unlock_up_free;
	}

	preempt_disable();
	i = 0;
	/* Choose the copy that is on our node */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	IP6T_ENTRY_ITERATE(loc_cpu_entry,
			  private->size,
			  add_counter_to_entry,
			  paddc,
			  &i);
	preempt_enable();
 unlock_up_free:
	mutex_unlock(&t->lock);
	xt_table_unlock(t);
	module_put(t->me);
 free:
	vfree(paddc);

	return ret;
}

#ifdef CONFIG_COMPAT
struct compat_ip6t_replace {
	char			name[IP6T_TABLE_MAXNAMELEN];
	u32			valid_hooks;
	u32			num_entries;
	u32			size;
	u32			hook_entry[NF_INET_NUMHOOKS];
	u32			underflow[NF_INET_NUMHOOKS];
	u32			num_counters;
	compat_uptr_t		counters;	/* struct ip6t_counters * */
	struct compat_ip6t_entry entries[0];
};

static int
compat_copy_entry_to_user(struct ip6t_entry *e, void __user **dstptr,
			  unsigned int *size, struct xt_counters *counters,
			  unsigned int *i)
{
	struct ip6t_entry_target *t;
	struct compat_ip6t_entry __user *ce;
	u_int16_t target_offset, next_offset;
	compat_uint_t origsize;
	int ret;

	ret = -EFAULT;
	origsize = *size;
	ce = (struct compat_ip6t_entry __user *)*dstptr;
	if (copy_to_user(ce, e, sizeof(struct ip6t_entry)))
		goto out;

	if (copy_to_user(&ce->counters, &counters[*i], sizeof(counters[*i])))
		goto out;

	*dstptr += sizeof(struct compat_ip6t_entry);
	*size -= sizeof(struct ip6t_entry) - sizeof(struct compat_ip6t_entry);

	ret = IP6T_MATCH_ITERATE(e, xt_compat_match_to_user, dstptr, size);
	target_offset = e->target_offset - (origsize - *size);
	if (ret)
		goto out;
	t = ip6t_get_target(e);
	ret = xt_compat_target_to_user(t, dstptr, size);
	if (ret)
		goto out;
	ret = -EFAULT;
	next_offset = e->next_offset - (origsize - *size);
	if (put_user(target_offset, &ce->target_offset))
		goto out;
	if (put_user(next_offset, &ce->next_offset))
		goto out;

	(*i)++;
	return 0;
out:
	return ret;
}

static int
compat_find_calc_match(struct ip6t_entry_match *m,
		       const char *name,
		       const struct ip6t_ip6 *ipv6,
		       unsigned int hookmask,
		       int *size, unsigned int *i)
{
	struct xt_match *match;

	match = try_then_request_module(xt_find_match(AF_INET6, m->u.user.name,
						      m->u.user.revision),
					"ip6t_%s", m->u.user.name);
	if (IS_ERR(match) || !match) {
		duprintf("compat_check_calc_match: `%s' not found\n",
			 m->u.user.name);
		return match ? PTR_ERR(match) : -ENOENT;
	}
	m->u.kernel.match = match;
	*size += xt_compat_match_offset(match);

	(*i)++;
	return 0;
}

static int
compat_release_match(struct ip6t_entry_match *m, unsigned int *i)
{
	if (i && (*i)-- == 0)
		return 1;

	module_put(m->u.kernel.match->me);
	return 0;
}

static int
compat_release_entry(struct compat_ip6t_entry *e, unsigned int *i)
{
	struct ip6t_entry_target *t;

	if (i && (*i)-- == 0)
		return 1;

	/* Cleanup all matches */
	COMPAT_IP6T_MATCH_ITERATE(e, compat_release_match, NULL);
	t = compat_ip6t_get_target(e);
	module_put(t->u.kernel.target->me);
	return 0;
}

static int
check_compat_entry_size_and_hooks(struct compat_ip6t_entry *e,
				  struct xt_table_info *newinfo,
				  unsigned int *size,
				  unsigned char *base,
				  unsigned char *limit,
				  unsigned int *hook_entries,
				  unsigned int *underflows,
				  unsigned int *i,
				  const char *name)
{
	struct ip6t_entry_target *t;
	struct xt_target *target;
	unsigned int entry_offset;
	unsigned int j;
	int ret, off, h;

	duprintf("check_compat_entry_size_and_hooks %p\n", e);
	if ((unsigned long)e % __alignof__(struct compat_ip6t_entry) != 0
	    || (unsigned char *)e + sizeof(struct compat_ip6t_entry) >= limit) {
		duprintf("Bad offset %p, limit = %p\n", e, limit);
		return -EINVAL;
	}

	if (e->next_offset < sizeof(struct compat_ip6t_entry) +
			     sizeof(struct compat_xt_entry_target)) {
		duprintf("checking: element %p size %u\n",
			 e, e->next_offset);
		return -EINVAL;
	}

	/* For purposes of check_entry casting the compat entry is fine */
	ret = check_entry((struct ip6t_entry *)e, name);
	if (ret)
		return ret;

	off = sizeof(struct ip6t_entry) - sizeof(struct compat_ip6t_entry);
	entry_offset = (void *)e - (void *)base;
	j = 0;
	ret = COMPAT_IP6T_MATCH_ITERATE(e, compat_find_calc_match, name,
					&e->ipv6, e->comefrom, &off, &j);
	if (ret != 0)
		goto release_matches;

	t = compat_ip6t_get_target(e);
	target = try_then_request_module(xt_find_target(AF_INET6,
							t->u.user.name,
							t->u.user.revision),
					 "ip6t_%s", t->u.user.name);
	if (IS_ERR(target) || !target) {
		duprintf("check_compat_entry_size_and_hooks: `%s' not found\n",
			 t->u.user.name);
		ret = target ? PTR_ERR(target) : -ENOENT;
		goto release_matches;
	}
	t->u.kernel.target = target;

	off += xt_compat_target_offset(target);
	*size += off;
	ret = xt_compat_add_offset(AF_INET6, entry_offset, off);
	if (ret)
		goto out;

	/* Check hooks & underflows */
	for (h = 0; h < NF_INET_NUMHOOKS; h++) {
		if ((unsigned char *)e - base == hook_entries[h])
			newinfo->hook_entry[h] = hook_entries[h];
		if ((unsigned char *)e - base == underflows[h])
			newinfo->underflow[h] = underflows[h];
	}

	/* Clear counters and comefrom */
	memset(&e->counters, 0, sizeof(e->counters));
	e->comefrom = 0;

	(*i)++;
	return 0;

out:
	module_put(t->u.kernel.target->me);
release_matches:
	IP6T_MATCH_ITERATE(e, compat_release_match, &j);
	return ret;
}

static int
compat_copy_entry_from_user(struct compat_ip6t_entry *e, void **dstptr,
			    unsigned int *size, const char *name,
			    struct xt_table_info *newinfo, unsigned char *base)
{
	struct ip6t_entry_target *t;
	struct xt_target *target;
	struct ip6t_entry *de;
	unsigned int origsize;
	int ret, h;

	ret = 0;
	origsize = *size;
	de = (struct ip6t_entry *)*dstptr;
	memcpy(de, e, sizeof(struct ip6t_entry));
	memcpy(&de->counters, &e->counters, sizeof(e->counters));

	*dstptr += sizeof(struct ip6t_entry);
	*size += sizeof(struct ip6t_entry) - sizeof(struct compat_ip6t_entry);

	ret = COMPAT_IP6T_MATCH_ITERATE(e, xt_compat_match_from_user,
					dstptr, size);
	if (ret)
		return ret;
	de->target_offset = e->target_offset - (origsize - *size);
	t = compat_ip6t_get_target(e);
	target = t->u.kernel.target;
	xt_compat_target_from_user(t, dstptr, size);

	de->next_offset = e->next_offset - (origsize - *size);
	for (h = 0; h < NF_INET_NUMHOOKS; h++) {
		if ((unsigned char *)de - base < newinfo->hook_entry[h])
			newinfo->hook_entry[h] -= origsize - *size;
		if ((unsigned char *)de - base < newinfo->underflow[h])
			newinfo->underflow[h] -= origsize - *size;
	}
	return ret;
}

static int compat_check_entry(struct ip6t_entry *e, const char *name,
				     unsigned int *i)
{
	unsigned int j;
	int ret;
	struct xt_mtchk_param mtpar;

	j = 0;
	mtpar.table     = name;
	mtpar.entryinfo = &e->ipv6;
	mtpar.hook_mask = e->comefrom;
	mtpar.family    = NFPROTO_IPV6;
	ret = IP6T_MATCH_ITERATE(e, check_match, &mtpar, &j);
	if (ret)
		goto cleanup_matches;

	ret = check_target(e, name);
	if (ret)
		goto cleanup_matches;

	(*i)++;
	return 0;

 cleanup_matches:
	IP6T_MATCH_ITERATE(e, cleanup_match, &j);
	return ret;
}

static int
translate_compat_table(const char *name,
		       unsigned int valid_hooks,
		       struct xt_table_info **pinfo,
		       void **pentry0,
		       unsigned int total_size,
		       unsigned int number,
		       unsigned int *hook_entries,
		       unsigned int *underflows)
{
	unsigned int i, j;
	struct xt_table_info *newinfo, *info;
	void *pos, *entry0, *entry1;
	unsigned int size;
	int ret;

	info = *pinfo;
	entry0 = *pentry0;
	size = total_size;
	info->number = number;

	/* Init all hooks to impossible value. */
	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		info->hook_entry[i] = 0xFFFFFFFF;
		info->underflow[i] = 0xFFFFFFFF;
	}

	duprintf("translate_compat_table: size %u\n", info->size);
	j = 0;
	xt_compat_lock(AF_INET6);
	/* Walk through entries, checking offsets. */
	ret = COMPAT_IP6T_ENTRY_ITERATE(entry0, total_size,
					check_compat_entry_size_and_hooks,
					info, &size, entry0,
					entry0 + total_size,
					hook_entries, underflows, &j, name);
	if (ret != 0)
		goto out_unlock;

	ret = -EINVAL;
	if (j != number) {
		duprintf("translate_compat_table: %u not %u entries\n",
			 j, number);
		goto out_unlock;
	}

	/* Check hooks all assigned */
	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		/* Only hooks which are valid */
		if (!(valid_hooks & (1 << i)))
			continue;
		if (info->hook_entry[i] == 0xFFFFFFFF) {
			duprintf("Invalid hook entry %u %u\n",
				 i, hook_entries[i]);
			goto out_unlock;
		}
		if (info->underflow[i] == 0xFFFFFFFF) {
			duprintf("Invalid underflow %u %u\n",
				 i, underflows[i]);
			goto out_unlock;
		}
	}

	ret = -ENOMEM;
	newinfo = xt_alloc_table_info(size);
	if (!newinfo)
		goto out_unlock;

	newinfo->number = number;
	for (i = 0; i < NF_INET_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = info->hook_entry[i];
		newinfo->underflow[i] = info->underflow[i];
	}
	entry1 = newinfo->entries[raw_smp_processor_id()];
	pos = entry1;
	size = total_size;
	ret = COMPAT_IP6T_ENTRY_ITERATE(entry0, total_size,
					compat_copy_entry_from_user,
					&pos, &size, name, newinfo, entry1);
	xt_compat_flush_offsets(AF_INET6);
	xt_compat_unlock(AF_INET6);
	if (ret)
		goto free_newinfo;

	ret = -ELOOP;
	if (!mark_source_chains(newinfo, valid_hooks, entry1))
		goto free_newinfo;

	i = 0;
	ret = IP6T_ENTRY_ITERATE(entry1, newinfo->size, compat_check_entry,
				 name, &i);
	if (ret) {
		j -= i;
		COMPAT_IP6T_ENTRY_ITERATE_CONTINUE(entry0, newinfo->size, i,
						   compat_release_entry, &j);
		IP6T_ENTRY_ITERATE(entry1, newinfo->size, cleanup_entry, &i);
		xt_free_table_info(newinfo);
		return ret;
	}

	/* And one copy for every other CPU */
	for_each_possible_cpu(i)
		if (newinfo->entries[i] && newinfo->entries[i] != entry1)
			memcpy(newinfo->entries[i], entry1, newinfo->size);

	*pinfo = newinfo;
	*pentry0 = entry1;
	xt_free_table_info(info);
	return 0;

free_newinfo:
	xt_free_table_info(newinfo);
out:
	COMPAT_IP6T_ENTRY_ITERATE(entry0, total_size, compat_release_entry, &j);
	return ret;
out_unlock:
	xt_compat_flush_offsets(AF_INET6);
	xt_compat_unlock(AF_INET6);
	goto out;
}

static int
compat_do_replace(struct net *net, void __user *user, unsigned int len)
{
	int ret;
	struct compat_ip6t_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* overflow check */
	if (tmp.size >= INT_MAX / num_possible_cpus())
		return -ENOMEM;
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	/* choose the copy that is on our node/cpu */
	loc_cpu_entry = newinfo->entries[raw_smp_processor_id()];
	if (copy_from_user(loc_cpu_entry, user + sizeof(tmp),
			   tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	ret = translate_compat_table(tmp.name, tmp.valid_hooks,
				     &newinfo, &loc_cpu_entry, tmp.size,
				     tmp.num_entries, tmp.hook_entry,
				     tmp.underflow);
	if (ret != 0)
		goto free_newinfo;

	duprintf("compat_do_replace: Translated table\n");

	ret = __do_replace(net, tmp.name, tmp.valid_hooks, newinfo,
			   tmp.num_counters, compat_ptr(tmp.counters));
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	IP6T_ENTRY_ITERATE(loc_cpu_entry, newinfo->size, cleanup_entry, NULL);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

static int
compat_do_ip6t_set_ctl(struct sock *sk, int cmd, void __user *user,
		       unsigned int len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IP6T_SO_SET_REPLACE:
		ret = compat_do_replace(sock_net(sk), user, len);
		break;

	case IP6T_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(sock_net(sk), user, len, 1);
		break;

	default:
		duprintf("do_ip6t_set_ctl:  unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

struct compat_ip6t_get_entries {
	char name[IP6T_TABLE_MAXNAMELEN];
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
	const void *loc_cpu_entry;
	unsigned int i = 0;

	counters = alloc_counters(table);
	if (IS_ERR(counters))
		return PTR_ERR(counters);

	/* choose the copy that is on our node/cpu, ...
	 * This choice is lazy (because current thread is
	 * allowed to migrate to another cpu)
	 */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	pos = userptr;
	size = total_size;
	ret = IP6T_ENTRY_ITERATE(loc_cpu_entry, total_size,
				 compat_copy_entry_to_user,
				 &pos, &size, counters, &i);

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

	if (*len < sizeof(get)) {
		duprintf("compat_get_entries: %u < %zu\n", *len, sizeof(get));
		return -EINVAL;
	}

	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;

	if (*len != sizeof(struct compat_ip6t_get_entries) + get.size) {
		duprintf("compat_get_entries: %u != %zu\n",
			 *len, sizeof(get) + get.size);
		return -EINVAL;
	}

	xt_compat_lock(AF_INET6);
	t = xt_find_table_lock(net, AF_INET6, get.name);
	if (t && !IS_ERR(t)) {
		const struct xt_table_info *private = t->private;
		struct xt_table_info info;
		duprintf("t->private->number = %u\n", private->number);
		ret = compat_table_info(private, &info);
		if (!ret && get.size == info.size) {
			ret = compat_copy_entries_to_user(private->size,
							  t, uptr->entrytable);
		} else if (!ret) {
			duprintf("compat_get_entries: I've got %u not %u!\n",
				 private->size, get.size);
			ret = -EAGAIN;
		}
		xt_compat_flush_offsets(AF_INET6);
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = t ? PTR_ERR(t) : -ENOENT;

	xt_compat_unlock(AF_INET6);
	return ret;
}

static int do_ip6t_get_ctl(struct sock *, int, void __user *, int *);

static int
compat_do_ip6t_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
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

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IP6T_SO_SET_REPLACE:
		ret = do_replace(sock_net(sk), user, len);
		break;

	case IP6T_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(sock_net(sk), user, len, 0);
		break;

	default:
		duprintf("do_ip6t_set_ctl:  unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

static int
do_ip6t_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
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
		struct ip6t_get_revision rev;
		int target;

		if (*len != sizeof(rev)) {
			ret = -EINVAL;
			break;
		}
		if (copy_from_user(&rev, user, sizeof(rev)) != 0) {
			ret = -EFAULT;
			break;
		}

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
		duprintf("do_ip6t_get_ctl: unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

struct xt_table *ip6t_register_table(struct net *net, struct xt_table *table,
				     const struct ip6t_replace *repl)
{
	int ret;
	struct xt_table_info *newinfo;
	struct xt_table_info bootstrap
		= { 0, 0, 0, { 0 }, { 0 }, { } };
	void *loc_cpu_entry;
	struct xt_table *new_table;

	newinfo = xt_alloc_table_info(repl->size);
	if (!newinfo) {
		ret = -ENOMEM;
		goto out;
	}

	/* choose the copy on our node/cpu, but dont care about preemption */
	loc_cpu_entry = newinfo->entries[raw_smp_processor_id()];
	memcpy(loc_cpu_entry, repl->entries, repl->size);

	ret = translate_table(table->name, table->valid_hooks,
			      newinfo, loc_cpu_entry, repl->size,
			      repl->num_entries,
			      repl->hook_entry,
			      repl->underflow);
	if (ret != 0)
		goto out_free;

	new_table = xt_register_table(net, table, &bootstrap, newinfo);
	if (IS_ERR(new_table)) {
		ret = PTR_ERR(new_table);
		goto out_free;
	}
	return new_table;

out_free:
	xt_free_table_info(newinfo);
out:
	return ERR_PTR(ret);
}

void ip6t_unregister_table(struct xt_table *table)
{
	struct xt_table_info *private;
	void *loc_cpu_entry;
	struct module *table_owner = table->me;

	private = xt_unregister_table(table);

	/* Decrease module usage counts and free resources */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	IP6T_ENTRY_ITERATE(loc_cpu_entry, private->size, cleanup_entry, NULL);
	if (private->number > private->initial_entries)
		module_put(table_owner);
	xt_free_table_info(private);
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
icmp6_match(const struct sk_buff *skb, const struct xt_match_param *par)
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
		duprintf("Dropping evil ICMP tinygram.\n");
		*par->hotdrop = true;
		return false;
	}

	return icmp6_type_code_match(icmpinfo->type,
				     icmpinfo->code[0],
				     icmpinfo->code[1],
				     ic->icmp6_type, ic->icmp6_code,
				     !!(icmpinfo->invflags&IP6T_ICMP_INV));
}

/* Called when user tries to insert an entry of this type. */
static bool icmp6_checkentry(const struct xt_mtchk_param *par)
{
	const struct ip6t_icmp *icmpinfo = par->matchinfo;

	/* Must specify no unknown invflags */
	return !(icmpinfo->invflags & ~IP6T_ICMP_INV);
}

/* The built-in targets: standard (NULL) and error. */
static struct xt_target ip6t_standard_target __read_mostly = {
	.name		= IP6T_STANDARD_TARGET,
	.targetsize	= sizeof(int),
	.family		= AF_INET6,
#ifdef CONFIG_COMPAT
	.compatsize	= sizeof(compat_int_t),
	.compat_from_user = compat_standard_from_user,
	.compat_to_user	= compat_standard_to_user,
#endif
};

static struct xt_target ip6t_error_target __read_mostly = {
	.name		= IP6T_ERROR_TARGET,
	.target		= ip6t_error,
	.targetsize	= IP6T_FUNCTION_MAXNAMELEN,
	.family		= AF_INET6,
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

static struct xt_match icmp6_matchstruct __read_mostly = {
	.name		= "icmp6",
	.match		= icmp6_match,
	.matchsize	= sizeof(struct ip6t_icmp),
	.checkentry	= icmp6_checkentry,
	.proto		= IPPROTO_ICMPV6,
	.family		= AF_INET6,
};

static int __net_init ip6_tables_net_init(struct net *net)
{
	return xt_proto_init(net, AF_INET6);
}

static void __net_exit ip6_tables_net_exit(struct net *net)
{
	xt_proto_fini(net, AF_INET6);
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

	/* Noone else will be downing sem now, so we won't sleep */
	ret = xt_register_target(&ip6t_standard_target);
	if (ret < 0)
		goto err2;
	ret = xt_register_target(&ip6t_error_target);
	if (ret < 0)
		goto err3;
	ret = xt_register_match(&icmp6_matchstruct);
	if (ret < 0)
		goto err4;

	/* Register setsockopt */
	ret = nf_register_sockopt(&ip6t_sockopts);
	if (ret < 0)
		goto err5;

	printk(KERN_INFO "ip6_tables: (C) 2000-2006 Netfilter Core Team\n");
	return 0;

err5:
	xt_unregister_match(&icmp6_matchstruct);
err4:
	xt_unregister_target(&ip6t_error_target);
err3:
	xt_unregister_target(&ip6t_standard_target);
err2:
	unregister_pernet_subsys(&ip6_tables_net_ops);
err1:
	return ret;
}

static void __exit ip6_tables_fini(void)
{
	nf_unregister_sockopt(&ip6t_sockopts);

	xt_unregister_match(&icmp6_matchstruct);
	xt_unregister_target(&ip6t_error_target);
	xt_unregister_target(&ip6t_standard_target);

	unregister_pernet_subsys(&ip6_tables_net_ops);
}

/*
 * find the offset to specified header or the protocol number of last header
 * if target < 0. "last header" is transport protocol header, ESP, or
 * "No next header".
 *
 * If target header is found, its offset is set in *offset and return protocol
 * number. Otherwise, return -1.
 *
 * If the first fragment doesn't contain the final protocol header or
 * NEXTHDR_NONE it is considered invalid.
 *
 * Note that non-1st fragment is special case that "the protocol number
 * of last header" is "next header" field in Fragment header. In this case,
 * *offset is meaningless and fragment offset is stored in *fragoff if fragoff
 * isn't NULL.
 *
 */
int ipv6_find_hdr(const struct sk_buff *skb, unsigned int *offset,
		  int target, unsigned short *fragoff)
{
	unsigned int start = skb_network_offset(skb) + sizeof(struct ipv6hdr);
	u8 nexthdr = ipv6_hdr(skb)->nexthdr;
	unsigned int len = skb->len - start;

	if (fragoff)
		*fragoff = 0;

	while (nexthdr != target) {
		struct ipv6_opt_hdr _hdr, *hp;
		unsigned int hdrlen;

		if ((!ipv6_ext_hdr(nexthdr)) || nexthdr == NEXTHDR_NONE) {
			if (target < 0)
				break;
			return -ENOENT;
		}

		hp = skb_header_pointer(skb, start, sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return -EBADMSG;
		if (nexthdr == NEXTHDR_FRAGMENT) {
			unsigned short _frag_off;
			__be16 *fp;
			fp = skb_header_pointer(skb,
						start+offsetof(struct frag_hdr,
							       frag_off),
						sizeof(_frag_off),
						&_frag_off);
			if (fp == NULL)
				return -EBADMSG;

			_frag_off = ntohs(*fp) & ~0x7;
			if (_frag_off) {
				if (target < 0 &&
				    ((!ipv6_ext_hdr(hp->nexthdr)) ||
				     hp->nexthdr == NEXTHDR_NONE)) {
					if (fragoff)
						*fragoff = _frag_off;
					return hp->nexthdr;
				}
				return -ENOENT;
			}
			hdrlen = 8;
		} else if (nexthdr == NEXTHDR_AUTH)
			hdrlen = (hp->hdrlen + 2) << 2;
		else
			hdrlen = ipv6_optlen(hp);

		nexthdr = hp->nexthdr;
		len -= hdrlen;
		start += hdrlen;
	}

	*offset = start;
	return nexthdr;
}

EXPORT_SYMBOL(ip6t_register_table);
EXPORT_SYMBOL(ip6t_unregister_table);
EXPORT_SYMBOL(ip6t_do_table);
EXPORT_SYMBOL(ip6t_ext_hdr);
EXPORT_SYMBOL(ipv6_find_hdr);

module_init(ip6_tables_init);
module_exit(ip6_tables_fini);
