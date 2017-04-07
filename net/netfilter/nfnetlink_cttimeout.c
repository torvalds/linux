/*
 * (C) 2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2012 by Vyatta Inc. <http://www.vyatta.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation (or any later at your option).
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/rculist.h>
#include <linux/rculist_nulls.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/security.h>
#include <linux/skbuff.h>
#include <linux/errno.h>
#include <linux/netlink.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include <linux/netfilter.h>
#include <net/netlink.h>
#include <net/sock.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_l3proto.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_timeout.h>

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_cttimeout.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_DESCRIPTION("cttimeout: Extended Netfilter Connection Tracking timeout tuning");

static const struct nla_policy cttimeout_nla_policy[CTA_TIMEOUT_MAX+1] = {
	[CTA_TIMEOUT_NAME]	= { .type = NLA_NUL_STRING,
				    .len  = CTNL_TIMEOUT_NAME_MAX - 1},
	[CTA_TIMEOUT_L3PROTO]	= { .type = NLA_U16 },
	[CTA_TIMEOUT_L4PROTO]	= { .type = NLA_U8 },
	[CTA_TIMEOUT_DATA]	= { .type = NLA_NESTED },
};

static int
ctnl_timeout_parse_policy(void *timeouts, struct nf_conntrack_l4proto *l4proto,
			  struct net *net, const struct nlattr *attr)
{
	int ret = 0;

	if (likely(l4proto->ctnl_timeout.nlattr_to_obj)) {
		struct nlattr *tb[l4proto->ctnl_timeout.nlattr_max+1];

		ret = nla_parse_nested(tb, l4proto->ctnl_timeout.nlattr_max,
				       attr, l4proto->ctnl_timeout.nla_policy);
		if (ret < 0)
			return ret;

		ret = l4proto->ctnl_timeout.nlattr_to_obj(tb, net, timeouts);
	}
	return ret;
}

static int cttimeout_new_timeout(struct net *net, struct sock *ctnl,
				 struct sk_buff *skb,
				 const struct nlmsghdr *nlh,
				 const struct nlattr * const cda[])
{
	__u16 l3num;
	__u8 l4num;
	struct nf_conntrack_l4proto *l4proto;
	struct ctnl_timeout *timeout, *matching = NULL;
	char *name;
	int ret;

	if (!cda[CTA_TIMEOUT_NAME] ||
	    !cda[CTA_TIMEOUT_L3PROTO] ||
	    !cda[CTA_TIMEOUT_L4PROTO] ||
	    !cda[CTA_TIMEOUT_DATA])
		return -EINVAL;

	name = nla_data(cda[CTA_TIMEOUT_NAME]);
	l3num = ntohs(nla_get_be16(cda[CTA_TIMEOUT_L3PROTO]));
	l4num = nla_get_u8(cda[CTA_TIMEOUT_L4PROTO]);

	list_for_each_entry(timeout, &net->nfct_timeout_list, head) {
		if (strncmp(timeout->name, name, CTNL_TIMEOUT_NAME_MAX) != 0)
			continue;

		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;

		matching = timeout;
		break;
	}

	if (matching) {
		if (nlh->nlmsg_flags & NLM_F_REPLACE) {
			/* You cannot replace one timeout policy by another of
			 * different kind, sorry.
			 */
			if (matching->l3num != l3num ||
			    matching->l4proto->l4proto != l4num)
				return -EINVAL;

			return ctnl_timeout_parse_policy(&matching->data,
							 matching->l4proto, net,
							 cda[CTA_TIMEOUT_DATA]);
		}

		return -EBUSY;
	}

	l4proto = nf_ct_l4proto_find_get(l3num, l4num);

	/* This protocol is not supportted, skip. */
	if (l4proto->l4proto != l4num) {
		ret = -EOPNOTSUPP;
		goto err_proto_put;
	}

	timeout = kzalloc(sizeof(struct ctnl_timeout) +
			  l4proto->ctnl_timeout.obj_size, GFP_KERNEL);
	if (timeout == NULL) {
		ret = -ENOMEM;
		goto err_proto_put;
	}

	ret = ctnl_timeout_parse_policy(&timeout->data, l4proto, net,
					cda[CTA_TIMEOUT_DATA]);
	if (ret < 0)
		goto err;

	strcpy(timeout->name, nla_data(cda[CTA_TIMEOUT_NAME]));
	timeout->l3num = l3num;
	timeout->l4proto = l4proto;
	atomic_set(&timeout->refcnt, 1);
	list_add_tail_rcu(&timeout->head, &net->nfct_timeout_list);

	return 0;
err:
	kfree(timeout);
err_proto_put:
	nf_ct_l4proto_put(l4proto);
	return ret;
}

static int
ctnl_timeout_fill_info(struct sk_buff *skb, u32 portid, u32 seq, u32 type,
		       int event, struct ctnl_timeout *timeout)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	unsigned int flags = portid ? NLM_F_MULTI : 0;
	struct nf_conntrack_l4proto *l4proto = timeout->l4proto;

	event |= NFNL_SUBSYS_CTNETLINK_TIMEOUT << 8;
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = AF_UNSPEC;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = 0;

	if (nla_put_string(skb, CTA_TIMEOUT_NAME, timeout->name) ||
	    nla_put_be16(skb, CTA_TIMEOUT_L3PROTO, htons(timeout->l3num)) ||
	    nla_put_u8(skb, CTA_TIMEOUT_L4PROTO, timeout->l4proto->l4proto) ||
	    nla_put_be32(skb, CTA_TIMEOUT_USE,
			 htonl(atomic_read(&timeout->refcnt))))
		goto nla_put_failure;

	if (likely(l4proto->ctnl_timeout.obj_to_nlattr)) {
		struct nlattr *nest_parms;
		int ret;

		nest_parms = nla_nest_start(skb,
					    CTA_TIMEOUT_DATA | NLA_F_NESTED);
		if (!nest_parms)
			goto nla_put_failure;

		ret = l4proto->ctnl_timeout.obj_to_nlattr(skb, &timeout->data);
		if (ret < 0)
			goto nla_put_failure;

		nla_nest_end(skb, nest_parms);
	}

	nlmsg_end(skb, nlh);
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static int
ctnl_timeout_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct ctnl_timeout *cur, *last;

	if (cb->args[2])
		return 0;

	last = (struct ctnl_timeout *)cb->args[1];
	if (cb->args[1])
		cb->args[1] = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(cur, &net->nfct_timeout_list, head) {
		if (last) {
			if (cur != last)
				continue;

			last = NULL;
		}
		if (ctnl_timeout_fill_info(skb, NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq,
					   NFNL_MSG_TYPE(cb->nlh->nlmsg_type),
					   IPCTNL_MSG_TIMEOUT_NEW, cur) < 0) {
			cb->args[1] = (unsigned long)cur;
			break;
		}
	}
	if (!cb->args[1])
		cb->args[2] = 1;
	rcu_read_unlock();
	return skb->len;
}

static int cttimeout_get_timeout(struct net *net, struct sock *ctnl,
				 struct sk_buff *skb,
				 const struct nlmsghdr *nlh,
				 const struct nlattr * const cda[])
{
	int ret = -ENOENT;
	char *name;
	struct ctnl_timeout *cur;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = ctnl_timeout_dump,
		};
		return netlink_dump_start(ctnl, skb, nlh, &c);
	}

	if (!cda[CTA_TIMEOUT_NAME])
		return -EINVAL;
	name = nla_data(cda[CTA_TIMEOUT_NAME]);

	list_for_each_entry(cur, &net->nfct_timeout_list, head) {
		struct sk_buff *skb2;

		if (strncmp(cur->name, name, CTNL_TIMEOUT_NAME_MAX) != 0)
			continue;

		skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
		if (skb2 == NULL) {
			ret = -ENOMEM;
			break;
		}

		ret = ctnl_timeout_fill_info(skb2, NETLINK_CB(skb).portid,
					     nlh->nlmsg_seq,
					     NFNL_MSG_TYPE(nlh->nlmsg_type),
					     IPCTNL_MSG_TIMEOUT_NEW, cur);
		if (ret <= 0) {
			kfree_skb(skb2);
			break;
		}
		ret = netlink_unicast(ctnl, skb2, NETLINK_CB(skb).portid,
					MSG_DONTWAIT);
		if (ret > 0)
			ret = 0;

		/* this avoids a loop in nfnetlink. */
		return ret == -EAGAIN ? -ENOBUFS : ret;
	}
	return ret;
}

static void untimeout(struct nf_conntrack_tuple_hash *i,
		      struct ctnl_timeout *timeout)
{
	struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(i);
	struct nf_conn_timeout *timeout_ext = nf_ct_timeout_find(ct);

	if (timeout_ext && (!timeout || timeout_ext->timeout == timeout))
		RCU_INIT_POINTER(timeout_ext->timeout, NULL);
}

static void ctnl_untimeout(struct net *net, struct ctnl_timeout *timeout)
{
	struct nf_conntrack_tuple_hash *h;
	const struct hlist_nulls_node *nn;
	unsigned int last_hsize;
	spinlock_t *lock;
	int i, cpu;

	for_each_possible_cpu(cpu) {
		struct ct_pcpu *pcpu = per_cpu_ptr(net->ct.pcpu_lists, cpu);

		spin_lock_bh(&pcpu->lock);
		hlist_nulls_for_each_entry(h, nn, &pcpu->unconfirmed, hnnode)
			untimeout(h, timeout);
		spin_unlock_bh(&pcpu->lock);
	}

	local_bh_disable();
restart:
	last_hsize = nf_conntrack_htable_size;
	for (i = 0; i < last_hsize; i++) {
		lock = &nf_conntrack_locks[i % CONNTRACK_LOCKS];
		nf_conntrack_lock(lock);
		if (last_hsize != nf_conntrack_htable_size) {
			spin_unlock(lock);
			goto restart;
		}

		hlist_nulls_for_each_entry(h, nn, &nf_conntrack_hash[i], hnnode)
			untimeout(h, timeout);
		spin_unlock(lock);
	}
	local_bh_enable();
}

/* try to delete object, fail if it is still in use. */
static int ctnl_timeout_try_del(struct net *net, struct ctnl_timeout *timeout)
{
	int ret = 0;

	/* We want to avoid races with ctnl_timeout_put. So only when the
	 * current refcnt is 1, we decrease it to 0.
	 */
	if (atomic_cmpxchg(&timeout->refcnt, 1, 0) == 1) {
		/* We are protected by nfnl mutex. */
		list_del_rcu(&timeout->head);
		nf_ct_l4proto_put(timeout->l4proto);
		ctnl_untimeout(net, timeout);
		kfree_rcu(timeout, rcu_head);
	} else {
		ret = -EBUSY;
	}
	return ret;
}

static int cttimeout_del_timeout(struct net *net, struct sock *ctnl,
				 struct sk_buff *skb,
				 const struct nlmsghdr *nlh,
				 const struct nlattr * const cda[])
{
	struct ctnl_timeout *cur, *tmp;
	int ret = -ENOENT;
	char *name;

	if (!cda[CTA_TIMEOUT_NAME]) {
		list_for_each_entry_safe(cur, tmp, &net->nfct_timeout_list,
					 head)
			ctnl_timeout_try_del(net, cur);

		return 0;
	}
	name = nla_data(cda[CTA_TIMEOUT_NAME]);

	list_for_each_entry(cur, &net->nfct_timeout_list, head) {
		if (strncmp(cur->name, name, CTNL_TIMEOUT_NAME_MAX) != 0)
			continue;

		ret = ctnl_timeout_try_del(net, cur);
		if (ret < 0)
			return ret;

		break;
	}
	return ret;
}

static int cttimeout_default_set(struct net *net, struct sock *ctnl,
				 struct sk_buff *skb,
				 const struct nlmsghdr *nlh,
				 const struct nlattr * const cda[])
{
	__u16 l3num;
	__u8 l4num;
	struct nf_conntrack_l4proto *l4proto;
	unsigned int *timeouts;
	int ret;

	if (!cda[CTA_TIMEOUT_L3PROTO] ||
	    !cda[CTA_TIMEOUT_L4PROTO] ||
	    !cda[CTA_TIMEOUT_DATA])
		return -EINVAL;

	l3num = ntohs(nla_get_be16(cda[CTA_TIMEOUT_L3PROTO]));
	l4num = nla_get_u8(cda[CTA_TIMEOUT_L4PROTO]);
	l4proto = nf_ct_l4proto_find_get(l3num, l4num);

	/* This protocol is not supported, skip. */
	if (l4proto->l4proto != l4num) {
		ret = -EOPNOTSUPP;
		goto err;
	}

	timeouts = l4proto->get_timeouts(net);

	ret = ctnl_timeout_parse_policy(timeouts, l4proto, net,
					cda[CTA_TIMEOUT_DATA]);
	if (ret < 0)
		goto err;

	nf_ct_l4proto_put(l4proto);
	return 0;
err:
	nf_ct_l4proto_put(l4proto);
	return ret;
}

static int
cttimeout_default_fill_info(struct net *net, struct sk_buff *skb, u32 portid,
			    u32 seq, u32 type, int event,
			    struct nf_conntrack_l4proto *l4proto)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	unsigned int flags = portid ? NLM_F_MULTI : 0;

	event |= NFNL_SUBSYS_CTNETLINK_TIMEOUT << 8;
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = AF_UNSPEC;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = 0;

	if (nla_put_be16(skb, CTA_TIMEOUT_L3PROTO, htons(l4proto->l3proto)) ||
	    nla_put_u8(skb, CTA_TIMEOUT_L4PROTO, l4proto->l4proto))
		goto nla_put_failure;

	if (likely(l4proto->ctnl_timeout.obj_to_nlattr)) {
		struct nlattr *nest_parms;
		unsigned int *timeouts = l4proto->get_timeouts(net);
		int ret;

		nest_parms = nla_nest_start(skb,
					    CTA_TIMEOUT_DATA | NLA_F_NESTED);
		if (!nest_parms)
			goto nla_put_failure;

		ret = l4proto->ctnl_timeout.obj_to_nlattr(skb, timeouts);
		if (ret < 0)
			goto nla_put_failure;

		nla_nest_end(skb, nest_parms);
	}

	nlmsg_end(skb, nlh);
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static int cttimeout_default_get(struct net *net, struct sock *ctnl,
				 struct sk_buff *skb,
				 const struct nlmsghdr *nlh,
				 const struct nlattr * const cda[])
{
	__u16 l3num;
	__u8 l4num;
	struct nf_conntrack_l4proto *l4proto;
	struct sk_buff *skb2;
	int ret, err;

	if (!cda[CTA_TIMEOUT_L3PROTO] || !cda[CTA_TIMEOUT_L4PROTO])
		return -EINVAL;

	l3num = ntohs(nla_get_be16(cda[CTA_TIMEOUT_L3PROTO]));
	l4num = nla_get_u8(cda[CTA_TIMEOUT_L4PROTO]);
	l4proto = nf_ct_l4proto_find_get(l3num, l4num);

	/* This protocol is not supported, skip. */
	if (l4proto->l4proto != l4num) {
		err = -EOPNOTSUPP;
		goto err;
	}

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb2 == NULL) {
		err = -ENOMEM;
		goto err;
	}

	ret = cttimeout_default_fill_info(net, skb2, NETLINK_CB(skb).portid,
					  nlh->nlmsg_seq,
					  NFNL_MSG_TYPE(nlh->nlmsg_type),
					  IPCTNL_MSG_TIMEOUT_DEFAULT_SET,
					  l4proto);
	if (ret <= 0) {
		kfree_skb(skb2);
		err = -ENOMEM;
		goto err;
	}
	ret = netlink_unicast(ctnl, skb2, NETLINK_CB(skb).portid, MSG_DONTWAIT);
	if (ret > 0)
		ret = 0;

	/* this avoids a loop in nfnetlink. */
	return ret == -EAGAIN ? -ENOBUFS : ret;
err:
	nf_ct_l4proto_put(l4proto);
	return err;
}

#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
static struct ctnl_timeout *
ctnl_timeout_find_get(struct net *net, const char *name)
{
	struct ctnl_timeout *timeout, *matching = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(timeout, &net->nfct_timeout_list, head) {
		if (strncmp(timeout->name, name, CTNL_TIMEOUT_NAME_MAX) != 0)
			continue;

		if (!try_module_get(THIS_MODULE))
			goto err;

		if (!atomic_inc_not_zero(&timeout->refcnt)) {
			module_put(THIS_MODULE);
			goto err;
		}
		matching = timeout;
		break;
	}
err:
	rcu_read_unlock();
	return matching;
}

static void ctnl_timeout_put(struct ctnl_timeout *timeout)
{
	if (atomic_dec_and_test(&timeout->refcnt))
		kfree_rcu(timeout, rcu_head);

	module_put(THIS_MODULE);
}
#endif /* CONFIG_NF_CONNTRACK_TIMEOUT */

static const struct nfnl_callback cttimeout_cb[IPCTNL_MSG_TIMEOUT_MAX] = {
	[IPCTNL_MSG_TIMEOUT_NEW]	= { .call = cttimeout_new_timeout,
					    .attr_count = CTA_TIMEOUT_MAX,
					    .policy = cttimeout_nla_policy },
	[IPCTNL_MSG_TIMEOUT_GET]	= { .call = cttimeout_get_timeout,
					    .attr_count = CTA_TIMEOUT_MAX,
					    .policy = cttimeout_nla_policy },
	[IPCTNL_MSG_TIMEOUT_DELETE]	= { .call = cttimeout_del_timeout,
					    .attr_count = CTA_TIMEOUT_MAX,
					    .policy = cttimeout_nla_policy },
	[IPCTNL_MSG_TIMEOUT_DEFAULT_SET]= { .call = cttimeout_default_set,
					    .attr_count = CTA_TIMEOUT_MAX,
					    .policy = cttimeout_nla_policy },
	[IPCTNL_MSG_TIMEOUT_DEFAULT_GET]= { .call = cttimeout_default_get,
					    .attr_count = CTA_TIMEOUT_MAX,
					    .policy = cttimeout_nla_policy },
};

static const struct nfnetlink_subsystem cttimeout_subsys = {
	.name				= "conntrack_timeout",
	.subsys_id			= NFNL_SUBSYS_CTNETLINK_TIMEOUT,
	.cb_count			= IPCTNL_MSG_TIMEOUT_MAX,
	.cb				= cttimeout_cb,
};

MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_CTNETLINK_TIMEOUT);

static int __net_init cttimeout_net_init(struct net *net)
{
	INIT_LIST_HEAD(&net->nfct_timeout_list);

	return 0;
}

static void __net_exit cttimeout_net_exit(struct net *net)
{
	struct ctnl_timeout *cur, *tmp;

	ctnl_untimeout(net, NULL);

	list_for_each_entry_safe(cur, tmp, &net->nfct_timeout_list, head) {
		list_del_rcu(&cur->head);
		nf_ct_l4proto_put(cur->l4proto);

		if (atomic_dec_and_test(&cur->refcnt))
			kfree_rcu(cur, rcu_head);
	}
}

static struct pernet_operations cttimeout_ops = {
	.init	= cttimeout_net_init,
	.exit	= cttimeout_net_exit,
};

static int __init cttimeout_init(void)
{
	int ret;

	ret = register_pernet_subsys(&cttimeout_ops);
	if (ret < 0)
		return ret;

	ret = nfnetlink_subsys_register(&cttimeout_subsys);
	if (ret < 0) {
		pr_err("cttimeout_init: cannot register cttimeout with "
			"nfnetlink.\n");
		goto err_out;
	}
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	RCU_INIT_POINTER(nf_ct_timeout_find_get_hook, ctnl_timeout_find_get);
	RCU_INIT_POINTER(nf_ct_timeout_put_hook, ctnl_timeout_put);
#endif /* CONFIG_NF_CONNTRACK_TIMEOUT */
	return 0;

err_out:
	unregister_pernet_subsys(&cttimeout_ops);
	return ret;
}

static void __exit cttimeout_exit(void)
{
	pr_info("cttimeout: unregistering from nfnetlink.\n");

	nfnetlink_subsys_unregister(&cttimeout_subsys);

	unregister_pernet_subsys(&cttimeout_ops);
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	RCU_INIT_POINTER(nf_ct_timeout_find_get_hook, NULL);
	RCU_INIT_POINTER(nf_ct_timeout_put_hook, NULL);
	synchronize_rcu();
#endif /* CONFIG_NF_CONNTRACK_TIMEOUT */
}

module_init(cttimeout_init);
module_exit(cttimeout_exit);
