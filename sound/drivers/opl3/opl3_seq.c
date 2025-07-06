// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Uros Bizjak <uros@kss-loka.si>
 *
 *  Midi Sequencer interface routines for OPL2/OPL3/OPL4 FM
 *
 *  OPL2/3 FM instrument loader:
 *   alsa-tools/seq/sbiload/
 */

#include "opl3_voice.h"
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/module.h>
#include <sound/initval.h>

MODULE_AUTHOR("Uros Bizjak <uros@kss-loka.si>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ALSA driver for OPL3 FM synth");

bool use_internal_drums = 0;
module_param(use_internal_drums, bool, 0444);
MODULE_PARM_DESC(use_internal_drums, "Enable internal OPL2/3 drums.");

int snd_opl3_synth_use_inc(struct snd_opl3 * opl3)
{
	if (!try_module_get(opl3->card->module))
		return -EFAULT;
	return 0;

}

void snd_opl3_synth_use_dec(struct snd_opl3 * opl3)
{
	module_put(opl3->card->module);
}

int snd_opl3_synth_setup(struct snd_opl3 * opl3)
{
	int idx;
	struct snd_hwdep *hwdep = opl3->hwdep;

	mutex_lock(&hwdep->open_mutex);
	if (hwdep->used) {
		mutex_unlock(&hwdep->open_mutex);
		return -EBUSY;
	}
	hwdep->used++;
	mutex_unlock(&hwdep->open_mutex);

	snd_opl3_reset(opl3);

	for (idx = 0; idx < MAX_OPL3_VOICES; idx++) {
		opl3->voices[idx].state = SNDRV_OPL3_ST_OFF;
		opl3->voices[idx].time = 0;
		opl3->voices[idx].keyon_reg = 0x00;
	}
	opl3->use_time = 0;
	opl3->connection_reg = 0x00;
	if (opl3->hardware >= OPL3_HW_OPL3) {
		/* Clear 4-op connections */
		opl3->command(opl3, OPL3_RIGHT | OPL3_REG_CONNECTION_SELECT,
				 opl3->connection_reg);
		opl3->max_voices = MAX_OPL3_VOICES;
	}
	return 0;
}

void snd_opl3_synth_cleanup(struct snd_opl3 * opl3)
{
	unsigned long flags;
	struct snd_hwdep *hwdep;

	/* Stop system timer */
	spin_lock_irqsave(&opl3->sys_timer_lock, flags);
	if (opl3->sys_timer_status) {
		timer_delete(&opl3->tlist);
		opl3->sys_timer_status = 0;
	}
	spin_unlock_irqrestore(&opl3->sys_timer_lock, flags);

	snd_opl3_reset(opl3);
	hwdep = opl3->hwdep;
	mutex_lock(&hwdep->open_mutex);
	hwdep->used--;
	mutex_unlock(&hwdep->open_mutex);
	wake_up(&hwdep->open_wait);
}

static int snd_opl3_synth_use(void *private_data, struct snd_seq_port_subscribe * info)
{
	struct snd_opl3 *opl3 = private_data;
	int err;

	err = snd_opl3_synth_setup(opl3);
	if (err < 0)
		return err;

	if (use_internal_drums) {
		/* Percussion mode */
		opl3->voices[6].state = opl3->voices[7].state = 
			opl3->voices[8].state = SNDRV_OPL3_ST_NOT_AVAIL;
		snd_opl3_load_drums(opl3);
		opl3->drum_reg = OPL3_PERCUSSION_ENABLE;
		opl3->command(opl3, OPL3_LEFT | OPL3_REG_PERCUSSION, opl3->drum_reg);
	} else {
		opl3->drum_reg = 0x00;
	}

	if (info->sender.client != SNDRV_SEQ_CLIENT_SYSTEM) {
		err = snd_opl3_synth_use_inc(opl3);
		if (err < 0)
			return err;
	}
	opl3->synth_mode = SNDRV_OPL3_MODE_SEQ;
	return 0;
}

static int snd_opl3_synth_unuse(void *private_data, struct snd_seq_port_subscribe * info)
{
	struct snd_opl3 *opl3 = private_data;

	snd_opl3_synth_cleanup(opl3);

	if (info->sender.client != SNDRV_SEQ_CLIENT_SYSTEM)
		snd_opl3_synth_use_dec(opl3);
	return 0;
}

/*
 * MIDI emulation operators
 */
const struct snd_midi_op opl3_ops = {
	.note_on =		snd_opl3_note_on,
	.note_off =		snd_opl3_note_off,
	.key_press =		snd_opl3_key_press,
	.note_terminate =	snd_opl3_terminate_note,
	.control =		snd_opl3_control,
	.nrpn =			snd_opl3_nrpn,
	.sysex =		snd_opl3_sysex,
};

static int snd_opl3_synth_event_input(struct snd_seq_event * ev, int direct,
				      void *private_data, int atomic, int hop)
{
	struct snd_opl3 *opl3 = private_data;

	snd_midi_process_event(&opl3_ops, ev, opl3->chset);
	return 0;
}

/* ------------------------------ */

static void snd_opl3_synth_free_port(void *private_data)
{
	struct snd_opl3 *opl3 = private_data;

	snd_midi_channel_free_set(opl3->chset);
}

static int snd_opl3_synth_create_port(struct snd_opl3 * opl3)
{
	struct snd_seq_port_callback callbacks;
	char name[32];
	int voices, opl_ver;

	voices = (opl3->hardware < OPL3_HW_OPL3) ?
		MAX_OPL2_VOICES : MAX_OPL3_VOICES;
	opl3->chset = snd_midi_channel_alloc_set(16);
	if (opl3->chset == NULL)
		return -ENOMEM;
	opl3->chset->private_data = opl3;

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.owner = THIS_MODULE;
	callbacks.use = snd_opl3_synth_use;
	callbacks.unuse = snd_opl3_synth_unuse;
	callbacks.event_input = snd_opl3_synth_event_input;
	callbacks.private_free = snd_opl3_synth_free_port;
	callbacks.private_data = opl3;

	opl_ver = (opl3->hardware & OPL3_HW_MASK) >> 8;
	sprintf(name, "OPL%i FM Port", opl_ver);

	opl3->chset->client = opl3->seq_client;
	opl3->chset->port = snd_seq_event_port_attach(opl3->seq_client, &callbacks,
						      SNDRV_SEQ_PORT_CAP_WRITE |
						      SNDRV_SEQ_PORT_CAP_SUBS_WRITE,
						      SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC |
						      SNDRV_SEQ_PORT_TYPE_MIDI_GM |
						      SNDRV_SEQ_PORT_TYPE_DIRECT_SAMPLE |
						      SNDRV_SEQ_PORT_TYPE_HARDWARE |
						      SNDRV_SEQ_PORT_TYPE_SYNTHESIZER,
						      16, voices,
						      name);
	if (opl3->chset->port < 0) {
		int port;
		port = opl3->chset->port;
		snd_midi_channel_free_set(opl3->chset);
		return port;
	}
	return 0;
}

/* ------------------------------ */

static int snd_opl3_seq_probe(struct device *_dev)
{
	struct snd_seq_device *dev = to_seq_dev(_dev);
	struct snd_opl3 *opl3;
	int client, err;
	char name[32];
	int opl_ver;

	opl3 = *(struct snd_opl3 **)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (opl3 == NULL)
		return -EINVAL;

	spin_lock_init(&opl3->voice_lock);

	opl3->seq_client = -1;

	/* allocate new client */
	opl_ver = (opl3->hardware & OPL3_HW_MASK) >> 8;
	sprintf(name, "OPL%i FM synth", opl_ver);
	client = opl3->seq_client =
		snd_seq_create_kernel_client(opl3->card, opl3->seq_dev_num,
					     name);
	if (client < 0)
		return client;

	err = snd_opl3_synth_create_port(opl3);
	if (err < 0) {
		snd_seq_delete_kernel_client(client);
		opl3->seq_client = -1;
		return err;
	}

	/* setup system timer */
	timer_setup(&opl3->tlist, snd_opl3_timer_func, 0);
	spin_lock_init(&opl3->sys_timer_lock);
	opl3->sys_timer_status = 0;

#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
	snd_opl3_init_seq_oss(opl3, name);
#endif
	return 0;
}

static int snd_opl3_seq_remove(struct device *_dev)
{
	struct snd_seq_device *dev = to_seq_dev(_dev);
	struct snd_opl3 *opl3;

	opl3 = *(struct snd_opl3 **)SNDRV_SEQ_DEVICE_ARGPTR(dev);
	if (opl3 == NULL)
		return -EINVAL;

#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
	snd_opl3_free_seq_oss(opl3);
#endif
	if (opl3->seq_client >= 0) {
		snd_seq_delete_kernel_client(opl3->seq_client);
		opl3->seq_client = -1;
	}
	return 0;
}

static struct snd_seq_driver opl3_seq_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.probe = snd_opl3_seq_probe,
		.remove = snd_opl3_seq_remove,
	},
	.id = SNDRV_SEQ_DEV_ID_OPL3,
	.argsize = sizeof(struct snd_opl3 *),
};

module_snd_seq_driver(opl3_seq_driver);
