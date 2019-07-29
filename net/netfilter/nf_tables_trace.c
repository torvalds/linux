// SPDX-License-Identifier: GPL-2.0-only
/*
 * (C) 2015 Red Hat GmbH
 * Author: Florian Westphal <fw@strlen.de>
 */

#include <linux/module.h>
#include <linux/static_key.h>
#include <linux/hash.h>
#include <linux/jhash.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_tables.h>

#define NFT_TRACETYPE_LL_HSIZE		20
#define NFT_TRACETYPE_NETWORK_HSIZE	40
#define NFT_TRACETYPE_TRANSPORT_HSIZE	20

DEFINE_STATIC_KEY_FALSE(nft_trace_enabled);
EXPORT_SYMBOL_GPL(nft_trace_enabled);

static int trace_fill_id(struct sk_buff *nlskb, struct sk_buff *skb)
{
	__be32 id;

	/* using skb address as ID results in a limited number of
	 * values (and quick reuse).
	 *
	 * So we attempt to use as many skb members that will not
	 * change while skb is with netfilter.
	 */
	id = (__be32)jhash_2words(hash32_ptr(skb), skb_get_hash(skb),
				  skb->skb_iif);

	return nla_put_be32(nlskb, NFTA_TRACE_ID, id);
}

static int trace_fill_header(struct sk_buff *nlskb, u16 type,
			     const struct sk_buff *skb,
			     int off, unsigned int len)
{
	struct nlattr *nla;

	if (len == 0)
		return 0;

	nla = nla_reserve(nlskb, type, len);
	if (!nla || skb_copy_bits(skb, off, nla_data(nla), len))
		return -1;

	return 0;
}

static int nf_trace_fill_ll_header(struct sk_buff *nlskb,
				   const struct sk_buff *skb)
{
	struct vlan_ethhdr veth;
	int off;

	BUILD_BUG_ON(sizeof(veth) > NFT_TRACETYPE_LL_HSIZE);

	off = skb_mac_header(skb) - skb->data;
	if (off != -ETH_HLEN)
		return -1;

	if (skb_copy_bits(skb, off, &veth, ETH_HLEN))
		return -1;

	veth.h_vlan_proto = skb->vlan_proto;
	veth.h_vlan_TCI = htons(skb_vlan_tag_get(skb));
	veth.h_vlan_encapsulated_proto = skb->protocol;

	return nla_put(nlskb, NFTA_TRACE_LL_HEADER, sizeof(veth), &veth);
}

static int nf_trace_fill_dev_info(struct sk_buff *nlskb,
				  const struct net_device *indev,
				  const struct net_device *outdev)
{
	if (indev) {
		if (nla_put_be32(nlskb, NFTA_TRACE_IIF,
				 htonl(indev->ifindex)))
			return -1;

		if (nla_put_be16(nlskb, NFTA_TRACE_IIFTYPE,
				 htons(indev->type)))
			return -1;
	}

	if (outdev) {
		if (nla_put_be32(nlskb, NFTA_TRACE_OIF,
				 htonl(outdev->ifindex)))
			return -1;

		if (nla_put_be16(nlskb, NFTA_TRACE_OIFTYPE,
				 htons(outdev->type)))
			return -1;
	}

	return 0;
}

static int nf_trace_fill_pkt_info(struct sk_buff *nlskb,
				  const struct nft_pktinfo *pkt)
{
	const struct sk_buff *skb = pkt->skb;
	int off = skb_network_offset(skb);
	unsigned int len, nh_end;

	nh_end = pkt->tprot_set ? pkt->xt.thoff : skb->len;
	len = min_t(unsigned int, nh_end - skb_network_offset(skb),
		    NFT_TRACETYPE_NETWORK_HSIZE);
	if (trace_fill_header(nlskb, NFTA_TRACE_NETWORK_HEADER, skb, off, len))
		return -1;

	if (pkt->tprot_set) {
		len = min_t(unsigned int, skb->len - pkt->xt.thoff,
			    NFT_TRACETYPE_TRANSPORT_HSIZE);
		if (trace_fill_header(nlskb, NFTA_TRACE_TRANSPORT_HEADER, skb,
				      pkt->xt.thoff, len))
			return -1;
	}

	if (!skb_mac_header_was_set(skb))
		return 0;

	if (skb_vlan_tag_get(skb))
		return nf_trace_fill_ll_header(nlskb, skb);

	off = skb_mac_header(skb) - skb->data;
	len = min_t(unsigned int, -off, NFT_TRACETYPE_LL_HSIZE);
	return trace_fill_header(nlskb, NFTA_TRACE_LL_HEADER,
				 skb, off, len);
}

static int nf_trace_fill_rule_info(struct sk_buff *nlskb,
				   const struct nft_traceinfo *info)
{
	if (!info->rule)
		return 0;

	/* a continue verdict with ->type == RETURN means that this is
	 * an implicit return (end of chain reached).
	 *
	 * Since no rule matched, the ->rule pointer is invalid.
	 */
	if (info->type == NFT_TRACETYPE_RETURN &&
	    info->verdict->code == NFT_CONTINUE)
		return 0;

	return nla_put_be64(nlskb, NFTA_TRACE_RULE_HANDLE,
			    cpu_to_be64(info->rule->handle),
			    NFTA_TRACE_PAD);
}

static bool nft_trace_have_verdict_chain(struct nft_traceinfo *info)
{
	switch (info->type) {
	case NFT_TRACETYPE_RETURN:
	case NFT_TRACETYPE_RULE:
		break;
	default:
		return false;
	}

	switch (info->verdict->code) {
	case NFT_JUMP:
	case NFT_GOTO:
		break;
	default:
		return false;
	}

	return true;
}

void nft_trace_notify(struct nft_traceinfo *info)
{
	const struct nft_pktinfo *pkt = info->pkt;
	struct nfgenmsg *nfmsg;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	unsigned int size;
	u16 event;

	if (!nfnetlink_has_listeners(nft_net(pkt), NFNLGRP_NFTRACE))
		return;

	size = nlmsg_total_size(sizeof(struct nfgenmsg)) +
		nla_total_size(strlen(info->chain->table->name)) +
		nla_total_size(strlen(info->chain->name)) +
		nla_total_size_64bit(sizeof(__be64)) +	/* rule handle */
		nla_total_size(sizeof(__be32)) +	/* trace type */
		nla_total_size(0) +			/* VERDICT, nested */
			nla_total_size(sizeof(u32)) +	/* verdict code */
		nla_total_size(sizeof(u32)) +		/* id */
		nla_total_size(NFT_TRACETYPE_LL_HSIZE) +
		nla_total_size(NFT_TRACETYPE_NETWORK_HSIZE) +
		nla_total_size(NFT_TRACETYPE_TRANSPORT_HSIZE) +
		nla_total_size(sizeof(u32)) +		/* iif */
		nla_total_size(sizeof(__be16)) +	/* iiftype */
		nla_total_size(sizeof(u32)) +		/* oif */
		nla_total_size(sizeof(__be16)) +	/* oiftype */
		nla_total_size(sizeof(u32)) +		/* mark */
		nla_total_size(sizeof(u32)) +		/* nfproto */
		nla_total_size(sizeof(u32));		/* policy */

	if (nft_trace_have_verdict_chain(info))
		size += nla_total_size(strlen(info->verdict->chain->name)); /* jump target */

	skb = nlmsg_new(size, GFP_ATOMIC);
	if (!skb)
		return;

	event = nfnl_msg_type(NFNL_SUBSYS_NFTABLES, NFT_MSG_TRACE);
	nlh = nlmsg_put(skb, 0, 0, event, sizeof(struct nfgenmsg), 0);
	if (!nlh)
		goto nla_put_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family	= info->basechain->type->family;
	nfmsg->version		= NFNETLINK_V0;
	nfmsg->res_id		= 0;

	if (nla_put_be32(skb, NFTA_TRACE_NFPROTO, htonl(nft_pf(pkt))))
		goto nla_put_failure;

	if (nla_put_be32(skb, NFTA_TRACE_TYPE, htonl(info->type)))
		goto nla_put_failure;

	if (trace_fill_id(skb, pkt->skb))
		goto nla_put_failure;

	if (nla_put_string(skb, NFTA_TRACE_CHAIN, info->chain->name))
		goto nla_put_failure;

	if (nla_put_string(skb, NFTA_TRACE_TABLE, info->chain->table->name))
		goto nla_put_failure;

	if (nf_trace_fill_rule_info(skb, info))
		goto nla_put_failure;

	switch (info->type) {
	case NFT_TRACETYPE_UNSPEC:
	case __NFT_TRACETYPE_MAX:
		break;
	case NFT_TRACETYPE_RETURN:
	case NFT_TRACETYPE_RULE:
		if (nft_verdict_dump(skb, NFTA_TRACE_VERDICT, info->verdict))
			goto nla_put_failure;
		break;
	case NFT_TRACETYPE_POLICY:
		if (nla_put_be32(skb, NFTA_TRACE_POLICY,
				 htonl(info->basechain->policy)))
			goto nla_put_failure;
		break;
	}

	if (pkt->skb->mark &&
	    nla_put_be32(skb, NFTA_TRACE_MARK, htonl(pkt->skb->mark)))
		goto nla_put_failure;

	if (!info->packet_dumped) {
		if (nf_trace_fill_dev_info(skb, nft_in(pkt), nft_out(pkt)))
			goto nla_put_failure;

		if (nf_trace_fill_pkt_info(skb, pkt))
			goto nla_put_failure;
		info->packet_dumped = true;
	}

	nlmsg_end(skb, nlh);
	nfnetlink_send(skb, nft_net(pkt), 0, NFNLGRP_NFTRACE, 0, GFP_ATOMIC);
	return;

 nla_put_failure:
	WARN_ON_ONCE(1);
	kfree_skb(skb);
}

void nft_trace_init(struct nft_traceinfo *info, const struct nft_pktinfo *pkt,
		    const struct nft_verdict *verdict,
		    const struct nft_chain *chain)
{
	info->basechain = nft_base_chain(chain);
	info->trace = true;
	info->packet_dumped = false;
	info->pkt = pkt;
	info->verdict = verdict;
}
