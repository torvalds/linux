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

	if (num_actions == 0)
		return ERR_PTR(-EOPNOTSUPP);

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

static int nft_setup_cb_call(enum tc_setup_type type, void *type_data,
			     struct list_head *cb_list)
{
	struct flow_block_cb *block_cb;
	int err;

	list_for_each_entry(block_cb, cb_list, list) {
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

static void nft_flow_cls_offload_setup(struct flow_cls_offload *cls_flow,
				       const struct nft_base_chain *basechain,
				       const struct nft_rule *rule,
				       const struct nft_flow_rule *flow,
				       struct netlink_ext_ack *extack,
				       enum flow_cls_command command)
{
	__be16 proto = ETH_P_ALL;

	memset(cls_flow, 0, sizeof(*cls_flow));

	if (flow)
		proto = flow->proto;

	nft_flow_offload_common_init(&cls_flow->common, proto,
				     basechain->ops.priority, extack);
	cls_flow->command = command;
	cls_flow->cookie = (unsigned long) rule;
	if (flow)
		cls_flow->rule = flow->rule;
}

static int nft_flow_offload_rule(struct nft_chain *chain,
				 struct nft_rule *rule,
				 struct nft_flow_rule *flow,
				 enum flow_cls_command command)
{
	struct netlink_ext_ack extack = {};
	struct flow_cls_offload cls_flow;
	struct nft_base_chain *basechain;

	if (!nft_is_base_chain(chain))
		return -EOPNOTSUPP;

	basechain = nft_base_chain(chain);
	nft_flow_cls_offload_setup(&cls_flow, basechain, rule, flow, &extack,
				   command);

	return nft_setup_cb_call(TC_SETUP_CLSFLOWER, &cls_flow,
				 &basechain->flow_block.cb_list);
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
	struct flow_cls_offload cls_flow;
	struct netlink_ext_ack extack;
	struct nft_chain *chain;
	struct nft_rule *rule;

	chain = &basechain->chain;
	list_for_each_entry(rule, &chain->rules, list) {
		memset(&extack, 0, sizeof(extack));
		nft_flow_cls_offload_setup(&cls_flow, basechain, rule, NULL,
					   &extack, FLOW_CLS_DESTROY);
		nft_setup_cb_call(TC_SETUP_CLSFLOWER, &cls_flow, &bo->cb_list);
	}

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

static void nft_flow_block_offload_init(struct flow_block_offload *bo,
					struct net *net,
					enum flow_block_command cmd,
					struct nft_base_chain *basechain,
					struct netlink_ext_ack *extack)
{
	memset(bo, 0, sizeof(*bo));
	bo->net		= net;
	bo->block	= &basechain->flow_block;
	bo->command	= cmd;
	bo->binder_type	= FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS;
	bo->extack	= extack;
	INIT_LIST_HEAD(&bo->cb_list);
}

static int nft_block_offload_cmd(struct nft_base_chain *chain,
				 struct net_device *dev,
				 enum flow_block_command cmd)
{
	struct netlink_ext_ack extack = {};
	struct flow_block_offload bo;
	int err;

	nft_flow_block_offload_init(&bo, dev_net(dev), cmd, chain, &extack);

	err = dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_BLOCK, &bo);
	if (err < 0)
		return err;

	return nft_block_setup(chain, &bo, cmd);
}

static void nft_indr_block_cleanup(struct flow_block_cb *block_cb)
{
	struct nft_base_chain *basechain = block_cb->indr.data;
	struct net_device *dev = block_cb->indr.dev;
	struct netlink_ext_ack extack = {};
	struct net *net = dev_net(dev);
	struct flow_block_offload bo;

	nft_flow_block_offload_init(&bo, dev_net(dev), FLOW_BLOCK_UNBIND,
				    basechain, &extack);
	mutex_lock(&net->nft.commit_mutex);
	list_move(&block_cb->list, &bo.cb_list);
	nft_flow_offload_unbind(&bo, basechain);
	mutex_unlock(&net->nft.commit_mutex);
}

static int nft_indr_block_offload_cmd(struct nft_base_chain *basechain,
				      struct net_device *dev,
				      enum flow_block_command cmd)
{
	struct netlink_ext_ack extack = {};
	struct flow_block_offload bo;
	int err;

	nft_flow_block_offload_init(&bo, dev_net(dev), cmd, basechain, &extack);

	err = flow_indr_dev_setup_offload(dev, TC_SETUP_BLOCK, basechain, &bo,
					  nft_indr_block_cleanup);
	if (err < 0)
		return err;

	if (list_empty(&bo.cb_list))
		return -EOPNOTSUPP;

	return nft_block_setup(basechain, &bo, cmd);
}

#define FLOW_SETUP_BLOCK TC_SETUP_BLOCK

static int nft_chain_offload_cmd(struct nft_base_chain *basechain,
				 struct net_device *dev,
				 enum flow_block_command cmd)
{
	int err;

	if (dev->netdev_ops->ndo_setup_tc)
		err = nft_block_offload_cmd(basechain, dev, cmd);
	else
		err = nft_indr_block_offload_cmd(basechain, dev, cmd);

	return err;
}

static int nft_flow_block_chain(struct nft_base_chain *basechain,
				const struct net_device *this_dev,
				enum flow_block_command cmd)
{
	struct net_device *dev;
	struct nft_hook *hook;
	int err, i = 0;

	list_for_each_entry(hook, &basechain->hook_list, list) {
		dev = hook->ops.dev;
		if (this_dev && this_dev != dev)
			continue;

		err = nft_chain_offload_cmd(basechain, dev, cmd);
		if (err < 0 && cmd == FLOW_BLOCK_BIND) {
			if (!this_dev)
				goto err_flow_block;

			return err;
		}
		i++;
	}

	return 0;

err_flow_block:
	list_for_each_entry(hook, &basechain->hook_list, list) {
		if (i-- <= 0)
			break;

		dev = hook->ops.dev;
		nft_chain_offload_cmd(basechain, dev, FLOW_BLOCK_UNBIND);
	}
	return err;
}

static int nft_flow_offload_chain(struct nft_chain *chain, u8 *ppolicy,
				  enum flow_block_command cmd)
{
	struct nft_base_chain *basechain;
	u8 policy;

	if (!nft_is_base_chain(chain))
		return -EOPNOTSUPP;

	basechain = nft_base_chain(chain);
	policy = ppolicy ? *ppolicy : basechain->policy;

	/* Only default policy to accept is supported for now. */
	if (cmd == FLOW_BLOCK_BIND && policy == NF_DROP)
		return -EOPNOTSUPP;

	return nft_flow_block_chain(basechain, NULL, cmd);
}

static void nft_flow_rule_offload_abort(struct net *net,
					struct nft_trans *trans)
{
	int err = 0;

	list_for_each_entry_continue_reverse(trans, &net->nft.commit_list, list) {
		if (trans->ctx.family != NFPROTO_NETDEV)
			continue;

		switch (trans->msg_type) {
		case NFT_MSG_NEWCHAIN:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD) ||
			    nft_trans_chain_update(trans))
				continue;

			err = nft_flow_offload_chain(trans->ctx.chain, NULL,
						     FLOW_BLOCK_UNBIND);
			break;
		case NFT_MSG_DELCHAIN:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			err = nft_flow_offload_chain(trans->ctx.chain, NULL,
						     FLOW_BLOCK_BIND);
			break;
		case NFT_MSG_NEWRULE:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			err = nft_flow_offload_rule(trans->ctx.chain,
						    nft_trans_rule(trans),
						    NULL, FLOW_CLS_DESTROY);
			break;
		case NFT_MSG_DELRULE:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			err = nft_flow_offload_rule(trans->ctx.chain,
						    nft_trans_rule(trans),
						    nft_trans_flow_rule(trans),
						    FLOW_CLS_REPLACE);
			break;
		}

		if (WARN_ON_ONCE(err))
			break;
	}
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
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD) ||
			    nft_trans_chain_update(trans))
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
			    !(trans->ctx.flags & NLM_F_APPEND)) {
				err = -EOPNOTSUPP;
				break;
			}
			err = nft_flow_offload_rule(trans->ctx.chain,
						    nft_trans_rule(trans),
						    nft_trans_flow_rule(trans),
						    FLOW_CLS_REPLACE);
			break;
		case NFT_MSG_DELRULE:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			err = nft_flow_offload_rule(trans->ctx.chain,
						    nft_trans_rule(trans),
						    NULL, FLOW_CLS_DESTROY);
			break;
		}

		if (err) {
			nft_flow_rule_offload_abort(net, trans);
			break;
		}
	}

	list_for_each_entry(trans, &net->nft.commit_list, list) {
		if (trans->ctx.family != NFPROTO_NETDEV)
			continue;

		switch (trans->msg_type) {
		case NFT_MSG_NEWRULE:
		case NFT_MSG_DELRULE:
			if (!(trans->ctx.chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			nft_flow_rule_destroy(nft_trans_flow_rule(trans));
			break;
		default:
			break;
		}
	}

	return err;
}

static struct nft_chain *__nft_offload_get_chain(struct net_device *dev)
{
	struct nft_base_chain *basechain;
	struct net *net = dev_net(dev);
	struct nft_hook *hook, *found;
	const struct nft_table *table;
	struct nft_chain *chain;

	list_for_each_entry(table, &net->nft.tables, list) {
		if (table->family != NFPROTO_NETDEV)
			continue;

		list_for_each_entry(chain, &table->chains, list) {
			if (!nft_is_base_chain(chain) ||
			    !(chain->flags & NFT_CHAIN_HW_OFFLOAD))
				continue;

			found = NULL;
			basechain = nft_base_chain(chain);
			list_for_each_entry(hook, &basechain->hook_list, list) {
				if (hook->ops.dev != dev)
					continue;

				found = hook;
				break;
			}
			if (!found)
				continue;

			return chain;
		}
	}

	return NULL;
}

static int nft_offload_netdev_event(struct notifier_block *this,
				    unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);
	struct nft_chain *chain;

	if (event != NETDEV_UNREGISTER)
		return NOTIFY_DONE;

	mutex_lock(&net->nft.commit_mutex);
	chain = __nft_offload_get_chain(dev);
	if (chain)
		nft_flow_block_chain(nft_base_chain(chain), dev,
				     FLOW_BLOCK_UNBIND);

	mutex_unlock(&net->nft.commit_mutex);

	return NOTIFY_DONE;
}

static struct notifier_block nft_offload_netdev_notifier = {
	.notifier_call	= nft_offload_netdev_event,
};

int nft_offload_init(void)
{
	return register_netdevice_notifier(&nft_offload_netdev_notifier);
}

void nft_offload_exit(void)
{
	unregister_netdevice_notifier(&nft_offload_netdev_notifier);
}
