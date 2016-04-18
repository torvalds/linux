/*
 * net/sched/cls_flower.c		Flower classifier
 *
 * Copyright (c) 2015 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/rhashtable.h>

#include <linux/if_ether.h>
#include <linux/in6.h>
#include <linux/ip.h>

#include <net/sch_generic.h>
#include <net/pkt_cls.h>
#include <net/ip.h>
#include <net/flow_dissector.h>

struct fl_flow_key {
	int	indev_ifindex;
	struct flow_dissector_key_control control;
	struct flow_dissector_key_basic basic;
	struct flow_dissector_key_eth_addrs eth;
	struct flow_dissector_key_addrs ipaddrs;
	union {
		struct flow_dissector_key_ipv4_addrs ipv4;
		struct flow_dissector_key_ipv6_addrs ipv6;
	};
	struct flow_dissector_key_ports tp;
} __aligned(BITS_PER_LONG / 8); /* Ensure that we can do comparisons as longs. */

struct fl_flow_mask_range {
	unsigned short int start;
	unsigned short int end;
};

struct fl_flow_mask {
	struct fl_flow_key key;
	struct fl_flow_mask_range range;
	struct rcu_head	rcu;
};

struct cls_fl_head {
	struct rhashtable ht;
	struct fl_flow_mask mask;
	struct flow_dissector dissector;
	u32 hgen;
	bool mask_assigned;
	struct list_head filters;
	struct rhashtable_params ht_params;
	struct rcu_head rcu;
};

struct cls_fl_filter {
	struct rhash_head ht_node;
	struct fl_flow_key mkey;
	struct tcf_exts exts;
	struct tcf_result res;
	struct fl_flow_key key;
	struct list_head list;
	u32 handle;
	struct rcu_head	rcu;
};

static unsigned short int fl_mask_range(const struct fl_flow_mask *mask)
{
	return mask->range.end - mask->range.start;
}

static void fl_mask_update_range(struct fl_flow_mask *mask)
{
	const u8 *bytes = (const u8 *) &mask->key;
	size_t size = sizeof(mask->key);
	size_t i, first = 0, last = size - 1;

	for (i = 0; i < sizeof(mask->key); i++) {
		if (bytes[i]) {
			if (!first && i)
				first = i;
			last = i;
		}
	}
	mask->range.start = rounddown(first, sizeof(long));
	mask->range.end = roundup(last + 1, sizeof(long));
}

static void *fl_key_get_start(struct fl_flow_key *key,
			      const struct fl_flow_mask *mask)
{
	return (u8 *) key + mask->range.start;
}

static void fl_set_masked_key(struct fl_flow_key *mkey, struct fl_flow_key *key,
			      struct fl_flow_mask *mask)
{
	const long *lkey = fl_key_get_start(key, mask);
	const long *lmask = fl_key_get_start(&mask->key, mask);
	long *lmkey = fl_key_get_start(mkey, mask);
	int i;

	for (i = 0; i < fl_mask_range(mask); i += sizeof(long))
		*lmkey++ = *lkey++ & *lmask++;
}

static void fl_clear_masked_range(struct fl_flow_key *key,
				  struct fl_flow_mask *mask)
{
	memset(fl_key_get_start(key, mask), 0, fl_mask_range(mask));
}

static int fl_classify(struct sk_buff *skb, const struct tcf_proto *tp,
		       struct tcf_result *res)
{
	struct cls_fl_head *head = rcu_dereference_bh(tp->root);
	struct cls_fl_filter *f;
	struct fl_flow_key skb_key;
	struct fl_flow_key skb_mkey;

	fl_clear_masked_range(&skb_key, &head->mask);
	skb_key.indev_ifindex = skb->skb_iif;
	/* skb_flow_dissect() does not set n_proto in case an unknown protocol,
	 * so do it rather here.
	 */
	skb_key.basic.n_proto = skb->protocol;
	skb_flow_dissect(skb, &head->dissector, &skb_key, 0);

	fl_set_masked_key(&skb_mkey, &skb_key, &head->mask);

	f = rhashtable_lookup_fast(&head->ht,
				   fl_key_get_start(&skb_mkey, &head->mask),
				   head->ht_params);
	if (f) {
		*res = f->res;
		return tcf_exts_exec(skb, &f->exts, res);
	}
	return -1;
}

static int fl_init(struct tcf_proto *tp)
{
	struct cls_fl_head *head;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (!head)
		return -ENOBUFS;

	INIT_LIST_HEAD_RCU(&head->filters);
	rcu_assign_pointer(tp->root, head);

	return 0;
}

static void fl_destroy_filter(struct rcu_head *head)
{
	struct cls_fl_filter *f = container_of(head, struct cls_fl_filter, rcu);

	tcf_exts_destroy(&f->exts);
	kfree(f);
}

static void fl_hw_destroy_filter(struct tcf_proto *tp, unsigned long cookie)
{
	struct net_device *dev = tp->q->dev_queue->dev;
	struct tc_cls_flower_offload offload = {0};
	struct tc_to_netdev tc;

	if (!tc_should_offload(dev, 0))
		return;

	offload.command = TC_CLSFLOWER_DESTROY;
	offload.cookie = cookie;

	tc.type = TC_SETUP_CLSFLOWER;
	tc.cls_flower = &offload;

	dev->netdev_ops->ndo_setup_tc(dev, tp->q->handle, tp->protocol, &tc);
}

static void fl_hw_replace_filter(struct tcf_proto *tp,
				 struct flow_dissector *dissector,
				 struct fl_flow_key *mask,
				 struct fl_flow_key *key,
				 struct tcf_exts *actions,
				 unsigned long cookie, u32 flags)
{
	struct net_device *dev = tp->q->dev_queue->dev;
	struct tc_cls_flower_offload offload = {0};
	struct tc_to_netdev tc;

	if (!tc_should_offload(dev, flags))
		return;

	offload.command = TC_CLSFLOWER_REPLACE;
	offload.cookie = cookie;
	offload.dissector = dissector;
	offload.mask = mask;
	offload.key = key;
	offload.exts = actions;

	tc.type = TC_SETUP_CLSFLOWER;
	tc.cls_flower = &offload;

	dev->netdev_ops->ndo_setup_tc(dev, tp->q->handle, tp->protocol, &tc);
}

static bool fl_destroy(struct tcf_proto *tp, bool force)
{
	struct cls_fl_head *head = rtnl_dereference(tp->root);
	struct cls_fl_filter *f, *next;

	if (!force && !list_empty(&head->filters))
		return false;

	list_for_each_entry_safe(f, next, &head->filters, list) {
		fl_hw_destroy_filter(tp, (unsigned long)f);
		list_del_rcu(&f->list);
		call_rcu(&f->rcu, fl_destroy_filter);
	}
	RCU_INIT_POINTER(tp->root, NULL);
	if (head->mask_assigned)
		rhashtable_destroy(&head->ht);
	kfree_rcu(head, rcu);
	return true;
}

static unsigned long fl_get(struct tcf_proto *tp, u32 handle)
{
	struct cls_fl_head *head = rtnl_dereference(tp->root);
	struct cls_fl_filter *f;

	list_for_each_entry(f, &head->filters, list)
		if (f->handle == handle)
			return (unsigned long) f;
	return 0;
}

static const struct nla_policy fl_policy[TCA_FLOWER_MAX + 1] = {
	[TCA_FLOWER_UNSPEC]		= { .type = NLA_UNSPEC },
	[TCA_FLOWER_CLASSID]		= { .type = NLA_U32 },
	[TCA_FLOWER_INDEV]		= { .type = NLA_STRING,
					    .len = IFNAMSIZ },
	[TCA_FLOWER_KEY_ETH_DST]	= { .len = ETH_ALEN },
	[TCA_FLOWER_KEY_ETH_DST_MASK]	= { .len = ETH_ALEN },
	[TCA_FLOWER_KEY_ETH_SRC]	= { .len = ETH_ALEN },
	[TCA_FLOWER_KEY_ETH_SRC_MASK]	= { .len = ETH_ALEN },
	[TCA_FLOWER_KEY_ETH_TYPE]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_IP_PROTO]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_IPV4_SRC]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_IPV4_SRC_MASK]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_IPV4_DST]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_IPV4_DST_MASK]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_IPV6_SRC]	= { .len = sizeof(struct in6_addr) },
	[TCA_FLOWER_KEY_IPV6_SRC_MASK]	= { .len = sizeof(struct in6_addr) },
	[TCA_FLOWER_KEY_IPV6_DST]	= { .len = sizeof(struct in6_addr) },
	[TCA_FLOWER_KEY_IPV6_DST_MASK]	= { .len = sizeof(struct in6_addr) },
	[TCA_FLOWER_KEY_TCP_SRC]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_TCP_DST]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_UDP_SRC]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_UDP_DST]	= { .type = NLA_U16 },
};

static void fl_set_key_val(struct nlattr **tb,
			   void *val, int val_type,
			   void *mask, int mask_type, int len)
{
	if (!tb[val_type])
		return;
	memcpy(val, nla_data(tb[val_type]), len);
	if (mask_type == TCA_FLOWER_UNSPEC || !tb[mask_type])
		memset(mask, 0xff, len);
	else
		memcpy(mask, nla_data(tb[mask_type]), len);
}

static int fl_set_key(struct net *net, struct nlattr **tb,
		      struct fl_flow_key *key, struct fl_flow_key *mask)
{
#ifdef CONFIG_NET_CLS_IND
	if (tb[TCA_FLOWER_INDEV]) {
		int err = tcf_change_indev(net, tb[TCA_FLOWER_INDEV]);
		if (err < 0)
			return err;
		key->indev_ifindex = err;
		mask->indev_ifindex = 0xffffffff;
	}
#endif

	fl_set_key_val(tb, key->eth.dst, TCA_FLOWER_KEY_ETH_DST,
		       mask->eth.dst, TCA_FLOWER_KEY_ETH_DST_MASK,
		       sizeof(key->eth.dst));
	fl_set_key_val(tb, key->eth.src, TCA_FLOWER_KEY_ETH_SRC,
		       mask->eth.src, TCA_FLOWER_KEY_ETH_SRC_MASK,
		       sizeof(key->eth.src));

	fl_set_key_val(tb, &key->basic.n_proto, TCA_FLOWER_KEY_ETH_TYPE,
		       &mask->basic.n_proto, TCA_FLOWER_UNSPEC,
		       sizeof(key->basic.n_proto));

	if (key->basic.n_proto == htons(ETH_P_IP) ||
	    key->basic.n_proto == htons(ETH_P_IPV6)) {
		fl_set_key_val(tb, &key->basic.ip_proto, TCA_FLOWER_KEY_IP_PROTO,
			       &mask->basic.ip_proto, TCA_FLOWER_UNSPEC,
			       sizeof(key->basic.ip_proto));
	}

	if (tb[TCA_FLOWER_KEY_IPV4_SRC] || tb[TCA_FLOWER_KEY_IPV4_DST]) {
		key->control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
		fl_set_key_val(tb, &key->ipv4.src, TCA_FLOWER_KEY_IPV4_SRC,
			       &mask->ipv4.src, TCA_FLOWER_KEY_IPV4_SRC_MASK,
			       sizeof(key->ipv4.src));
		fl_set_key_val(tb, &key->ipv4.dst, TCA_FLOWER_KEY_IPV4_DST,
			       &mask->ipv4.dst, TCA_FLOWER_KEY_IPV4_DST_MASK,
			       sizeof(key->ipv4.dst));
	} else if (tb[TCA_FLOWER_KEY_IPV6_SRC] || tb[TCA_FLOWER_KEY_IPV6_DST]) {
		key->control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		fl_set_key_val(tb, &key->ipv6.src, TCA_FLOWER_KEY_IPV6_SRC,
			       &mask->ipv6.src, TCA_FLOWER_KEY_IPV6_SRC_MASK,
			       sizeof(key->ipv6.src));
		fl_set_key_val(tb, &key->ipv6.dst, TCA_FLOWER_KEY_IPV6_DST,
			       &mask->ipv6.dst, TCA_FLOWER_KEY_IPV6_DST_MASK,
			       sizeof(key->ipv6.dst));
	}

	if (key->basic.ip_proto == IPPROTO_TCP) {
		fl_set_key_val(tb, &key->tp.src, TCA_FLOWER_KEY_TCP_SRC,
			       &mask->tp.src, TCA_FLOWER_UNSPEC,
			       sizeof(key->tp.src));
		fl_set_key_val(tb, &key->tp.dst, TCA_FLOWER_KEY_TCP_DST,
			       &mask->tp.dst, TCA_FLOWER_UNSPEC,
			       sizeof(key->tp.dst));
	} else if (key->basic.ip_proto == IPPROTO_UDP) {
		fl_set_key_val(tb, &key->tp.src, TCA_FLOWER_KEY_UDP_SRC,
			       &mask->tp.src, TCA_FLOWER_UNSPEC,
			       sizeof(key->tp.src));
		fl_set_key_val(tb, &key->tp.dst, TCA_FLOWER_KEY_UDP_DST,
			       &mask->tp.dst, TCA_FLOWER_UNSPEC,
			       sizeof(key->tp.dst));
	}

	return 0;
}

static bool fl_mask_eq(struct fl_flow_mask *mask1,
		       struct fl_flow_mask *mask2)
{
	const long *lmask1 = fl_key_get_start(&mask1->key, mask1);
	const long *lmask2 = fl_key_get_start(&mask2->key, mask2);

	return !memcmp(&mask1->range, &mask2->range, sizeof(mask1->range)) &&
	       !memcmp(lmask1, lmask2, fl_mask_range(mask1));
}

static const struct rhashtable_params fl_ht_params = {
	.key_offset = offsetof(struct cls_fl_filter, mkey), /* base offset */
	.head_offset = offsetof(struct cls_fl_filter, ht_node),
	.automatic_shrinking = true,
};

static int fl_init_hashtable(struct cls_fl_head *head,
			     struct fl_flow_mask *mask)
{
	head->ht_params = fl_ht_params;
	head->ht_params.key_len = fl_mask_range(mask);
	head->ht_params.key_offset += mask->range.start;

	return rhashtable_init(&head->ht, &head->ht_params);
}

#define FL_KEY_MEMBER_OFFSET(member) offsetof(struct fl_flow_key, member)
#define FL_KEY_MEMBER_SIZE(member) (sizeof(((struct fl_flow_key *) 0)->member))
#define FL_KEY_MEMBER_END_OFFSET(member)					\
	(FL_KEY_MEMBER_OFFSET(member) + FL_KEY_MEMBER_SIZE(member))

#define FL_KEY_IN_RANGE(mask, member)						\
        (FL_KEY_MEMBER_OFFSET(member) <= (mask)->range.end &&			\
         FL_KEY_MEMBER_END_OFFSET(member) >= (mask)->range.start)

#define FL_KEY_SET(keys, cnt, id, member)					\
	do {									\
		keys[cnt].key_id = id;						\
		keys[cnt].offset = FL_KEY_MEMBER_OFFSET(member);		\
		cnt++;								\
	} while(0);

#define FL_KEY_SET_IF_IN_RANGE(mask, keys, cnt, id, member)			\
	do {									\
		if (FL_KEY_IN_RANGE(mask, member))				\
			FL_KEY_SET(keys, cnt, id, member);			\
	} while(0);

static void fl_init_dissector(struct cls_fl_head *head,
			      struct fl_flow_mask *mask)
{
	struct flow_dissector_key keys[FLOW_DISSECTOR_KEY_MAX];
	size_t cnt = 0;

	FL_KEY_SET(keys, cnt, FLOW_DISSECTOR_KEY_CONTROL, control);
	FL_KEY_SET(keys, cnt, FLOW_DISSECTOR_KEY_BASIC, basic);
	FL_KEY_SET_IF_IN_RANGE(mask, keys, cnt,
			       FLOW_DISSECTOR_KEY_ETH_ADDRS, eth);
	FL_KEY_SET_IF_IN_RANGE(mask, keys, cnt,
			       FLOW_DISSECTOR_KEY_IPV4_ADDRS, ipv4);
	FL_KEY_SET_IF_IN_RANGE(mask, keys, cnt,
			       FLOW_DISSECTOR_KEY_IPV6_ADDRS, ipv6);
	FL_KEY_SET_IF_IN_RANGE(mask, keys, cnt,
			       FLOW_DISSECTOR_KEY_PORTS, tp);

	skb_flow_dissector_init(&head->dissector, keys, cnt);
}

static int fl_check_assign_mask(struct cls_fl_head *head,
				struct fl_flow_mask *mask)
{
	int err;

	if (head->mask_assigned) {
		if (!fl_mask_eq(&head->mask, mask))
			return -EINVAL;
		else
			return 0;
	}

	/* Mask is not assigned yet. So assign it and init hashtable
	 * according to that.
	 */
	err = fl_init_hashtable(head, mask);
	if (err)
		return err;
	memcpy(&head->mask, mask, sizeof(head->mask));
	head->mask_assigned = true;

	fl_init_dissector(head, mask);

	return 0;
}

static int fl_set_parms(struct net *net, struct tcf_proto *tp,
			struct cls_fl_filter *f, struct fl_flow_mask *mask,
			unsigned long base, struct nlattr **tb,
			struct nlattr *est, bool ovr)
{
	struct tcf_exts e;
	int err;

	tcf_exts_init(&e, TCA_FLOWER_ACT, 0);
	err = tcf_exts_validate(net, tp, tb, est, &e, ovr);
	if (err < 0)
		return err;

	if (tb[TCA_FLOWER_CLASSID]) {
		f->res.classid = nla_get_u32(tb[TCA_FLOWER_CLASSID]);
		tcf_bind_filter(tp, &f->res, base);
	}

	err = fl_set_key(net, tb, &f->key, &mask->key);
	if (err)
		goto errout;

	fl_mask_update_range(mask);
	fl_set_masked_key(&f->mkey, &f->key, mask);

	tcf_exts_change(tp, &f->exts, &e);

	return 0;
errout:
	tcf_exts_destroy(&e);
	return err;
}

static u32 fl_grab_new_handle(struct tcf_proto *tp,
			      struct cls_fl_head *head)
{
	unsigned int i = 0x80000000;
	u32 handle;

	do {
		if (++head->hgen == 0x7FFFFFFF)
			head->hgen = 1;
	} while (--i > 0 && fl_get(tp, head->hgen));

	if (unlikely(i == 0)) {
		pr_err("Insufficient number of handles\n");
		handle = 0;
	} else {
		handle = head->hgen;
	}

	return handle;
}

static int fl_change(struct net *net, struct sk_buff *in_skb,
		     struct tcf_proto *tp, unsigned long base,
		     u32 handle, struct nlattr **tca,
		     unsigned long *arg, bool ovr)
{
	struct cls_fl_head *head = rtnl_dereference(tp->root);
	struct cls_fl_filter *fold = (struct cls_fl_filter *) *arg;
	struct cls_fl_filter *fnew;
	struct nlattr *tb[TCA_FLOWER_MAX + 1];
	struct fl_flow_mask mask = {};
	u32 flags = 0;
	int err;

	if (!tca[TCA_OPTIONS])
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_FLOWER_MAX, tca[TCA_OPTIONS], fl_policy);
	if (err < 0)
		return err;

	if (fold && handle && fold->handle != handle)
		return -EINVAL;

	fnew = kzalloc(sizeof(*fnew), GFP_KERNEL);
	if (!fnew)
		return -ENOBUFS;

	tcf_exts_init(&fnew->exts, TCA_FLOWER_ACT, 0);

	if (!handle) {
		handle = fl_grab_new_handle(tp, head);
		if (!handle) {
			err = -EINVAL;
			goto errout;
		}
	}
	fnew->handle = handle;

	if (tb[TCA_FLOWER_FLAGS])
		flags = nla_get_u32(tb[TCA_FLOWER_FLAGS]);

	err = fl_set_parms(net, tp, fnew, &mask, base, tb, tca[TCA_RATE], ovr);
	if (err)
		goto errout;

	err = fl_check_assign_mask(head, &mask);
	if (err)
		goto errout;

	err = rhashtable_insert_fast(&head->ht, &fnew->ht_node,
				     head->ht_params);
	if (err)
		goto errout;

	fl_hw_replace_filter(tp,
			     &head->dissector,
			     &mask.key,
			     &fnew->key,
			     &fnew->exts,
			     (unsigned long)fnew,
			     flags);

	if (fold) {
		rhashtable_remove_fast(&head->ht, &fold->ht_node,
				       head->ht_params);
		fl_hw_destroy_filter(tp, (unsigned long)fold);
	}

	*arg = (unsigned long) fnew;

	if (fold) {
		list_replace_rcu(&fold->list, &fnew->list);
		tcf_unbind_filter(tp, &fold->res);
		call_rcu(&fold->rcu, fl_destroy_filter);
	} else {
		list_add_tail_rcu(&fnew->list, &head->filters);
	}

	return 0;

errout:
	kfree(fnew);
	return err;
}

static int fl_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct cls_fl_head *head = rtnl_dereference(tp->root);
	struct cls_fl_filter *f = (struct cls_fl_filter *) arg;

	rhashtable_remove_fast(&head->ht, &f->ht_node,
			       head->ht_params);
	list_del_rcu(&f->list);
	fl_hw_destroy_filter(tp, (unsigned long)f);
	tcf_unbind_filter(tp, &f->res);
	call_rcu(&f->rcu, fl_destroy_filter);
	return 0;
}

static void fl_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct cls_fl_head *head = rtnl_dereference(tp->root);
	struct cls_fl_filter *f;

	list_for_each_entry_rcu(f, &head->filters, list) {
		if (arg->count < arg->skip)
			goto skip;
		if (arg->fn(tp, (unsigned long) f, arg) < 0) {
			arg->stop = 1;
			break;
		}
skip:
		arg->count++;
	}
}

static int fl_dump_key_val(struct sk_buff *skb,
			   void *val, int val_type,
			   void *mask, int mask_type, int len)
{
	int err;

	if (!memchr_inv(mask, 0, len))
		return 0;
	err = nla_put(skb, val_type, len, val);
	if (err)
		return err;
	if (mask_type != TCA_FLOWER_UNSPEC) {
		err = nla_put(skb, mask_type, len, mask);
		if (err)
			return err;
	}
	return 0;
}

static int fl_dump(struct net *net, struct tcf_proto *tp, unsigned long fh,
		   struct sk_buff *skb, struct tcmsg *t)
{
	struct cls_fl_head *head = rtnl_dereference(tp->root);
	struct cls_fl_filter *f = (struct cls_fl_filter *) fh;
	struct nlattr *nest;
	struct fl_flow_key *key, *mask;

	if (!f)
		return skb->len;

	t->tcm_handle = f->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (!nest)
		goto nla_put_failure;

	if (f->res.classid &&
	    nla_put_u32(skb, TCA_FLOWER_CLASSID, f->res.classid))
		goto nla_put_failure;

	key = &f->key;
	mask = &head->mask.key;

	if (mask->indev_ifindex) {
		struct net_device *dev;

		dev = __dev_get_by_index(net, key->indev_ifindex);
		if (dev && nla_put_string(skb, TCA_FLOWER_INDEV, dev->name))
			goto nla_put_failure;
	}

	if (fl_dump_key_val(skb, key->eth.dst, TCA_FLOWER_KEY_ETH_DST,
			    mask->eth.dst, TCA_FLOWER_KEY_ETH_DST_MASK,
			    sizeof(key->eth.dst)) ||
	    fl_dump_key_val(skb, key->eth.src, TCA_FLOWER_KEY_ETH_SRC,
			    mask->eth.src, TCA_FLOWER_KEY_ETH_SRC_MASK,
			    sizeof(key->eth.src)) ||
	    fl_dump_key_val(skb, &key->basic.n_proto, TCA_FLOWER_KEY_ETH_TYPE,
			    &mask->basic.n_proto, TCA_FLOWER_UNSPEC,
			    sizeof(key->basic.n_proto)))
		goto nla_put_failure;
	if ((key->basic.n_proto == htons(ETH_P_IP) ||
	     key->basic.n_proto == htons(ETH_P_IPV6)) &&
	    fl_dump_key_val(skb, &key->basic.ip_proto, TCA_FLOWER_KEY_IP_PROTO,
			    &mask->basic.ip_proto, TCA_FLOWER_UNSPEC,
			    sizeof(key->basic.ip_proto)))
		goto nla_put_failure;

	if (key->control.addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS &&
	    (fl_dump_key_val(skb, &key->ipv4.src, TCA_FLOWER_KEY_IPV4_SRC,
			     &mask->ipv4.src, TCA_FLOWER_KEY_IPV4_SRC_MASK,
			     sizeof(key->ipv4.src)) ||
	     fl_dump_key_val(skb, &key->ipv4.dst, TCA_FLOWER_KEY_IPV4_DST,
			     &mask->ipv4.dst, TCA_FLOWER_KEY_IPV4_DST_MASK,
			     sizeof(key->ipv4.dst))))
		goto nla_put_failure;
	else if (key->control.addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS &&
		 (fl_dump_key_val(skb, &key->ipv6.src, TCA_FLOWER_KEY_IPV6_SRC,
				  &mask->ipv6.src, TCA_FLOWER_KEY_IPV6_SRC_MASK,
				  sizeof(key->ipv6.src)) ||
		  fl_dump_key_val(skb, &key->ipv6.dst, TCA_FLOWER_KEY_IPV6_DST,
				  &mask->ipv6.dst, TCA_FLOWER_KEY_IPV6_DST_MASK,
				  sizeof(key->ipv6.dst))))
		goto nla_put_failure;

	if (key->basic.ip_proto == IPPROTO_TCP &&
	    (fl_dump_key_val(skb, &key->tp.src, TCA_FLOWER_KEY_TCP_SRC,
			     &mask->tp.src, TCA_FLOWER_UNSPEC,
			     sizeof(key->tp.src)) ||
	     fl_dump_key_val(skb, &key->tp.dst, TCA_FLOWER_KEY_TCP_DST,
			     &mask->tp.dst, TCA_FLOWER_UNSPEC,
			     sizeof(key->tp.dst))))
		goto nla_put_failure;
	else if (key->basic.ip_proto == IPPROTO_UDP &&
		 (fl_dump_key_val(skb, &key->tp.src, TCA_FLOWER_KEY_UDP_SRC,
				  &mask->tp.src, TCA_FLOWER_UNSPEC,
				  sizeof(key->tp.src)) ||
		  fl_dump_key_val(skb, &key->tp.dst, TCA_FLOWER_KEY_UDP_DST,
				  &mask->tp.dst, TCA_FLOWER_UNSPEC,
				  sizeof(key->tp.dst))))
		goto nla_put_failure;

	if (tcf_exts_dump(skb, &f->exts))
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &f->exts) < 0)
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static struct tcf_proto_ops cls_fl_ops __read_mostly = {
	.kind		= "flower",
	.classify	= fl_classify,
	.init		= fl_init,
	.destroy	= fl_destroy,
	.get		= fl_get,
	.change		= fl_change,
	.delete		= fl_delete,
	.walk		= fl_walk,
	.dump		= fl_dump,
	.owner		= THIS_MODULE,
};

static int __init cls_fl_init(void)
{
	return register_tcf_proto_ops(&cls_fl_ops);
}

static void __exit cls_fl_exit(void)
{
	unregister_tcf_proto_ops(&cls_fl_ops);
}

module_init(cls_fl_init);
module_exit(cls_fl_exit);

MODULE_AUTHOR("Jiri Pirko <jiri@resnulli.us>");
MODULE_DESCRIPTION("Flower classifier");
MODULE_LICENSE("GPL v2");
