#ifndef _NF_NAT_RULE_H
#define _NF_NAT_RULE_H
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat.h>
#include <linux/netfilter_ipv4/ip_tables.h>

extern int nf_nat_rule_init(void) __init;
extern void nf_nat_rule_cleanup(void);
extern int nf_nat_rule_find(struct sk_buff *skb,
			    unsigned int hooknum,
			    const struct net_device *in,
			    const struct net_device *out,
			    struct nf_conn *ct);

extern unsigned int
alloc_null_binding(struct nf_conn *ct, unsigned int hooknum);

extern unsigned int
alloc_null_binding_confirmed(struct nf_conn *ct, unsigned int hooknum);
#endif /* _NF_NAT_RULE_H */
