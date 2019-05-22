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

struct snd_seq_subscribers {
	struct snd_seq_port_subscribe info;	/* additional info */
	struct list_head src_list;	/* link of sources */
	struct list_head dest_list;	/* link of destinations */
	atomic_t ref_count;
};

struct snd_seq_port_subs_info {
	struct list_head list_head;	/* list of subscribed ports */
	unsigned int count;		/* count of subscribers */
	unsigned int exclusive: 1;	/* exclusive mode */
	struct rw_semaphore list_mutex;
	rwlock_t list_lock;
	int (*open)(void *private_data, struct snd_seq_port_subscribe *info);
	int (*close)(void *private_data, struct snd_seq_port_subscribe *info);
};

struct snd_seq_client_port {

	struct snd_seq_addr addr;	/* client/port number */
	struct module *owner;		/* owner of this port */
	char name[64];			/* port name */	
	struct list_head list;		/* port list */
	snd_use_lock_t use_lock;

	/* subscribers */
	struct snd_seq_port_subs_info c_src;	/* read (sender) list */
	struct snd_seq_port_subs_info c_dest;	/* write (dest) list */

	int (*event_input)(struct snd_seq_event *ev, int direct, void *private_data,
			   int atomic, int hop);
	void (*private_free)(void *private_data);
	void *private_data;
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
		
};

struct snd_seq_client;

/* return pointer to port structure and lock port */
struct snd_seq_client_port *snd_seq_port_use_ptr(struct snd_seq_client *client, int num);

/* search for next port - port is locked if found */
struct snd_seq_client_port *snd_seq_port_query_nearest(struct snd_seq_client *client,
						       struct snd_seq_port_info *pinfo);

/* unlock the port */
#define snd_seq_port_unlock(port) snd_use_lock_free(&(port)->use_lock)

/* create a port, port number is returned (-1 on failure) */
struct snd_seq_client_port *snd_seq_create_port(struct snd_seq_client *client, int port_index);

/* delete a port */
int snd_seq_delete_port(struct snd_seq_client *client, int port);

/* delete all ports */
int snd_seq_delete_all_ports(struct snd_seq_client *client);

/* set port info fields */
int snd_seq_set_port_info(struct snd_seq_client_port *port,
			  struct snd_seq_port_info *info);

/* get port info fields */
int snd_seq_get_port_info(struct snd_seq_client_port *port,
			  struct snd_seq_port_info *info);

/* add subscriber to subscription list */
int snd_seq_port_connect(struct snd_seq_client *caller,
			 struct snd_seq_client *s, struct snd_seq_client_port *sp,
			 struct snd_seq_client *d, struct snd_seq_client_port *dp,
			 struct snd_seq_port_subscribe *info);

/* remove subscriber from subscription list */ 
int snd_seq_port_disconnect(struct snd_seq_client *caller,
			    struct snd_seq_client *s, struct snd_seq_client_port *sp,
			    struct snd_seq_client *d, struct snd_seq_client_port *dp,
			    struct snd_seq_port_subscribe *info);

/* subscribe port */
int snd_seq_port_subscribe(struct snd_seq_client_port *port,
			   struct snd_seq_port_subscribe *info);

/* get matched subscriber */
int snd_seq_port_get_subscription(struct snd_seq_port_subs_info *src_grp,
				  struct snd_seq_addr *dest_addr,
				  struct snd_seq_port_subscribe *subs);

#endif
