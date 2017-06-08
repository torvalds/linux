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
#include <linux/workqueue.h>

#include <linux/if_ether.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/mpls.h>

#include <net/sch_generic.h>
#include <net/pkt_cls.h>
#include <net/ip.h>
#include <net/flow_dissector.h>

#include <net/dst.h>
#include <net/dst_metadata.h>

struct fl_flow_key {
	int	indev_ifindex;
	struct flow_dissector_key_control control;
	struct flow_dissector_key_control enc_control;
	struct flow_dissector_key_basic basic;
	struct flow_dissector_key_eth_addrs eth;
	struct flow_dissector_key_vlan vlan;
	union {
		struct flow_dissector_key_ipv4_addrs ipv4;
		struct flow_dissector_key_ipv6_addrs ipv6;
	};
	struct flow_dissector_key_ports tp;
	struct flow_dissector_key_icmp icmp;
	struct flow_dissector_key_arp arp;
	struct flow_dissector_key_keyid enc_key_id;
	union {
		struct flow_dissector_key_ipv4_addrs enc_ipv4;
		struct flow_dissector_key_ipv6_addrs enc_ipv6;
	};
	struct flow_dissector_key_ports enc_tp;
	struct flow_dissector_key_mpls mpls;
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
	union {
		struct work_struct work;
		struct rcu_head	rcu;
	};
};

struct cls_fl_filter {
	struct rhash_head ht_node;
	struct fl_flow_key mkey;
	struct tcf_exts exts;
	struct tcf_result res;
	struct fl_flow_key key;
	struct list_head list;
	u32 handle;
	u32 flags;
	struct rcu_head	rcu;
	struct tc_to_netdev tc;
	struct net_device *hw_dev;
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

static struct cls_fl_filter *fl_lookup(struct cls_fl_head *head,
				       struct fl_flow_key *mkey)
{
	return rhashtable_lookup_fast(&head->ht,
				      fl_key_get_start(mkey, &head->mask),
				      head->ht_params);
}

static int fl_classify(struct sk_buff *skb, const struct tcf_proto *tp,
		       struct tcf_result *res)
{
	struct cls_fl_head *head = rcu_dereference_bh(tp->root);
	struct cls_fl_filter *f;
	struct fl_flow_key skb_key;
	struct fl_flow_key skb_mkey;
	struct ip_tunnel_info *info;

	if (!atomic_read(&head->ht.nelems))
		return -1;

	fl_clear_masked_range(&skb_key, &head->mask);

	info = skb_tunnel_info(skb);
	if (info) {
		struct ip_tunnel_key *key = &info->key;

		switch (ip_tunnel_info_af(info)) {
		case AF_INET:
			skb_key.enc_control.addr_type =
				FLOW_DISSECTOR_KEY_IPV4_ADDRS;
			skb_key.enc_ipv4.src = key->u.ipv4.src;
			skb_key.enc_ipv4.dst = key->u.ipv4.dst;
			break;
		case AF_INET6:
			skb_key.enc_control.addr_type =
				FLOW_DISSECTOR_KEY_IPV6_ADDRS;
			skb_key.enc_ipv6.src = key->u.ipv6.src;
			skb_key.enc_ipv6.dst = key->u.ipv6.dst;
			break;
		}

		skb_key.enc_key_id.keyid = tunnel_id_to_key32(key->tun_id);
		skb_key.enc_tp.src = key->tp_src;
		skb_key.enc_tp.dst = key->tp_dst;
	}

	skb_key.indev_ifindex = skb->skb_iif;
	/* skb_flow_dissect() does not set n_proto in case an unknown protocol,
	 * so do it rather here.
	 */
	skb_key.basic.n_proto = skb->protocol;
	skb_flow_dissect(skb, &head->dissector, &skb_key, 0);

	fl_set_masked_key(&skb_mkey, &skb_key, &head->mask);

	f = fl_lookup(head, &skb_mkey);
	if (f && !tc_skip_sw(f->flags)) {
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

static void fl_hw_destroy_filter(struct tcf_proto *tp, struct cls_fl_filter *f)
{
	struct tc_cls_flower_offload offload = {0};
	struct net_device *dev = f->hw_dev;
	struct tc_to_netdev *tc = &f->tc;

	if (!tc_can_offload(dev, tp))
		return;

	offload.command = TC_CLSFLOWER_DESTROY;
	offload.prio = tp->prio;
	offload.cookie = (unsigned long)f;

	tc->type = TC_SETUP_CLSFLOWER;
	tc->cls_flower = &offload;

	dev->netdev_ops->ndo_setup_tc(dev, tp->q->handle, tp->protocol, tc);
}

static int fl_hw_replace_filter(struct tcf_proto *tp,
				struct flow_dissector *dissector,
				struct fl_flow_key *mask,
				struct cls_fl_filter *f)
{
	struct net_device *dev = tp->q->dev_queue->dev;
	struct tc_cls_flower_offload offload = {0};
	struct tc_to_netdev *tc = &f->tc;
	int err;

	if (!tc_can_offload(dev, tp)) {
		if (tcf_exts_get_dev(dev, &f->exts, &f->hw_dev) ||
		    (f->hw_dev && !tc_can_offload(f->hw_dev, tp))) {
			f->hw_dev = dev;
			return tc_skip_sw(f->flags) ? -EINVAL : 0;
		}
		dev = f->hw_dev;
		tc->egress_dev = true;
	} else {
		f->hw_dev = dev;
	}

	offload.command = TC_CLSFLOWER_REPLACE;
	offload.prio = tp->prio;
	offload.cookie = (unsigned long)f;
	offload.dissector = dissector;
	offload.mask = mask;
	offload.key = &f->mkey;
	offload.exts = &f->exts;

	tc->type = TC_SETUP_CLSFLOWER;
	tc->cls_flower = &offload;

	err = dev->netdev_ops->ndo_setup_tc(dev, tp->q->handle, tp->protocol,
					    tc);
	if (!err)
		f->flags |= TCA_CLS_FLAGS_IN_HW;

	if (tc_skip_sw(f->flags))
		return err;
	return 0;
}

static void fl_hw_update_stats(struct tcf_proto *tp, struct cls_fl_filter *f)
{
	struct tc_cls_flower_offload offload = {0};
	struct net_device *dev = f->hw_dev;
	struct tc_to_netdev *tc = &f->tc;

	if (!tc_can_offload(dev, tp))
		return;

	offload.command = TC_CLSFLOWER_STATS;
	offload.prio = tp->prio;
	offload.cookie = (unsigned long)f;
	offload.exts = &f->exts;

	tc->type = TC_SETUP_CLSFLOWER;
	tc->cls_flower = &offload;

	dev->netdev_ops->ndo_setup_tc(dev, tp->q->handle, tp->protocol, tc);
}

static void __fl_delete(struct tcf_proto *tp, struct cls_fl_filter *f)
{
	list_del_rcu(&f->list);
	if (!tc_skip_hw(f->flags))
		fl_hw_destroy_filter(tp, f);
	tcf_unbind_filter(tp, &f->res);
	call_rcu(&f->rcu, fl_destroy_filter);
}

static void fl_destroy_sleepable(struct work_struct *work)
{
	struct cls_fl_head *head = container_of(work, struct cls_fl_head,
						work);
	if (head->mask_assigned)
		rhashtable_destroy(&head->ht);
	kfree(head);
	module_put(THIS_MODULE);
}

static void fl_destroy_rcu(struct rcu_head *rcu)
{
	struct cls_fl_head *head = container_of(rcu, struct cls_fl_head, rcu);

	INIT_WORK(&head->work, fl_destroy_sleepable);
	schedule_work(&head->work);
}

static void fl_destroy(struct tcf_proto *tp)
{
	struct cls_fl_head *head = rtnl_dereference(tp->root);
	struct cls_fl_filter *f, *next;

	list_for_each_entry_safe(f, next, &head->filters, list)
		__fl_delete(tp, f);

	__module_get(THIS_MODULE);
	call_rcu(&head->rcu, fl_destroy_rcu);
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
	[TCA_FLOWER_KEY_VLAN_ID]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_VLAN_PRIO]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_VLAN_ETH_TYPE]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_ENC_KEY_ID]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ENC_IPV4_SRC]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ENC_IPV4_SRC_MASK] = { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ENC_IPV4_DST]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ENC_IPV4_DST_MASK] = { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ENC_IPV6_SRC]	= { .len = sizeof(struct in6_addr) },
	[TCA_FLOWER_KEY_ENC_IPV6_SRC_MASK] = { .len = sizeof(struct in6_addr) },
	[TCA_FLOWER_KEY_ENC_IPV6_DST]	= { .len = sizeof(struct in6_addr) },
	[TCA_FLOWER_KEY_ENC_IPV6_DST_MASK] = { .len = sizeof(struct in6_addr) },
	[TCA_FLOWER_KEY_TCP_SRC_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_TCP_DST_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_UDP_SRC_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_UDP_DST_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_SCTP_SRC_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_SCTP_DST_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_SCTP_SRC]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_SCTP_DST]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_ENC_UDP_SRC_PORT]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_ENC_UDP_SRC_PORT_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_ENC_UDP_DST_PORT]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_ENC_UDP_DST_PORT_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_FLAGS]		= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_FLAGS_MASK]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ICMPV4_TYPE]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ICMPV4_TYPE_MASK] = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ICMPV4_CODE]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ICMPV4_CODE_MASK] = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ICMPV6_TYPE]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ICMPV6_TYPE_MASK] = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ICMPV6_CODE]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ICMPV6_CODE_MASK] = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ARP_SIP]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ARP_SIP_MASK]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ARP_TIP]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ARP_TIP_MASK]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ARP_OP]		= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ARP_OP_MASK]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ARP_SHA]	= { .len = ETH_ALEN },
	[TCA_FLOWER_KEY_ARP_SHA_MASK]	= { .len = ETH_ALEN },
	[TCA_FLOWER_KEY_ARP_THA]	= { .len = ETH_ALEN },
	[TCA_FLOWER_KEY_ARP_THA_MASK]	= { .len = ETH_ALEN },
	[TCA_FLOWER_KEY_MPLS_TTL]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_MPLS_BOS]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_MPLS_TC]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_MPLS_LABEL]	= { .type = NLA_U32 },
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

static int fl_set_key_mpls(struct nlattr **tb,
			   struct flow_dissector_key_mpls *key_val,
			   struct flow_dissector_key_mpls *key_mask)
{
	if (tb[TCA_FLOWER_KEY_MPLS_TTL]) {
		key_val->mpls_ttl = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_TTL]);
		key_mask->mpls_ttl = MPLS_TTL_MASK;
	}
	if (tb[TCA_FLOWER_KEY_MPLS_BOS]) {
		u8 bos = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_BOS]);

		if (bos & ~MPLS_BOS_MASK)
			return -EINVAL;
		key_val->mpls_bos = bos;
		key_mask->mpls_bos = MPLS_BOS_MASK;
	}
	if (tb[TCA_FLOWER_KEY_MPLS_TC]) {
		u8 tc = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_TC]);

		if (tc & ~MPLS_TC_MASK)
			return -EINVAL;
		key_val->mpls_tc = tc;
		key_mask->mpls_tc = MPLS_TC_MASK;
	}
	if (tb[TCA_FLOWER_KEY_MPLS_LABEL]) {
		u32 label = nla_get_u32(tb[TCA_FLOWER_KEY_MPLS_LABEL]);

		if (label & ~MPLS_LABEL_MASK)
			return -EINVAL;
		key_val->mpls_label = label;
		key_mask->mpls_label = MPLS_LABEL_MASK;
	}
	return 0;
}

static void fl_set_key_vlan(struct nlattr **tb,
			    struct flow_dissector_key_vlan *key_val,
			    struct flow_dissector_key_vlan *key_mask)
{
#define VLAN_PRIORITY_MASK	0x7

	if (tb[TCA_FLOWER_KEY_VLAN_ID]) {
		key_val->vlan_id =
			nla_get_u16(tb[TCA_FLOWER_KEY_VLAN_ID]) & VLAN_VID_MASK;
		key_mask->vlan_id = VLAN_VID_MASK;
	}
	if (tb[TCA_FLOWER_KEY_VLAN_PRIO]) {
		key_val->vlan_priority =
			nla_get_u8(tb[TCA_FLOWER_KEY_VLAN_PRIO]) &
			VLAN_PRIORITY_MASK;
		key_mask->vlan_priority = VLAN_PRIORITY_MASK;
	}
}

static void fl_set_key_flag(u32 flower_key, u32 flower_mask,
			    u32 *dissector_key, u32 *dissector_mask,
			    u32 flower_flag_bit, u32 dissector_flag_bit)
{
	if (flower_mask & flower_flag_bit) {
		*dissector_mask |= dissector_flag_bit;
		if (flower_key & flower_flag_bit)
			*dissector_key |= dissector_flag_bit;
	}
}

static int fl_set_key_flags(struct nlattr **tb,
			    u32 *flags_key, u32 *flags_mask)
{
	u32 key, mask;

	/* mask is mandatory for flags */
	if (!tb[TCA_FLOWER_KEY_FLAGS_MASK])
		return -EINVAL;

	key = be32_to_cpu(nla_get_u32(tb[TCA_FLOWER_KEY_FLAGS]));
	mask = be32_to_cpu(nla_get_u32(tb[TCA_FLOWER_KEY_FLAGS_MASK]));

	*flags_key  = 0;
	*flags_mask = 0;

	fl_set_key_flag(key, mask, flags_key, flags_mask,
			TCA_FLOWER_KEY_FLAGS_IS_FRAGMENT, FLOW_DIS_IS_FRAGMENT);

	return 0;
}

static int fl_set_key(struct net *net, struct nlattr **tb,
		      struct fl_flow_key *key, struct fl_flow_key *mask)
{
	__be16 ethertype;
	int ret = 0;
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

	if (tb[TCA_FLOWER_KEY_ETH_TYPE]) {
		ethertype = nla_get_be16(tb[TCA_FLOWER_KEY_ETH_TYPE]);

		if (ethertype == htons(ETH_P_8021Q)) {
			fl_set_key_vlan(tb, &key->vlan, &mask->vlan);
			fl_set_key_val(tb, &key->basic.n_proto,
				       TCA_FLOWER_KEY_VLAN_ETH_TYPE,
				       &mask->basic.n_proto, TCA_FLOWER_UNSPEC,
				       sizeof(key->basic.n_proto));
		} else {
			key->basic.n_proto = ethertype;
			mask->basic.n_proto = cpu_to_be16(~0);
		}
	}

	if (key->basic.n_proto == htons(ETH_P_IP) ||
	    key->basic.n_proto == htons(ETH_P_IPV6)) {
		fl_set_key_val(tb, &key->basic.ip_proto, TCA_FLOWER_KEY_IP_PROTO,
			       &mask->basic.ip_proto, TCA_FLOWER_UNSPEC,
			       sizeof(key->basic.ip_proto));
	}

	if (tb[TCA_FLOWER_KEY_IPV4_SRC] || tb[TCA_FLOWER_KEY_IPV4_DST]) {
		key->control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
		mask->control.addr_type = ~0;
		fl_set_key_val(tb, &key->ipv4.src, TCA_FLOWER_KEY_IPV4_SRC,
			       &mask->ipv4.src, TCA_FLOWER_KEY_IPV4_SRC_MASK,
			       sizeof(key->ipv4.src));
		fl_set_key_val(tb, &key->ipv4.dst, TCA_FLOWER_KEY_IPV4_DST,
			       &mask->ipv4.dst, TCA_FLOWER_KEY_IPV4_DST_MASK,
			       sizeof(key->ipv4.dst));
	} else if (tb[TCA_FLOWER_KEY_IPV6_SRC] || tb[TCA_FLOWER_KEY_IPV6_DST]) {
		key->control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		mask->control.addr_type = ~0;
		fl_set_key_val(tb, &key->ipv6.src, TCA_FLOWER_KEY_IPV6_SRC,
			       &mask->ipv6.src, TCA_FLOWER_KEY_IPV6_SRC_MASK,
			       sizeof(key->ipv6.src));
		fl_set_key_val(tb, &key->ipv6.dst, TCA_FLOWER_KEY_IPV6_DST,
			       &mask->ipv6.dst, TCA_FLOWER_KEY_IPV6_DST_MASK,
			       sizeof(key->ipv6.dst));
	}

	if (key->basic.ip_proto == IPPROTO_TCP) {
		fl_set_key_val(tb, &key->tp.src, TCA_FLOWER_KEY_TCP_SRC,
			       &mask->tp.src, TCA_FLOWER_KEY_TCP_SRC_MASK,
			       sizeof(key->tp.src));
		fl_set_key_val(tb, &key->tp.dst, TCA_FLOWER_KEY_TCP_DST,
			       &mask->tp.dst, TCA_FLOWER_KEY_TCP_DST_MASK,
			       sizeof(key->tp.dst));
	} else if (key->basic.ip_proto == IPPROTO_UDP) {
		fl_set_key_val(tb, &key->tp.src, TCA_FLOWER_KEY_UDP_SRC,
			       &mask->tp.src, TCA_FLOWER_KEY_UDP_SRC_MASK,
			       sizeof(key->tp.src));
		fl_set_key_val(tb, &key->tp.dst, TCA_FLOWER_KEY_UDP_DST,
			       &mask->tp.dst, TCA_FLOWER_KEY_UDP_DST_MASK,
			       sizeof(key->tp.dst));
	} else if (key->basic.ip_proto == IPPROTO_SCTP) {
		fl_set_key_val(tb, &key->tp.src, TCA_FLOWER_KEY_SCTP_SRC,
			       &mask->tp.src, TCA_FLOWER_KEY_SCTP_SRC_MASK,
			       sizeof(key->tp.src));
		fl_set_key_val(tb, &key->tp.dst, TCA_FLOWER_KEY_SCTP_DST,
			       &mask->tp.dst, TCA_FLOWER_KEY_SCTP_DST_MASK,
			       sizeof(key->tp.dst));
	} else if (key->basic.n_proto == htons(ETH_P_IP) &&
		   key->basic.ip_proto == IPPROTO_ICMP) {
		fl_set_key_val(tb, &key->icmp.type, TCA_FLOWER_KEY_ICMPV4_TYPE,
			       &mask->icmp.type,
			       TCA_FLOWER_KEY_ICMPV4_TYPE_MASK,
			       sizeof(key->icmp.type));
		fl_set_key_val(tb, &key->icmp.code, TCA_FLOWER_KEY_ICMPV4_CODE,
			       &mask->icmp.code,
			       TCA_FLOWER_KEY_ICMPV4_CODE_MASK,
			       sizeof(key->icmp.code));
	} else if (key->basic.n_proto == htons(ETH_P_IPV6) &&
		   key->basic.ip_proto == IPPROTO_ICMPV6) {
		fl_set_key_val(tb, &key->icmp.type, TCA_FLOWER_KEY_ICMPV6_TYPE,
			       &mask->icmp.type,
			       TCA_FLOWER_KEY_ICMPV6_TYPE_MASK,
			       sizeof(key->icmp.type));
		fl_set_key_val(tb, &key->icmp.code, TCA_FLOWER_KEY_ICMPV6_CODE,
			       &mask->icmp.code,
			       TCA_FLOWER_KEY_ICMPV6_CODE_MASK,
			       sizeof(key->icmp.code));
	} else if (key->basic.n_proto == htons(ETH_P_MPLS_UC) ||
		   key->basic.n_proto == htons(ETH_P_MPLS_MC)) {
		ret = fl_set_key_mpls(tb, &key->mpls, &mask->mpls);
		if (ret)
			return ret;
	} else if (key->basic.n_proto == htons(ETH_P_ARP) ||
		   key->basic.n_proto == htons(ETH_P_RARP)) {
		fl_set_key_val(tb, &key->arp.sip, TCA_FLOWER_KEY_ARP_SIP,
			       &mask->arp.sip, TCA_FLOWER_KEY_ARP_SIP_MASK,
			       sizeof(key->arp.sip));
		fl_set_key_val(tb, &key->arp.tip, TCA_FLOWER_KEY_ARP_TIP,
			       &mask->arp.tip, TCA_FLOWER_KEY_ARP_TIP_MASK,
			       sizeof(key->arp.tip));
		fl_set_key_val(tb, &key->arp.op, TCA_FLOWER_KEY_ARP_OP,
			       &mask->arp.op, TCA_FLOWER_KEY_ARP_OP_MASK,
			       sizeof(key->arp.op));
		fl_set_key_val(tb, key->arp.sha, TCA_FLOWER_KEY_ARP_SHA,
			       mask->arp.sha, TCA_FLOWER_KEY_ARP_SHA_MASK,
			       sizeof(key->arp.sha));
		fl_set_key_val(tb, key->arp.tha, TCA_FLOWER_KEY_ARP_THA,
			       mask->arp.tha, TCA_FLOWER_KEY_ARP_THA_MASK,
			       sizeof(key->arp.tha));
	}

	if (tb[TCA_FLOWER_KEY_ENC_IPV4_SRC] ||
	    tb[TCA_FLOWER_KEY_ENC_IPV4_DST]) {
		key->enc_control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
		mask->enc_control.addr_type = ~0;
		fl_set_key_val(tb, &key->enc_ipv4.src,
			       TCA_FLOWER_KEY_ENC_IPV4_SRC,
			       &mask->enc_ipv4.src,
			       TCA_FLOWER_KEY_ENC_IPV4_SRC_MASK,
			       sizeof(key->enc_ipv4.src));
		fl_set_key_val(tb, &key->enc_ipv4.dst,
			       TCA_FLOWER_KEY_ENC_IPV4_DST,
			       &mask->enc_ipv4.dst,
			       TCA_FLOWER_KEY_ENC_IPV4_DST_MASK,
			       sizeof(key->enc_ipv4.dst));
	}

	if (tb[TCA_FLOWER_KEY_ENC_IPV6_SRC] ||
	    tb[TCA_FLOWER_KEY_ENC_IPV6_DST]) {
		key->enc_control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		mask->enc_control.addr_type = ~0;
		fl_set_key_val(tb, &key->enc_ipv6.src,
			       TCA_FLOWER_KEY_ENC_IPV6_SRC,
			       &mask->enc_ipv6.src,
			       TCA_FLOWER_KEY_ENC_IPV6_SRC_MASK,
			       sizeof(key->enc_ipv6.src));
		fl_set_key_val(tb, &key->enc_ipv6.dst,
			       TCA_FLOWER_KEY_ENC_IPV6_DST,
			       &mask->enc_ipv6.dst,
			       TCA_FLOWER_KEY_ENC_IPV6_DST_MASK,
			       sizeof(key->enc_ipv6.dst));
	}

	fl_set_key_val(tb, &key->enc_key_id.keyid, TCA_FLOWER_KEY_ENC_KEY_ID,
		       &mask->enc_key_id.keyid, TCA_FLOWER_UNSPEC,
		       sizeof(key->enc_key_id.keyid));

	fl_set_key_val(tb, &key->enc_tp.src, TCA_FLOWER_KEY_ENC_UDP_SRC_PORT,
		       &mask->enc_tp.src, TCA_FLOWER_KEY_ENC_UDP_SRC_PORT_MASK,
		       sizeof(key->enc_tp.src));

	fl_set_key_val(tb, &key->enc_tp.dst, TCA_FLOWER_KEY_ENC_UDP_DST_PORT,
		       &mask->enc_tp.dst, TCA_FLOWER_KEY_ENC_UDP_DST_PORT_MASK,
		       sizeof(key->enc_tp.dst));

	if (tb[TCA_FLOWER_KEY_FLAGS])
		ret = fl_set_key_flags(tb, &key->control.flags, &mask->control.flags);

	return ret;
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

#define FL_KEY_IS_MASKED(mask, member)						\
	memchr_inv(((char *)mask) + FL_KEY_MEMBER_OFFSET(member),		\
		   0, FL_KEY_MEMBER_SIZE(member))				\

#define FL_KEY_SET(keys, cnt, id, member)					\
	do {									\
		keys[cnt].key_id = id;						\
		keys[cnt].offset = FL_KEY_MEMBER_OFFSET(member);		\
		cnt++;								\
	} while(0);

#define FL_KEY_SET_IF_MASKED(mask, keys, cnt, id, member)			\
	do {									\
		if (FL_KEY_IS_MASKED(mask, member))				\
			FL_KEY_SET(keys, cnt, id, member);			\
	} while(0);

static void fl_init_dissector(struct cls_fl_head *head,
			      struct fl_flow_mask *mask)
{
	struct flow_dissector_key keys[FLOW_DISSECTOR_KEY_MAX];
	size_t cnt = 0;

	FL_KEY_SET(keys, cnt, FLOW_DISSECTOR_KEY_CONTROL, control);
	FL_KEY_SET(keys, cnt, FLOW_DISSECTOR_KEY_BASIC, basic);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_ETH_ADDRS, eth);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_IPV4_ADDRS, ipv4);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_IPV6_ADDRS, ipv6);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_PORTS, tp);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_ICMP, icmp);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_ARP, arp);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_MPLS, mpls);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_VLAN, vlan);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_KEYID, enc_key_id);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS, enc_ipv4);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS, enc_ipv6);
	if (FL_KEY_IS_MASKED(&mask->key, enc_ipv4) ||
	    FL_KEY_IS_MASKED(&mask->key, enc_ipv6))
		FL_KEY_SET(keys, cnt, FLOW_DISSECTOR_KEY_ENC_CONTROL,
			   enc_control);
	FL_KEY_SET_IF_MASKED(&mask->key, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_PORTS, enc_tp);

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

	err = tcf_exts_init(&e, TCA_FLOWER_ACT, 0);
	if (err < 0)
		return err;
	err = tcf_exts_validate(net, tp, tb, est, &e, ovr);
	if (err < 0)
		goto errout;

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
	struct nlattr **tb;
	struct fl_flow_mask mask = {};
	int err;

	if (!tca[TCA_OPTIONS])
		return -EINVAL;

	tb = kcalloc(TCA_FLOWER_MAX + 1, sizeof(struct nlattr *), GFP_KERNEL);
	if (!tb)
		return -ENOBUFS;

	err = nla_parse_nested(tb, TCA_FLOWER_MAX, tca[TCA_OPTIONS],
			       fl_policy, NULL);
	if (err < 0)
		goto errout_tb;

	if (fold && handle && fold->handle != handle) {
		err = -EINVAL;
		goto errout_tb;
	}

	fnew = kzalloc(sizeof(*fnew), GFP_KERNEL);
	if (!fnew) {
		err = -ENOBUFS;
		goto errout_tb;
	}

	err = tcf_exts_init(&fnew->exts, TCA_FLOWER_ACT, 0);
	if (err < 0)
		goto errout;

	if (!handle) {
		handle = fl_grab_new_handle(tp, head);
		if (!handle) {
			err = -EINVAL;
			goto errout;
		}
	}
	fnew->handle = handle;

	if (tb[TCA_FLOWER_FLAGS]) {
		fnew->flags = nla_get_u32(tb[TCA_FLOWER_FLAGS]);

		if (!tc_flags_valid(fnew->flags)) {
			err = -EINVAL;
			goto errout;
		}
	}

	err = fl_set_parms(net, tp, fnew, &mask, base, tb, tca[TCA_RATE], ovr);
	if (err)
		goto errout;

	err = fl_check_assign_mask(head, &mask);
	if (err)
		goto errout;

	if (!tc_skip_sw(fnew->flags)) {
		if (!fold && fl_lookup(head, &fnew->mkey)) {
			err = -EEXIST;
			goto errout;
		}

		err = rhashtable_insert_fast(&head->ht, &fnew->ht_node,
					     head->ht_params);
		if (err)
			goto errout;
	}

	if (!tc_skip_hw(fnew->flags)) {
		err = fl_hw_replace_filter(tp,
					   &head->dissector,
					   &mask.key,
					   fnew);
		if (err)
			goto errout;
	}

	if (!tc_in_hw(fnew->flags))
		fnew->flags |= TCA_CLS_FLAGS_NOT_IN_HW;

	if (fold) {
		if (!tc_skip_sw(fold->flags))
			rhashtable_remove_fast(&head->ht, &fold->ht_node,
					       head->ht_params);
		if (!tc_skip_hw(fold->flags))
			fl_hw_destroy_filter(tp, fold);
	}

	*arg = (unsigned long) fnew;

	if (fold) {
		list_replace_rcu(&fold->list, &fnew->list);
		tcf_unbind_filter(tp, &fold->res);
		call_rcu(&fold->rcu, fl_destroy_filter);
	} else {
		list_add_tail_rcu(&fnew->list, &head->filters);
	}

	kfree(tb);
	return 0;

errout:
	tcf_exts_destroy(&fnew->exts);
	kfree(fnew);
errout_tb:
	kfree(tb);
	return err;
}

static int fl_delete(struct tcf_proto *tp, unsigned long arg, bool *last)
{
	struct cls_fl_head *head = rtnl_dereference(tp->root);
	struct cls_fl_filter *f = (struct cls_fl_filter *) arg;

	if (!tc_skip_sw(f->flags))
		rhashtable_remove_fast(&head->ht, &f->ht_node,
				       head->ht_params);
	__fl_delete(tp, f);
	*last = list_empty(&head->filters);
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

static int fl_dump_key_mpls(struct sk_buff *skb,
			    struct flow_dissector_key_mpls *mpls_key,
			    struct flow_dissector_key_mpls *mpls_mask)
{
	int err;

	if (!memchr_inv(mpls_mask, 0, sizeof(*mpls_mask)))
		return 0;
	if (mpls_mask->mpls_ttl) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_TTL,
				 mpls_key->mpls_ttl);
		if (err)
			return err;
	}
	if (mpls_mask->mpls_tc) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_TC,
				 mpls_key->mpls_tc);
		if (err)
			return err;
	}
	if (mpls_mask->mpls_label) {
		err = nla_put_u32(skb, TCA_FLOWER_KEY_MPLS_LABEL,
				  mpls_key->mpls_label);
		if (err)
			return err;
	}
	if (mpls_mask->mpls_bos) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_BOS,
				 mpls_key->mpls_bos);
		if (err)
			return err;
	}
	return 0;
}

static int fl_dump_key_vlan(struct sk_buff *skb,
			    struct flow_dissector_key_vlan *vlan_key,
			    struct flow_dissector_key_vlan *vlan_mask)
{
	int err;

	if (!memchr_inv(vlan_mask, 0, sizeof(*vlan_mask)))
		return 0;
	if (vlan_mask->vlan_id) {
		err = nla_put_u16(skb, TCA_FLOWER_KEY_VLAN_ID,
				  vlan_key->vlan_id);
		if (err)
			return err;
	}
	if (vlan_mask->vlan_priority) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_VLAN_PRIO,
				 vlan_key->vlan_priority);
		if (err)
			return err;
	}
	return 0;
}

static void fl_get_key_flag(u32 dissector_key, u32 dissector_mask,
			    u32 *flower_key, u32 *flower_mask,
			    u32 flower_flag_bit, u32 dissector_flag_bit)
{
	if (dissector_mask & dissector_flag_bit) {
		*flower_mask |= flower_flag_bit;
		if (dissector_key & dissector_flag_bit)
			*flower_key |= flower_flag_bit;
	}
}

static int fl_dump_key_flags(struct sk_buff *skb, u32 flags_key, u32 flags_mask)
{
	u32 key, mask;
	__be32 _key, _mask;
	int err;

	if (!memchr_inv(&flags_mask, 0, sizeof(flags_mask)))
		return 0;

	key = 0;
	mask = 0;

	fl_get_key_flag(flags_key, flags_mask, &key, &mask,
			TCA_FLOWER_KEY_FLAGS_IS_FRAGMENT, FLOW_DIS_IS_FRAGMENT);

	_key = cpu_to_be32(key);
	_mask = cpu_to_be32(mask);

	err = nla_put(skb, TCA_FLOWER_KEY_FLAGS, 4, &_key);
	if (err)
		return err;

	return nla_put(skb, TCA_FLOWER_KEY_FLAGS_MASK, 4, &_mask);
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

	if (!tc_skip_hw(f->flags))
		fl_hw_update_stats(tp, f);

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

	if (fl_dump_key_mpls(skb, &key->mpls, &mask->mpls))
		goto nla_put_failure;

	if (fl_dump_key_vlan(skb, &key->vlan, &mask->vlan))
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
			     &mask->tp.src, TCA_FLOWER_KEY_TCP_SRC_MASK,
			     sizeof(key->tp.src)) ||
	     fl_dump_key_val(skb, &key->tp.dst, TCA_FLOWER_KEY_TCP_DST,
			     &mask->tp.dst, TCA_FLOWER_KEY_TCP_DST_MASK,
			     sizeof(key->tp.dst))))
		goto nla_put_failure;
	else if (key->basic.ip_proto == IPPROTO_UDP &&
		 (fl_dump_key_val(skb, &key->tp.src, TCA_FLOWER_KEY_UDP_SRC,
				  &mask->tp.src, TCA_FLOWER_KEY_UDP_SRC_MASK,
				  sizeof(key->tp.src)) ||
		  fl_dump_key_val(skb, &key->tp.dst, TCA_FLOWER_KEY_UDP_DST,
				  &mask->tp.dst, TCA_FLOWER_KEY_UDP_DST_MASK,
				  sizeof(key->tp.dst))))
		goto nla_put_failure;
	else if (key->basic.ip_proto == IPPROTO_SCTP &&
		 (fl_dump_key_val(skb, &key->tp.src, TCA_FLOWER_KEY_SCTP_SRC,
				  &mask->tp.src, TCA_FLOWER_KEY_SCTP_SRC_MASK,
				  sizeof(key->tp.src)) ||
		  fl_dump_key_val(skb, &key->tp.dst, TCA_FLOWER_KEY_SCTP_DST,
				  &mask->tp.dst, TCA_FLOWER_KEY_SCTP_DST_MASK,
				  sizeof(key->tp.dst))))
		goto nla_put_failure;
	else if (key->basic.n_proto == htons(ETH_P_IP) &&
		 key->basic.ip_proto == IPPROTO_ICMP &&
		 (fl_dump_key_val(skb, &key->icmp.type,
				  TCA_FLOWER_KEY_ICMPV4_TYPE, &mask->icmp.type,
				  TCA_FLOWER_KEY_ICMPV4_TYPE_MASK,
				  sizeof(key->icmp.type)) ||
		  fl_dump_key_val(skb, &key->icmp.code,
				  TCA_FLOWER_KEY_ICMPV4_CODE, &mask->icmp.code,
				  TCA_FLOWER_KEY_ICMPV4_CODE_MASK,
				  sizeof(key->icmp.code))))
		goto nla_put_failure;
	else if (key->basic.n_proto == htons(ETH_P_IPV6) &&
		 key->basic.ip_proto == IPPROTO_ICMPV6 &&
		 (fl_dump_key_val(skb, &key->icmp.type,
				  TCA_FLOWER_KEY_ICMPV6_TYPE, &mask->icmp.type,
				  TCA_FLOWER_KEY_ICMPV6_TYPE_MASK,
				  sizeof(key->icmp.type)) ||
		  fl_dump_key_val(skb, &key->icmp.code,
				  TCA_FLOWER_KEY_ICMPV6_CODE, &mask->icmp.code,
				  TCA_FLOWER_KEY_ICMPV6_CODE_MASK,
				  sizeof(key->icmp.code))))
		goto nla_put_failure;
	else if ((key->basic.n_proto == htons(ETH_P_ARP) ||
		  key->basic.n_proto == htons(ETH_P_RARP)) &&
		 (fl_dump_key_val(skb, &key->arp.sip,
				  TCA_FLOWER_KEY_ARP_SIP, &mask->arp.sip,
				  TCA_FLOWER_KEY_ARP_SIP_MASK,
				  sizeof(key->arp.sip)) ||
		  fl_dump_key_val(skb, &key->arp.tip,
				  TCA_FLOWER_KEY_ARP_TIP, &mask->arp.tip,
				  TCA_FLOWER_KEY_ARP_TIP_MASK,
				  sizeof(key->arp.tip)) ||
		  fl_dump_key_val(skb, &key->arp.op,
				  TCA_FLOWER_KEY_ARP_OP, &mask->arp.op,
				  TCA_FLOWER_KEY_ARP_OP_MASK,
				  sizeof(key->arp.op)) ||
		  fl_dump_key_val(skb, key->arp.sha, TCA_FLOWER_KEY_ARP_SHA,
				  mask->arp.sha, TCA_FLOWER_KEY_ARP_SHA_MASK,
				  sizeof(key->arp.sha)) ||
		  fl_dump_key_val(skb, key->arp.tha, TCA_FLOWER_KEY_ARP_THA,
				  mask->arp.tha, TCA_FLOWER_KEY_ARP_THA_MASK,
				  sizeof(key->arp.tha))))
		goto nla_put_failure;

	if (key->enc_control.addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS &&
	    (fl_dump_key_val(skb, &key->enc_ipv4.src,
			    TCA_FLOWER_KEY_ENC_IPV4_SRC, &mask->enc_ipv4.src,
			    TCA_FLOWER_KEY_ENC_IPV4_SRC_MASK,
			    sizeof(key->enc_ipv4.src)) ||
	     fl_dump_key_val(skb, &key->enc_ipv4.dst,
			     TCA_FLOWER_KEY_ENC_IPV4_DST, &mask->enc_ipv4.dst,
			     TCA_FLOWER_KEY_ENC_IPV4_DST_MASK,
			     sizeof(key->enc_ipv4.dst))))
		goto nla_put_failure;
	else if (key->enc_control.addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS &&
		 (fl_dump_key_val(skb, &key->enc_ipv6.src,
			    TCA_FLOWER_KEY_ENC_IPV6_SRC, &mask->enc_ipv6.src,
			    TCA_FLOWER_KEY_ENC_IPV6_SRC_MASK,
			    sizeof(key->enc_ipv6.src)) ||
		 fl_dump_key_val(skb, &key->enc_ipv6.dst,
				 TCA_FLOWER_KEY_ENC_IPV6_DST,
				 &mask->enc_ipv6.dst,
				 TCA_FLOWER_KEY_ENC_IPV6_DST_MASK,
			    sizeof(key->enc_ipv6.dst))))
		goto nla_put_failure;

	if (fl_dump_key_val(skb, &key->enc_key_id, TCA_FLOWER_KEY_ENC_KEY_ID,
			    &mask->enc_key_id, TCA_FLOWER_UNSPEC,
			    sizeof(key->enc_key_id)) ||
	    fl_dump_key_val(skb, &key->enc_tp.src,
			    TCA_FLOWER_KEY_ENC_UDP_SRC_PORT,
			    &mask->enc_tp.src,
			    TCA_FLOWER_KEY_ENC_UDP_SRC_PORT_MASK,
			    sizeof(key->enc_tp.src)) ||
	    fl_dump_key_val(skb, &key->enc_tp.dst,
			    TCA_FLOWER_KEY_ENC_UDP_DST_PORT,
			    &mask->enc_tp.dst,
			    TCA_FLOWER_KEY_ENC_UDP_DST_PORT_MASK,
			    sizeof(key->enc_tp.dst)))
		goto nla_put_failure;

	if (fl_dump_key_flags(skb, key->control.flags, mask->control.flags))
		goto nla_put_failure;

	if (f->flags && nla_put_u32(skb, TCA_FLOWER_FLAGS, f->flags))
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
