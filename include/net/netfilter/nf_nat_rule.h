#ifndef _NF_NAT_RULE_H
#define _NF_NAT_RULE_H
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_nat.h>
#include <linux/netfilter_ipv4/ip_tables.h>

/* Compatibility definitions for ipt_FOO modules */
#define ip_nat_range			nf_nat_range
#define ip_conntrack_tuple		nf_conntrack_tuple
#define ip_conntrack_get		nf_ct_get
#define ip_conntrack			nf_conn
#define ip_nat_setup_info		nf_nat_setup_info
#define ip_nat_multi_range_compat	nf_nat_multi_range_compat
#define ip_ct_iterate_cleanup		nf_ct_iterate_cleanup
#define	IP_NF_ASSERT			NF_CT_ASSERT

extern int nf_nat_rule_init(void) __init;
extern void nf_nat_rule_cleanup(void);
extern int nf_nat_rule_find(struct sk_buff **pskb,
			    unsigned int hooknum,
			    const struct net_device *in,
			    const struct net_device *out,
			    struct nf_conn *ct,
			    struct nf_nat_info *info);

extern unsigned int
alloc_null_binding(struct nf_conn *ct,
		   struct nf_nat_info *info,
		   unsigned int hooknum);

extern unsigned int
alloc_null_binding_confirmed(struct nf_conn *ct,
			     struct nf_nat_info *info,
			     unsigned int hooknum);
#endif /* _NF_NAT_RULE_H */
