/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_NAT_H
#define _NF_NAT_H

#include <linux/list.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter/nf_conntrack_pptp.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <uapi/linux/netfilter/nf_nat.h>

enum nf_nat_manip_type {
	NF_NAT_MANIP_SRC,
	NF_NAT_MANIP_DST
};

/* SRC manip occurs POST_ROUTING or LOCAL_IN */
#define HOOK2MANIP(hooknum) ((hooknum) != NF_INET_POST_ROUTING && \
			     (hooknum) != NF_INET_LOCAL_IN)

/* per conntrack: nat application helper private data */
union nf_conntrack_nat_help {
	/* insert nat helper private data here */
#if IS_ENABLED(CONFIG_NF_NAT_PPTP)
	struct nf_nat_pptp nat_pptp_info;
#endif
};

/* The structure embedded in the conntrack structure. */
struct nf_conn_nat {
	union nf_conntrack_nat_help help;
#if IS_ENABLED(CONFIG_NF_NAT_MASQUERADE)
	int masq_index;
#endif
};

/* Set up the info structure to map into this range. */
unsigned int nf_nat_setup_info(struct nf_conn *ct,
			       const struct nf_nat_range2 *range,
			       enum nf_nat_manip_type maniptype);

extern unsigned int nf_nat_alloc_null_binding(struct nf_conn *ct,
					      unsigned int hooknum);

struct nf_conn_nat *nf_ct_nat_ext_add(struct nf_conn *ct);

static inline struct nf_conn_nat *nfct_nat(const struct nf_conn *ct)
{
#if IS_ENABLED(CONFIG_NF_NAT)
	return nf_ct_ext_find(ct, NF_CT_EXT_NAT);
#else
	return NULL;
#endif
}

static inline bool nf_nat_oif_changed(unsigned int hooknum,
				      enum ip_conntrack_info ctinfo,
				      struct nf_conn_nat *nat,
				      const struct net_device *out)
{
#if IS_ENABLED(CONFIG_NF_NAT_MASQUERADE)
	return nat && nat->masq_index && hooknum == NF_INET_POST_ROUTING &&
	       CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL &&
	       nat->masq_index != out->ifindex;
#else
	return false;
#endif
}

int nf_nat_register_fn(struct net *net, u8 pf, const struct nf_hook_ops *ops,
		       const struct nf_hook_ops *nat_ops, unsigned int ops_count);
void nf_nat_unregister_fn(struct net *net, u8 pf, const struct nf_hook_ops *ops,
			  unsigned int ops_count);

unsigned int nf_nat_packet(struct nf_conn *ct, enum ip_conntrack_info ctinfo,
			   unsigned int hooknum, struct sk_buff *skb);

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

int nf_nat_ipv4_register_fn(struct net *net, const struct nf_hook_ops *ops);
void nf_nat_ipv4_unregister_fn(struct net *net, const struct nf_hook_ops *ops);

int nf_nat_ipv6_register_fn(struct net *net, const struct nf_hook_ops *ops);
void nf_nat_ipv6_unregister_fn(struct net *net, const struct nf_hook_ops *ops);

int nf_nat_inet_register_fn(struct net *net, const struct nf_hook_ops *ops);
void nf_nat_inet_unregister_fn(struct net *net, const struct nf_hook_ops *ops);

unsigned int
nf_nat_inet_fn(void *priv, struct sk_buff *skb,
	       const struct nf_hook_state *state);

static inline int nf_nat_initialized(const struct nf_conn *ct,
				     enum nf_nat_manip_type manip)
{
	if (manip == NF_NAT_MANIP_SRC)
		return ct->status & IPS_SRC_NAT_DONE;
	else
		return ct->status & IPS_DST_NAT_DONE;
}
#endif
