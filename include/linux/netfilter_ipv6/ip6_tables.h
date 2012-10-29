/*
 * 25-Jul-1998 Major changes to allow for ip chain table
 *
 * 3-Jan-2000 Named tables to allow packet selection for different uses.
 */

/*
 * 	Format of an IP6 firewall descriptor
 *
 * 	src, dst, src_mask, dst_mask are always stored in network byte order.
 * 	flags are stored in host byte order (of course).
 * 	Port numbers are stored in HOST byte order.
 */
#ifndef _IP6_TABLES_H
#define _IP6_TABLES_H

#include <linux/if.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>

#include <linux/init.h>
#include <uapi/linux/netfilter_ipv6/ip6_tables.h>

extern void ip6t_init(void) __init;

extern void *ip6t_alloc_initial_table(const struct xt_table *);
extern struct xt_table *ip6t_register_table(struct net *net,
					    const struct xt_table *table,
					    const struct ip6t_replace *repl);
extern void ip6t_unregister_table(struct net *net, struct xt_table *table);
extern unsigned int ip6t_do_table(struct sk_buff *skb,
				  unsigned int hook,
				  const struct net_device *in,
				  const struct net_device *out,
				  struct xt_table *table);

/* Check for an extension */
static inline int
ip6t_ext_hdr(u8 nexthdr)
{	return (nexthdr == IPPROTO_HOPOPTS) ||
	       (nexthdr == IPPROTO_ROUTING) ||
	       (nexthdr == IPPROTO_FRAGMENT) ||
	       (nexthdr == IPPROTO_ESP) ||
	       (nexthdr == IPPROTO_AH) ||
	       (nexthdr == IPPROTO_NONE) ||
	       (nexthdr == IPPROTO_DSTOPTS);
}

enum {
	IP6T_FH_F_FRAG	= (1 << 0),
	IP6T_FH_F_AUTH	= (1 << 1),
};

/* find specified header and get offset to it */
extern int ipv6_find_hdr(const struct sk_buff *skb, unsigned int *offset,
			 int target, unsigned short *fragoff, int *fragflg);

#ifdef CONFIG_COMPAT
#include <net/compat.h>

struct compat_ip6t_entry {
	struct ip6t_ip6 ipv6;
	compat_uint_t nfcache;
	__u16 target_offset;
	__u16 next_offset;
	compat_uint_t comefrom;
	struct compat_xt_counters counters;
	unsigned char elems[0];
};

static inline struct xt_entry_target *
compat_ip6t_get_target(struct compat_ip6t_entry *e)
{
	return (void *)e + e->target_offset;
}

#endif /* CONFIG_COMPAT */
#endif /* _IP6_TABLES_H */
