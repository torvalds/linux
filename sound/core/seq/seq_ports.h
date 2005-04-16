/*
 *   ALSA sequencer Ports 
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
#ifndef __SND_SEQ_PORTS_H
#define __SND_SEQ_PORTS_H

#include <sound/seq_kernel.h>
#include "seq_lock.h"

/* list of 'exported' ports */

/* Client ports that are not exported are still accessible, but are
 anonymous ports. 
 
 If a port supports SUBSCRIPTION, that port can send events to all
 subscribersto a special address, with address
 (queue==SNDRV_SEQ_ADDRESS_SUBSCRIBERS). The message is then send to all
 recipients that are registered in the subscription list. A typical
 application for these SUBSCRIPTION events is handling of incoming MIDI
 data. The port doesn't 'know' what other clients are interested in this
 message. If for instance a MIDI recording application would like to receive
 the events from that port, it will first have to subscribe with that port.
 
*/

typedef struct subscribers_t {
	snd_seq_port_subscribe_t info;	/* additional info */
	struct list_head src_list;	/* link of sources */
	struct list_head dest_list;	/* link of destinations */
	atomic_t ref_count;
} subscribers_t;

typedef struct port_subs_info_t {
	struct list_head list_head;	/* list of subscribed ports */
	unsigned int count;		/* count of subscribers */
	unsigned int exclusive: 1;	/* exclusive mode */
	struct rw_semaphore list_mutex;
	rwlock_t list_lock;
	snd_seq_kernel_port_open_t *open;
	snd_seq_kernel_port_close_t *close;
} port_subs_info_t;

typedef struct client_port_t {

	snd_seq_addr_t addr;		/* client/port number */
	struct module *owner;		/* owner of this port */
	char name[64];			/* port name */	
	struct list_head list;		/* port list */
	snd_use_lock_t use_lock;

	/* subscribers */
	port_subs_info_t c_src;		/* read (sender) list */
	port_subs_info_t c_dest;	/* write (dest) list */

	snd_seq_kernel_port_input_t *event_input;
	snd_seq_kernel_port_private_free_t *private_free;
	void *private_data;
	unsigned int callback_all : 1;
	unsigned int closing : 1;
	unsigned int timestamping: 1;
	unsigned int time_real: 1;
	int time_queue;
	
	/* capability, inport, output, sync */
	unsigned int capability;	/* port capability bits */
	unsigned int type;		/* port type bits */

	/* supported channels */
	int midi_channels;
	int midi_voices;
	int synth_voices;
		
} client_port_t;

/* return pointer to port structure and lock port */
client_port_t *snd_seq_port_use_ptr(client_t *client, int num);

/* search for next port - port is locked if found */
client_port_t *snd_seq_port_query_nearest(client_t *client, snd_seq_port_info_t *pinfo);

/* unlock the port */
#define snd_seq_port_unlock(port) snd_use_lock_free(&(port)->use_lock)

/* create a port, port number is returned (-1 on failure) */
client_port_t *snd_seq_create_port(client_t *client, int port_index);

/* delete a port */
int snd_seq_delete_port(client_t *client, int port);

/* delete all ports */
int snd_seq_delete_all_ports(client_t *client);

/* set port info fields */
int snd_seq_set_port_info(client_port_t *port, snd_seq_port_info_t *info);

/* get port info fields */
int snd_seq_get_port_info(client_port_t *port, snd_seq_port_info_t *info);

/* add subscriber to subscription list */
int snd_seq_port_connect(client_t *caller, client_t *s, client_port_t *sp, client_t *d, client_port_t *dp, snd_seq_port_subscribe_t *info);

/* remove subscriber from subscription list */ 
int snd_seq_port_disconnect(client_t *caller, client_t *s, client_port_t *sp, client_t *d, client_port_t *dp, snd_seq_port_subscribe_t *info);

/* subscribe port */
int snd_seq_port_subscribe(client_port_t *port, snd_seq_port_subscribe_t *info);

/* get matched subscriber */
subscribers_t *snd_seq_port_get_subscription(port_subs_info_t *src_grp, snd_seq_addr_t *dest_addr);

#endif
