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
 *  Solaris Porting LAyer Tests (SPLAT) Atomic Tests.
\*****************************************************************************/

#include <sys/atomic.h>
#include <sys/thread.h>
#include <sys/mutex.h>
#include <linux/mm_compat.h>
#include <linux/slab.h>
#include "splat-internal.h"

#define SPLAT_ATOMIC_NAME		"atomic"
#define SPLAT_ATOMIC_DESC		"Kernel Atomic Tests"

#define SPLAT_ATOMIC_TEST1_ID		0x0b01
#define SPLAT_ATOMIC_TEST1_NAME		"64-bit"
#define SPLAT_ATOMIC_TEST1_DESC		"Validate 64-bit atomic ops"

#define SPLAT_ATOMIC_TEST_MAGIC		0x43435454UL
#define SPLAT_ATOMIC_INIT_VALUE		10000000UL

typedef enum {
	SPLAT_ATOMIC_INC_64    = 0,
	SPLAT_ATOMIC_DEC_64    = 1,
	SPLAT_ATOMIC_ADD_64    = 2,
	SPLAT_ATOMIC_SUB_64    = 3,
	SPLAT_ATOMIC_ADD_64_NV = 4,
	SPLAT_ATOMIC_SUB_64_NV = 5,
	SPLAT_ATOMIC_COUNT_64  = 6
} atomic_op_t;

typedef struct atomic_priv {
        unsigned long ap_magic;
        struct file *ap_file;
	kmutex_t ap_lock;
        wait_queue_head_t ap_waitq;
	volatile uint64_t ap_atomic;
	volatile uint64_t ap_atomic_exited;
	atomic_op_t ap_op;

} atomic_priv_t;

static void
splat_atomic_work(void *priv)
{
	atomic_priv_t *ap;
	atomic_op_t op;
	int i;

	ap = (atomic_priv_t *)priv;
	ASSERT(ap->ap_magic == SPLAT_ATOMIC_TEST_MAGIC);

	mutex_enter(&ap->ap_lock);
	op = ap->ap_op;
	wake_up(&ap->ap_waitq);
	mutex_exit(&ap->ap_lock);

        splat_vprint(ap->ap_file, SPLAT_ATOMIC_TEST1_NAME,
	             "Thread %d successfully started: %lu/%lu\n", op,
		     (long unsigned)ap->ap_atomic,
		     (long unsigned)ap->ap_atomic_exited);

	for (i = 0; i < SPLAT_ATOMIC_INIT_VALUE / 10; i++) {

		/* Periodically sleep to mix up the ordering */
		if ((i % (SPLAT_ATOMIC_INIT_VALUE / 100)) == 0) {
		        splat_vprint(ap->ap_file, SPLAT_ATOMIC_TEST1_NAME,
			     "Thread %d sleeping: %lu/%lu\n", op,
			     (long unsigned)ap->ap_atomic,
			     (long unsigned)ap->ap_atomic_exited);
		        set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ / 100);
		}

		switch (op) {
			case SPLAT_ATOMIC_INC_64:
				atomic_inc_64(&ap->ap_atomic);
				break;
			case SPLAT_ATOMIC_DEC_64:
				atomic_dec_64(&ap->ap_atomic);
				break;
			case SPLAT_ATOMIC_ADD_64:
				atomic_add_64(&ap->ap_atomic, 3);
				break;
			case SPLAT_ATOMIC_SUB_64:
				atomic_sub_64(&ap->ap_atomic, 3);
				break;
			case SPLAT_ATOMIC_ADD_64_NV:
				atomic_add_64_nv(&ap->ap_atomic, 5);
				break;
			case SPLAT_ATOMIC_SUB_64_NV:
				atomic_sub_64_nv(&ap->ap_atomic, 5);
				break;
			default:
				PANIC("Undefined op %d\n", op);
		}
	}

	atomic_inc_64(&ap->ap_atomic_exited);

        splat_vprint(ap->ap_file, SPLAT_ATOMIC_TEST1_NAME,
	             "Thread %d successfully exited: %lu/%lu\n", op,
		     (long unsigned)ap->ap_atomic,
		     (long unsigned)ap->ap_atomic_exited);

	wake_up(&ap->ap_waitq);
	thread_exit();
}

static int
splat_atomic_test1_cond(atomic_priv_t *ap, int started)
{
	return (ap->ap_atomic_exited == started);
}

static int
splat_atomic_test1(struct file *file, void *arg)
{
	atomic_priv_t ap;
        DEFINE_WAIT(wait);
	kthread_t *thr;
	int i, rc = 0;

	ap.ap_magic = SPLAT_ATOMIC_TEST_MAGIC;
	ap.ap_file = file;
	mutex_init(&ap.ap_lock, SPLAT_ATOMIC_TEST1_NAME, MUTEX_DEFAULT, NULL);
	init_waitqueue_head(&ap.ap_waitq);
	ap.ap_atomic = SPLAT_ATOMIC_INIT_VALUE;
	ap.ap_atomic_exited = 0;

	for (i = 0; i < SPLAT_ATOMIC_COUNT_64; i++) {
		mutex_enter(&ap.ap_lock);
		ap.ap_op = i;

		thr = (kthread_t *)thread_create(NULL, 0, splat_atomic_work,
						 &ap, 0, &p0, TS_RUN,
						 defclsyspri);
		if (thr == NULL) {
			rc = -ESRCH;
			mutex_exit(&ap.ap_lock);
			break;
		}

		/* Prepare to wait, the new thread will wake us once it
		 * has made a copy of the unique private passed data */
                prepare_to_wait(&ap.ap_waitq, &wait, TASK_UNINTERRUPTIBLE);
		mutex_exit(&ap.ap_lock);
		schedule();
	}

	wait_event(ap.ap_waitq, splat_atomic_test1_cond(&ap, i));

	if (rc) {
		splat_vprint(file, SPLAT_ATOMIC_TEST1_NAME, "Only started "
			     "%d/%d test threads\n", i, SPLAT_ATOMIC_COUNT_64);
		return rc;
	}

	if (ap.ap_atomic != SPLAT_ATOMIC_INIT_VALUE) {
		splat_vprint(file, SPLAT_ATOMIC_TEST1_NAME,
			     "Final value %lu does not match initial value %lu\n",
			     (long unsigned)ap.ap_atomic, SPLAT_ATOMIC_INIT_VALUE);
		return -EINVAL;
	}

        splat_vprint(file, SPLAT_ATOMIC_TEST1_NAME,
	           "Success initial and final values match, %lu == %lu\n",
	           (long unsigned)ap.ap_atomic, SPLAT_ATOMIC_INIT_VALUE);

	mutex_destroy(&ap.ap_lock);

	return 0;
}

splat_subsystem_t *
splat_atomic_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_ATOMIC_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_ATOMIC_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
        INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_ATOMIC;

        SPLAT_TEST_INIT(sub, SPLAT_ATOMIC_TEST1_NAME, SPLAT_ATOMIC_TEST1_DESC,
                      SPLAT_ATOMIC_TEST1_ID, splat_atomic_test1);

        return sub;
}

void
splat_atomic_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        SPLAT_TEST_FINI(sub, SPLAT_ATOMIC_TEST1_ID);

        kfree(sub);
}

int
splat_atomic_id(void) {
        return SPLAT_SUBSYSTEM_ATOMIC;
}
