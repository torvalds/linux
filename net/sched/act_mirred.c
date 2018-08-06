/*
 * net/sched/act_mirred.c	packet mirroring and redirect actions
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Jamal Hadi Salim (2002-4)
 *
 * TODO: Add ingress support (and socket redirect support)
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/if_arp.h>
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <linux/tc_act/tc_mirred.h>
#include <net/tc_act/tc_mirred.h>

static LIST_HEAD(mirred_list);

static bool tcf_mirred_is_act_redirect(int action)
{
	return action == TCA_EGRESS_REDIR || action == TCA_INGRESS_REDIR;
}

static bool tcf_mirred_act_wants_ingress(int action)
{
	switch (action) {
	case TCA_EGRESS_REDIR:
	case TCA_EGRESS_MIRROR:
		return false;
	case TCA_INGRESS_REDIR:
	case TCA_INGRESS_MIRROR:
		return true;
	default:
		BUG();
	}
}

static bool tcf_mirred_can_reinsert(int action)
{
	switch (action) {
	case TC_ACT_SHOT:
	case TC_ACT_STOLEN:
	case TC_ACT_QUEUED:
	case TC_ACT_TRAP:
		return true;
	}
	return false;
}

static void tcf_mirred_release(struct tc_action *a)
{
	struct tcf_mirred *m = to_mirred(a);
	struct net_device *dev;

	list_del(&m->tcfm_list);
	dev = rtnl_dereference(m->tcfm_dev);
	if (dev)
		dev_put(dev);
}

static const struct nla_policy mirred_policy[TCA_MIRRED_MAX + 1] = {
	[TCA_MIRRED_PARMS]	= { .len = sizeof(struct tc_mirred) },
};

static unsigned int mirred_net_id;
static struct tc_action_ops act_mirred_ops;

static int tcf_mirred_init(struct net *net, struct nlattr *nla,
			   struct nlattr *est, struct tc_action **a,
			   int ovr, int bind, bool rtnl_held,
			   struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);
	struct nlattr *tb[TCA_MIRRED_MAX + 1];
	bool mac_header_xmit = false;
	struct tc_mirred *parm;
	struct tcf_mirred *m;
	struct net_device *dev;
	bool exists = false;
	int ret, err;

	if (!nla) {
		NL_SET_ERR_MSG_MOD(extack, "Mirred requires attributes to be passed");
		return -EINVAL;
	}
	ret = nla_parse_nested(tb, TCA_MIRRED_MAX, nla, mirred_policy, extack);
	if (ret < 0)
		return ret;
	if (!tb[TCA_MIRRED_PARMS]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing required mirred parameters");
		return -EINVAL;
	}
	parm = nla_data(tb[TCA_MIRRED_PARMS]);

	err = tcf_idr_check_alloc(tn, &parm->index, a, bind);
	if (err < 0)
		return err;
	exists = err;
	if (exists && bind)
		return 0;

	switch (parm->eaction) {
	case TCA_EGRESS_MIRROR:
	case TCA_EGRESS_REDIR:
	case TCA_INGRESS_REDIR:
	case TCA_INGRESS_MIRROR:
		break;
	default:
		if (exists)
			tcf_idr_release(*a, bind);
		else
			tcf_idr_cleanup(tn, parm->index);
		NL_SET_ERR_MSG_MOD(extack, "Unknown mirred option");
		return -EINVAL;
	}
	if (parm->ifindex) {
		dev = __dev_get_by_index(net, parm->ifindex);
		if (dev == NULL) {
			if (exists)
				tcf_idr_release(*a, bind);
			else
				tcf_idr_cleanup(tn, parm->index);
			return -ENODEV;
		}
		mac_header_xmit = dev_is_mac_header_xmit(dev);
	} else {
		dev = NULL;
	}

	if (!exists) {
		if (!dev) {
			tcf_idr_cleanup(tn, parm->index);
			NL_SET_ERR_MSG_MOD(extack, "Specified device does not exist");
			return -EINVAL;
		}
		ret = tcf_idr_create(tn, parm->index, est, a,
				     &act_mirred_ops, bind, true);
		if (ret) {
			tcf_idr_cleanup(tn, parm->index);
			return ret;
		}
		ret = ACT_P_CREATED;
	} else if (!ovr) {
		tcf_idr_release(*a, bind);
		return -EEXIST;
	}
	m = to_mirred(*a);

	ASSERT_RTNL();
	m->tcf_action = parm->action;
	m->tcfm_eaction = parm->eaction;
	if (dev != NULL) {
		if (ret != ACT_P_CREATED)
			dev_put(rcu_dereference_protected(m->tcfm_dev, 1));
		dev_hold(dev);
		rcu_assign_pointer(m->tcfm_dev, dev);
		m->tcfm_mac_header_xmit = mac_header_xmit;
	}

	if (ret == ACT_P_CREATED) {
		list_add(&m->tcfm_list, &mirred_list);
		tcf_idr_insert(tn, *a);
	}

	return ret;
}

static int tcf_mirred(struct sk_buff *skb, const struct tc_action *a,
		      struct tcf_result *res)
{
	struct tcf_mirred *m = to_mirred(a);
	struct sk_buff *skb2 = skb;
	bool m_mac_header_xmit;
	struct net_device *dev;
	int retval, err = 0;
	bool use_reinsert;
	bool want_ingress;
	bool is_redirect;
	int m_eaction;
	int mac_len;

	tcf_lastuse_update(&m->tcf_tm);
	bstats_cpu_update(this_cpu_ptr(m->common.cpu_bstats), skb);

	m_mac_header_xmit = READ_ONCE(m->tcfm_mac_header_xmit);
	m_eaction = READ_ONCE(m->tcfm_eaction);
	retval = READ_ONCE(m->tcf_action);
	dev = rcu_dereference_bh(m->tcfm_dev);
	if (unlikely(!dev)) {
		pr_notice_once("tc mirred: target device is gone\n");
		goto out;
	}

	if (unlikely(!(dev->flags & IFF_UP))) {
		net_notice_ratelimited("tc mirred to Houston: device %s is down\n",
				       dev->name);
		goto out;
	}

	/* we could easily avoid the clone only if called by ingress and clsact;
	 * since we can't easily detect the clsact caller, skip clone only for
	 * ingress - that covers the TC S/W datapath.
	 */
	is_redirect = tcf_mirred_is_act_redirect(m_eaction);
	use_reinsert = skb_at_tc_ingress(skb) && is_redirect &&
		       tcf_mirred_can_reinsert(retval);
	if (!use_reinsert) {
		skb2 = skb_clone(skb, GFP_ATOMIC);
		if (!skb2)
			goto out;
	}

	/* If action's target direction differs than filter's direction,
	 * and devices expect a mac header on xmit, then mac push/pull is
	 * needed.
	 */
	want_ingress = tcf_mirred_act_wants_ingress(m_eaction);
	if (skb_at_tc_ingress(skb) != want_ingress && m_mac_header_xmit) {
		if (!skb_at_tc_ingress(skb)) {
			/* caught at egress, act ingress: pull mac */
			mac_len = skb_network_header(skb) - skb_mac_header(skb);
			skb_pull_rcsum(skb2, mac_len);
		} else {
			/* caught at ingress, act egress: push mac */
			skb_push_rcsum(skb2, skb->mac_len);
		}
	}

	skb2->skb_iif = skb->dev->ifindex;
	skb2->dev = dev;

	/* mirror is always swallowed */
	if (is_redirect) {
		skb2->tc_redirected = 1;
		skb2->tc_from_ingress = skb2->tc_at_ingress;

		/* let's the caller reinsert the packet, if possible */
		if (use_reinsert) {
			res->ingress = want_ingress;
			res->qstats = this_cpu_ptr(m->common.cpu_qstats);
			return TC_ACT_REINSERT;
		}
	}

	if (!want_ingress)
		err = dev_queue_xmit(skb2);
	else
		err = netif_receive_skb(skb2);

	if (err) {
out:
		qstats_overlimit_inc(this_cpu_ptr(m->common.cpu_qstats));
		if (tcf_mirred_is_act_redirect(m_eaction))
			retval = TC_ACT_SHOT;
	}

	return retval;
}

static void tcf_stats_update(struct tc_action *a, u64 bytes, u32 packets,
			     u64 lastuse)
{
	struct tcf_mirred *m = to_mirred(a);
	struct tcf_t *tm = &m->tcf_tm;

	_bstats_cpu_update(this_cpu_ptr(a->cpu_bstats), bytes, packets);
	tm->lastuse = max_t(u64, tm->lastuse, lastuse);
}

static int tcf_mirred_dump(struct sk_buff *skb, struct tc_action *a, int bind,
			   int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_mirred *m = to_mirred(a);
	struct net_device *dev = rtnl_dereference(m->tcfm_dev);
	struct tc_mirred opt = {
		.index   = m->tcf_index,
		.action  = m->tcf_action,
		.refcnt  = refcount_read(&m->tcf_refcnt) - ref,
		.bindcnt = atomic_read(&m->tcf_bindcnt) - bind,
		.eaction = m->tcfm_eaction,
		.ifindex = dev ? dev->ifindex : 0,
	};
	struct tcf_t t;

	if (nla_put(skb, TCA_MIRRED_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	tcf_tm_dump(&t, &m->tcf_tm);
	if (nla_put_64bit(skb, TCA_MIRRED_TM, sizeof(t), &t, TCA_MIRRED_PAD))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int tcf_mirred_walker(struct net *net, struct sk_buff *skb,
			     struct netlink_callback *cb, int type,
			     const struct tc_action_ops *ops,
			     struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops, extack);
}

static int tcf_mirred_search(struct net *net, struct tc_action **a, u32 index,
			     struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);

	return tcf_idr_search(tn, a, index);
}

static int mirred_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct tcf_mirred *m;

	ASSERT_RTNL();
	if (event == NETDEV_UNREGISTER) {
		list_for_each_entry(m, &mirred_list, tcfm_list) {
			if (rcu_access_pointer(m->tcfm_dev) == dev) {
				dev_put(dev);
				/* Note : no rcu grace period necessary, as
				 * net_device are already rcu protected.
				 */
				RCU_INIT_POINTER(m->tcfm_dev, NULL);
			}
		}
	}

	return NOTIFY_DONE;
}

static struct notifier_block mirred_device_notifier = {
	.notifier_call = mirred_device_event,
};

static struct net_device *tcf_mirred_get_dev(const struct tc_action *a)
{
	struct tcf_mirred *m = to_mirred(a);

	return rtnl_dereference(m->tcfm_dev);
}

static int tcf_mirred_delete(struct net *net, u32 index)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);

	return tcf_idr_delete_index(tn, index);
}

static struct tc_action_ops act_mirred_ops = {
	.kind		=	"mirred",
	.type		=	TCA_ACT_MIRRED,
	.owner		=	THIS_MODULE,
	.act		=	tcf_mirred,
	.stats_update	=	tcf_stats_update,
	.dump		=	tcf_mirred_dump,
	.cleanup	=	tcf_mirred_release,
	.init		=	tcf_mirred_init,
	.walk		=	tcf_mirred_walker,
	.lookup		=	tcf_mirred_search,
	.size		=	sizeof(struct tcf_mirred),
	.get_dev	=	tcf_mirred_get_dev,
	.delete		=	tcf_mirred_delete,
};

static __net_init int mirred_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);

	return tc_action_net_init(tn, &act_mirred_ops);
}

static void __net_exit mirred_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, mirred_net_id);
}

static struct pernet_operations mirred_net_ops = {
	.init = mirred_init_net,
	.exit_batch = mirred_exit_net,
	.id   = &mirred_net_id,
	.size = sizeof(struct tc_action_net),
};

MODULE_AUTHOR("Jamal Hadi Salim(2002)");
MODULE_DESCRIPTION("Device Mirror/redirect actions");
MODULE_LICENSE("GPL");

static int __init mirred_init_module(void)
{
	int err = register_netdevice_notifier(&mirred_device_notifier);
	if (err)
		return err;

	pr_info("Mirror/redirect action on\n");
	return tcf_register_action(&act_mirred_ops, &mirred_net_ops);
}

static void __exit mirred_cleanup_module(void)
{
	tcf_unregister_action(&act_mirred_ops, &mirred_net_ops);
	unregister_netdevice_notifier(&mirred_device_notifier);
}

module_init(mirred_init_module);
module_exit(mirred_cleanup_module);
