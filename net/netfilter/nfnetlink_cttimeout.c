// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) 2012 by Pablo Neira Ayuso <pablo@netfilter.org>
 * (C) 2012 by Vyatta Inc. <http://www.vyatta.com>
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
#include <net/netns/generic.h>
#include <net/sock.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_tuple.h>
#include <net/netfilter/nf_conntrack_timeout.h>

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_cttimeout.h>

static unsigned int nfct_timeout_id __read_mostly;

struct nfct_timeout_pernet {
	struct list_head	nfct_timeout_list;
};

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

static struct nfct_timeout_pernet *nfct_timeout_pernet(struct net *net)
{
	return net_generic(net, nfct_timeout_id);
}

static int
ctnl_timeout_parse_policy(void *timeout,
			  const struct nf_conntrack_l4proto *l4proto,
			  struct net *net, const struct nlattr *attr)
{
	struct nlattr **tb;
	int ret = 0;

	tb = kcalloc(l4proto->ctnl_timeout.nlattr_max + 1, sizeof(*tb),
		     GFP_KERNEL);

	if (!tb)
		return -ENOMEM;

	ret = nla_parse_nested_deprecated(tb,
					  l4proto->ctnl_timeout.nlattr_max,
					  attr,
					  l4proto->ctnl_timeout.nla_policy,
					  NULL);
	if (ret < 0)
		goto err;

	ret = l4proto->ctnl_timeout.nlattr_to_obj(tb, net, timeout);

err:
	kfree(tb);
	return ret;
}

static int cttimeout_new_timeout(struct sk_buff *skb,
				 const struct nfnl_info *info,
				 const struct nlattr * const cda[])
{
	struct nfct_timeout_pernet *pernet = nfct_timeout_pernet(info->net);
	__u16 l3num;
	__u8 l4num;
	const struct nf_conntrack_l4proto *l4proto;
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

	list_for_each_entry(timeout, &pernet->nfct_timeout_list, head) {
		if (strncmp(timeout->name, name, CTNL_TIMEOUT_NAME_MAX) != 0)
			continue;

		if (info->nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;

		matching = timeout;
		break;
	}

	if (matching) {
		if (info->nlh->nlmsg_flags & NLM_F_REPLACE) {
			/* You cannot replace one timeout policy by another of
			 * different kind, sorry.
			 */
			if (matching->timeout.l3num != l3num ||
			    matching->timeout.l4proto->l4proto != l4num)
				return -EINVAL;

			return ctnl_timeout_parse_policy(&matching->timeout.data,
							 matching->timeout.l4proto,
							 info->net,
							 cda[CTA_TIMEOUT_DATA]);
		}

		return -EBUSY;
	}

	l4proto = nf_ct_l4proto_find(l4num);

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

	ret = ctnl_timeout_parse_policy(&timeout->timeout.data, l4proto,
					info->net, cda[CTA_TIMEOUT_DATA]);
	if (ret < 0)
		goto err;

	strcpy(timeout->name, nla_data(cda[CTA_TIMEOUT_NAME]));
	timeout->timeout.l3num = l3num;
	timeout->timeout.l4proto = l4proto;
	refcount_set(&timeout->refcnt, 1);
	list_add_tail_rcu(&timeout->head, &pernet->nfct_timeout_list);

	return 0;
err:
	kfree(timeout);
err_proto_put:
	return ret;
}

static int
ctnl_timeout_fill_info(struct sk_buff *skb, u32 portid, u32 seq, u32 type,
		       int event, struct ctnl_timeout *timeout)
{
	struct nlmsghdr *nlh;
	unsigned int flags = portid ? NLM_F_MULTI : 0;
	const struct nf_conntrack_l4proto *l4proto = timeout->timeout.l4proto;
	struct nlattr *nest_parms;
	int ret;

	event = nfnl_msg_type(NFNL_SUBSYS_CTNETLINK_TIMEOUT, event);
	nlh = nfnl_msg_put(skb, portid, seq, event, flags, AF_UNSPEC,
			   NFNETLINK_V0, 0);
	if (!nlh)
		goto nlmsg_failure;

	if (nla_put_string(skb, CTA_TIMEOUT_NAME, timeout->name) ||
	    nla_put_be16(skb, CTA_TIMEOUT_L3PROTO,
			 htons(timeout->timeout.l3num)) ||
	    nla_put_u8(skb, CTA_TIMEOUT_L4PROTO, l4proto->l4proto) ||
	    nla_put_be32(skb, CTA_TIMEOUT_USE,
			 htonl(refcount_read(&timeout->refcnt))))
		goto nla_put_failure;

	nest_parms = nla_nest_start(skb, CTA_TIMEOUT_DATA);
	if (!nest_parms)
		goto nla_put_failure;

	ret = l4proto->ctnl_timeout.obj_to_nlattr(skb, &timeout->timeout.data);
	if (ret < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);

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
	struct nfct_timeout_pernet *pernet;
	struct net *net = sock_net(skb->sk);
	struct ctnl_timeout *cur, *last;

	if (cb->args[2])
		return 0;

	last = (struct ctnl_timeout *)cb->args[1];
	if (cb->args[1])
		cb->args[1] = 0;

	rcu_read_lock();
	pernet = nfct_timeout_pernet(net);
	list_for_each_entry_rcu(cur, &pernet->nfct_timeout_list, head) {
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

static int cttimeout_get_timeout(struct sk_buff *skb,
				 const struct nfnl_info *info,
				 const struct nlattr * const cda[])
{
	struct nfct_timeout_pernet *pernet = nfct_timeout_pernet(info->net);
	int ret = -ENOENT;
	char *name;
	struct ctnl_timeout *cur;

	if (info->nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = ctnl_timeout_dump,
		};
		return netlink_dump_start(info->sk, skb, info->nlh, &c);
	}

	if (!cda[CTA_TIMEOUT_NAME])
		return -EINVAL;
	name = nla_data(cda[CTA_TIMEOUT_NAME]);

	list_for_each_entry(cur, &pernet->nfct_timeout_list, head) {
		struct sk_buff *skb2;

		if (strncmp(cur->name, name, CTNL_TIMEOUT_NAME_MAX) != 0)
			continue;

		skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
		if (skb2 == NULL) {
			ret = -ENOMEM;
			break;
		}

		ret = ctnl_timeout_fill_info(skb2, NETLINK_CB(skb).portid,
					     info->nlh->nlmsg_seq,
					     NFNL_MSG_TYPE(info->nlh->nlmsg_type),
					     IPCTNL_MSG_TIMEOUT_NEW, cur);
		if (ret <= 0) {
			kfree_skb(skb2);
			break;
		}

		ret = nfnetlink_unicast(skb2, info->net, NETLINK_CB(skb).portid);
		break;
	}

	return ret;
}

/* try to delete object, fail if it is still in use. */
static int ctnl_timeout_try_del(struct net *net, struct ctnl_timeout *timeout)
{
	int ret = 0;

	/* We want to avoid races with ctnl_timeout_put. So only when the
	 * current refcnt is 1, we decrease it to 0.
	 */
	if (refcount_dec_if_one(&timeout->refcnt)) {
		/* We are protected by nfnl mutex. */
		list_del_rcu(&timeout->head);
		nf_ct_untimeout(net, &timeout->timeout);
		kfree_rcu(timeout, rcu_head);
	} else {
		ret = -EBUSY;
	}
	return ret;
}

static int cttimeout_del_timeout(struct sk_buff *skb,
				 const struct nfnl_info *info,
				 const struct nlattr * const cda[])
{
	struct nfct_timeout_pernet *pernet = nfct_timeout_pernet(info->net);
	struct ctnl_timeout *cur, *tmp;
	int ret = -ENOENT;
	char *name;

	if (!cda[CTA_TIMEOUT_NAME]) {
		list_for_each_entry_safe(cur, tmp, &pernet->nfct_timeout_list,
					 head)
			ctnl_timeout_try_del(info->net, cur);

		return 0;
	}
	name = nla_data(cda[CTA_TIMEOUT_NAME]);

	list_for_each_entry(cur, &pernet->nfct_timeout_list, head) {
		if (strncmp(cur->name, name, CTNL_TIMEOUT_NAME_MAX) != 0)
			continue;

		ret = ctnl_timeout_try_del(info->net, cur);
		if (ret < 0)
			return ret;

		break;
	}
	return ret;
}

static int cttimeout_default_set(struct sk_buff *skb,
				 const struct nfnl_info *info,
				 const struct nlattr * const cda[])
{
	const struct nf_conntrack_l4proto *l4proto;
	__u8 l4num;
	int ret;

	if (!cda[CTA_TIMEOUT_L3PROTO] ||
	    !cda[CTA_TIMEOUT_L4PROTO] ||
	    !cda[CTA_TIMEOUT_DATA])
		return -EINVAL;

	l4num = nla_get_u8(cda[CTA_TIMEOUT_L4PROTO]);
	l4proto = nf_ct_l4proto_find(l4num);

	/* This protocol is not supported, skip. */
	if (l4proto->l4proto != l4num) {
		ret = -EOPNOTSUPP;
		goto err;
	}

	ret = ctnl_timeout_parse_policy(NULL, l4proto, info->net,
					cda[CTA_TIMEOUT_DATA]);
	if (ret < 0)
		goto err;

	return 0;
err:
	return ret;
}

static int
cttimeout_default_fill_info(struct net *net, struct sk_buff *skb, u32 portid,
			    u32 seq, u32 type, int event, u16 l3num,
			    const struct nf_conntrack_l4proto *l4proto,
			    const unsigned int *timeouts)
{
	struct nlmsghdr *nlh;
	unsigned int flags = portid ? NLM_F_MULTI : 0;
	struct nlattr *nest_parms;
	int ret;

	event = nfnl_msg_type(NFNL_SUBSYS_CTNETLINK_TIMEOUT, event);
	nlh = nfnl_msg_put(skb, portid, seq, event, flags, AF_UNSPEC,
			   NFNETLINK_V0, 0);
	if (!nlh)
		goto nlmsg_failure;

	if (nla_put_be16(skb, CTA_TIMEOUT_L3PROTO, htons(l3num)) ||
	    nla_put_u8(skb, CTA_TIMEOUT_L4PROTO, l4proto->l4proto))
		goto nla_put_failure;

	nest_parms = nla_nest_start(skb, CTA_TIMEOUT_DATA);
	if (!nest_parms)
		goto nla_put_failure;

	ret = l4proto->ctnl_timeout.obj_to_nlattr(skb, timeouts);
	if (ret < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);

	nlmsg_end(skb, nlh);
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static int cttimeout_default_get(struct sk_buff *skb,
				 const struct nfnl_info *info,
				 const struct nlattr * const cda[])
{
	const struct nf_conntrack_l4proto *l4proto;
	unsigned int *timeouts = NULL;
	struct sk_buff *skb2;
	__u16 l3num;
	__u8 l4num;
	int ret;

	if (!cda[CTA_TIMEOUT_L3PROTO] || !cda[CTA_TIMEOUT_L4PROTO])
		return -EINVAL;

	l3num = ntohs(nla_get_be16(cda[CTA_TIMEOUT_L3PROTO]));
	l4num = nla_get_u8(cda[CTA_TIMEOUT_L4PROTO]);
	l4proto = nf_ct_l4proto_find(l4num);

	if (l4proto->l4proto != l4num)
		return -EOPNOTSUPP;

	switch (l4proto->l4proto) {
	case IPPROTO_ICMP:
		timeouts = &nf_icmp_pernet(info->net)->timeout;
		break;
	case IPPROTO_TCP:
		timeouts = nf_tcp_pernet(info->net)->timeouts;
		break;
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
		timeouts = nf_udp_pernet(info->net)->timeouts;
		break;
	case IPPROTO_DCCP:
#ifdef CONFIG_NF_CT_PROTO_DCCP
		timeouts = nf_dccp_pernet(info->net)->dccp_timeout;
#endif
		break;
	case IPPROTO_ICMPV6:
		timeouts = &nf_icmpv6_pernet(info->net)->timeout;
		break;
	case IPPROTO_SCTP:
#ifdef CONFIG_NF_CT_PROTO_SCTP
		timeouts = nf_sctp_pernet(info->net)->timeouts;
#endif
		break;
	case IPPROTO_GRE:
#ifdef CONFIG_NF_CT_PROTO_GRE
		timeouts = nf_gre_pernet(info->net)->timeouts;
#endif
		break;
	case 255:
		timeouts = &nf_generic_pernet(info->net)->timeout;
		break;
	default:
		WARN_ONCE(1, "Missing timeouts for proto %d", l4proto->l4proto);
		break;
	}

	if (!timeouts)
		return -EOPNOTSUPP;

	skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb2)
		return -ENOMEM;

	ret = cttimeout_default_fill_info(info->net, skb2,
					  NETLINK_CB(skb).portid,
					  info->nlh->nlmsg_seq,
					  NFNL_MSG_TYPE(info->nlh->nlmsg_type),
					  IPCTNL_MSG_TIMEOUT_DEFAULT_SET,
					  l3num, l4proto, timeouts);
	if (ret <= 0) {
		kfree_skb(skb2);
		return -ENOMEM;
	}

	return nfnetlink_unicast(skb2, info->net, NETLINK_CB(skb).portid);
}

static struct nf_ct_timeout *ctnl_timeout_find_get(struct net *net,
						   const char *name)
{
	struct nfct_timeout_pernet *pernet = nfct_timeout_pernet(net);
	struct ctnl_timeout *timeout, *matching = NULL;

	list_for_each_entry_rcu(timeout, &pernet->nfct_timeout_list, head) {
		if (strncmp(timeout->name, name, CTNL_TIMEOUT_NAME_MAX) != 0)
			continue;

		if (!try_module_get(THIS_MODULE))
			goto err;

		if (!refcount_inc_not_zero(&timeout->refcnt)) {
			module_put(THIS_MODULE);
			goto err;
		}
		matching = timeout;
		break;
	}
err:
	return matching ? &matching->timeout : NULL;
}

static void ctnl_timeout_put(struct nf_ct_timeout *t)
{
	struct ctnl_timeout *timeout =
		container_of(t, struct ctnl_timeout, timeout);

	if (refcount_dec_and_test(&timeout->refcnt))
		kfree_rcu(timeout, rcu_head);

	module_put(THIS_MODULE);
}

static const struct nfnl_callback cttimeout_cb[IPCTNL_MSG_TIMEOUT_MAX] = {
	[IPCTNL_MSG_TIMEOUT_NEW] = {
		.call		= cttimeout_new_timeout,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= CTA_TIMEOUT_MAX,
		.policy		= cttimeout_nla_policy
	},
	[IPCTNL_MSG_TIMEOUT_GET] = {
		.call		= cttimeout_get_timeout,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= CTA_TIMEOUT_MAX,
		.policy		= cttimeout_nla_policy
	},
	[IPCTNL_MSG_TIMEOUT_DELETE] = {
		.call		= cttimeout_del_timeout,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= CTA_TIMEOUT_MAX,
		.policy		= cttimeout_nla_policy
	},
	[IPCTNL_MSG_TIMEOUT_DEFAULT_SET] = {
		.call		= cttimeout_default_set,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= CTA_TIMEOUT_MAX,
		.policy		= cttimeout_nla_policy
	},
	[IPCTNL_MSG_TIMEOUT_DEFAULT_GET] = {
		.call		= cttimeout_default_get,
		.type		= NFNL_CB_MUTEX,
		.attr_count	= CTA_TIMEOUT_MAX,
		.policy		= cttimeout_nla_policy
	},
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
	struct nfct_timeout_pernet *pernet = nfct_timeout_pernet(net);

	INIT_LIST_HEAD(&pernet->nfct_timeout_list);

	return 0;
}

static void __net_exit cttimeout_net_exit(struct net *net)
{
	struct nfct_timeout_pernet *pernet = nfct_timeout_pernet(net);
	struct ctnl_timeout *cur, *tmp;

	nf_ct_unconfirmed_destroy(net);
	nf_ct_untimeout(net, NULL);

	list_for_each_entry_safe(cur, tmp, &pernet->nfct_timeout_list, head) {
		list_del_rcu(&cur->head);

		if (refcount_dec_and_test(&cur->refcnt))
			kfree_rcu(cur, rcu_head);
	}
}

static struct pernet_operations cttimeout_ops = {
	.init	= cttimeout_net_init,
	.exit	= cttimeout_net_exit,
	.id     = &nfct_timeout_id,
	.size   = sizeof(struct nfct_timeout_pernet),
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
	RCU_INIT_POINTER(nf_ct_timeout_find_get_hook, ctnl_timeout_find_get);
	RCU_INIT_POINTER(nf_ct_timeout_put_hook, ctnl_timeout_put);
	return 0;

err_out:
	unregister_pernet_subsys(&cttimeout_ops);
	return ret;
}

static void __exit cttimeout_exit(void)
{
	nfnetlink_subsys_unregister(&cttimeout_subsys);

	unregister_pernet_subsys(&cttimeout_ops);
	RCU_INIT_POINTER(nf_ct_timeout_find_get_hook, NULL);
	RCU_INIT_POINTER(nf_ct_timeout_put_hook, NULL);
	synchronize_rcu();
}

module_init(cttimeout_init);
module_exit(cttimeout_exit);
