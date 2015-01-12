#ifndef _NF_NAT_REDIRECT_H_
#define _NF_NAT_REDIRECT_H_

unsigned int
nf_nat_redirect_ipv4(struct sk_buff *skb,
		     const struct nf_nat_ipv4_multi_range_compat *mr,
		     unsigned int hooknum);
unsigned int
nf_nat_redirect_ipv6(struct sk_buff *skb, const struct nf_nat_range *range,
		     unsigned int hooknum);

#endif /* _NF_NAT_REDIRECT_H_ */
