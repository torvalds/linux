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

struct nft_flow_rule *nft_flow_rule_create(struct net *net,
					   const struct nft_rule *rule)
{
	struct nft_offload_ctx *ctx;
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

	ctx = kzalloc(sizeof(struct nft_offload_ctx), GFP_KERNEL);
	if (!ctx) {
		err = -ENOMEM;
		goto err_out;
	}
	ctx->net = net;
	ctx->dep.type = NFT_OFFLOAD_DEP_UNSPEC;

	while (expr->ops && expr != nft_expr_last(rule)) {
		if (!expr->ops->offload) {
			err = -EOPNOTSUPP;
			goto err_out;
		}
		err = expr->ops->offload(ctx, flow, expr);
		if (err < 0)
			goto err_out;

		expr = nft_expr_next(expr);
	}
	flow->proto = ctx->dep.l3num;
	kfree(ctx);

	return flow;
err_out:
	kfree(ctx);
	nft_flow_rule_destroy(flow);

	return ERR_PTR(err);
}

void nft_flow_rule_destroy(struct nft_flow_rule *flow)
{
	struct flow_action_entry *entry;
	int i;

	flow_action_for_each(i, entry, &flow->rule->action) {
		switch (entry->id) {
		case FLOW_ACTION_REDIRECT:
		case FLOW_ACTION_MIRRED:
			dev_put(entry->dev);
			break;
		default:
			break;
		}
	}
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

static int nft_flow_offload_rule(struct nft_chain *chain,
				 struct nft_rule *rule,
				 struct nft_flow_rule *flow,
				 enum flow_cls_command command)
{
	struct flow_cls_offload cls_flow = {};
	struct nft_base_chain *basechain;
	struct netlink_ext_ack extack;
	__be16 proto = ETH_P_ALL;

	if (!nft_is_base_chain(chain))
		return -EOPNOTSUPP;

	basechain = nft_base_chain(chain);

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

static int nft_block_setup(struct nft_base_chain *basechain,
			   struct flow_block_offload *bo,
			   enum flow_block_command cmd)
{
	int err;

	switch (cmd) {
	case FLOW_BLOCK_BIND:
		err = nft_flow_offload_bind(bo, basechain);
		break;
	case FLOW_BLOCK_UNBIND:
		err = nft_flow_offload_unbind(bo, basechain);
		break;
	default:
		WARN_ON_ONCE(1);
		err = -EOPNOTSUPP;
	}

	return err;
}

static int nft_block_offload_cmd(struct nft_base_chain *chain,
				 struct net_device *dev,
				 enum flow_block_command cmd)
{
	struct netlink_ext_ack extack = {};
	struct flow_block_offload bo = {};
	int err;

	bo.net = dev_net(dev);
	bo.block = &chain->flow_block;
	bo.command = cmd;
	bo.binder_type = FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS;
	bo.extack = &extack;
	INIT_LIST_HEAD(&bo.cb_list);

	err = dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_BLOCK, &bo);
	if (err < 0)
		return err;

	return nft_block_setup(chain, &bo, cmd);
}

static void nft_indr_block_ing_cmd(struct net_device *dev,
				   struct nft_base_chain *chain,
				   flow_indr_block_bind_cb_t *cb,
				   void *cb_priv,
				   enum flow_block_command cmd)
{
	struct netlink_ext_ack extack = {};
	struct flow_block_offload bo = {};

	if (!chain)
		return;

	bo.net = dev_net(dev);
	bo.block = &chain->flow_block;
	bo.command = cmd;
	bo.binder_type = FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS;
	bo.extack = &extack;
	INIT_LIST_HEAD(&bo.cb_list);

	cb(dev, cb_priv, TC_SETUP_BLOCK, &bo);

	nft_block_setup(chain, &bo, cmd);
}

static int nft_indr_block_offload_cmd(struct nft_base_chain *chain,
				      struct net_device *dev,
				      enum flow_block_command cmd)
{
	struct flow_block_offload bo = {};
	struct netlink_ext_ack extack = {};

	bo.net = dev_net(dev);
	bo.block = &chain->flow_block;
	bo.command = cmd;
	bo.binder_type = FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS;
	bo.extack = &extack;
	INIT_LIST_HEAD(&bo.cb_list);

	flow_indr_block_call(dev, &bo, cmd);

	if (list_empty(&bo.cb_list))
		return -EOPNOTSUPP;

	return nft_block_setup(chain, &bo, cmd);
}

#define FLOW_SETUP_BLOCK TC_SETUP_BLOCK

static int nft_flow_offload_chain(struct nft_chain *chain,
				  u8 *ppolicy,
				  enum flow_block_command cmd)
{
	struct nft_base_chain *basechain;
	struct net_device *dev;
	u8 policy;

	if (!nft_is_base_chain(chain))
		return -EOPNOTSUPP;

	basechain = nft_base_chain(chain);
	dev = basechain->ops.dev;
	if (!dev)
		return -EOPNOTSUPP;

	policy = ppolicy ? *ppolicy : basechain->policy;

	/* Only default policy to accept is supported for now. */
	if (cmd == FLOW_BLOCK_BIND && policy == NF_DROP)
		return -EOPNOTSUPP;

	if (dev->netdev_ops->ndo_setup_tc)
		return nft_block_offload_cmd(basechain, dev, cmd);
	else
		return nft_indr_block_offload_cmd(basechain, dev, cmd);
}

int nft_flow_rule_offload_commit(struct net *net)
{
	struct nft_trans *trans;
	int err = 0;
	u8 policy;

	list_for_each_entry(trans, &net->nft.commit_list, list) {
		if (trans->ctx.family != NFPROTO_NETDEV)
			continue;

		switch (trans->msg_type) {
		case NFT_MSG_NEWCHAIN:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			policy = nft_trans_chain_policy(trans);
			err = nft_flow_offload_chain(trans->ctx.chain, &policy,
						     FLOW_BLOCK_BIND);
			break;
		case NFT_MSG_DELCHAIN:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			policy = nft_trans_chain_policy(trans);
			err = nft_flow_offload_chain(trans->ctx.chain, &policy,
						     FLOW_BLOCK_UNBIND);
			break;
		case NFT_MSG_NEWRULE:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			if (trans->ctx.flags & NLM_F_REPLACE ||
			    !(trans->ctx.flags & NLM_F_APPEND))
				return -EOPNOTSUPP;

			err = nft_flow_offload_rule(trans->ctx.chain,
						    nft_trans_rule(trans),
						    nft_trans_flow_rule(trans),
						    FLOW_CLS_REPLACE);
			nft_flow_rule_destroy(nft_trans_flow_rule(trans));
			break;
		case NFT_MSG_DELRULE:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			err = nft_flow_offload_rule(trans->ctx.chain,
						    nft_trans_rule(trans),
						    nft_trans_flow_rule(trans),
						    FLOW_CLS_DESTROY);
			break;
		}

		if (err)
			return err;
	}

	return err;
}

static struct nft_chain *__nft_offload_get_chain(struct net_device *dev)
{
	struct nft_base_chain *basechain;
	struct net *net = dev_net(dev);
	const struct nft_table *table;
	struct nft_chain *chain;

	list_for_each_entry(table, &net->nft.tables, list) {
		if (table->family != NFPROTO_NETDEV)
			continue;

		list_for_each_entry(chain, &table->chains, list) {
			if (!nft_is_base_chain(chain) ||
			    !(chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			basechain = nft_base_chain(chain);
			if (strncmp(basechain->dev_name, dev->name, IFNAMSIZ))
				continue;

			return chain;
		}
	}

	return NULL;
}

static void nft_indr_block_cb(struct net_device *dev,
			      flow_indr_block_bind_cb_t *cb, void *cb_priv,
			      enum flow_block_command cmd)
{
	struct net *net = dev_net(dev);
	struct nft_chain *chain;

	mutex_lock(&net->nft.commit_mutex);
	chain = __nft_offload_get_chain(dev);
	if (chain) {
		struct nft_base_chain *basechain;

		basechain = nft_base_chain(chain);
		nft_indr_block_ing_cmd(dev, basechain, cb, cb_priv, cmd);
	}
	mutex_unlock(&net->nft.commit_mutex);
}

static void nft_offload_chain_clean(struct nft_chain *chain)
{
	struct nft_rule *rule;

	list_for_each_entry(rule, &chain->rules, list) {
		nft_flow_offload_rule(chain, rule,
				      NULL, FLOW_CLS_DESTROY);
	}

	nft_flow_offload_chain(chain, NULL, FLOW_BLOCK_UNBIND);
}

static int nft_offload_netdev_event(struct notifier_block *this,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);
	struct nft_chain *chain;

	mutex_lock(&net->nft.commit_mutex);
	chain = __nft_offload_get_chain(dev);
	if (chain)
		nft_offload_chain_clean(chain);
	mutex_unlock(&net->nft.commit_mutex);

	return NOTIFY_DONE;
}

static struct flow_indr_block_ing_entry block_ing_entry = {
	.cb	= nft_indr_block_cb,
	.list	= LIST_HEAD_INIT(block_ing_entry.list),
};

static struct notifier_block nft_offload_netdev_notifier = {
	.notifier_call	= nft_offload_netdev_event,
};

int nft_offload_init(void)
{
	int err;

	err = register_netdevice_notifier(&nft_offload_netdev_notifier);
	if (err < 0)
		return err;

	flow_indr_add_block_ing_cb(&block_ing_entry);

	return 0;
}

void nft_offload_exit(void)
{
	flow_indr_del_block_ing_cb(&block_ing_entry);
	unregister_netdevice_notifier(&nft_offload_netdev_notifier);
}
