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

#include <net/netlink.h>
#include <net/pkt_sched.h>

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

static void ingress_walk(struct Qdisc *sch, struct qdisc_walker *walker)
{
}

static struct tcf_proto __rcu **ingress_find_tcf(struct Qdisc *sch,
						 unsigned long cl)
{
	struct net_device *dev = qdisc_dev(sch);

	return &dev->ingress_cl_list;
}

static int ingress_init(struct Qdisc *sch, struct nlattr *opt)
{
	net_inc_ingress_queue();
	sch->flags |= TCQ_F_CPUSTATS;

	return 0;
}

static void ingress_destroy(struct Qdisc *sch)
{
	struct net_device *dev = qdisc_dev(sch);

	tcf_destroy_chain(&dev->ingress_cl_list);
	net_dec_ingress_queue();
}

static int ingress_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct nlattr *nest;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	return nla_nest_end(skb, nest);

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static const struct Qdisc_class_ops ingress_class_ops = {
	.leaf		=	ingress_leaf,
	.get		=	ingress_get,
	.put		=	ingress_put,
	.walk		=	ingress_walk,
	.tcf_chain	=	ingress_find_tcf,
	.bind_tcf	=	ingress_bind_filter,
	.unbind_tcf	=	ingress_put,
};

static struct Qdisc_ops ingress_qdisc_ops __read_mostly = {
	.cl_ops		=	&ingress_class_ops,
	.id		=	"ingress",
	.init		=	ingress_init,
	.destroy	=	ingress_destroy,
	.dump		=	ingress_dump,
	.owner		=	THIS_MODULE,
};

static int __init ingress_module_init(void)
{
	return register_qdisc(&ingress_qdisc_ops);
}

static void __exit ingress_module_exit(void)
{
	unregister_qdisc(&ingress_qdisc_ops);
}

module_init(ingress_module_init);
module_exit(ingress_module_exit);

MODULE_LICENSE("GPL");
