// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2019, Intel Corporation.
 */
#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/mptcp.h>
#include "protocol.h"

static struct workqueue_struct *pm_wq;

/* path manager command handlers */

int mptcp_pm_announce_addr(struct mptcp_sock *msk,
			   const struct mptcp_addr_info *addr)
{
	return -ENOTSUPP;
}

int mptcp_pm_remove_addr(struct mptcp_sock *msk, u8 local_id)
{
	return -ENOTSUPP;
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
	pr_debug("msk=%p", msk);
	return false;
}

void mptcp_pm_fully_established(struct mptcp_sock *msk)
{
	pr_debug("msk=%p", msk);
}

void mptcp_pm_connection_closed(struct mptcp_sock *msk)
{
	pr_debug("msk=%p", msk);
}

void mptcp_pm_subflow_established(struct mptcp_sock *msk,
				  struct mptcp_subflow_context *subflow)
{
	pr_debug("msk=%p", msk);
}

void mptcp_pm_subflow_closed(struct mptcp_sock *msk, u8 id)
{
	pr_debug("msk=%p", msk);
}

void mptcp_pm_add_addr_received(struct mptcp_sock *msk,
				const struct mptcp_addr_info *addr)
{
	pr_debug("msk=%p, remote_id=%d", msk, addr->id);
}

/* path manager helpers */

bool mptcp_pm_addr_signal(struct mptcp_sock *msk, unsigned int remaining,
			  struct mptcp_addr_info *saddr)
{
	return false;
}

int mptcp_pm_get_local_id(struct mptcp_sock *msk, struct sock_common *skc)
{
	return 0;
}

static void pm_worker(struct work_struct *work)
{
}

void mptcp_pm_data_init(struct mptcp_sock *msk)
{
	msk->pm.add_addr_signaled = 0;
	msk->pm.add_addr_accepted = 0;
	msk->pm.local_addr_used = 0;
	msk->pm.subflows = 0;
	WRITE_ONCE(msk->pm.work_pending, false);
	WRITE_ONCE(msk->pm.addr_signal, false);
	WRITE_ONCE(msk->pm.accept_addr, false);
	WRITE_ONCE(msk->pm.accept_subflow, false);
	msk->pm.status = 0;

	spin_lock_init(&msk->pm.lock);
	INIT_WORK(&msk->pm.work, pm_worker);
}

void mptcp_pm_init(void)
{
	pm_wq = alloc_workqueue("pm_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 8);
	if (!pm_wq)
		panic("Failed to allocate workqueue");
}
