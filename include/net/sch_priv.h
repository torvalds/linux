/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_SCHED_PRIV_H
#define __NET_SCHED_PRIV_H

#include <net/sch_generic.h>

struct mq_sched {
	struct Qdisc		**qdiscs;
};

int mq_init_common(struct Qdisc *sch, struct nlattr *opt,
		   struct netlink_ext_ack *extack,
		   const struct Qdisc_ops *qdisc_ops);
void mq_destroy_common(struct Qdisc *sch);
void mq_attach(struct Qdisc *sch);
void mq_dump_common(struct Qdisc *sch, struct sk_buff *skb);
struct netdev_queue *mq_select_queue(struct Qdisc *sch,
				     struct tcmsg *tcm);
struct Qdisc *mq_leaf(struct Qdisc *sch, unsigned long cl);
unsigned long mq_find(struct Qdisc *sch, u32 classid);
int mq_dump_class(struct Qdisc *sch, unsigned long cl,
		  struct sk_buff *skb, struct tcmsg *tcm);
int mq_dump_class_stats(struct Qdisc *sch, unsigned long cl,
			struct gnet_dump *d);
void mq_walk(struct Qdisc *sch, struct qdisc_walker *arg);

#endif
