/*
 * net/sched/simp.c	Simple example of an action
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Jamal Hadi Salim (2005-8)
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
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
static struct tcf_hashinfo simp_hash_info;

#define SIMP_MAX_DATA	32
static int tcf_simp(struct sk_buff *skb, const struct tc_action *a,
		    struct tcf_result *res)
{
	struct tcf_defact *d = a->priv;

	spin_lock(&d->tcf_lock);
	d->tcf_tm.lastuse = jiffies;
	bstats_update(&d->tcf_bstats, skb);

	/* print policy string followed by _ then packet count
	 * Example if this was the 3rd packet and the string was "hello"
	 * then it would look like "hello_3" (without quotes)
	 */
	pr_info("simple: %s_%d\n",
	       (char *)d->tcfd_defdata, d->tcf_bstats.packets);
	spin_unlock(&d->tcf_lock);
	return d->tcf_action;
}

static int tcf_simp_release(struct tc_action *a, int bind)
{
	struct tcf_defact *d = to_defact(a);
	int ret = 0;
	if (d) {
		if (bind)
			d->tcf_bindcnt--;
		d->tcf_refcnt--;
		if (d->tcf_bindcnt <= 0 && d->tcf_refcnt <= 0) {
			kfree(d->tcfd_defdata);
			tcf_hash_destroy(a);
			ret = 1;
		}
	}
	return ret;
}

static int alloc_defdata(struct tcf_defact *d, char *defdata)
{
	d->tcfd_defdata = kzalloc(SIMP_MAX_DATA, GFP_KERNEL);
	if (unlikely(!d->tcfd_defdata))
		return -ENOMEM;
	strlcpy(d->tcfd_defdata, defdata, SIMP_MAX_DATA);
	return 0;
}

static void reset_policy(struct tcf_defact *d, char *defdata,
			 struct tc_defact *p)
{
	spin_lock_bh(&d->tcf_lock);
	d->tcf_action = p->action;
	memset(d->tcfd_defdata, 0, SIMP_MAX_DATA);
	strlcpy(d->tcfd_defdata, defdata, SIMP_MAX_DATA);
	spin_unlock_bh(&d->tcf_lock);
}

static const struct nla_policy simple_policy[TCA_DEF_MAX + 1] = {
	[TCA_DEF_PARMS]	= { .len = sizeof(struct tc_defact) },
	[TCA_DEF_DATA]	= { .type = NLA_STRING, .len = SIMP_MAX_DATA },
};

static int tcf_simp_init(struct net *net, struct nlattr *nla,
			 struct nlattr *est, struct tc_action *a,
			 int ovr, int bind)
{
	struct nlattr *tb[TCA_DEF_MAX + 1];
	struct tc_defact *parm;
	struct tcf_defact *d;
	char *defdata;
	int ret = 0, err;

	if (nla == NULL)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_DEF_MAX, nla, simple_policy);
	if (err < 0)
		return err;

	if (tb[TCA_DEF_PARMS] == NULL)
		return -EINVAL;

	if (tb[TCA_DEF_DATA] == NULL)
		return -EINVAL;

	parm = nla_data(tb[TCA_DEF_PARMS]);
	defdata = nla_data(tb[TCA_DEF_DATA]);

	if (!tcf_hash_check(parm->index, a, bind)) {
		ret = tcf_hash_create(parm->index, est, a, sizeof(*d), bind);
		if (ret)
			return ret;

		d = to_defact(a);
		ret = alloc_defdata(d, defdata);
		if (ret < 0) {
			tcf_hash_cleanup(a, est);
			return ret;
		}
		d->tcf_action = parm->action;
		ret = ACT_P_CREATED;
	} else {
		d = to_defact(a);

		if (bind)
			return 0;
		tcf_simp_release(a, bind);
		if (!ovr)
			return -EEXIST;

		reset_policy(d, defdata, parm);
	}

	if (ret == ACT_P_CREATED)
		tcf_hash_insert(a);
	return ret;
}

static int tcf_simp_dump(struct sk_buff *skb, struct tc_action *a,
			 int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_defact *d = a->priv;
	struct tc_defact opt = {
		.index   = d->tcf_index,
		.refcnt  = d->tcf_refcnt - ref,
		.bindcnt = d->tcf_bindcnt - bind,
		.action  = d->tcf_action,
	};
	struct tcf_t t;

	if (nla_put(skb, TCA_DEF_PARMS, sizeof(opt), &opt) ||
	    nla_put_string(skb, TCA_DEF_DATA, d->tcfd_defdata))
		goto nla_put_failure;
	t.install = jiffies_to_clock_t(jiffies - d->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - d->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(d->tcf_tm.expires);
	if (nla_put(skb, TCA_DEF_TM, sizeof(t), &t))
		goto nla_put_failure;
	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct tc_action_ops act_simp_ops = {
	.kind		=	"simple",
	.hinfo		=	&simp_hash_info,
	.type		=	TCA_ACT_SIMP,
	.owner		=	THIS_MODULE,
	.act		=	tcf_simp,
	.dump		=	tcf_simp_dump,
	.cleanup	=	tcf_simp_release,
	.init		=	tcf_simp_init,
};

MODULE_AUTHOR("Jamal Hadi Salim(2005)");
MODULE_DESCRIPTION("Simple example action");
MODULE_LICENSE("GPL");

static int __init simp_init_module(void)
{
	int err, ret;
	err = tcf_hashinfo_init(&simp_hash_info, SIMP_TAB_MASK);
	if (err)
		return err;

	ret = tcf_register_action(&act_simp_ops);
	if (!ret)
		pr_info("Simple TC action Loaded\n");
	else
		tcf_hashinfo_destroy(&simp_hash_info);

	return ret;
}

static void __exit simp_cleanup_module(void)
{
	tcf_hashinfo_destroy(&simp_hash_info);
	tcf_unregister_action(&act_simp_ops);
}

module_init(simp_init_module);
module_exit(simp_cleanup_module);
