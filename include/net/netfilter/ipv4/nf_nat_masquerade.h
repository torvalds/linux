/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_NAT_MASQUERADE_IPV4_H_
#define _NF_NAT_MASQUERADE_IPV4_H_

#include <net/netfilter/nf_nat.h>

unsigned int
nf_nat_masquerade_ipv4(struct sk_buff *skb, unsigned int hooknum,
		       const struct nf_nat_range2 *range,
		       const struct net_device *out);

int nf_nat_masquerade_ipv4_register_notifier(void);
void nf_nat_masquerade_ipv4_unregister_notifier(void);

#endif /*_NF_NAT_MASQUERADE_IPV4_H_ */
