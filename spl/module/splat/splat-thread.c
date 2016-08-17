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
 *  Solaris Porting LAyer Tests (SPLAT) Thread Tests.
\*****************************************************************************/

#include <sys/thread.h>
#include <sys/random.h>
#include <linux/delay.h>
#include <linux/mm_compat.h>
#include <linux/wait_compat.h>
#include <linux/slab.h>
#include "splat-internal.h"

#define SPLAT_THREAD_NAME		"thread"
#define SPLAT_THREAD_DESC		"Kernel Thread Tests"

#define SPLAT_THREAD_TEST1_ID		0x0601
#define SPLAT_THREAD_TEST1_NAME		"create"
#define SPLAT_THREAD_TEST1_DESC		"Validate thread creation"

#define SPLAT_THREAD_TEST2_ID		0x0602
#define SPLAT_THREAD_TEST2_NAME		"exit"
#define SPLAT_THREAD_TEST2_DESC		"Validate thread exit"

#define SPLAT_THREAD_TEST3_ID		0x6003
#define SPLAT_THREAD_TEST3_NAME		"tsd"
#define SPLAT_THREAD_TEST3_DESC		"Validate thread specific data"

#define SPLAT_THREAD_TEST_MAGIC		0x4488CC00UL
#define SPLAT_THREAD_TEST_KEYS		32
#define SPLAT_THREAD_TEST_THREADS	16

typedef struct thread_priv {
        unsigned long tp_magic;
        struct file *tp_file;
        spinlock_t tp_lock;
        spl_wait_queue_head_t tp_waitq;
	uint_t tp_keys[SPLAT_THREAD_TEST_KEYS];
	int tp_rc;
	int tp_count;
	int tp_dtor_count;
} thread_priv_t;

static int
splat_thread_rc(thread_priv_t *tp, int rc)
{
	int ret;

	spin_lock(&tp->tp_lock);
	ret = (tp->tp_rc == rc);
	spin_unlock(&tp->tp_lock);

	return ret;
}

static int
splat_thread_count(thread_priv_t *tp, int count)
{
	int ret;

	spin_lock(&tp->tp_lock);
	ret = (tp->tp_count == count);
	spin_unlock(&tp->tp_lock);

	return ret;
}

static void
splat_thread_work1(void *priv)
{
	thread_priv_t *tp = (thread_priv_t *)priv;

	spin_lock(&tp->tp_lock);
	ASSERT(tp->tp_magic == SPLAT_THREAD_TEST_MAGIC);
	tp->tp_rc = 1;
	wake_up(&tp->tp_waitq);
	spin_unlock(&tp->tp_lock);

	thread_exit();
}

static int
splat_thread_test1(struct file *file, void *arg)
{
	thread_priv_t tp;
	kthread_t *thr;

	tp.tp_magic = SPLAT_THREAD_TEST_MAGIC;
	tp.tp_file = file;
        spin_lock_init(&tp.tp_lock);
	init_waitqueue_head(&tp.tp_waitq);
	tp.tp_rc = 0;

	thr = (kthread_t *)thread_create(NULL, 0, splat_thread_work1, &tp, 0,
			                 &p0, TS_RUN, defclsyspri);
	/* Must never fail under Solaris, but we check anyway since this
	 * can happen in the linux SPL, we may want to change this behavior */
	if (thr == NULL)
		return  -ESRCH;

	/* Sleep until the thread sets tp.tp_rc == 1 */
	wait_event(tp.tp_waitq, splat_thread_rc(&tp, 1));

        splat_vprint(file, SPLAT_THREAD_TEST1_NAME, "%s",
	           "Thread successfully started properly\n");
	return 0;
}

static void
splat_thread_work2(void *priv)
{
	thread_priv_t *tp = (thread_priv_t *)priv;

	spin_lock(&tp->tp_lock);
	ASSERT(tp->tp_magic == SPLAT_THREAD_TEST_MAGIC);
	tp->tp_rc = 1;
	wake_up(&tp->tp_waitq);
	spin_unlock(&tp->tp_lock);

	thread_exit();

	/* The following code is unreachable when thread_exit() is
	 * working properly, which is exactly what we're testing */
	spin_lock(&tp->tp_lock);
	tp->tp_rc = 2;
	wake_up(&tp->tp_waitq);
	spin_unlock(&tp->tp_lock);
}

static int
splat_thread_test2(struct file *file, void *arg)
{
	thread_priv_t tp;
	kthread_t *thr;
	int rc = 0;

	tp.tp_magic = SPLAT_THREAD_TEST_MAGIC;
	tp.tp_file = file;
        spin_lock_init(&tp.tp_lock);
	init_waitqueue_head(&tp.tp_waitq);
	tp.tp_rc = 0;

	thr = (kthread_t *)thread_create(NULL, 0, splat_thread_work2, &tp, 0,
			                 &p0, TS_RUN, defclsyspri);
	/* Must never fail under Solaris, but we check anyway since this
	 * can happen in the linux SPL, we may want to change this behavior */
	if (thr == NULL)
		return -ESRCH;

	/* Sleep until the thread sets tp.tp_rc == 1 */
	wait_event(tp.tp_waitq, splat_thread_rc(&tp, 1));

	/* Sleep until the thread sets tp.tp_rc == 2, or until we hit
	 * the timeout.  If thread exit is working properly we should
	 * hit the timeout and never see to.tp_rc == 2. */
	rc = wait_event_timeout(tp.tp_waitq, splat_thread_rc(&tp, 2), HZ / 10);
	if (rc > 0) {
		rc = -EINVAL;
	        splat_vprint(file, SPLAT_THREAD_TEST2_NAME, "%s",
		           "Thread did not exit properly at thread_exit()\n");
	} else {
	        splat_vprint(file, SPLAT_THREAD_TEST2_NAME, "%s",
		           "Thread successfully exited at thread_exit()\n");
	}

	return rc;
}

static void
splat_thread_work3_common(thread_priv_t *tp)
{
	ulong_t rnd;
	int i, rc = 0;

	/* set a unique value for each key using a random value */
	get_random_bytes((void *)&rnd, 4);
	for (i = 0; i < SPLAT_THREAD_TEST_KEYS; i++)
		tsd_set(tp->tp_keys[i], (void *)(i + rnd));

	/* verify the unique value for each key */
	for (i = 0; i < SPLAT_THREAD_TEST_KEYS; i++)
		if (tsd_get(tp->tp_keys[i]) !=  (void *)(i + rnd))
			rc = -EINVAL;

	/* set the value to thread_priv_t for use by the destructor */
	for (i = 0; i < SPLAT_THREAD_TEST_KEYS; i++)
		tsd_set(tp->tp_keys[i], (void *)tp);

	spin_lock(&tp->tp_lock);
	if (rc && !tp->tp_rc)
		tp->tp_rc = rc;

	tp->tp_count++;
	wake_up_all(&tp->tp_waitq);
	spin_unlock(&tp->tp_lock);
}

static void
splat_thread_work3_wait(void *priv)
{
	thread_priv_t *tp = (thread_priv_t *)priv;

	ASSERT(tp->tp_magic == SPLAT_THREAD_TEST_MAGIC);
	splat_thread_work3_common(tp);
	wait_event(tp->tp_waitq, splat_thread_count(tp, 0));
	thread_exit();
}

static void
splat_thread_work3_exit(void *priv)
{
	thread_priv_t *tp = (thread_priv_t *)priv;

	ASSERT(tp->tp_magic == SPLAT_THREAD_TEST_MAGIC);
	splat_thread_work3_common(tp);
	thread_exit();
}

static void
splat_thread_dtor3(void *priv)
{
	thread_priv_t *tp = (thread_priv_t *)priv;

	ASSERT(tp->tp_magic == SPLAT_THREAD_TEST_MAGIC);
	spin_lock(&tp->tp_lock);
	tp->tp_dtor_count++;
	spin_unlock(&tp->tp_lock);
}

/*
 * Create threads which set and verify SPLAT_THREAD_TEST_KEYS number of
 * keys.  These threads may then exit by calling thread_exit() which calls
 * tsd_exit() resulting in all their thread specific data being reclaimed.
 * Alternately, the thread may block in which case the thread specific
 * data will be reclaimed as part of tsd_destroy().  In either case all
 * thread specific data must be reclaimed, this is verified by ensuring
 * the registered destructor is called the correct number of times.
 */
static int
splat_thread_test3(struct file *file, void *arg)
{
	int i, rc = 0, expected, wait_count = 0, exit_count = 0;
	thread_priv_t tp;

	tp.tp_magic = SPLAT_THREAD_TEST_MAGIC;
	tp.tp_file = file;
        spin_lock_init(&tp.tp_lock);
	init_waitqueue_head(&tp.tp_waitq);
	tp.tp_rc = 0;
	tp.tp_count = 0;
	tp.tp_dtor_count = 0;

	for (i = 0; i < SPLAT_THREAD_TEST_KEYS; i++) {
		tp.tp_keys[i] = 0;
		tsd_create(&tp.tp_keys[i], splat_thread_dtor3);
	}

	/* Start tsd wait threads */
	for (i = 0; i < SPLAT_THREAD_TEST_THREADS; i++) {
		if (thread_create(NULL, 0, splat_thread_work3_wait,
				  &tp, 0, &p0, TS_RUN, defclsyspri))
			wait_count++;
	}

	/* All wait threads have setup their tsd and are blocking. */
	wait_event(tp.tp_waitq, splat_thread_count(&tp, wait_count));

	if (tp.tp_dtor_count != 0) {
	        splat_vprint(file, SPLAT_THREAD_TEST3_NAME,
		    "Prematurely ran %d tsd destructors\n", tp.tp_dtor_count);
		if (!rc)
			rc = -ERANGE;
	}

	/* Start tsd exit threads */
	for (i = 0; i < SPLAT_THREAD_TEST_THREADS; i++) {
		if (thread_create(NULL, 0, splat_thread_work3_exit,
				  &tp, 0, &p0, TS_RUN, defclsyspri))
			exit_count++;
	}

	/* All exit threads verified tsd and are in the process of exiting */
	wait_event(tp.tp_waitq,splat_thread_count(&tp, wait_count+exit_count));
	msleep(500);

	expected = (SPLAT_THREAD_TEST_KEYS * exit_count);
	if (tp.tp_dtor_count != expected) {
	        splat_vprint(file, SPLAT_THREAD_TEST3_NAME,
		    "Expected %d exit tsd destructors but saw %d\n",
		    expected, tp.tp_dtor_count);
		if (!rc)
			rc = -ERANGE;
	}

	/* Destroy all keys and associated tsd in blocked threads */
	for (i = 0; i < SPLAT_THREAD_TEST_KEYS; i++)
		tsd_destroy(&tp.tp_keys[i]);

	expected = (SPLAT_THREAD_TEST_KEYS * (exit_count + wait_count));
	if (tp.tp_dtor_count != expected) {
	        splat_vprint(file, SPLAT_THREAD_TEST3_NAME,
		    "Expected %d wait+exit tsd destructors but saw %d\n",
		    expected, tp.tp_dtor_count);
		if (!rc)
			rc = -ERANGE;
	}

	/* Release the remaining wait threads, sleep briefly while they exit */
	spin_lock(&tp.tp_lock);
	tp.tp_count = 0;
	wake_up_all(&tp.tp_waitq);
	spin_unlock(&tp.tp_lock);
	msleep(500);

	if (tp.tp_rc) {
	        splat_vprint(file, SPLAT_THREAD_TEST3_NAME,
		    "Thread tsd_get()/tsd_set() error %d\n", tp.tp_rc);
		if (!rc)
			rc = tp.tp_rc;
	} else if (!rc) {
	        splat_vprint(file, SPLAT_THREAD_TEST3_NAME, "%s",
		    "Thread specific data verified\n");
	}

	return rc;
}

splat_subsystem_t *
splat_thread_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_THREAD_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_THREAD_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
        INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_THREAD;

        splat_test_init(sub, SPLAT_THREAD_TEST1_NAME, SPLAT_THREAD_TEST1_DESC,
                      SPLAT_THREAD_TEST1_ID, splat_thread_test1);
        splat_test_init(sub, SPLAT_THREAD_TEST2_NAME, SPLAT_THREAD_TEST2_DESC,
                      SPLAT_THREAD_TEST2_ID, splat_thread_test2);
        splat_test_init(sub, SPLAT_THREAD_TEST3_NAME, SPLAT_THREAD_TEST3_DESC,
                      SPLAT_THREAD_TEST3_ID, splat_thread_test3);

        return sub;
}

void
splat_thread_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        splat_test_fini(sub, SPLAT_THREAD_TEST3_ID);
        splat_test_fini(sub, SPLAT_THREAD_TEST2_ID);
        splat_test_fini(sub, SPLAT_THREAD_TEST1_ID);

        kfree(sub);
}

int
splat_thread_id(void) {
        return SPLAT_SUBSYSTEM_THREAD;
}
