/*
 * net/sched/cls_flow.c		Generic flow classifier
 *
 * Copyright (c) 2007, 2008 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <linux/pkt_cls.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_vlan.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <net/pkt_cls.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/flow_keys.h>

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
#include <net/netfilter/nf_conntrack.h>
#endif

struct flow_head {
	struct list_head	filters;
};

struct flow_filter {
	struct list_head	list;
	struct tcf_exts		exts;
	struct tcf_ematch_tree	ematches;
	struct timer_list	perturb_timer;
	u32			perturb_period;
	u32			handle;

	u32			nkeys;
	u32			keymask;
	u32			mode;
	u32			mask;
	u32			xor;
	u32			rshift;
	u32			addend;
	u32			divisor;
	u32			baseclass;
	u32			hashrnd;
};

static const struct tcf_ext_map flow_ext_map = {
	.action	= TCA_FLOW_ACT,
	.police	= TCA_FLOW_POLICE,
};

static inline u32 addr_fold(void *addr)
{
	unsigned long a = (unsigned long)addr;

	return (a & 0xFFFFFFFF) ^ (BITS_PER_LONG > 32 ? a >> 32 : 0);
}

static u32 flow_get_src(const struct sk_buff *skb, const struct flow_keys *flow)
{
	if (flow->src)
		return ntohl(flow->src);
	return addr_fold(skb->sk);
}

static u32 flow_get_dst(const struct sk_buff *skb, const struct flow_keys *flow)
{
	if (flow->dst)
		return ntohl(flow->dst);
	return addr_fold(skb_dst(skb)) ^ (__force u16)skb->protocol;
}

static u32 flow_get_proto(const struct sk_buff *skb, const struct flow_keys *flow)
{
	return flow->ip_proto;
}

static u32 flow_get_proto_src(const struct sk_buff *skb, const struct flow_keys *flow)
{
	if (flow->ports)
		return ntohs(flow->port16[0]);

	return addr_fold(skb->sk);
}

static u32 flow_get_proto_dst(const struct sk_buff *skb, const struct flow_keys *flow)
{
	if (flow->ports)
		return ntohs(flow->port16[1]);

	return addr_fold(skb_dst(skb)) ^ (__force u16)skb->protocol;
}

static u32 flow_get_iif(const struct sk_buff *skb)
{
	return skb->skb_iif;
}

static u32 flow_get_priority(const struct sk_buff *skb)
{
	return skb->priority;
}

static u32 flow_get_mark(const struct sk_buff *skb)
{
	return skb->mark;
}

static u32 flow_get_nfct(const struct sk_buff *skb)
{
#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
	return addr_fold(skb->nfct);
#else
	return 0;
#endif
}

#if defined(CONFIG_NF_CONNTRACK) || defined(CONFIG_NF_CONNTRACK_MODULE)
#define CTTUPLE(skb, member)						\
({									\
	enum ip_conntrack_info ctinfo;					\
	const struct nf_conn *ct = nf_ct_get(skb, &ctinfo);		\
	if (ct == NULL)							\
		goto fallback;						\
	ct->tuplehash[CTINFO2DIR(ctinfo)].tuple.member;			\
})
#else
#define CTTUPLE(skb, member)						\
({									\
	goto fallback;							\
	0;								\
})
#endif

static u32 flow_get_nfct_src(const struct sk_buff *skb, const struct flow_keys *flow)
{
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		return ntohl(CTTUPLE(skb, src.u3.ip));
	case htons(ETH_P_IPV6):
		return ntohl(CTTUPLE(skb, src.u3.ip6[3]));
	}
fallback:
	return flow_get_src(skb, flow);
}

static u32 flow_get_nfct_dst(const struct sk_buff *skb, const struct flow_keys *flow)
{
	switch (skb->protocol) {
	case htons(ETH_P_IP):
		return ntohl(CTTUPLE(skb, dst.u3.ip));
	case htons(ETH_P_IPV6):
		return ntohl(CTTUPLE(skb, dst.u3.ip6[3]));
	}
fallback:
	return flow_get_dst(skb, flow);
}

static u32 flow_get_nfct_proto_src(const struct sk_buff *skb, const struct flow_keys *flow)
{
	return ntohs(CTTUPLE(skb, src.u.all));
fallback:
	return flow_get_proto_src(skb, flow);
}

static u32 flow_get_nfct_proto_dst(const struct sk_buff *skb, const struct flow_keys *flow)
{
	return ntohs(CTTUPLE(skb, dst.u.all));
fallback:
	return flow_get_proto_dst(skb, flow);
}

static u32 flow_get_rtclassid(const struct sk_buff *skb)
{
#ifdef CONFIG_IP_ROUTE_CLASSID
	if (skb_dst(skb))
		return skb_dst(skb)->tclassid;
#endif
	return 0;
}

static u32 flow_get_skuid(const struct sk_buff *skb)
{
	if (skb->sk && skb->sk->sk_socket && skb->sk->sk_socket->file)
		return skb->sk->sk_socket->file->f_cred->fsuid;
	return 0;
}

static u32 flow_get_skgid(const struct sk_buff *skb)
{
	if (skb->sk && skb->sk->sk_socket && skb->sk->sk_socket->file)
		return skb->sk->sk_socket->file->f_cred->fsgid;
	return 0;
}

static u32 flow_get_vlan_tag(const struct sk_buff *skb)
{
	u16 uninitialized_var(tag);

	if (vlan_get_tag(skb, &tag) < 0)
		return 0;
	return tag & VLAN_VID_MASK;
}

static u32 flow_get_rxhash(struct sk_buff *skb)
{
	return skb_get_rxhash(skb);
}

static u32 flow_key_get(struct sk_buff *skb, int key, struct flow_keys *flow)
{
	switch (key) {
	case FLOW_KEY_SRC:
		return flow_get_src(skb, flow);
	case FLOW_KEY_DST:
		return flow_get_dst(skb, flow);
	case FLOW_KEY_PROTO:
		return flow_get_proto(skb, flow);
	case FLOW_KEY_PROTO_SRC:
		return flow_get_proto_src(skb, flow);
	case FLOW_KEY_PROTO_DST:
		return flow_get_proto_dst(skb, flow);
	case FLOW_KEY_IIF:
		return flow_get_iif(skb);
	case FLOW_KEY_PRIORITY:
		return flow_get_priority(skb);
	case FLOW_KEY_MARK:
		return flow_get_mark(skb);
	case FLOW_KEY_NFCT:
		return flow_get_nfct(skb);
	case FLOW_KEY_NFCT_SRC:
		return flow_get_nfct_src(skb, flow);
	case FLOW_KEY_NFCT_DST:
		return flow_get_nfct_dst(skb, flow);
	case FLOW_KEY_NFCT_PROTO_SRC:
		return flow_get_nfct_proto_src(skb, flow);
	case FLOW_KEY_NFCT_PROTO_DST:
		return flow_get_nfct_proto_dst(skb, flow);
	case FLOW_KEY_RTCLASSID:
		return flow_get_rtclassid(skb);
	case FLOW_KEY_SKUID:
		return flow_get_skuid(skb);
	case FLOW_KEY_SKGID:
		return flow_get_skgid(skb);
	case FLOW_KEY_VLAN_TAG:
		return flow_get_vlan_tag(skb);
	case FLOW_KEY_RXHASH:
		return flow_get_rxhash(skb);
	default:
		WARN_ON(1);
		return 0;
	}
}

#define FLOW_KEYS_NEEDED ((1 << FLOW_KEY_SRC) | 		\
			  (1 << FLOW_KEY_DST) |			\
			  (1 << FLOW_KEY_PROTO) |		\
			  (1 << FLOW_KEY_PROTO_SRC) |		\
			  (1 << FLOW_KEY_PROTO_DST) | 		\
			  (1 << FLOW_KEY_NFCT_SRC) |		\
			  (1 << FLOW_KEY_NFCT_DST) |		\
			  (1 << FLOW_KEY_NFCT_PROTO_SRC) |	\
			  (1 << FLOW_KEY_NFCT_PROTO_DST))

static int flow_classify(struct sk_buff *skb, const struct tcf_proto *tp,
			 struct tcf_result *res)
{
	struct flow_head *head = tp->root;
	struct flow_filter *f;
	u32 keymask;
	u32 classid;
	unsigned int n, key;
	int r;

	list_for_each_entry(f, &head->filters, list) {
		u32 keys[FLOW_KEY_MAX + 1];
		struct flow_keys flow_keys;

		if (!tcf_em_tree_match(skb, &f->ematches, NULL))
			continue;

		keymask = f->keymask;
		if (keymask & FLOW_KEYS_NEEDED)
			skb_flow_dissect(skb, &flow_keys);

		for (n = 0; n < f->nkeys; n++) {
			key = ffs(keymask) - 1;
			keymask &= ~(1 << key);
			keys[n] = flow_key_get(skb, key, &flow_keys);
		}

		if (f->mode == FLOW_MODE_HASH)
			classid = jhash2(keys, f->nkeys, f->hashrnd);
		else {
			classid = keys[0];
			classid = (classid & f->mask) ^ f->xor;
			classid = (classid >> f->rshift) + f->addend;
		}

		if (f->divisor)
			classid %= f->divisor;

		res->class   = 0;
		res->classid = TC_H_MAKE(f->baseclass, f->baseclass + classid);

		r = tcf_exts_exec(skb, &f->exts, res);
		if (r < 0)
			continue;
		return r;
	}
	return -1;
}

static void flow_perturbation(unsigned long arg)
{
	struct flow_filter *f = (struct flow_filter *)arg;

	get_random_bytes(&f->hashrnd, 4);
	if (f->perturb_period)
		mod_timer(&f->perturb_timer, jiffies + f->perturb_period);
}

static const struct nla_policy flow_policy[TCA_FLOW_MAX + 1] = {
	[TCA_FLOW_KEYS]		= { .type = NLA_U32 },
	[TCA_FLOW_MODE]		= { .type = NLA_U32 },
	[TCA_FLOW_BASECLASS]	= { .type = NLA_U32 },
	[TCA_FLOW_RSHIFT]	= { .type = NLA_U32 },
	[TCA_FLOW_ADDEND]	= { .type = NLA_U32 },
	[TCA_FLOW_MASK]		= { .type = NLA_U32 },
	[TCA_FLOW_XOR]		= { .type = NLA_U32 },
	[TCA_FLOW_DIVISOR]	= { .type = NLA_U32 },
	[TCA_FLOW_ACT]		= { .type = NLA_NESTED },
	[TCA_FLOW_POLICE]	= { .type = NLA_NESTED },
	[TCA_FLOW_EMATCHES]	= { .type = NLA_NESTED },
	[TCA_FLOW_PERTURB]	= { .type = NLA_U32 },
};

static int flow_change(struct tcf_proto *tp, unsigned long base,
		       u32 handle, struct nlattr **tca,
		       unsigned long *arg)
{
	struct flow_head *head = tp->root;
	struct flow_filter *f;
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_FLOW_MAX + 1];
	struct tcf_exts e;
	struct tcf_ematch_tree t;
	unsigned int nkeys = 0;
	unsigned int perturb_period = 0;
	u32 baseclass = 0;
	u32 keymask = 0;
	u32 mode;
	int err;

	if (opt == NULL)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_FLOW_MAX, opt, flow_policy);
	if (err < 0)
		return err;

	if (tb[TCA_FLOW_BASECLASS]) {
		baseclass = nla_get_u32(tb[TCA_FLOW_BASECLASS]);
		if (TC_H_MIN(baseclass) == 0)
			return -EINVAL;
	}

	if (tb[TCA_FLOW_KEYS]) {
		keymask = nla_get_u32(tb[TCA_FLOW_KEYS]);

		nkeys = hweight32(keymask);
		if (nkeys == 0)
			return -EINVAL;

		if (fls(keymask) - 1 > FLOW_KEY_MAX)
			return -EOPNOTSUPP;
	}

	err = tcf_exts_validate(tp, tb, tca[TCA_RATE], &e, &flow_ext_map);
	if (err < 0)
		return err;

	err = tcf_em_tree_validate(tp, tb[TCA_FLOW_EMATCHES], &t);
	if (err < 0)
		goto err1;

	f = (struct flow_filter *)*arg;
	if (f != NULL) {
		err = -EINVAL;
		if (f->handle != handle && handle)
			goto err2;

		mode = f->mode;
		if (tb[TCA_FLOW_MODE])
			mode = nla_get_u32(tb[TCA_FLOW_MODE]);
		if (mode != FLOW_MODE_HASH && nkeys > 1)
			goto err2;

		if (mode == FLOW_MODE_HASH)
			perturb_period = f->perturb_period;
		if (tb[TCA_FLOW_PERTURB]) {
			if (mode != FLOW_MODE_HASH)
				goto err2;
			perturb_period = nla_get_u32(tb[TCA_FLOW_PERTURB]) * HZ;
		}
	} else {
		err = -EINVAL;
		if (!handle)
			goto err2;
		if (!tb[TCA_FLOW_KEYS])
			goto err2;

		mode = FLOW_MODE_MAP;
		if (tb[TCA_FLOW_MODE])
			mode = nla_get_u32(tb[TCA_FLOW_MODE]);
		if (mode != FLOW_MODE_HASH && nkeys > 1)
			goto err2;

		if (tb[TCA_FLOW_PERTURB]) {
			if (mode != FLOW_MODE_HASH)
				goto err2;
			perturb_period = nla_get_u32(tb[TCA_FLOW_PERTURB]) * HZ;
		}

		if (TC_H_MAJ(baseclass) == 0)
			baseclass = TC_H_MAKE(tp->q->handle, baseclass);
		if (TC_H_MIN(baseclass) == 0)
			baseclass = TC_H_MAKE(baseclass, 1);

		err = -ENOBUFS;
		f = kzalloc(sizeof(*f), GFP_KERNEL);
		if (f == NULL)
			goto err2;

		f->handle = handle;
		f->mask	  = ~0U;

		get_random_bytes(&f->hashrnd, 4);
		f->perturb_timer.function = flow_perturbation;
		f->perturb_timer.data = (unsigned long)f;
		init_timer_deferrable(&f->perturb_timer);
	}

	tcf_exts_change(tp, &f->exts, &e);
	tcf_em_tree_change(tp, &f->ematches, &t);

	tcf_tree_lock(tp);

	if (tb[TCA_FLOW_KEYS]) {
		f->keymask = keymask;
		f->nkeys   = nkeys;
	}

	f->mode = mode;

	if (tb[TCA_FLOW_MASK])
		f->mask = nla_get_u32(tb[TCA_FLOW_MASK]);
	if (tb[TCA_FLOW_XOR])
		f->xor = nla_get_u32(tb[TCA_FLOW_XOR]);
	if (tb[TCA_FLOW_RSHIFT])
		f->rshift = nla_get_u32(tb[TCA_FLOW_RSHIFT]);
	if (tb[TCA_FLOW_ADDEND])
		f->addend = nla_get_u32(tb[TCA_FLOW_ADDEND]);

	if (tb[TCA_FLOW_DIVISOR])
		f->divisor = nla_get_u32(tb[TCA_FLOW_DIVISOR]);
	if (baseclass)
		f->baseclass = baseclass;

	f->perturb_period = perturb_period;
	del_timer(&f->perturb_timer);
	if (perturb_period)
		mod_timer(&f->perturb_timer, jiffies + perturb_period);

	if (*arg == 0)
		list_add_tail(&f->list, &head->filters);

	tcf_tree_unlock(tp);

	*arg = (unsigned long)f;
	return 0;

err2:
	tcf_em_tree_destroy(tp, &t);
err1:
	tcf_exts_destroy(tp, &e);
	return err;
}

static void flow_destroy_filter(struct tcf_proto *tp, struct flow_filter *f)
{
	del_timer_sync(&f->perturb_timer);
	tcf_exts_destroy(tp, &f->exts);
	tcf_em_tree_destroy(tp, &f->ematches);
	kfree(f);
}

static int flow_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct flow_filter *f = (struct flow_filter *)arg;

	tcf_tree_lock(tp);
	list_del(&f->list);
	tcf_tree_unlock(tp);
	flow_destroy_filter(tp, f);
	return 0;
}

static int flow_init(struct tcf_proto *tp)
{
	struct flow_head *head;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (head == NULL)
		return -ENOBUFS;
	INIT_LIST_HEAD(&head->filters);
	tp->root = head;
	return 0;
}

static void flow_destroy(struct tcf_proto *tp)
{
	struct flow_head *head = tp->root;
	struct flow_filter *f, *next;

	list_for_each_entry_safe(f, next, &head->filters, list) {
		list_del(&f->list);
		flow_destroy_filter(tp, f);
	}
	kfree(head);
}

static unsigned long flow_get(struct tcf_proto *tp, u32 handle)
{
	struct flow_head *head = tp->root;
	struct flow_filter *f;

	list_for_each_entry(f, &head->filters, list)
		if (f->handle == handle)
			return (unsigned long)f;
	return 0;
}

static void flow_put(struct tcf_proto *tp, unsigned long f)
{
}

static int flow_dump(struct tcf_proto *tp, unsigned long fh,
		     struct sk_buff *skb, struct tcmsg *t)
{
	struct flow_filter *f = (struct flow_filter *)fh;
	struct nlattr *nest;

	if (f == NULL)
		return skb->len;

	t->tcm_handle = f->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	NLA_PUT_U32(skb, TCA_FLOW_KEYS, f->keymask);
	NLA_PUT_U32(skb, TCA_FLOW_MODE, f->mode);

	if (f->mask != ~0 || f->xor != 0) {
		NLA_PUT_U32(skb, TCA_FLOW_MASK, f->mask);
		NLA_PUT_U32(skb, TCA_FLOW_XOR, f->xor);
	}
	if (f->rshift)
		NLA_PUT_U32(skb, TCA_FLOW_RSHIFT, f->rshift);
	if (f->addend)
		NLA_PUT_U32(skb, TCA_FLOW_ADDEND, f->addend);

	if (f->divisor)
		NLA_PUT_U32(skb, TCA_FLOW_DIVISOR, f->divisor);
	if (f->baseclass)
		NLA_PUT_U32(skb, TCA_FLOW_BASECLASS, f->baseclass);

	if (f->perturb_period)
		NLA_PUT_U32(skb, TCA_FLOW_PERTURB, f->perturb_period / HZ);

	if (tcf_exts_dump(skb, &f->exts, &flow_ext_map) < 0)
		goto nla_put_failure;
#ifdef CONFIG_NET_EMATCH
	if (f->ematches.hdr.nmatches &&
	    tcf_em_tree_dump(skb, &f->ematches, TCA_FLOW_EMATCHES) < 0)
		goto nla_put_failure;
#endif
	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &f->exts, &flow_ext_map) < 0)
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, nest);
	return -1;
}

static void flow_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct flow_head *head = tp->root;
	struct flow_filter *f;

	list_for_each_entry(f, &head->filters, list) {
		if (arg->count < arg->skip)
			goto skip;
		if (arg->fn(tp, (unsigned long)f, arg) < 0) {
			arg->stop = 1;
			break;
		}
skip:
		arg->count++;
	}
}

static struct tcf_proto_ops cls_flow_ops __read_mostly = {
	.kind		= "flow",
	.classify	= flow_classify,
	.init		= flow_init,
	.destroy	= flow_destroy,
	.change		= flow_change,
	.delete		= flow_delete,
	.get		= flow_get,
	.put		= flow_put,
	.dump		= flow_dump,
	.walk		= flow_walk,
	.owner		= THIS_MODULE,
};

static int __init cls_flow_init(void)
{
	return register_tcf_proto_ops(&cls_flow_ops);
}

static void __exit cls_flow_exit(void)
{
	unregister_tcf_proto_ops(&cls_flow_ops);
}

module_init(cls_flow_init);
module_exit(cls_flow_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick McHardy <kaber@trash.net>");
MODULE_DESCRIPTION("TC flow classifier");
