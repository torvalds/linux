/*
 * Stateless NAT actions
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/tc_act/tc_nat.h>
#include <net/act_api.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/netlink.h>
#include <net/tc_act/tc_nat.h>
#include <net/tcp.h>
#include <net/udp.h>


#define NAT_TAB_MASK	15
static struct tcf_common *tcf_nat_ht[NAT_TAB_MASK + 1];
static u32 nat_idx_gen;
static DEFINE_RWLOCK(nat_lock);

static struct tcf_hashinfo nat_hash_info = {
	.htab	=	tcf_nat_ht,
	.hmask	=	NAT_TAB_MASK,
	.lock	=	&nat_lock,
};

static int tcf_nat_init(struct rtattr *rta, struct rtattr *est,
			struct tc_action *a, int ovr, int bind)
{
	struct rtattr *tb[TCA_NAT_MAX];
	struct tc_nat *parm;
	int ret = 0;
	struct tcf_nat *p;
	struct tcf_common *pc;

	if (rta == NULL || rtattr_parse_nested(tb, TCA_NAT_MAX, rta) < 0)
		return -EINVAL;

	if (tb[TCA_NAT_PARMS - 1] == NULL ||
	    RTA_PAYLOAD(tb[TCA_NAT_PARMS - 1]) < sizeof(*parm))
		return -EINVAL;
	parm = RTA_DATA(tb[TCA_NAT_PARMS - 1]);

	pc = tcf_hash_check(parm->index, a, bind, &nat_hash_info);
	if (!pc) {
		pc = tcf_hash_create(parm->index, est, a, sizeof(*p), bind,
				     &nat_idx_gen, &nat_hash_info);
		if (unlikely(!pc))
			return -ENOMEM;
		p = to_tcf_nat(pc);
		ret = ACT_P_CREATED;
	} else {
		p = to_tcf_nat(pc);
		if (!ovr) {
			tcf_hash_release(pc, bind, &nat_hash_info);
			return -EEXIST;
		}
	}

	spin_lock_bh(&p->tcf_lock);
	p->old_addr = parm->old_addr;
	p->new_addr = parm->new_addr;
	p->mask = parm->mask;
	p->flags = parm->flags;

	p->tcf_action = parm->action;
	spin_unlock_bh(&p->tcf_lock);

	if (ret == ACT_P_CREATED)
		tcf_hash_insert(pc, &nat_hash_info);

	return ret;
}

static int tcf_nat_cleanup(struct tc_action *a, int bind)
{
	struct tcf_nat *p = a->priv;

	return tcf_hash_release(&p->common, bind, &nat_hash_info);
}

static int tcf_nat(struct sk_buff *skb, struct tc_action *a,
		   struct tcf_result *res)
{
	struct tcf_nat *p = a->priv;
	struct iphdr *iph;
	__be32 old_addr;
	__be32 new_addr;
	__be32 mask;
	__be32 addr;
	int egress;
	int action;
	int ihl;

	spin_lock(&p->tcf_lock);

	p->tcf_tm.lastuse = jiffies;
	old_addr = p->old_addr;
	new_addr = p->new_addr;
	mask = p->mask;
	egress = p->flags & TCA_NAT_FLAG_EGRESS;
	action = p->tcf_action;

	p->tcf_bstats.bytes += skb->len;
	p->tcf_bstats.packets++;

	spin_unlock(&p->tcf_lock);

	if (unlikely(action == TC_ACT_SHOT))
		goto drop;

	if (!pskb_may_pull(skb, sizeof(*iph)))
		goto drop;

	iph = ip_hdr(skb);

	if (egress)
		addr = iph->saddr;
	else
		addr = iph->daddr;

	if (!((old_addr ^ addr) & mask)) {
		if (skb_cloned(skb) &&
		    !skb_clone_writable(skb, sizeof(*iph)) &&
		    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
			goto drop;

		new_addr &= mask;
		new_addr |= addr & ~mask;

		/* Rewrite IP header */
		iph = ip_hdr(skb);
		if (egress)
			iph->saddr = new_addr;
		else
			iph->daddr = new_addr;

		nf_csum_replace4(&iph->check, addr, new_addr);
	}

	ihl = iph->ihl * 4;

	/* It would be nice to share code with stateful NAT. */
	switch (iph->frag_off & htons(IP_OFFSET) ? 0 : iph->protocol) {
	case IPPROTO_TCP:
	{
		struct tcphdr *tcph;

		if (!pskb_may_pull(skb, ihl + sizeof(*tcph)) ||
		    (skb_cloned(skb) &&
		     !skb_clone_writable(skb, ihl + sizeof(*tcph)) &&
		     pskb_expand_head(skb, 0, 0, GFP_ATOMIC)))
			goto drop;

		tcph = (void *)(skb_network_header(skb) + ihl);
		nf_proto_csum_replace4(&tcph->check, skb, addr, new_addr, 1);
		break;
	}
	case IPPROTO_UDP:
	{
		struct udphdr *udph;

		if (!pskb_may_pull(skb, ihl + sizeof(*udph)) ||
		    (skb_cloned(skb) &&
		     !skb_clone_writable(skb, ihl + sizeof(*udph)) &&
		     pskb_expand_head(skb, 0, 0, GFP_ATOMIC)))
			goto drop;

		udph = (void *)(skb_network_header(skb) + ihl);
		if (udph->check || skb->ip_summed == CHECKSUM_PARTIAL) {
			nf_proto_csum_replace4(&udph->check, skb, addr,
					       new_addr, 1);
			if (!udph->check)
				udph->check = CSUM_MANGLED_0;
		}
		break;
	}
	case IPPROTO_ICMP:
	{
		struct icmphdr *icmph;

		if (!pskb_may_pull(skb, ihl + sizeof(*icmph) + sizeof(*iph)))
			goto drop;

		icmph = (void *)(skb_network_header(skb) + ihl);

		if ((icmph->type != ICMP_DEST_UNREACH) &&
		    (icmph->type != ICMP_TIME_EXCEEDED) &&
		    (icmph->type != ICMP_PARAMETERPROB))
			break;

		iph = (void *)(icmph + 1);
		if (egress)
			addr = iph->daddr;
		else
			addr = iph->saddr;

		if ((old_addr ^ addr) & mask)
			break;

		if (skb_cloned(skb) &&
		    !skb_clone_writable(skb,
					ihl + sizeof(*icmph) + sizeof(*iph)) &&
		    pskb_expand_head(skb, 0, 0, GFP_ATOMIC))
			goto drop;

		icmph = (void *)(skb_network_header(skb) + ihl);
		iph = (void *)(icmph + 1);

		new_addr &= mask;
		new_addr |= addr & ~mask;

		/* XXX Fix up the inner checksums. */
		if (egress)
			iph->daddr = new_addr;
		else
			iph->saddr = new_addr;

		nf_proto_csum_replace4(&icmph->checksum, skb, addr, new_addr,
				       1);
		break;
	}
	default:
		break;
	}

	return action;

drop:
	spin_lock(&p->tcf_lock);
	p->tcf_qstats.drops++;
	spin_unlock(&p->tcf_lock);
	return TC_ACT_SHOT;
}

static int tcf_nat_dump(struct sk_buff *skb, struct tc_action *a,
			int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_nat *p = a->priv;
	struct tc_nat *opt;
	struct tcf_t t;
	int s;

	s = sizeof(*opt);

	/* netlink spinlocks held above us - must use ATOMIC */
	opt = kzalloc(s, GFP_ATOMIC);
	if (unlikely(!opt))
		return -ENOBUFS;

	opt->old_addr = p->old_addr;
	opt->new_addr = p->new_addr;
	opt->mask = p->mask;
	opt->flags = p->flags;

	opt->index = p->tcf_index;
	opt->action = p->tcf_action;
	opt->refcnt = p->tcf_refcnt - ref;
	opt->bindcnt = p->tcf_bindcnt - bind;

	RTA_PUT(skb, TCA_NAT_PARMS, s, opt);
	t.install = jiffies_to_clock_t(jiffies - p->tcf_tm.install);
	t.lastuse = jiffies_to_clock_t(jiffies - p->tcf_tm.lastuse);
	t.expires = jiffies_to_clock_t(p->tcf_tm.expires);
	RTA_PUT(skb, TCA_NAT_TM, sizeof(t), &t);

	kfree(opt);

	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	kfree(opt);
	return -1;
}

static struct tc_action_ops act_nat_ops = {
	.kind		=	"nat",
	.hinfo		=	&nat_hash_info,
	.type		=	TCA_ACT_NAT,
	.capab		=	TCA_CAP_NONE,
	.owner		=	THIS_MODULE,
	.act		=	tcf_nat,
	.dump		=	tcf_nat_dump,
	.cleanup	=	tcf_nat_cleanup,
	.lookup		=	tcf_hash_search,
	.init		=	tcf_nat_init,
	.walk		=	tcf_generic_walker
};

MODULE_DESCRIPTION("Stateless NAT actions");
MODULE_LICENSE("GPL");

static int __init nat_init_module(void)
{
	return tcf_register_action(&act_nat_ops);
}

static void __exit nat_cleanup_module(void)
{
	tcf_unregister_action(&act_nat_ops);
}

module_init(nat_init_module);
module_exit(nat_cleanup_module);
