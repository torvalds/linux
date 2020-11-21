/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _IPV6_NF_REJECT_H
#define _IPV6_NF_REJECT_H

#include <linux/icmpv6.h>
#include <net/netfilter/nf_reject.h>

void nf_send_unreach6(struct net *net, struct sk_buff *skb_in, unsigned char code,
		      unsigned int hooknum);
void nf_send_reset6(struct net *net, struct sock *sk, struct sk_buff *oldskb,
		    int hook);
const struct tcphdr *nf_reject_ip6_tcphdr_get(struct sk_buff *oldskb,
					      struct tcphdr *otcph,
					      unsigned int *otcplen, int hook);
struct ipv6hdr *nf_reject_ip6hdr_put(struct sk_buff *nskb,
				     const struct sk_buff *oldskb,
				     __u8 protocol, int hoplimit);
void nf_reject_ip6_tcphdr_put(struct sk_buff *nskb,
			      const struct sk_buff *oldskb,
			      const struct tcphdr *oth, unsigned int otcplen);

#endif /* _IPV6_NF_REJECT_H */
