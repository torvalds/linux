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

struct snd_seq_queue {
	int queue;		/* queue number */

	char name[64];		/* name of this queue */

	struct snd_seq_prioq	*tickq;		/* midi tick event queue */
	struct snd_seq_prioq	*timeq;		/* real-time event queue */	
	
	struct snd_seq_timer *timer;	/* time keeper for this queue */
	int	owner;		/* client that 'owns' the timer */
	bool	locked;		/* timer is only accesibble by owner if set */
	bool	klocked;	/* kernel lock (after START) */
	bool	check_again;	/* concurrent access happened during check */
	bool	check_blocked;	/* queue being checked */

	unsigned int flags;		/* status flags */
	unsigned int info_flags;	/* info for sync */

	spinlock_t owner_lock;
	spinlock_t check_lock;

	/* clients which uses this queue (bitmap) */
	DECLARE_BITMAP(clients_bitmap, SNDRV_SEQ_MAX_CLIENTS);
	unsigned int clients;	/* users of this queue */
	struct mutex timer_mutex;

	snd_use_lock_t use_lock;
};


/* get the number of current queues */
int snd_seq_queue_get_cur_queues(void);

/* delete queues */ 
void snd_seq_queues_delete(void);


/* create new queue (constructor) */
struct snd_seq_queue *snd_seq_queue_alloc(int client, int locked, unsigned int flags);

/* delete queue (destructor) */
int snd_seq_queue_delete(int client, int queueid);

/* notification that client has left the system */
void snd_seq_queue_client_termination(int client);

/* final stage */
void snd_seq_queue_client_leave(int client);

/* enqueue a event received from one the clients */
int snd_seq_enqueue_event(struct snd_seq_event_cell *cell, int atomic, int hop);

/* Remove events */
void snd_seq_queue_client_leave_cells(int client);
void snd_seq_queue_remove_cells(int client, struct snd_seq_remove_events *info);

/* return pointer to queue structure for specified id */
struct snd_seq_queue *queueptr(int queueid);
/* unlock */
#define queuefree(q) snd_use_lock_free(&(q)->use_lock)

/* return the (first) queue matching with the specified name */
struct snd_seq_queue *snd_seq_queue_find_name(char *name);

/* check single queue and dispatch events */
void snd_seq_check_queue(struct snd_seq_queue *q, int atomic, int hop);

/* access to queue's parameters */
int snd_seq_queue_check_access(int queueid, int client);
int snd_seq_queue_timer_set_tempo(int queueid, int client, struct snd_seq_queue_tempo *info);
int snd_seq_queue_set_owner(int queueid, int client, int locked);
int snd_seq_queue_set_locked(int queueid, int client, int locked);
int snd_seq_queue_timer_open(int queueid);
int snd_seq_queue_timer_close(int queueid);
int snd_seq_queue_use(int queueid, int client, int use);
int snd_seq_queue_is_used(int queueid, int client);

int snd_seq_control_queue(struct snd_seq_event *ev, int atomic, int hop);

#endif
