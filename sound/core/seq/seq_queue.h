/*
 *   ALSA sequencer Queue handling
 *   Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
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
#ifndef __SND_SEQ_QUEUE_H
#define __SND_SEQ_QUEUE_H

#include "seq_memory.h"
#include "seq_prioq.h"
#include "seq_timer.h"
#include "seq_lock.h"
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/bitops.h>

#define SEQ_QUEUE_NO_OWNER (-1)

struct _snd_seq_queue {
	int queue;		/* queue number */

	char name[64];		/* name of this queue */

	prioq_t	*tickq;		/* midi tick event queue */
	prioq_t	*timeq;		/* real-time event queue */	
	
	seq_timer_t *timer;	/* time keeper for this queue */
	int	owner;		/* client that 'owns' the timer */
	unsigned int	locked:1,	/* timer is only accesibble by owner if set */
		klocked:1,	/* kernel lock (after START) */	
		check_again:1,
		check_blocked:1;

	unsigned int flags;		/* status flags */
	unsigned int info_flags;	/* info for sync */

	spinlock_t owner_lock;
	spinlock_t check_lock;

	/* clients which uses this queue (bitmap) */
	DECLARE_BITMAP(clients_bitmap, SNDRV_SEQ_MAX_CLIENTS);
	unsigned int clients;	/* users of this queue */
	struct semaphore timer_mutex;

	snd_use_lock_t use_lock;
};


/* get the number of current queues */
int snd_seq_queue_get_cur_queues(void);

/* init queues structure */
int snd_seq_queues_init(void);

/* delete queues */ 
void snd_seq_queues_delete(void);


/* create new queue (constructor) */
int snd_seq_queue_alloc(int client, int locked, unsigned int flags);

/* delete queue (destructor) */
int snd_seq_queue_delete(int client, int queueid);

/* notification that client has left the system */
void snd_seq_queue_client_termination(int client);

/* final stage */
void snd_seq_queue_client_leave(int client);

/* enqueue a event received from one the clients */
int snd_seq_enqueue_event(snd_seq_event_cell_t *cell, int atomic, int hop);

/* Remove events */
void snd_seq_queue_client_leave_cells(int client);
void snd_seq_queue_remove_cells(int client, snd_seq_remove_events_t *info);

/* return pointer to queue structure for specified id */
queue_t *queueptr(int queueid);
/* unlock */
#define queuefree(q) snd_use_lock_free(&(q)->use_lock)

/* return the (first) queue matching with the specified name */
queue_t *snd_seq_queue_find_name(char *name);

/* check single queue and dispatch events */
void snd_seq_check_queue(queue_t *q, int atomic, int hop);

/* access to queue's parameters */
int snd_seq_queue_check_access(int queueid, int client);
int snd_seq_queue_timer_set_tempo(int queueid, int client, snd_seq_queue_tempo_t *info);
int snd_seq_queue_set_owner(int queueid, int client, int locked);
int snd_seq_queue_set_locked(int queueid, int client, int locked);
int snd_seq_queue_timer_open(int queueid);
int snd_seq_queue_timer_close(int queueid);
int snd_seq_queue_use(int queueid, int client, int use);
int snd_seq_queue_is_used(int queueid, int client);

int snd_seq_control_queue(snd_seq_event_t *ev, int atomic, int hop);

/*
 * 64bit division - for sync stuff..
 */
#if defined(i386) || defined(i486)

#define udiv_qrnnd(q, r, n1, n0, d) \
  __asm__ ("divl %4"		\
	   : "=a" ((u32)(q)),	\
	     "=d" ((u32)(r))	\
	   : "0" ((u32)(n0)),	\
	     "1" ((u32)(n1)),	\
	     "rm" ((u32)(d)))

#define u64_div(x,y,q) do {u32 __tmp; udiv_qrnnd(q, __tmp, (x)>>32, x, y);} while (0)
#define u64_mod(x,y,r) do {u32 __tmp; udiv_qrnnd(__tmp, q, (x)>>32, x, y);} while (0)
#define u64_divmod(x,y,q,r) udiv_qrnnd(q, r, (x)>>32, x, y)

#else
#define u64_div(x,y,q)	((q) = (u32)((u64)(x) / (u64)(y)))
#define u64_mod(x,y,r)	((r) = (u32)((u64)(x) % (u64)(y)))
#define u64_divmod(x,y,q,r) (u64_div(x,y,q), u64_mod(x,y,r))
#endif


#endif
