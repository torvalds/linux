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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <net/pkt_sched.h>

#define TCA_ACT_SIMP 22

/* XXX: Hide all these common elements under some macro 
 * probably
*/
#include <linux/tc_act/tc_defact.h>
#include <net/tc_act/tc_defact.h>

/* use generic hash table with 8 buckets */
#define MY_TAB_SIZE     8
#define MY_TAB_MASK     (MY_TAB_SIZE - 1)
static u32 idx_gen;
static struct tcf_defact *tcf_simp_ht[MY_TAB_SIZE];
static DEFINE_RWLOCK(simp_lock);

/* override the defaults */
#define tcf_st		tcf_defact
#define tc_st		tc_defact
#define tcf_t_lock	simp_lock
#define tcf_ht		tcf_simp_ht

#define CONFIG_NET_ACT_INIT 1
#include <net/pkt_act.h>
#include <net/act_generic.h>

static int tcf_simp(struct sk_buff **pskb, struct tc_action *a, struct tcf_result *res)
{
	struct sk_buff *skb = *pskb;
	struct tcf_defact *p = PRIV(a, defact);

	spin_lock(&p->lock);
	p->tm.lastuse = jiffies;
	p->bstats.bytes += skb->len;
	p->bstats.packets++;

	/* print policy string followed by _ then packet count 
	 * Example if this was the 3rd packet and the string was "hello" 
	 * then it would look like "hello_3" (without quotes) 
	 **/
	printk("simple: %s_%d\n", (char *)p->defdata, p->bstats.packets);
	spin_unlock(&p->lock);
	return p->action;
}

static struct tc_action_ops act_simp_ops = {
	.kind = "simple",
	.type = TCA_ACT_SIMP,
	.capab = TCA_CAP_NONE,
	.owner = THIS_MODULE,
	.act = tcf_simp,
	tca_use_default_ops
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
