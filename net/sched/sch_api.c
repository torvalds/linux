/*
 * net/sched/sch_api.c	Packet scheduler API.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Fixes:
 *
 * Rani Assaf <rani@magic.metawire.com> :980802: JIFFIES and CPU clock sources are repaired.
 * Eduardo J. Blanco <ejbs@netlabs.com.uy> :990222: kmod support
 * Jamal Hadi Salim <hadi@nortelnetworks.com>: 990601: ingress support
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kmod.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <linux/hrtimer.h>

#include <net/netlink.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/system.h>

static int qdisc_notify(struct sk_buff *oskb, struct nlmsghdr *n, u32 clid,
			struct Qdisc *old, struct Qdisc *new);
static int tclass_notify(struct sk_buff *oskb, struct nlmsghdr *n,
			 struct Qdisc *q, unsigned long cl, int event);

/*

   Short review.
   -------------

   This file consists of two interrelated parts:

   1. queueing disciplines manager frontend.
   2. traffic classes manager frontend.

   Generally, queueing discipline ("qdisc") is a black box,
   which is able to enqueue packets and to dequeue them (when
   device is ready to send something) in order and at times
   determined by algorithm hidden in it.

   qdisc's are divided to two categories:
   - "queues", which have no internal structure visible from outside.
   - "schedulers", which split all the packets to "traffic classes",
     using "packet classifiers" (look at cls_api.c)

   In turn, classes may have child qdiscs (as rule, queues)
   attached to them etc. etc. etc.

   The goal of the routines in this file is to translate
   information supplied by user in the form of handles
   to more intelligible for kernel form, to make some sanity
   checks and part of work, which is common to all qdiscs
   and to provide rtnetlink notifications.

   All real intelligent work is done inside qdisc modules.



   Every discipline has two major routines: enqueue and dequeue.

   ---dequeue

   dequeue usually returns a skb to send. It is allowed to return NULL,
   but it does not mean that queue is empty, it just means that
   discipline does not want to send anything this time.
   Queue is really empty if q->q.qlen == 0.
   For complicated disciplines with multiple queues q->q is not
   real packet queue, but however q->q.qlen must be valid.

   ---enqueue

   enqueue returns 0, if packet was enqueued successfully.
   If packet (this one or another one) was dropped, it returns
   not zero error code.
   NET_XMIT_DROP 	- this packet dropped
     Expected action: do not backoff, but wait until queue will clear.
   NET_XMIT_CN	 	- probably this packet enqueued, but another one dropped.
     Expected action: backoff or ignore
   NET_XMIT_POLICED	- dropped by police.
     Expected action: backoff or error to real-time apps.

   Auxiliary routines:

   ---requeue

   requeues once dequeued packet. It is used for non-standard or
   just buggy devices, which can defer output even if dev->tbusy=0.

   ---reset

   returns qdisc to initial state: purge all buffers, clear all
   timers, counters (except for statistics) etc.

   ---init

   initializes newly created qdisc.

   ---destroy

   destroys resources allocated by init and during lifetime of qdisc.

   ---change

   changes qdisc parameters.
 */

/* Protects list of registered TC modules. It is pure SMP lock. */
static DEFINE_RWLOCK(qdisc_mod_lock);


/************************************************
 *	Queueing disciplines manipulation.	*
 ************************************************/


/* The list of all installed queueing disciplines. */

static struct Qdisc_ops *qdisc_base;

/* Register/uregister queueing discipline */

int register_qdisc(struct Qdisc_ops *qops)
{
	struct Qdisc_ops *q, **qp;
	int rc = -EEXIST;

	write_lock(&qdisc_mod_lock);
	for (qp = &qdisc_base; (q = *qp) != NULL; qp = &q->next)
		if (!strcmp(qops->id, q->id))
			goto out;

	if (qops->enqueue == NULL)
		qops->enqueue = noop_qdisc_ops.enqueue;
	if (qops->requeue == NULL)
		qops->requeue = noop_qdisc_ops.requeue;
	if (qops->dequeue == NULL)
		qops->dequeue = noop_qdisc_ops.dequeue;

	qops->next = NULL;
	*qp = qops;
	rc = 0;
out:
	write_unlock(&qdisc_mod_lock);
	return rc;
}

int unregister_qdisc(struct Qdisc_ops *qops)
{
	struct Qdisc_ops *q, **qp;
	int err = -ENOENT;

	write_lock(&qdisc_mod_lock);
	for (qp = &qdisc_base; (q=*qp)!=NULL; qp = &q->next)
		if (q == qops)
			break;
	if (q) {
		*qp = q->next;
		q->next = NULL;
		err = 0;
	}
	write_unlock(&qdisc_mod_lock);
	return err;
}

/* We know handle. Find qdisc among all qdisc's attached to device
   (root qdisc, all its children, children of children etc.)
 */

struct Qdisc *qdisc_lookup(struct net_device *dev, u32 handle)
{
	struct Qdisc *q;

	list_for_each_entry(q, &dev->qdisc_list, list) {
		if (q->handle == handle)
			return q;
	}
	return NULL;
}

static struct Qdisc *qdisc_leaf(struct Qdisc *p, u32 classid)
{
	unsigned long cl;
	struct Qdisc *leaf;
	struct Qdisc_class_ops *cops = p->ops->cl_ops;

	if (cops == NULL)
		return NULL;
	cl = cops->get(p, classid);

	if (cl == 0)
		return NULL;
	leaf = cops->leaf(p, cl);
	cops->put(p, cl);
	return leaf;
}

/* Find queueing discipline by name */

static struct Qdisc_ops *qdisc_lookup_ops(struct rtattr *kind)
{
	struct Qdisc_ops *q = NULL;

	if (kind) {
		read_lock(&qdisc_mod_lock);
		for (q = qdisc_base; q; q = q->next) {
			if (rtattr_strcmp(kind, q->id) == 0) {
				if (!try_module_get(q->owner))
					q = NULL;
				break;
			}
		}
		read_unlock(&qdisc_mod_lock);
	}
	return q;
}

static struct qdisc_rate_table *qdisc_rtab_list;

struct qdisc_rate_table *qdisc_get_rtab(struct tc_ratespec *r, struct rtattr *tab)
{
	struct qdisc_rate_table *rtab;

	for (rtab = qdisc_rtab_list; rtab; rtab = rtab->next) {
		if (memcmp(&rtab->rate, r, sizeof(struct tc_ratespec)) == 0) {
			rtab->refcnt++;
			return rtab;
		}
	}

	if (tab == NULL || r->rate == 0 || r->cell_log == 0 || RTA_PAYLOAD(tab) != 1024)
		return NULL;

	rtab = kmalloc(sizeof(*rtab), GFP_KERNEL);
	if (rtab) {
		rtab->rate = *r;
		rtab->refcnt = 1;
		memcpy(rtab->data, RTA_DATA(tab), 1024);
		rtab->next = qdisc_rtab_list;
		qdisc_rtab_list = rtab;
	}
	return rtab;
}

void qdisc_put_rtab(struct qdisc_rate_table *tab)
{
	struct qdisc_rate_table *rtab, **rtabp;

	if (!tab || --tab->refcnt)
		return;

	for (rtabp = &qdisc_rtab_list; (rtab=*rtabp) != NULL; rtabp = &rtab->next) {
		if (rtab == tab) {
			*rtabp = rtab->next;
			kfree(rtab);
			return;
		}
	}
}

static enum hrtimer_restart qdisc_watchdog(struct hrtimer *timer)
{
	struct qdisc_watchdog *wd = container_of(timer, struct qdisc_watchdog,
						 timer);
	struct net_device *dev = wd->qdisc->dev;

	wd->qdisc->flags &= ~TCQ_F_THROTTLED;
	smp_wmb();
	if (spin_trylock(&dev->queue_lock)) {
		qdisc_run(dev);
		spin_unlock(&dev->queue_lock);
	} else
		netif_schedule(dev);

	return HRTIMER_NORESTART;
}

void qdisc_watchdog_init(struct qdisc_watchdog *wd, struct Qdisc *qdisc)
{
	hrtimer_init(&wd->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	wd->timer.function = qdisc_watchdog;
	wd->qdisc = qdisc;
}
EXPORT_SYMBOL(qdisc_watchdog_init);

void qdisc_watchdog_schedule(struct qdisc_watchdog *wd, psched_time_t expires)
{
	ktime_t time;

	wd->qdisc->flags |= TCQ_F_THROTTLED;
	time = ktime_set(0, 0);
	time = ktime_add_ns(time, PSCHED_US2NS(expires));
	hrtimer_start(&wd->timer, time, HRTIMER_MODE_ABS);
}
EXPORT_SYMBOL(qdisc_watchdog_schedule);

void qdisc_watchdog_cancel(struct qdisc_watchdog *wd)
{
	hrtimer_cancel(&wd->timer);
	wd->qdisc->flags &= ~TCQ_F_THROTTLED;
}
EXPORT_SYMBOL(qdisc_watchdog_cancel);

/* Allocate an unique handle from space managed by kernel */

static u32 qdisc_alloc_handle(struct net_device *dev)
{
	int i = 0x10000;
	static u32 autohandle = TC_H_MAKE(0x80000000U, 0);

	do {
		autohandle += TC_H_MAKE(0x10000U, 0);
		if (autohandle == TC_H_MAKE(TC_H_ROOT, 0))
			autohandle = TC_H_MAKE(0x80000000U, 0);
	} while	(qdisc_lookup(dev, autohandle) && --i > 0);

	return i>0 ? autohandle : 0;
}

/* Attach toplevel qdisc to device dev */

static struct Qdisc *
dev_graft_qdisc(struct net_device *dev, struct Qdisc *qdisc)
{
	struct Qdisc *oqdisc;

	if (dev->flags & IFF_UP)
		dev_deactivate(dev);

	qdisc_lock_tree(dev);
	if (qdisc && qdisc->flags&TCQ_F_INGRESS) {
		oqdisc = dev->qdisc_ingress;
		/* Prune old scheduler */
		if (oqdisc && atomic_read(&oqdisc->refcnt) <= 1) {
			/* delete */
			qdisc_reset(oqdisc);
			dev->qdisc_ingress = NULL;
		} else {  /* new */
			dev->qdisc_ingress = qdisc;
		}

	} else {

		oqdisc = dev->qdisc_sleeping;

		/* Prune old scheduler */
		if (oqdisc && atomic_read(&oqdisc->refcnt) <= 1)
			qdisc_reset(oqdisc);

		/* ... and graft new one */
		if (qdisc == NULL)
			qdisc = &noop_qdisc;
		dev->qdisc_sleeping = qdisc;
		dev->qdisc = &noop_qdisc;
	}

	qdisc_unlock_tree(dev);

	if (dev->flags & IFF_UP)
		dev_activate(dev);

	return oqdisc;
}

void qdisc_tree_decrease_qlen(struct Qdisc *sch, unsigned int n)
{
	struct Qdisc_class_ops *cops;
	unsigned long cl;
	u32 parentid;

	if (n == 0)
		return;
	while ((parentid = sch->parent)) {
		sch = qdisc_lookup(sch->dev, TC_H_MAJ(parentid));
		cops = sch->ops->cl_ops;
		if (cops->qlen_notify) {
			cl = cops->get(sch, parentid);
			cops->qlen_notify(sch, cl);
			cops->put(sch, cl);
		}
		sch->q.qlen -= n;
	}
}
EXPORT_SYMBOL(qdisc_tree_decrease_qlen);

/* Graft qdisc "new" to class "classid" of qdisc "parent" or
   to device "dev".

   Old qdisc is not destroyed but returned in *old.
 */

static int qdisc_graft(struct net_device *dev, struct Qdisc *parent,
		       u32 classid,
		       struct Qdisc *new, struct Qdisc **old)
{
	int err = 0;
	struct Qdisc *q = *old;


	if (parent == NULL) {
		if (q && q->flags&TCQ_F_INGRESS) {
			*old = dev_graft_qdisc(dev, q);
		} else {
			*old = dev_graft_qdisc(dev, new);
		}
	} else {
		struct Qdisc_class_ops *cops = parent->ops->cl_ops;

		err = -EINVAL;

		if (cops) {
			unsigned long cl = cops->get(parent, classid);
			if (cl) {
				err = cops->graft(parent, cl, new, old);
				if (new)
					new->parent = classid;
				cops->put(parent, cl);
			}
		}
	}
	return err;
}

/*
   Allocate and initialize new qdisc.

   Parameters are passed via opt.
 */

static struct Qdisc *
qdisc_create(struct net_device *dev, u32 handle, struct rtattr **tca, int *errp)
{
	int err;
	struct rtattr *kind = tca[TCA_KIND-1];
	struct Qdisc *sch;
	struct Qdisc_ops *ops;

	ops = qdisc_lookup_ops(kind);
#ifdef CONFIG_KMOD
	if (ops == NULL && kind != NULL) {
		char name[IFNAMSIZ];
		if (rtattr_strlcpy(name, kind, IFNAMSIZ) < IFNAMSIZ) {
			/* We dropped the RTNL semaphore in order to
			 * perform the module load.  So, even if we
			 * succeeded in loading the module we have to
			 * tell the caller to replay the request.  We
			 * indicate this using -EAGAIN.
			 * We replay the request because the device may
			 * go away in the mean time.
			 */
			rtnl_unlock();
			request_module("sch_%s", name);
			rtnl_lock();
			ops = qdisc_lookup_ops(kind);
			if (ops != NULL) {
				/* We will try again qdisc_lookup_ops,
				 * so don't keep a reference.
				 */
				module_put(ops->owner);
				err = -EAGAIN;
				goto err_out;
			}
		}
	}
#endif

	err = -ENOENT;
	if (ops == NULL)
		goto err_out;

	sch = qdisc_alloc(dev, ops);
	if (IS_ERR(sch)) {
		err = PTR_ERR(sch);
		goto err_out2;
	}

	if (handle == TC_H_INGRESS) {
		sch->flags |= TCQ_F_INGRESS;
		sch->stats_lock = &dev->ingress_lock;
		handle = TC_H_MAKE(TC_H_INGRESS, 0);
	} else {
		sch->stats_lock = &dev->queue_lock;
		if (handle == 0) {
			handle = qdisc_alloc_handle(dev);
			err = -ENOMEM;
			if (handle == 0)
				goto err_out3;
		}
	}

	sch->handle = handle;

	if (!ops->init || (err = ops->init(sch, tca[TCA_OPTIONS-1])) == 0) {
#ifdef CONFIG_NET_ESTIMATOR
		if (tca[TCA_RATE-1]) {
			err = gen_new_estimator(&sch->bstats, &sch->rate_est,
						sch->stats_lock,
						tca[TCA_RATE-1]);
			if (err) {
				/*
				 * Any broken qdiscs that would require
				 * a ops->reset() here? The qdisc was never
				 * in action so it shouldn't be necessary.
				 */
				if (ops->destroy)
					ops->destroy(sch);
				goto err_out3;
			}
		}
#endif
		qdisc_lock_tree(dev);
		list_add_tail(&sch->list, &dev->qdisc_list);
		qdisc_unlock_tree(dev);

		return sch;
	}
err_out3:
	dev_put(dev);
	kfree((char *) sch - sch->padded);
err_out2:
	module_put(ops->owner);
err_out:
	*errp = err;
	return NULL;
}

static int qdisc_change(struct Qdisc *sch, struct rtattr **tca)
{
	if (tca[TCA_OPTIONS-1]) {
		int err;

		if (sch->ops->change == NULL)
			return -EINVAL;
		err = sch->ops->change(sch, tca[TCA_OPTIONS-1]);
		if (err)
			return err;
	}
#ifdef CONFIG_NET_ESTIMATOR
	if (tca[TCA_RATE-1])
		gen_replace_estimator(&sch->bstats, &sch->rate_est,
			sch->stats_lock, tca[TCA_RATE-1]);
#endif
	return 0;
}

struct check_loop_arg
{
	struct qdisc_walker 	w;
	struct Qdisc		*p;
	int			depth;
};

static int check_loop_fn(struct Qdisc *q, unsigned long cl, struct qdisc_walker *w);

static int check_loop(struct Qdisc *q, struct Qdisc *p, int depth)
{
	struct check_loop_arg	arg;

	if (q->ops->cl_ops == NULL)
		return 0;

	arg.w.stop = arg.w.skip = arg.w.count = 0;
	arg.w.fn = check_loop_fn;
	arg.depth = depth;
	arg.p = p;
	q->ops->cl_ops->walk(q, &arg.w);
	return arg.w.stop ? -ELOOP : 0;
}

static int
check_loop_fn(struct Qdisc *q, unsigned long cl, struct qdisc_walker *w)
{
	struct Qdisc *leaf;
	struct Qdisc_class_ops *cops = q->ops->cl_ops;
	struct check_loop_arg *arg = (struct check_loop_arg *)w;

	leaf = cops->leaf(q, cl);
	if (leaf) {
		if (leaf == arg->p || arg->depth > 7)
			return -ELOOP;
		return check_loop(leaf, arg->p, arg->depth + 1);
	}
	return 0;
}

/*
 * Delete/get qdisc.
 */

static int tc_get_qdisc(struct sk_buff *skb, struct nlmsghdr *n, void *arg)
{
	struct tcmsg *tcm = NLMSG_DATA(n);
	struct rtattr **tca = arg;
	struct net_device *dev;
	u32 clid = tcm->tcm_parent;
	struct Qdisc *q = NULL;
	struct Qdisc *p = NULL;
	int err;

	if ((dev = __dev_get_by_index(tcm->tcm_ifindex)) == NULL)
		return -ENODEV;

	if (clid) {
		if (clid != TC_H_ROOT) {
			if (TC_H_MAJ(clid) != TC_H_MAJ(TC_H_INGRESS)) {
				if ((p = qdisc_lookup(dev, TC_H_MAJ(clid))) == NULL)
					return -ENOENT;
				q = qdisc_leaf(p, clid);
			} else { /* ingress */
				q = dev->qdisc_ingress;
			}
		} else {
			q = dev->qdisc_sleeping;
		}
		if (!q)
			return -ENOENT;

		if (tcm->tcm_handle && q->handle != tcm->tcm_handle)
			return -EINVAL;
	} else {
		if ((q = qdisc_lookup(dev, tcm->tcm_handle)) == NULL)
			return -ENOENT;
	}

	if (tca[TCA_KIND-1] && rtattr_strcmp(tca[TCA_KIND-1], q->ops->id))
		return -EINVAL;

	if (n->nlmsg_type == RTM_DELQDISC) {
		if (!clid)
			return -EINVAL;
		if (q->handle == 0)
			return -ENOENT;
		if ((err = qdisc_graft(dev, p, clid, NULL, &q)) != 0)
			return err;
		if (q) {
			qdisc_notify(skb, n, clid, q, NULL);
			qdisc_lock_tree(dev);
			qdisc_destroy(q);
			qdisc_unlock_tree(dev);
		}
	} else {
		qdisc_notify(skb, n, clid, NULL, q);
	}
	return 0;
}

/*
   Create/change qdisc.
 */

static int tc_modify_qdisc(struct sk_buff *skb, struct nlmsghdr *n, void *arg)
{
	struct tcmsg *tcm;
	struct rtattr **tca;
	struct net_device *dev;
	u32 clid;
	struct Qdisc *q, *p;
	int err;

replay:
	/* Reinit, just in case something touches this. */
	tcm = NLMSG_DATA(n);
	tca = arg;
	clid = tcm->tcm_parent;
	q = p = NULL;

	if ((dev = __dev_get_by_index(tcm->tcm_ifindex)) == NULL)
		return -ENODEV;

	if (clid) {
		if (clid != TC_H_ROOT) {
			if (clid != TC_H_INGRESS) {
				if ((p = qdisc_lookup(dev, TC_H_MAJ(clid))) == NULL)
					return -ENOENT;
				q = qdisc_leaf(p, clid);
			} else { /*ingress */
				q = dev->qdisc_ingress;
			}
		} else {
			q = dev->qdisc_sleeping;
		}

		/* It may be default qdisc, ignore it */
		if (q && q->handle == 0)
			q = NULL;

		if (!q || !tcm->tcm_handle || q->handle != tcm->tcm_handle) {
			if (tcm->tcm_handle) {
				if (q && !(n->nlmsg_flags&NLM_F_REPLACE))
					return -EEXIST;
				if (TC_H_MIN(tcm->tcm_handle))
					return -EINVAL;
				if ((q = qdisc_lookup(dev, tcm->tcm_handle)) == NULL)
					goto create_n_graft;
				if (n->nlmsg_flags&NLM_F_EXCL)
					return -EEXIST;
				if (tca[TCA_KIND-1] && rtattr_strcmp(tca[TCA_KIND-1], q->ops->id))
					return -EINVAL;
				if (q == p ||
				    (p && check_loop(q, p, 0)))
					return -ELOOP;
				atomic_inc(&q->refcnt);
				goto graft;
			} else {
				if (q == NULL)
					goto create_n_graft;

				/* This magic test requires explanation.
				 *
				 *   We know, that some child q is already
				 *   attached to this parent and have choice:
				 *   either to change it or to create/graft new one.
				 *
				 *   1. We are allowed to create/graft only
				 *   if CREATE and REPLACE flags are set.
				 *
				 *   2. If EXCL is set, requestor wanted to say,
				 *   that qdisc tcm_handle is not expected
				 *   to exist, so that we choose create/graft too.
				 *
				 *   3. The last case is when no flags are set.
				 *   Alas, it is sort of hole in API, we
				 *   cannot decide what to do unambiguously.
				 *   For now we select create/graft, if
				 *   user gave KIND, which does not match existing.
				 */
				if ((n->nlmsg_flags&NLM_F_CREATE) &&
				    (n->nlmsg_flags&NLM_F_REPLACE) &&
				    ((n->nlmsg_flags&NLM_F_EXCL) ||
				     (tca[TCA_KIND-1] &&
				      rtattr_strcmp(tca[TCA_KIND-1], q->ops->id))))
					goto create_n_graft;
			}
		}
	} else {
		if (!tcm->tcm_handle)
			return -EINVAL;
		q = qdisc_lookup(dev, tcm->tcm_handle);
	}

	/* Change qdisc parameters */
	if (q == NULL)
		return -ENOENT;
	if (n->nlmsg_flags&NLM_F_EXCL)
		return -EEXIST;
	if (tca[TCA_KIND-1] && rtattr_strcmp(tca[TCA_KIND-1], q->ops->id))
		return -EINVAL;
	err = qdisc_change(q, tca);
	if (err == 0)
		qdisc_notify(skb, n, clid, NULL, q);
	return err;

create_n_graft:
	if (!(n->nlmsg_flags&NLM_F_CREATE))
		return -ENOENT;
	if (clid == TC_H_INGRESS)
		q = qdisc_create(dev, tcm->tcm_parent, tca, &err);
	else
		q = qdisc_create(dev, tcm->tcm_handle, tca, &err);
	if (q == NULL) {
		if (err == -EAGAIN)
			goto replay;
		return err;
	}

graft:
	if (1) {
		struct Qdisc *old_q = NULL;
		err = qdisc_graft(dev, p, clid, q, &old_q);
		if (err) {
			if (q) {
				qdisc_lock_tree(dev);
				qdisc_destroy(q);
				qdisc_unlock_tree(dev);
			}
			return err;
		}
		qdisc_notify(skb, n, clid, old_q, q);
		if (old_q) {
			qdisc_lock_tree(dev);
			qdisc_destroy(old_q);
			qdisc_unlock_tree(dev);
		}
	}
	return 0;
}

static int tc_fill_qdisc(struct sk_buff *skb, struct Qdisc *q, u32 clid,
			 u32 pid, u32 seq, u16 flags, int event)
{
	struct tcmsg *tcm;
	struct nlmsghdr  *nlh;
	unsigned char *b = skb_tail_pointer(skb);
	struct gnet_dump d;

	nlh = NLMSG_NEW(skb, pid, seq, event, sizeof(*tcm), flags);
	tcm = NLMSG_DATA(nlh);
	tcm->tcm_family = AF_UNSPEC;
	tcm->tcm__pad1 = 0;
	tcm->tcm__pad2 = 0;
	tcm->tcm_ifindex = q->dev->ifindex;
	tcm->tcm_parent = clid;
	tcm->tcm_handle = q->handle;
	tcm->tcm_info = atomic_read(&q->refcnt);
	RTA_PUT(skb, TCA_KIND, IFNAMSIZ, q->ops->id);
	if (q->ops->dump && q->ops->dump(q, skb) < 0)
		goto rtattr_failure;
	q->qstats.qlen = q->q.qlen;

	if (gnet_stats_start_copy_compat(skb, TCA_STATS2, TCA_STATS,
			TCA_XSTATS, q->stats_lock, &d) < 0)
		goto rtattr_failure;

	if (q->ops->dump_stats && q->ops->dump_stats(q, &d) < 0)
		goto rtattr_failure;

	if (gnet_stats_copy_basic(&d, &q->bstats) < 0 ||
#ifdef CONFIG_NET_ESTIMATOR
	    gnet_stats_copy_rate_est(&d, &q->rate_est) < 0 ||
#endif
	    gnet_stats_copy_queue(&d, &q->qstats) < 0)
		goto rtattr_failure;

	if (gnet_stats_finish_copy(&d) < 0)
		goto rtattr_failure;

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int qdisc_notify(struct sk_buff *oskb, struct nlmsghdr *n,
			u32 clid, struct Qdisc *old, struct Qdisc *new)
{
	struct sk_buff *skb;
	u32 pid = oskb ? NETLINK_CB(oskb).pid : 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (old && old->handle) {
		if (tc_fill_qdisc(skb, old, clid, pid, n->nlmsg_seq, 0, RTM_DELQDISC) < 0)
			goto err_out;
	}
	if (new) {
		if (tc_fill_qdisc(skb, new, clid, pid, n->nlmsg_seq, old ? NLM_F_REPLACE : 0, RTM_NEWQDISC) < 0)
			goto err_out;
	}

	if (skb->len)
		return rtnetlink_send(skb, pid, RTNLGRP_TC, n->nlmsg_flags&NLM_F_ECHO);

err_out:
	kfree_skb(skb);
	return -EINVAL;
}

static int tc_dump_qdisc(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx, q_idx;
	int s_idx, s_q_idx;
	struct net_device *dev;
	struct Qdisc *q;

	s_idx = cb->args[0];
	s_q_idx = q_idx = cb->args[1];
	read_lock(&dev_base_lock);
	idx = 0;
	for_each_netdev(dev) {
		if (idx < s_idx)
			goto cont;
		if (idx > s_idx)
			s_q_idx = 0;
		q_idx = 0;
		list_for_each_entry(q, &dev->qdisc_list, list) {
			if (q_idx < s_q_idx) {
				q_idx++;
				continue;
			}
			if (tc_fill_qdisc(skb, q, q->parent, NETLINK_CB(cb->skb).pid,
					  cb->nlh->nlmsg_seq, NLM_F_MULTI, RTM_NEWQDISC) <= 0)
				goto done;
			q_idx++;
		}
cont:
		idx++;
	}

done:
	read_unlock(&dev_base_lock);

	cb->args[0] = idx;
	cb->args[1] = q_idx;

	return skb->len;
}



/************************************************
 *	Traffic classes manipulation.		*
 ************************************************/



static int tc_ctl_tclass(struct sk_buff *skb, struct nlmsghdr *n, void *arg)
{
	struct tcmsg *tcm = NLMSG_DATA(n);
	struct rtattr **tca = arg;
	struct net_device *dev;
	struct Qdisc *q = NULL;
	struct Qdisc_class_ops *cops;
	unsigned long cl = 0;
	unsigned long new_cl;
	u32 pid = tcm->tcm_parent;
	u32 clid = tcm->tcm_handle;
	u32 qid = TC_H_MAJ(clid);
	int err;

	if ((dev = __dev_get_by_index(tcm->tcm_ifindex)) == NULL)
		return -ENODEV;

	/*
	   parent == TC_H_UNSPEC - unspecified parent.
	   parent == TC_H_ROOT   - class is root, which has no parent.
	   parent == X:0	 - parent is root class.
	   parent == X:Y	 - parent is a node in hierarchy.
	   parent == 0:Y	 - parent is X:Y, where X:0 is qdisc.

	   handle == 0:0	 - generate handle from kernel pool.
	   handle == 0:Y	 - class is X:Y, where X:0 is qdisc.
	   handle == X:Y	 - clear.
	   handle == X:0	 - root class.
	 */

	/* Step 1. Determine qdisc handle X:0 */

	if (pid != TC_H_ROOT) {
		u32 qid1 = TC_H_MAJ(pid);

		if (qid && qid1) {
			/* If both majors are known, they must be identical. */
			if (qid != qid1)
				return -EINVAL;
		} else if (qid1) {
			qid = qid1;
		} else if (qid == 0)
			qid = dev->qdisc_sleeping->handle;

		/* Now qid is genuine qdisc handle consistent
		   both with parent and child.

		   TC_H_MAJ(pid) still may be unspecified, complete it now.
		 */
		if (pid)
			pid = TC_H_MAKE(qid, pid);
	} else {
		if (qid == 0)
			qid = dev->qdisc_sleeping->handle;
	}

	/* OK. Locate qdisc */
	if ((q = qdisc_lookup(dev, qid)) == NULL)
		return -ENOENT;

	/* An check that it supports classes */
	cops = q->ops->cl_ops;
	if (cops == NULL)
		return -EINVAL;

	/* Now try to get class */
	if (clid == 0) {
		if (pid == TC_H_ROOT)
			clid = qid;
	} else
		clid = TC_H_MAKE(qid, clid);

	if (clid)
		cl = cops->get(q, clid);

	if (cl == 0) {
		err = -ENOENT;
		if (n->nlmsg_type != RTM_NEWTCLASS || !(n->nlmsg_flags&NLM_F_CREATE))
			goto out;
	} else {
		switch (n->nlmsg_type) {
		case RTM_NEWTCLASS:
			err = -EEXIST;
			if (n->nlmsg_flags&NLM_F_EXCL)
				goto out;
			break;
		case RTM_DELTCLASS:
			err = cops->delete(q, cl);
			if (err == 0)
				tclass_notify(skb, n, q, cl, RTM_DELTCLASS);
			goto out;
		case RTM_GETTCLASS:
			err = tclass_notify(skb, n, q, cl, RTM_NEWTCLASS);
			goto out;
		default:
			err = -EINVAL;
			goto out;
		}
	}

	new_cl = cl;
	err = cops->change(q, clid, pid, tca, &new_cl);
	if (err == 0)
		tclass_notify(skb, n, q, new_cl, RTM_NEWTCLASS);

out:
	if (cl)
		cops->put(q, cl);

	return err;
}


static int tc_fill_tclass(struct sk_buff *skb, struct Qdisc *q,
			  unsigned long cl,
			  u32 pid, u32 seq, u16 flags, int event)
{
	struct tcmsg *tcm;
	struct nlmsghdr  *nlh;
	unsigned char *b = skb_tail_pointer(skb);
	struct gnet_dump d;
	struct Qdisc_class_ops *cl_ops = q->ops->cl_ops;

	nlh = NLMSG_NEW(skb, pid, seq, event, sizeof(*tcm), flags);
	tcm = NLMSG_DATA(nlh);
	tcm->tcm_family = AF_UNSPEC;
	tcm->tcm_ifindex = q->dev->ifindex;
	tcm->tcm_parent = q->handle;
	tcm->tcm_handle = q->handle;
	tcm->tcm_info = 0;
	RTA_PUT(skb, TCA_KIND, IFNAMSIZ, q->ops->id);
	if (cl_ops->dump && cl_ops->dump(q, cl, skb, tcm) < 0)
		goto rtattr_failure;

	if (gnet_stats_start_copy_compat(skb, TCA_STATS2, TCA_STATS,
			TCA_XSTATS, q->stats_lock, &d) < 0)
		goto rtattr_failure;

	if (cl_ops->dump_stats && cl_ops->dump_stats(q, cl, &d) < 0)
		goto rtattr_failure;

	if (gnet_stats_finish_copy(&d) < 0)
		goto rtattr_failure;

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	nlmsg_trim(skb, b);
	return -1;
}

static int tclass_notify(struct sk_buff *oskb, struct nlmsghdr *n,
			  struct Qdisc *q, unsigned long cl, int event)
{
	struct sk_buff *skb;
	u32 pid = oskb ? NETLINK_CB(oskb).pid : 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tc_fill_tclass(skb, q, cl, pid, n->nlmsg_seq, 0, event) < 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	return rtnetlink_send(skb, pid, RTNLGRP_TC, n->nlmsg_flags&NLM_F_ECHO);
}

struct qdisc_dump_args
{
	struct qdisc_walker w;
	struct sk_buff *skb;
	struct netlink_callback *cb;
};

static int qdisc_class_dump(struct Qdisc *q, unsigned long cl, struct qdisc_walker *arg)
{
	struct qdisc_dump_args *a = (struct qdisc_dump_args *)arg;

	return tc_fill_tclass(a->skb, q, cl, NETLINK_CB(a->cb->skb).pid,
			      a->cb->nlh->nlmsg_seq, NLM_F_MULTI, RTM_NEWTCLASS);
}

static int tc_dump_tclass(struct sk_buff *skb, struct netlink_callback *cb)
{
	int t;
	int s_t;
	struct net_device *dev;
	struct Qdisc *q;
	struct tcmsg *tcm = (struct tcmsg*)NLMSG_DATA(cb->nlh);
	struct qdisc_dump_args arg;

	if (cb->nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*tcm)))
		return 0;
	if ((dev = dev_get_by_index(tcm->tcm_ifindex)) == NULL)
		return 0;

	s_t = cb->args[0];
	t = 0;

	list_for_each_entry(q, &dev->qdisc_list, list) {
		if (t < s_t || !q->ops->cl_ops ||
		    (tcm->tcm_parent &&
		     TC_H_MAJ(tcm->tcm_parent) != q->handle)) {
			t++;
			continue;
		}
		if (t > s_t)
			memset(&cb->args[1], 0, sizeof(cb->args)-sizeof(cb->args[0]));
		arg.w.fn = qdisc_class_dump;
		arg.skb = skb;
		arg.cb = cb;
		arg.w.stop  = 0;
		arg.w.skip = cb->args[1];
		arg.w.count = 0;
		q->ops->cl_ops->walk(q, &arg.w);
		cb->args[1] = arg.w.count;
		if (arg.w.stop)
			break;
		t++;
	}

	cb->args[0] = t;

	dev_put(dev);
	return skb->len;
}

/* Main classifier routine: scans classifier chain attached
   to this qdisc, (optionally) tests for protocol and asks
   specific classifiers.
 */
int tc_classify(struct sk_buff *skb, struct tcf_proto *tp,
	struct tcf_result *res)
{
	int err = 0;
	__be16 protocol = skb->protocol;
#ifdef CONFIG_NET_CLS_ACT
	struct tcf_proto *otp = tp;
reclassify:
#endif
	protocol = skb->protocol;

	for ( ; tp; tp = tp->next) {
		if ((tp->protocol == protocol ||
			tp->protocol == htons(ETH_P_ALL)) &&
			(err = tp->classify(skb, tp, res)) >= 0) {
#ifdef CONFIG_NET_CLS_ACT
			if ( TC_ACT_RECLASSIFY == err) {
				__u32 verd = (__u32) G_TC_VERD(skb->tc_verd);
				tp = otp;

				if (MAX_REC_LOOP < verd++) {
					printk("rule prio %d protocol %02x reclassify is buggy packet dropped\n",
						tp->prio&0xffff, ntohs(tp->protocol));
					return TC_ACT_SHOT;
				}
				skb->tc_verd = SET_TC_VERD(skb->tc_verd,verd);
				goto reclassify;
			} else {
				if (skb->tc_verd)
					skb->tc_verd = SET_TC_VERD(skb->tc_verd,0);
				return err;
			}
#else

			return err;
#endif
		}

	}
	return -1;
}

void tcf_destroy(struct tcf_proto *tp)
{
	tp->ops->destroy(tp);
	module_put(tp->ops->owner);
	kfree(tp);
}

void tcf_destroy_chain(struct tcf_proto *fl)
{
	struct tcf_proto *tp;

	while ((tp = fl) != NULL) {
		fl = tp->next;
		tcf_destroy(tp);
	}
}
EXPORT_SYMBOL(tcf_destroy_chain);

#ifdef CONFIG_PROC_FS
static int psched_show(struct seq_file *seq, void *v)
{
	seq_printf(seq, "%08x %08x %08x %08x\n",
		   (u32)NSEC_PER_USEC, (u32)PSCHED_US2NS(1),
		   1000000,
		   (u32)NSEC_PER_SEC/(u32)ktime_to_ns(KTIME_MONOTONIC_RES));

	return 0;
}

static int psched_open(struct inode *inode, struct file *file)
{
	return single_open(file, psched_show, PDE(inode)->data);
}

static const struct file_operations psched_fops = {
	.owner = THIS_MODULE,
	.open = psched_open,
	.read  = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static int __init pktsched_init(void)
{
	register_qdisc(&pfifo_qdisc_ops);
	register_qdisc(&bfifo_qdisc_ops);
	proc_net_fops_create("psched", 0, &psched_fops);

	rtnl_register(PF_UNSPEC, RTM_NEWQDISC, tc_modify_qdisc, NULL);
	rtnl_register(PF_UNSPEC, RTM_DELQDISC, tc_get_qdisc, NULL);
	rtnl_register(PF_UNSPEC, RTM_GETQDISC, tc_get_qdisc, tc_dump_qdisc);
	rtnl_register(PF_UNSPEC, RTM_NEWTCLASS, tc_ctl_tclass, NULL);
	rtnl_register(PF_UNSPEC, RTM_DELTCLASS, tc_ctl_tclass, NULL);
	rtnl_register(PF_UNSPEC, RTM_GETTCLASS, tc_ctl_tclass, tc_dump_tclass);

	return 0;
}

subsys_initcall(pktsched_init);

EXPORT_SYMBOL(qdisc_get_rtab);
EXPORT_SYMBOL(qdisc_put_rtab);
EXPORT_SYMBOL(register_qdisc);
EXPORT_SYMBOL(unregister_qdisc);
EXPORT_SYMBOL(tc_classify);
