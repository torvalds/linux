/*
 * cgroup_freezer.c -  control group freezer subsystem
 *
 * Copyright IBM Corporation, 2007
 *
 * Author : Cedric Le Goater <clg@fr.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/cgroup.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/freezer.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/cpu.h>

/*
 * A cgroup is freezing if any FREEZING flags are set.  FREEZING_SELF is
 * set if "FROZEN" is written to freezer.state cgroupfs file, and cleared
 * for "THAWED".  FREEZING_PARENT is set if the parent freezer is FREEZING
 * for whatever reason.  IOW, a cgroup has FREEZING_PARENT set if one of
 * its ancestors has FREEZING_SELF set.
 */
enum freezer_state_flags {
	CGROUP_FREEZER_ONLINE	= (1 << 0), /* freezer is fully online */
	CGROUP_FREEZING_SELF	= (1 << 1), /* this freezer is freezing */
	CGROUP_FREEZING_PARENT	= (1 << 2), /* the parent freezer is freezing */
	CGROUP_FROZEN		= (1 << 3), /* this and its descendants frozen */

	/* mask for all FREEZING flags */
	CGROUP_FREEZING		= CGROUP_FREEZING_SELF | CGROUP_FREEZING_PARENT,
};

struct freezer {
	struct cgroup_subsys_state	css;
	unsigned int			state;
};

static DEFINE_MUTEX(freezer_mutex);

static inline struct freezer *css_freezer(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct freezer, css) : NULL;
}

static inline struct freezer *task_freezer(struct task_struct *task)
{
	return css_freezer(task_css(task, freezer_cgrp_id));
}

static struct freezer *parent_freezer(struct freezer *freezer)
{
	return css_freezer(freezer->css.parent);
}

bool cgroup_freezing(struct task_struct *task)
{
	bool ret;
	unsigned int state;

	rcu_read_lock();
	/* Check if the cgroup is still FREEZING, but not FROZEN. The extra
	 * !FROZEN check is required, because the FREEZING bit is not cleared
	 * when the state FROZEN is reached.
	 */
	state = task_freezer(task)->state;
	ret = (state & CGROUP_FREEZING) && !(state & CGROUP_FROZEN);
	rcu_read_unlock();

	return ret;
}

static const char *freezer_state_strs(unsigned int state)
{
	if (state & CGROUP_FROZEN)
		return "FROZEN";
	if (state & CGROUP_FREEZING)
		return "FREEZING";
	return "THAWED";
};

static struct cgroup_subsys_state *
freezer_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct freezer *freezer;

	freezer = kzalloc(sizeof(struct freezer), GFP_KERNEL);
	if (!freezer)
		return ERR_PTR(-ENOMEM);

	return &freezer->css;
}

/**
 * freezer_css_online - commit creation of a freezer css
 * @css: css being created
 *
 * We're committing to creation of @css.  Mark it online and inherit
 * parent's freezing state while holding cpus read lock and freezer_mutex.
 */
static int freezer_css_online(struct cgroup_subsys_state *css)
{
	struct freezer *freezer = css_freezer(css);
	struct freezer *parent = parent_freezer(freezer);

	cpus_read_lock();
	mutex_lock(&freezer_mutex);

	freezer->state |= CGROUP_FREEZER_ONLINE;

	if (parent && (parent->state & CGROUP_FREEZING)) {
		freezer->state |= CGROUP_FREEZING_PARENT | CGROUP_FROZEN;
		static_branch_inc_cpuslocked(&freezer_active);
	}

	mutex_unlock(&freezer_mutex);
	cpus_read_unlock();
	return 0;
}

/**
 * freezer_css_offline - initiate destruction of a freezer css
 * @css: css being destroyed
 *
 * @css is going away.  Mark it dead and decrement freezer_active if
 * it was holding one.
 */
static void freezer_css_offline(struct cgroup_subsys_state *css)
{
	struct freezer *freezer = css_freezer(css);

	cpus_read_lock();
	mutex_lock(&freezer_mutex);

	if (freezer->state & CGROUP_FREEZING)
		static_branch_dec_cpuslocked(&freezer_active);

	freezer->state = 0;

	mutex_unlock(&freezer_mutex);
	cpus_read_unlock();
}

static void freezer_css_free(struct cgroup_subsys_state *css)
{
	kfree(css_freezer(css));
}

/*
 * Tasks can be migrated into a different freezer anytime regardless of its
 * current state.  freezer_attach() is responsible for making new tasks
 * conform to the current state.
 *
 * Freezer state changes and task migration are synchronized via
 * @freezer->lock.  freezer_attach() makes the new tasks conform to the
 * current state and all following state changes can see the new tasks.
 */
static void freezer_attach(struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct cgroup_subsys_state *new_css;

	mutex_lock(&freezer_mutex);

	/*
	 * Make the new tasks conform to the current state of @new_css.
	 * For simplicity, when migrating any task to a FROZEN cgroup, we
	 * revert it to FREEZING and let update_if_frozen() determine the
	 * correct state later.
	 *
	 * Tasks in @tset are on @new_css but may not conform to its
	 * current state before executing the following - !frozen tasks may
	 * be visible in a FROZEN cgroup and frozen tasks in a THAWED one.
	 */
	cgroup_taskset_for_each(task, new_css, tset) {
		struct freezer *freezer = css_freezer(new_css);

		if (!(freezer->state & CGROUP_FREEZING)) {
			__thaw_task(task);
		} else {
			freeze_task(task);

			/* clear FROZEN and propagate upwards */
			while (freezer && (freezer->state & CGROUP_FROZEN)) {
				freezer->state &= ~CGROUP_FROZEN;
				freezer = parent_freezer(freezer);
			}
		}
	}

	mutex_unlock(&freezer_mutex);
}

/**
 * freezer_fork - cgroup post fork callback
 * @task: a task which has just been forked
 *
 * @task has just been created and should conform to the current state of
 * the cgroup_freezer it belongs to.  This function may race against
 * freezer_attach().  Losing to freezer_attach() means that we don't have
 * to do anything as freezer_attach() will put @task into the appropriate
 * state.
 */
static void freezer_fork(struct task_struct *task)
{
	struct freezer *freezer;

	/*
	 * The root cgroup is non-freezable, so we can skip locking the
	 * freezer.  This is safe regardless of race with task migration.
	 * If we didn't race or won, skipping is obviously the right thing
	 * to do.  If we lost and root is the new cgroup, noop is still the
	 * right thing to do.
	 */
	if (task_css_is_root(task, freezer_cgrp_id))
		return;

	mutex_lock(&freezer_mutex);
	rcu_read_lock();

	freezer = task_freezer(task);
	if (freezer->state & CGROUP_FREEZING)
		freeze_task(task);

	rcu_read_unlock();
	mutex_unlock(&freezer_mutex);
}

/**
 * update_if_frozen - update whether a cgroup finished freezing
 * @css: css of interest
 *
 * Once FREEZING is initiated, transition to FROZEN is lazily updated by
 * calling this function.  If the current state is FREEZING but not FROZEN,
 * this function checks whether all tasks of this cgroup and the descendant
 * cgroups finished freezing and, if so, sets FROZEN.
 *
 * The caller is responsible for grabbing RCU read lock and calling
 * update_if_frozen() on all descendants prior to invoking this function.
 *
 * Task states and freezer state might disagree while tasks are being
 * migrated into or out of @css, so we can't verify task states against
 * @freezer state here.  See freezer_attach() for details.
 */
static void update_if_frozen(struct cgroup_subsys_state *css)
{
	struct freezer *freezer = css_freezer(css);
	struct cgroup_subsys_state *pos;
	struct css_task_iter it;
	struct task_struct *task;

	lockdep_assert_held(&freezer_mutex);

	if (!(freezer->state & CGROUP_FREEZING) ||
	    (freezer->state & CGROUP_FROZEN))
		return;

	/* are all (live) children frozen? */
	rcu_read_lock();
	css_for_each_child(pos, css) {
		struct freezer *child = css_freezer(pos);

		if ((child->state & CGROUP_FREEZER_ONLINE) &&
		    !(child->state & CGROUP_FROZEN)) {
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();

	/* are all tasks frozen? */
	css_task_iter_start(css, 0, &it);

	while ((task = css_task_iter_next(&it))) {
		if (freezing(task) && !frozen(task))
			goto out_iter_end;
	}

	freezer->state |= CGROUP_FROZEN;
out_iter_end:
	css_task_iter_end(&it);
}

static int freezer_read(struct seq_file *m, void *v)
{
	struct cgroup_subsys_state *css = seq_css(m), *pos;

	mutex_lock(&freezer_mutex);
	rcu_read_lock();

	/* update states bottom-up */
	css_for_each_descendant_post(pos, css) {
		if (!css_tryget_online(pos))
			continue;
		rcu_read_unlock();

		update_if_frozen(pos);

		rcu_read_lock();
		css_put(pos);
	}

	rcu_read_unlock();
	mutex_unlock(&freezer_mutex);

	seq_puts(m, freezer_state_strs(css_freezer(css)->state));
	seq_putc(m, '\n');
	return 0;
}

static void freeze_cgroup(struct freezer *freezer)
{
	struct css_task_iter it;
	struct task_struct *task;

	css_task_iter_start(&freezer->css, 0, &it);
	while ((task = css_task_iter_next(&it)))
		freeze_task(task);
	css_task_iter_end(&it);
}

static void unfreeze_cgroup(struct freezer *freezer)
{
	struct css_task_iter it;
	struct task_struct *task;

	css_task_iter_start(&freezer->css, 0, &it);
	while ((task = css_task_iter_next(&it)))
		__thaw_task(task);
	css_task_iter_end(&it);
}

/**
 * freezer_apply_state - apply state change to a single cgroup_freezer
 * @freezer: freezer to apply state change to
 * @freeze: whether to freeze or unfreeze
 * @state: CGROUP_FREEZING_* flag to set or clear
 *
 * Set or clear @state on @cgroup according to @freeze, and perform
 * freezing or thawing as necessary.
 */
static void freezer_apply_state(struct freezer *freezer, bool freeze,
				unsigned int state)
{
	/* also synchronizes against task migration, see freezer_attach() */
	lockdep_assert_held(&freezer_mutex);

	if (!(freezer->state & CGROUP_FREEZER_ONLINE))
		return;

	if (freeze) {
		if (!(freezer->state & CGROUP_FREEZING))
			static_branch_inc_cpuslocked(&freezer_active);
		freezer->state |= state;
		freeze_cgroup(freezer);
	} else {
		bool was_freezing = freezer->state & CGROUP_FREEZING;

		freezer->state &= ~state;

		if (!(freezer->state & CGROUP_FREEZING)) {
			freezer->state &= ~CGROUP_FROZEN;
			if (was_freezing)
				static_branch_dec_cpuslocked(&freezer_active);
			unfreeze_cgroup(freezer);
		}
	}
}

/**
 * freezer_change_state - change the freezing state of a cgroup_freezer
 * @freezer: freezer of interest
 * @freeze: whether to freeze or thaw
 *
 * Freeze or thaw @freezer according to @freeze.  The operations are
 * recursive - all descendants of @freezer will be affected.
 */
static void freezer_change_state(struct freezer *freezer, bool freeze)
{
	struct cgroup_subsys_state *pos;

	cpus_read_lock();
	/*
	 * Update all its descendants in pre-order traversal.  Each
	 * descendant will try to inherit its parent's FREEZING state as
	 * CGROUP_FREEZING_PARENT.
	 */
	mutex_lock(&freezer_mutex);
	rcu_read_lock();
	css_for_each_descendant_pre(pos, &freezer->css) {
		struct freezer *pos_f = css_freezer(pos);
		struct freezer *parent = parent_freezer(pos_f);

		if (!css_tryget_online(pos))
			continue;
		rcu_read_unlock();

		if (pos_f == freezer)
			freezer_apply_state(pos_f, freeze,
					    CGROUP_FREEZING_SELF);
		else
			freezer_apply_state(pos_f,
					    parent->state & CGROUP_FREEZING,
					    CGROUP_FREEZING_PARENT);

		rcu_read_lock();
		css_put(pos);
	}
	rcu_read_unlock();
	mutex_unlock(&freezer_mutex);
	cpus_read_unlock();
}

static ssize_t freezer_write(struct kernfs_open_file *of,
			     char *buf, size_t nbytes, loff_t off)
{
	bool freeze;

	buf = strstrip(buf);

	if (strcmp(buf, freezer_state_strs(0)) == 0)
		freeze = false;
	else if (strcmp(buf, freezer_state_strs(CGROUP_FROZEN)) == 0)
		freeze = true;
	else
		return -EINVAL;

	freezer_change_state(css_freezer(of_css(of)), freeze);
	return nbytes;
}

static u64 freezer_self_freezing_read(struct cgroup_subsys_state *css,
				      struct cftype *cft)
{
	struct freezer *freezer = css_freezer(css);

	return (bool)(freezer->state & CGROUP_FREEZING_SELF);
}

static u64 freezer_parent_freezing_read(struct cgroup_subsys_state *css,
					struct cftype *cft)
{
	struct freezer *freezer = css_freezer(css);

	return (bool)(freezer->state & CGROUP_FREEZING_PARENT);
}

static struct cftype files[] = {
	{
		.name = "state",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = freezer_read,
		.write = freezer_write,
	},
	{
		.name = "self_freezing",
		.flags = CFTYPE_NOT_ON_ROOT,
		.read_u64 = freezer_self_freezing_read,
	},
	{
		.name = "parent_freezing",
		.flags = CFTYPE_NOT_ON_ROOT,
		.read_u64 = freezer_parent_freezing_read,
	},
	{ }	/* terminate */
};

struct cgroup_subsys freezer_cgrp_subsys = {
	.css_alloc	= freezer_css_alloc,
	.css_online	= freezer_css_online,
	.css_offline	= freezer_css_offline,
	.css_free	= freezer_css_free,
	.attach		= freezer_attach,
	.fork		= freezer_fork,
	.legacy_cftypes	= files,
};
