/*
 * Packet matching code.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2005 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 19 Jan 2002 Harald Welte <laforge@gnumonks.org>
 * 	- increase module usage count as soon as we have rules inside
 * 	  a table
 * 08 Oct 2005 Harald Welte <lafore@netfilter.org>
 * 	- Generalize into "x_tables" layer and "{ip,ip6,arp}_tables"
 */
#include <linux/config.h>
#include <linux/cache.h>
#include <linux/capability.h>
#include <linux/skbuff.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/icmp.h>
#include <net/ip.h>
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

static inline int
ip_checkentry(const struct ipt_ip *ip)
{
	if (ip->flags & ~IPT_F_MASK) {
		duprintf("Unknown flag bits set: %08X\n",
			 ip->flags & ~IPT_F_MASK);
		return 0;
	}
	if (ip->invflags & ~IPT_INV_MASK) {
		duprintf("Unknown invflag bits set: %08X\n",
			 ip->invflags & ~IPT_INV_MASK);
		return 0;
	}
	return 1;
}

static unsigned int
ipt_error(struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  unsigned int hooknum,
	  const struct xt_target *target,
	  const void *targinfo,
	  void *userinfo)
{
	if (net_ratelimit())
		printk("ip_tables: error: `%s'\n", (char *)targinfo);

	return NF_DROP;
}

static inline
int do_match(struct ipt_entry_match *m,
	     const struct sk_buff *skb,
	     const struct net_device *in,
	     const struct net_device *out,
	     int offset,
	     int *hotdrop)
{
	/* Stop iteration if it doesn't match */
	if (!m->u.kernel.match->match(skb, in, out, m->u.kernel.match, m->data,
				      offset, skb->nh.iph->ihl*4, hotdrop))
		return 1;
	else
		return 0;
}

static inline struct ipt_entry *
get_entry(void *base, unsigned int offset)
{
	return (struct ipt_entry *)(base + offset);
}

/* Returns one of the generic firewall policies, like NF_ACCEPT. */
unsigned int
ipt_do_table(struct sk_buff **pskb,
	     unsigned int hook,
	     const struct net_device *in,
	     const struct net_device *out,
	     struct ipt_table *table,
	     void *userdata)
{
	static const char nulldevname[IFNAMSIZ] __attribute__((aligned(sizeof(long))));
	u_int16_t offset;
	struct iphdr *ip;
	u_int16_t datalen;
	int hotdrop = 0;
	/* Initializing verdict to NF_DROP keeps gcc happy. */
	unsigned int verdict = NF_DROP;
	const char *indev, *outdev;
	void *table_base;
	struct ipt_entry *e, *back;
	struct xt_table_info *private = table->private;

	/* Initialization */
	ip = (*pskb)->nh.iph;
	datalen = (*pskb)->len - ip->ihl * 4;
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
					      *pskb, in, out,
					      offset, &hotdrop) != 0)
				goto no_match;

			ADD_COUNTER(e->counters, ntohs(ip->tot_len), 1);

			t = ipt_get_target(e);
			IP_NF_ASSERT(t->u.kernel.target);
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
				verdict = t->u.kernel.target->target(pskb,
								     in, out,
								     hook,
								     t->u.kernel.target,
								     t->data,
								     userdata);

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
				ip = (*pskb)->nh.iph;
				datalen = (*pskb)->len - ip->ihl * 4;

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

			if (e->comefrom & (1 << NF_IP_NUMHOOKS)) {
				printk("iptables: loop hook %u pos %u %08X.\n",
				       hook, pos, e->comefrom);
				return 0;
			}
			e->comefrom
				|= ((1 << hook) | (1 << NF_IP_NUMHOOKS));

			/* Unconditional return/END. */
			if (e->target_offset == sizeof(struct ipt_entry)
			    && (strcmp(t->target.u.user.name,
				       IPT_STANDARD_TARGET) == 0)
			    && t->verdict < 0
			    && unconditional(&e->ip)) {
				unsigned int oldpos, size;

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
		m->u.kernel.match->destroy(m->u.kernel.match, m->data,
					   m->u.match_size - sizeof(*m));
	module_put(m->u.kernel.match->me);
	return 0;
}

static inline int
standard_check(const struct ipt_entry_target *t,
	       unsigned int max_offset)
{
	struct ipt_standard_target *targ = (void *)t;

	/* Check standard info. */
	if (targ->verdict >= 0
	    && targ->verdict > max_offset - sizeof(struct ipt_entry)) {
		duprintf("ipt_standard_check: bad verdict (%i)\n",
			 targ->verdict);
		return 0;
	}
	if (targ->verdict < -NF_MAX_VERDICT - 1) {
		duprintf("ipt_standard_check: bad negative verdict (%i)\n",
			 targ->verdict);
		return 0;
	}
	return 1;
}

static inline int
check_match(struct ipt_entry_match *m,
	    const char *name,
	    const struct ipt_ip *ip,
	    unsigned int hookmask,
	    unsigned int *i)
{
	struct ipt_match *match;
	int ret;

	match = try_then_request_module(xt_find_match(AF_INET, m->u.user.name,
						   m->u.user.revision),
					"ipt_%s", m->u.user.name);
	if (IS_ERR(match) || !match) {
		duprintf("check_match: `%s' not found\n", m->u.user.name);
		return match ? PTR_ERR(match) : -ENOENT;
	}
	m->u.kernel.match = match;

	ret = xt_check_match(match, AF_INET, m->u.match_size - sizeof(*m),
			     name, hookmask, ip->proto,
			     ip->invflags & IPT_INV_PROTO);
	if (ret)
		goto err;

	if (m->u.kernel.match->checkentry
	    && !m->u.kernel.match->checkentry(name, ip, match, m->data,
					      m->u.match_size - sizeof(*m),
					      hookmask)) {
		duprintf("ip_tables: check failed for `%s'.\n",
			 m->u.kernel.match->name);
		ret = -EINVAL;
		goto err;
	}

	(*i)++;
	return 0;
err:
	module_put(m->u.kernel.match->me);
	return ret;
}

static struct ipt_target ipt_standard_target;

static inline int
check_entry(struct ipt_entry *e, const char *name, unsigned int size,
	    unsigned int *i)
{
	struct ipt_entry_target *t;
	struct ipt_target *target;
	int ret;
	unsigned int j;

	if (!ip_checkentry(&e->ip)) {
		duprintf("ip_tables: ip check failed %p %s.\n", e, name);
		return -EINVAL;
	}

	j = 0;
	ret = IPT_MATCH_ITERATE(e, check_match, name, &e->ip, e->comefrom, &j);
	if (ret != 0)
		goto cleanup_matches;

	t = ipt_get_target(e);
	target = try_then_request_module(xt_find_target(AF_INET,
						     t->u.user.name,
						     t->u.user.revision),
					 "ipt_%s", t->u.user.name);
	if (IS_ERR(target) || !target) {
		duprintf("check_entry: `%s' not found\n", t->u.user.name);
		ret = target ? PTR_ERR(target) : -ENOENT;
		goto cleanup_matches;
	}
	t->u.kernel.target = target;

	ret = xt_check_target(target, AF_INET, t->u.target_size - sizeof(*t),
			      name, e->comefrom, e->ip.proto,
			      e->ip.invflags & IPT_INV_PROTO);
	if (ret)
		goto err;

	if (t->u.kernel.target == &ipt_standard_target) {
		if (!standard_check(t, size)) {
			ret = -EINVAL;
			goto cleanup_matches;
		}
	} else if (t->u.kernel.target->checkentry
		   && !t->u.kernel.target->checkentry(name, e, target, t->data,
						      t->u.target_size
						      - sizeof(*t),
						      e->comefrom)) {
		duprintf("ip_tables: check failed for `%s'.\n",
			 t->u.kernel.target->name);
		ret = -EINVAL;
		goto err;
	}

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
		t->u.kernel.target->destroy(t->u.kernel.target, t->data,
					    t->u.target_size - sizeof(*t));
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
				check_entry, name, size, &i);

	if (ret != 0) {
		IPT_ENTRY_ITERATE(entry0, newinfo->size,
				  cleanup_entry, &i);
		return ret;
	}

	/* And one copy for every other CPU */
	for_each_cpu(i) {
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

	for_each_cpu(cpu) {
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

static int
copy_entries_to_user(unsigned int total_size,
		     struct ipt_table *table,
		     void __user *userptr)
{
	unsigned int off, num, countersize;
	struct ipt_entry *e;
	struct xt_counters *counters;
	struct xt_table_info *private = table->private;
	int ret = 0;
	void *loc_cpu_entry;

	/* We need atomic snapshot of counters: rest doesn't change
	   (other than comefrom, which userspace doesn't care
	   about). */
	countersize = sizeof(struct xt_counters) * private->number;
	counters = vmalloc_node(countersize, numa_node_id());

	if (counters == NULL)
		return -ENOMEM;

	/* First, sum counters... */
	write_lock_bh(&table->lock);
	get_counters(private, counters);
	write_unlock_bh(&table->lock);

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

static int
get_entries(const struct ipt_get_entries *entries,
	    struct ipt_get_entries __user *uptr)
{
	int ret;
	struct ipt_table *t;

	t = xt_find_table_lock(AF_INET, entries->name);
	if (t && !IS_ERR(t)) {
		struct xt_table_info *private = t->private;
		duprintf("t->private->number = %u\n",
			 private->number);
		if (entries->size == private->size)
			ret = copy_entries_to_user(private->size,
						   t, uptr->entrytable);
		else {
			duprintf("get_entries: I've got %u not %u!\n",
				 private->size,
				 entries->size);
			ret = -EINVAL;
		}
		module_put(t->me);
		xt_table_unlock(t);
	} else
		ret = t ? PTR_ERR(t) : -ENOENT;

	return ret;
}

static int
do_replace(void __user *user, unsigned int len)
{
	int ret;
	struct ipt_replace tmp;
	struct ipt_table *t;
	struct xt_table_info *newinfo, *oldinfo;
	struct xt_counters *counters;
	void *loc_cpu_entry, *loc_cpu_old_entry;

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

	counters = vmalloc(tmp.num_counters * sizeof(struct xt_counters));
	if (!counters) {
		ret = -ENOMEM;
		goto free_newinfo;
	}

	ret = translate_table(tmp.name, tmp.valid_hooks,
			      newinfo, loc_cpu_entry, tmp.size, tmp.num_entries,
			      tmp.hook_entry, tmp.underflow);
	if (ret != 0)
		goto free_newinfo_counters;

	duprintf("ip_tables: Translated table\n");

	t = try_then_request_module(xt_find_table_lock(AF_INET, tmp.name),
				    "iptable_%s", tmp.name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free_newinfo_counters_untrans;
	}

	/* You lied! */
	if (tmp.valid_hooks != t->valid_hooks) {
		duprintf("Valid hook crap: %08X vs %08X\n",
			 tmp.valid_hooks, t->valid_hooks);
		ret = -EINVAL;
		goto put_module;
	}

	oldinfo = xt_replace_table(t, tmp.num_counters, newinfo, &ret);
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
	if (copy_to_user(tmp.counters, counters,
			 sizeof(struct xt_counters) * tmp.num_counters) != 0)
		ret = -EFAULT;
	vfree(counters);
	xt_table_unlock(t);
	return ret;

 put_module:
	module_put(t->me);
	xt_table_unlock(t);
 free_newinfo_counters_untrans:
	IPT_ENTRY_ITERATE(loc_cpu_entry, newinfo->size, cleanup_entry,NULL);
 free_newinfo_counters:
	vfree(counters);
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
do_add_counters(void __user *user, unsigned int len)
{
	unsigned int i;
	struct xt_counters_info tmp, *paddc;
	struct ipt_table *t;
	struct xt_table_info *private;
	int ret = 0;
	void *loc_cpu_entry;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	if (len != sizeof(tmp) + tmp.num_counters*sizeof(struct xt_counters))
		return -EINVAL;

	paddc = vmalloc_node(len, numa_node_id());
	if (!paddc)
		return -ENOMEM;

	if (copy_from_user(paddc, user, len) != 0) {
		ret = -EFAULT;
		goto free;
	}

	t = xt_find_table_lock(AF_INET, tmp.name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free;
	}

	write_lock_bh(&t->lock);
	private = t->private;
	if (private->number != paddc->num_counters) {
		ret = -EINVAL;
		goto unlock_up_free;
	}

	i = 0;
	/* Choose the copy that is on our node */
	loc_cpu_entry = private->entries[raw_smp_processor_id()];
	IPT_ENTRY_ITERATE(loc_cpu_entry,
			  private->size,
			  add_counter_to_entry,
			  paddc->counters,
			  &i);
 unlock_up_free:
	write_unlock_bh(&t->lock);
	xt_table_unlock(t);
	module_put(t->me);
 free:
	vfree(paddc);

	return ret;
}

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
		ret = do_add_counters(user, len);
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
	case IPT_SO_GET_INFO: {
		char name[IPT_TABLE_MAXNAMELEN];
		struct ipt_table *t;

		if (*len != sizeof(struct ipt_getinfo)) {
			duprintf("length %u != %u\n", *len,
				 sizeof(struct ipt_getinfo));
			ret = -EINVAL;
			break;
		}

		if (copy_from_user(name, user, sizeof(name)) != 0) {
			ret = -EFAULT;
			break;
		}
		name[IPT_TABLE_MAXNAMELEN-1] = '\0';

		t = try_then_request_module(xt_find_table_lock(AF_INET, name),
					    "iptable_%s", name);
		if (t && !IS_ERR(t)) {
			struct ipt_getinfo info;
			struct xt_table_info *private = t->private;

			info.valid_hooks = t->valid_hooks;
			memcpy(info.hook_entry, private->hook_entry,
			       sizeof(info.hook_entry));
			memcpy(info.underflow, private->underflow,
			       sizeof(info.underflow));
			info.num_entries = private->number;
			info.size = private->size;
			memcpy(info.name, name, sizeof(info.name));

			if (copy_to_user(user, &info, *len) != 0)
				ret = -EFAULT;
			else
				ret = 0;
			xt_table_unlock(t);
			module_put(t->me);
		} else
			ret = t ? PTR_ERR(t) : -ENOENT;
	}
	break;

	case IPT_SO_GET_ENTRIES: {
		struct ipt_get_entries get;

		if (*len < sizeof(get)) {
			duprintf("get_entries: %u < %u\n", *len, sizeof(get));
			ret = -EINVAL;
		} else if (copy_from_user(&get, user, sizeof(get)) != 0) {
			ret = -EFAULT;
		} else if (*len != sizeof(struct ipt_get_entries) + get.size) {
			duprintf("get_entries: %u != %u\n", *len,
				 sizeof(struct ipt_get_entries) + get.size);
			ret = -EINVAL;
		} else
			ret = get_entries(&get, user);
		break;
	}

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

	if (xt_register_table(table, &bootstrap, newinfo) != 0) {
		xt_free_table_info(newinfo);
		return ret;
	}

	return 0;
}

void ipt_unregister_table(struct ipt_table *table)
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
static inline int
icmp_type_code_match(u_int8_t test_type, u_int8_t min_code, u_int8_t max_code,
		     u_int8_t type, u_int8_t code,
		     int invert)
{
	return ((test_type == 0xFF) || (type == test_type && code >= min_code && code <= max_code))
		^ invert;
}

static int
icmp_match(const struct sk_buff *skb,
	   const struct net_device *in,
	   const struct net_device *out,
	   const struct xt_match *match,
	   const void *matchinfo,
	   int offset,
	   unsigned int protoff,
	   int *hotdrop)
{
	struct icmphdr _icmph, *ic;
	const struct ipt_icmp *icmpinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	ic = skb_header_pointer(skb, protoff, sizeof(_icmph), &_icmph);
	if (ic == NULL) {
		/* We've been asked to examine this packet, and we
		 * can't.  Hence, no choice but to drop.
		 */
		duprintf("Dropping evil ICMP tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return icmp_type_code_match(icmpinfo->type,
				    icmpinfo->code[0],
				    icmpinfo->code[1],
				    ic->type, ic->code,
				    !!(icmpinfo->invflags&IPT_ICMP_INV));
}

/* Called when user tries to insert an entry of this type. */
static int
icmp_checkentry(const char *tablename,
	   const void *info,
	   const struct xt_match *match,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	const struct ipt_icmp *icmpinfo = matchinfo;

	/* Must specify no unknown invflags */
	return !(icmpinfo->invflags & ~IPT_ICMP_INV);
}

/* The built-in targets: standard (NULL) and error. */
static struct ipt_target ipt_standard_target = {
	.name		= IPT_STANDARD_TARGET,
	.targetsize	= sizeof(int),
	.family		= AF_INET,
};

static struct ipt_target ipt_error_target = {
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
	.get_optmin	= IPT_BASE_CTL,
	.get_optmax	= IPT_SO_GET_MAX+1,
	.get		= do_ipt_get_ctl,
};

static struct ipt_match icmp_matchstruct = {
	.name		= "icmp",
	.match		= icmp_match,
	.matchsize	= sizeof(struct ipt_icmp),
	.proto		= IPPROTO_ICMP,
	.family		= AF_INET,
	.checkentry	= icmp_checkentry,
};

static int __init init(void)
{
	int ret;

	xt_proto_init(AF_INET);

	/* Noone else will be downing sem now, so we won't sleep */
	xt_register_target(&ipt_standard_target);
	xt_register_target(&ipt_error_target);
	xt_register_match(&icmp_matchstruct);

	/* Register setsockopt */
	ret = nf_register_sockopt(&ipt_sockopts);
	if (ret < 0) {
		duprintf("Unable to register sockopts.\n");
		return ret;
	}

	printk("ip_tables: (C) 2000-2006 Netfilter Core Team\n");
	return 0;
}

static void __exit fini(void)
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
module_init(init);
module_exit(fini);
