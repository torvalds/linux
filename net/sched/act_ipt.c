/*
 * net/sched/ipt.c	iptables target interface
 *
 *TODO: Add other tables. For now we only support the ipv4 table targets
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Copyright:	Jamal Hadi Salim (2002-4)
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
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
#include <linux/kmod.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_ipt.h>
#include <net/tc_act/tc_ipt.h>

#include <linux/netfilter_ipv4/ip_tables.h>

/* use generic hash table */
#define MY_TAB_SIZE     16
#define MY_TAB_MASK     15

static u32 idx_gen;
static struct tcf_ipt *tcf_ipt_ht[MY_TAB_SIZE];
/* ipt hash table lock */
static DEFINE_RWLOCK(ipt_lock);

/* ovewrride the defaults */
#define tcf_st		tcf_ipt
#define tcf_t_lock	ipt_lock
#define tcf_ht		tcf_ipt_ht

#define CONFIG_NET_ACT_INIT
#include <net/pkt_act.h>

static int
ipt_init_target(struct ipt_entry_target *t, char *table, unsigned int hook)
{
	struct ipt_target *target;
	int ret = 0;

	target = xt_find_target(AF_INET, t->u.user.name, t->u.user.revision);
	if (!target)
		return -ENOENT;

	DPRINTK("ipt_init_target: found %s\n", target->name);
	t->u.kernel.target = target;

	ret = xt_check_target(target, AF_INET, t->u.target_size - sizeof(*t),
			      table, hook, 0, 0);
	if (ret)
		return ret;

	if (t->u.kernel.target->checkentry
	    && !t->u.kernel.target->checkentry(table, NULL,
		    			       t->u.kernel.target, t->data,
					       t->u.target_size - sizeof(*t),
					       hook)) {
		DPRINTK("ipt_init_target: check failed for `%s'.\n",
			t->u.kernel.target->name);
		module_put(t->u.kernel.target->me);
		ret = -EINVAL;
	}

	return ret;
}

static void
ipt_destroy_target(struct ipt_entry_target *t)
{
	if (t->u.kernel.target->destroy)
		t->u.kernel.target->destroy(t->u.kernel.target, t->data,
		                            t->u.target_size - sizeof(*t));
        module_put(t->u.kernel.target->me);
}

static int
tcf_ipt_release(struct tcf_ipt *p, int bind)
{
	int ret = 0;
	if (p) {
		if (bind)
			p->bindcnt--;
		p->refcnt--;
		if (p->bindcnt <= 0 && p->refcnt <= 0) {
			ipt_destroy_target(p->t);
			kfree(p->tname);
			kfree(p->t);
			tcf_hash_destroy(p);
			ret = ACT_P_DELETED;
		}
	}
	return ret;
}

static int
tcf_ipt_init(struct rtattr *rta, struct rtattr *est, struct tc_action *a,
             int ovr, int bind)
{
	struct rtattr *tb[TCA_IPT_MAX];
	struct tcf_ipt *p;
	struct ipt_entry_target *td, *t;
	char *tname;
	int ret = 0, err;
	u32 hook = 0;
	u32 index = 0;

	if (rta == NULL || rtattr_parse_nested(tb, TCA_IPT_MAX, rta) < 0)
		return -EINVAL;

	if (tb[TCA_IPT_HOOK-1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_IPT_HOOK-1]) < sizeof(u32))
		return -EINVAL;
	if (tb[TCA_IPT_TARG-1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_IPT_TARG-1]) < sizeof(*t))
		return -EINVAL;
	td = (struct ipt_entry_target *)RTA_DATA(tb[TCA_IPT_TARG-1]);
	if (RTA_PAYLOAD(tb[TCA_IPT_TARG-1]) < td->u.target_size)
		return -EINVAL;

	if (tb[TCA_IPT_INDEX-1] != NULL &&
	    RTA_PAYLOAD(tb[TCA_IPT_INDEX-1]) >= sizeof(u32))
		index = *(u32 *)RTA_DATA(tb[TCA_IPT_INDEX-1]);

	p = tcf_hash_check(index, a, ovr, bind);
	if (p == NULL) {
		p = tcf_hash_create(index, est, a, sizeof(*p), ovr, bind);
		if (p == NULL)
			return -ENOMEM;
		ret = ACT_P_CREATED;
	} else {
		if (!ovr) {
			tcf_ipt_release(p, bind);
			return -EEXIST;
		}
	}

	hook = *(u32 *)RTA_DATA(tb[TCA_IPT_HOOK-1]);

	err = -ENOMEM;
	tname = kmalloc(IFNAMSIZ, GFP_KERNEL);
	if (tname == NULL)
		goto err1;
	if (tb[TCA_IPT_TABLE - 1] == NULL ||
	    rtattr_strlcpy(tname, tb[TCA_IPT_TABLE-1], IFNAMSIZ) >= IFNAMSIZ)
		strcpy(tname, "mangle");

	t = kmalloc(td->u.target_size, GFP_KERNEL);
	if (t == NULL)
		goto err2;
	memcpy(t, td, td->u.target_size);

	if ((err = ipt_init_target(t, tname, hook)) < 0)
		goto err3;

	spin_lock_bh(&p->lock);
	if (ret != ACT_P_CREATED) {
		ipt_destroy_target(p->t);
		kfree(p->tname);
		kfree(p->t);
	}
	p->tname = tname;
	p->t     = t;
	p->hook  = hook;
	spin_unlock_bh(&p->lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(p);
	return ret;

err3:
	kfree(t);
err2:
	kfree(tname);
err1:
	kfree(p);
	return err;
}

static int
tcf_ipt_cleanup(struct tc_action *a, int bind)
{
	struct tcf_ipt *p = PRIV(a, ipt);
	return tcf_ipt_release(p, bind);
}

static int
tcf_ipt(struct sk_buff *skb, struct tc_action *a, struct tcf_result *res)
{
	int ret = 0, result = 0;
	struct tcf_ipt *p = PRIV(a, ipt);

	if (skb_cloned(skb)) {
		if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
			return TC_ACT_UNSPEC;
	}

	spin_lock(&p->lock);

	p->tm.lastuse = jiffies;
	p->bstats.bytes += skb->len;
	p->bstats.packets++;

	/* yes, we have to worry about both in and out dev
	 worry later - danger - this API seems to have changed
	 from earlier kernels */

	/* iptables targets take a double skb pointer in case the skb
	 * needs to be replaced. We don't own the skb, so this must not
	 * happen. The pskb_expand_head above should make sure of this */
	ret = p->t->u.kernel.target->target(&skb, skb->dev, NULL, p->hook,
					    p->t->u.kernel.target, p->t->data,
					    NULL);
	switch (ret) {
	case NF_ACCEPT:
		result = TC_ACT_OK;
		break;
	case NF_DROP:
		result = TC_ACT_SHOT;
		p->qstats.drops++;
		break;
	case IPT_CONTINUE:
		result = TC_ACT_PIPE;
		break;
	default:
		if (net_ratelimit())
			printk("Bogus netfilter code %d assume ACCEPT\n", ret);
		result = TC_POLICE_OK;
		break;
	}
	spin_unlock(&p->lock);
	return result;

}

static int
tcf_ipt_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	struct ipt_entry_target *t;
	struct tcf_t tm;
	struct tc_cnt c;
	unsigned char *b = skb->tail;
	struct tcf_ipt *p = PRIV(a, ipt);

	/* for simple targets kernel size == user size
	** user name = target name
	** for foolproof you need to not assume this
	*/

	t = kmalloc(p->t->u.user.target_size, GFP_ATOMIC);
	if (t == NULL)
		goto rtattr_failure;

	c.bindcnt = p->bindcnt - bind;
	c.refcnt = p->refcnt - ref;
	memcpy(t, p->t, p->t->u.user.target_size);
	strcpy(t->u.user.name, p->t->u.kernel.target->name);

	DPRINTK("\ttcf_ipt_dump tablename %s length %d\n", p->tname,
		strlen(p->tname));
	DPRINTK("\tdump target name %s size %d size user %d "
	        "data[0] %x data[1] %x\n", p->t->u.kernel.target->name,
	        p->t->u.target_size, p->t->u.user.target_size,
	        p->t->data[0], p->t->data[1]);
	RTA_PUT(skb, TCA_IPT_TARG, p->t->u.user.target_size, t);
	RTA_PUT(skb, TCA_IPT_INDEX, 4, &p->index);
	RTA_PUT(skb, TCA_IPT_HOOK, 4, &p->hook);
	RTA_PUT(skb, TCA_IPT_CNT, sizeof(struct tc_cnt), &c);
	RTA_PUT(skb, TCA_IPT_TABLE, IFNAMSIZ, p->tname);
	tm.install = jiffies_to_clock_t(jiffies - p->tm.install);
	tm.lastuse = jiffies_to_clock_t(jiffies - p->tm.lastuse);
	tm.expires = jiffies_to_clock_t(p->tm.expires);
	RTA_PUT(skb, TCA_IPT_TM, sizeof (tm), &tm);
	kfree(t);
	return skb->len;

      rtattr_failure:
	skb_trim(skb, b - skb->data);
	kfree(t);
	return -1;
}

static struct tc_action_ops act_ipt_ops = {
	.kind		=	"ipt",
	.type		=	TCA_ACT_IPT,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_ipt,
	.dump		=	tcf_ipt_dump,
	.cleanup	=	tcf_ipt_cleanup,
	.lookup		=	tcf_hash_search,
	.init		=	tcf_ipt_init,
	.walk		=	tcf_generic_walker
};

MODULE_AUTHOR("Jamal Hadi Salim(2002-4)");
MODULE_DESCRIPTION("Iptables target actions");
MODULE_LICENSE("GPL");

static int __init
ipt_init_module(void)
{
	return tcf_register_action(&act_ipt_ops);
}

static void __exit
ipt_cleanup_module(void)
{
	tcf_unregister_action(&act_ipt_ops);
}

module_init(ipt_init_module);
module_exit(ipt_cleanup_module);
