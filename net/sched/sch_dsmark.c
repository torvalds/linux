/* net/sched/sch_dsmark.c - Differentiated Services field marker */

/* Written 1998-2000 by Werner Almesberger, EPFL ICA */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/bitops.h>
#include <net/pkt_sched.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <asm/byteorder.h>

/*
 * classid	class		marking
 * -------	-----		-------
 *   n/a	  0		n/a
 *   x:0	  1		use entry [0]
 *   ...	 ...		...
 *   x:y y>0	 y+1		use entry [y]
 *   ...	 ...		...
 * x:indices-1	indices		use entry [indices-1]
 *   ...	 ...		...
 *   x:y	 y+1		use entry [y & (indices-1)]
 *   ...	 ...		...
 * 0xffff	0x10000		use entry [indices-1]
 */


#define NO_DEFAULT_INDEX	(1 << 16)

struct dsmark_qdisc_data {
	struct Qdisc		*q;
	struct tcf_proto	*filter_list;
	u8			*mask;	/* "owns" the array */
	u8			*value;
	u16			indices;
	u32			default_index;	/* index range is 0...0xffff */
	int			set_tc_index;
};

static inline int dsmark_valid_index(struct dsmark_qdisc_data *p, u16 index)
{
	return index <= p->indices && index > 0;
}

/* ------------------------- Class/flow operations ------------------------- */

static int dsmark_graft(struct Qdisc *sch, unsigned long arg,
			struct Qdisc *new, struct Qdisc **old)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);

	pr_debug("dsmark_graft(sch %p,[qdisc %p],new %p,old %p)\n",
		sch, p, new, old);

	if (new == NULL) {
		new = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops,
					sch->handle);
		if (new == NULL)
			new = &noop_qdisc;
	}

	sch_tree_lock(sch);
	*old = p->q;
	p->q = new;
	qdisc_tree_decrease_qlen(*old, (*old)->q.qlen);
	qdisc_reset(*old);
	sch_tree_unlock(sch);

	return 0;
}

static struct Qdisc *dsmark_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	return p->q;
}

static unsigned long dsmark_get(struct Qdisc *sch, u32 classid)
{
	pr_debug("dsmark_get(sch %p,[qdisc %p],classid %x)\n",
		sch, qdisc_priv(sch), classid);

	return TC_H_MIN(classid) + 1;
}

static unsigned long dsmark_bind_filter(struct Qdisc *sch,
					unsigned long parent, u32 classid)
{
	return dsmark_get(sch, classid);
}

static void dsmark_put(struct Qdisc *sch, unsigned long cl)
{
}

static const struct nla_policy dsmark_policy[TCA_DSMARK_MAX + 1] = {
	[TCA_DSMARK_INDICES]		= { .type = NLA_U16 },
	[TCA_DSMARK_DEFAULT_INDEX]	= { .type = NLA_U16 },
	[TCA_DSMARK_SET_TC_INDEX]	= { .type = NLA_FLAG },
	[TCA_DSMARK_MASK]		= { .type = NLA_U8 },
	[TCA_DSMARK_VALUE]		= { .type = NLA_U8 },
};

static int dsmark_change(struct Qdisc *sch, u32 classid, u32 parent,
			 struct nlattr **tca, unsigned long *arg)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	struct nlattr *opt = tca[TCA_OPTIONS];
	struct nlattr *tb[TCA_DSMARK_MAX + 1];
	int err = -EINVAL;
	u8 mask = 0;

	pr_debug("dsmark_change(sch %p,[qdisc %p],classid %x,parent %x),"
		"arg 0x%lx\n", sch, p, classid, parent, *arg);

	if (!dsmark_valid_index(p, *arg)) {
		err = -ENOENT;
		goto errout;
	}

	if (!opt)
		goto errout;

	err = nla_parse_nested(tb, TCA_DSMARK_MAX, opt, dsmark_policy);
	if (err < 0)
		goto errout;

	if (tb[TCA_DSMARK_MASK])
		mask = nla_get_u8(tb[TCA_DSMARK_MASK]);

	if (tb[TCA_DSMARK_VALUE])
		p->value[*arg - 1] = nla_get_u8(tb[TCA_DSMARK_VALUE]);

	if (tb[TCA_DSMARK_MASK])
		p->mask[*arg - 1] = mask;

	err = 0;

errout:
	return err;
}

static int dsmark_delete(struct Qdisc *sch, unsigned long arg)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);

	if (!dsmark_valid_index(p, arg))
		return -EINVAL;

	p->mask[arg - 1] = 0xff;
	p->value[arg - 1] = 0;

	return 0;
}

static void dsmark_walk(struct Qdisc *sch, struct qdisc_walker *walker)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	int i;

	pr_debug("dsmark_walk(sch %p,[qdisc %p],walker %p)\n", sch, p, walker);

	if (walker->stop)
		return;

	for (i = 0; i < p->indices; i++) {
		if (p->mask[i] == 0xff && !p->value[i])
			goto ignore;
		if (walker->count >= walker->skip) {
			if (walker->fn(sch, i + 1, walker) < 0) {
				walker->stop = 1;
				break;
			}
		}
ignore:
		walker->count++;
	}
}

static inline struct tcf_proto **dsmark_find_tcf(struct Qdisc *sch,
						 unsigned long cl)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	return &p->filter_list;
}

/* --------------------------- Qdisc operations ---------------------------- */

static int dsmark_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	int err;

	pr_debug("dsmark_enqueue(skb %p,sch %p,[qdisc %p])\n", skb, sch, p);

	if (p->set_tc_index) {
		switch (skb->protocol) {
		case htons(ETH_P_IP):
			if (skb_cow_head(skb, sizeof(struct iphdr)))
				goto drop;

			skb->tc_index = ipv4_get_dsfield(ip_hdr(skb))
				& ~INET_ECN_MASK;
			break;

		case htons(ETH_P_IPV6):
			if (skb_cow_head(skb, sizeof(struct ipv6hdr)))
				goto drop;

			skb->tc_index = ipv6_get_dsfield(ipv6_hdr(skb))
				& ~INET_ECN_MASK;
			break;
		default:
			skb->tc_index = 0;
			break;
		}
	}

	if (TC_H_MAJ(skb->priority) == sch->handle)
		skb->tc_index = TC_H_MIN(skb->priority);
	else {
		struct tcf_result res;
		int result = tc_classify(skb, p->filter_list, &res);

		pr_debug("result %d class 0x%04x\n", result, res.classid);

		switch (result) {
#ifdef CONFIG_NET_CLS_ACT
		case TC_ACT_QUEUED:
		case TC_ACT_STOLEN:
			kfree_skb(skb);
			return NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;

		case TC_ACT_SHOT:
			goto drop;
#endif
		case TC_ACT_OK:
			skb->tc_index = TC_H_MIN(res.classid);
			break;

		default:
			if (p->default_index != NO_DEFAULT_INDEX)
				skb->tc_index = p->default_index;
			break;
		}
	}

	err = qdisc_enqueue(skb, p->q);
	if (err != NET_XMIT_SUCCESS) {
		if (net_xmit_drop_count(err))
			sch->qstats.drops++;
		return err;
	}

	sch->q.qlen++;

	return NET_XMIT_SUCCESS;

drop:
	qdisc_drop(skb, sch);
	return NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
}

static struct sk_buff *dsmark_dequeue(struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	struct sk_buff *skb;
	u32 index;

	pr_debug("dsmark_dequeue(sch %p,[qdisc %p])\n", sch, p);

	skb = p->q->ops->dequeue(p->q);
	if (skb == NULL)
		return NULL;

	qdisc_bstats_update(sch, skb);
	sch->q.qlen--;

	index = skb->tc_index & (p->indices - 1);
	pr_debug("index %d->%d\n", skb->tc_index, index);

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		ipv4_change_dsfield(ip_hdr(skb), p->mask[index],
				    p->value[index]);
			break;
	case htons(ETH_P_IPV6):
		ipv6_change_dsfield(ipv6_hdr(skb), p->mask[index],
				    p->value[index]);
			break;
	default:
		/*
		 * Only complain if a change was actually attempted.
		 * This way, we can send non-IP traffic through dsmark
		 * and don't need yet another qdisc as a bypass.
		 */
		if (p->mask[index] != 0xff || p->value[index])
			pr_warning("dsmark_dequeue: unsupported protocol %d\n",
				   ntohs(skb->protocol));
		break;
	}

	return skb;
}

static struct sk_buff *dsmark_peek(struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);

	pr_debug("dsmark_peek(sch %p,[qdisc %p])\n", sch, p);

	return p->q->ops->peek(p->q);
}

static unsigned int dsmark_drop(struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	unsigned int len;

	pr_debug("dsmark_reset(sch %p,[qdisc %p])\n", sch, p);

	if (p->q->ops->drop == NULL)
		return 0;

	len = p->q->ops->drop(p->q);
	if (len)
		sch->q.qlen--;

	return len;
}

static int dsmark_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	struct nlattr *tb[TCA_DSMARK_MAX + 1];
	int err = -EINVAL;
	u32 default_index = NO_DEFAULT_INDEX;
	u16 indices;
	u8 *mask;

	pr_debug("dsmark_init(sch %p,[qdisc %p],opt %p)\n", sch, p, opt);

	if (!opt)
		goto errout;

	err = nla_parse_nested(tb, TCA_DSMARK_MAX, opt, dsmark_policy);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	indices = nla_get_u16(tb[TCA_DSMARK_INDICES]);

	if (hweight32(indices) != 1)
		goto errout;

	if (tb[TCA_DSMARK_DEFAULT_INDEX])
		default_index = nla_get_u16(tb[TCA_DSMARK_DEFAULT_INDEX]);

	mask = kmalloc(indices * 2, GFP_KERNEL);
	if (mask == NULL) {
		err = -ENOMEM;
		goto errout;
	}

	p->mask = mask;
	memset(p->mask, 0xff, indices);

	p->value = p->mask + indices;
	memset(p->value, 0, indices);

	p->indices = indices;
	p->default_index = default_index;
	p->set_tc_index = nla_get_flag(tb[TCA_DSMARK_SET_TC_INDEX]);

	p->q = qdisc_create_dflt(sch->dev_queue, &pfifo_qdisc_ops, sch->handle);
	if (p->q == NULL)
		p->q = &noop_qdisc;

	pr_debug("dsmark_init: qdisc %p\n", p->q);

	err = 0;
errout:
	return err;
}

static void dsmark_reset(struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);

	pr_debug("dsmark_reset(sch %p,[qdisc %p])\n", sch, p);
	qdisc_reset(p->q);
	sch->q.qlen = 0;
}

static void dsmark_destroy(struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);

	pr_debug("dsmark_destroy(sch %p,[qdisc %p])\n", sch, p);

	tcf_destroy_chain(&p->filter_list);
	qdisc_destroy(p->q);
	kfree(p->mask);
}

static int dsmark_dump_class(struct Qdisc *sch, unsigned long cl,
			     struct sk_buff *skb, struct tcmsg *tcm)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	struct nlattr *opts = NULL;

	pr_debug("dsmark_dump_class(sch %p,[qdisc %p],class %ld\n", sch, p, cl);

	if (!dsmark_valid_index(p, cl))
		return -EINVAL;

	tcm->tcm_handle = TC_H_MAKE(TC_H_MAJ(sch->handle), cl - 1);
	tcm->tcm_info = p->q->handle;

	opts = nla_nest_start(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;
	if (nla_put_u8(skb, TCA_DSMARK_MASK, p->mask[cl - 1]) ||
	    nla_put_u8(skb, TCA_DSMARK_VALUE, p->value[cl - 1]))
		goto nla_put_failure;

	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -EMSGSIZE;
}

static int dsmark_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct dsmark_qdisc_data *p = qdisc_priv(sch);
	struct nlattr *opts = NULL;

	opts = nla_nest_start(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;
	if (nla_put_u16(skb, TCA_DSMARK_INDICES, p->indices))
		goto nla_put_failure;

	if (p->default_index != NO_DEFAULT_INDEX &&
	    nla_put_u16(skb, TCA_DSMARK_DEFAULT_INDEX, p->default_index))
		goto nla_put_failure;

	if (p->set_tc_index &&
	    nla_put_flag(skb, TCA_DSMARK_SET_TC_INDEX))
		goto nla_put_failure;

	return nla_nest_end(skb, opts);

nla_put_failure:
	nla_nest_cancel(skb, opts);
	return -EMSGSIZE;
}

static const struct Qdisc_class_ops dsmark_class_ops = {
	.graft		=	dsmark_graft,
	.leaf		=	dsmark_leaf,
	.get		=	dsmark_get,
	.put		=	dsmark_put,
	.change		=	dsmark_change,
	.delete		=	dsmark_delete,
	.walk		=	dsmark_walk,
	.tcf_chain	=	dsmark_find_tcf,
	.bind_tcf	=	dsmark_bind_filter,
	.unbind_tcf	=	dsmark_put,
	.dump		=	dsmark_dump_class,
};

static struct Qdisc_ops dsmark_qdisc_ops __read_mostly = {
	.next		=	NULL,
	.cl_ops		=	&dsmark_class_ops,
	.id		=	"dsmark",
	.priv_size	=	sizeof(struct dsmark_qdisc_data),
	.enqueue	=	dsmark_enqueue,
	.dequeue	=	dsmark_dequeue,
	.peek		=	dsmark_peek,
	.drop		=	dsmark_drop,
	.init		=	dsmark_init,
	.reset		=	dsmark_reset,
	.destroy	=	dsmark_destroy,
	.change		=	NULL,
	.dump		=	dsmark_dump,
	.owner		=	THIS_MODULE,
};

static int __init dsmark_module_init(void)
{
	return register_qdisc(&dsmark_qdisc_ops);
}

static void __exit dsmark_module_exit(void)
{
	unregister_qdisc(&dsmark_qdisc_ops);
}

module_init(dsmark_module_init)
module_exit(dsmark_module_exit)

MODULE_LICENSE("GPL");
