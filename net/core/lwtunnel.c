// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lwtunnel	Infrastructure for light weight tunnels like mpls
 *
 * Authors:	Roopa Prabhu, <roopa@cumulusnetworks.com>
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
#include <net/rtnh.h>

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
	case LWTUNNEL_ENCAP_SEG6:
		return "SEG6";
	case LWTUNNEL_ENCAP_BPF:
		return "BPF";
	case LWTUNNEL_ENCAP_SEG6_LOCAL:
		return "SEG6LOCAL";
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
EXPORT_SYMBOL_GPL(lwtunnel_state_alloc);

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
EXPORT_SYMBOL_GPL(lwtunnel_encap_add_ops);

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
EXPORT_SYMBOL_GPL(lwtunnel_encap_del_ops);

int lwtunnel_build_state(u16 encap_type,
			 struct nlattr *encap, unsigned int family,
			 const void *cfg, struct lwtunnel_state **lws,
			 struct netlink_ext_ack *extack)
{
	const struct lwtunnel_encap_ops *ops;
	bool found = false;
	int ret = -EINVAL;

	if (encap_type == LWTUNNEL_ENCAP_NONE ||
	    encap_type > LWTUNNEL_ENCAP_MAX) {
		NL_SET_ERR_MSG_ATTR(extack, encap,
				    "Unknown LWT encapsulation type");
		return ret;
	}

	ret = -EOPNOTSUPP;
	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[encap_type]);
	if (likely(ops && ops->build_state && try_module_get(ops->owner)))
		found = true;
	rcu_read_unlock();

	if (found) {
		ret = ops->build_state(encap, family, cfg, lws, extack);
		if (ret)
			module_put(ops->owner);
	} else {
		/* don't rely on -EOPNOTSUPP to detect match as build_state
		 * handlers could return it
		 */
		NL_SET_ERR_MSG_ATTR(extack, encap,
				    "LWT encapsulation type not supported");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(lwtunnel_build_state);

int lwtunnel_valid_encap_type(u16 encap_type, struct netlink_ext_ack *extack)
{
	const struct lwtunnel_encap_ops *ops;
	int ret = -EINVAL;

	if (encap_type == LWTUNNEL_ENCAP_NONE ||
	    encap_type > LWTUNNEL_ENCAP_MAX) {
		NL_SET_ERR_MSG(extack, "Unknown lwt encapsulation type");
		return ret;
	}

	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[encap_type]);
	rcu_read_unlock();
#ifdef CONFIG_MODULES
	if (!ops) {
		const char *encap_type_str = lwtunnel_encap_str(encap_type);

		if (encap_type_str) {
			__rtnl_unlock();
			request_module("rtnl-lwt-%s", encap_type_str);
			rtnl_lock();

			rcu_read_lock();
			ops = rcu_dereference(lwtun_encaps[encap_type]);
			rcu_read_unlock();
		}
	}
#endif
	ret = ops ? 0 : -EOPNOTSUPP;
	if (ret < 0)
		NL_SET_ERR_MSG(extack, "lwt encapsulation type not supported");

	return ret;
}
EXPORT_SYMBOL_GPL(lwtunnel_valid_encap_type);

int lwtunnel_valid_encap_type_attr(struct nlattr *attr, int remaining,
				   struct netlink_ext_ack *extack)
{
	struct rtnexthop *rtnh = (struct rtnexthop *)attr;
	struct nlattr *nla_entype;
	struct nlattr *attrs;
	u16 encap_type;
	int attrlen;

	while (rtnh_ok(rtnh, remaining)) {
		attrlen = rtnh_attrlen(rtnh);
		if (attrlen > 0) {
			attrs = rtnh_attrs(rtnh);
			nla_entype = nla_find(attrs, attrlen, RTA_ENCAP_TYPE);

			if (nla_entype) {
				encap_type = nla_get_u16(nla_entype);

				if (lwtunnel_valid_encap_type(encap_type,
							      extack) != 0)
					return -EOPNOTSUPP;
			}
		}
		rtnh = rtnh_next(rtnh, &remaining);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(lwtunnel_valid_encap_type_attr);

void lwtstate_free(struct lwtunnel_state *lws)
{
	const struct lwtunnel_encap_ops *ops = lwtun_encaps[lws->type];

	if (ops->destroy_state) {
		ops->destroy_state(lws);
		kfree_rcu(lws, rcu);
	} else {
		kfree(lws);
	}
	module_put(ops->owner);
}
EXPORT_SYMBOL_GPL(lwtstate_free);

int lwtunnel_fill_encap(struct sk_buff *skb, struct lwtunnel_state *lwtstate,
			int encap_attr, int encap_type_attr)
{
	const struct lwtunnel_encap_ops *ops;
	struct nlattr *nest;
	int ret;

	if (!lwtstate)
		return 0;

	if (lwtstate->type == LWTUNNEL_ENCAP_NONE ||
	    lwtstate->type > LWTUNNEL_ENCAP_MAX)
		return 0;

	nest = nla_nest_start_noflag(skb, encap_attr);
	if (!nest)
		return -EMSGSIZE;

	ret = -EOPNOTSUPP;
	rcu_read_lock();
	ops = rcu_dereference(lwtun_encaps[lwtstate->type]);
	if (likely(ops && ops->fill_encap))
		ret = ops->fill_encap(skb, lwtstate);
	rcu_read_unlock();

	if (ret)
		goto nla_put_failure;
	nla_nest_end(skb, nest);
	ret = nla_put_u16(skb, encap_type_attr, lwtstate->type);
	if (ret)
		goto nla_put_failure;

	return 0;

nla_put_failure:
	nla_nest_cancel(skb, nest);

	return (ret == -EOPNOTSUPP ? 0 : ret);
}
EXPORT_SYMBOL_GPL(lwtunnel_fill_encap);

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
EXPORT_SYMBOL_GPL(lwtunnel_get_encap_size);

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
EXPORT_SYMBOL_GPL(lwtunnel_cmp_encap);

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
EXPORT_SYMBOL_GPL(lwtunnel_output);

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
EXPORT_SYMBOL_GPL(lwtunnel_xmit);

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
EXPORT_SYMBOL_GPL(lwtunnel_input);
