// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2022, Intel Corporation.
 */

#include "protocol.h"
#include "mib.h"
#include "mptcp_pm_gen.h"

#define mptcp_for_each_userspace_pm_addr(__msk, __entry)			\
	list_for_each_entry(__entry,						\
			    &((__msk)->pm.userspace_pm_local_addr_list), list)

void mptcp_userspace_pm_free_local_addr_list(struct mptcp_sock *msk)
{
	struct mptcp_pm_addr_entry *entry, *tmp;
	struct sock *sk = (struct sock *)msk;
	LIST_HEAD(free_list);

	spin_lock_bh(&msk->pm.lock);
	list_splice_init(&msk->pm.userspace_pm_local_addr_list, &free_list);
	spin_unlock_bh(&msk->pm.lock);

	list_for_each_entry_safe(entry, tmp, &free_list, list) {
		sock_kfree_s(sk, entry, sizeof(*entry));
	}
}

static struct mptcp_pm_addr_entry *
mptcp_userspace_pm_lookup_addr(struct mptcp_sock *msk,
			       const struct mptcp_addr_info *addr)
{
	struct mptcp_pm_addr_entry *entry;

	mptcp_for_each_userspace_pm_addr(msk, entry) {
		if (mptcp_addresses_equal(&entry->addr, addr, false))
			return entry;
	}
	return NULL;
}

static int mptcp_userspace_pm_append_new_local_addr(struct mptcp_sock *msk,
						    struct mptcp_pm_addr_entry *entry,
						    bool needs_id)
{
	DECLARE_BITMAP(id_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);
	struct sock *sk = (struct sock *)msk;
	struct mptcp_pm_addr_entry *e;
	bool addr_match = false;
	bool id_match = false;
	int ret = -EINVAL;

	bitmap_zero(id_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);

	spin_lock_bh(&msk->pm.lock);
	mptcp_for_each_userspace_pm_addr(msk, e) {
		addr_match = mptcp_addresses_equal(&e->addr, &entry->addr, true);
		if (addr_match && entry->addr.id == 0 && needs_id)
			entry->addr.id = e->addr.id;
		id_match = (e->addr.id == entry->addr.id);
		if (addr_match || id_match)
			break;
		__set_bit(e->addr.id, id_bitmap);
	}

	if (!addr_match && !id_match) {
		/* Memory for the entry is allocated from the
		 * sock option buffer.
		 */
		e = sock_kmemdup(sk, entry, sizeof(*entry), GFP_ATOMIC);
		if (!e) {
			ret = -ENOMEM;
			goto append_err;
		}

		if (!e->addr.id && needs_id)
			e->addr.id = find_next_zero_bit(id_bitmap,
							MPTCP_PM_MAX_ADDR_ID + 1,
							1);
		list_add_tail_rcu(&e->list, &msk->pm.userspace_pm_local_addr_list);
		msk->pm.local_addr_used++;
		ret = e->addr.id;
	} else if (addr_match && id_match) {
		ret = entry->addr.id;
	}

append_err:
	spin_unlock_bh(&msk->pm.lock);
	return ret;
}

/* If the subflow is closed from the other peer (not via a
 * subflow destroy command then), we want to keep the entry
 * not to assign the same ID to another address and to be
 * able to send RM_ADDR after the removal of the subflow.
 */
static int mptcp_userspace_pm_delete_local_addr(struct mptcp_sock *msk,
						struct mptcp_pm_addr_entry *addr)
{
	struct sock *sk = (struct sock *)msk;
	struct mptcp_pm_addr_entry *entry;

	entry = mptcp_userspace_pm_lookup_addr(msk, &addr->addr);
	if (!entry)
		return -EINVAL;

	/* TODO: a refcount is needed because the entry can
	 * be used multiple times (e.g. fullmesh mode).
	 */
	list_del_rcu(&entry->list);
	sock_kfree_s(sk, entry, sizeof(*entry));
	msk->pm.local_addr_used--;
	return 0;
}

static struct mptcp_pm_addr_entry *
mptcp_userspace_pm_lookup_addr_by_id(struct mptcp_sock *msk, unsigned int id)
{
	struct mptcp_pm_addr_entry *entry;

	mptcp_for_each_userspace_pm_addr(msk, entry) {
		if (entry->addr.id == id)
			return entry;
	}
	return NULL;
}

int mptcp_userspace_pm_get_local_id(struct mptcp_sock *msk,
				    struct mptcp_pm_addr_entry *skc)
{
	__be16 msk_sport =  ((struct inet_sock *)
			     inet_sk((struct sock *)msk))->inet_sport;
	struct mptcp_pm_addr_entry *entry;

	spin_lock_bh(&msk->pm.lock);
	entry = mptcp_userspace_pm_lookup_addr(msk, &skc->addr);
	spin_unlock_bh(&msk->pm.lock);
	if (entry)
		return entry->addr.id;

	if (skc->addr.port == msk_sport)
		skc->addr.port = 0;

	return mptcp_userspace_pm_append_new_local_addr(msk, skc, true);
}

bool mptcp_userspace_pm_is_backup(struct mptcp_sock *msk,
				  struct mptcp_addr_info *skc)
{
	struct mptcp_pm_addr_entry *entry;
	bool backup;

	spin_lock_bh(&msk->pm.lock);
	entry = mptcp_userspace_pm_lookup_addr(msk, skc);
	backup = entry && !!(entry->flags & MPTCP_PM_ADDR_FLAG_BACKUP);
	spin_unlock_bh(&msk->pm.lock);

	return backup;
}

static struct mptcp_sock *mptcp_userspace_pm_get_sock(const struct genl_info *info)
{
	struct mptcp_sock *msk;
	struct nlattr *token;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ATTR_TOKEN))
		return NULL;

	token = info->attrs[MPTCP_PM_ATTR_TOKEN];
	msk = mptcp_token_get_sock(genl_info_net(info), nla_get_u32(token));
	if (!msk) {
		NL_SET_ERR_MSG_ATTR(info->extack, token, "invalid token");
		return NULL;
	}

	if (!mptcp_pm_is_userspace(msk)) {
		NL_SET_ERR_MSG_ATTR(info->extack, token,
				    "userspace PM not selected");
		sock_put((struct sock *)msk);
		return NULL;
	}

	return msk;
}

int mptcp_pm_nl_announce_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct mptcp_pm_addr_entry addr_val;
	struct mptcp_sock *msk;
	struct nlattr *addr;
	int err = -EINVAL;
	struct sock *sk;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ATTR_ADDR))
		return err;

	msk = mptcp_userspace_pm_get_sock(info);
	if (!msk)
		return err;

	sk = (struct sock *)msk;

	addr = info->attrs[MPTCP_PM_ATTR_ADDR];
	err = mptcp_pm_parse_entry(addr, info, true, &addr_val);
	if (err < 0)
		goto announce_err;

	if (addr_val.addr.id == 0) {
		NL_SET_ERR_MSG_ATTR(info->extack, addr, "invalid addr id");
		err = -EINVAL;
		goto announce_err;
	}

	if (!(addr_val.flags & MPTCP_PM_ADDR_FLAG_SIGNAL)) {
		NL_SET_ERR_MSG_ATTR(info->extack, addr, "invalid addr flags");
		err = -EINVAL;
		goto announce_err;
	}

	err = mptcp_userspace_pm_append_new_local_addr(msk, &addr_val, false);
	if (err < 0) {
		NL_SET_ERR_MSG_ATTR(info->extack, addr,
				    "did not match address and id");
		goto announce_err;
	}

	lock_sock(sk);
	spin_lock_bh(&msk->pm.lock);

	if (mptcp_pm_alloc_anno_list(msk, &addr_val.addr)) {
		msk->pm.add_addr_signaled++;
		mptcp_pm_announce_addr(msk, &addr_val.addr, false);
		mptcp_pm_addr_send_ack(msk);
	}

	spin_unlock_bh(&msk->pm.lock);
	release_sock(sk);

	err = 0;
 announce_err:
	sock_put(sk);
	return err;
}

static int mptcp_userspace_pm_remove_id_zero_address(struct mptcp_sock *msk)
{
	struct mptcp_rm_list list = { .nr = 0 };
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	bool has_id_0 = false;
	int err = -EINVAL;

	lock_sock(sk);
	mptcp_for_each_subflow(msk, subflow) {
		if (READ_ONCE(subflow->local_id) == 0) {
			has_id_0 = true;
			break;
		}
	}
	if (!has_id_0)
		goto remove_err;

	list.ids[list.nr++] = 0;

	spin_lock_bh(&msk->pm.lock);
	mptcp_pm_remove_addr(msk, &list);
	spin_unlock_bh(&msk->pm.lock);

	err = 0;

remove_err:
	release_sock(sk);
	return err;
}

void mptcp_pm_remove_addr_entry(struct mptcp_sock *msk,
				struct mptcp_pm_addr_entry *entry)
{
	struct mptcp_rm_list alist = { .nr = 0 };
	int anno_nr = 0;

	/* only delete if either announced or matching a subflow */
	if (mptcp_remove_anno_list_by_saddr(msk, &entry->addr))
		anno_nr++;
	else if (!mptcp_lookup_subflow_by_saddr(&msk->conn_list, &entry->addr))
		return;

	alist.ids[alist.nr++] = entry->addr.id;

	spin_lock_bh(&msk->pm.lock);
	msk->pm.add_addr_signaled -= anno_nr;
	mptcp_pm_remove_addr(msk, &alist);
	spin_unlock_bh(&msk->pm.lock);
}

int mptcp_pm_nl_remove_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct mptcp_pm_addr_entry *match;
	struct mptcp_sock *msk;
	struct nlattr *id;
	int err = -EINVAL;
	struct sock *sk;
	u8 id_val;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ATTR_LOC_ID))
		return err;

	id = info->attrs[MPTCP_PM_ATTR_LOC_ID];
	id_val = nla_get_u8(id);

	msk = mptcp_userspace_pm_get_sock(info);
	if (!msk)
		return err;

	sk = (struct sock *)msk;

	if (id_val == 0) {
		err = mptcp_userspace_pm_remove_id_zero_address(msk);
		goto out;
	}

	lock_sock(sk);

	spin_lock_bh(&msk->pm.lock);
	match = mptcp_userspace_pm_lookup_addr_by_id(msk, id_val);
	if (!match) {
		spin_unlock_bh(&msk->pm.lock);
		release_sock(sk);
		goto out;
	}

	list_del_rcu(&match->list);
	spin_unlock_bh(&msk->pm.lock);

	mptcp_pm_remove_addr_entry(msk, match);

	release_sock(sk);

	kfree_rcu_mightsleep(match);
	/* Adjust sk_omem_alloc like sock_kfree_s() does, to match
	 * with allocation of this memory by sock_kmemdup()
	 */
	atomic_sub(sizeof(*match), &sk->sk_omem_alloc);

	err = 0;
out:
	if (err)
		NL_SET_ERR_MSG_ATTR_FMT(info->extack, id,
					"address with id %u not found",
					id_val);

	sock_put(sk);
	return err;
}

int mptcp_pm_nl_subflow_create_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct mptcp_pm_addr_entry entry = { 0 };
	struct mptcp_addr_info addr_r;
	struct nlattr *raddr, *laddr;
	struct mptcp_pm_local local;
	struct mptcp_sock *msk;
	int err = -EINVAL;
	struct sock *sk;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ATTR_ADDR) ||
	    GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ATTR_ADDR_REMOTE))
		return err;

	msk = mptcp_userspace_pm_get_sock(info);
	if (!msk)
		return err;

	sk = (struct sock *)msk;

	laddr = info->attrs[MPTCP_PM_ATTR_ADDR];
	err = mptcp_pm_parse_entry(laddr, info, true, &entry);
	if (err < 0)
		goto create_err;

	if (entry.flags & MPTCP_PM_ADDR_FLAG_SIGNAL) {
		NL_SET_ERR_MSG_ATTR(info->extack, laddr, "invalid addr flags");
		err = -EINVAL;
		goto create_err;
	}
	entry.flags |= MPTCP_PM_ADDR_FLAG_SUBFLOW;

	raddr = info->attrs[MPTCP_PM_ATTR_ADDR_REMOTE];
	err = mptcp_pm_parse_addr(raddr, info, &addr_r);
	if (err < 0)
		goto create_err;

	if (!mptcp_pm_addr_families_match(sk, &entry.addr, &addr_r)) {
		GENL_SET_ERR_MSG(info, "families mismatch");
		err = -EINVAL;
		goto create_err;
	}

	err = mptcp_userspace_pm_append_new_local_addr(msk, &entry, false);
	if (err < 0) {
		NL_SET_ERR_MSG_ATTR(info->extack, laddr,
				    "did not match address and id");
		goto create_err;
	}

	local.addr = entry.addr;
	local.flags = entry.flags;
	local.ifindex = entry.ifindex;

	lock_sock(sk);
	err = __mptcp_subflow_connect(sk, &local, &addr_r);
	release_sock(sk);

	if (err)
		GENL_SET_ERR_MSG_FMT(info, "connect error: %d", err);

	spin_lock_bh(&msk->pm.lock);
	if (err)
		mptcp_userspace_pm_delete_local_addr(msk, &entry);
	else
		msk->pm.subflows++;
	spin_unlock_bh(&msk->pm.lock);

 create_err:
	sock_put(sk);
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
			if (!ipv6_addr_equal(&local->addr6, &issk->pinet6->saddr) ||
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

int mptcp_pm_nl_subflow_destroy_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct mptcp_pm_addr_entry addr_l;
	struct mptcp_addr_info addr_r;
	struct nlattr *raddr, *laddr;
	struct mptcp_sock *msk;
	struct sock *sk, *ssk;
	int err = -EINVAL;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ATTR_ADDR) ||
	    GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ATTR_ADDR_REMOTE))
		return err;

	msk = mptcp_userspace_pm_get_sock(info);
	if (!msk)
		return err;

	sk = (struct sock *)msk;

	laddr = info->attrs[MPTCP_PM_ATTR_ADDR];
	err = mptcp_pm_parse_entry(laddr, info, true, &addr_l);
	if (err < 0)
		goto destroy_err;

	raddr = info->attrs[MPTCP_PM_ATTR_ADDR_REMOTE];
	err = mptcp_pm_parse_addr(raddr, info, &addr_r);
	if (err < 0)
		goto destroy_err;

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (addr_l.addr.family == AF_INET && ipv6_addr_v4mapped(&addr_r.addr6)) {
		ipv6_addr_set_v4mapped(addr_l.addr.addr.s_addr, &addr_l.addr.addr6);
		addr_l.addr.family = AF_INET6;
	}
	if (addr_r.family == AF_INET && ipv6_addr_v4mapped(&addr_l.addr.addr6)) {
		ipv6_addr_set_v4mapped(addr_r.addr.s_addr, &addr_r.addr6);
		addr_r.family = AF_INET6;
	}
#endif
	if (addr_l.addr.family != addr_r.family) {
		GENL_SET_ERR_MSG(info, "address families do not match");
		err = -EINVAL;
		goto destroy_err;
	}

	if (!addr_l.addr.port) {
		NL_SET_ERR_MSG_ATTR(info->extack, laddr, "missing local port");
		err = -EINVAL;
		goto destroy_err;
	}

	if (!addr_r.port) {
		NL_SET_ERR_MSG_ATTR(info->extack, raddr, "missing remote port");
		err = -EINVAL;
		goto destroy_err;
	}

	lock_sock(sk);
	ssk = mptcp_nl_find_ssk(msk, &addr_l.addr, &addr_r);
	if (!ssk) {
		GENL_SET_ERR_MSG(info, "subflow not found");
		err = -ESRCH;
		goto release_sock;
	}

	spin_lock_bh(&msk->pm.lock);
	mptcp_userspace_pm_delete_local_addr(msk, &addr_l);
	spin_unlock_bh(&msk->pm.lock);
	mptcp_subflow_shutdown(sk, ssk, RCV_SHUTDOWN | SEND_SHUTDOWN);
	mptcp_close_ssk(sk, ssk, mptcp_subflow_ctx(ssk));
	MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_RMSUBFLOW);
release_sock:
	release_sock(sk);

destroy_err:
	sock_put(sk);
	return err;
}

int mptcp_userspace_pm_set_flags(struct mptcp_pm_addr_entry *local,
				 struct genl_info *info)
{
	struct mptcp_addr_info rem = { .family = AF_UNSPEC, };
	struct mptcp_pm_addr_entry *entry;
	struct nlattr *attr, *attr_rem;
	struct mptcp_sock *msk;
	int ret = -EINVAL;
	struct sock *sk;
	u8 bkup = 0;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ATTR_ADDR_REMOTE))
		return ret;

	msk = mptcp_userspace_pm_get_sock(info);
	if (!msk)
		return ret;

	sk = (struct sock *)msk;

	attr = info->attrs[MPTCP_PM_ATTR_ADDR];
	if (local->addr.family == AF_UNSPEC) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "invalid local address family");
		ret = -EINVAL;
		goto set_flags_err;
	}

	attr_rem = info->attrs[MPTCP_PM_ATTR_ADDR_REMOTE];
	ret = mptcp_pm_parse_addr(attr_rem, info, &rem);
	if (ret < 0)
		goto set_flags_err;

	if (rem.family == AF_UNSPEC) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr_rem,
				    "invalid remote address family");
		ret = -EINVAL;
		goto set_flags_err;
	}

	if (local->flags & MPTCP_PM_ADDR_FLAG_BACKUP)
		bkup = 1;

	spin_lock_bh(&msk->pm.lock);
	entry = mptcp_userspace_pm_lookup_addr(msk, &local->addr);
	if (entry) {
		if (bkup)
			entry->flags |= MPTCP_PM_ADDR_FLAG_BACKUP;
		else
			entry->flags &= ~MPTCP_PM_ADDR_FLAG_BACKUP;
	}
	spin_unlock_bh(&msk->pm.lock);

	lock_sock(sk);
	ret = mptcp_pm_mp_prio_send_ack(msk, &local->addr, &rem, bkup);
	release_sock(sk);

	/* mptcp_pm_mp_prio_send_ack() only fails in one case */
	if (ret < 0)
		GENL_SET_ERR_MSG(info, "subflow not found");

set_flags_err:
	sock_put(sk);
	return ret;
}

int mptcp_userspace_pm_dump_addr(struct sk_buff *msg,
				 struct netlink_callback *cb)
{
	struct id_bitmap {
		DECLARE_BITMAP(map, MPTCP_PM_MAX_ADDR_ID + 1);
	} *bitmap;
	const struct genl_info *info = genl_info_dump(cb);
	struct mptcp_pm_addr_entry *entry;
	struct mptcp_sock *msk;
	int ret = -EINVAL;
	struct sock *sk;

	BUILD_BUG_ON(sizeof(struct id_bitmap) > sizeof(cb->ctx));

	bitmap = (struct id_bitmap *)cb->ctx;

	msk = mptcp_userspace_pm_get_sock(info);
	if (!msk)
		return ret;

	sk = (struct sock *)msk;

	lock_sock(sk);
	spin_lock_bh(&msk->pm.lock);
	mptcp_for_each_userspace_pm_addr(msk, entry) {
		if (test_bit(entry->addr.id, bitmap->map))
			continue;

		if (mptcp_pm_genl_fill_addr(msg, cb, entry) < 0)
			break;

		__set_bit(entry->addr.id, bitmap->map);
	}
	spin_unlock_bh(&msk->pm.lock);
	release_sock(sk);
	ret = msg->len;

	sock_put(sk);
	return ret;
}

int mptcp_userspace_pm_get_addr(u8 id, struct mptcp_pm_addr_entry *addr,
				struct genl_info *info)
{
	struct mptcp_pm_addr_entry *entry;
	struct mptcp_sock *msk;
	int ret = -EINVAL;
	struct sock *sk;

	msk = mptcp_userspace_pm_get_sock(info);
	if (!msk)
		return ret;

	sk = (struct sock *)msk;

	lock_sock(sk);
	spin_lock_bh(&msk->pm.lock);
	entry = mptcp_userspace_pm_lookup_addr_by_id(msk, id);
	if (entry) {
		*addr = *entry;
		ret = 0;
	}
	spin_unlock_bh(&msk->pm.lock);
	release_sock(sk);

	sock_put(sk);
	return ret;
}

static struct mptcp_pm_ops mptcp_pm_userspace = {
	.name			= "userspace",
	.owner			= THIS_MODULE,
};

void __init mptcp_pm_userspace_register(void)
{
	mptcp_pm_register(&mptcp_pm_userspace);
}
