#ifndef _NF_DUP_IPV6_H_
#define _NF_DUP_IPV6_H_

void nf_dup_ipv6(struct net *net, struct sk_buff *skb, unsigned int hooknum,
		 const struct in6_addr *gw, int oif);

#endif /* _NF_DUP_IPV6_H_ */
