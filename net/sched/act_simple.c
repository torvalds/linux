/*
 * net/sched/simp.c	Simple example of an action
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Jamal Hadi Salim (2005)
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>

#define TCA_ACT_SIMP 22

#include <linux/tc_act/tc_defact.h>
#include <net/tc_act/tc_defact.h>

#define SIMP_TAB_MASK     7
static struct tcf_common *tcf_simp_ht[SIMP_TAB_MASK + 1];
static u32 simp_idx_gen;
static DEFINE_RWLOCK(simp_lock);

static struct tcf_hashinfo simp_hash_info = {
	.htab	=	tcf_simp_ht,
	.hmask	=	SIMP_TAB_MASK,
	.lock	=	&simp_lock,
};

static int tcf_simp(struct sk_buff *skb, struct tc_action *a, struct tcf_result *res)
{
	struct tcf_defact *d = a->priv;

	spin_lock(&d->tcf_lock);
	d->tcf_tm.lastuse = jiffies;
	d->tcf_bstats.bytes += skb->len;
	d->tcf_bstats.packets++;

	/* print policy string followed by _ then packet count
	 * Example if this was the 3rd packet and the string was "hello"
	 * then it would look like "hello_3" (without quotes)
	 **/
	printk("simple: %s_%d\n",
	       (char *)d->tcfd_defdata, d->tcf_bstats.packets);
	spin_unlock(&d->tcf_lock);
	return d->tcf_action;
}

static int tcf_simp_release(struct tcf_defact *d, int bind)
{
	int ret = 0;
	if (d) {
		if (bind)
			d->tcf_bindcnt--;
		d->tcf_refcnt--;
		if (d->tcf_bindcnt <= 0 && d->tcf_refcnt <= 0) {
			kfree(d->tcfd_defdata);
			tcf_hash_destroy(&d->common, &simp_hash_info);
			ret = 1;
		}
	}
	return ret;
}

static int alloc_defdata(struct tcf_defact *d, u32 datalen, void *defdata)
{
	d->tcfd_defdata = kmemdup(defdata, datalen, GFP_KERNEL);
	if (unlikely(!d->tcfd_defdata))
		return -ENOMEM;
	d->tcfd_datalen = datalen;
	return 0;
}

static int realloc_defdata(struct tcf_defact *d, u32 datalen, void *defdata)
{
	kfree(d->tcfd_defdata);
	return alloc_defdata(d, datalen, defdata);
}

static int tcf_simp_init(struct rtattr *rta, struct rtattr *est,
			 struct tc_action *a, int ovr, int bind)
{
	struct rtattr *tb[TCA_DEF_MAX];
	struct tc_defact *parm;
	struct tcf_defact *d;
	struct tcf_common *pc;
	void *defdata;
	u32 datalen = 0;
	int ret = 0;

	if (rta == NULL || rtattr_parse_nested(tb, TCA_DEF_MAX, rta) < 0)
		return -EINVAL;

	if (tb[TCA_DEF_PARMS - 1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_DEF_PARMS - 1]) < sizeof(*parm))
		return -EINVAL;

	parm = RTA_DATA(tb[TCA_DEF_PARMS - 1]);
	defdata = RTA_DATA(tb[TCA_DEF_DATA - 1]);
	if (defdata == NULL)
		return -EINVAL;

	datalen = RTA_PAYLOAD(tb[TCA_DEF_DATA - 1]);
	if (datalen <= 0)
		return -EINVAL;

	pc = tcf_hash_check(parm->index, a, bind, &simp_hash_info);
	if (!pc) {
		pc = tcf_hash_create(parm->index, est, a, sizeof(*d), bind,
				     &simp_idx_gen, &simp_hash_info);
		if (unlikely(!pc))
			return -ENOMEM;

		d = to_defact(pc);
		ret = alloc_defdata(d, datalen, defdata);
		if (ret < 0) {
			kfree(pc);
			return ret;
		}
		ret = ACT_P_CREATED;
	} else {
		d = to_defact(pc);
		if (!ovr) {
			tcf_simp_release(d, bind);
			return -EEXIST;
		}
		realloc_defdata(d, datalen, defdata);
	}

	spin_lock_bh(&d->tcf_lock);
	d->tcf_action = parm->action;
	spin_unlock_bh(&d->tcf_lock);

	if (ret == ACT_P_CREATED)
		tcf_hash_insert(pc, &simp_hash_info);
	return ret;
}

static inline int tcf_simp_cleanup(struct tc_action *a, int bind)
{
	struct tcf_defact *d = a->priv;

	if (d)
		return tcf_simp_release(d, bind);
	return 0;
}

static inline int tcf_simp_dump(struct sk_buff *skb, struct tc_action *a,
				int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_defact *d = a->priv;
	struct tc_defact opt;
	struct tcf_t t;

	opt.index = d->tcf_index;
	opt.refcnt = d->tcf_refcnt - ref;
	opt.bindcnt = d->tcf_bindcnt - bind;
	opt.action = d->tcf_action;
	RTA_PUT(skb, TCA_DEF_PARMS, sizeof(opt), &opt);
	RTA_PUT(skb, TCA_DEF_DATA, d->tcfd_datalen, d->tcfd_defdata);
	t.install = jiffies_to_clock_t(jiffies - d->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - d->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(d->tcf_tm.expires);
	RTA_PUT(skb, TCA_DEF_TM, sizeof(t), &t);
	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct tc_action_ops act_simp_ops = {
	.kind		=	"simple",
	.hinfo		=	&simp_hash_info,
	.type		=	TCA_ACT_SIMP,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_simp,
	.dump		=	tcf_simp_dump,
	.cleanup	=	tcf_simp_cleanup,
	.init		=	tcf_simp_init,
	.walk		=	tcf_generic_walker,
};

MODULE_AUTHOR("Jamal Hadi Salim(2005)");
MODULE_DESCRIPTION("Simple example action");
MODULE_LICENSE("GPL");

static int __init simp_init_module(void)
{
	int ret = tcf_register_action(&act_simp_ops);
	if (!ret)
		printk("Simple TC action Loaded\n");
	return ret;
}

static void __exit simp_cleanup_module(void)
{
	tcf_unregister_action(&act_simp_ops);
}

module_init(simp_init_module);
module_exit(simp_cleanup_module);
