/*
 * cgroup_timer_slack.c - control group timer slack subsystem
 *
 * Copyright Nokia Corparation, 2011
 * Author: Kirill A. Shutemov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/cgroup.h>
#include <linux/slab.h>
#include <linux/err.h>

struct cgroup_subsys timer_slack_subsys;
struct tslack_cgroup {
	struct cgroup_subsys_state css;
	unsigned long min_slack_ns;
};

static struct tslack_cgroup *cgroup_to_tslack(struct cgroup *cgroup)
{
	struct cgroup_subsys_state *css;

	css = cgroup_subsys_state(cgroup, timer_slack_subsys.subsys_id);
	return container_of(css, struct tslack_cgroup, css);
}

static struct cgroup_subsys_state *tslack_create(struct cgroup_subsys *subsys,
		struct cgroup *cgroup)
{
	struct tslack_cgroup *tslack_cgroup;

	tslack_cgroup = kmalloc(sizeof(*tslack_cgroup), GFP_KERNEL);
	if (!tslack_cgroup)
		return ERR_PTR(-ENOMEM);

	if (cgroup->parent) {
		struct tslack_cgroup *parent;

		parent = cgroup_to_tslack(cgroup->parent);
		tslack_cgroup->min_slack_ns = parent->min_slack_ns;
	} else
		tslack_cgroup->min_slack_ns = 0UL;

	return &tslack_cgroup->css;
}

static void tslack_destroy(struct cgroup_subsys *tslack_cgroup,
		struct cgroup *cgroup)
{
	kfree(cgroup_to_tslack(cgroup));
}

static u64 tslack_read_min(struct cgroup *cgroup, struct cftype *cft)
{
	return cgroup_to_tslack(cgroup)->min_slack_ns;
}

static int tslack_write_min(struct cgroup *cgroup, struct cftype *cft, u64 val)
{
	if (val > ULONG_MAX)
		return -EINVAL;

	cgroup_to_tslack(cgroup)->min_slack_ns = val;

	return 0;
}

static u64 tslack_read_effective(struct cgroup *cgroup, struct cftype *cft)
{
	unsigned long min;

	min = cgroup_to_tslack(cgroup)->min_slack_ns;
	while (cgroup->parent) {
		cgroup = cgroup->parent;
		min = max(cgroup_to_tslack(cgroup)->min_slack_ns, min);
	}

	return min;
}

static struct cftype files[] = {
	{
		.name = "min_slack_ns",
		.read_u64 = tslack_read_min,
		.write_u64 = tslack_write_min,
	},
	{
		.name = "effective_slack_ns",
		.read_u64 = tslack_read_effective,
	},
};

static int tslack_populate(struct cgroup_subsys *subsys, struct cgroup *cgroup)
{
	return cgroup_add_files(cgroup, subsys, files, ARRAY_SIZE(files));
}

struct cgroup_subsys timer_slack_subsys = {
	.name		= "timer_slack",
	.subsys_id	= timer_slack_subsys_id,
	.create		= tslack_create,
	.destroy	= tslack_destroy,
	.populate	= tslack_populate,
};

unsigned long task_get_effective_timer_slack(struct task_struct *tsk)
{
	struct cgroup *cgroup;
	unsigned long slack;

	rcu_read_lock();
	cgroup = task_cgroup(tsk, timer_slack_subsys.subsys_id);
	slack = tslack_read_effective(cgroup, NULL);
	rcu_read_unlock();

	return max(tsk->timer_slack_ns, slack);
}
