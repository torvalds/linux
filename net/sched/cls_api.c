/*
 * net/sched/cls_api.c	Packet classifier API.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 * Changes:
 *
 * Eduardo J. Blanco <ejbs@netlabs.com.uy> :990222: kmod support
 *
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <net/sock.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>

#if 0 /* control */
#define DPRINTK(format,args...) printk(KERN_DEBUG format,##args)
#else
#define DPRINTK(format,args...)
#endif

/* The list of all installed classifier types */

static struct tcf_proto_ops *tcf_proto_base;

/* Protects list of registered TC modules. It is pure SMP lock. */
static DEFINE_RWLOCK(cls_mod_lock);

/* Find classifier type by string name */

static struct tcf_proto_ops * tcf_proto_lookup_ops(struct rtattr *kind)
{
	struct tcf_proto_ops *t = NULL;

	if (kind) {
		read_lock(&cls_mod_lock);
		for (t = tcf_proto_base; t; t = t->next) {
			if (rtattr_strcmp(kind, t->kind) == 0) {
				if (!try_module_get(t->owner))
					t = NULL;
				break;
			}
		}
		read_unlock(&cls_mod_lock);
	}
	return t;
}

/* Register(unregister) new classifier type */

int register_tcf_proto_ops(struct tcf_proto_ops *ops)
{
	struct tcf_proto_ops *t, **tp;
	int rc = -EEXIST;

	write_lock(&cls_mod_lock);
	for (tp = &tcf_proto_base; (t = *tp) != NULL; tp = &t->next)
		if (!strcmp(ops->kind, t->kind))
			goto out;

	ops->next = NULL;
	*tp = ops;
	rc = 0;
out:
	write_unlock(&cls_mod_lock);
	return rc;
}

int unregister_tcf_proto_ops(struct tcf_proto_ops *ops)
{
	struct tcf_proto_ops *t, **tp;
	int rc = -ENOENT;

	write_lock(&cls_mod_lock);
	for (tp = &tcf_proto_base; (t=*tp) != NULL; tp = &t->next)
		if (t == ops)
			break;

	if (!t)
		goto out;
	*tp = t->next;
	rc = 0;
out:
	write_unlock(&cls_mod_lock);
	return rc;
}

static int tfilter_notify(struct sk_buff *oskb, struct nlmsghdr *n,
			  struct tcf_proto *tp, unsigned long fh, int event);


/* Select new prio value from the range, managed by kernel. */

static __inline__ u32 tcf_auto_prio(struct tcf_proto *tp)
{
	u32 first = TC_H_MAKE(0xC0000000U,0U);

	if (tp)
		first = tp->prio-1;

	return first;
}

/* Add/change/delete/get a filter node */

static int tc_ctl_tfilter(struct sk_buff *skb, struct nlmsghdr *n, void *arg)
{
	struct rtattr **tca;
	struct tcmsg *t;
	u32 protocol;
	u32 prio;
	u32 nprio;
	u32 parent;
	struct net_device *dev;
	struct Qdisc  *q;
	struct tcf_proto **back, **chain;
	struct tcf_proto *tp;
	struct tcf_proto_ops *tp_ops;
	struct Qdisc_class_ops *cops;
	unsigned long cl;
	unsigned long fh;
	int err;

replay:
	tca = arg;
	t = NLMSG_DATA(n);
	protocol = TC_H_MIN(t->tcm_info);
	prio = TC_H_MAJ(t->tcm_info);
	nprio = prio;
	parent = t->tcm_parent;
	cl = 0;

	if (prio == 0) {
		/* If no priority is given, user wants we allocated it. */
		if (n->nlmsg_type != RTM_NEWTFILTER || !(n->nlmsg_flags&NLM_F_CREATE))
			return -ENOENT;
		prio = TC_H_MAKE(0x80000000U,0U);
	}

	/* Find head of filter chain. */

	/* Find link */
	if ((dev = __dev_get_by_index(t->tcm_ifindex)) == NULL)
		return -ENODEV;

	/* Find qdisc */
	if (!parent) {
		q = dev->qdisc_sleeping;
		parent = q->handle;
	} else if ((q = qdisc_lookup(dev, TC_H_MAJ(t->tcm_parent))) == NULL)
		return -EINVAL;

	/* Is it classful? */
	if ((cops = q->ops->cl_ops) == NULL)
		return -EINVAL;

	/* Do we search for filter, attached to class? */
	if (TC_H_MIN(parent)) {
		cl = cops->get(q, parent);
		if (cl == 0)
			return -ENOENT;
	}

	/* And the last stroke */
	chain = cops->tcf_chain(q, cl);
	err = -EINVAL;
	if (chain == NULL)
		goto errout;

	/* Check the chain for existence of proto-tcf with this priority */
	for (back = chain; (tp=*back) != NULL; back = &tp->next) {
		if (tp->prio >= prio) {
			if (tp->prio == prio) {
				if (!nprio || (tp->protocol != protocol && protocol))
					goto errout;
			} else
				tp = NULL;
			break;
		}
	}

	if (tp == NULL) {
		/* Proto-tcf does not exist, create new one */

		if (tca[TCA_KIND-1] == NULL || !protocol)
			goto errout;

		err = -ENOENT;
		if (n->nlmsg_type != RTM_NEWTFILTER || !(n->nlmsg_flags&NLM_F_CREATE))
			goto errout;


		/* Create new proto tcf */

		err = -ENOBUFS;
		if ((tp = kmalloc(sizeof(*tp), GFP_KERNEL)) == NULL)
			goto errout;
		err = -EINVAL;
		tp_ops = tcf_proto_lookup_ops(tca[TCA_KIND-1]);
		if (tp_ops == NULL) {
#ifdef CONFIG_KMOD
			struct rtattr *kind = tca[TCA_KIND-1];
			char name[IFNAMSIZ];

			if (kind != NULL &&
			    rtattr_strlcpy(name, kind, IFNAMSIZ) < IFNAMSIZ) {
				rtnl_unlock();
				request_module("cls_%s", name);
				rtnl_lock();
				tp_ops = tcf_proto_lookup_ops(kind);
				/* We dropped the RTNL semaphore in order to
				 * perform the module load.  So, even if we
				 * succeeded in loading the module we have to
				 * replay the request.  We indicate this using
				 * -EAGAIN.
				 */
				if (tp_ops != NULL) {
					module_put(tp_ops->owner);
					err = -EAGAIN;
				}
			}
#endif
			kfree(tp);
			goto errout;
		}
		memset(tp, 0, sizeof(*tp));
		tp->ops = tp_ops;
		tp->protocol = protocol;
		tp->prio = nprio ? : tcf_auto_prio(*back);
		tp->q = q;
		tp->classify = tp_ops->classify;
		tp->classid = parent;
		if ((err = tp_ops->init(tp)) != 0) {
			module_put(tp_ops->owner);
			kfree(tp);
			goto errout;
		}

		qdisc_lock_tree(dev);
		tp->next = *back;
		*back = tp;
		qdisc_unlock_tree(dev);

	} else if (tca[TCA_KIND-1] && rtattr_strcmp(tca[TCA_KIND-1], tp->ops->kind))
		goto errout;

	fh = tp->ops->get(tp, t->tcm_handle);

	if (fh == 0) {
		if (n->nlmsg_type == RTM_DELTFILTER && t->tcm_handle == 0) {
			qdisc_lock_tree(dev);
			*back = tp->next;
			qdisc_unlock_tree(dev);

			tfilter_notify(skb, n, tp, fh, RTM_DELTFILTER);
			tcf_destroy(tp);
			err = 0;
			goto errout;
		}

		err = -ENOENT;
		if (n->nlmsg_type != RTM_NEWTFILTER || !(n->nlmsg_flags&NLM_F_CREATE))
			goto errout;
	} else {
		switch (n->nlmsg_type) {
		case RTM_NEWTFILTER:	
			err = -EEXIST;
			if (n->nlmsg_flags&NLM_F_EXCL)
				goto errout;
			break;
		case RTM_DELTFILTER:
			err = tp->ops->delete(tp, fh);
			if (err == 0)
				tfilter_notify(skb, n, tp, fh, RTM_DELTFILTER);
			goto errout;
		case RTM_GETTFILTER:
			err = tfilter_notify(skb, n, tp, fh, RTM_NEWTFILTER);
			goto errout;
		default:
			err = -EINVAL;
			goto errout;
		}
	}

	err = tp->ops->change(tp, cl, t->tcm_handle, tca, &fh);
	if (err == 0)
		tfilter_notify(skb, n, tp, fh, RTM_NEWTFILTER);

errout:
	if (cl)
		cops->put(q, cl);
	if (err == -EAGAIN)
		/* Replay the request. */
		goto replay;
	return err;
}

static int
tcf_fill_node(struct sk_buff *skb, struct tcf_proto *tp, unsigned long fh,
	      u32 pid, u32 seq, u16 flags, int event)
{
	struct tcmsg *tcm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_NEW(skb, pid, seq, event, sizeof(*tcm), flags);
	tcm = NLMSG_DATA(nlh);
	tcm->tcm_family = AF_UNSPEC;
	tcm->tcm__pad1 = 0;
	tcm->tcm__pad1 = 0;
	tcm->tcm_ifindex = tp->q->dev->ifindex;
	tcm->tcm_parent = tp->classid;
	tcm->tcm_info = TC_H_MAKE(tp->prio, tp->protocol);
	RTA_PUT(skb, TCA_KIND, IFNAMSIZ, tp->ops->kind);
	tcm->tcm_handle = fh;
	if (RTM_DELTFILTER != event) {
		tcm->tcm_handle = 0;
		if (tp->ops->dump && tp->ops->dump(tp, fh, skb, tcm) < 0)
			goto rtattr_failure;
	}
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

static int tfilter_notify(struct sk_buff *oskb, struct nlmsghdr *n,
			  struct tcf_proto *tp, unsigned long fh, int event)
{
	struct sk_buff *skb;
	u32 pid = oskb ? NETLINK_CB(oskb).pid : 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tcf_fill_node(skb, tp, fh, pid, n->nlmsg_seq, 0, event) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	return rtnetlink_send(skb, pid, RTNLGRP_TC, n->nlmsg_flags&NLM_F_ECHO);
}

struct tcf_dump_args
{
	struct tcf_walker w;
	struct sk_buff *skb;
	struct netlink_callback *cb;
};

static int tcf_node_dump(struct tcf_proto *tp, unsigned long n, struct tcf_walker *arg)
{
	struct tcf_dump_args *a = (void*)arg;

	return tcf_fill_node(a->skb, tp, n, NETLINK_CB(a->cb->skb).pid,
			     a->cb->nlh->nlmsg_seq, NLM_F_MULTI, RTM_NEWTFILTER);
}

static int tc_dump_tfilter(struct sk_buff *skb, struct netlink_callback *cb)
{
	int t;
	int s_t;
	struct net_device *dev;
	struct Qdisc *q;
	struct tcf_proto *tp, **chain;
	struct tcmsg *tcm = (struct tcmsg*)NLMSG_DATA(cb->nlh);
	unsigned long cl = 0;
	struct Qdisc_class_ops *cops;
	struct tcf_dump_args arg;

	if (cb->nlh->nlmsg_len < NLMSG_LENGTH(sizeof(*tcm)))
		return skb->len;
	if ((dev = dev_get_by_index(tcm->tcm_ifindex)) == NULL)
		return skb->len;

	read_lock_bh(&qdisc_tree_lock);
	if (!tcm->tcm_parent)
		q = dev->qdisc_sleeping;
	else
		q = qdisc_lookup(dev, TC_H_MAJ(tcm->tcm_parent));
	if (!q)
		goto out;
	if ((cops = q->ops->cl_ops) == NULL)
		goto errout;
	if (TC_H_MIN(tcm->tcm_parent)) {
		cl = cops->get(q, tcm->tcm_parent);
		if (cl == 0)
			goto errout;
	}
	chain = cops->tcf_chain(q, cl);
	if (chain == NULL)
		goto errout;

	s_t = cb->args[0];

	for (tp=*chain, t=0; tp; tp = tp->next, t++) {
		if (t < s_t) continue;
		if (TC_H_MAJ(tcm->tcm_info) &&
		    TC_H_MAJ(tcm->tcm_info) != tp->prio)
			continue;
		if (TC_H_MIN(tcm->tcm_info) &&
		    TC_H_MIN(tcm->tcm_info) != tp->protocol)
			continue;
		if (t > s_t)
			memset(&cb->args[1], 0, sizeof(cb->args)-sizeof(cb->args[0]));
		if (cb->args[1] == 0) {
			if (tcf_fill_node(skb, tp, 0, NETLINK_CB(cb->skb).pid,
					  cb->nlh->nlmsg_seq, NLM_F_MULTI, RTM_NEWTFILTER) <= 0) {
				break;
			}
			cb->args[1] = 1;
		}
		if (tp->ops->walk == NULL)
			continue;
		arg.w.fn = tcf_node_dump;
		arg.skb = skb;
		arg.cb = cb;
		arg.w.stop = 0;
		arg.w.skip = cb->args[1]-1;
		arg.w.count = 0;
		tp->ops->walk(tp, &arg.w);
		cb->args[1] = arg.w.count+1;
		if (arg.w.stop)
			break;
	}

	cb->args[0] = t;

errout:
	if (cl)
		cops->put(q, cl);
out:
	read_unlock_bh(&qdisc_tree_lock);
	dev_put(dev);
	return skb->len;
}

void
tcf_exts_destroy(struct tcf_proto *tp, struct tcf_exts *exts)
{
#ifdef CONFIG_NET_CLS_ACT
	if (exts->action) {
		tcf_action_destroy(exts->action, TCA_ACT_UNBIND);
		exts->action = NULL;
	}
#elif defined CONFIG_NET_CLS_POLICE
	if (exts->police) {
		tcf_police_release(exts->police, TCA_ACT_UNBIND);
		exts->police = NULL;
	}
#endif
}


int
tcf_exts_validate(struct tcf_proto *tp, struct rtattr **tb,
	          struct rtattr *rate_tlv, struct tcf_exts *exts,
	          struct tcf_ext_map *map)
{
	memset(exts, 0, sizeof(*exts));
	
#ifdef CONFIG_NET_CLS_ACT
	{
		int err;
		struct tc_action *act;

		if (map->police && tb[map->police-1]) {
			act = tcf_action_init_1(tb[map->police-1], rate_tlv, "police",
				TCA_ACT_NOREPLACE, TCA_ACT_BIND, &err);
			if (act == NULL)
				return err;

			act->type = TCA_OLD_COMPAT;
			exts->action = act;
		} else if (map->action && tb[map->action-1]) {
			act = tcf_action_init(tb[map->action-1], rate_tlv, NULL,
				TCA_ACT_NOREPLACE, TCA_ACT_BIND, &err);
			if (act == NULL)
				return err;

			exts->action = act;
		}
	}
#elif defined CONFIG_NET_CLS_POLICE
	if (map->police && tb[map->police-1]) {
		struct tcf_police *p;
		
		p = tcf_police_locate(tb[map->police-1], rate_tlv);
		if (p == NULL)
			return -EINVAL;

		exts->police = p;
	} else if (map->action && tb[map->action-1])
		return -EOPNOTSUPP;
#else
	if ((map->action && tb[map->action-1]) ||
	    (map->police && tb[map->police-1]))
		return -EOPNOTSUPP;
#endif

	return 0;
}

void
tcf_exts_change(struct tcf_proto *tp, struct tcf_exts *dst,
	        struct tcf_exts *src)
{
#ifdef CONFIG_NET_CLS_ACT
	if (src->action) {
		struct tc_action *act;
		tcf_tree_lock(tp);
		act = xchg(&dst->action, src->action);
		tcf_tree_unlock(tp);
		if (act)
			tcf_action_destroy(act, TCA_ACT_UNBIND);
	}
#elif defined CONFIG_NET_CLS_POLICE
	if (src->police) {
		struct tcf_police *p;
		tcf_tree_lock(tp);
		p = xchg(&dst->police, src->police);
		tcf_tree_unlock(tp);
		if (p)
			tcf_police_release(p, TCA_ACT_UNBIND);
	}
#endif
}

int
tcf_exts_dump(struct sk_buff *skb, struct tcf_exts *exts,
	      struct tcf_ext_map *map)
{
#ifdef CONFIG_NET_CLS_ACT
	if (map->action && exts->action) {
		/*
		 * again for backward compatible mode - we want
		 * to work with both old and new modes of entering
		 * tc data even if iproute2  was newer - jhs
		 */
		struct rtattr * p_rta = (struct rtattr*) skb->tail;

		if (exts->action->type != TCA_OLD_COMPAT) {
			RTA_PUT(skb, map->action, 0, NULL);
			if (tcf_action_dump(skb, exts->action, 0, 0) < 0)
				goto rtattr_failure;
			p_rta->rta_len = skb->tail - (u8*)p_rta;
		} else if (map->police) {
			RTA_PUT(skb, map->police, 0, NULL);
			if (tcf_action_dump_old(skb, exts->action, 0, 0) < 0)
				goto rtattr_failure;
			p_rta->rta_len = skb->tail - (u8*)p_rta;
		}
	}
#elif defined CONFIG_NET_CLS_POLICE
	if (map->police && exts->police) {
		struct rtattr * p_rta = (struct rtattr*) skb->tail;

		RTA_PUT(skb, map->police, 0, NULL);

		if (tcf_police_dump(skb, exts->police) < 0)
			goto rtattr_failure;

		p_rta->rta_len = skb->tail - (u8*)p_rta;
	}
#endif
	return 0;
rtattr_failure: __attribute__ ((unused))
	return -1;
}

int
tcf_exts_dump_stats(struct sk_buff *skb, struct tcf_exts *exts,
	            struct tcf_ext_map *map)
{
#ifdef CONFIG_NET_CLS_ACT
	if (exts->action)
		if (tcf_action_copy_stats(skb, exts->action, 1) < 0)
			goto rtattr_failure;
#elif defined CONFIG_NET_CLS_POLICE
	if (exts->police)
		if (tcf_police_dump_stats(skb, exts->police) < 0)
			goto rtattr_failure;
#endif
	return 0;
rtattr_failure: __attribute__ ((unused))
	return -1;
}

static int __init tc_filter_init(void)
{
	struct rtnetlink_link *link_p = rtnetlink_links[PF_UNSPEC];

	/* Setup rtnetlink links. It is made here to avoid
	   exporting large number of public symbols.
	 */

	if (link_p) {
		link_p[RTM_NEWTFILTER-RTM_BASE].doit = tc_ctl_tfilter;
		link_p[RTM_DELTFILTER-RTM_BASE].doit = tc_ctl_tfilter;
		link_p[RTM_GETTFILTER-RTM_BASE].doit = tc_ctl_tfilter;
		link_p[RTM_GETTFILTER-RTM_BASE].dumpit = tc_dump_tfilter;
	}
	return 0;
}

subsys_initcall(tc_filter_init);

EXPORT_SYMBOL(register_tcf_proto_ops);
EXPORT_SYMBOL(unregister_tcf_proto_ops);
EXPORT_SYMBOL(tcf_exts_validate);
EXPORT_SYMBOL(tcf_exts_destroy);
EXPORT_SYMBOL(tcf_exts_change);
EXPORT_SYMBOL(tcf_exts_dump);
EXPORT_SYMBOL(tcf_exts_dump_stats);
