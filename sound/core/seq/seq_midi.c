/*
 *   Generic MIDI synth driver for ALSA sequencer
 *   Copyright (c) 1998 by Frank van de Pol <fvdpol@coil.demon.nl>
 *                         Jaroslav Kysela <perex@suse.cz>
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
 
/* 
Possible options for midisynth module:
	- automatic opening of midi ports on first received event or subscription
	  (close will be performed when client leaves)
*/


#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/moduleparam.h>
#include <asm/semaphore.h>
#include <sound/core.h>
#include <sound/rawmidi.h>
#include <sound/seq_kernel.h>
#include <sound/seq_device.h>
#include <sound/seq_midi_event.h>
#include <sound/initval.h>

MODULE_AUTHOR("Frank van de Pol <fvdpol@coil.demon.nl>, Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture sequencer MIDI synth.");
MODULE_LICENSE("GPL");
static int output_buffer_size = PAGE_SIZE;
module_param(output_buffer_size, int, 0644);
MODULE_PARM_DESC(output_buffer_size, "Output buffer size in bytes.");
static int input_buffer_size = PAGE_SIZE;
module_param(input_buffer_size, int, 0644);
MODULE_PARM_DESC(input_buffer_size, "Input buffer size in bytes.");

/* data for this midi synth driver */
typedef struct {
	snd_card_t *card;
	int device;
	int subdevice;
	snd_rawmidi_file_t input_rfile;
	snd_rawmidi_file_t output_rfile;
	int seq_client;
	int seq_port;
	snd_midi_event_t *parser;
} seq_midisynth_t;

typedef struct {
	int seq_client;
	int num_ports;
	int ports_per_device[SNDRV_RAWMIDI_DEVICES];
 	seq_midisynth_t *ports[SNDRV_RAWMIDI_DEVICES];
} seq_midisynth_client_t;

static seq_midisynth_client_t *synths[SNDRV_CARDS];
static DECLARE_MUTEX(register_mutex);

/* handle rawmidi input event (MIDI v1.0 stream) */
static void snd_midi_input_event(snd_rawmidi_substream_t * substream)
{
	snd_rawmidi_runtime_t *runtime;
	seq_midisynth_t *msynth;
	snd_seq_event_t ev;
	char buf[16], *pbuf;
	long res, count;

	if (substream == NULL)
		return;
	runtime = substream->runtime;
	msynth = (seq_midisynth_t *) runtime->private_data;
	if (msynth == NULL)
		return;
	memset(&ev, 0, sizeof(ev));
	while (runtime->avail > 0) {
		res = snd_rawmidi_kernel_read(substream, buf, sizeof(buf));
		if (res <= 0)
			continue;
		if (msynth->parser == NULL)
			continue;
		pbuf = buf;
		while (res > 0) {
			count = snd_midi_event_encode(msynth->parser, pbuf, res, &ev);
			if (count < 0)
				break;
			pbuf += count;
			res -= count;
			if (ev.type != SNDRV_SEQ_EVENT_NONE) {
				ev.source.port = msynth->seq_port;
				ev.dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
				snd_seq_kernel_client_dispatch(msynth->seq_client, &ev, 1, 0);
				/* clear event and reset header */
				memset(&ev, 0, sizeof(ev));
			}
		}
	}
}

static int dump_midi(snd_rawmidi_substream_t *substream, const char *buf, int count)
{
	snd_rawmidi_runtime_t *runtime;
	int tmp;

	snd_assert(substream != NULL || buf != NULL, return -EINVAL);
	runtime = substream->runtime;
	if ((tmp = runtime->avail) < count) {
		snd_printd("warning, output event was lost (count = %i, available = %i)\n", count, tmp);
		return -ENOMEM;
	}
	if (snd_rawmidi_kernel_write(substream, buf, count) < count)
		return -EINVAL;
	return 0;
}

static int event_process_midi(snd_seq_event_t * ev, int direct,
			      void *private_data, int atomic, int hop)
{
	seq_midisynth_t *msynth = (seq_midisynth_t *) private_data;
	unsigned char msg[10];	/* buffer for constructing midi messages */
	snd_rawmidi_substream_t *substream;
	int len;

	snd_assert(msynth != NULL, return -EINVAL);
	substream = msynth->output_rfile.output;
	if (substream == NULL)
		return -ENODEV;
	if (ev->type == SNDRV_SEQ_EVENT_SYSEX) {	/* special case, to save space */
		if ((ev->flags & SNDRV_SEQ_EVENT_LENGTH_MASK) != SNDRV_SEQ_EVENT_LENGTH_VARIABLE) {
			/* invalid event */
			snd_printd("seq_midi: invalid sysex event flags = 0x%x\n", ev->flags);
			return 0;
		}
		snd_seq_dump_var_event(ev, (snd_seq_dump_func_t)dump_midi, substream);
		snd_midi_event_reset_decode(msynth->parser);
	} else {
		if (msynth->parser == NULL)
			return -EIO;
		len = snd_midi_event_decode(msynth->parser, msg, sizeof(msg), ev);
		if (len < 0)
			return 0;
		if (dump_midi(substream, msg, len) < 0)
			snd_midi_event_reset_decode(msynth->parser);
	}
	return 0;
}


static int snd_seq_midisynth_new(seq_midisynth_t *msynth,
				 snd_card_t *card,
				 int device,
				 int subdevice)
{
	if (snd_midi_event_new(MAX_MIDI_EVENT_BUF, &msynth->parser) < 0)
		return -ENOMEM;
	msynth->card = card;
	msynth->device = device;
	msynth->subdevice = subdevice;
	return 0;
}

/* open associated midi device for input */
static int midisynth_subscribe(void *private_data, snd_seq_port_subscribe_t *info)
{
	int err;
	seq_midisynth_t *msynth = (seq_midisynth_t *)private_data;
	snd_rawmidi_runtime_t *runtime;
	snd_rawmidi_params_t params;

	/* open midi port */
	if ((err = snd_rawmidi_kernel_open(msynth->card->number, msynth->device, msynth->subdevice, SNDRV_RAWMIDI_LFLG_INPUT, &msynth->input_rfile)) < 0) {
		snd_printd("midi input open failed!!!\n");
		return err;
	}
	runtime = msynth->input_rfile.input->runtime;
	memset(&params, 0, sizeof(params));
	params.avail_min = 1;
	params.buffer_size = input_buffer_size;
	if ((err = snd_rawmidi_input_params(msynth->input_rfile.input, &params)) < 0) {
		snd_rawmidi_kernel_release(&msynth->input_rfile);
		return err;
	}
	snd_midi_event_reset_encode(msynth->parser);
	runtime->event = snd_midi_input_event;
	runtime->private_data = msynth;
	snd_rawmidi_kernel_read(msynth->input_rfile.input, NULL, 0);
	return 0;
}

/* close associated midi device for input */
static int midisynth_unsubscribe(void *private_data, snd_seq_port_subscribe_t *info)
{
	int err;
	seq_midisynth_t *msynth = (seq_midisynth_t *)private_data;

	snd_assert(msynth->input_rfile.input != NULL, return -EINVAL);
	err = snd_rawmidi_kernel_release(&msynth->input_rfile);
	return err;
}

/* open associated midi device for output */
static int midisynth_use(void *private_data, snd_seq_port_subscribe_t *info)
{
	int err;
	seq_midisynth_t *msynth = (seq_midisynth_t *)private_data;
	snd_rawmidi_params_t params;

	/* open midi port */
	if ((err = snd_rawmidi_kernel_open(msynth->card->number, msynth->device, msynth->subdevice, SNDRV_RAWMIDI_LFLG_OUTPUT, &msynth->output_rfile)) < 0) {
		snd_printd("midi output open failed!!!\n");
		return err;
	}
	memset(&params, 0, sizeof(params));
	params.avail_min = 1;
	params.buffer_size = output_buffer_size;
	if ((err = snd_rawmidi_output_params(msynth->output_rfile.output, &params)) < 0) {
		snd_rawmidi_kernel_release(&msynth->output_rfile);
		return err;
	}
	snd_midi_event_reset_decode(msynth->parser);
	return 0;
}

/* close associated midi device for output */
static int midisynth_unuse(void *private_data, snd_seq_port_subscribe_t *info)
{
	seq_midisynth_t *msynth = (seq_midisynth_t *)private_data;
	unsigned char buf = 0xff; /* MIDI reset */

	snd_assert(msynth->output_rfile.output != NULL, return -EINVAL);
	/* sending single MIDI reset message to shut the device up */
	snd_rawmidi_kernel_write(msynth->output_rfile.output, &buf, 1);
	snd_rawmidi_drain_output(msynth->output_rfile.output);
	return snd_rawmidi_kernel_release(&msynth->output_rfile);
}

/* delete given midi synth port */
static void snd_seq_midisynth_delete(seq_midisynth_t *msynth)
{
	if (msynth == NULL)
		return;

	if (msynth->seq_client > 0) {
		/* delete port */
		snd_seq_event_port_detach(msynth->seq_client, msynth->seq_port);
	}

	if (msynth->parser)
		snd_midi_event_free(msynth->parser);
}

/* set our client name */
static int set_client_name(seq_midisynth_client_t *client, snd_card_t *card,
			   snd_rawmidi_info_t *rmidi)
{
	snd_seq_client_info_t cinfo;
	const char *name;

	memset(&cinfo, 0, sizeof(cinfo));
	cinfo.client = client->seq_client;
	cinfo.type = KERNEL_CLIENT;
	name = rmidi->name[0] ? (const char *)rmidi->name : "External MIDI";
	strlcpy(cinfo.name, name, sizeof(cinfo.name));
	return snd_seq_kernel_client_ctl(client->seq_client, SNDRV_SEQ_IOCTL_SET_CLIENT_INFO, &cinfo);
}

/* register new midi synth port */
static int
snd_seq_midisynth_register_port(snd_seq_device_t *dev)
{
	seq_midisynth_client_t *client;
	seq_midisynth_t *msynth, *ms;
	snd_seq_port_info_t *port;
	snd_rawmidi_info_t *info;
	int newclient = 0;
	unsigned int p, ports;
	snd_seq_client_callback_t callbacks;
	snd_seq_port_callback_t pcallbacks;
	snd_card_t *card = dev->card;
	int device = dev->device;
	unsigned int input_count = 0, output_count = 0;

	snd_assert(card != NULL && device >= 0 && device < SNDRV_RAWMIDI_DEVICES, return -EINVAL);
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (! info)
		return -ENOMEM;
	info->device = device;
	info->stream = SNDRV_RAWMIDI_STREAM_OUTPUT;
	info->subdevice = 0;
	if (snd_rawmidi_info_select(card, info) >= 0)
		output_count = info->subdevices_count;
	info->stream = SNDRV_RAWMIDI_STREAM_INPUT;
	if (snd_rawmidi_info_select(card, info) >= 0) {
		input_count = info->subdevices_count;
	}
	ports = output_count;
	if (ports < input_count)
		ports = input_count;
	if (ports == 0) {
		kfree(info);
		return -ENODEV;
	}
	if (ports > (256 / SNDRV_RAWMIDI_DEVICES))
		ports = 256 / SNDRV_RAWMIDI_DEVICES;

	down(&register_mutex);
	client = synths[card->number];
	if (client == NULL) {
		newclient = 1;
		client = kzalloc(sizeof(*client), GFP_KERNEL);
		if (client == NULL) {
			up(&register_mutex);
			kfree(info);
			return -ENOMEM;
		}
		memset(&callbacks, 0, sizeof(callbacks));
		callbacks.private_data = client;
		callbacks.allow_input = callbacks.allow_output = 1;
		client->seq_client = snd_seq_create_kernel_client(card, 0, &callbacks);
		if (client->seq_client < 0) {
			kfree(client);
			up(&register_mutex);
			kfree(info);
			return -ENOMEM;
		}
		set_client_name(client, card, info);
	} else if (device == 0)
		set_client_name(client, card, info); /* use the first device's name */

	msynth = kcalloc(ports, sizeof(seq_midisynth_t), GFP_KERNEL);
	port = kmalloc(sizeof(*port), GFP_KERNEL);
	if (msynth == NULL || port == NULL)
		goto __nomem;

	for (p = 0; p < ports; p++) {
		ms = &msynth[p];

		if (snd_seq_midisynth_new(ms, card, device, p) < 0)
			goto __nomem;

		/* declare port */
		memset(port, 0, sizeof(*port));
		port->addr.client = client->seq_client;
		port->addr.port = device * (256 / SNDRV_RAWMIDI_DEVICES) + p;
		port->flags = SNDRV_SEQ_PORT_FLG_GIVEN_PORT;
		memset(info, 0, sizeof(*info));
		info->device = device;
		if (p < output_count)
			info->stream = SNDRV_RAWMIDI_STREAM_OUTPUT;
		else
			info->stream = SNDRV_RAWMIDI_STREAM_INPUT;
		info->subdevice = p;
		if (snd_rawmidi_info_select(card, info) >= 0)
			strcpy(port->name, info->subname);
		if (! port->name[0]) {
			if (info->name[0]) {
				if (ports > 1)
					snprintf(port->name, sizeof(port->name), "%s-%d", info->name, p);
				else
					snprintf(port->name, sizeof(port->name), "%s", info->name);
			} else {
				/* last resort */
				if (ports > 1)
					sprintf(port->name, "MIDI %d-%d-%d", card->number, device, p);
				else
					sprintf(port->name, "MIDI %d-%d", card->number, device);
			}
		}
		if ((info->flags & SNDRV_RAWMIDI_INFO_OUTPUT) && p < output_count)
			port->capability |= SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SYNC_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_WRITE;
		if ((info->flags & SNDRV_RAWMIDI_INFO_INPUT) && p < input_count)
			port->capability |= SNDRV_SEQ_PORT_CAP_READ | SNDRV_SEQ_PORT_CAP_SYNC_READ | SNDRV_SEQ_PORT_CAP_SUBS_READ;
		if ((port->capability & (SNDRV_SEQ_PORT_CAP_WRITE|SNDRV_SEQ_PORT_CAP_READ)) == (SNDRV_SEQ_PORT_CAP_WRITE|SNDRV_SEQ_PORT_CAP_READ) &&
		    info->flags & SNDRV_RAWMIDI_INFO_DUPLEX)
			port->capability |= SNDRV_SEQ_PORT_CAP_DUPLEX;
		port->type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC;
		port->midi_channels = 16;
		memset(&pcallbacks, 0, sizeof(pcallbacks));
		pcallbacks.owner = THIS_MODULE;
		pcallbacks.private_data = ms;
		pcallbacks.subscribe = midisynth_subscribe;
		pcallbacks.unsubscribe = midisynth_unsubscribe;
		pcallbacks.use = midisynth_use;
		pcallbacks.unuse = midisynth_unuse;
		pcallbacks.event_input = event_process_midi;
		port->kernel = &pcallbacks;
		if (snd_seq_kernel_client_ctl(client->seq_client, SNDRV_SEQ_IOCTL_CREATE_PORT, port)<0)
			goto __nomem;
		ms->seq_client = client->seq_client;
		ms->seq_port = port->addr.port;
	}
	client->ports_per_device[device] = ports;
	client->ports[device] = msynth;
	client->num_ports++;
	if (newclient)
		synths[card->number] = client;
	up(&register_mutex);
	kfree(info);
	kfree(port);
	return 0;	/* success */

      __nomem:
	if (msynth != NULL) {
	      	for (p = 0; p < ports; p++)
	      		snd_seq_midisynth_delete(&msynth[p]);
		kfree(msynth);
	}
	if (newclient) {
		snd_seq_delete_kernel_client(client->seq_client);
		kfree(client);
	}
	kfree(info);
	kfree(port);
	up(&register_mutex);
	return -ENOMEM;
}

/* release midi synth port */
static int
snd_seq_midisynth_unregister_port(snd_seq_device_t *dev)
{
	seq_midisynth_client_t *client;
	seq_midisynth_t *msynth;
	snd_card_t *card = dev->card;
	int device = dev->device, p, ports;
	
	down(&register_mutex);
	client = synths[card->number];
	if (client == NULL || client->ports[device] == NULL) {
		up(&register_mutex);
		return -ENODEV;
	}
	ports = client->ports_per_device[device];
	client->ports_per_device[device] = 0;
	msynth = client->ports[device];
	client->ports[device] = NULL;
	snd_runtime_check(msynth != NULL || ports <= 0, goto __skip);
	for (p = 0; p < ports; p++)
		snd_seq_midisynth_delete(&msynth[p]);
	kfree(msynth);
      __skip:
	client->num_ports--;
	if (client->num_ports <= 0) {
		snd_seq_delete_kernel_client(client->seq_client);
		synths[card->number] = NULL;
		kfree(client);
	}
	up(&register_mutex);
	return 0;
}


static int __init alsa_seq_midi_init(void)
{
	static snd_seq_dev_ops_t ops = {
		snd_seq_midisynth_register_port,
		snd_seq_midisynth_unregister_port,
	};
	memset(&synths, 0, sizeof(synths));
	snd_seq_autoload_lock();
	snd_seq_device_register_driver(SNDRV_SEQ_DEV_ID_MIDISYNTH, &ops, 0);
	snd_seq_autoload_unlock();
	return 0;
}

static void __exit alsa_seq_midi_exit(void)
{
	snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_MIDISYNTH);
}

module_init(alsa_seq_midi_init)
module_exit(alsa_seq_midi_exit)
