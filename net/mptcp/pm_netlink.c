// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2020, Red Hat, Inc.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/inet.h>
#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/inet_common.h>
#include <net/netns/generic.h>
#include <net/mptcp.h>
#include <net/genetlink.h>
#include <uapi/linux/mptcp.h>

#include "protocol.h"
#include "mib.h"

/* forward declaration */
static struct genl_family mptcp_genl_family;

static int pm_nl_pernet_id;

struct mptcp_pm_add_entry {
	struct list_head	list;
	struct mptcp_addr_info	addr;
	u8			retrans_times;
	struct timer_list	add_timer;
	struct mptcp_sock	*sock;
};

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
#define ADD_ADDR_RETRANS_MAX	3

static struct pm_nl_pernet *pm_nl_get_pernet(const struct net *net)
{
	return net_generic(net, pm_nl_pernet_id);
}

static struct pm_nl_pernet *
pm_nl_get_pernet_from_msk(const struct mptcp_sock *msk)
{
	return pm_nl_get_pernet(sock_net((struct sock *)msk));
}

bool mptcp_addresses_equal(const struct mptcp_addr_info *a,
			   const struct mptcp_addr_info *b, bool use_port)
{
	bool addr_equals = false;

	if (a->family == b->family) {
		if (a->family == AF_INET)
			addr_equals = a->addr.s_addr == b->addr.s_addr;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
		else
			addr_equals = !ipv6_addr_cmp(&a->addr6, &b->addr6);
	} else if (a->family == AF_INET) {
		if (ipv6_addr_v4mapped(&b->addr6))
			addr_equals = a->addr.s_addr == b->addr6.s6_addr32[3];
	} else if (b->family == AF_INET) {
		if (ipv6_addr_v4mapped(&a->addr6))
			addr_equals = a->addr6.s6_addr32[3] == b->addr.s_addr;
#endif
	}

	if (!addr_equals)
		return false;
	if (!use_port)
		return true;

	return a->port == b->port;
}

void mptcp_local_address(const struct sock_common *skc, struct mptcp_addr_info *addr)
{
	addr->family = skc->skc_family;
	addr->port = htons(skc->skc_num);
	if (addr->family == AF_INET)
		addr->addr.s_addr = skc->skc_rcv_saddr;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else if (addr->family == AF_INET6)
		addr->addr6 = skc->skc_v6_rcv_saddr;
#endif
}

static void remote_address(const struct sock_common *skc,
			   struct mptcp_addr_info *addr)
{
	addr->family = skc->skc_family;
	addr->port = skc->skc_dport;
	if (addr->family == AF_INET)
		addr->addr.s_addr = skc->skc_daddr;
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	else if (addr->family == AF_INET6)
		addr->addr6 = skc->skc_v6_daddr;
#endif
}

static bool lookup_subflow_by_saddr(const struct list_head *list,
				    const struct mptcp_addr_info *saddr)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_addr_info cur;
	struct sock_common *skc;

	list_for_each_entry(subflow, list, node) {
		skc = (struct sock_common *)mptcp_subflow_tcp_sock(subflow);

		mptcp_local_address(skc, &cur);
		if (mptcp_addresses_equal(&cur, saddr, saddr->port))
			return true;
	}

	return false;
}

static bool lookup_subflow_by_daddr(const struct list_head *list,
				    const struct mptcp_addr_info *daddr)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_addr_info cur;
	struct sock_common *skc;

	list_for_each_entry(subflow, list, node) {
		skc = (struct sock_common *)mptcp_subflow_tcp_sock(subflow);

		remote_address(skc, &cur);
		if (mptcp_addresses_equal(&cur, daddr, daddr->port))
			return true;
	}

	return false;
}

static struct mptcp_pm_addr_entry *
select_local_address(const struct pm_nl_pernet *pernet,
		     const struct mptcp_sock *msk)
{
	struct mptcp_pm_addr_entry *entry, *ret = NULL;

	msk_owned_by_me(msk);

	rcu_read_lock();
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (!(entry->flags & MPTCP_PM_ADDR_FLAG_SUBFLOW))
			continue;

		if (!test_bit(entry->addr.id, msk->pm.id_avail_bitmap))
			continue;

		ret = entry;
		break;
	}
	rcu_read_unlock();
	return ret;
}

static struct mptcp_pm_addr_entry *
select_signal_address(struct pm_nl_pernet *pernet, const struct mptcp_sock *msk)
{
	struct mptcp_pm_addr_entry *entry, *ret = NULL;

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

		ret = entry;
		break;
	}
	rcu_read_unlock();
	return ret;
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

struct mptcp_pm_add_entry *
mptcp_lookup_anno_list_by_saddr(const struct mptcp_sock *msk,
				const struct mptcp_addr_info *addr)
{
	struct mptcp_pm_add_entry *entry;

	lockdep_assert_held(&msk->pm.lock);

	list_for_each_entry(entry, &msk->pm.anno_list, list) {
		if (mptcp_addresses_equal(&entry->addr, addr, true))
			return entry;
	}

	return NULL;
}

bool mptcp_pm_sport_in_anno_list(struct mptcp_sock *msk, const struct sock *sk)
{
	struct mptcp_pm_add_entry *entry;
	struct mptcp_addr_info saddr;
	bool ret = false;

	mptcp_local_address((struct sock_common *)sk, &saddr);

	spin_lock_bh(&msk->pm.lock);
	list_for_each_entry(entry, &msk->pm.anno_list, list) {
		if (mptcp_addresses_equal(&entry->addr, &saddr, true)) {
			ret = true;
			goto out;
		}
	}

out:
	spin_unlock_bh(&msk->pm.lock);
	return ret;
}

static void mptcp_pm_add_timer(struct timer_list *timer)
{
	struct mptcp_pm_add_entry *entry = from_timer(entry, timer, add_timer);
	struct mptcp_sock *msk = entry->sock;
	struct sock *sk = (struct sock *)msk;

	pr_debug("msk=%p", msk);

	if (!msk)
		return;

	if (inet_sk_state_load(sk) == TCP_CLOSE)
		return;

	if (!entry->addr.id)
		return;

	if (mptcp_pm_should_add_signal_addr(msk)) {
		sk_reset_timer(sk, timer, jiffies + TCP_RTO_MAX / 8);
		goto out;
	}

	spin_lock_bh(&msk->pm.lock);

	if (!mptcp_pm_should_add_signal_addr(msk)) {
		pr_debug("retransmit ADD_ADDR id=%d", entry->addr.id);
		mptcp_pm_announce_addr(msk, &entry->addr, false);
		mptcp_pm_add_addr_send_ack(msk);
		entry->retrans_times++;
	}

	if (entry->retrans_times < ADD_ADDR_RETRANS_MAX)
		sk_reset_timer(sk, timer,
			       jiffies + mptcp_get_add_addr_timeout(sock_net(sk)));

	spin_unlock_bh(&msk->pm.lock);

	if (entry->retrans_times == ADD_ADDR_RETRANS_MAX)
		mptcp_pm_subflow_established(msk);

out:
	__sock_put(sk);
}

struct mptcp_pm_add_entry *
mptcp_pm_del_add_timer(struct mptcp_sock *msk,
		       const struct mptcp_addr_info *addr, bool check_id)
{
	struct mptcp_pm_add_entry *entry;
	struct sock *sk = (struct sock *)msk;

	spin_lock_bh(&msk->pm.lock);
	entry = mptcp_lookup_anno_list_by_saddr(msk, addr);
	if (entry && (!check_id || entry->addr.id == addr->id))
		entry->retrans_times = ADD_ADDR_RETRANS_MAX;
	spin_unlock_bh(&msk->pm.lock);

	if (entry && (!check_id || entry->addr.id == addr->id))
		sk_stop_timer_sync(sk, &entry->add_timer);

	return entry;
}

bool mptcp_pm_alloc_anno_list(struct mptcp_sock *msk,
			      const struct mptcp_addr_info *addr)
{
	struct mptcp_pm_add_entry *add_entry = NULL;
	struct sock *sk = (struct sock *)msk;
	struct net *net = sock_net(sk);

	lockdep_assert_held(&msk->pm.lock);

	add_entry = mptcp_lookup_anno_list_by_saddr(msk, addr);

	if (add_entry) {
		if (mptcp_pm_is_kernel(msk))
			return false;

		sk_reset_timer(sk, &add_entry->add_timer,
			       jiffies + mptcp_get_add_addr_timeout(net));
		return true;
	}

	add_entry = kmalloc(sizeof(*add_entry), GFP_ATOMIC);
	if (!add_entry)
		return false;

	list_add(&add_entry->list, &msk->pm.anno_list);

	add_entry->addr = *addr;
	add_entry->sock = msk;
	add_entry->retrans_times = 0;

	timer_setup(&add_entry->add_timer, mptcp_pm_add_timer, 0);
	sk_reset_timer(sk, &add_entry->add_timer,
		       jiffies + mptcp_get_add_addr_timeout(net));

	return true;
}

void mptcp_pm_free_anno_list(struct mptcp_sock *msk)
{
	struct mptcp_pm_add_entry *entry, *tmp;
	struct sock *sk = (struct sock *)msk;
	LIST_HEAD(free_list);

	pr_debug("msk=%p", msk);

	spin_lock_bh(&msk->pm.lock);
	list_splice_init(&msk->pm.anno_list, &free_list);
	spin_unlock_bh(&msk->pm.lock);

	list_for_each_entry_safe(entry, tmp, &free_list, list) {
		sk_stop_timer_sync(sk, &entry->add_timer);
		kfree(entry);
	}
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
	remote_address((struct sock_common *)sk, &remote);

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
			remote_address((struct sock_common *)ssk, &addrs[i]);
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

static void __mptcp_pm_send_ack(struct mptcp_sock *msk, struct mptcp_subflow_context *subflow,
				bool prio, bool backup)
{
	struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
	bool slow;

	pr_debug("send ack for %s",
		 prio ? "mp_prio" : (mptcp_pm_should_add_signal(msk) ? "add_addr" : "rm_addr"));

	slow = lock_sock_fast(ssk);
	if (prio) {
		subflow->send_mp_prio = 1;
		subflow->backup = backup;
		subflow->request_bkup = backup;
	}

	__mptcp_subflow_send_ack(ssk);
	unlock_sock_fast(ssk, slow);
}

static void mptcp_pm_send_ack(struct mptcp_sock *msk, struct mptcp_subflow_context *subflow,
			      bool prio, bool backup)
{
	spin_unlock_bh(&msk->pm.lock);
	__mptcp_pm_send_ack(msk, subflow, prio, backup);
	spin_lock_bh(&msk->pm.lock);
}

static struct mptcp_pm_addr_entry *
__lookup_addr_by_id(struct pm_nl_pernet *pernet, unsigned int id)
{
	struct mptcp_pm_addr_entry *entry;

	list_for_each_entry(entry, &pernet->local_addr_list, list) {
		if (entry->addr.id == id)
			return entry;
	}
	return NULL;
}

static struct mptcp_pm_addr_entry *
__lookup_addr(struct pm_nl_pernet *pernet, const struct mptcp_addr_info *info,
	      bool lookup_by_id)
{
	struct mptcp_pm_addr_entry *entry;

	list_for_each_entry(entry, &pernet->local_addr_list, list) {
		if ((!lookup_by_id &&
		     mptcp_addresses_equal(&entry->addr, info, entry->addr.port)) ||
		    (lookup_by_id && entry->addr.id == info->id))
			return entry;
	}
	return NULL;
}

static void mptcp_pm_create_subflow_or_signal_addr(struct mptcp_sock *msk)
{
	struct sock *sk = (struct sock *)msk;
	struct mptcp_pm_addr_entry *local;
	unsigned int add_addr_signal_max;
	unsigned int local_addr_max;
	struct pm_nl_pernet *pernet;
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
		entry = __lookup_addr(pernet, &mpc_addr, false);
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
		local = select_signal_address(pernet, msk);

		/* due to racing events on both ends we can reach here while
		 * previous add address is still running: if we invoke now
		 * mptcp_pm_announce_addr(), that will fail and the
		 * corresponding id will be marked as used.
		 * Instead let the PM machinery reschedule us when the
		 * current address announce will be completed.
		 */
		if (msk->pm.addr_signal & BIT(MPTCP_ADD_ADDR_SIGNAL))
			return;

		if (local) {
			if (mptcp_pm_alloc_anno_list(msk, &local->addr)) {
				__clear_bit(local->addr.id, msk->pm.id_avail_bitmap);
				msk->pm.add_addr_signaled++;
				mptcp_pm_announce_addr(msk, &local->addr, false);
				mptcp_pm_nl_addr_send_ack(msk);
			}
		}
	}

	/* check if should create a new subflow */
	while (msk->pm.local_addr_used < local_addr_max &&
	       msk->pm.subflows < subflows_max) {
		struct mptcp_addr_info addrs[MPTCP_PM_ADDR_MAX];
		bool fullmesh;
		int i, nr;

		local = select_local_address(pernet, msk);
		if (!local)
			break;

		fullmesh = !!(local->flags & MPTCP_PM_ADDR_FLAG_FULLMESH);

		msk->pm.local_addr_used++;
		__clear_bit(local->addr.id, msk->pm.id_avail_bitmap);
		nr = fill_remote_addresses_vec(msk, &local->addr, fullmesh, addrs);
		if (nr == 0)
			continue;

		spin_unlock_bh(&msk->pm.lock);
		for (i = 0; i < nr; i++)
			__mptcp_subflow_connect(sk, &local->addr, &addrs[i]);
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
					     struct mptcp_addr_info *addrs)
{
	struct sock *sk = (struct sock *)msk;
	struct mptcp_pm_addr_entry *entry;
	struct pm_nl_pernet *pernet;
	unsigned int subflows_max;
	int i = 0;

	pernet = pm_nl_get_pernet_from_msk(msk);
	subflows_max = mptcp_pm_get_subflows_max(msk);

	rcu_read_lock();
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (!(entry->flags & MPTCP_PM_ADDR_FLAG_FULLMESH))
			continue;

		if (!mptcp_pm_addr_families_match(sk, &entry->addr, remote))
			continue;

		if (msk->pm.subflows < subflows_max) {
			msk->pm.subflows++;
			addrs[i++] = entry->addr;
		}
	}
	rcu_read_unlock();

	/* If the array is empty, fill in the single
	 * 'IPADDRANY' local address
	 */
	if (!i) {
		struct mptcp_addr_info local;

		memset(&local, 0, sizeof(local));
		local.family =
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
			       remote->family == AF_INET6 &&
			       ipv6_addr_v4mapped(&remote->addr6) ? AF_INET :
#endif
			       remote->family;

		if (!mptcp_pm_addr_families_match(sk, &local, remote))
			return 0;

		msk->pm.subflows++;
		addrs[i++] = local;
	}

	return i;
}

static void mptcp_pm_nl_add_addr_received(struct mptcp_sock *msk)
{
	struct mptcp_addr_info addrs[MPTCP_PM_ADDR_MAX];
	struct sock *sk = (struct sock *)msk;
	unsigned int add_addr_accept_max;
	struct mptcp_addr_info remote;
	unsigned int subflows_max;
	int i, nr;

	add_addr_accept_max = mptcp_pm_get_add_addr_accept_max(msk);
	subflows_max = mptcp_pm_get_subflows_max(msk);

	pr_debug("accepted %d:%d remote family %d",
		 msk->pm.add_addr_accepted, add_addr_accept_max,
		 msk->pm.remote.family);

	remote = msk->pm.remote;
	mptcp_pm_announce_addr(msk, &remote, true);
	mptcp_pm_nl_addr_send_ack(msk);

	if (lookup_subflow_by_daddr(&msk->conn_list, &remote))
		return;

	/* pick id 0 port, if none is provided the remote address */
	if (!remote.port)
		remote.port = sk->sk_dport;

	/* connect to the specified remote address, using whatever
	 * local address the routing configuration will pick.
	 */
	nr = fill_local_addresses_vec(msk, &remote, addrs);
	if (nr == 0)
		return;

	msk->pm.add_addr_accepted++;
	if (msk->pm.add_addr_accepted >= add_addr_accept_max ||
	    msk->pm.subflows >= subflows_max)
		WRITE_ONCE(msk->pm.accept_addr, false);

	spin_unlock_bh(&msk->pm.lock);
	for (i = 0; i < nr; i++)
		__mptcp_subflow_connect(sk, &addrs[i], &remote);
	spin_lock_bh(&msk->pm.lock);
}

void mptcp_pm_nl_addr_send_ack(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;

	msk_owned_by_me(msk);
	lockdep_assert_held(&msk->pm.lock);

	if (!mptcp_pm_should_add_signal(msk) &&
	    !mptcp_pm_should_rm_signal(msk))
		return;

	subflow = list_first_entry_or_null(&msk->conn_list, typeof(*subflow), node);
	if (subflow)
		mptcp_pm_send_ack(msk, subflow, false, false);
}

int mptcp_pm_nl_mp_prio_send_ack(struct mptcp_sock *msk,
				 struct mptcp_addr_info *addr,
				 struct mptcp_addr_info *rem,
				 u8 bkup)
{
	struct mptcp_subflow_context *subflow;

	pr_debug("bkup=%d", bkup);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		struct mptcp_addr_info local, remote;

		mptcp_local_address((struct sock_common *)ssk, &local);
		if (!mptcp_addresses_equal(&local, addr, addr->port))
			continue;

		if (rem && rem->family != AF_UNSPEC) {
			remote_address((struct sock_common *)ssk, &remote);
			if (!mptcp_addresses_equal(&remote, rem, rem->port))
				continue;
		}

		__mptcp_pm_send_ack(msk, subflow, true, bkup);
		return 0;
	}

	return -EINVAL;
}

static bool mptcp_local_id_match(const struct mptcp_sock *msk, u8 local_id, u8 id)
{
	return local_id == id || (!local_id && msk->mpc_endpoint_id == id);
}

static void mptcp_pm_nl_rm_addr_or_subflow(struct mptcp_sock *msk,
					   const struct mptcp_rm_list *rm_list,
					   enum linux_mptcp_mib_field rm_type)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct sock *sk = (struct sock *)msk;
	u8 i;

	pr_debug("%s rm_list_nr %d",
		 rm_type == MPTCP_MIB_RMADDR ? "address" : "subflow", rm_list->nr);

	msk_owned_by_me(msk);

	if (sk->sk_state == TCP_LISTEN)
		return;

	if (!rm_list->nr)
		return;

	if (list_empty(&msk->conn_list))
		return;

	for (i = 0; i < rm_list->nr; i++) {
		u8 rm_id = rm_list->ids[i];
		bool removed = false;

		mptcp_for_each_subflow_safe(msk, subflow, tmp) {
			struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
			u8 remote_id = READ_ONCE(subflow->remote_id);
			int how = RCV_SHUTDOWN | SEND_SHUTDOWN;
			u8 id = subflow_get_local_id(subflow);

			if (rm_type == MPTCP_MIB_RMADDR && remote_id != rm_id)
				continue;
			if (rm_type == MPTCP_MIB_RMSUBFLOW && !mptcp_local_id_match(msk, id, rm_id))
				continue;

			pr_debug(" -> %s rm_list_ids[%d]=%u local_id=%u remote_id=%u mpc_id=%u",
				 rm_type == MPTCP_MIB_RMADDR ? "address" : "subflow",
				 i, rm_id, id, remote_id, msk->mpc_endpoint_id);
			spin_unlock_bh(&msk->pm.lock);
			mptcp_subflow_shutdown(sk, ssk, how);

			/* the following takes care of updating the subflows counter */
			mptcp_close_ssk(sk, ssk, subflow);
			spin_lock_bh(&msk->pm.lock);

			removed = true;
			__MPTCP_INC_STATS(sock_net(sk), rm_type);
		}
		if (rm_type == MPTCP_MIB_RMSUBFLOW)
			__set_bit(rm_id ? rm_id : msk->mpc_endpoint_id, msk->pm.id_avail_bitmap);
		if (!removed)
			continue;

		if (!mptcp_pm_is_kernel(msk))
			continue;

		if (rm_type == MPTCP_MIB_RMADDR) {
			msk->pm.add_addr_accepted--;
			WRITE_ONCE(msk->pm.accept_addr, true);
		} else if (rm_type == MPTCP_MIB_RMSUBFLOW) {
			msk->pm.local_addr_used--;
		}
	}
}

static void mptcp_pm_nl_rm_addr_received(struct mptcp_sock *msk)
{
	mptcp_pm_nl_rm_addr_or_subflow(msk, &msk->pm.rm_list_rx, MPTCP_MIB_RMADDR);
}

void mptcp_pm_nl_rm_subflow_received(struct mptcp_sock *msk,
				     const struct mptcp_rm_list *rm_list)
{
	mptcp_pm_nl_rm_addr_or_subflow(msk, rm_list, MPTCP_MIB_RMSUBFLOW);
}

void mptcp_pm_nl_work(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;

	msk_owned_by_me(msk);

	if (!(pm->status & MPTCP_PM_WORK_MASK))
		return;

	spin_lock_bh(&msk->pm.lock);

	pr_debug("msk=%p status=%x", msk, pm->status);
	if (pm->status & BIT(MPTCP_PM_ADD_ADDR_RECEIVED)) {
		pm->status &= ~BIT(MPTCP_PM_ADD_ADDR_RECEIVED);
		mptcp_pm_nl_add_addr_received(msk);
	}
	if (pm->status & BIT(MPTCP_PM_ADD_ADDR_SEND_ACK)) {
		pm->status &= ~BIT(MPTCP_PM_ADD_ADDR_SEND_ACK);
		mptcp_pm_nl_addr_send_ack(msk);
	}
	if (pm->status & BIT(MPTCP_PM_RM_ADDR_RECEIVED)) {
		pm->status &= ~BIT(MPTCP_PM_RM_ADDR_RECEIVED);
		mptcp_pm_nl_rm_addr_received(msk);
	}
	if (pm->status & BIT(MPTCP_PM_ESTABLISHED)) {
		pm->status &= ~BIT(MPTCP_PM_ESTABLISHED);
		mptcp_pm_nl_fully_established(msk);
	}
	if (pm->status & BIT(MPTCP_PM_SUBFLOW_ESTABLISHED)) {
		pm->status &= ~BIT(MPTCP_PM_SUBFLOW_ESTABLISHED);
		mptcp_pm_nl_subflow_established(msk);
	}

	spin_unlock_bh(&msk->pm.lock);
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
					     bool needs_id)
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
	err = __inet_listen_sk(ssk, backlog);
	if (!err)
		mptcp_event_pm_listener(ssk, MPTCP_EVENT_LISTENER_CREATED);
	release_sock(ssk);
	return err;
}

int mptcp_pm_nl_get_local_id(struct mptcp_sock *msk, struct mptcp_addr_info *skc)
{
	struct mptcp_pm_addr_entry *entry;
	struct pm_nl_pernet *pernet;
	int ret = -1;

	pernet = pm_nl_get_pernet_from_msk(msk);

	rcu_read_lock();
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (mptcp_addresses_equal(&entry->addr, skc, entry->addr.port)) {
			ret = entry->addr.id;
			break;
		}
	}
	rcu_read_unlock();
	if (ret >= 0)
		return ret;

	/* address not found, add to local list */
	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return -ENOMEM;

	entry->addr = *skc;
	entry->addr.id = 0;
	entry->addr.port = 0;
	entry->ifindex = 0;
	entry->flags = MPTCP_PM_ADDR_FLAG_IMPLICIT;
	entry->lsk = NULL;
	ret = mptcp_pm_nl_append_new_local_addr(pernet, entry, true);
	if (ret < 0)
		kfree(entry);

	return ret;
}

#define MPTCP_PM_CMD_GRP_OFFSET       0
#define MPTCP_PM_EV_GRP_OFFSET        1

static const struct genl_multicast_group mptcp_pm_mcgrps[] = {
	[MPTCP_PM_CMD_GRP_OFFSET]	= { .name = MPTCP_PM_CMD_GRP_NAME, },
	[MPTCP_PM_EV_GRP_OFFSET]        = { .name = MPTCP_PM_EV_GRP_NAME,
					    .flags = GENL_MCAST_CAP_NET_ADMIN,
					  },
};

void mptcp_pm_nl_subflow_chk_stale(const struct mptcp_sock *msk, struct sock *ssk)
{
	struct mptcp_subflow_context *iter, *subflow = mptcp_subflow_ctx(ssk);
	struct sock *sk = (struct sock *)msk;
	unsigned int active_max_loss_cnt;
	struct net *net = sock_net(sk);
	unsigned int stale_loss_cnt;
	bool slow;

	stale_loss_cnt = mptcp_stale_loss_cnt(net);
	if (subflow->stale || !stale_loss_cnt || subflow->stale_count <= stale_loss_cnt)
		return;

	/* look for another available subflow not in loss state */
	active_max_loss_cnt = max_t(int, stale_loss_cnt - 1, 1);
	mptcp_for_each_subflow(msk, iter) {
		if (iter != subflow && mptcp_subflow_active(iter) &&
		    iter->stale_count < active_max_loss_cnt) {
			/* we have some alternatives, try to mark this subflow as idle ...*/
			slow = lock_sock_fast(ssk);
			if (!tcp_rtx_and_write_queues_empty(ssk)) {
				subflow->stale = 1;
				__mptcp_retransmit_pending_data(sk);
				MPTCP_INC_STATS(net, MPTCP_MIB_SUBFLOWSTALE);
			}
			unlock_sock_fast(ssk, slow);

			/* always try to push the pending data regardless of re-injections:
			 * we can possibly use backup subflows now, and subflow selection
			 * is cheap under the msk socket lock
			 */
			__mptcp_push_pending(sk, 0);
			return;
		}
	}
}

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

static struct pm_nl_pernet *genl_info_pm_nl(struct genl_info *info)
{
	return pm_nl_get_pernet(genl_info_net(info));
}

static int mptcp_nl_add_subflow_or_signal_addr(struct net *net)
{
	struct mptcp_sock *msk;
	long s_slot = 0, s_num = 0;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;

		if (!READ_ONCE(msk->fully_established) ||
		    mptcp_pm_is_userspace(msk))
			goto next;

		lock_sock(sk);
		spin_lock_bh(&msk->pm.lock);
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

int mptcp_pm_nl_add_addr_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attr = info->attrs[MPTCP_PM_ENDPOINT_ADDR];
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry addr, *entry;
	int ret;

	ret = mptcp_pm_parse_entry(attr, info, true, &addr);
	if (ret < 0)
		return ret;

	if (addr.addr.port && !(addr.flags & MPTCP_PM_ADDR_FLAG_SIGNAL)) {
		GENL_SET_ERR_MSG(info, "flags must have signal when using port");
		return -EINVAL;
	}

	if (addr.flags & MPTCP_PM_ADDR_FLAG_SIGNAL &&
	    addr.flags & MPTCP_PM_ADDR_FLAG_FULLMESH) {
		GENL_SET_ERR_MSG(info, "flags mustn't have both signal and fullmesh");
		return -EINVAL;
	}

	if (addr.flags & MPTCP_PM_ADDR_FLAG_IMPLICIT) {
		GENL_SET_ERR_MSG(info, "can't create IMPLICIT endpoint");
		return -EINVAL;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL_ACCOUNT);
	if (!entry) {
		GENL_SET_ERR_MSG(info, "can't allocate addr");
		return -ENOMEM;
	}

	*entry = addr;
	if (entry->addr.port) {
		ret = mptcp_pm_nl_create_listen_socket(skb->sk, entry);
		if (ret) {
			GENL_SET_ERR_MSG_FMT(info, "create listen socket error: %d", ret);
			goto out_free;
		}
	}
	ret = mptcp_pm_nl_append_new_local_addr(pernet, entry,
						!mptcp_pm_has_addr_attr_id(attr, info));
	if (ret < 0) {
		GENL_SET_ERR_MSG_FMT(info, "too many addresses or duplicate one: %d", ret);
		goto out_free;
	}

	mptcp_nl_add_subflow_or_signal_addr(sock_net(skb->sk));
	return 0;

out_free:
	__mptcp_pm_release_addr_entry(entry);
	return ret;
}

int mptcp_pm_nl_get_flags_and_ifindex_by_id(struct mptcp_sock *msk, unsigned int id,
					    u8 *flags, int *ifindex)
{
	struct mptcp_pm_addr_entry *entry;
	struct sock *sk = (struct sock *)msk;
	struct net *net = sock_net(sk);

	rcu_read_lock();
	entry = __lookup_addr_by_id(pm_nl_get_pernet(net), id);
	if (entry) {
		*flags = entry->flags;
		*ifindex = entry->ifindex;
	}
	rcu_read_unlock();

	return 0;
}

static bool remove_anno_list_by_saddr(struct mptcp_sock *msk,
				      const struct mptcp_addr_info *addr)
{
	struct mptcp_pm_add_entry *entry;

	entry = mptcp_pm_del_add_timer(msk, addr, false);
	if (entry) {
		list_del(&entry->list);
		kfree(entry);
		return true;
	}

	return false;
}

static bool mptcp_pm_remove_anno_addr(struct mptcp_sock *msk,
				      const struct mptcp_addr_info *addr,
				      bool force)
{
	struct mptcp_rm_list list = { .nr = 0 };
	bool ret;

	list.ids[list.nr++] = addr->id;

	ret = remove_anno_list_by_saddr(msk, addr);
	if (ret || force) {
		spin_lock_bh(&msk->pm.lock);
		mptcp_pm_remove_addr(msk, &list);
		spin_unlock_bh(&msk->pm.lock);
	}
	return ret;
}

static int mptcp_nl_remove_subflow_and_signal_addr(struct net *net,
						   const struct mptcp_pm_addr_entry *entry)
{
	const struct mptcp_addr_info *addr = &entry->addr;
	struct mptcp_rm_list list = { .nr = 0 };
	long s_slot = 0, s_num = 0;
	struct mptcp_sock *msk;

	pr_debug("remove_id=%d", addr->id);

	list.ids[list.nr++] = addr->id;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;
		bool remove_subflow;

		if (mptcp_pm_is_userspace(msk))
			goto next;

		if (list_empty(&msk->conn_list)) {
			mptcp_pm_remove_anno_addr(msk, addr, false);
			goto next;
		}

		lock_sock(sk);
		remove_subflow = lookup_subflow_by_saddr(&msk->conn_list, addr);
		mptcp_pm_remove_anno_addr(msk, addr, remove_subflow &&
					  !(entry->flags & MPTCP_PM_ADDR_FLAG_IMPLICIT));
		if (remove_subflow)
			mptcp_pm_remove_subflow(msk, &list);
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
		mptcp_pm_nl_rm_subflow_received(msk, &list);
		spin_unlock_bh(&msk->pm.lock);
		release_sock(sk);

next:
		sock_put(sk);
		cond_resched();
	}

	return 0;
}

int mptcp_pm_nl_del_addr_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attr = info->attrs[MPTCP_PM_ENDPOINT_ADDR];
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry addr, *entry;
	unsigned int addr_max;
	int ret;

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
		GENL_SET_ERR_MSG(info, "address not found");
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

void mptcp_pm_remove_addrs(struct mptcp_sock *msk, struct list_head *rm_list)
{
	struct mptcp_rm_list alist = { .nr = 0 };
	struct mptcp_pm_addr_entry *entry;

	list_for_each_entry(entry, rm_list, list) {
		if ((remove_anno_list_by_saddr(msk, &entry->addr) ||
		     lookup_subflow_by_saddr(&msk->conn_list, &entry->addr)) &&
		    alist.nr < MPTCP_RM_IDS_MAX)
			alist.ids[alist.nr++] = entry->addr.id;
	}

	if (alist.nr) {
		spin_lock_bh(&msk->pm.lock);
		mptcp_pm_remove_addr(msk, &alist);
		spin_unlock_bh(&msk->pm.lock);
	}
}

void mptcp_pm_remove_addrs_and_subflows(struct mptcp_sock *msk,
					struct list_head *rm_list)
{
	struct mptcp_rm_list alist = { .nr = 0 }, slist = { .nr = 0 };
	struct mptcp_pm_addr_entry *entry;

	list_for_each_entry(entry, rm_list, list) {
		if (lookup_subflow_by_saddr(&msk->conn_list, &entry->addr) &&
		    slist.nr < MPTCP_RM_IDS_MAX)
			slist.ids[slist.nr++] = entry->addr.id;

		if (remove_anno_list_by_saddr(msk, &entry->addr) &&
		    alist.nr < MPTCP_RM_IDS_MAX)
			alist.ids[alist.nr++] = entry->addr.id;
	}

	if (alist.nr) {
		spin_lock_bh(&msk->pm.lock);
		mptcp_pm_remove_addr(msk, &alist);
		spin_unlock_bh(&msk->pm.lock);
	}
	if (slist.nr)
		mptcp_pm_remove_subflow(msk, &slist);
}

static void mptcp_nl_remove_addrs_list(struct net *net,
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
			mptcp_pm_remove_addrs_and_subflows(msk, rm_list);
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
	mptcp_nl_remove_addrs_list(sock_net(skb->sk), &free_list);
	synchronize_rcu();
	__flush_addrs(&free_list);
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

int mptcp_pm_nl_get_addr_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attr = info->attrs[MPTCP_PM_ENDPOINT_ADDR];
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry addr, *entry;
	struct sk_buff *msg;
	void *reply;
	int ret;

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

	spin_lock_bh(&pernet->lock);
	entry = __lookup_addr_by_id(pernet, addr.addr.id);
	if (!entry) {
		GENL_SET_ERR_MSG(info, "address not found");
		ret = -EINVAL;
		goto unlock_fail;
	}

	ret = mptcp_nl_fill_addr(msg, entry);
	if (ret)
		goto unlock_fail;

	genlmsg_end(msg, reply);
	ret = genlmsg_reply(msg, info);
	spin_unlock_bh(&pernet->lock);
	return ret;

unlock_fail:
	spin_unlock_bh(&pernet->lock);

fail:
	nlmsg_free(msg);
	return ret;
}

int mptcp_pm_nl_get_addr_dumpit(struct sk_buff *msg,
				struct netlink_callback *cb)
{
	struct net *net = sock_net(msg->sk);
	struct mptcp_pm_addr_entry *entry;
	struct pm_nl_pernet *pernet;
	int id = cb->args[0];
	void *hdr;
	int i;

	pernet = pm_nl_get_pernet(net);

	spin_lock_bh(&pernet->lock);
	for (i = id; i < MPTCP_PM_MAX_ADDR_ID + 1; i++) {
		if (test_bit(i, pernet->id_bitmap)) {
			entry = __lookup_addr_by_id(pernet, i);
			if (!entry)
				break;

			if (entry->addr.id <= id)
				continue;

			hdr = genlmsg_put(msg, NETLINK_CB(cb->skb).portid,
					  cb->nlh->nlmsg_seq, &mptcp_genl_family,
					  NLM_F_MULTI, MPTCP_PM_CMD_GET_ADDR);
			if (!hdr)
				break;

			if (mptcp_nl_fill_addr(msg, entry) < 0) {
				genlmsg_cancel(msg, hdr);
				break;
			}

			id = entry->addr.id;
			genlmsg_end(msg, hdr);
		}
	}
	spin_unlock_bh(&pernet->lock);

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
		GENL_SET_ERR_MSG(info, "limit greater than maximum");
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

	list.ids[list.nr++] = addr->id;

	spin_lock_bh(&msk->pm.lock);
	mptcp_pm_nl_rm_subflow_received(msk, &list);
	mptcp_pm_create_subflow_or_signal_addr(msk);
	spin_unlock_bh(&msk->pm.lock);
}

static int mptcp_nl_set_flags(struct net *net,
			      struct mptcp_addr_info *addr,
			      u8 bkup, u8 changed)
{
	long s_slot = 0, s_num = 0;
	struct mptcp_sock *msk;
	int ret = -EINVAL;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;

		if (list_empty(&msk->conn_list) || mptcp_pm_is_userspace(msk))
			goto next;

		lock_sock(sk);
		if (changed & MPTCP_PM_ADDR_FLAG_BACKUP)
			ret = mptcp_pm_nl_mp_prio_send_ack(msk, addr, NULL, bkup);
		if (changed & MPTCP_PM_ADDR_FLAG_FULLMESH)
			mptcp_pm_nl_fullmesh(msk, addr);
		release_sock(sk);

next:
		sock_put(sk);
		cond_resched();
	}

	return ret;
}

int mptcp_pm_nl_set_flags(struct net *net, struct mptcp_pm_addr_entry *addr, u8 bkup)
{
	struct pm_nl_pernet *pernet = pm_nl_get_pernet(net);
	u8 changed, mask = MPTCP_PM_ADDR_FLAG_BACKUP |
			   MPTCP_PM_ADDR_FLAG_FULLMESH;
	struct mptcp_pm_addr_entry *entry;
	u8 lookup_by_id = 0;

	if (addr->addr.family == AF_UNSPEC) {
		lookup_by_id = 1;
		if (!addr->addr.id)
			return -EOPNOTSUPP;
	}

	spin_lock_bh(&pernet->lock);
	entry = __lookup_addr(pernet, &addr->addr, lookup_by_id);
	if (!entry) {
		spin_unlock_bh(&pernet->lock);
		return -EINVAL;
	}
	if ((addr->flags & MPTCP_PM_ADDR_FLAG_FULLMESH) &&
	    (entry->flags & MPTCP_PM_ADDR_FLAG_SIGNAL)) {
		spin_unlock_bh(&pernet->lock);
		return -EINVAL;
	}

	changed = (addr->flags ^ entry->flags) & mask;
	entry->flags = (entry->flags & ~mask) | (addr->flags & mask);
	*addr = *entry;
	spin_unlock_bh(&pernet->lock);

	mptcp_nl_set_flags(net, &addr->addr, bkup, changed);
	return 0;
}

int mptcp_pm_nl_set_flags_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct mptcp_pm_addr_entry remote = { .addr = { .family = AF_UNSPEC }, };
	struct mptcp_pm_addr_entry addr = { .addr = { .family = AF_UNSPEC }, };
	struct nlattr *attr_rem = info->attrs[MPTCP_PM_ATTR_ADDR_REMOTE];
	struct nlattr *token = info->attrs[MPTCP_PM_ATTR_TOKEN];
	struct nlattr *attr = info->attrs[MPTCP_PM_ATTR_ADDR];
	struct net *net = sock_net(skb->sk);
	u8 bkup = 0;
	int ret;

	ret = mptcp_pm_parse_entry(attr, info, false, &addr);
	if (ret < 0)
		return ret;

	if (attr_rem) {
		ret = mptcp_pm_parse_entry(attr_rem, info, false, &remote);
		if (ret < 0)
			return ret;
	}

	if (addr.flags & MPTCP_PM_ADDR_FLAG_BACKUP)
		bkup = 1;

	return mptcp_pm_set_flags(net, token, &addr, &remote, bkup);
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
		const struct ipv6_pinfo *np = inet6_sk(ssk);

		if (nla_put_in6_addr(skb, MPTCP_ATTR_SADDR6, &np->saddr))
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

	if (nla_put_u32(skb, MPTCP_ATTR_TOKEN, msk->token))
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
	int err = nla_put_u32(skb, MPTCP_ATTR_TOKEN, msk->token);

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

	if (nla_put_u32(skb, MPTCP_ATTR_TOKEN, msk->token))
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

	if (nla_put_u32(skb, MPTCP_ATTR_TOKEN, msk->token))
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
		const struct ipv6_pinfo *np = inet6_sk(ssk);

		if (nla_put_in6_addr(skb, MPTCP_ATTR_SADDR6, &np->saddr))
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
		if (nla_put_u32(skb, MPTCP_ATTR_TOKEN, msk->token) < 0)
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

static struct genl_family mptcp_genl_family __ro_after_init = {
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

void __init mptcp_pm_nl_init(void)
{
	if (register_pernet_subsys(&mptcp_pm_pernet_ops) < 0)
		panic("Failed to register MPTCP PM pernet subsystem.\n");

	if (genl_register_family(&mptcp_genl_family))
		panic("Failed to register MPTCP PM netlink family\n");
}
