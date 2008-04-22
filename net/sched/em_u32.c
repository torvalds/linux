/*
 * net/sched/em_u32.c	U32 Ematch
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 *		Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Based on net/sched/cls_u32.c
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <net/pkt_cls.h>

static int em_u32_match(struct sk_buff *skb, struct tcf_ematch *em,
			struct tcf_pkt_info *info)
{
	struct tc_u32_key *key = (struct tc_u32_key *) em->data;
	const unsigned char *ptr = skb_network_header(skb);

	if (info) {
		if (info->ptr)
			ptr = info->ptr;
		ptr += (info->nexthdr & key->offmask);
	}

	ptr += key->off;

	if (!tcf_valid_offset(skb, ptr, sizeof(u32)))
		return 0;

	return !(((*(__be32*) ptr)  ^ key->val) & key->mask);
}

static struct tcf_ematch_ops em_u32_ops = {
	.kind	  = TCF_EM_U32,
	.datalen  = sizeof(struct tc_u32_key),
	.match	  = em_u32_match,
	.owner	  = THIS_MODULE,
	.link	  = LIST_HEAD_INIT(em_u32_ops.link)
};

static int __init init_em_u32(void)
{
	return tcf_em_register(&em_u32_ops);
}

static void __exit exit_em_u32(void)
{
	tcf_em_unregister(&em_u32_ops);
}

MODULE_LICENSE("GPL");

module_init(init_em_u32);
module_exit(exit_em_u32);

MODULE_ALIAS_TCF_EMATCH(TCF_EM_U32);
