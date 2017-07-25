/*
 * (C) 2012 Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation (or any later at your option).
 *
 * This software has been sponsored by Vyatta Inc. <http://www.vyatta.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/rculist.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <net/netlink.h>
#include <net/sock.h>

#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_ecache.h>

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_conntrack.h>
#include <linux/netfilter/nfnetlink_cthelper.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pablo Neira Ayuso <pablo@netfilter.org>");
MODULE_DESCRIPTION("nfnl_cthelper: User-space connection tracking helpers");

struct nfnl_cthelper {
	struct list_head		list;
	struct nf_conntrack_helper	helper;
};

static LIST_HEAD(nfnl_cthelper_list);

static int
nfnl_userspace_cthelper(struct sk_buff *skb, unsigned int protoff,
			struct nf_conn *ct, enum ip_conntrack_info ctinfo)
{
	const struct nf_conn_help *help;
	struct nf_conntrack_helper *helper;

	help = nfct_help(ct);
	if (help == NULL)
		return NF_DROP;

	/* rcu_read_lock()ed by nf_hook_thresh */
	helper = rcu_dereference(help->helper);
	if (helper == NULL)
		return NF_DROP;

	/* This is a user-space helper not yet configured, skip. */
	if ((helper->flags &
	    (NF_CT_HELPER_F_USERSPACE | NF_CT_HELPER_F_CONFIGURED)) ==
	     NF_CT_HELPER_F_USERSPACE)
		return NF_ACCEPT;

	/* If the user-space helper is not available, don't block traffic. */
	return NF_QUEUE_NR(helper->queue_num) | NF_VERDICT_FLAG_QUEUE_BYPASS;
}

static const struct nla_policy nfnl_cthelper_tuple_pol[NFCTH_TUPLE_MAX+1] = {
	[NFCTH_TUPLE_L3PROTONUM] = { .type = NLA_U16, },
	[NFCTH_TUPLE_L4PROTONUM] = { .type = NLA_U8, },
};

static int
nfnl_cthelper_parse_tuple(struct nf_conntrack_tuple *tuple,
			  const struct nlattr *attr)
{
	int err;
	struct nlattr *tb[NFCTH_TUPLE_MAX+1];

	err = nla_parse_nested(tb, NFCTH_TUPLE_MAX, attr,
			       nfnl_cthelper_tuple_pol, NULL);
	if (err < 0)
		return err;

	if (!tb[NFCTH_TUPLE_L3PROTONUM] || !tb[NFCTH_TUPLE_L4PROTONUM])
		return -EINVAL;

	/* Not all fields are initialized so first zero the tuple */
	memset(tuple, 0, sizeof(struct nf_conntrack_tuple));

	tuple->src.l3num = ntohs(nla_get_be16(tb[NFCTH_TUPLE_L3PROTONUM]));
	tuple->dst.protonum = nla_get_u8(tb[NFCTH_TUPLE_L4PROTONUM]);

	return 0;
}

static int
nfnl_cthelper_from_nlattr(struct nlattr *attr, struct nf_conn *ct)
{
	struct nf_conn_help *help = nfct_help(ct);

	if (attr == NULL)
		return -EINVAL;

	if (help->helper->data_len == 0)
		return -EINVAL;

	nla_memcpy(help->data, nla_data(attr), sizeof(help->data));
	return 0;
}

static int
nfnl_cthelper_to_nlattr(struct sk_buff *skb, const struct nf_conn *ct)
{
	const struct nf_conn_help *help = nfct_help(ct);

	if (help->helper->data_len &&
	    nla_put(skb, CTA_HELP_INFO, help->helper->data_len, &help->data))
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -ENOSPC;
}

static const struct nla_policy nfnl_cthelper_expect_pol[NFCTH_POLICY_MAX+1] = {
	[NFCTH_POLICY_NAME] = { .type = NLA_NUL_STRING,
				.len = NF_CT_HELPER_NAME_LEN-1 },
	[NFCTH_POLICY_EXPECT_MAX] = { .type = NLA_U32, },
	[NFCTH_POLICY_EXPECT_TIMEOUT] = { .type = NLA_U32, },
};

static int
nfnl_cthelper_expect_policy(struct nf_conntrack_expect_policy *expect_policy,
			    const struct nlattr *attr)
{
	int err;
	struct nlattr *tb[NFCTH_POLICY_MAX+1];

	err = nla_parse_nested(tb, NFCTH_POLICY_MAX, attr,
			       nfnl_cthelper_expect_pol, NULL);
	if (err < 0)
		return err;

	if (!tb[NFCTH_POLICY_NAME] ||
	    !tb[NFCTH_POLICY_EXPECT_MAX] ||
	    !tb[NFCTH_POLICY_EXPECT_TIMEOUT])
		return -EINVAL;

	strncpy(expect_policy->name,
		nla_data(tb[NFCTH_POLICY_NAME]), NF_CT_HELPER_NAME_LEN);
	expect_policy->max_expected =
		ntohl(nla_get_be32(tb[NFCTH_POLICY_EXPECT_MAX]));
	if (expect_policy->max_expected > NF_CT_EXPECT_MAX_CNT)
		return -EINVAL;

	expect_policy->timeout =
		ntohl(nla_get_be32(tb[NFCTH_POLICY_EXPECT_TIMEOUT]));

	return 0;
}

static const struct nla_policy
nfnl_cthelper_expect_policy_set[NFCTH_POLICY_SET_MAX+1] = {
	[NFCTH_POLICY_SET_NUM] = { .type = NLA_U32, },
};

static int
nfnl_cthelper_parse_expect_policy(struct nf_conntrack_helper *helper,
				  const struct nlattr *attr)
{
	int i, ret;
	struct nf_conntrack_expect_policy *expect_policy;
	struct nlattr *tb[NFCTH_POLICY_SET_MAX+1];
	unsigned int class_max;

	ret = nla_parse_nested(tb, NFCTH_POLICY_SET_MAX, attr,
			       nfnl_cthelper_expect_policy_set, NULL);
	if (ret < 0)
		return ret;

	if (!tb[NFCTH_POLICY_SET_NUM])
		return -EINVAL;

	class_max = ntohl(nla_get_be32(tb[NFCTH_POLICY_SET_NUM]));
	if (class_max == 0)
		return -EINVAL;
	if (class_max > NF_CT_MAX_EXPECT_CLASSES)
		return -EOVERFLOW;

	expect_policy = kzalloc(sizeof(struct nf_conntrack_expect_policy) *
				class_max, GFP_KERNEL);
	if (expect_policy == NULL)
		return -ENOMEM;

	for (i = 0; i < class_max; i++) {
		if (!tb[NFCTH_POLICY_SET+i])
			goto err;

		ret = nfnl_cthelper_expect_policy(&expect_policy[i],
						  tb[NFCTH_POLICY_SET+i]);
		if (ret < 0)
			goto err;
	}

	helper->expect_class_max = class_max - 1;
	helper->expect_policy = expect_policy;
	return 0;
err:
	kfree(expect_policy);
	return -EINVAL;
}

static int
nfnl_cthelper_create(const struct nlattr * const tb[],
		     struct nf_conntrack_tuple *tuple)
{
	struct nf_conntrack_helper *helper;
	struct nfnl_cthelper *nfcth;
	unsigned int size;
	int ret;

	if (!tb[NFCTH_TUPLE] || !tb[NFCTH_POLICY] || !tb[NFCTH_PRIV_DATA_LEN])
		return -EINVAL;

	nfcth = kzalloc(sizeof(*nfcth), GFP_KERNEL);
	if (nfcth == NULL)
		return -ENOMEM;
	helper = &nfcth->helper;

	ret = nfnl_cthelper_parse_expect_policy(helper, tb[NFCTH_POLICY]);
	if (ret < 0)
		goto err1;

	strncpy(helper->name, nla_data(tb[NFCTH_NAME]), NF_CT_HELPER_NAME_LEN);
	size = ntohl(nla_get_be32(tb[NFCTH_PRIV_DATA_LEN]));
	if (size > FIELD_SIZEOF(struct nf_conn_help, data)) {
		ret = -ENOMEM;
		goto err2;
	}

	helper->flags |= NF_CT_HELPER_F_USERSPACE;
	memcpy(&helper->tuple, tuple, sizeof(struct nf_conntrack_tuple));

	helper->me = THIS_MODULE;
	helper->help = nfnl_userspace_cthelper;
	helper->from_nlattr = nfnl_cthelper_from_nlattr;
	helper->to_nlattr = nfnl_cthelper_to_nlattr;

	/* Default to queue number zero, this can be updated at any time. */
	if (tb[NFCTH_QUEUE_NUM])
		helper->queue_num = ntohl(nla_get_be32(tb[NFCTH_QUEUE_NUM]));

	if (tb[NFCTH_STATUS]) {
		int status = ntohl(nla_get_be32(tb[NFCTH_STATUS]));

		switch(status) {
		case NFCT_HELPER_STATUS_ENABLED:
			helper->flags |= NF_CT_HELPER_F_CONFIGURED;
			break;
		case NFCT_HELPER_STATUS_DISABLED:
			helper->flags &= ~NF_CT_HELPER_F_CONFIGURED;
			break;
		}
	}

	ret = nf_conntrack_helper_register(helper);
	if (ret < 0)
		goto err2;

	list_add_tail(&nfcth->list, &nfnl_cthelper_list);
	return 0;
err2:
	kfree(helper->expect_policy);
err1:
	kfree(nfcth);
	return ret;
}

static int
nfnl_cthelper_update_policy_one(const struct nf_conntrack_expect_policy *policy,
				struct nf_conntrack_expect_policy *new_policy,
				const struct nlattr *attr)
{
	struct nlattr *tb[NFCTH_POLICY_MAX + 1];
	int err;

	err = nla_parse_nested(tb, NFCTH_POLICY_MAX, attr,
			       nfnl_cthelper_expect_pol, NULL);
	if (err < 0)
		return err;

	if (!tb[NFCTH_POLICY_NAME] ||
	    !tb[NFCTH_POLICY_EXPECT_MAX] ||
	    !tb[NFCTH_POLICY_EXPECT_TIMEOUT])
		return -EINVAL;

	if (nla_strcmp(tb[NFCTH_POLICY_NAME], policy->name))
		return -EBUSY;

	new_policy->max_expected =
		ntohl(nla_get_be32(tb[NFCTH_POLICY_EXPECT_MAX]));
	if (new_policy->max_expected > NF_CT_EXPECT_MAX_CNT)
		return -EINVAL;

	new_policy->timeout =
		ntohl(nla_get_be32(tb[NFCTH_POLICY_EXPECT_TIMEOUT]));

	return 0;
}

static int nfnl_cthelper_update_policy_all(struct nlattr *tb[],
					   struct nf_conntrack_helper *helper)
{
	struct nf_conntrack_expect_policy new_policy[helper->expect_class_max + 1];
	struct nf_conntrack_expect_policy *policy;
	int i, err;

	/* Check first that all policy attributes are well-formed, so we don't
	 * leave things in inconsistent state on errors.
	 */
	for (i = 0; i < helper->expect_class_max + 1; i++) {

		if (!tb[NFCTH_POLICY_SET + i])
			return -EINVAL;

		err = nfnl_cthelper_update_policy_one(&helper->expect_policy[i],
						      &new_policy[i],
						      tb[NFCTH_POLICY_SET + i]);
		if (err < 0)
			return err;
	}
	/* Now we can safely update them. */
	for (i = 0; i < helper->expect_class_max + 1; i++) {
		policy = (struct nf_conntrack_expect_policy *)
				&helper->expect_policy[i];
		policy->max_expected = new_policy->max_expected;
		policy->timeout	= new_policy->timeout;
	}

	return 0;
}

static int nfnl_cthelper_update_policy(struct nf_conntrack_helper *helper,
				       const struct nlattr *attr)
{
	struct nlattr *tb[NFCTH_POLICY_SET_MAX + 1];
	unsigned int class_max;
	int err;

	err = nla_parse_nested(tb, NFCTH_POLICY_SET_MAX, attr,
			       nfnl_cthelper_expect_policy_set, NULL);
	if (err < 0)
		return err;

	if (!tb[NFCTH_POLICY_SET_NUM])
		return -EINVAL;

	class_max = ntohl(nla_get_be32(tb[NFCTH_POLICY_SET_NUM]));
	if (helper->expect_class_max + 1 != class_max)
		return -EBUSY;

	return nfnl_cthelper_update_policy_all(tb, helper);
}

static int
nfnl_cthelper_update(const struct nlattr * const tb[],
		     struct nf_conntrack_helper *helper)
{
	int ret;

	if (tb[NFCTH_PRIV_DATA_LEN])
		return -EBUSY;

	if (tb[NFCTH_POLICY]) {
		ret = nfnl_cthelper_update_policy(helper, tb[NFCTH_POLICY]);
		if (ret < 0)
			return ret;
	}
	if (tb[NFCTH_QUEUE_NUM])
		helper->queue_num = ntohl(nla_get_be32(tb[NFCTH_QUEUE_NUM]));

	if (tb[NFCTH_STATUS]) {
		int status = ntohl(nla_get_be32(tb[NFCTH_STATUS]));

		switch(status) {
		case NFCT_HELPER_STATUS_ENABLED:
			helper->flags |= NF_CT_HELPER_F_CONFIGURED;
			break;
		case NFCT_HELPER_STATUS_DISABLED:
			helper->flags &= ~NF_CT_HELPER_F_CONFIGURED;
			break;
		}
	}
	return 0;
}

static int nfnl_cthelper_new(struct net *net, struct sock *nfnl,
			     struct sk_buff *skb, const struct nlmsghdr *nlh,
			     const struct nlattr * const tb[],
			     struct netlink_ext_ack *extack)
{
	const char *helper_name;
	struct nf_conntrack_helper *cur, *helper = NULL;
	struct nf_conntrack_tuple tuple;
	struct nfnl_cthelper *nlcth;
	int ret = 0;

	if (!tb[NFCTH_NAME] || !tb[NFCTH_TUPLE])
		return -EINVAL;

	helper_name = nla_data(tb[NFCTH_NAME]);

	ret = nfnl_cthelper_parse_tuple(&tuple, tb[NFCTH_TUPLE]);
	if (ret < 0)
		return ret;

	list_for_each_entry(nlcth, &nfnl_cthelper_list, list) {
		cur = &nlcth->helper;

		if (strncmp(cur->name, helper_name, NF_CT_HELPER_NAME_LEN))
			continue;

		if ((tuple.src.l3num != cur->tuple.src.l3num ||
		     tuple.dst.protonum != cur->tuple.dst.protonum))
			continue;

		if (nlh->nlmsg_flags & NLM_F_EXCL)
			return -EEXIST;

		helper = cur;
		break;
	}

	if (helper == NULL)
		ret = nfnl_cthelper_create(tb, &tuple);
	else
		ret = nfnl_cthelper_update(tb, helper);

	return ret;
}

static int
nfnl_cthelper_dump_tuple(struct sk_buff *skb,
			 struct nf_conntrack_helper *helper)
{
	struct nlattr *nest_parms;

	nest_parms = nla_nest_start(skb, NFCTH_TUPLE | NLA_F_NESTED);
	if (nest_parms == NULL)
		goto nla_put_failure;

	if (nla_put_be16(skb, NFCTH_TUPLE_L3PROTONUM,
			 htons(helper->tuple.src.l3num)))
		goto nla_put_failure;

	if (nla_put_u8(skb, NFCTH_TUPLE_L4PROTONUM, helper->tuple.dst.protonum))
		goto nla_put_failure;

	nla_nest_end(skb, nest_parms);
	return 0;

nla_put_failure:
	return -1;
}

static int
nfnl_cthelper_dump_policy(struct sk_buff *skb,
			struct nf_conntrack_helper *helper)
{
	int i;
	struct nlattr *nest_parms1, *nest_parms2;

	nest_parms1 = nla_nest_start(skb, NFCTH_POLICY | NLA_F_NESTED);
	if (nest_parms1 == NULL)
		goto nla_put_failure;

	if (nla_put_be32(skb, NFCTH_POLICY_SET_NUM,
			 htonl(helper->expect_class_max + 1)))
		goto nla_put_failure;

	for (i = 0; i < helper->expect_class_max + 1; i++) {
		nest_parms2 = nla_nest_start(skb,
				(NFCTH_POLICY_SET+i) | NLA_F_NESTED);
		if (nest_parms2 == NULL)
			goto nla_put_failure;

		if (nla_put_string(skb, NFCTH_POLICY_NAME,
				   helper->expect_policy[i].name))
			goto nla_put_failure;

		if (nla_put_be32(skb, NFCTH_POLICY_EXPECT_MAX,
				 htonl(helper->expect_policy[i].max_expected)))
			goto nla_put_failure;

		if (nla_put_be32(skb, NFCTH_POLICY_EXPECT_TIMEOUT,
				 htonl(helper->expect_policy[i].timeout)))
			goto nla_put_failure;

		nla_nest_end(skb, nest_parms2);
	}
	nla_nest_end(skb, nest_parms1);
	return 0;

nla_put_failure:
	return -1;
}

static int
nfnl_cthelper_fill_info(struct sk_buff *skb, u32 portid, u32 seq, u32 type,
			int event, struct nf_conntrack_helper *helper)
{
	struct nlmsghdr *nlh;
	struct nfgenmsg *nfmsg;
	unsigned int flags = portid ? NLM_F_MULTI : 0;
	int status;

	event = nfnl_msg_type(NFNL_SUBSYS_CTHELPER, event);
	nlh = nlmsg_put(skb, portid, seq, event, sizeof(*nfmsg), flags);
	if (nlh == NULL)
		goto nlmsg_failure;

	nfmsg = nlmsg_data(nlh);
	nfmsg->nfgen_family = AF_UNSPEC;
	nfmsg->version = NFNETLINK_V0;
	nfmsg->res_id = 0;

	if (nla_put_string(skb, NFCTH_NAME, helper->name))
		goto nla_put_failure;

	if (nla_put_be32(skb, NFCTH_QUEUE_NUM, htonl(helper->queue_num)))
		goto nla_put_failure;

	if (nfnl_cthelper_dump_tuple(skb, helper) < 0)
		goto nla_put_failure;

	if (nfnl_cthelper_dump_policy(skb, helper) < 0)
		goto nla_put_failure;

	if (nla_put_be32(skb, NFCTH_PRIV_DATA_LEN, htonl(helper->data_len)))
		goto nla_put_failure;

	if (helper->flags & NF_CT_HELPER_F_CONFIGURED)
		status = NFCT_HELPER_STATUS_ENABLED;
	else
		status = NFCT_HELPER_STATUS_DISABLED;

	if (nla_put_be32(skb, NFCTH_STATUS, htonl(status)))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return skb->len;

nlmsg_failure:
nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -1;
}

static int
nfnl_cthelper_dump_table(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nf_conntrack_helper *cur, *last;

	rcu_read_lock();
	last = (struct nf_conntrack_helper *)cb->args[1];
	for (; cb->args[0] < nf_ct_helper_hsize; cb->args[0]++) {
restart:
		hlist_for_each_entry_rcu(cur,
				&nf_ct_helper_hash[cb->args[0]], hnode) {

			/* skip non-userspace conntrack helpers. */
			if (!(cur->flags & NF_CT_HELPER_F_USERSPACE))
				continue;

			if (cb->args[1]) {
				if (cur != last)
					continue;
				cb->args[1] = 0;
			}
			if (nfnl_cthelper_fill_info(skb,
					    NETLINK_CB(cb->skb).portid,
					    cb->nlh->nlmsg_seq,
					    NFNL_MSG_TYPE(cb->nlh->nlmsg_type),
					    NFNL_MSG_CTHELPER_NEW, cur) < 0) {
				cb->args[1] = (unsigned long)cur;
				goto out;
			}
		}
	}
	if (cb->args[1]) {
		cb->args[1] = 0;
		goto restart;
	}
out:
	rcu_read_unlock();
	return skb->len;
}

static int nfnl_cthelper_get(struct net *net, struct sock *nfnl,
			     struct sk_buff *skb, const struct nlmsghdr *nlh,
			     const struct nlattr * const tb[],
			     struct netlink_ext_ack *extack)
{
	int ret = -ENOENT;
	struct nf_conntrack_helper *cur;
	struct sk_buff *skb2;
	char *helper_name = NULL;
	struct nf_conntrack_tuple tuple;
	struct nfnl_cthelper *nlcth;
	bool tuple_set = false;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		struct netlink_dump_control c = {
			.dump = nfnl_cthelper_dump_table,
		};
		return netlink_dump_start(nfnl, skb, nlh, &c);
	}

	if (tb[NFCTH_NAME])
		helper_name = nla_data(tb[NFCTH_NAME]);

	if (tb[NFCTH_TUPLE]) {
		ret = nfnl_cthelper_parse_tuple(&tuple, tb[NFCTH_TUPLE]);
		if (ret < 0)
			return ret;

		tuple_set = true;
	}

	list_for_each_entry(nlcth, &nfnl_cthelper_list, list) {
		cur = &nlcth->helper;
		if (helper_name &&
		    strncmp(cur->name, helper_name, NF_CT_HELPER_NAME_LEN))
			continue;

		if (tuple_set &&
		    (tuple.src.l3num != cur->tuple.src.l3num ||
		     tuple.dst.protonum != cur->tuple.dst.protonum))
			continue;

		skb2 = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
		if (skb2 == NULL) {
			ret = -ENOMEM;
			break;
		}

		ret = nfnl_cthelper_fill_info(skb2, NETLINK_CB(skb).portid,
					      nlh->nlmsg_seq,
					      NFNL_MSG_TYPE(nlh->nlmsg_type),
					      NFNL_MSG_CTHELPER_NEW, cur);
		if (ret <= 0) {
			kfree_skb(skb2);
			break;
		}

		ret = netlink_unicast(nfnl, skb2, NETLINK_CB(skb).portid,
				      MSG_DONTWAIT);
		if (ret > 0)
			ret = 0;

		/* this avoids a loop in nfnetlink. */
		return ret == -EAGAIN ? -ENOBUFS : ret;
	}
	return ret;
}

static int nfnl_cthelper_del(struct net *net, struct sock *nfnl,
			     struct sk_buff *skb, const struct nlmsghdr *nlh,
			     const struct nlattr * const tb[],
			     struct netlink_ext_ack *extack)
{
	char *helper_name = NULL;
	struct nf_conntrack_helper *cur;
	struct nf_conntrack_tuple tuple;
	bool tuple_set = false, found = false;
	struct nfnl_cthelper *nlcth, *n;
	int j = 0, ret;

	if (tb[NFCTH_NAME])
		helper_name = nla_data(tb[NFCTH_NAME]);

	if (tb[NFCTH_TUPLE]) {
		ret = nfnl_cthelper_parse_tuple(&tuple, tb[NFCTH_TUPLE]);
		if (ret < 0)
			return ret;

		tuple_set = true;
	}

	ret = -ENOENT;
	list_for_each_entry_safe(nlcth, n, &nfnl_cthelper_list, list) {
		cur = &nlcth->helper;
		j++;

		if (helper_name &&
		    strncmp(cur->name, helper_name, NF_CT_HELPER_NAME_LEN))
			continue;

		if (tuple_set &&
		    (tuple.src.l3num != cur->tuple.src.l3num ||
		     tuple.dst.protonum != cur->tuple.dst.protonum))
			continue;

		if (refcount_dec_if_one(&cur->refcnt)) {
			found = true;
			nf_conntrack_helper_unregister(cur);
			kfree(cur->expect_policy);

			list_del(&nlcth->list);
			kfree(nlcth);
		} else {
			ret = -EBUSY;
		}
	}

	/* Make sure we return success if we flush and there is no helpers */
	return (found || j == 0) ? 0 : ret;
}

static const struct nla_policy nfnl_cthelper_policy[NFCTH_MAX+1] = {
	[NFCTH_NAME] = { .type = NLA_NUL_STRING,
			 .len = NF_CT_HELPER_NAME_LEN-1 },
	[NFCTH_QUEUE_NUM] = { .type = NLA_U32, },
};

static const struct nfnl_callback nfnl_cthelper_cb[NFNL_MSG_CTHELPER_MAX] = {
	[NFNL_MSG_CTHELPER_NEW]		= { .call = nfnl_cthelper_new,
					    .attr_count = NFCTH_MAX,
					    .policy = nfnl_cthelper_policy },
	[NFNL_MSG_CTHELPER_GET]		= { .call = nfnl_cthelper_get,
					    .attr_count = NFCTH_MAX,
					    .policy = nfnl_cthelper_policy },
	[NFNL_MSG_CTHELPER_DEL]		= { .call = nfnl_cthelper_del,
					    .attr_count = NFCTH_MAX,
					    .policy = nfnl_cthelper_policy },
};

static const struct nfnetlink_subsystem nfnl_cthelper_subsys = {
	.name				= "cthelper",
	.subsys_id			= NFNL_SUBSYS_CTHELPER,
	.cb_count			= NFNL_MSG_CTHELPER_MAX,
	.cb				= nfnl_cthelper_cb,
};

MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_CTHELPER);

static int __init nfnl_cthelper_init(void)
{
	int ret;

	ret = nfnetlink_subsys_register(&nfnl_cthelper_subsys);
	if (ret < 0) {
		pr_err("nfnl_cthelper: cannot register with nfnetlink.\n");
		goto err_out;
	}
	return 0;
err_out:
	return ret;
}

static void __exit nfnl_cthelper_exit(void)
{
	struct nf_conntrack_helper *cur;
	struct nfnl_cthelper *nlcth, *n;

	nfnetlink_subsys_unregister(&nfnl_cthelper_subsys);

	list_for_each_entry_safe(nlcth, n, &nfnl_cthelper_list, list) {
		cur = &nlcth->helper;

		nf_conntrack_helper_unregister(cur);
		kfree(cur->expect_policy);
		kfree(nlcth);
	}
}

module_init(nfnl_cthelper_init);
module_exit(nfnl_cthelper_exit);
