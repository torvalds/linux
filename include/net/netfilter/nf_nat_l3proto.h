/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_NAT_L3PROTO_H
#define _NF_NAT_L3PROTO_H

struct nf_nat_l3proto {
	u8	l3proto;

	bool	(*manip_pkt)(struct sk_buff *skb,
			     unsigned int iphdroff,
			     const struct nf_conntrack_tuple *target,
			     enum nf_nat_manip_type maniptype);

	void	(*csum_update)(struct sk_buff *skb, unsigned int iphdroff,
			       __sum16 *check,
			       const struct nf_conntrack_tuple *t,
			       enum nf_nat_manip_type maniptype);

	void	(*csum_recalc)(struct sk_buff *skb, u8 proto,
			       void *data, __sum16 *check,
			       int datalen, int oldlen);

	void	(*decode_session)(struct sk_buff *skb,
				  const struct nf_conn *ct,
				  enum ip_conntrack_dir dir,
				  unsigned long statusbit,
				  struct flowi *fl);

	int	(*nlattr_to_range)(struct nlattr *tb[],
				   struct nf_nat_range2 *range);
};

int nf_nat_l3proto_register(const struct nf_nat_l3proto *);
void nf_nat_l3proto_unregister(const struct nf_nat_l3proto *);
const struct nf_nat_l3proto *__nf_nat_l3proto_find(u8 l3proto);

int nf_nat_icmp_reply_translation(struct sk_buff *skb, struct nf_conn *ct,
				  enum ip_conntrack_info ctinfo,
				  unsigned int hooknum);

int nf_nat_icmpv6_reply_translation(struct sk_buff *skb, struct nf_conn *ct,
				    enum ip_conntrack_info ctinfo,
				    unsigned int hooknum, unsigned int hdrlen);

int nf_nat_l3proto_ipv4_register_fn(struct net *net, const struct nf_hook_ops *ops);
void nf_nat_l3proto_ipv4_unregister_fn(struct net *net, const struct nf_hook_ops *ops);

int nf_nat_l3proto_ipv6_register_fn(struct net *net, const struct nf_hook_ops *ops);
void nf_nat_l3proto_ipv6_unregister_fn(struct net *net, const struct nf_hook_ops *ops);

#endif /* _NF_NAT_L3PROTO_H */
