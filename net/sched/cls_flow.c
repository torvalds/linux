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

#include <net/pkt_cls.h>
#include <net/ip.h>
#include <net/route.h>
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
};

static u32 flow_hashrnd __read_mostly;
static int flow_hashrnd_initted __read_mostly;

static const struct tcf_ext_map flow_ext_map = {
	.action	= TCA_FLOW_ACT,
	.police	= TCA_FLOW_POLICE,
};

static inline u32 addr_fold(void *addr)
{
	unsigned long a = (unsigned long)addr;

	return (a & 0xFFFFFFFF) ^ (BITS_PER_LONG > 32 ? a >> 32 : 0);
}

static u32 flow_get_src(const struct sk_buff *skb)
{
	switch (skb->protocol) {
	case __constant_htons(ETH_P_IP):
		return ntohl(ip_hdr(skb)->saddr);
	case __constant_htons(ETH_P_IPV6):
		return ntohl(ipv6_hdr(skb)->saddr.s6_addr32[3]);
	default:
		return addr_fold(skb->sk);
	}
}

static u32 flow_get_dst(const struct sk_buff *skb)
{
	switch (skb->protocol) {
	case __constant_htons(ETH_P_IP):
		return ntohl(ip_hdr(skb)->daddr);
	case __constant_htons(ETH_P_IPV6):
		return ntohl(ipv6_hdr(skb)->daddr.s6_addr32[3]);
	default:
		return addr_fold(skb->dst) ^ (__force u16)skb->protocol;
	}
}

static u32 flow_get_proto(const struct sk_buff *skb)
{
	switch (skb->protocol) {
	case __constant_htons(ETH_P_IP):
		return ip_hdr(skb)->protocol;
	case __constant_htons(ETH_P_IPV6):
		return ipv6_hdr(skb)->nexthdr;
	default:
		return 0;
	}
}

static int has_ports(u8 protocol)
{
	switch (protocol) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
	case IPPROTO_SCTP:
	case IPPROTO_DCCP:
	case IPPROTO_ESP:
		return 1;
	default:
		return 0;
	}
}

static u32 flow_get_proto_src(const struct sk_buff *skb)
{
	u32 res = 0;

	switch (skb->protocol) {
	case __constant_htons(ETH_P_IP): {
		struct iphdr *iph = ip_hdr(skb);

		if (!(iph->frag_off&htons(IP_MF|IP_OFFSET)) &&
		    has_ports(iph->protocol))
			res = ntohs(*(__be16 *)((void *)iph + iph->ihl * 4));
		break;
	}
	case __constant_htons(ETH_P_IPV6): {
		struct ipv6hdr *iph = ipv6_hdr(skb);

		if (has_ports(iph->nexthdr))
			res = ntohs(*(__be16 *)&iph[1]);
		break;
	}
	default:
		res = addr_fold(skb->sk);
	}

	return res;
}

static u32 flow_get_proto_dst(const struct sk_buff *skb)
{
	u32 res = 0;

	switch (skb->protocol) {
	case __constant_htons(ETH_P_IP): {
		struct iphdr *iph = ip_hdr(skb);

		if (!(iph->frag_off&htons(IP_MF|IP_OFFSET)) &&
		    has_ports(iph->protocol))
			res = ntohs(*(__be16 *)((void *)iph + iph->ihl * 4 + 2));
		break;
	}
	case __constant_htons(ETH_P_IPV6): {
		struct ipv6hdr *iph = ipv6_hdr(skb);

		if (has_ports(iph->nexthdr))
			res = ntohs(*(__be16 *)((void *)&iph[1] + 2));
		break;
	}
	default:
		res = addr_fold(skb->dst) ^ (__force u16)skb->protocol;
	}

	return res;
}

static u32 flow_get_iif(const struct sk_buff *skb)
{
	return skb->iif;
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
	struct nf_conn *ct = nf_ct_get(skb, &ctinfo);			\
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

static u32 flow_get_nfct_src(const struct sk_buff *skb)
{
	switch (skb->protocol) {
	case __constant_htons(ETH_P_IP):
		return ntohl(CTTUPLE(skb, src.u3.ip));
	case __constant_htons(ETH_P_IPV6):
		return ntohl(CTTUPLE(skb, src.u3.ip6[3]));
	}
fallback:
	return flow_get_src(skb);
}

static u32 flow_get_nfct_dst(const struct sk_buff *skb)
{
	switch (skb->protocol) {
	case __constant_htons(ETH_P_IP):
		return ntohl(CTTUPLE(skb, dst.u3.ip));
	case __constant_htons(ETH_P_IPV6):
		return ntohl(CTTUPLE(skb, dst.u3.ip6[3]));
	}
fallback:
	return flow_get_dst(skb);
}

static u32 flow_get_nfct_proto_src(const struct sk_buff *skb)
{
	return ntohs(CTTUPLE(skb, src.u.all));
fallback:
	return flow_get_proto_src(skb);
}

static u32 flow_get_nfct_proto_dst(const struct sk_buff *skb)
{
	return ntohs(CTTUPLE(skb, dst.u.all));
fallback:
	return flow_get_proto_dst(skb);
}

static u32 flow_get_rtclassid(const struct sk_buff *skb)
{
#ifdef CONFIG_NET_CLS_ROUTE
	if (skb->dst)
		return skb->dst->tclassid;
#endif
	return 0;
}

static u32 flow_get_skuid(const struct sk_buff *skb)
{
	if (skb->sk && skb->sk->sk_socket && skb->sk->sk_socket->file)
		return skb->sk->sk_socket->file->f_uid;
	return 0;
}

static u32 flow_get_skgid(const struct sk_buff *skb)
{
	if (skb->sk && skb->sk->sk_socket && skb->sk->sk_socket->file)
		return skb->sk->sk_socket->file->f_gid;
	return 0;
}

static u32 flow_get_vlan_tag(const struct sk_buff *skb)
{
	u16 uninitialized_var(tag);

	if (vlan_get_tag(skb, &tag) < 0)
		return 0;
	return tag & VLAN_VID_MASK;
}

static u32 flow_key_get(const struct sk_buff *skb, int key)
{
	switch (key) {
	case FLOW_KEY_SRC:
		return flow_get_src(skb);
	case FLOW_KEY_DST:
		return flow_get_dst(skb);
	case FLOW_KEY_PROTO:
		return flow_get_proto(skb);
	case FLOW_KEY_PROTO_SRC:
		return flow_get_proto_src(skb);
	case FLOW_KEY_PROTO_DST:
		return flow_get_proto_dst(skb);
	case FLOW_KEY_IIF:
		return flow_get_iif(skb);
	case FLOW_KEY_PRIORITY:
		return flow_get_priority(skb);
	case FLOW_KEY_MARK:
		return flow_get_mark(skb);
	case FLOW_KEY_NFCT:
		return flow_get_nfct(skb);
	case FLOW_KEY_NFCT_SRC:
		return flow_get_nfct_src(skb);
	case FLOW_KEY_NFCT_DST:
		return flow_get_nfct_dst(skb);
	case FLOW_KEY_NFCT_PROTO_SRC:
		return flow_get_nfct_proto_src(skb);
	case FLOW_KEY_NFCT_PROTO_DST:
		return flow_get_nfct_proto_dst(skb);
	case FLOW_KEY_RTCLASSID:
		return flow_get_rtclassid(skb);
	case FLOW_KEY_SKUID:
		return flow_get_skuid(skb);
	case FLOW_KEY_SKGID:
		return flow_get_skgid(skb);
	case FLOW_KEY_VLAN_TAG:
		return flow_get_vlan_tag(skb);
	default:
		WARN_ON(1);
		return 0;
	}
}

static int flow_classify(struct sk_buff *skb, struct tcf_proto *tp,
			 struct tcf_result *res)
{
	struct flow_head *head = tp->root;
	struct flow_filter *f;
	u32 keymask;
	u32 classid;
	unsigned int n, key;
	int r;

	list_for_each_entry(f, &head->filters, list) {
		u32 keys[f->nkeys];

		if (!tcf_em_tree_match(skb, &f->ematches, NULL))
			continue;

		keymask = f->keymask;

		for (n = 0; n < f->nkeys; n++) {
			key = ffs(keymask) - 1;
			keymask &= ~(1 << key);
			keys[n] = flow_key_get(skb, key);
		}

		if (f->mode == FLOW_MODE_HASH)
			classid = jhash2(keys, f->nkeys, flow_hashrnd);
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

	if (!flow_hashrnd_initted) {
		get_random_bytes(&flow_hashrnd, 4);
		flow_hashrnd_initted = 1;
	}

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
	return;
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
