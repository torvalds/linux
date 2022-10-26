/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Primitives for DAMON Sysfs Interface
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/damon.h>
#include <linux/kobject.h>

extern struct mutex damon_sysfs_lock;
