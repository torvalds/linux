// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/sched/cls_flower.c		Flower classifier
 *
 * Copyright (c) 2015 Jiri Pirko <jiri@resnulli.us>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/rhashtable.h>
#include <linux/workqueue.h>
#include <linux/refcount.h>

#include <linux/if_ether.h>
#include <linux/in6.h>
#include <linux/ip.h>
#include <linux/mpls.h>

#include <net/sch_generic.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/ip.h>
#include <net/flow_dissector.h>
#include <net/geneve.h>
#include <net/vxlan.h>
#include <net/erspan.h>
#include <net/gtp.h>

#include <net/dst.h>
#include <net/dst_metadata.h>

#include <uapi/linux/netfilter/nf_conntrack_common.h>

#define TCA_FLOWER_KEY_CT_FLAGS_MAX \
		((__TCA_FLOWER_KEY_CT_FLAGS_MAX - 1) << 1)
#define TCA_FLOWER_KEY_CT_FLAGS_MASK \
		(TCA_FLOWER_KEY_CT_FLAGS_MAX - 1)

struct fl_flow_key {
	struct flow_dissector_key_meta meta;
	struct flow_dissector_key_control control;
	struct flow_dissector_key_control enc_control;
	struct flow_dissector_key_basic basic;
	struct flow_dissector_key_eth_addrs eth;
	struct flow_dissector_key_vlan vlan;
	struct flow_dissector_key_vlan cvlan;
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
	struct flow_dissector_key_tcp tcp;
	struct flow_dissector_key_ip ip;
	struct flow_dissector_key_ip enc_ip;
	struct flow_dissector_key_enc_opts enc_opts;
	union {
		struct flow_dissector_key_ports tp;
		struct {
			struct flow_dissector_key_ports tp_min;
			struct flow_dissector_key_ports tp_max;
		};
	} tp_range;
	struct flow_dissector_key_ct ct;
	struct flow_dissector_key_hash hash;
	struct flow_dissector_key_num_of_vlans num_of_vlans;
} __aligned(BITS_PER_LONG / 8); /* Ensure that we can do comparisons as longs. */

struct fl_flow_mask_range {
	unsigned short int start;
	unsigned short int end;
};

struct fl_flow_mask {
	struct fl_flow_key key;
	struct fl_flow_mask_range range;
	u32 flags;
	struct rhash_head ht_node;
	struct rhashtable ht;
	struct rhashtable_params filter_ht_params;
	struct flow_dissector dissector;
	struct list_head filters;
	struct rcu_work rwork;
	struct list_head list;
	refcount_t refcnt;
};

struct fl_flow_tmplt {
	struct fl_flow_key dummy_key;
	struct fl_flow_key mask;
	struct flow_dissector dissector;
	struct tcf_chain *chain;
};

struct cls_fl_head {
	struct rhashtable ht;
	spinlock_t masks_lock; /* Protect masks list */
	struct list_head masks;
	struct list_head hw_filters;
	struct rcu_work rwork;
	struct idr handle_idr;
};

struct cls_fl_filter {
	struct fl_flow_mask *mask;
	struct rhash_head ht_node;
	struct fl_flow_key mkey;
	struct tcf_exts exts;
	struct tcf_result res;
	struct fl_flow_key key;
	struct list_head list;
	struct list_head hw_list;
	u32 handle;
	u32 flags;
	u32 in_hw_count;
	struct rcu_work rwork;
	struct net_device *hw_dev;
	/* Flower classifier is unlocked, which means that its reference counter
	 * can be changed concurrently without any kind of external
	 * synchronization. Use atomic reference counter to be concurrency-safe.
	 */
	refcount_t refcnt;
	bool deleted;
};

static const struct rhashtable_params mask_ht_params = {
	.key_offset = offsetof(struct fl_flow_mask, key),
	.key_len = sizeof(struct fl_flow_key),
	.head_offset = offsetof(struct fl_flow_mask, ht_node),
	.automatic_shrinking = true,
};

static unsigned short int fl_mask_range(const struct fl_flow_mask *mask)
{
	return mask->range.end - mask->range.start;
}

static void fl_mask_update_range(struct fl_flow_mask *mask)
{
	const u8 *bytes = (const u8 *) &mask->key;
	size_t size = sizeof(mask->key);
	size_t i, first = 0, last;

	for (i = 0; i < size; i++) {
		if (bytes[i]) {
			first = i;
			break;
		}
	}
	last = first;
	for (i = size - 1; i != first; i--) {
		if (bytes[i]) {
			last = i;
			break;
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

static bool fl_mask_fits_tmplt(struct fl_flow_tmplt *tmplt,
			       struct fl_flow_mask *mask)
{
	const long *lmask = fl_key_get_start(&mask->key, mask);
	const long *ltmplt;
	int i;

	if (!tmplt)
		return true;
	ltmplt = fl_key_get_start(&tmplt->mask, mask);
	for (i = 0; i < fl_mask_range(mask); i += sizeof(long)) {
		if (~*ltmplt++ & *lmask++)
			return false;
	}
	return true;
}

static void fl_clear_masked_range(struct fl_flow_key *key,
				  struct fl_flow_mask *mask)
{
	memset(fl_key_get_start(key, mask), 0, fl_mask_range(mask));
}

static bool fl_range_port_dst_cmp(struct cls_fl_filter *filter,
				  struct fl_flow_key *key,
				  struct fl_flow_key *mkey)
{
	u16 min_mask, max_mask, min_val, max_val;

	min_mask = ntohs(filter->mask->key.tp_range.tp_min.dst);
	max_mask = ntohs(filter->mask->key.tp_range.tp_max.dst);
	min_val = ntohs(filter->key.tp_range.tp_min.dst);
	max_val = ntohs(filter->key.tp_range.tp_max.dst);

	if (min_mask && max_mask) {
		if (ntohs(key->tp_range.tp.dst) < min_val ||
		    ntohs(key->tp_range.tp.dst) > max_val)
			return false;

		/* skb does not have min and max values */
		mkey->tp_range.tp_min.dst = filter->mkey.tp_range.tp_min.dst;
		mkey->tp_range.tp_max.dst = filter->mkey.tp_range.tp_max.dst;
	}
	return true;
}

static bool fl_range_port_src_cmp(struct cls_fl_filter *filter,
				  struct fl_flow_key *key,
				  struct fl_flow_key *mkey)
{
	u16 min_mask, max_mask, min_val, max_val;

	min_mask = ntohs(filter->mask->key.tp_range.tp_min.src);
	max_mask = ntohs(filter->mask->key.tp_range.tp_max.src);
	min_val = ntohs(filter->key.tp_range.tp_min.src);
	max_val = ntohs(filter->key.tp_range.tp_max.src);

	if (min_mask && max_mask) {
		if (ntohs(key->tp_range.tp.src) < min_val ||
		    ntohs(key->tp_range.tp.src) > max_val)
			return false;

		/* skb does not have min and max values */
		mkey->tp_range.tp_min.src = filter->mkey.tp_range.tp_min.src;
		mkey->tp_range.tp_max.src = filter->mkey.tp_range.tp_max.src;
	}
	return true;
}

static struct cls_fl_filter *__fl_lookup(struct fl_flow_mask *mask,
					 struct fl_flow_key *mkey)
{
	return rhashtable_lookup_fast(&mask->ht, fl_key_get_start(mkey, mask),
				      mask->filter_ht_params);
}

static struct cls_fl_filter *fl_lookup_range(struct fl_flow_mask *mask,
					     struct fl_flow_key *mkey,
					     struct fl_flow_key *key)
{
	struct cls_fl_filter *filter, *f;

	list_for_each_entry_rcu(filter, &mask->filters, list) {
		if (!fl_range_port_dst_cmp(filter, key, mkey))
			continue;

		if (!fl_range_port_src_cmp(filter, key, mkey))
			continue;

		f = __fl_lookup(mask, mkey);
		if (f)
			return f;
	}
	return NULL;
}

static noinline_for_stack
struct cls_fl_filter *fl_mask_lookup(struct fl_flow_mask *mask, struct fl_flow_key *key)
{
	struct fl_flow_key mkey;

	fl_set_masked_key(&mkey, key, mask);
	if ((mask->flags & TCA_FLOWER_MASK_FLAGS_RANGE))
		return fl_lookup_range(mask, &mkey, key);

	return __fl_lookup(mask, &mkey);
}

static u16 fl_ct_info_to_flower_map[] = {
	[IP_CT_ESTABLISHED] =		TCA_FLOWER_KEY_CT_FLAGS_TRACKED |
					TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED,
	[IP_CT_RELATED] =		TCA_FLOWER_KEY_CT_FLAGS_TRACKED |
					TCA_FLOWER_KEY_CT_FLAGS_RELATED,
	[IP_CT_ESTABLISHED_REPLY] =	TCA_FLOWER_KEY_CT_FLAGS_TRACKED |
					TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED |
					TCA_FLOWER_KEY_CT_FLAGS_REPLY,
	[IP_CT_RELATED_REPLY] =		TCA_FLOWER_KEY_CT_FLAGS_TRACKED |
					TCA_FLOWER_KEY_CT_FLAGS_RELATED |
					TCA_FLOWER_KEY_CT_FLAGS_REPLY,
	[IP_CT_NEW] =			TCA_FLOWER_KEY_CT_FLAGS_TRACKED |
					TCA_FLOWER_KEY_CT_FLAGS_NEW,
};

static int fl_classify(struct sk_buff *skb, const struct tcf_proto *tp,
		       struct tcf_result *res)
{
	struct cls_fl_head *head = rcu_dereference_bh(tp->root);
	bool post_ct = tc_skb_cb(skb)->post_ct;
	u16 zone = tc_skb_cb(skb)->zone;
	struct fl_flow_key skb_key;
	struct fl_flow_mask *mask;
	struct cls_fl_filter *f;

	list_for_each_entry_rcu(mask, &head->masks, list) {
		flow_dissector_init_keys(&skb_key.control, &skb_key.basic);
		fl_clear_masked_range(&skb_key, mask);

		skb_flow_dissect_meta(skb, &mask->dissector, &skb_key);
		/* skb_flow_dissect() does not set n_proto in case an unknown
		 * protocol, so do it rather here.
		 */
		skb_key.basic.n_proto = skb_protocol(skb, false);
		skb_flow_dissect_tunnel_info(skb, &mask->dissector, &skb_key);
		skb_flow_dissect_ct(skb, &mask->dissector, &skb_key,
				    fl_ct_info_to_flower_map,
				    ARRAY_SIZE(fl_ct_info_to_flower_map),
				    post_ct, zone);
		skb_flow_dissect_hash(skb, &mask->dissector, &skb_key);
		skb_flow_dissect(skb, &mask->dissector, &skb_key,
				 FLOW_DISSECTOR_F_STOP_BEFORE_ENCAP);

		f = fl_mask_lookup(mask, &skb_key);
		if (f && !tc_skip_sw(f->flags)) {
			*res = f->res;
			return tcf_exts_exec(skb, &f->exts, res);
		}
	}
	return -1;
}

static int fl_init(struct tcf_proto *tp)
{
	struct cls_fl_head *head;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (!head)
		return -ENOBUFS;

	spin_lock_init(&head->masks_lock);
	INIT_LIST_HEAD_RCU(&head->masks);
	INIT_LIST_HEAD(&head->hw_filters);
	rcu_assign_pointer(tp->root, head);
	idr_init(&head->handle_idr);

	return rhashtable_init(&head->ht, &mask_ht_params);
}

static void fl_mask_free(struct fl_flow_mask *mask, bool mask_init_done)
{
	/* temporary masks don't have their filters list and ht initialized */
	if (mask_init_done) {
		WARN_ON(!list_empty(&mask->filters));
		rhashtable_destroy(&mask->ht);
	}
	kfree(mask);
}

static void fl_mask_free_work(struct work_struct *work)
{
	struct fl_flow_mask *mask = container_of(to_rcu_work(work),
						 struct fl_flow_mask, rwork);

	fl_mask_free(mask, true);
}

static void fl_uninit_mask_free_work(struct work_struct *work)
{
	struct fl_flow_mask *mask = container_of(to_rcu_work(work),
						 struct fl_flow_mask, rwork);

	fl_mask_free(mask, false);
}

static bool fl_mask_put(struct cls_fl_head *head, struct fl_flow_mask *mask)
{
	if (!refcount_dec_and_test(&mask->refcnt))
		return false;

	rhashtable_remove_fast(&head->ht, &mask->ht_node, mask_ht_params);

	spin_lock(&head->masks_lock);
	list_del_rcu(&mask->list);
	spin_unlock(&head->masks_lock);

	tcf_queue_work(&mask->rwork, fl_mask_free_work);

	return true;
}

static struct cls_fl_head *fl_head_dereference(struct tcf_proto *tp)
{
	/* Flower classifier only changes root pointer during init and destroy.
	 * Users must obtain reference to tcf_proto instance before calling its
	 * API, so tp->root pointer is protected from concurrent call to
	 * fl_destroy() by reference counting.
	 */
	return rcu_dereference_raw(tp->root);
}

static void __fl_destroy_filter(struct cls_fl_filter *f)
{
	tcf_exts_destroy(&f->exts);
	tcf_exts_put_net(&f->exts);
	kfree(f);
}

static void fl_destroy_filter_work(struct work_struct *work)
{
	struct cls_fl_filter *f = container_of(to_rcu_work(work),
					struct cls_fl_filter, rwork);

	__fl_destroy_filter(f);
}

static void fl_hw_destroy_filter(struct tcf_proto *tp, struct cls_fl_filter *f,
				 bool rtnl_held, struct netlink_ext_ack *extack)
{
	struct tcf_block *block = tp->chain->block;
	struct flow_cls_offload cls_flower = {};

	tc_cls_common_offload_init(&cls_flower.common, tp, f->flags, extack);
	cls_flower.command = FLOW_CLS_DESTROY;
	cls_flower.cookie = (unsigned long) f;

	tc_setup_cb_destroy(block, tp, TC_SETUP_CLSFLOWER, &cls_flower, false,
			    &f->flags, &f->in_hw_count, rtnl_held);

}

static int fl_hw_replace_filter(struct tcf_proto *tp,
				struct cls_fl_filter *f, bool rtnl_held,
				struct netlink_ext_ack *extack)
{
	struct tcf_block *block = tp->chain->block;
	struct flow_cls_offload cls_flower = {};
	bool skip_sw = tc_skip_sw(f->flags);
	int err = 0;

	cls_flower.rule = flow_rule_alloc(tcf_exts_num_actions(&f->exts));
	if (!cls_flower.rule)
		return -ENOMEM;

	tc_cls_common_offload_init(&cls_flower.common, tp, f->flags, extack);
	cls_flower.command = FLOW_CLS_REPLACE;
	cls_flower.cookie = (unsigned long) f;
	cls_flower.rule->match.dissector = &f->mask->dissector;
	cls_flower.rule->match.mask = &f->mask->key;
	cls_flower.rule->match.key = &f->mkey;
	cls_flower.classid = f->res.classid;

	err = tc_setup_offload_action(&cls_flower.rule->action, &f->exts,
				      cls_flower.common.extack);
	if (err) {
		kfree(cls_flower.rule);

		return skip_sw ? err : 0;
	}

	err = tc_setup_cb_add(block, tp, TC_SETUP_CLSFLOWER, &cls_flower,
			      skip_sw, &f->flags, &f->in_hw_count, rtnl_held);
	tc_cleanup_offload_action(&cls_flower.rule->action);
	kfree(cls_flower.rule);

	if (err) {
		fl_hw_destroy_filter(tp, f, rtnl_held, NULL);
		return err;
	}

	if (skip_sw && !(f->flags & TCA_CLS_FLAGS_IN_HW))
		return -EINVAL;

	return 0;
}

static void fl_hw_update_stats(struct tcf_proto *tp, struct cls_fl_filter *f,
			       bool rtnl_held)
{
	struct tcf_block *block = tp->chain->block;
	struct flow_cls_offload cls_flower = {};

	tc_cls_common_offload_init(&cls_flower.common, tp, f->flags, NULL);
	cls_flower.command = FLOW_CLS_STATS;
	cls_flower.cookie = (unsigned long) f;
	cls_flower.classid = f->res.classid;

	tc_setup_cb_call(block, TC_SETUP_CLSFLOWER, &cls_flower, false,
			 rtnl_held);

	tcf_exts_hw_stats_update(&f->exts, cls_flower.stats.bytes,
				 cls_flower.stats.pkts,
				 cls_flower.stats.drops,
				 cls_flower.stats.lastused,
				 cls_flower.stats.used_hw_stats,
				 cls_flower.stats.used_hw_stats_valid);
}

static void __fl_put(struct cls_fl_filter *f)
{
	if (!refcount_dec_and_test(&f->refcnt))
		return;

	if (tcf_exts_get_net(&f->exts))
		tcf_queue_work(&f->rwork, fl_destroy_filter_work);
	else
		__fl_destroy_filter(f);
}

static struct cls_fl_filter *__fl_get(struct cls_fl_head *head, u32 handle)
{
	struct cls_fl_filter *f;

	rcu_read_lock();
	f = idr_find(&head->handle_idr, handle);
	if (f && !refcount_inc_not_zero(&f->refcnt))
		f = NULL;
	rcu_read_unlock();

	return f;
}

static int __fl_delete(struct tcf_proto *tp, struct cls_fl_filter *f,
		       bool *last, bool rtnl_held,
		       struct netlink_ext_ack *extack)
{
	struct cls_fl_head *head = fl_head_dereference(tp);

	*last = false;

	spin_lock(&tp->lock);
	if (f->deleted) {
		spin_unlock(&tp->lock);
		return -ENOENT;
	}

	f->deleted = true;
	rhashtable_remove_fast(&f->mask->ht, &f->ht_node,
			       f->mask->filter_ht_params);
	idr_remove(&head->handle_idr, f->handle);
	list_del_rcu(&f->list);
	spin_unlock(&tp->lock);

	*last = fl_mask_put(head, f->mask);
	if (!tc_skip_hw(f->flags))
		fl_hw_destroy_filter(tp, f, rtnl_held, extack);
	tcf_unbind_filter(tp, &f->res);
	__fl_put(f);

	return 0;
}

static void fl_destroy_sleepable(struct work_struct *work)
{
	struct cls_fl_head *head = container_of(to_rcu_work(work),
						struct cls_fl_head,
						rwork);

	rhashtable_destroy(&head->ht);
	kfree(head);
	module_put(THIS_MODULE);
}

static void fl_destroy(struct tcf_proto *tp, bool rtnl_held,
		       struct netlink_ext_ack *extack)
{
	struct cls_fl_head *head = fl_head_dereference(tp);
	struct fl_flow_mask *mask, *next_mask;
	struct cls_fl_filter *f, *next;
	bool last;

	list_for_each_entry_safe(mask, next_mask, &head->masks, list) {
		list_for_each_entry_safe(f, next, &mask->filters, list) {
			__fl_delete(tp, f, &last, rtnl_held, extack);
			if (last)
				break;
		}
	}
	idr_destroy(&head->handle_idr);

	__module_get(THIS_MODULE);
	tcf_queue_work(&head->rwork, fl_destroy_sleepable);
}

static void fl_put(struct tcf_proto *tp, void *arg)
{
	struct cls_fl_filter *f = arg;

	__fl_put(f);
}

static void *fl_get(struct tcf_proto *tp, u32 handle)
{
	struct cls_fl_head *head = fl_head_dereference(tp);

	return __fl_get(head, handle);
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
	[TCA_FLOWER_KEY_MPLS_OPTS]	= { .type = NLA_NESTED },
	[TCA_FLOWER_KEY_TCP_FLAGS]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_TCP_FLAGS_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_IP_TOS]		= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_IP_TOS_MASK]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_IP_TTL]		= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_IP_TTL_MASK]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_CVLAN_ID]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_CVLAN_PRIO]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_CVLAN_ETH_TYPE]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_ENC_IP_TOS]	= { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ENC_IP_TOS_MASK] = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ENC_IP_TTL]	 = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ENC_IP_TTL_MASK] = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ENC_OPTS]	= { .type = NLA_NESTED },
	[TCA_FLOWER_KEY_ENC_OPTS_MASK]	= { .type = NLA_NESTED },
	[TCA_FLOWER_KEY_CT_STATE]	=
		NLA_POLICY_MASK(NLA_U16, TCA_FLOWER_KEY_CT_FLAGS_MASK),
	[TCA_FLOWER_KEY_CT_STATE_MASK]	=
		NLA_POLICY_MASK(NLA_U16, TCA_FLOWER_KEY_CT_FLAGS_MASK),
	[TCA_FLOWER_KEY_CT_ZONE]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_CT_ZONE_MASK]	= { .type = NLA_U16 },
	[TCA_FLOWER_KEY_CT_MARK]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_CT_MARK_MASK]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_CT_LABELS]	= { .type = NLA_BINARY,
					    .len = 128 / BITS_PER_BYTE },
	[TCA_FLOWER_KEY_CT_LABELS_MASK]	= { .type = NLA_BINARY,
					    .len = 128 / BITS_PER_BYTE },
	[TCA_FLOWER_FLAGS]		= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_HASH]		= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_HASH_MASK]	= { .type = NLA_U32 },
	[TCA_FLOWER_KEY_NUM_OF_VLANS]	= { .type = NLA_U8 },

};

static const struct nla_policy
enc_opts_policy[TCA_FLOWER_KEY_ENC_OPTS_MAX + 1] = {
	[TCA_FLOWER_KEY_ENC_OPTS_UNSPEC]        = {
		.strict_start_type = TCA_FLOWER_KEY_ENC_OPTS_VXLAN },
	[TCA_FLOWER_KEY_ENC_OPTS_GENEVE]        = { .type = NLA_NESTED },
	[TCA_FLOWER_KEY_ENC_OPTS_VXLAN]         = { .type = NLA_NESTED },
	[TCA_FLOWER_KEY_ENC_OPTS_ERSPAN]        = { .type = NLA_NESTED },
	[TCA_FLOWER_KEY_ENC_OPTS_GTP]		= { .type = NLA_NESTED },
};

static const struct nla_policy
geneve_opt_policy[TCA_FLOWER_KEY_ENC_OPT_GENEVE_MAX + 1] = {
	[TCA_FLOWER_KEY_ENC_OPT_GENEVE_CLASS]      = { .type = NLA_U16 },
	[TCA_FLOWER_KEY_ENC_OPT_GENEVE_TYPE]       = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ENC_OPT_GENEVE_DATA]       = { .type = NLA_BINARY,
						       .len = 128 },
};

static const struct nla_policy
vxlan_opt_policy[TCA_FLOWER_KEY_ENC_OPT_VXLAN_MAX + 1] = {
	[TCA_FLOWER_KEY_ENC_OPT_VXLAN_GBP]         = { .type = NLA_U32 },
};

static const struct nla_policy
erspan_opt_policy[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_MAX + 1] = {
	[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_VER]        = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_INDEX]      = { .type = NLA_U32 },
	[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_DIR]        = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_HWID]       = { .type = NLA_U8 },
};

static const struct nla_policy
gtp_opt_policy[TCA_FLOWER_KEY_ENC_OPT_GTP_MAX + 1] = {
	[TCA_FLOWER_KEY_ENC_OPT_GTP_PDU_TYPE]	   = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_ENC_OPT_GTP_QFI]	   = { .type = NLA_U8 },
};

static const struct nla_policy
mpls_stack_entry_policy[TCA_FLOWER_KEY_MPLS_OPT_LSE_MAX + 1] = {
	[TCA_FLOWER_KEY_MPLS_OPT_LSE_DEPTH]    = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_MPLS_OPT_LSE_TTL]      = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_MPLS_OPT_LSE_BOS]      = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_MPLS_OPT_LSE_TC]       = { .type = NLA_U8 },
	[TCA_FLOWER_KEY_MPLS_OPT_LSE_LABEL]    = { .type = NLA_U32 },
};

static void fl_set_key_val(struct nlattr **tb,
			   void *val, int val_type,
			   void *mask, int mask_type, int len)
{
	if (!tb[val_type])
		return;
	nla_memcpy(val, tb[val_type], len);
	if (mask_type == TCA_FLOWER_UNSPEC || !tb[mask_type])
		memset(mask, 0xff, len);
	else
		nla_memcpy(mask, tb[mask_type], len);
}

static int fl_set_key_port_range(struct nlattr **tb, struct fl_flow_key *key,
				 struct fl_flow_key *mask,
				 struct netlink_ext_ack *extack)
{
	fl_set_key_val(tb, &key->tp_range.tp_min.dst,
		       TCA_FLOWER_KEY_PORT_DST_MIN, &mask->tp_range.tp_min.dst,
		       TCA_FLOWER_UNSPEC, sizeof(key->tp_range.tp_min.dst));
	fl_set_key_val(tb, &key->tp_range.tp_max.dst,
		       TCA_FLOWER_KEY_PORT_DST_MAX, &mask->tp_range.tp_max.dst,
		       TCA_FLOWER_UNSPEC, sizeof(key->tp_range.tp_max.dst));
	fl_set_key_val(tb, &key->tp_range.tp_min.src,
		       TCA_FLOWER_KEY_PORT_SRC_MIN, &mask->tp_range.tp_min.src,
		       TCA_FLOWER_UNSPEC, sizeof(key->tp_range.tp_min.src));
	fl_set_key_val(tb, &key->tp_range.tp_max.src,
		       TCA_FLOWER_KEY_PORT_SRC_MAX, &mask->tp_range.tp_max.src,
		       TCA_FLOWER_UNSPEC, sizeof(key->tp_range.tp_max.src));

	if (mask->tp_range.tp_min.dst && mask->tp_range.tp_max.dst &&
	    ntohs(key->tp_range.tp_max.dst) <=
	    ntohs(key->tp_range.tp_min.dst)) {
		NL_SET_ERR_MSG_ATTR(extack,
				    tb[TCA_FLOWER_KEY_PORT_DST_MIN],
				    "Invalid destination port range (min must be strictly smaller than max)");
		return -EINVAL;
	}
	if (mask->tp_range.tp_min.src && mask->tp_range.tp_max.src &&
	    ntohs(key->tp_range.tp_max.src) <=
	    ntohs(key->tp_range.tp_min.src)) {
		NL_SET_ERR_MSG_ATTR(extack,
				    tb[TCA_FLOWER_KEY_PORT_SRC_MIN],
				    "Invalid source port range (min must be strictly smaller than max)");
		return -EINVAL;
	}

	return 0;
}

static int fl_set_key_mpls_lse(const struct nlattr *nla_lse,
			       struct flow_dissector_key_mpls *key_val,
			       struct flow_dissector_key_mpls *key_mask,
			       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_MAX + 1];
	struct flow_dissector_mpls_lse *lse_mask;
	struct flow_dissector_mpls_lse *lse_val;
	u8 lse_index;
	u8 depth;
	int err;

	err = nla_parse_nested(tb, TCA_FLOWER_KEY_MPLS_OPT_LSE_MAX, nla_lse,
			       mpls_stack_entry_policy, extack);
	if (err < 0)
		return err;

	if (!tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_DEPTH]) {
		NL_SET_ERR_MSG(extack, "Missing MPLS option \"depth\"");
		return -EINVAL;
	}

	depth = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_DEPTH]);

	/* LSE depth starts at 1, for consistency with terminology used by
	 * RFC 3031 (section 3.9), where depth 0 refers to unlabeled packets.
	 */
	if (depth < 1 || depth > FLOW_DIS_MPLS_MAX) {
		NL_SET_ERR_MSG_ATTR(extack,
				    tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_DEPTH],
				    "Invalid MPLS depth");
		return -EINVAL;
	}
	lse_index = depth - 1;

	dissector_set_mpls_lse(key_val, lse_index);
	dissector_set_mpls_lse(key_mask, lse_index);

	lse_val = &key_val->ls[lse_index];
	lse_mask = &key_mask->ls[lse_index];

	if (tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_TTL]) {
		lse_val->mpls_ttl = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_TTL]);
		lse_mask->mpls_ttl = MPLS_TTL_MASK;
	}
	if (tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_BOS]) {
		u8 bos = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_BOS]);

		if (bos & ~MPLS_BOS_MASK) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_BOS],
					    "Bottom Of Stack (BOS) must be 0 or 1");
			return -EINVAL;
		}
		lse_val->mpls_bos = bos;
		lse_mask->mpls_bos = MPLS_BOS_MASK;
	}
	if (tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_TC]) {
		u8 tc = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_TC]);

		if (tc & ~MPLS_TC_MASK) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_TC],
					    "Traffic Class (TC) must be between 0 and 7");
			return -EINVAL;
		}
		lse_val->mpls_tc = tc;
		lse_mask->mpls_tc = MPLS_TC_MASK;
	}
	if (tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_LABEL]) {
		u32 label = nla_get_u32(tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_LABEL]);

		if (label & ~MPLS_LABEL_MASK) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[TCA_FLOWER_KEY_MPLS_OPT_LSE_LABEL],
					    "Label must be between 0 and 1048575");
			return -EINVAL;
		}
		lse_val->mpls_label = label;
		lse_mask->mpls_label = MPLS_LABEL_MASK;
	}

	return 0;
}

static int fl_set_key_mpls_opts(const struct nlattr *nla_mpls_opts,
				struct flow_dissector_key_mpls *key_val,
				struct flow_dissector_key_mpls *key_mask,
				struct netlink_ext_ack *extack)
{
	struct nlattr *nla_lse;
	int rem;
	int err;

	if (!(nla_mpls_opts->nla_type & NLA_F_NESTED)) {
		NL_SET_ERR_MSG_ATTR(extack, nla_mpls_opts,
				    "NLA_F_NESTED is missing");
		return -EINVAL;
	}

	nla_for_each_nested(nla_lse, nla_mpls_opts, rem) {
		if (nla_type(nla_lse) != TCA_FLOWER_KEY_MPLS_OPTS_LSE) {
			NL_SET_ERR_MSG_ATTR(extack, nla_lse,
					    "Invalid MPLS option type");
			return -EINVAL;
		}

		err = fl_set_key_mpls_lse(nla_lse, key_val, key_mask, extack);
		if (err < 0)
			return err;
	}
	if (rem) {
		NL_SET_ERR_MSG(extack,
			       "Bytes leftover after parsing MPLS options");
		return -EINVAL;
	}

	return 0;
}

static int fl_set_key_mpls(struct nlattr **tb,
			   struct flow_dissector_key_mpls *key_val,
			   struct flow_dissector_key_mpls *key_mask,
			   struct netlink_ext_ack *extack)
{
	struct flow_dissector_mpls_lse *lse_mask;
	struct flow_dissector_mpls_lse *lse_val;

	if (tb[TCA_FLOWER_KEY_MPLS_OPTS]) {
		if (tb[TCA_FLOWER_KEY_MPLS_TTL] ||
		    tb[TCA_FLOWER_KEY_MPLS_BOS] ||
		    tb[TCA_FLOWER_KEY_MPLS_TC] ||
		    tb[TCA_FLOWER_KEY_MPLS_LABEL]) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[TCA_FLOWER_KEY_MPLS_OPTS],
					    "MPLS label, Traffic Class, Bottom Of Stack and Time To Live must be encapsulated in the MPLS options attribute");
			return -EBADMSG;
		}

		return fl_set_key_mpls_opts(tb[TCA_FLOWER_KEY_MPLS_OPTS],
					    key_val, key_mask, extack);
	}

	lse_val = &key_val->ls[0];
	lse_mask = &key_mask->ls[0];

	if (tb[TCA_FLOWER_KEY_MPLS_TTL]) {
		lse_val->mpls_ttl = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_TTL]);
		lse_mask->mpls_ttl = MPLS_TTL_MASK;
		dissector_set_mpls_lse(key_val, 0);
		dissector_set_mpls_lse(key_mask, 0);
	}
	if (tb[TCA_FLOWER_KEY_MPLS_BOS]) {
		u8 bos = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_BOS]);

		if (bos & ~MPLS_BOS_MASK) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[TCA_FLOWER_KEY_MPLS_BOS],
					    "Bottom Of Stack (BOS) must be 0 or 1");
			return -EINVAL;
		}
		lse_val->mpls_bos = bos;
		lse_mask->mpls_bos = MPLS_BOS_MASK;
		dissector_set_mpls_lse(key_val, 0);
		dissector_set_mpls_lse(key_mask, 0);
	}
	if (tb[TCA_FLOWER_KEY_MPLS_TC]) {
		u8 tc = nla_get_u8(tb[TCA_FLOWER_KEY_MPLS_TC]);

		if (tc & ~MPLS_TC_MASK) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[TCA_FLOWER_KEY_MPLS_TC],
					    "Traffic Class (TC) must be between 0 and 7");
			return -EINVAL;
		}
		lse_val->mpls_tc = tc;
		lse_mask->mpls_tc = MPLS_TC_MASK;
		dissector_set_mpls_lse(key_val, 0);
		dissector_set_mpls_lse(key_mask, 0);
	}
	if (tb[TCA_FLOWER_KEY_MPLS_LABEL]) {
		u32 label = nla_get_u32(tb[TCA_FLOWER_KEY_MPLS_LABEL]);

		if (label & ~MPLS_LABEL_MASK) {
			NL_SET_ERR_MSG_ATTR(extack,
					    tb[TCA_FLOWER_KEY_MPLS_LABEL],
					    "Label must be between 0 and 1048575");
			return -EINVAL;
		}
		lse_val->mpls_label = label;
		lse_mask->mpls_label = MPLS_LABEL_MASK;
		dissector_set_mpls_lse(key_val, 0);
		dissector_set_mpls_lse(key_mask, 0);
	}
	return 0;
}

static void fl_set_key_vlan(struct nlattr **tb,
			    __be16 ethertype,
			    int vlan_id_key, int vlan_prio_key,
			    int vlan_next_eth_type_key,
			    struct flow_dissector_key_vlan *key_val,
			    struct flow_dissector_key_vlan *key_mask)
{
#define VLAN_PRIORITY_MASK	0x7

	if (tb[vlan_id_key]) {
		key_val->vlan_id =
			nla_get_u16(tb[vlan_id_key]) & VLAN_VID_MASK;
		key_mask->vlan_id = VLAN_VID_MASK;
	}
	if (tb[vlan_prio_key]) {
		key_val->vlan_priority =
			nla_get_u8(tb[vlan_prio_key]) &
			VLAN_PRIORITY_MASK;
		key_mask->vlan_priority = VLAN_PRIORITY_MASK;
	}
	if (ethertype) {
		key_val->vlan_tpid = ethertype;
		key_mask->vlan_tpid = cpu_to_be16(~0);
	}
	if (tb[vlan_next_eth_type_key]) {
		key_val->vlan_eth_type =
			nla_get_be16(tb[vlan_next_eth_type_key]);
		key_mask->vlan_eth_type = cpu_to_be16(~0);
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

static int fl_set_key_flags(struct nlattr **tb, u32 *flags_key,
			    u32 *flags_mask, struct netlink_ext_ack *extack)
{
	u32 key, mask;

	/* mask is mandatory for flags */
	if (!tb[TCA_FLOWER_KEY_FLAGS_MASK]) {
		NL_SET_ERR_MSG(extack, "Missing flags mask");
		return -EINVAL;
	}

	key = be32_to_cpu(nla_get_be32(tb[TCA_FLOWER_KEY_FLAGS]));
	mask = be32_to_cpu(nla_get_be32(tb[TCA_FLOWER_KEY_FLAGS_MASK]));

	*flags_key  = 0;
	*flags_mask = 0;

	fl_set_key_flag(key, mask, flags_key, flags_mask,
			TCA_FLOWER_KEY_FLAGS_IS_FRAGMENT, FLOW_DIS_IS_FRAGMENT);
	fl_set_key_flag(key, mask, flags_key, flags_mask,
			TCA_FLOWER_KEY_FLAGS_FRAG_IS_FIRST,
			FLOW_DIS_FIRST_FRAG);

	return 0;
}

static void fl_set_key_ip(struct nlattr **tb, bool encap,
			  struct flow_dissector_key_ip *key,
			  struct flow_dissector_key_ip *mask)
{
	int tos_key = encap ? TCA_FLOWER_KEY_ENC_IP_TOS : TCA_FLOWER_KEY_IP_TOS;
	int ttl_key = encap ? TCA_FLOWER_KEY_ENC_IP_TTL : TCA_FLOWER_KEY_IP_TTL;
	int tos_mask = encap ? TCA_FLOWER_KEY_ENC_IP_TOS_MASK : TCA_FLOWER_KEY_IP_TOS_MASK;
	int ttl_mask = encap ? TCA_FLOWER_KEY_ENC_IP_TTL_MASK : TCA_FLOWER_KEY_IP_TTL_MASK;

	fl_set_key_val(tb, &key->tos, tos_key, &mask->tos, tos_mask, sizeof(key->tos));
	fl_set_key_val(tb, &key->ttl, ttl_key, &mask->ttl, ttl_mask, sizeof(key->ttl));
}

static int fl_set_geneve_opt(const struct nlattr *nla, struct fl_flow_key *key,
			     int depth, int option_len,
			     struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_MAX + 1];
	struct nlattr *class = NULL, *type = NULL, *data = NULL;
	struct geneve_opt *opt;
	int err, data_len = 0;

	if (option_len > sizeof(struct geneve_opt))
		data_len = option_len - sizeof(struct geneve_opt);

	opt = (struct geneve_opt *)&key->enc_opts.data[key->enc_opts.len];
	memset(opt, 0xff, option_len);
	opt->length = data_len / 4;
	opt->r1 = 0;
	opt->r2 = 0;
	opt->r3 = 0;

	/* If no mask has been prodived we assume an exact match. */
	if (!depth)
		return sizeof(struct geneve_opt) + data_len;

	if (nla_type(nla) != TCA_FLOWER_KEY_ENC_OPTS_GENEVE) {
		NL_SET_ERR_MSG(extack, "Non-geneve option type for mask");
		return -EINVAL;
	}

	err = nla_parse_nested_deprecated(tb,
					  TCA_FLOWER_KEY_ENC_OPT_GENEVE_MAX,
					  nla, geneve_opt_policy, extack);
	if (err < 0)
		return err;

	/* We are not allowed to omit any of CLASS, TYPE or DATA
	 * fields from the key.
	 */
	if (!option_len &&
	    (!tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_CLASS] ||
	     !tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_TYPE] ||
	     !tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_DATA])) {
		NL_SET_ERR_MSG(extack, "Missing tunnel key geneve option class, type or data");
		return -EINVAL;
	}

	/* Omitting any of CLASS, TYPE or DATA fields is allowed
	 * for the mask.
	 */
	if (tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_DATA]) {
		int new_len = key->enc_opts.len;

		data = tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_DATA];
		data_len = nla_len(data);
		if (data_len < 4) {
			NL_SET_ERR_MSG(extack, "Tunnel key geneve option data is less than 4 bytes long");
			return -ERANGE;
		}
		if (data_len % 4) {
			NL_SET_ERR_MSG(extack, "Tunnel key geneve option data is not a multiple of 4 bytes long");
			return -ERANGE;
		}

		new_len += sizeof(struct geneve_opt) + data_len;
		BUILD_BUG_ON(FLOW_DIS_TUN_OPTS_MAX != IP_TUNNEL_OPTS_MAX);
		if (new_len > FLOW_DIS_TUN_OPTS_MAX) {
			NL_SET_ERR_MSG(extack, "Tunnel options exceeds max size");
			return -ERANGE;
		}
		opt->length = data_len / 4;
		memcpy(opt->opt_data, nla_data(data), data_len);
	}

	if (tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_CLASS]) {
		class = tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_CLASS];
		opt->opt_class = nla_get_be16(class);
	}

	if (tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_TYPE]) {
		type = tb[TCA_FLOWER_KEY_ENC_OPT_GENEVE_TYPE];
		opt->type = nla_get_u8(type);
	}

	return sizeof(struct geneve_opt) + data_len;
}

static int fl_set_vxlan_opt(const struct nlattr *nla, struct fl_flow_key *key,
			    int depth, int option_len,
			    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_FLOWER_KEY_ENC_OPT_VXLAN_MAX + 1];
	struct vxlan_metadata *md;
	int err;

	md = (struct vxlan_metadata *)&key->enc_opts.data[key->enc_opts.len];
	memset(md, 0xff, sizeof(*md));

	if (!depth)
		return sizeof(*md);

	if (nla_type(nla) != TCA_FLOWER_KEY_ENC_OPTS_VXLAN) {
		NL_SET_ERR_MSG(extack, "Non-vxlan option type for mask");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, TCA_FLOWER_KEY_ENC_OPT_VXLAN_MAX, nla,
			       vxlan_opt_policy, extack);
	if (err < 0)
		return err;

	if (!option_len && !tb[TCA_FLOWER_KEY_ENC_OPT_VXLAN_GBP]) {
		NL_SET_ERR_MSG(extack, "Missing tunnel key vxlan option gbp");
		return -EINVAL;
	}

	if (tb[TCA_FLOWER_KEY_ENC_OPT_VXLAN_GBP]) {
		md->gbp = nla_get_u32(tb[TCA_FLOWER_KEY_ENC_OPT_VXLAN_GBP]);
		md->gbp &= VXLAN_GBP_MASK;
	}

	return sizeof(*md);
}

static int fl_set_erspan_opt(const struct nlattr *nla, struct fl_flow_key *key,
			     int depth, int option_len,
			     struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_MAX + 1];
	struct erspan_metadata *md;
	int err;

	md = (struct erspan_metadata *)&key->enc_opts.data[key->enc_opts.len];
	memset(md, 0xff, sizeof(*md));
	md->version = 1;

	if (!depth)
		return sizeof(*md);

	if (nla_type(nla) != TCA_FLOWER_KEY_ENC_OPTS_ERSPAN) {
		NL_SET_ERR_MSG(extack, "Non-erspan option type for mask");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, TCA_FLOWER_KEY_ENC_OPT_ERSPAN_MAX, nla,
			       erspan_opt_policy, extack);
	if (err < 0)
		return err;

	if (!option_len && !tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_VER]) {
		NL_SET_ERR_MSG(extack, "Missing tunnel key erspan option ver");
		return -EINVAL;
	}

	if (tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_VER])
		md->version = nla_get_u8(tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_VER]);

	if (md->version == 1) {
		if (!option_len && !tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_INDEX]) {
			NL_SET_ERR_MSG(extack, "Missing tunnel key erspan option index");
			return -EINVAL;
		}
		if (tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_INDEX]) {
			nla = tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_INDEX];
			memset(&md->u, 0x00, sizeof(md->u));
			md->u.index = nla_get_be32(nla);
		}
	} else if (md->version == 2) {
		if (!option_len && (!tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_DIR] ||
				    !tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_HWID])) {
			NL_SET_ERR_MSG(extack, "Missing tunnel key erspan option dir or hwid");
			return -EINVAL;
		}
		if (tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_DIR]) {
			nla = tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_DIR];
			md->u.md2.dir = nla_get_u8(nla);
		}
		if (tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_HWID]) {
			nla = tb[TCA_FLOWER_KEY_ENC_OPT_ERSPAN_HWID];
			set_hwid(&md->u.md2, nla_get_u8(nla));
		}
	} else {
		NL_SET_ERR_MSG(extack, "Tunnel key erspan option ver is incorrect");
		return -EINVAL;
	}

	return sizeof(*md);
}

static int fl_set_gtp_opt(const struct nlattr *nla, struct fl_flow_key *key,
			  int depth, int option_len,
			  struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_FLOWER_KEY_ENC_OPT_GTP_MAX + 1];
	struct gtp_pdu_session_info *sinfo;
	u8 len = key->enc_opts.len;
	int err;

	sinfo = (struct gtp_pdu_session_info *)&key->enc_opts.data[len];
	memset(sinfo, 0xff, option_len);

	if (!depth)
		return sizeof(*sinfo);

	if (nla_type(nla) != TCA_FLOWER_KEY_ENC_OPTS_GTP) {
		NL_SET_ERR_MSG_MOD(extack, "Non-gtp option type for mask");
		return -EINVAL;
	}

	err = nla_parse_nested(tb, TCA_FLOWER_KEY_ENC_OPT_GTP_MAX, nla,
			       gtp_opt_policy, extack);
	if (err < 0)
		return err;

	if (!option_len &&
	    (!tb[TCA_FLOWER_KEY_ENC_OPT_GTP_PDU_TYPE] ||
	     !tb[TCA_FLOWER_KEY_ENC_OPT_GTP_QFI])) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Missing tunnel key gtp option pdu type or qfi");
		return -EINVAL;
	}

	if (tb[TCA_FLOWER_KEY_ENC_OPT_GTP_PDU_TYPE])
		sinfo->pdu_type =
			nla_get_u8(tb[TCA_FLOWER_KEY_ENC_OPT_GTP_PDU_TYPE]);

	if (tb[TCA_FLOWER_KEY_ENC_OPT_GTP_QFI])
		sinfo->qfi = nla_get_u8(tb[TCA_FLOWER_KEY_ENC_OPT_GTP_QFI]);

	return sizeof(*sinfo);
}

static int fl_set_enc_opt(struct nlattr **tb, struct fl_flow_key *key,
			  struct fl_flow_key *mask,
			  struct netlink_ext_ack *extack)
{
	const struct nlattr *nla_enc_key, *nla_opt_key, *nla_opt_msk = NULL;
	int err, option_len, key_depth, msk_depth = 0;

	err = nla_validate_nested_deprecated(tb[TCA_FLOWER_KEY_ENC_OPTS],
					     TCA_FLOWER_KEY_ENC_OPTS_MAX,
					     enc_opts_policy, extack);
	if (err)
		return err;

	nla_enc_key = nla_data(tb[TCA_FLOWER_KEY_ENC_OPTS]);

	if (tb[TCA_FLOWER_KEY_ENC_OPTS_MASK]) {
		err = nla_validate_nested_deprecated(tb[TCA_FLOWER_KEY_ENC_OPTS_MASK],
						     TCA_FLOWER_KEY_ENC_OPTS_MAX,
						     enc_opts_policy, extack);
		if (err)
			return err;

		nla_opt_msk = nla_data(tb[TCA_FLOWER_KEY_ENC_OPTS_MASK]);
		msk_depth = nla_len(tb[TCA_FLOWER_KEY_ENC_OPTS_MASK]);
		if (!nla_ok(nla_opt_msk, msk_depth)) {
			NL_SET_ERR_MSG(extack, "Invalid nested attribute for masks");
			return -EINVAL;
		}
	}

	nla_for_each_attr(nla_opt_key, nla_enc_key,
			  nla_len(tb[TCA_FLOWER_KEY_ENC_OPTS]), key_depth) {
		switch (nla_type(nla_opt_key)) {
		case TCA_FLOWER_KEY_ENC_OPTS_GENEVE:
			if (key->enc_opts.dst_opt_type &&
			    key->enc_opts.dst_opt_type != TUNNEL_GENEVE_OPT) {
				NL_SET_ERR_MSG(extack, "Duplicate type for geneve options");
				return -EINVAL;
			}
			option_len = 0;
			key->enc_opts.dst_opt_type = TUNNEL_GENEVE_OPT;
			option_len = fl_set_geneve_opt(nla_opt_key, key,
						       key_depth, option_len,
						       extack);
			if (option_len < 0)
				return option_len;

			key->enc_opts.len += option_len;
			/* At the same time we need to parse through the mask
			 * in order to verify exact and mask attribute lengths.
			 */
			mask->enc_opts.dst_opt_type = TUNNEL_GENEVE_OPT;
			option_len = fl_set_geneve_opt(nla_opt_msk, mask,
						       msk_depth, option_len,
						       extack);
			if (option_len < 0)
				return option_len;

			mask->enc_opts.len += option_len;
			if (key->enc_opts.len != mask->enc_opts.len) {
				NL_SET_ERR_MSG(extack, "Key and mask miss aligned");
				return -EINVAL;
			}
			break;
		case TCA_FLOWER_KEY_ENC_OPTS_VXLAN:
			if (key->enc_opts.dst_opt_type) {
				NL_SET_ERR_MSG(extack, "Duplicate type for vxlan options");
				return -EINVAL;
			}
			option_len = 0;
			key->enc_opts.dst_opt_type = TUNNEL_VXLAN_OPT;
			option_len = fl_set_vxlan_opt(nla_opt_key, key,
						      key_depth, option_len,
						      extack);
			if (option_len < 0)
				return option_len;

			key->enc_opts.len += option_len;
			/* At the same time we need to parse through the mask
			 * in order to verify exact and mask attribute lengths.
			 */
			mask->enc_opts.dst_opt_type = TUNNEL_VXLAN_OPT;
			option_len = fl_set_vxlan_opt(nla_opt_msk, mask,
						      msk_depth, option_len,
						      extack);
			if (option_len < 0)
				return option_len;

			mask->enc_opts.len += option_len;
			if (key->enc_opts.len != mask->enc_opts.len) {
				NL_SET_ERR_MSG(extack, "Key and mask miss aligned");
				return -EINVAL;
			}
			break;
		case TCA_FLOWER_KEY_ENC_OPTS_ERSPAN:
			if (key->enc_opts.dst_opt_type) {
				NL_SET_ERR_MSG(extack, "Duplicate type for erspan options");
				return -EINVAL;
			}
			option_len = 0;
			key->enc_opts.dst_opt_type = TUNNEL_ERSPAN_OPT;
			option_len = fl_set_erspan_opt(nla_opt_key, key,
						       key_depth, option_len,
						       extack);
			if (option_len < 0)
				return option_len;

			key->enc_opts.len += option_len;
			/* At the same time we need to parse through the mask
			 * in order to verify exact and mask attribute lengths.
			 */
			mask->enc_opts.dst_opt_type = TUNNEL_ERSPAN_OPT;
			option_len = fl_set_erspan_opt(nla_opt_msk, mask,
						       msk_depth, option_len,
						       extack);
			if (option_len < 0)
				return option_len;

			mask->enc_opts.len += option_len;
			if (key->enc_opts.len != mask->enc_opts.len) {
				NL_SET_ERR_MSG(extack, "Key and mask miss aligned");
				return -EINVAL;
			}
			break;
		case TCA_FLOWER_KEY_ENC_OPTS_GTP:
			if (key->enc_opts.dst_opt_type) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Duplicate type for gtp options");
				return -EINVAL;
			}
			option_len = 0;
			key->enc_opts.dst_opt_type = TUNNEL_GTP_OPT;
			option_len = fl_set_gtp_opt(nla_opt_key, key,
						    key_depth, option_len,
						    extack);
			if (option_len < 0)
				return option_len;

			key->enc_opts.len += option_len;
			/* At the same time we need to parse through the mask
			 * in order to verify exact and mask attribute lengths.
			 */
			mask->enc_opts.dst_opt_type = TUNNEL_GTP_OPT;
			option_len = fl_set_gtp_opt(nla_opt_msk, mask,
						    msk_depth, option_len,
						    extack);
			if (option_len < 0)
				return option_len;

			mask->enc_opts.len += option_len;
			if (key->enc_opts.len != mask->enc_opts.len) {
				NL_SET_ERR_MSG_MOD(extack,
						   "Key and mask miss aligned");
				return -EINVAL;
			}
			break;
		default:
			NL_SET_ERR_MSG(extack, "Unknown tunnel option type");
			return -EINVAL;
		}

		if (!msk_depth)
			continue;

		if (!nla_ok(nla_opt_msk, msk_depth)) {
			NL_SET_ERR_MSG(extack, "A mask attribute is invalid");
			return -EINVAL;
		}
		nla_opt_msk = nla_next(nla_opt_msk, &msk_depth);
	}

	return 0;
}

static int fl_validate_ct_state(u16 state, struct nlattr *tb,
				struct netlink_ext_ack *extack)
{
	if (state && !(state & TCA_FLOWER_KEY_CT_FLAGS_TRACKED)) {
		NL_SET_ERR_MSG_ATTR(extack, tb,
				    "no trk, so no other flag can be set");
		return -EINVAL;
	}

	if (state & TCA_FLOWER_KEY_CT_FLAGS_NEW &&
	    state & TCA_FLOWER_KEY_CT_FLAGS_ESTABLISHED) {
		NL_SET_ERR_MSG_ATTR(extack, tb,
				    "new and est are mutually exclusive");
		return -EINVAL;
	}

	if (state & TCA_FLOWER_KEY_CT_FLAGS_INVALID &&
	    state & ~(TCA_FLOWER_KEY_CT_FLAGS_TRACKED |
		      TCA_FLOWER_KEY_CT_FLAGS_INVALID)) {
		NL_SET_ERR_MSG_ATTR(extack, tb,
				    "when inv is set, only trk may be set");
		return -EINVAL;
	}

	if (state & TCA_FLOWER_KEY_CT_FLAGS_NEW &&
	    state & TCA_FLOWER_KEY_CT_FLAGS_REPLY) {
		NL_SET_ERR_MSG_ATTR(extack, tb,
				    "new and rpl are mutually exclusive");
		return -EINVAL;
	}

	return 0;
}

static int fl_set_key_ct(struct nlattr **tb,
			 struct flow_dissector_key_ct *key,
			 struct flow_dissector_key_ct *mask,
			 struct netlink_ext_ack *extack)
{
	if (tb[TCA_FLOWER_KEY_CT_STATE]) {
		int err;

		if (!IS_ENABLED(CONFIG_NF_CONNTRACK)) {
			NL_SET_ERR_MSG(extack, "Conntrack isn't enabled");
			return -EOPNOTSUPP;
		}
		fl_set_key_val(tb, &key->ct_state, TCA_FLOWER_KEY_CT_STATE,
			       &mask->ct_state, TCA_FLOWER_KEY_CT_STATE_MASK,
			       sizeof(key->ct_state));

		err = fl_validate_ct_state(key->ct_state & mask->ct_state,
					   tb[TCA_FLOWER_KEY_CT_STATE_MASK],
					   extack);
		if (err)
			return err;

	}
	if (tb[TCA_FLOWER_KEY_CT_ZONE]) {
		if (!IS_ENABLED(CONFIG_NF_CONNTRACK_ZONES)) {
			NL_SET_ERR_MSG(extack, "Conntrack zones isn't enabled");
			return -EOPNOTSUPP;
		}
		fl_set_key_val(tb, &key->ct_zone, TCA_FLOWER_KEY_CT_ZONE,
			       &mask->ct_zone, TCA_FLOWER_KEY_CT_ZONE_MASK,
			       sizeof(key->ct_zone));
	}
	if (tb[TCA_FLOWER_KEY_CT_MARK]) {
		if (!IS_ENABLED(CONFIG_NF_CONNTRACK_MARK)) {
			NL_SET_ERR_MSG(extack, "Conntrack mark isn't enabled");
			return -EOPNOTSUPP;
		}
		fl_set_key_val(tb, &key->ct_mark, TCA_FLOWER_KEY_CT_MARK,
			       &mask->ct_mark, TCA_FLOWER_KEY_CT_MARK_MASK,
			       sizeof(key->ct_mark));
	}
	if (tb[TCA_FLOWER_KEY_CT_LABELS]) {
		if (!IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS)) {
			NL_SET_ERR_MSG(extack, "Conntrack labels aren't enabled");
			return -EOPNOTSUPP;
		}
		fl_set_key_val(tb, key->ct_labels, TCA_FLOWER_KEY_CT_LABELS,
			       mask->ct_labels, TCA_FLOWER_KEY_CT_LABELS_MASK,
			       sizeof(key->ct_labels));
	}

	return 0;
}

static bool is_vlan_key(struct nlattr *tb, __be16 *ethertype,
			struct fl_flow_key *key, struct fl_flow_key *mask,
			int vthresh)
{
	const bool good_num_of_vlans = key->num_of_vlans.num_of_vlans > vthresh;

	if (!tb) {
		*ethertype = 0;
		return good_num_of_vlans;
	}

	*ethertype = nla_get_be16(tb);
	if (good_num_of_vlans || eth_type_vlan(*ethertype))
		return true;

	key->basic.n_proto = *ethertype;
	mask->basic.n_proto = cpu_to_be16(~0);
	return false;
}

static int fl_set_key(struct net *net, struct nlattr **tb,
		      struct fl_flow_key *key, struct fl_flow_key *mask,
		      struct netlink_ext_ack *extack)
{
	__be16 ethertype;
	int ret = 0;

	if (tb[TCA_FLOWER_INDEV]) {
		int err = tcf_change_indev(net, tb[TCA_FLOWER_INDEV], extack);
		if (err < 0)
			return err;
		key->meta.ingress_ifindex = err;
		mask->meta.ingress_ifindex = 0xffffffff;
	}

	fl_set_key_val(tb, key->eth.dst, TCA_FLOWER_KEY_ETH_DST,
		       mask->eth.dst, TCA_FLOWER_KEY_ETH_DST_MASK,
		       sizeof(key->eth.dst));
	fl_set_key_val(tb, key->eth.src, TCA_FLOWER_KEY_ETH_SRC,
		       mask->eth.src, TCA_FLOWER_KEY_ETH_SRC_MASK,
		       sizeof(key->eth.src));
	fl_set_key_val(tb, &key->num_of_vlans,
		       TCA_FLOWER_KEY_NUM_OF_VLANS,
		       &mask->num_of_vlans,
		       TCA_FLOWER_UNSPEC,
		       sizeof(key->num_of_vlans));

	if (is_vlan_key(tb[TCA_FLOWER_KEY_ETH_TYPE], &ethertype, key, mask, 0)) {
		fl_set_key_vlan(tb, ethertype, TCA_FLOWER_KEY_VLAN_ID,
				TCA_FLOWER_KEY_VLAN_PRIO,
				TCA_FLOWER_KEY_VLAN_ETH_TYPE,
				&key->vlan, &mask->vlan);

		if (is_vlan_key(tb[TCA_FLOWER_KEY_VLAN_ETH_TYPE],
				&ethertype, key, mask, 1)) {
			fl_set_key_vlan(tb, ethertype,
					TCA_FLOWER_KEY_CVLAN_ID,
					TCA_FLOWER_KEY_CVLAN_PRIO,
					TCA_FLOWER_KEY_CVLAN_ETH_TYPE,
					&key->cvlan, &mask->cvlan);
			fl_set_key_val(tb, &key->basic.n_proto,
				       TCA_FLOWER_KEY_CVLAN_ETH_TYPE,
				       &mask->basic.n_proto,
				       TCA_FLOWER_UNSPEC,
				       sizeof(key->basic.n_proto));
		}
	}

	if (key->basic.n_proto == htons(ETH_P_IP) ||
	    key->basic.n_proto == htons(ETH_P_IPV6)) {
		fl_set_key_val(tb, &key->basic.ip_proto, TCA_FLOWER_KEY_IP_PROTO,
			       &mask->basic.ip_proto, TCA_FLOWER_UNSPEC,
			       sizeof(key->basic.ip_proto));
		fl_set_key_ip(tb, false, &key->ip, &mask->ip);
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
		fl_set_key_val(tb, &key->tcp.flags, TCA_FLOWER_KEY_TCP_FLAGS,
			       &mask->tcp.flags, TCA_FLOWER_KEY_TCP_FLAGS_MASK,
			       sizeof(key->tcp.flags));
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
		ret = fl_set_key_mpls(tb, &key->mpls, &mask->mpls, extack);
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

	if (key->basic.ip_proto == IPPROTO_TCP ||
	    key->basic.ip_proto == IPPROTO_UDP ||
	    key->basic.ip_proto == IPPROTO_SCTP) {
		ret = fl_set_key_port_range(tb, key, mask, extack);
		if (ret)
			return ret;
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

	fl_set_key_ip(tb, true, &key->enc_ip, &mask->enc_ip);

	fl_set_key_val(tb, &key->hash.hash, TCA_FLOWER_KEY_HASH,
		       &mask->hash.hash, TCA_FLOWER_KEY_HASH_MASK,
		       sizeof(key->hash.hash));

	if (tb[TCA_FLOWER_KEY_ENC_OPTS]) {
		ret = fl_set_enc_opt(tb, key, mask, extack);
		if (ret)
			return ret;
	}

	ret = fl_set_key_ct(tb, &key->ct, &mask->ct, extack);
	if (ret)
		return ret;

	if (tb[TCA_FLOWER_KEY_FLAGS])
		ret = fl_set_key_flags(tb, &key->control.flags,
				       &mask->control.flags, extack);

	return ret;
}

static void fl_mask_copy(struct fl_flow_mask *dst,
			 struct fl_flow_mask *src)
{
	const void *psrc = fl_key_get_start(&src->key, src);
	void *pdst = fl_key_get_start(&dst->key, src);

	memcpy(pdst, psrc, fl_mask_range(src));
	dst->range = src->range;
}

static const struct rhashtable_params fl_ht_params = {
	.key_offset = offsetof(struct cls_fl_filter, mkey), /* base offset */
	.head_offset = offsetof(struct cls_fl_filter, ht_node),
	.automatic_shrinking = true,
};

static int fl_init_mask_hashtable(struct fl_flow_mask *mask)
{
	mask->filter_ht_params = fl_ht_params;
	mask->filter_ht_params.key_len = fl_mask_range(mask);
	mask->filter_ht_params.key_offset += mask->range.start;

	return rhashtable_init(&mask->ht, &mask->filter_ht_params);
}

#define FL_KEY_MEMBER_OFFSET(member) offsetof(struct fl_flow_key, member)
#define FL_KEY_MEMBER_SIZE(member) sizeof_field(struct fl_flow_key, member)

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

static void fl_init_dissector(struct flow_dissector *dissector,
			      struct fl_flow_key *mask)
{
	struct flow_dissector_key keys[FLOW_DISSECTOR_KEY_MAX];
	size_t cnt = 0;

	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_META, meta);
	FL_KEY_SET(keys, cnt, FLOW_DISSECTOR_KEY_CONTROL, control);
	FL_KEY_SET(keys, cnt, FLOW_DISSECTOR_KEY_BASIC, basic);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_ETH_ADDRS, eth);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_IPV4_ADDRS, ipv4);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_IPV6_ADDRS, ipv6);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_PORTS, tp);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_PORTS_RANGE, tp_range);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_IP, ip);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_TCP, tcp);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_ICMP, icmp);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_ARP, arp);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_MPLS, mpls);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_VLAN, vlan);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_CVLAN, cvlan);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_KEYID, enc_key_id);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS, enc_ipv4);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS, enc_ipv6);
	if (FL_KEY_IS_MASKED(mask, enc_ipv4) ||
	    FL_KEY_IS_MASKED(mask, enc_ipv6))
		FL_KEY_SET(keys, cnt, FLOW_DISSECTOR_KEY_ENC_CONTROL,
			   enc_control);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_PORTS, enc_tp);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_IP, enc_ip);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_ENC_OPTS, enc_opts);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_CT, ct);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_HASH, hash);
	FL_KEY_SET_IF_MASKED(mask, keys, cnt,
			     FLOW_DISSECTOR_KEY_NUM_OF_VLANS, num_of_vlans);

	skb_flow_dissector_init(dissector, keys, cnt);
}

static struct fl_flow_mask *fl_create_new_mask(struct cls_fl_head *head,
					       struct fl_flow_mask *mask)
{
	struct fl_flow_mask *newmask;
	int err;

	newmask = kzalloc(sizeof(*newmask), GFP_KERNEL);
	if (!newmask)
		return ERR_PTR(-ENOMEM);

	fl_mask_copy(newmask, mask);

	if ((newmask->key.tp_range.tp_min.dst &&
	     newmask->key.tp_range.tp_max.dst) ||
	    (newmask->key.tp_range.tp_min.src &&
	     newmask->key.tp_range.tp_max.src))
		newmask->flags |= TCA_FLOWER_MASK_FLAGS_RANGE;

	err = fl_init_mask_hashtable(newmask);
	if (err)
		goto errout_free;

	fl_init_dissector(&newmask->dissector, &newmask->key);

	INIT_LIST_HEAD_RCU(&newmask->filters);

	refcount_set(&newmask->refcnt, 1);
	err = rhashtable_replace_fast(&head->ht, &mask->ht_node,
				      &newmask->ht_node, mask_ht_params);
	if (err)
		goto errout_destroy;

	spin_lock(&head->masks_lock);
	list_add_tail_rcu(&newmask->list, &head->masks);
	spin_unlock(&head->masks_lock);

	return newmask;

errout_destroy:
	rhashtable_destroy(&newmask->ht);
errout_free:
	kfree(newmask);

	return ERR_PTR(err);
}

static int fl_check_assign_mask(struct cls_fl_head *head,
				struct cls_fl_filter *fnew,
				struct cls_fl_filter *fold,
				struct fl_flow_mask *mask)
{
	struct fl_flow_mask *newmask;
	int ret = 0;

	rcu_read_lock();

	/* Insert mask as temporary node to prevent concurrent creation of mask
	 * with same key. Any concurrent lookups with same key will return
	 * -EAGAIN because mask's refcnt is zero.
	 */
	fnew->mask = rhashtable_lookup_get_insert_fast(&head->ht,
						       &mask->ht_node,
						       mask_ht_params);
	if (!fnew->mask) {
		rcu_read_unlock();

		if (fold) {
			ret = -EINVAL;
			goto errout_cleanup;
		}

		newmask = fl_create_new_mask(head, mask);
		if (IS_ERR(newmask)) {
			ret = PTR_ERR(newmask);
			goto errout_cleanup;
		}

		fnew->mask = newmask;
		return 0;
	} else if (IS_ERR(fnew->mask)) {
		ret = PTR_ERR(fnew->mask);
	} else if (fold && fold->mask != fnew->mask) {
		ret = -EINVAL;
	} else if (!refcount_inc_not_zero(&fnew->mask->refcnt)) {
		/* Mask was deleted concurrently, try again */
		ret = -EAGAIN;
	}
	rcu_read_unlock();
	return ret;

errout_cleanup:
	rhashtable_remove_fast(&head->ht, &mask->ht_node,
			       mask_ht_params);
	return ret;
}

static int fl_set_parms(struct net *net, struct tcf_proto *tp,
			struct cls_fl_filter *f, struct fl_flow_mask *mask,
			unsigned long base, struct nlattr **tb,
			struct nlattr *est,
			struct fl_flow_tmplt *tmplt,
			u32 flags, u32 fl_flags,
			struct netlink_ext_ack *extack)
{
	int err;

	err = tcf_exts_validate_ex(net, tp, tb, est, &f->exts, flags,
				   fl_flags, extack);
	if (err < 0)
		return err;

	if (tb[TCA_FLOWER_CLASSID]) {
		f->res.classid = nla_get_u32(tb[TCA_FLOWER_CLASSID]);
		if (flags & TCA_ACT_FLAGS_NO_RTNL)
			rtnl_lock();
		tcf_bind_filter(tp, &f->res, base);
		if (flags & TCA_ACT_FLAGS_NO_RTNL)
			rtnl_unlock();
	}

	err = fl_set_key(net, tb, &f->key, &mask->key, extack);
	if (err)
		return err;

	fl_mask_update_range(mask);
	fl_set_masked_key(&f->mkey, &f->key, mask);

	if (!fl_mask_fits_tmplt(tmplt, mask)) {
		NL_SET_ERR_MSG_MOD(extack, "Mask does not fit the template");
		return -EINVAL;
	}

	return 0;
}

static int fl_ht_insert_unique(struct cls_fl_filter *fnew,
			       struct cls_fl_filter *fold,
			       bool *in_ht)
{
	struct fl_flow_mask *mask = fnew->mask;
	int err;

	err = rhashtable_lookup_insert_fast(&mask->ht,
					    &fnew->ht_node,
					    mask->filter_ht_params);
	if (err) {
		*in_ht = false;
		/* It is okay if filter with same key exists when
		 * overwriting.
		 */
		return fold && err == -EEXIST ? 0 : err;
	}

	*in_ht = true;
	return 0;
}

static int fl_change(struct net *net, struct sk_buff *in_skb,
		     struct tcf_proto *tp, unsigned long base,
		     u32 handle, struct nlattr **tca,
		     void **arg, u32 flags,
		     struct netlink_ext_ack *extack)
{
	struct cls_fl_head *head = fl_head_dereference(tp);
	bool rtnl_held = !(flags & TCA_ACT_FLAGS_NO_RTNL);
	struct cls_fl_filter *fold = *arg;
	struct cls_fl_filter *fnew;
	struct fl_flow_mask *mask;
	struct nlattr **tb;
	bool in_ht;
	int err;

	if (!tca[TCA_OPTIONS]) {
		err = -EINVAL;
		goto errout_fold;
	}

	mask = kzalloc(sizeof(struct fl_flow_mask), GFP_KERNEL);
	if (!mask) {
		err = -ENOBUFS;
		goto errout_fold;
	}

	tb = kcalloc(TCA_FLOWER_MAX + 1, sizeof(struct nlattr *), GFP_KERNEL);
	if (!tb) {
		err = -ENOBUFS;
		goto errout_mask_alloc;
	}

	err = nla_parse_nested_deprecated(tb, TCA_FLOWER_MAX,
					  tca[TCA_OPTIONS], fl_policy, NULL);
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
	INIT_LIST_HEAD(&fnew->hw_list);
	refcount_set(&fnew->refcnt, 1);

	err = tcf_exts_init(&fnew->exts, net, TCA_FLOWER_ACT, 0);
	if (err < 0)
		goto errout;

	if (tb[TCA_FLOWER_FLAGS]) {
		fnew->flags = nla_get_u32(tb[TCA_FLOWER_FLAGS]);

		if (!tc_flags_valid(fnew->flags)) {
			err = -EINVAL;
			goto errout;
		}
	}

	err = fl_set_parms(net, tp, fnew, mask, base, tb, tca[TCA_RATE],
			   tp->chain->tmplt_priv, flags, fnew->flags,
			   extack);
	if (err)
		goto errout;

	err = fl_check_assign_mask(head, fnew, fold, mask);
	if (err)
		goto errout;

	err = fl_ht_insert_unique(fnew, fold, &in_ht);
	if (err)
		goto errout_mask;

	if (!tc_skip_hw(fnew->flags)) {
		err = fl_hw_replace_filter(tp, fnew, rtnl_held, extack);
		if (err)
			goto errout_ht;
	}

	if (!tc_in_hw(fnew->flags))
		fnew->flags |= TCA_CLS_FLAGS_NOT_IN_HW;

	spin_lock(&tp->lock);

	/* tp was deleted concurrently. -EAGAIN will cause caller to lookup
	 * proto again or create new one, if necessary.
	 */
	if (tp->deleting) {
		err = -EAGAIN;
		goto errout_hw;
	}

	if (fold) {
		/* Fold filter was deleted concurrently. Retry lookup. */
		if (fold->deleted) {
			err = -EAGAIN;
			goto errout_hw;
		}

		fnew->handle = handle;

		if (!in_ht) {
			struct rhashtable_params params =
				fnew->mask->filter_ht_params;

			err = rhashtable_insert_fast(&fnew->mask->ht,
						     &fnew->ht_node,
						     params);
			if (err)
				goto errout_hw;
			in_ht = true;
		}

		refcount_inc(&fnew->refcnt);
		rhashtable_remove_fast(&fold->mask->ht,
				       &fold->ht_node,
				       fold->mask->filter_ht_params);
		idr_replace(&head->handle_idr, fnew, fnew->handle);
		list_replace_rcu(&fold->list, &fnew->list);
		fold->deleted = true;

		spin_unlock(&tp->lock);

		fl_mask_put(head, fold->mask);
		if (!tc_skip_hw(fold->flags))
			fl_hw_destroy_filter(tp, fold, rtnl_held, NULL);
		tcf_unbind_filter(tp, &fold->res);
		/* Caller holds reference to fold, so refcnt is always > 0
		 * after this.
		 */
		refcount_dec(&fold->refcnt);
		__fl_put(fold);
	} else {
		if (handle) {
			/* user specifies a handle and it doesn't exist */
			err = idr_alloc_u32(&head->handle_idr, fnew, &handle,
					    handle, GFP_ATOMIC);

			/* Filter with specified handle was concurrently
			 * inserted after initial check in cls_api. This is not
			 * necessarily an error if NLM_F_EXCL is not set in
			 * message flags. Returning EAGAIN will cause cls_api to
			 * try to update concurrently inserted rule.
			 */
			if (err == -ENOSPC)
				err = -EAGAIN;
		} else {
			handle = 1;
			err = idr_alloc_u32(&head->handle_idr, fnew, &handle,
					    INT_MAX, GFP_ATOMIC);
		}
		if (err)
			goto errout_hw;

		refcount_inc(&fnew->refcnt);
		fnew->handle = handle;
		list_add_tail_rcu(&fnew->list, &fnew->mask->filters);
		spin_unlock(&tp->lock);
	}

	*arg = fnew;

	kfree(tb);
	tcf_queue_work(&mask->rwork, fl_uninit_mask_free_work);
	return 0;

errout_ht:
	spin_lock(&tp->lock);
errout_hw:
	fnew->deleted = true;
	spin_unlock(&tp->lock);
	if (!tc_skip_hw(fnew->flags))
		fl_hw_destroy_filter(tp, fnew, rtnl_held, NULL);
	if (in_ht)
		rhashtable_remove_fast(&fnew->mask->ht, &fnew->ht_node,
				       fnew->mask->filter_ht_params);
errout_mask:
	fl_mask_put(head, fnew->mask);
errout:
	__fl_put(fnew);
errout_tb:
	kfree(tb);
errout_mask_alloc:
	tcf_queue_work(&mask->rwork, fl_uninit_mask_free_work);
errout_fold:
	if (fold)
		__fl_put(fold);
	return err;
}

static int fl_delete(struct tcf_proto *tp, void *arg, bool *last,
		     bool rtnl_held, struct netlink_ext_ack *extack)
{
	struct cls_fl_head *head = fl_head_dereference(tp);
	struct cls_fl_filter *f = arg;
	bool last_on_mask;
	int err = 0;

	err = __fl_delete(tp, f, &last_on_mask, rtnl_held, extack);
	*last = list_empty(&head->masks);
	__fl_put(f);

	return err;
}

static void fl_walk(struct tcf_proto *tp, struct tcf_walker *arg,
		    bool rtnl_held)
{
	struct cls_fl_head *head = fl_head_dereference(tp);
	unsigned long id = arg->cookie, tmp;
	struct cls_fl_filter *f;

	arg->count = arg->skip;

	rcu_read_lock();
	idr_for_each_entry_continue_ul(&head->handle_idr, f, tmp, id) {
		/* don't return filters that are being deleted */
		if (!refcount_inc_not_zero(&f->refcnt))
			continue;
		rcu_read_unlock();

		if (arg->fn(tp, f, arg) < 0) {
			__fl_put(f);
			arg->stop = 1;
			rcu_read_lock();
			break;
		}
		__fl_put(f);
		arg->count++;
		rcu_read_lock();
	}
	rcu_read_unlock();
	arg->cookie = id;
}

static struct cls_fl_filter *
fl_get_next_hw_filter(struct tcf_proto *tp, struct cls_fl_filter *f, bool add)
{
	struct cls_fl_head *head = fl_head_dereference(tp);

	spin_lock(&tp->lock);
	if (list_empty(&head->hw_filters)) {
		spin_unlock(&tp->lock);
		return NULL;
	}

	if (!f)
		f = list_entry(&head->hw_filters, struct cls_fl_filter,
			       hw_list);
	list_for_each_entry_continue(f, &head->hw_filters, hw_list) {
		if (!(add && f->deleted) && refcount_inc_not_zero(&f->refcnt)) {
			spin_unlock(&tp->lock);
			return f;
		}
	}

	spin_unlock(&tp->lock);
	return NULL;
}

static int fl_reoffload(struct tcf_proto *tp, bool add, flow_setup_cb_t *cb,
			void *cb_priv, struct netlink_ext_ack *extack)
{
	struct tcf_block *block = tp->chain->block;
	struct flow_cls_offload cls_flower = {};
	struct cls_fl_filter *f = NULL;
	int err;

	/* hw_filters list can only be changed by hw offload functions after
	 * obtaining rtnl lock. Make sure it is not changed while reoffload is
	 * iterating it.
	 */
	ASSERT_RTNL();

	while ((f = fl_get_next_hw_filter(tp, f, add))) {
		cls_flower.rule =
			flow_rule_alloc(tcf_exts_num_actions(&f->exts));
		if (!cls_flower.rule) {
			__fl_put(f);
			return -ENOMEM;
		}

		tc_cls_common_offload_init(&cls_flower.common, tp, f->flags,
					   extack);
		cls_flower.command = add ?
			FLOW_CLS_REPLACE : FLOW_CLS_DESTROY;
		cls_flower.cookie = (unsigned long)f;
		cls_flower.rule->match.dissector = &f->mask->dissector;
		cls_flower.rule->match.mask = &f->mask->key;
		cls_flower.rule->match.key = &f->mkey;

		err = tc_setup_offload_action(&cls_flower.rule->action, &f->exts,
					      cls_flower.common.extack);
		if (err) {
			kfree(cls_flower.rule);
			if (tc_skip_sw(f->flags)) {
				__fl_put(f);
				return err;
			}
			goto next_flow;
		}

		cls_flower.classid = f->res.classid;

		err = tc_setup_cb_reoffload(block, tp, add, cb,
					    TC_SETUP_CLSFLOWER, &cls_flower,
					    cb_priv, &f->flags,
					    &f->in_hw_count);
		tc_cleanup_offload_action(&cls_flower.rule->action);
		kfree(cls_flower.rule);

		if (err) {
			__fl_put(f);
			return err;
		}
next_flow:
		__fl_put(f);
	}

	return 0;
}

static void fl_hw_add(struct tcf_proto *tp, void *type_data)
{
	struct flow_cls_offload *cls_flower = type_data;
	struct cls_fl_filter *f =
		(struct cls_fl_filter *) cls_flower->cookie;
	struct cls_fl_head *head = fl_head_dereference(tp);

	spin_lock(&tp->lock);
	list_add(&f->hw_list, &head->hw_filters);
	spin_unlock(&tp->lock);
}

static void fl_hw_del(struct tcf_proto *tp, void *type_data)
{
	struct flow_cls_offload *cls_flower = type_data;
	struct cls_fl_filter *f =
		(struct cls_fl_filter *) cls_flower->cookie;

	spin_lock(&tp->lock);
	if (!list_empty(&f->hw_list))
		list_del_init(&f->hw_list);
	spin_unlock(&tp->lock);
}

static int fl_hw_create_tmplt(struct tcf_chain *chain,
			      struct fl_flow_tmplt *tmplt)
{
	struct flow_cls_offload cls_flower = {};
	struct tcf_block *block = chain->block;

	cls_flower.rule = flow_rule_alloc(0);
	if (!cls_flower.rule)
		return -ENOMEM;

	cls_flower.common.chain_index = chain->index;
	cls_flower.command = FLOW_CLS_TMPLT_CREATE;
	cls_flower.cookie = (unsigned long) tmplt;
	cls_flower.rule->match.dissector = &tmplt->dissector;
	cls_flower.rule->match.mask = &tmplt->mask;
	cls_flower.rule->match.key = &tmplt->dummy_key;

	/* We don't care if driver (any of them) fails to handle this
	 * call. It serves just as a hint for it.
	 */
	tc_setup_cb_call(block, TC_SETUP_CLSFLOWER, &cls_flower, false, true);
	kfree(cls_flower.rule);

	return 0;
}

static void fl_hw_destroy_tmplt(struct tcf_chain *chain,
				struct fl_flow_tmplt *tmplt)
{
	struct flow_cls_offload cls_flower = {};
	struct tcf_block *block = chain->block;

	cls_flower.common.chain_index = chain->index;
	cls_flower.command = FLOW_CLS_TMPLT_DESTROY;
	cls_flower.cookie = (unsigned long) tmplt;

	tc_setup_cb_call(block, TC_SETUP_CLSFLOWER, &cls_flower, false, true);
}

static void *fl_tmplt_create(struct net *net, struct tcf_chain *chain,
			     struct nlattr **tca,
			     struct netlink_ext_ack *extack)
{
	struct fl_flow_tmplt *tmplt;
	struct nlattr **tb;
	int err;

	if (!tca[TCA_OPTIONS])
		return ERR_PTR(-EINVAL);

	tb = kcalloc(TCA_FLOWER_MAX + 1, sizeof(struct nlattr *), GFP_KERNEL);
	if (!tb)
		return ERR_PTR(-ENOBUFS);
	err = nla_parse_nested_deprecated(tb, TCA_FLOWER_MAX,
					  tca[TCA_OPTIONS], fl_policy, NULL);
	if (err)
		goto errout_tb;

	tmplt = kzalloc(sizeof(*tmplt), GFP_KERNEL);
	if (!tmplt) {
		err = -ENOMEM;
		goto errout_tb;
	}
	tmplt->chain = chain;
	err = fl_set_key(net, tb, &tmplt->dummy_key, &tmplt->mask, extack);
	if (err)
		goto errout_tmplt;

	fl_init_dissector(&tmplt->dissector, &tmplt->mask);

	err = fl_hw_create_tmplt(chain, tmplt);
	if (err)
		goto errout_tmplt;

	kfree(tb);
	return tmplt;

errout_tmplt:
	kfree(tmplt);
errout_tb:
	kfree(tb);
	return ERR_PTR(err);
}

static void fl_tmplt_destroy(void *tmplt_priv)
{
	struct fl_flow_tmplt *tmplt = tmplt_priv;

	fl_hw_destroy_tmplt(tmplt->chain, tmplt);
	kfree(tmplt);
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

static int fl_dump_key_port_range(struct sk_buff *skb, struct fl_flow_key *key,
				  struct fl_flow_key *mask)
{
	if (fl_dump_key_val(skb, &key->tp_range.tp_min.dst,
			    TCA_FLOWER_KEY_PORT_DST_MIN,
			    &mask->tp_range.tp_min.dst, TCA_FLOWER_UNSPEC,
			    sizeof(key->tp_range.tp_min.dst)) ||
	    fl_dump_key_val(skb, &key->tp_range.tp_max.dst,
			    TCA_FLOWER_KEY_PORT_DST_MAX,
			    &mask->tp_range.tp_max.dst, TCA_FLOWER_UNSPEC,
			    sizeof(key->tp_range.tp_max.dst)) ||
	    fl_dump_key_val(skb, &key->tp_range.tp_min.src,
			    TCA_FLOWER_KEY_PORT_SRC_MIN,
			    &mask->tp_range.tp_min.src, TCA_FLOWER_UNSPEC,
			    sizeof(key->tp_range.tp_min.src)) ||
	    fl_dump_key_val(skb, &key->tp_range.tp_max.src,
			    TCA_FLOWER_KEY_PORT_SRC_MAX,
			    &mask->tp_range.tp_max.src, TCA_FLOWER_UNSPEC,
			    sizeof(key->tp_range.tp_max.src)))
		return -1;

	return 0;
}

static int fl_dump_key_mpls_opt_lse(struct sk_buff *skb,
				    struct flow_dissector_key_mpls *mpls_key,
				    struct flow_dissector_key_mpls *mpls_mask,
				    u8 lse_index)
{
	struct flow_dissector_mpls_lse *lse_mask = &mpls_mask->ls[lse_index];
	struct flow_dissector_mpls_lse *lse_key = &mpls_key->ls[lse_index];
	int err;

	err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_OPT_LSE_DEPTH,
			 lse_index + 1);
	if (err)
		return err;

	if (lse_mask->mpls_ttl) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_OPT_LSE_TTL,
				 lse_key->mpls_ttl);
		if (err)
			return err;
	}
	if (lse_mask->mpls_bos) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_OPT_LSE_BOS,
				 lse_key->mpls_bos);
		if (err)
			return err;
	}
	if (lse_mask->mpls_tc) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_OPT_LSE_TC,
				 lse_key->mpls_tc);
		if (err)
			return err;
	}
	if (lse_mask->mpls_label) {
		err = nla_put_u32(skb, TCA_FLOWER_KEY_MPLS_OPT_LSE_LABEL,
				  lse_key->mpls_label);
		if (err)
			return err;
	}

	return 0;
}

static int fl_dump_key_mpls_opts(struct sk_buff *skb,
				 struct flow_dissector_key_mpls *mpls_key,
				 struct flow_dissector_key_mpls *mpls_mask)
{
	struct nlattr *opts;
	struct nlattr *lse;
	u8 lse_index;
	int err;

	opts = nla_nest_start(skb, TCA_FLOWER_KEY_MPLS_OPTS);
	if (!opts)
		return -EMSGSIZE;

	for (lse_index = 0; lse_index < FLOW_DIS_MPLS_MAX; lse_index++) {
		if (!(mpls_mask->used_lses & 1 << lse_index))
			continue;

		lse = nla_nest_start(skb, TCA_FLOWER_KEY_MPLS_OPTS_LSE);
		if (!lse) {
			err = -EMSGSIZE;
			goto err_opts;
		}

		err = fl_dump_key_mpls_opt_lse(skb, mpls_key, mpls_mask,
					       lse_index);
		if (err)
			goto err_opts_lse;
		nla_nest_end(skb, lse);
	}
	nla_nest_end(skb, opts);

	return 0;

err_opts_lse:
	nla_nest_cancel(skb, lse);
err_opts:
	nla_nest_cancel(skb, opts);

	return err;
}

static int fl_dump_key_mpls(struct sk_buff *skb,
			    struct flow_dissector_key_mpls *mpls_key,
			    struct flow_dissector_key_mpls *mpls_mask)
{
	struct flow_dissector_mpls_lse *lse_mask;
	struct flow_dissector_mpls_lse *lse_key;
	int err;

	if (!mpls_mask->used_lses)
		return 0;

	lse_mask = &mpls_mask->ls[0];
	lse_key = &mpls_key->ls[0];

	/* For backward compatibility, don't use the MPLS nested attributes if
	 * the rule can be expressed using the old attributes.
	 */
	if (mpls_mask->used_lses & ~1 ||
	    (!lse_mask->mpls_ttl && !lse_mask->mpls_bos &&
	     !lse_mask->mpls_tc && !lse_mask->mpls_label))
		return fl_dump_key_mpls_opts(skb, mpls_key, mpls_mask);

	if (lse_mask->mpls_ttl) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_TTL,
				 lse_key->mpls_ttl);
		if (err)
			return err;
	}
	if (lse_mask->mpls_tc) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_TC,
				 lse_key->mpls_tc);
		if (err)
			return err;
	}
	if (lse_mask->mpls_label) {
		err = nla_put_u32(skb, TCA_FLOWER_KEY_MPLS_LABEL,
				  lse_key->mpls_label);
		if (err)
			return err;
	}
	if (lse_mask->mpls_bos) {
		err = nla_put_u8(skb, TCA_FLOWER_KEY_MPLS_BOS,
				 lse_key->mpls_bos);
		if (err)
			return err;
	}
	return 0;
}

static int fl_dump_key_ip(struct sk_buff *skb, bool encap,
			  struct flow_dissector_key_ip *key,
			  struct flow_dissector_key_ip *mask)
{
	int tos_key = encap ? TCA_FLOWER_KEY_ENC_IP_TOS : TCA_FLOWER_KEY_IP_TOS;
	int ttl_key = encap ? TCA_FLOWER_KEY_ENC_IP_TTL : TCA_FLOWER_KEY_IP_TTL;
	int tos_mask = encap ? TCA_FLOWER_KEY_ENC_IP_TOS_MASK : TCA_FLOWER_KEY_IP_TOS_MASK;
	int ttl_mask = encap ? TCA_FLOWER_KEY_ENC_IP_TTL_MASK : TCA_FLOWER_KEY_IP_TTL_MASK;

	if (fl_dump_key_val(skb, &key->tos, tos_key, &mask->tos, tos_mask, sizeof(key->tos)) ||
	    fl_dump_key_val(skb, &key->ttl, ttl_key, &mask->ttl, ttl_mask, sizeof(key->ttl)))
		return -1;

	return 0;
}

static int fl_dump_key_vlan(struct sk_buff *skb,
			    int vlan_id_key, int vlan_prio_key,
			    struct flow_dissector_key_vlan *vlan_key,
			    struct flow_dissector_key_vlan *vlan_mask)
{
	int err;

	if (!memchr_inv(vlan_mask, 0, sizeof(*vlan_mask)))
		return 0;
	if (vlan_mask->vlan_id) {
		err = nla_put_u16(skb, vlan_id_key,
				  vlan_key->vlan_id);
		if (err)
			return err;
	}
	if (vlan_mask->vlan_priority) {
		err = nla_put_u8(skb, vlan_prio_key,
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
	fl_get_key_flag(flags_key, flags_mask, &key, &mask,
			TCA_FLOWER_KEY_FLAGS_FRAG_IS_FIRST,
			FLOW_DIS_FIRST_FRAG);

	_key = cpu_to_be32(key);
	_mask = cpu_to_be32(mask);

	err = nla_put(skb, TCA_FLOWER_KEY_FLAGS, 4, &_key);
	if (err)
		return err;

	return nla_put(skb, TCA_FLOWER_KEY_FLAGS_MASK, 4, &_mask);
}

static int fl_dump_key_geneve_opt(struct sk_buff *skb,
				  struct flow_dissector_key_enc_opts *enc_opts)
{
	struct geneve_opt *opt;
	struct nlattr *nest;
	int opt_off = 0;

	nest = nla_nest_start_noflag(skb, TCA_FLOWER_KEY_ENC_OPTS_GENEVE);
	if (!nest)
		goto nla_put_failure;

	while (enc_opts->len > opt_off) {
		opt = (struct geneve_opt *)&enc_opts->data[opt_off];

		if (nla_put_be16(skb, TCA_FLOWER_KEY_ENC_OPT_GENEVE_CLASS,
				 opt->opt_class))
			goto nla_put_failure;
		if (nla_put_u8(skb, TCA_FLOWER_KEY_ENC_OPT_GENEVE_TYPE,
			       opt->type))
			goto nla_put_failure;
		if (nla_put(skb, TCA_FLOWER_KEY_ENC_OPT_GENEVE_DATA,
			    opt->length * 4, opt->opt_data))
			goto nla_put_failure;

		opt_off += sizeof(struct geneve_opt) + opt->length * 4;
	}
	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int fl_dump_key_vxlan_opt(struct sk_buff *skb,
				 struct flow_dissector_key_enc_opts *enc_opts)
{
	struct vxlan_metadata *md;
	struct nlattr *nest;

	nest = nla_nest_start_noflag(skb, TCA_FLOWER_KEY_ENC_OPTS_VXLAN);
	if (!nest)
		goto nla_put_failure;

	md = (struct vxlan_metadata *)&enc_opts->data[0];
	if (nla_put_u32(skb, TCA_FLOWER_KEY_ENC_OPT_VXLAN_GBP, md->gbp))
		goto nla_put_failure;

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int fl_dump_key_erspan_opt(struct sk_buff *skb,
				  struct flow_dissector_key_enc_opts *enc_opts)
{
	struct erspan_metadata *md;
	struct nlattr *nest;

	nest = nla_nest_start_noflag(skb, TCA_FLOWER_KEY_ENC_OPTS_ERSPAN);
	if (!nest)
		goto nla_put_failure;

	md = (struct erspan_metadata *)&enc_opts->data[0];
	if (nla_put_u8(skb, TCA_FLOWER_KEY_ENC_OPT_ERSPAN_VER, md->version))
		goto nla_put_failure;

	if (md->version == 1 &&
	    nla_put_be32(skb, TCA_FLOWER_KEY_ENC_OPT_ERSPAN_INDEX, md->u.index))
		goto nla_put_failure;

	if (md->version == 2 &&
	    (nla_put_u8(skb, TCA_FLOWER_KEY_ENC_OPT_ERSPAN_DIR,
			md->u.md2.dir) ||
	     nla_put_u8(skb, TCA_FLOWER_KEY_ENC_OPT_ERSPAN_HWID,
			get_hwid(&md->u.md2))))
		goto nla_put_failure;

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int fl_dump_key_gtp_opt(struct sk_buff *skb,
			       struct flow_dissector_key_enc_opts *enc_opts)

{
	struct gtp_pdu_session_info *session_info;
	struct nlattr *nest;

	nest = nla_nest_start_noflag(skb, TCA_FLOWER_KEY_ENC_OPTS_GTP);
	if (!nest)
		goto nla_put_failure;

	session_info = (struct gtp_pdu_session_info *)&enc_opts->data[0];

	if (nla_put_u8(skb, TCA_FLOWER_KEY_ENC_OPT_GTP_PDU_TYPE,
		       session_info->pdu_type))
		goto nla_put_failure;

	if (nla_put_u8(skb, TCA_FLOWER_KEY_ENC_OPT_GTP_QFI, session_info->qfi))
		goto nla_put_failure;

	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int fl_dump_key_ct(struct sk_buff *skb,
			  struct flow_dissector_key_ct *key,
			  struct flow_dissector_key_ct *mask)
{
	if (IS_ENABLED(CONFIG_NF_CONNTRACK) &&
	    fl_dump_key_val(skb, &key->ct_state, TCA_FLOWER_KEY_CT_STATE,
			    &mask->ct_state, TCA_FLOWER_KEY_CT_STATE_MASK,
			    sizeof(key->ct_state)))
		goto nla_put_failure;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_ZONES) &&
	    fl_dump_key_val(skb, &key->ct_zone, TCA_FLOWER_KEY_CT_ZONE,
			    &mask->ct_zone, TCA_FLOWER_KEY_CT_ZONE_MASK,
			    sizeof(key->ct_zone)))
		goto nla_put_failure;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_MARK) &&
	    fl_dump_key_val(skb, &key->ct_mark, TCA_FLOWER_KEY_CT_MARK,
			    &mask->ct_mark, TCA_FLOWER_KEY_CT_MARK_MASK,
			    sizeof(key->ct_mark)))
		goto nla_put_failure;

	if (IS_ENABLED(CONFIG_NF_CONNTRACK_LABELS) &&
	    fl_dump_key_val(skb, &key->ct_labels, TCA_FLOWER_KEY_CT_LABELS,
			    &mask->ct_labels, TCA_FLOWER_KEY_CT_LABELS_MASK,
			    sizeof(key->ct_labels)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int fl_dump_key_options(struct sk_buff *skb, int enc_opt_type,
			       struct flow_dissector_key_enc_opts *enc_opts)
{
	struct nlattr *nest;
	int err;

	if (!enc_opts->len)
		return 0;

	nest = nla_nest_start_noflag(skb, enc_opt_type);
	if (!nest)
		goto nla_put_failure;

	switch (enc_opts->dst_opt_type) {
	case TUNNEL_GENEVE_OPT:
		err = fl_dump_key_geneve_opt(skb, enc_opts);
		if (err)
			goto nla_put_failure;
		break;
	case TUNNEL_VXLAN_OPT:
		err = fl_dump_key_vxlan_opt(skb, enc_opts);
		if (err)
			goto nla_put_failure;
		break;
	case TUNNEL_ERSPAN_OPT:
		err = fl_dump_key_erspan_opt(skb, enc_opts);
		if (err)
			goto nla_put_failure;
		break;
	case TUNNEL_GTP_OPT:
		err = fl_dump_key_gtp_opt(skb, enc_opts);
		if (err)
			goto nla_put_failure;
		break;
	default:
		goto nla_put_failure;
	}
	nla_nest_end(skb, nest);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int fl_dump_key_enc_opt(struct sk_buff *skb,
			       struct flow_dissector_key_enc_opts *key_opts,
			       struct flow_dissector_key_enc_opts *msk_opts)
{
	int err;

	err = fl_dump_key_options(skb, TCA_FLOWER_KEY_ENC_OPTS, key_opts);
	if (err)
		return err;

	return fl_dump_key_options(skb, TCA_FLOWER_KEY_ENC_OPTS_MASK, msk_opts);
}

static int fl_dump_key(struct sk_buff *skb, struct net *net,
		       struct fl_flow_key *key, struct fl_flow_key *mask)
{
	if (mask->meta.ingress_ifindex) {
		struct net_device *dev;

		dev = __dev_get_by_index(net, key->meta.ingress_ifindex);
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

	if (mask->num_of_vlans.num_of_vlans) {
		if (nla_put_u8(skb, TCA_FLOWER_KEY_NUM_OF_VLANS, key->num_of_vlans.num_of_vlans))
			goto nla_put_failure;
	}

	if (fl_dump_key_mpls(skb, &key->mpls, &mask->mpls))
		goto nla_put_failure;

	if (fl_dump_key_vlan(skb, TCA_FLOWER_KEY_VLAN_ID,
			     TCA_FLOWER_KEY_VLAN_PRIO, &key->vlan, &mask->vlan))
		goto nla_put_failure;

	if (fl_dump_key_vlan(skb, TCA_FLOWER_KEY_CVLAN_ID,
			     TCA_FLOWER_KEY_CVLAN_PRIO,
			     &key->cvlan, &mask->cvlan) ||
	    (mask->cvlan.vlan_tpid &&
	     nla_put_be16(skb, TCA_FLOWER_KEY_VLAN_ETH_TYPE,
			  key->cvlan.vlan_tpid)))
		goto nla_put_failure;

	if (mask->basic.n_proto) {
		if (mask->cvlan.vlan_eth_type) {
			if (nla_put_be16(skb, TCA_FLOWER_KEY_CVLAN_ETH_TYPE,
					 key->basic.n_proto))
				goto nla_put_failure;
		} else if (mask->vlan.vlan_eth_type) {
			if (nla_put_be16(skb, TCA_FLOWER_KEY_VLAN_ETH_TYPE,
					 key->vlan.vlan_eth_type))
				goto nla_put_failure;
		}
	}

	if ((key->basic.n_proto == htons(ETH_P_IP) ||
	     key->basic.n_proto == htons(ETH_P_IPV6)) &&
	    (fl_dump_key_val(skb, &key->basic.ip_proto, TCA_FLOWER_KEY_IP_PROTO,
			    &mask->basic.ip_proto, TCA_FLOWER_UNSPEC,
			    sizeof(key->basic.ip_proto)) ||
	    fl_dump_key_ip(skb, false, &key->ip, &mask->ip)))
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
			     sizeof(key->tp.dst)) ||
	     fl_dump_key_val(skb, &key->tcp.flags, TCA_FLOWER_KEY_TCP_FLAGS,
			     &mask->tcp.flags, TCA_FLOWER_KEY_TCP_FLAGS_MASK,
			     sizeof(key->tcp.flags))))
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

	if ((key->basic.ip_proto == IPPROTO_TCP ||
	     key->basic.ip_proto == IPPROTO_UDP ||
	     key->basic.ip_proto == IPPROTO_SCTP) &&
	     fl_dump_key_port_range(skb, key, mask))
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
			    sizeof(key->enc_tp.dst)) ||
	    fl_dump_key_ip(skb, true, &key->enc_ip, &mask->enc_ip) ||
	    fl_dump_key_enc_opt(skb, &key->enc_opts, &mask->enc_opts))
		goto nla_put_failure;

	if (fl_dump_key_ct(skb, &key->ct, &mask->ct))
		goto nla_put_failure;

	if (fl_dump_key_flags(skb, key->control.flags, mask->control.flags))
		goto nla_put_failure;

	if (fl_dump_key_val(skb, &key->hash.hash, TCA_FLOWER_KEY_HASH,
			     &mask->hash.hash, TCA_FLOWER_KEY_HASH_MASK,
			     sizeof(key->hash.hash)))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int fl_dump(struct net *net, struct tcf_proto *tp, void *fh,
		   struct sk_buff *skb, struct tcmsg *t, bool rtnl_held)
{
	struct cls_fl_filter *f = fh;
	struct nlattr *nest;
	struct fl_flow_key *key, *mask;
	bool skip_hw;

	if (!f)
		return skb->len;

	t->tcm_handle = f->handle;

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (!nest)
		goto nla_put_failure;

	spin_lock(&tp->lock);

	if (f->res.classid &&
	    nla_put_u32(skb, TCA_FLOWER_CLASSID, f->res.classid))
		goto nla_put_failure_locked;

	key = &f->key;
	mask = &f->mask->key;
	skip_hw = tc_skip_hw(f->flags);

	if (fl_dump_key(skb, net, key, mask))
		goto nla_put_failure_locked;

	if (f->flags && nla_put_u32(skb, TCA_FLOWER_FLAGS, f->flags))
		goto nla_put_failure_locked;

	spin_unlock(&tp->lock);

	if (!skip_hw)
		fl_hw_update_stats(tp, f, rtnl_held);

	if (nla_put_u32(skb, TCA_FLOWER_IN_HW_COUNT, f->in_hw_count))
		goto nla_put_failure;

	if (tcf_exts_dump(skb, &f->exts))
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &f->exts) < 0)
		goto nla_put_failure;

	return skb->len;

nla_put_failure_locked:
	spin_unlock(&tp->lock);
nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static int fl_terse_dump(struct net *net, struct tcf_proto *tp, void *fh,
			 struct sk_buff *skb, struct tcmsg *t, bool rtnl_held)
{
	struct cls_fl_filter *f = fh;
	struct nlattr *nest;
	bool skip_hw;

	if (!f)
		return skb->len;

	t->tcm_handle = f->handle;

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (!nest)
		goto nla_put_failure;

	spin_lock(&tp->lock);

	skip_hw = tc_skip_hw(f->flags);

	if (f->flags && nla_put_u32(skb, TCA_FLOWER_FLAGS, f->flags))
		goto nla_put_failure_locked;

	spin_unlock(&tp->lock);

	if (!skip_hw)
		fl_hw_update_stats(tp, f, rtnl_held);

	if (tcf_exts_terse_dump(skb, &f->exts))
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	return skb->len;

nla_put_failure_locked:
	spin_unlock(&tp->lock);
nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static int fl_tmplt_dump(struct sk_buff *skb, struct net *net, void *tmplt_priv)
{
	struct fl_flow_tmplt *tmplt = tmplt_priv;
	struct fl_flow_key *key, *mask;
	struct nlattr *nest;

	nest = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (!nest)
		goto nla_put_failure;

	key = &tmplt->dummy_key;
	mask = &tmplt->mask;

	if (fl_dump_key(skb, net, key, mask))
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	return skb->len;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static void fl_bind_class(void *fh, u32 classid, unsigned long cl, void *q,
			  unsigned long base)
{
	struct cls_fl_filter *f = fh;

	if (f && f->res.classid == classid) {
		if (cl)
			__tcf_bind_filter(q, &f->res, base);
		else
			__tcf_unbind_filter(q, &f->res);
	}
}

static bool fl_delete_empty(struct tcf_proto *tp)
{
	struct cls_fl_head *head = fl_head_dereference(tp);

	spin_lock(&tp->lock);
	tp->deleting = idr_is_empty(&head->handle_idr);
	spin_unlock(&tp->lock);

	return tp->deleting;
}

static struct tcf_proto_ops cls_fl_ops __read_mostly = {
	.kind		= "flower",
	.classify	= fl_classify,
	.init		= fl_init,
	.destroy	= fl_destroy,
	.get		= fl_get,
	.put		= fl_put,
	.change		= fl_change,
	.delete		= fl_delete,
	.delete_empty	= fl_delete_empty,
	.walk		= fl_walk,
	.reoffload	= fl_reoffload,
	.hw_add		= fl_hw_add,
	.hw_del		= fl_hw_del,
	.dump		= fl_dump,
	.terse_dump	= fl_terse_dump,
	.bind_class	= fl_bind_class,
	.tmplt_create	= fl_tmplt_create,
	.tmplt_destroy	= fl_tmplt_destroy,
	.tmplt_dump	= fl_tmplt_dump,
	.owner		= THIS_MODULE,
	.flags		= TCF_PROTO_OPS_DOIT_UNLOCKED,
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
