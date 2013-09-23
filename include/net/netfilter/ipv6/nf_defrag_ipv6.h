#ifndef _NF_DEFRAG_IPV6_H
#define _NF_DEFRAG_IPV6_H

void nf_defrag_ipv6_enable(void);

int nf_ct_frag6_init(void);
void nf_ct_frag6_cleanup(void);
struct sk_buff *nf_ct_frag6_gather(struct sk_buff *skb, u32 user);
void nf_ct_frag6_output(unsigned int hooknum, struct sk_buff *skb,
			struct net_device *in, struct net_device *out,
			int (*okfn)(struct sk_buff *));

struct inet_frags_ctl;

#endif /* _NF_DEFRAG_IPV6_H */
