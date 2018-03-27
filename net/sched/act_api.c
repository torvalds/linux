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
#include <linux/rhashtable.h>
#include <linux/list.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/sch_generic.h>
#include <net/pkt_cls.h>
#include <net/act_api.h>
#include <net/netlink.h>

static int tcf_action_goto_chain_init(struct tc_action *a, struct tcf_proto *tp)
{
	u32 chain_index = a->tcfa_action & TC_ACT_EXT_VAL_MASK;

	if (!tp)
		return -EINVAL;
	a->goto_chain = tcf_chain_get(tp->chain->block, chain_index, true);
	if (!a->goto_chain)
		return -ENOMEM;
	return 0;
}

static void tcf_action_goto_chain_fini(struct tc_action *a)
{
	tcf_chain_put(a->goto_chain);
}

static void tcf_action_goto_chain_exec(const struct tc_action *a,
				       struct tcf_result *res)
{
	const struct tcf_chain *chain = a->goto_chain;

	res->goto_tp = rcu_dereference_bh(chain->filter_chain);
}

/* XXX: For standalone actions, we don't need a RCU grace period either, because
 * actions are always connected to filters and filters are already destroyed in
 * RCU callbacks, so after a RCU grace period actions are already disconnected
 * from filters. Readers later can not find us.
 */
static void free_tcf(struct tc_action *p)
{
	free_percpu(p->cpu_bstats);
	free_percpu(p->cpu_qstats);

	if (p->act_cookie) {
		kfree(p->act_cookie->data);
		kfree(p->act_cookie);
	}
	if (p->goto_chain)
		tcf_action_goto_chain_fini(p);

	kfree(p);
}

static void tcf_idr_remove(struct tcf_idrinfo *idrinfo, struct tc_action *p)
{
	spin_lock_bh(&idrinfo->lock);
	idr_remove(&idrinfo->action_idr, p->tcfa_index);
	spin_unlock_bh(&idrinfo->lock);
	gen_kill_estimator(&p->tcfa_rate_est);
	free_tcf(p);
}

int __tcf_idr_release(struct tc_action *p, bool bind, bool strict)
{
	int ret = 0;

	ASSERT_RTNL();

	if (p) {
		if (bind)
			p->tcfa_bindcnt--;
		else if (strict && p->tcfa_bindcnt > 0)
			return -EPERM;

		p->tcfa_refcnt--;
		if (p->tcfa_bindcnt <= 0 && p->tcfa_refcnt <= 0) {
			if (p->ops->cleanup)
				p->ops->cleanup(p);
			tcf_idr_remove(p->idrinfo, p);
			ret = ACT_P_DELETED;
		}
	}

	return ret;
}
EXPORT_SYMBOL(__tcf_idr_release);

static size_t tcf_action_shared_attrs_size(const struct tc_action *act)
{
	u32 cookie_len = 0;

	if (act->act_cookie)
		cookie_len = nla_total_size(act->act_cookie->len);

	return  nla_total_size(0) /* action number nested */
		+ nla_total_size(IFNAMSIZ) /* TCA_ACT_KIND */
		+ cookie_len /* TCA_ACT_COOKIE */
		+ nla_total_size(0) /* TCA_ACT_STATS nested */
		/* TCA_STATS_BASIC */
		+ nla_total_size_64bit(sizeof(struct gnet_stats_basic))
		/* TCA_STATS_QUEUE */
		+ nla_total_size_64bit(sizeof(struct gnet_stats_queue))
		+ nla_total_size(0) /* TCA_OPTIONS nested */
		+ nla_total_size(sizeof(struct tcf_t)); /* TCA_GACT_TM */
}

static size_t tcf_action_full_attrs_size(size_t sz)
{
	return NLMSG_HDRLEN                     /* struct nlmsghdr */
		+ sizeof(struct tcamsg)
		+ nla_total_size(0)             /* TCA_ACT_TAB nested */
		+ sz;
}

static size_t tcf_action_fill_size(const struct tc_action *act)
{
	size_t sz = tcf_action_shared_attrs_size(act);

	if (act->ops->get_fill_size)
		return act->ops->get_fill_size(act) + sz;
	return sz;
}

static int tcf_dump_walker(struct tcf_idrinfo *idrinfo, struct sk_buff *skb,
			   struct netlink_callback *cb)
{
	int err = 0, index = -1, s_i = 0, n_i = 0;
	u32 act_flags = cb->args[2];
	unsigned long jiffy_since = cb->args[3];
	struct nlattr *nest;
	struct idr *idr = &idrinfo->action_idr;
	struct tc_action *p;
	unsigned long id = 1;

	spin_lock_bh(&idrinfo->lock);

	s_i = cb->args[0];

	idr_for_each_entry_ul(idr, p, id) {
		index++;
		if (index < s_i)
			continue;

		if (jiffy_since &&
		    time_after(jiffy_since,
			       (unsigned long)p->tcfa_tm.lastuse))
			continue;

		nest = nla_nest_start(skb, n_i);
		if (!nest)
			goto nla_put_failure;
		err = tcf_action_dump_1(skb, p, 0, 0);
		if (err < 0) {
			index--;
			nlmsg_trim(skb, nest);
			goto done;
		}
		nla_nest_end(skb, nest);
		n_i++;
		if (!(act_flags & TCA_FLAG_LARGE_DUMP_ON) &&
		    n_i >= TCA_ACT_MAX_PRIO)
			goto done;
	}
done:
	if (index >= 0)
		cb->args[0] = index + 1;

	spin_unlock_bh(&idrinfo->lock);
	if (n_i) {
		if (act_flags & TCA_FLAG_LARGE_DUMP_ON)
			cb->args[1] = n_i;
	}
	return n_i;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	goto done;
}

static int tcf_del_walker(struct tcf_idrinfo *idrinfo, struct sk_buff *skb,
			  const struct tc_action_ops *ops)
{
	struct nlattr *nest;
	int n_i = 0;
	int ret = -EINVAL;
	struct idr *idr = &idrinfo->action_idr;
	struct tc_action *p;
	unsigned long id = 1;

	nest = nla_nest_start(skb, 0);
	if (nest == NULL)
		goto nla_put_failure;
	if (nla_put_string(skb, TCA_KIND, ops->kind))
		goto nla_put_failure;

	idr_for_each_entry_ul(idr, p, id) {
		ret = __tcf_idr_release(p, false, true);
		if (ret == ACT_P_DELETED) {
			module_put(ops->owner);
			n_i++;
		} else if (ret < 0) {
			goto nla_put_failure;
		}
	}
	if (nla_put_u32(skb, TCA_FCNT, n_i))
		goto nla_put_failure;
	nla_nest_end(skb, nest);

	return n_i;
nla_put_failure:
	nla_nest_cancel(skb, nest);
	return ret;
}

int tcf_generic_walker(struct tc_action_net *tn, struct sk_buff *skb,
		       struct netlink_callback *cb, int type,
		       const struct tc_action_ops *ops,
		       struct netlink_ext_ack *extack)
{
	struct tcf_idrinfo *idrinfo = tn->idrinfo;

	if (type == RTM_DELACTION) {
		return tcf_del_walker(idrinfo, skb, ops);
	} else if (type == RTM_GETACTION) {
		return tcf_dump_walker(idrinfo, skb, cb);
	} else {
		WARN(1, "tcf_generic_walker: unknown command %d\n", type);
		NL_SET_ERR_MSG(extack, "tcf_generic_walker: unknown command");
		return -EINVAL;
	}
}
EXPORT_SYMBOL(tcf_generic_walker);

static struct tc_action *tcf_idr_lookup(u32 index, struct tcf_idrinfo *idrinfo)
{
	struct tc_action *p = NULL;

	spin_lock_bh(&idrinfo->lock);
	p = idr_find(&idrinfo->action_idr, index);
	spin_unlock_bh(&idrinfo->lock);

	return p;
}

int tcf_idr_search(struct tc_action_net *tn, struct tc_action **a, u32 index)
{
	struct tcf_idrinfo *idrinfo = tn->idrinfo;
	struct tc_action *p = tcf_idr_lookup(index, idrinfo);

	if (p) {
		*a = p;
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL(tcf_idr_search);

bool tcf_idr_check(struct tc_action_net *tn, u32 index, struct tc_action **a,
		   int bind)
{
	struct tcf_idrinfo *idrinfo = tn->idrinfo;
	struct tc_action *p = tcf_idr_lookup(index, idrinfo);

	if (index && p) {
		if (bind)
			p->tcfa_bindcnt++;
		p->tcfa_refcnt++;
		*a = p;
		return true;
	}
	return false;
}
EXPORT_SYMBOL(tcf_idr_check);

int tcf_idr_create(struct tc_action_net *tn, u32 index, struct nlattr *est,
		   struct tc_action **a, const struct tc_action_ops *ops,
		   int bind, bool cpustats)
{
	struct tc_action *p = kzalloc(ops->size, GFP_KERNEL);
	struct tcf_idrinfo *idrinfo = tn->idrinfo;
	struct idr *idr = &idrinfo->action_idr;
	int err = -ENOMEM;

	if (unlikely(!p))
		return -ENOMEM;
	p->tcfa_refcnt = 1;
	if (bind)
		p->tcfa_bindcnt = 1;

	if (cpustats) {
		p->cpu_bstats = netdev_alloc_pcpu_stats(struct gnet_stats_basic_cpu);
		if (!p->cpu_bstats)
			goto err1;
		p->cpu_qstats = alloc_percpu(struct gnet_stats_queue);
		if (!p->cpu_qstats)
			goto err2;
	}
	spin_lock_init(&p->tcfa_lock);
	idr_preload(GFP_KERNEL);
	spin_lock_bh(&idrinfo->lock);
	/* user doesn't specify an index */
	if (!index) {
		index = 1;
		err = idr_alloc_u32(idr, NULL, &index, UINT_MAX, GFP_ATOMIC);
	} else {
		err = idr_alloc_u32(idr, NULL, &index, index, GFP_ATOMIC);
	}
	spin_unlock_bh(&idrinfo->lock);
	idr_preload_end();
	if (err)
		goto err3;

	p->tcfa_index = index;
	p->tcfa_tm.install = jiffies;
	p->tcfa_tm.lastuse = jiffies;
	p->tcfa_tm.firstuse = 0;
	if (est) {
		err = gen_new_estimator(&p->tcfa_bstats, p->cpu_bstats,
					&p->tcfa_rate_est,
					&p->tcfa_lock, NULL, est);
		if (err)
			goto err4;
	}

	p->idrinfo = idrinfo;
	p->ops = ops;
	INIT_LIST_HEAD(&p->list);
	*a = p;
	return 0;
err4:
	idr_remove(idr, index);
err3:
	free_percpu(p->cpu_qstats);
err2:
	free_percpu(p->cpu_bstats);
err1:
	kfree(p);
	return err;
}
EXPORT_SYMBOL(tcf_idr_create);

void tcf_idr_insert(struct tc_action_net *tn, struct tc_action *a)
{
	struct tcf_idrinfo *idrinfo = tn->idrinfo;

	spin_lock_bh(&idrinfo->lock);
	idr_replace(&idrinfo->action_idr, a, a->tcfa_index);
	spin_unlock_bh(&idrinfo->lock);
}
EXPORT_SYMBOL(tcf_idr_insert);

void tcf_idrinfo_destroy(const struct tc_action_ops *ops,
			 struct tcf_idrinfo *idrinfo)
{
	struct idr *idr = &idrinfo->action_idr;
	struct tc_action *p;
	int ret;
	unsigned long id = 1;

	idr_for_each_entry_ul(idr, p, id) {
		ret = __tcf_idr_release(p, false, true);
		if (ret == ACT_P_DELETED)
			module_put(ops->owner);
		else if (ret < 0)
			return;
	}
	idr_destroy(&idrinfo->action_idr);
}
EXPORT_SYMBOL(tcf_idrinfo_destroy);

static LIST_HEAD(act_base);
static DEFINE_RWLOCK(act_mod_lock);

int tcf_register_action(struct tc_action_ops *act,
			struct pernet_operations *ops)
{
	struct tc_action_ops *a;
	int ret;

	if (!act->act || !act->dump || !act->init || !act->walk || !act->lookup)
		return -EINVAL;

	/* We have to register pernet ops before making the action ops visible,
	 * otherwise tcf_action_init_1() could get a partially initialized
	 * netns.
	 */
	ret = register_pernet_subsys(ops);
	if (ret)
		return ret;

	write_lock(&act_mod_lock);
	list_for_each_entry(a, &act_base, head) {
		if (act->type == a->type || (strcmp(act->kind, a->kind) == 0)) {
			write_unlock(&act_mod_lock);
			unregister_pernet_subsys(ops);
			return -EEXIST;
		}
	}
	list_add_tail(&act->head, &act_base);
	write_unlock(&act_mod_lock);

	return 0;
}
EXPORT_SYMBOL(tcf_register_action);

int tcf_unregister_action(struct tc_action_ops *act,
			  struct pernet_operations *ops)
{
	struct tc_action_ops *a;
	int err = -ENOENT;

	write_lock(&act_mod_lock);
	list_for_each_entry(a, &act_base, head) {
		if (a == act) {
			list_del(&act->head);
			err = 0;
			break;
		}
	}
	write_unlock(&act_mod_lock);
	if (!err)
		unregister_pernet_subsys(ops);
	return err;
}
EXPORT_SYMBOL(tcf_unregister_action);

/* lookup by name */
static struct tc_action_ops *tc_lookup_action_n(char *kind)
{
	struct tc_action_ops *a, *res = NULL;

	if (kind) {
		read_lock(&act_mod_lock);
		list_for_each_entry(a, &act_base, head) {
			if (strcmp(kind, a->kind) == 0) {
				if (try_module_get(a->owner))
					res = a;
				break;
			}
		}
		read_unlock(&act_mod_lock);
	}
	return res;
}

/* lookup by nlattr */
static struct tc_action_ops *tc_lookup_action(struct nlattr *kind)
{
	struct tc_action_ops *a, *res = NULL;

	if (kind) {
		read_lock(&act_mod_lock);
		list_for_each_entry(a, &act_base, head) {
			if (nla_strcmp(kind, a->kind) == 0) {
				if (try_module_get(a->owner))
					res = a;
				break;
			}
		}
		read_unlock(&act_mod_lock);
	}
	return res;
}

/*TCA_ACT_MAX_PRIO is 32, there count upto 32 */
#define TCA_ACT_MAX_PRIO_MASK 0x1FF
int tcf_action_exec(struct sk_buff *skb, struct tc_action **actions,
		    int nr_actions, struct tcf_result *res)
{
	u32 jmp_prgcnt = 0;
	u32 jmp_ttl = TCA_ACT_MAX_PRIO; /*matches actions per filter */
	int i;
	int ret = TC_ACT_OK;

	if (skb_skip_tc_classify(skb))
		return TC_ACT_OK;

restart_act_graph:
	for (i = 0; i < nr_actions; i++) {
		const struct tc_action *a = actions[i];

		if (jmp_prgcnt > 0) {
			jmp_prgcnt -= 1;
			continue;
		}
repeat:
		ret = a->ops->act(skb, a, res);
		if (ret == TC_ACT_REPEAT)
			goto repeat;	/* we need a ttl - JHS */

		if (TC_ACT_EXT_CMP(ret, TC_ACT_JUMP)) {
			jmp_prgcnt = ret & TCA_ACT_MAX_PRIO_MASK;
			if (!jmp_prgcnt || (jmp_prgcnt > nr_actions)) {
				/* faulty opcode, stop pipeline */
				return TC_ACT_OK;
			} else {
				jmp_ttl -= 1;
				if (jmp_ttl > 0)
					goto restart_act_graph;
				else /* faulty graph, stop pipeline */
					return TC_ACT_OK;
			}
		} else if (TC_ACT_EXT_CMP(ret, TC_ACT_GOTO_CHAIN)) {
			tcf_action_goto_chain_exec(a, res);
		}

		if (ret != TC_ACT_PIPE)
			break;
	}

	return ret;
}
EXPORT_SYMBOL(tcf_action_exec);

int tcf_action_destroy(struct list_head *actions, int bind)
{
	const struct tc_action_ops *ops;
	struct tc_action *a, *tmp;
	int ret = 0;

	list_for_each_entry_safe(a, tmp, actions, list) {
		ops = a->ops;
		ret = __tcf_idr_release(a, bind, true);
		if (ret == ACT_P_DELETED)
			module_put(ops->owner);
		else if (ret < 0)
			return ret;
	}
	return ret;
}

int
tcf_action_dump_old(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	return a->ops->dump(skb, a, bind, ref);
}

int
tcf_action_dump_1(struct sk_buff *skb, struct tc_action *a, int bind, int ref)
{
	int err = -EINVAL;
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;

	if (nla_put_string(skb, TCA_KIND, a->ops->kind))
		goto nla_put_failure;
	if (tcf_action_copy_stats(skb, a, 0))
		goto nla_put_failure;
	if (a->act_cookie) {
		if (nla_put(skb, TCA_ACT_COOKIE, a->act_cookie->len,
			    a->act_cookie->data))
			goto nla_put_failure;
	}

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

int tcf_action_dump(struct sk_buff *skb, struct list_head *actions,
		    int bind, int ref)
{
	struct tc_action *a;
	int err = -EINVAL;
	struct nlattr *nest;

	list_for_each_entry(a, actions, list) {
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

static struct tc_cookie *nla_memdup_cookie(struct nlattr **tb)
{
	struct tc_cookie *c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return NULL;

	c->data = nla_memdup(tb[TCA_ACT_COOKIE], GFP_KERNEL);
	if (!c->data) {
		kfree(c);
		return NULL;
	}
	c->len = nla_len(tb[TCA_ACT_COOKIE]);

	return c;
}

struct tc_action *tcf_action_init_1(struct net *net, struct tcf_proto *tp,
				    struct nlattr *nla, struct nlattr *est,
				    char *name, int ovr, int bind,
				    struct netlink_ext_ack *extack)
{
	struct tc_action *a;
	struct tc_action_ops *a_o;
	struct tc_cookie *cookie = NULL;
	char act_name[IFNAMSIZ];
	struct nlattr *tb[TCA_ACT_MAX + 1];
	struct nlattr *kind;
	int err;

	if (name == NULL) {
		err = nla_parse_nested(tb, TCA_ACT_MAX, nla, NULL, extack);
		if (err < 0)
			goto err_out;
		err = -EINVAL;
		kind = tb[TCA_ACT_KIND];
		if (!kind) {
			NL_SET_ERR_MSG(extack, "TC action kind must be specified");
			goto err_out;
		}
		if (nla_strlcpy(act_name, kind, IFNAMSIZ) >= IFNAMSIZ) {
			NL_SET_ERR_MSG(extack, "TC action name too long");
			goto err_out;
		}
		if (tb[TCA_ACT_COOKIE]) {
			int cklen = nla_len(tb[TCA_ACT_COOKIE]);

			if (cklen > TC_COOKIE_MAX_SIZE) {
				NL_SET_ERR_MSG(extack, "TC cookie size above the maximum");
				goto err_out;
			}

			cookie = nla_memdup_cookie(tb);
			if (!cookie) {
				NL_SET_ERR_MSG(extack, "No memory to generate TC cookie");
				err = -ENOMEM;
				goto err_out;
			}
		}
	} else {
		if (strlcpy(act_name, name, IFNAMSIZ) >= IFNAMSIZ) {
			NL_SET_ERR_MSG(extack, "TC action name too long");
			err = -EINVAL;
			goto err_out;
		}
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
		NL_SET_ERR_MSG(extack, "Failed to load TC action module");
		err = -ENOENT;
		goto err_out;
	}

	/* backward compatibility for policer */
	if (name == NULL)
		err = a_o->init(net, tb[TCA_ACT_OPTIONS], est, &a, ovr, bind,
				extack);
	else
		err = a_o->init(net, nla, est, &a, ovr, bind, extack);
	if (err < 0)
		goto err_mod;

	if (name == NULL && tb[TCA_ACT_COOKIE]) {
		if (a->act_cookie) {
			kfree(a->act_cookie->data);
			kfree(a->act_cookie);
		}
		a->act_cookie = cookie;
	}

	/* module count goes up only when brand new policy is created
	 * if it exists and is only bound to in a_o->init() then
	 * ACT_P_CREATED is not returned (a zero is).
	 */
	if (err != ACT_P_CREATED)
		module_put(a_o->owner);

	if (TC_ACT_EXT_CMP(a->tcfa_action, TC_ACT_GOTO_CHAIN)) {
		err = tcf_action_goto_chain_init(a, tp);
		if (err) {
			LIST_HEAD(actions);

			list_add_tail(&a->list, &actions);
			tcf_action_destroy(&actions, bind);
			NL_SET_ERR_MSG(extack, "Failed to init TC action chain");
			return ERR_PTR(err);
		}
	}

	return a;

err_mod:
	module_put(a_o->owner);
err_out:
	if (cookie) {
		kfree(cookie->data);
		kfree(cookie);
	}
	return ERR_PTR(err);
}

static void cleanup_a(struct list_head *actions, int ovr)
{
	struct tc_action *a;

	if (!ovr)
		return;

	list_for_each_entry(a, actions, list)
		a->tcfa_refcnt--;
}

int tcf_action_init(struct net *net, struct tcf_proto *tp, struct nlattr *nla,
		    struct nlattr *est, char *name, int ovr, int bind,
		    struct list_head *actions, size_t *attr_size,
		    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct tc_action *act;
	size_t sz = 0;
	int err;
	int i;

	err = nla_parse_nested(tb, TCA_ACT_MAX_PRIO, nla, NULL, extack);
	if (err < 0)
		return err;

	for (i = 1; i <= TCA_ACT_MAX_PRIO && tb[i]; i++) {
		act = tcf_action_init_1(net, tp, tb[i], est, name, ovr, bind,
					extack);
		if (IS_ERR(act)) {
			err = PTR_ERR(act);
			goto err;
		}
		act->order = i;
		sz += tcf_action_fill_size(act);
		if (ovr)
			act->tcfa_refcnt++;
		list_add_tail(&act->list, actions);
	}

	*attr_size = tcf_action_full_attrs_size(sz);

	/* Remove the temp refcnt which was necessary to protect against
	 * destroying an existing action which was being replaced
	 */
	cleanup_a(actions, ovr);
	return 0;

err:
	tcf_action_destroy(actions, bind);
	return err;
}

int tcf_action_copy_stats(struct sk_buff *skb, struct tc_action *p,
			  int compat_mode)
{
	int err = 0;
	struct gnet_dump d;

	if (p == NULL)
		goto errout;

	/* compat_mode being true specifies a call that is supposed
	 * to add additional backward compatibility statistic TLVs.
	 */
	if (compat_mode) {
		if (p->type == TCA_OLD_COMPAT)
			err = gnet_stats_start_copy_compat(skb, 0,
							   TCA_STATS,
							   TCA_XSTATS,
							   &p->tcfa_lock, &d,
							   TCA_PAD);
		else
			return 0;
	} else
		err = gnet_stats_start_copy(skb, TCA_ACT_STATS,
					    &p->tcfa_lock, &d, TCA_ACT_PAD);

	if (err < 0)
		goto errout;

	if (gnet_stats_copy_basic(NULL, &d, p->cpu_bstats, &p->tcfa_bstats) < 0 ||
	    gnet_stats_copy_rate_est(&d, &p->tcfa_rate_est) < 0 ||
	    gnet_stats_copy_queue(&d, p->cpu_qstats,
				  &p->tcfa_qstats,
				  p->tcfa_qstats.qlen) < 0)
		goto errout;

	if (gnet_stats_finish_copy(&d) < 0)
		goto errout;

	return 0;

errout:
	return -1;
}

static int tca_get_fill(struct sk_buff *skb, struct list_head *actions,
			u32 portid, u32 seq, u16 flags, int event, int bind,
			int ref)
{
	struct tcamsg *t;
	struct nlmsghdr *nlh;
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;

	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*t), flags);
	if (!nlh)
		goto out_nlmsg_trim;
	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (!nest)
		goto out_nlmsg_trim;

	if (tcf_action_dump(skb, actions, bind, ref) < 0)
		goto out_nlmsg_trim;

	nla_nest_end(skb, nest);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	return skb->len;

out_nlmsg_trim:
	nlmsg_trim(skb, b);
	return -1;
}

static int
tcf_get_notify(struct net *net, u32 portid, struct nlmsghdr *n,
	       struct list_head *actions, int event,
	       struct netlink_ext_ack *extack)
{
	struct sk_buff *skb;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;
	if (tca_get_fill(skb, actions, portid, n->nlmsg_seq, 0, event,
			 0, 0) <= 0) {
		NL_SET_ERR_MSG(extack, "Failed to fill netlink attributes while adding TC action");
		kfree_skb(skb);
		return -EINVAL;
	}

	return rtnl_unicast(skb, net, portid);
}

static struct tc_action *tcf_action_get_1(struct net *net, struct nlattr *nla,
					  struct nlmsghdr *n, u32 portid,
					  struct netlink_ext_ack *extack)
{
	struct nlattr *tb[TCA_ACT_MAX + 1];
	const struct tc_action_ops *ops;
	struct tc_action *a;
	int index;
	int err;

	err = nla_parse_nested(tb, TCA_ACT_MAX, nla, NULL, extack);
	if (err < 0)
		goto err_out;

	err = -EINVAL;
	if (tb[TCA_ACT_INDEX] == NULL ||
	    nla_len(tb[TCA_ACT_INDEX]) < sizeof(index)) {
		NL_SET_ERR_MSG(extack, "Invalid TC action index value");
		goto err_out;
	}
	index = nla_get_u32(tb[TCA_ACT_INDEX]);

	err = -EINVAL;
	ops = tc_lookup_action(tb[TCA_ACT_KIND]);
	if (!ops) { /* could happen in batch of actions */
		NL_SET_ERR_MSG(extack, "Specified TC action not found");
		goto err_out;
	}
	err = -ENOENT;
	if (ops->lookup(net, &a, index, extack) == 0)
		goto err_mod;

	module_put(ops->owner);
	return a;

err_mod:
	module_put(ops->owner);
err_out:
	return ERR_PTR(err);
}

static int tca_action_flush(struct net *net, struct nlattr *nla,
			    struct nlmsghdr *n, u32 portid,
			    struct netlink_ext_ack *extack)
{
	struct sk_buff *skb;
	unsigned char *b;
	struct nlmsghdr *nlh;
	struct tcamsg *t;
	struct netlink_callback dcb;
	struct nlattr *nest;
	struct nlattr *tb[TCA_ACT_MAX + 1];
	const struct tc_action_ops *ops;
	struct nlattr *kind;
	int err = -ENOMEM;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return err;

	b = skb_tail_pointer(skb);

	err = nla_parse_nested(tb, TCA_ACT_MAX, nla, NULL, extack);
	if (err < 0)
		goto err_out;

	err = -EINVAL;
	kind = tb[TCA_ACT_KIND];
	ops = tc_lookup_action(kind);
	if (!ops) { /*some idjot trying to flush unknown action */
		NL_SET_ERR_MSG(extack, "Cannot flush unknown TC action");
		goto err_out;
	}

	nlh = nlmsg_put(skb, portid, n->nlmsg_seq, RTM_DELACTION,
			sizeof(*t), 0);
	if (!nlh) {
		NL_SET_ERR_MSG(extack, "Failed to create TC action flush notification");
		goto out_module_put;
	}
	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (!nest) {
		NL_SET_ERR_MSG(extack, "Failed to add new netlink message");
		goto out_module_put;
	}

	err = ops->walk(net, skb, &dcb, RTM_DELACTION, ops, extack);
	if (err <= 0) {
		nla_nest_cancel(skb, nest);
		goto out_module_put;
	}

	nla_nest_end(skb, nest);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	nlh->nlmsg_flags |= NLM_F_ROOT;
	module_put(ops->owner);
	err = rtnetlink_send(skb, net, portid, RTNLGRP_TC,
			     n->nlmsg_flags & NLM_F_ECHO);
	if (err > 0)
		return 0;
	if (err < 0)
		NL_SET_ERR_MSG(extack, "Failed to send TC action flush notification");

	return err;

out_module_put:
	module_put(ops->owner);
err_out:
	kfree_skb(skb);
	return err;
}

static int
tcf_del_notify(struct net *net, struct nlmsghdr *n, struct list_head *actions,
	       u32 portid, size_t attr_size, struct netlink_ext_ack *extack)
{
	int ret;
	struct sk_buff *skb;

	skb = alloc_skb(attr_size <= NLMSG_GOODSIZE ? NLMSG_GOODSIZE : attr_size,
			GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tca_get_fill(skb, actions, portid, n->nlmsg_seq, 0, RTM_DELACTION,
			 0, 1) <= 0) {
		NL_SET_ERR_MSG(extack, "Failed to fill netlink TC action attributes");
		kfree_skb(skb);
		return -EINVAL;
	}

	/* now do the delete */
	ret = tcf_action_destroy(actions, 0);
	if (ret < 0) {
		NL_SET_ERR_MSG(extack, "Failed to delete TC action");
		kfree_skb(skb);
		return ret;
	}

	ret = rtnetlink_send(skb, net, portid, RTNLGRP_TC,
			     n->nlmsg_flags & NLM_F_ECHO);
	if (ret > 0)
		return 0;
	return ret;
}

static int
tca_action_gd(struct net *net, struct nlattr *nla, struct nlmsghdr *n,
	      u32 portid, int event, struct netlink_ext_ack *extack)
{
	int i, ret;
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct tc_action *act;
	size_t attr_size = 0;
	LIST_HEAD(actions);

	ret = nla_parse_nested(tb, TCA_ACT_MAX_PRIO, nla, NULL, extack);
	if (ret < 0)
		return ret;

	if (event == RTM_DELACTION && n->nlmsg_flags & NLM_F_ROOT) {
		if (tb[1])
			return tca_action_flush(net, tb[1], n, portid, extack);

		NL_SET_ERR_MSG(extack, "Invalid netlink attributes while flushing TC action");
		return -EINVAL;
	}

	for (i = 1; i <= TCA_ACT_MAX_PRIO && tb[i]; i++) {
		act = tcf_action_get_1(net, tb[i], n, portid, extack);
		if (IS_ERR(act)) {
			ret = PTR_ERR(act);
			goto err;
		}
		act->order = i;
		attr_size += tcf_action_fill_size(act);
		list_add_tail(&act->list, &actions);
	}

	attr_size = tcf_action_full_attrs_size(attr_size);

	if (event == RTM_GETACTION)
		ret = tcf_get_notify(net, portid, n, &actions, event, extack);
	else { /* delete */
		ret = tcf_del_notify(net, n, &actions, portid, attr_size, extack);
		if (ret)
			goto err;
		return ret;
	}
err:
	if (event != RTM_GETACTION)
		tcf_action_destroy(&actions, 0);
	return ret;
}

static int
tcf_add_notify(struct net *net, struct nlmsghdr *n, struct list_head *actions,
	       u32 portid, size_t attr_size, struct netlink_ext_ack *extack)
{
	struct sk_buff *skb;
	int err = 0;

	skb = alloc_skb(attr_size <= NLMSG_GOODSIZE ? NLMSG_GOODSIZE : attr_size,
			GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	if (tca_get_fill(skb, actions, portid, n->nlmsg_seq, n->nlmsg_flags,
			 RTM_NEWACTION, 0, 0) <= 0) {
		NL_SET_ERR_MSG(extack, "Failed to fill netlink attributes while adding TC action");
		kfree_skb(skb);
		return -EINVAL;
	}

	err = rtnetlink_send(skb, net, portid, RTNLGRP_TC,
			     n->nlmsg_flags & NLM_F_ECHO);
	if (err > 0)
		err = 0;
	return err;
}

static int tcf_action_add(struct net *net, struct nlattr *nla,
			  struct nlmsghdr *n, u32 portid, int ovr,
			  struct netlink_ext_ack *extack)
{
	size_t attr_size = 0;
	int ret = 0;
	LIST_HEAD(actions);

	ret = tcf_action_init(net, NULL, nla, NULL, NULL, ovr, 0, &actions,
			      &attr_size, extack);
	if (ret)
		return ret;

	return tcf_add_notify(net, n, &actions, portid, attr_size, extack);
}

static u32 tcaa_root_flags_allowed = TCA_FLAG_LARGE_DUMP_ON;
static const struct nla_policy tcaa_policy[TCA_ROOT_MAX + 1] = {
	[TCA_ROOT_FLAGS] = { .type = NLA_BITFIELD32,
			     .validation_data = &tcaa_root_flags_allowed },
	[TCA_ROOT_TIME_DELTA]      = { .type = NLA_U32 },
};

static int tc_ctl_action(struct sk_buff *skb, struct nlmsghdr *n,
			 struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tca[TCA_ROOT_MAX + 1];
	u32 portid = skb ? NETLINK_CB(skb).portid : 0;
	int ret = 0, ovr = 0;

	if ((n->nlmsg_type != RTM_GETACTION) &&
	    !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	ret = nlmsg_parse(n, sizeof(struct tcamsg), tca, TCA_ROOT_MAX, NULL,
			  extack);
	if (ret < 0)
		return ret;

	if (tca[TCA_ACT_TAB] == NULL) {
		NL_SET_ERR_MSG(extack, "Netlink action attributes missing");
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
		ret = tcf_action_add(net, tca[TCA_ACT_TAB], n, portid, ovr,
				     extack);
		if (ret == -EAGAIN)
			goto replay;
		break;
	case RTM_DELACTION:
		ret = tca_action_gd(net, tca[TCA_ACT_TAB], n,
				    portid, RTM_DELACTION, extack);
		break;
	case RTM_GETACTION:
		ret = tca_action_gd(net, tca[TCA_ACT_TAB], n,
				    portid, RTM_GETACTION, extack);
		break;
	default:
		BUG();
	}

	return ret;
}

static struct nlattr *find_dump_kind(struct nlattr **nla)
{
	struct nlattr *tb1, *tb2[TCA_ACT_MAX + 1];
	struct nlattr *tb[TCA_ACT_MAX_PRIO + 1];
	struct nlattr *kind;

	tb1 = nla[TCA_ACT_TAB];
	if (tb1 == NULL)
		return NULL;

	if (nla_parse(tb, TCA_ACT_MAX_PRIO, nla_data(tb1),
		      NLMSG_ALIGN(nla_len(tb1)), NULL, NULL) < 0)
		return NULL;

	if (tb[1] == NULL)
		return NULL;
	if (nla_parse_nested(tb2, TCA_ACT_MAX, tb[1], NULL, NULL) < 0)
		return NULL;
	kind = tb2[TCA_ACT_KIND];

	return kind;
}

static int tc_dump_action(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct nlmsghdr *nlh;
	unsigned char *b = skb_tail_pointer(skb);
	struct nlattr *nest;
	struct tc_action_ops *a_o;
	int ret = 0;
	struct tcamsg *t = (struct tcamsg *) nlmsg_data(cb->nlh);
	struct nlattr *tb[TCA_ROOT_MAX + 1];
	struct nlattr *count_attr = NULL;
	unsigned long jiffy_since = 0;
	struct nlattr *kind = NULL;
	struct nla_bitfield32 bf;
	u32 msecs_since = 0;
	u32 act_count = 0;

	ret = nlmsg_parse(cb->nlh, sizeof(struct tcamsg), tb, TCA_ROOT_MAX,
			  tcaa_policy, NULL);
	if (ret < 0)
		return ret;

	kind = find_dump_kind(tb);
	if (kind == NULL) {
		pr_info("tc_dump_action: action bad kind\n");
		return 0;
	}

	a_o = tc_lookup_action(kind);
	if (a_o == NULL)
		return 0;

	cb->args[2] = 0;
	if (tb[TCA_ROOT_FLAGS]) {
		bf = nla_get_bitfield32(tb[TCA_ROOT_FLAGS]);
		cb->args[2] = bf.value;
	}

	if (tb[TCA_ROOT_TIME_DELTA]) {
		msecs_since = nla_get_u32(tb[TCA_ROOT_TIME_DELTA]);
	}

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			cb->nlh->nlmsg_type, sizeof(*t), 0);
	if (!nlh)
		goto out_module_put;

	if (msecs_since)
		jiffy_since = jiffies - msecs_to_jiffies(msecs_since);

	t = nlmsg_data(nlh);
	t->tca_family = AF_UNSPEC;
	t->tca__pad1 = 0;
	t->tca__pad2 = 0;
	cb->args[3] = jiffy_since;
	count_attr = nla_reserve(skb, TCA_ROOT_COUNT, sizeof(u32));
	if (!count_attr)
		goto out_module_put;

	nest = nla_nest_start(skb, TCA_ACT_TAB);
	if (nest == NULL)
		goto out_module_put;

	ret = a_o->walk(net, skb, cb, RTM_GETACTION, a_o, NULL);
	if (ret < 0)
		goto out_module_put;

	if (ret > 0) {
		nla_nest_end(skb, nest);
		ret = skb->len;
		act_count = cb->args[1];
		memcpy(nla_data(count_attr), &act_count, sizeof(u32));
		cb->args[1] = 0;
	} else
		nlmsg_trim(skb, b);

	nlh->nlmsg_len = skb_tail_pointer(skb) - b;
	if (NETLINK_CB(cb->skb).portid && ret)
		nlh->nlmsg_flags |= NLM_F_MULTI;
	module_put(a_o->owner);
	return skb->len;

out_module_put:
	module_put(a_o->owner);
	nlmsg_trim(skb, b);
	return skb->len;
}

struct tcf_action_net {
	struct rhashtable egdev_ht;
};

static unsigned int tcf_action_net_id;

struct tcf_action_egdev_cb {
	struct list_head list;
	tc_setup_cb_t *cb;
	void *cb_priv;
};

struct tcf_action_egdev {
	struct rhash_head ht_node;
	const struct net_device *dev;
	unsigned int refcnt;
	struct list_head cb_list;
};

static const struct rhashtable_params tcf_action_egdev_ht_params = {
	.key_offset = offsetof(struct tcf_action_egdev, dev),
	.head_offset = offsetof(struct tcf_action_egdev, ht_node),
	.key_len = sizeof(const struct net_device *),
};

static struct tcf_action_egdev *
tcf_action_egdev_lookup(const struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct tcf_action_net *tan = net_generic(net, tcf_action_net_id);

	return rhashtable_lookup_fast(&tan->egdev_ht, &dev,
				      tcf_action_egdev_ht_params);
}

static struct tcf_action_egdev *
tcf_action_egdev_get(const struct net_device *dev)
{
	struct tcf_action_egdev *egdev;
	struct tcf_action_net *tan;

	egdev = tcf_action_egdev_lookup(dev);
	if (egdev)
		goto inc_ref;

	egdev = kzalloc(sizeof(*egdev), GFP_KERNEL);
	if (!egdev)
		return NULL;
	INIT_LIST_HEAD(&egdev->cb_list);
	egdev->dev = dev;
	tan = net_generic(dev_net(dev), tcf_action_net_id);
	rhashtable_insert_fast(&tan->egdev_ht, &egdev->ht_node,
			       tcf_action_egdev_ht_params);

inc_ref:
	egdev->refcnt++;
	return egdev;
}

static void tcf_action_egdev_put(struct tcf_action_egdev *egdev)
{
	struct tcf_action_net *tan;

	if (--egdev->refcnt)
		return;
	tan = net_generic(dev_net(egdev->dev), tcf_action_net_id);
	rhashtable_remove_fast(&tan->egdev_ht, &egdev->ht_node,
			       tcf_action_egdev_ht_params);
	kfree(egdev);
}

static struct tcf_action_egdev_cb *
tcf_action_egdev_cb_lookup(struct tcf_action_egdev *egdev,
			   tc_setup_cb_t *cb, void *cb_priv)
{
	struct tcf_action_egdev_cb *egdev_cb;

	list_for_each_entry(egdev_cb, &egdev->cb_list, list)
		if (egdev_cb->cb == cb && egdev_cb->cb_priv == cb_priv)
			return egdev_cb;
	return NULL;
}

static int tcf_action_egdev_cb_call(struct tcf_action_egdev *egdev,
				    enum tc_setup_type type,
				    void *type_data, bool err_stop)
{
	struct tcf_action_egdev_cb *egdev_cb;
	int ok_count = 0;
	int err;

	list_for_each_entry(egdev_cb, &egdev->cb_list, list) {
		err = egdev_cb->cb(type, type_data, egdev_cb->cb_priv);
		if (err) {
			if (err_stop)
				return err;
		} else {
			ok_count++;
		}
	}
	return ok_count;
}

static int tcf_action_egdev_cb_add(struct tcf_action_egdev *egdev,
				   tc_setup_cb_t *cb, void *cb_priv)
{
	struct tcf_action_egdev_cb *egdev_cb;

	egdev_cb = tcf_action_egdev_cb_lookup(egdev, cb, cb_priv);
	if (WARN_ON(egdev_cb))
		return -EEXIST;
	egdev_cb = kzalloc(sizeof(*egdev_cb), GFP_KERNEL);
	if (!egdev_cb)
		return -ENOMEM;
	egdev_cb->cb = cb;
	egdev_cb->cb_priv = cb_priv;
	list_add(&egdev_cb->list, &egdev->cb_list);
	return 0;
}

static void tcf_action_egdev_cb_del(struct tcf_action_egdev *egdev,
				    tc_setup_cb_t *cb, void *cb_priv)
{
	struct tcf_action_egdev_cb *egdev_cb;

	egdev_cb = tcf_action_egdev_cb_lookup(egdev, cb, cb_priv);
	if (WARN_ON(!egdev_cb))
		return;
	list_del(&egdev_cb->list);
	kfree(egdev_cb);
}

static int __tc_setup_cb_egdev_register(const struct net_device *dev,
					tc_setup_cb_t *cb, void *cb_priv)
{
	struct tcf_action_egdev *egdev = tcf_action_egdev_get(dev);
	int err;

	if (!egdev)
		return -ENOMEM;
	err = tcf_action_egdev_cb_add(egdev, cb, cb_priv);
	if (err)
		goto err_cb_add;
	return 0;

err_cb_add:
	tcf_action_egdev_put(egdev);
	return err;
}
int tc_setup_cb_egdev_register(const struct net_device *dev,
			       tc_setup_cb_t *cb, void *cb_priv)
{
	int err;

	rtnl_lock();
	err = __tc_setup_cb_egdev_register(dev, cb, cb_priv);
	rtnl_unlock();
	return err;
}
EXPORT_SYMBOL_GPL(tc_setup_cb_egdev_register);

static void __tc_setup_cb_egdev_unregister(const struct net_device *dev,
					   tc_setup_cb_t *cb, void *cb_priv)
{
	struct tcf_action_egdev *egdev = tcf_action_egdev_lookup(dev);

	if (WARN_ON(!egdev))
		return;
	tcf_action_egdev_cb_del(egdev, cb, cb_priv);
	tcf_action_egdev_put(egdev);
}
void tc_setup_cb_egdev_unregister(const struct net_device *dev,
				  tc_setup_cb_t *cb, void *cb_priv)
{
	rtnl_lock();
	__tc_setup_cb_egdev_unregister(dev, cb, cb_priv);
	rtnl_unlock();
}
EXPORT_SYMBOL_GPL(tc_setup_cb_egdev_unregister);

int tc_setup_cb_egdev_call(const struct net_device *dev,
			   enum tc_setup_type type, void *type_data,
			   bool err_stop)
{
	struct tcf_action_egdev *egdev = tcf_action_egdev_lookup(dev);

	if (!egdev)
		return 0;
	return tcf_action_egdev_cb_call(egdev, type, type_data, err_stop);
}
EXPORT_SYMBOL_GPL(tc_setup_cb_egdev_call);

static __net_init int tcf_action_net_init(struct net *net)
{
	struct tcf_action_net *tan = net_generic(net, tcf_action_net_id);

	return rhashtable_init(&tan->egdev_ht, &tcf_action_egdev_ht_params);
}

static void __net_exit tcf_action_net_exit(struct net *net)
{
	struct tcf_action_net *tan = net_generic(net, tcf_action_net_id);

	rhashtable_destroy(&tan->egdev_ht);
}

static struct pernet_operations tcf_action_net_ops = {
	.init = tcf_action_net_init,
	.exit = tcf_action_net_exit,
	.id = &tcf_action_net_id,
	.size = sizeof(struct tcf_action_net),
};

static int __init tc_action_init(void)
{
	int err;

	err = register_pernet_subsys(&tcf_action_net_ops);
	if (err)
		return err;

	rtnl_register(PF_UNSPEC, RTM_NEWACTION, tc_ctl_action, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_DELACTION, tc_ctl_action, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_GETACTION, tc_ctl_action, tc_dump_action,
		      0);

	return 0;
}

subsys_initcall(tc_action_init);
