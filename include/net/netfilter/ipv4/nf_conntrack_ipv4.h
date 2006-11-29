/*
 * IPv4 support for nf_conntrack.
 *
 * 23 Mar 2004: Yasuyuki Kozakai @ USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- move L3 protocol dependent part from include/linux/netfilter_ipv4/
 *	  ip_conntarck.h
 */

#ifndef _NF_CONNTRACK_IPV4_H
#define _NF_CONNTRACK_IPV4_H

#ifdef CONFIG_IP_NF_NAT_NEEDED
#include <linux/netfilter_ipv4/ip_nat.h>

/* per conntrack: nat application helper private data */
union ip_conntrack_nat_help {
        /* insert nat helper private data here */
};

struct nf_conntrack_ipv4_nat {
	struct ip_nat_info info;
	union ip_conntrack_nat_help help;
#if defined(CONFIG_IP_NF_TARGET_MASQUERADE) || \
	defined(CONFIG_IP_NF_TARGET_MASQUERADE_MODULE)
	int masq_index;
#endif
};
#endif /* CONFIG_IP_NF_NAT_NEEDED */

struct nf_conntrack_ipv4 {
#ifdef CONFIG_IP_NF_NAT_NEEDED
	struct nf_conntrack_ipv4_nat *nat;
#endif
};

/* Returns new sk_buff, or NULL */
struct sk_buff *
nf_ct_ipv4_ct_gather_frags(struct sk_buff *skb);

extern struct nf_conntrack_l4proto nf_conntrack_l4proto_tcp4;
extern struct nf_conntrack_l4proto nf_conntrack_l4proto_udp4;
extern struct nf_conntrack_l4proto nf_conntrack_l4proto_icmp;

extern int nf_conntrack_ipv4_compat_init(void);
extern void nf_conntrack_ipv4_compat_fini(void);

#endif /*_NF_CONNTRACK_IPV4_H*/
