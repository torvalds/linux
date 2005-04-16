/*
 *   ALSA sequencer Priority Queue
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
#ifndef __SND_SEQ_PRIOQ_H
#define __SND_SEQ_PRIOQ_H

#include "seq_memory.h"


/* === PRIOQ === */

typedef struct {
	snd_seq_event_cell_t* head;      /* pointer to head of prioq */
	snd_seq_event_cell_t* tail;      /* pointer to tail of prioq */
	int cells;
	spinlock_t lock;
} prioq_t;


/* create new prioq (constructor) */
extern prioq_t *snd_seq_prioq_new(void);

/* delete prioq (destructor) */
extern void snd_seq_prioq_delete(prioq_t **fifo);

/* enqueue cell to prioq */
extern int snd_seq_prioq_cell_in(prioq_t *f, snd_seq_event_cell_t *cell);

/* dequeue cell from prioq */ 
extern snd_seq_event_cell_t *snd_seq_prioq_cell_out(prioq_t *f);

/* return number of events available in prioq */
extern int snd_seq_prioq_avail(prioq_t *f);

/* peek at cell at the head of the prioq */
extern snd_seq_event_cell_t *snd_seq_prioq_cell_peek(prioq_t *f);

/* client left queue */
extern void snd_seq_prioq_leave(prioq_t *f, int client, int timestamp);        

/* Remove events */
void snd_seq_prioq_remove_events(prioq_t * f, int client,
	snd_seq_remove_events_t *info);

#endif
