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
 *  Solaris Porting LAyer Tests (SPLAT) Time Tests.
\*****************************************************************************/

#include <sys/time.h>
#include <linux/mm_compat.h>
#include <linux/slab.h>
#include "splat-internal.h"

#define SPLAT_TIME_NAME			"time"
#define SPLAT_TIME_DESC			"Kernel Time Tests"

#define SPLAT_TIME_TEST1_ID		0x0801
#define SPLAT_TIME_TEST1_NAME		"time1"
#define SPLAT_TIME_TEST1_DESC		"HZ Test"

#define SPLAT_TIME_TEST2_ID		0x0802
#define SPLAT_TIME_TEST2_NAME		"time2"
#define SPLAT_TIME_TEST2_DESC		"Monotonic Test"

static int
splat_time_test1(struct file *file, void *arg)
{
	int myhz = hz;
	splat_vprint(file, SPLAT_TIME_TEST1_NAME, "hz is %d\n", myhz);
        return 0;
}

static int
splat_time_test2(struct file *file, void *arg)
{
        hrtime_t tm1, tm2;
	int i;

        tm1 = gethrtime();
        splat_vprint(file, SPLAT_TIME_TEST2_NAME, "time is %lld\n", tm1);

        for(i = 0; i < 100; i++) {
                tm2 = gethrtime();
                splat_vprint(file, SPLAT_TIME_TEST2_NAME, "time is %lld\n", tm2);

                if(tm1 > tm2) {
                        splat_print(file, "%s: gethrtime() is not giving "
				    "monotonically increasing values\n",
				    SPLAT_TIME_TEST2_NAME);
                        return 1;
                }
                tm1 = tm2;

                set_current_state(TASK_INTERRUPTIBLE);
                schedule_timeout(10);
        }

        return 0;
}

splat_subsystem_t *
splat_time_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_TIME_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_TIME_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_TIME;

        SPLAT_TEST_INIT(sub, SPLAT_TIME_TEST1_NAME, SPLAT_TIME_TEST1_DESC,
	              SPLAT_TIME_TEST1_ID, splat_time_test1);
        SPLAT_TEST_INIT(sub, SPLAT_TIME_TEST2_NAME, SPLAT_TIME_TEST2_DESC,
	              SPLAT_TIME_TEST2_ID, splat_time_test2);

        return sub;
}

void
splat_time_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        SPLAT_TEST_FINI(sub, SPLAT_TIME_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_TIME_TEST1_ID);

        kfree(sub);
}

int
splat_time_id(void)
{
        return SPLAT_SUBSYSTEM_TIME;
}
