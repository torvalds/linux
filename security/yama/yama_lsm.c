/*
 * Yama Linux Security Module
 *
 * Author: Kees Cook <keescook@chromium.org>
 *
 * Copyright (C) 2010 Canonical, Ltd.
 * Copyright (C) 2011 The Chromium OS Authors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <linux/lsm_hooks.h>
#include <linux/sysctl.h>
#include <linux/ptrace.h>
#include <linux/prctl.h>
#include <linux/ratelimit.h>
#include <linux/workqueue.h>

#define YAMA_SCOPE_DISABLED	0
#define YAMA_SCOPE_RELATIONAL	1
#define YAMA_SCOPE_CAPABILITY	2
#define YAMA_SCOPE_NO_ATTACH	3

static int ptrace_scope = YAMA_SCOPE_RELATIONAL;

/* describe a ptrace relationship for potential exception */
struct ptrace_relation {
	struct task_struct *tracer;
	struct task_struct *tracee;
	bool invalid;
	struct list_head node;
	struct rcu_head rcu;
};

static LIST_HEAD(ptracer_relations);
static DEFINE_SPINLOCK(ptracer_relations_lock);

static void yama_relation_cleanup(struct work_struct *work);
static DECLARE_WORK(yama_relation_work, yama_relation_cleanup);

/**
 * yama_relation_cleanup - remove invalid entries from the relation list
 *
 */
static void yama_relation_cleanup(struct work_struct *work)
{
	struct ptrace_relation *relation;

	spin_lock(&ptracer_relations_lock);
	rcu_read_lock();
	list_for_each_entry_rcu(relation, &ptracer_relations, node) {
		if (relation->invalid) {
			list_del_rcu(&relation->node);
			kfree_rcu(relation, rcu);
		}
	}
	rcu_read_unlock();
	spin_unlock(&ptracer_relations_lock);
}

/**
 * yama_ptracer_add - add/replace an exception for this tracer/tracee pair
 * @tracer: the task_struct of the process doing the ptrace
 * @tracee: the task_struct of the process to be ptraced
 *
 * Each tracee can have, at most, one tracer registered. Each time this
 * is called, the prior registered tracer will be replaced for the tracee.
 *
 * Returns 0 if relationship was added, -ve on error.
 */
static int yama_ptracer_add(struct task_struct *tracer,
			    struct task_struct *tracee)
{
	struct ptrace_relation *relation, *added;

	added = kmalloc(sizeof(*added), GFP_KERNEL);
	if (!added)
		return -ENOMEM;

	added->tracee = tracee;
	added->tracer = tracer;
	added->invalid = false;

	spin_lock(&ptracer_relations_lock);
	rcu_read_lock();
	list_for_each_entry_rcu(relation, &ptracer_relations, node) {
		if (relation->invalid)
			continue;
		if (relation->tracee == tracee) {
			list_replace_rcu(&relation->node, &added->node);
			kfree_rcu(relation, rcu);
			goto out;
		}
	}

	list_add_rcu(&added->node, &ptracer_relations);

out:
	rcu_read_unlock();
	spin_unlock(&ptracer_relations_lock);
	return 0;
}

/**
 * yama_ptracer_del - remove exceptions related to the given tasks
 * @tracer: remove any relation where tracer task matches
 * @tracee: remove any relation where tracee task matches
 */
static void yama_ptracer_del(struct task_struct *tracer,
			     struct task_struct *tracee)
{
	struct ptrace_relation *relation;
	bool marked = false;

	rcu_read_lock();
	list_for_each_entry_rcu(relation, &ptracer_relations, node) {
		if (relation->invalid)
			continue;
		if (relation->tracee == tracee ||
		    (tracer && relation->tracer == tracer)) {
			relation->invalid = true;
			marked = true;
		}
	}
	rcu_read_unlock();

	if (marked)
		schedule_work(&yama_relation_work);
}

/**
 * yama_task_free - check for task_pid to remove from exception list
 * @task: task being removed
 */
void yama_task_free(struct task_struct *task)
{
	yama_ptracer_del(task, task);
}

/**
 * yama_task_prctl - check for Yama-specific prctl operations
 * @option: operation
 * @arg2: argument
 * @arg3: argument
 * @arg4: argument
 * @arg5: argument
 *
 * Return 0 on success, -ve on error.  -ENOSYS is returned when Yama
 * does not handle the given option.
 */
int yama_task_prctl(int option, unsigned long arg2, unsigned long arg3,
			   unsigned long arg4, unsigned long arg5)
{
	int rc = -ENOSYS;
	struct task_struct *myself = current;

	switch (option) {
	case PR_SET_PTRACER:
		/* Since a thread can call prctl(), find the group leader
		 * before calling _add() or _del() on it, since we want
		 * process-level granularity of control. The tracer group
		 * leader checking is handled later when walking the ancestry
		 * at the time of PTRACE_ATTACH check.
		 */
		rcu_read_lock();
		if (!thread_group_leader(myself))
			myself = rcu_dereference(myself->group_leader);
		get_task_struct(myself);
		rcu_read_unlock();

		if (arg2 == 0) {
			yama_ptracer_del(NULL, myself);
			rc = 0;
		} else if (arg2 == PR_SET_PTRACER_ANY || (int)arg2 == -1) {
			rc = yama_ptracer_add(NULL, myself);
		} else {
			struct task_struct *tracer;

			rcu_read_lock();
			tracer = find_task_by_vpid(arg2);
			if (tracer)
				get_task_struct(tracer);
			else
				rc = -EINVAL;
			rcu_read_unlock();

			if (tracer) {
				rc = yama_ptracer_add(tracer, myself);
				put_task_struct(tracer);
			}
		}

		put_task_struct(myself);
		break;
	}

	return rc;
}

/**
 * task_is_descendant - walk up a process family tree looking for a match
 * @parent: the process to compare against while walking up from child
 * @child: the process to start from while looking upwards for parent
 *
 * Returns 1 if child is a descendant of parent, 0 if not.
 */
static int task_is_descendant(struct task_struct *parent,
			      struct task_struct *child)
{
	int rc = 0;
	struct task_struct *walker = child;

	if (!parent || !child)
		return 0;

	rcu_read_lock();
	if (!thread_group_leader(parent))
		parent = rcu_dereference(parent->group_leader);
	while (walker->pid > 0) {
		if (!thread_group_leader(walker))
			walker = rcu_dereference(walker->group_leader);
		if (walker == parent) {
			rc = 1;
			break;
		}
		walker = rcu_dereference(walker->real_parent);
	}
	rcu_read_unlock();

	return rc;
}

/**
 * ptracer_exception_found - tracer registered as exception for this tracee
 * @tracer: the task_struct of the process attempting ptrace
 * @tracee: the task_struct of the process to be ptraced
 *
 * Returns 1 if tracer has is ptracer exception ancestor for tracee.
 */
static int ptracer_exception_found(struct task_struct *tracer,
				   struct task_struct *tracee)
{
	int rc = 0;
	struct ptrace_relation *relation;
	struct task_struct *parent = NULL;
	bool found = false;

	rcu_read_lock();
	if (!thread_group_leader(tracee))
		tracee = rcu_dereference(tracee->group_leader);
	list_for_each_entry_rcu(relation, &ptracer_relations, node) {
		if (relation->invalid)
			continue;
		if (relation->tracee == tracee) {
			parent = relation->tracer;
			found = true;
			break;
		}
	}

	if (found && (parent == NULL || task_is_descendant(parent, tracer)))
		rc = 1;
	rcu_read_unlock();

	return rc;
}

/**
 * yama_ptrace_access_check - validate PTRACE_ATTACH calls
 * @child: task that current task is attempting to ptrace
 * @mode: ptrace attach mode
 *
 * Returns 0 if following the ptrace is allowed, -ve on error.
 */
static int yama_ptrace_access_check(struct task_struct *child,
				    unsigned int mode)
{
	int rc = 0;

	/* require ptrace target be a child of ptracer on attach */
	if (mode == PTRACE_MODE_ATTACH) {
		switch (ptrace_scope) {
		case YAMA_SCOPE_DISABLED:
			/* No additional restrictions. */
			break;
		case YAMA_SCOPE_RELATIONAL:
			rcu_read_lock();
			if (!task_is_descendant(current, child) &&
			    !ptracer_exception_found(current, child) &&
			    !ns_capable(__task_cred(child)->user_ns, CAP_SYS_PTRACE))
				rc = -EPERM;
			rcu_read_unlock();
			break;
		case YAMA_SCOPE_CAPABILITY:
			rcu_read_lock();
			if (!ns_capable(__task_cred(child)->user_ns, CAP_SYS_PTRACE))
				rc = -EPERM;
			rcu_read_unlock();
			break;
		case YAMA_SCOPE_NO_ATTACH:
		default:
			rc = -EPERM;
			break;
		}
	}

	if (rc) {
		printk_ratelimited(KERN_NOTICE
			"ptrace of pid %d was attempted by: %s (pid %d)\n",
			child->pid, current->comm, current->pid);
	}

	return rc;
}

/**
 * yama_ptrace_traceme - validate PTRACE_TRACEME calls
 * @parent: task that will become the ptracer of the current task
 *
 * Returns 0 if following the ptrace is allowed, -ve on error.
 */
int yama_ptrace_traceme(struct task_struct *parent)
{
	int rc = 0;

	/* Only disallow PTRACE_TRACEME on more aggressive settings. */
	switch (ptrace_scope) {
	case YAMA_SCOPE_CAPABILITY:
		if (!has_ns_capability(parent, current_user_ns(), CAP_SYS_PTRACE))
			rc = -EPERM;
		break;
	case YAMA_SCOPE_NO_ATTACH:
		rc = -EPERM;
		break;
	}

	if (rc) {
		printk_ratelimited(KERN_NOTICE
			"ptraceme of pid %d was attempted by: %s (pid %d)\n",
			current->pid, parent->comm, parent->pid);
	}

	return rc;
}

static struct security_hook_list yama_hooks[] = {
	LSM_HOOK_INIT(ptrace_access_check, yama_ptrace_access_check),
	LSM_HOOK_INIT(ptrace_traceme, yama_ptrace_traceme),
	LSM_HOOK_INIT(task_prctl, yama_task_prctl),
	LSM_HOOK_INIT(task_free, yama_task_free),
};

void __init yama_add_hooks(void)
{
	security_add_hooks(yama_hooks, ARRAY_SIZE(yama_hooks));
}

#ifdef CONFIG_SYSCTL
static int yama_dointvec_minmax(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table table_copy;

	if (write && !capable(CAP_SYS_PTRACE))
		return -EPERM;

	/* Lock the max value if it ever gets set. */
	table_copy = *table;
	if (*(int *)table_copy.data == *(int *)table_copy.extra2)
		table_copy.extra1 = table_copy.extra2;

	return proc_dointvec_minmax(&table_copy, write, buffer, lenp, ppos);
}

static int zero;
static int max_scope = YAMA_SCOPE_NO_ATTACH;

struct ctl_path yama_sysctl_path[] = {
	{ .procname = "kernel", },
	{ .procname = "yama", },
	{ }
};

static struct ctl_table yama_sysctl_table[] = {
	{
		.procname       = "ptrace_scope",
		.data           = &ptrace_scope,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = yama_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &max_scope,
	},
	{ }
};
#endif /* CONFIG_SYSCTL */

static __init int yama_init(void)
{
#ifndef CONFIG_SECURITY_YAMA_STACKED
	/*
	 * If yama is being stacked this is already taken care of.
	 */
	if (!security_module_enable("yama"))
		return 0;
	yama_add_hooks();
#endif
	pr_info("Yama: becoming mindful.\n");

#ifdef CONFIG_SYSCTL
	if (!register_sysctl_paths(yama_sysctl_path, yama_sysctl_table))
		panic("Yama: sysctl registration failed.\n");
#endif

	return 0;
}

security_initcall(yama_init);
