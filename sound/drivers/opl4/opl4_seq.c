/*
 * OPL4 sequencer functions
 *
 * Copyright (c) 2003 by Clemens Ladisch <clemens@ladisch.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed and/or modified under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opl4_local.h"
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <sound/initval.h>

MODULE_AUTHOR("Clemens Ladisch <clemens@ladisch.de>");
MODULE_DESCRIPTION("OPL4 wavetable synth driver");
MODULE_LICENSE("Dual BSD/GPL");

int volume_boost = 8;

module_param(volume_boost, int, 0644);
MODULE_PARM_DESC(volume_boost, "Additional volume for OPL4 wavetable sounds.");

static int snd_opl4_seq_use_inc(struct snd_opl4 *opl4)
{
	if (!try_module_get(opl4->card->module))
		return -EFAULT;
	return 0;
}

static void snd_opl4_seq_use_dec(struct snd_opl4 *opl4)
{
	module_put(opl4->card->module);
}

static int snd_opl4_seq_use(void *private_data, struct snd_seq_port_subscribe *info)
{
	struct snd_opl4 *opl4 = private_data;
	int err;

	mutex_lock(&opl4->access_mutex);

	if (opl4->used) {
		mutex_unlock(&opl4->access_mutex);
		return -EBUSY;
	}
	opl4->used++;

	if (info->sender.client != SNDRV_SEQ_CLIENT_SYSTEM) {
		err = snd_opl4_seq_use_inc(opl4);
		if (err < 0) {
			mutex_unlock(&opl4->access_mutex);
			return err;
		}
	}

	mutex_unlock(&opl4->access_mutex);

	snd_opl4_synth_reset(opl4);
	return 0;
}

static int snd_opl4_seq_unuse(void *private_data, struct snd_seq_port_subscribe *info)
{
	struct snd_opl4 *opl4 = private_data;

	snd_opl4_synth_shutdown(opl4);

	mutex_lock(&opl4->access_mutex);
	opl4->used--;
	mutex_unlock(&opl4->access_mutex);

	if (info->sender.client != SNDRV_SEQ_CLIENT_SYSTEM)
		snd_opl4_seq_use_dec(opl4);
	return 0;
}

static struct snd_midi_op opl4_ops = {
	.note_on =		snd_opl4_note_on,
	.note_off =		snd_opl4_note_off,
	.note_terminate =	snd_opl4_terminate_note,
	.control =		snd_opl4_control,
	.sysex =		snd_opl4_sysex,
};

static int snd_opl4_seq_event_input(struct snd_seq_event *ev, int direct,
				    void *private_data, int atomic, int hop)
{
	struct snd_opl4 *opl4 = private_data;

	snd_midi_process_event(&opl4_ops, ev, opl4->chset);
	return 0;
}

static void snd_opl4_seq_free_port(void *private_data)
{
	struct snd_opl4 *opl4 = private_data;

	snd_midi_channel_free_set(opl4->chset);
}

static int snd_opl4_seq_new_device(struct snd_seq_device *dev)
{
	struct snd_opl4 *opl4;
	int client;
	struct snd_seq_port_callback pcallbacks;

	opl4 = *(struct snd_opl4 **)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (!opl4)
		return -EINVAL;

	if (snd_yrw801_detect(opl4) < 0)
		return -ENODEV;

	opl4->chset = snd_midi_channel_alloc_set(16);
	if (!opl4->chset)
		return -ENOMEM;
	opl4->chset->private_data = opl4;

	/* allocate new client */
	client = snd_seq_create_kernel_client(opl4->card, opl4->seq_dev_num,
					      "OPL4 Wavetable");
	if (client < 0) {
		snd_midi_channel_free_set(opl4->chset);
		return client;
	}
	opl4->seq_client = client;
	opl4->chset->client = client;

	/* create new port */
	memset(&pcallbacks, 0, sizeof(pcallbacks));
	pcallbacks.owner = THIS_MODULE;
	pcallbacks.use = snd_opl4_seq_use;
	pcallbacks.unuse = snd_opl4_seq_unuse;
	pcallbacks.event_input = snd_opl4_seq_event_input;
	pcallbacks.private_free = snd_opl4_seq_free_port;
	pcallbacks.private_data = opl4;

	opl4->chset->port = snd_seq_event_port_attach(client, &pcallbacks,
						      SNDRV_SEQ_PORT_CAP_WRITE |
						      SNDRV_SEQ_PORT_CAP_SUBS_WRITE,
						      SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC |
						      SNDRV_SEQ_PORT_TYPE_MIDI_GM,
						      16, 24,
						      "OPL4 Wavetable Port");
	if (opl4->chset->port < 0) {
		int err = opl4->chset->port;
		snd_midi_channel_free_set(opl4->chset);
		snd_seq_delete_kernel_client(client);
		opl4->seq_client = -1;
		return err;
	}
	return 0;
}

static int snd_opl4_seq_delete_device(struct snd_seq_device *dev)
{
	struct snd_opl4 *opl4;

	opl4 = *(struct snd_opl4 **)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (!opl4)
		return -EINVAL;

	if (opl4->seq_client >= 0) {
		snd_seq_delete_kernel_client(opl4->seq_client);
		opl4->seq_client = -1;
	}
	return 0;
}

static int __init alsa_opl4_synth_init(void)
{
	static struct snd_seq_dev_ops ops = {
		snd_opl4_seq_new_device,
		snd_opl4_seq_delete_device
	};

	return snd_seq_device_register_driver(SNDRV_SEQ_DEV_ID_OPL4, &ops,
					      sizeof(struct snd_opl4 *));
}

static void __exit alsa_opl4_synth_exit(void)
{
	snd_seq_device_unregister_driver(SNDRV_SEQ_DEV_ID_OPL4);
}

module_init(alsa_opl4_synth_init)
module_exit(alsa_opl4_synth_exit)
