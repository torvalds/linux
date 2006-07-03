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

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_mirred.h>
#include <net/tc_act/tc_mirred.h>

#include <linux/etherdevice.h>
#include <linux/if_arp.h>


/* use generic hash table */
#define MY_TAB_SIZE     8
#define MY_TAB_MASK     (MY_TAB_SIZE - 1)
static u32 idx_gen;
static struct tcf_mirred *tcf_mirred_ht[MY_TAB_SIZE];
static DEFINE_RWLOCK(mirred_lock);

/* ovewrride the defaults */
#define tcf_st		tcf_mirred
#define tc_st		tc_mirred
#define tcf_t_lock	mirred_lock
#define tcf_ht		tcf_mirred_ht

#define CONFIG_NET_ACT_INIT 1
#include <net/pkt_act.h>

static inline int
tcf_mirred_release(struct tcf_mirred *p, int bind)
{
	if (p) {
		if (bind)
			p->bindcnt--;
		p->refcnt--;
		if(!p->bindcnt && p->refcnt <= 0) {
			dev_put(p->dev);
			tcf_hash_destroy(p);
			return 1;
		}
	}
	return 0;
}

static int
tcf_mirred_init(struct rtattr *rta, struct rtattr *est, struct tc_action *a,
                int ovr, int bind)
{
	struct rtattr *tb[TCA_MIRRED_MAX];
	struct tc_mirred *parm;
	struct tcf_mirred *p;
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
		dev = __dev_get_by_index(parm->ifindex);
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

	p = tcf_hash_check(parm->index, a, ovr, bind);
	if (p == NULL) {
		if (!parm->ifindex)
			return -EINVAL;
		p = tcf_hash_create(parm->index, est, a, sizeof(*p), ovr, bind);
		if (p == NULL)
			return -ENOMEM;
		ret = ACT_P_CREATED;
	} else {
		if (!ovr) {
			tcf_mirred_release(p, bind);
			return -EEXIST;
		}
	}

	spin_lock_bh(&p->lock);
	p->action = parm->action;
	p->eaction = parm->eaction;
	if (parm->ifindex) {
		p->ifindex = parm->ifindex;
		if (ret != ACT_P_CREATED)
			dev_put(p->dev);
		p->dev = dev;
		dev_hold(dev);
		p->ok_push = ok_push;
	}
	spin_unlock_bh(&p->lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(p);

	DPRINTK("tcf_mirred_init index %d action %d eaction %d device %s "
	        "ifindex %d\n", parm->index, parm->action, parm->eaction,
	        dev->name, parm->ifindex);
	return ret;
}

static int
tcf_mirred_cleanup(struct tc_action *a, int bind)
{
	struct tcf_mirred *p = PRIV(a, mirred);

	if (p != NULL)
		return tcf_mirred_release(p, bind);
	return 0;
}

static int
tcf_mirred(struct sk_buff *skb, struct tc_action *a, struct tcf_result *res)
{
	struct tcf_mirred *p = PRIV(a, mirred);
	struct net_device *dev;
	struct sk_buff *skb2 = NULL;
	u32 at = G_TC_AT(skb->tc_verd);

	spin_lock(&p->lock);

	dev = p->dev;
	p->tm.lastuse = jiffies;

	if (!(dev->flags&IFF_UP) ) {
		if (net_ratelimit())
			printk("mirred to Houston: device %s is gone!\n",
			       dev->name);
bad_mirred:
		if (skb2 != NULL)
			kfree_skb(skb2);
		p->qstats.overlimits++;
		p->bstats.bytes += skb->len;
		p->bstats.packets++;
		spin_unlock(&p->lock);
		/* should we be asking for packet to be dropped?
		 * may make sense for redirect case only
		*/
		return TC_ACT_SHOT;
	}

	skb2 = skb_clone(skb, GFP_ATOMIC);
	if (skb2 == NULL)
		goto bad_mirred;
	if (p->eaction != TCA_EGRESS_MIRROR && p->eaction != TCA_EGRESS_REDIR) {
		if (net_ratelimit())
			printk("tcf_mirred unknown action %d\n", p->eaction);
		goto bad_mirred;
	}

	p->bstats.bytes += skb2->len;
	p->bstats.packets++;
	if (!(at & AT_EGRESS))
		if (p->ok_push)
			skb_push(skb2, skb2->dev->hard_header_len);

	/* mirror is always swallowed */
	if (p->eaction != TCA_EGRESS_MIRROR)
		skb2->tc_verd = SET_TC_FROM(skb2->tc_verd, at);

	skb2->dev = dev;
	skb2->input_dev = skb->dev;
	dev_queue_xmit(skb2);
	spin_unlock(&p->lock);
	return p->action;
}

static int
tcf_mirred_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	unsigned char *b = skb->tail;
	struct tc_mirred opt;
	struct tcf_mirred *p = PRIV(a, mirred);
	struct tcf_t t;

	opt.index = p->index;
	opt.action = p->action;
	opt.refcnt = p->refcnt - ref;
	opt.bindcnt = p->bindcnt - bind;
	opt.eaction = p->eaction;
	opt.ifindex = p->ifindex;
	DPRINTK("tcf_mirred_dump index %d action %d eaction %d ifindex %d\n",
	         p->index, p->action, p->eaction, p->ifindex);
	RTA_PUT(skb, TCA_MIRRED_PARMS, sizeof(opt), &opt);
	t.install = jiffies_to_clock_t(jiffies - p->tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - p->tm.lastuse);
	t.expires = jiffies_to_clock_t(p->tm.expires);
	RTA_PUT(skb, TCA_MIRRED_TM, sizeof(t), &t);
	return skb->len;

      rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static struct tc_action_ops act_mirred_ops = {
	.kind		=	"mirred",
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

static int __init
mirred_init_module(void)
{
	printk("Mirror/redirect action on\n");
	return tcf_register_action(&act_mirred_ops);
}

static void __exit
mirred_cleanup_module(void)
{
	tcf_unregister_action(&act_mirred_ops);
}

module_init(mirred_init_module);
module_exit(mirred_cleanup_module);
