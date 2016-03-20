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
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_mirred.h>
#include <net/tc_act/tc_mirred.h>

#include <linux/if_arp.h>

#define MIRRED_TAB_MASK     7
static LIST_HEAD(mirred_list);
static DEFINE_SPINLOCK(mirred_list_lock);

static void tcf_mirred_release(struct tc_action *a, int bind)
{
	struct tcf_mirred *m = to_mirred(a);
	struct net_device *dev = rcu_dereference_protected(m->tcfm_dev, 1);

	/* We could be called either in a RCU callback or with RTNL lock held. */
	spin_lock_bh(&mirred_list_lock);
	list_del(&m->tcfm_list);
	spin_unlock_bh(&mirred_list_lock);
	if (dev)
		dev_put(dev);
}

static const struct nla_policy mirred_policy[TCA_MIRRED_MAX + 1] = {
	[TCA_MIRRED_PARMS]	= { .len = sizeof(struct tc_mirred) },
};

static int mirred_net_id;

static int tcf_mirred_init(struct net *net, struct nlattr *nla,
			   struct nlattr *est, struct tc_action *a, int ovr,
			   int bind)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);
	struct nlattr *tb[TCA_MIRRED_MAX + 1];
	struct tc_mirred *parm;
	struct tcf_mirred *m;
	struct net_device *dev;
	int ret, ok_push = 0;

	if (nla == NULL)
		return -EINVAL;
	ret = nla_parse_nested(tb, TCA_MIRRED_MAX, nla, mirred_policy);
	if (ret < 0)
		return ret;
	if (tb[TCA_MIRRED_PARMS] == NULL)
		return -EINVAL;
	parm = nla_data(tb[TCA_MIRRED_PARMS]);
	switch (parm->eaction) {
	case TCA_EGRESS_MIRROR:
	case TCA_EGRESS_REDIR:
		break;
	default:
		return -EINVAL;
	}
	if (parm->ifindex) {
		dev = __dev_get_by_index(net, parm->ifindex);
		if (dev == NULL)
			return -ENODEV;
		switch (dev->type) {
		case ARPHRD_TUNNEL:
		case ARPHRD_TUNNEL6:
		case ARPHRD_SIT:
		case ARPHRD_IPGRE:
		case ARPHRD_VOID:
		case ARPHRD_NONE:
			ok_push = 0;
			break;
		default:
			ok_push = 1;
			break;
		}
	} else {
		dev = NULL;
	}

	if (!tcf_hash_check(tn, parm->index, a, bind)) {
		if (dev == NULL)
			return -EINVAL;
		ret = tcf_hash_create(tn, parm->index, est, a,
				      sizeof(*m), bind, true);
		if (ret)
			return ret;
		ret = ACT_P_CREATED;
	} else {
		if (bind)
			return 0;

		tcf_hash_release(a, bind);
		if (!ovr)
			return -EEXIST;
	}
	m = to_mirred(a);

	ASSERT_RTNL();
	m->tcf_action = parm->action;
	m->tcfm_eaction = parm->eaction;
	if (dev != NULL) {
		m->tcfm_ifindex = parm->ifindex;
		if (ret != ACT_P_CREATED)
			dev_put(rcu_dereference_protected(m->tcfm_dev, 1));
		dev_hold(dev);
		rcu_assign_pointer(m->tcfm_dev, dev);
		m->tcfm_ok_push = ok_push;
	}

	if (ret == ACT_P_CREATED) {
		spin_lock_bh(&mirred_list_lock);
		list_add(&m->tcfm_list, &mirred_list);
		spin_unlock_bh(&mirred_list_lock);
		tcf_hash_insert(tn, a);
	}

	return ret;
}

static int tcf_mirred(struct sk_buff *skb, const struct tc_action *a,
		      struct tcf_result *res)
{
	struct tcf_mirred *m = a->priv;
	struct net_device *dev;
	struct sk_buff *skb2;
	int retval, err;
	u32 at;

	tcf_lastuse_update(&m->tcf_tm);

	bstats_cpu_update(this_cpu_ptr(m->common.cpu_bstats), skb);

	rcu_read_lock();
	retval = READ_ONCE(m->tcf_action);
	dev = rcu_dereference(m->tcfm_dev);
	if (unlikely(!dev)) {
		pr_notice_once("tc mirred: target device is gone\n");
		goto out;
	}

	if (unlikely(!(dev->flags & IFF_UP))) {
		net_notice_ratelimited("tc mirred to Houston: device %s is down\n",
				       dev->name);
		goto out;
	}

	at = G_TC_AT(skb->tc_verd);
	skb2 = skb_clone(skb, GFP_ATOMIC);
	if (!skb2)
		goto out;

	if (!(at & AT_EGRESS)) {
		if (m->tcfm_ok_push)
			skb_push(skb2, skb->mac_len);
	}

	/* mirror is always swallowed */
	if (m->tcfm_eaction != TCA_EGRESS_MIRROR)
		skb2->tc_verd = SET_TC_FROM(skb2->tc_verd, at);

	skb2->skb_iif = skb->dev->ifindex;
	skb2->dev = dev;
	err = dev_queue_xmit(skb2);

	if (err) {
out:
		qstats_overlimit_inc(this_cpu_ptr(m->common.cpu_qstats));
		if (m->tcfm_eaction != TCA_EGRESS_MIRROR)
			retval = TC_ACT_SHOT;
	}
	rcu_read_unlock();

	return retval;
}

static int tcf_mirred_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_mirred *m = a->priv;
	struct tc_mirred opt = {
		.index   = m->tcf_index,
		.action  = m->tcf_action,
		.refcnt  = m->tcf_refcnt - ref,
		.bindcnt = m->tcf_bindcnt - bind,
		.eaction = m->tcfm_eaction,
		.ifindex = m->tcfm_ifindex,
	};
	struct tcf_t t;

	if (nla_put(skb, TCA_MIRRED_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;
	t.install = jiffies_to_clock_t(jiffies - m->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - m->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(m->tcf_tm.expires);
	if (nla_put(skb, TCA_MIRRED_TM, sizeof(t), &t))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int tcf_mirred_walker(struct net *net, struct sk_buff *skb,
			     struct netlink_callback *cb, int type,
			     struct tc_action *a)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);

	return tcf_generic_walker(tn, skb, cb, type, a);
}

static int tcf_mirred_search(struct net *net, struct tc_action *a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);

	return tcf_hash_search(tn, a, index);
}

static int mirred_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct tcf_mirred *m;

	ASSERT_RTNL();
	if (event == NETDEV_UNREGISTER) {
		spin_lock_bh(&mirred_list_lock);
		list_for_each_entry(m, &mirred_list, tcfm_list) {
			if (rcu_access_pointer(m->tcfm_dev) == dev) {
				dev_put(dev);
				/* Note : no rcu grace period necessary, as
				 * net_device are already rcu protected.
				 */
				RCU_INIT_POINTER(m->tcfm_dev, NULL);
			}
		}
		spin_unlock_bh(&mirred_list_lock);
	}

	return NOTIFY_DONE;
}

static struct notifier_block mirred_device_notifier = {
	.notifier_call = mirred_device_event,
};

static struct tc_action_ops act_mirred_ops = {
	.kind		=	"mirred",
	.type		=	TCA_ACT_MIRRED,
	.owner		=	THIS_MODULE,
	.act		=	tcf_mirred,
	.dump		=	tcf_mirred_dump,
	.cleanup	=	tcf_mirred_release,
	.init		=	tcf_mirred_init,
	.walk		=	tcf_mirred_walker,
	.lookup		=	tcf_mirred_search,
};

static __net_init int mirred_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);

	return tc_action_net_init(tn, &act_mirred_ops, MIRRED_TAB_MASK);
}

static void __net_exit mirred_exit_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, mirred_net_id);

	tc_action_net_exit(tn);
}

static struct pernet_operations mirred_net_ops = {
	.init = mirred_init_net,
	.exit = mirred_exit_net,
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
