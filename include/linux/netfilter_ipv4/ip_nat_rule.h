#ifndef _IP_NAT_RULE_H
#define _IP_NAT_RULE_H
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_nat.h>

#ifdef __KERNEL__

extern int ip_nat_rule_init(void) __init;
extern void ip_nat_rule_cleanup(void);
extern int ip_nat_rule_find(struct sk_buff **pskb,
			    unsigned int hooknum,
			    const struct net_device *in,
			    const struct net_device *out,
			    struct ip_conntrack *ct,
			    struct ip_nat_info *info);

extern unsigned int
alloc_null_binding(struct ip_conntrack *conntrack,
		   struct ip_nat_info *info,
		   unsigned int hooknum);

extern unsigned int
alloc_null_binding_confirmed(struct ip_conntrack *conntrack,
			     struct ip_nat_info *info,
			     unsigned int hooknum);
#endif
#endif /* _IP_NAT_RULE_H */
