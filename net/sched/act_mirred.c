// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/act_mirred.c	packet mirroring and redirect actions
 *
 * Authors:	Jamal Hadi Salim (2002-4)
 *
 * TODO: Add ingress support (and socket redirect support)
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
#include <net/dst.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>
#include <linux/tc_act/tc_mirred.h>
#include <net/tc_act/tc_mirred.h>
#include <net/tc_wrapper.h>

static LIST_HEAD(mirred_list);
static DEFINE_SPINLOCK(mirred_list_lock);

#define MIRRED_NEST_LIMIT    4
static DEFINE_PER_CPU(unsigned int, mirred_nest_level);

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

static struct net_device *tcf_mirred_dev_dereference(struct tcf_mirred *m)
{
	return rcu_dereference_protected(m->tcfm_dev,
					 lockdep_is_held(&m->tcf_lock));
}

static void tcf_mirred_release(struct tc_action *a)
{
	struct tcf_mirred *m = to_mirred(a);
	struct net_device *dev;

	spin_lock(&mirred_list_lock);
	list_del(&m->tcfm_list);
	spin_unlock(&mirred_list_lock);

	/* last reference to action, no need to lock */
	dev = rcu_dereference_protected(m->tcfm_dev, 1);
	netdev_put(dev, &m->tcfm_dev_tracker);
}

static const struct nla_policy mirred_policy[TCA_MIRRED_MAX + 1] = {
	[TCA_MIRRED_PARMS]	= { .len = sizeof(struct tc_mirred) },
	[TCA_MIRRED_BLOCKID]	= NLA_POLICY_MIN(NLA_U32, 1),
};

static struct tc_action_ops act_mirred_ops;

static void tcf_mirred_replace_dev(struct tcf_mirred *m,
				   struct net_device *ndev)
{
	struct net_device *odev;

	odev = rcu_replace_pointer(m->tcfm_dev, ndev,
				   lockdep_is_held(&m->tcf_lock));
	netdev_put(odev, &m->tcfm_dev_tracker);
}

static int tcf_mirred_init(struct net *net, struct nlattr *nla,
			   struct nlattr *est, struct tc_action **a,
			   struct tcf_proto *tp,
			   u32 flags, struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, act_mirred_ops.net_id);
	bool bind = flags & TCA_ACT_FLAGS_BIND;
	struct nlattr *tb[TCA_MIRRED_MAX + 1];
	struct tcf_chain *goto_ch = NULL;
	bool mac_header_xmit = false;
	struct tc_mirred *parm;
	struct tcf_mirred *m;
	bool exists = false;
	int ret, err;
	u32 index;

	if (!nla) {
		NL_SET_ERR_MSG_MOD(extack, "Mirred requires attributes to be passed");
		return -EINVAL;
	}
	ret = nla_parse_nested_deprecated(tb, TCA_MIRRED_MAX, nla,
					  mirred_policy, extack);
	if (ret < 0)
		return ret;
	if (!tb[TCA_MIRRED_PARMS]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing required mirred parameters");
		return -EINVAL;
	}
	parm = nla_data(tb[TCA_MIRRED_PARMS]);
	index = parm->index;
	err = tcf_idr_check_alloc(tn, &index, a, bind);
	if (err < 0)
		return err;
	exists = err;
	if (exists && bind)
		return ACT_P_BOUND;

	if (tb[TCA_MIRRED_BLOCKID] && parm->ifindex) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot specify Block ID and dev simultaneously");
		if (exists)
			tcf_idr_release(*a, bind);
		else
			tcf_idr_cleanup(tn, index);

		return -EINVAL;
	}

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
			tcf_idr_cleanup(tn, index);
		NL_SET_ERR_MSG_MOD(extack, "Unknown mirred option");
		return -EINVAL;
	}

	if (!exists) {
		if (!parm->ifindex && !tb[TCA_MIRRED_BLOCKID]) {
			tcf_idr_cleanup(tn, index);
			NL_SET_ERR_MSG_MOD(extack,
					   "Must specify device or block");
			return -EINVAL;
		}
		ret = tcf_idr_create_from_flags(tn, index, est, a,
						&act_mirred_ops, bind, flags);
		if (ret) {
			tcf_idr_cleanup(tn, index);
			return ret;
		}
		ret = ACT_P_CREATED;
	} else if (!(flags & TCA_ACT_FLAGS_REPLACE)) {
		tcf_idr_release(*a, bind);
		return -EEXIST;
	}

	m = to_mirred(*a);
	if (ret == ACT_P_CREATED)
		INIT_LIST_HEAD(&m->tcfm_list);

	err = tcf_action_check_ctrlact(parm->action, tp, &goto_ch, extack);
	if (err < 0)
		goto release_idr;

	spin_lock_bh(&m->tcf_lock);

	if (parm->ifindex) {
		struct net_device *ndev;

		ndev = dev_get_by_index(net, parm->ifindex);
		if (!ndev) {
			spin_unlock_bh(&m->tcf_lock);
			err = -ENODEV;
			goto put_chain;
		}
		mac_header_xmit = dev_is_mac_header_xmit(ndev);
		tcf_mirred_replace_dev(m, ndev);
		netdev_tracker_alloc(ndev, &m->tcfm_dev_tracker, GFP_ATOMIC);
		m->tcfm_mac_header_xmit = mac_header_xmit;
		m->tcfm_blockid = 0;
	} else if (tb[TCA_MIRRED_BLOCKID]) {
		tcf_mirred_replace_dev(m, NULL);
		m->tcfm_mac_header_xmit = false;
		m->tcfm_blockid = nla_get_u32(tb[TCA_MIRRED_BLOCKID]);
	}
	goto_ch = tcf_action_set_ctrlact(*a, parm->action, goto_ch);
	m->tcfm_eaction = parm->eaction;
	spin_unlock_bh(&m->tcf_lock);
	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);

	if (ret == ACT_P_CREATED) {
		spin_lock(&mirred_list_lock);
		list_add(&m->tcfm_list, &mirred_list);
		spin_unlock(&mirred_list_lock);
	}

	return ret;
put_chain:
	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);
release_idr:
	tcf_idr_release(*a, bind);
	return err;
}

static int
tcf_mirred_forward(bool at_ingress, bool want_ingress, struct sk_buff *skb)
{
	int err;

	if (!want_ingress)
		err = tcf_dev_queue_xmit(skb, dev_queue_xmit);
	else if (!at_ingress)
		err = netif_rx(skb);
	else
		err = netif_receive_skb(skb);

	return err;
}

static int tcf_mirred_to_dev(struct sk_buff *skb, struct tcf_mirred *m,
			     struct net_device *dev,
			     const bool m_mac_header_xmit, int m_eaction,
			     int retval)
{
	struct sk_buff *skb_to_send = skb;
	bool want_ingress;
	bool is_redirect;
	bool expects_nh;
	bool at_ingress;
	bool dont_clone;
	int mac_len;
	bool at_nh;
	int err;

	is_redirect = tcf_mirred_is_act_redirect(m_eaction);
	if (unlikely(!(dev->flags & IFF_UP)) || !netif_carrier_ok(dev)) {
		net_notice_ratelimited("tc mirred to Houston: device %s is down\n",
				       dev->name);
		goto err_cant_do;
	}

	/* we could easily avoid the clone only if called by ingress and clsact;
	 * since we can't easily detect the clsact caller, skip clone only for
	 * ingress - that covers the TC S/W datapath.
	 */
	at_ingress = skb_at_tc_ingress(skb);
	dont_clone = skb_at_tc_ingress(skb) && is_redirect &&
		tcf_mirred_can_reinsert(retval);
	if (!dont_clone) {
		skb_to_send = skb_clone(skb, GFP_ATOMIC);
		if (!skb_to_send)
			goto err_cant_do;
	}

	want_ingress = tcf_mirred_act_wants_ingress(m_eaction);

	/* All mirred/redirected skbs should clear previous ct info */
	nf_reset_ct(skb_to_send);
	if (want_ingress && !at_ingress) /* drop dst for egress -> ingress */
		skb_dst_drop(skb_to_send);

	expects_nh = want_ingress || !m_mac_header_xmit;
	at_nh = skb->data == skb_network_header(skb);
	if (at_nh != expects_nh) {
		mac_len = at_ingress ? skb->mac_len :
			  skb_network_offset(skb);
		if (expects_nh) {
			/* target device/action expect data at nh */
			skb_pull_rcsum(skb_to_send, mac_len);
		} else {
			/* target device/action expect data at mac */
			skb_push_rcsum(skb_to_send, mac_len);
		}
	}

	skb_to_send->skb_iif = skb->dev->ifindex;
	skb_to_send->dev = dev;

	if (is_redirect) {
		if (skb == skb_to_send)
			retval = TC_ACT_CONSUMED;

		skb_set_redirected(skb_to_send, skb_to_send->tc_at_ingress);

		err = tcf_mirred_forward(at_ingress, want_ingress, skb_to_send);
	} else {
		err = tcf_mirred_forward(at_ingress, want_ingress, skb_to_send);
	}
	if (err)
		tcf_action_inc_overlimit_qstats(&m->common);

	return retval;

err_cant_do:
	if (is_redirect)
		retval = TC_ACT_SHOT;
	tcf_action_inc_overlimit_qstats(&m->common);
	return retval;
}

static int tcf_blockcast_redir(struct sk_buff *skb, struct tcf_mirred *m,
			       struct tcf_block *block, int m_eaction,
			       const u32 exception_ifindex, int retval)
{
	struct net_device *dev_prev = NULL;
	struct net_device *dev = NULL;
	unsigned long index;
	int mirred_eaction;

	mirred_eaction = tcf_mirred_act_wants_ingress(m_eaction) ?
		TCA_INGRESS_MIRROR : TCA_EGRESS_MIRROR;

	xa_for_each(&block->ports, index, dev) {
		if (index == exception_ifindex)
			continue;

		if (!dev_prev)
			goto assign_prev;

		tcf_mirred_to_dev(skb, m, dev_prev,
				  dev_is_mac_header_xmit(dev),
				  mirred_eaction, retval);
assign_prev:
		dev_prev = dev;
	}

	if (dev_prev)
		return tcf_mirred_to_dev(skb, m, dev_prev,
					 dev_is_mac_header_xmit(dev_prev),
					 m_eaction, retval);

	return retval;
}

static int tcf_blockcast_mirror(struct sk_buff *skb, struct tcf_mirred *m,
				struct tcf_block *block, int m_eaction,
				const u32 exception_ifindex, int retval)
{
	struct net_device *dev = NULL;
	unsigned long index;

	xa_for_each(&block->ports, index, dev) {
		if (index == exception_ifindex)
			continue;

		tcf_mirred_to_dev(skb, m, dev,
				  dev_is_mac_header_xmit(dev),
				  m_eaction, retval);
	}

	return retval;
}

static int tcf_blockcast(struct sk_buff *skb, struct tcf_mirred *m,
			 const u32 blockid, struct tcf_result *res,
			 int retval)
{
	const u32 exception_ifindex = skb->dev->ifindex;
	struct tcf_block *block;
	bool is_redirect;
	int m_eaction;

	m_eaction = READ_ONCE(m->tcfm_eaction);
	is_redirect = tcf_mirred_is_act_redirect(m_eaction);

	/* we are already under rcu protection, so can call block lookup
	 * directly.
	 */
	block = tcf_block_lookup(dev_net(skb->dev), blockid);
	if (!block || xa_empty(&block->ports)) {
		tcf_action_inc_overlimit_qstats(&m->common);
		return retval;
	}

	if (is_redirect)
		return tcf_blockcast_redir(skb, m, block, m_eaction,
					   exception_ifindex, retval);

	/* If it's not redirect, it is mirror */
	return tcf_blockcast_mirror(skb, m, block, m_eaction, exception_ifindex,
				    retval);
}

TC_INDIRECT_SCOPE int tcf_mirred_act(struct sk_buff *skb,
				     const struct tc_action *a,
				     struct tcf_result *res)
{
	struct tcf_mirred *m = to_mirred(a);
	int retval = READ_ONCE(m->tcf_action);
	unsigned int nest_level;
	bool m_mac_header_xmit;
	struct net_device *dev;
	int m_eaction;
	u32 blockid;

	nest_level = __this_cpu_inc_return(mirred_nest_level);
	if (unlikely(nest_level > MIRRED_NEST_LIMIT)) {
		net_warn_ratelimited("Packet exceeded mirred recursion limit on dev %s\n",
				     netdev_name(skb->dev));
		retval = TC_ACT_SHOT;
		goto dec_nest_level;
	}

	tcf_lastuse_update(&m->tcf_tm);
	tcf_action_update_bstats(&m->common, skb);

	blockid = READ_ONCE(m->tcfm_blockid);
	if (blockid) {
		retval = tcf_blockcast(skb, m, blockid, res, retval);
		goto dec_nest_level;
	}

	dev = rcu_dereference_bh(m->tcfm_dev);
	if (unlikely(!dev)) {
		pr_notice_once("tc mirred: target device is gone\n");
		tcf_action_inc_overlimit_qstats(&m->common);
		goto dec_nest_level;
	}

	m_mac_header_xmit = READ_ONCE(m->tcfm_mac_header_xmit);
	m_eaction = READ_ONCE(m->tcfm_eaction);

	retval = tcf_mirred_to_dev(skb, m, dev, m_mac_header_xmit, m_eaction,
				   retval);

dec_nest_level:
	__this_cpu_dec(mirred_nest_level);

	return retval;
}

static void tcf_stats_update(struct tc_action *a, u64 bytes, u64 packets,
			     u64 drops, u64 lastuse, bool hw)
{
	struct tcf_mirred *m = to_mirred(a);
	struct tcf_t *tm = &m->tcf_tm;

	tcf_action_update_stats(a, bytes, packets, drops, hw);
	tm->lastuse = max_t(u64, tm->lastuse, lastuse);
}

static int tcf_mirred_dump(struct sk_buff *skb, struct tc_action *a, int bind,
			   int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_mirred *m = to_mirred(a);
	struct tc_mirred opt = {
		.index   = m->tcf_index,
		.refcnt  = refcount_read(&m->tcf_refcnt) - ref,
		.bindcnt = atomic_read(&m->tcf_bindcnt) - bind,
	};
	struct net_device *dev;
	struct tcf_t t;
	u32 blockid;

	spin_lock_bh(&m->tcf_lock);
	opt.action = m->tcf_action;
	opt.eaction = m->tcfm_eaction;
	dev = tcf_mirred_dev_dereference(m);
	if (dev)
		opt.ifindex = dev->ifindex;

	if (nla_put(skb, TCA_MIRRED_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	blockid = m->tcfm_blockid;
	if (blockid && nla_put_u32(skb, TCA_MIRRED_BLOCKID, blockid))
		goto nla_put_failure;

	tcf_tm_dump(&t, &m->tcf_tm);
	if (nla_put_64bit(skb, TCA_MIRRED_TM, sizeof(t), &t, TCA_MIRRED_PAD))
		goto nla_put_failure;
	spin_unlock_bh(&m->tcf_lock);

	return skb->len;

nla_put_failure:
	spin_unlock_bh(&m->tcf_lock);
	nlmsg_trim(skb, b);
	return -1;
}

static int mirred_device_event(struct notifier_block *unused,
			       unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct tcf_mirred *m;

	ASSERT_RTNL();
	if (event == NETDEV_UNREGISTER) {
		spin_lock(&mirred_list_lock);
		list_for_each_entry(m, &mirred_list, tcfm_list) {
			spin_lock_bh(&m->tcf_lock);
			if (tcf_mirred_dev_dereference(m) == dev) {
				netdev_put(dev, &m->tcfm_dev_tracker);
				/* Note : no rcu grace period necessary, as
				 * net_device are already rcu protected.
				 */
				RCU_INIT_POINTER(m->tcfm_dev, NULL);
			}
			spin_unlock_bh(&m->tcf_lock);
		}
		spin_unlock(&mirred_list_lock);
	}

	return NOTIFY_DONE;
}

static struct notifier_block mirred_device_notifier = {
	.notifier_call = mirred_device_event,
};

static void tcf_mirred_dev_put(void *priv)
{
	struct net_device *dev = priv;

	dev_put(dev);
}

static struct net_device *
tcf_mirred_get_dev(const struct tc_action *a,
		   tc_action_priv_destructor *destructor)
{
	struct tcf_mirred *m = to_mirred(a);
	struct net_device *dev;

	rcu_read_lock();
	dev = rcu_dereference(m->tcfm_dev);
	if (dev) {
		dev_hold(dev);
		*destructor = tcf_mirred_dev_put;
	}
	rcu_read_unlock();

	return dev;
}

static size_t tcf_mirred_get_fill_size(const struct tc_action *act)
{
	return nla_total_size(sizeof(struct tc_mirred));
}

static void tcf_offload_mirred_get_dev(struct flow_action_entry *entry,
				       const struct tc_action *act)
{
	entry->dev = act->ops->get_dev(act, &entry->destructor);
	if (!entry->dev)
		return;
	entry->destructor_priv = entry->dev;
}

static int tcf_mirred_offload_act_setup(struct tc_action *act, void *entry_data,
					u32 *index_inc, bool bind,
					struct netlink_ext_ack *extack)
{
	if (bind) {
		struct flow_action_entry *entry = entry_data;

		if (is_tcf_mirred_egress_redirect(act)) {
			entry->id = FLOW_ACTION_REDIRECT;
			tcf_offload_mirred_get_dev(entry, act);
		} else if (is_tcf_mirred_egress_mirror(act)) {
			entry->id = FLOW_ACTION_MIRRED;
			tcf_offload_mirred_get_dev(entry, act);
		} else if (is_tcf_mirred_ingress_redirect(act)) {
			entry->id = FLOW_ACTION_REDIRECT_INGRESS;
			tcf_offload_mirred_get_dev(entry, act);
		} else if (is_tcf_mirred_ingress_mirror(act)) {
			entry->id = FLOW_ACTION_MIRRED_INGRESS;
			tcf_offload_mirred_get_dev(entry, act);
		} else {
			NL_SET_ERR_MSG_MOD(extack, "Unsupported mirred offload");
			return -EOPNOTSUPP;
		}
		*index_inc = 1;
	} else {
		struct flow_offload_action *fl_action = entry_data;

		if (is_tcf_mirred_egress_redirect(act))
			fl_action->id = FLOW_ACTION_REDIRECT;
		else if (is_tcf_mirred_egress_mirror(act))
			fl_action->id = FLOW_ACTION_MIRRED;
		else if (is_tcf_mirred_ingress_redirect(act))
			fl_action->id = FLOW_ACTION_REDIRECT_INGRESS;
		else if (is_tcf_mirred_ingress_mirror(act))
			fl_action->id = FLOW_ACTION_MIRRED_INGRESS;
		else
			return -EOPNOTSUPP;
	}

	return 0;
}

static struct tc_action_ops act_mirred_ops = {
	.kind		=	"mirred",
	.id		=	TCA_ID_MIRRED,
	.owner		=	THIS_MODULE,
	.act		=	tcf_mirred_act,
	.stats_update	=	tcf_stats_update,
	.dump		=	tcf_mirred_dump,
	.cleanup	=	tcf_mirred_release,
	.init		=	tcf_mirred_init,
	.get_fill_size	=	tcf_mirred_get_fill_size,
	.offload_act_setup =	tcf_mirred_offload_act_setup,
	.size		=	sizeof(struct tcf_mirred),
	.get_dev	=	tcf_mirred_get_dev,
};

static __net_init int mirred_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, act_mirred_ops.net_id);

	return tc_action_net_init(net, tn, &act_mirred_ops);
}

static void __net_exit mirred_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, act_mirred_ops.net_id);
}

static struct pernet_operations mirred_net_ops = {
	.init = mirred_init_net,
	.exit_batch = mirred_exit_net,
	.id   = &act_mirred_ops.net_id,
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
	err = tcf_register_action(&act_mirred_ops, &mirred_net_ops);
	if (err)
		unregister_netdevice_notifier(&mirred_device_notifier);

	return err;
}

static void __exit mirred_cleanup_module(void)
{
	tcf_unregister_action(&act_mirred_ops, &mirred_net_ops);
	unregister_netdevice_notifier(&mirred_device_notifier);
}

module_init(mirred_init_module);
module_exit(mirred_cleanup_module);
