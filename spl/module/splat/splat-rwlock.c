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
 *  Solaris Porting LAyer Tests (SPLAT) Read/Writer Lock Tests.
\*****************************************************************************/

#include <sys/random.h>
#include <sys/rwlock.h>
#include <sys/taskq.h>
#include <linux/delay.h>
#include <linux/mm_compat.h>
#include "splat-internal.h"

#define SPLAT_RWLOCK_NAME		"rwlock"
#define SPLAT_RWLOCK_DESC		"Kernel RW Lock Tests"

#define SPLAT_RWLOCK_TEST1_ID		0x0701
#define SPLAT_RWLOCK_TEST1_NAME		"N-rd/1-wr"
#define SPLAT_RWLOCK_TEST1_DESC		"Multiple readers one writer"

#define SPLAT_RWLOCK_TEST2_ID		0x0702
#define SPLAT_RWLOCK_TEST2_NAME		"0-rd/N-wr"
#define SPLAT_RWLOCK_TEST2_DESC		"Multiple writers"

#define SPLAT_RWLOCK_TEST3_ID		0x0703
#define SPLAT_RWLOCK_TEST3_NAME		"held"
#define SPLAT_RWLOCK_TEST3_DESC		"RW_{LOCK|READ|WRITE}_HELD"

#define SPLAT_RWLOCK_TEST4_ID		0x0704
#define SPLAT_RWLOCK_TEST4_NAME		"tryenter"
#define SPLAT_RWLOCK_TEST4_DESC		"Tryenter"

#define SPLAT_RWLOCK_TEST5_ID		0x0705
#define SPLAT_RWLOCK_TEST5_NAME		"rw_downgrade"
#define SPLAT_RWLOCK_TEST5_DESC		"Write downgrade"

#define SPLAT_RWLOCK_TEST6_ID		0x0706
#define SPLAT_RWLOCK_TEST6_NAME		"rw_tryupgrade-1"
#define SPLAT_RWLOCK_TEST6_DESC		"rwsem->count value"

#define SPLAT_RWLOCK_TEST7_ID		0x0707
#define SPLAT_RWLOCK_TEST7_NAME		"rw_tryupgrade-2"
#define SPLAT_RWLOCK_TEST7_DESC		"Read upgrade"

#define SPLAT_RWLOCK_TEST_MAGIC		0x115599DDUL
#define SPLAT_RWLOCK_TEST_NAME		"rwlock_test"
#define SPLAT_RWLOCK_TEST_TASKQ		"rwlock_taskq"
#define SPLAT_RWLOCK_TEST_COUNT		8

#define SPLAT_RWLOCK_RELEASE_INIT	0
#define SPLAT_RWLOCK_RELEASE_WR		1
#define SPLAT_RWLOCK_RELEASE_RD		2

typedef struct rw_priv {
	unsigned long rw_magic;
	struct file *rw_file;
	krwlock_t rw_rwlock;
	spinlock_t rw_lock;
	spl_wait_queue_head_t rw_waitq;
	int rw_completed;
	int rw_holders;
	int rw_waiters;
	int rw_release;
	int rw_rc;
	krw_t rw_type;
} rw_priv_t;

typedef struct rw_thr {
	const char *rwt_name;
	rw_priv_t *rwt_rwp;
	struct task_struct *rwt_thread;
} rw_thr_t;

void splat_init_rw_priv(rw_priv_t *rwp, struct file *file)
{
	rwp->rw_magic = SPLAT_RWLOCK_TEST_MAGIC;
	rwp->rw_file = file;
	rw_init(&rwp->rw_rwlock, SPLAT_RWLOCK_TEST_NAME, RW_DEFAULT, NULL);
	spin_lock_init(&rwp->rw_lock);
	init_waitqueue_head(&rwp->rw_waitq);
	rwp->rw_completed = 0;
	rwp->rw_holders = 0;
	rwp->rw_waiters = 0;
	rwp->rw_release = SPLAT_RWLOCK_RELEASE_INIT;
	rwp->rw_rc = 0;
	rwp->rw_type = 0;
}

#if defined(CONFIG_PREEMPT_RT_FULL)
static int
splat_rwlock_test1(struct file *file, void *arg)
{
	/*
	 * This test will never succeed on PREEMPT_RT_FULL because these
	 * kernels only allow a single thread to hold the lock.
	 */
	return 0;
}
#else
static int
splat_rwlock_wr_thr(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwp = rwt->rwt_rwp;
	uint8_t rnd;

	ASSERT(rwp->rw_magic == SPLAT_RWLOCK_TEST_MAGIC);

	get_random_bytes((void *)&rnd, 1);
	msleep((unsigned int)rnd);

	splat_vprint(rwp->rw_file, rwt->rwt_name,
	    "%s trying to acquire rwlock (%d holding/%d waiting)\n",
	    rwt->rwt_thread->comm, rwp->rw_holders, rwp->rw_waiters);
	spin_lock(&rwp->rw_lock);
	rwp->rw_waiters++;
	spin_unlock(&rwp->rw_lock);
	rw_enter(&rwp->rw_rwlock, RW_WRITER);

	spin_lock(&rwp->rw_lock);
	rwp->rw_waiters--;
	rwp->rw_holders++;
	spin_unlock(&rwp->rw_lock);
	splat_vprint(rwp->rw_file, rwt->rwt_name,
	    "%s acquired rwlock (%d holding/%d waiting)\n",
	    rwt->rwt_thread->comm, rwp->rw_holders, rwp->rw_waiters);

	/* Wait for control thread to signal we can release the write lock */
	wait_event_interruptible(rwp->rw_waitq, splat_locked_test(&rwp->rw_lock,
	    rwp->rw_release == SPLAT_RWLOCK_RELEASE_WR));

	spin_lock(&rwp->rw_lock);
	rwp->rw_completed++;
	rwp->rw_holders--;
	spin_unlock(&rwp->rw_lock);
	splat_vprint(rwp->rw_file, rwt->rwt_name,
	    "%s dropped rwlock (%d holding/%d waiting)\n",
	    rwt->rwt_thread->comm, rwp->rw_holders, rwp->rw_waiters);

	rw_exit(&rwp->rw_rwlock);

	return 0;
}

static int
splat_rwlock_rd_thr(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwp = rwt->rwt_rwp;
	uint8_t rnd;

	ASSERT(rwp->rw_magic == SPLAT_RWLOCK_TEST_MAGIC);

	get_random_bytes((void *)&rnd, 1);
	msleep((unsigned int)rnd);

	/* Don't try and take the semaphore until after someone has it */
	wait_event_interruptible(rwp->rw_waitq,
	    splat_locked_test(&rwp->rw_lock, rwp->rw_holders > 0));

	splat_vprint(rwp->rw_file, rwt->rwt_name,
	    "%s trying to acquire rwlock (%d holding/%d waiting)\n",
	    rwt->rwt_thread->comm, rwp->rw_holders, rwp->rw_waiters);
	spin_lock(&rwp->rw_lock);
	rwp->rw_waiters++;
	spin_unlock(&rwp->rw_lock);
	rw_enter(&rwp->rw_rwlock, RW_READER);

	spin_lock(&rwp->rw_lock);
	rwp->rw_waiters--;
	rwp->rw_holders++;
	spin_unlock(&rwp->rw_lock);
	splat_vprint(rwp->rw_file, rwt->rwt_name,
	    "%s acquired rwlock (%d holding/%d waiting)\n",
	    rwt->rwt_thread->comm, rwp->rw_holders, rwp->rw_waiters);

	/* Wait for control thread to signal we can release the read lock */
	wait_event_interruptible(rwp->rw_waitq, splat_locked_test(&rwp->rw_lock,
	    rwp->rw_release == SPLAT_RWLOCK_RELEASE_RD));

	spin_lock(&rwp->rw_lock);
	rwp->rw_completed++;
	rwp->rw_holders--;
	spin_unlock(&rwp->rw_lock);
	splat_vprint(rwp->rw_file, rwt->rwt_name,
	    "%s dropped rwlock (%d holding/%d waiting)\n",
	    rwt->rwt_thread->comm, rwp->rw_holders, rwp->rw_waiters);

	rw_exit(&rwp->rw_rwlock);

	return 0;
}

static int
splat_rwlock_test1(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	rw_thr_t rwt[SPLAT_RWLOCK_TEST_COUNT];
	rw_priv_t *rwp;

	rwp = (rw_priv_t *)kmalloc(sizeof(*rwp), GFP_KERNEL);
	if (rwp == NULL)
		return -ENOMEM;

	splat_init_rw_priv(rwp, file);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < SPLAT_RWLOCK_TEST_COUNT; i++) {
		rwt[i].rwt_rwp = rwp;
		rwt[i].rwt_name = SPLAT_RWLOCK_TEST1_NAME;

		/* The first thread will be the writer */
		if (i == 0)
			rwt[i].rwt_thread = spl_kthread_create(splat_rwlock_wr_thr,
			    &rwt[i], "%s/%d", SPLAT_RWLOCK_TEST_NAME, i);
		else
			rwt[i].rwt_thread = spl_kthread_create(splat_rwlock_rd_thr,
			    &rwt[i], "%s/%d", SPLAT_RWLOCK_TEST_NAME, i);

		if (!IS_ERR(rwt[i].rwt_thread)) {
			wake_up_process(rwt[i].rwt_thread);
			count++;
		}
	}

	/* Wait for the writer */
	while (splat_locked_test(&rwp->rw_lock, rwp->rw_holders == 0)) {
		wake_up_interruptible(&rwp->rw_waitq);
		msleep(100);
	}

	/* Wait for 'count-1' readers */
	while (splat_locked_test(&rwp->rw_lock, rwp->rw_waiters < count - 1)) {
		wake_up_interruptible(&rwp->rw_waitq);
		msleep(100);
	}

	/* Verify there is only one lock holder */
	if (splat_locked_test(&rwp->rw_lock, rwp->rw_holders) != 1) {
		splat_vprint(file, SPLAT_RWLOCK_TEST1_NAME, "Only 1 holder "
			     "expected for rwlock (%d holding/%d waiting)\n",
			     rwp->rw_holders, rwp->rw_waiters);
		rc = -EINVAL;
	}

	/* Verify 'count-1' readers */
	if (splat_locked_test(&rwp->rw_lock, rwp->rw_waiters != count - 1)) {
		splat_vprint(file, SPLAT_RWLOCK_TEST1_NAME, "Only %d waiters "
			     "expected for rwlock (%d holding/%d waiting)\n",
			     count - 1, rwp->rw_holders, rwp->rw_waiters);
		rc = -EINVAL;
	}

	/* Signal the writer to release, allows readers to acquire */
	spin_lock(&rwp->rw_lock);
	rwp->rw_release = SPLAT_RWLOCK_RELEASE_WR;
	wake_up_interruptible(&rwp->rw_waitq);
	spin_unlock(&rwp->rw_lock);

	/* Wait for 'count-1' readers to hold the lock */
	while (splat_locked_test(&rwp->rw_lock, rwp->rw_holders < count - 1)) {
		wake_up_interruptible(&rwp->rw_waitq);
		msleep(100);
	}

	/* Verify there are 'count-1' readers */
	if (splat_locked_test(&rwp->rw_lock, rwp->rw_holders != count - 1)) {
		splat_vprint(file, SPLAT_RWLOCK_TEST1_NAME, "Only %d holders "
			     "expected for rwlock (%d holding/%d waiting)\n",
			     count - 1, rwp->rw_holders, rwp->rw_waiters);
		rc = -EINVAL;
	}

	/* Release 'count-1' readers */
	spin_lock(&rwp->rw_lock);
	rwp->rw_release = SPLAT_RWLOCK_RELEASE_RD;
	wake_up_interruptible(&rwp->rw_waitq);
	spin_unlock(&rwp->rw_lock);

	/* Wait for the test to complete */
	while (splat_locked_test(&rwp->rw_lock,
				 rwp->rw_holders>0 || rwp->rw_waiters>0))
		msleep(100);

	rw_destroy(&(rwp->rw_rwlock));
	kfree(rwp);

	return rc;
}
#endif

static void
splat_rwlock_test2_func(void *arg)
{
	rw_priv_t *rwp = (rw_priv_t *)arg;
	int rc;
	ASSERT(rwp->rw_magic == SPLAT_RWLOCK_TEST_MAGIC);

	/* Read the value before sleeping and write it after we wake up to
	 * maximize the chance of a race if rwlocks are not working properly */
	rw_enter(&rwp->rw_rwlock, RW_WRITER);
	rc = rwp->rw_rc;
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ / 100);  /* 1/100 of a second */
	VERIFY(rwp->rw_rc == rc);
	rwp->rw_rc = rc + 1;
	rw_exit(&rwp->rw_rwlock);
}

static int
splat_rwlock_test2(struct file *file, void *arg)
{
	rw_priv_t *rwp;
	taskq_t *tq;
	int i, rc = 0, tq_count = 256;

	rwp = (rw_priv_t *)kmalloc(sizeof(*rwp), GFP_KERNEL);
	if (rwp == NULL)
		return -ENOMEM;

	splat_init_rw_priv(rwp, file);

	/* Create several threads allowing tasks to race with each other */
	tq = taskq_create(SPLAT_RWLOCK_TEST_TASKQ, num_online_cpus(),
			  defclsyspri, 50, INT_MAX, TASKQ_PREPOPULATE);
	if (tq == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/*
	 * Schedule N work items to the work queue each of which enters the
	 * writer rwlock, sleeps briefly, then exits the writer rwlock.  On a
	 * multiprocessor box these work items will be handled by all available
	 * CPUs.  The task function checks to ensure the tracked shared variable
	 * is always only incremented by one.  Additionally, the rwlock itself
	 * is instrumented such that if any two processors are in the
	 * critical region at the same time the system will panic.  If the
	 * rwlock is implemented right this will never happy, that's a pass.
	 */
	for (i = 0; i < tq_count; i++) {
		if (taskq_dispatch(tq, splat_rwlock_test2_func, rwp,
		    TQ_SLEEP) == TASKQID_INVALID) {
			splat_vprint(file, SPLAT_RWLOCK_TEST2_NAME,
				     "Failed to queue task %d\n", i);
			rc = -EINVAL;
		}
	}

	taskq_wait(tq);

	if (rwp->rw_rc == tq_count) {
		splat_vprint(file, SPLAT_RWLOCK_TEST2_NAME, "%d racing threads "
			     "correctly entered/exited the rwlock %d times\n",
			     num_online_cpus(), rwp->rw_rc);
	} else {
		splat_vprint(file, SPLAT_RWLOCK_TEST2_NAME, "%d racing threads "
			     "only processed %d/%d w rwlock work items\n",
			     num_online_cpus(), rwp->rw_rc, tq_count);
		rc = -EINVAL;
	}

	taskq_destroy(tq);
	rw_destroy(&(rwp->rw_rwlock));
out:
	kfree(rwp);
	return rc;
}

#define splat_rwlock_test3_helper(rwp,rex1,rex2,wex1,wex2,held_func,rc)	\
do {									\
	int result, _rc1_, _rc2_, _rc3_, _rc4_;				\
									\
	rc = 0;								\
	rw_enter(&(rwp)->rw_rwlock, RW_READER);				\
	_rc1_ = ((result = held_func(&(rwp)->rw_rwlock)) != rex1);	\
	splat_vprint(file, SPLAT_RWLOCK_TEST3_NAME, "%s" #held_func	\
		     " returned %d (expected %d) when RW_READER\n",	\
		     _rc1_ ? "Fail " : "", result, rex1);		\
	rw_exit(&(rwp)->rw_rwlock);					\
	_rc2_ = ((result = held_func(&(rwp)->rw_rwlock)) != rex2);	\
	splat_vprint(file, SPLAT_RWLOCK_TEST3_NAME, "%s" #held_func	\
		     " returned %d (expected %d) when !RW_READER\n",	\
		     _rc2_ ? "Fail " : "", result, rex2);		\
									\
	rw_enter(&(rwp)->rw_rwlock, RW_WRITER);				\
	_rc3_ = ((result = held_func(&(rwp)->rw_rwlock)) != wex1);	\
	splat_vprint(file, SPLAT_RWLOCK_TEST3_NAME, "%s" #held_func	\
		     " returned %d (expected %d) when RW_WRITER\n",	\
		     _rc3_ ? "Fail " : "", result, wex1);		\
	rw_exit(&(rwp)->rw_rwlock);					\
	_rc4_ = ((result = held_func(&(rwp)->rw_rwlock)) != wex2);	\
	splat_vprint(file, SPLAT_RWLOCK_TEST3_NAME, "%s" #held_func	\
		     " returned %d (expected %d) when !RW_WRITER\n",	\
		     _rc4_ ? "Fail " : "", result, wex2);		\
									\
	rc = ((_rc1_ ||  _rc2_ || _rc3_ || _rc4_) ? -EINVAL : 0);	\
} while(0);

static int
splat_rwlock_test3(struct file *file, void *arg)
{
	rw_priv_t *rwp;
	int rc1, rc2, rc3;

	rwp = (rw_priv_t *)kmalloc(sizeof(*rwp), GFP_KERNEL);
	if (rwp == NULL)
		return -ENOMEM;

	splat_init_rw_priv(rwp, file);

	splat_rwlock_test3_helper(rwp, 1, 0, 1, 0, RW_LOCK_HELD, rc1);
	splat_rwlock_test3_helper(rwp, 1, 0, 0, 0, RW_READ_HELD, rc2);
	splat_rwlock_test3_helper(rwp, 0, 0, 1, 0, RW_WRITE_HELD, rc3);

	rw_destroy(&rwp->rw_rwlock);
	kfree(rwp);

	return ((rc1 || rc2 || rc3) ? -EINVAL : 0);
}

static void
splat_rwlock_test4_func(void *arg)
{
	rw_priv_t *rwp = (rw_priv_t *)arg;
	ASSERT(rwp->rw_magic == SPLAT_RWLOCK_TEST_MAGIC);

	if (rw_tryenter(&rwp->rw_rwlock, rwp->rw_type)) {
		rwp->rw_rc = 0;
		rw_exit(&rwp->rw_rwlock);
	} else {
		rwp->rw_rc = -EBUSY;
	}
}

static char *
splat_rwlock_test4_name(krw_t type)
{
	switch (type) {
		case RW_NONE: return "RW_NONE";
		case RW_WRITER: return "RW_WRITER";
		case RW_READER: return "RW_READER";
	}

	return NULL;
}

static int
splat_rwlock_test4_type(taskq_t *tq, rw_priv_t *rwp, int expected_rc,
			krw_t holder_type, krw_t try_type)
{
	int id, rc = 0;

	/* Schedule a task function which will try and acquire the rwlock
	 * using type try_type while the rwlock is being held as holder_type.
	 * The result must match expected_rc for the test to pass */
	rwp->rw_rc = -EINVAL;
	rwp->rw_type = try_type;

	if (holder_type == RW_WRITER || holder_type == RW_READER)
		rw_enter(&rwp->rw_rwlock, holder_type);

	id = taskq_dispatch(tq, splat_rwlock_test4_func, rwp, TQ_SLEEP);
	if (id == TASKQID_INVALID) {
		splat_vprint(rwp->rw_file, SPLAT_RWLOCK_TEST4_NAME, "%s",
			     "taskq_dispatch() failed\n");
		rc = -EINVAL;
		goto out;
	}

	taskq_wait_id(tq, id);

	if (rwp->rw_rc != expected_rc)
		rc = -EINVAL;

	splat_vprint(rwp->rw_file, SPLAT_RWLOCK_TEST4_NAME,
		     "%srw_tryenter(%s) returned %d (expected %d) when %s\n",
		     rc ? "Fail " : "", splat_rwlock_test4_name(try_type),
		     rwp->rw_rc, expected_rc,
		     splat_rwlock_test4_name(holder_type));
out:
	if (holder_type == RW_WRITER || holder_type == RW_READER)
		rw_exit(&rwp->rw_rwlock);

	return rc;
}

static int
splat_rwlock_test4(struct file *file, void *arg)
{
	rw_priv_t *rwp;
	taskq_t *tq;
	int rc = 0, rc1, rc2, rc3, rc4, rc5, rc6;

	rwp = (rw_priv_t *)kmalloc(sizeof(*rwp), GFP_KERNEL);
	if (rwp == NULL)
		return -ENOMEM;

	tq = taskq_create(SPLAT_RWLOCK_TEST_TASKQ, 1, defclsyspri,
			  50, INT_MAX, TASKQ_PREPOPULATE);
	if (tq == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	splat_init_rw_priv(rwp, file);

	/*
	 * Validate all combinations of rw_tryenter() contention.
	 *
	 * The concurrent reader test is modified for PREEMPT_RT_FULL
	 * kernels which do not permit concurrent read locks to be taken
	 * from different threads.  The same thread is allowed to take
	 * the read lock multiple times.
	 */
	rc1 = splat_rwlock_test4_type(tq, rwp, -EBUSY, RW_WRITER, RW_WRITER);
	rc2 = splat_rwlock_test4_type(tq, rwp, -EBUSY, RW_WRITER, RW_READER);
	rc3 = splat_rwlock_test4_type(tq, rwp, -EBUSY, RW_READER, RW_WRITER);
#if defined(CONFIG_PREEMPT_RT_FULL)
	rc4 = splat_rwlock_test4_type(tq, rwp, -EBUSY, RW_READER, RW_READER);
#else
	rc4 = splat_rwlock_test4_type(tq, rwp, 0,      RW_READER, RW_READER);
#endif
	rc5 = splat_rwlock_test4_type(tq, rwp, 0,      RW_NONE,   RW_WRITER);
	rc6 = splat_rwlock_test4_type(tq, rwp, 0,      RW_NONE,   RW_READER);

	if (rc1 || rc2 || rc3 || rc4 || rc5 || rc6)
		rc = -EINVAL;

	taskq_destroy(tq);
out:
	rw_destroy(&(rwp->rw_rwlock));
	kfree(rwp);

	return rc;
}

static int
splat_rwlock_test5(struct file *file, void *arg)
{
	rw_priv_t *rwp;
	int rc = -EINVAL;

	rwp = (rw_priv_t *)kmalloc(sizeof(*rwp), GFP_KERNEL);
	if (rwp == NULL)
		return -ENOMEM;

	splat_init_rw_priv(rwp, file);

	rw_enter(&rwp->rw_rwlock, RW_WRITER);
	if (!RW_WRITE_HELD(&rwp->rw_rwlock)) {
		splat_vprint(file, SPLAT_RWLOCK_TEST5_NAME,
			     "rwlock should be write lock: %d\n",
			     RW_WRITE_HELD(&rwp->rw_rwlock));
		goto out;
	}

	rw_downgrade(&rwp->rw_rwlock);
	if (!RW_READ_HELD(&rwp->rw_rwlock)) {
		splat_vprint(file, SPLAT_RWLOCK_TEST5_NAME,
			     "rwlock should be read lock: %d\n",
			     RW_READ_HELD(&rwp->rw_rwlock));
		goto out;
	}

	rc = 0;
	splat_vprint(file, SPLAT_RWLOCK_TEST5_NAME, "%s",
		     "rwlock properly downgraded\n");
out:
	rw_exit(&rwp->rw_rwlock);
	rw_destroy(&rwp->rw_rwlock);
	kfree(rwp);

	return rc;
}

static int
splat_rwlock_test6(struct file *file, void *arg)
{
	rw_priv_t *rwp;
	int rc;

	rwp = (rw_priv_t *)kmalloc(sizeof(*rwp), GFP_KERNEL);
	if (rwp == NULL)
		return -ENOMEM;

	splat_init_rw_priv(rwp, file);

	rw_enter(&rwp->rw_rwlock, RW_READER);
	if (RWSEM_COUNT(SEM(&rwp->rw_rwlock)) !=
	    SPL_RWSEM_SINGLE_READER_VALUE) {
		splat_vprint(file, SPLAT_RWLOCK_TEST6_NAME,
		    "We assumed single reader rwsem->count "
		    "should be %ld, but is %ld\n",
		    (long int)SPL_RWSEM_SINGLE_READER_VALUE,
		    (long int)RWSEM_COUNT(SEM(&rwp->rw_rwlock)));
		rc = -ENOLCK;
		goto out;
	}
	rw_exit(&rwp->rw_rwlock);

	rw_enter(&rwp->rw_rwlock, RW_WRITER);
	if (RWSEM_COUNT(SEM(&rwp->rw_rwlock)) !=
	    SPL_RWSEM_SINGLE_WRITER_VALUE) {
		splat_vprint(file, SPLAT_RWLOCK_TEST6_NAME,
		    "We assumed single writer rwsem->count "
		    "should be %ld, but is %ld\n",
		    (long int)SPL_RWSEM_SINGLE_WRITER_VALUE,
		    (long int)RWSEM_COUNT(SEM(&rwp->rw_rwlock)));
		rc = -ENOLCK;
		goto out;
	}
	rc = 0;
	splat_vprint(file, SPLAT_RWLOCK_TEST6_NAME, "%s",
		     "rwsem->count same as we assumed\n");
out:
	rw_exit(&rwp->rw_rwlock);
	rw_destroy(&rwp->rw_rwlock);
	kfree(rwp);

	return rc;
}

static int
splat_rwlock_test7(struct file *file, void *arg)
{
	rw_priv_t *rwp;
	int rc;

	rwp = (rw_priv_t *)kmalloc(sizeof(*rwp), GFP_KERNEL);
	if (rwp == NULL)
		return -ENOMEM;

	splat_init_rw_priv(rwp, file);

	rw_enter(&rwp->rw_rwlock, RW_READER);
	if (!RW_READ_HELD(&rwp->rw_rwlock)) {
		splat_vprint(file, SPLAT_RWLOCK_TEST7_NAME,
		             "rwlock should be read lock: %d\n",
			     RW_READ_HELD(&rwp->rw_rwlock));
		rc = -ENOLCK;
		goto out;
	}

	/* With one reader upgrade should never fail. */
	rc = rw_tryupgrade(&rwp->rw_rwlock);
	if (!rc) {
		splat_vprint(file, SPLAT_RWLOCK_TEST7_NAME,
			     "rwlock failed upgrade from reader: %d\n",
			     RW_READ_HELD(&rwp->rw_rwlock));
		rc = -ENOLCK;
		goto out;
	}

	if (RW_READ_HELD(&rwp->rw_rwlock) || !RW_WRITE_HELD(&rwp->rw_rwlock)) {
		splat_vprint(file, SPLAT_RWLOCK_TEST7_NAME, "rwlock should "
			   "have 0 (not %d) reader and 1 (not %d) writer\n",
			   RW_READ_HELD(&rwp->rw_rwlock),
			   RW_WRITE_HELD(&rwp->rw_rwlock));
		goto out;
	}

	rc = 0;
	splat_vprint(file, SPLAT_RWLOCK_TEST7_NAME, "%s",
		     "rwlock properly upgraded\n");
out:
	rw_exit(&rwp->rw_rwlock);
	rw_destroy(&rwp->rw_rwlock);
	kfree(rwp);

	return rc;
}

splat_subsystem_t *
splat_rwlock_init(void)
{
	splat_subsystem_t *sub;

	sub = kmalloc(sizeof(*sub), GFP_KERNEL);
	if (sub == NULL)
		return NULL;

	memset(sub, 0, sizeof(*sub));
	strncpy(sub->desc.name, SPLAT_RWLOCK_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_RWLOCK_DESC, SPLAT_DESC_SIZE);
	INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
	spin_lock_init(&sub->test_lock);
	sub->desc.id = SPLAT_SUBSYSTEM_RWLOCK;

	splat_test_init(sub, SPLAT_RWLOCK_TEST1_NAME, SPLAT_RWLOCK_TEST1_DESC,
		      SPLAT_RWLOCK_TEST1_ID, splat_rwlock_test1);
	splat_test_init(sub, SPLAT_RWLOCK_TEST2_NAME, SPLAT_RWLOCK_TEST2_DESC,
		      SPLAT_RWLOCK_TEST2_ID, splat_rwlock_test2);
	splat_test_init(sub, SPLAT_RWLOCK_TEST3_NAME, SPLAT_RWLOCK_TEST3_DESC,
		      SPLAT_RWLOCK_TEST3_ID, splat_rwlock_test3);
	splat_test_init(sub, SPLAT_RWLOCK_TEST4_NAME, SPLAT_RWLOCK_TEST4_DESC,
		      SPLAT_RWLOCK_TEST4_ID, splat_rwlock_test4);
	splat_test_init(sub, SPLAT_RWLOCK_TEST5_NAME, SPLAT_RWLOCK_TEST5_DESC,
		      SPLAT_RWLOCK_TEST5_ID, splat_rwlock_test5);
	splat_test_init(sub, SPLAT_RWLOCK_TEST6_NAME, SPLAT_RWLOCK_TEST6_DESC,
		      SPLAT_RWLOCK_TEST6_ID, splat_rwlock_test6);
	splat_test_init(sub, SPLAT_RWLOCK_TEST7_NAME, SPLAT_RWLOCK_TEST7_DESC,
		      SPLAT_RWLOCK_TEST7_ID, splat_rwlock_test7);

	return sub;
}

void
splat_rwlock_fini(splat_subsystem_t *sub)
{
	ASSERT(sub);
	splat_test_fini(sub, SPLAT_RWLOCK_TEST7_ID);
	splat_test_fini(sub, SPLAT_RWLOCK_TEST6_ID);
	splat_test_fini(sub, SPLAT_RWLOCK_TEST5_ID);
	splat_test_fini(sub, SPLAT_RWLOCK_TEST4_ID);
	splat_test_fini(sub, SPLAT_RWLOCK_TEST3_ID);
	splat_test_fini(sub, SPLAT_RWLOCK_TEST2_ID);
	splat_test_fini(sub, SPLAT_RWLOCK_TEST1_ID);
	kfree(sub);
}

int
splat_rwlock_id(void) {
	return SPLAT_SUBSYSTEM_RWLOCK;
}
