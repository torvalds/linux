// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/of.h>

#include "walt.h"
#include "trace.h"

/*
 * Scheduler boost is a mechanism to temporarily place tasks on CPUs
 * with higher capacity than those where a task would have normally
 * ended up with their load characteristics. Any entity enabling
 * boost is responsible for disabling it as well.
 */
unsigned int sched_boost_type;
enum sched_boost_policy boost_policy;

static DEFINE_MUTEX(boost_mutex);

void walt_init_tg(struct task_group *tg)
{
	struct walt_task_group *wtg;

	wtg = (struct walt_task_group *) tg->android_vendor_data1;

	wtg->colocate = false;
	wtg->sched_boost_enable[NO_BOOST] = false;
	wtg->sched_boost_enable[FULL_THROTTLE_BOOST] = true;
	wtg->sched_boost_enable[CONSERVATIVE_BOOST] = false;
	wtg->sched_boost_enable[RESTRAINED_BOOST] = false;
	wtg->sched_boost_enable[STORAGE_BOOST] = true;
}

void walt_init_topapp_tg(struct task_group *tg)
{
	struct walt_task_group *wtg;

	wtg = (struct walt_task_group *) tg->android_vendor_data1;

	wtg->colocate = true;
	wtg->sched_boost_enable[NO_BOOST] = false;
	wtg->sched_boost_enable[FULL_THROTTLE_BOOST] = true;
	wtg->sched_boost_enable[CONSERVATIVE_BOOST] = true;
	wtg->sched_boost_enable[RESTRAINED_BOOST] = false;
	wtg->sched_boost_enable[STORAGE_BOOST] = true;
}

void walt_init_foreground_tg(struct task_group *tg)
{
	struct walt_task_group *wtg;

	wtg = (struct walt_task_group *) tg->android_vendor_data1;

	wtg->colocate = false;
	wtg->sched_boost_enable[NO_BOOST] = false;
	wtg->sched_boost_enable[FULL_THROTTLE_BOOST] = true;
	wtg->sched_boost_enable[CONSERVATIVE_BOOST] = true;
	wtg->sched_boost_enable[RESTRAINED_BOOST] = false;
	wtg->sched_boost_enable[STORAGE_BOOST] = true;
}

/*
 * Scheduler boost type and boost policy might at first seem unrelated,
 * however, there exists a connection between them that will allow us
 * to use them interchangeably during placement decisions. We'll explain
 * the connection here in one possible way so that the implications are
 * clear when looking at placement policies.
 *
 * When policy = SCHED_BOOST_NONE, type is either none or RESTRAINED
 * When policy = SCHED_BOOST_ON_ALL or SCHED_BOOST_ON_BIG, type can
 * neither be none nor RESTRAINED.
 */
static void set_boost_policy(int type)
{
	if (type == NO_BOOST || type == RESTRAINED_BOOST) {
		boost_policy = SCHED_BOOST_NONE;
		return;
	}

	if (hmp_capable()) {
		boost_policy = SCHED_BOOST_ON_BIG;
		return;
	}

	boost_policy = SCHED_BOOST_ON_ALL;
}

static bool verify_boost_params(int type)
{
	return type >= STORAGE_BOOST_DISABLE && type <= STORAGE_BOOST;
}

static void sched_no_boost_nop(void)
{
}

static void sched_full_throttle_boost_enter(void)
{
	core_ctl_set_boost(true);
	walt_enable_frequency_aggregation(true);
}

static void sched_full_throttle_boost_exit(void)
{
	core_ctl_set_boost(false);
	walt_enable_frequency_aggregation(false);
}

static void sched_conservative_boost_enter(void)
{
}

static void sched_conservative_boost_exit(void)
{
}

static void sched_restrained_boost_enter(void)
{
	walt_enable_frequency_aggregation(true);
}

static void sched_restrained_boost_exit(void)
{
	walt_enable_frequency_aggregation(false);
}

static void sched_storage_boost_enter(void)
{
	core_ctl_set_boost(true);
}

static void sched_storage_boost_exit(void)
{
	core_ctl_set_boost(false);
}

struct sched_boost_data {
	int	refcount;
	void	(*enter)(void);
	void	(*exit)(void);
};

static struct sched_boost_data sched_boosts[] = {
	[NO_BOOST] = {
		.refcount	= 0,
		.enter		= sched_no_boost_nop,
		.exit		= sched_no_boost_nop,
	},
	[FULL_THROTTLE_BOOST] = {
		.refcount	= 0,
		.enter		= sched_full_throttle_boost_enter,
		.exit		= sched_full_throttle_boost_exit,
	},
	[CONSERVATIVE_BOOST] = {
		.refcount	= 0,
		.enter		= sched_conservative_boost_enter,
		.exit		= sched_conservative_boost_exit,
	},
	[RESTRAINED_BOOST] = {
		.refcount	= 0,
		.enter		= sched_restrained_boost_enter,
		.exit		= sched_restrained_boost_exit,
	},
	[STORAGE_BOOST] = {
		.refcount	= 0,
		.enter		= sched_storage_boost_enter,
		.exit		= sched_storage_boost_exit,
	},

};

#define SCHED_BOOST_START FULL_THROTTLE_BOOST
#define SCHED_BOOST_END (STORAGE_BOOST + 1)

static int sched_effective_boost(void)
{
	int i;

	/*
	 * The boosts are sorted in descending order by
	 * priority.
	 */
	for (i = SCHED_BOOST_START; i < SCHED_BOOST_END; i++) {
		if (sched_boosts[i].refcount >= 1)
			return i;
	}

	return NO_BOOST;
}

static void sched_boost_disable(int type)
{
	struct sched_boost_data *sb = &sched_boosts[type];
	int next_boost, prev_boost = sched_boost_type;

	if (sb->refcount <= 0)
		return;

	sb->refcount--;

	if (sb->refcount)
		return;

	next_boost = sched_effective_boost();
	if (next_boost == prev_boost)
		return;
	/*
	 * This boost's refcount becomes zero, so it must
	 * be disabled. Disable it first and then apply
	 * the next boost.
	 */
	sched_boosts[prev_boost].exit();
	sched_boosts[next_boost].enter();
}

static void sched_boost_enable(int type)
{
	struct sched_boost_data *sb = &sched_boosts[type];
	int next_boost, prev_boost = sched_boost_type;

	sb->refcount++;

	if (sb->refcount != 1)
		return;

	/*
	 * This boost enable request did not come before.
	 * Take this new request and find the next boost
	 * by aggregating all the enabled boosts. If there
	 * is a change, disable the previous boost and enable
	 * the next boost.
	 */

	next_boost = sched_effective_boost();
	if (next_boost == prev_boost)
		return;

	sched_boosts[prev_boost].exit();
	sched_boosts[next_boost].enter();
}

static void sched_boost_disable_all(void)
{
	int i;
	int prev_boost = sched_boost_type;

	if (prev_boost != NO_BOOST) {
		sched_boosts[prev_boost].exit();
		for (i = SCHED_BOOST_START; i < SCHED_BOOST_END; i++)
			sched_boosts[i].refcount = 0;
	}
}

static void _sched_set_boost(int type)
{
	if (type == 0)
		sched_boost_disable_all();
	else if (type > 0)
		sched_boost_enable(type);
	else
		sched_boost_disable(-type);

	/*
	 * sysctl_sched_boost holds the boost request from
	 * user space which could be different from the
	 * effectively enabled boost. Update the effective
	 * boost here.
	 */

	sched_boost_type = sched_effective_boost();
	sysctl_sched_boost = sched_boost_type;
	set_boost_policy(sysctl_sched_boost);
	trace_sched_set_boost(sysctl_sched_boost);
}

int sched_set_boost(int type)
{
	int ret = 0;

	if (unlikely(walt_disabled))
		return -EAGAIN;

	mutex_lock(&boost_mutex);
	if (verify_boost_params(type))
		_sched_set_boost(type);
	else
		ret = -EINVAL;
	mutex_unlock(&boost_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sched_set_boost);

int sched_boost_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret;
	unsigned int *data = (unsigned int *)table->data;

	mutex_lock(&boost_mutex);

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);

	if (ret || !write)
		goto done;

	if (verify_boost_params(*data))
		_sched_set_boost(*data);
	else
		ret = -EINVAL;

done:
	mutex_unlock(&boost_mutex);
	return ret;
}

void walt_boost_init(void)
{
	/* force call the callbacks for default boost */
	sched_set_boost(FULL_THROTTLE_BOOST);
}
