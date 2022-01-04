// SPDX-License-Identifier: GPL-2.0+
/*
 *  IPv6 IOAM Lightweight Tunnel implementation
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include <linux/netlink.h>
#include <linux/in6.h>
#include <linux/ioam6.h>
#include <linux/ioam6_iptunnel.h>
#include <net/dst.h>
#include <net/sock.h>
#include <net/lwtunnel.h>
#include <net/ioam6.h>

#define IOAM6_MASK_SHORT_FIELDS 0xff100000
#define IOAM6_MASK_WIDE_FIELDS 0xe00000

struct ioam6_lwt_encap {
	struct ipv6_hopopt_hdr	eh;
	u8			pad[2];	/* 2-octet padding for 4n-alignment */
	struct ioam6_hdr	ioamh;
	struct ioam6_trace_hdr	traceh;
} __packed;

struct ioam6_lwt {
	struct ioam6_lwt_encap	tuninfo;
};

static struct ioam6_lwt *ioam6_lwt_state(struct lwtunnel_state *lwt)
{
	return (struct ioam6_lwt *)lwt->data;
}

static struct ioam6_lwt_encap *ioam6_lwt_info(struct lwtunnel_state *lwt)
{
	return &ioam6_lwt_state(lwt)->tuninfo;
}

static struct ioam6_trace_hdr *ioam6_trace(struct lwtunnel_state *lwt)
{
	return &(ioam6_lwt_state(lwt)->tuninfo.traceh);
}

static const struct nla_policy ioam6_iptunnel_policy[IOAM6_IPTUNNEL_MAX + 1] = {
	[IOAM6_IPTUNNEL_TRACE]	= NLA_POLICY_EXACT_LEN(sizeof(struct ioam6_trace_hdr)),
};

static int nla_put_ioam6_trace(struct sk_buff *skb, int attrtype,
			       struct ioam6_trace_hdr *trace)
{
	struct ioam6_trace_hdr *data;
	struct nlattr *nla;
	int len;

	len = sizeof(*trace);

	nla = nla_reserve(skb, attrtype, len);
	if (!nla)
		return -EMSGSIZE;

	data = nla_data(nla);
	memcpy(data, trace, len);

	return 0;
}

static bool ioam6_validate_trace_hdr(struct ioam6_trace_hdr *trace)
{
	u32 fields;

	if (!trace->type_be32 || !trace->remlen ||
	    trace->remlen > IOAM6_TRACE_DATA_SIZE_MAX / 4)
		return false;

	trace->nodelen = 0;
	fields = be32_to_cpu(trace->type_be32);

	trace->nodelen += hweight32(fields & IOAM6_MASK_SHORT_FIELDS)
				* (sizeof(__be32) / 4);
	trace->nodelen += hweight32(fields & IOAM6_MASK_WIDE_FIELDS)
				* (sizeof(__be64) / 4);

	return true;
}

static int ioam6_build_state(struct net *net, struct nlattr *nla,
			     unsigned int family, const void *cfg,
			     struct lwtunnel_state **ts,
			     struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IOAM6_IPTUNNEL_MAX + 1];
	struct ioam6_lwt_encap *tuninfo;
	struct ioam6_trace_hdr *trace;
	struct lwtunnel_state *s;
	int len_aligned;
	int len, err;

	if (family != AF_INET6)
		return -EINVAL;

	err = nla_parse_nested(tb, IOAM6_IPTUNNEL_MAX, nla,
			       ioam6_iptunnel_policy, extack);
	if (err < 0)
		return err;

	if (!tb[IOAM6_IPTUNNEL_TRACE]) {
		NL_SET_ERR_MSG(extack, "missing trace");
		return -EINVAL;
	}

	trace = nla_data(tb[IOAM6_IPTUNNEL_TRACE]);
	if (!ioam6_validate_trace_hdr(trace)) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IOAM6_IPTUNNEL_TRACE],
				    "invalid trace validation");
		return -EINVAL;
	}

	len = sizeof(*tuninfo) + trace->remlen * 4;
	len_aligned = ALIGN(len, 8);

	s = lwtunnel_state_alloc(len_aligned);
	if (!s)
		return -ENOMEM;

	tuninfo = ioam6_lwt_info(s);
	tuninfo->eh.hdrlen = (len_aligned >> 3) - 1;
	tuninfo->pad[0] = IPV6_TLV_PADN;
	tuninfo->ioamh.type = IOAM6_TYPE_PREALLOC;
	tuninfo->ioamh.opt_type = IPV6_TLV_IOAM;
	tuninfo->ioamh.opt_len = sizeof(tuninfo->ioamh) - 2 + sizeof(*trace)
					+ trace->remlen * 4;

	memcpy(&tuninfo->traceh, trace, sizeof(*trace));

	len = len_aligned - len;
	if (len == 1) {
		tuninfo->traceh.data[trace->remlen * 4] = IPV6_TLV_PAD1;
	} else if (len > 0) {
		tuninfo->traceh.data[trace->remlen * 4] = IPV6_TLV_PADN;
		tuninfo->traceh.data[trace->remlen * 4 + 1] = len - 2;
	}

	s->type = LWTUNNEL_ENCAP_IOAM6;
	s->flags |= LWTUNNEL_STATE_OUTPUT_REDIRECT;

	*ts = s;

	return 0;
}

static int ioam6_do_inline(struct sk_buff *skb, struct ioam6_lwt_encap *tuninfo)
{
	struct ioam6_trace_hdr *trace;
	struct ipv6hdr *oldhdr, *hdr;
	struct ioam6_namespace *ns;
	int hdrlen, err;

	hdrlen = (tuninfo->eh.hdrlen + 1) << 3;

	err = skb_cow_head(skb, hdrlen + skb->mac_len);
	if (unlikely(err))
		return err;

	oldhdr = ipv6_hdr(skb);
	skb_pull(skb, sizeof(*oldhdr));
	skb_postpull_rcsum(skb, skb_network_header(skb), sizeof(*oldhdr));

	skb_push(skb, sizeof(*oldhdr) + hdrlen);
	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);

	hdr = ipv6_hdr(skb);
	memmove(hdr, oldhdr, sizeof(*oldhdr));
	tuninfo->eh.nexthdr = hdr->nexthdr;

	skb_set_transport_header(skb, sizeof(*hdr));
	skb_postpush_rcsum(skb, hdr, sizeof(*hdr) + hdrlen);

	memcpy(skb_transport_header(skb), (u8 *)tuninfo, hdrlen);

	hdr->nexthdr = NEXTHDR_HOP;
	hdr->payload_len = cpu_to_be16(skb->len - sizeof(*hdr));

	trace = (struct ioam6_trace_hdr *)(skb_transport_header(skb)
					   + sizeof(struct ipv6_hopopt_hdr) + 2
					   + sizeof(struct ioam6_hdr));

	ns = ioam6_namespace(dev_net(skb_dst(skb)->dev), trace->namespace_id);
	if (ns)
		ioam6_fill_trace_data(skb, ns, trace);

	return 0;
}

static int ioam6_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct lwtunnel_state *lwt = skb_dst(skb)->lwtstate;
	int err = -EINVAL;

	if (skb->protocol != htons(ETH_P_IPV6))
		goto drop;

	/* Only for packets we send and
	 * that do not contain a Hop-by-Hop yet
	 */
	if (skb->dev || ipv6_hdr(skb)->nexthdr == NEXTHDR_HOP)
		goto out;

	err = ioam6_do_inline(skb, ioam6_lwt_info(lwt));
	if (unlikely(err))
		goto drop;

	err = skb_cow_head(skb, LL_RESERVED_SPACE(skb_dst(skb)->dev));
	if (unlikely(err))
		goto drop;

out:
	return lwt->orig_output(net, sk, skb);

drop:
	kfree_skb(skb);
	return err;
}

static int ioam6_fill_encap_info(struct sk_buff *skb,
				 struct lwtunnel_state *lwtstate)
{
	struct ioam6_trace_hdr *trace = ioam6_trace(lwtstate);

	if (nla_put_ioam6_trace(skb, IOAM6_IPTUNNEL_TRACE, trace))
		return -EMSGSIZE;

	return 0;
}

static int ioam6_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	struct ioam6_trace_hdr *trace = ioam6_trace(lwtstate);

	return nla_total_size(sizeof(*trace));
}

static int ioam6_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct ioam6_trace_hdr *a_hdr = ioam6_trace(a);
	struct ioam6_trace_hdr *b_hdr = ioam6_trace(b);

	return (a_hdr->namespace_id != b_hdr->namespace_id);
}

static const struct lwtunnel_encap_ops ioam6_iptun_ops = {
	.build_state	= ioam6_build_state,
	.output		= ioam6_output,
	.fill_encap	= ioam6_fill_encap_info,
	.get_encap_size	= ioam6_encap_nlsize,
	.cmp_encap	= ioam6_encap_cmp,
	.owner		= THIS_MODULE,
};

int __init ioam6_iptunnel_init(void)
{
	return lwtunnel_encap_add_ops(&ioam6_iptun_ops, LWTUNNEL_ENCAP_IOAM6);
}

void ioam6_iptunnel_exit(void)
{
	lwtunnel_encap_del_ops(&ioam6_iptun_ops, LWTUNNEL_ENCAP_IOAM6);
}
