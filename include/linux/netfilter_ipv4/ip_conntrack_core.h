#ifndef _IP_CONNTRACK_CORE_H
#define _IP_CONNTRACK_CORE_H
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/lockhelp.h>

/* This header is used to share core functionality between the
   standalone connection tracking module, and the compatibility layer's use
   of connection tracking. */
extern unsigned int ip_conntrack_in(unsigned int hooknum,
				    struct sk_buff **pskb,
				    const struct net_device *in,
				    const struct net_device *out,
				    int (*okfn)(struct sk_buff *));

extern int ip_conntrack_init(void);
extern void ip_conntrack_cleanup(void);

struct ip_conntrack_protocol;

extern int
ip_ct_get_tuple(const struct iphdr *iph,
		const struct sk_buff *skb,
		unsigned int dataoff,
		struct ip_conntrack_tuple *tuple,
		const struct ip_conntrack_protocol *protocol);

extern int
ip_ct_invert_tuple(struct ip_conntrack_tuple *inverse,
		   const struct ip_conntrack_tuple *orig,
		   const struct ip_conntrack_protocol *protocol);

/* Find a connection corresponding to a tuple. */
struct ip_conntrack_tuple_hash *
ip_conntrack_find_get(const struct ip_conntrack_tuple *tuple,
		      const struct ip_conntrack *ignored_conntrack);

extern int __ip_conntrack_confirm(struct sk_buff **pskb);

/* Confirm a connection: returns NF_DROP if packet must be dropped. */
static inline int ip_conntrack_confirm(struct sk_buff **pskb)
{
	if ((*pskb)->nfct
	    && !is_confirmed((struct ip_conntrack *)(*pskb)->nfct))
		return __ip_conntrack_confirm(pskb);
	return NF_ACCEPT;
}

extern struct list_head *ip_conntrack_hash;
extern struct list_head ip_conntrack_expect_list;
DECLARE_RWLOCK_EXTERN(ip_conntrack_lock);
#endif /* _IP_CONNTRACK_CORE_H */

