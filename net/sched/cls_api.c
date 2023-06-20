// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/cls_api.c	Packet classifier API.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 *
 * Eduardo J. Blanco <ejbs@netlabs.com.uy> :990222: kmod support
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/jhash.h>
#include <linux/rculist.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_pedit.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_act/tc_vlan.h>
#include <net/tc_act/tc_tunnel_key.h>
#include <net/tc_act/tc_csum.h>
#include <net/tc_act/tc_gact.h>
#include <net/tc_act/tc_police.h>
#include <net/tc_act/tc_sample.h>
#include <net/tc_act/tc_skbedit.h>
#include <net/tc_act/tc_ct.h>
#include <net/tc_act/tc_mpls.h>
#include <net/tc_act/tc_gate.h>
#include <net/flow_offload.h>

extern const struct nla_policy rtm_tca_policy[TCA_MAX + 1];

/* The list of all installed classifier types */
static LIST_HEAD(tcf_proto_base);

/* Protects list of registered TC modules. It is pure SMP lock. */
static DEFINE_RWLOCK(cls_mod_lock);

#ifdef CONFIG_NET_CLS_ACT
DEFINE_STATIC_KEY_FALSE(tc_skb_ext_tc);
EXPORT_SYMBOL(tc_skb_ext_tc);

void tc_skb_ext_tc_enable(void)
{
	static_branch_inc(&tc_skb_ext_tc);
}
EXPORT_SYMBOL(tc_skb_ext_tc_enable);

void tc_skb_ext_tc_disable(void)
{
	static_branch_dec(&tc_skb_ext_tc);
}
EXPORT_SYMBOL(tc_skb_ext_tc_disable);
#endif

static u32 destroy_obj_hashfn(const struct tcf_proto *tp)
{
	return jhash_3words(tp->chain->index, tp->prio,
			    (__force __u32)tp->protocol, 0);
}

static void tcf_proto_signal_destroying(struct tcf_chain *chain,
					struct tcf_proto *tp)
{
	struct tcf_block *block = chain->block;

	mutex_lock(&block->proto_destroy_lock);
	hash_add_rcu(block->proto_destroy_ht, &tp->destroy_ht_node,
		     destroy_obj_hashfn(tp));
	mutex_unlock(&block->proto_destroy_lock);
}

static bool tcf_proto_cmp(const struct tcf_proto *tp1,
			  const struct tcf_proto *tp2)
{
	return tp1->chain->index == tp2->chain->index &&
	       tp1->prio == tp2->prio &&
	       tp1->protocol == tp2->protocol;
}

static bool tcf_proto_exists_destroying(struct tcf_chain *chain,
					struct tcf_proto *tp)
{
	u32 hash = destroy_obj_hashfn(tp);
	struct tcf_proto *iter;
	bool found = false;

	rcu_read_lock();
	hash_for_each_possible_rcu(chain->block->proto_destroy_ht, iter,
				   destroy_ht_node, hash) {
		if (tcf_proto_cmp(tp, iter)) {
			found = true;
			break;
		}
	}
	rcu_read_unlock();

	return found;
}

static void
tcf_proto_signal_destroyed(struct tcf_chain *chain, struct tcf_proto *tp)
{
	struct tcf_block *block = chain->block;

	mutex_lock(&block->proto_destroy_lock);
	if (hash_hashed(&tp->destroy_ht_node))
		hash_del_rcu(&tp->destroy_ht_node);
	mutex_unlock(&block->proto_destroy_lock);
}

/* Find classifier type by string name */

static const struct tcf_proto_ops *__tcf_proto_lookup_ops(const char *kind)
{
	const struct tcf_proto_ops *t, *res = NULL;

	if (kind) {
		read_lock(&cls_mod_lock);
		list_for_each_entry(t, &tcf_proto_base, head) {
			if (strcmp(kind, t->kind) == 0) {
				if (try_module_get(t->owner))
					res = t;
				break;
			}
		}
		read_unlock(&cls_mod_lock);
	}
	return res;
}

static const struct tcf_proto_ops *
tcf_proto_lookup_ops(const char *kind, bool rtnl_held,
		     struct netlink_ext_ack *extack)
{
	const struct tcf_proto_ops *ops;

	ops = __tcf_proto_lookup_ops(kind);
	if (ops)
		return ops;
#ifdef CONFIG_MODULES
	if (rtnl_held)
		rtnl_unlock();
	request_module("cls_%s", kind);
	if (rtnl_held)
		rtnl_lock();
	ops = __tcf_proto_lookup_ops(kind);
	/* We dropped the RTNL semaphore in order to perform
	 * the module load. So, even if we succeeded in loading
	 * the module we have to replay the request. We indicate
	 * this using -EAGAIN.
	 */
	if (ops) {
		module_put(ops->owner);
		return ERR_PTR(-EAGAIN);
	}
#endif
	NL_SET_ERR_MSG(extack, "TC classifier not found");
	return ERR_PTR(-ENOENT);
}

/* Register(unregister) new classifier type */

int register_tcf_proto_ops(struct tcf_proto_ops *ops)
{
	struct tcf_proto_ops *t;
	int rc = -EEXIST;

	write_lock(&cls_mod_lock);
	list_for_each_entry(t, &tcf_proto_base, head)
		if (!strcmp(ops->kind, t->kind))
			goto out;

	list_add_tail(&ops->head, &tcf_proto_base);
	rc = 0;
out:
	write_unlock(&cls_mod_lock);
	return rc;
}
EXPORT_SYMBOL(register_tcf_proto_ops);

static struct workqueue_struct *tc_filter_wq;

int unregister_tcf_proto_ops(struct tcf_proto_ops *ops)
{
	struct tcf_proto_ops *t;
	int rc = -ENOENT;

	/* Wait for outstanding call_rcu()s, if any, from a
	 * tcf_proto_ops's destroy() handler.
	 */
	rcu_barrier();
	flush_workqueue(tc_filter_wq);

	write_lock(&cls_mod_lock);
	list_for_each_entry(t, &tcf_proto_base, head) {
		if (t == ops) {
			list_del(&t->head);
			rc = 0;
			break;
		}
	}
	write_unlock(&cls_mod_lock);
	return rc;
}
EXPORT_SYMBOL(unregister_tcf_proto_ops);

bool tcf_queue_work(struct rcu_work *rwork, work_func_t func)
{
	INIT_RCU_WORK(rwork, func);
	return queue_rcu_work(tc_filter_wq, rwork);
}
EXPORT_SYMBOL(tcf_queue_work);

/* Select new prio value from the range, managed by kernel. */

static inline u32 tcf_auto_prio(struct tcf_proto *tp)
{
	u32 first = TC_H_MAKE(0xC0000000U, 0U);

	if (tp)
		first = tp->prio - 1;

	return TC_H_MAJ(first);
}

static bool tcf_proto_check_kind(struct nlattr *kind, char *name)
{
	if (kind)
		return nla_strscpy(name, kind, IFNAMSIZ) < 0;
	memset(name, 0, IFNAMSIZ);
	return false;
}

static bool tcf_proto_is_unlocked(const char *kind)
{
	const struct tcf_proto_ops *ops;
	bool ret;

	if (strlen(kind) == 0)
		return false;

	ops = tcf_proto_lookup_ops(kind, false, NULL);
	/* On error return false to take rtnl lock. Proto lookup/create
	 * functions will perform lookup again and properly handle errors.
	 */
	if (IS_ERR(ops))
		return false;

	ret = !!(ops->flags & TCF_PROTO_OPS_DOIT_UNLOCKED);
	module_put(ops->owner);
	return ret;
}

static struct tcf_proto *tcf_proto_create(const char *kind, u32 protocol,
					  u32 prio, struct tcf_chain *chain,
					  bool rtnl_held,
					  struct netlink_ext_ack *extack)
{
	struct tcf_proto *tp;
	int err;

	tp = kzalloc(sizeof(*tp), GFP_KERNEL);
	if (!tp)
		return ERR_PTR(-ENOBUFS);

	tp->ops = tcf_proto_lookup_ops(kind, rtnl_held, extack);
	if (IS_ERR(tp->ops)) {
		err = PTR_ERR(tp->ops);
		goto errout;
	}
	tp->classify = tp->ops->classify;
	tp->protocol = protocol;
	tp->prio = prio;
	tp->chain = chain;
	spin_lock_init(&tp->lock);
	refcount_set(&tp->refcnt, 1);

	err = tp->ops->init(tp);
	if (err) {
		module_put(tp->ops->owner);
		goto errout;
	}
	return tp;

errout:
	kfree(tp);
	return ERR_PTR(err);
}

static void tcf_proto_get(struct tcf_proto *tp)
{
	refcount_inc(&tp->refcnt);
}

static void tcf_chain_put(struct tcf_chain *chain);

static void tcf_proto_destroy(struct tcf_proto *tp, bool rtnl_held,
			      bool sig_destroy, struct netlink_ext_ack *extack)
{
	tp->ops->destroy(tp, rtnl_held, extack);
	if (sig_destroy)
		tcf_proto_signal_destroyed(tp->chain, tp);
	tcf_chain_put(tp->chain);
	module_put(tp->ops->owner);
	kfree_rcu(tp, rcu);
}

static void tcf_proto_put(struct tcf_proto *tp, bool rtnl_held,
			  struct netlink_ext_ack *extack)
{
	if (refcount_dec_and_test(&tp->refcnt))
		tcf_proto_destroy(tp, rtnl_held, true, extack);
}

static bool tcf_proto_check_delete(struct tcf_proto *tp)
{
	if (tp->ops->delete_empty)
		return tp->ops->delete_empty(tp);

	tp->deleting = true;
	return tp->deleting;
}

static void tcf_proto_mark_delete(struct tcf_proto *tp)
{
	spin_lock(&tp->lock);
	tp->deleting = true;
	spin_unlock(&tp->lock);
}

static bool tcf_proto_is_deleting(struct tcf_proto *tp)
{
	bool deleting;

	spin_lock(&tp->lock);
	deleting = tp->deleting;
	spin_unlock(&tp->lock);

	return deleting;
}

#define ASSERT_BLOCK_LOCKED(block)					\
	lockdep_assert_held(&(block)->lock)

struct tcf_filter_chain_list_item {
	struct list_head list;
	tcf_chain_head_change_t *chain_head_change;
	void *chain_head_change_priv;
};

static struct tcf_chain *tcf_chain_create(struct tcf_block *block,
					  u32 chain_index)
{
	struct tcf_chain *chain;

	ASSERT_BLOCK_LOCKED(block);

	chain = kzalloc(sizeof(*chain), GFP_KERNEL);
	if (!chain)
		return NULL;
	list_add_tail_rcu(&chain->list, &block->chain_list);
	mutex_init(&chain->filter_chain_lock);
	chain->block = block;
	chain->index = chain_index;
	chain->refcnt = 1;
	if (!chain->index)
		block->chain0.chain = chain;
	return chain;
}

static void tcf_chain_head_change_item(struct tcf_filter_chain_list_item *item,
				       struct tcf_proto *tp_head)
{
	if (item->chain_head_change)
		item->chain_head_change(tp_head, item->chain_head_change_priv);
}

static void tcf_chain0_head_change(struct tcf_chain *chain,
				   struct tcf_proto *tp_head)
{
	struct tcf_filter_chain_list_item *item;
	struct tcf_block *block = chain->block;

	if (chain->index)
		return;

	mutex_lock(&block->lock);
	list_for_each_entry(item, &block->chain0.filter_chain_list, list)
		tcf_chain_head_change_item(item, tp_head);
	mutex_unlock(&block->lock);
}

/* Returns true if block can be safely freed. */

static bool tcf_chain_detach(struct tcf_chain *chain)
{
	struct tcf_block *block = chain->block;

	ASSERT_BLOCK_LOCKED(block);

	list_del_rcu(&chain->list);
	if (!chain->index)
		block->chain0.chain = NULL;

	if (list_empty(&block->chain_list) &&
	    refcount_read(&block->refcnt) == 0)
		return true;

	return false;
}

static void tcf_block_destroy(struct tcf_block *block)
{
	mutex_destroy(&block->lock);
	mutex_destroy(&block->proto_destroy_lock);
	kfree_rcu(block, rcu);
}

static void tcf_chain_destroy(struct tcf_chain *chain, bool free_block)
{
	struct tcf_block *block = chain->block;

	mutex_destroy(&chain->filter_chain_lock);
	kfree_rcu(chain, rcu);
	if (free_block)
		tcf_block_destroy(block);
}

static void tcf_chain_hold(struct tcf_chain *chain)
{
	ASSERT_BLOCK_LOCKED(chain->block);

	++chain->refcnt;
}

static bool tcf_chain_held_by_acts_only(struct tcf_chain *chain)
{
	ASSERT_BLOCK_LOCKED(chain->block);

	/* In case all the references are action references, this
	 * chain should not be shown to the user.
	 */
	return chain->refcnt == chain->action_refcnt;
}

static struct tcf_chain *tcf_chain_lookup(struct tcf_block *block,
					  u32 chain_index)
{
	struct tcf_chain *chain;

	ASSERT_BLOCK_LOCKED(block);

	list_for_each_entry(chain, &block->chain_list, list) {
		if (chain->index == chain_index)
			return chain;
	}
	return NULL;
}

#if IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
static struct tcf_chain *tcf_chain_lookup_rcu(const struct tcf_block *block,
					      u32 chain_index)
{
	struct tcf_chain *chain;

	list_for_each_entry_rcu(chain, &block->chain_list, list) {
		if (chain->index == chain_index)
			return chain;
	}
	return NULL;
}
#endif

static int tc_chain_notify(struct tcf_chain *chain, struct sk_buff *oskb,
			   u32 seq, u16 flags, int event, bool unicast);

static struct tcf_chain *__tcf_chain_get(struct tcf_block *block,
					 u32 chain_index, bool create,
					 bool by_act)
{
	struct tcf_chain *chain = NULL;
	bool is_first_reference;

	mutex_lock(&block->lock);
	chain = tcf_chain_lookup(block, chain_index);
	if (chain) {
		tcf_chain_hold(chain);
	} else {
		if (!create)
			goto errout;
		chain = tcf_chain_create(block, chain_index);
		if (!chain)
			goto errout;
	}

	if (by_act)
		++chain->action_refcnt;
	is_first_reference = chain->refcnt - chain->action_refcnt == 1;
	mutex_unlock(&block->lock);

	/* Send notification only in case we got the first
	 * non-action reference. Until then, the chain acts only as
	 * a placeholder for actions pointing to it and user ought
	 * not know about them.
	 */
	if (is_first_reference && !by_act)
		tc_chain_notify(chain, NULL, 0, NLM_F_CREATE | NLM_F_EXCL,
				RTM_NEWCHAIN, false);

	return chain;

errout:
	mutex_unlock(&block->lock);
	return chain;
}

static struct tcf_chain *tcf_chain_get(struct tcf_block *block, u32 chain_index,
				       bool create)
{
	return __tcf_chain_get(block, chain_index, create, false);
}

struct tcf_chain *tcf_chain_get_by_act(struct tcf_block *block, u32 chain_index)
{
	return __tcf_chain_get(block, chain_index, true, true);
}
EXPORT_SYMBOL(tcf_chain_get_by_act);

static void tc_chain_tmplt_del(const struct tcf_proto_ops *tmplt_ops,
			       void *tmplt_priv);
static int tc_chain_notify_delete(const struct tcf_proto_ops *tmplt_ops,
				  void *tmplt_priv, u32 chain_index,
				  struct tcf_block *block, struct sk_buff *oskb,
				  u32 seq, u16 flags, bool unicast);

static void __tcf_chain_put(struct tcf_chain *chain, bool by_act,
			    bool explicitly_created)
{
	struct tcf_block *block = chain->block;
	const struct tcf_proto_ops *tmplt_ops;
	bool free_block = false;
	unsigned int refcnt;
	void *tmplt_priv;

	mutex_lock(&block->lock);
	if (explicitly_created) {
		if (!chain->explicitly_created) {
			mutex_unlock(&block->lock);
			return;
		}
		chain->explicitly_created = false;
	}

	if (by_act)
		chain->action_refcnt--;

	/* tc_chain_notify_delete can't be called while holding block lock.
	 * However, when block is unlocked chain can be changed concurrently, so
	 * save these to temporary variables.
	 */
	refcnt = --chain->refcnt;
	tmplt_ops = chain->tmplt_ops;
	tmplt_priv = chain->tmplt_priv;

	/* The last dropped non-action reference will trigger notification. */
	if (refcnt - chain->action_refcnt == 0 && !by_act) {
		tc_chain_notify_delete(tmplt_ops, tmplt_priv, chain->index,
				       block, NULL, 0, 0, false);
		/* Last reference to chain, no need to lock. */
		chain->flushing = false;
	}

	if (refcnt == 0)
		free_block = tcf_chain_detach(chain);
	mutex_unlock(&block->lock);

	if (refcnt == 0) {
		tc_chain_tmplt_del(tmplt_ops, tmplt_priv);
		tcf_chain_destroy(chain, free_block);
	}
}

static void tcf_chain_put(struct tcf_chain *chain)
{
	__tcf_chain_put(chain, false, false);
}

void tcf_chain_put_by_act(struct tcf_chain *chain)
{
	__tcf_chain_put(chain, true, false);
}
EXPORT_SYMBOL(tcf_chain_put_by_act);

static void tcf_chain_put_explicitly_created(struct tcf_chain *chain)
{
	__tcf_chain_put(chain, false, true);
}

static void tcf_chain_flush(struct tcf_chain *chain, bool rtnl_held)
{
	struct tcf_proto *tp, *tp_next;

	mutex_lock(&chain->filter_chain_lock);
	tp = tcf_chain_dereference(chain->filter_chain, chain);
	while (tp) {
		tp_next = rcu_dereference_protected(tp->next, 1);
		tcf_proto_signal_destroying(chain, tp);
		tp = tp_next;
	}
	tp = tcf_chain_dereference(chain->filter_chain, chain);
	RCU_INIT_POINTER(chain->filter_chain, NULL);
	tcf_chain0_head_change(chain, NULL);
	chain->flushing = true;
	mutex_unlock(&chain->filter_chain_lock);

	while (tp) {
		tp_next = rcu_dereference_protected(tp->next, 1);
		tcf_proto_put(tp, rtnl_held, NULL);
		tp = tp_next;
	}
}

static int tcf_block_setup(struct tcf_block *block,
			   struct flow_block_offload *bo);

static void tcf_block_offload_init(struct flow_block_offload *bo,
				   struct net_device *dev, struct Qdisc *sch,
				   enum flow_block_command command,
				   enum flow_block_binder_type binder_type,
				   struct flow_block *flow_block,
				   bool shared, struct netlink_ext_ack *extack)
{
	bo->net = dev_net(dev);
	bo->command = command;
	bo->binder_type = binder_type;
	bo->block = flow_block;
	bo->block_shared = shared;
	bo->extack = extack;
	bo->sch = sch;
	bo->cb_list_head = &flow_block->cb_list;
	INIT_LIST_HEAD(&bo->cb_list);
}

static void tcf_block_unbind(struct tcf_block *block,
			     struct flow_block_offload *bo);

static void tc_block_indr_cleanup(struct flow_block_cb *block_cb)
{
	struct tcf_block *block = block_cb->indr.data;
	struct net_device *dev = block_cb->indr.dev;
	struct Qdisc *sch = block_cb->indr.sch;
	struct netlink_ext_ack extack = {};
	struct flow_block_offload bo = {};

	tcf_block_offload_init(&bo, dev, sch, FLOW_BLOCK_UNBIND,
			       block_cb->indr.binder_type,
			       &block->flow_block, tcf_block_shared(block),
			       &extack);
	rtnl_lock();
	down_write(&block->cb_lock);
	list_del(&block_cb->driver_list);
	list_move(&block_cb->list, &bo.cb_list);
	tcf_block_unbind(block, &bo);
	up_write(&block->cb_lock);
	rtnl_unlock();
}

static bool tcf_block_offload_in_use(struct tcf_block *block)
{
	return atomic_read(&block->offloadcnt);
}

static int tcf_block_offload_cmd(struct tcf_block *block,
				 struct net_device *dev, struct Qdisc *sch,
				 struct tcf_block_ext_info *ei,
				 enum flow_block_command command,
				 struct netlink_ext_ack *extack)
{
	struct flow_block_offload bo = {};

	tcf_block_offload_init(&bo, dev, sch, command, ei->binder_type,
			       &block->flow_block, tcf_block_shared(block),
			       extack);

	if (dev->netdev_ops->ndo_setup_tc) {
		int err;

		err = dev->netdev_ops->ndo_setup_tc(dev, TC_SETUP_BLOCK, &bo);
		if (err < 0) {
			if (err != -EOPNOTSUPP)
				NL_SET_ERR_MSG(extack, "Driver ndo_setup_tc failed");
			return err;
		}

		return tcf_block_setup(block, &bo);
	}

	flow_indr_dev_setup_offload(dev, sch, TC_SETUP_BLOCK, block, &bo,
				    tc_block_indr_cleanup);
	tcf_block_setup(block, &bo);

	return -EOPNOTSUPP;
}

static int tcf_block_offload_bind(struct tcf_block *block, struct Qdisc *q,
				  struct tcf_block_ext_info *ei,
				  struct netlink_ext_ack *extack)
{
	struct net_device *dev = q->dev_queue->dev;
	int err;

	down_write(&block->cb_lock);

	/* If tc offload feature is disabled and the block we try to bind
	 * to already has some offloaded filters, forbid to bind.
	 */
	if (dev->netdev_ops->ndo_setup_tc &&
	    !tc_can_offload(dev) &&
	    tcf_block_offload_in_use(block)) {
		NL_SET_ERR_MSG(extack, "Bind to offloaded block failed as dev has offload disabled");
		err = -EOPNOTSUPP;
		goto err_unlock;
	}

	err = tcf_block_offload_cmd(block, dev, q, ei, FLOW_BLOCK_BIND, extack);
	if (err == -EOPNOTSUPP)
		goto no_offload_dev_inc;
	if (err)
		goto err_unlock;

	up_write(&block->cb_lock);
	return 0;

no_offload_dev_inc:
	if (tcf_block_offload_in_use(block))
		goto err_unlock;

	err = 0;
	block->nooffloaddevcnt++;
err_unlock:
	up_write(&block->cb_lock);
	return err;
}

static void tcf_block_offload_unbind(struct tcf_block *block, struct Qdisc *q,
				     struct tcf_block_ext_info *ei)
{
	struct net_device *dev = q->dev_queue->dev;
	int err;

	down_write(&block->cb_lock);
	err = tcf_block_offload_cmd(block, dev, q, ei, FLOW_BLOCK_UNBIND, NULL);
	if (err == -EOPNOTSUPP)
		goto no_offload_dev_dec;
	up_write(&block->cb_lock);
	return;

no_offload_dev_dec:
	WARN_ON(block->nooffloaddevcnt-- == 0);
	up_write(&block->cb_lock);
}

static int
tcf_chain0_head_change_cb_add(struct tcf_block *block,
			      struct tcf_block_ext_info *ei,
			      struct netlink_ext_ack *extack)
{
	struct tcf_filter_chain_list_item *item;
	struct tcf_chain *chain0;

	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item) {
		NL_SET_ERR_MSG(extack, "Memory allocation for head change callback item failed");
		return -ENOMEM;
	}
	item->chain_head_change = ei->chain_head_change;
	item->chain_head_change_priv = ei->chain_head_change_priv;

	mutex_lock(&block->lock);
	chain0 = block->chain0.chain;
	if (chain0)
		tcf_chain_hold(chain0);
	else
		list_add(&item->list, &block->chain0.filter_chain_list);
	mutex_unlock(&block->lock);

	if (chain0) {
		struct tcf_proto *tp_head;

		mutex_lock(&chain0->filter_chain_lock);

		tp_head = tcf_chain_dereference(chain0->filter_chain, chain0);
		if (tp_head)
			tcf_chain_head_change_item(item, tp_head);

		mutex_lock(&block->lock);
		list_add(&item->list, &block->chain0.filter_chain_list);
		mutex_unlock(&block->lock);

		mutex_unlock(&chain0->filter_chain_lock);
		tcf_chain_put(chain0);
	}

	return 0;
}

static void
tcf_chain0_head_change_cb_del(struct tcf_block *block,
			      struct tcf_block_ext_info *ei)
{
	struct tcf_filter_chain_list_item *item;

	mutex_lock(&block->lock);
	list_for_each_entry(item, &block->chain0.filter_chain_list, list) {
		if ((!ei->chain_head_change && !ei->chain_head_change_priv) ||
		    (item->chain_head_change == ei->chain_head_change &&
		     item->chain_head_change_priv == ei->chain_head_change_priv)) {
			if (block->chain0.chain)
				tcf_chain_head_change_item(item, NULL);
			list_del(&item->list);
			mutex_unlock(&block->lock);

			kfree(item);
			return;
		}
	}
	mutex_unlock(&block->lock);
	WARN_ON(1);
}

struct tcf_net {
	spinlock_t idr_lock; /* Protects idr */
	struct idr idr;
};

static unsigned int tcf_net_id;

static int tcf_block_insert(struct tcf_block *block, struct net *net,
			    struct netlink_ext_ack *extack)
{
	struct tcf_net *tn = net_generic(net, tcf_net_id);
	int err;

	idr_preload(GFP_KERNEL);
	spin_lock(&tn->idr_lock);
	err = idr_alloc_u32(&tn->idr, block, &block->index, block->index,
			    GFP_NOWAIT);
	spin_unlock(&tn->idr_lock);
	idr_preload_end();

	return err;
}

static void tcf_block_remove(struct tcf_block *block, struct net *net)
{
	struct tcf_net *tn = net_generic(net, tcf_net_id);

	spin_lock(&tn->idr_lock);
	idr_remove(&tn->idr, block->index);
	spin_unlock(&tn->idr_lock);
}

static struct tcf_block *tcf_block_create(struct net *net, struct Qdisc *q,
					  u32 block_index,
					  struct netlink_ext_ack *extack)
{
	struct tcf_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block) {
		NL_SET_ERR_MSG(extack, "Memory allocation for block failed");
		return ERR_PTR(-ENOMEM);
	}
	mutex_init(&block->lock);
	mutex_init(&block->proto_destroy_lock);
	init_rwsem(&block->cb_lock);
	flow_block_init(&block->flow_block);
	INIT_LIST_HEAD(&block->chain_list);
	INIT_LIST_HEAD(&block->owner_list);
	INIT_LIST_HEAD(&block->chain0.filter_chain_list);

	refcount_set(&block->refcnt, 1);
	block->net = net;
	block->index = block_index;

	/* Don't store q pointer for blocks which are shared */
	if (!tcf_block_shared(block))
		block->q = q;
	return block;
}

static struct tcf_block *tcf_block_lookup(struct net *net, u32 block_index)
{
	struct tcf_net *tn = net_generic(net, tcf_net_id);

	return idr_find(&tn->idr, block_index);
}

static struct tcf_block *tcf_block_refcnt_get(struct net *net, u32 block_index)
{
	struct tcf_block *block;

	rcu_read_lock();
	block = tcf_block_lookup(net, block_index);
	if (block && !refcount_inc_not_zero(&block->refcnt))
		block = NULL;
	rcu_read_unlock();

	return block;
}

static struct tcf_chain *
__tcf_get_next_chain(struct tcf_block *block, struct tcf_chain *chain)
{
	mutex_lock(&block->lock);
	if (chain)
		chain = list_is_last(&chain->list, &block->chain_list) ?
			NULL : list_next_entry(chain, list);
	else
		chain = list_first_entry_or_null(&block->chain_list,
						 struct tcf_chain, list);

	/* skip all action-only chains */
	while (chain && tcf_chain_held_by_acts_only(chain))
		chain = list_is_last(&chain->list, &block->chain_list) ?
			NULL : list_next_entry(chain, list);

	if (chain)
		tcf_chain_hold(chain);
	mutex_unlock(&block->lock);

	return chain;
}

/* Function to be used by all clients that want to iterate over all chains on
 * block. It properly obtains block->lock and takes reference to chain before
 * returning it. Users of this function must be tolerant to concurrent chain
 * insertion/deletion or ensure that no concurrent chain modification is
 * possible. Note that all netlink dump callbacks cannot guarantee to provide
 * consistent dump because rtnl lock is released each time skb is filled with
 * data and sent to user-space.
 */

struct tcf_chain *
tcf_get_next_chain(struct tcf_block *block, struct tcf_chain *chain)
{
	struct tcf_chain *chain_next = __tcf_get_next_chain(block, chain);

	if (chain)
		tcf_chain_put(chain);

	return chain_next;
}
EXPORT_SYMBOL(tcf_get_next_chain);

static struct tcf_proto *
__tcf_get_next_proto(struct tcf_chain *chain, struct tcf_proto *tp)
{
	u32 prio = 0;

	ASSERT_RTNL();
	mutex_lock(&chain->filter_chain_lock);

	if (!tp) {
		tp = tcf_chain_dereference(chain->filter_chain, chain);
	} else if (tcf_proto_is_deleting(tp)) {
		/* 'deleting' flag is set and chain->filter_chain_lock was
		 * unlocked, which means next pointer could be invalid. Restart
		 * search.
		 */
		prio = tp->prio + 1;
		tp = tcf_chain_dereference(chain->filter_chain, chain);

		for (; tp; tp = tcf_chain_dereference(tp->next, chain))
			if (!tp->deleting && tp->prio >= prio)
				break;
	} else {
		tp = tcf_chain_dereference(tp->next, chain);
	}

	if (tp)
		tcf_proto_get(tp);

	mutex_unlock(&chain->filter_chain_lock);

	return tp;
}

/* Function to be used by all clients that want to iterate over all tp's on
 * chain. Users of this function must be tolerant to concurrent tp
 * insertion/deletion or ensure that no concurrent chain modification is
 * possible. Note that all netlink dump callbacks cannot guarantee to provide
 * consistent dump because rtnl lock is released each time skb is filled with
 * data and sent to user-space.
 */

struct tcf_proto *
tcf_get_next_proto(struct tcf_chain *chain, struct tcf_proto *tp)
{
	struct tcf_proto *tp_next = __tcf_get_next_proto(chain, tp);

	if (tp)
		tcf_proto_put(tp, true, NULL);

	return tp_next;
}
EXPORT_SYMBOL(tcf_get_next_proto);

static void tcf_block_flush_all_chains(struct tcf_block *block, bool rtnl_held)
{
	struct tcf_chain *chain;

	/* Last reference to block. At this point chains cannot be added or
	 * removed concurrently.
	 */
	for (chain = tcf_get_next_chain(block, NULL);
	     chain;
	     chain = tcf_get_next_chain(block, chain)) {
		tcf_chain_put_explicitly_created(chain);
		tcf_chain_flush(chain, rtnl_held);
	}
}

/* Lookup Qdisc and increments its reference counter.
 * Set parent, if necessary.
 */

static int __tcf_qdisc_find(struct net *net, struct Qdisc **q,
			    u32 *parent, int ifindex, bool rtnl_held,
			    struct netlink_ext_ack *extack)
{
	const struct Qdisc_class_ops *cops;
	struct net_device *dev;
	int err = 0;

	if (ifindex == TCM_IFINDEX_MAGIC_BLOCK)
		return 0;

	rcu_read_lock();

	/* Find link */
	dev = dev_get_by_index_rcu(net, ifindex);
	if (!dev) {
		rcu_read_unlock();
		return -ENODEV;
	}

	/* Find qdisc */
	if (!*parent) {
		*q = rcu_dereference(dev->qdisc);
		*parent = (*q)->handle;
	} else {
		*q = qdisc_lookup_rcu(dev, TC_H_MAJ(*parent));
		if (!*q) {
			NL_SET_ERR_MSG(extack, "Parent Qdisc doesn't exists");
			err = -EINVAL;
			goto errout_rcu;
		}
	}

	*q = qdisc_refcount_inc_nz(*q);
	if (!*q) {
		NL_SET_ERR_MSG(extack, "Parent Qdisc doesn't exists");
		err = -EINVAL;
		goto errout_rcu;
	}

	/* Is it classful? */
	cops = (*q)->ops->cl_ops;
	if (!cops) {
		NL_SET_ERR_MSG(extack, "Qdisc not classful");
		err = -EINVAL;
		goto errout_qdisc;
	}

	if (!cops->tcf_block) {
		NL_SET_ERR_MSG(extack, "Class doesn't support blocks");
		err = -EOPNOTSUPP;
		goto errout_qdisc;
	}

errout_rcu:
	/* At this point we know that qdisc is not noop_qdisc,
	 * which means that qdisc holds a reference to net_device
	 * and we hold a reference to qdisc, so it is safe to release
	 * rcu read lock.
	 */
	rcu_read_unlock();
	return err;

errout_qdisc:
	rcu_read_unlock();

	if (rtnl_held)
		qdisc_put(*q);
	else
		qdisc_put_unlocked(*q);
	*q = NULL;

	return err;
}

static int __tcf_qdisc_cl_find(struct Qdisc *q, u32 parent, unsigned long *cl,
			       int ifindex, struct netlink_ext_ack *extack)
{
	if (ifindex == TCM_IFINDEX_MAGIC_BLOCK)
		return 0;

	/* Do we search for filter, attached to class? */
	if (TC_H_MIN(parent)) {
		const struct Qdisc_class_ops *cops = q->ops->cl_ops;

		*cl = cops->find(q, parent);
		if (*cl == 0) {
			NL_SET_ERR_MSG(extack, "Specified class doesn't exist");
			return -ENOENT;
		}
	}

	return 0;
}

static struct tcf_block *__tcf_block_find(struct net *net, struct Qdisc *q,
					  unsigned long cl, int ifindex,
					  u32 block_index,
					  struct netlink_ext_ack *extack)
{
	struct tcf_block *block;

	if (ifindex == TCM_IFINDEX_MAGIC_BLOCK) {
		block = tcf_block_refcnt_get(net, block_index);
		if (!block) {
			NL_SET_ERR_MSG(extack, "Block of given index was not found");
			return ERR_PTR(-EINVAL);
		}
	} else {
		const struct Qdisc_class_ops *cops = q->ops->cl_ops;

		block = cops->tcf_block(q, cl, extack);
		if (!block)
			return ERR_PTR(-EINVAL);

		if (tcf_block_shared(block)) {
			NL_SET_ERR_MSG(extack, "This filter block is shared. Please use the block index to manipulate the filters");
			return ERR_PTR(-EOPNOTSUPP);
		}

		/* Always take reference to block in order to support execution
		 * of rules update path of cls API without rtnl lock. Caller
		 * must release block when it is finished using it. 'if' block
		 * of this conditional obtain reference to block by calling
		 * tcf_block_refcnt_get().
		 */
		refcount_inc(&block->refcnt);
	}

	return block;
}

static void __tcf_block_put(struct tcf_block *block, struct Qdisc *q,
			    struct tcf_block_ext_info *ei, bool rtnl_held)
{
	if (refcount_dec_and_mutex_lock(&block->refcnt, &block->lock)) {
		/* Flushing/putting all chains will cause the block to be
		 * deallocated when last chain is freed. However, if chain_list
		 * is empty, block has to be manually deallocated. After block
		 * reference counter reached 0, it is no longer possible to
		 * increment it or add new chains to block.
		 */
		bool free_block = list_empty(&block->chain_list);

		mutex_unlock(&block->lock);
		if (tcf_block_shared(block))
			tcf_block_remove(block, block->net);

		if (q)
			tcf_block_offload_unbind(block, q, ei);

		if (free_block)
			tcf_block_destroy(block);
		else
			tcf_block_flush_all_chains(block, rtnl_held);
	} else if (q) {
		tcf_block_offload_unbind(block, q, ei);
	}
}

static void tcf_block_refcnt_put(struct tcf_block *block, bool rtnl_held)
{
	__tcf_block_put(block, NULL, NULL, rtnl_held);
}

/* Find tcf block.
 * Set q, parent, cl when appropriate.
 */

static struct tcf_block *tcf_block_find(struct net *net, struct Qdisc **q,
					u32 *parent, unsigned long *cl,
					int ifindex, u32 block_index,
					struct netlink_ext_ack *extack)
{
	struct tcf_block *block;
	int err = 0;

	ASSERT_RTNL();

	err = __tcf_qdisc_find(net, q, parent, ifindex, true, extack);
	if (err)
		goto errout;

	err = __tcf_qdisc_cl_find(*q, *parent, cl, ifindex, extack);
	if (err)
		goto errout_qdisc;

	block = __tcf_block_find(net, *q, *cl, ifindex, block_index, extack);
	if (IS_ERR(block)) {
		err = PTR_ERR(block);
		goto errout_qdisc;
	}

	return block;

errout_qdisc:
	if (*q)
		qdisc_put(*q);
errout:
	*q = NULL;
	return ERR_PTR(err);
}

static void tcf_block_release(struct Qdisc *q, struct tcf_block *block,
			      bool rtnl_held)
{
	if (!IS_ERR_OR_NULL(block))
		tcf_block_refcnt_put(block, rtnl_held);

	if (q) {
		if (rtnl_held)
			qdisc_put(q);
		else
			qdisc_put_unlocked(q);
	}
}

struct tcf_block_owner_item {
	struct list_head list;
	struct Qdisc *q;
	enum flow_block_binder_type binder_type;
};

static void
tcf_block_owner_netif_keep_dst(struct tcf_block *block,
			       struct Qdisc *q,
			       enum flow_block_binder_type binder_type)
{
	if (block->keep_dst &&
	    binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_INGRESS &&
	    binder_type != FLOW_BLOCK_BINDER_TYPE_CLSACT_EGRESS)
		netif_keep_dst(qdisc_dev(q));
}

void tcf_block_netif_keep_dst(struct tcf_block *block)
{
	struct tcf_block_owner_item *item;

	block->keep_dst = true;
	list_for_each_entry(item, &block->owner_list, list)
		tcf_block_owner_netif_keep_dst(block, item->q,
					       item->binder_type);
}
EXPORT_SYMBOL(tcf_block_netif_keep_dst);

static int tcf_block_owner_add(struct tcf_block *block,
			       struct Qdisc *q,
			       enum flow_block_binder_type binder_type)
{
	struct tcf_block_owner_item *item;

	item = kmalloc(sizeof(*item), GFP_KERNEL);
	if (!item)
		return -ENOMEM;
	item->q = q;
	item->binder_type = binder_type;
	list_add(&item->list, &block->owner_list);
	return 0;
}

static void tcf_block_owner_del(struct tcf_block *block,
				struct Qdisc *q,
				enum flow_block_binder_type binder_type)
{
	struct tcf_block_owner_item *item;

	list_for_each_entry(item, &block->owner_list, list) {
		if (item->q == q && item->binder_type == binder_type) {
			list_del(&item->list);
			kfree(item);
			return;
		}
	}
	WARN_ON(1);
}

int tcf_block_get_ext(struct tcf_block **p_block, struct Qdisc *q,
		      struct tcf_block_ext_info *ei,
		      struct netlink_ext_ack *extack)
{
	struct net *net = qdisc_net(q);
	struct tcf_block *block = NULL;
	int err;

	if (ei->block_index)
		/* block_index not 0 means the shared block is requested */
		block = tcf_block_refcnt_get(net, ei->block_index);

	if (!block) {
		block = tcf_block_create(net, q, ei->block_index, extack);
		if (IS_ERR(block))
			return PTR_ERR(block);
		if (tcf_block_shared(block)) {
			err = tcf_block_insert(block, net, extack);
			if (err)
				goto err_block_insert;
		}
	}

	err = tcf_block_owner_add(block, q, ei->binder_type);
	if (err)
		goto err_block_owner_add;

	tcf_block_owner_netif_keep_dst(block, q, ei->binder_type);

	err = tcf_chain0_head_change_cb_add(block, ei, extack);
	if (err)
		goto err_chain0_head_change_cb_add;

	err = tcf_block_offload_bind(block, q, ei, extack);
	if (err)
		goto err_block_offload_bind;

	*p_block = block;
	return 0;

err_block_offload_bind:
	tcf_chain0_head_change_cb_del(block, ei);
err_chain0_head_change_cb_add:
	tcf_block_owner_del(block, q, ei->binder_type);
err_block_owner_add:
err_block_insert:
	tcf_block_refcnt_put(block, true);
	return err;
}
EXPORT_SYMBOL(tcf_block_get_ext);

static void tcf_chain_head_change_dflt(struct tcf_proto *tp_head, void *priv)
{
	struct tcf_proto __rcu **p_filter_chain = priv;

	rcu_assign_pointer(*p_filter_chain, tp_head);
}

int tcf_block_get(struct tcf_block **p_block,
		  struct tcf_proto __rcu **p_filter_chain, struct Qdisc *q,
		  struct netlink_ext_ack *extack)
{
	struct tcf_block_ext_info ei = {
		.chain_head_change = tcf_chain_head_change_dflt,
		.chain_head_change_priv = p_filter_chain,
	};

	WARN_ON(!p_filter_chain);
	return tcf_block_get_ext(p_block, q, &ei, extack);
}
EXPORT_SYMBOL(tcf_block_get);

/* XXX: Standalone actions are not allowed to jump to any chain, and bound
 * actions should be all removed after flushing.
 */
void tcf_block_put_ext(struct tcf_block *block, struct Qdisc *q,
		       struct tcf_block_ext_info *ei)
{
	if (!block)
		return;
	tcf_chain0_head_change_cb_del(block, ei);
	tcf_block_owner_del(block, q, ei->binder_type);

	__tcf_block_put(block, q, ei, true);
}
EXPORT_SYMBOL(tcf_block_put_ext);

void tcf_block_put(struct tcf_block *block)
{
	struct tcf_block_ext_info ei = {0, };

	if (!block)
		return;
	tcf_block_put_ext(block, block->q, &ei);
}

EXPORT_SYMBOL(tcf_block_put);

static int
tcf_block_playback_offloads(struct tcf_block *block, flow_setup_cb_t *cb,
			    void *cb_priv, bool add, bool offload_in_use,
			    struct netlink_ext_ack *extack)
{
	struct tcf_chain *chain, *chain_prev;
	struct tcf_proto *tp, *tp_prev;
	int err;

	lockdep_assert_held(&block->cb_lock);

	for (chain = __tcf_get_next_chain(block, NULL);
	     chain;
	     chain_prev = chain,
		     chain = __tcf_get_next_chain(block, chain),
		     tcf_chain_put(chain_prev)) {
		for (tp = __tcf_get_next_proto(chain, NULL); tp;
		     tp_prev = tp,
			     tp = __tcf_get_next_proto(chain, tp),
			     tcf_proto_put(tp_prev, true, NULL)) {
			if (tp->ops->reoffload) {
				err = tp->ops->reoffload(tp, add, cb, cb_priv,
							 extack);
				if (err && add)
					goto err_playback_remove;
			} else if (add && offload_in_use) {
				err = -EOPNOTSUPP;
				NL_SET_ERR_MSG(extack, "Filter HW offload failed - classifier without re-offloading support");
				goto err_playback_remove;
			}
		}
	}

	return 0;

err_playback_remove:
	tcf_proto_put(tp, true, NULL);
	tcf_chain_put(chain);
	tcf_block_playback_offloads(block, cb, cb_priv, false, offload_in_use,
				    extack);
	return err;
}

static int tcf_block_bind(struct tcf_block *block,
			  struct flow_block_offload *bo)
{
	struct flow_block_cb *block_cb, *next;
	int err, i = 0;

	lockdep_assert_held(&block->cb_lock);

	list_for_each_entry(block_cb, &bo->cb_list, list) {
		err = tcf_block_playback_offloads(block, block_cb->cb,
						  block_cb->cb_priv, true,
						  tcf_block_offload_in_use(block),
						  bo->extack);
		if (err)
			goto err_unroll;
		if (!bo->unlocked_driver_cb)
			block->lockeddevcnt++;

		i++;
	}
	list_splice(&bo->cb_list, &block->flow_block.cb_list);

	return 0;

err_unroll:
	list_for_each_entry_safe(block_cb, next, &bo->cb_list, list) {
		if (i-- > 0) {
			list_del(&block_cb->list);
			tcf_block_playback_offloads(block, block_cb->cb,
						    block_cb->cb_priv, false,
						    tcf_block_offload_in_use(block),
						    NULL);
			if (!bo->unlocked_driver_cb)
				block->lockeddevcnt--;
		}
		flow_block_cb_free(block_cb);
	}

	return err;
}

static void tcf_block_unbind(struct tcf_block *block,
			     struct flow_block_offload *bo)
{
	struct flow_block_cb *block_cb, *next;

	lockdep_assert_held(&block->cb_lock);

	list_for_each_entry_safe(block_cb, next, &bo->cb_list, list) {
		tcf_block_playback_offloads(block, block_cb->cb,
					    block_cb->cb_priv, false,
					    tcf_block_offload_in_use(block),
					    NULL);
		list_del(&block_cb->list);
		flow_block_cb_free(block_cb);
		if (!bo->unlocked_driver_cb)
			block->lockeddevcnt--;
	}
}

static int tcf_block_setup(struct tcf_block *block,
			   struct flow_block_offload *bo)
{
	int err;

	switch (bo->command) {
	case FLOW_BLOCK_BIND:
		err = tcf_block_bind(block, bo);
		break;
	case FLOW_BLOCK_UNBIND:
		err = 0;
		tcf_block_unbind(block, bo);
		break;
	default:
		WARN_ON_ONCE(1);
		err = -EOPNOTSUPP;
	}

	return err;
}

/* Main classifier routine: scans classifier chain attached
 * to this qdisc, (optionally) tests for protocol and asks
 * specific classifiers.
 */
static inline int __tcf_classify(struct sk_buff *skb,
				 const struct tcf_proto *tp,
				 const struct tcf_proto *orig_tp,
				 struct tcf_result *res,
				 bool compat_mode,
				 u32 *last_executed_chain)
{
#ifdef CONFIG_NET_CLS_ACT
	const int max_reclassify_loop = 16;
	const struct tcf_proto *first_tp;
	int limit = 0;

reclassify:
#endif
	for (; tp; tp = rcu_dereference_bh(tp->next)) {
		__be16 protocol = skb_protocol(skb, false);
		int err;

		if (tp->protocol != protocol &&
		    tp->protocol != htons(ETH_P_ALL))
			continue;

		err = tp->classify(skb, tp, res);
#ifdef CONFIG_NET_CLS_ACT
		if (unlikely(err == TC_ACT_RECLASSIFY && !compat_mode)) {
			first_tp = orig_tp;
			*last_executed_chain = first_tp->chain->index;
			goto reset;
		} else if (unlikely(TC_ACT_EXT_CMP(err, TC_ACT_GOTO_CHAIN))) {
			first_tp = res->goto_tp;
			*last_executed_chain = err & TC_ACT_EXT_VAL_MASK;
			goto reset;
		}
#endif
		if (err >= 0)
			return err;
	}

	return TC_ACT_UNSPEC; /* signal: continue lookup */
#ifdef CONFIG_NET_CLS_ACT
reset:
	if (unlikely(limit++ >= max_reclassify_loop)) {
		net_notice_ratelimited("%u: reclassify loop, rule prio %u, protocol %02x\n",
				       tp->chain->block->index,
				       tp->prio & 0xffff,
				       ntohs(tp->protocol));
		return TC_ACT_SHOT;
	}

	tp = first_tp;
	goto reclassify;
#endif
}

int tcf_classify(struct sk_buff *skb,
		 const struct tcf_block *block,
		 const struct tcf_proto *tp,
		 struct tcf_result *res, bool compat_mode)
{
#if !IS_ENABLED(CONFIG_NET_TC_SKB_EXT)
	u32 last_executed_chain = 0;

	return __tcf_classify(skb, tp, tp, res, compat_mode,
			      &last_executed_chain);
#else
	u32 last_executed_chain = tp ? tp->chain->index : 0;
	const struct tcf_proto *orig_tp = tp;
	struct tc_skb_ext *ext;
	int ret;

	if (block) {
		ext = skb_ext_find(skb, TC_SKB_EXT);

		if (ext && ext->chain) {
			struct tcf_chain *fchain;

			fchain = tcf_chain_lookup_rcu(block, ext->chain);
			if (!fchain)
				return TC_ACT_SHOT;

			/* Consume, so cloned/redirect skbs won't inherit ext */
			skb_ext_del(skb, TC_SKB_EXT);

			tp = rcu_dereference_bh(fchain->filter_chain);
			last_executed_chain = fchain->index;
		}
	}

	ret = __tcf_classify(skb, tp, orig_tp, res, compat_mode,
			     &last_executed_chain);

	if (tc_skb_ext_tc_enabled()) {
		/* If we missed on some chain */
		if (ret == TC_ACT_UNSPEC && last_executed_chain) {
			struct tc_skb_cb *cb = tc_skb_cb(skb);

			ext = tc_skb_ext_alloc(skb);
			if (WARN_ON_ONCE(!ext))
				return TC_ACT_SHOT;
			ext->chain = last_executed_chain;
			ext->mru = cb->mru;
			ext->post_ct = cb->post_ct;
			ext->post_ct_snat = cb->post_ct_snat;
			ext->post_ct_dnat = cb->post_ct_dnat;
			ext->zone = cb->zone;
		}
	}

	return ret;
#endif
}
EXPORT_SYMBOL(tcf_classify);

struct tcf_chain_info {
	struct tcf_proto __rcu **pprev;
	struct tcf_proto __rcu *next;
};

static struct tcf_proto *tcf_chain_tp_prev(struct tcf_chain *chain,
					   struct tcf_chain_info *chain_info)
{
	return tcf_chain_dereference(*chain_info->pprev, chain);
}

static int tcf_chain_tp_insert(struct tcf_chain *chain,
			       struct tcf_chain_info *chain_info,
			       struct tcf_proto *tp)
{
	if (chain->flushing)
		return -EAGAIN;

	RCU_INIT_POINTER(tp->next, tcf_chain_tp_prev(chain, chain_info));
	if (*chain_info->pprev == chain->filter_chain)
		tcf_chain0_head_change(chain, tp);
	tcf_proto_get(tp);
	rcu_assign_pointer(*chain_info->pprev, tp);

	return 0;
}

static void tcf_chain_tp_remove(struct tcf_chain *chain,
				struct tcf_chain_info *chain_info,
				struct tcf_proto *tp)
{
	struct tcf_proto *next = tcf_chain_dereference(chain_info->next, chain);

	tcf_proto_mark_delete(tp);
	if (tp == chain->filter_chain)
		tcf_chain0_head_change(chain, next);
	RCU_INIT_POINTER(*chain_info->pprev, next);
}

static struct tcf_proto *tcf_chain_tp_find(struct tcf_chain *chain,
					   struct tcf_chain_info *chain_info,
					   u32 protocol, u32 prio,
					   bool prio_allocate);

/* Try to insert new proto.
 * If proto with specified priority already exists, free new proto
 * and return existing one.
 */

static struct tcf_proto *tcf_chain_tp_insert_unique(struct tcf_chain *chain,
						    struct tcf_proto *tp_new,
						    u32 protocol, u32 prio,
						    bool rtnl_held)
{
	struct tcf_chain_info chain_info;
	struct tcf_proto *tp;
	int err = 0;

	mutex_lock(&chain->filter_chain_lock);

	if (tcf_proto_exists_destroying(chain, tp_new)) {
		mutex_unlock(&chain->filter_chain_lock);
		tcf_proto_destroy(tp_new, rtnl_held, false, NULL);
		return ERR_PTR(-EAGAIN);
	}

	tp = tcf_chain_tp_find(chain, &chain_info,
			       protocol, prio, false);
	if (!tp)
		err = tcf_chain_tp_insert(chain, &chain_info, tp_new);
	mutex_unlock(&chain->filter_chain_lock);

	if (tp) {
		tcf_proto_destroy(tp_new, rtnl_held, false, NULL);
		tp_new = tp;
	} else if (err) {
		tcf_proto_destroy(tp_new, rtnl_held, false, NULL);
		tp_new = ERR_PTR(err);
	}

	return tp_new;
}

static void tcf_chain_tp_delete_empty(struct tcf_chain *chain,
				      struct tcf_proto *tp, bool rtnl_held,
				      struct netlink_ext_ack *extack)
{
	struct tcf_chain_info chain_info;
	struct tcf_proto *tp_iter;
	struct tcf_proto **pprev;
	struct tcf_proto *next;

	mutex_lock(&chain->filter_chain_lock);

	/* Atomically find and remove tp from chain. */
	for (pprev = &chain->filter_chain;
	     (tp_iter = tcf_chain_dereference(*pprev, chain));
	     pprev = &tp_iter->next) {
		if (tp_iter == tp) {
			chain_info.pprev = pprev;
			chain_info.next = tp_iter->next;
			WARN_ON(tp_iter->deleting);
			break;
		}
	}
	/* Verify that tp still exists and no new filters were inserted
	 * concurrently.
	 * Mark tp for deletion if it is empty.
	 */
	if (!tp_iter || !tcf_proto_check_delete(tp)) {
		mutex_unlock(&chain->filter_chain_lock);
		return;
	}

	tcf_proto_signal_destroying(chain, tp);
	next = tcf_chain_dereference(chain_info.next, chain);
	if (tp == chain->filter_chain)
		tcf_chain0_head_change(chain, next);
	RCU_INIT_POINTER(*chain_info.pprev, next);
	mutex_unlock(&chain->filter_chain_lock);

	tcf_proto_put(tp, rtnl_held, extack);
}

static struct tcf_proto *tcf_chain_tp_find(struct tcf_chain *chain,
					   struct tcf_chain_info *chain_info,
					   u32 protocol, u32 prio,
					   bool prio_allocate)
{
	struct tcf_proto **pprev;
	struct tcf_proto *tp;

	/* Check the chain for existence of proto-tcf with this priority */
	for (pprev = &chain->filter_chain;
	     (tp = tcf_chain_dereference(*pprev, chain));
	     pprev = &tp->next) {
		if (tp->prio >= prio) {
			if (tp->prio == prio) {
				if (prio_allocate ||
				    (tp->protocol != protocol && protocol))
					return ERR_PTR(-EINVAL);
			} else {
				tp = NULL;
			}
			break;
		}
	}
	chain_info->pprev = pprev;
	if (tp) {
		chain_info->next = tp->next;
		tcf_proto_get(tp);
	} else {
		chain_info->next = NULL;
	}
	return tp;
}

static int tcf_fill_node(struct net *net, struct sk_buff *skb,
			 struct tcf_proto *tp, struct tcf_block *block,
			 struct Qdisc *q, u32 parent, void *fh,
			 u32 portid, u32 seq, u16 flags, int event,
			 bool terse_dump, bool rtnl_held)
{
	struct tcmsg *tcm;
	struct nlmsghdr  *nlh;
	unsigned char *b = skb_tail_pointer(skb);

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*tcm), flags);
	if (!nlh)
		goto out_nlmsg_trim;
	tcm = nlmsg_data(nlh);
	tcm->tcm_family = AF_UNSPEC;
	tcm->tcm__pad1 = 0;
	tcm->tcm__pad2 = 0;
	if (q) {
		tcm->tcm_ifindex = qdisc_dev(q)->ifindex;
		tcm->tcm_parent = parent;
	} else {
		tcm->tcm_ifindex = TCM_IFINDEX_MAGIC_BLOCK;
		tcm->tcm_block_index = block->index;
	}
	tcm->tcm_info = TC_H_MAKE(tp->prio, tp->protocol);
	if (nla_put_string(skb, TCA_KIND, tp->ops->kind))
		goto nla_put_failure;
	if (nla_put_u32(skb, TCA_CHAIN, tp->chain->index))
		goto nla_put_failure;
	if (!fh) {
		tcm->tcm_handle = 0;
	} else if (terse_dump) {
		if (tp->ops->terse_dump) {
			if (tp->ops->terse_dump(net, tp, fh, skb, tcm,
						rtnl_held) < 0)
				goto nla_put_failure;
		} else {
			goto cls_op_not_supp;
		}
	} else {
		if (tp->ops->dump &&
		    tp->ops->dump(net, tp, fh, skb, tcm, rtnl_held) < 0)
			goto nla_put_failure;
	}
	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

out_nlmsg_trim:
nla_put_failure:
cls_op_not_supp:
	nlmsg_trim(skb, b);
	return -1;
}

static int tfilter_notify(struct net *net, struct sk_buff *oskb,
			  struct nlmsghdr *n, struct tcf_proto *tp,
			  struct tcf_block *block, struct Qdisc *q,
			  u32 parent, void *fh, int event, bool unicast,
			  bool rtnl_held)
{
	struct sk_buff *skb;
	u32 portid = oskb ? NETLINK_CB(oskb).portid : 0;
	int err = 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tcf_fill_node(net, skb, tp, block, q, parent, fh, portid,
			  n->nlmsg_seq, n->nlmsg_flags, event,
			  false, rtnl_held) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	if (unicast)
		err = rtnl_unicast(skb, net, portid);
	else
		err = rtnetlink_send(skb, net, portid, RTNLGRP_TC,
				     n->nlmsg_flags & NLM_F_ECHO);
	return err;
}

static int tfilter_del_notify(struct net *net, struct sk_buff *oskb,
			      struct nlmsghdr *n, struct tcf_proto *tp,
			      struct tcf_block *block, struct Qdisc *q,
			      u32 parent, void *fh, bool unicast, bool *last,
			      bool rtnl_held, struct netlink_ext_ack *extack)
{
	struct sk_buff *skb;
	u32 portid = oskb ? NETLINK_CB(oskb).portid : 0;
	int err;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tcf_fill_node(net, skb, tp, block, q, parent, fh, portid,
			  n->nlmsg_seq, n->nlmsg_flags, RTM_DELTFILTER,
			  false, rtnl_held) <= 0) {
		NL_SET_ERR_MSG(extack, "Failed to build del event notification");
		kfree_skb(skb);
		return -EINVAL;
	}

	err = tp->ops->delete(tp, fh, last, rtnl_held, extack);
	if (err) {
		kfree_skb(skb);
		return err;
	}

	if (unicast)
		err = rtnl_unicast(skb, net, portid);
	else
		err = rtnetlink_send(skb, net, portid, RTNLGRP_TC,
				     n->nlmsg_flags & NLM_F_ECHO);
	if (err < 0)
		NL_SET_ERR_MSG(extack, "Failed to send filter delete notification");

	return err;
}

static void tfilter_notify_chain(struct net *net, struct sk_buff *oskb,
				 struct tcf_block *block, struct Qdisc *q,
				 u32 parent, struct nlmsghdr *n,
				 struct tcf_chain *chain, int event)
{
	struct tcf_proto *tp;

	for (tp = tcf_get_next_proto(chain, NULL);
	     tp; tp = tcf_get_next_proto(chain, tp))
		tfilter_notify(net, oskb, n, tp, block,
			       q, parent, NULL, event, false, true);
}

static void tfilter_put(struct tcf_proto *tp, void *fh)
{
	if (tp->ops->put && fh)
		tp->ops->put(tp, fh);
}

static int tc_new_tfilter(struct sk_buff *skb, struct nlmsghdr *n,
			  struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_MAX + 1];
	char name[IFNAMSIZ];
	struct tcmsg *t;
	u32 protocol;
	u32 prio;
	bool prio_allocate;
	u32 parent;
	u32 chain_index;
	struct Qdisc *q;
	struct tcf_chain_info chain_info;
	struct tcf_chain *chain;
	struct tcf_block *block;
	struct tcf_proto *tp;
	unsigned long cl;
	void *fh;
	int err;
	int tp_created;
	bool rtnl_held = false;
	u32 flags;

	if (!netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

replay:
	tp_created = 0;

	err = nlmsg_parse_deprecated(n, sizeof(*t), tca, TCA_MAX,
				     rtm_tca_policy, extack);
	if (err < 0)
		return err;

	t = nlmsg_data(n);
	protocol = TC_H_MIN(t->tcm_info);
	prio = TC_H_MAJ(t->tcm_info);
	prio_allocate = false;
	parent = t->tcm_parent;
	tp = NULL;
	cl = 0;
	block = NULL;
	q = NULL;
	chain = NULL;
	flags = 0;

	if (prio == 0) {
		/* If no priority is provided by the user,
		 * we allocate one.
		 */
		if (n->nlmsg_flags & NLM_F_CREATE) {
			prio = TC_H_MAKE(0x80000000U, 0U);
			prio_allocate = true;
		} else {
			NL_SET_ERR_MSG(extack, "Invalid filter command with priority of zero");
			return -ENOENT;
		}
	}

	/* Find head of filter chain. */

	err = __tcf_qdisc_find(net, &q, &parent, t->tcm_ifindex, false, extack);
	if (err)
		return err;

	if (tcf_proto_check_kind(tca[TCA_KIND], name)) {
		NL_SET_ERR_MSG(extack, "Specified TC filter name too long");
		err = -EINVAL;
		goto errout;
	}

	/* Take rtnl mutex if rtnl_held was set to true on previous iteration,
	 * block is shared (no qdisc found), qdisc is not unlocked, classifier
	 * type is not specified, classifier is not unlocked.
	 */
	if (rtnl_held ||
	    (q && !(q->ops->cl_ops->flags & QDISC_CLASS_OPS_DOIT_UNLOCKED)) ||
	    !tcf_proto_is_unlocked(name)) {
		rtnl_held = true;
		rtnl_lock();
	}

	err = __tcf_qdisc_cl_find(q, parent, &cl, t->tcm_ifindex, extack);
	if (err)
		goto errout;

	block = __tcf_block_find(net, q, cl, t->tcm_ifindex, t->tcm_block_index,
				 extack);
	if (IS_ERR(block)) {
		err = PTR_ERR(block);
		goto errout;
	}
	block->classid = parent;

	chain_index = tca[TCA_CHAIN] ? nla_get_u32(tca[TCA_CHAIN]) : 0;
	if (chain_index > TC_ACT_EXT_VAL_MASK) {
		NL_SET_ERR_MSG(extack, "Specified chain index exceeds upper limit");
		err = -EINVAL;
		goto errout;
	}
	chain = tcf_chain_get(block, chain_index, true);
	if (!chain) {
		NL_SET_ERR_MSG(extack, "Cannot create specified filter chain");
		err = -ENOMEM;
		goto errout;
	}

	mutex_lock(&chain->filter_chain_lock);
	tp = tcf_chain_tp_find(chain, &chain_info, protocol,
			       prio, prio_allocate);
	if (IS_ERR(tp)) {
		NL_SET_ERR_MSG(extack, "Filter with specified priority/protocol not found");
		err = PTR_ERR(tp);
		goto errout_locked;
	}

	if (tp == NULL) {
		struct tcf_proto *tp_new = NULL;

		if (chain->flushing) {
			err = -EAGAIN;
			goto errout_locked;
		}

		/* Proto-tcf does not exist, create new one */

		if (tca[TCA_KIND] == NULL || !protocol) {
			NL_SET_ERR_MSG(extack, "Filter kind and protocol must be specified");
			err = -EINVAL;
			goto errout_locked;
		}

		if (!(n->nlmsg_flags & NLM_F_CREATE)) {
			NL_SET_ERR_MSG(extack, "Need both RTM_NEWTFILTER and NLM_F_CREATE to create a new filter");
			err = -ENOENT;
			goto errout_locked;
		}

		if (prio_allocate)
			prio = tcf_auto_prio(tcf_chain_tp_prev(chain,
							       &chain_info));

		mutex_unlock(&chain->filter_chain_lock);
		tp_new = tcf_proto_create(name, protocol, prio, chain,
					  rtnl_held, extack);
		if (IS_ERR(tp_new)) {
			err = PTR_ERR(tp_new);
			goto errout_tp;
		}

		tp_created = 1;
		tp = tcf_chain_tp_insert_unique(chain, tp_new, protocol, prio,
						rtnl_held);
		if (IS_ERR(tp)) {
			err = PTR_ERR(tp);
			goto errout_tp;
		}
	} else {
		mutex_unlock(&chain->filter_chain_lock);
	}

	if (tca[TCA_KIND] && nla_strcmp(tca[TCA_KIND], tp->ops->kind)) {
		NL_SET_ERR_MSG(extack, "Specified filter kind does not match existing one");
		err = -EINVAL;
		goto errout;
	}

	fh = tp->ops->get(tp, t->tcm_handle);

	if (!fh) {
		if (!(n->nlmsg_flags & NLM_F_CREATE)) {
			NL_SET_ERR_MSG(extack, "Need both RTM_NEWTFILTER and NLM_F_CREATE to create a new filter");
			err = -ENOENT;
			goto errout;
		}
	} else if (n->nlmsg_flags & NLM_F_EXCL) {
		tfilter_put(tp, fh);
		NL_SET_ERR_MSG(extack, "Filter already exists");
		err = -EEXIST;
		goto errout;
	}

	if (chain->tmplt_ops && chain->tmplt_ops != tp->ops) {
		NL_SET_ERR_MSG(extack, "Chain template is set to a different filter kind");
		err = -EINVAL;
		goto errout;
	}

	if (!(n->nlmsg_flags & NLM_F_CREATE))
		flags |= TCA_ACT_FLAGS_REPLACE;
	if (!rtnl_held)
		flags |= TCA_ACT_FLAGS_NO_RTNL;
	err = tp->ops->change(net, skb, tp, cl, t->tcm_handle, tca, &fh,
			      flags, extack);
	if (err == 0) {
		tfilter_notify(net, skb, n, tp, block, q, parent, fh,
			       RTM_NEWTFILTER, false, rtnl_held);
		tfilter_put(tp, fh);
		/* q pointer is NULL for shared blocks */
		if (q)
			q->flags &= ~TCQ_F_CAN_BYPASS;
	}

errout:
	if (err && tp_created)
		tcf_chain_tp_delete_empty(chain, tp, rtnl_held, NULL);
errout_tp:
	if (chain) {
		if (tp && !IS_ERR(tp))
			tcf_proto_put(tp, rtnl_held, NULL);
		if (!tp_created)
			tcf_chain_put(chain);
	}
	tcf_block_release(q, block, rtnl_held);

	if (rtnl_held)
		rtnl_unlock();

	if (err == -EAGAIN) {
		/* Take rtnl lock in case EAGAIN is caused by concurrent flush
		 * of target chain.
		 */
		rtnl_held = true;
		/* Replay the request. */
		goto replay;
	}
	return err;

errout_locked:
	mutex_unlock(&chain->filter_chain_lock);
	goto errout;
}

static int tc_del_tfilter(struct sk_buff *skb, struct nlmsghdr *n,
			  struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_MAX + 1];
	char name[IFNAMSIZ];
	struct tcmsg *t;
	u32 protocol;
	u32 prio;
	u32 parent;
	u32 chain_index;
	struct Qdisc *q = NULL;
	struct tcf_chain_info chain_info;
	struct tcf_chain *chain = NULL;
	struct tcf_block *block = NULL;
	struct tcf_proto *tp = NULL;
	unsigned long cl = 0;
	void *fh = NULL;
	int err;
	bool rtnl_held = false;

	if (!netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	err = nlmsg_parse_deprecated(n, sizeof(*t), tca, TCA_MAX,
				     rtm_tca_policy, extack);
	if (err < 0)
		return err;

	t = nlmsg_data(n);
	protocol = TC_H_MIN(t->tcm_info);
	prio = TC_H_MAJ(t->tcm_info);
	parent = t->tcm_parent;

	if (prio == 0 && (protocol || t->tcm_handle || tca[TCA_KIND])) {
		NL_SET_ERR_MSG(extack, "Cannot flush filters with protocol, handle or kind set");
		return -ENOENT;
	}

	/* Find head of filter chain. */

	err = __tcf_qdisc_find(net, &q, &parent, t->tcm_ifindex, false, extack);
	if (err)
		return err;

	if (tcf_proto_check_kind(tca[TCA_KIND], name)) {
		NL_SET_ERR_MSG(extack, "Specified TC filter name too long");
		err = -EINVAL;
		goto errout;
	}
	/* Take rtnl mutex if flushing whole chain, block is shared (no qdisc
	 * found), qdisc is not unlocked, classifier type is not specified,
	 * classifier is not unlocked.
	 */
	if (!prio ||
	    (q && !(q->ops->cl_ops->flags & QDISC_CLASS_OPS_DOIT_UNLOCKED)) ||
	    !tcf_proto_is_unlocked(name)) {
		rtnl_held = true;
		rtnl_lock();
	}

	err = __tcf_qdisc_cl_find(q, parent, &cl, t->tcm_ifindex, extack);
	if (err)
		goto errout;

	block = __tcf_block_find(net, q, cl, t->tcm_ifindex, t->tcm_block_index,
				 extack);
	if (IS_ERR(block)) {
		err = PTR_ERR(block);
		goto errout;
	}

	chain_index = tca[TCA_CHAIN] ? nla_get_u32(tca[TCA_CHAIN]) : 0;
	if (chain_index > TC_ACT_EXT_VAL_MASK) {
		NL_SET_ERR_MSG(extack, "Specified chain index exceeds upper limit");
		err = -EINVAL;
		goto errout;
	}
	chain = tcf_chain_get(block, chain_index, false);
	if (!chain) {
		/* User requested flush on non-existent chain. Nothing to do,
		 * so just return success.
		 */
		if (prio == 0) {
			err = 0;
			goto errout;
		}
		NL_SET_ERR_MSG(extack, "Cannot find specified filter chain");
		err = -ENOENT;
		goto errout;
	}

	if (prio == 0) {
		tfilter_notify_chain(net, skb, block, q, parent, n,
				     chain, RTM_DELTFILTER);
		tcf_chain_flush(chain, rtnl_held);
		err = 0;
		goto errout;
	}

	mutex_lock(&chain->filter_chain_lock);
	tp = tcf_chain_tp_find(chain, &chain_info, protocol,
			       prio, false);
	if (!tp || IS_ERR(tp)) {
		NL_SET_ERR_MSG(extack, "Filter with specified priority/protocol not found");
		err = tp ? PTR_ERR(tp) : -ENOENT;
		goto errout_locked;
	} else if (tca[TCA_KIND] && nla_strcmp(tca[TCA_KIND], tp->ops->kind)) {
		NL_SET_ERR_MSG(extack, "Specified filter kind does not match existing one");
		err = -EINVAL;
		goto errout_locked;
	} else if (t->tcm_handle == 0) {
		tcf_proto_signal_destroying(chain, tp);
		tcf_chain_tp_remove(chain, &chain_info, tp);
		mutex_unlock(&chain->filter_chain_lock);

		tcf_proto_put(tp, rtnl_held, NULL);
		tfilter_notify(net, skb, n, tp, block, q, parent, fh,
			       RTM_DELTFILTER, false, rtnl_held);
		err = 0;
		goto errout;
	}
	mutex_unlock(&chain->filter_chain_lock);

	fh = tp->ops->get(tp, t->tcm_handle);

	if (!fh) {
		NL_SET_ERR_MSG(extack, "Specified filter handle not found");
		err = -ENOENT;
	} else {
		bool last;

		err = tfilter_del_notify(net, skb, n, tp, block,
					 q, parent, fh, false, &last,
					 rtnl_held, extack);

		if (err)
			goto errout;
		if (last)
			tcf_chain_tp_delete_empty(chain, tp, rtnl_held, extack);
	}

errout:
	if (chain) {
		if (tp && !IS_ERR(tp))
			tcf_proto_put(tp, rtnl_held, NULL);
		tcf_chain_put(chain);
	}
	tcf_block_release(q, block, rtnl_held);

	if (rtnl_held)
		rtnl_unlock();

	return err;

errout_locked:
	mutex_unlock(&chain->filter_chain_lock);
	goto errout;
}

static int tc_get_tfilter(struct sk_buff *skb, struct nlmsghdr *n,
			  struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_MAX + 1];
	char name[IFNAMSIZ];
	struct tcmsg *t;
	u32 protocol;
	u32 prio;
	u32 parent;
	u32 chain_index;
	struct Qdisc *q = NULL;
	struct tcf_chain_info chain_info;
	struct tcf_chain *chain = NULL;
	struct tcf_block *block = NULL;
	struct tcf_proto *tp = NULL;
	unsigned long cl = 0;
	void *fh = NULL;
	int err;
	bool rtnl_held = false;

	err = nlmsg_parse_deprecated(n, sizeof(*t), tca, TCA_MAX,
				     rtm_tca_policy, extack);
	if (err < 0)
		return err;

	t = nlmsg_data(n);
	protocol = TC_H_MIN(t->tcm_info);
	prio = TC_H_MAJ(t->tcm_info);
	parent = t->tcm_parent;

	if (prio == 0) {
		NL_SET_ERR_MSG(extack, "Invalid filter command with priority of zero");
		return -ENOENT;
	}

	/* Find head of filter chain. */

	err = __tcf_qdisc_find(net, &q, &parent, t->tcm_ifindex, false, extack);
	if (err)
		return err;

	if (tcf_proto_check_kind(tca[TCA_KIND], name)) {
		NL_SET_ERR_MSG(extack, "Specified TC filter name too long");
		err = -EINVAL;
		goto errout;
	}
	/* Take rtnl mutex if block is shared (no qdisc found), qdisc is not
	 * unlocked, classifier type is not specified, classifier is not
	 * unlocked.
	 */
	if ((q && !(q->ops->cl_ops->flags & QDISC_CLASS_OPS_DOIT_UNLOCKED)) ||
	    !tcf_proto_is_unlocked(name)) {
		rtnl_held = true;
		rtnl_lock();
	}

	err = __tcf_qdisc_cl_find(q, parent, &cl, t->tcm_ifindex, extack);
	if (err)
		goto errout;

	block = __tcf_block_find(net, q, cl, t->tcm_ifindex, t->tcm_block_index,
				 extack);
	if (IS_ERR(block)) {
		err = PTR_ERR(block);
		goto errout;
	}

	chain_index = tca[TCA_CHAIN] ? nla_get_u32(tca[TCA_CHAIN]) : 0;
	if (chain_index > TC_ACT_EXT_VAL_MASK) {
		NL_SET_ERR_MSG(extack, "Specified chain index exceeds upper limit");
		err = -EINVAL;
		goto errout;
	}
	chain = tcf_chain_get(block, chain_index, false);
	if (!chain) {
		NL_SET_ERR_MSG(extack, "Cannot find specified filter chain");
		err = -EINVAL;
		goto errout;
	}

	mutex_lock(&chain->filter_chain_lock);
	tp = tcf_chain_tp_find(chain, &chain_info, protocol,
			       prio, false);
	mutex_unlock(&chain->filter_chain_lock);
	if (!tp || IS_ERR(tp)) {
		NL_SET_ERR_MSG(extack, "Filter with specified priority/protocol not found");
		err = tp ? PTR_ERR(tp) : -ENOENT;
		goto errout;
	} else if (tca[TCA_KIND] && nla_strcmp(tca[TCA_KIND], tp->ops->kind)) {
		NL_SET_ERR_MSG(extack, "Specified filter kind does not match existing one");
		err = -EINVAL;
		goto errout;
	}

	fh = tp->ops->get(tp, t->tcm_handle);

	if (!fh) {
		NL_SET_ERR_MSG(extack, "Specified filter handle not found");
		err = -ENOENT;
	} else {
		err = tfilter_notify(net, skb, n, tp, block, q, parent,
				     fh, RTM_NEWTFILTER, true, rtnl_held);
		if (err < 0)
			NL_SET_ERR_MSG(extack, "Failed to send filter notify message");
	}

	tfilter_put(tp, fh);
errout:
	if (chain) {
		if (tp && !IS_ERR(tp))
			tcf_proto_put(tp, rtnl_held, NULL);
		tcf_chain_put(chain);
	}
	tcf_block_release(q, block, rtnl_held);

	if (rtnl_held)
		rtnl_unlock();

	return err;
}

struct tcf_dump_args {
	struct tcf_walker w;
	struct sk_buff *skb;
	struct netlink_callback *cb;
	struct tcf_block *block;
	struct Qdisc *q;
	u32 parent;
	bool terse_dump;
};

static int tcf_node_dump(struct tcf_proto *tp, void *n, struct tcf_walker *arg)
{
	struct tcf_dump_args *a = (void *)arg;
	struct net *net = sock_net(a->skb->sk);

	return tcf_fill_node(net, a->skb, tp, a->block, a->q, a->parent,
			     n, NETLINK_CB(a->cb->skb).portid,
			     a->cb->nlh->nlmsg_seq, NLM_F_MULTI,
			     RTM_NEWTFILTER, a->terse_dump, true);
}

static bool tcf_chain_dump(struct tcf_chain *chain, struct Qdisc *q, u32 parent,
			   struct sk_buff *skb, struct netlink_callback *cb,
			   long index_start, long *p_index, bool terse)
{
	struct net *net = sock_net(skb->sk);
	struct tcf_block *block = chain->block;
	struct tcmsg *tcm = nlmsg_data(cb->nlh);
	struct tcf_proto *tp, *tp_prev;
	struct tcf_dump_args arg;

	for (tp = __tcf_get_next_proto(chain, NULL);
	     tp;
	     tp_prev = tp,
		     tp = __tcf_get_next_proto(chain, tp),
		     tcf_proto_put(tp_prev, true, NULL),
		     (*p_index)++) {
		if (*p_index < index_start)
			continue;
		if (TC_H_MAJ(tcm->tcm_info) &&
		    TC_H_MAJ(tcm->tcm_info) != tp->prio)
			continue;
		if (TC_H_MIN(tcm->tcm_info) &&
		    TC_H_MIN(tcm->tcm_info) != tp->protocol)
			continue;
		if (*p_index > index_start)
			memset(&cb->args[1], 0,
			       sizeof(cb->args) - sizeof(cb->args[0]));
		if (cb->args[1] == 0) {
			if (tcf_fill_node(net, skb, tp, block, q, parent, NULL,
					  NETLINK_CB(cb->skb).portid,
					  cb->nlh->nlmsg_seq, NLM_F_MULTI,
					  RTM_NEWTFILTER, false, true) <= 0)
				goto errout;
			cb->args[1] = 1;
		}
		if (!tp->ops->walk)
			continue;
		arg.w.fn = tcf_node_dump;
		arg.skb = skb;
		arg.cb = cb;
		arg.block = block;
		arg.q = q;
		arg.parent = parent;
		arg.w.stop = 0;
		arg.w.skip = cb->args[1] - 1;
		arg.w.count = 0;
		arg.w.cookie = cb->args[2];
		arg.terse_dump = terse;
		tp->ops->walk(tp, &arg.w, true);
		cb->args[2] = arg.w.cookie;
		cb->args[1] = arg.w.count + 1;
		if (arg.w.stop)
			goto errout;
	}
	return true;

errout:
	tcf_proto_put(tp, true, NULL);
	return false;
}

static const struct nla_policy tcf_tfilter_dump_policy[TCA_MAX + 1] = {
	[TCA_DUMP_FLAGS] = NLA_POLICY_BITFIELD32(TCA_DUMP_FLAGS_TERSE),
};

/* called with RTNL */
static int tc_dump_tfilter(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct tcf_chain *chain, *chain_prev;
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_MAX + 1];
	struct Qdisc *q = NULL;
	struct tcf_block *block;
	struct tcmsg *tcm = nlmsg_data(cb->nlh);
	bool terse_dump = false;
	long index_start;
	long index;
	u32 parent;
	int err;

	if (nlmsg_len(cb->nlh) < sizeof(*tcm))
		return skb->len;

	err = nlmsg_parse_deprecated(cb->nlh, sizeof(*tcm), tca, TCA_MAX,
				     tcf_tfilter_dump_policy, cb->extack);
	if (err)
		return err;

	if (tca[TCA_DUMP_FLAGS]) {
		struct nla_bitfield32 flags =
			nla_get_bitfield32(tca[TCA_DUMP_FLAGS]);

		terse_dump = flags.value & TCA_DUMP_FLAGS_TERSE;
	}

	if (tcm->tcm_ifindex == TCM_IFINDEX_MAGIC_BLOCK) {
		block = tcf_block_refcnt_get(net, tcm->tcm_block_index);
		if (!block)
			goto out;
		/* If we work with block index, q is NULL and parent value
		 * will never be used in the following code. The check
		 * in tcf_fill_node prevents it. However, compiler does not
		 * see that far, so set parent to zero to silence the warning
		 * about parent being uninitialized.
		 */
		parent = 0;
	} else {
		const struct Qdisc_class_ops *cops;
		struct net_device *dev;
		unsigned long cl = 0;

		dev = __dev_get_by_index(net, tcm->tcm_ifindex);
		if (!dev)
			return skb->len;

		parent = tcm->tcm_parent;
		if (!parent)
			q = rtnl_dereference(dev->qdisc);
		else
			q = qdisc_lookup(dev, TC_H_MAJ(tcm->tcm_parent));
		if (!q)
			goto out;
		cops = q->ops->cl_ops;
		if (!cops)
			goto out;
		if (!cops->tcf_block)
			goto out;
		if (TC_H_MIN(tcm->tcm_parent)) {
			cl = cops->find(q, tcm->tcm_parent);
			if (cl == 0)
				goto out;
		}
		block = cops->tcf_block(q, cl, NULL);
		if (!block)
			goto out;
		parent = block->classid;
		if (tcf_block_shared(block))
			q = NULL;
	}

	index_start = cb->args[0];
	index = 0;

	for (chain = __tcf_get_next_chain(block, NULL);
	     chain;
	     chain_prev = chain,
		     chain = __tcf_get_next_chain(block, chain),
		     tcf_chain_put(chain_prev)) {
		if (tca[TCA_CHAIN] &&
		    nla_get_u32(tca[TCA_CHAIN]) != chain->index)
			continue;
		if (!tcf_chain_dump(chain, q, parent, skb, cb,
				    index_start, &index, terse_dump)) {
			tcf_chain_put(chain);
			err = -EMSGSIZE;
			break;
		}
	}

	if (tcm->tcm_ifindex == TCM_IFINDEX_MAGIC_BLOCK)
		tcf_block_refcnt_put(block, true);
	cb->args[0] = index;

out:
	/* If we did no progress, the error (EMSGSIZE) is real */
	if (skb->len == 0 && err)
		return err;
	return skb->len;
}

static int tc_chain_fill_node(const struct tcf_proto_ops *tmplt_ops,
			      void *tmplt_priv, u32 chain_index,
			      struct net *net, struct sk_buff *skb,
			      struct tcf_block *block,
			      u32 portid, u32 seq, u16 flags, int event)
{
	unsigned char *b = skb_tail_pointer(skb);
	const struct tcf_proto_ops *ops;
	struct nlmsghdr *nlh;
	struct tcmsg *tcm;
	void *priv;

	ops = tmplt_ops;
	priv = tmplt_priv;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*tcm), flags);
	if (!nlh)
		goto out_nlmsg_trim;
	tcm = nlmsg_data(nlh);
	tcm->tcm_family = AF_UNSPEC;
	tcm->tcm__pad1 = 0;
	tcm->tcm__pad2 = 0;
	tcm->tcm_handle = 0;
	if (block->q) {
		tcm->tcm_ifindex = qdisc_dev(block->q)->ifindex;
		tcm->tcm_parent = block->q->handle;
	} else {
		tcm->tcm_ifindex = TCM_IFINDEX_MAGIC_BLOCK;
		tcm->tcm_block_index = block->index;
	}

	if (nla_put_u32(skb, TCA_CHAIN, chain_index))
		goto nla_put_failure;

	if (ops) {
		if (nla_put_string(skb, TCA_KIND, ops->kind))
			goto nla_put_failure;
		if (ops->tmplt_dump(skb, net, priv) < 0)
			goto nla_put_failure;
	}

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

out_nlmsg_trim:
nla_put_failure:
	nlmsg_trim(skb, b);
	return -EMSGSIZE;
}

static int tc_chain_notify(struct tcf_chain *chain, struct sk_buff *oskb,
			   u32 seq, u16 flags, int event, bool unicast)
{
	u32 portid = oskb ? NETLINK_CB(oskb).portid : 0;
	struct tcf_block *block = chain->block;
	struct net *net = block->net;
	struct sk_buff *skb;
	int err = 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tc_chain_fill_node(chain->tmplt_ops, chain->tmplt_priv,
			       chain->index, net, skb, block, portid,
			       seq, flags, event) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	if (unicast)
		err = rtnl_unicast(skb, net, portid);
	else
		err = rtnetlink_send(skb, net, portid, RTNLGRP_TC,
				     flags & NLM_F_ECHO);

	return err;
}

static int tc_chain_notify_delete(const struct tcf_proto_ops *tmplt_ops,
				  void *tmplt_priv, u32 chain_index,
				  struct tcf_block *block, struct sk_buff *oskb,
				  u32 seq, u16 flags, bool unicast)
{
	u32 portid = oskb ? NETLINK_CB(oskb).portid : 0;
	struct net *net = block->net;
	struct sk_buff *skb;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tc_chain_fill_node(tmplt_ops, tmplt_priv, chain_index, net, skb,
			       block, portid, seq, flags, RTM_DELCHAIN) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	if (unicast)
		return rtnl_unicast(skb, net, portid);

	return rtnetlink_send(skb, net, portid, RTNLGRP_TC, flags & NLM_F_ECHO);
}

static int tc_chain_tmplt_add(struct tcf_chain *chain, struct net *net,
			      struct nlattr **tca,
			      struct netlink_ext_ack *extack)
{
	const struct tcf_proto_ops *ops;
	char name[IFNAMSIZ];
	void *tmplt_priv;

	/* If kind is not set, user did not specify template. */
	if (!tca[TCA_KIND])
		return 0;

	if (tcf_proto_check_kind(tca[TCA_KIND], name)) {
		NL_SET_ERR_MSG(extack, "Specified TC chain template name too long");
		return -EINVAL;
	}

	ops = tcf_proto_lookup_ops(name, true, extack);
	if (IS_ERR(ops))
		return PTR_ERR(ops);
	if (!ops->tmplt_create || !ops->tmplt_destroy || !ops->tmplt_dump) {
		NL_SET_ERR_MSG(extack, "Chain templates are not supported with specified classifier");
		return -EOPNOTSUPP;
	}

	tmplt_priv = ops->tmplt_create(net, chain, tca, extack);
	if (IS_ERR(tmplt_priv)) {
		module_put(ops->owner);
		return PTR_ERR(tmplt_priv);
	}
	chain->tmplt_ops = ops;
	chain->tmplt_priv = tmplt_priv;
	return 0;
}

static void tc_chain_tmplt_del(const struct tcf_proto_ops *tmplt_ops,
			       void *tmplt_priv)
{
	/* If template ops are set, no work to do for us. */
	if (!tmplt_ops)
		return;

	tmplt_ops->tmplt_destroy(tmplt_priv);
	module_put(tmplt_ops->owner);
}

/* Add/delete/get a chain */

static int tc_ctl_chain(struct sk_buff *skb, struct nlmsghdr *n,
			struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_MAX + 1];
	struct tcmsg *t;
	u32 parent;
	u32 chain_index;
	struct Qdisc *q;
	struct tcf_chain *chain;
	struct tcf_block *block;
	unsigned long cl;
	int err;

	if (n->nlmsg_type != RTM_GETCHAIN &&
	    !netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

replay:
	q = NULL;
	err = nlmsg_parse_deprecated(n, sizeof(*t), tca, TCA_MAX,
				     rtm_tca_policy, extack);
	if (err < 0)
		return err;

	t = nlmsg_data(n);
	parent = t->tcm_parent;
	cl = 0;

	block = tcf_block_find(net, &q, &parent, &cl,
			       t->tcm_ifindex, t->tcm_block_index, extack);
	if (IS_ERR(block))
		return PTR_ERR(block);

	chain_index = tca[TCA_CHAIN] ? nla_get_u32(tca[TCA_CHAIN]) : 0;
	if (chain_index > TC_ACT_EXT_VAL_MASK) {
		NL_SET_ERR_MSG(extack, "Specified chain index exceeds upper limit");
		err = -EINVAL;
		goto errout_block;
	}

	mutex_lock(&block->lock);
	chain = tcf_chain_lookup(block, chain_index);
	if (n->nlmsg_type == RTM_NEWCHAIN) {
		if (chain) {
			if (tcf_chain_held_by_acts_only(chain)) {
				/* The chain exists only because there is
				 * some action referencing it.
				 */
				tcf_chain_hold(chain);
			} else {
				NL_SET_ERR_MSG(extack, "Filter chain already exists");
				err = -EEXIST;
				goto errout_block_locked;
			}
		} else {
			if (!(n->nlmsg_flags & NLM_F_CREATE)) {
				NL_SET_ERR_MSG(extack, "Need both RTM_NEWCHAIN and NLM_F_CREATE to create a new chain");
				err = -ENOENT;
				goto errout_block_locked;
			}
			chain = tcf_chain_create(block, chain_index);
			if (!chain) {
				NL_SET_ERR_MSG(extack, "Failed to create filter chain");
				err = -ENOMEM;
				goto errout_block_locked;
			}
		}
	} else {
		if (!chain || tcf_chain_held_by_acts_only(chain)) {
			NL_SET_ERR_MSG(extack, "Cannot find specified filter chain");
			err = -EINVAL;
			goto errout_block_locked;
		}
		tcf_chain_hold(chain);
	}

	if (n->nlmsg_type == RTM_NEWCHAIN) {
		/* Modifying chain requires holding parent block lock. In case
		 * the chain was successfully added, take a reference to the
		 * chain. This ensures that an empty chain does not disappear at
		 * the end of this function.
		 */
		tcf_chain_hold(chain);
		chain->explicitly_created = true;
	}
	mutex_unlock(&block->lock);

	switch (n->nlmsg_type) {
	case RTM_NEWCHAIN:
		err = tc_chain_tmplt_add(chain, net, tca, extack);
		if (err) {
			tcf_chain_put_explicitly_created(chain);
			goto errout;
		}

		tc_chain_notify(chain, NULL, 0, NLM_F_CREATE | NLM_F_EXCL,
				RTM_NEWCHAIN, false);
		break;
	case RTM_DELCHAIN:
		tfilter_notify_chain(net, skb, block, q, parent, n,
				     chain, RTM_DELTFILTER);
		/* Flush the chain first as the user requested chain removal. */
		tcf_chain_flush(chain, true);
		/* In case the chain was successfully deleted, put a reference
		 * to the chain previously taken during addition.
		 */
		tcf_chain_put_explicitly_created(chain);
		break;
	case RTM_GETCHAIN:
		err = tc_chain_notify(chain, skb, n->nlmsg_seq,
				      n->nlmsg_flags, n->nlmsg_type, true);
		if (err < 0)
			NL_SET_ERR_MSG(extack, "Failed to send chain notify message");
		break;
	default:
		err = -EOPNOTSUPP;
		NL_SET_ERR_MSG(extack, "Unsupported message type");
		goto errout;
	}

errout:
	tcf_chain_put(chain);
errout_block:
	tcf_block_release(q, block, true);
	if (err == -EAGAIN)
		/* Replay the request. */
		goto replay;
	return err;

errout_block_locked:
	mutex_unlock(&block->lock);
	goto errout_block;
}

/* called with RTNL */
static int tc_dump_chain(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_MAX + 1];
	struct Qdisc *q = NULL;
	struct tcf_block *block;
	struct tcmsg *tcm = nlmsg_data(cb->nlh);
	struct tcf_chain *chain;
	long index_start;
	long index;
	int err;

	if (nlmsg_len(cb->nlh) < sizeof(*tcm))
		return skb->len;

	err = nlmsg_parse_deprecated(cb->nlh, sizeof(*tcm), tca, TCA_MAX,
				     rtm_tca_policy, cb->extack);
	if (err)
		return err;

	if (tcm->tcm_ifindex == TCM_IFINDEX_MAGIC_BLOCK) {
		block = tcf_block_refcnt_get(net, tcm->tcm_block_index);
		if (!block)
			goto out;
	} else {
		const struct Qdisc_class_ops *cops;
		struct net_device *dev;
		unsigned long cl = 0;

		dev = __dev_get_by_index(net, tcm->tcm_ifindex);
		if (!dev)
			return skb->len;

		if (!tcm->tcm_parent)
			q = rtnl_dereference(dev->qdisc);
		else
			q = qdisc_lookup(dev, TC_H_MAJ(tcm->tcm_parent));

		if (!q)
			goto out;
		cops = q->ops->cl_ops;
		if (!cops)
			goto out;
		if (!cops->tcf_block)
			goto out;
		if (TC_H_MIN(tcm->tcm_parent)) {
			cl = cops->find(q, tcm->tcm_parent);
			if (cl == 0)
				goto out;
		}
		block = cops->tcf_block(q, cl, NULL);
		if (!block)
			goto out;
		if (tcf_block_shared(block))
			q = NULL;
	}

	index_start = cb->args[0];
	index = 0;

	mutex_lock(&block->lock);
	list_for_each_entry(chain, &block->chain_list, list) {
		if ((tca[TCA_CHAIN] &&
		     nla_get_u32(tca[TCA_CHAIN]) != chain->index))
			continue;
		if (index < index_start) {
			index++;
			continue;
		}
		if (tcf_chain_held_by_acts_only(chain))
			continue;
		err = tc_chain_fill_node(chain->tmplt_ops, chain->tmplt_priv,
					 chain->index, net, skb, block,
					 NETLINK_CB(cb->skb).portid,
					 cb->nlh->nlmsg_seq, NLM_F_MULTI,
					 RTM_NEWCHAIN);
		if (err <= 0)
			break;
		index++;
	}
	mutex_unlock(&block->lock);

	if (tcm->tcm_ifindex == TCM_IFINDEX_MAGIC_BLOCK)
		tcf_block_refcnt_put(block, true);
	cb->args[0] = index;

out:
	/* If we did no progress, the error (EMSGSIZE) is real */
	if (skb->len == 0 && err)
		return err;
	return skb->len;
}

void tcf_exts_destroy(struct tcf_exts *exts)
{
#ifdef CONFIG_NET_CLS_ACT
	if (exts->actions) {
		tcf_action_destroy(exts->actions, TCA_ACT_UNBIND);
		kfree(exts->actions);
	}
	exts->nr_actions = 0;
#endif
}
EXPORT_SYMBOL(tcf_exts_destroy);

int tcf_exts_validate_ex(struct net *net, struct tcf_proto *tp, struct nlattr **tb,
			 struct nlattr *rate_tlv, struct tcf_exts *exts,
			 u32 flags, u32 fl_flags, struct netlink_ext_ack *extack)
{
#ifdef CONFIG_NET_CLS_ACT
	{
		int init_res[TCA_ACT_MAX_PRIO] = {};
		struct tc_action *act;
		size_t attr_size = 0;

		if (exts->police && tb[exts->police]) {
			struct tc_action_ops *a_o;

			a_o = tc_action_load_ops(tb[exts->police], true,
						 !(flags & TCA_ACT_FLAGS_NO_RTNL),
						 extack);
			if (IS_ERR(a_o))
				return PTR_ERR(a_o);
			flags |= TCA_ACT_FLAGS_POLICE | TCA_ACT_FLAGS_BIND;
			act = tcf_action_init_1(net, tp, tb[exts->police],
						rate_tlv, a_o, init_res, flags,
						extack);
			module_put(a_o->owner);
			if (IS_ERR(act))
				return PTR_ERR(act);

			act->type = exts->type = TCA_OLD_COMPAT;
			exts->actions[0] = act;
			exts->nr_actions = 1;
			tcf_idr_insert_many(exts->actions);
		} else if (exts->action && tb[exts->action]) {
			int err;

			flags |= TCA_ACT_FLAGS_BIND;
			err = tcf_action_init(net, tp, tb[exts->action],
					      rate_tlv, exts->actions, init_res,
					      &attr_size, flags, fl_flags,
					      extack);
			if (err < 0)
				return err;
			exts->nr_actions = err;
		}
	}
#else
	if ((exts->action && tb[exts->action]) ||
	    (exts->police && tb[exts->police])) {
		NL_SET_ERR_MSG(extack, "Classifier actions are not supported per compile options (CONFIG_NET_CLS_ACT)");
		return -EOPNOTSUPP;
	}
#endif

	return 0;
}
EXPORT_SYMBOL(tcf_exts_validate_ex);

int tcf_exts_validate(struct net *net, struct tcf_proto *tp, struct nlattr **tb,
		      struct nlattr *rate_tlv, struct tcf_exts *exts,
		      u32 flags, struct netlink_ext_ack *extack)
{
	return tcf_exts_validate_ex(net, tp, tb, rate_tlv, exts,
				    flags, 0, extack);
}
EXPORT_SYMBOL(tcf_exts_validate);

void tcf_exts_change(struct tcf_exts *dst, struct tcf_exts *src)
{
#ifdef CONFIG_NET_CLS_ACT
	struct tcf_exts old = *dst;

	*dst = *src;
	tcf_exts_destroy(&old);
#endif
}
EXPORT_SYMBOL(tcf_exts_change);

#ifdef CONFIG_NET_CLS_ACT
static struct tc_action *tcf_exts_first_act(struct tcf_exts *exts)
{
	if (exts->nr_actions == 0)
		return NULL;
	else
		return exts->actions[0];
}
#endif

int tcf_exts_dump(struct sk_buff *skb, struct tcf_exts *exts)
{
#ifdef CONFIG_NET_CLS_ACT
	struct nlattr *nest;

	if (exts->action && tcf_exts_has_actions(exts)) {
		/*
		 * again for backward compatible mode - we want
		 * to work with both old and new modes of entering
		 * tc data even if iproute2  was newer - jhs
		 */
		if (exts->type != TCA_OLD_COMPAT) {
			nest = nla_nest_start_noflag(skb, exts->action);
			if (nest == NULL)
				goto nla_put_failure;

			if (tcf_action_dump(skb, exts->actions, 0, 0, false)
			    < 0)
				goto nla_put_failure;
			nla_nest_end(skb, nest);
		} else if (exts->police) {
			struct tc_action *act = tcf_exts_first_act(exts);
			nest = nla_nest_start_noflag(skb, exts->police);
			if (nest == NULL || !act)
				goto nla_put_failure;
			if (tcf_action_dump_old(skb, act, 0, 0) < 0)
				goto nla_put_failure;
			nla_nest_end(skb, nest);
		}
	}
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
#else
	return 0;
#endif
}
EXPORT_SYMBOL(tcf_exts_dump);

int tcf_exts_terse_dump(struct sk_buff *skb, struct tcf_exts *exts)
{
#ifdef CONFIG_NET_CLS_ACT
	struct nlattr *nest;

	if (!exts->action || !tcf_exts_has_actions(exts))
		return 0;

	nest = nla_nest_start_noflag(skb, exts->action);
	if (!nest)
		goto nla_put_failure;

	if (tcf_action_dump(skb, exts->actions, 0, 0, true) < 0)
		goto nla_put_failure;
	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
#else
	return 0;
#endif
}
EXPORT_SYMBOL(tcf_exts_terse_dump);

int tcf_exts_dump_stats(struct sk_buff *skb, struct tcf_exts *exts)
{
#ifdef CONFIG_NET_CLS_ACT
	struct tc_action *a = tcf_exts_first_act(exts);
	if (a != NULL && tcf_action_copy_stats(skb, a, 1) < 0)
		return -1;
#endif
	return 0;
}
EXPORT_SYMBOL(tcf_exts_dump_stats);

static void tcf_block_offload_inc(struct tcf_block *block, u32 *flags)
{
	if (*flags & TCA_CLS_FLAGS_IN_HW)
		return;
	*flags |= TCA_CLS_FLAGS_IN_HW;
	atomic_inc(&block->offloadcnt);
}

static void tcf_block_offload_dec(struct tcf_block *block, u32 *flags)
{
	if (!(*flags & TCA_CLS_FLAGS_IN_HW))
		return;
	*flags &= ~TCA_CLS_FLAGS_IN_HW;
	atomic_dec(&block->offloadcnt);
}

static void tc_cls_offload_cnt_update(struct tcf_block *block,
				      struct tcf_proto *tp, u32 *cnt,
				      u32 *flags, u32 diff, bool add)
{
	lockdep_assert_held(&block->cb_lock);

	spin_lock(&tp->lock);
	if (add) {
		if (!*cnt)
			tcf_block_offload_inc(block, flags);
		*cnt += diff;
	} else {
		*cnt -= diff;
		if (!*cnt)
			tcf_block_offload_dec(block, flags);
	}
	spin_unlock(&tp->lock);
}

static void
tc_cls_offload_cnt_reset(struct tcf_block *block, struct tcf_proto *tp,
			 u32 *cnt, u32 *flags)
{
	lockdep_assert_held(&block->cb_lock);

	spin_lock(&tp->lock);
	tcf_block_offload_dec(block, flags);
	*cnt = 0;
	spin_unlock(&tp->lock);
}

static int
__tc_setup_cb_call(struct tcf_block *block, enum tc_setup_type type,
		   void *type_data, bool err_stop)
{
	struct flow_block_cb *block_cb;
	int ok_count = 0;
	int err;

	list_for_each_entry(block_cb, &block->flow_block.cb_list, list) {
		err = block_cb->cb(type, type_data, block_cb->cb_priv);
		if (err) {
			if (err_stop)
				return err;
		} else {
			ok_count++;
		}
	}
	return ok_count;
}

int tc_setup_cb_call(struct tcf_block *block, enum tc_setup_type type,
		     void *type_data, bool err_stop, bool rtnl_held)
{
	bool take_rtnl = READ_ONCE(block->lockeddevcnt) && !rtnl_held;
	int ok_count;

retry:
	if (take_rtnl)
		rtnl_lock();
	down_read(&block->cb_lock);
	/* Need to obtain rtnl lock if block is bound to devs that require it.
	 * In block bind code cb_lock is obtained while holding rtnl, so we must
	 * obtain the locks in same order here.
	 */
	if (!rtnl_held && !take_rtnl && block->lockeddevcnt) {
		up_read(&block->cb_lock);
		take_rtnl = true;
		goto retry;
	}

	ok_count = __tc_setup_cb_call(block, type, type_data, err_stop);

	up_read(&block->cb_lock);
	if (take_rtnl)
		rtnl_unlock();
	return ok_count;
}
EXPORT_SYMBOL(tc_setup_cb_call);

/* Non-destructive filter add. If filter that wasn't already in hardware is
 * successfully offloaded, increment block offloads counter. On failure,
 * previously offloaded filter is considered to be intact and offloads counter
 * is not decremented.
 */

int tc_setup_cb_add(struct tcf_block *block, struct tcf_proto *tp,
		    enum tc_setup_type type, void *type_data, bool err_stop,
		    u32 *flags, unsigned int *in_hw_count, bool rtnl_held)
{
	bool take_rtnl = READ_ONCE(block->lockeddevcnt) && !rtnl_held;
	int ok_count;

retry:
	if (take_rtnl)
		rtnl_lock();
	down_read(&block->cb_lock);
	/* Need to obtain rtnl lock if block is bound to devs that require it.
	 * In block bind code cb_lock is obtained while holding rtnl, so we must
	 * obtain the locks in same order here.
	 */
	if (!rtnl_held && !take_rtnl && block->lockeddevcnt) {
		up_read(&block->cb_lock);
		take_rtnl = true;
		goto retry;
	}

	/* Make sure all netdevs sharing this block are offload-capable. */
	if (block->nooffloaddevcnt && err_stop) {
		ok_count = -EOPNOTSUPP;
		goto err_unlock;
	}

	ok_count = __tc_setup_cb_call(block, type, type_data, err_stop);
	if (ok_count < 0)
		goto err_unlock;

	if (tp->ops->hw_add)
		tp->ops->hw_add(tp, type_data);
	if (ok_count > 0)
		tc_cls_offload_cnt_update(block, tp, in_hw_count, flags,
					  ok_count, true);
err_unlock:
	up_read(&block->cb_lock);
	if (take_rtnl)
		rtnl_unlock();
	return min(ok_count, 0);
}
EXPORT_SYMBOL(tc_setup_cb_add);

/* Destructive filter replace. If filter that wasn't already in hardware is
 * successfully offloaded, increment block offload counter. On failure,
 * previously offloaded filter is considered to be destroyed and offload counter
 * is decremented.
 */

int tc_setup_cb_replace(struct tcf_block *block, struct tcf_proto *tp,
			enum tc_setup_type type, void *type_data, bool err_stop,
			u32 *old_flags, unsigned int *old_in_hw_count,
			u32 *new_flags, unsigned int *new_in_hw_count,
			bool rtnl_held)
{
	bool take_rtnl = READ_ONCE(block->lockeddevcnt) && !rtnl_held;
	int ok_count;

retry:
	if (take_rtnl)
		rtnl_lock();
	down_read(&block->cb_lock);
	/* Need to obtain rtnl lock if block is bound to devs that require it.
	 * In block bind code cb_lock is obtained while holding rtnl, so we must
	 * obtain the locks in same order here.
	 */
	if (!rtnl_held && !take_rtnl && block->lockeddevcnt) {
		up_read(&block->cb_lock);
		take_rtnl = true;
		goto retry;
	}

	/* Make sure all netdevs sharing this block are offload-capable. */
	if (block->nooffloaddevcnt && err_stop) {
		ok_count = -EOPNOTSUPP;
		goto err_unlock;
	}

	tc_cls_offload_cnt_reset(block, tp, old_in_hw_count, old_flags);
	if (tp->ops->hw_del)
		tp->ops->hw_del(tp, type_data);

	ok_count = __tc_setup_cb_call(block, type, type_data, err_stop);
	if (ok_count < 0)
		goto err_unlock;

	if (tp->ops->hw_add)
		tp->ops->hw_add(tp, type_data);
	if (ok_count > 0)
		tc_cls_offload_cnt_update(block, tp, new_in_hw_count,
					  new_flags, ok_count, true);
err_unlock:
	up_read(&block->cb_lock);
	if (take_rtnl)
		rtnl_unlock();
	return min(ok_count, 0);
}
EXPORT_SYMBOL(tc_setup_cb_replace);

/* Destroy filter and decrement block offload counter, if filter was previously
 * offloaded.
 */

int tc_setup_cb_destroy(struct tcf_block *block, struct tcf_proto *tp,
			enum tc_setup_type type, void *type_data, bool err_stop,
			u32 *flags, unsigned int *in_hw_count, bool rtnl_held)
{
	bool take_rtnl = READ_ONCE(block->lockeddevcnt) && !rtnl_held;
	int ok_count;

retry:
	if (take_rtnl)
		rtnl_lock();
	down_read(&block->cb_lock);
	/* Need to obtain rtnl lock if block is bound to devs that require it.
	 * In block bind code cb_lock is obtained while holding rtnl, so we must
	 * obtain the locks in same order here.
	 */
	if (!rtnl_held && !take_rtnl && block->lockeddevcnt) {
		up_read(&block->cb_lock);
		take_rtnl = true;
		goto retry;
	}

	ok_count = __tc_setup_cb_call(block, type, type_data, err_stop);

	tc_cls_offload_cnt_reset(block, tp, in_hw_count, flags);
	if (tp->ops->hw_del)
		tp->ops->hw_del(tp, type_data);

	up_read(&block->cb_lock);
	if (take_rtnl)
		rtnl_unlock();
	return min(ok_count, 0);
}
EXPORT_SYMBOL(tc_setup_cb_destroy);

int tc_setup_cb_reoffload(struct tcf_block *block, struct tcf_proto *tp,
			  bool add, flow_setup_cb_t *cb,
			  enum tc_setup_type type, void *type_data,
			  void *cb_priv, u32 *flags, unsigned int *in_hw_count)
{
	int err = cb(type, type_data, cb_priv);

	if (err) {
		if (add && tc_skip_sw(*flags))
			return err;
	} else {
		tc_cls_offload_cnt_update(block, tp, in_hw_count, flags, 1,
					  add);
	}

	return 0;
}
EXPORT_SYMBOL(tc_setup_cb_reoffload);

static int tcf_act_get_cookie(struct flow_action_entry *entry,
			      const struct tc_action *act)
{
	struct tc_cookie *cookie;
	int err = 0;

	rcu_read_lock();
	cookie = rcu_dereference(act->act_cookie);
	if (cookie) {
		entry->cookie = flow_action_cookie_create(cookie->data,
							  cookie->len,
							  GFP_ATOMIC);
		if (!entry->cookie)
			err = -ENOMEM;
	}
	rcu_read_unlock();
	return err;
}

static void tcf_act_put_cookie(struct flow_action_entry *entry)
{
	flow_action_cookie_destroy(entry->cookie);
}

void tc_cleanup_offload_action(struct flow_action *flow_action)
{
	struct flow_action_entry *entry;
	int i;

	flow_action_for_each(i, entry, flow_action) {
		tcf_act_put_cookie(entry);
		if (entry->destructor)
			entry->destructor(entry->destructor_priv);
	}
}
EXPORT_SYMBOL(tc_cleanup_offload_action);

static int tc_setup_offload_act(struct tc_action *act,
				struct flow_action_entry *entry,
				u32 *index_inc,
				struct netlink_ext_ack *extack)
{
#ifdef CONFIG_NET_CLS_ACT
	if (act->ops->offload_act_setup) {
		return act->ops->offload_act_setup(act, entry, index_inc, true,
						   extack);
	} else {
		NL_SET_ERR_MSG(extack, "Action does not support offload");
		return -EOPNOTSUPP;
	}
#else
	return 0;
#endif
}

int tc_setup_action(struct flow_action *flow_action,
		    struct tc_action *actions[],
		    struct netlink_ext_ack *extack)
{
	int i, j, k, index, err = 0;
	struct tc_action *act;

	BUILD_BUG_ON(TCA_ACT_HW_STATS_ANY != FLOW_ACTION_HW_STATS_ANY);
	BUILD_BUG_ON(TCA_ACT_HW_STATS_IMMEDIATE != FLOW_ACTION_HW_STATS_IMMEDIATE);
	BUILD_BUG_ON(TCA_ACT_HW_STATS_DELAYED != FLOW_ACTION_HW_STATS_DELAYED);

	if (!actions)
		return 0;

	j = 0;
	tcf_act_for_each_action(i, act, actions) {
		struct flow_action_entry *entry;

		entry = &flow_action->entries[j];
		spin_lock_bh(&act->tcfa_lock);
		err = tcf_act_get_cookie(entry, act);
		if (err)
			goto err_out_locked;

		index = 0;
		err = tc_setup_offload_act(act, entry, &index, extack);
		if (err)
			goto err_out_locked;

		for (k = 0; k < index ; k++) {
			entry[k].hw_stats = tc_act_hw_stats(act->hw_stats);
			entry[k].hw_index = act->tcfa_index;
		}

		j += index;

		spin_unlock_bh(&act->tcfa_lock);
	}

err_out:
	if (err)
		tc_cleanup_offload_action(flow_action);

	return err;
err_out_locked:
	spin_unlock_bh(&act->tcfa_lock);
	goto err_out;
}

int tc_setup_offload_action(struct flow_action *flow_action,
			    const struct tcf_exts *exts,
			    struct netlink_ext_ack *extack)
{
#ifdef CONFIG_NET_CLS_ACT
	if (!exts)
		return 0;

	return tc_setup_action(flow_action, exts->actions, extack);
#else
	return 0;
#endif
}
EXPORT_SYMBOL(tc_setup_offload_action);

unsigned int tcf_exts_num_actions(struct tcf_exts *exts)
{
	unsigned int num_acts = 0;
	struct tc_action *act;
	int i;

	tcf_exts_for_each_action(i, act, exts) {
		if (is_tcf_pedit(act))
			num_acts += tcf_pedit_nkeys(act);
		else
			num_acts++;
	}
	return num_acts;
}
EXPORT_SYMBOL(tcf_exts_num_actions);

#ifdef CONFIG_NET_CLS_ACT
static int tcf_qevent_parse_block_index(struct nlattr *block_index_attr,
					u32 *p_block_index,
					struct netlink_ext_ack *extack)
{
	*p_block_index = nla_get_u32(block_index_attr);
	if (!*p_block_index) {
		NL_SET_ERR_MSG(extack, "Block number may not be zero");
		return -EINVAL;
	}

	return 0;
}

int tcf_qevent_init(struct tcf_qevent *qe, struct Qdisc *sch,
		    enum flow_block_binder_type binder_type,
		    struct nlattr *block_index_attr,
		    struct netlink_ext_ack *extack)
{
	u32 block_index;
	int err;

	if (!block_index_attr)
		return 0;

	err = tcf_qevent_parse_block_index(block_index_attr, &block_index, extack);
	if (err)
		return err;

	if (!block_index)
		return 0;

	qe->info.binder_type = binder_type;
	qe->info.chain_head_change = tcf_chain_head_change_dflt;
	qe->info.chain_head_change_priv = &qe->filter_chain;
	qe->info.block_index = block_index;

	return tcf_block_get_ext(&qe->block, sch, &qe->info, extack);
}
EXPORT_SYMBOL(tcf_qevent_init);

void tcf_qevent_destroy(struct tcf_qevent *qe, struct Qdisc *sch)
{
	if (qe->info.block_index)
		tcf_block_put_ext(qe->block, sch, &qe->info);
}
EXPORT_SYMBOL(tcf_qevent_destroy);

int tcf_qevent_validate_change(struct tcf_qevent *qe, struct nlattr *block_index_attr,
			       struct netlink_ext_ack *extack)
{
	u32 block_index;
	int err;

	if (!block_index_attr)
		return 0;

	err = tcf_qevent_parse_block_index(block_index_attr, &block_index, extack);
	if (err)
		return err;

	/* Bounce newly-configured block or change in block. */
	if (block_index != qe->info.block_index) {
		NL_SET_ERR_MSG(extack, "Change of blocks is not supported");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(tcf_qevent_validate_change);

struct sk_buff *tcf_qevent_handle(struct tcf_qevent *qe, struct Qdisc *sch, struct sk_buff *skb,
				  struct sk_buff **to_free, int *ret)
{
	struct tcf_result cl_res;
	struct tcf_proto *fl;

	if (!qe->info.block_index)
		return skb;

	fl = rcu_dereference_bh(qe->filter_chain);

	switch (tcf_classify(skb, NULL, fl, &cl_res, false)) {
	case TC_ACT_SHOT:
		qdisc_qstats_drop(sch);
		__qdisc_drop(skb, to_free);
		*ret = __NET_XMIT_BYPASS;
		return NULL;
	case TC_ACT_STOLEN:
	case TC_ACT_QUEUED:
	case TC_ACT_TRAP:
		__qdisc_drop(skb, to_free);
		*ret = __NET_XMIT_STOLEN;
		return NULL;
	case TC_ACT_REDIRECT:
		skb_do_redirect(skb);
		*ret = __NET_XMIT_STOLEN;
		return NULL;
	}

	return skb;
}
EXPORT_SYMBOL(tcf_qevent_handle);

int tcf_qevent_dump(struct sk_buff *skb, int attr_name, struct tcf_qevent *qe)
{
	if (!qe->info.block_index)
		return 0;
	return nla_put_u32(skb, attr_name, qe->info.block_index);
}
EXPORT_SYMBOL(tcf_qevent_dump);
#endif

static __net_init int tcf_net_init(struct net *net)
{
	struct tcf_net *tn = net_generic(net, tcf_net_id);

	spin_lock_init(&tn->idr_lock);
	idr_init(&tn->idr);
	return 0;
}

static void __net_exit tcf_net_exit(struct net *net)
{
	struct tcf_net *tn = net_generic(net, tcf_net_id);

	idr_destroy(&tn->idr);
}

static struct pernet_operations tcf_net_ops = {
	.init = tcf_net_init,
	.exit = tcf_net_exit,
	.id   = &tcf_net_id,
	.size = sizeof(struct tcf_net),
};

static int __init tc_filter_init(void)
{
	int err;

	tc_filter_wq = alloc_ordered_workqueue("tc_filter_workqueue", 0);
	if (!tc_filter_wq)
		return -ENOMEM;

	err = register_pernet_subsys(&tcf_net_ops);
	if (err)
		goto err_register_pernet_subsys;

	rtnl_register(PF_UNSPEC, RTM_NEWTFILTER, tc_new_tfilter, NULL,
		      RTNL_FLAG_DOIT_UNLOCKED);
	rtnl_register(PF_UNSPEC, RTM_DELTFILTER, tc_del_tfilter, NULL,
		      RTNL_FLAG_DOIT_UNLOCKED);
	rtnl_register(PF_UNSPEC, RTM_GETTFILTER, tc_get_tfilter,
		      tc_dump_tfilter, RTNL_FLAG_DOIT_UNLOCKED);
	rtnl_register(PF_UNSPEC, RTM_NEWCHAIN, tc_ctl_chain, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_DELCHAIN, tc_ctl_chain, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_GETCHAIN, tc_ctl_chain,
		      tc_dump_chain, 0);

	return 0;

err_register_pernet_subsys:
	destroy_workqueue(tc_filter_wq);
	return err;
}

subsys_initcall(tc_filter_init);
