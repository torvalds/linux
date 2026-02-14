// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * em_canid.c  Ematch rule to match CAN frames according to their CAN IDs
 *
 * Idea:       Oliver Hartkopp <oliver.hartkopp@volkswagen.de>
 * Copyright:  (c) 2011 Czech Technical University in Prague
 *             (c) 2011 Volkswagen Group Research
 * Authors:    Michal Sojka <sojkam1@fel.cvut.cz>
 *             Pavel Pisa <pisa@cmp.felk.cvut.cz>
 *             Rostislav Lisovy <lisovy@gmail.cz>
 * Funded by:  Volkswagen Group Research
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <net/pkt_cls.h>
#include <linux/can.h>

#define EM_CAN_RULES_MAX 500

struct canid_match {
	/* For each SFF CAN ID (11 bit) there is one record in this bitfield */
	DECLARE_BITMAP(match_sff, (1 << CAN_SFF_ID_BITS));

	int rules_count;
	int sff_rules_count;
	int eff_rules_count;

	/*
	 * Raw rules copied from netlink message; Used for sending
	 * information to userspace (when 'tc filter show' is invoked)
	 * AND when matching EFF frames
	 */
	struct can_filter rules_raw[];
};

/**
 * em_canid_get_id() - Extracts Can ID out of the sk_buff structure.
 * @skb: buffer to extract Can ID from
 */
static canid_t em_canid_get_id(struct sk_buff *skb)
{
	/* CAN ID is stored within the data field */
	struct can_frame *cf = (struct can_frame *)skb->data;

	return cf->can_id;
}

static void em_canid_sff_match_add(struct canid_match *cm, u32 can_id,
					u32 can_mask)
{
	int i;

	/*
	 * Limit can_mask and can_id to SFF range to
	 * protect against write after end of array
	 */
	can_mask &= CAN_SFF_MASK;
	can_id &= can_mask;

	/* Single frame */
	if (can_mask == CAN_SFF_MASK) {
		set_bit(can_id, cm->match_sff);
		return;
	}

	/* All frames */
	if (can_mask == 0) {
		bitmap_fill(cm->match_sff, (1 << CAN_SFF_ID_BITS));
		return;
	}

	/*
	 * Individual frame filter.
	 * Add record (set bit to 1) for each ID that
	 * conforms particular rule
	 */
	for (i = 0; i < (1 << CAN_SFF_ID_BITS); i++) {
		if ((i & can_mask) == can_id)
			set_bit(i, cm->match_sff);
	}
}

static inline struct canid_match *em_canid_priv(struct tcf_ematch *m)
{
	return (struct canid_match *)m->data;
}

static int em_canid_match(struct sk_buff *skb, struct tcf_ematch *m,
			 struct tcf_pkt_info *info)
{
	struct canid_match *cm = em_canid_priv(m);
	canid_t can_id;
	int match = 0;
	int i;
	const struct can_filter *lp;

	if (!pskb_may_pull(skb, CAN_MTU))
		return 0;

	can_id = em_canid_get_id(skb);

	if (can_id & CAN_EFF_FLAG) {
		for (i = 0, lp = cm->rules_raw;
		     i < cm->eff_rules_count; i++, lp++) {
			if (!(((lp->can_id ^ can_id) & lp->can_mask))) {
				match = 1;
				break;
			}
		}
	} else { /* SFF */
		can_id &= CAN_SFF_MASK;
		match = (test_bit(can_id, cm->match_sff) ? 1 : 0);
	}

	return match;
}

static int em_canid_change(struct net *net, void *data, int len,
			  struct tcf_ematch *m)
{
	struct can_filter *conf = data; /* Array with rules */
	struct canid_match *cm;
	int i;

	if (!len)
		return -EINVAL;

	if (len % sizeof(struct can_filter))
		return -EINVAL;

	if (len > sizeof(struct can_filter) * EM_CAN_RULES_MAX)
		return -EINVAL;

	cm = kzalloc(sizeof(struct canid_match) + len, GFP_KERNEL);
	if (!cm)
		return -ENOMEM;

	cm->rules_count = len / sizeof(struct can_filter);

	/*
	 * We need two for() loops for copying rules into two contiguous
	 * areas in rules_raw to process all eff rules with a simple loop.
	 * NB: The configuration interface supports sff and eff rules.
	 * We do not support filters here that match for the same can_id
	 * provided in a SFF and EFF frame (e.g. 0x123 / 0x80000123).
	 * For this (unusual case) two filters have to be specified. The
	 * SFF/EFF separation is done with the CAN_EFF_FLAG in the can_id.
	 */

	/* Fill rules_raw with EFF rules first */
	for (i = 0; i < cm->rules_count; i++) {
		if (conf[i].can_id & CAN_EFF_FLAG) {
			memcpy(cm->rules_raw + cm->eff_rules_count,
				&conf[i],
				sizeof(struct can_filter));

			cm->eff_rules_count++;
		}
	}

	/* append SFF frame rules */
	for (i = 0; i < cm->rules_count; i++) {
		if (!(conf[i].can_id & CAN_EFF_FLAG)) {
			memcpy(cm->rules_raw
				+ cm->eff_rules_count
				+ cm->sff_rules_count,
				&conf[i], sizeof(struct can_filter));

			cm->sff_rules_count++;

			em_canid_sff_match_add(cm,
				conf[i].can_id, conf[i].can_mask);
		}
	}

	m->datalen = sizeof(struct canid_match) + len;
	m->data = (unsigned long)cm;
	return 0;
}

static void em_canid_destroy(struct tcf_ematch *m)
{
	struct canid_match *cm = em_canid_priv(m);

	kfree(cm);
}

static int em_canid_dump(struct sk_buff *skb, struct tcf_ematch *m)
{
	struct canid_match *cm = em_canid_priv(m);

	/*
	 * When configuring this ematch 'rules_count' is set not to exceed
	 * 'rules_raw' array size
	 */
	if (nla_put_nohdr(skb, sizeof(struct can_filter) * cm->rules_count,
	    &cm->rules_raw) < 0)
		return -EMSGSIZE;

	return 0;
}

static struct tcf_ematch_ops em_canid_ops = {
	.kind	  = TCF_EM_CANID,
	.change	  = em_canid_change,
	.match	  = em_canid_match,
	.destroy  = em_canid_destroy,
	.dump	  = em_canid_dump,
	.owner	  = THIS_MODULE,
	.link	  = LIST_HEAD_INIT(em_canid_ops.link)
};

static int __init init_em_canid(void)
{
	return tcf_em_register(&em_canid_ops);
}

static void __exit exit_em_canid(void)
{
	tcf_em_unregister(&em_canid_ops);
}

MODULE_DESCRIPTION("ematch classifier to match CAN IDs embedded in skb CAN frames");
MODULE_LICENSE("GPL");

module_init(init_em_canid);
module_exit(exit_em_canid);

MODULE_ALIAS_TCF_EMATCH(TCF_EM_CANID);
