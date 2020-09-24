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

/* path manager command handlers */

int mptcp_pm_announce_addr(struct mptcp_sock *msk,
			   const struct mptcp_addr_info *addr,
			   bool echo)
{
	pr_debug("msk=%p, local_id=%d", msk, addr->id);

	msk->pm.local = *addr;
	WRITE_ONCE(msk->pm.add_addr_echo, echo);
	WRITE_ONCE(msk->pm.add_addr_signal, true);
	return 0;
}

int mptcp_pm_remove_addr(struct mptcp_sock *msk, u8 local_id)
{
	pr_debug("msk=%p, local_id=%d", msk, local_id);

	msk->pm.rm_id = local_id;
	WRITE_ONCE(msk->pm.rm_addr_signal, true);
	return 0;
}

int mptcp_pm_remove_subflow(struct mptcp_sock *msk, u8 remote_id)
{
	return -ENOTSUPP;
}

/* path manager event handlers */

void mptcp_pm_new_connection(struct mptcp_sock *msk, int server_side)
{
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p, token=%u side=%d", msk, msk->token, server_side);

	WRITE_ONCE(pm->server_side, server_side);
}

bool mptcp_pm_allow_new_subflow(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;
	int ret = 0;

	pr_debug("msk=%p subflows=%d max=%d allow=%d", msk, pm->subflows,
		 pm->subflows_max, READ_ONCE(pm->accept_subflow));

	/* try to avoid acquiring the lock below */
	if (!READ_ONCE(pm->accept_subflow))
		return false;

	spin_lock_bh(&pm->lock);
	if (READ_ONCE(pm->accept_subflow)) {
		ret = pm->subflows < pm->subflows_max;
		if (ret && ++pm->subflows == pm->subflows_max)
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
	if (schedule_work(&msk->work))
		sock_hold((struct sock *)msk);
	return true;
}

void mptcp_pm_fully_established(struct mptcp_sock *msk)
{
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p", msk);

	/* try to avoid acquiring the lock below */
	if (!READ_ONCE(pm->work_pending))
		return;

	spin_lock_bh(&pm->lock);

	if (READ_ONCE(pm->work_pending))
		mptcp_pm_schedule_work(msk, MPTCP_PM_ESTABLISHED);

	spin_unlock_bh(&pm->lock);
}

void mptcp_pm_connection_closed(struct mptcp_sock *msk)
{
	pr_debug("msk=%p", msk);
}

void mptcp_pm_subflow_established(struct mptcp_sock *msk,
				  struct mptcp_subflow_context *subflow)
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

void mptcp_pm_subflow_closed(struct mptcp_sock *msk, u8 id)
{
	pr_debug("msk=%p", msk);
}

void mptcp_pm_add_addr_received(struct mptcp_sock *msk,
				const struct mptcp_addr_info *addr)
{
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p remote_id=%d accept=%d", msk, addr->id,
		 READ_ONCE(pm->accept_addr));

	spin_lock_bh(&pm->lock);

	if (!READ_ONCE(pm->accept_addr))
		mptcp_pm_announce_addr(msk, addr, true);
	else if (mptcp_pm_schedule_work(msk, MPTCP_PM_ADD_ADDR_RECEIVED))
		pm->remote = *addr;

	spin_unlock_bh(&pm->lock);
}

void mptcp_pm_rm_addr_received(struct mptcp_sock *msk, u8 rm_id)
{
	struct mptcp_pm_data *pm = &msk->pm;

	pr_debug("msk=%p remote_id=%d", msk, rm_id);

	spin_lock_bh(&pm->lock);
	mptcp_pm_schedule_work(msk, MPTCP_PM_RM_ADDR_RECEIVED);
	pm->rm_id = rm_id;
	spin_unlock_bh(&pm->lock);
}

/* path manager helpers */

bool mptcp_pm_add_addr_signal(struct mptcp_sock *msk, unsigned int remaining,
			      struct mptcp_addr_info *saddr, bool *echo)
{
	int ret = false;

	spin_lock_bh(&msk->pm.lock);

	/* double check after the lock is acquired */
	if (!mptcp_pm_should_add_signal(msk))
		goto out_unlock;

	if (remaining < mptcp_add_addr_len(msk->pm.local.family))
		goto out_unlock;

	*saddr = msk->pm.local;
	*echo = READ_ONCE(msk->pm.add_addr_echo);
	WRITE_ONCE(msk->pm.add_addr_signal, false);
	ret = true;

out_unlock:
	spin_unlock_bh(&msk->pm.lock);
	return ret;
}

bool mptcp_pm_rm_addr_signal(struct mptcp_sock *msk, unsigned int remaining,
			     u8 *rm_id)
{
	int ret = false;

	spin_lock_bh(&msk->pm.lock);

	/* double check after the lock is acquired */
	if (!mptcp_pm_should_rm_signal(msk))
		goto out_unlock;

	if (remaining < TCPOLEN_MPTCP_RM_ADDR_BASE)
		goto out_unlock;

	*rm_id = msk->pm.rm_id;
	WRITE_ONCE(msk->pm.rm_addr_signal, false);
	ret = true;

out_unlock:
	spin_unlock_bh(&msk->pm.lock);
	return ret;
}

int mptcp_pm_get_local_id(struct mptcp_sock *msk, struct sock_common *skc)
{
	return mptcp_pm_nl_get_local_id(msk, skc);
}

void mptcp_pm_data_init(struct mptcp_sock *msk)
{
	msk->pm.add_addr_signaled = 0;
	msk->pm.add_addr_accepted = 0;
	msk->pm.local_addr_used = 0;
	msk->pm.subflows = 0;
	msk->pm.rm_id = 0;
	WRITE_ONCE(msk->pm.work_pending, false);
	WRITE_ONCE(msk->pm.add_addr_signal, false);
	WRITE_ONCE(msk->pm.rm_addr_signal, false);
	WRITE_ONCE(msk->pm.accept_addr, false);
	WRITE_ONCE(msk->pm.accept_subflow, false);
	WRITE_ONCE(msk->pm.add_addr_echo, false);
	msk->pm.status = 0;

	spin_lock_init(&msk->pm.lock);
	INIT_LIST_HEAD(&msk->pm.anno_list);

	mptcp_pm_nl_data_init(msk);
}

void __init mptcp_pm_init(void)
{
	mptcp_pm_nl_init();
}
