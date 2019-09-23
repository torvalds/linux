/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <net/flow_offload.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_offload.h>
#include <net/pkt_cls.h>

static struct nft_flow_rule *nft_flow_rule_alloc(int num_actions)
{
	struct nft_flow_rule *flow;

	flow = kzalloc(sizeof(struct nft_flow_rule), GFP_KERNEL);
	if (!flow)
		return NULL;

	flow->rule = flow_rule_alloc(num_actions);
	if (!flow->rule) {
		kfree(flow);
		return NULL;
	}

	flow->rule->match.dissector	= &flow->match.dissector;
	flow->rule->match.mask		= &flow->match.mask;
	flow->rule->match.key		= &flow->match.key;

	return flow;
}

struct nft_flow_rule *nft_flow_rule_create(const struct nft_rule *rule)
{
	struct nft_offload_ctx ctx = {
		.dep	= {
			.type	= NFT_OFFLOAD_DEP_UNSPEC,
		},
	};
	struct nft_flow_rule *flow;
	int num_actions = 0, err;
	struct nft_expr *expr;

	expr = nft_expr_first(rule);
	while (expr->ops && expr != nft_expr_last(rule)) {
		if (expr->ops->offload_flags & NFT_OFFLOAD_F_ACTION)
			num_actions++;

		expr = nft_expr_next(expr);
	}

	flow = nft_flow_rule_alloc(num_actions);
	if (!flow)
		return ERR_PTR(-ENOMEM);

	expr = nft_expr_first(rule);
	while (expr->ops && expr != nft_expr_last(rule)) {
		if (!expr->ops->offload) {
			err = -EOPNOTSUPP;
			goto err_out;
		}
		err = expr->ops->offload(&ctx, flow, expr);
		if (err < 0)
			goto err_out;

		expr = nft_expr_next(expr);
	}
	flow->proto = ctx.dep.l3num;

	return flow;
err_out:
	nft_flow_rule_destroy(flow);

	return ERR_PTR(err);
}

void nft_flow_rule_destroy(struct nft_flow_rule *flow)
{
	kfree(flow->rule);
	kfree(flow);
}

void nft_offload_set_dependency(struct nft_offload_ctx *ctx,
				enum nft_offload_dep_type type)
{
	ctx->dep.type = type;
}

void nft_offload_update_dependency(struct nft_offload_ctx *ctx,
				   const void *data, u32 len)
{
	switch (ctx->dep.type) {
	case NFT_OFFLOAD_DEP_NETWORK:
		WARN_ON(len != sizeof(__u16));
		memcpy(&ctx->dep.l3num, data, sizeof(__u16));
		break;
	case NFT_OFFLOAD_DEP_TRANSPORT:
		WARN_ON(len != sizeof(__u8));
		memcpy(&ctx->dep.protonum, data, sizeof(__u8));
		break;
	default:
		break;
	}
	ctx->dep.type = NFT_OFFLOAD_DEP_UNSPEC;
}

static void nft_flow_offload_common_init(struct flow_cls_common_offload *common,
					 __be16 proto, int priority,
					 struct netlink_ext_ack *extack)
{
	common->protocol = proto;
	common->prio = priority;
	common->extack = extack;
}

static int nft_setup_cb_call(struct nft_base_chain *basechain,
			     enum tc_setup_type type, void *type_data)
{
	struct flow_block_cb *block_cb;
	int err;

	list_for_each_entry(block_cb, &basechain->flow_block.cb_list, list) {
		err = block_cb->cb(type, type_data, block_cb->cb_priv);
		if (err < 0)
			return err;
	}
	return 0;
}

int nft_chain_offload_priority(struct nft_base_chain *basechain)
{
	if (basechain->ops.priority <= 0 ||
	    basechain->ops.priority > USHRT_MAX)
		return -1;

	return 0;
}

static int nft_flow_offload_rule(struct nft_trans *trans,
				 enum flow_cls_command command)
{
	struct nft_flow_rule *flow = nft_trans_flow_rule(trans);
	struct nft_rule *rule = nft_trans_rule(trans);
	struct flow_cls_offload cls_flow = {};
	struct nft_base_chain *basechain;
	struct netlink_ext_ack extack;
	__be16 proto = ETH_P_ALL;

	if (!nft_is_base_chain(trans->ctx.chain))
		return -EOPNOTSUPP;

	basechain = nft_base_chain(trans->ctx.chain);

	if (flow)
		proto = flow->proto;

	nft_flow_offload_common_init(&cls_flow.common, proto,
				     basechain->ops.priority, &extack);
	cls_flow.command = command;
	cls_flow.cookie = (unsigned long) rule;
	if (flow)
		cls_flow.rule = flow->rule;

	return nft_setup_cb_call(basechain, TC_SETUP_CLSFLOWER, &cls_flow);
}

static int nft_flow_offload_bind(struct flow_block_offload *bo,
				 struct nft_base_chain *basechain)
{
	list_splice(&bo->cb_list, &basechain->flow_block.cb_list);
	return 0;
}

static int nft_flow_offload_unbind(struct flow_block_offload *bo,
				   struct nft_base_chain *basechain)
{
	struct flow_block_cb *block_cb, *next;

	list_for_each_entry_safe(block_cb, next, &bo->cb_list, list) {
		list_del(&block_cb->list);
		flow_block_cb_free(block_cb);
	}

	return 0;
}

#define FLOW_SETUP_BLOCK TC_SETUP_BLOCK

static int nft_flow_offload_chain(struct nft_trans *trans,
				  enum flow_block_command cmd)
{
	struct nft_chain *chain = trans->ctx.chain;
	struct netlink_ext_ack extack = {};
	struct flow_block_offload bo = {};
	struct nft_base_chain *basechain;
	struct net_device *dev;
	int err;

	if (!nft_is_base_chain(chain))
		return -EOPNOTSUPP;

	basechain = nft_base_chain(chain);
	dev = basechain->ops.dev;
	if (!dev || !dev->netdev_ops->ndo_setup_tc)
		return -EOPNOTSUPP;

	/* Only default policy to accept is supported for now. */
	if (cmd == FLOW_BLOCK_BIND &&
	    nft_trans_chain_policy(trans) != -1 &&
	    nft_trans_chain_policy(trans) != NF_ACCEPT)
		return -EOPNOTSUPP;

	bo.command = cmd;
	bo.block = &basechain->flow_block;
	bo.binder_type = FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS;
	bo.extack = &extack;
	INIT_LIST_HEAD(&bo.cb_list);

	err = dev->netdev_ops->ndo_setup_tc(dev, FLOW_SETUP_BLOCK, &bo);
	if (err < 0)
		return err;

	switch (cmd) {
	case FLOW_BLOCK_BIND:
		err = nft_flow_offload_bind(&bo, basechain);
		break;
	case FLOW_BLOCK_UNBIND:
		err = nft_flow_offload_unbind(&bo, basechain);
		break;
	}

	return err;
}

int nft_flow_rule_offload_commit(struct net *net)
{
	struct nft_trans *trans;
	int err = 0;

	list_for_each_entry(trans, &net->nft.commit_list, list) {
		if (trans->ctx.family != NFPROTO_NETDEV)
			continue;

		switch (trans->msg_type) {
		case NFT_MSG_NEWCHAIN:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			err = nft_flow_offload_chain(trans, FLOW_BLOCK_BIND);
			break;
		case NFT_MSG_DELCHAIN:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			err = nft_flow_offload_chain(trans, FLOW_BLOCK_UNBIND);
			break;
		case NFT_MSG_NEWRULE:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			if (trans->ctx.flags & NLM_F_REPLACE ||
			    !(trans->ctx.flags & NLM_F_APPEND))
				return -EOPNOTSUPP;

			err = nft_flow_offload_rule(trans, FLOW_CLS_REPLACE);
			nft_flow_rule_destroy(nft_trans_flow_rule(trans));
			break;
		case NFT_MSG_DELRULE:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			err = nft_flow_offload_rule(trans, FLOW_CLS_DESTROY);
			break;
		}

		if (err)
			return err;
	}

	return err;
}
