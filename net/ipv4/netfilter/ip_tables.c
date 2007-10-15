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
#include <linux/cache.h>
#include <linux/capability.h>
#include <linux/skbuff.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/icmp.h>
#include <net/ip.h>
#include <net/compat.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/err.h>
#include <linux/cpumask.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("IPv4 packet filter");

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
		       __FUNCTION__, __FILE__, __LINE__);	\
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

/* Returns whether matches rule or not. */
static inline int
ip_packet_match(const struct iphdr *ip,
		const char *indev,
		const char *outdev,
		const struct ipt_ip *ipinfo,
		int isfrag)
{
	size_t i;
	unsigned long ret;

#define FWINV(bool,invflg) ((bool) ^ !!(ipinfo->invflags & invflg))

	if (FWINV((ip->saddr&ipinfo->smsk.s_addr) != ipinfo->src.s_addr,
		  IPT_INV_SRCIP)
	    || FWINV((ip->daddr&ipinfo->dmsk.s_addr) != ipinfo->dst.s_addr,
		     IPT_INV_DSTIP)) {
		dprintf("Source or dest mismatch.\n");

		dprintf("SRC: %u.%u.%u.%u. Mask: %u.%u.%u.%u. Target: %u.%u.%u.%u.%s\n",
			NIPQUAD(ip->saddr),
			NIPQUAD(ipinfo->smsk.s_addr),
			NIPQUAD(ipinfo->src.s_addr),
			ipinfo->invflags & IPT_INV_SRCIP ? " (INV)" : "");
		dprintf("DST: %u.%u.%u.%u Mask: %u.%u.%u.%u Target: %u.%u.%u.%u.%s\n",
			NIPQUAD(ip->daddr),
			NIPQUAD(ipinfo->dmsk.s_addr),
			NIPQUAD(ipinfo->dst.s_addr),
			ipinfo->invflags & IPT_INV_DSTIP ? " (INV)" : "");
		return 0;
	}

	/* Look for ifname matches; this should unroll nicely. */
	for (i = 0, ret = 0; i < IFNAMSIZ/sizeof(unsigned long); i++) {
		ret |= (((const unsigned long *)indev)[i]
			^ ((const unsigned long *)ipinfo->iniface)[i])
			& ((const unsigned long *)ipinfo->iniface_mask)[i];
	}

	if (FWINV(ret != 0, IPT_INV_VIA_IN)) {
		dprintf("VIA in mismatch (%s vs %s).%s\n",
			indev, ipinfo->iniface,
			ipinfo->invflags&IPT_INV_VIA_IN ?" (INV)":"");
		return 0;
	}

	for (i = 0, ret = 0; i < IFNAMSIZ/sizeof(unsigned long); i++) {
		ret |= (((const unsigned long *)outdev)[i]
			^ ((const unsigned long *)ipinfo->outiface)[i])
			& ((const unsigned long *)ipinfo->outiface_mask)[i];
	}

	if (FWINV(ret != 0, IPT_INV_VIA_OUT)) {
		dprintf("VIA out mismatch (%s vs %s).%s\n",
			outdev, ipinfo->outiface,
			ipinfo->invflags&IPT_INV_VIA_OUT ?" (INV)":"");
		return 0;
	}

	/* Check specific protocol */
	if (ipinfo->proto
	    && FWINV(ip->protocol != ipinfo->proto, IPT_INV_PROTO)) {
		dprintf("Packet protocol %hi does not match %hi.%s\n",
			ip->protocol, ipinfo->proto,
			ipinfo->invflags&IPT_INV_PROTO ? " (INV)":"");
		return 0;
	}

	/* If we have a fragment rule but the packet is not a fragment
	 * then we return zero */
	if (FWINV((ipinfo->flags&IPT_F_FRAG) && !isfrag, IPT_INV_FRAG)) {
		dprintf("Fragment rule but not fragment.%s\n",
			ipinfo->invflags & IPT_INV_FRAG ? " (INV)" : "");
		return 0;
	}

	return 1;
}

static inline bool
ip_checkentry(const struct ipt_ip *ip)
{
	if (ip->flags & ~IPT_F_MASK) {
		duprintf("Unknown flag bits set: %08X\n",
			 ip->flags & ~IPT_F_MASK);
		return false;
	}
	if (ip->invflags & ~IPT_INV_MASK) {
		duprintf("Unknown invflag bits set: %08X\n",
			 ip->invflags & ~IPT_INV_MASK);
		return false;
	}
	return true;
}

static unsigned int
ipt_error(struct sk_buff *skb,
	  const struct net_device *in,
	  const struct net_device *out,
	  unsigned int hooknum,
	  const struct xt_target *target,
	  const void *targinfo)
{
	if (net_ratelimit())
		printk("ip_tables: error: `%s'\n", (char *)targinfo);

	return NF_DROP;
}

static inline
bool do_match(struct ipt_entry_match *m,
	      const struct sk_buff *skb,
	      const struct net_device *in,
	      const struct net_device *out,
	      int offset,
	      bool *hotdrop)
{
	/* Stop iteration if it doesn't match */
	if (!m->u.kernel.match->match(skb, in, out, m->u.kernel.match, m->data,
				      offset, ip_hdrlen(skb), hotdrop))
		return true;
	else
		return false;
}

static inline struct ipt_entry *
get_entry(void *base, unsigned int offset)
{
	return (struct ipt_entry *)(base + offset);
}

/* All zeroes == unconditional rule. */
static inline int
unconditional(const struct ipt_ip *ip)
{
	unsigned int i;

	for (i = 0; i < sizeof(*ip)/sizeof(__u32); i++)
		if (((__u32 *)ip)[i])
			return 0;

	return 1;
}

#if defined(CONFIG_NETFILTER_XT_TARGET_TRACE) || \
    defined(CONFIG_NETFILTER_XT_TARGET_TRACE_MODULE)
static const char *hooknames[] = {
	[NF_IP_PRE_ROUTING]		= "PREROUTING",
	[NF_IP_LOCAL_IN]		= "INPUT",
	[NF_IP_FORWARD]			= "FORWARD",
	[NF_IP_LOCAL_OUT]		= "OUTPUT",
	[NF_IP_POST_ROUTING]		= "POSTROUTING",
};

enum nf_ip_trace_comments {
	NF_IP_TRACE_COMMENT_RULE,
	NF_IP_TRACE_COMMENT_RETURN,
	NF_IP_TRACE_COMMENT_POLICY,
};

static const char *comments[] = {
	[NF_IP_TRACE_COMMENT_RULE]	= "rule",
	[NF_IP_TRACE_COMMENT_RETURN]	= "return",
	[NF_IP_TRACE_COMMENT_POLICY]	= "policy",
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

static inline int
get_chainname_rulenum(struct ipt_entry *s, struct ipt_entry *e,
		      char *hookname, char **chainname,
		      char **comment, unsigned int *rulenum)
{
	struct ipt_standard_target *t = (void *)ipt_get_target(s);

	if (strcmp(t->target.u.kernel.target->name, IPT_ERROR_TARGET) == 0) {
		/* Head of user chain: ERROR target with chainname */
		*chainname = t->target.data;
		(*rulenum) = 0;
	} else if (s == e) {
		(*rulenum)++;

		if (s->target_offset == sizeof(struct ipt_entry)
		   && strcmp(t->target.u.kernel.target->name,
			     IPT_STANDARD_TARGET) == 0
		   && t->verdict < 0
		   && unconditional(&s->ip)) {
			/* Tail of chains: STANDARD target (return/policy) */
			*comment = *chainname == hookname
				? (char *)comments[NF_IP_TRACE_COMMENT_POLICY]
				: (char *)comments[NF_IP_TRACE_COMMENT_RETURN];
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
			 char *tablename,
			 struct xt_table_info *private,
			 struct ipt_entry *e)
{
	void *table_base;
	struct ipt_entry *root;
	char *hookname, *chainname, *comment;
	unsigned int rulenum = 0;

	table_base = (void *)private->entries[smp_processor_id()];
	root = get_entry(table_base, private->hook_entry[hook]);

	hookname = chainname = (char *)hooknames[hook];
	comment = (char *)comments[NF_IP_TRACE_COMMENT_RULE];

	IPT_ENTRY_ITERATE(root,
			  private->size - private->hook_entry[hook],
			  get_chainname_rulenum,
			  e, hookname, &chainname, &comment, &rulenum);

	nf_log_packet(AF_INET, hook, skb, in, out, &trace_loginfo,
		      "TRACE: %s:%s:%s:%u ",
		      tablename, chainname, comment, rulenum);
}
#endif

/* Returns one of the generic firewall policies, like NF_ACCEPT. */
unsigned int
ipt_do_table(struct sk_buff *skb,
	     unsigned int hook,
	     const struct net_device *in,
	     const struct net_device *out,
	     struct xt_table *table)
{
	static const char nulldevname[IFNAMSIZ] __attribute__((aligned(sizeof(long))));
	u_int16_t offset;
	struct iphdr *ip;
	u_int16_t datalen;
	bool hotdrop = false;
	/* Initializing verdict to NF_DROP keeps gcc happy. */
	unsigned int verdict = NF_DROP;
	const char *indev, *outdev;
	void *table_base;
	struct ipt_entry *e, *back;
	struct xt_table_info *private;

	/* Initialization */
	ip = ip_hdr(skb);
	datalen = skb->len - ip->ihl * 4;
	indev = in ? in->name : nulldevname;
	outdev = out ? out->name : nulldevname;
	/* We handle fragments by dealing with the first fragment as
	 * if it was a normal packet.  All other fragments are treated
	 * normally, except that they will NEVER match rules that ask
	 * things we don't know, ie. tcp syn flag or ports).  If the
	 * rule is also a fragment-specific rule, non-fragments won't
	 * match it. */
	offset = ntohs(ip->frag_off) & IP_OFFSET;

	read_lock_bh(&table->lock);
	IP_NF_ASSERT(table->valid_hooks & (1 << hook));
	private = table->private;
	table_base = (void *)private->entries[smp_processor_id()];
	e = get_entry(table_base, private->hook_entry[hook]);

	/* For return from builtin chain */
	back = get_entry(table_base, private->underflow[hook]);

	do {
		IP_NF_ASSERT(e);
		IP_NF_ASSERT(back);
		if (ip_packet_match(ip, indev, outdev, &e->ip, offset)) {
			struct ipt_entry_target *t;

			if (IPT_MATCH_ITERATE(e, do_match,
					      skb, in, out,
					      offset, &hotdrop) != 0)
				goto no_match;

			ADD_COUNTER(e->counters, ntohs(ip->tot_len), 1);

			t = ipt_get_target(e);
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

				v = ((struct ipt_standard_target *)t)->verdict;
				if (v < 0) {
					/* Pop from stack? */
					if (v != IPT_RETURN) {
						verdict = (unsigned)(-v) - 1;
						break;
					}
					e = back;
					back = get_entry(table_base,
							 back->comefrom);
					continue;
				}
				if (table_base + v != (void *)e + e->next_offset
				    && !(e->ip.flags & IPT_F_GOTO)) {
					/* Save old back ptr in next entry */
					struct ipt_entry *next
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
#ifdef CONFIG_NETFILTER_DEBUG
				((struct ipt_entry *)table_base)->comefrom
					= 0xeeeeeeec;
#endif
				verdict = t->u.kernel.target->target(skb,
								     in, out,
								     hook,
								     t->u.kernel.target,
								     t->data);

#ifdef CONFIG_NETFILTER_DEBUG
				if (((struct ipt_entry *)table_base)->comefrom
				    != 0xeeeeeeec
				    && verdict == IPT_CONTINUE) {
					printk("Target %s reentered!\n",
					       t->u.kernel.target->name);
					verdict = NF_DROP;
				}
				((struct ipt_entry *)table_base)->comefrom
					= 0x57acc001;
#endif
				/* Target might have changed stuff. */
				ip = ip_hdr(skb);
				datalen = skb->len - ip->ihl * 4;

				if (verdict == IPT_CONTINUE)
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

	read_unlock_bh(&table->lock);

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
	for (hook = 0; hook < NF_IP_NUMHOOKS; hook++) {
		unsigned int pos = newinfo->hook_entry[hook];
		struct ipt_entry *e
			= (struct ipt_entry *)(entry0 + pos);

		if (!(valid_hooks & (1 << hook)))
			continue;

		/* Set initial back pointer. */
		e->counters.pcnt = pos;

		for (;;) {
			struct ipt_standard_target *t
				= (void *)ipt_get_target(e);
			int visited = e->comefrom & (1 << hook);

			if (e->comefrom & (1 << NF_IP_NUMHOOKS)) {
				printk("iptables: loop hook %u pos %u %08X.\n",
				       hook, pos, e->comefrom);
				return 0;
			}
			e->comefrom
				|= ((1 << hook) | (1 << NF_IP_NUMHOOKS));

			/* Unconditional return/END. */
			if ((e->target_offset == sizeof(struct ipt_entry)
			    && (strcmp(t->target.u.user.name,
				       IPT_STANDARD_TARGET) == 0)
			    && t->verdict < 0
			    && unconditional(&e->ip)) || visited) {
				unsigned int oldpos, size;

				if (t->verdict < -NF_MAX_VERDICT - 1) {
					duprintf("mark_source_chains: bad "
						"negative verdict (%i)\n",
								t->verdict);
					return 0;
				}

				/* Return: backtrack through the last
				   big jump. */
				do {
					e->comefrom ^= (1<<NF_IP_NUMHOOKS);
#ifdef DEBUG_IP_FIREWALL_USER
					if (e->comefrom
					    & (1 << NF_IP_NUMHOOKS)) {
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

					e = (struct ipt_entry *)
						(entry0 + pos);
				} while (oldpos == pos + e->next_offset);

				/* Move along one */
				size = e->next_offset;
				e = (struct ipt_entry *)
					(entry0 + pos + size);
				e->counters.pcnt = pos;
				pos += size;
			} else {
				int newpos = t->verdict;

				if (strcmp(t->target.u.user.name,
					   IPT_STANDARD_TARGET) == 0
				    && newpos >= 0) {
					if (newpos > newinfo->size -
						sizeof(struct ipt_entry)) {
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
				e = (struct ipt_entry *)
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

static inline int
cleanup_match(struct ipt_entry_match *m, unsigned int *i)
{
	if (i && (*i)-- == 0)
		return 1;

	if (m->u.kernel.match->destroy)
		m->u.kernel.match->destroy(m->u.kernel.match, m->data);
	module_put(m->u.kernel.match->me);
	return 0;
}

static inline int
check_entry(struct ipt_entry *e, const char *name)
{
	struct ipt_entry_target *t;

	if (!ip_checkentry(&e->ip)) {
		duprintf("ip_tables: ip check failed %p %s.\n", e, name);
		return -EINVAL;
	}

	if (e->target_offset + sizeof(struct ipt_entry_target) > e->next_offset)
		return -EINVAL;

	t = ipt_get_target(e);
	if (e->target_offset + t->u.target_size > e->next_offset)
		return -EINVAL;

	return 0;
}

static inline int check_match(struct ipt_entry_match *m, const char *name,
				const struct ipt_ip *ip, unsigned int hookmask,
				unsigned int *i)
{
	struct xt_match *match;
	int ret;

	match = m->u.kernel.match;
	ret = xt_check_match(match, AF_INET, m->u.match_size - sizeof(*m),
			     name, hookmask, ip->proto,
			     ip->invflags & IPT_INV_PROTO);
	if (!ret && m->u.kernel.match->checkentry
	    && !m->u.kernel.match->checkentry(name, ip, match, m->data,
					      hookmask)) {
		duprintf("ip_tables: check failed for `%s'.\n",
			 m->u.kernel.match->name);
		ret = -EINVAL;
	}
	if (!ret)
		(*i)++;
	return ret;
}

static inline int
find_check_match(struct ipt_entry_match *m,
	    const char *name,
	    const struct ipt_ip *ip,
	    unsigned int hookmask,
	    unsigned int *i)
{
	struct xt_match *match;
	int ret;

	match = try_then_request_module(xt_find_match(AF_INET, m->u.user.name,
						   m->u.user.revision),
					"ipt_%s", m->u.user.name);
	if (IS_ERR(match) || !match) {
		duprintf("find_check_match: `%s' not found\n", m->u.user.name);
		return match ? PTR_ERR(match) : -ENOENT;
	}
	m->u.kernel.match = match;

	ret = check_match(m, name, ip, hookmask, i);
	if (ret)
		goto err;

	return 0;
err:
	module_put(m->u.kernel.match->me);
	return ret;
}

static inline int check_target(struct ipt_entry *e, const char *name)
{
	struct ipt_entry_target *t;
	struct xt_target *target;
	int ret;

	t = ipt_get_target(e);
	target = t->u.kernel.target;
	ret = xt_check_target(target, AF_INET, t->u.target_size - sizeof(*t),
			      name, e->comefrom, e->ip.proto,
			      e->ip.invflags & IPT_INV_PROTO);
	if (!ret && t->u.kernel.target->checkentry
		   && !t->u.kernel.target->checkentry(name, e, target,
						      t->data, e->comefrom)) {
		duprintf("ip_tables: check failed for `%s'.\n",
			 t->u.kernel.target->name);
		ret = -EINVAL;
	}
	return ret;
}

static inline int
find_check_entry(struct ipt_entry *e, const char *name, unsigned int size,
	    unsigned int *i)
{
	struct ipt_entry_target *t;
	struct xt_target *target;
	int ret;
	unsigned int j;

	ret = check_entry(e, name);
	if (ret)
		return ret;

	j = 0;
	ret = IPT_MATCH_ITERATE(e, find_check_match, name, &e->ip,
							e->comefrom, &j);
	if (ret != 0)
		goto cleanup_matches;

	t = ipt_get_target(e);
	target = try_then_request_module(xt_find_target(AF_INET,
						     t->u.user.name,
						     t->u.user.revision),
					 "ipt_%s", t->u.user.name);
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
	IPT_MATCH_ITERATE(e, cleanup_match, &j);
	return ret;
}

static inline int
check_entry_size_and_hooks(struct ipt_entry *e,
			   struct xt_table_info *newinfo,
			   unsigned char *base,
			   unsigned char *limit,
			   const unsigned int *hook_entries,
			   const unsigned int *underflows,
			   unsigned int *i)
{
	unsigned int h;

	if ((unsigned long)e % __alignof__(struct ipt_entry) != 0
	    || (unsigned char *)e + sizeof(struct ipt_entry) >= limit) {
		duprintf("Bad offset %p\n", e);
		return -EINVAL;
	}

	if (e->next_offset
	    < sizeof(struct ipt_entry) + sizeof(struct ipt_entry_target)) {
		duprintf("checking: element %p size %u\n",
			 e, e->next_offset);
		return -EINVAL;
	}

	/* Check hooks & underflows */
	for (h = 0; h < NF_IP_NUMHOOKS; h++) {
		if ((unsigned char *)e - base == hook_entries[h])
			newinfo->hook_entry[h] = hook_entries[h];
		if ((unsigned char *)e - base == underflows[h])
			newinfo->underflow[h] = underflows[h];
	}

	/* FIXME: underflows must be unconditional, standard verdicts
	   < 0 (not IPT_RETURN). --RR */

	/* Clear counters and comefrom */
	e->counters = ((struct xt_counters) { 0, 0 });
	e->comefrom = 0;

	(*i)++;
	return 0;
}

static inline int
cleanup_entry(struct ipt_entry *e, unsigned int *i)
{
	struct ipt_entry_target *t;

	if (i && (*i)-- == 0)
		return 1;

	/* Cleanup all matches */
	IPT_MATCH_ITERATE(e, cleanup_match, NULL);
	t = ipt_get_target(e);
	if (t->u.kernel.target->destroy)
		t->u.kernel.target->destroy(t->u.kernel.target, t->data);
	module_put(t->u.kernel.target->me);
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
	for (i = 0; i < NF_IP_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = 0xFFFFFFFF;
		newinfo->underflow[i] = 0xFFFFFFFF;
	}

	duprintf("translate_table: size %u\n", newinfo->size);
	i = 0;
	/* Walk through entries, checking offsets. */
	ret = IPT_ENTRY_ITERATE(entry0, newinfo->size,
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
	for (i = 0; i < NF_IP_NUMHOOKS; i++) {
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
	ret = IPT_ENTRY_ITERATE(entry0, newinfo->size,
				find_check_entry, name, size, &i);

	if (ret != 0) {
		IPT_ENTRY_ITERATE(entry0, newinfo->size,
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
add_entry_to_counter(const struct ipt_entry *e,
		     struct xt_counters total[],
		     unsigned int *i)
{
	ADD_COUNTER(total[*i], e->counters.bcnt, e->counters.pcnt);

	(*i)++;
	return 0;
}

static inline int
set_entry_to_counter(const struct ipt_entry *e,
		     struct ipt_counters total[],
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
	IPT_ENTRY_ITERATE(t->entries[curcpu],
			  t->size,
			  set_entry_to_counter,
			  counters,
			  &i);

	for_each_possible_cpu(cpu) {
		if (cpu == curcpu)
			continue;
		i = 0;
		IPT_ENTRY_ITERATE(t->entries[cpu],
				  t->size,
				  add_entry_to_counter,
				  counters,
				  &i);
	}
}

static inline struct xt_counters * alloc_counters(struct xt_table *table)
{
	unsigned int countersize;
	struct xt_counters *counters;
	struct xt_table_info *private = table->private;

	/* We need atomic snapshot of counters: rest doesn't change
	   (other than comefrom, which userspace doesn't care
	   about). */
	countersize = sizeof(struct xt_counters) * private->number;
	counters = vmalloc_node(countersize, numa_node_id());

	if (counters == NULL)
		return ERR_PTR(-ENOMEM);

	/* First, sum counters... */
	write_lock_bh(&table->lock);
	get_counters(private, counters);
	write_unlock_bh(&table->lock);

	return counters;
}

static int
copy_entries_to_user(unsigned int total_size,
		     struct xt_table *table,
		     void __user *userptr)
{
	unsigned int off, num;
	struct ipt_entry *e;
	struct xt_counters *counters;
	struct xt_table_info *private = table->private;
	int ret = 0;
	void *loc_cpu_entry;

	counters = alloc_counters(table);
	if (IS_ERR(counters))
		return PTR_ERR(counters);

	/* choose the copy that is on our node/cpu, ...
	 * This choice is lazy (because current thread is
	 * allowed to migrate to another cpu)
	 */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	/* ... then copy entire thing ... */
	if (copy_to_user(userptr, loc_cpu_entry, total_size) != 0) {
		ret = -EFAULT;
		goto free_counters;
	}

	/* FIXME: use iterator macros --RR */
	/* ... then go back and fix counters and names */
	for (off = 0, num = 0; off < total_size; off += e->next_offset, num++){
		unsigned int i;
		struct ipt_entry_match *m;
		struct ipt_entry_target *t;

		e = (struct ipt_entry *)(loc_cpu_entry + off);
		if (copy_to_user(userptr + off
				 + offsetof(struct ipt_entry, counters),
				 &counters[num],
				 sizeof(counters[num])) != 0) {
			ret = -EFAULT;
			goto free_counters;
		}

		for (i = sizeof(struct ipt_entry);
		     i < e->target_offset;
		     i += m->u.match_size) {
			m = (void *)e + i;

			if (copy_to_user(userptr + off + i
					 + offsetof(struct ipt_entry_match,
						    u.user.name),
					 m->u.kernel.match->name,
					 strlen(m->u.kernel.match->name)+1)
			    != 0) {
				ret = -EFAULT;
				goto free_counters;
			}
		}

		t = ipt_get_target(e);
		if (copy_to_user(userptr + off + e->target_offset
				 + offsetof(struct ipt_entry_target,
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
struct compat_delta {
	struct compat_delta *next;
	unsigned int offset;
	short delta;
};

static struct compat_delta *compat_offsets = NULL;

static int compat_add_offset(unsigned int offset, short delta)
{
	struct compat_delta *tmp;

	tmp = kmalloc(sizeof(struct compat_delta), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	tmp->offset = offset;
	tmp->delta = delta;
	if (compat_offsets) {
		tmp->next = compat_offsets->next;
		compat_offsets->next = tmp;
	} else {
		compat_offsets = tmp;
		tmp->next = NULL;
	}
	return 0;
}

static void compat_flush_offsets(void)
{
	struct compat_delta *tmp, *next;

	if (compat_offsets) {
		for(tmp = compat_offsets; tmp; tmp = next) {
			next = tmp->next;
			kfree(tmp);
		}
		compat_offsets = NULL;
	}
}

static short compat_calc_jump(unsigned int offset)
{
	struct compat_delta *tmp;
	short delta;

	for(tmp = compat_offsets, delta = 0; tmp; tmp = tmp->next)
		if (tmp->offset < offset)
			delta += tmp->delta;
	return delta;
}

static void compat_standard_from_user(void *dst, void *src)
{
	int v = *(compat_int_t *)src;

	if (v > 0)
		v += compat_calc_jump(v);
	memcpy(dst, &v, sizeof(v));
}

static int compat_standard_to_user(void __user *dst, void *src)
{
	compat_int_t cv = *(int *)src;

	if (cv > 0)
		cv -= compat_calc_jump(cv);
	return copy_to_user(dst, &cv, sizeof(cv)) ? -EFAULT : 0;
}

static inline int
compat_calc_match(struct ipt_entry_match *m, int * size)
{
	*size += xt_compat_match_offset(m->u.kernel.match);
	return 0;
}

static int compat_calc_entry(struct ipt_entry *e, struct xt_table_info *info,
		void *base, struct xt_table_info *newinfo)
{
	struct ipt_entry_target *t;
	unsigned int entry_offset;
	int off, i, ret;

	off = 0;
	entry_offset = (void *)e - base;
	IPT_MATCH_ITERATE(e, compat_calc_match, &off);
	t = ipt_get_target(e);
	off += xt_compat_target_offset(t->u.kernel.target);
	newinfo->size -= off;
	ret = compat_add_offset(entry_offset, off);
	if (ret)
		return ret;

	for (i = 0; i< NF_IP_NUMHOOKS; i++) {
		if (info->hook_entry[i] && (e < (struct ipt_entry *)
				(base + info->hook_entry[i])))
			newinfo->hook_entry[i] -= off;
		if (info->underflow[i] && (e < (struct ipt_entry *)
				(base + info->underflow[i])))
			newinfo->underflow[i] -= off;
	}
	return 0;
}

static int compat_table_info(struct xt_table_info *info,
		struct xt_table_info *newinfo)
{
	void *loc_cpu_entry;
	int i;

	if (!newinfo || !info)
		return -EINVAL;

	memset(newinfo, 0, sizeof(struct xt_table_info));
	newinfo->size = info->size;
	newinfo->number = info->number;
	for (i = 0; i < NF_IP_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = info->hook_entry[i];
		newinfo->underflow[i] = info->underflow[i];
	}
	loc_cpu_entry = info->entries[raw_smp_processor_id()];
	return IPT_ENTRY_ITERATE(loc_cpu_entry, info->size,
			compat_calc_entry, info, loc_cpu_entry, newinfo);
}
#endif

static int get_info(void __user *user, int *len, int compat)
{
	char name[IPT_TABLE_MAXNAMELEN];
	struct xt_table *t;
	int ret;

	if (*len != sizeof(struct ipt_getinfo)) {
		duprintf("length %u != %u\n", *len,
			(unsigned int)sizeof(struct ipt_getinfo));
		return -EINVAL;
	}

	if (copy_from_user(name, user, sizeof(name)) != 0)
		return -EFAULT;

	name[IPT_TABLE_MAXNAMELEN-1] = '\0';
#ifdef CONFIG_COMPAT
	if (compat)
		xt_compat_lock(AF_INET);
#endif
	t = try_then_request_module(xt_find_table_lock(AF_INET, name),
			"iptable_%s", name);
	if (t && !IS_ERR(t)) {
		struct ipt_getinfo info;
		struct xt_table_info *private = t->private;

#ifdef CONFIG_COMPAT
		if (compat) {
			struct xt_table_info tmp;
			ret = compat_table_info(private, &tmp);
			compat_flush_offsets();
			private =  &tmp;
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
		xt_compat_unlock(AF_INET);
#endif
	return ret;
}

static int
get_entries(struct ipt_get_entries __user *uptr, int *len)
{
	int ret;
	struct ipt_get_entries get;
	struct xt_table *t;

	if (*len < sizeof(get)) {
		duprintf("get_entries: %u < %d\n", *len,
				(unsigned int)sizeof(get));
		return -EINVAL;
	}
	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;
	if (*len != sizeof(struct ipt_get_entries) + get.size) {
		duprintf("get_entries: %u != %u\n", *len,
				(unsigned int)(sizeof(struct ipt_get_entries) +
				get.size));
		return -EINVAL;
	}

	t = xt_find_table_lock(AF_INET, get.name);
	if (t && !IS_ERR(t)) {
		struct xt_table_info *private = t->private;
		duprintf("t->private->number = %u\n",
			 private->number);
		if (get.size == private->size)
			ret = copy_entries_to_user(private->size,
						   t, uptr->entrytable);
		else {
			duprintf("get_entries: I've got %u not %u!\n",
				 private->size,
				 get.size);
			ret = -EINVAL;
		}
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = t ? PTR_ERR(t) : -ENOENT;

	return ret;
}

static int
__do_replace(const char *name, unsigned int valid_hooks,
		struct xt_table_info *newinfo, unsigned int num_counters,
		void __user *counters_ptr)
{
	int ret;
	struct xt_table *t;
	struct xt_table_info *oldinfo;
	struct xt_counters *counters;
	void *loc_cpu_old_entry;

	ret = 0;
	counters = vmalloc(num_counters * sizeof(struct xt_counters));
	if (!counters) {
		ret = -ENOMEM;
		goto out;
	}

	t = try_then_request_module(xt_find_table_lock(AF_INET, name),
				    "iptable_%s", name);
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
	IPT_ENTRY_ITERATE(loc_cpu_old_entry, oldinfo->size, cleanup_entry,NULL);
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
do_replace(void __user *user, unsigned int len)
{
	int ret;
	struct ipt_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* Hack: Causes ipchains to give correct error msg --RR */
	if (len != sizeof(tmp) + tmp.size)
		return -ENOPROTOOPT;

	/* overflow check */
	if (tmp.size >= (INT_MAX - sizeof(struct xt_table_info)) / NR_CPUS -
			SMP_CACHE_BYTES)
		return -ENOMEM;
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	/* choose the copy that is our node/cpu */
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

	ret = __do_replace(tmp.name, tmp.valid_hooks,
			      newinfo, tmp.num_counters,
			      tmp.counters);
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	IPT_ENTRY_ITERATE(loc_cpu_entry, newinfo->size, cleanup_entry,NULL);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

/* We're lazy, and add to the first CPU; overflow works its fey magic
 * and everything is OK. */
static inline int
add_counter_to_entry(struct ipt_entry *e,
		     const struct xt_counters addme[],
		     unsigned int *i)
{
#if 0
	duprintf("add_counter: Entry %u %lu/%lu + %lu/%lu\n",
		 *i,
		 (long unsigned int)e->counters.pcnt,
		 (long unsigned int)e->counters.bcnt,
		 (long unsigned int)addme[*i].pcnt,
		 (long unsigned int)addme[*i].bcnt);
#endif

	ADD_COUNTER(e->counters, addme[*i].bcnt, addme[*i].pcnt);

	(*i)++;
	return 0;
}

static int
do_add_counters(void __user *user, unsigned int len, int compat)
{
	unsigned int i;
	struct xt_counters_info tmp;
	struct xt_counters *paddc;
	unsigned int num_counters;
	char *name;
	int size;
	void *ptmp;
	struct xt_table *t;
	struct xt_table_info *private;
	int ret = 0;
	void *loc_cpu_entry;
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

	t = xt_find_table_lock(AF_INET, name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free;
	}

	write_lock_bh(&t->lock);
	private = t->private;
	if (private->number != num_counters) {
		ret = -EINVAL;
		goto unlock_up_free;
	}

	i = 0;
	/* Choose the copy that is on our node */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	IPT_ENTRY_ITERATE(loc_cpu_entry,
			  private->size,
			  add_counter_to_entry,
			  paddc,
			  &i);
 unlock_up_free:
	write_unlock_bh(&t->lock);
	xt_table_unlock(t);
	module_put(t->me);
 free:
	vfree(paddc);

	return ret;
}

#ifdef CONFIG_COMPAT
struct compat_ipt_replace {
	char			name[IPT_TABLE_MAXNAMELEN];
	u32			valid_hooks;
	u32			num_entries;
	u32			size;
	u32			hook_entry[NF_IP_NUMHOOKS];
	u32			underflow[NF_IP_NUMHOOKS];
	u32			num_counters;
	compat_uptr_t		counters;	/* struct ipt_counters * */
	struct compat_ipt_entry	entries[0];
};

static inline int compat_copy_match_to_user(struct ipt_entry_match *m,
		void __user **dstptr, compat_uint_t *size)
{
	return xt_compat_match_to_user(m, dstptr, size);
}

static int compat_copy_entry_to_user(struct ipt_entry *e,
		void __user **dstptr, compat_uint_t *size)
{
	struct ipt_entry_target *t;
	struct compat_ipt_entry __user *ce;
	u_int16_t target_offset, next_offset;
	compat_uint_t origsize;
	int ret;

	ret = -EFAULT;
	origsize = *size;
	ce = (struct compat_ipt_entry __user *)*dstptr;
	if (copy_to_user(ce, e, sizeof(struct ipt_entry)))
		goto out;

	*dstptr += sizeof(struct compat_ipt_entry);
	ret = IPT_MATCH_ITERATE(e, compat_copy_match_to_user, dstptr, size);
	target_offset = e->target_offset - (origsize - *size);
	if (ret)
		goto out;
	t = ipt_get_target(e);
	ret = xt_compat_target_to_user(t, dstptr, size);
	if (ret)
		goto out;
	ret = -EFAULT;
	next_offset = e->next_offset - (origsize - *size);
	if (put_user(target_offset, &ce->target_offset))
		goto out;
	if (put_user(next_offset, &ce->next_offset))
		goto out;
	return 0;
out:
	return ret;
}

static inline int
compat_find_calc_match(struct ipt_entry_match *m,
	    const char *name,
	    const struct ipt_ip *ip,
	    unsigned int hookmask,
	    int *size, int *i)
{
	struct xt_match *match;

	match = try_then_request_module(xt_find_match(AF_INET, m->u.user.name,
						   m->u.user.revision),
					"ipt_%s", m->u.user.name);
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

static inline int
compat_release_match(struct ipt_entry_match *m, unsigned int *i)
{
	if (i && (*i)-- == 0)
		return 1;

	module_put(m->u.kernel.match->me);
	return 0;
}

static inline int
compat_release_entry(struct ipt_entry *e, unsigned int *i)
{
	struct ipt_entry_target *t;

	if (i && (*i)-- == 0)
		return 1;

	/* Cleanup all matches */
	IPT_MATCH_ITERATE(e, compat_release_match, NULL);
	t = ipt_get_target(e);
	module_put(t->u.kernel.target->me);
	return 0;
}

static inline int
check_compat_entry_size_and_hooks(struct ipt_entry *e,
			   struct xt_table_info *newinfo,
			   unsigned int *size,
			   unsigned char *base,
			   unsigned char *limit,
			   unsigned int *hook_entries,
			   unsigned int *underflows,
			   unsigned int *i,
			   const char *name)
{
	struct ipt_entry_target *t;
	struct xt_target *target;
	unsigned int entry_offset;
	int ret, off, h, j;

	duprintf("check_compat_entry_size_and_hooks %p\n", e);
	if ((unsigned long)e % __alignof__(struct compat_ipt_entry) != 0
	    || (unsigned char *)e + sizeof(struct compat_ipt_entry) >= limit) {
		duprintf("Bad offset %p, limit = %p\n", e, limit);
		return -EINVAL;
	}

	if (e->next_offset < sizeof(struct compat_ipt_entry) +
			sizeof(struct compat_xt_entry_target)) {
		duprintf("checking: element %p size %u\n",
			 e, e->next_offset);
		return -EINVAL;
	}

	ret = check_entry(e, name);
	if (ret)
		return ret;

	off = 0;
	entry_offset = (void *)e - (void *)base;
	j = 0;
	ret = IPT_MATCH_ITERATE(e, compat_find_calc_match, name, &e->ip,
			e->comefrom, &off, &j);
	if (ret != 0)
		goto release_matches;

	t = ipt_get_target(e);
	target = try_then_request_module(xt_find_target(AF_INET,
						     t->u.user.name,
						     t->u.user.revision),
					 "ipt_%s", t->u.user.name);
	if (IS_ERR(target) || !target) {
		duprintf("check_compat_entry_size_and_hooks: `%s' not found\n",
							t->u.user.name);
		ret = target ? PTR_ERR(target) : -ENOENT;
		goto release_matches;
	}
	t->u.kernel.target = target;

	off += xt_compat_target_offset(target);
	*size += off;
	ret = compat_add_offset(entry_offset, off);
	if (ret)
		goto out;

	/* Check hooks & underflows */
	for (h = 0; h < NF_IP_NUMHOOKS; h++) {
		if ((unsigned char *)e - base == hook_entries[h])
			newinfo->hook_entry[h] = hook_entries[h];
		if ((unsigned char *)e - base == underflows[h])
			newinfo->underflow[h] = underflows[h];
	}

	/* Clear counters and comefrom */
	e->counters = ((struct ipt_counters) { 0, 0 });
	e->comefrom = 0;

	(*i)++;
	return 0;

out:
	module_put(t->u.kernel.target->me);
release_matches:
	IPT_MATCH_ITERATE(e, compat_release_match, &j);
	return ret;
}

static inline int compat_copy_match_from_user(struct ipt_entry_match *m,
	void **dstptr, compat_uint_t *size, const char *name,
	const struct ipt_ip *ip, unsigned int hookmask)
{
	xt_compat_match_from_user(m, dstptr, size);
	return 0;
}

static int compat_copy_entry_from_user(struct ipt_entry *e, void **dstptr,
	unsigned int *size, const char *name,
	struct xt_table_info *newinfo, unsigned char *base)
{
	struct ipt_entry_target *t;
	struct xt_target *target;
	struct ipt_entry *de;
	unsigned int origsize;
	int ret, h;

	ret = 0;
	origsize = *size;
	de = (struct ipt_entry *)*dstptr;
	memcpy(de, e, sizeof(struct ipt_entry));

	*dstptr += sizeof(struct compat_ipt_entry);
	ret = IPT_MATCH_ITERATE(e, compat_copy_match_from_user, dstptr, size,
			name, &de->ip, de->comefrom);
	if (ret)
		return ret;
	de->target_offset = e->target_offset - (origsize - *size);
	t = ipt_get_target(e);
	target = t->u.kernel.target;
	xt_compat_target_from_user(t, dstptr, size);

	de->next_offset = e->next_offset - (origsize - *size);
	for (h = 0; h < NF_IP_NUMHOOKS; h++) {
		if ((unsigned char *)de - base < newinfo->hook_entry[h])
			newinfo->hook_entry[h] -= origsize - *size;
		if ((unsigned char *)de - base < newinfo->underflow[h])
			newinfo->underflow[h] -= origsize - *size;
	}
	return ret;
}

static inline int compat_check_entry(struct ipt_entry *e, const char *name,
						unsigned int *i)
{
	int j, ret;

	j = 0;
	ret = IPT_MATCH_ITERATE(e, check_match, name, &e->ip, e->comefrom, &j);
	if (ret)
		goto cleanup_matches;

	ret = check_target(e, name);
	if (ret)
		goto cleanup_matches;

	(*i)++;
	return 0;

 cleanup_matches:
	IPT_MATCH_ITERATE(e, cleanup_match, &j);
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
	for (i = 0; i < NF_IP_NUMHOOKS; i++) {
		info->hook_entry[i] = 0xFFFFFFFF;
		info->underflow[i] = 0xFFFFFFFF;
	}

	duprintf("translate_compat_table: size %u\n", info->size);
	j = 0;
	xt_compat_lock(AF_INET);
	/* Walk through entries, checking offsets. */
	ret = IPT_ENTRY_ITERATE(entry0, total_size,
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
	for (i = 0; i < NF_IP_NUMHOOKS; i++) {
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
	for (i = 0; i < NF_IP_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = info->hook_entry[i];
		newinfo->underflow[i] = info->underflow[i];
	}
	entry1 = newinfo->entries[raw_smp_processor_id()];
	pos = entry1;
	size =  total_size;
	ret = IPT_ENTRY_ITERATE(entry0, total_size,
			compat_copy_entry_from_user, &pos, &size,
			name, newinfo, entry1);
	compat_flush_offsets();
	xt_compat_unlock(AF_INET);
	if (ret)
		goto free_newinfo;

	ret = -ELOOP;
	if (!mark_source_chains(newinfo, valid_hooks, entry1))
		goto free_newinfo;

	i = 0;
	ret = IPT_ENTRY_ITERATE(entry1, newinfo->size, compat_check_entry,
								name, &i);
	if (ret) {
		j -= i;
		IPT_ENTRY_ITERATE_CONTINUE(entry1, newinfo->size, i,
						compat_release_entry, &j);
		IPT_ENTRY_ITERATE(entry1, newinfo->size, cleanup_entry, &i);
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
	IPT_ENTRY_ITERATE(entry0, total_size, compat_release_entry, &j);
	return ret;
out_unlock:
	compat_flush_offsets();
	xt_compat_unlock(AF_INET);
	goto out;
}

static int
compat_do_replace(void __user *user, unsigned int len)
{
	int ret;
	struct compat_ipt_replace tmp;
	struct xt_table_info *newinfo;
	void *loc_cpu_entry;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* Hack: Causes ipchains to give correct error msg --RR */
	if (len != sizeof(tmp) + tmp.size)
		return -ENOPROTOOPT;

	/* overflow check */
	if (tmp.size >= (INT_MAX - sizeof(struct xt_table_info)) / NR_CPUS -
			SMP_CACHE_BYTES)
		return -ENOMEM;
	if (tmp.num_counters >= INT_MAX / sizeof(struct xt_counters))
		return -ENOMEM;

	newinfo = xt_alloc_table_info(tmp.size);
	if (!newinfo)
		return -ENOMEM;

	/* choose the copy that is our node/cpu */
	loc_cpu_entry = newinfo->entries[raw_smp_processor_id()];
	if (copy_from_user(loc_cpu_entry, user + sizeof(tmp),
			   tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	ret = translate_compat_table(tmp.name, tmp.valid_hooks,
			      &newinfo, &loc_cpu_entry, tmp.size,
			      tmp.num_entries, tmp.hook_entry, tmp.underflow);
	if (ret != 0)
		goto free_newinfo;

	duprintf("compat_do_replace: Translated table\n");

	ret = __do_replace(tmp.name, tmp.valid_hooks,
			      newinfo, tmp.num_counters,
			      compat_ptr(tmp.counters));
	if (ret)
		goto free_newinfo_untrans;
	return 0;

 free_newinfo_untrans:
	IPT_ENTRY_ITERATE(loc_cpu_entry, newinfo->size, cleanup_entry,NULL);
 free_newinfo:
	xt_free_table_info(newinfo);
	return ret;
}

static int
compat_do_ipt_set_ctl(struct sock *sk,	int cmd, void __user *user,
		unsigned int len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IPT_SO_SET_REPLACE:
		ret = compat_do_replace(user, len);
		break;

	case IPT_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(user, len, 1);
		break;

	default:
		duprintf("do_ipt_set_ctl:  unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

struct compat_ipt_get_entries
{
	char name[IPT_TABLE_MAXNAMELEN];
	compat_uint_t size;
	struct compat_ipt_entry entrytable[0];
};

static int compat_copy_entries_to_user(unsigned int total_size,
		     struct xt_table *table, void __user *userptr)
{
	unsigned int off, num;
	struct compat_ipt_entry e;
	struct xt_counters *counters;
	struct xt_table_info *private = table->private;
	void __user *pos;
	unsigned int size;
	int ret = 0;
	void *loc_cpu_entry;

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
	ret = IPT_ENTRY_ITERATE(loc_cpu_entry, total_size,
			compat_copy_entry_to_user, &pos, &size);
	if (ret)
		goto free_counters;

	/* ... then go back and fix counters and names */
	for (off = 0, num = 0; off < size; off += e.next_offset, num++) {
		unsigned int i;
		struct ipt_entry_match m;
		struct ipt_entry_target t;

		ret = -EFAULT;
		if (copy_from_user(&e, userptr + off,
					sizeof(struct compat_ipt_entry)))
			goto free_counters;
		if (copy_to_user(userptr + off +
			offsetof(struct compat_ipt_entry, counters),
			 &counters[num], sizeof(counters[num])))
			goto free_counters;

		for (i = sizeof(struct compat_ipt_entry);
				i < e.target_offset; i += m.u.match_size) {
			if (copy_from_user(&m, userptr + off + i,
					sizeof(struct ipt_entry_match)))
				goto free_counters;
			if (copy_to_user(userptr + off + i +
				offsetof(struct ipt_entry_match, u.user.name),
				m.u.kernel.match->name,
				strlen(m.u.kernel.match->name) + 1))
				goto free_counters;
		}

		if (copy_from_user(&t, userptr + off + e.target_offset,
					sizeof(struct ipt_entry_target)))
			goto free_counters;
		if (copy_to_user(userptr + off + e.target_offset +
			offsetof(struct ipt_entry_target, u.user.name),
			t.u.kernel.target->name,
			strlen(t.u.kernel.target->name) + 1))
			goto free_counters;
	}
	ret = 0;
free_counters:
	vfree(counters);
	return ret;
}

static int
compat_get_entries(struct compat_ipt_get_entries __user *uptr, int *len)
{
	int ret;
	struct compat_ipt_get_entries get;
	struct xt_table *t;


	if (*len < sizeof(get)) {
		duprintf("compat_get_entries: %u < %u\n",
				*len, (unsigned int)sizeof(get));
		return -EINVAL;
	}

	if (copy_from_user(&get, uptr, sizeof(get)) != 0)
		return -EFAULT;

	if (*len != sizeof(struct compat_ipt_get_entries) + get.size) {
		duprintf("compat_get_entries: %u != %u\n", *len,
			(unsigned int)(sizeof(struct compat_ipt_get_entries) +
			get.size));
		return -EINVAL;
	}

	xt_compat_lock(AF_INET);
	t = xt_find_table_lock(AF_INET, get.name);
	if (t && !IS_ERR(t)) {
		struct xt_table_info *private = t->private;
		struct xt_table_info info;
		duprintf("t->private->number = %u\n",
			 private->number);
		ret = compat_table_info(private, &info);
		if (!ret && get.size == info.size) {
			ret = compat_copy_entries_to_user(private->size,
						   t, uptr->entrytable);
		} else if (!ret) {
			duprintf("compat_get_entries: I've got %u not %u!\n",
				 private->size,
				 get.size);
			ret = -EINVAL;
		}
		compat_flush_offsets();
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = t ? PTR_ERR(t) : -ENOENT;

	xt_compat_unlock(AF_INET);
	return ret;
}

static int do_ipt_get_ctl(struct sock *, int, void __user *, int *);

static int
compat_do_ipt_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IPT_SO_GET_INFO:
		ret = get_info(user, len, 1);
		break;
	case IPT_SO_GET_ENTRIES:
		ret = compat_get_entries(user, len);
		break;
	default:
		ret = do_ipt_get_ctl(sk, cmd, user, len);
	}
	return ret;
}
#endif

static int
do_ipt_set_ctl(struct sock *sk,	int cmd, void __user *user, unsigned int len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IPT_SO_SET_REPLACE:
		ret = do_replace(user, len);
		break;

	case IPT_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(user, len, 0);
		break;

	default:
		duprintf("do_ipt_set_ctl:  unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

static int
do_ipt_get_ctl(struct sock *sk, int cmd, void __user *user, int *len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IPT_SO_GET_INFO:
		ret = get_info(user, len, 0);
		break;

	case IPT_SO_GET_ENTRIES:
		ret = get_entries(user, len);
		break;

	case IPT_SO_GET_REVISION_MATCH:
	case IPT_SO_GET_REVISION_TARGET: {
		struct ipt_get_revision rev;
		int target;

		if (*len != sizeof(rev)) {
			ret = -EINVAL;
			break;
		}
		if (copy_from_user(&rev, user, sizeof(rev)) != 0) {
			ret = -EFAULT;
			break;
		}

		if (cmd == IPT_SO_GET_REVISION_TARGET)
			target = 1;
		else
			target = 0;

		try_then_request_module(xt_find_revision(AF_INET, rev.name,
							 rev.revision,
							 target, &ret),
					"ipt_%s", rev.name);
		break;
	}

	default:
		duprintf("do_ipt_get_ctl: unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

int ipt_register_table(struct xt_table *table, const struct ipt_replace *repl)
{
	int ret;
	struct xt_table_info *newinfo;
	static struct xt_table_info bootstrap
		= { 0, 0, 0, { 0 }, { 0 }, { } };
	void *loc_cpu_entry;

	newinfo = xt_alloc_table_info(repl->size);
	if (!newinfo)
		return -ENOMEM;

	/* choose the copy on our node/cpu
	 * but dont care of preemption
	 */
	loc_cpu_entry = newinfo->entries[raw_smp_processor_id()];
	memcpy(loc_cpu_entry, repl->entries, repl->size);

	ret = translate_table(table->name, table->valid_hooks,
			      newinfo, loc_cpu_entry, repl->size,
			      repl->num_entries,
			      repl->hook_entry,
			      repl->underflow);
	if (ret != 0) {
		xt_free_table_info(newinfo);
		return ret;
	}

	ret = xt_register_table(table, &bootstrap, newinfo);
	if (ret != 0) {
		xt_free_table_info(newinfo);
		return ret;
	}

	return 0;
}

void ipt_unregister_table(struct xt_table *table)
{
	struct xt_table_info *private;
	void *loc_cpu_entry;

	private = xt_unregister_table(table);

	/* Decrease module usage counts and free resources */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	IPT_ENTRY_ITERATE(loc_cpu_entry, private->size, cleanup_entry, NULL);
	xt_free_table_info(private);
}

/* Returns 1 if the type and code is matched by the range, 0 otherwise */
static inline bool
icmp_type_code_match(u_int8_t test_type, u_int8_t min_code, u_int8_t max_code,
		     u_int8_t type, u_int8_t code,
		     bool invert)
{
	return ((test_type == 0xFF) || (type == test_type && code >= min_code && code <= max_code))
		^ invert;
}

static bool
icmp_match(const struct sk_buff *skb,
	   const struct net_device *in,
	   const struct net_device *out,
	   const struct xt_match *match,
	   const void *matchinfo,
	   int offset,
	   unsigned int protoff,
	   bool *hotdrop)
{
	struct icmphdr _icmph, *ic;
	const struct ipt_icmp *icmpinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return false;

	ic = skb_header_pointer(skb, protoff, sizeof(_icmph), &_icmph);
	if (ic == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		duprintf("Dropping evil ICMP tinygram.\n");
		*hotdrop = true;
		return false;
	}

	return icmp_type_code_match(icmpinfo->type,
				    icmpinfo->code[0],
				    icmpinfo->code[1],
				    ic->type, ic->code,
				    !!(icmpinfo->invflags&IPT_ICMP_INV));
}

/* Called when user tries to insert an entry of this type. */
static bool
icmp_checkentry(const char *tablename,
	   const void *info,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int hook_mask)
{
	const struct ipt_icmp *icmpinfo = matchinfo;

	/* Must specify no unknown invflags */
	return !(icmpinfo->invflags & ~IPT_ICMP_INV);
}

/* The built-in targets: standard (NULL) and error. */
static struct xt_target ipt_standard_target __read_mostly = {
	.name		= IPT_STANDARD_TARGET,
	.targetsize	= sizeof(int),
	.family		= AF_INET,
#ifdef CONFIG_COMPAT
	.compatsize	= sizeof(compat_int_t),
	.compat_from_user = compat_standard_from_user,
	.compat_to_user	= compat_standard_to_user,
#endif
};

static struct xt_target ipt_error_target __read_mostly = {
	.name		= IPT_ERROR_TARGET,
	.target		= ipt_error,
	.targetsize	= IPT_FUNCTION_MAXNAMELEN,
	.family		= AF_INET,
};

static struct nf_sockopt_ops ipt_sockopts = {
	.pf		= PF_INET,
	.set_optmin	= IPT_BASE_CTL,
	.set_optmax	= IPT_SO_SET_MAX+1,
	.set		= do_ipt_set_ctl,
#ifdef CONFIG_COMPAT
	.compat_set	= compat_do_ipt_set_ctl,
#endif
	.get_optmin	= IPT_BASE_CTL,
	.get_optmax	= IPT_SO_GET_MAX+1,
	.get		= do_ipt_get_ctl,
#ifdef CONFIG_COMPAT
	.compat_get	= compat_do_ipt_get_ctl,
#endif
	.owner		= THIS_MODULE,
};

static struct xt_match icmp_matchstruct __read_mostly = {
	.name		= "icmp",
	.match		= icmp_match,
	.matchsize	= sizeof(struct ipt_icmp),
	.proto		= IPPROTO_ICMP,
	.family		= AF_INET,
	.checkentry	= icmp_checkentry,
};

static int __init ip_tables_init(void)
{
	int ret;

	ret = xt_proto_init(AF_INET);
	if (ret < 0)
		goto err1;

	/* Noone else will be downing sem now, so we won't sleep */
	ret = xt_register_target(&ipt_standard_target);
	if (ret < 0)
		goto err2;
	ret = xt_register_target(&ipt_error_target);
	if (ret < 0)
		goto err3;
	ret = xt_register_match(&icmp_matchstruct);
	if (ret < 0)
		goto err4;

	/* Register setsockopt */
	ret = nf_register_sockopt(&ipt_sockopts);
	if (ret < 0)
		goto err5;

	printk(KERN_INFO "ip_tables: (C) 2000-2006 Netfilter Core Team\n");
	return 0;

err5:
	xt_unregister_match(&icmp_matchstruct);
err4:
	xt_unregister_target(&ipt_error_target);
err3:
	xt_unregister_target(&ipt_standard_target);
err2:
	xt_proto_fini(AF_INET);
err1:
	return ret;
}

static void __exit ip_tables_fini(void)
{
	nf_unregister_sockopt(&ipt_sockopts);

	xt_unregister_match(&icmp_matchstruct);
	xt_unregister_target(&ipt_error_target);
	xt_unregister_target(&ipt_standard_target);

	xt_proto_fini(AF_INET);
}

EXPORT_SYMBOL(ipt_register_table);
EXPORT_SYMBOL(ipt_unregister_table);
EXPORT_SYMBOL(ipt_do_table);
module_init(ip_tables_init);
module_exit(ip_tables_fini);
