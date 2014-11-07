/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 * Copyright (C) 2014 Fujitsu.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __BTRFS_ASYNC_THREAD_
#define __BTRFS_ASYNC_THREAD_
#include <linux/workqueue.h>

struct btrfs_workqueue;
/* Internal use only */
struct __btrfs_workqueue;
struct btrfs_work;
typedef void (*btrfs_func_t)(struct btrfs_work *arg);
typedef void (*btrfs_work_func_t)(struct work_struct *arg);

struct btrfs_work {
	btrfs_func_t func;
	btrfs_func_t ordered_func;
	btrfs_func_t ordered_free;

	/* Don't touch things below */
	struct work_struct normal_work;
	struct list_head ordered_list;
	struct __btrfs_workqueue *wq;
	unsigned long flags;
};

#define BTRFS_WORK_HELPER_PROTO(name)					\
void btrfs_##name(struct work_struct *arg)

BTRFS_WORK_HELPER_PROTO(worker_helper);
BTRFS_WORK_HELPER_PROTO(delalloc_helper);
BTRFS_WORK_HELPER_PROTO(flush_delalloc_helper);
BTRFS_WORK_HELPER_PROTO(cache_helper);
BTRFS_WORK_HELPER_PROTO(submit_helper);
BTRFS_WORK_HELPER_PROTO(fixup_helper);
BTRFS_WORK_HELPER_PROTO(endio_helper);
BTRFS_WORK_HELPER_PROTO(endio_meta_helper);
BTRFS_WORK_HELPER_PROTO(endio_meta_write_helper);
BTRFS_WORK_HELPER_PROTO(endio_raid56_helper);
BTRFS_WORK_HELPER_PROTO(endio_repair_helper);
BTRFS_WORK_HELPER_PROTO(rmw_helper);
BTRFS_WORK_HELPER_PROTO(endio_write_helper);
BTRFS_WORK_HELPER_PROTO(freespace_write_helper);
BTRFS_WORK_HELPER_PROTO(delayed_meta_helper);
BTRFS_WORK_HELPER_PROTO(readahead_helper);
BTRFS_WORK_HELPER_PROTO(qgroup_rescan_helper);
BTRFS_WORK_HELPER_PROTO(extent_refs_helper);
BTRFS_WORK_HELPER_PROTO(scrub_helper);
BTRFS_WORK_HELPER_PROTO(scrubwrc_helper);
BTRFS_WORK_HELPER_PROTO(scrubnc_helper);

struct btrfs_workqueue *btrfs_alloc_workqueue(const char *name,
					      int flags,
					      int max_active,
					      int thresh);
void btrfs_init_work(struct btrfs_work *work, btrfs_work_func_t helper,
		     btrfs_func_t func,
		     btrfs_func_t ordered_func,
		     btrfs_func_t ordered_free);
void btrfs_queue_work(struct btrfs_workqueue *wq,
		      struct btrfs_work *work);
void btrfs_destroy_workqueue(struct btrfs_workqueue *wq);
void btrfs_workqueue_set_max(struct btrfs_workqueue *wq, int max);
void btrfs_set_work_high_priority(struct btrfs_work *work);
#endif
