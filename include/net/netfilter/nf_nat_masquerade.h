/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_NAT_MASQUERADE_H_
#define _NF_NAT_MASQUERADE_H_

#include <net/netfilter/nf_nat.h>

unsigned int
nf_nat_masquerade_ipv4(struct sk_buff *skb, unsigned int hooknum,
		       const struct nf_nat_range2 *range,
		       const struct net_device *out);

int nf_nat_masquerade_inet_register_notifiers(void);
void nf_nat_masquerade_inet_unregister_notifiers(void);

unsigned int
nf_nat_masquerade_ipv6(struct sk_buff *skb, const struct nf_nat_range2 *range,
		       const struct net_device *out);

#endif /*_NF_NAT_MASQUERADE_H_ */
