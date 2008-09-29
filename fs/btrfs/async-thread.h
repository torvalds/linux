/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
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

struct btrfs_worker_thread;

/*
 * This is similar to a workqueue, but it is meant to spread the operations
 * across all available cpus instead of just the CPU that was used to
 * queue the work.  There is also some batching introduced to try and
 * cut down on context switches.
 *
 * By default threads are added on demand up to 2 * the number of cpus.
 * Changing struct btrfs_workers->max_workers is one way to prevent
 * demand creation of kthreads.
 *
 * the basic model of these worker threads is to embed a btrfs_work
 * structure in your own data struct, and use container_of in a
 * work function to get back to your data struct.
 */
struct btrfs_work {
	/*
	 * only func should be set to the function you want called
	 * your work struct is passed as the only arg
	 */
	void (*func)(struct btrfs_work *work);

	/*
	 * flags should be set to zero.  It is used to make sure the
	 * struct is only inserted once into the list.
	 */
	unsigned long flags;

	/* don't touch these */
	struct btrfs_worker_thread *worker;
	struct list_head list;
};

struct btrfs_workers {
	/* current number of running workers */
	int num_workers;

	/* max number of workers allowed.  changed by btrfs_start_workers */
	int max_workers;

	/* once a worker has this many requests or fewer, it is idle */
	int idle_thresh;

	/* list with all the work threads.  The workers on the idle thread
	 * may be actively servicing jobs, but they haven't yet hit the
	 * idle thresh limit above.
	 */
	struct list_head worker_list;
	struct list_head idle_list;

	/* lock for finding the next worker thread to queue on */
	spinlock_t lock;

	/* extra name for this worker, used for current->name */
	char *name;
};

int btrfs_queue_worker(struct btrfs_workers *workers, struct btrfs_work *work);
int btrfs_start_workers(struct btrfs_workers *workers, int num_workers);
int btrfs_stop_workers(struct btrfs_workers *workers);
void btrfs_init_workers(struct btrfs_workers *workers, char *name, int max);
int btrfs_requeue_work(struct btrfs_work *work);
#endif
