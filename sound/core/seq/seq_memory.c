/*
 *  ALSA sequencer Memory Manager
 *  Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 *                        Jaroslav Kysela <perex@suse.cz>
 *                2000 by Takashi Iwai <tiwai@suse.de>
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

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <sound/core.h>

#include <sound/seq_kernel.h>
#include "seq_memory.h"
#include "seq_queue.h"
#include "seq_info.h"
#include "seq_lock.h"

/* semaphore in struct file record */
#define semaphore_of(fp)	((fp)->f_dentry->d_inode->i_sem)


static inline int snd_seq_pool_available(pool_t *pool)
{
	return pool->total_elements - atomic_read(&pool->counter);
}

static inline int snd_seq_output_ok(pool_t *pool)
{
	return snd_seq_pool_available(pool) >= pool->room;
}

/*
 * Variable length event:
 * The event like sysex uses variable length type.
 * The external data may be stored in three different formats.
 * 1) kernel space
 *    This is the normal case.
 *      ext.data.len = length
 *      ext.data.ptr = buffer pointer
 * 2) user space
 *    When an event is generated via read(), the external data is
 *    kept in user space until expanded.
 *      ext.data.len = length | SNDRV_SEQ_EXT_USRPTR
 *      ext.data.ptr = userspace pointer
 * 3) chained cells
 *    When the variable length event is enqueued (in prioq or fifo),
 *    the external data is decomposed to several cells.
 *      ext.data.len = length | SNDRV_SEQ_EXT_CHAINED
 *      ext.data.ptr = the additiona cell head
 *         -> cell.next -> cell.next -> ..
 */

/*
 * exported:
 * call dump function to expand external data.
 */

static int get_var_len(const snd_seq_event_t *event)
{
	if ((event->flags & SNDRV_SEQ_EVENT_LENGTH_MASK) != SNDRV_SEQ_EVENT_LENGTH_VARIABLE)
		return -EINVAL;

	return event->data.ext.len & ~SNDRV_SEQ_EXT_MASK;
}

int snd_seq_dump_var_event(const snd_seq_event_t *event, snd_seq_dump_func_t func, void *private_data)
{
	int len, err;
	snd_seq_event_cell_t *cell;

	if ((len = get_var_len(event)) <= 0)
		return len;

	if (event->data.ext.len & SNDRV_SEQ_EXT_USRPTR) {
		char buf[32];
		char __user *curptr = (char __user *)event->data.ext.ptr;
		while (len > 0) {
			int size = sizeof(buf);
			if (len < size)
				size = len;
			if (copy_from_user(buf, curptr, size))
				return -EFAULT;
			err = func(private_data, buf, size);
			if (err < 0)
				return err;
			curptr += size;
			len -= size;
		}
		return 0;
	} if (! (event->data.ext.len & SNDRV_SEQ_EXT_CHAINED)) {
		return func(private_data, event->data.ext.ptr, len);
	}

	cell = (snd_seq_event_cell_t*)event->data.ext.ptr;
	for (; len > 0 && cell; cell = cell->next) {
		int size = sizeof(snd_seq_event_t);
		if (len < size)
			size = len;
		err = func(private_data, &cell->event, size);
		if (err < 0)
			return err;
		len -= size;
	}
	return 0;
}


/*
 * exported:
 * expand the variable length event to linear buffer space.
 */

static int seq_copy_in_kernel(char **bufptr, const void *src, int size)
{
	memcpy(*bufptr, src, size);
	*bufptr += size;
	return 0;
}

static int seq_copy_in_user(char __user **bufptr, const void *src, int size)
{
	if (copy_to_user(*bufptr, src, size))
		return -EFAULT;
	*bufptr += size;
	return 0;
}

int snd_seq_expand_var_event(const snd_seq_event_t *event, int count, char *buf, int in_kernel, int size_aligned)
{
	int len, newlen;
	int err;

	if ((len = get_var_len(event)) < 0)
		return len;
	newlen = len;
	if (size_aligned > 0)
		newlen = ((len + size_aligned - 1) / size_aligned) * size_aligned;
	if (count < newlen)
		return -EAGAIN;

	if (event->data.ext.len & SNDRV_SEQ_EXT_USRPTR) {
		if (! in_kernel)
			return -EINVAL;
		if (copy_from_user(buf, (void __user *)event->data.ext.ptr, len))
			return -EFAULT;
		return newlen;
	}
	err = snd_seq_dump_var_event(event,
				     in_kernel ? (snd_seq_dump_func_t)seq_copy_in_kernel :
				     (snd_seq_dump_func_t)seq_copy_in_user,
				     &buf);
	return err < 0 ? err : newlen;
}


/*
 * release this cell, free extended data if available
 */

static inline void free_cell(pool_t *pool, snd_seq_event_cell_t *cell)
{
	cell->next = pool->free;
	pool->free = cell;
	atomic_dec(&pool->counter);
}

void snd_seq_cell_free(snd_seq_event_cell_t * cell)
{
	unsigned long flags;
	pool_t *pool;

	snd_assert(cell != NULL, return);
	pool = cell->pool;
	snd_assert(pool != NULL, return);

	spin_lock_irqsave(&pool->lock, flags);
	free_cell(pool, cell);
	if (snd_seq_ev_is_variable(&cell->event)) {
		if (cell->event.data.ext.len & SNDRV_SEQ_EXT_CHAINED) {
			snd_seq_event_cell_t *curp, *nextptr;
			curp = cell->event.data.ext.ptr;
			for (; curp; curp = nextptr) {
				nextptr = curp->next;
				curp->next = pool->free;
				free_cell(pool, curp);
			}
		}
	}
	if (waitqueue_active(&pool->output_sleep)) {
		/* has enough space now? */
		if (snd_seq_output_ok(pool))
			wake_up(&pool->output_sleep);
	}
	spin_unlock_irqrestore(&pool->lock, flags);
}


/*
 * allocate an event cell.
 */
static int snd_seq_cell_alloc(pool_t *pool, snd_seq_event_cell_t **cellp, int nonblock, struct file *file)
{
	snd_seq_event_cell_t *cell;
	unsigned long flags;
	int err = -EAGAIN;
	wait_queue_t wait;

	if (pool == NULL)
		return -EINVAL;

	*cellp = NULL;

	init_waitqueue_entry(&wait, current);
	spin_lock_irqsave(&pool->lock, flags);
	if (pool->ptr == NULL) {	/* not initialized */
		snd_printd("seq: pool is not initialized\n");
		err = -EINVAL;
		goto __error;
	}
	while (pool->free == NULL && ! nonblock && ! pool->closing) {

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&pool->output_sleep, &wait);
		spin_unlock_irq(&pool->lock);
		schedule();
		spin_lock_irq(&pool->lock);
		remove_wait_queue(&pool->output_sleep, &wait);
		/* interrupted? */
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			goto __error;
		}
	}
	if (pool->closing) { /* closing.. */
		err = -ENOMEM;
		goto __error;
	}

	cell = pool->free;
	if (cell) {
		int used;
		pool->free = cell->next;
		atomic_inc(&pool->counter);
		used = atomic_read(&pool->counter);
		if (pool->max_used < used)
			pool->max_used = used;
		pool->event_alloc_success++;
		/* clear cell pointers */
		cell->next = NULL;
		err = 0;
	} else
		pool->event_alloc_failures++;
	*cellp = cell;

__error:
	spin_unlock_irqrestore(&pool->lock, flags);
	return err;
}


/*
 * duplicate the event to a cell.
 * if the event has external data, the data is decomposed to additional
 * cells.
 */
int snd_seq_event_dup(pool_t *pool, snd_seq_event_t *event, snd_seq_event_cell_t **cellp, int nonblock, struct file *file)
{
	int ncells, err;
	unsigned int extlen;
	snd_seq_event_cell_t *cell;

	*cellp = NULL;

	ncells = 0;
	extlen = 0;
	if (snd_seq_ev_is_variable(event)) {
		extlen = event->data.ext.len & ~SNDRV_SEQ_EXT_MASK;
		ncells = (extlen + sizeof(snd_seq_event_t) - 1) / sizeof(snd_seq_event_t);
	}
	if (ncells >= pool->total_elements)
		return -ENOMEM;

	err = snd_seq_cell_alloc(pool, &cell, nonblock, file);
	if (err < 0)
		return err;

	/* copy the event */
	cell->event = *event;

	/* decompose */
	if (snd_seq_ev_is_variable(event)) {
		int len = extlen;
		int is_chained = event->data.ext.len & SNDRV_SEQ_EXT_CHAINED;
		int is_usrptr = event->data.ext.len & SNDRV_SEQ_EXT_USRPTR;
		snd_seq_event_cell_t *src, *tmp, *tail;
		char *buf;

		cell->event.data.ext.len = extlen | SNDRV_SEQ_EXT_CHAINED;
		cell->event.data.ext.ptr = NULL;

		src = (snd_seq_event_cell_t*)event->data.ext.ptr;
		buf = (char *)event->data.ext.ptr;
		tail = NULL;

		while (ncells-- > 0) {
			int size = sizeof(snd_seq_event_t);
			if (len < size)
				size = len;
			err = snd_seq_cell_alloc(pool, &tmp, nonblock, file);
			if (err < 0)
				goto __error;
			if (cell->event.data.ext.ptr == NULL)
				cell->event.data.ext.ptr = tmp;
			if (tail)
				tail->next = tmp;
			tail = tmp;
			/* copy chunk */
			if (is_chained && src) {
				tmp->event = src->event;
				src = src->next;
			} else if (is_usrptr) {
				if (copy_from_user(&tmp->event, (char __user *)buf, size)) {
					err = -EFAULT;
					goto __error;
				}
			} else {
				memcpy(&tmp->event, buf, size);
			}
			buf += size;
			len -= size;
		}
	}

	*cellp = cell;
	return 0;

__error:
	snd_seq_cell_free(cell);
	return err;
}
  

/* poll wait */
int snd_seq_pool_poll_wait(pool_t *pool, struct file *file, poll_table *wait)
{
	poll_wait(file, &pool->output_sleep, wait);
	return snd_seq_output_ok(pool);
}


/* allocate room specified number of events */
int snd_seq_pool_init(pool_t *pool)
{
	int cell;
	snd_seq_event_cell_t *cellptr;
	unsigned long flags;

	snd_assert(pool != NULL, return -EINVAL);
	if (pool->ptr)			/* should be atomic? */
		return 0;

	pool->ptr = vmalloc(sizeof(snd_seq_event_cell_t) * pool->size);
	if (pool->ptr == NULL) {
		snd_printd("seq: malloc for sequencer events failed\n");
		return -ENOMEM;
	}

	/* add new cells to the free cell list */
	spin_lock_irqsave(&pool->lock, flags);
	pool->free = NULL;

	for (cell = 0; cell < pool->size; cell++) {
		cellptr = pool->ptr + cell;
		cellptr->pool = pool;
		cellptr->next = pool->free;
		pool->free = cellptr;
	}
	pool->room = (pool->size + 1) / 2;

	/* init statistics */
	pool->max_used = 0;
	pool->total_elements = pool->size;
	spin_unlock_irqrestore(&pool->lock, flags);
	return 0;
}

/* remove events */
int snd_seq_pool_done(pool_t *pool)
{
	unsigned long flags;
	snd_seq_event_cell_t *ptr;
	int max_count = 5 * HZ;

	snd_assert(pool != NULL, return -EINVAL);

	/* wait for closing all threads */
	spin_lock_irqsave(&pool->lock, flags);
	pool->closing = 1;
	spin_unlock_irqrestore(&pool->lock, flags);

	if (waitqueue_active(&pool->output_sleep))
		wake_up(&pool->output_sleep);

	while (atomic_read(&pool->counter) > 0) {
		if (max_count == 0) {
			snd_printk(KERN_WARNING "snd_seq_pool_done timeout: %d cells remain\n", atomic_read(&pool->counter));
			break;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(1);
		max_count--;
	}
	
	/* release all resources */
	spin_lock_irqsave(&pool->lock, flags);
	ptr = pool->ptr;
	pool->ptr = NULL;
	pool->free = NULL;
	pool->total_elements = 0;
	spin_unlock_irqrestore(&pool->lock, flags);

	vfree(ptr);

	spin_lock_irqsave(&pool->lock, flags);
	pool->closing = 0;
	spin_unlock_irqrestore(&pool->lock, flags);

	return 0;
}


/* init new memory pool */
pool_t *snd_seq_pool_new(int poolsize)
{
	pool_t *pool;

	/* create pool block */
	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (pool == NULL) {
		snd_printd("seq: malloc failed for pool\n");
		return NULL;
	}
	spin_lock_init(&pool->lock);
	pool->ptr = NULL;
	pool->free = NULL;
	pool->total_elements = 0;
	atomic_set(&pool->counter, 0);
	pool->closing = 0;
	init_waitqueue_head(&pool->output_sleep);
	
	pool->size = poolsize;

	/* init statistics */
	pool->max_used = 0;
	return pool;
}

/* remove memory pool */
int snd_seq_pool_delete(pool_t **ppool)
{
	pool_t *pool = *ppool;

	*ppool = NULL;
	if (pool == NULL)
		return 0;
	snd_seq_pool_done(pool);
	kfree(pool);
	return 0;
}

/* initialize sequencer memory */
int __init snd_sequencer_memory_init(void)
{
	return 0;
}

/* release sequencer memory */
void __exit snd_sequencer_memory_done(void)
{
}


/* exported to seq_clientmgr.c */
void snd_seq_info_pool(snd_info_buffer_t * buffer, pool_t *pool, char *space)
{
	if (pool == NULL)
		return;
	snd_iprintf(buffer, "%sPool size          : %d\n", space, pool->total_elements);
	snd_iprintf(buffer, "%sCells in use       : %d\n", space, atomic_read(&pool->counter));
	snd_iprintf(buffer, "%sPeak cells in use  : %d\n", space, pool->max_used);
	snd_iprintf(buffer, "%sAlloc success      : %d\n", space, pool->event_alloc_success);
	snd_iprintf(buffer, "%sAlloc failures     : %d\n", space, pool->event_alloc_failures);
}
