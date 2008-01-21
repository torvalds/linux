/* net/sched/sch_ingress.c - Ingress qdisc
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Authors:     Jamal Hadi Salim 1999
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>


/* Thanks to Doron Oz for this hack */
#ifndef CONFIG_NET_CLS_ACT
#ifdef CONFIG_NETFILTER
static int nf_registered;
#endif
#endif

struct ingress_qdisc_data {
	struct Qdisc		*q;
	struct tcf_proto	*filter_list;
};

/* ------------------------- Class/flow operations ------------------------- */

static int ingress_graft(struct Qdisc *sch, unsigned long arg,
			 struct Qdisc *new, struct Qdisc **old)
{
	return 1;
}

static struct Qdisc *ingress_leaf(struct Qdisc *sch, unsigned long arg)
{
	return NULL;
}

static unsigned long ingress_get(struct Qdisc *sch, u32 classid)
{
	return TC_H_MIN(classid) + 1;
}

static unsigned long ingress_bind_filter(struct Qdisc *sch,
					 unsigned long parent, u32 classid)
{
	return ingress_get(sch, classid);
}

static void ingress_put(struct Qdisc *sch, unsigned long cl)
{
}

static int ingress_change(struct Qdisc *sch, u32 classid, u32 parent,
			  struct rtattr **tca, unsigned long *arg)
{
	return 0;
}

static void ingress_walk(struct Qdisc *sch, struct qdisc_walker *walker)
{
	return;
}

static struct tcf_proto **ingress_find_tcf(struct Qdisc *sch, unsigned long cl)
{
	struct ingress_qdisc_data *p = qdisc_priv(sch);

	return &p->filter_list;
}

/* --------------------------- Qdisc operations ---------------------------- */

static int ingress_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct ingress_qdisc_data *p = qdisc_priv(sch);
	struct tcf_result res;
	int result;

	result = tc_classify(skb, p->filter_list, &res);

	/*
	 * Unlike normal "enqueue" functions, ingress_enqueue returns a
	 * firewall FW_* code.
	 */
#ifdef CONFIG_NET_CLS_ACT
	sch->bstats.packets++;
	sch->bstats.bytes += skb->len;
	switch (result) {
	case TC_ACT_SHOT:
		result = TC_ACT_SHOT;
		sch->qstats.drops++;
		break;
	case TC_ACT_STOLEN:
	case TC_ACT_QUEUED:
		result = TC_ACT_STOLEN;
		break;
	case TC_ACT_RECLASSIFY:
	case TC_ACT_OK:
		skb->tc_index = TC_H_MIN(res.classid);
	default:
		result = TC_ACT_OK;
		break;
	}
#else
	result = NF_ACCEPT;
	sch->bstats.packets++;
	sch->bstats.bytes += skb->len;
#endif

	return result;
}

static struct sk_buff *ingress_dequeue(struct Qdisc *sch)
{
	return NULL;
}

static int ingress_requeue(struct sk_buff *skb, struct Qdisc *sch)
{
	return 0;
}

static unsigned int ingress_drop(struct Qdisc *sch)
{
	return 0;
}

#ifndef CONFIG_NET_CLS_ACT
#ifdef CONFIG_NETFILTER
static unsigned int ing_hook(unsigned int hook, struct sk_buff *skb,
			     const struct net_device *indev,
			     const struct net_device *outdev,
			     int (*okfn)(struct sk_buff *))
{

	struct Qdisc *q;
	struct net_device *dev = skb->dev;
	int fwres = NF_ACCEPT;

	if (dev->qdisc_ingress) {
		spin_lock(&dev->ingress_lock);
		if ((q = dev->qdisc_ingress) != NULL)
			fwres = q->enqueue(skb, q);
		spin_unlock(&dev->ingress_lock);
	}

	return fwres;
}

/* after ipt_filter */
static struct nf_hook_ops ing_ops[] __read_mostly = {
	{
		.hook           = ing_hook,
		.owner		= THIS_MODULE,
		.pf             = PF_INET,
		.hooknum        = NF_INET_PRE_ROUTING,
		.priority       = NF_IP_PRI_FILTER + 1,
	},
	{
		.hook           = ing_hook,
		.owner		= THIS_MODULE,
		.pf             = PF_INET6,
		.hooknum        = NF_INET_PRE_ROUTING,
		.priority       = NF_IP6_PRI_FILTER + 1,
	},
};
#endif
#endif

static int ingress_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct ingress_qdisc_data *p = qdisc_priv(sch);

	/* Make sure either netfilter or preferably CLS_ACT is
	 * compiled in */
#ifndef CONFIG_NET_CLS_ACT
#ifndef CONFIG_NETFILTER
	printk("You MUST compile classifier actions into the kernel\n");
	return -EINVAL;
#else
	printk("Ingress scheduler: Classifier actions prefered over netfilter\n");
#endif
#endif

#ifndef CONFIG_NET_CLS_ACT
#ifdef CONFIG_NETFILTER
	if (!nf_registered) {
		if (nf_register_hooks(ing_ops, ARRAY_SIZE(ing_ops)) < 0) {
			printk("ingress qdisc registration error \n");
			return -EINVAL;
		}
		nf_registered++;
	}
#endif
#endif
	p->q = &noop_qdisc;
	return 0;
}

static void ingress_reset(struct Qdisc *sch)
{
	return;
}

/* ------------------------------------------------------------- */

static void ingress_destroy(struct Qdisc *sch)
{
	struct ingress_qdisc_data *p = qdisc_priv(sch);

	tcf_destroy_chain(p->filter_list);
}

static int ingress_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct rtattr *rta;

	rta = (struct rtattr *)b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);
	rta->rta_len = skb_tail_pointer(skb) - b;
	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static const struct Qdisc_class_ops ingress_class_ops = {
	.graft		=	ingress_graft,
	.leaf		=	ingress_leaf,
	.get		=	ingress_get,
	.put		=	ingress_put,
	.change		=	ingress_change,
	.walk		=	ingress_walk,
	.tcf_chain	=	ingress_find_tcf,
	.bind_tcf	=	ingress_bind_filter,
	.unbind_tcf	=	ingress_put,
};

static struct Qdisc_ops ingress_qdisc_ops __read_mostly = {
	.cl_ops		=	&ingress_class_ops,
	.id		=	"ingress",
	.priv_size	=	sizeof(struct ingress_qdisc_data),
	.enqueue	=	ingress_enqueue,
	.dequeue	=	ingress_dequeue,
	.requeue	=	ingress_requeue,
	.drop		=	ingress_drop,
	.init		=	ingress_init,
	.reset		=	ingress_reset,
	.destroy	=	ingress_destroy,
	.dump		=	ingress_dump,
	.owner		=	THIS_MODULE,
};

static int __init ingress_module_init(void)
{
	int ret = 0;

	if ((ret = register_qdisc(&ingress_qdisc_ops)) < 0) {
		printk("Unable to register Ingress qdisc\n");
		return ret;
	}

	return ret;
}

static void __exit ingress_module_exit(void)
{
	unregister_qdisc(&ingress_qdisc_ops);
#ifndef CONFIG_NET_CLS_ACT
#ifdef CONFIG_NETFILTER
	if (nf_registered)
		nf_unregister_hooks(ing_ops, ARRAY_SIZE(ing_ops));
#endif
#endif
}

module_init(ingress_module_init)
module_exit(ingress_module_exit)
MODULE_LICENSE("GPL");
