#ifndef _NF_DUP_IPV4_H_
#define _NF_DUP_IPV4_H_

void nf_dup_ipv4(struct sk_buff *skb, unsigned int hooknum,
		 const struct in_addr *gw, int oif);

#endif /* _NF_DUP_IPV4_H_ */
