/*
 * net/sched/mirred.c	packet mirroring and redirect actions
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
#include <net/net_namespace.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_mirred.h>
#include <net/tc_act/tc_mirred.h>

#include <linux/if_arp.h>

#define MIRRED_TAB_MASK     7
static struct tcf_common *tcf_mirred_ht[MIRRED_TAB_MASK + 1];
static u32 mirred_idx_gen;
static DEFINE_RWLOCK(mirred_lock);

static struct tcf_hashinfo mirred_hash_info = {
	.htab	=	tcf_mirred_ht,
	.hmask	=	MIRRED_TAB_MASK,
	.lock	=	&mirred_lock,
};

static inline int tcf_mirred_release(struct tcf_mirred *m, int bind)
{
	if (m) {
		if (bind)
			m->tcf_bindcnt--;
		m->tcf_refcnt--;
		if(!m->tcf_bindcnt && m->tcf_refcnt <= 0) {
			dev_put(m->tcfm_dev);
			tcf_hash_destroy(&m->common, &mirred_hash_info);
			return 1;
		}
	}
	return 0;
}

static int tcf_mirred_init(struct rtattr *rta, struct rtattr *est,
			   struct tc_action *a, int ovr, int bind)
{
	struct rtattr *tb[TCA_MIRRED_MAX];
	struct tc_mirred *parm;
	struct tcf_mirred *m;
	struct tcf_common *pc;
	struct net_device *dev = NULL;
	int ret = 0;
	int ok_push = 0;

	if (rta == NULL || rtattr_parse_nested(tb, TCA_MIRRED_MAX, rta) < 0)
		return -EINVAL;

	if (tb[TCA_MIRRED_PARMS-1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_MIRRED_PARMS-1]) < sizeof(*parm))
		return -EINVAL;
	parm = RTA_DATA(tb[TCA_MIRRED_PARMS-1]);

	if (parm->ifindex) {
		dev = __dev_get_by_index(&init_net, parm->ifindex);
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
	}

	pc = tcf_hash_check(parm->index, a, bind, &mirred_hash_info);
	if (!pc) {
		if (!parm->ifindex)
			return -EINVAL;
		pc = tcf_hash_create(parm->index, est, a, sizeof(*m), bind,
				     &mirred_idx_gen, &mirred_hash_info);
		if (unlikely(!pc))
			return -ENOMEM;
		ret = ACT_P_CREATED;
	} else {
		if (!ovr) {
			tcf_mirred_release(to_mirred(pc), bind);
			return -EEXIST;
		}
	}
	m = to_mirred(pc);

	spin_lock_bh(&m->tcf_lock);
	m->tcf_action = parm->action;
	m->tcfm_eaction = parm->eaction;
	if (parm->ifindex) {
		m->tcfm_ifindex = parm->ifindex;
		if (ret != ACT_P_CREATED)
			dev_put(m->tcfm_dev);
		m->tcfm_dev = dev;
		dev_hold(dev);
		m->tcfm_ok_push = ok_push;
	}
	spin_unlock_bh(&m->tcf_lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(pc, &mirred_hash_info);

	return ret;
}

static int tcf_mirred_cleanup(struct tc_action *a, int bind)
{
	struct tcf_mirred *m = a->priv;

	if (m)
		return tcf_mirred_release(m, bind);
	return 0;
}

static int tcf_mirred(struct sk_buff *skb, struct tc_action *a,
		      struct tcf_result *res)
{
	struct tcf_mirred *m = a->priv;
	struct net_device *dev;
	struct sk_buff *skb2 = NULL;
	u32 at = G_TC_AT(skb->tc_verd);

	spin_lock(&m->tcf_lock);

	dev = m->tcfm_dev;
	m->tcf_tm.lastuse = jiffies;

	if (!(dev->flags&IFF_UP) ) {
		if (net_ratelimit())
			printk("mirred to Houston: device %s is gone!\n",
			       dev->name);
bad_mirred:
		if (skb2 != NULL)
			kfree_skb(skb2);
		m->tcf_qstats.overlimits++;
		m->tcf_bstats.bytes += skb->len;
		m->tcf_bstats.packets++;
		spin_unlock(&m->tcf_lock);
		/* should we be asking for packet to be dropped?
		 * may make sense for redirect case only
		*/
		return TC_ACT_SHOT;
	}

	skb2 = skb_act_clone(skb, GFP_ATOMIC);
	if (skb2 == NULL)
		goto bad_mirred;
	if (m->tcfm_eaction != TCA_EGRESS_MIRROR &&
	    m->tcfm_eaction != TCA_EGRESS_REDIR) {
		if (net_ratelimit())
			printk("tcf_mirred unknown action %d\n",
			       m->tcfm_eaction);
		goto bad_mirred;
	}

	m->tcf_bstats.bytes += skb2->len;
	m->tcf_bstats.packets++;
	if (!(at & AT_EGRESS))
		if (m->tcfm_ok_push)
			skb_push(skb2, skb2->dev->hard_header_len);

	/* mirror is always swallowed */
	if (m->tcfm_eaction != TCA_EGRESS_MIRROR)
		skb2->tc_verd = SET_TC_FROM(skb2->tc_verd, at);

	skb2->dev = dev;
	skb2->iif = skb->dev->ifindex;
	dev_queue_xmit(skb2);
	spin_unlock(&m->tcf_lock);
	return m->tcf_action;
}

static int tcf_mirred_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_mirred *m = a->priv;
	struct tc_mirred opt;
	struct tcf_t t;

	opt.index = m->tcf_index;
	opt.action = m->tcf_action;
	opt.refcnt = m->tcf_refcnt - ref;
	opt.bindcnt = m->tcf_bindcnt - bind;
	opt.eaction = m->tcfm_eaction;
	opt.ifindex = m->tcfm_ifindex;
	RTA_PUT(skb, TCA_MIRRED_PARMS, sizeof(opt), &opt);
	t.install = jiffies_to_clock_t(jiffies - m->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - m->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(m->tcf_tm.expires);
	RTA_PUT(skb, TCA_MIRRED_TM, sizeof(t), &t);
	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct tc_action_ops act_mirred_ops = {
	.kind		=	"mirred",
	.hinfo		=	&mirred_hash_info,
	.type		=	TCA_ACT_MIRRED,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_mirred,
	.dump		=	tcf_mirred_dump,
	.cleanup	=	tcf_mirred_cleanup,
	.lookup		=	tcf_hash_search,
	.init		=	tcf_mirred_init,
	.walk		=	tcf_generic_walker
};

MODULE_AUTHOR("Jamal Hadi Salim(2002)");
MODULE_DESCRIPTION("Device Mirror/redirect actions");
MODULE_LICENSE("GPL");

static int __init mirred_init_module(void)
{
	printk("Mirror/redirect action on\n");
	return tcf_register_action(&act_mirred_ops);
}

static void __exit mirred_cleanup_module(void)
{
	tcf_unregister_action(&act_mirred_ops);
}

module_init(mirred_init_module);
module_exit(mirred_cleanup_module);
