/*
 *   ALSA sequencer Priority Queue
 *   Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
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

#include <linux/time.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "seq_timer.h"
#include "seq_prioq.h"


/* Implementation is a simple linked list for now...

   This priority queue orders the events on timestamp. For events with an
   equeal timestamp the queue behaves as a FIFO. 

   *
   *           +-------+
   *  Head --> | first |
   *           +-------+
   *                 |next
   *           +-----v-+
   *           |       |
   *           +-------+
   *                 |
   *           +-----v-+
   *           |       |
   *           +-------+
   *                 |
   *           +-----v-+
   *  Tail --> | last  |
   *           +-------+
   *

 */



/* create new prioq (constructor) */
struct snd_seq_prioq *snd_seq_prioq_new(void)
{
	struct snd_seq_prioq *f;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (f == NULL) {
		snd_printd("oops: malloc failed for snd_seq_prioq_new()\n");
		return NULL;
	}
	
	spin_lock_init(&f->lock);
	f->head = NULL;
	f->tail = NULL;
	f->cells = 0;
	
	return f;
}

/* delete prioq (destructor) */
void snd_seq_prioq_delete(struct snd_seq_prioq **fifo)
{
	struct snd_seq_prioq *f = *fifo;
	*fifo = NULL;

	if (f == NULL) {
		snd_printd("oops: snd_seq_prioq_delete() called with NULL prioq\n");
		return;
	}

	/* release resources...*/
	/*....................*/
	
	if (f->cells > 0) {
		/* drain prioQ */
		while (f->cells > 0)
			snd_seq_cell_free(snd_seq_prioq_cell_out(f));
	}
	
	kfree(f);
}




/* compare timestamp between events */
/* return 1 if a >= b; 0 */
static inline int compare_timestamp(struct snd_seq_event *a,
				    struct snd_seq_event *b)
{
	if ((a->flags & SNDRV_SEQ_TIME_STAMP_MASK) == SNDRV_SEQ_TIME_STAMP_TICK) {
		/* compare ticks */
		return (snd_seq_compare_tick_time(&a->time.tick, &b->time.tick));
	} else {
		/* compare real time */
		return (snd_seq_compare_real_time(&a->time.time, &b->time.time));
	}
}

/* compare timestamp between events */
/* return negative if a < b;
 *        zero     if a = b;
 *        positive if a > b;
 */
static inline int compare_timestamp_rel(struct snd_seq_event *a,
					struct snd_seq_event *b)
{
	if ((a->flags & SNDRV_SEQ_TIME_STAMP_MASK) == SNDRV_SEQ_TIME_STAMP_TICK) {
		/* compare ticks */
		if (a->time.tick > b->time.tick)
			return 1;
		else if (a->time.tick == b->time.tick)
			return 0;
		else
			return -1;
	} else {
		/* compare real time */
		if (a->time.time.tv_sec > b->time.time.tv_sec)
			return 1;
		else if (a->time.time.tv_sec == b->time.time.tv_sec) {
			if (a->time.time.tv_nsec > b->time.time.tv_nsec)
				return 1;
			else if (a->time.time.tv_nsec == b->time.time.tv_nsec)
				return 0;
			else
				return -1;
		} else
			return -1;
	}
}

/* enqueue cell to prioq */
int snd_seq_prioq_cell_in(struct snd_seq_prioq * f,
			  struct snd_seq_event_cell * cell)
{
	struct snd_seq_event_cell *cur, *prev;
	unsigned long flags;
	int count;
	int prior;

	if (snd_BUG_ON(!f || !cell))
		return -EINVAL;
	
	/* check flags */
	prior = (cell->event.flags & SNDRV_SEQ_PRIORITY_MASK);

	spin_lock_irqsave(&f->lock, flags);

	/* check if this element needs to inserted at the end (ie. ordered 
	   data is inserted) This will be very likeley if a sequencer 
	   application or midi file player is feeding us (sequential) data */
	if (f->tail && !prior) {
		if (compare_timestamp(&cell->event, &f->tail->event)) {
			/* add new cell to tail of the fifo */
			f->tail->next = cell;
			f->tail = cell;
			cell->next = NULL;
			f->cells++;
			spin_unlock_irqrestore(&f->lock, flags);
			return 0;
		}
	}
	/* traverse list of elements to find the place where the new cell is
	   to be inserted... Note that this is a order n process ! */

	prev = NULL;		/* previous cell */
	cur = f->head;		/* cursor */

	count = 10000; /* FIXME: enough big, isn't it? */
	while (cur != NULL) {
		/* compare timestamps */
		int rel = compare_timestamp_rel(&cell->event, &cur->event);
		if (rel < 0)
			/* new cell has earlier schedule time, */
			break;
		else if (rel == 0 && prior)
			/* equal schedule time and prior to others */
			break;
		/* new cell has equal or larger schedule time, */
		/* move cursor to next cell */
		prev = cur;
		cur = cur->next;
		if (! --count) {
			spin_unlock_irqrestore(&f->lock, flags);
			snd_printk(KERN_ERR "cannot find a pointer.. infinite loop?\n");
			return -EINVAL;
		}
	}

	/* insert it before cursor */
	if (prev != NULL)
		prev->next = cell;
	cell->next = cur;

	if (f->head == cur) /* this is the first cell, set head to it */
		f->head = cell;
	if (cur == NULL) /* reached end of the list */
		f->tail = cell;
	f->cells++;
	spin_unlock_irqrestore(&f->lock, flags);
	return 0;
}

/* dequeue cell from prioq */
struct snd_seq_event_cell *snd_seq_prioq_cell_out(struct snd_seq_prioq *f)
{
	struct snd_seq_event_cell *cell;
	unsigned long flags;

	if (f == NULL) {
		snd_printd("oops: snd_seq_prioq_cell_in() called with NULL prioq\n");
		return NULL;
	}
	spin_lock_irqsave(&f->lock, flags);

	cell = f->head;
	if (cell) {
		f->head = cell->next;

		/* reset tail if this was the last element */
		if (f->tail == cell)
			f->tail = NULL;

		cell->next = NULL;
		f->cells--;
	}

	spin_unlock_irqrestore(&f->lock, flags);
	return cell;
}

/* return number of events available in prioq */
int snd_seq_prioq_avail(struct snd_seq_prioq * f)
{
	if (f == NULL) {
		snd_printd("oops: snd_seq_prioq_cell_in() called with NULL prioq\n");
		return 0;
	}
	return f->cells;
}


/* peek at cell at the head of the prioq */
struct snd_seq_event_cell *snd_seq_prioq_cell_peek(struct snd_seq_prioq * f)
{
	if (f == NULL) {
		snd_printd("oops: snd_seq_prioq_cell_in() called with NULL prioq\n");
		return NULL;
	}
	return f->head;
}


static inline int prioq_match(struct snd_seq_event_cell *cell,
			      int client, int timestamp)
{
	if (cell->event.source.client == client ||
	    cell->event.dest.client == client)
		return 1;
	if (!timestamp)
		return 0;
	switch (cell->event.flags & SNDRV_SEQ_TIME_STAMP_MASK) {
	case SNDRV_SEQ_TIME_STAMP_TICK:
		if (cell->event.time.tick)
			return 1;
		break;
	case SNDRV_SEQ_TIME_STAMP_REAL:
		if (cell->event.time.time.tv_sec ||
		    cell->event.time.time.tv_nsec)
			return 1;
		break;
	}
	return 0;
}

/* remove cells for left client */
void snd_seq_prioq_leave(struct snd_seq_prioq * f, int client, int timestamp)
{
	register struct snd_seq_event_cell *cell, *next;
	unsigned long flags;
	struct snd_seq_event_cell *prev = NULL;
	struct snd_seq_event_cell *freefirst = NULL, *freeprev = NULL, *freenext;

	/* collect all removed cells */
	spin_lock_irqsave(&f->lock, flags);
	cell = f->head;
	while (cell) {
		next = cell->next;
		if (prioq_match(cell, client, timestamp)) {
			/* remove cell from prioq */
			if (cell == f->head) {
				f->head = cell->next;
			} else {
				prev->next = cell->next;
			}
			if (cell == f->tail)
				f->tail = cell->next;
			f->cells--;
			/* add cell to free list */
			cell->next = NULL;
			if (freefirst == NULL) {
				freefirst = cell;
			} else {
				freeprev->next = cell;
			}
			freeprev = cell;
		} else {
#if 0
			printk("type = %i, source = %i, dest = %i, client = %i\n",
				cell->event.type,
				cell->event.source.client,
				cell->event.dest.client,
				client);
#endif
			prev = cell;
		}
		cell = next;		
	}
	spin_unlock_irqrestore(&f->lock, flags);	

	/* remove selected cells */
	while (freefirst) {
		freenext = freefirst->next;
		snd_seq_cell_free(freefirst);
		freefirst = freenext;
	}
}

static int prioq_remove_match(struct snd_seq_remove_events *info,
			      struct snd_seq_event *ev)
{
	int res;

	if (info->remove_mode & SNDRV_SEQ_REMOVE_DEST) {
		if (ev->dest.client != info->dest.client ||
				ev->dest.port != info->dest.port)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_DEST_CHANNEL) {
		if (! snd_seq_ev_is_channel_type(ev))
			return 0;
		/* data.note.channel and data.control.channel are identical */
		if (ev->data.note.channel != info->channel)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_TIME_AFTER) {
		if (info->remove_mode & SNDRV_SEQ_REMOVE_TIME_TICK)
			res = snd_seq_compare_tick_time(&ev->time.tick, &info->time.tick);
		else
			res = snd_seq_compare_real_time(&ev->time.time, &info->time.time);
		if (!res)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_TIME_BEFORE) {
		if (info->remove_mode & SNDRV_SEQ_REMOVE_TIME_TICK)
			res = snd_seq_compare_tick_time(&ev->time.tick, &info->time.tick);
		else
			res = snd_seq_compare_real_time(&ev->time.time, &info->time.time);
		if (res)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_EVENT_TYPE) {
		if (ev->type != info->type)
			return 0;
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_IGNORE_OFF) {
		/* Do not remove off events */
		switch (ev->type) {
		case SNDRV_SEQ_EVENT_NOTEOFF:
		/* case SNDRV_SEQ_EVENT_SAMPLE_STOP: */
			return 0;
		default:
			break;
		}
	}
	if (info->remove_mode & SNDRV_SEQ_REMOVE_TAG_MATCH) {
		if (info->tag != ev->tag)
			return 0;
	}

	return 1;
}

/* remove cells matching remove criteria */
void snd_seq_prioq_remove_events(struct snd_seq_prioq * f, int client,
				 struct snd_seq_remove_events *info)
{
	struct snd_seq_event_cell *cell, *next;
	unsigned long flags;
	struct snd_seq_event_cell *prev = NULL;
	struct snd_seq_event_cell *freefirst = NULL, *freeprev = NULL, *freenext;

	/* collect all removed cells */
	spin_lock_irqsave(&f->lock, flags);
	cell = f->head;

	while (cell) {
		next = cell->next;
		if (cell->event.source.client == client &&
			prioq_remove_match(info, &cell->event)) {

			/* remove cell from prioq */
			if (cell == f->head) {
				f->head = cell->next;
			} else {
				prev->next = cell->next;
			}

			if (cell == f->tail)
				f->tail = cell->next;
			f->cells--;

			/* add cell to free list */
			cell->next = NULL;
			if (freefirst == NULL) {
				freefirst = cell;
			} else {
				freeprev->next = cell;
			}

			freeprev = cell;
		} else {
			prev = cell;
		}
		cell = next;		
	}
	spin_unlock_irqrestore(&f->lock, flags);	

	/* remove selected cells */
	while (freefirst) {
		freenext = freefirst->next;
		snd_seq_cell_free(freefirst);
		freefirst = freenext;
	}
}


