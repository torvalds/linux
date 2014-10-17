#ifndef _NF_NAT_REDIRECT_IPV6_H_
#define _NF_NAT_REDIRECT_IPV6_H_

unsigned int
nf_nat_redirect_ipv6(struct sk_buff *skb, const struct nf_nat_range *range,
		     unsigned int hooknum);

#endif /* _NF_NAT_REDIRECT_IPV6_H_ */
