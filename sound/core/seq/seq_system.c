// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA sequencer System services Client
 *   Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "seq_system.h"
#include "seq_timer.h"
#include "seq_queue.h"

/* internal client that provide system services, access to timer etc. */

/*
 * Port "Timer"
 *      - send tempo /start/stop etc. events to this port to manipulate the 
 *        queue's timer. The queue address is specified in
 *	  data.queue.queue.
 *      - this port supports subscription. The received timer events are 
 *        broadcasted to all subscribed clients. The modified tempo
 *	  value is stored on data.queue.value.
 *	  The modifier client/port is not send.
 *
 * Port "Announce"
 *      - does not receive message
 *      - supports supscription. For each client or port attaching to or 
 *        detaching from the system an announcement is send to the subscribed
 *        clients.
 *
 * Idea: the subscription mechanism might also work handy for distributing 
 * synchronisation and timing information. In this case we would ideally have
 * a list of subscribers for each type of sync (time, tick), for each timing
 * queue.
 *
 * NOTE: the queue to be started, stopped, etc. must be specified
 *	 in data.queue.addr.queue field.  queue is used only for
 *	 scheduling, and no longer referred as affected queue.
 *	 They are used only for timer broadcast (see above).
 *							-- iwai
 */


/* client id of our system client */
static int sysclient = -1;

/* port id numbers for this client */
static int announce_port = -1;



/* fill standard header data, source port & channel are filled in */
static int setheader(struct snd_seq_event * ev, int client, int port)
{
	if (announce_port < 0)
		return -ENODEV;

	memset(ev, 0, sizeof(struct snd_seq_event));

	ev->flags &= ~SNDRV_SEQ_EVENT_LENGTH_MASK;
	ev->flags |= SNDRV_SEQ_EVENT_LENGTH_FIXED;

	ev->source.client = sysclient;
	ev->source.port = announce_port;
	ev->dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;

	/* fill data */
	/*ev->data.addr.queue = SNDRV_SEQ_ADDRESS_UNKNOWN;*/
	ev->data.addr.client = client;
	ev->data.addr.port = port;

	return 0;
}


/* entry points for broadcasting system events */
void snd_seq_system_broadcast(int client, int port, int type)
{
	struct snd_seq_event ev;
	
	if (setheader(&ev, client, port) < 0)
		return;
	ev.type = type;
	snd_seq_kernel_client_dispatch(sysclient, &ev, 0, 0);
}

/* entry points for broadcasting system events */
int snd_seq_system_notify(int client, int port, struct snd_seq_event *ev)
{
	ev->flags = SNDRV_SEQ_EVENT_LENGTH_FIXED;
	ev->source.client = sysclient;
	ev->source.port = announce_port;
	ev->dest.client = client;
	ev->dest.port = port;
	return snd_seq_kernel_client_dispatch(sysclient, ev, 0, 0);
}

/* call-back handler for timer events */
static int event_input_timer(struct snd_seq_event * ev, int direct, void *private_data, int atomic, int hop)
{
	return snd_seq_control_queue(ev, atomic, hop);
}

/* register our internal client */
int __init snd_seq_system_client_init(void)
{
	struct snd_seq_port_callback pcallbacks;
	struct snd_seq_port_info *port;
	int err;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	memset(&pcallbacks, 0, sizeof(pcallbacks));
	pcallbacks.owner = THIS_MODULE;
	pcallbacks.event_input = event_input_timer;

	/* register client */
	sysclient = snd_seq_create_kernel_client(NULL, 0, "System");
	if (sysclient < 0) {
		kfree(port);
		return sysclient;
	}

	/* register timer */
	strcpy(port->name, "Timer");
	port->capability = SNDRV_SEQ_PORT_CAP_WRITE; /* accept queue control */
	port->capability |= SNDRV_SEQ_PORT_CAP_READ|SNDRV_SEQ_PORT_CAP_SUBS_READ; /* for broadcast */
	port->kernel = &pcallbacks;
	port->type = 0;
	port->flags = SNDRV_SEQ_PORT_FLG_GIVEN_PORT;
	port->addr.client = sysclient;
	port->addr.port = SNDRV_SEQ_PORT_SYSTEM_TIMER;
	err = snd_seq_kernel_client_ctl(sysclient, SNDRV_SEQ_IOCTL_CREATE_PORT,
					port);
	if (err < 0)
		goto error_port;

	/* register announcement port */
	strcpy(port->name, "Announce");
	port->capability = SNDRV_SEQ_PORT_CAP_READ|SNDRV_SEQ_PORT_CAP_SUBS_READ; /* for broadcast only */
	port->kernel = NULL;
	port->type = 0;
	port->flags = SNDRV_SEQ_PORT_FLG_GIVEN_PORT;
	port->addr.client = sysclient;
	port->addr.port = SNDRV_SEQ_PORT_SYSTEM_ANNOUNCE;
	err = snd_seq_kernel_client_ctl(sysclient, SNDRV_SEQ_IOCTL_CREATE_PORT,
					port);
	if (err < 0)
		goto error_port;
	announce_port = port->addr.port;

	kfree(port);
	return 0;

 error_port:
	snd_seq_system_client_done();
	kfree(port);
	return err;
}


/* unregister our internal client */
void snd_seq_system_client_done(void)
{
	int oldsysclient = sysclient;

	if (oldsysclient >= 0) {
		sysclient = -1;
		announce_port = -1;
		snd_seq_delete_kernel_client(oldsysclient);
	}
}
