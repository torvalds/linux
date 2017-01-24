#include <linux/errno.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/types.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <net/ip6_fib.h>
#include <net/lwtunnel.h>
#include <net/protocol.h>
#include <uapi/linux/ila.h>
#include "ila.h"

static inline struct ila_params *ila_params_lwtunnel(
	struct lwtunnel_state *lwstate)
{
	return (struct ila_params *)lwstate->data;
}

static int ila_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);

	if (skb->protocol != htons(ETH_P_IPV6))
		goto drop;

	ila_update_ipv6_locator(skb, ila_params_lwtunnel(dst->lwtstate), true);

	return dst->lwtstate->orig_output(net, sk, skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

static int ila_input(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);

	if (skb->protocol != htons(ETH_P_IPV6))
		goto drop;

	ila_update_ipv6_locator(skb, ila_params_lwtunnel(dst->lwtstate), false);

	return dst->lwtstate->orig_input(skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

static const struct nla_policy ila_nl_policy[ILA_ATTR_MAX + 1] = {
	[ILA_ATTR_LOCATOR] = { .type = NLA_U64, },
	[ILA_ATTR_CSUM_MODE] = { .type = NLA_U8, },
};

static int ila_build_state(struct net_device *dev, struct nlattr *nla,
			   unsigned int family, const void *cfg,
			   struct lwtunnel_state **ts)
{
	struct ila_params *p;
	struct nlattr *tb[ILA_ATTR_MAX + 1];
	size_t encap_len = sizeof(*p);
	struct lwtunnel_state *newts;
	const struct fib6_config *cfg6 = cfg;
	struct ila_addr *iaddr;
	int ret;

	if (family != AF_INET6)
		return -EINVAL;

	if (cfg6->fc_dst_len < sizeof(struct ila_locator) + 1) {
		/* Need to have full locator and at least type field
		 * included in destination
		 */
		return -EINVAL;
	}

	iaddr = (struct ila_addr *)&cfg6->fc_dst;

	if (!ila_addr_is_ila(iaddr) || ila_csum_neutral_set(iaddr->ident)) {
		/* Don't allow translation for a non-ILA address or checksum
		 * neutral flag to be set.
		 */
		return -EINVAL;
	}

	ret = nla_parse_nested(tb, ILA_ATTR_MAX, nla,
			       ila_nl_policy);
	if (ret < 0)
		return ret;

	if (!tb[ILA_ATTR_LOCATOR])
		return -EINVAL;

	newts = lwtunnel_state_alloc(encap_len);
	if (!newts)
		return -ENOMEM;

	newts->len = encap_len;
	p = ila_params_lwtunnel(newts);

	p->locator.v64 = (__force __be64)nla_get_u64(tb[ILA_ATTR_LOCATOR]);

	/* Precompute checksum difference for translation since we
	 * know both the old locator and the new one.
	 */
	p->locator_match = iaddr->loc;
	p->csum_diff = compute_csum_diff8(
		(__be32 *)&p->locator_match, (__be32 *)&p->locator);

	if (tb[ILA_ATTR_CSUM_MODE])
		p->csum_mode = nla_get_u8(tb[ILA_ATTR_CSUM_MODE]);

	ila_init_saved_csum(p);

	newts->type = LWTUNNEL_ENCAP_ILA;
	newts->flags |= LWTUNNEL_STATE_OUTPUT_REDIRECT |
			LWTUNNEL_STATE_INPUT_REDIRECT;

	*ts = newts;

	return 0;
}

static int ila_fill_encap_info(struct sk_buff *skb,
			       struct lwtunnel_state *lwtstate)
{
	struct ila_params *p = ila_params_lwtunnel(lwtstate);

	if (nla_put_u64_64bit(skb, ILA_ATTR_LOCATOR, (__force u64)p->locator.v64,
			      ILA_ATTR_PAD))
		goto nla_put_failure;
	if (nla_put_u8(skb, ILA_ATTR_CSUM_MODE, (__force u8)p->csum_mode))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int ila_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	return nla_total_size_64bit(sizeof(u64)) + /* ILA_ATTR_LOCATOR */
	       nla_total_size(sizeof(u8)) +        /* ILA_ATTR_CSUM_MODE */
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
