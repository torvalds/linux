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
 *  Solaris Porting LAyer Tests (SPLAT) Mutex Tests.
\*****************************************************************************/

#include <sys/mutex.h>
#include <sys/taskq.h>
#include <linux/delay.h>
#include <linux/mm_compat.h>
#include "splat-internal.h"

#define SPLAT_MUTEX_NAME                "mutex"
#define SPLAT_MUTEX_DESC                "Kernel Mutex Tests"

#define SPLAT_MUTEX_TEST1_ID            0x0401
#define SPLAT_MUTEX_TEST1_NAME          "tryenter"
#define SPLAT_MUTEX_TEST1_DESC          "Validate mutex_tryenter() correctness"

#define SPLAT_MUTEX_TEST2_ID            0x0402
#define SPLAT_MUTEX_TEST2_NAME          "race"
#define SPLAT_MUTEX_TEST2_DESC          "Many threads entering/exiting the mutex"

#define SPLAT_MUTEX_TEST3_ID            0x0403
#define SPLAT_MUTEX_TEST3_NAME          "owned"
#define SPLAT_MUTEX_TEST3_DESC          "Validate mutex_owned() correctness"

#define SPLAT_MUTEX_TEST4_ID            0x0404
#define SPLAT_MUTEX_TEST4_NAME          "owner"
#define SPLAT_MUTEX_TEST4_DESC          "Validate mutex_owner() correctness"

#define SPLAT_MUTEX_TEST_MAGIC          0x115599DDUL
#define SPLAT_MUTEX_TEST_NAME           "mutex_test"
#define SPLAT_MUTEX_TEST_TASKQ          "mutex_taskq"
#define SPLAT_MUTEX_TEST_COUNT          128

typedef struct mutex_priv {
        unsigned long mp_magic;
        struct file *mp_file;
        kmutex_t mp_mtx;
        int mp_rc;
        int mp_rc2;
} mutex_priv_t;

static void
splat_mutex_test1_func(void *arg)
{
        mutex_priv_t *mp = (mutex_priv_t *)arg;
        ASSERT(mp->mp_magic == SPLAT_MUTEX_TEST_MAGIC);

        if (mutex_tryenter(&mp->mp_mtx)) {
                mp->mp_rc = 0;
                mutex_exit(&mp->mp_mtx);
        } else {
                mp->mp_rc = -EBUSY;
        }
}

static int
splat_mutex_test1(struct file *file, void *arg)
{
        mutex_priv_t *mp;
        taskq_t *tq;
	taskqid_t id;
        int rc = 0;

        mp = (mutex_priv_t *)kmalloc(sizeof(*mp), GFP_KERNEL);
        if (mp == NULL)
                return -ENOMEM;

        tq = taskq_create(SPLAT_MUTEX_TEST_TASKQ, 1, defclsyspri,
                          50, INT_MAX, TASKQ_PREPOPULATE);
        if (tq == NULL) {
                rc = -ENOMEM;
                goto out2;
        }

        mp->mp_magic = SPLAT_MUTEX_TEST_MAGIC;
        mp->mp_file = file;
        mutex_init(&mp->mp_mtx, SPLAT_MUTEX_TEST_NAME, MUTEX_DEFAULT, NULL);
        mutex_enter(&mp->mp_mtx);

        /*
         * Schedule a task function which will try and acquire the mutex via
         * mutex_tryenter() while it's held.  This should fail and the task
         * function will indicate this status in the passed private data.
         */
        mp->mp_rc = -EINVAL;
	id = taskq_dispatch(tq, splat_mutex_test1_func, mp, TQ_SLEEP);
	if (id == TASKQID_INVALID) {
                mutex_exit(&mp->mp_mtx);
                splat_vprint(file, SPLAT_MUTEX_TEST1_NAME, "%s",
                             "taskq_dispatch() failed\n");
                rc = -EINVAL;
                goto out;
        }

        taskq_wait_id(tq, id);
        mutex_exit(&mp->mp_mtx);

        /* Task function successfully acquired mutex, very bad! */
        if (mp->mp_rc != -EBUSY) {
                splat_vprint(file, SPLAT_MUTEX_TEST1_NAME,
		    "mutex_trylock() incorrectly succeeded when "
		    "the mutex was held, %d/%d\n", (int)id, mp->mp_rc);
                rc = -EINVAL;
                goto out;
        } else {
                splat_vprint(file, SPLAT_MUTEX_TEST1_NAME, "%s",
                             "mutex_trylock() correctly failed when "
                             "the mutex was held\n");
        }

        /*
         * Schedule a task function which will try and acquire the mutex via
         * mutex_tryenter() while it is not held.  This should succeed and
         * can be verified by checking the private data.
         */
        mp->mp_rc = -EINVAL;
	id = taskq_dispatch(tq, splat_mutex_test1_func, mp, TQ_SLEEP);
	if (id == TASKQID_INVALID) {
                splat_vprint(file, SPLAT_MUTEX_TEST1_NAME, "%s",
                             "taskq_dispatch() failed\n");
                rc = -EINVAL;
                goto out;
        }

        taskq_wait_id(tq, id);

        /* Task function failed to acquire mutex, very bad! */
        if (mp->mp_rc != 0) {
                splat_vprint(file, SPLAT_MUTEX_TEST1_NAME,
		    "mutex_trylock() incorrectly failed when the mutex "
		    "was not held, %d/%d\n", (int)id, mp->mp_rc);
                rc = -EINVAL;
        } else {
                splat_vprint(file, SPLAT_MUTEX_TEST1_NAME, "%s",
                             "mutex_trylock() correctly succeeded "
                             "when the mutex was not held\n");
        }
out:
        taskq_destroy(tq);
        mutex_destroy(&(mp->mp_mtx));
out2:
        kfree(mp);
        return rc;
}

static void
splat_mutex_test2_func(void *arg)
{
        mutex_priv_t *mp = (mutex_priv_t *)arg;
        int rc;
        ASSERT(mp->mp_magic == SPLAT_MUTEX_TEST_MAGIC);

        /* Read the value before sleeping and write it after we wake up to
         * maximize the chance of a race if mutexs are not working properly */
        mutex_enter(&mp->mp_mtx);
        rc = mp->mp_rc;
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(HZ / 100);  /* 1/100 of a second */
        VERIFY(mp->mp_rc == rc);
        mp->mp_rc = rc + 1;
        mutex_exit(&mp->mp_mtx);
}

static int
splat_mutex_test2(struct file *file, void *arg)
{
        mutex_priv_t *mp;
        taskq_t *tq;
	taskqid_t id;
        int i, rc = 0;

        mp = (mutex_priv_t *)kmalloc(sizeof(*mp), GFP_KERNEL);
        if (mp == NULL)
                return -ENOMEM;

        /* Create several threads allowing tasks to race with each other */
        tq = taskq_create(SPLAT_MUTEX_TEST_TASKQ, num_online_cpus(),
                          defclsyspri, 50, INT_MAX, TASKQ_PREPOPULATE);
        if (tq == NULL) {
                rc = -ENOMEM;
                goto out;
        }

        mp->mp_magic = SPLAT_MUTEX_TEST_MAGIC;
        mp->mp_file = file;
        mutex_init(&(mp->mp_mtx), SPLAT_MUTEX_TEST_NAME, MUTEX_DEFAULT, NULL);
        mp->mp_rc = 0;

        /*
         * Schedule N work items to the work queue each of which enters the
         * mutex, sleeps briefly, then exits the mutex.  On a multiprocessor
         * box these work items will be handled by all available CPUs.  The
         * task function checks to ensure the tracked shared variable is
         * always only incremented by one.  Additionally, the mutex itself
         * is instrumented such that if any two processors are in the
         * critical region at the same time the system will panic.  If the
         * mutex is implemented right this will never happy, that's a pass.
         */
        for (i = 0; i < SPLAT_MUTEX_TEST_COUNT; i++) {
		id = taskq_dispatch(tq, splat_mutex_test2_func, mp, TQ_SLEEP);
		if (id == TASKQID_INVALID) {
                        splat_vprint(file, SPLAT_MUTEX_TEST2_NAME,
                                     "Failed to queue task %d\n", i);
                        rc = -EINVAL;
                }
        }

        taskq_wait(tq);

        if (mp->mp_rc == SPLAT_MUTEX_TEST_COUNT) {
                splat_vprint(file, SPLAT_MUTEX_TEST2_NAME, "%d racing threads "
                           "correctly entered/exited the mutex %d times\n",
                           num_online_cpus(), mp->mp_rc);
        } else {
                splat_vprint(file, SPLAT_MUTEX_TEST2_NAME, "%d racing threads "
                           "only processed %d/%d mutex work items\n",
                           num_online_cpus(),mp->mp_rc,SPLAT_MUTEX_TEST_COUNT);
                rc = -EINVAL;
        }

        taskq_destroy(tq);
        mutex_destroy(&(mp->mp_mtx));
out:
        kfree(mp);
        return rc;
}

static void
splat_mutex_owned(void *priv)
{
        mutex_priv_t *mp = (mutex_priv_t *)priv;

        ASSERT(mp->mp_magic == SPLAT_MUTEX_TEST_MAGIC);
        mp->mp_rc = mutex_owned(&mp->mp_mtx);
        mp->mp_rc2 = MUTEX_HELD(&mp->mp_mtx);
}

static int
splat_mutex_test3(struct file *file, void *arg)
{
        mutex_priv_t mp;
        taskq_t *tq;
	taskqid_t id;
        int rc = 0;

        mp.mp_magic = SPLAT_MUTEX_TEST_MAGIC;
        mp.mp_file = file;
        mutex_init(&mp.mp_mtx, SPLAT_MUTEX_TEST_NAME, MUTEX_DEFAULT, NULL);

        if ((tq = taskq_create(SPLAT_MUTEX_TEST_NAME, 1, defclsyspri,
                               50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
                splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Taskq '%s' "
                             "create failed\n", SPLAT_MUTEX_TEST3_NAME);
                return -EINVAL;
        }

        mutex_enter(&mp.mp_mtx);

        /* Mutex should be owned by current */
        if (!mutex_owned(&mp.mp_mtx)) {
                splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Unowned mutex "
                             "should be owned by pid %d\n", current->pid);
                rc = -EINVAL;
                goto out_exit;
        }

	id = taskq_dispatch(tq, splat_mutex_owned, &mp, TQ_SLEEP);
	if (id == TASKQID_INVALID) {
                splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Failed to "
                             "dispatch function '%s' to taskq\n",
                             sym2str(splat_mutex_owned));
                rc = -EINVAL;
                goto out_exit;
        }
        taskq_wait(tq);

        /* Mutex should not be owned which checked from a different thread */
        if (mp.mp_rc || mp.mp_rc2) {
                splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Mutex owned by "
                             "pid %d not by taskq\n", current->pid);
                rc = -EINVAL;
                goto out_exit;
        }

        mutex_exit(&mp.mp_mtx);

        /* Mutex should not be owned by current */
        if (mutex_owned(&mp.mp_mtx)) {
                splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Mutex owned by "
                             "pid %d it should be unowned\b", current->pid);
                rc = -EINVAL;
                goto out;
        }

	id = taskq_dispatch(tq, splat_mutex_owned, &mp, TQ_SLEEP);
	if (id == TASKQID_INVALID) {
                splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Failed to "
                             "dispatch function '%s' to taskq\n",
                             sym2str(splat_mutex_owned));
                rc = -EINVAL;
                goto out;
        }
        taskq_wait(tq);

        /* Mutex should be owned by no one */
        if (mp.mp_rc || mp.mp_rc2) {
                splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Mutex owned by "
                             "no one, %d/%d disagrees\n", mp.mp_rc, mp.mp_rc2);
                rc = -EINVAL;
                goto out;
        }

        splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "%s",
                   "Correct mutex_owned() behavior\n");
        goto out;
out_exit:
        mutex_exit(&mp.mp_mtx);
out:
        mutex_destroy(&mp.mp_mtx);
        taskq_destroy(tq);

        return rc;
}

static int
splat_mutex_test4(struct file *file, void *arg)
{
        kmutex_t mtx;
        kthread_t *owner;
        int rc = 0;

        mutex_init(&mtx, SPLAT_MUTEX_TEST_NAME, MUTEX_DEFAULT, NULL);

        /*
         * Verify mutex owner is cleared after being dropped.  Depending
         * on how you build your kernel this behavior changes, ensure the
         * SPL mutex implementation is properly detecting this.
         */
        mutex_enter(&mtx);
        msleep(100);
        mutex_exit(&mtx);
        if (MUTEX_HELD(&mtx)) {
                splat_vprint(file, SPLAT_MUTEX_TEST4_NAME, "Mutex should "
                           "not be held, bit is by %p\n", mutex_owner(&mtx));
                rc = -EINVAL;
                goto out;
        }

        mutex_enter(&mtx);

        /* Mutex should be owned by current */
        owner = mutex_owner(&mtx);
        if (current != owner) {
                splat_vprint(file, SPLAT_MUTEX_TEST4_NAME, "Mutex should "
                           "be owned by pid %d but is owned by pid %d\n",
                           current->pid, owner ? owner->pid : -1);
                rc = -EINVAL;
                goto out;
        }

        mutex_exit(&mtx);

        /* Mutex should not be owned by any task */
        owner = mutex_owner(&mtx);
        if (owner) {
                splat_vprint(file, SPLAT_MUTEX_TEST4_NAME, "Mutex should not "
                           "be owned but is owned by pid %d\n", owner->pid);
                rc = -EINVAL;
                goto out;
        }

        splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "%s",
                   "Correct mutex_owner() behavior\n");
out:
        mutex_destroy(&mtx);

        return rc;
}

splat_subsystem_t *
splat_mutex_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_MUTEX_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_MUTEX_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
        INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_MUTEX;

        splat_test_init(sub, SPLAT_MUTEX_TEST1_NAME, SPLAT_MUTEX_TEST1_DESC,
                      SPLAT_MUTEX_TEST1_ID, splat_mutex_test1);
        splat_test_init(sub, SPLAT_MUTEX_TEST2_NAME, SPLAT_MUTEX_TEST2_DESC,
                      SPLAT_MUTEX_TEST2_ID, splat_mutex_test2);
        splat_test_init(sub, SPLAT_MUTEX_TEST3_NAME, SPLAT_MUTEX_TEST3_DESC,
                      SPLAT_MUTEX_TEST3_ID, splat_mutex_test3);
        splat_test_init(sub, SPLAT_MUTEX_TEST4_NAME, SPLAT_MUTEX_TEST4_DESC,
                      SPLAT_MUTEX_TEST4_ID, splat_mutex_test4);

        return sub;
}

void
splat_mutex_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        splat_test_fini(sub, SPLAT_MUTEX_TEST4_ID);
        splat_test_fini(sub, SPLAT_MUTEX_TEST3_ID);
        splat_test_fini(sub, SPLAT_MUTEX_TEST2_ID);
        splat_test_fini(sub, SPLAT_MUTEX_TEST1_ID);

        kfree(sub);
}

int
splat_mutex_id(void) {
        return SPLAT_SUBSYSTEM_MUTEX;
}
