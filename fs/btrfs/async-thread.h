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

struct btrfs_workqueue_struct;
/* Internal use only */
struct __btrfs_workqueue_struct;

struct btrfs_work_struct {
	void (*func)(struct btrfs_work_struct *arg);
	void (*ordered_func)(struct btrfs_work_struct *arg);
	void (*ordered_free)(struct btrfs_work_struct *arg);

	/* Don't touch things below */
	struct work_struct normal_work;
	struct list_head ordered_list;
	struct __btrfs_workqueue_struct *wq;
	unsigned long flags;
};

struct btrfs_workqueue_struct *btrfs_alloc_workqueue(char *name,
						     int flags,
						     int max_active,
						     int thresh);
void btrfs_init_work(struct btrfs_work_struct *work,
		     void (*func)(struct btrfs_work_struct *),
		     void (*ordered_func)(struct btrfs_work_struct *),
		     void (*ordered_free)(struct btrfs_work_struct *));
void btrfs_queue_work(struct btrfs_workqueue_struct *wq,
		      struct btrfs_work_struct *work);
void btrfs_destroy_workqueue(struct btrfs_workqueue_struct *wq);
void btrfs_workqueue_set_max(struct btrfs_workqueue_struct *wq, int max);
void btrfs_set_work_high_priority(struct btrfs_work_struct *work);
#endif
