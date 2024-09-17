/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_SYNPROXY_SHARED_H
#define _NF_SYNPROXY_SHARED_H

#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/ip6_checksum.h>
#include <net/ip6_route.h>
#include <net/tcp.h>

#include <net/netfilter/nf_conntrack_seqadj.h>
#include <net/netfilter/nf_conntrack_synproxy.h>

struct synproxy_stats {
	unsigned int			syn_received;
	unsigned int			cookie_invalid;
	unsigned int			cookie_valid;
	unsigned int			cookie_retrans;
	unsigned int			conn_reopened;
};

struct synproxy_net {
	struct nf_conn			*tmpl;
	struct synproxy_stats __percpu	*stats;
	unsigned int			hook_ref4;
	unsigned int			hook_ref6;
};

extern unsigned int synproxy_net_id;
static inline struct synproxy_net *synproxy_pernet(struct net *net)
{
	return net_generic(net, synproxy_net_id);
}

struct synproxy_options {
	u8				options;
	u8				wscale;
	u16				mss_option;
	u16				mss_encode;
	u32				tsval;
	u32				tsecr;
};

struct nf_synproxy_info;
bool synproxy_parse_options(const struct sk_buff *skb, unsigned int doff,
			    const struct tcphdr *th,
			    struct synproxy_options *opts);

void synproxy_init_timestamp_cookie(const struct nf_synproxy_info *info,
				    struct synproxy_options *opts);

void synproxy_send_client_synack(struct net *net, const struct sk_buff *skb,
				 const struct tcphdr *th,
				 const struct synproxy_options *opts);

bool synproxy_recv_client_ack(struct net *net,
			      const struct sk_buff *skb,
			      const struct tcphdr *th,
			      struct synproxy_options *opts, u32 recv_seq);

struct nf_hook_state;

unsigned int ipv4_synproxy_hook(void *priv, struct sk_buff *skb,
				const struct nf_hook_state *nhs);
int nf_synproxy_ipv4_init(struct synproxy_net *snet, struct net *net);
void nf_synproxy_ipv4_fini(struct synproxy_net *snet, struct net *net);

#if IS_ENABLED(CONFIG_IPV6)
void synproxy_send_client_synack_ipv6(struct net *net,
				      const struct sk_buff *skb,
				      const struct tcphdr *th,
				      const struct synproxy_options *opts);

bool synproxy_recv_client_ack_ipv6(struct net *net, const struct sk_buff *skb,
				   const struct tcphdr *th,
				   struct synproxy_options *opts, u32 recv_seq);

unsigned int ipv6_synproxy_hook(void *priv, struct sk_buff *skb,
				const struct nf_hook_state *nhs);
int nf_synproxy_ipv6_init(struct synproxy_net *snet, struct net *net);
void nf_synproxy_ipv6_fini(struct synproxy_net *snet, struct net *net);
#else
static inline int
nf_synproxy_ipv6_init(struct synproxy_net *snet, struct net *net) { return 0; }
static inline void
nf_synproxy_ipv6_fini(struct synproxy_net *snet, struct net *net) {};
#endif /* CONFIG_IPV6 */

#endif /* _NF_SYNPROXY_SHARED_H */
