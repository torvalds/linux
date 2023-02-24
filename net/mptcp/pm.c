// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2019, Intel Corporation.
 */
#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/mptcp.h>
#include "protocol.h"

#include "mib.h"

/* path manager command handlers */

int mptcp_pm_announce_addr(struct mptcp_sock *msk,
			   const struct mptcp_addr_info *addr,
			   bool echo)
{
	u8 add_addr = READ_ONCE(msk->pm.addr_signal);

	pr_debug("msk=%p, local_id=%d, echo=%d", msk, addr->id, echo);

	lockdep_assert_held(&msk->pm.lock);

	if (add_addr &
	    (echo ? BIT(MPTCP_ADD_ADDR_ECHO) : BIT(MPTCP_ADD_ADDR_SIGNAL))) {
		pr_warn("addr_signal error, add_addr=%d, echo=%d", add_addr, echo);
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

	pr_debug("msk=%p, rm_list_nr=%d", msk, rm_list->nr);

	if (rm_addr) {
		pr_warn("addr_signal error, rm_addr=%d", rm_addr);
		return -EINVAL;
	}

	msk->pm.rm_list_tx = *rm_list;
	rm_addr |= BIT(MPTCP_RM_ADDR_SIGNAL);
	WRITE_ONCE(msk->pm.addr_signal, rm_addr);
	mptcp_pm_nl_addr_send_ack(msk);
	return 0;
}

int mptcp_pm_remove_subflow(struct mptcp_sock *msk, const struct mptcp_rm_list *rm_list)
{
	pr_debug("msk=%p, rm_list_nr=%d", msk, rm_list->nr);

	spin_lock_bh(&msk->pm.lock);
	mptcp_pm_nl_rm_subflow_received(msk, rm_list);
	spin_unlock_bh(&msk->pm.lock);
	return 0;
}

/* path manager event handlers */

void mptcp_pm_new_connection(struct mptcp_sock *msk, const struct sock *ssk, int server_side)
{
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p, token=%u side=%d", msk, msk->token, server_side);

	WRITE_ONCE(pm->server_side, server_side);
	mptcp_event(MPTCP_EVENT_CREATED, msk, ssk, GFP_ATOMIC);
}

bool mptcp_pm_allow_new_subflow(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;
	unsigned int subflows_max;
	int ret = 0;

	if (mptcp_pm_is_userspace(msk))
		return mptcp_userspace_pm_active(msk);

	subflows_max = mptcp_pm_get_subflows_max(msk);

	pr_debug("msk=%p subflows=%d max=%d allow=%d", msk, pm->subflows,
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
	pr_debug("msk=%p status=%x new=%lx", msk, msk->pm.status,
		 BIT(new_status));
	if (msk->pm.status & BIT(new_status))
		return false;

	msk->pm.status |= BIT(new_status);
	mptcp_schedule_work((struct sock *)msk);
	return true;
}

void mptcp_pm_fully_established(struct mptcp_sock *msk, const struct sock *ssk, gfp_t gfp)
{
	struct mptcp_pm_data *pm = &msk->pm;
	bool announce = false;

	pr_debug("msk=%p", msk);

	spin_lock_bh(&pm->lock);

	/* mptcp_pm_fully_established() can be invoked by multiple
	 * racing paths - accept() and check_fully_established()
	 * be sure to serve this event only once.
	 */
	if (READ_ONCE(pm->work_pending) &&
	    !(msk->pm.status & BIT(MPTCP_PM_ALREADY_ESTABLISHED)))
		mptcp_pm_schedule_work(msk, MPTCP_PM_ESTABLISHED);

	if ((msk->pm.status & BIT(MPTCP_PM_ALREADY_ESTABLISHED)) == 0)
		announce = true;

	msk->pm.status |= BIT(MPTCP_PM_ALREADY_ESTABLISHED);
	spin_unlock_bh(&pm->lock);

	if (announce)
		mptcp_event(MPTCP_EVENT_ESTABLISHED, msk, ssk, gfp);
}

void mptcp_pm_connection_closed(struct mptcp_sock *msk)
{
	pr_debug("msk=%p", msk);
}

void mptcp_pm_subflow_established(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p", msk);

	if (!READ_ONCE(pm->work_pending))
		return;

	spin_lock_bh(&pm->lock);

	if (READ_ONCE(pm->work_pending))
		mptcp_pm_schedule_work(msk, MPTCP_PM_SUBFLOW_ESTABLISHED);

	spin_unlock_bh(&pm->lock);
}

void mptcp_pm_subflow_check_next(struct mptcp_sock *msk, const struct sock *ssk,
				 const struct mptcp_subflow_context *subflow)
{
	struct mptcp_pm_data *pm = &msk->pm;
	bool update_subflows;

	update_subflows = (subflow->request_join || subflow->mp_join) &&
			  mptcp_pm_is_kernel(msk);
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

	pr_debug("msk=%p remote_id=%d accept=%d", msk, addr->id,
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
	} else if (!READ_ONCE(pm->accept_addr)) {
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

	pr_debug("msk=%p", msk);

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

void mptcp_pm_rm_addr_received(struct mptcp_sock *msk,
			       const struct mptcp_rm_list *rm_list)
{
	struct mptcp_pm_data *pm = &msk->pm;
	u8 i;

	pr_debug("msk=%p remote_ids_nr=%d", msk, rm_list->nr);

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
	if (subflow->backup != bkup) {
		subflow->backup = bkup;
		mptcp_data_lock(sk);
		if (!sock_owned_by_user(sk))
			msk->last_snd = NULL;
		else
			__set_bit(MPTCP_RESET_SCHEDULER,  &msk->cb_flags);
		mptcp_data_unlock(sk);
	}

	mptcp_event(MPTCP_EVENT_SUB_PRIORITY, msk, ssk, GFP_ATOMIC);
}

void mptcp_pm_mp_fail_received(struct sock *sk, u64 fail_seq)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(sk);
	struct mptcp_sock *msk = mptcp_sk(subflow->conn);

	pr_debug("fail_seq=%llu", fail_seq);

	if (!READ_ONCE(msk->allow_infinite_fallback))
		return;

	if (!subflow->fail_tout) {
		pr_debug("send MP_FAIL response and infinite map");

		subflow->send_mp_fail = 1;
		subflow->send_infinite_map = 1;
		tcp_send_ack(sk);
	} else {
		pr_debug("MP_FAIL response received");
		WRITE_ONCE(subflow->fail_tout, 0);
	}
}

/* path manager helpers */

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
	return mptcp_pm_nl_get_local_id(msk, skc);
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
		mptcp_pm_nl_subflow_chk_stale(msk, ssk);
	} else {
		subflow->stale_count = 0;
		mptcp_subflow_set_active(subflow);
	}
}

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

void mptcp_pm_data_reset(struct mptcp_sock *msk)
{
	u8 pm_type = mptcp_get_pm_type(sock_net((struct sock *)msk));
	struct mptcp_pm_data *pm = &msk->pm;

	pm->add_addr_signaled = 0;
	pm->add_addr_accepted = 0;
	pm->local_addr_used = 0;
	pm->subflows = 0;
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
	} else {
		WRITE_ONCE(pm->work_pending, 0);
		WRITE_ONCE(pm->accept_addr, 0);
		WRITE_ONCE(pm->accept_subflow, 0);
	}

	WRITE_ONCE(pm->addr_signal, 0);
	WRITE_ONCE(pm->remote_deny_join_id0, false);
	pm->status = 0;
	bitmap_fill(msk->pm.id_avail_bitmap, MPTCP_PM_MAX_ADDR_ID + 1);
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
	mptcp_pm_nl_init();
}
