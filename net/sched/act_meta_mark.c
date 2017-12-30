/*
 * net/sched/act_meta_mark.c IFE skb->mark metadata module
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

static int skbmark_encode(struct sk_buff *skb, void *skbdata,
			  struct tcf_meta_info *e)
{
	u32 ifemark = skb->mark;

	return ife_encode_meta_u32(ifemark, skbdata, e);
}

static int skbmark_decode(struct sk_buff *skb, void *data, u16 len)
{
	u32 ifemark = *(u32 *)data;

	skb->mark = ntohl(ifemark);
	return 0;
}

static int skbmark_check(struct sk_buff *skb, struct tcf_meta_info *e)
{
	return ife_check_meta_u32(skb->mark, e);
}

static struct tcf_meta_ops ife_skbmark_ops = {
	.metaid = IFE_META_SKBMARK,
	.metatype = NLA_U32,
	.name = "skbmark",
	.synopsis = "skb mark 32 bit metadata",
	.check_presence = skbmark_check,
	.encode = skbmark_encode,
	.decode = skbmark_decode,
	.get = ife_get_meta_u32,
	.alloc = ife_alloc_meta_u32,
	.release = ife_release_meta_gen,
	.validate = ife_validate_meta_u32,
	.owner = THIS_MODULE,
};

static int __init ifemark_init_module(void)
{
	return register_ife_op(&ife_skbmark_ops);
}

static void __exit ifemark_cleanup_module(void)
{
	unregister_ife_op(&ife_skbmark_ops);
}

module_init(ifemark_init_module);
module_exit(ifemark_cleanup_module);

MODULE_AUTHOR("Jamal Hadi Salim(2015)");
MODULE_DESCRIPTION("Inter-FE skb mark metadata module");
MODULE_LICENSE("GPL");
MODULE_ALIAS_IFE_META("skbmark");
