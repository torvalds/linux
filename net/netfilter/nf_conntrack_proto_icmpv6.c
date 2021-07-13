// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C)2003,2004 USAGI/WIDE Project
 *
 * Author:
 *	Yasuyuki Kozakai @USAGI <yasuyuki.kozakai@toshiba.co.jp>
 */

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/in6.h>
#include <linux/icmpv6.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <net/ip6_checksum.h>
#include <linux/seq_file.h>
#include <linux/netfilter_ipv6.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_timeout.h>
#include <net/netfilter/nf_conntrack_zones.h>
#include <net/netfilter/nf_log.h>

#include "nf_internals.h"

static const unsigned int nf_ct_icmpv6_timeout = 30*HZ;

bool icmpv6_pkt_to_tuple(const struct sk_buff *skb,
			 unsigned int dataoff,
			 struct net *net,
			 struct nf_conntrack_tuple *tuple)
{
	const struct icmp6hdr *hp;
	struct icmp6hdr _hdr;

	hp = skb_header_pointer(skb, dataoff, sizeof(_hdr), &_hdr);
	if (hp == NULL)
		return false;
	tuple->dst.u.icmp.type = hp->icmp6_type;
	tuple->src.u.icmp.id = hp->icmp6_identifier;
	tuple->dst.u.icmp.code = hp->icmp6_code;

	return true;
}

/* Add 1; spaces filled with 0. */
static const u_int8_t invmap[] = {
	[ICMPV6_ECHO_REQUEST - 128]	= ICMPV6_ECHO_REPLY + 1,
	[ICMPV6_ECHO_REPLY - 128]	= ICMPV6_ECHO_REQUEST + 1,
	[ICMPV6_NI_QUERY - 128]		= ICMPV6_NI_REPLY + 1,
	[ICMPV6_NI_REPLY - 128]		= ICMPV6_NI_QUERY + 1
};

static const u_int8_t noct_valid_new[] = {
	[ICMPV6_MGM_QUERY - 130] = 1,
	[ICMPV6_MGM_REPORT - 130] = 1,
	[ICMPV6_MGM_REDUCTION - 130] = 1,
	[NDISC_ROUTER_SOLICITATION - 130] = 1,
	[NDISC_ROUTER_ADVERTISEMENT - 130] = 1,
	[NDISC_NEIGHBOUR_SOLICITATION - 130] = 1,
	[NDISC_NEIGHBOUR_ADVERTISEMENT - 130] = 1,
	[ICMPV6_MLD2_REPORT - 130] = 1
};

bool nf_conntrack_invert_icmpv6_tuple(struct nf_conntrack_tuple *tuple,
				      const struct nf_conntrack_tuple *orig)
{
	int type = orig->dst.u.icmp.type - 128;
	if (type < 0 || type >= sizeof(invmap) || !invmap[type])
		return false;

	tuple->src.u.icmp.id   = orig->src.u.icmp.id;
	tuple->dst.u.icmp.type = invmap[type] - 1;
	tuple->dst.u.icmp.code = orig->dst.u.icmp.code;
	return true;
}

static unsigned int *icmpv6_get_timeouts(struct net *net)
{
	return &nf_icmpv6_pernet(net)->timeout;
}

/* Returns verdict for packet, or -1 for invalid. */
int nf_conntrack_icmpv6_packet(struct nf_conn *ct,
			       struct sk_buff *skb,
			       enum ip_conntrack_info ctinfo,
			       const struct nf_hook_state *state)
{
	unsigned int *timeout = nf_ct_timeout_lookup(ct);
	static const u8 valid_new[] = {
		[ICMPV6_ECHO_REQUEST - 128] = 1,
		[ICMPV6_NI_QUERY - 128] = 1
	};

	if (state->pf != NFPROTO_IPV6)
		return -NF_ACCEPT;

	if (!nf_ct_is_confirmed(ct)) {
		int type = ct->tuplehash[0].tuple.dst.u.icmp.type - 128;

		if (type < 0 || type >= sizeof(valid_new) || !valid_new[type]) {
			/* Can't create a new ICMPv6 `conn' with this. */
			pr_debug("icmpv6: can't create new conn with type %u\n",
				 type + 128);
			nf_ct_dump_tuple_ipv6(&ct->tuplehash[0].tuple);
			return -NF_ACCEPT;
		}
	}

	if (!timeout)
		timeout = icmpv6_get_timeouts(nf_ct_net(ct));

	/* Do not immediately delete the connection after the first
	   successful reply to avoid excessive conntrackd traffic
	   and also to handle correctly ICMP echo reply duplicates. */
	nf_ct_refresh_acct(ct, ctinfo, skb, *timeout);

	return NF_ACCEPT;
}


static void icmpv6_error_log(const struct sk_buff *skb,
			     const struct nf_hook_state *state,
			     const char *msg)
{
	nf_l4proto_log_invalid(skb, state, IPPROTO_ICMPV6, "%s", msg);
}

int nf_conntrack_icmpv6_error(struct nf_conn *tmpl,
			      struct sk_buff *skb,
			      unsigned int dataoff,
			      const struct nf_hook_state *state)
{
	union nf_inet_addr outer_daddr;
	const struct icmp6hdr *icmp6h;
	struct icmp6hdr _ih;
	int type;

	icmp6h = skb_header_pointer(skb, dataoff, sizeof(_ih), &_ih);
	if (icmp6h == NULL) {
		icmpv6_error_log(skb, state, "short packet");
		return -NF_ACCEPT;
	}

	if (state->hook == NF_INET_PRE_ROUTING &&
	    state->net->ct.sysctl_checksum &&
	    nf_ip6_checksum(skb, state->hook, dataoff, IPPROTO_ICMPV6)) {
		icmpv6_error_log(skb, state, "ICMPv6 checksum failed");
		return -NF_ACCEPT;
	}

	type = icmp6h->icmp6_type - 130;
	if (type >= 0 && type < sizeof(noct_valid_new) &&
	    noct_valid_new[type]) {
		nf_ct_set(skb, NULL, IP_CT_UNTRACKED);
		return NF_ACCEPT;
	}

	/* is not error message ? */
	if (icmp6h->icmp6_type >= 128)
		return NF_ACCEPT;

	memcpy(&outer_daddr.ip6, &ipv6_hdr(skb)->daddr,
	       sizeof(outer_daddr.ip6));
	dataoff += sizeof(*icmp6h);
	return nf_conntrack_inet_error(tmpl, skb, dataoff, state,
				       IPPROTO_ICMPV6, &outer_daddr);
}

#if IS_ENABLED(CONFIG_NF_CT_NETLINK)

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
static int icmpv6_tuple_to_nlattr(struct sk_buff *skb,
				  const struct nf_conntrack_tuple *t)
{
	if (nla_put_be16(skb, CTA_PROTO_ICMPV6_ID, t->src.u.icmp.id) ||
	    nla_put_u8(skb, CTA_PROTO_ICMPV6_TYPE, t->dst.u.icmp.type) ||
	    nla_put_u8(skb, CTA_PROTO_ICMPV6_CODE, t->dst.u.icmp.code))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -1;
}

static const struct nla_policy icmpv6_nla_policy[CTA_PROTO_MAX+1] = {
	[CTA_PROTO_ICMPV6_TYPE]	= { .type = NLA_U8 },
	[CTA_PROTO_ICMPV6_CODE]	= { .type = NLA_U8 },
	[CTA_PROTO_ICMPV6_ID]	= { .type = NLA_U16 },
};

static int icmpv6_nlattr_to_tuple(struct nlattr *tb[],
				struct nf_conntrack_tuple *tuple,
				u_int32_t flags)
{
	if (flags & CTA_FILTER_FLAG(CTA_PROTO_ICMPV6_TYPE)) {
		if (!tb[CTA_PROTO_ICMPV6_TYPE])
			return -EINVAL;

		tuple->dst.u.icmp.type = nla_get_u8(tb[CTA_PROTO_ICMPV6_TYPE]);
		if (tuple->dst.u.icmp.type < 128 ||
		    tuple->dst.u.icmp.type - 128 >= sizeof(invmap) ||
		    !invmap[tuple->dst.u.icmp.type - 128])
			return -EINVAL;
	}

	if (flags & CTA_FILTER_FLAG(CTA_PROTO_ICMPV6_CODE)) {
		if (!tb[CTA_PROTO_ICMPV6_CODE])
			return -EINVAL;

		tuple->dst.u.icmp.code = nla_get_u8(tb[CTA_PROTO_ICMPV6_CODE]);
	}

	if (flags & CTA_FILTER_FLAG(CTA_PROTO_ICMPV6_ID)) {
		if (!tb[CTA_PROTO_ICMPV6_ID])
			return -EINVAL;

		tuple->src.u.icmp.id = nla_get_be16(tb[CTA_PROTO_ICMPV6_ID]);
	}

	return 0;
}

static unsigned int icmpv6_nlattr_tuple_size(void)
{
	static unsigned int size __read_mostly;

	if (!size)
		size = nla_policy_len(icmpv6_nla_policy, CTA_PROTO_MAX + 1);

	return size;
}
#endif

#ifdef CONFIG_NF_CONNTRACK_TIMEOUT

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_cttimeout.h>

static int icmpv6_timeout_nlattr_to_obj(struct nlattr *tb[],
					struct net *net, void *data)
{
	unsigned int *timeout = data;
	struct nf_icmp_net *in = nf_icmpv6_pernet(net);

	if (!timeout)
		timeout = icmpv6_get_timeouts(net);
	if (tb[CTA_TIMEOUT_ICMPV6_TIMEOUT]) {
		*timeout =
		    ntohl(nla_get_be32(tb[CTA_TIMEOUT_ICMPV6_TIMEOUT])) * HZ;
	} else {
		/* Set default ICMPv6 timeout. */
		*timeout = in->timeout;
	}
	return 0;
}

static int
icmpv6_timeout_obj_to_nlattr(struct sk_buff *skb, const void *data)
{
	const unsigned int *timeout = data;

	if (nla_put_be32(skb, CTA_TIMEOUT_ICMPV6_TIMEOUT, htonl(*timeout / HZ)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -ENOSPC;
}

static const struct nla_policy
icmpv6_timeout_nla_policy[CTA_TIMEOUT_ICMPV6_MAX+1] = {
	[CTA_TIMEOUT_ICMPV6_TIMEOUT]	= { .type = NLA_U32 },
};
#endif /* CONFIG_NF_CONNTRACK_TIMEOUT */

void nf_conntrack_icmpv6_init_net(struct net *net)
{
	struct nf_icmp_net *in = nf_icmpv6_pernet(net);

	in->timeout = nf_ct_icmpv6_timeout;
}

const struct nf_conntrack_l4proto nf_conntrack_l4proto_icmpv6 =
{
	.l4proto		= IPPROTO_ICMPV6,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.tuple_to_nlattr	= icmpv6_tuple_to_nlattr,
	.nlattr_tuple_size	= icmpv6_nlattr_tuple_size,
	.nlattr_to_tuple	= icmpv6_nlattr_to_tuple,
	.nla_policy		= icmpv6_nla_policy,
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	.ctnl_timeout		= {
		.nlattr_to_obj	= icmpv6_timeout_nlattr_to_obj,
		.obj_to_nlattr	= icmpv6_timeout_obj_to_nlattr,
		.nlattr_max	= CTA_TIMEOUT_ICMP_MAX,
		.obj_size	= sizeof(unsigned int),
		.nla_policy	= icmpv6_timeout_nla_policy,
	},
#endif /* CONFIG_NF_CONNTRACK_TIMEOUT */
};
