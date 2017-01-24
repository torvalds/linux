/*
 * Timgad Linux Security Module
 *
 * Author: Djalal Harouni
 *
 * Copyright (c) 2016 Djalal Harouni
 * Copyright (c) 2017 Endocode AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */

#define TIMGAD_MOD_HARDEN			0x00000001
#define TIMGAD_MOD_HARDEN_STRICT		0x00000002

#define TIMGAD_OPTS_ALL					\
	((unsigned long) (TIMGAD_MOD_HARDEN |		\
			  TIMGAD_MOD_HARDEN_STRICT))

struct timgad_task;

static inline int timgad_op_to_flag(unsigned long op,
				    unsigned long value,
				    unsigned long *rvalue)
{
	return 0;
}

int timgad_task_set_op_flag(struct timgad_task *timgad_tsk,
			    unsigned long op, unsigned long flag,
			    unsigned long value);

int timgad_task_is_op_set(struct timgad_task *timgad_tsk, unsigned long op);

struct timgad_task *get_timgad_task(struct task_struct *tsk);
void put_timgad_task(struct timgad_task *timgad_tsk);
struct timgad_task *lookup_timgad_task(struct task_struct *tsk);
int insert_timgad_task(struct timgad_task *timgad_tsk);

struct timgad_task *init_timgad_task(struct task_struct *tsk,
				     unsigned long flag);
struct timgad_task *give_me_timgad_task(struct task_struct *tsk,
					unsigned long value);

int timgad_tasks_init(void);
void timgad_tasks_clean(void);
