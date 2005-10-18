/*
 * Packet matching code.
 *
 * Copyright (C) 1999 Paul `Rusty' Russell & Michael J. Neuling
 * Copyright (C) 2000-2002 Netfilter core team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 19 Jan 2002 Harald Welte <laforge@gnumonks.org>
 * 	- increase module usage count as soon as we have rules inside
 * 	  a table
 * 06 Jun 2002 Andras Kis-Szabo <kisza@sch.bme.hu>
 *      - new extension header parser code
 */
#include <linux/config.h>
#include <linux/skbuff.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmpv6.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>
#include <linux/proc_fs.h>
#include <linux/cpumask.h>

#include <linux/netfilter_ipv6/ip6_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Netfilter Core Team <coreteam@netfilter.org>");
MODULE_DESCRIPTION("IPv6 packet filter");

#define IPV6_HDR_LEN	(sizeof(struct ipv6hdr))
#define IPV6_OPTHDR_LEN	(sizeof(struct ipv6_opt_hdr))

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

static DECLARE_MUTEX(ip6t_mutex);

/* Must have mutex */
#define ASSERT_READ_LOCK(x) IP_NF_ASSERT(down_trylock(&ip6t_mutex) != 0)
#define ASSERT_WRITE_LOCK(x) IP_NF_ASSERT(down_trylock(&ip6t_mutex) != 0)
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
/* All the better to debug you with... */
#define static
#define inline
#endif

/* Locking is simple: we assume at worst case there will be one packet
   in user context and one from bottom halves (or soft irq if Alexey's
   softnet patch was applied).

   We keep a set of rules for each CPU, so we can avoid write-locking
   them; doing a readlock_bh() stops packets coming through if we're
   in user context.

   To be cache friendly on SMP, we arrange them like so:
   [ n-entries ]
   ... cache-align padding ...
   [ n-entries ]

   Hence the start of any table is given by get_table() below.  */

/* The table itself */
struct ip6t_table_info
{
	/* Size per table */
	unsigned int size;
	/* Number of entries: FIXME. --RR */
	unsigned int number;
	/* Initial number of entries. Needed for module usage count */
	unsigned int initial_entries;

	/* Entry points and underflows */
	unsigned int hook_entry[NF_IP6_NUMHOOKS];
	unsigned int underflow[NF_IP6_NUMHOOKS];

	/* ip6t_entry tables: one per CPU */
	char entries[0] ____cacheline_aligned;
};

static LIST_HEAD(ip6t_target);
static LIST_HEAD(ip6t_match);
static LIST_HEAD(ip6t_tables);
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

static int ip6_masked_addrcmp(struct in6_addr addr1, struct in6_addr mask,
			      struct in6_addr addr2)
{
	int i;
	for( i = 0; i < 16; i++){
		if((addr1.s6_addr[i] & mask.s6_addr[i]) != 
		   (addr2.s6_addr[i] & mask.s6_addr[i]))
			return 1;
	}
	return 0;
}

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
static inline int
ip6_packet_match(const struct sk_buff *skb,
		 const char *indev,
		 const char *outdev,
		 const struct ip6t_ip6 *ip6info,
		 unsigned int *protoff,
		 int *fragoff)
{
	size_t i;
	unsigned long ret;
	const struct ipv6hdr *ipv6 = skb->nh.ipv6h;

#define FWINV(bool,invflg) ((bool) ^ !!(ip6info->invflags & invflg))

	if (FWINV(ip6_masked_addrcmp(ipv6->saddr,ip6info->smsk,ip6info->src),
		  IP6T_INV_SRCIP)
	    || FWINV(ip6_masked_addrcmp(ipv6->daddr,ip6info->dmsk,ip6info->dst),
		     IP6T_INV_DSTIP)) {
		dprintf("Source or dest mismatch.\n");
/*
		dprintf("SRC: %u. Mask: %u. Target: %u.%s\n", ip->saddr,
			ipinfo->smsk.s_addr, ipinfo->src.s_addr,
			ipinfo->invflags & IP6T_INV_SRCIP ? " (INV)" : "");
		dprintf("DST: %u. Mask: %u. Target: %u.%s\n", ip->daddr,
			ipinfo->dmsk.s_addr, ipinfo->dst.s_addr,
			ipinfo->invflags & IP6T_INV_DSTIP ? " (INV)" : "");*/
		return 0;
	}

	/* Look for ifname matches; this should unroll nicely. */
	for (i = 0, ret = 0; i < IFNAMSIZ/sizeof(unsigned long); i++) {
		ret |= (((const unsigned long *)indev)[i]
			^ ((const unsigned long *)ip6info->iniface)[i])
			& ((const unsigned long *)ip6info->iniface_mask)[i];
	}

	if (FWINV(ret != 0, IP6T_INV_VIA_IN)) {
		dprintf("VIA in mismatch (%s vs %s).%s\n",
			indev, ip6info->iniface,
			ip6info->invflags&IP6T_INV_VIA_IN ?" (INV)":"");
		return 0;
	}

	for (i = 0, ret = 0; i < IFNAMSIZ/sizeof(unsigned long); i++) {
		ret |= (((const unsigned long *)outdev)[i]
			^ ((const unsigned long *)ip6info->outiface)[i])
			& ((const unsigned long *)ip6info->outiface_mask)[i];
	}

	if (FWINV(ret != 0, IP6T_INV_VIA_OUT)) {
		dprintf("VIA out mismatch (%s vs %s).%s\n",
			outdev, ip6info->outiface,
			ip6info->invflags&IP6T_INV_VIA_OUT ?" (INV)":"");
		return 0;
	}

/* ... might want to do something with class and flowlabel here ... */

	/* look for the desired protocol header */
	if((ip6info->flags & IP6T_F_PROTO)) {
		u_int8_t currenthdr = ipv6->nexthdr;
		struct ipv6_opt_hdr _hdr, *hp;
		u_int16_t ptr;		/* Header offset in skb */
		u_int16_t hdrlen;	/* Header */
		u_int16_t _fragoff = 0, *fp = NULL;

		ptr = IPV6_HDR_LEN;

		while (ip6t_ext_hdr(currenthdr)) {
	                /* Is there enough space for the next ext header? */
	                if (skb->len - ptr < IPV6_OPTHDR_LEN)
	                        return 0;

			/* NONE or ESP: there isn't protocol part */
			/* If we want to count these packets in '-p all',
			 * we will change the return 0 to 1*/
			if ((currenthdr == IPPROTO_NONE) || 
				(currenthdr == IPPROTO_ESP))
				break;

			hp = skb_header_pointer(skb, ptr, sizeof(_hdr), &_hdr);
			BUG_ON(hp == NULL);

			/* Size calculation */
	                if (currenthdr == IPPROTO_FRAGMENT) {
				fp = skb_header_pointer(skb,
						   ptr+offsetof(struct frag_hdr,
								frag_off),
						   sizeof(_fragoff),
						   &_fragoff);
				if (fp == NULL)
					return 0;

				_fragoff = ntohs(*fp) & ~0x7;
	                        hdrlen = 8;
	                } else if (currenthdr == IPPROTO_AH)
	                        hdrlen = (hp->hdrlen+2)<<2;
	                else
	                        hdrlen = ipv6_optlen(hp);

			currenthdr = hp->nexthdr;
	                ptr += hdrlen;
			/* ptr is too large */
	                if ( ptr > skb->len ) 
				return 0;
			if (_fragoff) {
				if (ip6t_ext_hdr(currenthdr))
					return 0;
				break;
			}
		}

		*protoff = ptr;
		*fragoff = _fragoff;

		/* currenthdr contains the protocol header */

		dprintf("Packet protocol %hi ?= %s%hi.\n",
				currenthdr, 
				ip6info->invflags & IP6T_INV_PROTO ? "!":"",
				ip6info->proto);

		if (ip6info->proto == currenthdr) {
			if(ip6info->invflags & IP6T_INV_PROTO) {
				return 0;
			}
			return 1;
		}

		/* We need match for the '-p all', too! */
		if ((ip6info->proto != 0) &&
			!(ip6info->invflags & IP6T_INV_PROTO))
			return 0;
	}
	return 1;
}

/* should be ip6 safe */
static inline int 
ip6_checkentry(const struct ip6t_ip6 *ipv6)
{
	if (ipv6->flags & ~IP6T_F_MASK) {
		duprintf("Unknown flag bits set: %08X\n",
			 ipv6->flags & ~IP6T_F_MASK);
		return 0;
	}
	if (ipv6->invflags & ~IP6T_INV_MASK) {
		duprintf("Unknown invflag bits set: %08X\n",
			 ipv6->invflags & ~IP6T_INV_MASK);
		return 0;
	}
	return 1;
}

static unsigned int
ip6t_error(struct sk_buff **pskb,
	  const struct net_device *in,
	  const struct net_device *out,
	  unsigned int hooknum,
	  const void *targinfo,
	  void *userinfo)
{
	if (net_ratelimit())
		printk("ip6_tables: error: `%s'\n", (char *)targinfo);

	return NF_DROP;
}

static inline
int do_match(struct ip6t_entry_match *m,
	     const struct sk_buff *skb,
	     const struct net_device *in,
	     const struct net_device *out,
	     int offset,
	     unsigned int protoff,
	     int *hotdrop)
{
	/* Stop iteration if it doesn't match */
	if (!m->u.kernel.match->match(skb, in, out, m->data,
				      offset, protoff, hotdrop))
		return 1;
	else
		return 0;
}

static inline struct ip6t_entry *
get_entry(void *base, unsigned int offset)
{
	return (struct ip6t_entry *)(base + offset);
}

/* Returns one of the generic firewall policies, like NF_ACCEPT. */
unsigned int
ip6t_do_table(struct sk_buff **pskb,
	      unsigned int hook,
	      const struct net_device *in,
	      const struct net_device *out,
	      struct ip6t_table *table,
	      void *userdata)
{
	static const char nulldevname[IFNAMSIZ];
	int offset = 0;
	unsigned int protoff = 0;
	int hotdrop = 0;
	/* Initializing verdict to NF_DROP keeps gcc happy. */
	unsigned int verdict = NF_DROP;
	const char *indev, *outdev;
	void *table_base;
	struct ip6t_entry *e, *back;

	/* Initialization */
	indev = in ? in->name : nulldevname;
	outdev = out ? out->name : nulldevname;

	/* We handle fragments by dealing with the first fragment as
	 * if it was a normal packet.  All other fragments are treated
	 * normally, except that they will NEVER match rules that ask
	 * things we don't know, ie. tcp syn flag or ports).  If the
	 * rule is also a fragment-specific rule, non-fragments won't
	 * match it. */

	read_lock_bh(&table->lock);
	IP_NF_ASSERT(table->valid_hooks & (1 << hook));
	table_base = (void *)table->private->entries
		+ TABLE_OFFSET(table->private, smp_processor_id());
	e = get_entry(table_base, table->private->hook_entry[hook]);

#ifdef CONFIG_NETFILTER_DEBUG
	/* Check noone else using our table */
	if (((struct ip6t_entry *)table_base)->comefrom != 0xdead57ac
	    && ((struct ip6t_entry *)table_base)->comefrom != 0xeeeeeeec) {
		printk("ASSERT: CPU #%u, %s comefrom(%p) = %X\n",
		       smp_processor_id(),
		       table->name,
		       &((struct ip6t_entry *)table_base)->comefrom,
		       ((struct ip6t_entry *)table_base)->comefrom);
	}
	((struct ip6t_entry *)table_base)->comefrom = 0x57acc001;
#endif

	/* For return from builtin chain */
	back = get_entry(table_base, table->private->underflow[hook]);

	do {
		IP_NF_ASSERT(e);
		IP_NF_ASSERT(back);
		if (ip6_packet_match(*pskb, indev, outdev, &e->ipv6,
			&protoff, &offset)) {
			struct ip6t_entry_target *t;

			if (IP6T_MATCH_ITERATE(e, do_match,
					       *pskb, in, out,
					       offset, protoff, &hotdrop) != 0)
				goto no_match;

			ADD_COUNTER(e->counters,
				    ntohs((*pskb)->nh.ipv6h->payload_len)
				    + IPV6_HDR_LEN,
				    1);

			t = ip6t_get_target(e);
			IP_NF_ASSERT(t->u.kernel.target);
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
#ifdef CONFIG_NETFILTER_DEBUG
				((struct ip6t_entry *)table_base)->comefrom
					= 0xeeeeeeec;
#endif
				verdict = t->u.kernel.target->target(pskb,
								     in, out,
								     hook,
								     t->data,
								     userdata);

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
	((struct ip6t_entry *)table_base)->comefrom = 0xdead57ac;
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

/* If it succeeds, returns element and locks mutex */
static inline void *
find_inlist_lock_noload(struct list_head *head,
			const char *name,
			int *error,
			struct semaphore *mutex)
{
	void *ret;

#if 1
	duprintf("find_inlist: searching for `%s' in %s.\n",
		 name, head == &ip6t_target ? "ip6t_target"
		 : head == &ip6t_match ? "ip6t_match"
		 : head == &ip6t_tables ? "ip6t_tables" : "UNKNOWN");
#endif

	*error = down_interruptible(mutex);
	if (*error != 0)
		return NULL;

	ret = list_named_find(head, name);
	if (!ret) {
		*error = -ENOENT;
		up(mutex);
	}
	return ret;
}

#ifndef CONFIG_KMOD
#define find_inlist_lock(h,n,p,e,m) find_inlist_lock_noload((h),(n),(e),(m))
#else
static void *
find_inlist_lock(struct list_head *head,
		 const char *name,
		 const char *prefix,
		 int *error,
		 struct semaphore *mutex)
{
	void *ret;

	ret = find_inlist_lock_noload(head, name, error, mutex);
	if (!ret) {
		duprintf("find_inlist: loading `%s%s'.\n", prefix, name);
		request_module("%s%s", prefix, name);
		ret = find_inlist_lock_noload(head, name, error, mutex);
	}

	return ret;
}
#endif

static inline struct ip6t_table *
ip6t_find_table_lock(const char *name, int *error, struct semaphore *mutex)
{
	return find_inlist_lock(&ip6t_tables, name, "ip6table_", error, mutex);
}

static inline struct ip6t_match *
find_match_lock(const char *name, int *error, struct semaphore *mutex)
{
	return find_inlist_lock(&ip6t_match, name, "ip6t_", error, mutex);
}

static struct ip6t_target *
ip6t_find_target_lock(const char *name, int *error, struct semaphore *mutex)
{
	return find_inlist_lock(&ip6t_target, name, "ip6t_", error, mutex);
}

/* All zeroes == unconditional rule. */
static inline int
unconditional(const struct ip6t_ip6 *ipv6)
{
	unsigned int i;

	for (i = 0; i < sizeof(*ipv6); i++)
		if (((char *)ipv6)[i])
			break;

	return (i == sizeof(*ipv6));
}

/* Figures out from what hook each rule can be called: returns 0 if
   there are loops.  Puts hook bitmask in comefrom. */
static int
mark_source_chains(struct ip6t_table_info *newinfo, unsigned int valid_hooks)
{
	unsigned int hook;

	/* No recursion; use packet counter to save back ptrs (reset
	   to 0 as we leave), and comefrom to save source hook bitmask */
	for (hook = 0; hook < NF_IP6_NUMHOOKS; hook++) {
		unsigned int pos = newinfo->hook_entry[hook];
		struct ip6t_entry *e
			= (struct ip6t_entry *)(newinfo->entries + pos);

		if (!(valid_hooks & (1 << hook)))
			continue;

		/* Set initial back pointer. */
		e->counters.pcnt = pos;

		for (;;) {
			struct ip6t_standard_target *t
				= (void *)ip6t_get_target(e);

			if (e->comefrom & (1 << NF_IP6_NUMHOOKS)) {
				printk("iptables: loop hook %u pos %u %08X.\n",
				       hook, pos, e->comefrom);
				return 0;
			}
			e->comefrom
				|= ((1 << hook) | (1 << NF_IP6_NUMHOOKS));

			/* Unconditional return/END. */
			if (e->target_offset == sizeof(struct ip6t_entry)
			    && (strcmp(t->target.u.user.name,
				       IP6T_STANDARD_TARGET) == 0)
			    && t->verdict < 0
			    && unconditional(&e->ipv6)) {
				unsigned int oldpos, size;

				/* Return: backtrack through the last
				   big jump. */
				do {
					e->comefrom ^= (1<<NF_IP6_NUMHOOKS);
#ifdef DEBUG_IP_FIREWALL_USER
					if (e->comefrom
					    & (1 << NF_IP6_NUMHOOKS)) {
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
						(newinfo->entries + pos);
				} while (oldpos == pos + e->next_offset);

				/* Move along one */
				size = e->next_offset;
				e = (struct ip6t_entry *)
					(newinfo->entries + pos + size);
				e->counters.pcnt = pos;
				pos += size;
			} else {
				int newpos = t->verdict;

				if (strcmp(t->target.u.user.name,
					   IP6T_STANDARD_TARGET) == 0
				    && newpos >= 0) {
					/* This a jump; chase it. */
					duprintf("Jump rule %u -> %u\n",
						 pos, newpos);
				} else {
					/* ... this is a fallthru */
					newpos = pos + e->next_offset;
				}
				e = (struct ip6t_entry *)
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
cleanup_match(struct ip6t_entry_match *m, unsigned int *i)
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
standard_check(const struct ip6t_entry_target *t,
	       unsigned int max_offset)
{
	struct ip6t_standard_target *targ = (void *)t;

	/* Check standard info. */
	if (t->u.target_size
	    != IP6T_ALIGN(sizeof(struct ip6t_standard_target))) {
		duprintf("standard_check: target size %u != %u\n",
			 t->u.target_size,
			 IP6T_ALIGN(sizeof(struct ip6t_standard_target)));
		return 0;
	}

	if (targ->verdict >= 0
	    && targ->verdict > max_offset - sizeof(struct ip6t_entry)) {
		duprintf("ip6t_standard_check: bad verdict (%i)\n",
			 targ->verdict);
		return 0;
	}

	if (targ->verdict < -NF_MAX_VERDICT - 1) {
		duprintf("ip6t_standard_check: bad negative verdict (%i)\n",
			 targ->verdict);
		return 0;
	}
	return 1;
}

static inline int
check_match(struct ip6t_entry_match *m,
	    const char *name,
	    const struct ip6t_ip6 *ipv6,
	    unsigned int hookmask,
	    unsigned int *i)
{
	int ret;
	struct ip6t_match *match;

	match = find_match_lock(m->u.user.name, &ret, &ip6t_mutex);
	if (!match) {
	  //		duprintf("check_match: `%s' not found\n", m->u.name);
		return ret;
	}
	if (!try_module_get(match->me)) {
		up(&ip6t_mutex);
		return -ENOENT;
	}
	m->u.kernel.match = match;
	up(&ip6t_mutex);

	if (m->u.kernel.match->checkentry
	    && !m->u.kernel.match->checkentry(name, ipv6, m->data,
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

static struct ip6t_target ip6t_standard_target;

static inline int
check_entry(struct ip6t_entry *e, const char *name, unsigned int size,
	    unsigned int *i)
{
	struct ip6t_entry_target *t;
	struct ip6t_target *target;
	int ret;
	unsigned int j;

	if (!ip6_checkentry(&e->ipv6)) {
		duprintf("ip_tables: ip check failed %p %s.\n", e, name);
		return -EINVAL;
	}

	j = 0;
	ret = IP6T_MATCH_ITERATE(e, check_match, name, &e->ipv6, e->comefrom, &j);
	if (ret != 0)
		goto cleanup_matches;

	t = ip6t_get_target(e);
	target = ip6t_find_target_lock(t->u.user.name, &ret, &ip6t_mutex);
	if (!target) {
		duprintf("check_entry: `%s' not found\n", t->u.user.name);
		goto cleanup_matches;
	}
	if (!try_module_get(target->me)) {
		up(&ip6t_mutex);
		ret = -ENOENT;
		goto cleanup_matches;
	}
	t->u.kernel.target = target;
	up(&ip6t_mutex);
	if (!t->u.kernel.target) {
		ret = -EBUSY;
		goto cleanup_matches;
	}
	if (t->u.kernel.target == &ip6t_standard_target) {
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
	IP6T_MATCH_ITERATE(e, cleanup_match, &j);
	return ret;
}

static inline int
check_entry_size_and_hooks(struct ip6t_entry *e,
			   struct ip6t_table_info *newinfo,
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
	for (h = 0; h < NF_IP6_NUMHOOKS; h++) {
		if ((unsigned char *)e - base == hook_entries[h])
			newinfo->hook_entry[h] = hook_entries[h];
		if ((unsigned char *)e - base == underflows[h])
			newinfo->underflow[h] = underflows[h];
	}

	/* FIXME: underflows must be unconditional, standard verdicts
           < 0 (not IP6T_RETURN). --RR */

	/* Clear counters and comefrom */
	e->counters = ((struct ip6t_counters) { 0, 0 });
	e->comefrom = 0;

	(*i)++;
	return 0;
}

static inline int
cleanup_entry(struct ip6t_entry *e, unsigned int *i)
{
	struct ip6t_entry_target *t;

	if (i && (*i)-- == 0)
		return 1;

	/* Cleanup all matches */
	IP6T_MATCH_ITERATE(e, cleanup_match, NULL);
	t = ip6t_get_target(e);
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
		struct ip6t_table_info *newinfo,
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
	for (i = 0; i < NF_IP6_NUMHOOKS; i++) {
		newinfo->hook_entry[i] = 0xFFFFFFFF;
		newinfo->underflow[i] = 0xFFFFFFFF;
	}

	duprintf("translate_table: size %u\n", newinfo->size);
	i = 0;
	/* Walk through entries, checking offsets. */
	ret = IP6T_ENTRY_ITERATE(newinfo->entries, newinfo->size,
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
	for (i = 0; i < NF_IP6_NUMHOOKS; i++) {
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
	ret = IP6T_ENTRY_ITERATE(newinfo->entries, newinfo->size,
				check_entry, name, size, &i);

	if (ret != 0) {
		IP6T_ENTRY_ITERATE(newinfo->entries, newinfo->size,
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

static struct ip6t_table_info *
replace_table(struct ip6t_table *table,
	      unsigned int num_counters,
	      struct ip6t_table_info *newinfo,
	      int *error)
{
	struct ip6t_table_info *oldinfo;

#ifdef CONFIG_NETFILTER_DEBUG
	{
		struct ip6t_entry *table_base;
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
add_entry_to_counter(const struct ip6t_entry *e,
		     struct ip6t_counters total[],
		     unsigned int *i)
{
	ADD_COUNTER(total[*i], e->counters.bcnt, e->counters.pcnt);

	(*i)++;
	return 0;
}

static void
get_counters(const struct ip6t_table_info *t,
	     struct ip6t_counters counters[])
{
	unsigned int cpu;
	unsigned int i;

	for_each_cpu(cpu) {
		i = 0;
		IP6T_ENTRY_ITERATE(t->entries + TABLE_OFFSET(t, cpu),
				  t->size,
				  add_entry_to_counter,
				  counters,
				  &i);
	}
}

static int
copy_entries_to_user(unsigned int total_size,
		     struct ip6t_table *table,
		     void __user *userptr)
{
	unsigned int off, num, countersize;
	struct ip6t_entry *e;
	struct ip6t_counters *counters;
	int ret = 0;

	/* We need atomic snapshot of counters: rest doesn't change
	   (other than comefrom, which userspace doesn't care
	   about). */
	countersize = sizeof(struct ip6t_counters) * table->private->number;
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
		struct ip6t_entry_match *m;
		struct ip6t_entry_target *t;

		e = (struct ip6t_entry *)(table->private->entries + off);
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

static int
get_entries(const struct ip6t_get_entries *entries,
	    struct ip6t_get_entries __user *uptr)
{
	int ret;
	struct ip6t_table *t;

	t = ip6t_find_table_lock(entries->name, &ret, &ip6t_mutex);
	if (t) {
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
		up(&ip6t_mutex);
	} else
		duprintf("get_entries: Can't find %s!\n",
			 entries->name);

	return ret;
}

static int
do_replace(void __user *user, unsigned int len)
{
	int ret;
	struct ip6t_replace tmp;
	struct ip6t_table *t;
	struct ip6t_table_info *newinfo, *oldinfo;
	struct ip6t_counters *counters;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	/* Pedantry: prevent them from hitting BUG() in vmalloc.c --RR */
	if ((SMP_ALIGN(tmp.size) >> PAGE_SHIFT) + 2 > num_physpages)
		return -ENOMEM;

	newinfo = vmalloc(sizeof(struct ip6t_table_info)
			  + SMP_ALIGN(tmp.size) *
			  		(highest_possible_processor_id()+1));
	if (!newinfo)
		return -ENOMEM;

	if (copy_from_user(newinfo->entries, user + sizeof(tmp),
			   tmp.size) != 0) {
		ret = -EFAULT;
		goto free_newinfo;
	}

	counters = vmalloc(tmp.num_counters * sizeof(struct ip6t_counters));
	if (!counters) {
		ret = -ENOMEM;
		goto free_newinfo;
	}
	memset(counters, 0, tmp.num_counters * sizeof(struct ip6t_counters));

	ret = translate_table(tmp.name, tmp.valid_hooks,
			      newinfo, tmp.size, tmp.num_entries,
			      tmp.hook_entry, tmp.underflow);
	if (ret != 0)
		goto free_newinfo_counters;

	duprintf("ip_tables: Translated table\n");

	t = ip6t_find_table_lock(tmp.name, &ret, &ip6t_mutex);
	if (!t)
		goto free_newinfo_counters_untrans;

	/* You lied! */
	if (tmp.valid_hooks != t->valid_hooks) {
		duprintf("Valid hook crap: %08X vs %08X\n",
			 tmp.valid_hooks, t->valid_hooks);
		ret = -EINVAL;
		goto free_newinfo_counters_untrans_unlock;
	}

	/* Get a reference in advance, we're not allowed fail later */
	if (!try_module_get(t->me)) {
		ret = -EBUSY;
		goto free_newinfo_counters_untrans_unlock;
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
	IP6T_ENTRY_ITERATE(oldinfo->entries, oldinfo->size, cleanup_entry,NULL);
	vfree(oldinfo);
	/* Silent error: too late now. */
	if (copy_to_user(tmp.counters, counters,
			 sizeof(struct ip6t_counters) * tmp.num_counters) != 0)
		ret = -EFAULT;
	vfree(counters);
	up(&ip6t_mutex);
	return ret;

 put_module:
	module_put(t->me);
 free_newinfo_counters_untrans_unlock:
	up(&ip6t_mutex);
 free_newinfo_counters_untrans:
	IP6T_ENTRY_ITERATE(newinfo->entries, newinfo->size, cleanup_entry,NULL);
 free_newinfo_counters:
	vfree(counters);
 free_newinfo:
	vfree(newinfo);
	return ret;
}

/* We're lazy, and add to the first CPU; overflow works its fey magic
 * and everything is OK. */
static inline int
add_counter_to_entry(struct ip6t_entry *e,
		     const struct ip6t_counters addme[],
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
	struct ip6t_counters_info tmp, *paddc;
	struct ip6t_table *t;
	int ret;

	if (copy_from_user(&tmp, user, sizeof(tmp)) != 0)
		return -EFAULT;

	if (len != sizeof(tmp) + tmp.num_counters*sizeof(struct ip6t_counters))
		return -EINVAL;

	paddc = vmalloc(len);
	if (!paddc)
		return -ENOMEM;

	if (copy_from_user(paddc, user, len) != 0) {
		ret = -EFAULT;
		goto free;
	}

	t = ip6t_find_table_lock(tmp.name, &ret, &ip6t_mutex);
	if (!t)
		goto free;

	write_lock_bh(&t->lock);
	if (t->private->number != paddc->num_counters) {
		ret = -EINVAL;
		goto unlock_up_free;
	}

	i = 0;
	IP6T_ENTRY_ITERATE(t->private->entries,
			  t->private->size,
			  add_counter_to_entry,
			  paddc->counters,
			  &i);
 unlock_up_free:
	write_unlock_bh(&t->lock);
	up(&ip6t_mutex);
 free:
	vfree(paddc);

	return ret;
}

static int
do_ip6t_set_ctl(struct sock *sk, int cmd, void __user *user, unsigned int len)
{
	int ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
	case IP6T_SO_SET_REPLACE:
		ret = do_replace(user, len);
		break;

	case IP6T_SO_SET_ADD_COUNTERS:
		ret = do_add_counters(user, len);
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
	case IP6T_SO_GET_INFO: {
		char name[IP6T_TABLE_MAXNAMELEN];
		struct ip6t_table *t;

		if (*len != sizeof(struct ip6t_getinfo)) {
			duprintf("length %u != %u\n", *len,
				 sizeof(struct ip6t_getinfo));
			ret = -EINVAL;
			break;
		}

		if (copy_from_user(name, user, sizeof(name)) != 0) {
			ret = -EFAULT;
			break;
		}
		name[IP6T_TABLE_MAXNAMELEN-1] = '\0';
		t = ip6t_find_table_lock(name, &ret, &ip6t_mutex);
		if (t) {
			struct ip6t_getinfo info;

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

			up(&ip6t_mutex);
		}
	}
	break;

	case IP6T_SO_GET_ENTRIES: {
		struct ip6t_get_entries get;

		if (*len < sizeof(get)) {
			duprintf("get_entries: %u < %u\n", *len, sizeof(get));
			ret = -EINVAL;
		} else if (copy_from_user(&get, user, sizeof(get)) != 0) {
			ret = -EFAULT;
		} else if (*len != sizeof(struct ip6t_get_entries) + get.size) {
			duprintf("get_entries: %u != %u\n", *len,
				 sizeof(struct ip6t_get_entries) + get.size);
			ret = -EINVAL;
		} else
			ret = get_entries(&get, user);
		break;
	}

	default:
		duprintf("do_ip6t_get_ctl: unknown request %i\n", cmd);
		ret = -EINVAL;
	}

	return ret;
}

/* Registration hooks for targets. */
int
ip6t_register_target(struct ip6t_target *target)
{
	int ret;

	ret = down_interruptible(&ip6t_mutex);
	if (ret != 0)
		return ret;

	if (!list_named_insert(&ip6t_target, target)) {
		duprintf("ip6t_register_target: `%s' already in list!\n",
			 target->name);
		ret = -EINVAL;
	}
	up(&ip6t_mutex);
	return ret;
}

void
ip6t_unregister_target(struct ip6t_target *target)
{
	down(&ip6t_mutex);
	LIST_DELETE(&ip6t_target, target);
	up(&ip6t_mutex);
}

int
ip6t_register_match(struct ip6t_match *match)
{
	int ret;

	ret = down_interruptible(&ip6t_mutex);
	if (ret != 0)
		return ret;

	if (!list_named_insert(&ip6t_match, match)) {
		duprintf("ip6t_register_match: `%s' already in list!\n",
			 match->name);
		ret = -EINVAL;
	}
	up(&ip6t_mutex);

	return ret;
}

void
ip6t_unregister_match(struct ip6t_match *match)
{
	down(&ip6t_mutex);
	LIST_DELETE(&ip6t_match, match);
	up(&ip6t_mutex);
}

int ip6t_register_table(struct ip6t_table *table,
			const struct ip6t_replace *repl)
{
	int ret;
	struct ip6t_table_info *newinfo;
	static struct ip6t_table_info bootstrap
		= { 0, 0, 0, { 0 }, { 0 }, { } };

	newinfo = vmalloc(sizeof(struct ip6t_table_info)
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

	ret = down_interruptible(&ip6t_mutex);
	if (ret != 0) {
		vfree(newinfo);
		return ret;
	}

	/* Don't autoload: we'd eat our tail... */
	if (list_named_find(&ip6t_tables, table->name)) {
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
	list_prepend(&ip6t_tables, table);

 unlock:
	up(&ip6t_mutex);
	return ret;

 free_unlock:
	vfree(newinfo);
	goto unlock;
}

void ip6t_unregister_table(struct ip6t_table *table)
{
	down(&ip6t_mutex);
	LIST_DELETE(&ip6t_tables, table);
	up(&ip6t_mutex);

	/* Decrease module usage counts and free resources */
	IP6T_ENTRY_ITERATE(table->private->entries, table->private->size,
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
		unsigned int tcpoff,
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
	op = skb_header_pointer(skb, tcpoff + sizeof(struct tcphdr), optlen,
				_opt);
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
	  unsigned int protoff,
	  int *hotdrop)
{
	struct tcphdr _tcph, *th;
	const struct ip6t_tcp *tcpinfo = matchinfo;

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

	th = skb_header_pointer(skb, protoff, sizeof(_tcph), &_tcph);
	if (th == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil TCP offset=0 tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	if (!port_match(tcpinfo->spts[0], tcpinfo->spts[1],
			ntohs(th->source),
			!!(tcpinfo->invflags & IP6T_TCP_INV_SRCPT)))
		return 0;
	if (!port_match(tcpinfo->dpts[0], tcpinfo->dpts[1],
			ntohs(th->dest),
			!!(tcpinfo->invflags & IP6T_TCP_INV_DSTPT)))
		return 0;
	if (!FWINVTCP((((unsigned char *)th)[13] & tcpinfo->flg_mask)
		      == tcpinfo->flg_cmp,
		      IP6T_TCP_INV_FLAGS))
		return 0;
	if (tcpinfo->option) {
		if (th->doff * 4 < sizeof(_tcph)) {
			*hotdrop = 1;
			return 0;
		}
		if (!tcp_find_option(tcpinfo->option, skb, protoff,
				     th->doff*4 - sizeof(*th),
				     tcpinfo->invflags & IP6T_TCP_INV_OPTION,
				     hotdrop))
			return 0;
	}
	return 1;
}

/* Called when user tries to insert an entry of this type. */
static int
tcp_checkentry(const char *tablename,
	       const struct ip6t_ip6 *ipv6,
	       void *matchinfo,
	       unsigned int matchsize,
	       unsigned int hook_mask)
{
	const struct ip6t_tcp *tcpinfo = matchinfo;

	/* Must specify proto == TCP, and no unknown invflags */
	return ipv6->proto == IPPROTO_TCP
		&& !(ipv6->invflags & IP6T_INV_PROTO)
		&& matchsize == IP6T_ALIGN(sizeof(struct ip6t_tcp))
		&& !(tcpinfo->invflags & ~IP6T_TCP_INV_MASK);
}

static int
udp_match(const struct sk_buff *skb,
	  const struct net_device *in,
	  const struct net_device *out,
	  const void *matchinfo,
	  int offset,
	  unsigned int protoff,
	  int *hotdrop)
{
	struct udphdr _udph, *uh;
	const struct ip6t_udp *udpinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	uh = skb_header_pointer(skb, protoff, sizeof(_udph), &_udph);
	if (uh == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil UDP tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return port_match(udpinfo->spts[0], udpinfo->spts[1],
			  ntohs(uh->source),
			  !!(udpinfo->invflags & IP6T_UDP_INV_SRCPT))
		&& port_match(udpinfo->dpts[0], udpinfo->dpts[1],
			      ntohs(uh->dest),
			      !!(udpinfo->invflags & IP6T_UDP_INV_DSTPT));
}

/* Called when user tries to insert an entry of this type. */
static int
udp_checkentry(const char *tablename,
	       const struct ip6t_ip6 *ipv6,
	       void *matchinfo,
	       unsigned int matchinfosize,
	       unsigned int hook_mask)
{
	const struct ip6t_udp *udpinfo = matchinfo;

	/* Must specify proto == UDP, and no unknown invflags */
	if (ipv6->proto != IPPROTO_UDP || (ipv6->invflags & IP6T_INV_PROTO)) {
		duprintf("ip6t_udp: Protocol %u != %u\n", ipv6->proto,
			 IPPROTO_UDP);
		return 0;
	}
	if (matchinfosize != IP6T_ALIGN(sizeof(struct ip6t_udp))) {
		duprintf("ip6t_udp: matchsize %u != %u\n",
			 matchinfosize, IP6T_ALIGN(sizeof(struct ip6t_udp)));
		return 0;
	}
	if (udpinfo->invflags & ~IP6T_UDP_INV_MASK) {
		duprintf("ip6t_udp: unknown flags %X\n",
			 udpinfo->invflags);
		return 0;
	}

	return 1;
}

/* Returns 1 if the type and code is matched by the range, 0 otherwise */
static inline int
icmp6_type_code_match(u_int8_t test_type, u_int8_t min_code, u_int8_t max_code,
		     u_int8_t type, u_int8_t code,
		     int invert)
{
	return (type == test_type && code >= min_code && code <= max_code)
		^ invert;
}

static int
icmp6_match(const struct sk_buff *skb,
	   const struct net_device *in,
	   const struct net_device *out,
	   const void *matchinfo,
	   int offset,
	   unsigned int protoff,
	   int *hotdrop)
{
	struct icmp6hdr _icmp, *ic;
	const struct ip6t_icmp *icmpinfo = matchinfo;

	/* Must not be a fragment. */
	if (offset)
		return 0;

	ic = skb_header_pointer(skb, protoff, sizeof(_icmp), &_icmp);
	if (ic == NULL) {
		/* We've been asked to examine this packet, and we
		   can't.  Hence, no choice but to drop. */
		duprintf("Dropping evil ICMP tinygram.\n");
		*hotdrop = 1;
		return 0;
	}

	return icmp6_type_code_match(icmpinfo->type,
				     icmpinfo->code[0],
				     icmpinfo->code[1],
				     ic->icmp6_type, ic->icmp6_code,
				     !!(icmpinfo->invflags&IP6T_ICMP_INV));
}

/* Called when user tries to insert an entry of this type. */
static int
icmp6_checkentry(const char *tablename,
	   const struct ip6t_ip6 *ipv6,
	   void *matchinfo,
	   unsigned int matchsize,
	   unsigned int hook_mask)
{
	const struct ip6t_icmp *icmpinfo = matchinfo;

	/* Must specify proto == ICMP, and no unknown invflags */
	return ipv6->proto == IPPROTO_ICMPV6
		&& !(ipv6->invflags & IP6T_INV_PROTO)
		&& matchsize == IP6T_ALIGN(sizeof(struct ip6t_icmp))
		&& !(icmpinfo->invflags & ~IP6T_ICMP_INV);
}

/* The built-in targets: standard (NULL) and error. */
static struct ip6t_target ip6t_standard_target = {
	.name		= IP6T_STANDARD_TARGET,
};

static struct ip6t_target ip6t_error_target = {
	.name		= IP6T_ERROR_TARGET,
	.target		= ip6t_error,
};

static struct nf_sockopt_ops ip6t_sockopts = {
	.pf		= PF_INET6,
	.set_optmin	= IP6T_BASE_CTL,
	.set_optmax	= IP6T_SO_SET_MAX+1,
	.set		= do_ip6t_set_ctl,
	.get_optmin	= IP6T_BASE_CTL,
	.get_optmax	= IP6T_SO_GET_MAX+1,
	.get		= do_ip6t_get_ctl,
};

static struct ip6t_match tcp_matchstruct = {
	.name		= "tcp",
	.match		= &tcp_match,
	.checkentry	= &tcp_checkentry,
};

static struct ip6t_match udp_matchstruct = {
	.name		= "udp",
	.match		= &udp_match,
	.checkentry	= &udp_checkentry,
};

static struct ip6t_match icmp6_matchstruct = {
	.name		= "icmp6",
	.match		= &icmp6_match,
	.checkentry	= &icmp6_checkentry,
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

static inline int print_target(const struct ip6t_target *t,
                               off_t start_offset, char *buffer, int length,
                               off_t *pos, unsigned int *count)
{
	if (t == &ip6t_standard_target || t == &ip6t_error_target)
		return 0;
	return print_name((char *)t, start_offset, buffer, length, pos, count);
}

static int ip6t_get_tables(char *buffer, char **start, off_t offset, int length)
{
	off_t pos = 0;
	unsigned int count = 0;

	if (down_interruptible(&ip6t_mutex) != 0)
		return 0;

	LIST_FIND(&ip6t_tables, print_name, char *,
		  offset, buffer, length, &pos, &count);

	up(&ip6t_mutex);

	/* `start' hack - see fs/proc/generic.c line ~105 */
	*start=(char *)((unsigned long)count-offset);
	return pos;
}

static int ip6t_get_targets(char *buffer, char **start, off_t offset, int length)
{
	off_t pos = 0;
	unsigned int count = 0;

	if (down_interruptible(&ip6t_mutex) != 0)
		return 0;

	LIST_FIND(&ip6t_target, print_target, struct ip6t_target *,
		  offset, buffer, length, &pos, &count);

	up(&ip6t_mutex);

	*start = (char *)((unsigned long)count - offset);
	return pos;
}

static int ip6t_get_matches(char *buffer, char **start, off_t offset, int length)
{
	off_t pos = 0;
	unsigned int count = 0;

	if (down_interruptible(&ip6t_mutex) != 0)
		return 0;

	LIST_FIND(&ip6t_match, print_name, char *,
		  offset, buffer, length, &pos, &count);

	up(&ip6t_mutex);

	*start = (char *)((unsigned long)count - offset);
	return pos;
}

static struct { char *name; get_info_t *get_info; } ip6t_proc_entry[] =
{ { "ip6_tables_names", ip6t_get_tables },
  { "ip6_tables_targets", ip6t_get_targets },
  { "ip6_tables_matches", ip6t_get_matches },
  { NULL, NULL} };
#endif /*CONFIG_PROC_FS*/

static int __init init(void)
{
	int ret;

	/* Noone else will be downing sem now, so we won't sleep */
	down(&ip6t_mutex);
	list_append(&ip6t_target, &ip6t_standard_target);
	list_append(&ip6t_target, &ip6t_error_target);
	list_append(&ip6t_match, &tcp_matchstruct);
	list_append(&ip6t_match, &udp_matchstruct);
	list_append(&ip6t_match, &icmp6_matchstruct);
	up(&ip6t_mutex);

	/* Register setsockopt */
	ret = nf_register_sockopt(&ip6t_sockopts);
	if (ret < 0) {
		duprintf("Unable to register sockopts.\n");
		return ret;
	}

#ifdef CONFIG_PROC_FS
	{
		struct proc_dir_entry *proc;
		int i;

		for (i = 0; ip6t_proc_entry[i].name; i++) {
			proc = proc_net_create(ip6t_proc_entry[i].name, 0,
					       ip6t_proc_entry[i].get_info);
			if (!proc) {
				while (--i >= 0)
				       proc_net_remove(ip6t_proc_entry[i].name);
				nf_unregister_sockopt(&ip6t_sockopts);
				return -ENOMEM;
			}
			proc->owner = THIS_MODULE;
		}
	}
#endif

	printk("ip6_tables: (C) 2000-2002 Netfilter core team\n");
	return 0;
}

static void __exit fini(void)
{
	nf_unregister_sockopt(&ip6t_sockopts);
#ifdef CONFIG_PROC_FS
	{
		int i;
		for (i = 0; ip6t_proc_entry[i].name; i++)
			proc_net_remove(ip6t_proc_entry[i].name);
	}
#endif
}

/*
 * find specified header up to transport protocol header.
 * If found target header, the offset to the header is set to *offset
 * and return 0. otherwise, return -1.
 *
 * Notes: - non-1st Fragment Header isn't skipped.
 *	  - ESP header isn't skipped.
 *	  - The target header may be trancated.
 */
int ipv6_find_hdr(const struct sk_buff *skb, unsigned int *offset, u8 target)
{
	unsigned int start = (u8*)(skb->nh.ipv6h + 1) - skb->data;
	u8 nexthdr = skb->nh.ipv6h->nexthdr;
	unsigned int len = skb->len - start;

	while (nexthdr != target) {
		struct ipv6_opt_hdr _hdr, *hp;
		unsigned int hdrlen;

		if ((!ipv6_ext_hdr(nexthdr)) || nexthdr == NEXTHDR_NONE)
			return -1;
		hp = skb_header_pointer(skb, start, sizeof(_hdr), &_hdr);
		if (hp == NULL)
			return -1;
		if (nexthdr == NEXTHDR_FRAGMENT) {
			unsigned short _frag_off, *fp;
			fp = skb_header_pointer(skb,
						start+offsetof(struct frag_hdr,
							       frag_off),
						sizeof(_frag_off),
						&_frag_off);
			if (fp == NULL)
				return -1;

			if (ntohs(*fp) & ~0x7)
				return -1;
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
	return 0;
}

EXPORT_SYMBOL(ip6t_register_table);
EXPORT_SYMBOL(ip6t_unregister_table);
EXPORT_SYMBOL(ip6t_do_table);
EXPORT_SYMBOL(ip6t_register_match);
EXPORT_SYMBOL(ip6t_unregister_match);
EXPORT_SYMBOL(ip6t_register_target);
EXPORT_SYMBOL(ip6t_unregister_target);
EXPORT_SYMBOL(ip6t_ext_hdr);
EXPORT_SYMBOL(ipv6_find_hdr);

module_init(init);
module_exit(fini);
