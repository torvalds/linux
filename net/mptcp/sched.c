// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2022, SUSE.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/spinlock.h>
#include "protocol.h"

static DEFINE_SPINLOCK(mptcp_sched_list_lock);
static LIST_HEAD(mptcp_sched_list);

static int mptcp_sched_default_get_subflow(struct mptcp_sock *msk,
					   struct mptcp_sched_data *data)
{
	struct sock *ssk;

	ssk = data->reinject ? mptcp_subflow_get_retrans(msk) :
			       mptcp_subflow_get_send(msk);
	if (!ssk)
		return -EINVAL;

	mptcp_subflow_set_scheduled(mptcp_subflow_ctx(ssk), true);
	return 0;
}

static struct mptcp_sched_ops mptcp_sched_default = {
	.get_subflow	= mptcp_sched_default_get_subflow,
	.name		= "default",
	.owner		= THIS_MODULE,
};

/* Must be called with rcu read lock held */
struct mptcp_sched_ops *mptcp_sched_find(const char *name)
{
	struct mptcp_sched_ops *sched, *ret = NULL;

	list_for_each_entry_rcu(sched, &mptcp_sched_list, list) {
		if (!strcmp(sched->name, name)) {
			ret = sched;
			break;
		}
	}

	return ret;
}

/* Build string with list of available scheduler values.
 * Similar to tcp_get_available_congestion_control()
 */
void mptcp_get_available_schedulers(char *buf, size_t maxlen)
{
	struct mptcp_sched_ops *sched;
	size_t offs = 0;

	rcu_read_lock();
	spin_lock(&mptcp_sched_list_lock);
	list_for_each_entry_rcu(sched, &mptcp_sched_list, list) {
		offs += snprintf(buf + offs, maxlen - offs,
				 "%s%s",
				 offs == 0 ? "" : " ", sched->name);

		if (WARN_ON_ONCE(offs >= maxlen))
			break;
	}
	spin_unlock(&mptcp_sched_list_lock);
	rcu_read_unlock();
}

int mptcp_register_scheduler(struct mptcp_sched_ops *sched)
{
	if (!sched->get_subflow)
		return -EINVAL;

	spin_lock(&mptcp_sched_list_lock);
	if (mptcp_sched_find(sched->name)) {
		spin_unlock(&mptcp_sched_list_lock);
		return -EEXIST;
	}
	list_add_tail_rcu(&sched->list, &mptcp_sched_list);
	spin_unlock(&mptcp_sched_list_lock);

	pr_debug("%s registered\n", sched->name);
	return 0;
}

void mptcp_unregister_scheduler(struct mptcp_sched_ops *sched)
{
	if (sched == &mptcp_sched_default)
		return;

	spin_lock(&mptcp_sched_list_lock);
	list_del_rcu(&sched->list);
	spin_unlock(&mptcp_sched_list_lock);
}

void mptcp_sched_init(void)
{
	mptcp_register_scheduler(&mptcp_sched_default);
}

int mptcp_init_sched(struct mptcp_sock *msk,
		     struct mptcp_sched_ops *sched)
{
	if (!sched)
		sched = &mptcp_sched_default;

	if (!bpf_try_module_get(sched, sched->owner))
		return -EBUSY;

	msk->sched = sched;
	if (msk->sched->init)
		msk->sched->init(msk);

	pr_debug("sched=%s\n", msk->sched->name);

	return 0;
}

void mptcp_release_sched(struct mptcp_sock *msk)
{
	struct mptcp_sched_ops *sched = msk->sched;

	if (!sched)
		return;

	msk->sched = NULL;
	if (sched->release)
		sched->release(msk);

	bpf_module_put(sched, sched->owner);
}

void mptcp_subflow_set_scheduled(struct mptcp_subflow_context *subflow,
				 bool scheduled)
{
	WRITE_ONCE(subflow->scheduled, scheduled);
}

int mptcp_sched_get_send(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sched_data data;

	msk_owned_by_me(msk);

	/* the following check is moved out of mptcp_subflow_get_send */
	if (__mptcp_check_fallback(msk)) {
		if (msk->first &&
		    __tcp_can_send(msk->first) &&
		    sk_stream_memory_free(msk->first)) {
			mptcp_subflow_set_scheduled(mptcp_subflow_ctx(msk->first), true);
			return 0;
		}
		return -EINVAL;
	}

	mptcp_for_each_subflow(msk, subflow) {
		if (READ_ONCE(subflow->scheduled))
			return 0;
	}

	data.reinject = false;
	if (msk->sched == &mptcp_sched_default || !msk->sched)
		return mptcp_sched_default_get_subflow(msk, &data);
	return msk->sched->get_subflow(msk, &data);
}

int mptcp_sched_get_retrans(struct mptcp_sock *msk)
{
	struct mptcp_subflow_context *subflow;
	struct mptcp_sched_data data;

	msk_owned_by_me(msk);

	/* the following check is moved out of mptcp_subflow_get_retrans */
	if (__mptcp_check_fallback(msk))
		return -EINVAL;

	mptcp_for_each_subflow(msk, subflow) {
		if (READ_ONCE(subflow->scheduled))
			return 0;
	}

	data.reinject = true;
	if (msk->sched == &mptcp_sched_default || !msk->sched)
		return mptcp_sched_default_get_subflow(msk, &data);
	return msk->sched->get_subflow(msk, &data);
}
