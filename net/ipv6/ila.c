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

struct ila_params {
	__be64 locator;
	__be64 locator_match;
	__wsum csum_diff;
};

static inline struct ila_params *ila_params_lwtunnel(
	struct lwtunnel_state *lwstate)
{
	return (struct ila_params *)lwstate->data;
}

static inline __wsum compute_csum_diff8(const __be32 *from, const __be32 *to)
{
	__be32 diff[] = {
		~from[0], ~from[1], to[0], to[1],
	};

	return csum_partial(diff, sizeof(diff), 0);
}

static inline __wsum get_csum_diff(struct ipv6hdr *ip6h, struct ila_params *p)
{
	if (*(__be64 *)&ip6h->daddr == p->locator_match)
		return p->csum_diff;
	else
		return compute_csum_diff8((__be32 *)&ip6h->daddr,
					  (__be32 *)&p->locator);
}

static void update_ipv6_locator(struct sk_buff *skb, struct ila_params *p)
{
	__wsum diff;
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	size_t nhoff = sizeof(struct ipv6hdr);

	/* First update checksum */
	switch (ip6h->nexthdr) {
	case NEXTHDR_TCP:
		if (likely(pskb_may_pull(skb, nhoff + sizeof(struct tcphdr)))) {
			struct tcphdr *th = (struct tcphdr *)
					(skb_network_header(skb) + nhoff);

			diff = get_csum_diff(ip6h, p);
			inet_proto_csum_replace_by_diff(&th->check, skb,
							diff, true);
		}
		break;
	case NEXTHDR_UDP:
		if (likely(pskb_may_pull(skb, nhoff + sizeof(struct udphdr)))) {
			struct udphdr *uh = (struct udphdr *)
					(skb_network_header(skb) + nhoff);

			if (uh->check || skb->ip_summed == CHECKSUM_PARTIAL) {
				diff = get_csum_diff(ip6h, p);
				inet_proto_csum_replace_by_diff(&uh->check, skb,
								diff, true);
				if (!uh->check)
					uh->check = CSUM_MANGLED_0;
			}
		}
		break;
	case NEXTHDR_ICMP:
		if (likely(pskb_may_pull(skb,
					 nhoff + sizeof(struct icmp6hdr)))) {
			struct icmp6hdr *ih = (struct icmp6hdr *)
					(skb_network_header(skb) + nhoff);

			diff = get_csum_diff(ip6h, p);
			inet_proto_csum_replace_by_diff(&ih->icmp6_cksum, skb,
							diff, true);
		}
		break;
	}

	/* Now change destination address */
	*(__be64 *)&ip6h->daddr = p->locator;
}

static int ila_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);

	if (skb->protocol != htons(ETH_P_IPV6))
		goto drop;

	update_ipv6_locator(skb, ila_params_lwtunnel(dst->lwtstate));

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

	update_ipv6_locator(skb, ila_params_lwtunnel(dst->lwtstate));

	return dst->lwtstate->orig_input(skb);

drop:
	kfree_skb(skb);
	return -EINVAL;
}

static struct nla_policy ila_nl_policy[ILA_ATTR_MAX + 1] = {
	[ILA_ATTR_LOCATOR] = { .type = NLA_U64, },
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
	int ret;

	if (family != AF_INET6)
		return -EINVAL;

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

	p->locator = (__force __be64)nla_get_u64(tb[ILA_ATTR_LOCATOR]);

	if (cfg6->fc_dst_len > sizeof(__be64)) {
		/* Precompute checksum difference for translation since we
		 * know both the old locator and the new one.
		 */
		p->locator_match = *(__be64 *)&cfg6->fc_dst;
		p->csum_diff = compute_csum_diff8(
			(__be32 *)&p->locator_match, (__be32 *)&p->locator);
	}

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

	if (nla_put_u64(skb, ILA_ATTR_LOCATOR, (__force u64)p->locator))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int ila_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	/* No encapsulation overhead */
	return 0;
}

static int ila_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct ila_params *a_p = ila_params_lwtunnel(a);
	struct ila_params *b_p = ila_params_lwtunnel(b);

	return (a_p->locator != b_p->locator);
}

static const struct lwtunnel_encap_ops ila_encap_ops = {
	.build_state = ila_build_state,
	.output = ila_output,
	.input = ila_input,
	.fill_encap = ila_fill_encap_info,
	.get_encap_size = ila_encap_nlsize,
	.cmp_encap = ila_encap_cmp,
};

static int __init ila_init(void)
{
	return lwtunnel_encap_add_ops(&ila_encap_ops, LWTUNNEL_ENCAP_ILA);
}

static void __exit ila_fini(void)
{
	lwtunnel_encap_del_ops(&ila_encap_ops, LWTUNNEL_ENCAP_ILA);
}

module_init(ila_init);
module_exit(ila_fini);
MODULE_AUTHOR("Tom Herbert <tom@herbertland.com>");
MODULE_LICENSE("GPL");
