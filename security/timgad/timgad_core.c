/*
 * Timgad Linux Security Module
 *
 * Author: Djalal Harouni
 *
 * Copyright (C) 2017 Endocode AG.
 * Copyright (c) 2016 Djalal Harouni
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/prctl.h>
#include <linux/rhashtable.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct timgad_task {
	atomic_t usage;

	struct rhash_head node;
	unsigned long key;

	struct task_struct *task;

	int mod_harden:2;

	struct work_struct clean_work;
};

static struct rhashtable timgad_tasks_table;

static inline int cmp_timgad_task(struct rhashtable_compare_arg *arg,
				  const void *obj)
{
	const unsigned long key = *(unsigned long *)arg->key;
	const struct timgad_task *ttask = obj;

	return atomic_read(&ttask->usage) == 0 || ttask->key != key;
}

static const struct rhashtable_params timgad_tasks_params = { };

int timgad_tasks_init(void)
{
	return rhashtable_init(&timgad_tasks_table, &timgad_tasks_params);
}

void timgad_tasks_clean(void)
{
	rhashtable_destroy(&timgad_tasks_table);
}

int timgad_task_set_flag(struct timgad_task *timgad_tsk, unsigned long op,
			 unsigned long flag, unsigned long value)
{
	int ret = -EINVAL;

	return ret;
}

int timgad_task_is_op_set(struct timgad_task *timgad_tsk, unsigned long op)
{
	int ret = -EINVAL;

	return ret;
}

static struct timgad_task *__lookup_timgad_task(struct task_struct *tsk)
{
	return rhashtable_lookup_fast(&timgad_tasks_table, tsk,
				      timgad_tasks_params);
}

struct timgad_task *get_timgad_task(struct task_struct *tsk)
{
	struct timgad_task ttask;

	return NULL;
}

void put_timgad_task(struct timgad_task *timgad_tsk)
{
	return;
}

struct timgad_task *lookup_timgad_task(struct task_struct *tsk)
{
	return NULL;
}

static void reclaim_timgad_task(struct work_struct *work)
{
	struct timgad_task *ttask = container_of(work, struct timgad_task,
						 clean_work);

	WARN_ON(atomic_read(&ttask->usage) != 0);

	rhashtable_remove_fast(&timgad_tasks_table, &ttask->node,
			       timgad_tasks_params);

	kfree(ttask);
}
