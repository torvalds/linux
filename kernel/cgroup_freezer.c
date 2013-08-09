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
	spinlock_t			lock;
};

static inline struct freezer *css_freezer(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct freezer, css) : NULL;
}

static inline struct freezer *task_freezer(struct task_struct *task)
{
	return css_freezer(task_css(task, freezer_subsys_id));
}

static struct freezer *parent_freezer(struct freezer *freezer)
{
	return css_freezer(css_parent(&freezer->css));
}

bool cgroup_freezing(struct task_struct *task)
{
	bool ret;

	rcu_read_lock();
	ret = task_freezer(task)->state & CGROUP_FREEZING;
	rcu_read_unlock();

	return ret;
}

/*
 * cgroups_write_string() limits the size of freezer state strings to
 * CGROUP_LOCAL_BUFFER_SIZE
 */
static const char *freezer_state_strs(unsigned int state)
{
	if (state & CGROUP_FROZEN)
		return "FROZEN";
	if (state & CGROUP_FREEZING)
		return "FREEZING";
	return "THAWED";
};

struct cgroup_subsys freezer_subsys;

static struct cgroup_subsys_state *
freezer_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct freezer *freezer;

	freezer = kzalloc(sizeof(struct freezer), GFP_KERNEL);
	if (!freezer)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&freezer->lock);
	return &freezer->css;
}

/**
 * freezer_css_online - commit creation of a freezer css
 * @css: css being created
 *
 * We're committing to creation of @css.  Mark it online and inherit
 * parent's freezing state while holding both parent's and our
 * freezer->lock.
 */
static int freezer_css_online(struct cgroup_subsys_state *css)
{
	struct freezer *freezer = css_freezer(css);
	struct freezer *parent = parent_freezer(freezer);

	/*
	 * The following double locking and freezing state inheritance
	 * guarantee that @cgroup can never escape ancestors' freezing
	 * states.  See css_for_each_descendant_pre() for details.
	 */
	if (parent)
		spin_lock_irq(&parent->lock);
	spin_lock_nested(&freezer->lock, SINGLE_DEPTH_NESTING);

	freezer->state |= CGROUP_FREEZER_ONLINE;

	if (parent && (parent->state & CGROUP_FREEZING)) {
		freezer->state |= CGROUP_FREEZING_PARENT | CGROUP_FROZEN;
		atomic_inc(&system_freezing_cnt);
	}

	spin_unlock(&freezer->lock);
	if (parent)
		spin_unlock_irq(&parent->lock);

	return 0;
}

/**
 * freezer_css_offline - initiate destruction of a freezer css
 * @css: css being destroyed
 *
 * @css is going away.  Mark it dead and decrement system_freezing_count if
 * it was holding one.
 */
static void freezer_css_offline(struct cgroup_subsys_state *css)
{
	struct freezer *freezer = css_freezer(css);

	spin_lock_irq(&freezer->lock);

	if (freezer->state & CGROUP_FREEZING)
		atomic_dec(&system_freezing_cnt);

	freezer->state = 0;

	spin_unlock_irq(&freezer->lock);
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
static void freezer_attach(struct cgroup_subsys_state *new_css,
			   struct cgroup_taskset *tset)
{
	struct freezer *freezer = css_freezer(new_css);
	struct task_struct *task;
	bool clear_frozen = false;

	spin_lock_irq(&freezer->lock);

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
	cgroup_taskset_for_each(task, new_css->cgroup, tset) {
		if (!(freezer->state & CGROUP_FREEZING)) {
			__thaw_task(task);
		} else {
			freeze_task(task);
			freezer->state &= ~CGROUP_FROZEN;
			clear_frozen = true;
		}
	}

	spin_unlock_irq(&freezer->lock);

	/*
	 * Propagate FROZEN clearing upwards.  We may race with
	 * update_if_frozen(), but as long as both work bottom-up, either
	 * update_if_frozen() sees child's FROZEN cleared or we clear the
	 * parent's FROZEN later.  No parent w/ !FROZEN children can be
	 * left FROZEN.
	 */
	while (clear_frozen && (freezer = parent_freezer(freezer))) {
		spin_lock_irq(&freezer->lock);
		freezer->state &= ~CGROUP_FROZEN;
		clear_frozen = freezer->state & CGROUP_FREEZING;
		spin_unlock_irq(&freezer->lock);
	}
}

static void freezer_fork(struct task_struct *task)
{
	struct freezer *freezer;

	rcu_read_lock();
	freezer = task_freezer(task);

	/*
	 * The root cgroup is non-freezable, so we can skip the
	 * following check.
	 */
	if (!parent_freezer(freezer))
		goto out;

	spin_lock_irq(&freezer->lock);
	if (freezer->state & CGROUP_FREEZING)
		freeze_task(task);
	spin_unlock_irq(&freezer->lock);
out:
	rcu_read_unlock();
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
	struct cgroup_task_iter it;
	struct task_struct *task;

	WARN_ON_ONCE(!rcu_read_lock_held());

	spin_lock_irq(&freezer->lock);

	if (!(freezer->state & CGROUP_FREEZING) ||
	    (freezer->state & CGROUP_FROZEN))
		goto out_unlock;

	/* are all (live) children frozen? */
	css_for_each_child(pos, css) {
		struct freezer *child = css_freezer(pos);

		if ((child->state & CGROUP_FREEZER_ONLINE) &&
		    !(child->state & CGROUP_FROZEN))
			goto out_unlock;
	}

	/* are all tasks frozen? */
	cgroup_task_iter_start(css->cgroup, &it);

	while ((task = cgroup_task_iter_next(css->cgroup, &it))) {
		if (freezing(task)) {
			/*
			 * freezer_should_skip() indicates that the task
			 * should be skipped when determining freezing
			 * completion.  Consider it frozen in addition to
			 * the usual frozen condition.
			 */
			if (!frozen(task) && !freezer_should_skip(task))
				goto out_iter_end;
		}
	}

	freezer->state |= CGROUP_FROZEN;
out_iter_end:
	cgroup_task_iter_end(css->cgroup, &it);
out_unlock:
	spin_unlock_irq(&freezer->lock);
}

static int freezer_read(struct cgroup_subsys_state *css, struct cftype *cft,
			struct seq_file *m)
{
	struct cgroup_subsys_state *pos;

	rcu_read_lock();

	/* update states bottom-up */
	css_for_each_descendant_post(pos, css)
		update_if_frozen(pos);
	update_if_frozen(css);

	rcu_read_unlock();

	seq_puts(m, freezer_state_strs(css_freezer(css)->state));
	seq_putc(m, '\n');
	return 0;
}

static void freeze_cgroup(struct freezer *freezer)
{
	struct cgroup *cgroup = freezer->css.cgroup;
	struct cgroup_task_iter it;
	struct task_struct *task;

	cgroup_task_iter_start(cgroup, &it);
	while ((task = cgroup_task_iter_next(cgroup, &it)))
		freeze_task(task);
	cgroup_task_iter_end(cgroup, &it);
}

static void unfreeze_cgroup(struct freezer *freezer)
{
	struct cgroup *cgroup = freezer->css.cgroup;
	struct cgroup_task_iter it;
	struct task_struct *task;

	cgroup_task_iter_start(cgroup, &it);
	while ((task = cgroup_task_iter_next(cgroup, &it)))
		__thaw_task(task);
	cgroup_task_iter_end(cgroup, &it);
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
	lockdep_assert_held(&freezer->lock);

	if (!(freezer->state & CGROUP_FREEZER_ONLINE))
		return;

	if (freeze) {
		if (!(freezer->state & CGROUP_FREEZING))
			atomic_inc(&system_freezing_cnt);
		freezer->state |= state;
		freeze_cgroup(freezer);
	} else {
		bool was_freezing = freezer->state & CGROUP_FREEZING;

		freezer->state &= ~state;

		if (!(freezer->state & CGROUP_FREEZING)) {
			if (was_freezing)
				atomic_dec(&system_freezing_cnt);
			freezer->state &= ~CGROUP_FROZEN;
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

	/* update @freezer */
	spin_lock_irq(&freezer->lock);
	freezer_apply_state(freezer, freeze, CGROUP_FREEZING_SELF);
	spin_unlock_irq(&freezer->lock);

	/*
	 * Update all its descendants in pre-order traversal.  Each
	 * descendant will try to inherit its parent's FREEZING state as
	 * CGROUP_FREEZING_PARENT.
	 */
	rcu_read_lock();
	css_for_each_descendant_pre(pos, &freezer->css) {
		struct freezer *pos_f = css_freezer(pos);
		struct freezer *parent = parent_freezer(pos_f);

		/*
		 * Our update to @parent->state is already visible which is
		 * all we need.  No need to lock @parent.  For more info on
		 * synchronization, see freezer_post_create().
		 */
		spin_lock_irq(&pos_f->lock);
		freezer_apply_state(pos_f, parent->state & CGROUP_FREEZING,
				    CGROUP_FREEZING_PARENT);
		spin_unlock_irq(&pos_f->lock);
	}
	rcu_read_unlock();
}

static int freezer_write(struct cgroup_subsys_state *css, struct cftype *cft,
			 const char *buffer)
{
	bool freeze;

	if (strcmp(buffer, freezer_state_strs(0)) == 0)
		freeze = false;
	else if (strcmp(buffer, freezer_state_strs(CGROUP_FROZEN)) == 0)
		freeze = true;
	else
		return -EINVAL;

	freezer_change_state(css_freezer(css), freeze);
	return 0;
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
		.read_seq_string = freezer_read,
		.write_string = freezer_write,
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

struct cgroup_subsys freezer_subsys = {
	.name		= "freezer",
	.css_alloc	= freezer_css_alloc,
	.css_online	= freezer_css_online,
	.css_offline	= freezer_css_offline,
	.css_free	= freezer_css_free,
	.subsys_id	= freezer_subsys_id,
	.attach		= freezer_attach,
	.fork		= freezer_fork,
	.base_cftypes	= files,
};
