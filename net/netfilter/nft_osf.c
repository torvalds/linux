#include <net/ip.h>
#include <net/tcp.h>

#include <net/netfilter/nf_tables.h>
#include <linux/netfilter/nfnetlink_osf.h>

struct nft_osf {
	enum nft_registers	dreg:8;
};

static const struct nla_policy nft_osf_policy[NFTA_OSF_MAX + 1] = {
	[NFTA_OSF_DREG]		= { .type = NLA_U32 },
};

static void nft_osf_eval(const struct nft_expr *expr, struct nft_regs *regs,
			 const struct nft_pktinfo *pkt)
{
	struct nft_osf *priv = nft_expr_priv(expr);
	u32 *dest = &regs->data[priv->dreg];
	struct sk_buff *skb = pkt->skb;
	const struct tcphdr *tcp;
	struct tcphdr _tcph;
	const char *os_name;

	tcp = skb_header_pointer(skb, ip_hdrlen(skb),
				 sizeof(struct tcphdr), &_tcph);
	if (!tcp) {
		regs->verdict.code = NFT_BREAK;
		return;
	}
	if (!tcp->syn) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	os_name = nf_osf_find(skb, nf_osf_fingers);
	if (!os_name)
		strncpy((char *)dest, "unknown", NFT_OSF_MAXGENRELEN);
	else
		strncpy((char *)dest, os_name, NFT_OSF_MAXGENRELEN);
}

static int nft_osf_init(const struct nft_ctx *ctx,
			const struct nft_expr *expr,
			const struct nlattr * const tb[])
{
	struct nft_osf *priv = nft_expr_priv(expr);
	int err;

	priv->dreg = nft_parse_register(tb[NFTA_OSF_DREG]);
	err = nft_validate_register_store(ctx, priv->dreg, NULL,
					  NFT_DATA_VALUE, NFT_OSF_MAXGENRELEN);
	if (err < 0)
		return err;

	return 0;
}

static int nft_osf_dump(struct sk_buff *skb, const struct nft_expr *expr)
{
	const struct nft_osf *priv = nft_expr_priv(expr);

	if (nft_dump_register(skb, NFTA_OSF_DREG, priv->dreg))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -1;
}

static struct nft_expr_type nft_osf_type;
static const struct nft_expr_ops nft_osf_op = {
	.eval		= nft_osf_eval,
	.size		= NFT_EXPR_SIZE(sizeof(struct nft_osf)),
	.init		= nft_osf_init,
	.dump		= nft_osf_dump,
	.type		= &nft_osf_type,
};

static struct nft_expr_type nft_osf_type __read_mostly = {
	.ops		= &nft_osf_op,
	.name		= "osf",
	.owner		= THIS_MODULE,
	.policy		= nft_osf_policy,
	.maxattr	= NFTA_OSF_MAX,
};

static int __init nft_osf_module_init(void)
{
	return nft_register_expr(&nft_osf_type);
}

static void __exit nft_osf_module_exit(void)
{
	return nft_unregister_expr(&nft_osf_type);
}

module_init(nft_osf_module_init);
module_exit(nft_osf_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fernando Fernandez <ffmancera@riseup.net>");
MODULE_ALIAS_NFT_EXPR("osf");
