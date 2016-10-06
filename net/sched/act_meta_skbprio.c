/*
 * net/sched/act_meta_prio.c IFE skb->priority metadata module
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * copyright Jamal Hadi Salim (2015)
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
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <uapi/linux/tc_act/tc_ife.h>
#include <net/tc_act/tc_ife.h>

static int skbprio_check(struct sk_buff *skb, struct tcf_meta_info *e)
{
	return ife_check_meta_u32(skb->priority, e);
}

static int skbprio_encode(struct sk_buff *skb, void *skbdata,
			  struct tcf_meta_info *e)
{
	u32 ifeprio = skb->priority; /* avoid having to cast skb->priority*/

	return ife_encode_meta_u32(ifeprio, skbdata, e);
}

static int skbprio_decode(struct sk_buff *skb, void *data, u16 len)
{
	u32 ifeprio = *(u32 *)data;

	skb->priority = ntohl(ifeprio);
	return 0;
}

static struct tcf_meta_ops ife_prio_ops = {
	.metaid = IFE_META_PRIO,
	.metatype = NLA_U32,
	.name = "skbprio",
	.synopsis = "skb prio metadata",
	.check_presence = skbprio_check,
	.encode = skbprio_encode,
	.decode = skbprio_decode,
	.get = ife_get_meta_u32,
	.alloc = ife_alloc_meta_u32,
	.owner = THIS_MODULE,
};

static int __init ifeprio_init_module(void)
{
	return register_ife_op(&ife_prio_ops);
}

static void __exit ifeprio_cleanup_module(void)
{
	unregister_ife_op(&ife_prio_ops);
}

module_init(ifeprio_init_module);
module_exit(ifeprio_cleanup_module);

MODULE_AUTHOR("Jamal Hadi Salim(2015)");
MODULE_DESCRIPTION("Inter-FE skb prio metadata action");
MODULE_LICENSE("GPL");
MODULE_ALIAS_IFE_META(IFE_META_PRIO);
