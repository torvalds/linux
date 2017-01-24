/*
 * Timgad Linux Security Module
 *
 * Author: Djalal Harouni
 *
 * Copyright (C) 2017 Endocode AG.
 * Copyright (C) 2017 Djalal Harouni
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <linux/lsm_hooks.h>
#include <linux/sysctl.h>
#include <linux/prctl.h>

#include "timgad_core.h"

enum {
	TIMGAD_MOD_HARDEN_OFF	= 0,
	TIMGAD_MOD_HARDEN_ON	= 1,
};

static int module_restrict;

static int timgad_set_op_value(struct task_struct *tsk,
			       unsigned long op, unsigned long value)
{
	int ret = 0;
	struct timgad_task *ttask;
	unsigned long flag = 0;

	ret = timgad_op_to_flag(op, value, &flag);
	if (ret < 0)
		return ret;

	ttask = get_timgad_task(tsk);
	if (!ttask) {
		ttask = give_me_timgad_task(tsk, value);
		if (IS_ERR(ttask))
			return PTR_ERR(ttask);

		return 0;
	}

	ret = timgad_task_set_op_flag(ttask, op, flag, value);

	put_timgad_task(ttask);
	return ret;
}

static int timgad_get_op_value(struct task_struct *tsk, unsigned long op)
{
	int ret = -EINVAL;
	struct timgad_task *ttask;

	ttask = get_timgad_task(tsk);
	if (!ttask)
		return ret;

	ret = timgad_task_is_op_set(ttask, op);
	put_timgad_task(ttask);

	return ret;
}

int timgad_task_copy(struct task_struct *tsk)
{
	int ret = -EINVAL;
	struct timgad_task *tparent;
	struct timgad_task *ttask = NULL;


	return ret;
}

/*
 * Return 0 on success, -error on error.  -EINVAL is returned when Timgad
 * does not handle the given option.
 */
int timgad_task_prctl(int option, unsigned long arg2, unsigned long arg3,
		      unsigned long arg4, unsigned long arg5)
{
	int ret = -EINVAL;
	struct task_struct *myself = current;

	if (option != PR_TIMGAD_OPTS)
		return ret;

	get_task_struct(myself);

	switch (arg2) {
	case PR_TIMGAD_SET_MOD_HARDEN:
		ret = timgad_set_op_value(myself, PR_TIMGAD_SET_MOD_HARDEN, arg3);
		break;
	case PR_TIMGAD_GET_MOD_HARDEN:
		ret = timgad_get_op_value(myself, PR_TIMGAD_SET_MOD_HARDEN);
		break;
	}

	put_task_struct(myself);

	return ret;
}

void timgad_task_free(struct task_struct *task)
{
	struct timgad_task *ttask;

	ttask = lookup_timgad_task(task);
	if (!ttask)
		return;

	put_timgad_task(ttask);
}

static struct security_hook_list timgad_hooks[] = {
	LSM_HOOK_INIT(task_copy, timgad_task_copy),
	LSM_HOOK_INIT(task_prctl, timgad_task_prctl),
	LSM_HOOK_INIT(task_free, timgad_task_free),
};

#ifdef CONFIG_SYSCTL
static int timgad_mod_dointvec_minmax(struct ctl_table *table, int write,
				      void __user *buffer, size_t *lenp,
				      loff_t *ppos)
{
	struct ctl_table table_copy;

	if (write && !capable(CAP_SYS_MODULE))
		return -EPERM;

	/* Lock the max value if it ever gets set. */
	table_copy = *table;
	if (*(int *)table_copy.data == *(int *)table_copy.extra2)
		table_copy.extra1 = table_copy.extra2;

	return proc_dointvec_minmax(&table_copy, write, buffer, lenp, ppos);
}

static int zero;
static int max_module_restrict_scope = TIMGAD_MOD_HARDEN_ON;

struct ctl_path timgad_sysctl_path[] = {
	{ .procname = "kernel", },
	{ .procname = "timgad", },
	{ }
};

static struct ctl_table timgad_sysctl_table[] = {
	{
		.procname       = "module_restrict",
		.data           = &module_restrict,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = timgad_mod_dointvec_minmax,
		.extra1         = &zero,
		.extra2         = &max_module_restrict_scope,
	},
	{ }
};
static void __init timgad_init_sysctl(void)
{
	if (!register_sysctl_paths(timgad_sysctl_path, timgad_sysctl_table))
		panic("Timgad: sysctl registration failed.\n");
}
#else
static inline void timgad_init_sysctl(void) { }
#endif /* CONFIG_SYSCTL */

void __init timgad_add_hooks(void)
{
	pr_info("Timgad: becoming mindful.\n");
	security_add_hooks(timgad_hooks, ARRAY_SIZE(timgad_hooks));
	timgad_init_sysctl();

	if (timgad_tasks_init())
		panic("Timgad: tasks initialization failed.\n");
}
