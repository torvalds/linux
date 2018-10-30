/*
 * Copyright (C) 2017 Joe Lawrence <joe.lawrence@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * livepatch-shadow-mod.c - Shadow variables, buggy module demo
 *
 * Purpose
 * -------
 *
 * As a demonstration of livepatch shadow variable API, this module
 * introduces memory leak behavior that livepatch modules
 * livepatch-shadow-fix1.ko and livepatch-shadow-fix2.ko correct and
 * enhance.
 *
 * WARNING - even though the livepatch-shadow-fix modules patch the
 * memory leak, please load these modules at your own risk -- some
 * amount of memory may leaked before the bug is patched.
 *
 *
 * Usage
 * -----
 *
 * Step 1 - Load the buggy demonstration module:
 *
 *   insmod samples/livepatch/livepatch-shadow-mod.ko
 *
 * Watch dmesg output for a few moments to see new dummy being allocated
 * and a periodic cleanup check.  (Note: a small amount of memory is
 * being leaked.)
 *
 *
 * Step 2 - Load livepatch fix1:
 *
 *   insmod samples/livepatch/livepatch-shadow-fix1.ko
 *
 * Continue watching dmesg and note that now livepatch_fix1_dummy_free()
 * and livepatch_fix1_dummy_alloc() are logging messages about leaked
 * memory and eventually leaks prevented.
 *
 *
 * Step 3 - Load livepatch fix2 (on top of fix1):
 *
 *   insmod samples/livepatch/livepatch-shadow-fix2.ko
 *
 * This module extends functionality through shadow variables, as a new
 * "check" counter is added to the dummy structure.  Periodic dmesg
 * messages will log these as dummies are cleaned up.
 *
 *
 * Step 4 - Cleanup
 *
 * Unwind the demonstration by disabling the livepatch fix modules, then
 * removing them and the demo module:
 *
 *   echo 0 > /sys/kernel/livepatch/livepatch_shadow_fix2/enabled
 *   echo 0 > /sys/kernel/livepatch/livepatch_shadow_fix1/enabled
 *   rmmod livepatch-shadow-fix2
 *   rmmod livepatch-shadow-fix1
 *   rmmod livepatch-shadow-mod
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/workqueue.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Lawrence <joe.lawrence@redhat.com>");
MODULE_DESCRIPTION("Buggy module for shadow variable demo");

/* Allocate new dummies every second */
#define ALLOC_PERIOD	1
/* Check for expired dummies after a few new ones have been allocated */
#define CLEANUP_PERIOD	(3 * ALLOC_PERIOD)
/* Dummies expire after a few cleanup instances */
#define EXPIRE_PERIOD	(4 * CLEANUP_PERIOD)

/*
 * Keep a list of all the dummies so we can clean up any residual ones
 * on module exit
 */
LIST_HEAD(dummy_list);
DEFINE_MUTEX(dummy_list_mutex);

struct dummy {
	struct list_head list;
	unsigned long jiffies_expire;
};

noinline struct dummy *dummy_alloc(void)
{
	struct dummy *d;
	void *leak;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return NULL;

	d->jiffies_expire = jiffies +
		msecs_to_jiffies(1000 * EXPIRE_PERIOD);

	/* Oops, forgot to save leak! */
	leak = kzalloc(sizeof(int), GFP_KERNEL);

	pr_info("%s: dummy @ %p, expires @ %lx\n",
		__func__, d, d->jiffies_expire);

	return d;
}

noinline void dummy_free(struct dummy *d)
{
	pr_info("%s: dummy @ %p, expired = %lx\n",
		__func__, d, d->jiffies_expire);

	kfree(d);
}

noinline bool dummy_check(struct dummy *d, unsigned long jiffies)
{
	return time_after(jiffies, d->jiffies_expire);
}

/*
 * alloc_work_func: allocates new dummy structures, allocates additional
 *                  memory, aptly named "leak", but doesn't keep
 *                  permanent record of it.
 */

static void alloc_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(alloc_dwork, alloc_work_func);

static void alloc_work_func(struct work_struct *work)
{
	struct dummy *d;

	d = dummy_alloc();
	if (!d)
		return;

	mutex_lock(&dummy_list_mutex);
	list_add(&d->list, &dummy_list);
	mutex_unlock(&dummy_list_mutex);

	schedule_delayed_work(&alloc_dwork,
		msecs_to_jiffies(1000 * ALLOC_PERIOD));
}

/*
 * cleanup_work_func: frees dummy structures.  Without knownledge of
 *                    "leak", it leaks the additional memory that
 *                    alloc_work_func created.
 */

static void cleanup_work_func(struct work_struct *work);
static DECLARE_DELAYED_WORK(cleanup_dwork, cleanup_work_func);

static void cleanup_work_func(struct work_struct *work)
{
	struct dummy *d, *tmp;
	unsigned long j;

	j = jiffies;
	pr_info("%s: jiffies = %lx\n", __func__, j);

	mutex_lock(&dummy_list_mutex);
	list_for_each_entry_safe(d, tmp, &dummy_list, list) {

		/* Kick out and free any expired dummies */
		if (dummy_check(d, j)) {
			list_del(&d->list);
			dummy_free(d);
		}
	}
	mutex_unlock(&dummy_list_mutex);

	schedule_delayed_work(&cleanup_dwork,
		msecs_to_jiffies(1000 * CLEANUP_PERIOD));
}

static int livepatch_shadow_mod_init(void)
{
	schedule_delayed_work(&alloc_dwork,
		msecs_to_jiffies(1000 * ALLOC_PERIOD));
	schedule_delayed_work(&cleanup_dwork,
		msecs_to_jiffies(1000 * CLEANUP_PERIOD));

	return 0;
}

static void livepatch_shadow_mod_exit(void)
{
	struct dummy *d, *tmp;

	/* Wait for any dummies at work */
	cancel_delayed_work_sync(&alloc_dwork);
	cancel_delayed_work_sync(&cleanup_dwork);

	/* Cleanup residual dummies */
	list_for_each_entry_safe(d, tmp, &dummy_list, list) {
		list_del(&d->list);
		dummy_free(d);
	}
}

module_init(livepatch_shadow_mod_init);
module_exit(livepatch_shadow_mod_exit);
