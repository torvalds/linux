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
#include <linux/kmod.h>
#include <net/netlink.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <linux/tc_act/tc_ipt.h>
#include <net/tc_act/tc_ipt.h>

#include <linux/netfilter_ipv4/ip_tables.h>


#define IPT_TAB_MASK     15
static struct tcf_common *tcf_ipt_ht[IPT_TAB_MASK + 1];
static u32 ipt_idx_gen;
static DEFINE_RWLOCK(ipt_lock);

static struct tcf_hashinfo ipt_hash_info = {
	.htab	=	tcf_ipt_ht,
	.hmask	=	IPT_TAB_MASK,
	.lock	=	&ipt_lock,
};

static int ipt_init_target(struct ipt_entry_target *t, char *table, unsigned int hook)
{
	struct xt_target *target;
	int ret = 0;

	target = xt_request_find_target(AF_INET, t->u.user.name,
					t->u.user.revision);
	if (!target)
		return -ENOENT;

	t->u.kernel.target = target;

	ret = xt_check_target(target, AF_INET, t->u.target_size - sizeof(*t),
			      table, hook, 0, 0);
	if (ret) {
		module_put(t->u.kernel.target->me);
		return ret;
	}
	if (t->u.kernel.target->checkentry
	    && !t->u.kernel.target->checkentry(table, NULL,
					       t->u.kernel.target, t->data,
					       hook)) {
		module_put(t->u.kernel.target->me);
		ret = -EINVAL;
	}

	return ret;
}

static void ipt_destroy_target(struct ipt_entry_target *t)
{
	if (t->u.kernel.target->destroy)
		t->u.kernel.target->destroy(t->u.kernel.target, t->data);
	module_put(t->u.kernel.target->me);
}

static int tcf_ipt_release(struct tcf_ipt *ipt, int bind)
{
	int ret = 0;
	if (ipt) {
		if (bind)
			ipt->tcf_bindcnt--;
		ipt->tcf_refcnt--;
		if (ipt->tcf_bindcnt <= 0 && ipt->tcf_refcnt <= 0) {
			ipt_destroy_target(ipt->tcfi_t);
			kfree(ipt->tcfi_tname);
			kfree(ipt->tcfi_t);
			tcf_hash_destroy(&ipt->common, &ipt_hash_info);
			ret = ACT_P_DELETED;
		}
	}
	return ret;
}

static int tcf_ipt_init(struct rtattr *rta, struct rtattr *est,
			struct tc_action *a, int ovr, int bind)
{
	struct rtattr *tb[TCA_IPT_MAX];
	struct tcf_ipt *ipt;
	struct tcf_common *pc;
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

	pc = tcf_hash_check(index, a, bind, &ipt_hash_info);
	if (!pc) {
		pc = tcf_hash_create(index, est, a, sizeof(*ipt), bind,
				     &ipt_idx_gen, &ipt_hash_info);
		if (unlikely(!pc))
			return -ENOMEM;
		ret = ACT_P_CREATED;
	} else {
		if (!ovr) {
			tcf_ipt_release(to_ipt(pc), bind);
			return -EEXIST;
		}
	}
	ipt = to_ipt(pc);

	hook = *(u32 *)RTA_DATA(tb[TCA_IPT_HOOK-1]);

	err = -ENOMEM;
	tname = kmalloc(IFNAMSIZ, GFP_KERNEL);
	if (unlikely(!tname))
		goto err1;
	if (tb[TCA_IPT_TABLE - 1] == NULL ||
	    rtattr_strlcpy(tname, tb[TCA_IPT_TABLE-1], IFNAMSIZ) >= IFNAMSIZ)
		strcpy(tname, "mangle");

	t = kmemdup(td, td->u.target_size, GFP_KERNEL);
	if (unlikely(!t))
		goto err2;

	if ((err = ipt_init_target(t, tname, hook)) < 0)
		goto err3;

	spin_lock_bh(&ipt->tcf_lock);
	if (ret != ACT_P_CREATED) {
		ipt_destroy_target(ipt->tcfi_t);
		kfree(ipt->tcfi_tname);
		kfree(ipt->tcfi_t);
	}
	ipt->tcfi_tname = tname;
	ipt->tcfi_t     = t;
	ipt->tcfi_hook  = hook;
	spin_unlock_bh(&ipt->tcf_lock);
	if (ret == ACT_P_CREATED)
		tcf_hash_insert(pc, &ipt_hash_info);
	return ret;

err3:
	kfree(t);
err2:
	kfree(tname);
err1:
	kfree(pc);
	return err;
}

static int tcf_ipt_cleanup(struct tc_action *a, int bind)
{
	struct tcf_ipt *ipt = a->priv;
	return tcf_ipt_release(ipt, bind);
}

static int tcf_ipt(struct sk_buff *skb, struct tc_action *a,
		   struct tcf_result *res)
{
	int ret = 0, result = 0;
	struct tcf_ipt *ipt = a->priv;

	if (skb_cloned(skb)) {
		if (pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
			return TC_ACT_UNSPEC;
	}

	spin_lock(&ipt->tcf_lock);

	ipt->tcf_tm.lastuse = jiffies;
	ipt->tcf_bstats.bytes += skb->len;
	ipt->tcf_bstats.packets++;

	/* yes, we have to worry about both in and out dev
	 worry later - danger - this API seems to have changed
	 from earlier kernels */

	/* iptables targets take a double skb pointer in case the skb
	 * needs to be replaced. We don't own the skb, so this must not
	 * happen. The pskb_expand_head above should make sure of this */
	ret = ipt->tcfi_t->u.kernel.target->target(&skb, skb->dev, NULL,
						   ipt->tcfi_hook,
						   ipt->tcfi_t->u.kernel.target,
						   ipt->tcfi_t->data);
	switch (ret) {
	case NF_ACCEPT:
		result = TC_ACT_OK;
		break;
	case NF_DROP:
		result = TC_ACT_SHOT;
		ipt->tcf_qstats.drops++;
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
	spin_unlock(&ipt->tcf_lock);
	return result;

}

static int tcf_ipt_dump(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_ipt *ipt = a->priv;
	struct ipt_entry_target *t;
	struct tcf_t tm;
	struct tc_cnt c;

	/* for simple targets kernel size == user size
	** user name = target name
	** for foolproof you need to not assume this
	*/

	t = kmemdup(ipt->tcfi_t, ipt->tcfi_t->u.user.target_size, GFP_ATOMIC);
	if (unlikely(!t))
		goto rtattr_failure;

	c.bindcnt = ipt->tcf_bindcnt - bind;
	c.refcnt = ipt->tcf_refcnt - ref;
	strcpy(t->u.user.name, ipt->tcfi_t->u.kernel.target->name);

	RTA_PUT(skb, TCA_IPT_TARG, ipt->tcfi_t->u.user.target_size, t);
	RTA_PUT(skb, TCA_IPT_INDEX, 4, &ipt->tcf_index);
	RTA_PUT(skb, TCA_IPT_HOOK, 4, &ipt->tcfi_hook);
	RTA_PUT(skb, TCA_IPT_CNT, sizeof(struct tc_cnt), &c);
	RTA_PUT(skb, TCA_IPT_TABLE, IFNAMSIZ, ipt->tcfi_tname);
	tm.install = jiffies_to_clock_t(jiffies - ipt->tcf_tm.install);
	tm.lastuse = jiffies_to_clock_t(jiffies - ipt->tcf_tm.lastuse);
	tm.expires = jiffies_to_clock_t(ipt->tcf_tm.expires);
	RTA_PUT(skb, TCA_IPT_TM, sizeof (tm), &tm);
	kfree(t);
	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	kfree(t);
	return -1;
}

static struct tc_action_ops act_ipt_ops = {
	.kind		=	"ipt",
	.hinfo		=	&ipt_hash_info,
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

static int __init ipt_init_module(void)
{
	return tcf_register_action(&act_ipt_ops);
}

static void __exit ipt_cleanup_module(void)
{
	tcf_unregister_action(&act_ipt_ops);
}

module_init(ipt_init_module);
module_exit(ipt_cleanup_module);
