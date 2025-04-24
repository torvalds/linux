// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2025, Matthieu Baerts.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <net/netns/generic.h>

#include "protocol.h"
#include "mib.h"
#include "mptcp_pm_gen.h"

static int pm_nl_pernet_id;

struct pm_nl_pernet {
	/* protects pernet updates */
	spinlock_t		lock;
	struct list_head	local_addr_list;
	unsigned int		addrs;
	unsigned int		stale_loss_cnt;
	unsigned int		add_addr_signal_max;
	unsigned int		add_addr_accept_max;
	unsigned int		local_addr_max;
	unsigned int		subflows_max;
	unsigned int		next_id;
	DECLARE_BITMAP(id_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);
};

#define MPTCP_PM_ADDR_MAX	8

static struct pm_nl_pernet *pm_nl_get_pernet(const struct net *net)
{
	return net_generic(net, pm_nl_pernet_id);
}

static struct pm_nl_pernet *
pm_nl_get_pernet_from_msk(const struct mptcp_sock *msk)
{
	return pm_nl_get_pernet(sock_net((struct sock *)msk));
}

static struct pm_nl_pernet *genl_info_pm_nl(struct genl_info *info)
{
	return pm_nl_get_pernet(genl_info_net(info));
}

unsigned int mptcp_pm_get_add_addr_signal_max(const struct mptcp_sock *msk)
{
	const struct pm_nl_pernet *pernet = pm_nl_get_pernet_from_msk(msk);

	return READ_ONCE(pernet->add_addr_signal_max);
}
EXPORT_SYMBOL_GPL(mptcp_pm_get_add_addr_signal_max);

unsigned int mptcp_pm_get_add_addr_accept_max(const struct mptcp_sock *msk)
{
	struct pm_nl_pernet *pernet = pm_nl_get_pernet_from_msk(msk);

	return READ_ONCE(pernet->add_addr_accept_max);
}
EXPORT_SYMBOL_GPL(mptcp_pm_get_add_addr_accept_max);

unsigned int mptcp_pm_get_subflows_max(const struct mptcp_sock *msk)
{
	struct pm_nl_pernet *pernet = pm_nl_get_pernet_from_msk(msk);

	return READ_ONCE(pernet->subflows_max);
}
EXPORT_SYMBOL_GPL(mptcp_pm_get_subflows_max);

unsigned int mptcp_pm_get_local_addr_max(const struct mptcp_sock *msk)
{
	struct pm_nl_pernet *pernet = pm_nl_get_pernet_from_msk(msk);

	return READ_ONCE(pernet->local_addr_max);
}
EXPORT_SYMBOL_GPL(mptcp_pm_get_local_addr_max);

static bool lookup_subflow_by_daddr(const struct list_head *list,
				    const struct mptcp_addr_info *daddr)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_addr_info cur;

	list_for_each_entry(subflow, list, node) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		if (!((1 << inet_sk_state_load(ssk)) &
		      (TCPF_ESTABLISHED | TCPF_SYN_SENT | TCPF_SYN_RECV)))
			continue;

		mptcp_remote_address((struct sock_common *)ssk, &cur);
		if (mptcp_addresses_equal(&cur, daddr, daddr->port))
			return true;
	}

	return false;
}

static bool
select_local_address(const struct pm_nl_pernet *pernet,
		     const struct mptcp_sock *msk,
		     struct mptcp_pm_local *new_local)
{
	struct mptcp_pm_addr_entry *entry;
	bool found = false;

	msk_owned_by_me(msk);

	rcu_read_lock();
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (!(entry->flags & MPTCP_PM_ADDR_FLAG_SUBFLOW))
			continue;

		if (!test_bit(entry->addr.id, msk->pm.id_avail_bitmap))
			continue;

		new_local->addr = entry->addr;
		new_local->flags = entry->flags;
		new_local->ifindex = entry->ifindex;
		found = true;
		break;
	}
	rcu_read_unlock();

	return found;
}

static bool
select_signal_address(struct pm_nl_pernet *pernet, const struct mptcp_sock *msk,
		      struct mptcp_pm_local *new_local)
{
	struct mptcp_pm_addr_entry *entry;
	bool found = false;

	rcu_read_lock();
	/* do not keep any additional per socket state, just signal
	 * the address list in order.
	 * Note: removal from the local address list during the msk life-cycle
	 * can lead to additional addresses not being announced.
	 */
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (!test_bit(entry->addr.id, msk->pm.id_avail_bitmap))
			continue;

		if (!(entry->flags & MPTCP_PM_ADDR_FLAG_SIGNAL))
			continue;

		new_local->addr = entry->addr;
		new_local->flags = entry->flags;
		new_local->ifindex = entry->ifindex;
		found = true;
		break;
	}
	rcu_read_unlock();

	return found;
}

/* Fill all the remote addresses into the array addrs[],
 * and return the array size.
 */
static unsigned int fill_remote_addresses_vec(struct mptcp_sock *msk,
					      struct mptcp_addr_info *local,
					      bool fullmesh,
					      struct mptcp_addr_info *addrs)
{
	bool deny_id0 = READ_ONCE(msk->pm.remote_deny_join_id0);
	struct sock *sk = (struct sock *)msk, *ssk;
	struct mptcp_subflow_context *subflow;
	struct mptcp_addr_info remote = { 0 };
	unsigned int subflows_max;
	int i = 0;

	subflows_max = mptcp_pm_get_subflows_max(msk);
	mptcp_remote_address((struct sock_common *)sk, &remote);

	/* Non-fullmesh endpoint, fill in the single entry
	 * corresponding to the primary MPC subflow remote address
	 */
	if (!fullmesh) {
		if (deny_id0)
			return 0;

		if (!mptcp_pm_addr_families_match(sk, local, &remote))
			return 0;

		msk->pm.subflows++;
		addrs[i++] = remote;
	} else {
		DECLARE_BITMAP(unavail_id, MPTCP_PM_MAX_ADDR_ID + 1);

		/* Forbid creation of new subflows matching existing
		 * ones, possibly already created by incoming ADD_ADDR
		 */
		bitmap_zero(unavail_id, MPTCP_PM_MAX_ADDR_ID + 1);
		mptcp_for_each_subflow(msk, subflow)
			if (READ_ONCE(subflow->local_id) == local->id)
				__set_bit(subflow->remote_id, unavail_id);

		mptcp_for_each_subflow(msk, subflow) {
			ssk = mptcp_subflow_tcp_sock(subflow);
			mptcp_remote_address((struct sock_common *)ssk, &addrs[i]);
			addrs[i].id = READ_ONCE(subflow->remote_id);
			if (deny_id0 && !addrs[i].id)
				continue;

			if (test_bit(addrs[i].id, unavail_id))
				continue;

			if (!mptcp_pm_addr_families_match(sk, local, &addrs[i]))
				continue;

			if (msk->pm.subflows < subflows_max) {
				/* forbid creating multiple address towards
				 * this id
				 */
				__set_bit(addrs[i].id, unavail_id);
				msk->pm.subflows++;
				i++;
			}
		}
	}

	return i;
}

static struct mptcp_pm_addr_entry *
__lookup_addr_by_id(struct pm_nl_pernet *pernet, unsigned int id)
{
	struct mptcp_pm_addr_entry *entry;

	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list,
				lockdep_is_held(&pernet->lock)) {
		if (entry->addr.id == id)
			return entry;
	}
	return NULL;
}

static struct mptcp_pm_addr_entry *
__lookup_addr(struct pm_nl_pernet *pernet, const struct mptcp_addr_info *info)
{
	struct mptcp_pm_addr_entry *entry;

	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list,
				lockdep_is_held(&pernet->lock)) {
		if (mptcp_addresses_equal(&entry->addr, info, entry->addr.port))
			return entry;
	}
	return NULL;
}

static void mptcp_pm_create_subflow_or_signal_addr(struct mptcp_sock *msk)
{
	struct sock *sk = (struct sock *)msk;
	unsigned int add_addr_signal_max;
	bool signal_and_subflow = false;
	unsigned int local_addr_max;
	struct pm_nl_pernet *pernet;
	struct mptcp_pm_local local;
	unsigned int subflows_max;

	pernet = pm_nl_get_pernet(sock_net(sk));

	add_addr_signal_max = mptcp_pm_get_add_addr_signal_max(msk);
	local_addr_max = mptcp_pm_get_local_addr_max(msk);
	subflows_max = mptcp_pm_get_subflows_max(msk);

	/* do lazy endpoint usage accounting for the MPC subflows */
	if (unlikely(!(msk->pm.status & BIT(MPTCP_PM_MPC_ENDPOINT_ACCOUNTED))) && msk->first) {
		struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(msk->first);
		struct mptcp_pm_addr_entry *entry;
		struct mptcp_addr_info mpc_addr;
		bool backup = false;

		mptcp_local_address((struct sock_common *)msk->first, &mpc_addr);
		rcu_read_lock();
		entry = __lookup_addr(pernet, &mpc_addr);
		if (entry) {
			__clear_bit(entry->addr.id, msk->pm.id_avail_bitmap);
			msk->mpc_endpoint_id = entry->addr.id;
			backup = !!(entry->flags & MPTCP_PM_ADDR_FLAG_BACKUP);
		}
		rcu_read_unlock();

		if (backup)
			mptcp_pm_send_ack(msk, subflow, true, backup);

		msk->pm.status |= BIT(MPTCP_PM_MPC_ENDPOINT_ACCOUNTED);
	}

	pr_debug("local %d:%d signal %d:%d subflows %d:%d\n",
		 msk->pm.local_addr_used, local_addr_max,
		 msk->pm.add_addr_signaled, add_addr_signal_max,
		 msk->pm.subflows, subflows_max);

	/* check first for announce */
	if (msk->pm.add_addr_signaled < add_addr_signal_max) {
		/* due to racing events on both ends we can reach here while
		 * previous add address is still running: if we invoke now
		 * mptcp_pm_announce_addr(), that will fail and the
		 * corresponding id will be marked as used.
		 * Instead let the PM machinery reschedule us when the
		 * current address announce will be completed.
		 */
		if (msk->pm.addr_signal & BIT(MPTCP_ADD_ADDR_SIGNAL))
			return;

		if (!select_signal_address(pernet, msk, &local))
			goto subflow;

		/* If the alloc fails, we are on memory pressure, not worth
		 * continuing, and trying to create subflows.
		 */
		if (!mptcp_pm_alloc_anno_list(msk, &local.addr))
			return;

		__clear_bit(local.addr.id, msk->pm.id_avail_bitmap);
		msk->pm.add_addr_signaled++;

		/* Special case for ID0: set the correct ID */
		if (local.addr.id == msk->mpc_endpoint_id)
			local.addr.id = 0;

		mptcp_pm_announce_addr(msk, &local.addr, false);
		mptcp_pm_addr_send_ack(msk);

		if (local.flags & MPTCP_PM_ADDR_FLAG_SUBFLOW)
			signal_and_subflow = true;
	}

subflow:
	/* check if should create a new subflow */
	while (msk->pm.local_addr_used < local_addr_max &&
	       msk->pm.subflows < subflows_max) {
		struct mptcp_addr_info addrs[MPTCP_PM_ADDR_MAX];
		bool fullmesh;
		int i, nr;

		if (signal_and_subflow)
			signal_and_subflow = false;
		else if (!select_local_address(pernet, msk, &local))
			break;

		fullmesh = !!(local.flags & MPTCP_PM_ADDR_FLAG_FULLMESH);

		__clear_bit(local.addr.id, msk->pm.id_avail_bitmap);

		/* Special case for ID0: set the correct ID */
		if (local.addr.id == msk->mpc_endpoint_id)
			local.addr.id = 0;
		else /* local_addr_used is not decr for ID 0 */
			msk->pm.local_addr_used++;

		nr = fill_remote_addresses_vec(msk, &local.addr, fullmesh, addrs);
		if (nr == 0)
			continue;

		spin_unlock_bh(&msk->pm.lock);
		for (i = 0; i < nr; i++)
			__mptcp_subflow_connect(sk, &local, &addrs[i]);
		spin_lock_bh(&msk->pm.lock);
	}
	mptcp_pm_nl_check_work_pending(msk);
}

static void mptcp_pm_nl_fully_established(struct mptcp_sock *msk)
{
	mptcp_pm_create_subflow_or_signal_addr(msk);
}

static void mptcp_pm_nl_subflow_established(struct mptcp_sock *msk)
{
	mptcp_pm_create_subflow_or_signal_addr(msk);
}

/* Fill all the local addresses into the array addrs[],
 * and return the array size.
 */
static unsigned int fill_local_addresses_vec(struct mptcp_sock *msk,
					     struct mptcp_addr_info *remote,
					     struct mptcp_pm_local *locals)
{
	struct sock *sk = (struct sock *)msk;
	struct mptcp_pm_addr_entry *entry;
	struct mptcp_addr_info mpc_addr;
	struct pm_nl_pernet *pernet;
	unsigned int subflows_max;
	int i = 0;

	pernet = pm_nl_get_pernet_from_msk(msk);
	subflows_max = mptcp_pm_get_subflows_max(msk);

	mptcp_local_address((struct sock_common *)msk, &mpc_addr);

	rcu_read_lock();
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (!(entry->flags & MPTCP_PM_ADDR_FLAG_FULLMESH))
			continue;

		if (!mptcp_pm_addr_families_match(sk, &entry->addr, remote))
			continue;

		if (msk->pm.subflows < subflows_max) {
			locals[i].addr = entry->addr;
			locals[i].flags = entry->flags;
			locals[i].ifindex = entry->ifindex;

			/* Special case for ID0: set the correct ID */
			if (mptcp_addresses_equal(&locals[i].addr, &mpc_addr, locals[i].addr.port))
				locals[i].addr.id = 0;

			msk->pm.subflows++;
			i++;
		}
	}
	rcu_read_unlock();

	/* If the array is empty, fill in the single
	 * 'IPADDRANY' local address
	 */
	if (!i) {
		memset(&locals[i], 0, sizeof(locals[i]));
		locals[i].addr.family =
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
			       remote->family == AF_INET6 &&
			       ipv6_addr_v4mapped(&remote->addr6) ? AF_INET :
#endif
			       remote->family;

		if (!mptcp_pm_addr_families_match(sk, &locals[i].addr, remote))
			return 0;

		msk->pm.subflows++;
		i++;
	}

	return i;
}

static void mptcp_pm_nl_add_addr_received(struct mptcp_sock *msk)
{
	struct mptcp_pm_local locals[MPTCP_PM_ADDR_MAX];
	struct sock *sk = (struct sock *)msk;
	unsigned int add_addr_accept_max;
	struct mptcp_addr_info remote;
	unsigned int subflows_max;
	bool sf_created = false;
	int i, nr;

	add_addr_accept_max = mptcp_pm_get_add_addr_accept_max(msk);
	subflows_max = mptcp_pm_get_subflows_max(msk);

	pr_debug("accepted %d:%d remote family %d\n",
		 msk->pm.add_addr_accepted, add_addr_accept_max,
		 msk->pm.remote.family);

	remote = msk->pm.remote;
	mptcp_pm_announce_addr(msk, &remote, true);
	mptcp_pm_addr_send_ack(msk);

	if (lookup_subflow_by_daddr(&msk->conn_list, &remote))
		return;

	/* pick id 0 port, if none is provided the remote address */
	if (!remote.port)
		remote.port = sk->sk_dport;

	/* connect to the specified remote address, using whatever
	 * local address the routing configuration will pick.
	 */
	nr = fill_local_addresses_vec(msk, &remote, locals);
	if (nr == 0)
		return;

	spin_unlock_bh(&msk->pm.lock);
	for (i = 0; i < nr; i++)
		if (__mptcp_subflow_connect(sk, &locals[i], &remote) == 0)
			sf_created = true;
	spin_lock_bh(&msk->pm.lock);

	if (sf_created) {
		/* add_addr_accepted is not decr for ID 0 */
		if (remote.id)
			msk->pm.add_addr_accepted++;
		if (msk->pm.add_addr_accepted >= add_addr_accept_max ||
		    msk->pm.subflows >= subflows_max)
			WRITE_ONCE(msk->pm.accept_addr, false);
	}
}

void mptcp_pm_nl_rm_addr(struct mptcp_sock *msk, u8 rm_id)
{
	if (rm_id && WARN_ON_ONCE(msk->pm.add_addr_accepted == 0)) {
		/* Note: if the subflow has been closed before, this
		 * add_addr_accepted counter will not be decremented.
		 */
		if (--msk->pm.add_addr_accepted < mptcp_pm_get_add_addr_accept_max(msk))
			WRITE_ONCE(msk->pm.accept_addr, true);
	}
}

static bool address_use_port(struct mptcp_pm_addr_entry *entry)
{
	return (entry->flags &
		(MPTCP_PM_ADDR_FLAG_SIGNAL | MPTCP_PM_ADDR_FLAG_SUBFLOW)) ==
		MPTCP_PM_ADDR_FLAG_SIGNAL;
}

/* caller must ensure the RCU grace period is already elapsed */
static void __mptcp_pm_release_addr_entry(struct mptcp_pm_addr_entry *entry)
{
	if (entry->lsk)
		sock_release(entry->lsk);
	kfree(entry);
}

static int mptcp_pm_nl_append_new_local_addr(struct pm_nl_pernet *pernet,
					     struct mptcp_pm_addr_entry *entry,
					     bool needs_id, bool replace)
{
	struct mptcp_pm_addr_entry *cur, *del_entry = NULL;
	unsigned int addr_max;
	int ret = -EINVAL;

	spin_lock_bh(&pernet->lock);
	/* to keep the code simple, don't do IDR-like allocation for address ID,
	 * just bail when we exceed limits
	 */
	if (pernet->next_id == MPTCP_PM_MAX_ADDR_ID)
		pernet->next_id = 1;
	if (pernet->addrs >= MPTCP_PM_ADDR_MAX) {
		ret = -ERANGE;
		goto out;
	}
	if (test_bit(entry->addr.id, pernet->id_bitmap)) {
		ret = -EBUSY;
		goto out;
	}

	/* do not insert duplicate address, differentiate on port only
	 * singled addresses
	 */
	if (!address_use_port(entry))
		entry->addr.port = 0;
	list_for_each_entry(cur, &pernet->local_addr_list, list) {
		if (mptcp_addresses_equal(&cur->addr, &entry->addr,
					  cur->addr.port || entry->addr.port)) {
			/* allow replacing the exiting endpoint only if such
			 * endpoint is an implicit one and the user-space
			 * did not provide an endpoint id
			 */
			if (!(cur->flags & MPTCP_PM_ADDR_FLAG_IMPLICIT)) {
				ret = -EEXIST;
				goto out;
			}
			if (entry->addr.id)
				goto out;

			/* allow callers that only need to look up the local
			 * addr's id to skip replacement. This allows them to
			 * avoid calling synchronize_rcu in the packet recv
			 * path.
			 */
			if (!replace) {
				kfree(entry);
				ret = cur->addr.id;
				goto out;
			}

			pernet->addrs--;
			entry->addr.id = cur->addr.id;
			list_del_rcu(&cur->list);
			del_entry = cur;
			break;
		}
	}

	if (!entry->addr.id && needs_id) {
find_next:
		entry->addr.id = find_next_zero_bit(pernet->id_bitmap,
						    MPTCP_PM_MAX_ADDR_ID + 1,
						    pernet->next_id);
		if (!entry->addr.id && pernet->next_id != 1) {
			pernet->next_id = 1;
			goto find_next;
		}
	}

	if (!entry->addr.id && needs_id)
		goto out;

	__set_bit(entry->addr.id, pernet->id_bitmap);
	if (entry->addr.id > pernet->next_id)
		pernet->next_id = entry->addr.id;

	if (entry->flags & MPTCP_PM_ADDR_FLAG_SIGNAL) {
		addr_max = pernet->add_addr_signal_max;
		WRITE_ONCE(pernet->add_addr_signal_max, addr_max + 1);
	}
	if (entry->flags & MPTCP_PM_ADDR_FLAG_SUBFLOW) {
		addr_max = pernet->local_addr_max;
		WRITE_ONCE(pernet->local_addr_max, addr_max + 1);
	}

	pernet->addrs++;
	if (!entry->addr.port)
		list_add_tail_rcu(&entry->list, &pernet->local_addr_list);
	else
		list_add_rcu(&entry->list, &pernet->local_addr_list);
	ret = entry->addr.id;

out:
	spin_unlock_bh(&pernet->lock);

	/* just replaced an existing entry, free it */
	if (del_entry) {
		synchronize_rcu();
		__mptcp_pm_release_addr_entry(del_entry);
	}
	return ret;
}

static struct lock_class_key mptcp_slock_keys[2];
static struct lock_class_key mptcp_keys[2];

static int mptcp_pm_nl_create_listen_socket(struct sock *sk,
					    struct mptcp_pm_addr_entry *entry)
{
	bool is_ipv6 = sk->sk_family == AF_INET6;
	int addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_storage addr;
	struct sock *newsk, *ssk;
	int backlog = 1024;
	int err;

	err = sock_create_kern(sock_net(sk), entry->addr.family,
			       SOCK_STREAM, IPPROTO_MPTCP, &entry->lsk);
	if (err)
		return err;

	newsk = entry->lsk->sk;
	if (!newsk)
		return -EINVAL;

	/* The subflow socket lock is acquired in a nested to the msk one
	 * in several places, even by the TCP stack, and this msk is a kernel
	 * socket: lockdep complains. Instead of propagating the _nested
	 * modifiers in several places, re-init the lock class for the msk
	 * socket to an mptcp specific one.
	 */
	sock_lock_init_class_and_name(newsk,
				      is_ipv6 ? "mlock-AF_INET6" : "mlock-AF_INET",
				      &mptcp_slock_keys[is_ipv6],
				      is_ipv6 ? "msk_lock-AF_INET6" : "msk_lock-AF_INET",
				      &mptcp_keys[is_ipv6]);

	lock_sock(newsk);
	ssk = __mptcp_nmpc_sk(mptcp_sk(newsk));
	release_sock(newsk);
	if (IS_ERR(ssk))
		return PTR_ERR(ssk);

	mptcp_info2sockaddr(&entry->addr, &addr, entry->addr.family);
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (entry->addr.family == AF_INET6)
		addrlen = sizeof(struct sockaddr_in6);
#endif
	if (ssk->sk_family == AF_INET)
		err = inet_bind_sk(ssk, (struct sockaddr *)&addr, addrlen);
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else if (ssk->sk_family == AF_INET6)
		err = inet6_bind_sk(ssk, (struct sockaddr *)&addr, addrlen);
#endif
	if (err)
		return err;

	/* We don't use mptcp_set_state() here because it needs to be called
	 * under the msk socket lock. For the moment, that will not bring
	 * anything more than only calling inet_sk_state_store(), because the
	 * old status is known (TCP_CLOSE).
	 */
	inet_sk_state_store(newsk, TCP_LISTEN);
	lock_sock(ssk);
	WRITE_ONCE(mptcp_subflow_ctx(ssk)->pm_listener, true);
	err = __inet_listen_sk(ssk, backlog);
	if (!err)
		mptcp_event_pm_listener(ssk, MPTCP_EVENT_LISTENER_CREATED);
	release_sock(ssk);
	return err;
}

int mptcp_pm_nl_get_local_id(struct mptcp_sock *msk,
			     struct mptcp_pm_addr_entry *skc)
{
	struct mptcp_pm_addr_entry *entry;
	struct pm_nl_pernet *pernet;
	int ret;

	pernet = pm_nl_get_pernet_from_msk(msk);

	rcu_read_lock();
	entry = __lookup_addr(pernet, &skc->addr);
	ret = entry ? entry->addr.id : -1;
	rcu_read_unlock();
	if (ret >= 0)
		return ret;

	/* address not found, add to local list */
	entry = kmemdup(skc, sizeof(*skc), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	entry->addr.port = 0;
	ret = mptcp_pm_nl_append_new_local_addr(pernet, entry, true, false);
	if (ret < 0)
		kfree(entry);

	return ret;
}

bool mptcp_pm_nl_is_backup(struct mptcp_sock *msk, struct mptcp_addr_info *skc)
{
	struct pm_nl_pernet *pernet = pm_nl_get_pernet_from_msk(msk);
	struct mptcp_pm_addr_entry *entry;
	bool backup;

	rcu_read_lock();
	entry = __lookup_addr(pernet, skc);
	backup = entry && !!(entry->flags & MPTCP_PM_ADDR_FLAG_BACKUP);
	rcu_read_unlock();

	return backup;
}

static int mptcp_nl_add_subflow_or_signal_addr(struct net *net,
					       struct mptcp_addr_info *addr)
{
	struct mptcp_sock *msk;
	long s_slot = 0, s_num = 0;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;
		struct mptcp_addr_info mpc_addr;

		if (!READ_ONCE(msk->fully_established) ||
		    mptcp_pm_is_userspace(msk))
			goto next;

		/* if the endp linked to the init sf is re-added with a != ID */
		mptcp_local_address((struct sock_common *)msk, &mpc_addr);

		lock_sock(sk);
		spin_lock_bh(&msk->pm.lock);
		if (mptcp_addresses_equal(addr, &mpc_addr, addr->port))
			msk->mpc_endpoint_id = addr->id;
		mptcp_pm_create_subflow_or_signal_addr(msk);
		spin_unlock_bh(&msk->pm.lock);
		release_sock(sk);

next:
		sock_put(sk);
		cond_resched();
	}

	return 0;
}

static bool mptcp_pm_has_addr_attr_id(const struct nlattr *attr,
				      struct genl_info *info)
{
	struct nlattr *tb[MPTCP_PM_ADDR_ATTR_MAX + 1];

	if (!nla_parse_nested_deprecated(tb, MPTCP_PM_ADDR_ATTR_MAX, attr,
					 mptcp_pm_address_nl_policy, info->extack) &&
	    tb[MPTCP_PM_ADDR_ATTR_ID])
		return true;
	return false;
}

/* Add an MPTCP endpoint */
int mptcp_pm_nl_add_addr_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry addr, *entry;
	struct nlattr *attr;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ENDPOINT_ADDR))
		return -EINVAL;

	attr = info->attrs[MPTCP_PM_ENDPOINT_ADDR];
	ret = mptcp_pm_parse_entry(attr, info, true, &addr);
	if (ret < 0)
		return ret;

	if (addr.addr.port && !address_use_port(&addr)) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "flags must have signal and not subflow when using port");
		return -EINVAL;
	}

	if (addr.flags & MPTCP_PM_ADDR_FLAG_SIGNAL &&
	    addr.flags & MPTCP_PM_ADDR_FLAG_FULLMESH) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "flags mustn't have both signal and fullmesh");
		return -EINVAL;
	}

	if (addr.flags & MPTCP_PM_ADDR_FLAG_IMPLICIT) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "can't create IMPLICIT endpoint");
		return -EINVAL;
	}

	entry = kmemdup(&addr, sizeof(addr), GFP_KERNEL_ACCOUNT);
	if (!entry) {
		GENL_SET_ERR_MSG(info, "can't allocate addr");
		return -ENOMEM;
	}

	if (entry->addr.port) {
		ret = mptcp_pm_nl_create_listen_socket(skb->sk, entry);
		if (ret) {
			GENL_SET_ERR_MSG_FMT(info, "create listen socket error: %d", ret);
			goto out_free;
		}
	}
	ret = mptcp_pm_nl_append_new_local_addr(pernet, entry,
						!mptcp_pm_has_addr_attr_id(attr, info),
						true);
	if (ret < 0) {
		GENL_SET_ERR_MSG_FMT(info, "too many addresses or duplicate one: %d", ret);
		goto out_free;
	}

	mptcp_nl_add_subflow_or_signal_addr(sock_net(skb->sk), &entry->addr);
	return 0;

out_free:
	__mptcp_pm_release_addr_entry(entry);
	return ret;
}

static u8 mptcp_endp_get_local_id(struct mptcp_sock *msk,
				  const struct mptcp_addr_info *addr)
{
	return msk->mpc_endpoint_id == addr->id ? 0 : addr->id;
}

static bool mptcp_pm_remove_anno_addr(struct mptcp_sock *msk,
				      const struct mptcp_addr_info *addr,
				      bool force)
{
	struct mptcp_rm_list list = { .nr = 0 };
	bool ret;

	list.ids[list.nr++] = mptcp_endp_get_local_id(msk, addr);

	ret = mptcp_remove_anno_list_by_saddr(msk, addr);
	if (ret || force) {
		spin_lock_bh(&msk->pm.lock);
		if (ret) {
			__set_bit(addr->id, msk->pm.id_avail_bitmap);
			msk->pm.add_addr_signaled--;
		}
		mptcp_pm_remove_addr(msk, &list);
		spin_unlock_bh(&msk->pm.lock);
	}
	return ret;
}

static void __mark_subflow_endp_available(struct mptcp_sock *msk, u8 id)
{
	/* If it was marked as used, and not ID 0, decrement local_addr_used */
	if (!__test_and_set_bit(id ? : msk->mpc_endpoint_id, msk->pm.id_avail_bitmap) &&
	    id && !WARN_ON_ONCE(msk->pm.local_addr_used == 0))
		msk->pm.local_addr_used--;
}

static int mptcp_nl_remove_subflow_and_signal_addr(struct net *net,
						   const struct mptcp_pm_addr_entry *entry)
{
	const struct mptcp_addr_info *addr = &entry->addr;
	struct mptcp_rm_list list = { .nr = 1 };
	long s_slot = 0, s_num = 0;
	struct mptcp_sock *msk;

	pr_debug("remove_id=%d\n", addr->id);

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;
		bool remove_subflow;

		if (mptcp_pm_is_userspace(msk))
			goto next;

		lock_sock(sk);
		remove_subflow = mptcp_lookup_subflow_by_saddr(&msk->conn_list, addr);
		mptcp_pm_remove_anno_addr(msk, addr, remove_subflow &&
					  !(entry->flags & MPTCP_PM_ADDR_FLAG_IMPLICIT));

		list.ids[0] = mptcp_endp_get_local_id(msk, addr);
		if (remove_subflow) {
			spin_lock_bh(&msk->pm.lock);
			mptcp_pm_rm_subflow(msk, &list);
			spin_unlock_bh(&msk->pm.lock);
		}

		if (entry->flags & MPTCP_PM_ADDR_FLAG_SUBFLOW) {
			spin_lock_bh(&msk->pm.lock);
			__mark_subflow_endp_available(msk, list.ids[0]);
			spin_unlock_bh(&msk->pm.lock);
		}

		if (msk->mpc_endpoint_id == entry->addr.id)
			msk->mpc_endpoint_id = 0;
		release_sock(sk);

next:
		sock_put(sk);
		cond_resched();
	}

	return 0;
}

static int mptcp_nl_remove_id_zero_address(struct net *net,
					   struct mptcp_addr_info *addr)
{
	struct mptcp_rm_list list = { .nr = 0 };
	long s_slot = 0, s_num = 0;
	struct mptcp_sock *msk;

	list.ids[list.nr++] = 0;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;
		struct mptcp_addr_info msk_local;

		if (list_empty(&msk->conn_list) || mptcp_pm_is_userspace(msk))
			goto next;

		mptcp_local_address((struct sock_common *)msk, &msk_local);
		if (!mptcp_addresses_equal(&msk_local, addr, addr->port))
			goto next;

		lock_sock(sk);
		spin_lock_bh(&msk->pm.lock);
		mptcp_pm_remove_addr(msk, &list);
		mptcp_pm_rm_subflow(msk, &list);
		__mark_subflow_endp_available(msk, 0);
		spin_unlock_bh(&msk->pm.lock);
		release_sock(sk);

next:
		sock_put(sk);
		cond_resched();
	}

	return 0;
}

/* Remove an MPTCP endpoint */
int mptcp_pm_nl_del_addr_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry addr, *entry;
	unsigned int addr_max;
	struct nlattr *attr;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, MPTCP_PM_ENDPOINT_ADDR))
		return -EINVAL;

	attr = info->attrs[MPTCP_PM_ENDPOINT_ADDR];
	ret = mptcp_pm_parse_entry(attr, info, false, &addr);
	if (ret < 0)
		return ret;

	/* the zero id address is special: the first address used by the msk
	 * always gets such an id, so different subflows can have different zero
	 * id addresses. Additionally zero id is not accounted for in id_bitmap.
	 * Let's use an 'mptcp_rm_list' instead of the common remove code.
	 */
	if (addr.addr.id == 0)
		return mptcp_nl_remove_id_zero_address(sock_net(skb->sk), &addr.addr);

	spin_lock_bh(&pernet->lock);
	entry = __lookup_addr_by_id(pernet, addr.addr.id);
	if (!entry) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr, "address not found");
		spin_unlock_bh(&pernet->lock);
		return -EINVAL;
	}
	if (entry->flags & MPTCP_PM_ADDR_FLAG_SIGNAL) {
		addr_max = pernet->add_addr_signal_max;
		WRITE_ONCE(pernet->add_addr_signal_max, addr_max - 1);
	}
	if (entry->flags & MPTCP_PM_ADDR_FLAG_SUBFLOW) {
		addr_max = pernet->local_addr_max;
		WRITE_ONCE(pernet->local_addr_max, addr_max - 1);
	}

	pernet->addrs--;
	list_del_rcu(&entry->list);
	__clear_bit(entry->addr.id, pernet->id_bitmap);
	spin_unlock_bh(&pernet->lock);

	mptcp_nl_remove_subflow_and_signal_addr(sock_net(skb->sk), entry);
	synchronize_rcu();
	__mptcp_pm_release_addr_entry(entry);

	return ret;
}

static void mptcp_pm_flush_addrs_and_subflows(struct mptcp_sock *msk,
					      struct list_head *rm_list)
{
	struct mptcp_rm_list alist = { .nr = 0 }, slist = { .nr = 0 };
	struct mptcp_pm_addr_entry *entry;

	list_for_each_entry(entry, rm_list, list) {
		if (slist.nr < MPTCP_RM_IDS_MAX &&
		    mptcp_lookup_subflow_by_saddr(&msk->conn_list, &entry->addr))
			slist.ids[slist.nr++] = mptcp_endp_get_local_id(msk, &entry->addr);

		if (alist.nr < MPTCP_RM_IDS_MAX &&
		    mptcp_remove_anno_list_by_saddr(msk, &entry->addr))
			alist.ids[alist.nr++] = mptcp_endp_get_local_id(msk, &entry->addr);
	}

	spin_lock_bh(&msk->pm.lock);
	if (alist.nr) {
		msk->pm.add_addr_signaled -= alist.nr;
		mptcp_pm_remove_addr(msk, &alist);
	}
	if (slist.nr)
		mptcp_pm_rm_subflow(msk, &slist);
	/* Reset counters: maybe some subflows have been removed before */
	bitmap_fill(msk->pm.id_avail_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);
	msk->pm.local_addr_used = 0;
	spin_unlock_bh(&msk->pm.lock);
}

static void mptcp_nl_flush_addrs_list(struct net *net,
				      struct list_head *rm_list)
{
	long s_slot = 0, s_num = 0;
	struct mptcp_sock *msk;

	if (list_empty(rm_list))
		return;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;

		if (!mptcp_pm_is_userspace(msk)) {
			lock_sock(sk);
			mptcp_pm_flush_addrs_and_subflows(msk, rm_list);
			release_sock(sk);
		}

		sock_put(sk);
		cond_resched();
	}
}

/* caller must ensure the RCU grace period is already elapsed */
static void __flush_addrs(struct list_head *list)
{
	while (!list_empty(list)) {
		struct mptcp_pm_addr_entry *cur;

		cur = list_entry(list->next,
				 struct mptcp_pm_addr_entry, list);
		list_del_rcu(&cur->list);
		__mptcp_pm_release_addr_entry(cur);
	}
}

static void __reset_counters(struct pm_nl_pernet *pernet)
{
	WRITE_ONCE(pernet->add_addr_signal_max, 0);
	WRITE_ONCE(pernet->add_addr_accept_max, 0);
	WRITE_ONCE(pernet->local_addr_max, 0);
	pernet->addrs = 0;
}

int mptcp_pm_nl_flush_addrs_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	LIST_HEAD(free_list);

	spin_lock_bh(&pernet->lock);
	list_splice_init(&pernet->local_addr_list, &free_list);
	__reset_counters(pernet);
	pernet->next_id = 1;
	bitmap_zero(pernet->id_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);
	spin_unlock_bh(&pernet->lock);
	mptcp_nl_flush_addrs_list(sock_net(skb->sk), &free_list);
	synchronize_rcu();
	__flush_addrs(&free_list);
	return 0;
}

int mptcp_pm_nl_get_addr(u8 id, struct mptcp_pm_addr_entry *addr,
			 struct genl_info *info)
{
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry *entry;
	int ret = -EINVAL;

	rcu_read_lock();
	entry = __lookup_addr_by_id(pernet, id);
	if (entry) {
		*addr = *entry;
		ret = 0;
	}
	rcu_read_unlock();

	return ret;
}

int mptcp_pm_nl_dump_addr(struct sk_buff *msg,
			  struct netlink_callback *cb)
{
	struct net *net = sock_net(msg->sk);
	struct mptcp_pm_addr_entry *entry;
	struct pm_nl_pernet *pernet;
	int id = cb->args[0];
	int i;

	pernet = pm_nl_get_pernet(net);

	rcu_read_lock();
	for (i = id; i < MPTCP_PM_MAX_ADDR_ID + 1; i++) {
		if (test_bit(i, pernet->id_bitmap)) {
			entry = __lookup_addr_by_id(pernet, i);
			if (!entry)
				break;

			if (entry->addr.id <= id)
				continue;

			if (mptcp_pm_genl_fill_addr(msg, cb, entry) < 0)
				break;

			id = entry->addr.id;
		}
	}
	rcu_read_unlock();

	cb->args[0] = id;
	return msg->len;
}

static int parse_limit(struct genl_info *info, int id, unsigned int *limit)
{
	struct nlattr *attr = info->attrs[id];

	if (!attr)
		return 0;

	*limit = nla_get_u32(attr);
	if (*limit > MPTCP_PM_ADDR_MAX) {
		NL_SET_ERR_MSG_ATTR_FMT(info->extack, attr,
					"limit greater than maximum (%u)",
					MPTCP_PM_ADDR_MAX);
		return -EINVAL;
	}
	return 0;
}

int mptcp_pm_nl_set_limits_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	unsigned int rcv_addrs, subflows;
	int ret;

	spin_lock_bh(&pernet->lock);
	rcv_addrs = pernet->add_addr_accept_max;
	ret = parse_limit(info, MPTCP_PM_ATTR_RCV_ADD_ADDRS, &rcv_addrs);
	if (ret)
		goto unlock;

	subflows = pernet->subflows_max;
	ret = parse_limit(info, MPTCP_PM_ATTR_SUBFLOWS, &subflows);
	if (ret)
		goto unlock;

	WRITE_ONCE(pernet->add_addr_accept_max, rcv_addrs);
	WRITE_ONCE(pernet->subflows_max, subflows);

unlock:
	spin_unlock_bh(&pernet->lock);
	return ret;
}

int mptcp_pm_nl_get_limits_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct sk_buff *msg;
	void *reply;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	reply = genlmsg_put_reply(msg, info, &mptcp_genl_family, 0,
				  MPTCP_PM_CMD_GET_LIMITS);
	if (!reply)
		goto fail;

	if (nla_put_u32(msg, MPTCP_PM_ATTR_RCV_ADD_ADDRS,
			READ_ONCE(pernet->add_addr_accept_max)))
		goto fail;

	if (nla_put_u32(msg, MPTCP_PM_ATTR_SUBFLOWS,
			READ_ONCE(pernet->subflows_max)))
		goto fail;

	genlmsg_end(msg, reply);
	return genlmsg_reply(msg, info);

fail:
	GENL_SET_ERR_MSG(info, "not enough space in Netlink message");
	nlmsg_free(msg);
	return -EMSGSIZE;
}

static void mptcp_pm_nl_fullmesh(struct mptcp_sock *msk,
				 struct mptcp_addr_info *addr)
{
	struct mptcp_rm_list list = { .nr = 0 };

	list.ids[list.nr++] = mptcp_endp_get_local_id(msk, addr);

	spin_lock_bh(&msk->pm.lock);
	mptcp_pm_rm_subflow(msk, &list);
	__mark_subflow_endp_available(msk, list.ids[0]);
	mptcp_pm_create_subflow_or_signal_addr(msk);
	spin_unlock_bh(&msk->pm.lock);
}

static void mptcp_pm_nl_set_flags_all(struct net *net,
				      struct mptcp_pm_addr_entry *local,
				      u8 changed)
{
	u8 is_subflow = !!(local->flags & MPTCP_PM_ADDR_FLAG_SUBFLOW);
	u8 bkup = !!(local->flags & MPTCP_PM_ADDR_FLAG_BACKUP);
	long s_slot = 0, s_num = 0;
	struct mptcp_sock *msk;

	if (changed == MPTCP_PM_ADDR_FLAG_FULLMESH && !is_subflow)
		return;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;

		if (list_empty(&msk->conn_list) || mptcp_pm_is_userspace(msk))
			goto next;

		lock_sock(sk);
		if (changed & MPTCP_PM_ADDR_FLAG_BACKUP)
			mptcp_pm_mp_prio_send_ack(msk, &local->addr, NULL, bkup);
		/* Subflows will only be recreated if the SUBFLOW flag is set */
		if (is_subflow && (changed & MPTCP_PM_ADDR_FLAG_FULLMESH))
			mptcp_pm_nl_fullmesh(msk, &local->addr);
		release_sock(sk);

next:
		sock_put(sk);
		cond_resched();
	}
}

int mptcp_pm_nl_set_flags(struct mptcp_pm_addr_entry *local,
			  struct genl_info *info)
{
	struct nlattr *attr = info->attrs[MPTCP_PM_ATTR_ADDR];
	u8 changed, mask = MPTCP_PM_ADDR_FLAG_BACKUP |
			   MPTCP_PM_ADDR_FLAG_FULLMESH;
	struct net *net = genl_info_net(info);
	struct mptcp_pm_addr_entry *entry;
	struct pm_nl_pernet *pernet;
	u8 lookup_by_id = 0;

	pernet = pm_nl_get_pernet(net);

	if (local->addr.family == AF_UNSPEC) {
		lookup_by_id = 1;
		if (!local->addr.id) {
			NL_SET_ERR_MSG_ATTR(info->extack, attr,
					    "missing address ID");
			return -EOPNOTSUPP;
		}
	}

	spin_lock_bh(&pernet->lock);
	entry = lookup_by_id ? __lookup_addr_by_id(pernet, local->addr.id) :
			       __lookup_addr(pernet, &local->addr);
	if (!entry) {
		spin_unlock_bh(&pernet->lock);
		NL_SET_ERR_MSG_ATTR(info->extack, attr, "address not found");
		return -EINVAL;
	}
	if ((local->flags & MPTCP_PM_ADDR_FLAG_FULLMESH) &&
	    (entry->flags & (MPTCP_PM_ADDR_FLAG_SIGNAL |
			     MPTCP_PM_ADDR_FLAG_IMPLICIT))) {
		spin_unlock_bh(&pernet->lock);
		NL_SET_ERR_MSG_ATTR(info->extack, attr, "invalid addr flags");
		return -EINVAL;
	}

	changed = (local->flags ^ entry->flags) & mask;
	entry->flags = (entry->flags & ~mask) | (local->flags & mask);
	*local = *entry;
	spin_unlock_bh(&pernet->lock);

	mptcp_pm_nl_set_flags_all(net, local, changed);
	return 0;
}

bool mptcp_pm_nl_check_work_pending(struct mptcp_sock *msk)
{
	struct pm_nl_pernet *pernet = pm_nl_get_pernet_from_msk(msk);

	if (msk->pm.subflows == mptcp_pm_get_subflows_max(msk) ||
	    (find_next_and_bit(pernet->id_bitmap, msk->pm.id_avail_bitmap,
			       MPTCP_PM_MAX_ADDR_ID + 1, 0) == MPTCP_PM_MAX_ADDR_ID + 1)) {
		WRITE_ONCE(msk->pm.work_pending, false);
		return false;
	}
	return true;
}

/* Called under PM lock */
void __mptcp_pm_kernel_worker(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;

	if (pm->status & BIT(MPTCP_PM_ADD_ADDR_RECEIVED)) {
		pm->status &= ~BIT(MPTCP_PM_ADD_ADDR_RECEIVED);
		mptcp_pm_nl_add_addr_received(msk);
	}
	if (pm->status & BIT(MPTCP_PM_ESTABLISHED)) {
		pm->status &= ~BIT(MPTCP_PM_ESTABLISHED);
		mptcp_pm_nl_fully_established(msk);
	}
	if (pm->status & BIT(MPTCP_PM_SUBFLOW_ESTABLISHED)) {
		pm->status &= ~BIT(MPTCP_PM_SUBFLOW_ESTABLISHED);
		mptcp_pm_nl_subflow_established(msk);
	}
}

static int __net_init pm_nl_init_net(struct net *net)
{
	struct pm_nl_pernet *pernet = pm_nl_get_pernet(net);

	INIT_LIST_HEAD_RCU(&pernet->local_addr_list);

	/* Cit. 2 subflows ought to be enough for anybody. */
	pernet->subflows_max = 2;
	pernet->next_id = 1;
	pernet->stale_loss_cnt = 4;
	spin_lock_init(&pernet->lock);

	/* No need to initialize other pernet fields, the struct is zeroed at
	 * allocation time.
	 */

	return 0;
}

static void __net_exit pm_nl_exit_net(struct list_head *net_list)
{
	struct net *net;

	list_for_each_entry(net, net_list, exit_list) {
		struct pm_nl_pernet *pernet = pm_nl_get_pernet(net);

		/* net is removed from namespace list, can't race with
		 * other modifiers, also netns core already waited for a
		 * RCU grace period.
		 */
		__flush_addrs(&pernet->local_addr_list);
	}
}

static struct pernet_operations mptcp_pm_pernet_ops = {
	.init = pm_nl_init_net,
	.exit_batch = pm_nl_exit_net,
	.id = &pm_nl_pernet_id,
	.size = sizeof(struct pm_nl_pernet),
};

struct mptcp_pm_ops mptcp_pm_kernel = {
	.name			= "kernel",
	.owner			= THIS_MODULE,
};

void __init mptcp_pm_kernel_register(void)
{
	if (register_pernet_subsys(&mptcp_pm_pernet_ops) < 0)
		panic("Failed to register MPTCP PM pernet subsystem.\n");

	mptcp_pm_register(&mptcp_pm_kernel);
}
