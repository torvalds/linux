/*
 *   ALSA sequencer Client Manager
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
#ifndef __SND_SEQ_CLIENTMGR_H
#define __SND_SEQ_CLIENTMGR_H

#include <sound/seq_kernel.h>
#include <linux/bitops.h>
#include "seq_fifo.h"
#include "seq_ports.h"
#include "seq_lock.h"


/* client manager */

struct snd_seq_user_client {
	struct file *file;	/* file struct of client */
	/* ... */
	struct pid *owner;
	
	/* fifo */
	struct snd_seq_fifo *fifo;	/* queue for incoming events */
	int fifo_pool_size;
};

struct snd_seq_kernel_client {
	/* ... */
	struct snd_card *card;
};


struct snd_seq_client {
	snd_seq_client_type_t type;
	unsigned int accept_input: 1,
		accept_output: 1;
	char name[64];		/* client name */
	int number;		/* client number */
	unsigned int filter;	/* filter flags */
	DECLARE_BITMAP(event_filter, 256);
	snd_use_lock_t use_lock;
	int event_lost;
	/* ports */
	int num_ports;		/* number of ports */
	struct list_head ports_list_head;
	rwlock_t ports_lock;
	struct mutex ports_mutex;
	struct mutex ioctl_mutex;
	int convert32;		/* convert 32->64bit */

	/* output pool */
	struct snd_seq_pool *pool;		/* memory pool for this client */

	union {
		struct snd_seq_user_client user;
		struct snd_seq_kernel_client kernel;
	} data;
};

/* usage statistics */
struct snd_seq_usage {
	int cur;
	int peak;
};


int client_init_data(void);
int snd_sequencer_device_init(void);
void snd_sequencer_device_done(void);

/* get locked pointer to client */
struct snd_seq_client *snd_seq_client_use_ptr(int clientid);

/* unlock pointer to client */
#define snd_seq_client_unlock(client) snd_use_lock_free(&(client)->use_lock)

/* dispatch event to client(s) */
int snd_seq_dispatch_event(struct snd_seq_event_cell *cell, int atomic, int hop);

/* exported to other modules */
int snd_seq_kernel_client_enqueue(int client, struct snd_seq_event *ev, int atomic, int hop);
int snd_seq_kernel_client_enqueue_blocking(int client, struct snd_seq_event * ev,
					   struct file *file, int atomic, int hop);
int snd_seq_kernel_client_write_poll(int clientid, struct file *file, poll_table *wait);
int snd_seq_client_notify_subscription(int client, int port,
				       struct snd_seq_port_subscribe *info, int evtype);

extern int seq_client_load[15];

#endif
