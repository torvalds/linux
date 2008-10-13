/*
 * net/sched/em_cmp.c	Simple packet data comparison ematch
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Thomas Graf <tgraf@suug.ch>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/tc_ematch/tc_em_cmp.h>
#include <asm/unaligned.h>
#include <net/pkt_cls.h>

static inline int cmp_needs_transformation(struct tcf_em_cmp *cmp)
{
	return unlikely(cmp->flags & TCF_EM_CMP_TRANS);
}

static int em_cmp_match(struct sk_buff *skb, struct tcf_ematch *em,
			struct tcf_pkt_info *info)
{
	struct tcf_em_cmp *cmp = (struct tcf_em_cmp *) em->data;
	unsigned char *ptr = tcf_get_base_ptr(skb, cmp->layer) + cmp->off;
	u32 val = 0;

	if (!tcf_valid_offset(skb, ptr, cmp->align))
		return 0;

	switch (cmp->align) {
		case TCF_EM_ALIGN_U8:
			val = *ptr;
			break;

		case TCF_EM_ALIGN_U16:
			val = get_unaligned_be16(ptr);

			if (cmp_needs_transformation(cmp))
				val = be16_to_cpu(val);
			break;

		case TCF_EM_ALIGN_U32:
			/* Worth checking boundries? The branching seems
			 * to get worse. Visit again. */
			val = get_unaligned_be32(ptr);

			if (cmp_needs_transformation(cmp))
				val = be32_to_cpu(val);
			break;

		default:
			return 0;
	}

	if (cmp->mask)
		val &= cmp->mask;

	switch (cmp->opnd) {
		case TCF_EM_OPND_EQ:
			return val == cmp->val;
		case TCF_EM_OPND_LT:
			return val < cmp->val;
		case TCF_EM_OPND_GT:
			return val > cmp->val;
	}

	return 0;
}

static struct tcf_ematch_ops em_cmp_ops = {
	.kind	  = TCF_EM_CMP,
	.datalen  = sizeof(struct tcf_em_cmp),
	.match	  = em_cmp_match,
	.owner	  = THIS_MODULE,
	.link	  = LIST_HEAD_INIT(em_cmp_ops.link)
};

static int __init init_em_cmp(void)
{
	return tcf_em_register(&em_cmp_ops);
}

static void __exit exit_em_cmp(void)
{
	tcf_em_unregister(&em_cmp_ops);
}

MODULE_LICENSE("GPL");

module_init(init_em_cmp);
module_exit(exit_em_cmp);

MODULE_ALIAS_TCF_EMATCH(TCF_EM_CMP);
