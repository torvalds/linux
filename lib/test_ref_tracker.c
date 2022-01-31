// SPDX-License-Identifier: GPL-2.0-only
/*
 * Referrence tracker self test.
 *
 * Copyright (c) 2021 Eric Dumazet <edumazet@google.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/ref_tracker.h>
#include <linux/slab.h>
#include <linux/timer.h>

static struct ref_tracker_dir ref_dir;
static struct ref_tracker *tracker[20];

#define TRT_ALLOC(X) static noinline void 				\
	alloctest_ref_tracker_alloc##X(struct ref_tracker_dir *dir, 	\
				    struct ref_tracker **trackerp)	\
	{								\
		ref_tracker_alloc(dir, trackerp, GFP_KERNEL);		\
	}

TRT_ALLOC(1)
TRT_ALLOC(2)
TRT_ALLOC(3)
TRT_ALLOC(4)
TRT_ALLOC(5)
TRT_ALLOC(6)
TRT_ALLOC(7)
TRT_ALLOC(8)
TRT_ALLOC(9)
TRT_ALLOC(10)
TRT_ALLOC(11)
TRT_ALLOC(12)
TRT_ALLOC(13)
TRT_ALLOC(14)
TRT_ALLOC(15)
TRT_ALLOC(16)
TRT_ALLOC(17)
TRT_ALLOC(18)
TRT_ALLOC(19)

#undef TRT_ALLOC

static noinline void
alloctest_ref_tracker_free(struct ref_tracker_dir *dir,
			   struct ref_tracker **trackerp)
{
	ref_tracker_free(dir, trackerp);
}


static struct timer_list test_ref_tracker_timer;
static atomic_t test_ref_timer_done = ATOMIC_INIT(0);

static void test_ref_tracker_timer_func(struct timer_list *t)
{
	ref_tracker_alloc(&ref_dir, &tracker[0], GFP_ATOMIC);
	atomic_set(&test_ref_timer_done, 1);
}

static int __init test_ref_tracker_init(void)
{
	int i;

	ref_tracker_dir_init(&ref_dir, 100);

	timer_setup(&test_ref_tracker_timer, test_ref_tracker_timer_func, 0);
	mod_timer(&test_ref_tracker_timer, jiffies + 1);

	alloctest_ref_tracker_alloc1(&ref_dir, &tracker[1]);
	alloctest_ref_tracker_alloc2(&ref_dir, &tracker[2]);
	alloctest_ref_tracker_alloc3(&ref_dir, &tracker[3]);
	alloctest_ref_tracker_alloc4(&ref_dir, &tracker[4]);
	alloctest_ref_tracker_alloc5(&ref_dir, &tracker[5]);
	alloctest_ref_tracker_alloc6(&ref_dir, &tracker[6]);
	alloctest_ref_tracker_alloc7(&ref_dir, &tracker[7]);
	alloctest_ref_tracker_alloc8(&ref_dir, &tracker[8]);
	alloctest_ref_tracker_alloc9(&ref_dir, &tracker[9]);
	alloctest_ref_tracker_alloc10(&ref_dir, &tracker[10]);
	alloctest_ref_tracker_alloc11(&ref_dir, &tracker[11]);
	alloctest_ref_tracker_alloc12(&ref_dir, &tracker[12]);
	alloctest_ref_tracker_alloc13(&ref_dir, &tracker[13]);
	alloctest_ref_tracker_alloc14(&ref_dir, &tracker[14]);
	alloctest_ref_tracker_alloc15(&ref_dir, &tracker[15]);
	alloctest_ref_tracker_alloc16(&ref_dir, &tracker[16]);
	alloctest_ref_tracker_alloc17(&ref_dir, &tracker[17]);
	alloctest_ref_tracker_alloc18(&ref_dir, &tracker[18]);
	alloctest_ref_tracker_alloc19(&ref_dir, &tracker[19]);

	/* free all trackers but first 0 and 1. */
	for (i = 2; i < ARRAY_SIZE(tracker); i++)
		alloctest_ref_tracker_free(&ref_dir, &tracker[i]);

	/* Attempt to free an already freed tracker. */
	alloctest_ref_tracker_free(&ref_dir, &tracker[2]);

	while (!atomic_read(&test_ref_timer_done))
		msleep(1);

	/* This should warn about tracker[0] & tracker[1] being not freed. */
	ref_tracker_dir_exit(&ref_dir);

	return 0;
}

static void __exit test_ref_tracker_exit(void)
{
}

module_init(test_ref_tracker_init);
module_exit(test_ref_tracker_exit);

MODULE_LICENSE("GPL v2");
