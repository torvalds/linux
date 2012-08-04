/*
 * net/sched/act_api.c	Packet action API.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Jamal Hadi Salim
 *
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/err.h>
#include <linux/module.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/sch_generic.h>
#include <net/act_api.h>
#include <net/netlink.h>

void tcf_hash_destroy(struct tcf_common *p, struct tcf_hashinfo *hinfo)
{
	unsigned int h = tcf_hash(p->tcfc_index, hinfo->hmask);
	struct tcf_common **p1p;

	for (p1p = &hinfo->htab[h]; *p1p; p1p = &(*p1p)->tcfc_next) {
		if (*p1p == p) {
			write_lock_bh(hinfo->lock);
			*p1p = p->tcfc_next;
			write_unlock_bh(hinfo->lock);
			gen_kill_estimator(&p->tcfc_bstats,
					   &p->tcfc_rate_est);
			/*
			 * gen_estimator est_timer() might access p->tcfc_lock
			 * or bstats, wait a RCU grace period before freeing p
			 */
			kfree_rcu(p, tcfc_rcu);
			return;
		}
	}
	WARN_ON(1);
}
EXPORT_SYMBOL(tcf_hash_destroy);

int tcf_hash_release(struct tcf_common *p, int bind,
		     struct tcf_hashinfo *hinfo)
{
	int ret = 0;

	if (p) {
		if (bind)
			p->tcfc_bindcnt--;

		p->tcfc_refcnt--;
		if (p->tcfc_bindcnt <= 0 && p->tcfc_refcnt <= 0) {
			tcf_hash_destroy(p, hinfo);
			ret = 1;
		}
	}
	return ret;
}
EXPORT_SYMBOL(tcf_hash_release);

static int tcf_dump_walker(struct sk_buff *skb, struct netlink_callback *cb,
			   struct tc_action *a, struct tcf_hashinfo *hinfo)
{
	struct tcf_common *p;
	int err = 0, index = -1, i = 0, s_i = 0, n_i = 0;
	struct nlattr *nest;

	read_lock_bh(hinfo->lock);

	s_i = cb->args[0];

	for (i = 0; i < (hinfo->hmask + 1); i++) {
		p = hinfo->htab[tcf_hash(i, hinfo->hmask)];

		for (; p; p = p->tcfc_next) {
			index++;
			if (index < s_i)
				continue;
			a->priv = p;
			a->order = n_i;

			nest = nla_nest_start(skb, a->order);
			if (nest == NULL)
				goto nla_put_failure;
			err = tcf_action_dump_1(skb, a, 0, 0);
			if (err < 0) {
				index--;
				nlmsg_trim(skb, nest);
				goto done;
			}
			nla_nest_end(skb, nest);
			n_i++;
			if (n_i >= TCA_ACT_MAX_PRIO)
				goto done;
		}
	}
done:
	read_unlock_bh(hinfo->lock);
	if (n_i)
		cb->args[0] += n_i;
	return n_i;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	goto done;
}

static int tcf_del_walker(struct sk_buff *skb, struct tc_action *a,
			  struct tcf_hashinfo *hinfo)
{
	struct tcf_common *p, *s_p;
	struct nlattr *nest;
	int i = 0, n_i = 0;

	nest = nla_nest_start(skb, a->order);
	if (nest == NULL)
		goto nla_put_failure;
	if (nla_put_string(skb, TCA_KIND, a->ops->kind))
		goto nla_put_failure;
	for (i = 0; i < (hinfo->hmask + 1); i++) {
		p = hinfo->htab[tcf_hash(i, hinfo->hmask)];

		while (p != NULL) {
			s_p = p->tcfc_next;
			if (ACT_P_DELETED == tcf_hash_release(p, 0, hinfo))
				module_put(a->ops->owner);
			n_i++;
			p = s_p;
		}
	}
	if (nla_put_u32(skb, TCA_FCNT, n_i))
		goto nla_put_failure;
	nla_nest_end(skb, nest);

	return n_i;
nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EINVAL;
}

int tcf_generic_walker(struct sk_buff *skb, struct netlink_callback *cb,
		       int type, struct tc_action *a)
{
	struct tcf_hashinfo *hinfo = a->ops->hinfo;

	if (type == RTM_DELACTION) {
		return tcf_del_walker(skb, a, hinfo);
	} else if (type == RTM_GETACTION) {
		return tcf_dump_walker(skb, cb, a, hinfo);
	} else {
		WARN(1, "tcf_generic_walker: unknown action %d\n", type);
		return -EINVAL;
	}
}
EXPORT_SYMBOL(tcf_generic_walker);

struct tcf_common *tcf_hash_lookup(u32 index, struct tcf_hashinfo *hinfo)
{
	struct tcf_common *p;

	read_lock_bh(hinfo->lock);
	for (p = hinfo->htab[tcf_hash(index, hinfo->hmask)]; p;
	     p = p->tcfc_next) {
		if (p->tcfc_index == index)
			break;
	}
	read_unlock_bh(hinfo->lock);

	return p;
}
EXPORT_SYMBOL(tcf_hash_lookup);

u32 tcf_hash_new_index(u32 *idx_gen, struct tcf_hashinfo *hinfo)
{
	u32 val = *idx_gen;

	do {
		if (++val == 0)
			val = 1;
	} while (tcf_hash_lookup(val, hinfo));

	return (*idx_gen = val);
}
EXPORT_SYMBOL(tcf_hash_new_index);

int tcf_hash_search(struct tc_action *a, u32 index)
{
	struct tcf_hashinfo *hinfo = a->ops->hinfo;
	struct tcf_common *p = tcf_hash_lookup(index, hinfo);

	if (p) {
		a->priv = p;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(tcf_hash_search);

struct tcf_common *tcf_hash_check(u32 index, struct tc_action *a, int bind,
				  struct tcf_hashinfo *hinfo)
{
	struct tcf_common *p = NULL;
	if (index && (p = tcf_hash_lookup(index, hinfo)) != NULL) {
		if (bind)
			p->tcfc_bindcnt++;
		p->tcfc_refcnt++;
		a->priv = p;
	}
	return p;
}
EXPORT_SYMBOL(tcf_hash_check);

struct tcf_common *tcf_hash_create(u32 index, struct nlattr *est,
				   struct tc_action *a, int size, int bind,
				   u32 *idx_gen, struct tcf_hashinfo *hinfo)
{
	struct tcf_common *p = kzalloc(size, GFP_KERNEL);

	if (unlikely(!p))
		return ERR_PTR(-ENOMEM);
	p->tcfc_refcnt = 1;
	if (bind)
		p->tcfc_bindcnt = 1;

	spin_lock_init(&p->tcfc_lock);
	p->tcfc_index = index ? index : tcf_hash_new_index(idx_gen, hinfo);
	p->tcfc_tm.install = jiffies;
	p->tcfc_tm.lastuse = jiffies;
	if (est) {
		int err = gen_new_estimator(&p->tcfc_bstats, &p->tcfc_rate_est,
					    &p->tcfc_lock, est);
		if (err) {
			kfree(p);
			return ERR_PTR(err);
		}
	}

	a->priv = (void *) p;
	return p;
}
EXPORT_SYMBOL(tcf_hash_create);

void tcf_hash_insert(struct tcf_common *p, struct tcf_hashinfo *hinfo)
{
	unsigned int h = tcf_hash(p->tcfc_index, hinfo->hmask);

	write_lock_bh(hinfo->lock);
	p->tcfc_next = hinfo->htab[h];
	hinfo->htab[h] = p;
	write_unlock_bh(hinfo->lock);
}
EXPORT_SYMBOL(tcf_hash_insert);

static struct tc_action_ops *act_base = NULL;
static DEFINE_RWLOCK(act_mod_lock);

int tcf_register_action(struct tc_action_ops *act)
{
	struct tc_action_ops *a, **ap;

	write_lock(&act_mod_lock);
	for (ap = &act_base; (a = *ap) != NULL; ap = &a->next) {
		if (act->type == a->type || (strcmp(act->kind, a->kind) == 0)) {
			write_unlock(&act_mod_lock);
			return -EEXIST;
		}
	}
	act->next = NULL;
	*ap = act;
	write_unlock(&act_mod_lock);
	return 0;
}
EXPORT_SYMBOL(tcf_register_action);

int tcf_unregister_action(struct tc_action_ops *act)
{
	struct tc_action_ops *a, **ap;
	int err = -ENOENT;

	write_lock(&act_mod_lock);
	for (ap = &act_base; (a = *ap) != NULL; ap = &a->next)
		if (a == act)
			break;
	if (a) {
		*ap = a->next;
		a->next = NULL;
		err = 0;
	}
	write_unlock(&act_mod_lock);
	return err;
}
EXPORT_SYMBOL(tcf_unregister_action);

/* lookup by name */
static struct tc_action_ops *tc_lookup_action_n(char *kind)
{
	struct tc_action_ops *a = NULL;

	if (kind) {
		read_lock(&act_mod_lock);
		for (a = act_base; a; a = a->next) {
			if (strcmp(kind, a->kind) == 0) {
				if (!try_module_get(a->owner)) {
					read_unlock(&act_mod_lock);
					return NULL;
				}
				break;
			}
		}
		read_unlock(&act_mod_lock);
	}
	return a;
}

/* lookup by nlattr */
static struct tc_action_ops *tc_lookup_action(struct nlattr *kind)
{
	struct tc_action_ops *a = NULL;

	if (kind) {
		read_lock(&act_mod_lock);
		for (a = act_base; a; a = a->next) {
			if (nla_strcmp(kind, a->kind) == 0) {
				if (!try_module_get(a->owner)) {
					read_unlock(&act_mod_lock);
					return NULL;
				}
				break;
			}
		}
		read_unlock(&act_mod_lock);
	}
	return a;
}

#if 0
/* lookup by id */
static struct tc_action_ops *tc_lookup_action_id(u32 type)
{
	struct tc_action_ops *a = NULL;

	if (type) {
		read_lock(&act_mod_lock);
		for (a = act_base; a; a = a->next) {
			if (a->type == type) {
				if (!try_module_get(a->owner)) {
					read_unlock(&act_mod_lock);
					return NULL;
				}
				break;
			}
		}
		read_unlock(&act_mod_lock);
	}
	return a;
}
#endif

int tcf_action_exec(struct sk_buff *skb, const struct tc_action *act,
		    struct tcf_result *res)
{
	const struct tc_action *a;
	int ret = -1;

	if (skb->tc_verd & TC_NCLS) {
		skb->tc_verd = CLR_TC_NCLS(skb->tc_verd);
		ret = TC_ACT_OK;
		goto exec_done;
	}
	while ((a = act) != NULL) {
repeat:
		if (a->ops && a->ops->act) {
			ret = a->ops->act(skb, a, res);
			if (TC_MUNGED & skb->tc_verd) {
				/* copied already, allow trampling */
				skb->tc_verd = SET_TC_OK2MUNGE(skb->tc_verd);
				skb->tc_verd = CLR_TC_MUNGED(skb->tc_verd);
			}
			if (ret == TC_ACT_REPEAT)
				goto repeat;	/* we need a ttl - JHS */
			if (ret != TC_ACT_PIPE)
				goto exec_done;
		}
		act = a->next;
	}
exec_done:
	return ret;
}
EXPORT_SYMBOL(tcf_action_exec);

void tcf_action_destroy(struct tc_action *act, int bind)
{
	struct tc_action *a;

	for (a = act; a; a = act) {
		if (a->ops && a->ops->cleanup) {
			if (a->ops->cleanup(a, bind) == ACT_P_DELETED)
				module_put(a->ops->owner);
			act = act->next;
			kfree(a);
		} else {
			/*FIXME: Remove later - catch insertion bugs*/
			WARN(1, "tcf_action_destroy: BUG? destroying NULL ops\n");
			act = act->next;
			kfree(a);
		}
	}
}

int
tcf_action_dump_old(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	int err = -EINVAL;

	if (a->ops == NULL || a->ops->dump == NULL)
		return err;
	return a->ops->dump(skb, a, bind, ref);
}

int
tcf_action_dump_1(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	int err = -EINVAL;
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;

	if (a->ops == NULL || a->ops->dump == NULL)
		return err;

	if (nla_put_string(skb, TCA_KIND, a->ops->kind))
		goto nla_put_failure;
	if (tcf_action_copy_stats(skb, a, 0))
		goto nla_put_failure;
	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;
	err = tcf_action_dump_old(skb, a, bind, ref);
	if (err > 0) {
		nla_nest_end(skb, nest);
		return err;
	}

nla_put_failure:
	nlmsg_trim(skb, b);
	return -1;
}
EXPORT_SYMBOL(tcf_action_dump_1);

int
tcf_action_dump(struct sk_buff *skb, struct tc_action *act, int bind, int ref)
{
	struct tc_action *a;
	int err = -EINVAL;
	struct nlattr *nest;

	while ((a = act) != NULL) {
		act = a->next;
		nest = nla_nest_start(skb, a->order);
		if (nest == NULL)
			goto nla_put_failure;
		err = tcf_action_dump_1(skb, a, bind, ref);
		if (err < 0)
			goto errout;
		nla_nest_end(skb, nest);
	}

	return 0;

nla_put_failure:
	err = -EINVAL;
errout:
	nla_nest_cancel(skb, nest);
	return err;
}

struct tc_action *tcf_action_init_1(struct nlattr *nla, struct nlattr *est,
				    char *name, int ovr, int bind)
{
	struct tc_action *a;
	struct tc_action_ops *a_o;
	char act_name[IFNAMSIZ];
	struct nlattr *tb[TCA_ACT_MAX + 1];
	struct nlattr *kind;
	int err;

	if (name == NULL) {
		err = nla_parse_nested(tb, TCA_ACT_MAX, nla, NULL);
		if (err < 0)
			goto err_out;
		err = -EINVAL;
		kind = tb[TCA_ACT_KIND];
		if (kind == NULL)
			goto err_out;
		if (nla_strlcpy(act_name, kind, IFNAMSIZ) >= IFNAMSIZ)
			goto err_out;
	} else {
		err = -EINVAL;
		if (strlcpy(act_name, name, IFNAMSIZ) >= IFNAMSIZ)
			goto err_out;
	}

	a_o = tc_lookup_action_n(act_name);
	if (a_o == NULL) {
#ifdef CONFIG_MODULES
		rtnl_unlock();
		request_module("act_%s", act_name);
		rtnl_lock();

		a_o = tc_lookup_action_n(act_name);

		/* We dropped the RTNL semaphore in order to
		 * perform the module load.  So, even if we
		 * succeeded in loading the module we have to
		 * tell the caller to replay the request.  We
		 * indicate this using -EAGAIN.
		 */
		if (a_o != NULL) {
			err = -EAGAIN;
			goto err_mod;
		}
#endif
		err = -ENOENT;
		goto err_out;
	}

	err = -ENOMEM;
	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (a == NULL)
		goto err_mod;

	/* backward compatibility for policer */
	if (name == NULL)
		err = a_o->init(tb[TCA_ACT_OPTIONS], est, a, ovr, bind);
	else
		err = a_o->init(nla, est, a, ovr, bind);
	if (err < 0)
		goto err_free;

	/* module count goes up only when brand new policy is created
	 * if it exists and is only bound to in a_o->init() then
	 * ACT_P_CREATED is not returned (a zero is).
	 */
	if (err != ACT_P_CREATED)
		module_put(a_o->owner);
	a->ops = a_o;

	return a;

err_free:
	kfree(a);
err_mod:
	module_put(a_o->owner);
err_out:
	return ERR_PTR(err);
}

struct tc_action *tcf_action_init(struct nlattr *nla, struct nlattr *est,
				  char *name, int ovr, int bind)
{
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct tc_action *head = NULL, *act, *act_prev = NULL;
	int err;
	int i;

	err = nla_parse_nested(tb, TCA_ACT_MAX_PRIO, nla, NULL);
	if (err < 0)
		return ERR_PTR(err);

	for (i = 1; i <= TCA_ACT_MAX_PRIO && tb[i]; i++) {
		act = tcf_action_init_1(tb[i], est, name, ovr, bind);
		if (IS_ERR(act))
			goto err;
		act->order = i;

		if (head == NULL)
			head = act;
		else
			act_prev->next = act;
		act_prev = act;
	}
	return head;

err:
	if (head != NULL)
		tcf_action_destroy(head, bind);
	return act;
}

int tcf_action_copy_stats(struct sk_buff *skb, struct tc_action *a,
			  int compat_mode)
{
	int err = 0;
	struct gnet_dump d;
	struct tcf_act_hdr *h = a->priv;

	if (h == NULL)
		goto errout;

	/* compat_mode being true specifies a call that is supposed
	 * to add additional backward compatibility statistic TLVs.
	 */
	if (compat_mode) {
		if (a->type == TCA_OLD_COMPAT)
			err = gnet_stats_start_copy_compat(skb, 0,
				TCA_STATS, TCA_XSTATS, &h->tcf_lock, &d);
		else
			return 0;
	} else
		err = gnet_stats_start_copy(skb, TCA_ACT_STATS,
					    &h->tcf_lock, &d);

	if (err < 0)
		goto errout;

	if (a->ops != NULL && a->ops->get_stats != NULL)
		if (a->ops->get_stats(skb, a) < 0)
			goto errout;

	if (gnet_stats_copy_basic(&d, &h->tcf_bstats) < 0 ||
	    gnet_stats_copy_rate_est(&d, &h->tcf_bstats,
				     &h->tcf_rate_est) < 0 ||
	    gnet_stats_copy_queue(&d, &h->tcf_qstats) < 0)
		goto errout;

	if (gnet_stats_finish_copy(&d) < 0)
		goto errout;

	return 0;

errout:
	return -1;
}

static int
tca_get_fill(struct sk_buff *skb, struct tc_action *a, u32 pid, u32 seq,
	     u16 flags, int event, int bind, int ref)
{
	struct tcamsg *t;
	struct nlmsghdr *nlh;
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(*t), flags);
	if (!nlh)
		goto out_nlmsg_trim;
	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (nest == NULL)
		goto out_nlmsg_trim;

	if (tcf_action_dump(skb, a, bind, ref) < 0)
		goto out_nlmsg_trim;

	nla_nest_end(skb, nest);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

out_nlmsg_trim:
	nlmsg_trim(skb, b);
	return -1;
}

static int
act_get_notify(struct net *net, u32 pid, struct nlmsghdr *n,
	       struct tc_action *a, int event)
{
	struct sk_buff *skb;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;
	if (tca_get_fill(skb, a, pid, n->nlmsg_seq, 0, event, 0, 0) <= 0) {
		kfree_skb(skb);
		return -EINVAL;
	}

	return rtnl_unicast(skb, net, pid);
}

static struct tc_action *
tcf_action_get_1(struct nlattr *nla, struct nlmsghdr *n, u32 pid)
{
	struct nlattr *tb[TCA_ACT_MAX + 1];
	struct tc_action *a;
	int index;
	int err;

	err = nla_parse_nested(tb, TCA_ACT_MAX, nla, NULL);
	if (err < 0)
		goto err_out;

	err = -EINVAL;
	if (tb[TCA_ACT_INDEX] == NULL ||
	    nla_len(tb[TCA_ACT_INDEX]) < sizeof(index))
		goto err_out;
	index = nla_get_u32(tb[TCA_ACT_INDEX]);

	err = -ENOMEM;
	a = kzalloc(sizeof(struct tc_action), GFP_KERNEL);
	if (a == NULL)
		goto err_out;

	err = -EINVAL;
	a->ops = tc_lookup_action(tb[TCA_ACT_KIND]);
	if (a->ops == NULL)
		goto err_free;
	if (a->ops->lookup == NULL)
		goto err_mod;
	err = -ENOENT;
	if (a->ops->lookup(a, index) == 0)
		goto err_mod;

	module_put(a->ops->owner);
	return a;

err_mod:
	module_put(a->ops->owner);
err_free:
	kfree(a);
err_out:
	return ERR_PTR(err);
}

static void cleanup_a(struct tc_action *act)
{
	struct tc_action *a;

	for (a = act; a; a = act) {
		act = a->next;
		kfree(a);
	}
}

static struct tc_action *create_a(int i)
{
	struct tc_action *act;

	act = kzalloc(sizeof(*act), GFP_KERNEL);
	if (act == NULL) {
		pr_debug("create_a: failed to alloc!\n");
		return NULL;
	}
	act->order = i;
	return act;
}

static int tca_action_flush(struct net *net, struct nlattr *nla,
			    struct nlmsghdr *n, u32 pid)
{
	struct sk_buff *skb;
	unsigned char *b;
	struct nlmsghdr *nlh;
	struct tcamsg *t;
	struct netlink_callback dcb;
	struct nlattr *nest;
	struct nlattr *tb[TCA_ACT_MAX + 1];
	struct nlattr *kind;
	struct tc_action *a = create_a(0);
	int err = -ENOMEM;

	if (a == NULL) {
		pr_debug("tca_action_flush: couldnt create tc_action\n");
		return err;
	}

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb) {
		pr_debug("tca_action_flush: failed skb alloc\n");
		kfree(a);
		return err;
	}

	b = skb_tail_pointer(skb);

	err = nla_parse_nested(tb, TCA_ACT_MAX, nla, NULL);
	if (err < 0)
		goto err_out;

	err = -EINVAL;
	kind = tb[TCA_ACT_KIND];
	a->ops = tc_lookup_action(kind);
	if (a->ops == NULL)
		goto err_out;

	nlh = nlmsg_put(skb, pid, n->nlmsg_seq, RTM_DELACTION, sizeof(*t), 0);
	if (!nlh)
		goto out_module_put;
	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (nest == NULL)
		goto out_module_put;

	err = a->ops->walk(skb, &dcb, RTM_DELACTION, a);
	if (err < 0)
		goto out_module_put;
	if (err == 0)
		goto noflush_out;

	nla_nest_end(skb, nest);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	nlh->nlmsg_flags |= NLM_F_ROOT;
	module_put(a->ops->owner);
	kfree(a);
	err = rtnetlink_send(skb, net, pid, RTNLGRP_TC,
			     n->nlmsg_flags & NLM_F_ECHO);
	if (err > 0)
		return 0;

	return err;

out_module_put:
	module_put(a->ops->owner);
err_out:
noflush_out:
	kfree_skb(skb);
	kfree(a);
	return err;
}

static int
tca_action_gd(struct net *net, struct nlattr *nla, struct nlmsghdr *n,
	      u32 pid, int event)
{
	int i, ret;
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct tc_action *head = NULL, *act, *act_prev = NULL;

	ret = nla_parse_nested(tb, TCA_ACT_MAX_PRIO, nla, NULL);
	if (ret < 0)
		return ret;

	if (event == RTM_DELACTION && n->nlmsg_flags & NLM_F_ROOT) {
		if (tb[1] != NULL)
			return tca_action_flush(net, tb[1], n, pid);
		else
			return -EINVAL;
	}

	for (i = 1; i <= TCA_ACT_MAX_PRIO && tb[i]; i++) {
		act = tcf_action_get_1(tb[i], n, pid);
		if (IS_ERR(act)) {
			ret = PTR_ERR(act);
			goto err;
		}
		act->order = i;

		if (head == NULL)
			head = act;
		else
			act_prev->next = act;
		act_prev = act;
	}

	if (event == RTM_GETACTION)
		ret = act_get_notify(net, pid, n, head, event);
	else { /* delete */
		struct sk_buff *skb;

		skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
		if (!skb) {
			ret = -ENOBUFS;
			goto err;
		}

		if (tca_get_fill(skb, head, pid, n->nlmsg_seq, 0, event,
				 0, 1) <= 0) {
			kfree_skb(skb);
			ret = -EINVAL;
			goto err;
		}

		/* now do the delete */
		tcf_action_destroy(head, 0);
		ret = rtnetlink_send(skb, net, pid, RTNLGRP_TC,
				     n->nlmsg_flags & NLM_F_ECHO);
		if (ret > 0)
			return 0;
		return ret;
	}
err:
	cleanup_a(head);
	return ret;
}

static int tcf_add_notify(struct net *net, struct tc_action *a,
			  u32 pid, u32 seq, int event, u16 flags)
{
	struct tcamsg *t;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	struct nlattr *nest;
	unsigned char *b;
	int err = 0;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	b = skb_tail_pointer(skb);

	nlh = nlmsg_put(skb, pid, seq, event, sizeof(*t), flags);
	if (!nlh)
		goto out_kfree_skb;
	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (nest == NULL)
		goto out_kfree_skb;

	if (tcf_action_dump(skb, a, 0, 0) < 0)
		goto out_kfree_skb;

	nla_nest_end(skb, nest);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	NETLINK_CB(skb).dst_group = RTNLGRP_TC;

	err = rtnetlink_send(skb, net, pid, RTNLGRP_TC, flags & NLM_F_ECHO);
	if (err > 0)
		err = 0;
	return err;

out_kfree_skb:
	kfree_skb(skb);
	return -1;
}


static int
tcf_action_add(struct net *net, struct nlattr *nla, struct nlmsghdr *n,
	       u32 pid, int ovr)
{
	int ret = 0;
	struct tc_action *act;
	struct tc_action *a;
	u32 seq = n->nlmsg_seq;

	act = tcf_action_init(nla, NULL, NULL, ovr, 0);
	if (act == NULL)
		goto done;
	if (IS_ERR(act)) {
		ret = PTR_ERR(act);
		goto done;
	}

	/* dump then free all the actions after update; inserted policy
	 * stays intact
	 */
	ret = tcf_add_notify(net, act, pid, seq, RTM_NEWACTION, n->nlmsg_flags);
	for (a = act; a; a = act) {
		act = a->next;
		kfree(a);
	}
done:
	return ret;
}

static int tc_ctl_action(struct sk_buff *skb, struct nlmsghdr *n, void *arg)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_ACT_MAX + 1];
	u32 pid = skb ? NETLINK_CB(skb).pid : 0;
	int ret = 0, ovr = 0;

	ret = nlmsg_parse(n, sizeof(struct tcamsg), tca, TCA_ACT_MAX, NULL);
	if (ret < 0)
		return ret;

	if (tca[TCA_ACT_TAB] == NULL) {
		pr_notice("tc_ctl_action: received NO action attribs\n");
		return -EINVAL;
	}

	/* n->nlmsg_flags & NLM_F_CREATE */
	switch (n->nlmsg_type) {
	case RTM_NEWACTION:
		/* we are going to assume all other flags
		 * imply create only if it doesn't exist
		 * Note that CREATE | EXCL implies that
		 * but since we want avoid ambiguity (eg when flags
		 * is zero) then just set this
		 */
		if (n->nlmsg_flags & NLM_F_REPLACE)
			ovr = 1;
replay:
		ret = tcf_action_add(net, tca[TCA_ACT_TAB], n, pid, ovr);
		if (ret == -EAGAIN)
			goto replay;
		break;
	case RTM_DELACTION:
		ret = tca_action_gd(net, tca[TCA_ACT_TAB], n,
				    pid, RTM_DELACTION);
		break;
	case RTM_GETACTION:
		ret = tca_action_gd(net, tca[TCA_ACT_TAB], n,
				    pid, RTM_GETACTION);
		break;
	default:
		BUG();
	}

	return ret;
}

static struct nlattr *
find_dump_kind(const struct nlmsghdr *n)
{
	struct nlattr *tb1, *tb2[TCA_ACT_MAX + 1];
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct nlattr *nla[TCAA_MAX + 1];
	struct nlattr *kind;

	if (nlmsg_parse(n, sizeof(struct tcamsg), nla, TCAA_MAX, NULL) < 0)
		return NULL;
	tb1 = nla[TCA_ACT_TAB];
	if (tb1 == NULL)
		return NULL;

	if (nla_parse(tb, TCA_ACT_MAX_PRIO, nla_data(tb1),
		      NLMSG_ALIGN(nla_len(tb1)), NULL) < 0)
		return NULL;

	if (tb[1] == NULL)
		return NULL;
	if (nla_parse(tb2, TCA_ACT_MAX, nla_data(tb[1]),
		      nla_len(tb[1]), NULL) < 0)
		return NULL;
	kind = tb2[TCA_ACT_KIND];

	return kind;
}

static int
tc_dump_action(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nlmsghdr *nlh;
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;
	struct tc_action_ops *a_o;
	struct tc_action a;
	int ret = 0;
	struct tcamsg *t = (struct tcamsg *) nlmsg_data(cb->nlh);
	struct nlattr *kind = find_dump_kind(cb->nlh);

	if (kind == NULL) {
		pr_info("tc_dump_action: action bad kind\n");
		return 0;
	}

	a_o = tc_lookup_action(kind);
	if (a_o == NULL)
		return 0;

	memset(&a, 0, sizeof(struct tc_action));
	a.ops = a_o;

	if (a_o->walk == NULL) {
		WARN(1, "tc_dump_action: %s !capable of dumping table\n",
		     a_o->kind);
		goto out_module_put;
	}

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).pid, cb->nlh->nlmsg_seq,
			cb->nlh->nlmsg_type, sizeof(*t), 0);
	if (!nlh)
		goto out_module_put;
	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (nest == NULL)
		goto out_module_put;

	ret = a_o->walk(skb, cb, RTM_GETACTION, &a);
	if (ret < 0)
		goto out_module_put;

	if (ret > 0) {
		nla_nest_end(skb, nest);
		ret = skb->len;
	} else
		nla_nest_cancel(skb, nest);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	if (NETLINK_CB(cb->skb).pid && ret)
		nlh->nlmsg_flags |= NLM_F_MULTI;
	module_put(a_o->owner);
	return skb->len;

out_module_put:
	module_put(a_o->owner);
	nlmsg_trim(skb, b);
	return skb->len;
}

static int __init tc_action_init(void)
{
	rtnl_register(PF_UNSPEC, RTM_NEWACTION, tc_ctl_action, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_DELACTION, tc_ctl_action, NULL, NULL);
	rtnl_register(PF_UNSPEC, RTM_GETACTION, tc_ctl_action, tc_dump_action,
		      NULL);

	return 0;
}

subsys_initcall(tc_action_init);
