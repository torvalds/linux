/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_NAT_MASQUERADE_IPV6_H_
#define _NF_NAT_MASQUERADE_IPV6_H_

unsigned int
nf_nat_masquerade_ipv6(struct sk_buff *skb, const struct nf_nat_range2 *range,
		       const struct net_device *out);
void nf_nat_masquerade_ipv6_register_notifier(void);
void nf_nat_masquerade_ipv6_unregister_notifier(void);

#endif /* _NF_NAT_MASQUERADE_IPV6_H_ */
