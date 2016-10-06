/*
 * lwtunnel	Infrastructure for light weight tunnels like mpls
 *
 * Authors:	Roopa Prabhu, <roopa@cumulusnetworks.com>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/lwtunnel.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/err.h>

#include <net/lwtunnel.h>
#include <net/rtnetlink.h>
#include <net/ip6_fib.h>

#ifdef CONFIG_MODULES

static const char *lwtunnel_encap_str(enum lwtunnel_encap_types encap_type)
{
	/* Only lwt encaps implemented without using an interface for
	 * the encap need to return a string here.
	 */
	switch (encap_type) {
	case LWTUNNEL_ENCAP_MPLS:
		return "MPLS";
	case LWTUNNEL_ENCAP_ILA:
		return "ILA";
	case LWTUNNEL_ENCAP_IP6:
	case LWTUNNEL_ENCAP_IP:
	case LWTUNNEL_ENCAP_NONE:
	case __LWTUNNEL_ENCAP_MAX:
		/* should not have got here */
		WARN_ON(1);
		break;
	}
	return NULL;
}

#endif /* CONFIG_MODULES */

struct lwtunnel_state *lwtunnel_state_alloc(int encap_len)
{
	struct lwtunnel_state *lws;

	lws = kzalloc(sizeof(*lws) + encap_len, GFP_ATOMIC);

	return lws;
}
EXPORT_SYMBOL(lwtunnel_state_alloc);

static const struct lwtunnel_encap_ops __rcu *
		lwtun_encaps[LWTUNNEL_ENCAP_MAX + 1] __read_mostly;

int lwtunnel_encap_add_ops(const struct lwtunnel_encap_ops *ops,
			   unsigned int num)
{
	if (num > LWTUNNEL_ENCAP_MAX)
		return -ERANGE;

	return !cmpxchg((const struct lwtunnel_encap_ops **)
			&lwtun_encaps[num],
			NULL, ops) ? 0 : -1;
}
EXPORT_SYMBOL(lwtunnel_encap_add_ops);

int lwtunnel_encap_del_ops(const struct lwtunnel_encap_ops *ops,
			   unsigned int encap_type)
{
	int ret;

	if (encap_type == LWTUNNEL_ENCAP_NONE ||
	    encap_type > LWTUNNEL_ENCAP_MAX)
		return -ERANGE;

	ret = (cmpxchg((const struct lwtunnel_encap_ops **)
		       &lwtun_encaps[encap_type],
		       ops, NULL) == ops) ? 0 : -1;

	synchronize_net();

	return ret;
}
EXPORT_SYMBOL(lwtunnel_encap_del_ops);

int lwtunnel_build_state(struct net_device *dev, u16 encap_type,
			 struct nlattr *encap, unsigned int family,
			 const void *cfg, struct lwtunnel_state **lws)
{
	const struct lwtunnel_encap_ops *ops;
	int ret = -EINVAL;

	if (encap_type == LWTUNNEL_ENCAP_NONE ||
	    encap_type > LWTUNNEL_ENCAP_MAX)
		return ret;

	ret = -EOPNOTSUPP;
	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[encap_type]);
#ifdef CONFIG_MODULES
	if (!ops) {
		const char *encap_type_str = lwtunnel_encap_str(encap_type);

		if (encap_type_str) {
			rcu_read_unlock();
			request_module("rtnl-lwt-%s", encap_type_str);
			rcu_read_lock();
			ops = rcu_dereference(lwtun_encaps[encap_type]);
		}
	}
#endif
	if (likely(ops && ops->build_state))
		ret = ops->build_state(dev, encap, family, cfg, lws);
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL(lwtunnel_build_state);

int lwtunnel_fill_encap(struct sk_buff *skb, struct lwtunnel_state *lwtstate)
{
	const struct lwtunnel_encap_ops *ops;
	struct nlattr *nest;
	int ret = -EINVAL;

	if (!lwtstate)
		return 0;

	if (lwtstate->type == LWTUNNEL_ENCAP_NONE ||
	    lwtstate->type > LWTUNNEL_ENCAP_MAX)
		return 0;

	ret = -EOPNOTSUPP;
	nest = nla_nest_start(skb, RTA_ENCAP);
	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[lwtstate->type]);
	if (likely(ops && ops->fill_encap))
		ret = ops->fill_encap(skb, lwtstate);
	rcu_read_unlock();

	if (ret)
		goto nla_put_failure;
	nla_nest_end(skb, nest);
	ret = nla_put_u16(skb, RTA_ENCAP_TYPE, lwtstate->type);
	if (ret)
		goto nla_put_failure;

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);

	return (ret == -EOPNOTSUPP ? 0 : ret);
}
EXPORT_SYMBOL(lwtunnel_fill_encap);

int lwtunnel_get_encap_size(struct lwtunnel_state *lwtstate)
{
	const struct lwtunnel_encap_ops *ops;
	int ret = 0;

	if (!lwtstate)
		return 0;

	if (lwtstate->type == LWTUNNEL_ENCAP_NONE ||
	    lwtstate->type > LWTUNNEL_ENCAP_MAX)
		return 0;

	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[lwtstate->type]);
	if (likely(ops && ops->get_encap_size))
		ret = nla_total_size(ops->get_encap_size(lwtstate));
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL(lwtunnel_get_encap_size);

int lwtunnel_cmp_encap(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	const struct lwtunnel_encap_ops *ops;
	int ret = 0;

	if (!a && !b)
		return 0;

	if (!a || !b)
		return 1;

	if (a->type != b->type)
		return 1;

	if (a->type == LWTUNNEL_ENCAP_NONE ||
	    a->type > LWTUNNEL_ENCAP_MAX)
		return 0;

	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[a->type]);
	if (likely(ops && ops->cmp_encap))
		ret = ops->cmp_encap(a, b);
	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL(lwtunnel_cmp_encap);

int lwtunnel_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	const struct lwtunnel_encap_ops *ops;
	struct lwtunnel_state *lwtstate;
	int ret = -EINVAL;

	if (!dst)
		goto drop;
	lwtstate = dst->lwtstate;

	if (lwtstate->type == LWTUNNEL_ENCAP_NONE ||
	    lwtstate->type > LWTUNNEL_ENCAP_MAX)
		return 0;

	ret = -EOPNOTSUPP;
	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[lwtstate->type]);
	if (likely(ops && ops->output))
		ret = ops->output(net, sk, skb);
	rcu_read_unlock();

	if (ret == -EOPNOTSUPP)
		goto drop;

	return ret;

drop:
	kfree_skb(skb);

	return ret;
}
EXPORT_SYMBOL(lwtunnel_output);

int lwtunnel_xmit(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	const struct lwtunnel_encap_ops *ops;
	struct lwtunnel_state *lwtstate;
	int ret = -EINVAL;

	if (!dst)
		goto drop;

	lwtstate = dst->lwtstate;

	if (lwtstate->type == LWTUNNEL_ENCAP_NONE ||
	    lwtstate->type > LWTUNNEL_ENCAP_MAX)
		return 0;

	ret = -EOPNOTSUPP;
	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[lwtstate->type]);
	if (likely(ops && ops->xmit))
		ret = ops->xmit(skb);
	rcu_read_unlock();

	if (ret == -EOPNOTSUPP)
		goto drop;

	return ret;

drop:
	kfree_skb(skb);

	return ret;
}
EXPORT_SYMBOL(lwtunnel_xmit);

int lwtunnel_input(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	const struct lwtunnel_encap_ops *ops;
	struct lwtunnel_state *lwtstate;
	int ret = -EINVAL;

	if (!dst)
		goto drop;
	lwtstate = dst->lwtstate;

	if (lwtstate->type == LWTUNNEL_ENCAP_NONE ||
	    lwtstate->type > LWTUNNEL_ENCAP_MAX)
		return 0;

	ret = -EOPNOTSUPP;
	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[lwtstate->type]);
	if (likely(ops && ops->input))
		ret = ops->input(skb);
	rcu_read_unlock();

	if (ret == -EOPNOTSUPP)
		goto drop;

	return ret;

drop:
	kfree_skb(skb);

	return ret;
}
EXPORT_SYMBOL(lwtunnel_input);
