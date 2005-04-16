/*
 *   ALSA sequencer FIFO
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
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
#ifndef __SND_SEQ_FIFO_H
#define __SND_SEQ_FIFO_H

#include "seq_memory.h"
#include "seq_lock.h"


/* === FIFO === */

typedef struct {
	pool_t *pool;			/* FIFO pool */
	snd_seq_event_cell_t* head;    	/* pointer to head of fifo */
	snd_seq_event_cell_t* tail;    	/* pointer to tail of fifo */
	int cells;
	spinlock_t lock;
	snd_use_lock_t use_lock;
	wait_queue_head_t input_sleep;
	atomic_t overflow;

} fifo_t;

/* create new fifo (constructor) */
extern fifo_t *snd_seq_fifo_new(int poolsize);

/* delete fifo (destructor) */
extern void snd_seq_fifo_delete(fifo_t **f);


/* enqueue event to fifo */
extern int snd_seq_fifo_event_in(fifo_t *f, snd_seq_event_t *event);

/* lock fifo from release */
#define snd_seq_fifo_lock(fifo)		snd_use_lock_use(&(fifo)->use_lock)
#define snd_seq_fifo_unlock(fifo)	snd_use_lock_free(&(fifo)->use_lock)

/* get a cell from fifo - fifo should be locked */
int snd_seq_fifo_cell_out(fifo_t *f, snd_seq_event_cell_t **cellp, int nonblock);

/* free dequeued cell - fifo should be locked */
extern void snd_seq_fifo_cell_putback(fifo_t *f, snd_seq_event_cell_t *cell);

/* clean up queue */
extern void snd_seq_fifo_clear(fifo_t *f);

/* polling */
extern int snd_seq_fifo_poll_wait(fifo_t *f, struct file *file, poll_table *wait);

/* resize pool in fifo */
int snd_seq_fifo_resize(fifo_t *f, int poolsize);


#endif
