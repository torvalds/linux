/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   ALSA sequencer FIFO
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 */
#ifndef __SND_SEQ_FIFO_H
#define __SND_SEQ_FIFO_H

#include "seq_memory.h"
#include "seq_lock.h"


/* === FIFO === */

struct snd_seq_fifo {
	struct snd_seq_pool *pool;		/* FIFO pool */
	struct snd_seq_event_cell *head;    	/* pointer to head of fifo */
	struct snd_seq_event_cell *tail;    	/* pointer to tail of fifo */
	int cells;
	spinlock_t lock;
	snd_use_lock_t use_lock;
	wait_queue_head_t input_sleep;
	atomic_t overflow;

};

/* create new fifo (constructor) */
struct snd_seq_fifo *snd_seq_fifo_new(int poolsize);

/* delete fifo (destructor) */
void snd_seq_fifo_delete(struct snd_seq_fifo **f);


/* enqueue event to fifo */
int snd_seq_fifo_event_in(struct snd_seq_fifo *f, struct snd_seq_event *event);

/* lock fifo from release */
#define snd_seq_fifo_lock(fifo)		snd_use_lock_use(&(fifo)->use_lock)
#define snd_seq_fifo_unlock(fifo)	snd_use_lock_free(&(fifo)->use_lock)

/* get a cell from fifo - fifo should be locked */
int snd_seq_fifo_cell_out(struct snd_seq_fifo *f, struct snd_seq_event_cell **cellp, int nonblock);

/* free dequeued cell - fifo should be locked */
void snd_seq_fifo_cell_putback(struct snd_seq_fifo *f, struct snd_seq_event_cell *cell);

/* clean up queue */
void snd_seq_fifo_clear(struct snd_seq_fifo *f);

/* polling */
int snd_seq_fifo_poll_wait(struct snd_seq_fifo *f, struct file *file, poll_table *wait);

/* resize pool in fifo */
int snd_seq_fifo_resize(struct snd_seq_fifo *f, int poolsize);

/* get the number of unused cells safely */
int snd_seq_fifo_unused_cells(struct snd_seq_fifo *f);

#endif
