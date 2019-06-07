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

void synproxy_send_client_synack(struct net *net, const struct sk_buff *skb,
				 const struct tcphdr *th,
				 const struct synproxy_options *opts);

bool synproxy_recv_client_ack(struct net *net,
			      const struct sk_buff *skb,
			      const struct tcphdr *th,
			      struct synproxy_options *opts, u32 recv_seq);

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
#endif /* CONFIG_IPV6 */

#endif /* _NF_SYNPROXY_SHARED_H */
