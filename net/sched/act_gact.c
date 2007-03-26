/*
 * net/sched/gact.c	Generic actions
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * copyright 	Jamal Hadi Salim (2002-4)
 *
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
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
#include <net/netlink.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_gact.h>
#include <net/tc_act/tc_gact.h>

#define GACT_TAB_MASK	15
static struct tcf_common *tcf_gact_ht[GACT_TAB_MASK + 1];
static u32 gact_idx_gen;
static DEFINE_RWLOCK(gact_lock);

static struct tcf_hashinfo gact_hash_info = {
	.htab	=	tcf_gact_ht,
	.hmask	=	GACT_TAB_MASK,
	.lock	=	&gact_lock,
};

#ifdef CONFIG_GACT_PROB
static int gact_net_rand(struct tcf_gact *gact)
{
	if (!gact->tcfg_pval || net_random() % gact->tcfg_pval)
		return gact->tcf_action;
	return gact->tcfg_paction;
}

static int gact_determ(struct tcf_gact *gact)
{
	if (!gact->tcfg_pval || gact->tcf_bstats.packets % gact->tcfg_pval)
		return gact->tcf_action;
	return gact->tcfg_paction;
}

typedef int (*g_rand)(struct tcf_gact *gact);
static g_rand gact_rand[MAX_RAND]= { NULL, gact_net_rand, gact_determ };
#endif /* CONFIG_GACT_PROB */

static int tcf_gact_init(struct rtattr *rta, struct rtattr *est,
			 struct tc_action *a, int ovr, int bind)
{
	struct rtattr *tb[TCA_GACT_MAX];
	struct tc_gact *parm;
	struct tcf_gact *gact;
	struct tcf_common *pc;
	int ret = 0;

	if (rta == NULL || rtattr_parse_nested(tb, TCA_GACT_MAX, rta) < 0)
		return -EINVAL;

	if (tb[TCA_GACT_PARMS - 1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_GACT_PARMS - 1]) < sizeof(*parm))
		return -EINVAL;
	parm = RTA_DATA(tb[TCA_GACT_PARMS - 1]);

	if (tb[TCA_GACT_PROB-1] != NULL)
#ifdef CONFIG_GACT_PROB
		if (RTA_PAYLOAD(tb[TCA_GACT_PROB-1]) < sizeof(struct tc_gact_p))
			return -EINVAL;
#else
		return -EOPNOTSUPP;
#endif

	pc = tcf_hash_check(parm->index, a, bind, &gact_hash_info);
	if (!pc) {
		pc = tcf_hash_create(parm->index, est, a, sizeof(*gact),
				     bind, &gact_idx_gen, &gact_hash_info);
		if (unlikely(!pc))
			return -ENOMEM;
		ret = ACT_P_CREATED;
	} else {
		if (!ovr) {
			tcf_hash_release(pc, bind, &gact_hash_info);
			return -EEXIST;
		}
	}

	gact = to_gact(pc);

	spin_lock_bh(&gact->tcf_lock);
	gact->tcf_action = parm->action;
#ifdef CONFIG_GACT_PROB
	if (tb[TCA_GACT_PROB-1] != NULL) {
		struct tc_gact_p *p_parm = RTA_DATA(tb[TCA_GACT_PROB-1]);
		gact->tcfg_paction = p_parm->paction;
		gact->tcfg_pval    = p_parm->pval;
		gact->tcfg_ptype   = p_parm->ptype;
	}
#endif
	spin_unlock_bh(&gact->tcf_lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(pc, &gact_hash_info);
	return ret;
}

static int tcf_gact_cleanup(struct tc_action *a, int bind)
{
	struct tcf_gact *gact = a->priv;

	if (gact)
		return tcf_hash_release(&gact->common, bind, &gact_hash_info);
	return 0;
}

static int tcf_gact(struct sk_buff *skb, struct tc_action *a, struct tcf_result *res)
{
	struct tcf_gact *gact = a->priv;
	int action = TC_ACT_SHOT;

	spin_lock(&gact->tcf_lock);
#ifdef CONFIG_GACT_PROB
	if (gact->tcfg_ptype && gact_rand[gact->tcfg_ptype] != NULL)
		action = gact_rand[gact->tcfg_ptype](gact);
	else
		action = gact->tcf_action;
#else
	action = gact->tcf_action;
#endif
	gact->tcf_bstats.bytes += skb->len;
	gact->tcf_bstats.packets++;
	if (action == TC_ACT_SHOT)
		gact->tcf_qstats.drops++;
	gact->tcf_tm.lastuse = jiffies;
	spin_unlock(&gact->tcf_lock);

	return action;
}

static int tcf_gact_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tc_gact opt;
	struct tcf_gact *gact = a->priv;
	struct tcf_t t;

	opt.index = gact->tcf_index;
	opt.refcnt = gact->tcf_refcnt - ref;
	opt.bindcnt = gact->tcf_bindcnt - bind;
	opt.action = gact->tcf_action;
	RTA_PUT(skb, TCA_GACT_PARMS, sizeof(opt), &opt);
#ifdef CONFIG_GACT_PROB
	if (gact->tcfg_ptype) {
		struct tc_gact_p p_opt;
		p_opt.paction = gact->tcfg_paction;
		p_opt.pval = gact->tcfg_pval;
		p_opt.ptype = gact->tcfg_ptype;
		RTA_PUT(skb, TCA_GACT_PROB, sizeof(p_opt), &p_opt);
	}
#endif
	t.install = jiffies_to_clock_t(jiffies - gact->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - gact->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(gact->tcf_tm.expires);
	RTA_PUT(skb, TCA_GACT_TM, sizeof(t), &t);
	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct tc_action_ops act_gact_ops = {
	.kind		=	"gact",
	.hinfo		=	&gact_hash_info,
	.type		=	TCA_ACT_GACT,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_gact,
	.dump		=	tcf_gact_dump,
	.cleanup	=	tcf_gact_cleanup,
	.lookup		=	tcf_hash_search,
	.init		=	tcf_gact_init,
	.walk		=	tcf_generic_walker
};

MODULE_AUTHOR("Jamal Hadi Salim(2002-4)");
MODULE_DESCRIPTION("Generic Classifier actions");
MODULE_LICENSE("GPL");

static int __init gact_init_module(void)
{
#ifdef CONFIG_GACT_PROB
	printk("GACT probability on\n");
#else
	printk("GACT probability NOT on\n");
#endif
	return tcf_register_action(&act_gact_ops);
}

static void __exit gact_cleanup_module(void)
{
	tcf_unregister_action(&act_gact_ops);
}

module_init(gact_init_module);
module_exit(gact_cleanup_module);
