/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NF_DUP_NETDEV_H_
#define _NF_DUP_NETDEV_H_

#include <net/netfilter/nf_tables.h>

void nf_dup_netdev_egress(const struct nft_pktinfo *pkt, int oif);
void nf_fwd_netdev_egress(const struct nft_pktinfo *pkt, int oif);

struct nft_offload_ctx;
struct nft_flow_rule;

int nft_fwd_dup_netdev_offload(struct nft_offload_ctx *ctx,
			       struct nft_flow_rule *flow,
			       enum flow_action_id id, int oif);
#endif
