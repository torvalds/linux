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
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter.h>
#include <linux/smp.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <linux/kmod.h>
#include <linux/stat.h>
#include <linux/interrupt.h>
#include <linux/list.h>


#undef DEBUG_INGRESS

#ifdef DEBUG_INGRESS  /* control */
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

#if 0  /* data */
#define D2PRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define D2PRINTK(format,args...)
#endif


#define PRIV(sch) qdisc_priv(sch)


/* Thanks to Doron Oz for this hack
*/
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


static int ingress_graft(struct Qdisc *sch,unsigned long arg,
    struct Qdisc *new,struct Qdisc **old)
{
#ifdef DEBUG_INGRESS
	struct ingress_qdisc_data *p = PRIV(sch);
#endif

	DPRINTK("ingress_graft(sch %p,[qdisc %p],new %p,old %p)\n",
		sch, p, new, old);
	DPRINTK("\n ingress_graft: You cannot add qdiscs to classes");
	return 1;
}


static struct Qdisc *ingress_leaf(struct Qdisc *sch, unsigned long arg)
{
	return NULL;
}


static unsigned long ingress_get(struct Qdisc *sch,u32 classid)
{
#ifdef DEBUG_INGRESS
	struct ingress_qdisc_data *p = PRIV(sch);
#endif
	DPRINTK("ingress_get(sch %p,[qdisc %p],classid %x)\n", sch, p, classid);
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
#ifdef DEBUG_INGRESS
	struct ingress_qdisc_data *p = PRIV(sch);
#endif
	DPRINTK("ingress_change(sch %p,[qdisc %p],classid %x,parent %x),"
		"arg 0x%lx\n", sch, p, classid, parent, *arg);
	DPRINTK("No effect. sch_ingress doesn't maintain classes at the moment");
	return 0;
}



static void ingress_walk(struct Qdisc *sch,struct qdisc_walker *walker)
{
#ifdef DEBUG_INGRESS
	struct ingress_qdisc_data *p = PRIV(sch);
#endif
	DPRINTK("ingress_walk(sch %p,[qdisc %p],walker %p)\n", sch, p, walker);
	DPRINTK("No effect. sch_ingress doesn't maintain classes at the moment");
}


static struct tcf_proto **ingress_find_tcf(struct Qdisc *sch,unsigned long cl)
{
	struct ingress_qdisc_data *p = PRIV(sch);

	return &p->filter_list;
}


/* --------------------------- Qdisc operations ---------------------------- */


static int ingress_enqueue(struct sk_buff *skb,struct Qdisc *sch)
{
	struct ingress_qdisc_data *p = PRIV(sch);
	struct tcf_result res;
	int result;

	D2PRINTK("ingress_enqueue(skb %p,sch %p,[qdisc %p])\n", skb, sch, p);
	result = tc_classify(skb, p->filter_list, &res);
	D2PRINTK("result %d class 0x%04x\n", result, res.classid);
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
		case TC_ACT_UNSPEC:
		default:
			skb->tc_index = TC_H_MIN(res.classid);
			result = TC_ACT_OK;
			break;
	}
/* backward compat */
#else
#ifdef	CONFIG_NET_CLS_POLICE
	switch (result) {
		case TC_POLICE_SHOT:
		result = NF_DROP;
		sch->qstats.drops++;
		break;
		case TC_POLICE_RECLASSIFY: /* DSCP remarking here ? */
		case TC_POLICE_OK:
		case TC_POLICE_UNSPEC:
		default:
		sch->bstats.packets++;
		sch->bstats.bytes += skb->len;
		result = NF_ACCEPT;
		break;
	}

#else
	D2PRINTK("Overriding result to ACCEPT\n");
	result = NF_ACCEPT;
	sch->bstats.packets++;
	sch->bstats.bytes += skb->len;
#endif
#endif

	return result;
}


static struct sk_buff *ingress_dequeue(struct Qdisc *sch)
{
/*
	struct ingress_qdisc_data *p = PRIV(sch);
	D2PRINTK("ingress_dequeue(sch %p,[qdisc %p])\n",sch,PRIV(p));
*/
	return NULL;
}


static int ingress_requeue(struct sk_buff *skb,struct Qdisc *sch)
{
/*
	struct ingress_qdisc_data *p = PRIV(sch);
	D2PRINTK("ingress_requeue(skb %p,sch %p,[qdisc %p])\n",skb,sch,PRIV(p));
*/
	return 0;
}

static unsigned int ingress_drop(struct Qdisc *sch)
{
#ifdef DEBUG_INGRESS
	struct ingress_qdisc_data *p = PRIV(sch);
#endif
	DPRINTK("ingress_drop(sch %p,[qdisc %p])\n", sch, p);
	return 0;
}

#ifndef CONFIG_NET_CLS_ACT
#ifdef CONFIG_NETFILTER
static unsigned int
ing_hook(unsigned int hook, struct sk_buff **pskb,
			     const struct net_device *indev,
			     const struct net_device *outdev,
			     int (*okfn)(struct sk_buff *))
{

	struct Qdisc *q;
	struct sk_buff *skb = *pskb;
	struct net_device *dev = skb->dev;
	int fwres=NF_ACCEPT;

	DPRINTK("ing_hook: skb %s dev=%s len=%u\n",
		skb->sk ? "(owned)" : "(unowned)",
		skb->dev ? (*pskb)->dev->name : "(no dev)",
		skb->len);

	if (dev->qdisc_ingress) {
		spin_lock(&dev->ingress_lock);
		if ((q = dev->qdisc_ingress) != NULL)
			fwres = q->enqueue(skb, q);
		spin_unlock(&dev->ingress_lock);
	}

	return fwres;
}

/* after ipt_filter */
static struct nf_hook_ops ing_ops = {
	.hook           = ing_hook,
	.owner		= THIS_MODULE,
	.pf             = PF_INET,
	.hooknum        = NF_IP_PRE_ROUTING,
	.priority       = NF_IP_PRI_FILTER + 1,
};

static struct nf_hook_ops ing6_ops = {
	.hook           = ing_hook,
	.owner		= THIS_MODULE,
	.pf             = PF_INET6,
	.hooknum        = NF_IP6_PRE_ROUTING,
	.priority       = NF_IP6_PRI_FILTER + 1,
};

#endif
#endif

static int ingress_init(struct Qdisc *sch,struct rtattr *opt)
{
	struct ingress_qdisc_data *p = PRIV(sch);

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
		if (nf_register_hook(&ing_ops) < 0) {
			printk("ingress qdisc registration error \n");
			return -EINVAL;
		}
		nf_registered++;

		if (nf_register_hook(&ing6_ops) < 0) {
			printk("IPv6 ingress qdisc registration error, " \
			    "disabling IPv6 support.\n");
		} else
			nf_registered++;
	}
#endif
#endif

	DPRINTK("ingress_init(sch %p,[qdisc %p],opt %p)\n",sch,p,opt);
	p->q = &noop_qdisc;
	return 0;
}


static void ingress_reset(struct Qdisc *sch)
{
	struct ingress_qdisc_data *p = PRIV(sch);

	DPRINTK("ingress_reset(sch %p,[qdisc %p])\n", sch, p);

/*
#if 0
*/
/* for future use */
	qdisc_reset(p->q);
/*
#endif
*/
}

/* ------------------------------------------------------------- */


/* ------------------------------------------------------------- */

static void ingress_destroy(struct Qdisc *sch)
{
	struct ingress_qdisc_data *p = PRIV(sch);

	DPRINTK("ingress_destroy(sch %p,[qdisc %p])\n", sch, p);
	tcf_destroy_chain(p->filter_list);
#if 0
/* for future use */
	qdisc_destroy(p->q);
#endif
}


static int ingress_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct rtattr *rta;

	rta = (struct rtattr *) b;
	RTA_PUT(skb, TCA_OPTIONS, 0, NULL);
	rta->rta_len = skb_tail_pointer(skb) - b;
	return skb->len;

rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static struct Qdisc_class_ops ingress_class_ops = {
	.graft		=	ingress_graft,
	.leaf		=	ingress_leaf,
	.get		=	ingress_get,
	.put		=	ingress_put,
	.change		=	ingress_change,
	.delete		=	NULL,
	.walk		=	ingress_walk,
	.tcf_chain	=	ingress_find_tcf,
	.bind_tcf	=	ingress_bind_filter,
	.unbind_tcf	=	ingress_put,
	.dump		=	NULL,
};

static struct Qdisc_ops ingress_qdisc_ops = {
	.next		=	NULL,
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
	.change		=	NULL,
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
	if (nf_registered) {
		nf_unregister_hook(&ing_ops);
		if (nf_registered > 1)
			nf_unregister_hook(&ing6_ops);
	}
#endif
#endif
}
module_init(ingress_module_init)
module_exit(ingress_module_exit)
MODULE_LICENSE("GPL");
