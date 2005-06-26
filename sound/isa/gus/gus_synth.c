/*
 *  Routines for Gravis UltraSound soundcards - Synthesizer
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>
#include <sound/seq_device.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>");
MODULE_DESCRIPTION("Routines for Gravis UltraSound soundcards - Synthesizer");
MODULE_LICENSE("GPL");

/*
 *
 */

static void snd_gus_synth_free_voices(snd_gus_card_t * gus, int client, int port)
{
	int idx;
	snd_gus_voice_t * voice;
	
	for (idx = 0; idx < 32; idx++) {
		voice = &gus->gf1.voices[idx];
		if (voice->use && voice->client == client && voice->port == port)
			snd_gf1_free_voice(gus, voice);
	}
}

static int snd_gus_synth_use(void *private_data, snd_seq_port_subscribe_t *info)
{
	snd_gus_port_t * port = (snd_gus_port_t *)private_data;
	snd_gus_card_t * gus = port->gus;
	snd_gus_voice_t * voice;
	unsigned int idx;

	if (info->voices > 32)
		return -EINVAL;
	down(&gus->register_mutex);
	if (!snd_gus_use_inc(gus)) {
		up(&gus->register_mutex);
		return -EFAULT;
	}
	for (idx = 0; idx < info->voices; idx++) {
		voice = snd_gf1_alloc_voice(gus, SNDRV_GF1_VOICE_TYPE_SYNTH, info->sender.client, info->sender.port);
		if (voice == NULL) {
			snd_gus_synth_free_voices(gus, info->sender.client, info->sender.port);
			snd_gus_use_dec(gus);
			up(&gus->register_mutex);
			return -EBUSY;
		}
		voice->index = idx;
	}
	up(&gus->register_mutex);
	return 0;
}

static int snd_gus_synth_unuse(void *private_data, snd_seq_port_subscribe_t *info)
{
	snd_gus_port_t * port = (snd_gus_port_t *)private_data;
	snd_gus_card_t * gus = port->gus;

	down(&gus->register_mutex);
	snd_gus_synth_free_voices(gus, info->sender.client, info->sender.port);
	snd_gus_use_dec(gus);
	up(&gus->register_mutex);
	return 0;
}

/*
 *
 */

static void snd_gus_synth_free_private_instruments(snd_gus_port_t *p, int client)
{
	snd_seq_instr_header_t ifree;

	memset(&ifree, 0, sizeof(ifree));
	ifree.cmd = SNDRV_SEQ_INSTR_FREE_CMD_PRIVATE;
	snd_seq_instr_list_free_cond(p->gus->gf1.ilist, &ifree, client, 0);
}
 
static int snd_gus_synth_event_input(snd_seq_event_t *ev, int direct,
				     void *private_data, int atomic, int hop)
{
	snd_gus_port_t * p = (snd_gus_port_t *) private_data;
	
	snd_assert(p != NULL, return -EINVAL);
	if (ev->type >= SNDRV_SEQ_EVENT_SAMPLE &&
	    ev->type <= SNDRV_SEQ_EVENT_SAMPLE_PRIVATE1) {
		snd_gus_sample_event(ev, p);
		return 0;
	}
	if (ev->source.client == SNDRV_SEQ_CLIENT_SYSTEM &&
	    ev->source.port == SNDRV_SEQ_PORT_SYSTEM_ANNOUNCE) {
		if (ev->type == SNDRV_SEQ_EVENT_CLIENT_EXIT) {
			snd_gus_synth_free_private_instruments(p, ev->data.addr.client);
			return 0;
		}
	}
	if (direct) {
		if (ev->type >= SNDRV_SEQ_EVENT_INSTR_BEGIN) {
			snd_seq_instr_event(&p->gus->gf1.iwffff_ops.kops,
					    p->gus->gf1.ilist,
					    ev,
					    p->gus->gf1.seq_client,
					    atomic, hop);
			return 0;
		}
	}
	return 0;
}

static void snd_gus_synth_instr_notify(void *private_data,
				       snd_seq_kinstr_t *instr,
				       int what)
{
	unsigned int idx;
	snd_gus_card_t *gus = private_data;
	snd_gus_voice_t *pvoice;
	unsigned long flags;
	
	spin_lock_irqsave(&gus->event_lock, flags);
	for (idx = 0; idx < 32; idx++) {
		pvoice = &gus->gf1.voices[idx];
		if (pvoice->use && !memcmp(&pvoice->instr, &instr->instr, sizeof(pvoice->instr))) {
			if (pvoice->sample_ops && pvoice->sample_ops->sample_stop) {
				pvoice->sample_ops->sample_stop(gus, pvoice, SAMPLE_STOP_IMMEDIATELY);
			} else {
				snd_gf1_stop_voice(gus, pvoice->number);
				pvoice->flags &= ~SNDRV_GF1_VFLG_RUNNING;
			}
		}
	}
	spin_unlock_irqrestore(&gus->event_lock, flags);
}

/*
 *
 */

static void snd_gus_synth_free_port(void *private_data)
{
	snd_gus_port_t * p = (snd_gus_port_t *)private_data;
	
	if (p)
		snd_midi_channel_free_set(p->chset);
}

static int snd_gus_synth_create_port(snd_gus_card_t * gus, int idx)
{
	snd_gus_port_t * p;
	snd_seq_port_callback_t callbacks;
	char name[32];
	int result;
	
	p = &gus->gf1.seq_ports[idx];
	p->chset = snd_midi_channel_alloc_set(16);
	if (p->chset == NULL)
		return -ENOMEM;
	p->chset->private_data = p;
	p->gus = gus;
	p->client = gus->gf1.seq_client;

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.owner = THIS_MODULE;
	callbacks.use = snd_gus_synth_use;
	callbacks.unuse = snd_gus_synth_unuse;
	callbacks.event_input = snd_gus_synth_event_input;
	callbacks.private_free = snd_gus_synth_free_port;
	callbacks.private_data = p;
	
	sprintf(name, "%s port %i", gus->interwave ? "AMD InterWave" : "GF1", idx);
	p->chset->port = snd_seq_event_port_attach(gus->gf1.seq_client,
						   &callbacks,
						   SNDRV_SEQ_PORT_CAP_WRITE | SNDRV_SEQ_PORT_CAP_SUBS_WRITE,
						   SNDRV_SEQ_PORT_TYPE_DIRECT_SAMPLE |
						   SNDRV_SEQ_PORT_TYPE_SYNTH,
						   16, 0,
						   name);
	if (p->chset->port < 0) {
		result = p->chset->port;
		snd_gus_synth_free_port(p);
		return result;
	}
	p->port = p->chset->port;
	return 0;
}						 

/*
 *
 */

static int snd_gus_synth_new_device(snd_seq_device_t *dev)
{
	snd_gus_card_t *gus;
	int client, i;
	snd_seq_client_callback_t callbacks;
	snd_seq_client_info_t *cinfo;
	snd_seq_port_subscribe_t sub;
	snd_iwffff_ops_t *iwops;
	snd_gf1_ops_t *gf1ops;
	snd_simple_ops_t *simpleops;

	gus = *(snd_gus_card_t**)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (gus == NULL)
		return -EINVAL;

	init_MUTEX(&gus->register_mutex);
	gus->gf1.seq_client = -1;
	
	cinfo = kmalloc(sizeof(*cinfo), GFP_KERNEL);
	if (! cinfo)
		return -ENOMEM;

	/* allocate new client */
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.private_data = gus;
	callbacks.allow_output = callbacks.allow_input = 1;
	client = gus->gf1.seq_client =
			snd_seq_create_kernel_client(gus->card, 1, &callbacks);
	if (client < 0) {
		kfree(cinfo);
		return client;
	}

	/* change name of client */
	memset(cinfo, 0, sizeof(*cinfo));
	cinfo->client = client;
	cinfo->type = KERNEL_CLIENT;
	sprintf(cinfo->name, gus->interwave ? "AMD InterWave" : "GF1");
	snd_seq_kernel_client_ctl(client, SNDRV_SEQ_IOCTL_SET_CLIENT_INFO, cinfo);
	kfree(cinfo);

	for (i = 0; i < 4; i++)
		snd_gus_synth_create_port(gus, i);
		
	gus->gf1.ilist = snd_seq_instr_list_new();
	if (gus->gf1.ilist == NULL) {
		snd_seq_delete_kernel_client(client);	
		gus->gf1.seq_client = -1;
		return -ENOMEM;
	}
	gus->gf1.ilist->flags = SNDRV_SEQ_INSTR_FLG_DIRECT;

	simpleops = &gus->gf1.simple_ops;
	snd_seq_simple_init(simpleops, gus, NULL);
	simpleops->put_sample = snd_gus_simple_put_sample;
	simpleops->get_sample = snd_gus_simple_get_sample;
	simpleops->remove_sample = snd_gus_simple_remove_sample;
	simpleops->notify = snd_gus_synth_instr_notify;

	gf1ops = &gus->gf1.gf1_ops;
	snd_seq_gf1_init(gf1ops, gus, &simpleops->kops);
	gf1ops->put_sample = snd_gus_gf1_put_sample;
	gf1ops->get_sample = snd_gus_gf1_get_sample;
	gf1ops->remove_sample = snd_gus_gf1_remove_sample;
	gf1ops->notify = snd_gus_synth_instr_notify;

	iwops = &gus->gf1.iwffff_ops;
	snd_seq_iwffff_init(iwops, gus, &gf1ops->kops);
	iwops->put_sample = snd_gus_iwffff_put_sample;
	iwops->get_sample = snd_gus_iwffff_get_sample;
	iwops->remove_sample = snd_gus_iwffff_remove_sample;
	iwops->notify = snd_gus_synth_instr_notify;

	memset(&sub, 0, sizeof(sub));
	sub.sender.client = SNDRV_SEQ_CLIENT_SYSTEM;
	sub.sender.port = SNDRV_SEQ_PORT_SYSTEM_ANNOUNCE;
	sub.dest.client = client;
	sub.dest.port = 0;
	snd_seq_kernel_client_ctl(client, SNDRV_SEQ_IOCTL_SUBSCRIBE_PORT, &sub);

	return 0;
}

static int snd_gus_synth_delete_device(snd_seq_device_t *dev)
{
	snd_gus_card_t *gus;

	gus = *(snd_gus_card_t**)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (gus == NULL)
		return -EINVAL;

	if (gus->gf1.seq_client >= 0) {
		snd_seq_delete_kernel_client(gus->gf1.seq_client);	
		gus->gf1.seq_client = -1;
	}
	if (gus->gf1.ilist)
		snd_seq_instr_list_free(&gus->gf1.ilist);
	return 0;
}

static int __init alsa_gus_synth_init(void)
{
	static snd_seq_dev_ops_t ops = {
		snd_gus_synth_new_device,
		snd_gus_synth_delete_device
	};

	return snd_seq_device_register_driver(SNDRV_SEQ_DEV_ID_GUS, &ops,
					      sizeof(snd_gus_card_t*));
}

static void __exit alsa_gus_synth_exit(void)
{
	snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_GUS);
}

module_init(alsa_gus_synth_init)
module_exit(alsa_gus_synth_exit)
