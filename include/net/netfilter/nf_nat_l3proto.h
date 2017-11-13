/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_NAT_L3PROTO_H
#define _NF_NAT_L3PROTO_H

struct nf_nat_l4proto;
struct nf_nat_l3proto {
	u8	l3proto;

	bool	(*in_range)(const struct nf_conntrack_tuple *t,
			    const struct nf_nat_range *range);

	u32 	(*secure_port)(const struct nf_conntrack_tuple *t, __be16);

	bool	(*manip_pkt)(struct sk_buff *skb,
			     unsigned int iphdroff,
			     const struct nf_nat_l4proto *l4proto,
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
				   struct nf_nat_range *range);
};

int nf_nat_l3proto_register(const struct nf_nat_l3proto *);
void nf_nat_l3proto_unregister(const struct nf_nat_l3proto *);
const struct nf_nat_l3proto *__nf_nat_l3proto_find(u8 l3proto);

int nf_nat_icmp_reply_translation(struct sk_buff *skb, struct nf_conn *ct,
				  enum ip_conntrack_info ctinfo,
				  unsigned int hooknum);

unsigned int nf_nat_ipv4_in(void *priv, struct sk_buff *skb,
			    const struct nf_hook_state *state,
			    unsigned int (*do_chain)(void *priv,
						     struct sk_buff *skb,
						     const struct nf_hook_state *state,
						     struct nf_conn *ct));

unsigned int nf_nat_ipv4_out(void *priv, struct sk_buff *skb,
			     const struct nf_hook_state *state,
			     unsigned int (*do_chain)(void *priv,
						      struct sk_buff *skb,
						      const struct nf_hook_state *state,
						      struct nf_conn *ct));

unsigned int nf_nat_ipv4_local_fn(void *priv,
				  struct sk_buff *skb,
				  const struct nf_hook_state *state,
				  unsigned int (*do_chain)(void *priv,
							   struct sk_buff *skb,
							   const struct nf_hook_state *state,
							   struct nf_conn *ct));

unsigned int nf_nat_ipv4_fn(void *priv, struct sk_buff *skb,
			    const struct nf_hook_state *state,
			    unsigned int (*do_chain)(void *priv,
						     struct sk_buff *skb,
						     const struct nf_hook_state *state,
						     struct nf_conn *ct));

int nf_nat_icmpv6_reply_translation(struct sk_buff *skb, struct nf_conn *ct,
				    enum ip_conntrack_info ctinfo,
				    unsigned int hooknum, unsigned int hdrlen);

unsigned int nf_nat_ipv6_in(void *priv, struct sk_buff *skb,
			    const struct nf_hook_state *state,
			    unsigned int (*do_chain)(void *priv,
						     struct sk_buff *skb,
						     const struct nf_hook_state *state,
						     struct nf_conn *ct));

unsigned int nf_nat_ipv6_out(void *priv, struct sk_buff *skb,
			     const struct nf_hook_state *state,
			     unsigned int (*do_chain)(void *priv,
						      struct sk_buff *skb,
						      const struct nf_hook_state *state,
						      struct nf_conn *ct));

unsigned int nf_nat_ipv6_local_fn(void *priv,
				  struct sk_buff *skb,
				  const struct nf_hook_state *state,
				  unsigned int (*do_chain)(void *priv,
							   struct sk_buff *skb,
							   const struct nf_hook_state *state,
							   struct nf_conn *ct));

unsigned int nf_nat_ipv6_fn(void *priv, struct sk_buff *skb,
			    const struct nf_hook_state *state,
			    unsigned int (*do_chain)(void *priv,
						     struct sk_buff *skb,
						     const struct nf_hook_state *state,
						     struct nf_conn *ct));

#endif /* _NF_NAT_L3PROTO_H */
