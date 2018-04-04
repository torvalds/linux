/*
 * net/sched/act_meta_tc_index.c IFE skb->tc_index metadata module
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * copyright Jamal Hadi Salim (2016)
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

static int skbtcindex_encode(struct sk_buff *skb, void *skbdata,
			     struct tcf_meta_info *e)
{
	u32 ifetc_index = skb->tc_index;

	return ife_encode_meta_u16(ifetc_index, skbdata, e);
}

static int skbtcindex_decode(struct sk_buff *skb, void *data, u16 len)
{
	u16 ifetc_index = *(u16 *)data;

	skb->tc_index = ntohs(ifetc_index);
	return 0;
}

static int skbtcindex_check(struct sk_buff *skb, struct tcf_meta_info *e)
{
	return ife_check_meta_u16(skb->tc_index, e);
}

static struct tcf_meta_ops ife_skbtcindex_ops = {
	.metaid = IFE_META_TCINDEX,
	.metatype = NLA_U16,
	.name = "tc_index",
	.synopsis = "skb tc_index 16 bit metadata",
	.check_presence = skbtcindex_check,
	.encode = skbtcindex_encode,
	.decode = skbtcindex_decode,
	.get = ife_get_meta_u16,
	.alloc = ife_alloc_meta_u16,
	.release = ife_release_meta_gen,
	.validate = ife_validate_meta_u16,
	.owner = THIS_MODULE,
};

static int __init ifetc_index_init_module(void)
{
	return register_ife_op(&ife_skbtcindex_ops);
}

static void __exit ifetc_index_cleanup_module(void)
{
	unregister_ife_op(&ife_skbtcindex_ops);
}

module_init(ifetc_index_init_module);
module_exit(ifetc_index_cleanup_module);

MODULE_AUTHOR("Jamal Hadi Salim(2016)");
MODULE_DESCRIPTION("Inter-FE skb tc_index metadata module");
MODULE_LICENSE("GPL");
MODULE_ALIAS_IFE_META("tcindex");
