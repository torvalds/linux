/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_NAT_L3PROTO_H
#define _NF_NAT_L3PROTO_H

unsigned int nf_nat_manip_pkt(struct sk_buff *skb, struct nf_conn *ct,
			      enum nf_nat_manip_type mtype,
			      enum ip_conntrack_dir dir);
void nf_nat_csum_recalc(struct sk_buff *skb,
			u8 nfproto, u8 proto, void *data, __sum16 *check,
			int datalen, int oldlen);

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
