// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2019, Intel Corporation.
 */
#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/rculist.h>
#include <linux/spinlock.h>
#include "protocol.h"
#include "mib.h"

#define ADD_ADDR_RETRANS_MAX	3

struct mptcp_pm_add_entry {
	struct list_head	list;
	struct mptcp_addr_info	addr;
	u8			retrans_times;
	struct timer_list	add_timer;
	struct mptcp_sock	*sock;
};

static DEFINE_SPINLOCK(mptcp_pm_list_lock);
static LIST_HEAD(mptcp_pm_list);

/* path manager helpers */

/* if sk is ipv4 or ipv6_only allows only same-family local and remote addresses,
 * otherwise allow any matching local/remote pair
 */
bool mptcp_pm_addr_families_match(const struct sock *sk,
				  const struct mptcp_addr_info *loc,
				  const struct mptcp_addr_info *rem)
{
	bool mptcp_is_v4 = sk->sk_family == AF_INET;

#if IS_ENABLED(CONFIG_MPTCP_IPV6)
	bool loc_is_v4 = loc->family == AF_INET || ipv6_addr_v4mapped(&loc->addr6);
	bool rem_is_v4 = rem->family == AF_INET || ipv6_addr_v4mapped(&rem->addr6);

	if (mptcp_is_v4)
		return loc_is_v4 && rem_is_v4;

	if (ipv6_only_sock(sk))
		return !loc_is_v4 && !rem_is_v4;

	return loc_is_v4 == rem_is_v4;
#else
	return mptcp_is_v4 && loc->family == AF_INET && rem->family == AF_INET;
#endif
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
			addr_equals = ipv6_addr_equal(&a->addr6, &b->addr6);
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

void mptcp_local_address(const struct sock_common *skc,
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

void mptcp_remote_address(const struct sock_common *skc,
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

static bool mptcp_pm_is_init_remote_addr(struct mptcp_sock *msk,
					 const struct mptcp_addr_info *remote)
{
	struct mptcp_addr_info mpc_remote;

	mptcp_remote_address((struct sock_common *)msk, &mpc_remote);
	return mptcp_addresses_equal(&mpc_remote, remote, remote->port);
}

bool mptcp_lookup_subflow_by_saddr(const struct list_head *list,
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

static struct mptcp_pm_add_entry *
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

bool mptcp_remove_anno_list_by_saddr(struct mptcp_sock *msk,
				     const struct mptcp_addr_info *addr)
{
	struct mptcp_pm_add_entry *entry;

	entry = mptcp_pm_del_add_timer(msk, addr, false);
	kfree(entry);
	return entry;
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

static void __mptcp_pm_send_ack(struct mptcp_sock *msk,
				struct mptcp_subflow_context *subflow,
				bool prio, bool backup)
{
	struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
	bool slow;

	pr_debug("send ack for %s\n",
		 prio ? "mp_prio" :
		 (mptcp_pm_should_add_signal(msk) ? "add_addr" : "rm_addr"));

	slow = lock_sock_fast(ssk);
	if (prio) {
		subflow->send_mp_prio = 1;
		subflow->request_bkup = backup;
	}

	__mptcp_subflow_send_ack(ssk);
	unlock_sock_fast(ssk, slow);
}

void mptcp_pm_send_ack(struct mptcp_sock *msk,
		       struct mptcp_subflow_context *subflow,
		       bool prio, bool backup)
{
	spin_unlock_bh(&msk->pm.lock);
	__mptcp_pm_send_ack(msk, subflow, prio, backup);
	spin_lock_bh(&msk->pm.lock);
}

void mptcp_pm_addr_send_ack(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow, *alt = NULL;

	msk_owned_by_me(msk);
	lockdep_assert_held(&msk->pm.lock);

	if (!mptcp_pm_should_add_signal(msk) &&
	    !mptcp_pm_should_rm_signal(msk))
		return;

	mptcp_for_each_subflow(msk, subflow) {
		if (__mptcp_subflow_active(subflow)) {
			if (!subflow->stale) {
				mptcp_pm_send_ack(msk, subflow, false, false);
				return;
			}

			if (!alt)
				alt = subflow;
		}
	}

	if (alt)
		mptcp_pm_send_ack(msk, alt, false, false);
}

int mptcp_pm_mp_prio_send_ack(struct mptcp_sock *msk,
			      struct mptcp_addr_info *addr,
			      struct mptcp_addr_info *rem,
			      u8 bkup)
{
	struct mptcp_subflow_context *subflow;

	pr_debug("bkup=%d\n", bkup);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		struct mptcp_addr_info local, remote;

		mptcp_local_address((struct sock_common *)ssk, &local);
		if (!mptcp_addresses_equal(&local, addr, addr->port))
			continue;

		if (rem && rem->family != AF_UNSPEC) {
			mptcp_remote_address((struct sock_common *)ssk, &remote);
			if (!mptcp_addresses_equal(&remote, rem, rem->port))
				continue;
		}

		__mptcp_pm_send_ack(msk, subflow, true, bkup);
		return 0;
	}

	return -EINVAL;
}

static void mptcp_pm_add_timer(struct timer_list *timer)
{
	struct mptcp_pm_add_entry *entry = from_timer(entry, timer, add_timer);
	struct mptcp_sock *msk = entry->sock;
	struct sock *sk = (struct sock *)msk;

	pr_debug("msk=%p\n", msk);

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
		pr_debug("retransmit ADD_ADDR id=%d\n", entry->addr.id);
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
	struct timer_list *add_timer = NULL;

	spin_lock_bh(&msk->pm.lock);
	entry = mptcp_lookup_anno_list_by_saddr(msk, addr);
	if (entry && (!check_id || entry->addr.id == addr->id)) {
		entry->retrans_times = ADD_ADDR_RETRANS_MAX;
		add_timer = &entry->add_timer;
	}
	if (!check_id && entry)
		list_del(&entry->list);
	spin_unlock_bh(&msk->pm.lock);

	/* no lock, because sk_stop_timer_sync() is calling del_timer_sync() */
	if (add_timer)
		sk_stop_timer_sync(sk, add_timer);

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
		if (WARN_ON_ONCE(mptcp_pm_is_kernel(msk)))
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

static void mptcp_pm_free_anno_list(struct mptcp_sock *msk)
{
	struct mptcp_pm_add_entry *entry, *tmp;
	struct sock *sk = (struct sock *)msk;
	LIST_HEAD(free_list);

	pr_debug("msk=%p\n", msk);

	spin_lock_bh(&msk->pm.lock);
	list_splice_init(&msk->pm.anno_list, &free_list);
	spin_unlock_bh(&msk->pm.lock);

	list_for_each_entry_safe(entry, tmp, &free_list, list) {
		sk_stop_timer_sync(sk, &entry->add_timer);
		kfree(entry);
	}
}

/* path manager command handlers */

int mptcp_pm_announce_addr(struct mptcp_sock *msk,
			   const struct mptcp_addr_info *addr,
			   bool echo)
{
	u8 add_addr = READ_ONCE(msk->pm.addr_signal);

	pr_debug("msk=%p, local_id=%d, echo=%d\n", msk, addr->id, echo);

	lockdep_assert_held(&msk->pm.lock);

	if (add_addr &
	    (echo ? BIT(MPTCP_ADD_ADDR_ECHO) : BIT(MPTCP_ADD_ADDR_SIGNAL))) {
		MPTCP_INC_STATS(sock_net((struct sock *)msk),
				echo ? MPTCP_MIB_ECHOADDTXDROP : MPTCP_MIB_ADDADDRTXDROP);
		return -EINVAL;
	}

	if (echo) {
		msk->pm.remote = *addr;
		add_addr |= BIT(MPTCP_ADD_ADDR_ECHO);
	} else {
		msk->pm.local = *addr;
		add_addr |= BIT(MPTCP_ADD_ADDR_SIGNAL);
	}
	WRITE_ONCE(msk->pm.addr_signal, add_addr);
	return 0;
}

int mptcp_pm_remove_addr(struct mptcp_sock *msk, const struct mptcp_rm_list *rm_list)
{
	u8 rm_addr = READ_ONCE(msk->pm.addr_signal);

	pr_debug("msk=%p, rm_list_nr=%d\n", msk, rm_list->nr);

	if (rm_addr) {
		MPTCP_ADD_STATS(sock_net((struct sock *)msk),
				MPTCP_MIB_RMADDRTXDROP, rm_list->nr);
		return -EINVAL;
	}

	msk->pm.rm_list_tx = *rm_list;
	rm_addr |= BIT(MPTCP_RM_ADDR_SIGNAL);
	WRITE_ONCE(msk->pm.addr_signal, rm_addr);
	mptcp_pm_addr_send_ack(msk);
	return 0;
}

/* path manager event handlers */

void mptcp_pm_new_connection(struct mptcp_sock *msk, const struct sock *ssk, int server_side)
{
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p, token=%u side=%d\n", msk, READ_ONCE(msk->token), server_side);

	WRITE_ONCE(pm->server_side, server_side);
	mptcp_event(MPTCP_EVENT_CREATED, msk, ssk, GFP_ATOMIC);
}

bool mptcp_pm_allow_new_subflow(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;
	unsigned int subflows_max;
	int ret = 0;

	if (mptcp_pm_is_userspace(msk)) {
		if (mptcp_userspace_pm_active(msk)) {
			spin_lock_bh(&pm->lock);
			pm->subflows++;
			spin_unlock_bh(&pm->lock);
			return true;
		}
		return false;
	}

	subflows_max = mptcp_pm_get_subflows_max(msk);

	pr_debug("msk=%p subflows=%d max=%d allow=%d\n", msk, pm->subflows,
		 subflows_max, READ_ONCE(pm->accept_subflow));

	/* try to avoid acquiring the lock below */
	if (!READ_ONCE(pm->accept_subflow))
		return false;

	spin_lock_bh(&pm->lock);
	if (READ_ONCE(pm->accept_subflow)) {
		ret = pm->subflows < subflows_max;
		if (ret && ++pm->subflows == subflows_max)
			WRITE_ONCE(pm->accept_subflow, false);
	}
	spin_unlock_bh(&pm->lock);

	return ret;
}

/* return true if the new status bit is currently cleared, that is, this event
 * can be server, eventually by an already scheduled work
 */
static bool mptcp_pm_schedule_work(struct mptcp_sock *msk,
				   enum mptcp_pm_status new_status)
{
	pr_debug("msk=%p status=%x new=%lx\n", msk, msk->pm.status,
		 BIT(new_status));
	if (msk->pm.status & BIT(new_status))
		return false;

	msk->pm.status |= BIT(new_status);
	mptcp_schedule_work((struct sock *)msk);
	return true;
}

void mptcp_pm_fully_established(struct mptcp_sock *msk, const struct sock *ssk)
{
	struct mptcp_pm_data *pm = &msk->pm;
	bool announce = false;

	pr_debug("msk=%p\n", msk);

	spin_lock_bh(&pm->lock);

	/* mptcp_pm_fully_established() can be invoked by multiple
	 * racing paths - accept() and check_fully_established()
	 * be sure to serve this event only once.
	 */
	if (READ_ONCE(pm->work_pending) &&
	    !(pm->status & BIT(MPTCP_PM_ALREADY_ESTABLISHED)))
		mptcp_pm_schedule_work(msk, MPTCP_PM_ESTABLISHED);

	if ((pm->status & BIT(MPTCP_PM_ALREADY_ESTABLISHED)) == 0)
		announce = true;

	pm->status |= BIT(MPTCP_PM_ALREADY_ESTABLISHED);
	spin_unlock_bh(&pm->lock);

	if (announce)
		mptcp_event(MPTCP_EVENT_ESTABLISHED, msk, ssk, GFP_ATOMIC);
}

void mptcp_pm_connection_closed(struct mptcp_sock *msk)
{
	pr_debug("msk=%p\n", msk);

	if (msk->token)
		mptcp_event(MPTCP_EVENT_CLOSED, msk, NULL, GFP_KERNEL);
}

void mptcp_pm_subflow_established(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p\n", msk);

	if (!READ_ONCE(pm->work_pending))
		return;

	spin_lock_bh(&pm->lock);

	if (READ_ONCE(pm->work_pending))
		mptcp_pm_schedule_work(msk, MPTCP_PM_SUBFLOW_ESTABLISHED);

	spin_unlock_bh(&pm->lock);
}

void mptcp_pm_subflow_check_next(struct mptcp_sock *msk,
				 const struct mptcp_subflow_context *subflow)
{
	struct mptcp_pm_data *pm = &msk->pm;
	bool update_subflows;

	update_subflows = subflow->request_join || subflow->mp_join;
	if (mptcp_pm_is_userspace(msk)) {
		if (update_subflows) {
			spin_lock_bh(&pm->lock);
			pm->subflows--;
			spin_unlock_bh(&pm->lock);
		}
		return;
	}

	if (!READ_ONCE(pm->work_pending) && !update_subflows)
		return;

	spin_lock_bh(&pm->lock);
	if (update_subflows)
		__mptcp_pm_close_subflow(msk);

	/* Even if this subflow is not really established, tell the PM to try
	 * to pick the next ones, if possible.
	 */
	if (mptcp_pm_nl_check_work_pending(msk))
		mptcp_pm_schedule_work(msk, MPTCP_PM_SUBFLOW_ESTABLISHED);

	spin_unlock_bh(&pm->lock);
}

void mptcp_pm_add_addr_received(const struct sock *ssk,
				const struct mptcp_addr_info *addr)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p remote_id=%d accept=%d\n", msk, addr->id,
		 READ_ONCE(pm->accept_addr));

	mptcp_event_addr_announced(ssk, addr);

	spin_lock_bh(&pm->lock);

	if (mptcp_pm_is_userspace(msk)) {
		if (mptcp_userspace_pm_active(msk)) {
			mptcp_pm_announce_addr(msk, addr, true);
			mptcp_pm_add_addr_send_ack(msk);
		} else {
			__MPTCP_INC_STATS(sock_net((struct sock *)msk), MPTCP_MIB_ADDADDRDROP);
		}
	/* id0 should not have a different address */
	} else if ((addr->id == 0 && !mptcp_pm_is_init_remote_addr(msk, addr)) ||
		   (addr->id > 0 && !READ_ONCE(pm->accept_addr))) {
		mptcp_pm_announce_addr(msk, addr, true);
		mptcp_pm_add_addr_send_ack(msk);
	} else if (mptcp_pm_schedule_work(msk, MPTCP_PM_ADD_ADDR_RECEIVED)) {
		pm->remote = *addr;
	} else {
		__MPTCP_INC_STATS(sock_net((struct sock *)msk), MPTCP_MIB_ADDADDRDROP);
	}

	spin_unlock_bh(&pm->lock);
}

void mptcp_pm_add_addr_echoed(struct mptcp_sock *msk,
			      const struct mptcp_addr_info *addr)
{
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p\n", msk);

	if (!READ_ONCE(pm->work_pending))
		return;

	spin_lock_bh(&pm->lock);

	if (mptcp_lookup_anno_list_by_saddr(msk, addr) && READ_ONCE(pm->work_pending))
		mptcp_pm_schedule_work(msk, MPTCP_PM_SUBFLOW_ESTABLISHED);

	spin_unlock_bh(&pm->lock);
}

void mptcp_pm_add_addr_send_ack(struct mptcp_sock *msk)
{
	if (!mptcp_pm_should_add_signal(msk))
		return;

	mptcp_pm_schedule_work(msk, MPTCP_PM_ADD_ADDR_SEND_ACK);
}

static void mptcp_pm_rm_addr_or_subflow(struct mptcp_sock *msk,
					const struct mptcp_rm_list *rm_list,
					enum linux_mptcp_mib_field rm_type)
{
	struct mptcp_subflow_context *subflow, *tmp;
	struct sock *sk = (struct sock *)msk;
	u8 i;

	pr_debug("%s rm_list_nr %d\n",
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

			if ((1 << inet_sk_state_load(ssk)) &
			    (TCPF_FIN_WAIT1 | TCPF_FIN_WAIT2 | TCPF_CLOSING | TCPF_CLOSE))
				continue;
			if (rm_type == MPTCP_MIB_RMADDR && remote_id != rm_id)
				continue;
			if (rm_type == MPTCP_MIB_RMSUBFLOW && id != rm_id)
				continue;

			pr_debug(" -> %s rm_list_ids[%d]=%u local_id=%u remote_id=%u mpc_id=%u\n",
				 rm_type == MPTCP_MIB_RMADDR ? "address" : "subflow",
				 i, rm_id, id, remote_id, msk->mpc_endpoint_id);
			spin_unlock_bh(&msk->pm.lock);
			mptcp_subflow_shutdown(sk, ssk, how);
			removed |= subflow->request_join;

			/* the following takes care of updating the subflows counter */
			mptcp_close_ssk(sk, ssk, subflow);
			spin_lock_bh(&msk->pm.lock);

			if (rm_type == MPTCP_MIB_RMSUBFLOW)
				__MPTCP_INC_STATS(sock_net(sk), rm_type);
		}

		if (rm_type == MPTCP_MIB_RMADDR) {
			__MPTCP_INC_STATS(sock_net(sk), rm_type);
			if (removed && mptcp_pm_is_kernel(msk))
				mptcp_pm_nl_rm_addr(msk, rm_id);
		}
	}
}

static void mptcp_pm_rm_addr_recv(struct mptcp_sock *msk)
{
	mptcp_pm_rm_addr_or_subflow(msk, &msk->pm.rm_list_rx, MPTCP_MIB_RMADDR);
}

void mptcp_pm_rm_subflow(struct mptcp_sock *msk,
			 const struct mptcp_rm_list *rm_list)
{
	mptcp_pm_rm_addr_or_subflow(msk, rm_list, MPTCP_MIB_RMSUBFLOW);
}

void mptcp_pm_rm_addr_received(struct mptcp_sock *msk,
			       const struct mptcp_rm_list *rm_list)
{
	struct mptcp_pm_data *pm = &msk->pm;
	u8 i;

	pr_debug("msk=%p remote_ids_nr=%d\n", msk, rm_list->nr);

	for (i = 0; i < rm_list->nr; i++)
		mptcp_event_addr_removed(msk, rm_list->ids[i]);

	spin_lock_bh(&pm->lock);
	if (mptcp_pm_schedule_work(msk, MPTCP_PM_RM_ADDR_RECEIVED))
		pm->rm_list_rx = *rm_list;
	else
		__MPTCP_INC_STATS(sock_net((struct sock *)msk), MPTCP_MIB_RMADDRDROP);
	spin_unlock_bh(&pm->lock);
}

void mptcp_pm_mp_prio_received(struct sock *ssk, u8 bkup)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	struct sock *sk = subflow->conn;
	struct mptcp_sock *msk;

	pr_debug("subflow->backup=%d, bkup=%d\n", subflow->backup, bkup);
	msk = mptcp_sk(sk);
	if (subflow->backup != bkup)
		subflow->backup = bkup;

	mptcp_event(MPTCP_EVENT_SUB_PRIORITY, msk, ssk, GFP_ATOMIC);
}

void mptcp_pm_mp_fail_received(struct sock *sk, u64 fail_seq)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);

	pr_debug("fail_seq=%llu\n", fail_seq);

	if (!READ_ONCE(msk->allow_infinite_fallback))
		return;

	if (!subflow->fail_tout) {
		pr_debug("send MP_FAIL response and infinite map\n");

		subflow->send_mp_fail = 1;
		subflow->send_infinite_map = 1;
		tcp_send_ack(sk);
	} else {
		pr_debug("MP_FAIL response received\n");
		WRITE_ONCE(subflow->fail_tout, 0);
	}
}

bool mptcp_pm_add_addr_signal(struct mptcp_sock *msk, const struct sk_buff *skb,
			      unsigned int opt_size, unsigned int remaining,
			      struct mptcp_addr_info *addr, bool *echo,
			      bool *drop_other_suboptions)
{
	int ret = false;
	u8 add_addr;
	u8 family;
	bool port;

	spin_lock_bh(&msk->pm.lock);

	/* double check after the lock is acquired */
	if (!mptcp_pm_should_add_signal(msk))
		goto out_unlock;

	/* always drop every other options for pure ack ADD_ADDR; this is a
	 * plain dup-ack from TCP perspective. The other MPTCP-relevant info,
	 * if any, will be carried by the 'original' TCP ack
	 */
	if (skb && skb_is_tcp_pure_ack(skb)) {
		remaining += opt_size;
		*drop_other_suboptions = true;
	}

	*echo = mptcp_pm_should_add_signal_echo(msk);
	port = !!(*echo ? msk->pm.remote.port : msk->pm.local.port);

	family = *echo ? msk->pm.remote.family : msk->pm.local.family;
	if (remaining < mptcp_add_addr_len(family, *echo, port))
		goto out_unlock;

	if (*echo) {
		*addr = msk->pm.remote;
		add_addr = msk->pm.addr_signal & ~BIT(MPTCP_ADD_ADDR_ECHO);
	} else {
		*addr = msk->pm.local;
		add_addr = msk->pm.addr_signal & ~BIT(MPTCP_ADD_ADDR_SIGNAL);
	}
	WRITE_ONCE(msk->pm.addr_signal, add_addr);
	ret = true;

out_unlock:
	spin_unlock_bh(&msk->pm.lock);
	return ret;
}

bool mptcp_pm_rm_addr_signal(struct mptcp_sock *msk, unsigned int remaining,
			     struct mptcp_rm_list *rm_list)
{
	int ret = false, len;
	u8 rm_addr;

	spin_lock_bh(&msk->pm.lock);

	/* double check after the lock is acquired */
	if (!mptcp_pm_should_rm_signal(msk))
		goto out_unlock;

	rm_addr = msk->pm.addr_signal & ~BIT(MPTCP_RM_ADDR_SIGNAL);
	len = mptcp_rm_addr_len(&msk->pm.rm_list_tx);
	if (len < 0) {
		WRITE_ONCE(msk->pm.addr_signal, rm_addr);
		goto out_unlock;
	}
	if (remaining < len)
		goto out_unlock;

	*rm_list = msk->pm.rm_list_tx;
	WRITE_ONCE(msk->pm.addr_signal, rm_addr);
	ret = true;

out_unlock:
	spin_unlock_bh(&msk->pm.lock);
	return ret;
}

int mptcp_pm_get_local_id(struct mptcp_sock *msk, struct sock_common *skc)
{
	struct mptcp_pm_addr_entry skc_local = { 0 };
	struct mptcp_addr_info msk_local;

	if (WARN_ON_ONCE(!msk))
		return -1;

	/* The 0 ID mapping is defined by the first subflow, copied into the msk
	 * addr
	 */
	mptcp_local_address((struct sock_common *)msk, &msk_local);
	mptcp_local_address((struct sock_common *)skc, &skc_local.addr);
	if (mptcp_addresses_equal(&msk_local, &skc_local.addr, false))
		return 0;

	skc_local.addr.id = 0;
	skc_local.flags = MPTCP_PM_ADDR_FLAG_IMPLICIT;

	if (mptcp_pm_is_userspace(msk))
		return mptcp_userspace_pm_get_local_id(msk, &skc_local);
	return mptcp_pm_nl_get_local_id(msk, &skc_local);
}

bool mptcp_pm_is_backup(struct mptcp_sock *msk, struct sock_common *skc)
{
	struct mptcp_addr_info skc_local;

	mptcp_local_address((struct sock_common *)skc, &skc_local);

	if (mptcp_pm_is_userspace(msk))
		return mptcp_userspace_pm_is_backup(msk, &skc_local);

	return mptcp_pm_nl_is_backup(msk, &skc_local);
}

static void mptcp_pm_subflows_chk_stale(const struct mptcp_sock *msk, struct sock *ssk)
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

void mptcp_pm_subflow_chk_stale(const struct mptcp_sock *msk, struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);
	u32 rcv_tstamp = READ_ONCE(tcp_sk(ssk)->rcv_tstamp);

	/* keep track of rtx periods with no progress */
	if (!subflow->stale_count) {
		subflow->stale_rcv_tstamp = rcv_tstamp;
		subflow->stale_count++;
	} else if (subflow->stale_rcv_tstamp == rcv_tstamp) {
		if (subflow->stale_count < U8_MAX)
			subflow->stale_count++;
		mptcp_pm_subflows_chk_stale(msk, ssk);
	} else {
		subflow->stale_count = 0;
		mptcp_subflow_set_active(subflow);
	}
}

void mptcp_pm_worker(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;

	msk_owned_by_me(msk);

	if (!(pm->status & MPTCP_PM_WORK_MASK))
		return;

	spin_lock_bh(&msk->pm.lock);

	pr_debug("msk=%p status=%x\n", msk, pm->status);
	if (pm->status & BIT(MPTCP_PM_ADD_ADDR_SEND_ACK)) {
		pm->status &= ~BIT(MPTCP_PM_ADD_ADDR_SEND_ACK);
		mptcp_pm_addr_send_ack(msk);
	}
	if (pm->status & BIT(MPTCP_PM_RM_ADDR_RECEIVED)) {
		pm->status &= ~BIT(MPTCP_PM_RM_ADDR_RECEIVED);
		mptcp_pm_rm_addr_recv(msk);
	}
	__mptcp_pm_kernel_worker(msk);

	spin_unlock_bh(&msk->pm.lock);
}

void mptcp_pm_destroy(struct mptcp_sock *msk)
{
	mptcp_pm_free_anno_list(msk);

	if (mptcp_pm_is_userspace(msk))
		mptcp_userspace_pm_free_local_addr_list(msk);
}

void mptcp_pm_data_reset(struct mptcp_sock *msk)
{
	u8 pm_type = mptcp_get_pm_type(sock_net((struct sock *)msk));
	struct mptcp_pm_data *pm = &msk->pm;

	memset(&pm->reset, 0, sizeof(pm->reset));
	pm->rm_list_tx.nr = 0;
	pm->rm_list_rx.nr = 0;
	WRITE_ONCE(pm->pm_type, pm_type);

	if (pm_type == MPTCP_PM_TYPE_KERNEL) {
		bool subflows_allowed = !!mptcp_pm_get_subflows_max(msk);

		/* pm->work_pending must be only be set to 'true' when
		 * pm->pm_type is set to MPTCP_PM_TYPE_KERNEL
		 */
		WRITE_ONCE(pm->work_pending,
			   (!!mptcp_pm_get_local_addr_max(msk) &&
			    subflows_allowed) ||
			   !!mptcp_pm_get_add_addr_signal_max(msk));
		WRITE_ONCE(pm->accept_addr,
			   !!mptcp_pm_get_add_addr_accept_max(msk) &&
			   subflows_allowed);
		WRITE_ONCE(pm->accept_subflow, subflows_allowed);

		bitmap_fill(pm->id_avail_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);
	}
}

void mptcp_pm_data_init(struct mptcp_sock *msk)
{
	spin_lock_init(&msk->pm.lock);
	INIT_LIST_HEAD(&msk->pm.anno_list);
	INIT_LIST_HEAD(&msk->pm.userspace_pm_local_addr_list);
	mptcp_pm_data_reset(msk);
}

void __init mptcp_pm_init(void)
{
	mptcp_pm_kernel_register();
	mptcp_pm_userspace_register();
	mptcp_pm_nl_init();
}

/* Must be called with rcu read lock held */
struct mptcp_pm_ops *mptcp_pm_find(const char *name)
{
	struct mptcp_pm_ops *pm_ops;

	list_for_each_entry_rcu(pm_ops, &mptcp_pm_list, list) {
		if (!strcmp(pm_ops->name, name))
			return pm_ops;
	}

	return NULL;
}

int mptcp_pm_validate(struct mptcp_pm_ops *pm_ops)
{
	return 0;
}

int mptcp_pm_register(struct mptcp_pm_ops *pm_ops)
{
	int ret;

	ret = mptcp_pm_validate(pm_ops);
	if (ret)
		return ret;

	spin_lock(&mptcp_pm_list_lock);
	if (mptcp_pm_find(pm_ops->name)) {
		spin_unlock(&mptcp_pm_list_lock);
		return -EEXIST;
	}
	list_add_tail_rcu(&pm_ops->list, &mptcp_pm_list);
	spin_unlock(&mptcp_pm_list_lock);

	pr_debug("%s registered\n", pm_ops->name);
	return 0;
}

void mptcp_pm_unregister(struct mptcp_pm_ops *pm_ops)
{
	/* skip unregistering the default path manager */
	if (WARN_ON_ONCE(pm_ops == &mptcp_pm_kernel))
		return;

	spin_lock(&mptcp_pm_list_lock);
	list_del_rcu(&pm_ops->list);
	spin_unlock(&mptcp_pm_list_lock);
}

/* Build string with list of available path manager values.
 * Similar to tcp_get_available_congestion_control()
 */
void mptcp_pm_get_available(char *buf, size_t maxlen)
{
	struct mptcp_pm_ops *pm_ops;
	size_t offs = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(pm_ops, &mptcp_pm_list, list) {
		offs += snprintf(buf + offs, maxlen - offs, "%s%s",
				 offs == 0 ? "" : " ", pm_ops->name);

		if (WARN_ON_ONCE(offs >= maxlen))
			break;
	}
	rcu_read_unlock();
}
