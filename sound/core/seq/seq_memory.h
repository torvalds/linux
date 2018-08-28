/*
 *  ALSA sequencer Memory Manager
 *  Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#ifndef __SND_SEQ_MEMORYMGR_H
#define __SND_SEQ_MEMORYMGR_H

#include <sound/seq_kernel.h>
#include <linux/poll.h>

struct snd_info_buffer;

/* container for sequencer event (internal use) */
struct snd_seq_event_cell {
	struct snd_seq_event event;
	struct snd_seq_pool *pool;				/* used pool */
	struct snd_seq_event_cell *next;	/* next cell */
};

/* design note: the pool is a contiguous block of memory, if we dynamicly
   want to add additional cells to the pool be better store this in another
   pool as we need to know the base address of the pool when releasing
   memory. */

struct snd_seq_pool {
	struct snd_seq_event_cell *ptr;	/* pointer to first event chunk */
	struct snd_seq_event_cell *free;	/* pointer to the head of the free list */

	int total_elements;	/* pool size actually allocated */
	atomic_t counter;	/* cells free */

	int size;		/* pool size to be allocated */
	int room;		/* watermark for sleep/wakeup */

	int closing;

	/* statistics */
	int max_used;
	int event_alloc_nopool;
	int event_alloc_failures;
	int event_alloc_success;

	/* Write locking */
	wait_queue_head_t output_sleep;

	/* Pool lock */
	spinlock_t lock;
};

void snd_seq_cell_free(struct snd_seq_event_cell *cell);

int snd_seq_event_dup(struct snd_seq_pool *pool, struct snd_seq_event *event,
		      struct snd_seq_event_cell **cellp, int nonblock,
		      struct file *file, struct mutex *mutexp);

/* return number of unused (free) cells */
static inline int snd_seq_unused_cells(struct snd_seq_pool *pool)
{
	return pool ? pool->total_elements - atomic_read(&pool->counter) : 0;
}

/* return total number of allocated cells */
static inline int snd_seq_total_cells(struct snd_seq_pool *pool)
{
	return pool ? pool->total_elements : 0;
}

/* init pool - allocate events */
int snd_seq_pool_init(struct snd_seq_pool *pool);

/* done pool - free events */
void snd_seq_pool_mark_closing(struct snd_seq_pool *pool);
int snd_seq_pool_done(struct snd_seq_pool *pool);

/* create pool */
struct snd_seq_pool *snd_seq_pool_new(int poolsize);

/* remove pool */
int snd_seq_pool_delete(struct snd_seq_pool **pool);

/* polling */
int snd_seq_pool_poll_wait(struct snd_seq_pool *pool, struct file *file, poll_table *wait);

void snd_seq_info_pool(struct snd_info_buffer *buffer,
		       struct snd_seq_pool *pool, char *space);

#endif
