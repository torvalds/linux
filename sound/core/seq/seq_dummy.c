/*
 * ALSA sequencer MIDI-through client
 * Copyright (c) 1999-2000 by Takashi Iwai <tiwai@suse.de>
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

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include "seq_clientmgr.h"
#include <sound/initval.h>
#include <sound/asoundef.h>

/*

  Sequencer MIDI-through client

  This gives a simple midi-through client.  All the normal input events
  are redirected to output port immediately.
  The routing can be done via aconnect program in alsa-utils.

  Each client has a static client number 62 (= SNDRV_SEQ_CLIENT_DUMMY).
  If you want to auto-load this module, you may add the following alias
  in your /etc/conf.modules file.

	alias snd-seq-client-62  snd-seq-dummy

  The module is loaded on demand for client 62, or /proc/asound/seq/
  is accessed.  If you don't need this module to be loaded, alias
  snd-seq-client-62 as "off".  This will help modprobe.

  The number of ports to be created can be specified via the module
  parameter "ports".  For example, to create four ports, add the
  following option in /etc/modprobe.conf:

	option snd-seq-dummy ports=4

  The modle option "duplex=1" enables duplex operation to the port.
  In duplex mode, a pair of ports are created instead of single port,
  and events are tunneled between pair-ports.  For example, input to
  port A is sent to output port of another port B and vice versa.
  In duplex mode, each port has DUPLEX capability.

 */


MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("ALSA sequencer MIDI-through client");
MODULE_LICENSE("GPL");
MODULE_ALIAS("snd-seq-client-" __stringify(SNDRV_SEQ_CLIENT_DUMMY));

static int ports = 1;
static int duplex = 0;

module_param(ports, int, 0444);
MODULE_PARM_DESC(ports, "number of ports to be created");
module_param(duplex, bool, 0444);
MODULE_PARM_DESC(duplex, "create DUPLEX ports");

struct snd_seq_dummy_port {
	int client;
	int port;
	int duplex;
	int connect;
};

static int my_client = -1;

/*
 * unuse callback - send ALL_SOUNDS_OFF and RESET_CONTROLLERS events
 * to subscribers.
 * Note: this callback is called only after all subscribers are removed.
 */
static int
dummy_unuse(void *private_data, struct snd_seq_port_subscribe *info)
{
	struct snd_seq_dummy_port *p;
	int i;
	struct snd_seq_event ev;

	p = private_data;
	memset(&ev, 0, sizeof(ev));
	if (p->duplex)
		ev.source.port = p->connect;
	else
		ev.source.port = p->port;
	ev.dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
	ev.type = SNDRV_SEQ_EVENT_CONTROLLER;
	for (i = 0; i < 16; i++) {
		ev.data.control.channel = i;
		ev.data.control.param = MIDI_CTL_ALL_SOUNDS_OFF;
		snd_seq_kernel_client_dispatch(p->client, &ev, 0, 0);
		ev.data.control.param = MIDI_CTL_RESET_CONTROLLERS;
		snd_seq_kernel_client_dispatch(p->client, &ev, 0, 0);
	}
	return 0;
}

/*
 * event input callback - just redirect events to subscribers
 */
static int
dummy_input(struct snd_seq_event *ev, int direct, void *private_data,
	    int atomic, int hop)
{
	struct snd_seq_dummy_port *p;
	struct snd_seq_event tmpev;

	p = private_data;
	if (ev->source.client == SNDRV_SEQ_CLIENT_SYSTEM ||
	    ev->type == SNDRV_SEQ_EVENT_KERNEL_ERROR)
		return 0; /* ignore system messages */
	tmpev = *ev;
	if (p->duplex)
		tmpev.source.port = p->connect;
	else
		tmpev.source.port = p->port;
	tmpev.dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
	return snd_seq_kernel_client_dispatch(p->client, &tmpev, atomic, hop);
}

/*
 * free_private callback
 */
static void
dummy_free(void *private_data)
{
	kfree(private_data);
}

/*
 * create a port
 */
static struct snd_seq_dummy_port __init *
create_port(int idx, int type)
{
	struct snd_seq_port_info pinfo;
	struct snd_seq_port_callback pcb;
	struct snd_seq_dummy_port *rec;

	if ((rec = kzalloc(sizeof(*rec), GFP_KERNEL)) == NULL)
		return NULL;

	rec->client = my_client;
	rec->duplex = duplex;
	rec->connect = 0;
	memset(&pinfo, 0, sizeof(pinfo));
	pinfo.addr.client = my_client;
	if (duplex)
		sprintf(pinfo.name, "Midi Through Port-%d:%c", idx,
			(type ? 'B' : 'A'));
	else
		sprintf(pinfo.name, "Midi Through Port-%d", idx);
	pinfo.capability = SNDRV_SEQ_PORT_CAP_READ | SNDRV_SEQ_PORT_CAP_SUBS_READ;
	pinfo.capability |= SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
	if (duplex)
		pinfo.capability |= SNDRV_SEQ_PORT_CAP_DUPLEX;
	pinfo.type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC
		| SNDRV_SEQ_PORT_TYPE_SOFTWARE
		| SNDRV_SEQ_PORT_TYPE_PORT;
	memset(&pcb, 0, sizeof(pcb));
	pcb.owner = THIS_MODULE;
	pcb.unuse = dummy_unuse;
	pcb.event_input = dummy_input;
	pcb.private_free = dummy_free;
	pcb.private_data = rec;
	pinfo.kernel = &pcb;
	if (snd_seq_kernel_client_ctl(my_client, SNDRV_SEQ_IOCTL_CREATE_PORT, &pinfo) < 0) {
		kfree(rec);
		return NULL;
	}
	rec->port = pinfo.addr.port;
	return rec;
}

/*
 * register client and create ports
 */
static int __init
register_client(void)
{
	struct snd_seq_dummy_port *rec1, *rec2;
	int i;

	if (ports < 1) {
		snd_printk(KERN_ERR "invalid number of ports %d\n", ports);
		return -EINVAL;
	}

	/* create client */
	my_client = snd_seq_create_kernel_client(NULL, SNDRV_SEQ_CLIENT_DUMMY,
						 "Midi Through");
	if (my_client < 0)
		return my_client;

	/* create ports */
	for (i = 0; i < ports; i++) {
		rec1 = create_port(i, 0);
		if (rec1 == NULL) {
			snd_seq_delete_kernel_client(my_client);
			return -ENOMEM;
		}
		if (duplex) {
			rec2 = create_port(i, 1);
			if (rec2 == NULL) {
				snd_seq_delete_kernel_client(my_client);
				return -ENOMEM;
			}
			rec1->connect = rec2->port;
			rec2->connect = rec1->port;
		}
	}

	return 0;
}

/*
 * delete client if exists
 */
static void __exit
delete_client(void)
{
	if (my_client >= 0)
		snd_seq_delete_kernel_client(my_client);
}

/*
 *  Init part
 */

static int __init alsa_seq_dummy_init(void)
{
	int err;
	snd_seq_autoload_lock();
	err = register_client();
	snd_seq_autoload_unlock();
	return err;
}

static void __exit alsa_seq_dummy_exit(void)
{
	delete_client();
}

module_init(alsa_seq_dummy_init)
module_exit(alsa_seq_dummy_exit)
