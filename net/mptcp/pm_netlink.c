// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2020, Red Hat, Inc.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include "protocol.h"
#include "mptcp_pm_gen.h"

#define MPTCP_PM_CMD_GRP_OFFSET       0
#define MPTCP_PM_EV_GRP_OFFSET        1

static const struct genl_multicast_group mptcp_pm_mcgrps[] = {
	[MPTCP_PM_CMD_GRP_OFFSET]	= { .name = MPTCP_PM_CMD_GRP_NAME, },
	[MPTCP_PM_EV_GRP_OFFSET]        = { .name = MPTCP_PM_EV_GRP_NAME,
					    .flags = GENL_MCAST_CAP_NET_ADMIN,
					  },
};

static int mptcp_pm_family_to_addr(int family)
{
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (family == AF_INET6)
		return MPTCP_PM_ADDR_ATTR_ADDR6;
#endif
	return MPTCP_PM_ADDR_ATTR_ADDR4;
}

static int mptcp_pm_parse_pm_addr_attr(struct nlattr *tb[],
				       const struct nlattr *attr,
				       struct genl_info *info,
				       struct mptcp_addr_info *addr,
				       bool require_family)
{
	int err, addr_addr;

	if (!attr) {
		GENL_SET_ERR_MSG(info, "missing address info");
		return -EINVAL;
	}

	/* no validation needed - was already done via nested policy */
	err = nla_parse_nested_deprecated(tb, MPTCP_PM_ADDR_ATTR_MAX, attr,
					  mptcp_pm_address_nl_policy, info->extack);
	if (err)
		return err;

	if (tb[MPTCP_PM_ADDR_ATTR_ID])
		addr->id = nla_get_u8(tb[MPTCP_PM_ADDR_ATTR_ID]);

	if (!tb[MPTCP_PM_ADDR_ATTR_FAMILY]) {
		if (!require_family)
			return 0;

		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "missing family");
		return -EINVAL;
	}

	addr->family = nla_get_u16(tb[MPTCP_PM_ADDR_ATTR_FAMILY]);
	if (addr->family != AF_INET
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	    && addr->family != AF_INET6
#endif
	    ) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "unknown address family");
		return -EINVAL;
	}
	addr_addr = mptcp_pm_family_to_addr(addr->family);
	if (!tb[addr_addr]) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "missing address data");
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (addr->family == AF_INET6)
		addr->addr6 = nla_get_in6_addr(tb[addr_addr]);
	else
#endif
		addr->addr.s_addr = nla_get_in_addr(tb[addr_addr]);

	if (tb[MPTCP_PM_ADDR_ATTR_PORT])
		addr->port = htons(nla_get_u16(tb[MPTCP_PM_ADDR_ATTR_PORT]));

	return 0;
}

int mptcp_pm_parse_addr(struct nlattr *attr, struct genl_info *info,
			struct mptcp_addr_info *addr)
{
	struct nlattr *tb[MPTCP_PM_ADDR_ATTR_MAX + 1];

	memset(addr, 0, sizeof(*addr));

	return mptcp_pm_parse_pm_addr_attr(tb, attr, info, addr, true);
}

int mptcp_pm_parse_entry(struct nlattr *attr, struct genl_info *info,
			 bool require_family,
			 struct mptcp_pm_addr_entry *entry)
{
	struct nlattr *tb[MPTCP_PM_ADDR_ATTR_MAX + 1];
	int err;

	memset(entry, 0, sizeof(*entry));

	err = mptcp_pm_parse_pm_addr_attr(tb, attr, info, &entry->addr, require_family);
	if (err)
		return err;

	if (tb[MPTCP_PM_ADDR_ATTR_IF_IDX]) {
		u32 val = nla_get_s32(tb[MPTCP_PM_ADDR_ATTR_IF_IDX]);

		entry->ifindex = val;
	}

	if (tb[MPTCP_PM_ADDR_ATTR_FLAGS])
		entry->flags = nla_get_u32(tb[MPTCP_PM_ADDR_ATTR_FLAGS]);

	if (tb[MPTCP_PM_ADDR_ATTR_PORT])
		entry->addr.port = htons(nla_get_u16(tb[MPTCP_PM_ADDR_ATTR_PORT]));

	return 0;
}

static int mptcp_nl_fill_addr(struct sk_buff *skb,
			      struct mptcp_pm_addr_entry *entry)
{
	struct mptcp_addr_info *addr = &entry->addr;
	struct nlattr *attr;

	attr = nla_nest_start(skb, MPTCP_PM_ATTR_ADDR);
	if (!attr)
		return -EMSGSIZE;

	if (nla_put_u16(skb, MPTCP_PM_ADDR_ATTR_FAMILY, addr->family))
		goto nla_put_failure;
	if (nla_put_u16(skb, MPTCP_PM_ADDR_ATTR_PORT, ntohs(addr->port)))
		goto nla_put_failure;
	if (nla_put_u8(skb, MPTCP_PM_ADDR_ATTR_ID, addr->id))
		goto nla_put_failure;
	if (nla_put_u32(skb, MPTCP_PM_ADDR_ATTR_FLAGS, entry->flags))
		goto nla_put_failure;
	if (entry->ifindex &&
	    nla_put_s32(skb, MPTCP_PM_ADDR_ATTR_IF_IDX, entry->ifindex))
		goto nla_put_failure;

	if (addr->family == AF_INET &&
	    nla_put_in_addr(skb, MPTCP_PM_ADDR_ATTR_ADDR4,
			    addr->addr.s_addr))
		goto nla_put_failure;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else if (addr->family == AF_INET6 &&
		 nla_put_in6_addr(skb, MPTCP_PM_ADDR_ATTR_ADDR6, &addr->addr6))
		goto nla_put_failure;
#endif
	nla_nest_end(skb, attr);
	return 0;

nla_put_failure:
	nla_nest_cancel(skb, attr);
	return -EMSGSIZE;
}

static int mptcp_pm_get_addr(u8 id, struct mptcp_pm_addr_entry *addr,
			     struct genl_info *info)
{
	if (info->attrs[MPTCP_PM_ATTR_TOKEN])
		return mptcp_userspace_pm_get_addr(id, addr, info);
	return mptcp_pm_nl_get_addr(id, addr, info);
}

int mptcp_pm_nl_get_addr_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct mptcp_pm_addr_entry addr;
	struct nlattr *attr;
	struct sk_buff *msg;
	void *reply;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ENDPOINT_ADDR))
		return -EINVAL;

	attr = info->attrs[MPTCP_PM_ENDPOINT_ADDR];
	ret = mptcp_pm_parse_entry(attr, info, false, &addr);
	if (ret < 0)
		return ret;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	reply = genlmsg_put_reply(msg, info, &mptcp_genl_family, 0,
				  info->genlhdr->cmd);
	if (!reply) {
		GENL_SET_ERR_MSG(info, "not enough space in Netlink message");
		ret = -EMSGSIZE;
		goto fail;
	}

	ret = mptcp_pm_get_addr(addr.addr.id, &addr, info);
	if (ret) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr, "address not found");
		goto fail;
	}

	ret = mptcp_nl_fill_addr(msg, &addr);
	if (ret)
		goto fail;

	genlmsg_end(msg, reply);
	ret = genlmsg_reply(msg, info);
	return ret;

fail:
	nlmsg_free(msg);
	return ret;
}

int mptcp_pm_genl_fill_addr(struct sk_buff *msg,
			    struct netlink_callback *cb,
			    struct mptcp_pm_addr_entry *entry)
{
	void *hdr;

	hdr = genlmsg_put(msg, NETLINK_CB(cb->skb).portid,
			  cb->nlh->nlmsg_seq, &mptcp_genl_family,
			  NLM_F_MULTI, MPTCP_PM_CMD_GET_ADDR);
	if (!hdr)
		return -EINVAL;

	if (mptcp_nl_fill_addr(msg, entry) < 0) {
		genlmsg_cancel(msg, hdr);
		return -EINVAL;
	}

	genlmsg_end(msg, hdr);
	return 0;
}

static int mptcp_pm_dump_addr(struct sk_buff *msg, struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);

	if (info->attrs[MPTCP_PM_ATTR_TOKEN])
		return mptcp_userspace_pm_dump_addr(msg, cb);
	return mptcp_pm_nl_dump_addr(msg, cb);
}

int mptcp_pm_nl_get_addr_dumpit(struct sk_buff *msg,
				struct netlink_callback *cb)
{
	return mptcp_pm_dump_addr(msg, cb);
}

static int mptcp_pm_set_flags(struct genl_info *info)
{
	struct mptcp_pm_addr_entry loc = { .addr = { .family = AF_UNSPEC }, };
	struct nlattr *attr_loc;
	int ret = -EINVAL;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ATTR_ADDR))
		return ret;

	attr_loc = info->attrs[MPTCP_PM_ATTR_ADDR];
	ret = mptcp_pm_parse_entry(attr_loc, info, false, &loc);
	if (ret < 0)
		return ret;

	if (info->attrs[MPTCP_PM_ATTR_TOKEN])
		return mptcp_userspace_pm_set_flags(&loc, info);
	return mptcp_pm_nl_set_flags(&loc, info);
}

int mptcp_pm_nl_set_flags_doit(struct sk_buff *skb, struct genl_info *info)
{
	return mptcp_pm_set_flags(info);
}

static void mptcp_nl_mcast_send(struct net *net, struct sk_buff *nlskb, gfp_t gfp)
{
	genlmsg_multicast_netns(&mptcp_genl_family, net,
				nlskb, 0, MPTCP_PM_EV_GRP_OFFSET, gfp);
}

bool mptcp_userspace_pm_active(const struct mptcp_sock *msk)
{
	return genl_has_listeners(&mptcp_genl_family,
				  sock_net((const struct sock *)msk),
				  MPTCP_PM_EV_GRP_OFFSET);
}

static int mptcp_event_add_subflow(struct sk_buff *skb, const struct sock *ssk)
{
	const struct inet_sock *issk = inet_sk(ssk);
	const struct mptcp_subflow_context *sf;

	if (nla_put_u16(skb, MPTCP_ATTR_FAMILY, ssk->sk_family))
		return -EMSGSIZE;

	switch (ssk->sk_family) {
	case AF_INET:
		if (nla_put_in_addr(skb, MPTCP_ATTR_SADDR4, issk->inet_saddr))
			return -EMSGSIZE;
		if (nla_put_in_addr(skb, MPTCP_ATTR_DADDR4, issk->inet_daddr))
			return -EMSGSIZE;
		break;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	case AF_INET6: {
		if (nla_put_in6_addr(skb, MPTCP_ATTR_SADDR6, &issk->pinet6->saddr))
			return -EMSGSIZE;
		if (nla_put_in6_addr(skb, MPTCP_ATTR_DADDR6, &ssk->sk_v6_daddr))
			return -EMSGSIZE;
		break;
	}
#endif
	default:
		WARN_ON_ONCE(1);
		return -EMSGSIZE;
	}

	if (nla_put_be16(skb, MPTCP_ATTR_SPORT, issk->inet_sport))
		return -EMSGSIZE;
	if (nla_put_be16(skb, MPTCP_ATTR_DPORT, issk->inet_dport))
		return -EMSGSIZE;

	sf = mptcp_subflow_ctx(ssk);
	if (WARN_ON_ONCE(!sf))
		return -EINVAL;

	if (nla_put_u8(skb, MPTCP_ATTR_LOC_ID, subflow_get_local_id(sf)))
		return -EMSGSIZE;

	if (nla_put_u8(skb, MPTCP_ATTR_REM_ID, sf->remote_id))
		return -EMSGSIZE;

	return 0;
}

static int mptcp_event_put_token_and_ssk(struct sk_buff *skb,
					 const struct mptcp_sock *msk,
					 const struct sock *ssk)
{
	const struct sock *sk = (const struct sock *)msk;
	const struct mptcp_subflow_context *sf;
	u8 sk_err;

	if (nla_put_u32(skb, MPTCP_ATTR_TOKEN, READ_ONCE(msk->token)))
		return -EMSGSIZE;

	if (mptcp_event_add_subflow(skb, ssk))
		return -EMSGSIZE;

	sf = mptcp_subflow_ctx(ssk);
	if (WARN_ON_ONCE(!sf))
		return -EINVAL;

	if (nla_put_u8(skb, MPTCP_ATTR_BACKUP, sf->backup))
		return -EMSGSIZE;

	if (ssk->sk_bound_dev_if &&
	    nla_put_s32(skb, MPTCP_ATTR_IF_IDX, ssk->sk_bound_dev_if))
		return -EMSGSIZE;

	sk_err = READ_ONCE(ssk->sk_err);
	if (sk_err && sk->sk_state == TCP_ESTABLISHED &&
	    nla_put_u8(skb, MPTCP_ATTR_ERROR, sk_err))
		return -EMSGSIZE;

	return 0;
}

static int mptcp_event_sub_established(struct sk_buff *skb,
				       const struct mptcp_sock *msk,
				       const struct sock *ssk)
{
	return mptcp_event_put_token_and_ssk(skb, msk, ssk);
}

static int mptcp_event_sub_closed(struct sk_buff *skb,
				  const struct mptcp_sock *msk,
				  const struct sock *ssk)
{
	const struct mptcp_subflow_context *sf;

	if (mptcp_event_put_token_and_ssk(skb, msk, ssk))
		return -EMSGSIZE;

	sf = mptcp_subflow_ctx(ssk);
	if (!sf->reset_seen)
		return 0;

	if (nla_put_u32(skb, MPTCP_ATTR_RESET_REASON, sf->reset_reason))
		return -EMSGSIZE;

	if (nla_put_u32(skb, MPTCP_ATTR_RESET_FLAGS, sf->reset_transient))
		return -EMSGSIZE;

	return 0;
}

static int mptcp_event_created(struct sk_buff *skb,
			       const struct mptcp_sock *msk,
			       const struct sock *ssk)
{
	int err = nla_put_u32(skb, MPTCP_ATTR_TOKEN, READ_ONCE(msk->token));

	if (err)
		return err;

	if (nla_put_u8(skb, MPTCP_ATTR_SERVER_SIDE, READ_ONCE(msk->pm.server_side)))
		return -EMSGSIZE;

	return mptcp_event_add_subflow(skb, ssk);
}

void mptcp_event_addr_removed(const struct mptcp_sock *msk, uint8_t id)
{
	struct net *net = sock_net((const struct sock *)msk);
	struct nlmsghdr *nlh;
	struct sk_buff *skb;

	if (!genl_has_listeners(&mptcp_genl_family, net, MPTCP_PM_EV_GRP_OFFSET))
		return;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return;

	nlh = genlmsg_put(skb, 0, 0, &mptcp_genl_family, 0, MPTCP_EVENT_REMOVED);
	if (!nlh)
		goto nla_put_failure;

	if (nla_put_u32(skb, MPTCP_ATTR_TOKEN, READ_ONCE(msk->token)))
		goto nla_put_failure;

	if (nla_put_u8(skb, MPTCP_ATTR_REM_ID, id))
		goto nla_put_failure;

	genlmsg_end(skb, nlh);
	mptcp_nl_mcast_send(net, skb, GFP_ATOMIC);
	return;

nla_put_failure:
	nlmsg_free(skb);
}

void mptcp_event_addr_announced(const struct sock *ssk,
				const struct mptcp_addr_info *info)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	struct net *net = sock_net(ssk);
	struct nlmsghdr *nlh;
	struct sk_buff *skb;

	if (!genl_has_listeners(&mptcp_genl_family, net, MPTCP_PM_EV_GRP_OFFSET))
		return;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return;

	nlh = genlmsg_put(skb, 0, 0, &mptcp_genl_family, 0,
			  MPTCP_EVENT_ANNOUNCED);
	if (!nlh)
		goto nla_put_failure;

	if (nla_put_u32(skb, MPTCP_ATTR_TOKEN, READ_ONCE(msk->token)))
		goto nla_put_failure;

	if (nla_put_u8(skb, MPTCP_ATTR_REM_ID, info->id))
		goto nla_put_failure;

	if (nla_put_be16(skb, MPTCP_ATTR_DPORT,
			 info->port == 0 ?
			 inet_sk(ssk)->inet_dport :
			 info->port))
		goto nla_put_failure;

	switch (info->family) {
	case AF_INET:
		if (nla_put_in_addr(skb, MPTCP_ATTR_DADDR4, info->addr.s_addr))
			goto nla_put_failure;
		break;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	case AF_INET6:
		if (nla_put_in6_addr(skb, MPTCP_ATTR_DADDR6, &info->addr6))
			goto nla_put_failure;
		break;
#endif
	default:
		WARN_ON_ONCE(1);
		goto nla_put_failure;
	}

	genlmsg_end(skb, nlh);
	mptcp_nl_mcast_send(net, skb, GFP_ATOMIC);
	return;

nla_put_failure:
	nlmsg_free(skb);
}

void mptcp_event_pm_listener(const struct sock *ssk,
			     enum mptcp_event_type event)
{
	const struct inet_sock *issk = inet_sk(ssk);
	struct net *net = sock_net(ssk);
	struct nlmsghdr *nlh;
	struct sk_buff *skb;

	if (!genl_has_listeners(&mptcp_genl_family, net, MPTCP_PM_EV_GRP_OFFSET))
		return;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return;

	nlh = genlmsg_put(skb, 0, 0, &mptcp_genl_family, 0, event);
	if (!nlh)
		goto nla_put_failure;

	if (nla_put_u16(skb, MPTCP_ATTR_FAMILY, ssk->sk_family))
		goto nla_put_failure;

	if (nla_put_be16(skb, MPTCP_ATTR_SPORT, issk->inet_sport))
		goto nla_put_failure;

	switch (ssk->sk_family) {
	case AF_INET:
		if (nla_put_in_addr(skb, MPTCP_ATTR_SADDR4, issk->inet_saddr))
			goto nla_put_failure;
		break;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	case AF_INET6: {
		if (nla_put_in6_addr(skb, MPTCP_ATTR_SADDR6, &issk->pinet6->saddr))
			goto nla_put_failure;
		break;
	}
#endif
	default:
		WARN_ON_ONCE(1);
		goto nla_put_failure;
	}

	genlmsg_end(skb, nlh);
	mptcp_nl_mcast_send(net, skb, GFP_KERNEL);
	return;

nla_put_failure:
	nlmsg_free(skb);
}

void mptcp_event(enum mptcp_event_type type, const struct mptcp_sock *msk,
		 const struct sock *ssk, gfp_t gfp)
{
	struct net *net = sock_net((const struct sock *)msk);
	struct nlmsghdr *nlh;
	struct sk_buff *skb;

	if (!genl_has_listeners(&mptcp_genl_family, net, MPTCP_PM_EV_GRP_OFFSET))
		return;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, gfp);
	if (!skb)
		return;

	nlh = genlmsg_put(skb, 0, 0, &mptcp_genl_family, 0, type);
	if (!nlh)
		goto nla_put_failure;

	switch (type) {
	case MPTCP_EVENT_UNSPEC:
		WARN_ON_ONCE(1);
		break;
	case MPTCP_EVENT_CREATED:
	case MPTCP_EVENT_ESTABLISHED:
		if (mptcp_event_created(skb, msk, ssk) < 0)
			goto nla_put_failure;
		break;
	case MPTCP_EVENT_CLOSED:
		if (nla_put_u32(skb, MPTCP_ATTR_TOKEN, READ_ONCE(msk->token)) < 0)
			goto nla_put_failure;
		break;
	case MPTCP_EVENT_ANNOUNCED:
	case MPTCP_EVENT_REMOVED:
		/* call mptcp_event_addr_announced()/removed instead */
		WARN_ON_ONCE(1);
		break;
	case MPTCP_EVENT_SUB_ESTABLISHED:
	case MPTCP_EVENT_SUB_PRIORITY:
		if (mptcp_event_sub_established(skb, msk, ssk) < 0)
			goto nla_put_failure;
		break;
	case MPTCP_EVENT_SUB_CLOSED:
		if (mptcp_event_sub_closed(skb, msk, ssk) < 0)
			goto nla_put_failure;
		break;
	case MPTCP_EVENT_LISTENER_CREATED:
	case MPTCP_EVENT_LISTENER_CLOSED:
		break;
	}

	genlmsg_end(skb, nlh);
	mptcp_nl_mcast_send(net, skb, gfp);
	return;

nla_put_failure:
	nlmsg_free(skb);
}

struct genl_family mptcp_genl_family __ro_after_init = {
	.name		= MPTCP_PM_NAME,
	.version	= MPTCP_PM_VER,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.ops		= mptcp_pm_nl_ops,
	.n_ops		= ARRAY_SIZE(mptcp_pm_nl_ops),
	.resv_start_op	= MPTCP_PM_CMD_SUBFLOW_DESTROY + 1,
	.mcgrps		= mptcp_pm_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(mptcp_pm_mcgrps),
};

void __init mptcp_pm_nl_init(void)
{
	if (genl_register_family(&mptcp_genl_family))
		panic("Failed to register MPTCP PM netlink family\n");
}
