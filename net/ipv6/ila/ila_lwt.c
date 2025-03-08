// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/types.h>
#include <net/checksum.h>
#include <net/dst_cache.h>
#include <net/ip.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/lwtunnel.h>
#include <net/protocol.h>
#include <uapi/linux/ila.h>
#include "ila.h"

struct ila_lwt {
	struct ila_params p;
	struct dst_cache dst_cache;
	u32 connected : 1;
	u32 lwt_output : 1;
};

static inline struct ila_lwt *ila_lwt_lwtunnel(
	struct lwtunnel_state *lwt)
{
	return (struct ila_lwt *)lwt->data;
}

static inline struct ila_params *ila_params_lwtunnel(
	struct lwtunnel_state *lwt)
{
	return &ila_lwt_lwtunnel(lwt)->p;
}

static int ila_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *orig_dst = skb_dst(skb);
	struct rt6_info *rt = dst_rt6_info(orig_dst);
	struct ila_lwt *ilwt = ila_lwt_lwtunnel(orig_dst->lwtstate);
	struct dst_entry *dst;
	int err = -EINVAL;

	if (skb->protocol != htons(ETH_P_IPV6))
		goto drop;

	if (ilwt->lwt_output)
		ila_update_ipv6_locator(skb,
					ila_params_lwtunnel(orig_dst->lwtstate),
					true);

	if (rt->rt6i_flags & (RTF_GATEWAY | RTF_CACHE)) {
		/* Already have a next hop address in route, no need for
		 * dest cache route.
		 */
		return orig_dst->lwtstate->orig_output(net, sk, skb);
	}

	local_bh_disable();
	dst = dst_cache_get(&ilwt->dst_cache);
	local_bh_enable();
	if (unlikely(!dst)) {
		struct ipv6hdr *ip6h = ipv6_hdr(skb);
		struct flowi6 fl6;

		/* Lookup a route for the new destination. Take into
		 * account that the base route may already have a gateway.
		 */

		memset(&fl6, 0, sizeof(fl6));
		fl6.flowi6_oif = orig_dst->dev->ifindex;
		fl6.flowi6_iif = LOOPBACK_IFINDEX;
		fl6.daddr = *rt6_nexthop(dst_rt6_info(orig_dst),
					 &ip6h->daddr);

		dst = ip6_route_output(net, NULL, &fl6);
		if (dst->error) {
			err = -EHOSTUNREACH;
			dst_release(dst);
			goto drop;
		}

		dst = xfrm_lookup(net, dst, flowi6_to_flowi(&fl6), NULL, 0);
		if (IS_ERR(dst)) {
			err = PTR_ERR(dst);
			goto drop;
		}

		/* cache only if we don't create a dst reference loop */
		if (ilwt->connected && orig_dst->lwtstate != dst->lwtstate) {
			local_bh_disable();
			dst_cache_set_ip6(&ilwt->dst_cache, dst, &fl6.saddr);
			local_bh_enable();
		}
	}

	skb_dst_drop(skb);
	skb_dst_set(skb, dst);
	return dst_output(net, sk, skb);

drop:
	kfree_skb(skb);
	return err;
}

static int ila_input(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct ila_lwt *ilwt = ila_lwt_lwtunnel(dst->lwtstate);

	if (skb->protocol != htons(ETH_P_IPV6))
		goto drop;

	if (!ilwt->lwt_output)
		ila_update_ipv6_locator(skb,
					ila_params_lwtunnel(dst->lwtstate),
					false);

	return dst->lwtstate->orig_input(skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

static const struct nla_policy ila_nl_policy[ILA_ATTR_MAX + 1] = {
	[ILA_ATTR_LOCATOR] = { .type = NLA_U64, },
	[ILA_ATTR_CSUM_MODE] = { .type = NLA_U8, },
	[ILA_ATTR_IDENT_TYPE] = { .type = NLA_U8, },
	[ILA_ATTR_HOOK_TYPE] = { .type = NLA_U8, },
};

static int ila_build_state(struct net *net, struct nlattr *nla,
			   unsigned int family, const void *cfg,
			   struct lwtunnel_state **ts,
			   struct netlink_ext_ack *extack)
{
	struct ila_lwt *ilwt;
	struct ila_params *p;
	struct nlattr *tb[ILA_ATTR_MAX + 1];
	struct lwtunnel_state *newts;
	const struct fib6_config *cfg6 = cfg;
	struct ila_addr *iaddr;
	u8 ident_type = ILA_ATYPE_USE_FORMAT;
	u8 hook_type = ILA_HOOK_ROUTE_OUTPUT;
	u8 csum_mode = ILA_CSUM_NO_ACTION;
	bool lwt_output = true;
	u8 eff_ident_type;
	int ret;

	if (family != AF_INET6)
		return -EINVAL;

	ret = nla_parse_nested_deprecated(tb, ILA_ATTR_MAX, nla,
					  ila_nl_policy, extack);
	if (ret < 0)
		return ret;

	if (!tb[ILA_ATTR_LOCATOR])
		return -EINVAL;

	iaddr = (struct ila_addr *)&cfg6->fc_dst;

	if (tb[ILA_ATTR_IDENT_TYPE])
		ident_type = nla_get_u8(tb[ILA_ATTR_IDENT_TYPE]);

	if (ident_type == ILA_ATYPE_USE_FORMAT) {
		/* Infer identifier type from type field in formatted
		 * identifier.
		 */

		if (cfg6->fc_dst_len < 8 * sizeof(struct ila_locator) + 3) {
			/* Need to have full locator and at least type field
			 * included in destination
			 */
			return -EINVAL;
		}

		eff_ident_type = iaddr->ident.type;
	} else {
		eff_ident_type = ident_type;
	}

	switch (eff_ident_type) {
	case ILA_ATYPE_IID:
		/* Don't allow ILA for IID type */
		return -EINVAL;
	case ILA_ATYPE_LUID:
		break;
	case ILA_ATYPE_VIRT_V4:
	case ILA_ATYPE_VIRT_UNI_V6:
	case ILA_ATYPE_VIRT_MULTI_V6:
	case ILA_ATYPE_NONLOCAL_ADDR:
		/* These ILA formats are not supported yet. */
	default:
		return -EINVAL;
	}

	if (tb[ILA_ATTR_HOOK_TYPE])
		hook_type = nla_get_u8(tb[ILA_ATTR_HOOK_TYPE]);

	switch (hook_type) {
	case ILA_HOOK_ROUTE_OUTPUT:
		lwt_output = true;
		break;
	case ILA_HOOK_ROUTE_INPUT:
		lwt_output = false;
		break;
	default:
		return -EINVAL;
	}

	if (tb[ILA_ATTR_CSUM_MODE])
		csum_mode = nla_get_u8(tb[ILA_ATTR_CSUM_MODE]);

	if (csum_mode == ILA_CSUM_NEUTRAL_MAP &&
	    ila_csum_neutral_set(iaddr->ident)) {
		/* Don't allow translation if checksum neutral bit is
		 * configured and it's set in the SIR address.
		 */
		return -EINVAL;
	}

	newts = lwtunnel_state_alloc(sizeof(*ilwt));
	if (!newts)
		return -ENOMEM;

	ilwt = ila_lwt_lwtunnel(newts);
	ret = dst_cache_init(&ilwt->dst_cache, GFP_ATOMIC);
	if (ret) {
		kfree(newts);
		return ret;
	}

	ilwt->lwt_output = !!lwt_output;

	p = ila_params_lwtunnel(newts);

	p->csum_mode = csum_mode;
	p->ident_type = ident_type;
	p->locator.v64 = (__force __be64)nla_get_u64(tb[ILA_ATTR_LOCATOR]);

	/* Precompute checksum difference for translation since we
	 * know both the old locator and the new one.
	 */
	p->locator_match = iaddr->loc;

	ila_init_saved_csum(p);

	newts->type = LWTUNNEL_ENCAP_ILA;
	newts->flags |= LWTUNNEL_STATE_OUTPUT_REDIRECT |
			LWTUNNEL_STATE_INPUT_REDIRECT;

	if (cfg6->fc_dst_len == 8 * sizeof(struct in6_addr))
		ilwt->connected = 1;

	*ts = newts;

	return 0;
}

static void ila_destroy_state(struct lwtunnel_state *lwt)
{
	dst_cache_destroy(&ila_lwt_lwtunnel(lwt)->dst_cache);
}

static int ila_fill_encap_info(struct sk_buff *skb,
			       struct lwtunnel_state *lwtstate)
{
	struct ila_params *p = ila_params_lwtunnel(lwtstate);
	struct ila_lwt *ilwt = ila_lwt_lwtunnel(lwtstate);

	if (nla_put_u64_64bit(skb, ILA_ATTR_LOCATOR, (__force u64)p->locator.v64,
			      ILA_ATTR_PAD))
		goto nla_put_failure;

	if (nla_put_u8(skb, ILA_ATTR_CSUM_MODE, (__force u8)p->csum_mode))
		goto nla_put_failure;

	if (nla_put_u8(skb, ILA_ATTR_IDENT_TYPE, (__force u8)p->ident_type))
		goto nla_put_failure;

	if (nla_put_u8(skb, ILA_ATTR_HOOK_TYPE,
		       ilwt->lwt_output ? ILA_HOOK_ROUTE_OUTPUT :
					  ILA_HOOK_ROUTE_INPUT))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int ila_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	return nla_total_size_64bit(sizeof(u64)) + /* ILA_ATTR_LOCATOR */
	       nla_total_size(sizeof(u8)) +        /* ILA_ATTR_CSUM_MODE */
	       nla_total_size(sizeof(u8)) +        /* ILA_ATTR_IDENT_TYPE */
	       nla_total_size(sizeof(u8)) +        /* ILA_ATTR_HOOK_TYPE */
	       0;
}

static int ila_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct ila_params *a_p = ila_params_lwtunnel(a);
	struct ila_params *b_p = ila_params_lwtunnel(b);

	return (a_p->locator.v64 != b_p->locator.v64);
}

static const struct lwtunnel_encap_ops ila_encap_ops = {
	.build_state = ila_build_state,
	.destroy_state = ila_destroy_state,
	.output = ila_output,
	.input = ila_input,
	.fill_encap = ila_fill_encap_info,
	.get_encap_size = ila_encap_nlsize,
	.cmp_encap = ila_encap_cmp,
	.owner = THIS_MODULE,
};

int ila_lwt_init(void)
{
	return lwtunnel_encap_add_ops(&ila_encap_ops, LWTUNNEL_ENCAP_ILA);
}

void ila_lwt_fini(void)
{
	lwtunnel_encap_del_ops(&ila_encap_ops, LWTUNNEL_ENCAP_ILA);
}
