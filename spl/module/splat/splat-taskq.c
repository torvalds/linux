/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting LAyer Tests (SPLAT) Task Queue Tests.
\*****************************************************************************/

#include <sys/kmem.h>
#include <sys/vmem.h>
#include <sys/random.h>
#include <sys/taskq.h>
#include <sys/time.h>
#include <sys/timer.h>
#include <linux/delay.h>
#include "splat-internal.h"

#define SPLAT_TASKQ_NAME		"taskq"
#define SPLAT_TASKQ_DESC		"Kernel Task Queue Tests"

#define SPLAT_TASKQ_TEST1_ID		0x0201
#define SPLAT_TASKQ_TEST1_NAME		"single"
#define SPLAT_TASKQ_TEST1_DESC		"Single task queue, single task"

#define SPLAT_TASKQ_TEST2_ID		0x0202
#define SPLAT_TASKQ_TEST2_NAME		"multiple"
#define SPLAT_TASKQ_TEST2_DESC		"Multiple task queues, multiple tasks"

#define SPLAT_TASKQ_TEST3_ID		0x0203
#define SPLAT_TASKQ_TEST3_NAME		"system"
#define SPLAT_TASKQ_TEST3_DESC		"System task queue, multiple tasks"

#define SPLAT_TASKQ_TEST4_ID		0x0204
#define SPLAT_TASKQ_TEST4_NAME		"wait"
#define SPLAT_TASKQ_TEST4_DESC		"Multiple task waiting"

#define SPLAT_TASKQ_TEST5_ID		0x0205
#define SPLAT_TASKQ_TEST5_NAME		"order"
#define SPLAT_TASKQ_TEST5_DESC		"Correct task ordering"

#define SPLAT_TASKQ_TEST6_ID		0x0206
#define SPLAT_TASKQ_TEST6_NAME		"front"
#define SPLAT_TASKQ_TEST6_DESC		"Correct ordering with TQ_FRONT flag"

#define SPLAT_TASKQ_TEST7_ID		0x0207
#define SPLAT_TASKQ_TEST7_NAME		"recurse"
#define SPLAT_TASKQ_TEST7_DESC		"Single task queue, recursive dispatch"

#define SPLAT_TASKQ_TEST8_ID		0x0208
#define SPLAT_TASKQ_TEST8_NAME		"contention"
#define SPLAT_TASKQ_TEST8_DESC		"1 queue, 100 threads, 131072 tasks"

#define SPLAT_TASKQ_TEST9_ID		0x0209
#define SPLAT_TASKQ_TEST9_NAME		"delay"
#define SPLAT_TASKQ_TEST9_DESC		"Delayed task execution"

#define SPLAT_TASKQ_TEST10_ID		0x020a
#define SPLAT_TASKQ_TEST10_NAME		"cancel"
#define SPLAT_TASKQ_TEST10_DESC		"Cancel task execution"

#define SPLAT_TASKQ_TEST11_ID		0x020b
#define SPLAT_TASKQ_TEST11_NAME		"dynamic"
#define SPLAT_TASKQ_TEST11_DESC		"Dynamic task queue thread creation"

#define SPLAT_TASKQ_ORDER_MAX		8
#define SPLAT_TASKQ_DEPTH_MAX		16


typedef struct splat_taskq_arg {
	int flag;
	int id;
	atomic_t *count;
	int order[SPLAT_TASKQ_ORDER_MAX];
	unsigned int depth;
	clock_t expire;
	taskq_t *tq;
	taskq_ent_t *tqe;
	spinlock_t lock;
	struct file *file;
	const char *name;
} splat_taskq_arg_t;

typedef struct splat_taskq_id {
	int id;
	splat_taskq_arg_t *arg;
} splat_taskq_id_t;

/*
 * Create a taskq, queue a task, wait until task completes, ensure
 * task ran properly, cleanup taskq.
 */
static void
splat_taskq_test13_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST1_NAME,
	           "Taskq '%s' function '%s' setting flag\n",
	           tq_arg->name, sym2str(splat_taskq_test13_func));
	tq_arg->flag = 1;
}

static int
splat_taskq_test1_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_arg_t tq_arg;
	taskq_ent_t *tqe;

	tqe = kmem_alloc(sizeof (taskq_ent_t), KM_SLEEP);
	taskq_init_ent(tqe);

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
		     "Taskq '%s' creating (%s dispatch)\n",
	             SPLAT_TASKQ_TEST1_NAME,
		     prealloc ? "prealloc" : "dynamic");
	if ((tq = taskq_create(SPLAT_TASKQ_TEST1_NAME, 1, defclsyspri,
			       50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
		           "Taskq '%s' create failed\n",
		           SPLAT_TASKQ_TEST1_NAME);
		kmem_free(tqe, sizeof (taskq_ent_t));
		return -EINVAL;
	}

	tq_arg.flag = 0;
	tq_arg.id   = 0;
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST1_NAME;

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
	           "Taskq '%s' function '%s' dispatching\n",
	           tq_arg.name, sym2str(splat_taskq_test13_func));
	if (prealloc) {
		taskq_dispatch_ent(tq, splat_taskq_test13_func,
		                   &tq_arg, TQ_SLEEP, tqe);
		id = tqe->tqent_id;
	} else {
		id = taskq_dispatch(tq, splat_taskq_test13_func,
				    &tq_arg, TQ_SLEEP);
	}

	if (id == 0) {
		splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
		             "Taskq '%s' function '%s' dispatch failed\n",
		             tq_arg.name, sym2str(splat_taskq_test13_func));
		kmem_free(tqe, sizeof (taskq_ent_t));
		taskq_destroy(tq);
		return -EINVAL;
	}

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME, "Taskq '%s' waiting\n",
	           tq_arg.name);
	taskq_wait(tq);
	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME, "Taskq '%s' destroying\n",
	           tq_arg.name);

	kmem_free(tqe, sizeof (taskq_ent_t));
	taskq_destroy(tq);

	return (tq_arg.flag) ? 0 : -EINVAL;
}

static int
splat_taskq_test1(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test1_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test1_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Create multiple taskq's, each with multiple tasks, wait until
 * all tasks complete, ensure all tasks ran properly and in the
 * correct order.  Run order must be the same as the order submitted
 * because we only have 1 thread per taskq.  Finally cleanup the taskq.
 */
static void
splat_taskq_test2_func1(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST2_NAME,
	           "Taskq '%s/%d' function '%s' flag = %d = %d * 2\n",
	           tq_arg->name, tq_arg->id,
	           sym2str(splat_taskq_test2_func1),
	           tq_arg->flag * 2, tq_arg->flag);
	tq_arg->flag *= 2;
}

static void
splat_taskq_test2_func2(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST2_NAME,
	           "Taskq '%s/%d' function '%s' flag = %d = %d + 1\n",
	           tq_arg->name, tq_arg->id,
	           sym2str(splat_taskq_test2_func2),
	           tq_arg->flag + 1, tq_arg->flag);
	tq_arg->flag += 1;
}

#define TEST2_TASKQS                    8
#define TEST2_THREADS_PER_TASKQ         1

static int
splat_taskq_test2_impl(struct file *file, void *arg, boolean_t prealloc) {
	taskq_t *tq[TEST2_TASKQS] = { NULL };
	taskqid_t id;
	splat_taskq_arg_t *tq_args[TEST2_TASKQS] = { NULL };
	taskq_ent_t *func1_tqes = NULL;
	taskq_ent_t *func2_tqes = NULL;
	int i, rc = 0;

	func1_tqes = kmalloc(sizeof(*func1_tqes) * TEST2_TASKQS, GFP_KERNEL);
	if (func1_tqes == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	func2_tqes = kmalloc(sizeof(*func2_tqes) * TEST2_TASKQS, GFP_KERNEL);
	if (func2_tqes == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	for (i = 0; i < TEST2_TASKQS; i++) {
		taskq_init_ent(&func1_tqes[i]);
		taskq_init_ent(&func2_tqes[i]);

		tq_args[i] = kmalloc(sizeof (splat_taskq_arg_t), GFP_KERNEL);
		if (tq_args[i] == NULL) {
			rc = -ENOMEM;
			break;
		}

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			     "Taskq '%s/%d' creating (%s dispatch)\n",
			     SPLAT_TASKQ_TEST2_NAME, i,
			     prealloc ? "prealloc" : "dynamic");
		if ((tq[i] = taskq_create(SPLAT_TASKQ_TEST2_NAME,
			                  TEST2_THREADS_PER_TASKQ,
					  defclsyspri, 50, INT_MAX,
					  TASKQ_PREPOPULATE)) == NULL) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' create failed\n",
				   SPLAT_TASKQ_TEST2_NAME, i);
			rc = -EINVAL;
			break;
		}

		tq_args[i]->flag = i;
		tq_args[i]->id   = i;
		tq_args[i]->file = file;
		tq_args[i]->name = SPLAT_TASKQ_TEST2_NAME;

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
		           "Taskq '%s/%d' function '%s' dispatching\n",
			   tq_args[i]->name, tq_args[i]->id,
		           sym2str(splat_taskq_test2_func1));
		if (prealloc) {
			taskq_dispatch_ent(tq[i], splat_taskq_test2_func1,
			    tq_args[i], TQ_SLEEP, &func1_tqes[i]);
			id = func1_tqes[i].tqent_id;
		} else {
			id = taskq_dispatch(tq[i], splat_taskq_test2_func1,
			    tq_args[i], TQ_SLEEP);
		}

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' function '%s' dispatch "
			           "failed\n", tq_args[i]->name, tq_args[i]->id,
			           sym2str(splat_taskq_test2_func1));
			rc = -EINVAL;
			break;
		}

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
		           "Taskq '%s/%d' function '%s' dispatching\n",
			   tq_args[i]->name, tq_args[i]->id,
		           sym2str(splat_taskq_test2_func2));
		if (prealloc) {
			taskq_dispatch_ent(tq[i], splat_taskq_test2_func2,
			    tq_args[i], TQ_SLEEP, &func2_tqes[i]);
			id = func2_tqes[i].tqent_id;
		} else {
			id = taskq_dispatch(tq[i], splat_taskq_test2_func2,
			    tq_args[i], TQ_SLEEP);
		}

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME, "Taskq "
				     "'%s/%d' function '%s' dispatch failed\n",
			             tq_args[i]->name, tq_args[i]->id,
			             sym2str(splat_taskq_test2_func2));
			rc = -EINVAL;
			break;
		}
	}

	/* When rc is set we're effectively just doing cleanup here, so
	 * ignore new errors in that case.  They just cause noise. */
	for (i = 0; i < TEST2_TASKQS; i++) {
		if (tq_args[i] == NULL)
			continue;

		if (tq[i] != NULL) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' waiting\n",
			           tq_args[i]->name, tq_args[i]->id);
			taskq_wait(tq[i]);
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d; destroying\n",
			          tq_args[i]->name, tq_args[i]->id);

			taskq_destroy(tq[i]);

			if (!rc && tq_args[i]->flag != ((i * 2) + 1)) {
				splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
				           "Taskq '%s/%d' processed tasks "
				           "out of order; %d != %d\n",
				           tq_args[i]->name, tq_args[i]->id,
				           tq_args[i]->flag, i * 2 + 1);
				rc = -EINVAL;
			} else {
				splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
				           "Taskq '%s/%d' processed tasks "
					   "in the correct order; %d == %d\n",
				           tq_args[i]->name, tq_args[i]->id,
				           tq_args[i]->flag, i * 2 + 1);
			}

			kfree(tq_args[i]);
		}
	}
out:
	if (func1_tqes)
		kfree(func1_tqes);

	if (func2_tqes)
		kfree(func2_tqes);

	return rc;
}

static int
splat_taskq_test2(struct file *file, void *arg) {
	int rc;

	rc = splat_taskq_test2_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test2_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Use the global system task queue with a single task, wait until task
 * completes, ensure task ran properly.
 */
static int
splat_taskq_test3_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskqid_t id;
	splat_taskq_arg_t *tq_arg;
	taskq_ent_t *tqe;
	int error;

	tq_arg = kmem_alloc(sizeof (splat_taskq_arg_t), KM_SLEEP);
	tqe = kmem_alloc(sizeof (taskq_ent_t), KM_SLEEP);
	taskq_init_ent(tqe);

	tq_arg->flag = 0;
	tq_arg->id   = 0;
	tq_arg->file = file;
	tq_arg->name = SPLAT_TASKQ_TEST3_NAME;

	splat_vprint(file, SPLAT_TASKQ_TEST3_NAME,
	           "Taskq '%s' function '%s' %s dispatch\n",
	           tq_arg->name, sym2str(splat_taskq_test13_func),
		   prealloc ? "prealloc" : "dynamic");
	if (prealloc) {
		taskq_dispatch_ent(system_taskq, splat_taskq_test13_func,
		                   tq_arg, TQ_SLEEP, tqe);
		id = tqe->tqent_id;
	} else {
		id = taskq_dispatch(system_taskq, splat_taskq_test13_func,
				    tq_arg, TQ_SLEEP);
	}

	if (id == 0) {
		splat_vprint(file, SPLAT_TASKQ_TEST3_NAME,
		           "Taskq '%s' function '%s' dispatch failed\n",
		           tq_arg->name, sym2str(splat_taskq_test13_func));
		kmem_free(tqe, sizeof (taskq_ent_t));
		kmem_free(tq_arg, sizeof (splat_taskq_arg_t));
		return -EINVAL;
	}

	splat_vprint(file, SPLAT_TASKQ_TEST3_NAME, "Taskq '%s' waiting\n",
	           tq_arg->name);
	taskq_wait(system_taskq);

	error = (tq_arg->flag) ? 0 : -EINVAL;

	kmem_free(tqe, sizeof (taskq_ent_t));
	kmem_free(tq_arg, sizeof (splat_taskq_arg_t));

	return (error);
}

static int
splat_taskq_test3(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test3_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test3_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Create a taskq and dispatch a large number of tasks to the queue.
 * Then use taskq_wait() to block until all the tasks complete, then
 * cross check that all the tasks ran by checking the shared atomic
 * counter which is incremented in the task function.
 *
 * First we try with a large 'maxalloc' value, then we try with a small one.
 * We should not drop tasks when TQ_SLEEP is used in taskq_dispatch(), even
 * if the number of pending tasks is above maxalloc.
 */
static void
splat_taskq_test4_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;
	ASSERT(tq_arg);

	atomic_inc(tq_arg->count);
}

static int
splat_taskq_test4_common(struct file *file, void *arg, int minalloc,
                         int maxalloc, int nr_tasks, boolean_t prealloc)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_arg_t tq_arg;
	taskq_ent_t *tqes;
	atomic_t count;
	int i, j, rc = 0;

	tqes = kmalloc(sizeof(*tqes) * nr_tasks, GFP_KERNEL);
	if (tqes == NULL)
		return -ENOMEM;

	splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
		     "Taskq '%s' creating (%s dispatch) (%d/%d/%d)\n",
		     SPLAT_TASKQ_TEST4_NAME,
		     prealloc ? "prealloc" : "dynamic",
		     minalloc, maxalloc, nr_tasks);
	if ((tq = taskq_create(SPLAT_TASKQ_TEST4_NAME, 1, defclsyspri,
		               minalloc, maxalloc, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST4_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST4_NAME;
	tq_arg.count = &count;

	for (i = 1; i <= nr_tasks; i *= 2) {
		atomic_set(tq_arg.count, 0);
		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
		             "Taskq '%s' function '%s' dispatched %d times\n",
		             tq_arg.name, sym2str(splat_taskq_test4_func), i);

		for (j = 0; j < i; j++) {
			taskq_init_ent(&tqes[j]);

			if (prealloc) {
				taskq_dispatch_ent(tq, splat_taskq_test4_func,
				                   &tq_arg, TQ_SLEEP, &tqes[j]);
				id = tqes[j].tqent_id;
			} else {
				id = taskq_dispatch(tq, splat_taskq_test4_func,
						    &tq_arg, TQ_SLEEP);
			}

			if (id == 0) {
				splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
				        "Taskq '%s' function '%s' dispatch "
					"%d failed\n", tq_arg.name,
					sym2str(splat_taskq_test4_func), j);
					rc = -EINVAL;
					goto out;
			}
		}

		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' "
			     "waiting for %d dispatches\n", tq_arg.name, i);
		taskq_wait(tq);
		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' "
			     "%d/%d dispatches finished\n", tq_arg.name,
			     atomic_read(&count), i);
		if (atomic_read(&count) != i) {
			rc = -ERANGE;
			goto out;

		}
	}
out:
	splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' destroying\n",
	           tq_arg.name);
	taskq_destroy(tq);

out_free:
	kfree(tqes);

	return rc;
}

static int
splat_taskq_test4_impl(struct file *file, void *arg, boolean_t prealloc)
{
	int rc;

	rc = splat_taskq_test4_common(file, arg, 50, INT_MAX, 1024, prealloc);
	if (rc)
		return rc;

	rc = splat_taskq_test4_common(file, arg, 1, 1, 32, prealloc);

	return rc;
}

static int
splat_taskq_test4(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test4_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test4_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Create a taskq and dispatch a specific sequence of tasks carefully
 * crafted to validate the order in which tasks are processed.  When
 * there are multiple worker threads each thread will process the
 * next pending task as soon as it completes its current task.  This
 * means that tasks do not strictly complete in order in which they
 * were dispatched (increasing task id).  This is fine but we need to
 * verify taskq_wait_outstanding() blocks until the passed task id and
 * all lower task ids complete.  We do this by dispatching the following
 * specific sequence of tasks each of which block for N time units.
 * We then use taskq_wait_outstanding() to unblock at specific task id and
 * verify the only the expected task ids have completed and in the
 * correct order.  The two cases of interest are:
 *
 * 1) Task ids larger than the waited for task id can run and
 *    complete as long as there is an available worker thread.
 * 2) All task ids lower than the waited one must complete before
 *    unblocking even if the waited task id itself has completed.
 *
 * The following table shows each task id and how they will be
 * scheduled.  Each rows represent one time unit and each column
 * one of the three worker threads.  The places taskq_wait_outstanding()
 * must unblock for a specific id are identified as well as the
 * task ids which must have completed and their order.
 *
 *       +-----+       <--- taskq_wait_outstanding(tq, 8) unblocks
 *       |     |            Required Completion Order: 1,2,4,5,3,8,6,7
 * +-----+     |
 * |     |     |
 * |     |     +-----+
 * |     |     |  8  |
 * |     |     +-----+ <--- taskq_wait_outstanding(tq, 3) unblocks
 * |     |  7  |     |      Required Completion Order: 1,2,4,5,3
 * |     +-----+     |
 * |  6  |     |     |
 * +-----+     |     |
 * |     |  5  |     |
 * |     +-----+     |
 * |  4  |     |     |
 * +-----+     |     |
 * |  1  |  2  |  3  |
 * +-----+-----+-----+
 *
 */
static void
splat_taskq_test5_func(void *arg)
{
	splat_taskq_id_t *tq_id = (splat_taskq_id_t *)arg;
	splat_taskq_arg_t *tq_arg = tq_id->arg;
	int factor;

	/* Delays determined by above table */
	switch (tq_id->id) {
		default:		factor = 0;	break;
		case 1: case 8:		factor = 1;	break;
		case 2: case 4: case 5:	factor = 2;	break;
		case 6: case 7:		factor = 4;	break;
		case 3:			factor = 5;	break;
	}

	msleep(factor * 100);
	splat_vprint(tq_arg->file, tq_arg->name,
		     "Taskqid %d complete for taskq '%s'\n",
		     tq_id->id, tq_arg->name);

	spin_lock(&tq_arg->lock);
	tq_arg->order[tq_arg->flag] = tq_id->id;
	tq_arg->flag++;
	spin_unlock(&tq_arg->lock);
}

static int
splat_taskq_test_order(splat_taskq_arg_t *tq_arg, int *order)
{
	int i, j;

	for (i = 0; i < SPLAT_TASKQ_ORDER_MAX; i++) {
		if (tq_arg->order[i] != order[i]) {
			splat_vprint(tq_arg->file, tq_arg->name,
				     "Taskq '%s' incorrect completion "
				     "order\n", tq_arg->name);
			splat_vprint(tq_arg->file, tq_arg->name,
				     "%s", "Expected { ");

			for (j = 0; j < SPLAT_TASKQ_ORDER_MAX; j++)
				splat_print(tq_arg->file, "%d ", order[j]);

			splat_print(tq_arg->file, "%s", "}\n");
			splat_vprint(tq_arg->file, tq_arg->name,
				     "%s", "Got      { ");

			for (j = 0; j < SPLAT_TASKQ_ORDER_MAX; j++)
				splat_print(tq_arg->file, "%d ",
					    tq_arg->order[j]);

			splat_print(tq_arg->file, "%s", "}\n");
			return -EILSEQ;
		}
	}

	splat_vprint(tq_arg->file, tq_arg->name,
		     "Taskq '%s' validated correct completion order\n",
		     tq_arg->name);

	return 0;
}

static int
splat_taskq_test5_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_id_t tq_id[SPLAT_TASKQ_ORDER_MAX];
	splat_taskq_arg_t tq_arg;
	int order1[SPLAT_TASKQ_ORDER_MAX] = { 1,2,4,5,3,0,0,0 };
	int order2[SPLAT_TASKQ_ORDER_MAX] = { 1,2,4,5,3,8,6,7 };
	taskq_ent_t *tqes;
	int i, rc = 0;

	tqes = kmem_alloc(sizeof(*tqes) * SPLAT_TASKQ_ORDER_MAX, KM_SLEEP);
	memset(tqes, 0, sizeof(*tqes) * SPLAT_TASKQ_ORDER_MAX);

	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
		     "Taskq '%s' creating (%s dispatch)\n",
		     SPLAT_TASKQ_TEST5_NAME,
		     prealloc ? "prealloc" : "dynamic");
	if ((tq = taskq_create(SPLAT_TASKQ_TEST5_NAME, 3, defclsyspri,
		               50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST5_NAME);
		return -EINVAL;
	}

	tq_arg.flag = 0;
	memset(&tq_arg.order, 0, sizeof(int) * SPLAT_TASKQ_ORDER_MAX);
	spin_lock_init(&tq_arg.lock);
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST5_NAME;

	for (i = 0; i < SPLAT_TASKQ_ORDER_MAX; i++) {
		taskq_init_ent(&tqes[i]);

		tq_id[i].id = i + 1;
		tq_id[i].arg = &tq_arg;

		if (prealloc) {
			taskq_dispatch_ent(tq, splat_taskq_test5_func,
			               &tq_id[i], TQ_SLEEP, &tqes[i]);
			id = tqes[i].tqent_id;
		} else {
			id = taskq_dispatch(tq, splat_taskq_test5_func,
					    &tq_id[i], TQ_SLEEP);
		}

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
			        "Taskq '%s' function '%s' dispatch failed\n",
				tq_arg.name, sym2str(splat_taskq_test5_func));
				rc = -EINVAL;
				goto out;
		}

		if (tq_id[i].id != id) {
			splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
			        "Taskq '%s' expected taskqid %d got %d\n",
				tq_arg.name, (int)tq_id[i].id, (int)id);
				rc = -EINVAL;
				goto out;
		}
	}

	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME, "Taskq '%s' "
		     "waiting for taskqid %d completion\n", tq_arg.name, 3);
	taskq_wait_outstanding(tq, 3);
	if ((rc = splat_taskq_test_order(&tq_arg, order1)))
		goto out;

	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME, "Taskq '%s' "
		     "waiting for taskqid %d completion\n", tq_arg.name, 8);
	taskq_wait_outstanding(tq, 8);
	rc = splat_taskq_test_order(&tq_arg, order2);

out:
	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
		     "Taskq '%s' destroying\n", tq_arg.name);
	taskq_destroy(tq);

	kmem_free(tqes, sizeof(*tqes) * SPLAT_TASKQ_ORDER_MAX);

	return rc;
}

static int
splat_taskq_test5(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test5_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test5_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Create a single task queue with three threads.  Dispatch 8 tasks,
 * setting TQ_FRONT on only the last three.  Sleep after
 * dispatching tasks 1-3 to ensure they will run and hold the threads
 * busy while we dispatch the remaining tasks.  Verify that tasks 6-8
 * run before task 4-5.
 *
 * The following table shows each task id and how they will be
 * scheduled.  Each rows represent one time unit and each column
 * one of the three worker threads.
 *
 * NB: The Horizontal Line is the LAST Time unit consumed by the Task,
 *     and must be included in the factor calculation.
 *  T
 * 17->       +-----+
 * 16         | T6  |
 * 15-> +-----+     |
 * 14   | T6  |     |
 * 13-> |     |  5  +-----+
 * 12   |     |     | T6  |
 * 11-> |     +-----|     |
 * 10   |  4  | T6  |     |
 *  9-> +-----+     |  8  |
 *  8   | T5  |     |     |
 *  7-> |     |  7  +-----+
 *  6   |     |     | T7  |
 *  5-> |     +-----+     |
 *  4   |  6  |  T5 |     |
 *  3-> +-----+     |     |
 *  2   | T3  |     |     |
 *  1   |  1  |  2  |  3  |
 *  0   +-----+-----+-----+
 *
 */
static void
splat_taskq_test6_func(void *arg)
{
        /* Delays determined by above table */
        static const int factor[SPLAT_TASKQ_ORDER_MAX+1] = {0,3,5,7,6,6,5,6,6};

	splat_taskq_id_t *tq_id = (splat_taskq_id_t *)arg;
	splat_taskq_arg_t *tq_arg = tq_id->arg;

	splat_vprint(tq_arg->file, tq_arg->name,
		     "Taskqid %d starting for taskq '%s'\n",
		     tq_id->id, tq_arg->name);

        if (tq_id->id < SPLAT_TASKQ_ORDER_MAX+1) {
		msleep(factor[tq_id->id] * 50);
	}

	spin_lock(&tq_arg->lock);
	tq_arg->order[tq_arg->flag] = tq_id->id;
	tq_arg->flag++;
	spin_unlock(&tq_arg->lock);

	splat_vprint(tq_arg->file, tq_arg->name,
		     "Taskqid %d complete for taskq '%s'\n",
		     tq_id->id, tq_arg->name);
}

static int
splat_taskq_test6_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_id_t tq_id[SPLAT_TASKQ_ORDER_MAX];
	splat_taskq_arg_t tq_arg;
	int order[SPLAT_TASKQ_ORDER_MAX] = { 1,2,3,6,7,8,4,5 };
	taskq_ent_t *tqes;
	int i, rc = 0;
	uint_t tflags;

	tqes = kmem_alloc(sizeof(*tqes) * SPLAT_TASKQ_ORDER_MAX, KM_SLEEP);
	memset(tqes, 0, sizeof(*tqes) * SPLAT_TASKQ_ORDER_MAX);

	splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
		     "Taskq '%s' creating (%s dispatch)\n",
		     SPLAT_TASKQ_TEST6_NAME,
		     prealloc ? "prealloc" : "dynamic");
	if ((tq = taskq_create(SPLAT_TASKQ_TEST6_NAME, 3, defclsyspri,
		               50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST6_NAME);
		return -EINVAL;
	}

	tq_arg.flag = 0;
	memset(&tq_arg.order, 0, sizeof(int) * SPLAT_TASKQ_ORDER_MAX);
	spin_lock_init(&tq_arg.lock);
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST6_NAME;

	for (i = 0; i < SPLAT_TASKQ_ORDER_MAX; i++) {
		taskq_init_ent(&tqes[i]);

		tq_id[i].id = i + 1;
		tq_id[i].arg = &tq_arg;
		tflags = TQ_SLEEP;
		if (i > 4)
			tflags |= TQ_FRONT;

		if (prealloc) {
			taskq_dispatch_ent(tq, splat_taskq_test6_func,
			                   &tq_id[i], tflags, &tqes[i]);
			id = tqes[i].tqent_id;
		} else {
			id = taskq_dispatch(tq, splat_taskq_test6_func,
					    &tq_id[i], tflags);
		}

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
			        "Taskq '%s' function '%s' dispatch failed\n",
				tq_arg.name, sym2str(splat_taskq_test6_func));
				rc = -EINVAL;
				goto out;
		}

		if (tq_id[i].id != id) {
			splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
			        "Taskq '%s' expected taskqid %d got %d\n",
				tq_arg.name, (int)tq_id[i].id, (int)id);
				rc = -EINVAL;
				goto out;
		}
		/* Sleep to let tasks 1-3 start executing. */
		if ( i == 2 )
			msleep(100);
	}

	splat_vprint(file, SPLAT_TASKQ_TEST6_NAME, "Taskq '%s' "
		     "waiting for taskqid %d completion\n", tq_arg.name,
		     SPLAT_TASKQ_ORDER_MAX);
	taskq_wait_outstanding(tq, SPLAT_TASKQ_ORDER_MAX);
	rc = splat_taskq_test_order(&tq_arg, order);

out:
	splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
		     "Taskq '%s' destroying\n", tq_arg.name);
	taskq_destroy(tq);

	kmem_free(tqes, sizeof(*tqes) * SPLAT_TASKQ_ORDER_MAX);

	return rc;
}

static int
splat_taskq_test6(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test6_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test6_impl(file, arg, B_TRUE);

	return rc;
}

static void
splat_taskq_test7_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;
	taskqid_t id;

	ASSERT(tq_arg);

	if (tq_arg->depth >= SPLAT_TASKQ_DEPTH_MAX)
		return;

	tq_arg->depth++;

	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST7_NAME,
	             "Taskq '%s' function '%s' dispatching (depth = %u)\n",
	             tq_arg->name, sym2str(splat_taskq_test7_func),
	             tq_arg->depth);

	if (tq_arg->tqe) {
		VERIFY(taskq_empty_ent(tq_arg->tqe));
		taskq_dispatch_ent(tq_arg->tq, splat_taskq_test7_func,
		                   tq_arg, TQ_SLEEP, tq_arg->tqe);
		id = tq_arg->tqe->tqent_id;
	} else {
		id = taskq_dispatch(tq_arg->tq, splat_taskq_test7_func,
		                    tq_arg, TQ_SLEEP);
	}

	if (id == 0) {
		splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST7_NAME,
		             "Taskq '%s' function '%s' dispatch failed "
		             "(depth = %u)\n", tq_arg->name,
		             sym2str(splat_taskq_test7_func), tq_arg->depth);
		tq_arg->flag = -EINVAL;
		return;
	}
}

static int
splat_taskq_test7_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskq_t *tq;
	splat_taskq_arg_t *tq_arg;
	taskq_ent_t *tqe;
	int error;

	splat_vprint(file, SPLAT_TASKQ_TEST7_NAME,
	             "Taskq '%s' creating (%s dispatch)\n",
	             SPLAT_TASKQ_TEST7_NAME,
	             prealloc ? "prealloc" :  "dynamic");
	if ((tq = taskq_create(SPLAT_TASKQ_TEST7_NAME, 1, defclsyspri,
	                       50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST7_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST7_NAME);
		return -EINVAL;
	}

	tq_arg = kmem_alloc(sizeof (splat_taskq_arg_t), KM_SLEEP);
	tqe = kmem_alloc(sizeof (taskq_ent_t), KM_SLEEP);

	tq_arg->depth = 0;
	tq_arg->flag  = 0;
	tq_arg->id    = 0;
	tq_arg->file  = file;
	tq_arg->name  = SPLAT_TASKQ_TEST7_NAME;
	tq_arg->tq    = tq;

	if (prealloc) {
		taskq_init_ent(tqe);
		tq_arg->tqe = tqe;
	} else {
		tq_arg->tqe = NULL;
	}

	splat_taskq_test7_func(tq_arg);

	if (tq_arg->flag == 0) {
		splat_vprint(file, SPLAT_TASKQ_TEST7_NAME,
		             "Taskq '%s' waiting\n", tq_arg->name);
		taskq_wait_outstanding(tq, SPLAT_TASKQ_DEPTH_MAX);
	}

	error = (tq_arg->depth == SPLAT_TASKQ_DEPTH_MAX ? 0 : -EINVAL);

	kmem_free(tqe, sizeof (taskq_ent_t));
	kmem_free(tq_arg, sizeof (splat_taskq_arg_t));

	splat_vprint(file, SPLAT_TASKQ_TEST7_NAME,
	              "Taskq '%s' destroying\n", tq_arg->name);
	taskq_destroy(tq);

	return (error);
}

static int
splat_taskq_test7(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test7_impl(file, arg, B_FALSE);
	if (rc)
		return (rc);

	rc = splat_taskq_test7_impl(file, arg, B_TRUE);

	return (rc);
}

static void
splat_taskq_throughput_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;
	ASSERT(tq_arg);

	atomic_inc(tq_arg->count);
}

static int
splat_taskq_throughput(struct file *file, void *arg, const char *name,
    int nthreads, int minalloc, int maxalloc, int flags, int tasks,
    struct timespec *delta)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_arg_t tq_arg;
	taskq_ent_t **tqes;
	atomic_t count;
	struct timespec start, stop;
	int i, j, rc = 0;

	tqes = vmalloc(sizeof (*tqes) * tasks);
	if (tqes == NULL)
		return (-ENOMEM);

	memset(tqes, 0, sizeof (*tqes) * tasks);

	splat_vprint(file, name, "Taskq '%s' creating (%d/%d/%d/%d)\n",
	    name, nthreads, minalloc, maxalloc, tasks);
	if ((tq = taskq_create(name, nthreads, defclsyspri,
	    minalloc, maxalloc, flags)) == NULL) {
		splat_vprint(file, name, "Taskq '%s' create failed\n", name);
		rc = -EINVAL;
		goto out_free;
	}

	tq_arg.file = file;
	tq_arg.name = name;
	tq_arg.count = &count;
	atomic_set(tq_arg.count, 0);

	getnstimeofday(&start);

	for (i = 0; i < tasks; i++) {
		tqes[i] = kmalloc(sizeof (taskq_ent_t), GFP_KERNEL);
		if (tqes[i] == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		taskq_init_ent(tqes[i]);
		taskq_dispatch_ent(tq, splat_taskq_throughput_func,
		    &tq_arg, TQ_SLEEP, tqes[i]);
		id = tqes[i]->tqent_id;

		if (id == 0) {
			splat_vprint(file, name, "Taskq '%s' function '%s' "
			    "dispatch %d failed\n", tq_arg.name,
			    sym2str(splat_taskq_throughput_func), i);
			rc = -EINVAL;
			goto out;
		}
	}

	splat_vprint(file, name, "Taskq '%s' waiting for %d dispatches\n",
	    tq_arg.name, tasks);

	taskq_wait(tq);

	if (delta != NULL) {
		getnstimeofday(&stop);
		*delta = timespec_sub(stop, start);
	}

	splat_vprint(file, name, "Taskq '%s' %d/%d dispatches finished\n",
	    tq_arg.name, atomic_read(tq_arg.count), tasks);

	if (atomic_read(tq_arg.count) != tasks)
		rc = -ERANGE;

out:
	splat_vprint(file, name, "Taskq '%s' destroying\n", tq_arg.name);
	taskq_destroy(tq);
out_free:
	for (j = 0; j < tasks && tqes[j] != NULL; j++)
		kfree(tqes[j]);

	vfree(tqes);

	return (rc);
}

/*
 * Create a taskq with 100 threads and dispatch a huge number of trivial
 * tasks to generate contention on tq->tq_lock.  This test should always
 * pass.  The purpose is to provide a benchmark for measuring the
 * effectiveness of taskq optimizations.
 */
#define	TEST8_NUM_TASKS			0x20000
#define	TEST8_THREADS_PER_TASKQ		100

static int
splat_taskq_test8(struct file *file, void *arg)
{
	return (splat_taskq_throughput(file, arg,
	    SPLAT_TASKQ_TEST8_NAME, TEST8_THREADS_PER_TASKQ,
	    1, INT_MAX, TASKQ_PREPOPULATE, TEST8_NUM_TASKS, NULL));
}

/*
 * Create a taskq and dispatch a number of delayed tasks to the queue.
 * For each task verify that it was run no early than requested.
 */
static void
splat_taskq_test9_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;
	ASSERT(tq_arg);

	if (ddi_time_after_eq(ddi_get_lbolt(), tq_arg->expire))
		atomic_inc(tq_arg->count);

	kmem_free(tq_arg, sizeof(splat_taskq_arg_t));
}

static int
splat_taskq_test9(struct file *file, void *arg)
{
	taskq_t *tq;
	atomic_t count;
	int i, rc = 0;
	int minalloc = 1;
	int maxalloc = 10;
	int nr_tasks = 100;

	splat_vprint(file, SPLAT_TASKQ_TEST9_NAME,
	    "Taskq '%s' creating (%s dispatch) (%d/%d/%d)\n",
	    SPLAT_TASKQ_TEST9_NAME, "delay", minalloc, maxalloc, nr_tasks);
	if ((tq = taskq_create(SPLAT_TASKQ_TEST9_NAME, 3, defclsyspri,
	    minalloc, maxalloc, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST9_NAME,
		    "Taskq '%s' create failed\n", SPLAT_TASKQ_TEST9_NAME);
		return -EINVAL;
	}

	atomic_set(&count, 0);

	for (i = 1; i <= nr_tasks; i++) {
		splat_taskq_arg_t *tq_arg;
		taskqid_t id;
		uint32_t rnd;

		/* A random timeout in jiffies of at most 5 seconds */
		get_random_bytes((void *)&rnd, 4);
		rnd = rnd % (5 * HZ);

		tq_arg = kmem_alloc(sizeof(splat_taskq_arg_t), KM_SLEEP);
		tq_arg->file = file;
		tq_arg->name = SPLAT_TASKQ_TEST9_NAME;
		tq_arg->expire = ddi_get_lbolt() + rnd;
		tq_arg->count = &count;

		splat_vprint(file, SPLAT_TASKQ_TEST9_NAME,
		    "Taskq '%s' delay dispatch %u jiffies\n",
		    SPLAT_TASKQ_TEST9_NAME, rnd);

		id = taskq_dispatch_delay(tq, splat_taskq_test9_func,
		    tq_arg, TQ_SLEEP, ddi_get_lbolt() + rnd);

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST9_NAME,
			   "Taskq '%s' delay dispatch failed\n",
			   SPLAT_TASKQ_TEST9_NAME);
			kmem_free(tq_arg, sizeof(splat_taskq_arg_t));
			taskq_wait(tq);
			rc = -EINVAL;
			goto out;
		}
	}

	splat_vprint(file, SPLAT_TASKQ_TEST9_NAME, "Taskq '%s' waiting for "
	    "%d delay dispatches\n", SPLAT_TASKQ_TEST9_NAME, nr_tasks);

	taskq_wait(tq);
	if (atomic_read(&count) != nr_tasks)
		rc = -ERANGE;

	splat_vprint(file, SPLAT_TASKQ_TEST9_NAME, "Taskq '%s' %d/%d delay "
	    "dispatches finished on time\n", SPLAT_TASKQ_TEST9_NAME,
	    atomic_read(&count), nr_tasks);
	splat_vprint(file, SPLAT_TASKQ_TEST9_NAME, "Taskq '%s' destroying\n",
	    SPLAT_TASKQ_TEST9_NAME);
out:
	taskq_destroy(tq);

	return rc;
}

/*
 * Create a taskq and dispatch then cancel tasks in the queue.
 */
static void
splat_taskq_test10_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;
	uint8_t rnd;

	if (ddi_time_after_eq(ddi_get_lbolt(), tq_arg->expire))
		atomic_inc(tq_arg->count);

	/* Randomly sleep to further perturb the system */
	get_random_bytes((void *)&rnd, 1);
	msleep(1 + (rnd % 9));
}

static int
splat_taskq_test10(struct file *file, void *arg)
{
	taskq_t *tq;
	splat_taskq_arg_t **tqas;
	atomic_t count;
	int i, j, rc = 0;
	int minalloc = 1;
	int maxalloc = 10;
	int nr_tasks = 100;
	int canceled = 0;
	int completed = 0;
	int blocked = 0;
	clock_t start, cancel;

	tqas = vmalloc(sizeof(*tqas) * nr_tasks);
	if (tqas == NULL)
		return -ENOMEM;
        memset(tqas, 0, sizeof(*tqas) * nr_tasks);

	splat_vprint(file, SPLAT_TASKQ_TEST10_NAME,
	    "Taskq '%s' creating (%s dispatch) (%d/%d/%d)\n",
	    SPLAT_TASKQ_TEST10_NAME, "delay", minalloc, maxalloc, nr_tasks);
	if ((tq = taskq_create(SPLAT_TASKQ_TEST10_NAME, 3, defclsyspri,
	    minalloc, maxalloc, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST10_NAME,
		    "Taskq '%s' create failed\n", SPLAT_TASKQ_TEST10_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	atomic_set(&count, 0);

	for (i = 0; i < nr_tasks; i++) {
		splat_taskq_arg_t *tq_arg;
		uint32_t rnd;

		/* A random timeout in jiffies of at most 5 seconds */
		get_random_bytes((void *)&rnd, 4);
		rnd = rnd % (5 * HZ);

		tq_arg = kmem_alloc(sizeof(splat_taskq_arg_t), KM_SLEEP);
		tq_arg->file = file;
		tq_arg->name = SPLAT_TASKQ_TEST10_NAME;
		tq_arg->count = &count;
		tqas[i] = tq_arg;

		/*
		 * Dispatch every 1/3 one immediately to mix it up, the cancel
		 * code is inherently racy and we want to try and provoke any
		 * subtle concurrently issues.
		 */
		if ((i % 3) == 0) {
			tq_arg->expire = ddi_get_lbolt();
			tq_arg->id = taskq_dispatch(tq, splat_taskq_test10_func,
			    tq_arg, TQ_SLEEP);
		} else {
			tq_arg->expire = ddi_get_lbolt() + rnd;
			tq_arg->id = taskq_dispatch_delay(tq,
			    splat_taskq_test10_func,
			    tq_arg, TQ_SLEEP, ddi_get_lbolt() + rnd);
		}

		if (tq_arg->id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST10_NAME,
			   "Taskq '%s' dispatch failed\n",
			   SPLAT_TASKQ_TEST10_NAME);
			kmem_free(tq_arg, sizeof(splat_taskq_arg_t));
			taskq_wait(tq);
			rc = -EINVAL;
			goto out;
		} else {
			splat_vprint(file, SPLAT_TASKQ_TEST10_NAME,
			    "Taskq '%s' dispatch %lu in %lu jiffies\n",
			    SPLAT_TASKQ_TEST10_NAME, (unsigned long)tq_arg->id,
			    !(i % 3) ? 0 : tq_arg->expire - ddi_get_lbolt());
		}
	}

	/*
	 * Start randomly canceling tasks for the duration of the test.  We
	 * happen to know the valid task id's will be in the range 1..nr_tasks
	 * because the taskq is private and was just created.  However, we
	 * have no idea of a particular task has already executed or not.
	 */
	splat_vprint(file, SPLAT_TASKQ_TEST10_NAME, "Taskq '%s' randomly "
	    "canceling task ids\n", SPLAT_TASKQ_TEST10_NAME);

	start = ddi_get_lbolt();
	i = 0;

	while (ddi_time_before(ddi_get_lbolt(), start + 5 * HZ)) {
		taskqid_t id;
		uint32_t rnd;

		i++;
		cancel = ddi_get_lbolt();
		get_random_bytes((void *)&rnd, 4);
		id = 1 + (rnd % nr_tasks);
		rc = taskq_cancel_id(tq, id);

		/*
		 * Keep track of the results of the random cancels.
		 */
		if (rc == 0) {
			canceled++;
		} else if (rc == ENOENT) {
			completed++;
		} else if (rc == EBUSY) {
			blocked++;
		} else {
			rc = -EINVAL;
			break;
		}

		/*
		 * Verify we never get blocked to long in taskq_cancel_id().
		 * The worst case is 10ms if we happen to cancel the task
		 * which is currently executing.  We allow a factor of 2x.
		 */
		if (ddi_get_lbolt() - cancel > HZ / 50) {
			splat_vprint(file, SPLAT_TASKQ_TEST10_NAME,
			    "Taskq '%s' cancel for %lu took %lu\n",
			    SPLAT_TASKQ_TEST10_NAME, (unsigned long)id,
			    ddi_get_lbolt() - cancel);
			rc = -ETIMEDOUT;
			break;
		}

		get_random_bytes((void *)&rnd, 4);
		msleep(1 + (rnd % 100));
		rc = 0;
	}

	taskq_wait(tq);

	/*
	 * Cross check the results of taskq_cancel_id() with the number of
	 * times the dispatched function actually ran successfully.
	 */
	if ((rc == 0) && (nr_tasks - canceled != atomic_read(&count)))
		rc = -EDOM;

	splat_vprint(file, SPLAT_TASKQ_TEST10_NAME, "Taskq '%s' %d attempts, "
	    "%d canceled, %d completed, %d blocked, %d/%d tasks run\n",
	    SPLAT_TASKQ_TEST10_NAME, i, canceled, completed, blocked,
	    atomic_read(&count), nr_tasks);
	splat_vprint(file, SPLAT_TASKQ_TEST10_NAME, "Taskq '%s' destroying %d\n",
	    SPLAT_TASKQ_TEST10_NAME, rc);
out:
	taskq_destroy(tq);
out_free:
	for (j = 0; j < nr_tasks && tqas[j] != NULL; j++)
		kmem_free(tqas[j], sizeof(splat_taskq_arg_t));
	vfree(tqas);

	return rc;
}

/*
 * Create a dynamic taskq with 100 threads and dispatch a huge number of
 * trivial tasks.  This will cause the taskq to grow quickly to its max
 * thread count.  This test should always pass.  The purpose is to provide
 * a benchmark for measuring the performance of dynamic taskqs.
 */
#define	TEST11_NUM_TASKS			100000
#define	TEST11_THREADS_PER_TASKQ		100

static int
splat_taskq_test11(struct file *file, void *arg)
{
	struct timespec normal, dynamic;
	int error;

	error = splat_taskq_throughput(file, arg, SPLAT_TASKQ_TEST11_NAME,
	    TEST11_THREADS_PER_TASKQ, 1, INT_MAX,
	    TASKQ_PREPOPULATE, TEST11_NUM_TASKS, &normal);
	if (error)
		return (error);

	error = splat_taskq_throughput(file, arg, SPLAT_TASKQ_TEST11_NAME,
	    TEST11_THREADS_PER_TASKQ, 1, INT_MAX,
	    TASKQ_PREPOPULATE | TASKQ_DYNAMIC, TEST11_NUM_TASKS, &dynamic);
	if (error)
		return (error);

	splat_vprint(file, SPLAT_TASKQ_TEST11_NAME,
	    "Timing taskq_wait(): normal=%ld.%09lds, dynamic=%ld.%09lds\n",
	    normal.tv_sec, normal.tv_nsec,
	    dynamic.tv_sec, dynamic.tv_nsec);

	/* A 10x increase in runtime is used to indicate a core problem. */
	if ((dynamic.tv_sec * NANOSEC + dynamic.tv_nsec) >
	    ((normal.tv_sec * NANOSEC + normal.tv_nsec) * 10))
		error = -ETIME;

	return (error);
}

splat_subsystem_t *
splat_taskq_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_TASKQ_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_TASKQ_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
	spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_TASKQ;

	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST1_NAME, SPLAT_TASKQ_TEST1_DESC,
	              SPLAT_TASKQ_TEST1_ID, splat_taskq_test1);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST2_NAME, SPLAT_TASKQ_TEST2_DESC,
	              SPLAT_TASKQ_TEST2_ID, splat_taskq_test2);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST3_NAME, SPLAT_TASKQ_TEST3_DESC,
	              SPLAT_TASKQ_TEST3_ID, splat_taskq_test3);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST4_NAME, SPLAT_TASKQ_TEST4_DESC,
	              SPLAT_TASKQ_TEST4_ID, splat_taskq_test4);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST5_NAME, SPLAT_TASKQ_TEST5_DESC,
	              SPLAT_TASKQ_TEST5_ID, splat_taskq_test5);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST6_NAME, SPLAT_TASKQ_TEST6_DESC,
	              SPLAT_TASKQ_TEST6_ID, splat_taskq_test6);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST7_NAME, SPLAT_TASKQ_TEST7_DESC,
	              SPLAT_TASKQ_TEST7_ID, splat_taskq_test7);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST8_NAME, SPLAT_TASKQ_TEST8_DESC,
	              SPLAT_TASKQ_TEST8_ID, splat_taskq_test8);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST9_NAME, SPLAT_TASKQ_TEST9_DESC,
	              SPLAT_TASKQ_TEST9_ID, splat_taskq_test9);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST10_NAME, SPLAT_TASKQ_TEST10_DESC,
	              SPLAT_TASKQ_TEST10_ID, splat_taskq_test10);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST11_NAME, SPLAT_TASKQ_TEST11_DESC,
	              SPLAT_TASKQ_TEST11_ID, splat_taskq_test11);

        return sub;
}

void
splat_taskq_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST11_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST10_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST9_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST8_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST7_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST6_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST5_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST4_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST3_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST2_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST1_ID);

        kfree(sub);
}

int
splat_taskq_id(void) {
        return SPLAT_SUBSYSTEM_TASKQ;
}
