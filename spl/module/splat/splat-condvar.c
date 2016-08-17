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
 *  Solaris Porting LAyer Tests (SPLAT) Condition Variable Tests.
\*****************************************************************************/

#include <sys/condvar.h>
#include <sys/timer.h>
#include <sys/thread.h>
#include "splat-internal.h"

#define SPLAT_CONDVAR_NAME		"condvar"
#define SPLAT_CONDVAR_DESC		"Kernel Condition Variable Tests"

#define SPLAT_CONDVAR_TEST1_ID		0x0501
#define SPLAT_CONDVAR_TEST1_NAME	"signal1"
#define SPLAT_CONDVAR_TEST1_DESC	"Wake a single thread, cv_wait()/cv_signal()"

#define SPLAT_CONDVAR_TEST2_ID		0x0502
#define SPLAT_CONDVAR_TEST2_NAME	"broadcast1"
#define SPLAT_CONDVAR_TEST2_DESC	"Wake all threads, cv_wait()/cv_broadcast()"

#define SPLAT_CONDVAR_TEST3_ID		0x0503
#define SPLAT_CONDVAR_TEST3_NAME	"signal2"
#define SPLAT_CONDVAR_TEST3_DESC	"Wake a single thread, cv_wait_timeout()/cv_signal()"

#define SPLAT_CONDVAR_TEST4_ID		0x0504
#define SPLAT_CONDVAR_TEST4_NAME	"broadcast2"
#define SPLAT_CONDVAR_TEST4_DESC	"Wake all threads, cv_wait_timeout()/cv_broadcast()"

#define SPLAT_CONDVAR_TEST5_ID		0x0505
#define SPLAT_CONDVAR_TEST5_NAME	"timeout"
#define SPLAT_CONDVAR_TEST5_DESC	"Timeout thread, cv_wait_timeout()"

#define SPLAT_CONDVAR_TEST_MAGIC	0x115599DDUL
#define SPLAT_CONDVAR_TEST_NAME		"condvar"
#define SPLAT_CONDVAR_TEST_COUNT	8

typedef struct condvar_priv {
	unsigned long cv_magic;
	struct file *cv_file;
	kcondvar_t cv_condvar;
	kmutex_t cv_mtx;
} condvar_priv_t;

typedef struct condvar_thr {
	const char *ct_name;
	condvar_priv_t *ct_cvp;
	struct task_struct *ct_thread;
	int ct_rc;
} condvar_thr_t;

int
splat_condvar_test12_thread(void *arg)
{
	condvar_thr_t *ct = (condvar_thr_t *)arg;
	condvar_priv_t *cv = ct->ct_cvp;

	ASSERT(cv->cv_magic == SPLAT_CONDVAR_TEST_MAGIC);

	mutex_enter(&cv->cv_mtx);
	splat_vprint(cv->cv_file, ct->ct_name,
	    "%s thread sleeping with %d waiters\n",
	    ct->ct_thread->comm, atomic_read(&cv->cv_condvar.cv_waiters));
	cv_wait(&cv->cv_condvar, &cv->cv_mtx);
	splat_vprint(cv->cv_file, ct->ct_name,
	    "%s thread woken %d waiters remain\n",
	    ct->ct_thread->comm, atomic_read(&cv->cv_condvar.cv_waiters));
	mutex_exit(&cv->cv_mtx);

	/* wait for main thread reap us */
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static int
splat_condvar_test1(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	condvar_thr_t ct[SPLAT_CONDVAR_TEST_COUNT];
	condvar_priv_t cv;

	cv.cv_magic = SPLAT_CONDVAR_TEST_MAGIC;
	cv.cv_file = file;
	mutex_init(&cv.cv_mtx, SPLAT_CONDVAR_TEST_NAME, MUTEX_DEFAULT, NULL);
	cv_init(&cv.cv_condvar, NULL, CV_DEFAULT, NULL);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < SPLAT_CONDVAR_TEST_COUNT; i++) {
		ct[i].ct_cvp = &cv;
		ct[i].ct_name = SPLAT_CONDVAR_TEST1_NAME;
		ct[i].ct_rc = 0;
		ct[i].ct_thread = spl_kthread_create(splat_condvar_test12_thread,
		    &ct[i], "%s/%d", SPLAT_CONDVAR_TEST_NAME, i);

		if (!IS_ERR(ct[i].ct_thread)) {
			wake_up_process(ct[i].ct_thread);
			count++;
		}
	}

	/* Wait until all threads are waiting on the condition variable */
	while (atomic_read(&cv.cv_condvar.cv_waiters) != count)
		schedule();

	/* Wake a single thread at a time, wait until it exits */
	for (i = 1; i <= count; i++) {
		cv_signal(&cv.cv_condvar);

		while (atomic_read(&cv.cv_condvar.cv_waiters) > (count - i))
			schedule();

		/* Correct behavior 1 thread woken */
		if (atomic_read(&cv.cv_condvar.cv_waiters) == (count - i))
			continue;

                splat_vprint(file, SPLAT_CONDVAR_TEST1_NAME, "Attempted to "
			   "wake %d thread but work %d threads woke\n",
			   1, count - atomic_read(&cv.cv_condvar.cv_waiters));
		rc = -EINVAL;
		break;
	}

	if (!rc)
                splat_vprint(file, SPLAT_CONDVAR_TEST1_NAME, "Correctly woke "
			   "%d sleeping threads %d at a time\n", count, 1);

	/* Wait until that last nutex is dropped */
	while (mutex_owner(&cv.cv_mtx))
		schedule();

	/* Wake everything for the failure case */
	cv_broadcast(&cv.cv_condvar);
	cv_destroy(&cv.cv_condvar);

	/* wait for threads to exit */
	for (i = 0; i < SPLAT_CONDVAR_TEST_COUNT; i++) {
		if (!IS_ERR(ct[i].ct_thread))
			kthread_stop(ct[i].ct_thread);
	}
	mutex_destroy(&cv.cv_mtx);

	return rc;
}

static int
splat_condvar_test2(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	condvar_thr_t ct[SPLAT_CONDVAR_TEST_COUNT];
	condvar_priv_t cv;

	cv.cv_magic = SPLAT_CONDVAR_TEST_MAGIC;
	cv.cv_file = file;
	mutex_init(&cv.cv_mtx, SPLAT_CONDVAR_TEST_NAME, MUTEX_DEFAULT, NULL);
	cv_init(&cv.cv_condvar, NULL, CV_DEFAULT, NULL);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < SPLAT_CONDVAR_TEST_COUNT; i++) {
		ct[i].ct_cvp = &cv;
		ct[i].ct_name = SPLAT_CONDVAR_TEST2_NAME;
		ct[i].ct_rc = 0;
		ct[i].ct_thread = spl_kthread_create(splat_condvar_test12_thread,
		    &ct[i], "%s/%d", SPLAT_CONDVAR_TEST_NAME, i);

		if (!IS_ERR(ct[i].ct_thread)) {
			wake_up_process(ct[i].ct_thread);
			count++;
		}
	}

	/* Wait until all threads are waiting on the condition variable */
	while (atomic_read(&cv.cv_condvar.cv_waiters) != count)
		schedule();

	/* Wake all threads waiting on the condition variable */
	cv_broadcast(&cv.cv_condvar);

	/* Wait until all threads have exited */
	while ((atomic_read(&cv.cv_condvar.cv_waiters) > 0) || mutex_owner(&cv.cv_mtx))
		schedule();

        splat_vprint(file, SPLAT_CONDVAR_TEST2_NAME, "Correctly woke all "
			   "%d sleeping threads at once\n", count);

	/* Wake everything for the failure case */
	cv_destroy(&cv.cv_condvar);

	/* wait for threads to exit */
	for (i = 0; i < SPLAT_CONDVAR_TEST_COUNT; i++) {
		if (!IS_ERR(ct[i].ct_thread))
			kthread_stop(ct[i].ct_thread);
	}
	mutex_destroy(&cv.cv_mtx);

	return rc;
}

int
splat_condvar_test34_thread(void *arg)
{
	condvar_thr_t *ct = (condvar_thr_t *)arg;
	condvar_priv_t *cv = ct->ct_cvp;
	clock_t rc;

	ASSERT(cv->cv_magic == SPLAT_CONDVAR_TEST_MAGIC);

	mutex_enter(&cv->cv_mtx);
	splat_vprint(cv->cv_file, ct->ct_name,
	    "%s thread sleeping with %d waiters\n",
	    ct->ct_thread->comm, atomic_read(&cv->cv_condvar.cv_waiters));

	/* Sleep no longer than 3 seconds, for this test we should
	 * actually never sleep that long without being woken up. */
	rc = cv_timedwait(&cv->cv_condvar, &cv->cv_mtx, lbolt + HZ * 3);
	if (rc == -1) {
		ct->ct_rc = -ETIMEDOUT;
		splat_vprint(cv->cv_file, ct->ct_name, "%s thread timed out, "
		    "should have been woken\n", ct->ct_thread->comm);
	} else {
		splat_vprint(cv->cv_file, ct->ct_name,
		    "%s thread woken %d waiters remain\n",
		    ct->ct_thread->comm,
		    atomic_read(&cv->cv_condvar.cv_waiters));
	}

	mutex_exit(&cv->cv_mtx);

	/* wait for main thread reap us */
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static int
splat_condvar_test3(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	condvar_thr_t ct[SPLAT_CONDVAR_TEST_COUNT];
	condvar_priv_t cv;

	cv.cv_magic = SPLAT_CONDVAR_TEST_MAGIC;
	cv.cv_file = file;
	mutex_init(&cv.cv_mtx, SPLAT_CONDVAR_TEST_NAME, MUTEX_DEFAULT, NULL);
	cv_init(&cv.cv_condvar, NULL, CV_DEFAULT, NULL);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < SPLAT_CONDVAR_TEST_COUNT; i++) {
		ct[i].ct_cvp = &cv;
		ct[i].ct_name = SPLAT_CONDVAR_TEST3_NAME;
		ct[i].ct_rc = 0;
		ct[i].ct_thread = spl_kthread_create(splat_condvar_test34_thread,
		    &ct[i], "%s/%d", SPLAT_CONDVAR_TEST_NAME, i);

		if (!IS_ERR(ct[i].ct_thread)) {
			wake_up_process(ct[i].ct_thread);
			count++;
		}
	}

	/* Wait until all threads are waiting on the condition variable */
	while (atomic_read(&cv.cv_condvar.cv_waiters) != count)
		schedule();

	/* Wake a single thread at a time, wait until it exits */
	for (i = 1; i <= count; i++) {
		cv_signal(&cv.cv_condvar);

		while (atomic_read(&cv.cv_condvar.cv_waiters) > (count - i))
			schedule();

		/* Correct behavior 1 thread woken */
		if (atomic_read(&cv.cv_condvar.cv_waiters) == (count - i))
			continue;

                splat_vprint(file, SPLAT_CONDVAR_TEST3_NAME, "Attempted to "
			   "wake %d thread but work %d threads woke\n",
			   1, count - atomic_read(&cv.cv_condvar.cv_waiters));
		rc = -EINVAL;
		break;
	}

	/* Validate no waiting thread timed out early */
	for (i = 0; i < count; i++)
		if (ct[i].ct_rc)
			rc = ct[i].ct_rc;

	if (!rc)
                splat_vprint(file, SPLAT_CONDVAR_TEST3_NAME, "Correctly woke "
			   "%d sleeping threads %d at a time\n", count, 1);

	/* Wait until that last nutex is dropped */
	while (mutex_owner(&cv.cv_mtx))
		schedule();

	/* Wake everything for the failure case */
	cv_broadcast(&cv.cv_condvar);
	cv_destroy(&cv.cv_condvar);

	/* wait for threads to exit */
	for (i = 0; i < SPLAT_CONDVAR_TEST_COUNT; i++) {
		if (!IS_ERR(ct[i].ct_thread))
			kthread_stop(ct[i].ct_thread);
	}
	mutex_destroy(&cv.cv_mtx);

	return rc;
}

static int
splat_condvar_test4(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	condvar_thr_t ct[SPLAT_CONDVAR_TEST_COUNT];
	condvar_priv_t cv;

	cv.cv_magic = SPLAT_CONDVAR_TEST_MAGIC;
	cv.cv_file = file;
	mutex_init(&cv.cv_mtx, SPLAT_CONDVAR_TEST_NAME, MUTEX_DEFAULT, NULL);
	cv_init(&cv.cv_condvar, NULL, CV_DEFAULT, NULL);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < SPLAT_CONDVAR_TEST_COUNT; i++) {
		ct[i].ct_cvp = &cv;
		ct[i].ct_name = SPLAT_CONDVAR_TEST3_NAME;
		ct[i].ct_rc = 0;
		ct[i].ct_thread = spl_kthread_create(splat_condvar_test34_thread,
		    &ct[i], "%s/%d", SPLAT_CONDVAR_TEST_NAME, i);

		if (!IS_ERR(ct[i].ct_thread)) {
			wake_up_process(ct[i].ct_thread);
			count++;
		}
	}

	/* Wait until all threads are waiting on the condition variable */
	while (atomic_read(&cv.cv_condvar.cv_waiters) != count)
		schedule();

	/* Wake a single thread at a time, wait until it exits */
	for (i = 1; i <= count; i++) {
		cv_signal(&cv.cv_condvar);

		while (atomic_read(&cv.cv_condvar.cv_waiters) > (count - i))
			schedule();

		/* Correct behavior 1 thread woken */
		if (atomic_read(&cv.cv_condvar.cv_waiters) == (count - i))
			continue;

                splat_vprint(file, SPLAT_CONDVAR_TEST3_NAME, "Attempted to "
			   "wake %d thread but work %d threads woke\n",
			   1, count - atomic_read(&cv.cv_condvar.cv_waiters));
		rc = -EINVAL;
		break;
	}

	/* Validate no waiting thread timed out early */
	for (i = 0; i < count; i++)
		if (ct[i].ct_rc)
			rc = ct[i].ct_rc;

	if (!rc)
                splat_vprint(file, SPLAT_CONDVAR_TEST3_NAME, "Correctly woke "
			   "%d sleeping threads %d at a time\n", count, 1);

	/* Wait until that last nutex is dropped */
	while (mutex_owner(&cv.cv_mtx))
		schedule();

	/* Wake everything for the failure case */
	cv_broadcast(&cv.cv_condvar);
	cv_destroy(&cv.cv_condvar);

	/* wait for threads to exit */
	for (i = 0; i < SPLAT_CONDVAR_TEST_COUNT; i++) {
		if (!IS_ERR(ct[i].ct_thread))
			kthread_stop(ct[i].ct_thread);
	}
	mutex_destroy(&cv.cv_mtx);

	return rc;
}

static int
splat_condvar_test5(struct file *file, void *arg)
{
        kcondvar_t condvar;
        kmutex_t mtx;
	clock_t time_left, time_before, time_after, time_delta;
	uint64_t whole_delta;
	uint32_t remain_delta;
	int rc = 0;

	mutex_init(&mtx, SPLAT_CONDVAR_TEST_NAME, MUTEX_DEFAULT, NULL);
	cv_init(&condvar, NULL, CV_DEFAULT, NULL);

        splat_vprint(file, SPLAT_CONDVAR_TEST5_NAME, "Thread going to sleep for "
	           "%d second and expecting to be woken by timeout\n", 1);

	/* Allow a 1 second timeout, plenty long to validate correctness. */
	time_before = lbolt;
	mutex_enter(&mtx);
	time_left = cv_timedwait(&condvar, &mtx, lbolt + HZ);
	mutex_exit(&mtx);
	time_after = lbolt;
	time_delta = time_after - time_before; /* XXX - Handle jiffie wrap */
	whole_delta  = time_delta;
	remain_delta = do_div(whole_delta, HZ);

	if (time_left == -1) {
		if (time_delta >= HZ) {
			splat_vprint(file, SPLAT_CONDVAR_TEST5_NAME,
			           "Thread correctly timed out and was asleep "
			           "for %d.%d seconds (%d second min)\n",
			           (int)whole_delta, (int)remain_delta, 1);
		} else {
			splat_vprint(file, SPLAT_CONDVAR_TEST5_NAME,
			           "Thread correctly timed out but was only "
			           "asleep for %d.%d seconds (%d second "
			           "min)\n", (int)whole_delta,
				   (int)remain_delta, 1);
			rc = -ETIMEDOUT;
		}
	} else {
		splat_vprint(file, SPLAT_CONDVAR_TEST5_NAME,
		           "Thread exited after only %d.%d seconds, it "
		           "did not hit the %d second timeout\n",
		           (int)whole_delta, (int)remain_delta, 1);
		rc = -ETIMEDOUT;
	}

	cv_destroy(&condvar);
	mutex_destroy(&mtx);

	return rc;
}

splat_subsystem_t *
splat_condvar_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_CONDVAR_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_CONDVAR_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
        INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_CONDVAR;

        SPLAT_TEST_INIT(sub, SPLAT_CONDVAR_TEST1_NAME, SPLAT_CONDVAR_TEST1_DESC,
                      SPLAT_CONDVAR_TEST1_ID, splat_condvar_test1);
        SPLAT_TEST_INIT(sub, SPLAT_CONDVAR_TEST2_NAME, SPLAT_CONDVAR_TEST2_DESC,
                      SPLAT_CONDVAR_TEST2_ID, splat_condvar_test2);
        SPLAT_TEST_INIT(sub, SPLAT_CONDVAR_TEST3_NAME, SPLAT_CONDVAR_TEST3_DESC,
                      SPLAT_CONDVAR_TEST3_ID, splat_condvar_test3);
        SPLAT_TEST_INIT(sub, SPLAT_CONDVAR_TEST4_NAME, SPLAT_CONDVAR_TEST4_DESC,
                      SPLAT_CONDVAR_TEST4_ID, splat_condvar_test4);
        SPLAT_TEST_INIT(sub, SPLAT_CONDVAR_TEST5_NAME, SPLAT_CONDVAR_TEST5_DESC,
                      SPLAT_CONDVAR_TEST5_ID, splat_condvar_test5);

        return sub;
}

void
splat_condvar_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        SPLAT_TEST_FINI(sub, SPLAT_CONDVAR_TEST5_ID);
        SPLAT_TEST_FINI(sub, SPLAT_CONDVAR_TEST4_ID);
        SPLAT_TEST_FINI(sub, SPLAT_CONDVAR_TEST3_ID);
        SPLAT_TEST_FINI(sub, SPLAT_CONDVAR_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_CONDVAR_TEST1_ID);

        kfree(sub);
}

int
splat_condvar_id(void) {
        return SPLAT_SUBSYSTEM_CONDVAR;
}
