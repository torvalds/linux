/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header for use in defining a given L4 protocol for connection tracking.
 *
 * 16 Dec 2003: Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 *	- generalized L3 protocol dependent part.
 *
 * Derived from include/linux/netfiter_ipv4/ip_conntrack_protcol.h
 */

#ifndef _NF_CONNTRACK_L4PROTO_H
#define _NF_CONNTRACK_L4PROTO_H
#include <linux/netlink.h>
#include <net/netlink.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netns/generic.h>

struct seq_file;

struct nf_conntrack_l4proto {
	/* L4 Protocol number. */
	u_int8_t l4proto;

	/* Resolve clashes on insertion races. */
	bool allow_clash;

	/* protoinfo nlattr size, closes a hole */
	u16 nlattr_size;

	/* called by gc worker if table is full */
	bool (*can_early_drop)(const struct nf_conn *ct);

	/* convert protoinfo to nfnetink attributes */
	int (*to_nlattr)(struct sk_buff *skb, struct nlattr *nla,
			 struct nf_conn *ct, bool destroy);

	/* convert nfnetlink attributes to protoinfo */
	int (*from_nlattr)(struct nlattr *tb[], struct nf_conn *ct);

	int (*tuple_to_nlattr)(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *t);
	/* Calculate tuple nlattr size */
	unsigned int (*nlattr_tuple_size)(void);
	int (*nlattr_to_tuple)(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t,
			       u_int32_t flags);
	const struct nla_policy *nla_policy;

	struct {
		int (*nlattr_to_obj)(struct nlattr *tb[],
				     struct net *net, void *data);
		int (*obj_to_nlattr)(struct sk_buff *skb, const void *data);

		u16 obj_size;
		u16 nlattr_max;
		const struct nla_policy *nla_policy;
	} ctnl_timeout;
#ifdef CONFIG_NF_CONNTRACK_PROCFS
	/* Print out the private part of the conntrack. */
	void (*print_conntrack)(struct seq_file *s, struct nf_conn *);
#endif
};

bool icmp_pkt_to_tuple(const struct sk_buff *skb,
		       unsigned int dataoff,
		       struct net *net,
		       struct nf_conntrack_tuple *tuple);

bool icmpv6_pkt_to_tuple(const struct sk_buff *skb,
			 unsigned int dataoff,
			 struct net *net,
			 struct nf_conntrack_tuple *tuple);

bool nf_conntrack_invert_icmp_tuple(struct nf_conntrack_tuple *tuple,
				    const struct nf_conntrack_tuple *orig);
bool nf_conntrack_invert_icmpv6_tuple(struct nf_conntrack_tuple *tuple,
				      const struct nf_conntrack_tuple *orig);

int nf_conntrack_inet_error(struct nf_conn *tmpl, struct sk_buff *skb,
			    unsigned int dataoff,
			    const struct nf_hook_state *state,
			    u8 l4proto,
			    union nf_inet_addr *outer_daddr);

int nf_conntrack_icmpv4_error(struct nf_conn *tmpl,
			      struct sk_buff *skb,
			      unsigned int dataoff,
			      const struct nf_hook_state *state);

int nf_conntrack_icmpv6_error(struct nf_conn *tmpl,
			      struct sk_buff *skb,
			      unsigned int dataoff,
			      const struct nf_hook_state *state);

int nf_conntrack_icmp_packet(struct nf_conn *ct,
			     struct sk_buff *skb,
			     enum ip_conntrack_info ctinfo,
			     const struct nf_hook_state *state);

int nf_conntrack_icmpv6_packet(struct nf_conn *ct,
			       struct sk_buff *skb,
			       enum ip_conntrack_info ctinfo,
			       const struct nf_hook_state *state);

int nf_conntrack_udp_packet(struct nf_conn *ct,
			    struct sk_buff *skb,
			    unsigned int dataoff,
			    enum ip_conntrack_info ctinfo,
			    const struct nf_hook_state *state);
int nf_conntrack_udplite_packet(struct nf_conn *ct,
				struct sk_buff *skb,
				unsigned int dataoff,
				enum ip_conntrack_info ctinfo,
				const struct nf_hook_state *state);
int nf_conntrack_tcp_packet(struct nf_conn *ct,
			    struct sk_buff *skb,
			    unsigned int dataoff,
			    enum ip_conntrack_info ctinfo,
			    const struct nf_hook_state *state);
int nf_conntrack_sctp_packet(struct nf_conn *ct,
			     struct sk_buff *skb,
			     unsigned int dataoff,
			     enum ip_conntrack_info ctinfo,
			     const struct nf_hook_state *state);
int nf_conntrack_gre_packet(struct nf_conn *ct,
			    struct sk_buff *skb,
			    unsigned int dataoff,
			    enum ip_conntrack_info ctinfo,
			    const struct nf_hook_state *state);

void nf_conntrack_generic_init_net(struct net *net);
void nf_conntrack_tcp_init_net(struct net *net);
void nf_conntrack_udp_init_net(struct net *net);
void nf_conntrack_gre_init_net(struct net *net);
void nf_conntrack_sctp_init_net(struct net *net);
void nf_conntrack_icmp_init_net(struct net *net);
void nf_conntrack_icmpv6_init_net(struct net *net);

/* Existing built-in generic protocol */
extern const struct nf_conntrack_l4proto nf_conntrack_l4proto_generic;

#define MAX_NF_CT_PROTO IPPROTO_UDPLITE

const struct nf_conntrack_l4proto *nf_ct_l4proto_find(u8 l4proto);

/* Generic netlink helpers */
int nf_ct_port_tuple_to_nlattr(struct sk_buff *skb,
			       const struct nf_conntrack_tuple *tuple);
int nf_ct_port_nlattr_to_tuple(struct nlattr *tb[],
			       struct nf_conntrack_tuple *t,
			       u_int32_t flags);
unsigned int nf_ct_port_nlattr_tuple_size(void);
extern const struct nla_policy nf_ct_port_nla_policy[];

#ifdef CONFIG_SYSCTL
__printf(4, 5) __cold
void nf_ct_l4proto_log_invalid(const struct sk_buff *skb,
			       const struct nf_conn *ct,
			       const struct nf_hook_state *state,
			       const char *fmt, ...);
__printf(4, 5) __cold
void nf_l4proto_log_invalid(const struct sk_buff *skb,
			    const struct nf_hook_state *state,
			    u8 protonum,
			    const char *fmt, ...);
#else
static inline __printf(4, 5) __cold
void nf_l4proto_log_invalid(const struct sk_buff *skb,
			    const struct nf_hook_state *state,
			    u8 protonum,
			    const char *fmt, ...) {}
static inline __printf(4, 5) __cold
void nf_ct_l4proto_log_invalid(const struct sk_buff *skb,
			       const struct nf_conn *ct,
			       const struct nf_hook_state *state,
			       const char *fmt, ...) { }
#endif /* CONFIG_SYSCTL */

#if IS_ENABLED(CONFIG_NF_CONNTRACK)
static inline struct nf_generic_net *nf_generic_pernet(struct net *net)
{
	return &net->ct.nf_ct_proto.generic;
}

static inline struct nf_tcp_net *nf_tcp_pernet(struct net *net)
{
	return &net->ct.nf_ct_proto.tcp;
}

static inline struct nf_udp_net *nf_udp_pernet(struct net *net)
{
	return &net->ct.nf_ct_proto.udp;
}

static inline struct nf_icmp_net *nf_icmp_pernet(struct net *net)
{
	return &net->ct.nf_ct_proto.icmp;
}

static inline struct nf_icmp_net *nf_icmpv6_pernet(struct net *net)
{
	return &net->ct.nf_ct_proto.icmpv6;
}

/* Caller must check nf_ct_protonum(ct) is IPPROTO_TCP before calling. */
static inline void nf_ct_set_tcp_be_liberal(struct nf_conn *ct)
{
	ct->proto.tcp.seen[0].flags |= IP_CT_TCP_FLAG_BE_LIBERAL;
	ct->proto.tcp.seen[1].flags |= IP_CT_TCP_FLAG_BE_LIBERAL;
}

/* Caller must check nf_ct_protonum(ct) is IPPROTO_TCP before calling. */
static inline bool nf_conntrack_tcp_established(const struct nf_conn *ct)
{
	return ct->proto.tcp.state == TCP_CONNTRACK_ESTABLISHED &&
	       test_bit(IPS_ASSURED_BIT, &ct->status);
}
#endif

#ifdef CONFIG_NF_CT_PROTO_SCTP
static inline struct nf_sctp_net *nf_sctp_pernet(struct net *net)
{
	return &net->ct.nf_ct_proto.sctp;
}
#endif

#ifdef CONFIG_NF_CT_PROTO_GRE
static inline struct nf_gre_net *nf_gre_pernet(struct net *net)
{
	return &net->ct.nf_ct_proto.gre;
}
#endif

#endif /*_NF_CONNTRACK_PROTOCOL_H*/
