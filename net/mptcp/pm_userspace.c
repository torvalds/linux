// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2022, Intel Corporation.
 */

#include "protocol.h"

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
