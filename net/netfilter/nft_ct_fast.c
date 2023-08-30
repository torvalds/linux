// SPDX-License-Identifier: GPL-2.0-only
#if IS_ENABLED(CONFIG_NFT_CT)
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_conntrack.h>

void nft_ct_get_fast_eval(const struct nft_expr *expr,
			  struct nft_regs *regs,
			  const struct nft_pktinfo *pkt)
{
	const struct nft_ct *priv = nft_expr_priv(expr);
	u32 *dest = &regs->data[priv->dreg];
	enum ip_conntrack_info ctinfo;
	const struct nf_conn *ct;
	unsigned int state;

	ct = nf_ct_get(pkt->skb, &ctinfo);

	switch (priv->key) {
	case NFT_CT_STATE:
		if (ct)
			state = NF_CT_STATE_BIT(ctinfo);
		else if (ctinfo == IP_CT_UNTRACKED)
			state = NF_CT_STATE_UNTRACKED_BIT;
		else
			state = NF_CT_STATE_INVALID_BIT;
		*dest = state;
		return;
	default:
		break;
	}

	if (!ct) {
		regs->verdict.code = NFT_BREAK;
		return;
	}

	switch (priv->key) {
	case NFT_CT_DIRECTION:
		nft_reg_store8(dest, CTINFO2DIR(ctinfo));
		return;
	case NFT_CT_STATUS:
		*dest = ct->status;
		return;
#ifdef CONFIG_NF_CONNTRACK_MARK
	case NFT_CT_MARK:
		*dest = ct->mark;
		return;
#endif
#ifdef CONFIG_NF_CONNTRACK_SECMARK
	case NFT_CT_SECMARK:
		*dest = ct->secmark;
		return;
#endif
	default:
		WARN_ON_ONCE(1);
		regs->verdict.code = NFT_BREAK;
		break;
	}
}
EXPORT_SYMBOL_GPL(nft_ct_get_fast_eval);
#endif
