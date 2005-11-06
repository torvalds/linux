/*
 * Packet matching code.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 19 Jan 2002 Harald Welte <laforge@gnumonks.org>
 * 	- increase module usage count as soon as we have rules inside
 * 	  a table
 */
#include <linux/config.h>
#include <linux/cache.h>
#include <linux/skbuff.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <net/ip.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <linux/proc_fs.h>
#include <linux/err.h>
#include <linux/cpumask.h>

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
#define SMP_ALIGN(x) (((x) + SMP_CACHE_BYTES-1) & ~(SMP_CACHE_BYTES-1))

static DECLARE_MUTEX(ipt_mutex);

/* Must have mutex */
#define ASSERT_READ_LOCK(x) IP_NF_ASSERT(down_trylock(&ipt_mutex) != 0)
#define ASSERT_WRITE_LOCK(x) IP_NF_ASSERT(down_trylock(&ipt_mutex) != 0)
#include <linux/netfilter_ipv4/listhelp.h>

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

   To be cache friendly on SMP, we arrange them like so:
   [ n-entries ]
   ... cache-align padding ...
   [ n-entries ]

   Hence the start of any table is given by get_table() below.  */

/* The table itself */
struct ipt_table_info
{
	/* Size per table */
	unsigned int size;
	/* Number of entries: FIXME. --RR */
	unsigned int number;
	/* Initial number of entries. Needed for module usage count */
	unsigned int initial_entries;

	/* Entry points and underflows */
	unsigned int hook_entry[NF_IP_NUMHOOKS];
	unsigned int underflow[NF_IP_NUMHOOKS];

	/* ipt_entry tables: one per CPU */
	char entries[0] ____cacheline_aligned;
};

static LIST_HEAD(ipt_target);
static LIST_HEAD(ipt_match);
static LIST_HEAD(ipt_tables);
#define ADD_COUNTER(c,b,p) do { (c).bcnt += (b); (c).pcnt += (p); } while(0)

#ifdef CONFIG_SMP
#define TABLE_OFFSET(t,p) (SMP_ALIGN((t)->size)*(p))
#else
#define TABLE_OFFSET(t,p) 0
#endif

#if 0
#define down(x) do { printk("DOWN:%u:" #x "\n", __LINE__); down(x); } while(0)
#define down_interruptible(x) ({ int __r; printk("DOWNi:%u:" #x "\n", __LINE__); __r = down_interruptible(x); if (__r != 0) printk("ABORT-DOWNi:%u\n", __LINE__); __r; })
#define up(x) do { printk("UP:%u:" #x "\n", __LINE__); up(x); } while(0)
#endif

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
	if (!m->u.kernel.match->match(skb, in, out, m->data, offset, hotdrop))
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
	table_base = (void *)table->private->entries
		+ TABLE_OFFSET(table->private, smp_processor_id());
	e = get_entry(table_base, table->private->hook_entry[hook]);

#ifdef CONFIG_NETFILTER_DEBUG
	/* Check noone else using our table */
	if (((struct ipt_entry *)table_base)->comefrom != 0xdead57ac
	    && ((struct ipt_entry *)table_base)->comefrom != 0xeeeeeeec) {
		printk("ASSERT: CPU #%u, %s comefrom(%p) = %X\n",
		       smp_processor_id(),
		       table->name,
		       &((struct ipt_entry *)table_base)->comefrom,
		       ((struct ipt_entry *)table_base)->comefrom);
	}
	((struct ipt_entry *)table_base)->comefrom = 0x57acc001;
#endif

	/* For return from builtin chain */
	back = get_entry(table_base, table->private->underflow[hook]);

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

#ifdef CONFIG_NETFILTER_DEBUG
	((struct ipt_entry *)table_base)->comefrom = 0xdead57ac;
#endif
	read_unlock_bh(&table->lock);

#ifdef DEBUG_ALLOW_ALL
	return NF_ACCEPT;
#else
	if (hotdrop)
		return NF_DROP;
	else return verdict;
#endif
}

/*
 * These are weird, but module loading must not be done with mutex
 * held (since they will register), and we have to have a single
 * function to use try_then_request_module().
 */

/* Find table by name, grabs mutex & ref.  Returns ERR_PTR() on error. */
static inline struct ipt_table *find_table_lock(const char *name)
{
	struct ipt_table *t;

	if (down_interruptible(&ipt_mutex) != 0)
		return ERR_PTR(-EINTR);

	list_for_each_entry(t, &ipt_tables, list)
		if (strcmp(t->name, name) == 0 && try_module_get(t->me))
			return t;
	up(&ipt_mutex);
	return NULL;
}

/* Find match, grabs ref.  Returns ERR_PTR() on error. */
static inline struct ipt_match *find_match(const char *name, u8 revision)
{
	struct ipt_match *m;
	int err = 0;

	if (down_interruptible(&ipt_mutex) != 0)
		return ERR_PTR(-EINTR);

	list_for_each_entry(m, &ipt_match, list) {
		if (strcmp(m->name, name) == 0) {
			if (m->revision == revision) {
				if (try_module_get(m->me)) {
					up(&ipt_mutex);
					return m;
				}
			} else
				err = -EPROTOTYPE; /* Found something. */
		}
	}
	up(&ipt_mutex);
	return ERR_PTR(err);
}

/* Find target, grabs ref.  Returns ERR_PTR() on error. */
static inline struct ipt_target *find_target(const char *name, u8 revision)
{
	struct ipt_target *t;
	int err = 0;

	if (down_interruptible(&ipt_mutex) != 0)
		return ERR_PTR(-EINTR);

	list_for_each_entry(t, &ipt_target, list) {
		if (strcmp(t->name, name) == 0) {
			if (t->revision == revision) {
				if (try_module_get(t->me)) {
					up(&ipt_mutex);
					return t;
				}
			} else
				err = -EPROTOTYPE; /* Found something. */
		}
	}
	up(&ipt_mutex);
	return ERR_PTR(err);
}

struct ipt_target *ipt_find_target(const char *name, u8 revision)
{
	struct ipt_target *target;

	target = try_then_request_module(find_target(name, revision),
					 "ipt_%s", name);
	if (IS_ERR(target) || !target)
		return NULL;
	return target;
}

static int match_revfn(const char *name, u8 revision, int *bestp)
{
	struct ipt_match *m;
	int have_rev = 0;

	list_for_each_entry(m, &ipt_match, list) {
		if (strcmp(m->name, name) == 0) {
			if (m->revision > *bestp)
				*bestp = m->revision;
			if (m->revision == revision)
				have_rev = 1;
		}
	}
	return have_rev;
}

static int target_revfn(const char *name, u8 revision, int *bestp)
{
	struct ipt_target *t;
	int have_rev = 0;

	list_for_each_entry(t, &ipt_target, list) {
		if (strcmp(t->name, name) == 0) {
			if (t->revision > *bestp)
				*bestp = t->revision;
			if (t->revision == revision)
				have_rev = 1;
		}
	}
	return have_rev;
}

/* Returns true or false (if no such extension at all) */
static inline int find_revision(const char *name, u8 revision,
				int (*revfn)(const char *, u8, int *),
				int *err)
{
	int have_rev, best = -1;

	if (down_interruptible(&ipt_mutex) != 0) {
		*err = -EINTR;
		return 1;
	}
	have_rev = revfn(name, revision, &best);
	up(&ipt_mutex);

	/* Nothing at all?  Return 0 to try loading module. */
	if (best == -1) {
		*err = -ENOENT;
		return 0;
	}

	*err = best;
	if (!have_rev)
		*err = -EPROTONOSUPPORT;
	return 1;
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
mark_source_chains(struct ipt_table_info *newinfo, unsigned int valid_hooks)
{
	unsigned int hook;

	/* No recursion; use packet counter to save back ptrs (reset
	   to 0 as we leave), and comefrom to save source hook bitmask */
	for (hook = 0; hook < NF_IP_NUMHOOKS; hook++) {
		unsigned int pos = newinfo->hook_entry[hook];
		struct ipt_entry *e
			= (struct ipt_entry *)(newinfo->entries + pos);

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
						(newinfo->entries + pos);
				} while (oldpos == pos + e->next_offset);

				/* Move along one */
				size = e->next_offset;
				e = (struct ipt_entry *)
					(newinfo->entries + pos + size);
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
					(newinfo->entries + newpos);
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
		m->u.kernel.match->destroy(m->data,
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
	if (t->u.target_size
	    != IPT_ALIGN(sizeof(struct ipt_standard_target))) {
		duprintf("standard_check: target size %u != %u\n",
			 t->u.target_size,
			 IPT_ALIGN(sizeof(struct ipt_standard_target)));
		return 0;
	}

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

	match = try_then_request_module(find_match(m->u.user.name,
						   m->u.user.revision),
					"ipt_%s", m->u.user.name);
	if (IS_ERR(match) || !match) {
		duprintf("check_match: `%s' not found\n", m->u.user.name);
		return match ? PTR_ERR(match) : -ENOENT;
	}
	m->u.kernel.match = match;

	if (m->u.kernel.match->checkentry
	    && !m->u.kernel.match->checkentry(name, ip, m->data,
					      m->u.match_size - sizeof(*m),
					      hookmask)) {
		module_put(m->u.kernel.match->me);
		duprintf("ip_tables: check failed for `%s'.\n",
			 m->u.kernel.match->name);
		return -EINVAL;
	}

	(*i)++;
	return 0;
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
	target = try_then_request_module(find_target(t->u.user.name,
						     t->u.user.revision),
					 "ipt_%s", t->u.user.name);
	if (IS_ERR(target) || !target) {
		duprintf("check_entry: `%s' not found\n", t->u.user.name);
		ret = target ? PTR_ERR(target) : -ENOENT;
		goto cleanup_matches;
	}
	t->u.kernel.target = target;

	if (t->u.kernel.target == &ipt_standard_target) {
		if (!standard_check(t, size)) {
			ret = -EINVAL;
			goto cleanup_matches;
		}
	} else if (t->u.kernel.target->checkentry
		   && !t->u.kernel.target->checkentry(name, e, t->data,
						      t->u.target_size
						      - sizeof(*t),
						      e->comefrom)) {
		module_put(t->u.kernel.target->me);
		duprintf("ip_tables: check failed for `%s'.\n",
			 t->u.kernel.target->name);
		ret = -EINVAL;
		goto cleanup_matches;
	}

	(*i)++;
	return 0;

 cleanup_matches:
	IPT_MATCH_ITERATE(e, cleanup_match, &j);
	return ret;
}

static inline int
check_entry_size_and_hooks(struct ipt_entry *e,
			   struct ipt_table_info *newinfo,
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
	e->counters = ((struct ipt_counters) { 0, 0 });
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
		t->u.kernel.target->destroy(t->data,
					    t->u.target_size - sizeof(*t));
	module_put(t->u.kernel.target->me);
	return 0;
}

/* Checks and translates the user-supplied table segment (held in
   newinfo) */
static int
translate_table(const char *name,
		unsigned int valid_hooks,
		struct ipt_table_info *newinfo,
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
	ret = IPT_ENTRY_ITERATE(newinfo->entries, newinfo->size,
				check_entry_size_and_hooks,
				newinfo,
				newinfo->entries,
				newinfo->entries + size,
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

	if (!mark_source_chains(newinfo, valid_hooks))
		return -ELOOP;

	/* Finally, each sanity check must pass */
	i = 0;
	ret = IPT_ENTRY_ITERATE(newinfo->entries, newinfo->size,
				check_entry, name, size, &i);

	if (ret != 0) {
		IPT_ENTRY_ITERATE(newinfo->entries, newinfo->size,
				  cleanup_entry, &i);
		return ret;
	}

	/* And one copy for every other CPU */
	for_each_cpu(i) {
		if (i == 0)
			continue;
		memcpy(newinfo->entries + SMP_ALIGN(newinfo->size) * i,
		       newinfo->entries,
		       SMP_ALIGN(newinfo->size));
	}

	return ret;
}

static struct ipt_table_info *
replace_table(struct ipt_table *table,
	      unsigned int num_counters,
	      struct ipt_table_info *newinfo,
	      int *error)
{
	struct ipt_table_info *oldinfo;

#ifdef CONFIG_NETFILTER_DEBUG
	{
		struct ipt_entry *table_base;
		unsigned int i;

		for_each_cpu(i) {
			table_base =
				(void *)newinfo->entries
				+ TABLE_OFFSET(newinfo, i);

			table_base->comefrom = 0xdead57ac;
		}
	}
#endif

	/* Do the substitution. */
	write_lock_bh(&table->lock);
	/* Check inside lock: is the old number correct? */
	if (num_counters != table->private->number) {
		duprintf("num_counters != table->private->number (%u/%u)\n",
			 num_counters, table->private->number);
		write_unlock_bh(&table->lock);
		*error = -EAGAIN;
		return NULL;
	}
	oldinfo = table->private;
	table->private = newinfo;
	newinfo->initial_entries = oldinfo->initial_entries;
	write_unlock_bh(&table->lock);

	return oldinfo;
}

/* Gets counters. */
static inline int
add_entry_to_counter(const struct ipt_entry *e,
		     struct ipt_counters total[],
		     unsigned int *i)
{
	ADD_COUNTER(total[*i], e->counters.bcnt, e->counters.pcnt);

	(*i)++;
	return 0;
}

static void
get_counters(const struct ipt_table_info *t,
	     struct ipt_counters counters[])
{
	unsigned int cpu;
	unsigned int i;

	for_each_cpu(cpu) {
		i = 0;
		IPT_ENTRY_ITERATE(t->entries + TABLE_OFFSET(t, cpu),
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
	struct ipt_counters *counters;
	int ret = 0;

	/* We need atomic snapshot of counters: rest doesn't change
	   (other than comefrom, which userspace doesn't care
	   about). */
	countersize = sizeof(struct ipt_counters) * table->private->number;
	counters = vmalloc(countersize);

	if (counters == NULL)
		return -ENOMEM;

	/* First, sum counters... */
	memset(counters, 0, countersize);
	write_lock_bh(&table->lock);
	get_counters(table->private, counters);
	write_unlock_bh(&table->lock);

	/* ... then copy entire thing from CPU 0... */
	if (copy_to_user(userptr, table->private->entries, total_size) != 0) {
		ret = -EFAULT;
		goto free_counters;
	}

	/* FIXME: use iterator macros --RR */
	/* ... then go back and fix counters and names */
	for (off = 0, num = 0; off < total_size; off += e->next_offset, num++){
		unsigned int i;
		struct ipt_entry_match *m;
		struct ipt_entry_target *t;

		e = (struct ipt_entry *)(table->private->entries + off);
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

	t = find_table_lock(entries->name);
	if (t && !IS_ERR(t)) {
		duprintf("t->private->number = %u\n",
			 t->private->number);
		if (entries->size == t->private->size)
			ret = copy_entries_to_user(t->private->size,
						   t, uptr->entrytable);
		else {
			duprintf("get_entries: I've got %u not %u!\n",
				 t->private->size,
				 entries->size);
			ret = -EINVAL;
		}
		module_put(t->me);
		up(&ipt_mutex);
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
	struct ipt_table_info *newinfo, *oldinfo;
	struct ipt_counters *counters;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* Hack: Causes ipchains to give correct error msg --RR */
	if (len != sizeof(tmp) + tmp.size)
		return -ENOPROTOOPT;

	/* Pedantry: prevent them from hitting BUG() in vmalloc.c --RR */
	if ((SMP_ALIGN(tmp.size) >> PAGE_SHIFT) + 2 > num_physpages)
		return -ENOMEM;

	newinfo = vmalloc(sizeof(struct ipt_table_info)
			  + SMP_ALIGN(tmp.size) * 
			  	(highest_possible_processor_id()+1));
	if (!newinfo)
		return -ENOMEM;

	if (copy_from_user(newinfo->entries, user + sizeof(tmp),
			   tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	counters = vmalloc(tmp.num_counters * sizeof(struct ipt_counters));
	if (!counters) {
		ret = -ENOMEM;
		goto free_newinfo;
	}
	memset(counters, 0, tmp.num_counters * sizeof(struct ipt_counters));

	ret = translate_table(tmp.name, tmp.valid_hooks,
			      newinfo, tmp.size, tmp.num_entries,
			      tmp.hook_entry, tmp.underflow);
	if (ret != 0)
		goto free_newinfo_counters;

	duprintf("ip_tables: Translated table\n");

	t = try_then_request_module(find_table_lock(tmp.name),
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

	oldinfo = replace_table(t, tmp.num_counters, newinfo, &ret);
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
	IPT_ENTRY_ITERATE(oldinfo->entries, oldinfo->size, cleanup_entry,NULL);
	vfree(oldinfo);
	if (copy_to_user(tmp.counters, counters,
			 sizeof(struct ipt_counters) * tmp.num_counters) != 0)
		ret = -EFAULT;
	vfree(counters);
	up(&ipt_mutex);
	return ret;

 put_module:
	module_put(t->me);
	up(&ipt_mutex);
 free_newinfo_counters_untrans:
	IPT_ENTRY_ITERATE(newinfo->entries, newinfo->size, cleanup_entry,NULL);
 free_newinfo_counters:
	vfree(counters);
 free_newinfo:
	vfree(newinfo);
	return ret;
}

/* We're lazy, and add to the first CPU; overflow works its fey magic
 * and everything is OK. */
static inline int
add_counter_to_entry(struct ipt_entry *e,
		     const struct ipt_counters addme[],
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
	struct ipt_counters_info tmp, *paddc;
	struct ipt_table *t;
	int ret = 0;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	if (len != sizeof(tmp) + tmp.num_counters*sizeof(struct ipt_counters))
		return -EINVAL;

	paddc = vmalloc(len);
	if (!paddc)
		return -ENOMEM;

	if (copy_from_user(paddc, user, len) != 0) {
		ret = -EFAULT;
		goto free;
	}

	t = find_table_lock(tmp.name);
	if (!t || IS_ERR(t)) {
		ret = t ? PTR_ERR(t) : -ENOENT;
		goto free;
	}

	write_lock_bh(&t->lock);
	if (t->private->number != paddc->num_counters) {
		ret = -EINVAL;
		goto unlock_up_free;
	}

	i = 0;
	IPT_ENTRY_ITERATE(t->private->entries,
			  t->private->size,
			  add_counter_to_entry,
			  paddc->counters,
			  &i);
 unlock_up_free:
	write_unlock_bh(&t->lock);
	up(&ipt_mutex);
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

		t = try_then_request_module(find_table_lock(name),
					    "iptable_%s", name);
		if (t && !IS_ERR(t)) {
			struct ipt_getinfo info;

			info.valid_hooks = t->valid_hooks;
			memcpy(info.hook_entry, t->private->hook_entry,
			       sizeof(info.hook_entry));
			memcpy(info.underflow, t->private->underflow,
			       sizeof(info.underflow));
			info.num_entries = t->private->number;
			info.size = t->private->size;
			memcpy(info.name, name, sizeof(info.name));

			if (copy_to_user(user, &info, *len) != 0)
				ret = -EFAULT;
			else
				ret = 0;
			up(&ipt_mutex);
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
		int (*revfn)(const char *, u8, int *);

		if (*len != sizeof(rev)) {
			ret = -EINVAL;
			break;
		}
		if (copy_from_user(&rev, user, sizeof(rev)) != 0) {
			ret = -EFAULT;
			break;
		}

		if (cmd == IPT_SO_GET_REVISION_TARGET)
			revfn = target_revfn;
		else
			revfn = match_revfn;

		try_then_request_module(find_revision(rev.name, rev.revision,
						      revfn, &ret),
					"ipt_%s", rev.name);
		break;
	}

	default:
		duprintf("do_ipt_get_ctl: unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

/* Registration hooks for targets. */
int
ipt_register_target(struct ipt_target *target)
{
	int ret;

	ret = down_interruptible(&ipt_mutex);
	if (ret != 0)
		return ret;
	list_add(&target->list, &ipt_target);
	up(&ipt_mutex);
	return ret;
}

void
ipt_unregister_target(struct ipt_target *target)
{
	down(&ipt_mutex);
	LIST_DELETE(&ipt_target, target);
	up(&ipt_mutex);
}

int
ipt_register_match(struct ipt_match *match)
{
	int ret;

	ret = down_interruptible(&ipt_mutex);
	if (ret != 0)
		return ret;

	list_add(&match->list, &ipt_match);
	up(&ipt_mutex);

	return ret;
}

void
ipt_unregister_match(struct ipt_match *match)
{
	down(&ipt_mutex);
	LIST_DELETE(&ipt_match, match);
	up(&ipt_mutex);
}

int ipt_register_table(struct ipt_table *table, const struct ipt_replace *repl)
{
	int ret;
	struct ipt_table_info *newinfo;
	static struct ipt_table_info bootstrap
		= { 0, 0, 0, { 0 }, { 0 }, { } };

	newinfo = vmalloc(sizeof(struct ipt_table_info)
			  + SMP_ALIGN(repl->size) * 
			  		(highest_possible_processor_id()+1));
	if (!newinfo)
		return -ENOMEM;

	memcpy(newinfo->entries, repl->entries, repl->size);

	ret = translate_table(table->name, table->valid_hooks,
			      newinfo, repl->size,
			      repl->num_entries,
			      repl->hook_entry,
			      repl->underflow);
	if (ret != 0) {
		vfree(newinfo);
		return ret;
	}

	ret = down_interruptible(&ipt_mutex);
	if (ret != 0) {
		vfree(newinfo);
		return ret;
	}

	/* Don't autoload: we'd eat our tail... */
	if (list_named_find(&ipt_tables, table->name)) {
		ret = -EEXIST;
		goto free_unlock;
	}

	/* Simplifies replace_table code. */
	table->private = &bootstrap;
	if (!replace_table(table, 0, newinfo, &ret))
		goto free_unlock;

	duprintf("table->private->number = %u\n",
		 table->private->number);
	
	/* save number of initial entries */
	table->private->initial_entries = table->private->number;

	rwlock_init(&table->lock);
	list_prepend(&ipt_tables, table);

 unlock:
	up(&ipt_mutex);
	return ret;

 free_unlock:
	vfree(newinfo);
	goto unlock;
}

void ipt_unregister_table(struct ipt_table *table)
{
	down(&ipt_mutex);
	LIST_DELETE(&ipt_tables, table);
	up(&ipt_mutex);

	/* Decrease module usage counts and free resources */
	IPT_ENTRY_ITERATE(table->private->entries, table->private->size,
			  cleanup_entry, NULL);
	vfree(table->private);
}

/* Returns 1 if the port is matched by the range, 0 otherwise */
static inline int
port_match(u_int16_t min, u_int16_t max, u_int16_t port, int invert)
{
	int ret;

	ret = (port >= min && port <= max) ^ invert;
	return ret;
}

static int
tcp_find_option(u_int8_t option,
		const struct sk_buff *skb,
		unsigned int optlen,
		int invert,
		int *hotdrop)
{
	/* tcp.doff is only 4 bits, ie. max 15 * 4 bytes */
	u_int8_t _opt[60 - sizeof(struct tcphdr)], *op;
	unsigned int i;

	duprintf("tcp_match: finding option\n");

	if (!optlen)
		return invert;

	/* If we don't have the whole header, drop packet. */
	op = skb_header_pointer(skb,
				skb->nh.iph->ihl*4 + sizeof(struct tcphdr),
				optlen, _opt);
	if (op == NULL) {
		*hotdrop = 1;
		return 0;
	}

	for (i = 0; i < optlen; ) {
		if (op[i] == option) return !invert;
		if (op[i] < 2) i++;
		else i += op[i+1]?:1;
	}

	return invert;
}

static int
tcp_match(const struct sk_buff *skb,
	  const struct net_device *in,
	  const struct net_device *out,
	  const void *matchinfo,
	  int offset,
	  int *hotdrop)
{
	struct tcphdr _tcph, *th;
	const struct ipt_tcp *tcpinfo = matchinfo;

	if (offset) {
		/* To quote Alan:

		   Don't allow a fragment of TCP 8 bytes in. Nobody normal
		   causes this. Its a cracker trying to break in by doing a
		   flag overwrite to pass the direction checks.
		*/
		if (offset == 1) {
			duprintf("Dropping evil TCP offset=1 frag.\n");
			*hotdrop = 1;
		}
		/* Must not be a fragment. */
		return 0;
	}

#define FWINVTCP(bool,invflg) ((bool) ^ !!(tcpinfo->invflags & invflg))

	th = skb_header_pointer(skb, skb->nh.iph->ihl*4,
				sizeof(_tcph), &_tcph);
	if (th == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil TCP offset=0 tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	if (!port_match(tcpinfo->spts[0], tcpinfo->spts[1],
			ntohs(th->source),
			!!(tcpinfo->invflags & IPT_TCP_INV_SRCPT)))
		return 0;
	if (!port_match(tcpinfo->dpts[0], tcpinfo->dpts[1],
			ntohs(th->dest),
			!!(tcpinfo->invflags & IPT_TCP_INV_DSTPT)))
		return 0;
	if (!FWINVTCP((((unsigned char *)th)[13] & tcpinfo->flg_mask)
		      == tcpinfo->flg_cmp,
		      IPT_TCP_INV_FLAGS))
		return 0;
	if (tcpinfo->option) {
		if (th->doff * 4 < sizeof(_tcph)) {
			*hotdrop = 1;
			return 0;
		}
		if (!tcp_find_option(tcpinfo->option, skb,
				     th->doff*4 - sizeof(_tcph),
				     tcpinfo->invflags & IPT_TCP_INV_OPTION,
				     hotdrop))
			return 0;
	}
	return 1;
}

/* Called when user tries to insert an entry of this type. */
static int
tcp_checkentry(const char *tablename,
	       const struct ipt_ip *ip,
	       void *matchinfo,
	       unsigned int matchsize,
	       unsigned int hook_mask)
{
	const struct ipt_tcp *tcpinfo = matchinfo;

	/* Must specify proto == TCP, and no unknown invflags */
	return ip->proto == IPPROTO_TCP
		&& !(ip->invflags & IPT_INV_PROTO)
		&& matchsize == IPT_ALIGN(sizeof(struct ipt_tcp))
		&& !(tcpinfo->invflags & ~IPT_TCP_INV_MASK);
}

static int
udp_match(const struct sk_buff *skb,
	  const struct net_device *in,
	  const struct net_device *out,
	  const void *matchinfo,
	  int offset,
	  int *hotdrop)
{
	struct udphdr _udph, *uh;
	const struct ipt_udp *udpinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	uh = skb_header_pointer(skb, skb->nh.iph->ihl*4,
				sizeof(_udph), &_udph);
	if (uh == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil UDP tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return port_match(udpinfo->spts[0], udpinfo->spts[1],
			  ntohs(uh->source),
			  !!(udpinfo->invflags & IPT_UDP_INV_SRCPT))
		&& port_match(udpinfo->dpts[0], udpinfo->dpts[1],
			      ntohs(uh->dest),
			      !!(udpinfo->invflags & IPT_UDP_INV_DSTPT));
}

/* Called when user tries to insert an entry of this type. */
static int
udp_checkentry(const char *tablename,
	       const struct ipt_ip *ip,
	       void *matchinfo,
	       unsigned int matchinfosize,
	       unsigned int hook_mask)
{
	const struct ipt_udp *udpinfo = matchinfo;

	/* Must specify proto == UDP, and no unknown invflags */
	if (ip->proto != IPPROTO_UDP || (ip->invflags & IPT_INV_PROTO)) {
		duprintf("ipt_udp: Protocol %u != %u\n", ip->proto,
			 IPPROTO_UDP);
		return 0;
	}
	if (matchinfosize != IPT_ALIGN(sizeof(struct ipt_udp))) {
		duprintf("ipt_udp: matchsize %u != %u\n",
			 matchinfosize, IPT_ALIGN(sizeof(struct ipt_udp)));
		return 0;
	}
	if (udpinfo->invflags & ~IPT_UDP_INV_MASK) {
		duprintf("ipt_udp: unknown flags %X\n",
			 udpinfo->invflags);
		return 0;
	}

	return 1;
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
	   const void *matchinfo,
	   int offset,
	   int *hotdrop)
{
	struct icmphdr _icmph, *ic;
	const struct ipt_icmp *icmpinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	ic = skb_header_pointer(skb, skb->nh.iph->ihl*4,
				sizeof(_icmph), &_icmph);
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
	   const struct ipt_ip *ip,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	const struct ipt_icmp *icmpinfo = matchinfo;

	/* Must specify proto == ICMP, and no unknown invflags */
	return ip->proto == IPPROTO_ICMP
		&& !(ip->invflags & IPT_INV_PROTO)
		&& matchsize == IPT_ALIGN(sizeof(struct ipt_icmp))
		&& !(icmpinfo->invflags & ~IPT_ICMP_INV);
}

/* The built-in targets: standard (NULL) and error. */
static struct ipt_target ipt_standard_target = {
	.name		= IPT_STANDARD_TARGET,
};

static struct ipt_target ipt_error_target = {
	.name		= IPT_ERROR_TARGET,
	.target		= ipt_error,
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

static struct ipt_match tcp_matchstruct = {
	.name		= "tcp",
	.match		= &tcp_match,
	.checkentry	= &tcp_checkentry,
};

static struct ipt_match udp_matchstruct = {
	.name		= "udp",
	.match		= &udp_match,
	.checkentry	= &udp_checkentry,
};

static struct ipt_match icmp_matchstruct = {
	.name		= "icmp",
	.match		= &icmp_match,
	.checkentry	= &icmp_checkentry,
};

#ifdef CONFIG_PROC_FS
static inline int print_name(const char *i,
			     off_t start_offset, char *buffer, int length,
			     off_t *pos, unsigned int *count)
{
	if ((*count)++ >= start_offset) {
		unsigned int namelen;

		namelen = sprintf(buffer + *pos, "%s\n",
				  i + sizeof(struct list_head));
		if (*pos + namelen > length) {
			/* Stop iterating */
			return 1;
		}
		*pos += namelen;
	}
	return 0;
}

static inline int print_target(const struct ipt_target *t,
                               off_t start_offset, char *buffer, int length,
                               off_t *pos, unsigned int *count)
{
	if (t == &ipt_standard_target || t == &ipt_error_target)
		return 0;
	return print_name((char *)t, start_offset, buffer, length, pos, count);
}

static int ipt_get_tables(char *buffer, char **start, off_t offset, int length)
{
	off_t pos = 0;
	unsigned int count = 0;

	if (down_interruptible(&ipt_mutex) != 0)
		return 0;

	LIST_FIND(&ipt_tables, print_name, void *,
		  offset, buffer, length, &pos, &count);

	up(&ipt_mutex);

	/* `start' hack - see fs/proc/generic.c line ~105 */
	*start=(char *)((unsigned long)count-offset);
	return pos;
}

static int ipt_get_targets(char *buffer, char **start, off_t offset, int length)
{
	off_t pos = 0;
	unsigned int count = 0;

	if (down_interruptible(&ipt_mutex) != 0)
		return 0;

	LIST_FIND(&ipt_target, print_target, struct ipt_target *,
		  offset, buffer, length, &pos, &count);
	
	up(&ipt_mutex);

	*start = (char *)((unsigned long)count - offset);
	return pos;
}

static int ipt_get_matches(char *buffer, char **start, off_t offset, int length)
{
	off_t pos = 0;
	unsigned int count = 0;

	if (down_interruptible(&ipt_mutex) != 0)
		return 0;
	
	LIST_FIND(&ipt_match, print_name, void *,
		  offset, buffer, length, &pos, &count);

	up(&ipt_mutex);

	*start = (char *)((unsigned long)count - offset);
	return pos;
}

static struct { char *name; get_info_t *get_info; } ipt_proc_entry[] =
{ { "ip_tables_names", ipt_get_tables },
  { "ip_tables_targets", ipt_get_targets },
  { "ip_tables_matches", ipt_get_matches },
  { NULL, NULL} };
#endif /*CONFIG_PROC_FS*/

static int __init init(void)
{
	int ret;

	/* Noone else will be downing sem now, so we won't sleep */
	down(&ipt_mutex);
	list_append(&ipt_target, &ipt_standard_target);
	list_append(&ipt_target, &ipt_error_target);
	list_append(&ipt_match, &tcp_matchstruct);
	list_append(&ipt_match, &udp_matchstruct);
	list_append(&ipt_match, &icmp_matchstruct);
	up(&ipt_mutex);

	/* Register setsockopt */
	ret = nf_register_sockopt(&ipt_sockopts);
	if (ret < 0) {
		duprintf("Unable to register sockopts.\n");
		return ret;
	}

#ifdef CONFIG_PROC_FS
	{
	struct proc_dir_entry *proc;
	int i;

	for (i = 0; ipt_proc_entry[i].name; i++) {
		proc = proc_net_create(ipt_proc_entry[i].name, 0,
				       ipt_proc_entry[i].get_info);
		if (!proc) {
			while (--i >= 0)
				proc_net_remove(ipt_proc_entry[i].name);
			nf_unregister_sockopt(&ipt_sockopts);
			return -ENOMEM;
		}
		proc->owner = THIS_MODULE;
	}
	}
#endif

	printk("ip_tables: (C) 2000-2002 Netfilter core team\n");
	return 0;
}

static void __exit fini(void)
{
	nf_unregister_sockopt(&ipt_sockopts);
#ifdef CONFIG_PROC_FS
	{
	int i;
	for (i = 0; ipt_proc_entry[i].name; i++)
		proc_net_remove(ipt_proc_entry[i].name);
	}
#endif
}

EXPORT_SYMBOL(ipt_register_table);
EXPORT_SYMBOL(ipt_unregister_table);
EXPORT_SYMBOL(ipt_register_match);
EXPORT_SYMBOL(ipt_unregister_match);
EXPORT_SYMBOL(ipt_do_table);
EXPORT_SYMBOL(ipt_register_target);
EXPORT_SYMBOL(ipt_unregister_target);
EXPORT_SYMBOL(ipt_find_target);

module_init(init);
module_exit(fini);
