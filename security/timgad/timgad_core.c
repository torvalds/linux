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

static const struct rhashtable_params timgad_tasks_params = {
	.nelem_hint = 1024,
	.head_offset = offsetof(struct timgad_task, node),
	.key_offset = offsetof(struct timgad_task, key),
	.key_len = sizeof(unsigned long),
	.max_size = 16384,
	.min_size = 256,
	.obj_cmpfn = cmp_timgad_task,
	.automatic_shrinking = true,
};

int timgad_tasks_init(void)
{
	return rhashtable_init(&timgad_tasks_table, &timgad_tasks_params);
}

void timgad_tasks_clean(void)
{
	rhashtable_destroy(&timgad_tasks_table);
}

static int get_timgad_task_new_flags(unsigned long op, unsigned long used,
				     unsigned long flag, int *new_flags)
{
	int ret = -EINVAL;

	return ret;
}

static int update_timgad_task_flags(struct timgad_task *timgad_tsk,
				    unsigned long op, int new_flags)
{
	int ret = -EINVAL;

	return ret;
}

int timgad_task_is_op_set(struct timgad_task *timgad_tsk, unsigned long op)
{
	if (op == PR_TIMGAD_SET_MOD_HARDEN)
		return timgad_tsk->mod_harden;

	return -EINVAL;
}

int timgad_task_set_op_flag(struct timgad_task *timgad_tsk, unsigned long op,
			    unsigned long flag, unsigned long value)
{
	int ret = -EINVAL;
	int new_flag = 0;
	int used = timgad_task_is_op_set(timgad_tsk, op);

	ret = get_timgad_task_new_flags(op, used, flag, &new_flag);
	if (ret < 0)
		return ret;

	/* Nothing to do if new flag did not change */
	if (new_flag == used)
		return 0;

	return update_timgad_task_flags(timgad_tsk, op, new_flag);
}

static struct timgad_task *__lookup_timgad_task(struct task_struct *tsk)
{
	return rhashtable_lookup_fast(&timgad_tasks_table, tsk,
				      timgad_tasks_params);
}

struct timgad_task *get_timgad_task(struct task_struct *tsk)
{
	struct timgad_task *ttask;

	rcu_read_lock();
	ttask = __lookup_timgad_task(tsk);
	if (ttask)
		atomic_inc(&ttask->usage);
	rcu_read_unlock();

	return ttask;
}

void put_timgad_task(struct timgad_task *timgad_tsk)
{
	if (timgad_tsk && atomic_dec_and_test(&timgad_tsk->usage))
		schedule_work(&timgad_tsk->clean_work);
}

struct timgad_task *lookup_timgad_task(struct task_struct *tsk)
{
	struct timgad_task *ttask;

	rcu_read_lock();
	ttask = __lookup_timgad_task(tsk);
	rcu_read_unlock();

	return ttask;
}

int insert_timgad_task(struct timgad_task *timgad_tsk)
{
	int ret;

	atomic_inc(&timgad_tsk->usage);
	ret = rhashtable_lookup_insert_key(&timgad_tasks_table,
					   timgad_tsk->task, &timgad_tsk->node,
					   timgad_tasks_params);
	if (ret)
		atomic_dec(&timgad_tsk->usage);

	return ret;
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

struct timgad_task *init_timgad_task(struct task_struct *tsk,
				     unsigned long value)
{
	struct timgad_task *ttask;

	ttask = kzalloc(sizeof(*ttask), GFP_KERNEL | __GFP_NOWARN);
	if (ttask == NULL)
		return ERR_PTR(-ENOMEM);

	ttask->task = tsk;
	ttask->mod_harden = value;

	atomic_set(&ttask->usage, 0);
	INIT_WORK(&ttask->clean_work, reclaim_timgad_task);

	return ttask;
}

/* On success, callers have to do put_timgad_task() */
struct timgad_task *give_me_timgad_task(struct task_struct *tsk,
					unsigned long value)
{
	int ret;
	struct timgad_task *ttask;

	ttask = init_timgad_task(tsk, value);
	if (IS_ERR(ttask))
		return ttask;

	/* Mark it as active */
	ret = insert_timgad_task(ttask);
	if (ret) {
		kfree(ttask);
		return ERR_PTR(ret);
	}

	return ttask;
}
