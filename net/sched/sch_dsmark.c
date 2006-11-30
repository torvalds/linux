/* net/sched/sch_dsmark.c - Differentiated Services field marker */

/* Written 1998-2000 by Werner Almesberger, EPFL ICA */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h> /* for pkt_sched */
#include <linux/rtnetlink.h>
#include <net/pkt_sched.h>
#include <net/dsfield.h>
#include <net/inet_ecn.h>
#include <asm/byteorder.h>


#if 0 /* control */
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

#if 0 /* data */
#define D2PRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define D2PRINTK(format,args...)
#endif


#define PRIV(sch) ((struct dsmark_qdisc_data *) qdisc_priv(sch))


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

static inline int dsmark_valid_indices(u16 indices)
{
	while (indices != 1) {
		if (indices & 1)
			return 0;
		indices >>= 1;
	}
 
	return 1;
}

static inline int dsmark_valid_index(struct dsmark_qdisc_data *p, u16 index)
{
	return (index <= p->indices && index > 0);
}

/* ------------------------- Class/flow operations ------------------------- */

static int dsmark_graft(struct Qdisc *sch, unsigned long arg,
			struct Qdisc *new, struct Qdisc **old)
{
	struct dsmark_qdisc_data *p = PRIV(sch);

	DPRINTK("dsmark_graft(sch %p,[qdisc %p],new %p,old %p)\n",
		sch, p, new, old);

	if (new == NULL) {
		new = qdisc_create_dflt(sch->dev, &pfifo_qdisc_ops,
					sch->handle);
		if (new == NULL)
			new = &noop_qdisc;
	}

	sch_tree_lock(sch);
	*old = xchg(&p->q, new);
	qdisc_reset(*old);
	sch->q.qlen = 0;
	sch_tree_unlock(sch);

        return 0;
}

static struct Qdisc *dsmark_leaf(struct Qdisc *sch, unsigned long arg)
{
	return PRIV(sch)->q;
}

static unsigned long dsmark_get(struct Qdisc *sch, u32 classid)
{
	DPRINTK("dsmark_get(sch %p,[qdisc %p],classid %x)\n",
		sch, PRIV(sch), classid);

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

static int dsmark_change(struct Qdisc *sch, u32 classid, u32 parent,
			 struct rtattr **tca, unsigned long *arg)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	struct rtattr *opt = tca[TCA_OPTIONS-1];
	struct rtattr *tb[TCA_DSMARK_MAX];
	int err = -EINVAL;
	u8 mask = 0;

	DPRINTK("dsmark_change(sch %p,[qdisc %p],classid %x,parent %x),"
		"arg 0x%lx\n", sch, p, classid, parent, *arg);

	if (!dsmark_valid_index(p, *arg)) {
		err = -ENOENT;
		goto rtattr_failure;
	}

	if (!opt || rtattr_parse_nested(tb, TCA_DSMARK_MAX, opt))
		goto rtattr_failure;

	if (tb[TCA_DSMARK_MASK-1])
		mask = RTA_GET_U8(tb[TCA_DSMARK_MASK-1]);

	if (tb[TCA_DSMARK_VALUE-1])
		p->value[*arg-1] = RTA_GET_U8(tb[TCA_DSMARK_VALUE-1]);
		
	if (tb[TCA_DSMARK_MASK-1])
		p->mask[*arg-1] = mask;

	err = 0;

rtattr_failure:
	return err;
}

static int dsmark_delete(struct Qdisc *sch, unsigned long arg)
{
	struct dsmark_qdisc_data *p = PRIV(sch);

	if (!dsmark_valid_index(p, arg))
		return -EINVAL;
	
	p->mask[arg-1] = 0xff;
	p->value[arg-1] = 0;

	return 0;
}

static void dsmark_walk(struct Qdisc *sch,struct qdisc_walker *walker)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	int i;

	DPRINTK("dsmark_walk(sch %p,[qdisc %p],walker %p)\n", sch, p, walker);

	if (walker->stop)
		return;

	for (i = 0; i < p->indices; i++) {
		if (p->mask[i] == 0xff && !p->value[i])
			goto ignore;
		if (walker->count >= walker->skip) {
			if (walker->fn(sch, i+1, walker) < 0) {
				walker->stop = 1;
				break;
			}
		}
ignore:		
		walker->count++;
        }
}

static struct tcf_proto **dsmark_find_tcf(struct Qdisc *sch,unsigned long cl)
{
	return &PRIV(sch)->filter_list;
}

/* --------------------------- Qdisc operations ---------------------------- */

static int dsmark_enqueue(struct sk_buff *skb,struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	int err;

	D2PRINTK("dsmark_enqueue(skb %p,sch %p,[qdisc %p])\n", skb, sch, p);

	if (p->set_tc_index) {
		/* FIXME: Safe with non-linear skbs? --RR */
		switch (skb->protocol) {
			case __constant_htons(ETH_P_IP):
				skb->tc_index = ipv4_get_dsfield(skb->nh.iph)
					& ~INET_ECN_MASK;
				break;
			case __constant_htons(ETH_P_IPV6):
				skb->tc_index = ipv6_get_dsfield(skb->nh.ipv6h)
					& ~INET_ECN_MASK;
				break;
			default:
				skb->tc_index = 0;
				break;
		};
	}

	if (TC_H_MAJ(skb->priority) == sch->handle)
		skb->tc_index = TC_H_MIN(skb->priority);
	else {
		struct tcf_result res;
		int result = tc_classify(skb, p->filter_list, &res);

		D2PRINTK("result %d class 0x%04x\n", result, res.classid);

		switch (result) {
#ifdef CONFIG_NET_CLS_POLICE
			case TC_POLICE_SHOT:
				kfree_skb(skb);
				sch->qstats.drops++;
				return NET_XMIT_POLICED;
#if 0
			case TC_POLICE_RECLASSIFY:
				/* FIXME: what to do here ??? */
#endif
#endif
			case TC_POLICE_OK:
				skb->tc_index = TC_H_MIN(res.classid);
				break;
			case TC_POLICE_UNSPEC:
				/* fall through */
			default:
				if (p->default_index != NO_DEFAULT_INDEX)
					skb->tc_index = p->default_index;
				break;
		};
	}

	err = p->q->enqueue(skb,p->q);
	if (err != NET_XMIT_SUCCESS) {
		sch->qstats.drops++;
		return err;
	}

	sch->bstats.bytes += skb->len;
	sch->bstats.packets++;
	sch->q.qlen++;

	return NET_XMIT_SUCCESS;
}

static struct sk_buff *dsmark_dequeue(struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	struct sk_buff *skb;
	u32 index;

	D2PRINTK("dsmark_dequeue(sch %p,[qdisc %p])\n", sch, p);

	skb = p->q->ops->dequeue(p->q);
	if (skb == NULL)
		return NULL;

	sch->q.qlen--;

	index = skb->tc_index & (p->indices - 1);
	D2PRINTK("index %d->%d\n", skb->tc_index, index);

	switch (skb->protocol) {
		case __constant_htons(ETH_P_IP):
			ipv4_change_dsfield(skb->nh.iph, p->mask[index],
					    p->value[index]);
			break;
		case __constant_htons(ETH_P_IPV6):
			ipv6_change_dsfield(skb->nh.ipv6h, p->mask[index],
					    p->value[index]);
			break;
		default:
			/*
			 * Only complain if a change was actually attempted.
			 * This way, we can send non-IP traffic through dsmark
			 * and don't need yet another qdisc as a bypass.
			 */
			if (p->mask[index] != 0xff || p->value[index])
				printk(KERN_WARNING "dsmark_dequeue: "
				       "unsupported protocol %d\n",
				       ntohs(skb->protocol));
			break;
	};

	return skb;
}

static int dsmark_requeue(struct sk_buff *skb,struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	int err;

	D2PRINTK("dsmark_requeue(skb %p,sch %p,[qdisc %p])\n", skb, sch, p);

	err = p->q->ops->requeue(skb, p->q);
	if (err != NET_XMIT_SUCCESS) {
		sch->qstats.drops++;
		return err;
	}

	sch->q.qlen++;
	sch->qstats.requeues++;

	return NET_XMIT_SUCCESS;
}

static unsigned int dsmark_drop(struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	unsigned int len;
	
	DPRINTK("dsmark_reset(sch %p,[qdisc %p])\n", sch, p);

	if (p->q->ops->drop == NULL)
		return 0;

	len = p->q->ops->drop(p->q);
	if (len)
		sch->q.qlen--;

	return len;
}

static int dsmark_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	struct rtattr *tb[TCA_DSMARK_MAX];
	int err = -EINVAL;
	u32 default_index = NO_DEFAULT_INDEX;
	u16 indices;
	u8 *mask;

	DPRINTK("dsmark_init(sch %p,[qdisc %p],opt %p)\n", sch, p, opt);

	if (!opt || rtattr_parse_nested(tb, TCA_DSMARK_MAX, opt) < 0)
		goto errout;

	indices = RTA_GET_U16(tb[TCA_DSMARK_INDICES-1]);
	if (!indices || !dsmark_valid_indices(indices))
		goto errout;

	if (tb[TCA_DSMARK_DEFAULT_INDEX-1])
		default_index = RTA_GET_U16(tb[TCA_DSMARK_DEFAULT_INDEX-1]);

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
	p->set_tc_index = RTA_GET_FLAG(tb[TCA_DSMARK_SET_TC_INDEX-1]);

	p->q = qdisc_create_dflt(sch->dev, &pfifo_qdisc_ops, sch->handle);
	if (p->q == NULL)
		p->q = &noop_qdisc;

	DPRINTK("dsmark_init: qdisc %p\n", p->q);

	err = 0;
errout:
rtattr_failure:
	return err;
}

static void dsmark_reset(struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = PRIV(sch);

	DPRINTK("dsmark_reset(sch %p,[qdisc %p])\n", sch, p);
	qdisc_reset(p->q);
	sch->q.qlen = 0;
}

static void dsmark_destroy(struct Qdisc *sch)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	struct tcf_proto *tp;

	DPRINTK("dsmark_destroy(sch %p,[qdisc %p])\n", sch, p);

	while (p->filter_list) {
		tp = p->filter_list;
		p->filter_list = tp->next;
		tcf_destroy(tp);
	}

	qdisc_destroy(p->q);
	kfree(p->mask);
}

static int dsmark_dump_class(struct Qdisc *sch, unsigned long cl,
			     struct sk_buff *skb, struct tcmsg *tcm)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	struct rtattr *opts = NULL;

	DPRINTK("dsmark_dump_class(sch %p,[qdisc %p],class %ld\n", sch, p, cl);

	if (!dsmark_valid_index(p, cl))
		return -EINVAL;

	tcm->tcm_handle = TC_H_MAKE(TC_H_MAJ(sch->handle), cl-1);
	tcm->tcm_info = p->q->handle;

	opts = RTA_NEST(skb, TCA_OPTIONS);
	RTA_PUT_U8(skb,TCA_DSMARK_MASK, p->mask[cl-1]);
	RTA_PUT_U8(skb,TCA_DSMARK_VALUE, p->value[cl-1]);

	return RTA_NEST_END(skb, opts);

rtattr_failure:
	return RTA_NEST_CANCEL(skb, opts);
}

static int dsmark_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct dsmark_qdisc_data *p = PRIV(sch);
	struct rtattr *opts = NULL;

	opts = RTA_NEST(skb, TCA_OPTIONS);
	RTA_PUT_U16(skb, TCA_DSMARK_INDICES, p->indices);

	if (p->default_index != NO_DEFAULT_INDEX)
		RTA_PUT_U16(skb, TCA_DSMARK_DEFAULT_INDEX, p->default_index);

	if (p->set_tc_index)
		RTA_PUT_FLAG(skb, TCA_DSMARK_SET_TC_INDEX);

	return RTA_NEST_END(skb, opts);

rtattr_failure:
	return RTA_NEST_CANCEL(skb, opts);
}

static struct Qdisc_class_ops dsmark_class_ops = {
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

static struct Qdisc_ops dsmark_qdisc_ops = {
	.next		=	NULL,
	.cl_ops		=	&dsmark_class_ops,
	.id		=	"dsmark",
	.priv_size	=	sizeof(struct dsmark_qdisc_data),
	.enqueue	=	dsmark_enqueue,
	.dequeue	=	dsmark_dequeue,
	.requeue	=	dsmark_requeue,
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
