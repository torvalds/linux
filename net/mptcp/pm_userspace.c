// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2022, Intel Corporation.
 */

#include "protocol.h"
#include "mib.h"

void mptcp_free_local_addr_list(struct mptcp_sock *msk)
{
	struct mptcp_pm_addr_entry *entry, *tmp;
	struct sock *sk = (struct sock *)msk;
	LIST_HEAD(free_list);

	if (!mptcp_pm_is_userspace(msk))
		return;

	spin_lock_bh(&msk->pm.lock);
	list_splice_init(&msk->pm.userspace_pm_local_addr_list, &free_list);
	spin_unlock_bh(&msk->pm.lock);

	list_for_each_entry_safe(entry, tmp, &free_list, list) {
		sock_kfree_s(sk, entry, sizeof(*entry));
	}
}

int mptcp_userspace_pm_append_new_local_addr(struct mptcp_sock *msk,
					     struct mptcp_pm_addr_entry *entry)
{
	DECLARE_BITMAP(id_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);
	struct mptcp_pm_addr_entry *match = NULL;
	struct sock *sk = (struct sock *)msk;
	struct mptcp_pm_addr_entry *e;
	bool addr_match = false;
	bool id_match = false;
	int ret = -EINVAL;

	bitmap_zero(id_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);

	spin_lock_bh(&msk->pm.lock);
	list_for_each_entry(e, &msk->pm.userspace_pm_local_addr_list, list) {
		addr_match = mptcp_addresses_equal(&e->addr, &entry->addr, true);
		if (addr_match && entry->addr.id == 0)
			entry->addr.id = e->addr.id;
		id_match = (e->addr.id == entry->addr.id);
		if (addr_match && id_match) {
			match = e;
			break;
		} else if (addr_match || id_match) {
			break;
		}
		__set_bit(e->addr.id, id_bitmap);
	}

	if (!match && !addr_match && !id_match) {
		/* Memory for the entry is allocated from the
		 * sock option buffer.
		 */
		e = sock_kmalloc(sk, sizeof(*e), GFP_ATOMIC);
		if (!e) {
			spin_unlock_bh(&msk->pm.lock);
			return -ENOMEM;
		}

		*e = *entry;
		if (!e->addr.id)
			e->addr.id = find_next_zero_bit(id_bitmap,
							MPTCP_PM_MAX_ADDR_ID + 1,
							1);
		list_add_tail_rcu(&e->list, &msk->pm.userspace_pm_local_addr_list);
		ret = e->addr.id;
	} else if (match) {
		ret = entry->addr.id;
	}

	spin_unlock_bh(&msk->pm.lock);
	return ret;
}

int mptcp_userspace_pm_get_flags_and_ifindex_by_id(struct mptcp_sock *msk,
						   unsigned int id,
						   u8 *flags, int *ifindex)
{
	struct mptcp_pm_addr_entry *entry, *match = NULL;

	*flags = 0;
	*ifindex = 0;

	spin_lock_bh(&msk->pm.lock);
	list_for_each_entry(entry, &msk->pm.userspace_pm_local_addr_list, list) {
		if (id == entry->addr.id) {
			match = entry;
			break;
		}
	}
	spin_unlock_bh(&msk->pm.lock);
	if (match) {
		*flags = match->flags;
		*ifindex = match->ifindex;
	}

	return 0;
}

int mptcp_userspace_pm_get_local_id(struct mptcp_sock *msk,
				    struct mptcp_addr_info *skc)
{
	struct mptcp_pm_addr_entry new_entry;
	__be16 msk_sport =  ((struct inet_sock *)
			     inet_sk((struct sock *)msk))->inet_sport;

	memset(&new_entry, 0, sizeof(struct mptcp_pm_addr_entry));
	new_entry.addr = *skc;
	new_entry.addr.id = 0;
	new_entry.flags = MPTCP_PM_ADDR_FLAG_IMPLICIT;

	if (new_entry.addr.port == msk_sport)
		new_entry.addr.port = 0;

	return mptcp_userspace_pm_append_new_local_addr(msk, &new_entry);
}

int mptcp_nl_cmd_announce(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *token = info->attrs[MPTCP_PM_ATTR_TOKEN];
	struct nlattr *addr = info->attrs[MPTCP_PM_ATTR_ADDR];
	struct mptcp_pm_addr_entry addr_val;
	struct mptcp_sock *msk;
	int err = -EINVAL;
	u32 token_val;

	if (!addr || !token) {
		GENL_SET_ERR_MSG(info, "missing required inputs");
		return err;
	}

	token_val = nla_get_u32(token);

	msk = mptcp_token_get_sock(sock_net(skb->sk), token_val);
	if (!msk) {
		NL_SET_ERR_MSG_ATTR(info->extack, token, "invalid token");
		return err;
	}

	if (!mptcp_pm_is_userspace(msk)) {
		GENL_SET_ERR_MSG(info, "invalid request; userspace PM not selected");
		goto announce_err;
	}

	err = mptcp_pm_parse_entry(addr, info, true, &addr_val);
	if (err < 0) {
		GENL_SET_ERR_MSG(info, "error parsing local address");
		goto announce_err;
	}

	if (addr_val.addr.id == 0 || !(addr_val.flags & MPTCP_PM_ADDR_FLAG_SIGNAL)) {
		GENL_SET_ERR_MSG(info, "invalid addr id or flags");
		err = -EINVAL;
		goto announce_err;
	}

	err = mptcp_userspace_pm_append_new_local_addr(msk, &addr_val);
	if (err < 0) {
		GENL_SET_ERR_MSG(info, "did not match address and id");
		goto announce_err;
	}

	lock_sock((struct sock *)msk);
	spin_lock_bh(&msk->pm.lock);

	if (mptcp_pm_alloc_anno_list(msk, &addr_val)) {
		mptcp_pm_announce_addr(msk, &addr_val.addr, false);
		mptcp_pm_nl_addr_send_ack(msk);
	}

	spin_unlock_bh(&msk->pm.lock);
	release_sock((struct sock *)msk);

	err = 0;
 announce_err:
	sock_put((struct sock *)msk);
	return err;
}

int mptcp_nl_cmd_remove(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *token = info->attrs[MPTCP_PM_ATTR_TOKEN];
	struct nlattr *id = info->attrs[MPTCP_PM_ATTR_LOC_ID];
	struct mptcp_pm_addr_entry *match = NULL;
	struct mptcp_pm_addr_entry *entry;
	struct mptcp_sock *msk;
	LIST_HEAD(free_list);
	int err = -EINVAL;
	u32 token_val;
	u8 id_val;

	if (!id || !token) {
		GENL_SET_ERR_MSG(info, "missing required inputs");
		return err;
	}

	id_val = nla_get_u8(id);
	token_val = nla_get_u32(token);

	msk = mptcp_token_get_sock(sock_net(skb->sk), token_val);
	if (!msk) {
		NL_SET_ERR_MSG_ATTR(info->extack, token, "invalid token");
		return err;
	}

	if (!mptcp_pm_is_userspace(msk)) {
		GENL_SET_ERR_MSG(info, "invalid request; userspace PM not selected");
		goto remove_err;
	}

	lock_sock((struct sock *)msk);

	list_for_each_entry(entry, &msk->pm.userspace_pm_local_addr_list, list) {
		if (entry->addr.id == id_val) {
			match = entry;
			break;
		}
	}

	if (!match) {
		GENL_SET_ERR_MSG(info, "address with specified id not found");
		release_sock((struct sock *)msk);
		goto remove_err;
	}

	list_move(&match->list, &free_list);

	mptcp_pm_remove_addrs_and_subflows(msk, &free_list);

	release_sock((struct sock *)msk);

	list_for_each_entry_safe(match, entry, &free_list, list) {
		sock_kfree_s((struct sock *)msk, match, sizeof(*match));
	}

	err = 0;
 remove_err:
	sock_put((struct sock *)msk);
	return err;
}

int mptcp_nl_cmd_sf_create(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *raddr = info->attrs[MPTCP_PM_ATTR_ADDR_REMOTE];
	struct nlattr *token = info->attrs[MPTCP_PM_ATTR_TOKEN];
	struct nlattr *laddr = info->attrs[MPTCP_PM_ATTR_ADDR];
	struct mptcp_addr_info addr_r;
	struct mptcp_addr_info addr_l;
	struct mptcp_sock *msk;
	int err = -EINVAL;
	struct sock *sk;
	u32 token_val;

	if (!laddr || !raddr || !token) {
		GENL_SET_ERR_MSG(info, "missing required inputs");
		return err;
	}

	token_val = nla_get_u32(token);

	msk = mptcp_token_get_sock(genl_info_net(info), token_val);
	if (!msk) {
		NL_SET_ERR_MSG_ATTR(info->extack, token, "invalid token");
		return err;
	}

	if (!mptcp_pm_is_userspace(msk)) {
		GENL_SET_ERR_MSG(info, "invalid request; userspace PM not selected");
		goto create_err;
	}

	err = mptcp_pm_parse_addr(laddr, info, &addr_l);
	if (err < 0) {
		NL_SET_ERR_MSG_ATTR(info->extack, laddr, "error parsing local addr");
		goto create_err;
	}

	if (addr_l.id == 0) {
		NL_SET_ERR_MSG_ATTR(info->extack, laddr, "missing local addr id");
		err = -EINVAL;
		goto create_err;
	}

	err = mptcp_pm_parse_addr(raddr, info, &addr_r);
	if (err < 0) {
		NL_SET_ERR_MSG_ATTR(info->extack, raddr, "error parsing remote addr");
		goto create_err;
	}

	sk = (struct sock *)msk;

	if (!mptcp_pm_addr_families_match(sk, &addr_l, &addr_r)) {
		GENL_SET_ERR_MSG(info, "families mismatch");
		err = -EINVAL;
		goto create_err;
	}

	lock_sock(sk);

	err = __mptcp_subflow_connect(sk, &addr_l, &addr_r);

	release_sock(sk);

 create_err:
	sock_put((struct sock *)msk);
	return err;
}

static struct sock *mptcp_nl_find_ssk(struct mptcp_sock *msk,
				      const struct mptcp_addr_info *local,
				      const struct mptcp_addr_info *remote)
{
	struct mptcp_subflow_context *subflow;

	if (local->family != remote->family)
		return NULL;

	mptcp_for_each_subflow(msk, subflow) {
		const struct inet_sock *issk;
		struct sock *ssk;

		ssk = mptcp_subflow_tcp_sock(subflow);

		if (local->family != ssk->sk_family)
			continue;

		issk = inet_sk(ssk);

		switch (ssk->sk_family) {
		case AF_INET:
			if (issk->inet_saddr != local->addr.s_addr ||
			    issk->inet_daddr != remote->addr.s_addr)
				continue;
			break;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
		case AF_INET6: {
			const struct ipv6_pinfo *pinfo = inet6_sk(ssk);

			if (!ipv6_addr_equal(&local->addr6, &pinfo->saddr) ||
			    !ipv6_addr_equal(&remote->addr6, &ssk->sk_v6_daddr))
				continue;
			break;
		}
#endif
		default:
			continue;
		}

		if (issk->inet_sport == local->port &&
		    issk->inet_dport == remote->port)
			return ssk;
	}

	return NULL;
}

int mptcp_nl_cmd_sf_destroy(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *raddr = info->attrs[MPTCP_PM_ATTR_ADDR_REMOTE];
	struct nlattr *token = info->attrs[MPTCP_PM_ATTR_TOKEN];
	struct nlattr *laddr = info->attrs[MPTCP_PM_ATTR_ADDR];
	struct mptcp_addr_info addr_l;
	struct mptcp_addr_info addr_r;
	struct mptcp_sock *msk;
	struct sock *sk, *ssk;
	int err = -EINVAL;
	u32 token_val;

	if (!laddr || !raddr || !token) {
		GENL_SET_ERR_MSG(info, "missing required inputs");
		return err;
	}

	token_val = nla_get_u32(token);

	msk = mptcp_token_get_sock(genl_info_net(info), token_val);
	if (!msk) {
		NL_SET_ERR_MSG_ATTR(info->extack, token, "invalid token");
		return err;
	}

	if (!mptcp_pm_is_userspace(msk)) {
		GENL_SET_ERR_MSG(info, "invalid request; userspace PM not selected");
		goto destroy_err;
	}

	err = mptcp_pm_parse_addr(laddr, info, &addr_l);
	if (err < 0) {
		NL_SET_ERR_MSG_ATTR(info->extack, laddr, "error parsing local addr");
		goto destroy_err;
	}

	err = mptcp_pm_parse_addr(raddr, info, &addr_r);
	if (err < 0) {
		NL_SET_ERR_MSG_ATTR(info->extack, raddr, "error parsing remote addr");
		goto destroy_err;
	}

	if (addr_l.family != addr_r.family) {
		GENL_SET_ERR_MSG(info, "address families do not match");
		err = -EINVAL;
		goto destroy_err;
	}

	if (!addr_l.port || !addr_r.port) {
		GENL_SET_ERR_MSG(info, "missing local or remote port");
		err = -EINVAL;
		goto destroy_err;
	}

	sk = (struct sock *)msk;
	lock_sock(sk);
	ssk = mptcp_nl_find_ssk(msk, &addr_l, &addr_r);
	if (ssk) {
		struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);

		mptcp_subflow_shutdown(sk, ssk, RCV_SHUTDOWN | SEND_SHUTDOWN);
		mptcp_close_ssk(sk, ssk, subflow);
		MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_RMSUBFLOW);
		err = 0;
	} else {
		err = -ESRCH;
	}
	release_sock(sk);

destroy_err:
	sock_put((struct sock *)msk);
	return err;
}

int mptcp_userspace_pm_set_flags(struct net *net, struct nlattr *token,
				 struct mptcp_pm_addr_entry *loc,
				 struct mptcp_pm_addr_entry *rem, u8 bkup)
{
	struct mptcp_sock *msk;
	int ret = -EINVAL;
	u32 token_val;

	token_val = nla_get_u32(token);

	msk = mptcp_token_get_sock(net, token_val);
	if (!msk)
		return ret;

	if (!mptcp_pm_is_userspace(msk))
		goto set_flags_err;

	if (loc->addr.family == AF_UNSPEC ||
	    rem->addr.family == AF_UNSPEC)
		goto set_flags_err;

	lock_sock((struct sock *)msk);
	ret = mptcp_pm_nl_mp_prio_send_ack(msk, &loc->addr, &rem->addr, bkup);
	release_sock((struct sock *)msk);

set_flags_err:
	sock_put((struct sock *)msk);
	return ret;
}
