// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2020, Red Hat, Inc.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/inet.h>
#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/netns/generic.h>
#include <net/mptcp.h>
#include <net/genetlink.h>
#include <uapi/linux/mptcp.h>

#include "protocol.h"
#include "mib.h"

/* forward declaration */
static struct genl_family mptcp_genl_family;

static int pm_nl_pernet_id;

struct mptcp_pm_addr_entry {
	struct list_head	list;
	struct mptcp_addr_info	addr;
	u8			flags;
	int			ifindex;
	struct socket		*lsk;
};

struct mptcp_pm_add_entry {
	struct list_head	list;
	struct mptcp_addr_info	addr;
	struct timer_list	add_timer;
	struct mptcp_sock	*sock;
	u8			retrans_times;
};

#define MAX_ADDR_ID		255
#define BITMAP_SZ DIV_ROUND_UP(MAX_ADDR_ID + 1, BITS_PER_LONG)

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
	unsigned long		id_bitmap[BITMAP_SZ];
};

#define MPTCP_PM_ADDR_MAX	8
#define ADD_ADDR_RETRANS_MAX	3

static bool addresses_equal(const struct mptcp_addr_info *a,
			    struct mptcp_addr_info *b, bool use_port)
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

static bool address_zero(const struct mptcp_addr_info *addr)
{
	struct mptcp_addr_info zero;

	memset(&zero, 0, sizeof(zero));
	zero.family = addr->family;

	return addresses_equal(addr, &zero, true);
}

static void local_address(const struct sock_common *skc,
			  struct mptcp_addr_info *addr)
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
				    struct mptcp_addr_info *saddr)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_addr_info cur;
	struct sock_common *skc;

	list_for_each_entry(subflow, list, node) {
		skc = (struct sock_common *)mptcp_subflow_tcp_sock(subflow);

		local_address(skc, &cur);
		if (addresses_equal(&cur, saddr, saddr->port))
			return true;
	}

	return false;
}

static bool lookup_subflow_by_daddr(const struct list_head *list,
				    struct mptcp_addr_info *daddr)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_addr_info cur;
	struct sock_common *skc;

	list_for_each_entry(subflow, list, node) {
		skc = (struct sock_common *)mptcp_subflow_tcp_sock(subflow);

		remote_address(skc, &cur);
		if (addresses_equal(&cur, daddr, daddr->port))
			return true;
	}

	return false;
}

static struct mptcp_pm_addr_entry *
select_local_address(const struct pm_nl_pernet *pernet,
		     struct mptcp_sock *msk)
{
	struct mptcp_pm_addr_entry *entry, *ret = NULL;
	struct sock *sk = (struct sock *)msk;

	msk_owned_by_me(msk);

	rcu_read_lock();
	__mptcp_flush_join_list(msk);
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (!(entry->flags & MPTCP_PM_ADDR_FLAG_SUBFLOW))
			continue;

		if (entry->addr.family != sk->sk_family) {
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
			if ((entry->addr.family == AF_INET &&
			     !ipv6_addr_v4mapped(&sk->sk_v6_daddr)) ||
			    (sk->sk_family == AF_INET &&
			     !ipv6_addr_v4mapped(&entry->addr.addr6)))
#endif
				continue;
		}

		/* avoid any address already in use by subflows and
		 * pending join
		 */
		if (!lookup_subflow_by_saddr(&msk->conn_list, &entry->addr)) {
			ret = entry;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

static struct mptcp_pm_addr_entry *
select_signal_address(struct pm_nl_pernet *pernet, unsigned int pos)
{
	struct mptcp_pm_addr_entry *entry, *ret = NULL;
	int i = 0;

	rcu_read_lock();
	/* do not keep any additional per socket state, just signal
	 * the address list in order.
	 * Note: removal from the local address list during the msk life-cycle
	 * can lead to additional addresses not being announced.
	 */
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (!(entry->flags & MPTCP_PM_ADDR_FLAG_SIGNAL))
			continue;
		if (i++ == pos) {
			ret = entry;
			break;
		}
	}
	rcu_read_unlock();
	return ret;
}

unsigned int mptcp_pm_get_add_addr_signal_max(struct mptcp_sock *msk)
{
	struct pm_nl_pernet *pernet;

	pernet = net_generic(sock_net((struct sock *)msk), pm_nl_pernet_id);
	return READ_ONCE(pernet->add_addr_signal_max);
}
EXPORT_SYMBOL_GPL(mptcp_pm_get_add_addr_signal_max);

unsigned int mptcp_pm_get_add_addr_accept_max(struct mptcp_sock *msk)
{
	struct pm_nl_pernet *pernet;

	pernet = net_generic(sock_net((struct sock *)msk), pm_nl_pernet_id);
	return READ_ONCE(pernet->add_addr_accept_max);
}
EXPORT_SYMBOL_GPL(mptcp_pm_get_add_addr_accept_max);

unsigned int mptcp_pm_get_subflows_max(struct mptcp_sock *msk)
{
	struct pm_nl_pernet *pernet;

	pernet = net_generic(sock_net((struct sock *)msk), pm_nl_pernet_id);
	return READ_ONCE(pernet->subflows_max);
}
EXPORT_SYMBOL_GPL(mptcp_pm_get_subflows_max);

unsigned int mptcp_pm_get_local_addr_max(struct mptcp_sock *msk)
{
	struct pm_nl_pernet *pernet;

	pernet = net_generic(sock_net((struct sock *)msk), pm_nl_pernet_id);
	return READ_ONCE(pernet->local_addr_max);
}
EXPORT_SYMBOL_GPL(mptcp_pm_get_local_addr_max);

static void check_work_pending(struct mptcp_sock *msk)
{
	if (msk->pm.add_addr_signaled == mptcp_pm_get_add_addr_signal_max(msk) &&
	    (msk->pm.local_addr_used == mptcp_pm_get_local_addr_max(msk) ||
	     msk->pm.subflows == mptcp_pm_get_subflows_max(msk)))
		WRITE_ONCE(msk->pm.work_pending, false);
}

struct mptcp_pm_add_entry *
mptcp_lookup_anno_list_by_saddr(struct mptcp_sock *msk,
				struct mptcp_addr_info *addr)
{
	struct mptcp_pm_add_entry *entry;

	lockdep_assert_held(&msk->pm.lock);

	list_for_each_entry(entry, &msk->pm.anno_list, list) {
		if (addresses_equal(&entry->addr, addr, true))
			return entry;
	}

	return NULL;
}

bool mptcp_pm_sport_in_anno_list(struct mptcp_sock *msk, const struct sock *sk)
{
	struct mptcp_pm_add_entry *entry;
	struct mptcp_addr_info saddr;
	bool ret = false;

	local_address((struct sock_common *)sk, &saddr);

	spin_lock_bh(&msk->pm.lock);
	list_for_each_entry(entry, &msk->pm.anno_list, list) {
		if (addresses_equal(&entry->addr, &saddr, true)) {
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
		       struct mptcp_addr_info *addr, bool check_id)
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

static bool mptcp_pm_alloc_anno_list(struct mptcp_sock *msk,
				     struct mptcp_pm_addr_entry *entry)
{
	struct mptcp_pm_add_entry *add_entry = NULL;
	struct sock *sk = (struct sock *)msk;
	struct net *net = sock_net(sk);

	lockdep_assert_held(&msk->pm.lock);

	if (mptcp_lookup_anno_list_by_saddr(msk, &entry->addr))
		return false;

	add_entry = kmalloc(sizeof(*add_entry), GFP_ATOMIC);
	if (!add_entry)
		return false;

	list_add(&add_entry->list, &msk->pm.anno_list);

	add_entry->addr = entry->addr;
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

static bool lookup_address_in_vec(struct mptcp_addr_info *addrs, unsigned int nr,
				  struct mptcp_addr_info *addr)
{
	int i;

	for (i = 0; i < nr; i++) {
		if (addresses_equal(&addrs[i], addr, addr->port))
			return true;
	}

	return false;
}

/* Fill all the remote addresses into the array addrs[],
 * and return the array size.
 */
static unsigned int fill_remote_addresses_vec(struct mptcp_sock *msk, bool fullmesh,
					      struct mptcp_addr_info *addrs)
{
	struct sock *sk = (struct sock *)msk, *ssk;
	struct mptcp_subflow_context *subflow;
	struct mptcp_addr_info remote = { 0 };
	unsigned int subflows_max;
	int i = 0;

	subflows_max = mptcp_pm_get_subflows_max(msk);

	/* Non-fullmesh endpoint, fill in the single entry
	 * corresponding to the primary MPC subflow remote address
	 */
	if (!fullmesh) {
		remote_address((struct sock_common *)sk, &remote);
		msk->pm.subflows++;
		addrs[i++] = remote;
	} else {
		mptcp_for_each_subflow(msk, subflow) {
			ssk = mptcp_subflow_tcp_sock(subflow);
			remote_address((struct sock_common *)ssk, &remote);
			if (!lookup_address_in_vec(addrs, i, &remote) &&
			    msk->pm.subflows < subflows_max) {
				msk->pm.subflows++;
				addrs[i++] = remote;
			}
		}
	}

	return i;
}

static void mptcp_pm_create_subflow_or_signal_addr(struct mptcp_sock *msk)
{
	struct sock *sk = (struct sock *)msk;
	struct mptcp_pm_addr_entry *local;
	unsigned int add_addr_signal_max;
	unsigned int local_addr_max;
	struct pm_nl_pernet *pernet;
	unsigned int subflows_max;

	pernet = net_generic(sock_net(sk), pm_nl_pernet_id);

	add_addr_signal_max = mptcp_pm_get_add_addr_signal_max(msk);
	local_addr_max = mptcp_pm_get_local_addr_max(msk);
	subflows_max = mptcp_pm_get_subflows_max(msk);

	pr_debug("local %d:%d signal %d:%d subflows %d:%d\n",
		 msk->pm.local_addr_used, local_addr_max,
		 msk->pm.add_addr_signaled, add_addr_signal_max,
		 msk->pm.subflows, subflows_max);

	/* check first for announce */
	if (msk->pm.add_addr_signaled < add_addr_signal_max) {
		local = select_signal_address(pernet,
					      msk->pm.add_addr_signaled);

		if (local) {
			if (mptcp_pm_alloc_anno_list(msk, local)) {
				msk->pm.add_addr_signaled++;
				mptcp_pm_announce_addr(msk, &local->addr, false);
				mptcp_pm_nl_addr_send_ack(msk);
			}
		} else {
			/* pick failed, avoid fourther attempts later */
			msk->pm.local_addr_used = add_addr_signal_max;
		}

		check_work_pending(msk);
	}

	/* check if should create a new subflow */
	if (msk->pm.local_addr_used < local_addr_max &&
	    msk->pm.subflows < subflows_max &&
	    !READ_ONCE(msk->pm.remote_deny_join_id0)) {
		local = select_local_address(pernet, msk);
		if (local) {
			bool fullmesh = !!(local->flags & MPTCP_PM_ADDR_FLAG_FULLMESH);
			struct mptcp_addr_info addrs[MPTCP_PM_ADDR_MAX];
			int i, nr;

			msk->pm.local_addr_used++;
			check_work_pending(msk);
			nr = fill_remote_addresses_vec(msk, fullmesh, addrs);
			spin_unlock_bh(&msk->pm.lock);
			for (i = 0; i < nr; i++)
				__mptcp_subflow_connect(sk, &local->addr, &addrs[i]);
			spin_lock_bh(&msk->pm.lock);
			return;
		}

		/* lookup failed, avoid fourther attempts later */
		msk->pm.local_addr_used = local_addr_max;
		check_work_pending(msk);
	}
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
					     struct mptcp_addr_info *addrs)
{
	struct sock *sk = (struct sock *)msk;
	struct mptcp_pm_addr_entry *entry;
	struct mptcp_addr_info local;
	struct pm_nl_pernet *pernet;
	unsigned int subflows_max;
	int i = 0;

	pernet = net_generic(sock_net(sk), pm_nl_pernet_id);
	subflows_max = mptcp_pm_get_subflows_max(msk);

	rcu_read_lock();
	__mptcp_flush_join_list(msk);
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (!(entry->flags & MPTCP_PM_ADDR_FLAG_FULLMESH))
			continue;

		if (entry->addr.family != sk->sk_family) {
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
			if ((entry->addr.family == AF_INET &&
			     !ipv6_addr_v4mapped(&sk->sk_v6_daddr)) ||
			    (sk->sk_family == AF_INET &&
			     !ipv6_addr_v4mapped(&entry->addr.addr6)))
#endif
				continue;
		}

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
		memset(&local, 0, sizeof(local));
		local.family = msk->pm.remote.family;

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

	if (lookup_subflow_by_daddr(&msk->conn_list, &msk->pm.remote))
		goto add_addr_echo;

	/* connect to the specified remote address, using whatever
	 * local address the routing configuration will pick.
	 */
	remote = msk->pm.remote;
	if (!remote.port)
		remote.port = sk->sk_dport;
	nr = fill_local_addresses_vec(msk, addrs);

	msk->pm.add_addr_accepted++;
	if (msk->pm.add_addr_accepted >= add_addr_accept_max ||
	    msk->pm.subflows >= subflows_max)
		WRITE_ONCE(msk->pm.accept_addr, false);

	spin_unlock_bh(&msk->pm.lock);
	for (i = 0; i < nr; i++)
		__mptcp_subflow_connect(sk, &addrs[i], &remote);
	spin_lock_bh(&msk->pm.lock);

add_addr_echo:
	mptcp_pm_announce_addr(msk, &msk->pm.remote, true);
	mptcp_pm_nl_addr_send_ack(msk);
}

void mptcp_pm_nl_addr_send_ack(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;

	msk_owned_by_me(msk);
	lockdep_assert_held(&msk->pm.lock);

	if (!mptcp_pm_should_add_signal(msk) &&
	    !mptcp_pm_should_rm_signal(msk))
		return;

	__mptcp_flush_join_list(msk);
	subflow = list_first_entry_or_null(&msk->conn_list, typeof(*subflow), node);
	if (subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		spin_unlock_bh(&msk->pm.lock);
		pr_debug("send ack for %s",
			 mptcp_pm_should_add_signal(msk) ? "add_addr" : "rm_addr");

		mptcp_subflow_send_ack(ssk);
		spin_lock_bh(&msk->pm.lock);
	}
}

int mptcp_pm_nl_mp_prio_send_ack(struct mptcp_sock *msk,
				 struct mptcp_addr_info *addr,
				 u8 bkup)
{
	struct mptcp_subflow_context *subflow;

	pr_debug("bkup=%d", bkup);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		struct sock *sk = (struct sock *)msk;
		struct mptcp_addr_info local;

		local_address((struct sock_common *)ssk, &local);
		if (!addresses_equal(&local, addr, addr->port))
			continue;

		subflow->backup = bkup;
		subflow->send_mp_prio = 1;
		subflow->request_bkup = bkup;
		__MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_MPPRIOTX);

		spin_unlock_bh(&msk->pm.lock);
		pr_debug("send ack for mp_prio");
		mptcp_subflow_send_ack(ssk);
		spin_lock_bh(&msk->pm.lock);

		return 0;
	}

	return -EINVAL;
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
		bool removed = false;

		list_for_each_entry_safe(subflow, tmp, &msk->conn_list, node) {
			struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
			int how = RCV_SHUTDOWN | SEND_SHUTDOWN;
			u8 id = subflow->local_id;

			if (rm_type == MPTCP_MIB_RMADDR)
				id = subflow->remote_id;

			if (rm_list->ids[i] != id)
				continue;

			pr_debug(" -> %s rm_list_ids[%d]=%u local_id=%u remote_id=%u",
				 rm_type == MPTCP_MIB_RMADDR ? "address" : "subflow",
				 i, rm_list->ids[i], subflow->local_id, subflow->remote_id);
			spin_unlock_bh(&msk->pm.lock);
			mptcp_subflow_shutdown(sk, ssk, how);
			mptcp_close_ssk(sk, ssk, subflow);
			spin_lock_bh(&msk->pm.lock);

			removed = true;
			msk->pm.subflows--;
			__MPTCP_INC_STATS(sock_net(sk), rm_type);
		}
		if (!removed)
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

static int mptcp_pm_nl_append_new_local_addr(struct pm_nl_pernet *pernet,
					     struct mptcp_pm_addr_entry *entry)
{
	struct mptcp_pm_addr_entry *cur;
	unsigned int addr_max;
	int ret = -EINVAL;

	spin_lock_bh(&pernet->lock);
	/* to keep the code simple, don't do IDR-like allocation for address ID,
	 * just bail when we exceed limits
	 */
	if (pernet->next_id == MAX_ADDR_ID)
		pernet->next_id = 1;
	if (pernet->addrs >= MPTCP_PM_ADDR_MAX)
		goto out;
	if (test_bit(entry->addr.id, pernet->id_bitmap))
		goto out;

	/* do not insert duplicate address, differentiate on port only
	 * singled addresses
	 */
	list_for_each_entry(cur, &pernet->local_addr_list, list) {
		if (addresses_equal(&cur->addr, &entry->addr,
				    address_use_port(entry) &&
				    address_use_port(cur)))
			goto out;
	}

	if (!entry->addr.id) {
find_next:
		entry->addr.id = find_next_zero_bit(pernet->id_bitmap,
						    MAX_ADDR_ID + 1,
						    pernet->next_id);
		if ((!entry->addr.id || entry->addr.id > MAX_ADDR_ID) &&
		    pernet->next_id != 1) {
			pernet->next_id = 1;
			goto find_next;
		}
	}

	if (!entry->addr.id || entry->addr.id > MAX_ADDR_ID)
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
	list_add_tail_rcu(&entry->list, &pernet->local_addr_list);
	ret = entry->addr.id;

out:
	spin_unlock_bh(&pernet->lock);
	return ret;
}

static int mptcp_pm_nl_create_listen_socket(struct sock *sk,
					    struct mptcp_pm_addr_entry *entry)
{
	struct sockaddr_storage addr;
	struct mptcp_sock *msk;
	struct socket *ssock;
	int backlog = 1024;
	int err;

	err = sock_create_kern(sock_net(sk), entry->addr.family,
			       SOCK_STREAM, IPPROTO_MPTCP, &entry->lsk);
	if (err)
		return err;

	msk = mptcp_sk(entry->lsk->sk);
	if (!msk) {
		err = -EINVAL;
		goto out;
	}

	ssock = __mptcp_nmpc_socket(msk);
	if (!ssock) {
		err = -EINVAL;
		goto out;
	}

	mptcp_info2sockaddr(&entry->addr, &addr, entry->addr.family);
	err = kernel_bind(ssock, (struct sockaddr *)&addr,
			  sizeof(struct sockaddr_in));
	if (err) {
		pr_warn("kernel_bind error, err=%d", err);
		goto out;
	}

	err = kernel_listen(ssock, backlog);
	if (err) {
		pr_warn("kernel_listen error, err=%d", err);
		goto out;
	}

	return 0;

out:
	sock_release(entry->lsk);
	return err;
}

int mptcp_pm_nl_get_local_id(struct mptcp_sock *msk, struct sock_common *skc)
{
	struct mptcp_pm_addr_entry *entry;
	struct mptcp_addr_info skc_local;
	struct mptcp_addr_info msk_local;
	struct pm_nl_pernet *pernet;
	int ret = -1;

	if (WARN_ON_ONCE(!msk))
		return -1;

	/* The 0 ID mapping is defined by the first subflow, copied into the msk
	 * addr
	 */
	local_address((struct sock_common *)msk, &msk_local);
	local_address((struct sock_common *)skc, &skc_local);
	if (addresses_equal(&msk_local, &skc_local, false))
		return 0;

	if (address_zero(&skc_local))
		return 0;

	pernet = net_generic(sock_net((struct sock *)msk), pm_nl_pernet_id);

	rcu_read_lock();
	list_for_each_entry_rcu(entry, &pernet->local_addr_list, list) {
		if (addresses_equal(&entry->addr, &skc_local, entry->addr.port)) {
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

	entry->addr = skc_local;
	entry->addr.id = 0;
	entry->addr.port = 0;
	entry->ifindex = 0;
	entry->flags = 0;
	entry->lsk = NULL;
	ret = mptcp_pm_nl_append_new_local_addr(pernet, entry);
	if (ret < 0)
		kfree(entry);

	return ret;
}

void mptcp_pm_nl_data_init(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;
	bool subflows;

	subflows = !!mptcp_pm_get_subflows_max(msk);
	WRITE_ONCE(pm->work_pending, (!!mptcp_pm_get_local_addr_max(msk) && subflows) ||
		   !!mptcp_pm_get_add_addr_signal_max(msk));
	WRITE_ONCE(pm->accept_addr, !!mptcp_pm_get_add_addr_accept_max(msk) && subflows);
	WRITE_ONCE(pm->accept_subflow, subflows);
}

#define MPTCP_PM_CMD_GRP_OFFSET       0
#define MPTCP_PM_EV_GRP_OFFSET        1

static const struct genl_multicast_group mptcp_pm_mcgrps[] = {
	[MPTCP_PM_CMD_GRP_OFFSET]	= { .name = MPTCP_PM_CMD_GRP_NAME, },
	[MPTCP_PM_EV_GRP_OFFSET]        = { .name = MPTCP_PM_EV_GRP_NAME,
					    .flags = GENL_UNS_ADMIN_PERM,
					  },
};

static const struct nla_policy
mptcp_pm_addr_policy[MPTCP_PM_ADDR_ATTR_MAX + 1] = {
	[MPTCP_PM_ADDR_ATTR_FAMILY]	= { .type	= NLA_U16,	},
	[MPTCP_PM_ADDR_ATTR_ID]		= { .type	= NLA_U8,	},
	[MPTCP_PM_ADDR_ATTR_ADDR4]	= { .type	= NLA_U32,	},
	[MPTCP_PM_ADDR_ATTR_ADDR6]	=
		NLA_POLICY_EXACT_LEN(sizeof(struct in6_addr)),
	[MPTCP_PM_ADDR_ATTR_PORT]	= { .type	= NLA_U16	},
	[MPTCP_PM_ADDR_ATTR_FLAGS]	= { .type	= NLA_U32	},
	[MPTCP_PM_ADDR_ATTR_IF_IDX]     = { .type	= NLA_S32	},
};

static const struct nla_policy mptcp_pm_policy[MPTCP_PM_ATTR_MAX + 1] = {
	[MPTCP_PM_ATTR_ADDR]		=
					NLA_POLICY_NESTED(mptcp_pm_addr_policy),
	[MPTCP_PM_ATTR_RCV_ADD_ADDRS]	= { .type	= NLA_U32,	},
	[MPTCP_PM_ATTR_SUBFLOWS]	= { .type	= NLA_U32,	},
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
				MPTCP_INC_STATS(sock_net(sk), MPTCP_MIB_SUBFLOWSTALE);
			}
			unlock_sock_fast(ssk, slow);

			/* always try to push the pending data regarless of re-injections:
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

static int mptcp_pm_parse_addr(struct nlattr *attr, struct genl_info *info,
			       bool require_family,
			       struct mptcp_pm_addr_entry *entry)
{
	struct nlattr *tb[MPTCP_PM_ADDR_ATTR_MAX + 1];
	int err, addr_addr;

	if (!attr) {
		GENL_SET_ERR_MSG(info, "missing address info");
		return -EINVAL;
	}

	/* no validation needed - was already done via nested policy */
	err = nla_parse_nested_deprecated(tb, MPTCP_PM_ADDR_ATTR_MAX, attr,
					  mptcp_pm_addr_policy, info->extack);
	if (err)
		return err;

	memset(entry, 0, sizeof(*entry));
	if (!tb[MPTCP_PM_ADDR_ATTR_FAMILY]) {
		if (!require_family)
			goto skip_family;

		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "missing family");
		return -EINVAL;
	}

	entry->addr.family = nla_get_u16(tb[MPTCP_PM_ADDR_ATTR_FAMILY]);
	if (entry->addr.family != AF_INET
#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	    && entry->addr.family != AF_INET6
#endif
	    ) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "unknown address family");
		return -EINVAL;
	}
	addr_addr = mptcp_pm_family_to_addr(entry->addr.family);
	if (!tb[addr_addr]) {
		NL_SET_ERR_MSG_ATTR(info->extack, attr,
				    "missing address data");
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	if (entry->addr.family == AF_INET6)
		entry->addr.addr6 = nla_get_in6_addr(tb[addr_addr]);
	else
#endif
		entry->addr.addr.s_addr = nla_get_in_addr(tb[addr_addr]);

skip_family:
	if (tb[MPTCP_PM_ADDR_ATTR_IF_IDX]) {
		u32 val = nla_get_s32(tb[MPTCP_PM_ADDR_ATTR_IF_IDX]);

		entry->ifindex = val;
	}

	if (tb[MPTCP_PM_ADDR_ATTR_ID])
		entry->addr.id = nla_get_u8(tb[MPTCP_PM_ADDR_ATTR_ID]);

	if (tb[MPTCP_PM_ADDR_ATTR_FLAGS])
		entry->flags = nla_get_u32(tb[MPTCP_PM_ADDR_ATTR_FLAGS]);

	if (tb[MPTCP_PM_ADDR_ATTR_PORT]) {
		if (!(entry->flags & MPTCP_PM_ADDR_FLAG_SIGNAL)) {
			NL_SET_ERR_MSG_ATTR(info->extack, attr,
					    "flags must have signal when using port");
			return -EINVAL;
		}
		entry->addr.port = htons(nla_get_u16(tb[MPTCP_PM_ADDR_ATTR_PORT]));
	}

	return 0;
}

static struct pm_nl_pernet *genl_info_pm_nl(struct genl_info *info)
{
	return net_generic(genl_info_net(info), pm_nl_pernet_id);
}

static int mptcp_nl_add_subflow_or_signal_addr(struct net *net)
{
	struct mptcp_sock *msk;
	long s_slot = 0, s_num = 0;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;

		if (!READ_ONCE(msk->fully_established))
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

static int mptcp_nl_cmd_add_addr(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attr = info->attrs[MPTCP_PM_ATTR_ADDR];
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry addr, *entry;
	int ret;

	ret = mptcp_pm_parse_addr(attr, info, true, &addr);
	if (ret < 0)
		return ret;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		GENL_SET_ERR_MSG(info, "can't allocate addr");
		return -ENOMEM;
	}

	*entry = addr;
	if (entry->addr.port) {
		ret = mptcp_pm_nl_create_listen_socket(skb->sk, entry);
		if (ret) {
			GENL_SET_ERR_MSG(info, "create listen socket error");
			kfree(entry);
			return ret;
		}
	}
	ret = mptcp_pm_nl_append_new_local_addr(pernet, entry);
	if (ret < 0) {
		GENL_SET_ERR_MSG(info, "too many addresses or duplicate one");
		if (entry->lsk)
			sock_release(entry->lsk);
		kfree(entry);
		return ret;
	}

	mptcp_nl_add_subflow_or_signal_addr(sock_net(skb->sk));

	return 0;
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

int mptcp_pm_get_flags_and_ifindex_by_id(struct net *net, unsigned int id,
					 u8 *flags, int *ifindex)
{
	struct mptcp_pm_addr_entry *entry;

	*flags = 0;
	*ifindex = 0;

	if (id) {
		rcu_read_lock();
		entry = __lookup_addr_by_id(net_generic(net, pm_nl_pernet_id), id);
		if (entry) {
			*flags = entry->flags;
			*ifindex = entry->ifindex;
		}
		rcu_read_unlock();
	}

	return 0;
}

static bool remove_anno_list_by_saddr(struct mptcp_sock *msk,
				      struct mptcp_addr_info *addr)
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
				      struct mptcp_addr_info *addr,
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
						   struct mptcp_addr_info *addr)
{
	struct mptcp_sock *msk;
	long s_slot = 0, s_num = 0;
	struct mptcp_rm_list list = { .nr = 0 };

	pr_debug("remove_id=%d", addr->id);

	list.ids[list.nr++] = addr->id;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;
		bool remove_subflow;

		if (list_empty(&msk->conn_list)) {
			mptcp_pm_remove_anno_addr(msk, addr, false);
			goto next;
		}

		lock_sock(sk);
		remove_subflow = lookup_subflow_by_saddr(&msk->conn_list, addr);
		mptcp_pm_remove_anno_addr(msk, addr, remove_subflow);
		if (remove_subflow)
			mptcp_pm_remove_subflow(msk, &list);
		release_sock(sk);

next:
		sock_put(sk);
		cond_resched();
	}

	return 0;
}

/* caller must ensure the RCU grace period is already elapsed */
static void __mptcp_pm_release_addr_entry(struct mptcp_pm_addr_entry *entry)
{
	if (entry->lsk)
		sock_release(entry->lsk);
	kfree(entry);
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

		if (list_empty(&msk->conn_list))
			goto next;

		local_address((struct sock_common *)msk, &msk_local);
		if (!addresses_equal(&msk_local, addr, addr->port))
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

static int mptcp_nl_cmd_del_addr(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attr = info->attrs[MPTCP_PM_ATTR_ADDR];
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry addr, *entry;
	unsigned int addr_max;
	int ret;

	ret = mptcp_pm_parse_addr(attr, info, false, &addr);
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

	mptcp_nl_remove_subflow_and_signal_addr(sock_net(skb->sk), &entry->addr);
	synchronize_rcu();
	__mptcp_pm_release_addr_entry(entry);

	return ret;
}

static void mptcp_pm_remove_addrs_and_subflows(struct mptcp_sock *msk,
					       struct list_head *rm_list)
{
	struct mptcp_rm_list alist = { .nr = 0 }, slist = { .nr = 0 };
	struct mptcp_pm_addr_entry *entry;

	list_for_each_entry(entry, rm_list, list) {
		if (lookup_subflow_by_saddr(&msk->conn_list, &entry->addr) &&
		    alist.nr < MPTCP_RM_IDS_MAX &&
		    slist.nr < MPTCP_RM_IDS_MAX) {
			alist.ids[alist.nr++] = entry->addr.id;
			slist.ids[slist.nr++] = entry->addr.id;
		} else if (remove_anno_list_by_saddr(msk, &entry->addr) &&
			 alist.nr < MPTCP_RM_IDS_MAX) {
			alist.ids[alist.nr++] = entry->addr.id;
		}
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

		lock_sock(sk);
		mptcp_pm_remove_addrs_and_subflows(msk, rm_list);
		release_sock(sk);

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

static int mptcp_nl_cmd_flush_addrs(struct sk_buff *skb, struct genl_info *info)
{
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	LIST_HEAD(free_list);

	spin_lock_bh(&pernet->lock);
	list_splice_init(&pernet->local_addr_list, &free_list);
	__reset_counters(pernet);
	pernet->next_id = 1;
	bitmap_zero(pernet->id_bitmap, MAX_ADDR_ID + 1);
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

static int mptcp_nl_cmd_get_addr(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attr = info->attrs[MPTCP_PM_ATTR_ADDR];
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry addr, *entry;
	struct sk_buff *msg;
	void *reply;
	int ret;

	ret = mptcp_pm_parse_addr(attr, info, false, &addr);
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

static int mptcp_nl_cmd_dump_addrs(struct sk_buff *msg,
				   struct netlink_callback *cb)
{
	struct net *net = sock_net(msg->sk);
	struct mptcp_pm_addr_entry *entry;
	struct pm_nl_pernet *pernet;
	int id = cb->args[0];
	void *hdr;
	int i;

	pernet = net_generic(net, pm_nl_pernet_id);

	spin_lock_bh(&pernet->lock);
	for (i = id; i < MAX_ADDR_ID + 1; i++) {
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

static int
mptcp_nl_cmd_set_limits(struct sk_buff *skb, struct genl_info *info)
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

static int
mptcp_nl_cmd_get_limits(struct sk_buff *skb, struct genl_info *info)
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

static int mptcp_nl_addr_backup(struct net *net,
				struct mptcp_addr_info *addr,
				u8 bkup)
{
	long s_slot = 0, s_num = 0;
	struct mptcp_sock *msk;
	int ret = -EINVAL;

	while ((msk = mptcp_token_iter_next(net, &s_slot, &s_num)) != NULL) {
		struct sock *sk = (struct sock *)msk;

		if (list_empty(&msk->conn_list))
			goto next;

		lock_sock(sk);
		spin_lock_bh(&msk->pm.lock);
		ret = mptcp_pm_nl_mp_prio_send_ack(msk, addr, bkup);
		spin_unlock_bh(&msk->pm.lock);
		release_sock(sk);

next:
		sock_put(sk);
		cond_resched();
	}

	return ret;
}

static int mptcp_nl_cmd_set_flags(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attr = info->attrs[MPTCP_PM_ATTR_ADDR];
	struct pm_nl_pernet *pernet = genl_info_pm_nl(info);
	struct mptcp_pm_addr_entry addr, *entry;
	struct net *net = sock_net(skb->sk);
	u8 bkup = 0;
	int ret;

	ret = mptcp_pm_parse_addr(attr, info, true, &addr);
	if (ret < 0)
		return ret;

	if (addr.flags & MPTCP_PM_ADDR_FLAG_BACKUP)
		bkup = 1;

	list_for_each_entry(entry, &pernet->local_addr_list, list) {
		if (addresses_equal(&entry->addr, &addr.addr, true)) {
			mptcp_nl_addr_backup(net, &entry->addr, bkup);

			if (bkup)
				entry->flags |= MPTCP_PM_ADDR_FLAG_BACKUP;
			else
				entry->flags &= ~MPTCP_PM_ADDR_FLAG_BACKUP;
		}
	}

	return 0;
}

static void mptcp_nl_mcast_send(struct net *net, struct sk_buff *nlskb, gfp_t gfp)
{
	genlmsg_multicast_netns(&mptcp_genl_family, net,
				nlskb, 0, MPTCP_PM_EV_GRP_OFFSET, gfp);
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

	if (nla_put_u8(skb, MPTCP_ATTR_LOC_ID, sf->local_id))
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

	sk_err = ssk->sk_err;
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
	kfree_skb(skb);
}

void mptcp_event_addr_announced(const struct mptcp_sock *msk,
				const struct mptcp_addr_info *info)
{
	struct net *net = sock_net((const struct sock *)msk);
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

	if (nla_put_be16(skb, MPTCP_ATTR_DPORT, info->port))
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
	kfree_skb(skb);
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
	}

	genlmsg_end(skb, nlh);
	mptcp_nl_mcast_send(net, skb, gfp);
	return;

nla_put_failure:
	kfree_skb(skb);
}

static const struct genl_small_ops mptcp_pm_ops[] = {
	{
		.cmd    = MPTCP_PM_CMD_ADD_ADDR,
		.doit   = mptcp_nl_cmd_add_addr,
		.flags  = GENL_ADMIN_PERM,
	},
	{
		.cmd    = MPTCP_PM_CMD_DEL_ADDR,
		.doit   = mptcp_nl_cmd_del_addr,
		.flags  = GENL_ADMIN_PERM,
	},
	{
		.cmd    = MPTCP_PM_CMD_FLUSH_ADDRS,
		.doit   = mptcp_nl_cmd_flush_addrs,
		.flags  = GENL_ADMIN_PERM,
	},
	{
		.cmd    = MPTCP_PM_CMD_GET_ADDR,
		.doit   = mptcp_nl_cmd_get_addr,
		.dumpit   = mptcp_nl_cmd_dump_addrs,
	},
	{
		.cmd    = MPTCP_PM_CMD_SET_LIMITS,
		.doit   = mptcp_nl_cmd_set_limits,
		.flags  = GENL_ADMIN_PERM,
	},
	{
		.cmd    = MPTCP_PM_CMD_GET_LIMITS,
		.doit   = mptcp_nl_cmd_get_limits,
	},
	{
		.cmd    = MPTCP_PM_CMD_SET_FLAGS,
		.doit   = mptcp_nl_cmd_set_flags,
		.flags  = GENL_ADMIN_PERM,
	},
};

static struct genl_family mptcp_genl_family __ro_after_init = {
	.name		= MPTCP_PM_NAME,
	.version	= MPTCP_PM_VER,
	.maxattr	= MPTCP_PM_ATTR_MAX,
	.policy		= mptcp_pm_policy,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.small_ops	= mptcp_pm_ops,
	.n_small_ops	= ARRAY_SIZE(mptcp_pm_ops),
	.mcgrps		= mptcp_pm_mcgrps,
	.n_mcgrps	= ARRAY_SIZE(mptcp_pm_mcgrps),
};

static int __net_init pm_nl_init_net(struct net *net)
{
	struct pm_nl_pernet *pernet = net_generic(net, pm_nl_pernet_id);

	INIT_LIST_HEAD_RCU(&pernet->local_addr_list);
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
		struct pm_nl_pernet *pernet = net_generic(net, pm_nl_pernet_id);

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
