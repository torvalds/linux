/*
 * net/sched/cls_api.c	Packet classifier API.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 *
 * Eduardo J. Blanco <ejbs@netlabs.com.uy> :990222: kmod support
 *
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
#include <linux/err.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>

/* The list of all installed classifier types */
static LIST_HEAD(tcf_proto_base);

/* Protects list of registered TC modules. It is pure SMP lock. */
static DEFINE_RWLOCK(cls_mod_lock);

/* Find classifier type by string name */

static const struct tcf_proto_ops *tcf_proto_lookup_ops(const char *kind)
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

int unregister_tcf_proto_ops(struct tcf_proto_ops *ops)
{
	struct tcf_proto_ops *t;
	int rc = -ENOENT;

	/* Wait for outstanding call_rcu()s, if any, from a
	 * tcf_proto_ops's destroy() handler.
	 */
	rcu_barrier();

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

/* Select new prio value from the range, managed by kernel. */

static inline u32 tcf_auto_prio(struct tcf_proto *tp)
{
	u32 first = TC_H_MAKE(0xC0000000U, 0U);

	if (tp)
		first = tp->prio - 1;

	return TC_H_MAJ(first);
}

static struct tcf_proto *tcf_proto_create(const char *kind, u32 protocol,
					  u32 prio, u32 parent, struct Qdisc *q,
					  struct tcf_chain *chain)
{
	struct tcf_proto *tp;
	int err;

	tp = kzalloc(sizeof(*tp), GFP_KERNEL);
	if (!tp)
		return ERR_PTR(-ENOBUFS);

	err = -ENOENT;
	tp->ops = tcf_proto_lookup_ops(kind);
	if (!tp->ops) {
#ifdef CONFIG_MODULES
		rtnl_unlock();
		request_module("cls_%s", kind);
		rtnl_lock();
		tp->ops = tcf_proto_lookup_ops(kind);
		/* We dropped the RTNL semaphore in order to perform
		 * the module load. So, even if we succeeded in loading
		 * the module we have to replay the request. We indicate
		 * this using -EAGAIN.
		 */
		if (tp->ops) {
			module_put(tp->ops->owner);
			err = -EAGAIN;
		} else {
			err = -ENOENT;
		}
		goto errout;
#endif
	}
	tp->classify = tp->ops->classify;
	tp->protocol = protocol;
	tp->prio = prio;
	tp->classid = parent;
	tp->q = q;
	tp->chain = chain;

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

static void tcf_proto_destroy(struct tcf_proto *tp)
{
	tp->ops->destroy(tp);
	module_put(tp->ops->owner);
	kfree_rcu(tp, rcu);
}

static struct tcf_chain *tcf_chain_create(struct tcf_block *block,
					  u32 chain_index)
{
	struct tcf_chain *chain;

	chain = kzalloc(sizeof(*chain), GFP_KERNEL);
	if (!chain)
		return NULL;
	list_add_tail(&chain->list, &block->chain_list);
	chain->block = block;
	chain->index = chain_index;
	chain->refcnt = 1;
	return chain;
}

static void tcf_chain_flush(struct tcf_chain *chain)
{
	struct tcf_proto *tp;

	if (chain->p_filter_chain)
		RCU_INIT_POINTER(*chain->p_filter_chain, NULL);
	while ((tp = rtnl_dereference(chain->filter_chain)) != NULL) {
		RCU_INIT_POINTER(chain->filter_chain, tp->next);
		tcf_proto_destroy(tp);
	}
}

static void tcf_chain_destroy(struct tcf_chain *chain)
{
	/* May be already removed from the list by the previous call. */
	if (!list_empty(&chain->list))
		list_del_init(&chain->list);

	/* There might still be a reference held when we got here from
	 * tcf_block_put. Wait for the user to drop reference before free.
	 */
	if (!chain->refcnt)
		kfree(chain);
}

struct tcf_chain *tcf_chain_get(struct tcf_block *block, u32 chain_index,
				bool create)
{
	struct tcf_chain *chain;

	list_for_each_entry(chain, &block->chain_list, list) {
		if (chain->index == chain_index) {
			chain->refcnt++;
			return chain;
		}
	}
	if (create)
		return tcf_chain_create(block, chain_index);
	else
		return NULL;
}
EXPORT_SYMBOL(tcf_chain_get);

void tcf_chain_put(struct tcf_chain *chain)
{
	/* Destroy unused chain, with exception of chain 0, which is the
	 * default one and has to be always present.
	 */
	if (--chain->refcnt == 0 && !chain->filter_chain && chain->index != 0)
		tcf_chain_destroy(chain);
}
EXPORT_SYMBOL(tcf_chain_put);

static void
tcf_chain_filter_chain_ptr_set(struct tcf_chain *chain,
			       struct tcf_proto __rcu **p_filter_chain)
{
	chain->p_filter_chain = p_filter_chain;
}

int tcf_block_get(struct tcf_block **p_block,
		  struct tcf_proto __rcu **p_filter_chain)
{
	struct tcf_block *block = kzalloc(sizeof(*block), GFP_KERNEL);
	struct tcf_chain *chain;
	int err;

	if (!block)
		return -ENOMEM;
	INIT_LIST_HEAD(&block->chain_list);
	/* Create chain 0 by default, it has to be always present. */
	chain = tcf_chain_create(block, 0);
	if (!chain) {
		err = -ENOMEM;
		goto err_chain_create;
	}
	tcf_chain_filter_chain_ptr_set(chain, p_filter_chain);
	*p_block = block;
	return 0;

err_chain_create:
	kfree(block);
	return err;
}
EXPORT_SYMBOL(tcf_block_get);

void tcf_block_put(struct tcf_block *block)
{
	struct tcf_chain *chain, *tmp;

	if (!block)
		return;

	list_for_each_entry_safe(chain, tmp, &block->chain_list, list) {
		tcf_chain_flush(chain);
		tcf_chain_destroy(chain);
	}
	kfree(block);
}
EXPORT_SYMBOL(tcf_block_put);

/* Main classifier routine: scans classifier chain attached
 * to this qdisc, (optionally) tests for protocol and asks
 * specific classifiers.
 */
int tcf_classify(struct sk_buff *skb, const struct tcf_proto *tp,
		 struct tcf_result *res, bool compat_mode)
{
	__be16 protocol = tc_skb_protocol(skb);
#ifdef CONFIG_NET_CLS_ACT
	const int max_reclassify_loop = 4;
	const struct tcf_proto *orig_tp = tp;
	const struct tcf_proto *first_tp;
	int limit = 0;

reclassify:
#endif
	for (; tp; tp = rcu_dereference_bh(tp->next)) {
		int err;

		if (tp->protocol != protocol &&
		    tp->protocol != htons(ETH_P_ALL))
			continue;

		err = tp->classify(skb, tp, res);
#ifdef CONFIG_NET_CLS_ACT
		if (unlikely(err == TC_ACT_RECLASSIFY && !compat_mode)) {
			first_tp = orig_tp;
			goto reset;
		} else if (unlikely(TC_ACT_EXT_CMP(err, TC_ACT_GOTO_CHAIN))) {
			first_tp = res->goto_tp;
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
		net_notice_ratelimited("%s: reclassify loop, rule prio %u, protocol %02x\n",
				       tp->q->ops->id, tp->prio & 0xffff,
				       ntohs(tp->protocol));
		return TC_ACT_SHOT;
	}

	tp = first_tp;
	protocol = tc_skb_protocol(skb);
	goto reclassify;
#endif
}
EXPORT_SYMBOL(tcf_classify);

struct tcf_chain_info {
	struct tcf_proto __rcu **pprev;
	struct tcf_proto __rcu *next;
};

static struct tcf_proto *tcf_chain_tp_prev(struct tcf_chain_info *chain_info)
{
	return rtnl_dereference(*chain_info->pprev);
}

static void tcf_chain_tp_insert(struct tcf_chain *chain,
				struct tcf_chain_info *chain_info,
				struct tcf_proto *tp)
{
	if (chain->p_filter_chain &&
	    *chain_info->pprev == chain->filter_chain)
		rcu_assign_pointer(*chain->p_filter_chain, tp);
	RCU_INIT_POINTER(tp->next, tcf_chain_tp_prev(chain_info));
	rcu_assign_pointer(*chain_info->pprev, tp);
}

static void tcf_chain_tp_remove(struct tcf_chain *chain,
				struct tcf_chain_info *chain_info,
				struct tcf_proto *tp)
{
	struct tcf_proto *next = rtnl_dereference(chain_info->next);

	if (chain->p_filter_chain && tp == chain->filter_chain)
		RCU_INIT_POINTER(*chain->p_filter_chain, next);
	RCU_INIT_POINTER(*chain_info->pprev, next);
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
	     (tp = rtnl_dereference(*pprev)); pprev = &tp->next) {
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
	chain_info->next = tp ? tp->next : NULL;
	return tp;
}

static int tcf_fill_node(struct net *net, struct sk_buff *skb,
			 struct tcf_proto *tp, void *fh, u32 portid,
			 u32 seq, u16 flags, int event)
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
	tcm->tcm_ifindex = qdisc_dev(tp->q)->ifindex;
	tcm->tcm_parent = tp->classid;
	tcm->tcm_info = TC_H_MAKE(tp->prio, tp->protocol);
	if (nla_put_string(skb, TCA_KIND, tp->ops->kind))
		goto nla_put_failure;
	if (nla_put_u32(skb, TCA_CHAIN, tp->chain->index))
		goto nla_put_failure;
	if (!fh) {
		tcm->tcm_handle = 0;
	} else {
		if (tp->ops->dump && tp->ops->dump(net, tp, fh, skb, tcm) < 0)
			goto nla_put_failure;
	}
	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

out_nlmsg_trim:
nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int tfilter_notify(struct net *net, struct sk_buff *oskb,
			  struct nlmsghdr *n, struct tcf_proto *tp,
			  void *fh, int event, bool unicast)
{
	struct sk_buff *skb;
	u32 portid = oskb ? NETLINK_CB(oskb).portid : 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tcf_fill_node(net, skb, tp, fh, portid, n->nlmsg_seq,
			  n->nlmsg_flags, event) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	if (unicast)
		return netlink_unicast(net->rtnl, skb, portid, MSG_DONTWAIT);

	return rtnetlink_send(skb, net, portid, RTNLGRP_TC,
			      n->nlmsg_flags & NLM_F_ECHO);
}

static int tfilter_del_notify(struct net *net, struct sk_buff *oskb,
			      struct nlmsghdr *n, struct tcf_proto *tp,
			      void *fh, bool unicast, bool *last)
{
	struct sk_buff *skb;
	u32 portid = oskb ? NETLINK_CB(oskb).portid : 0;
	int err;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tcf_fill_node(net, skb, tp, fh, portid, n->nlmsg_seq,
			  n->nlmsg_flags, RTM_DELTFILTER) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	err = tp->ops->delete(tp, fh, last);
	if (err) {
		kfree_skb(skb);
		return err;
	}

	if (unicast)
		return netlink_unicast(net->rtnl, skb, portid, MSG_DONTWAIT);

	return rtnetlink_send(skb, net, portid, RTNLGRP_TC,
			      n->nlmsg_flags & NLM_F_ECHO);
}

static void tfilter_notify_chain(struct net *net, struct sk_buff *oskb,
				 struct nlmsghdr *n,
				 struct tcf_chain *chain, int event)
{
	struct tcf_proto *tp;

	for (tp = rtnl_dereference(chain->filter_chain);
	     tp; tp = rtnl_dereference(tp->next))
		tfilter_notify(net, oskb, n, tp, 0, event, false);
}

/* Add/change/delete/get a filter node */

static int tc_ctl_tfilter(struct sk_buff *skb, struct nlmsghdr *n,
			  struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_MAX + 1];
	struct tcmsg *t;
	u32 protocol;
	u32 prio;
	bool prio_allocate;
	u32 parent;
	u32 chain_index;
	struct net_device *dev;
	struct Qdisc  *q;
	struct tcf_chain_info chain_info;
	struct tcf_chain *chain = NULL;
	struct tcf_block *block;
	struct tcf_proto *tp;
	const struct Qdisc_class_ops *cops;
	unsigned long cl;
	void *fh;
	int err;
	int tp_created;

	if ((n->nlmsg_type != RTM_GETTFILTER) &&
	    !netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

replay:
	tp_created = 0;

	err = nlmsg_parse(n, sizeof(*t), tca, TCA_MAX, NULL, extack);
	if (err < 0)
		return err;

	t = nlmsg_data(n);
	protocol = TC_H_MIN(t->tcm_info);
	prio = TC_H_MAJ(t->tcm_info);
	prio_allocate = false;
	parent = t->tcm_parent;
	cl = 0;

	if (prio == 0) {
		switch (n->nlmsg_type) {
		case RTM_DELTFILTER:
			if (protocol || t->tcm_handle || tca[TCA_KIND])
				return -ENOENT;
			break;
		case RTM_NEWTFILTER:
			/* If no priority is provided by the user,
			 * we allocate one.
			 */
			if (n->nlmsg_flags & NLM_F_CREATE) {
				prio = TC_H_MAKE(0x80000000U, 0U);
				prio_allocate = true;
				break;
			}
			/* fall-through */
		default:
			return -ENOENT;
		}
	}

	/* Find head of filter chain. */

	/* Find link */
	dev = __dev_get_by_index(net, t->tcm_ifindex);
	if (dev == NULL)
		return -ENODEV;

	/* Find qdisc */
	if (!parent) {
		q = dev->qdisc;
		parent = q->handle;
	} else {
		q = qdisc_lookup(dev, TC_H_MAJ(t->tcm_parent));
		if (q == NULL)
			return -EINVAL;
	}

	/* Is it classful? */
	cops = q->ops->cl_ops;
	if (!cops)
		return -EINVAL;

	if (!cops->tcf_block)
		return -EOPNOTSUPP;

	/* Do we search for filter, attached to class? */
	if (TC_H_MIN(parent)) {
		cl = cops->find(q, parent);
		if (cl == 0)
			return -ENOENT;
	}

	/* And the last stroke */
	block = cops->tcf_block(q, cl);
	if (!block) {
		err = -EINVAL;
		goto errout;
	}

	chain_index = tca[TCA_CHAIN] ? nla_get_u32(tca[TCA_CHAIN]) : 0;
	if (chain_index > TC_ACT_EXT_VAL_MASK) {
		err = -EINVAL;
		goto errout;
	}
	chain = tcf_chain_get(block, chain_index,
			      n->nlmsg_type == RTM_NEWTFILTER);
	if (!chain) {
		err = n->nlmsg_type == RTM_NEWTFILTER ? -ENOMEM : -EINVAL;
		goto errout;
	}

	if (n->nlmsg_type == RTM_DELTFILTER && prio == 0) {
		tfilter_notify_chain(net, skb, n, chain, RTM_DELTFILTER);
		tcf_chain_flush(chain);
		err = 0;
		goto errout;
	}

	tp = tcf_chain_tp_find(chain, &chain_info, protocol,
			       prio, prio_allocate);
	if (IS_ERR(tp)) {
		err = PTR_ERR(tp);
		goto errout;
	}

	if (tp == NULL) {
		/* Proto-tcf does not exist, create new one */

		if (tca[TCA_KIND] == NULL || !protocol) {
			err = -EINVAL;
			goto errout;
		}

		if (n->nlmsg_type != RTM_NEWTFILTER ||
		    !(n->nlmsg_flags & NLM_F_CREATE)) {
			err = -ENOENT;
			goto errout;
		}

		if (prio_allocate)
			prio = tcf_auto_prio(tcf_chain_tp_prev(&chain_info));

		tp = tcf_proto_create(nla_data(tca[TCA_KIND]),
				      protocol, prio, parent, q, chain);
		if (IS_ERR(tp)) {
			err = PTR_ERR(tp);
			goto errout;
		}
		tp_created = 1;
	} else if (tca[TCA_KIND] && nla_strcmp(tca[TCA_KIND], tp->ops->kind)) {
		err = -EINVAL;
		goto errout;
	}

	fh = tp->ops->get(tp, t->tcm_handle);

	if (!fh) {
		if (n->nlmsg_type == RTM_DELTFILTER && t->tcm_handle == 0) {
			tcf_chain_tp_remove(chain, &chain_info, tp);
			tfilter_notify(net, skb, n, tp, fh,
				       RTM_DELTFILTER, false);
			tcf_proto_destroy(tp);
			err = 0;
			goto errout;
		}

		if (n->nlmsg_type != RTM_NEWTFILTER ||
		    !(n->nlmsg_flags & NLM_F_CREATE)) {
			err = -ENOENT;
			goto errout;
		}
	} else {
		bool last;

		switch (n->nlmsg_type) {
		case RTM_NEWTFILTER:
			if (n->nlmsg_flags & NLM_F_EXCL) {
				if (tp_created)
					tcf_proto_destroy(tp);
				err = -EEXIST;
				goto errout;
			}
			break;
		case RTM_DELTFILTER:
			err = tfilter_del_notify(net, skb, n, tp, fh, false,
						 &last);
			if (err)
				goto errout;
			if (last) {
				tcf_chain_tp_remove(chain, &chain_info, tp);
				tcf_proto_destroy(tp);
			}
			goto errout;
		case RTM_GETTFILTER:
			err = tfilter_notify(net, skb, n, tp, fh,
					     RTM_NEWTFILTER, true);
			goto errout;
		default:
			err = -EINVAL;
			goto errout;
		}
	}

	err = tp->ops->change(net, skb, tp, cl, t->tcm_handle, tca, &fh,
			      n->nlmsg_flags & NLM_F_CREATE ? TCA_ACT_NOREPLACE : TCA_ACT_REPLACE);
	if (err == 0) {
		if (tp_created)
			tcf_chain_tp_insert(chain, &chain_info, tp);
		tfilter_notify(net, skb, n, tp, fh, RTM_NEWTFILTER, false);
	} else {
		if (tp_created)
			tcf_proto_destroy(tp);
	}

errout:
	if (chain)
		tcf_chain_put(chain);
	if (err == -EAGAIN)
		/* Replay the request. */
		goto replay;
	return err;
}

struct tcf_dump_args {
	struct tcf_walker w;
	struct sk_buff *skb;
	struct netlink_callback *cb;
};

static int tcf_node_dump(struct tcf_proto *tp, void *n, struct tcf_walker *arg)
{
	struct tcf_dump_args *a = (void *)arg;
	struct net *net = sock_net(a->skb->sk);

	return tcf_fill_node(net, a->skb, tp, n, NETLINK_CB(a->cb->skb).portid,
			     a->cb->nlh->nlmsg_seq, NLM_F_MULTI,
			     RTM_NEWTFILTER);
}

static bool tcf_chain_dump(struct tcf_chain *chain, struct sk_buff *skb,
			   struct netlink_callback *cb,
			   long index_start, long *p_index)
{
	struct net *net = sock_net(skb->sk);
	struct tcmsg *tcm = nlmsg_data(cb->nlh);
	struct tcf_dump_args arg;
	struct tcf_proto *tp;

	for (tp = rtnl_dereference(chain->filter_chain);
	     tp; tp = rtnl_dereference(tp->next), (*p_index)++) {
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
			if (tcf_fill_node(net, skb, tp, 0,
					  NETLINK_CB(cb->skb).portid,
					  cb->nlh->nlmsg_seq, NLM_F_MULTI,
					  RTM_NEWTFILTER) <= 0)
				return false;

			cb->args[1] = 1;
		}
		if (!tp->ops->walk)
			continue;
		arg.w.fn = tcf_node_dump;
		arg.skb = skb;
		arg.cb = cb;
		arg.w.stop = 0;
		arg.w.skip = cb->args[1] - 1;
		arg.w.count = 0;
		tp->ops->walk(tp, &arg.w);
		cb->args[1] = arg.w.count + 1;
		if (arg.w.stop)
			return false;
	}
	return true;
}

/* called with RTNL */
static int tc_dump_tfilter(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_MAX + 1];
	struct net_device *dev;
	struct Qdisc *q;
	struct tcf_block *block;
	struct tcf_chain *chain;
	struct tcmsg *tcm = nlmsg_data(cb->nlh);
	unsigned long cl = 0;
	const struct Qdisc_class_ops *cops;
	long index_start;
	long index;
	int err;

	if (nlmsg_len(cb->nlh) < sizeof(*tcm))
		return skb->len;

	err = nlmsg_parse(cb->nlh, sizeof(*tcm), tca, TCA_MAX, NULL, NULL);
	if (err)
		return err;

	dev = __dev_get_by_index(net, tcm->tcm_ifindex);
	if (!dev)
		return skb->len;

	if (!tcm->tcm_parent)
		q = dev->qdisc;
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
	block = cops->tcf_block(q, cl);
	if (!block)
		goto out;

	index_start = cb->args[0];
	index = 0;

	list_for_each_entry(chain, &block->chain_list, list) {
		if (tca[TCA_CHAIN] &&
		    nla_get_u32(tca[TCA_CHAIN]) != chain->index)
			continue;
		if (!tcf_chain_dump(chain, skb, cb, index_start, &index))
			break;
	}

	cb->args[0] = index;

out:
	return skb->len;
}

void tcf_exts_destroy(struct tcf_exts *exts)
{
#ifdef CONFIG_NET_CLS_ACT
	LIST_HEAD(actions);

	tcf_exts_to_list(exts, &actions);
	tcf_action_destroy(&actions, TCA_ACT_UNBIND);
	kfree(exts->actions);
	exts->nr_actions = 0;
#endif
}
EXPORT_SYMBOL(tcf_exts_destroy);

int tcf_exts_validate(struct net *net, struct tcf_proto *tp, struct nlattr **tb,
		      struct nlattr *rate_tlv, struct tcf_exts *exts, bool ovr)
{
#ifdef CONFIG_NET_CLS_ACT
	{
		struct tc_action *act;

		if (exts->police && tb[exts->police]) {
			act = tcf_action_init_1(net, tp, tb[exts->police],
						rate_tlv, "police", ovr,
						TCA_ACT_BIND);
			if (IS_ERR(act))
				return PTR_ERR(act);

			act->type = exts->type = TCA_OLD_COMPAT;
			exts->actions[0] = act;
			exts->nr_actions = 1;
		} else if (exts->action && tb[exts->action]) {
			LIST_HEAD(actions);
			int err, i = 0;

			err = tcf_action_init(net, tp, tb[exts->action],
					      rate_tlv, NULL, ovr, TCA_ACT_BIND,
					      &actions);
			if (err)
				return err;
			list_for_each_entry(act, &actions, list)
				exts->actions[i++] = act;
			exts->nr_actions = i;
		}
	}
#else
	if ((exts->action && tb[exts->action]) ||
	    (exts->police && tb[exts->police]))
		return -EOPNOTSUPP;
#endif

	return 0;
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
			LIST_HEAD(actions);

			nest = nla_nest_start(skb, exts->action);
			if (nest == NULL)
				goto nla_put_failure;

			tcf_exts_to_list(exts, &actions);
			if (tcf_action_dump(skb, &actions, 0, 0) < 0)
				goto nla_put_failure;
			nla_nest_end(skb, nest);
		} else if (exts->police) {
			struct tc_action *act = tcf_exts_first_act(exts);
			nest = nla_nest_start(skb, exts->police);
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

int tcf_exts_get_dev(struct net_device *dev, struct tcf_exts *exts,
		     struct net_device **hw_dev)
{
#ifdef CONFIG_NET_CLS_ACT
	const struct tc_action *a;
	LIST_HEAD(actions);

	if (!tcf_exts_has_actions(exts))
		return -EINVAL;

	tcf_exts_to_list(exts, &actions);
	list_for_each_entry(a, &actions, list) {
		if (a->ops->get_dev) {
			a->ops->get_dev(a, dev_net(dev), hw_dev);
			break;
		}
	}
	if (*hw_dev)
		return 0;
#endif
	return -EOPNOTSUPP;
}
EXPORT_SYMBOL(tcf_exts_get_dev);

static int __init tc_filter_init(void)
{
	rtnl_register(PF_UNSPEC, RTM_NEWTFILTER, tc_ctl_tfilter, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_DELTFILTER, tc_ctl_tfilter, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_GETTFILTER, tc_ctl_tfilter,
		      tc_dump_tfilter, 0);

	return 0;
}

subsys_initcall(tc_filter_init);
