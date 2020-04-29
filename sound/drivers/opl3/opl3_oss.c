// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Interface for OSS sequencer emulation
 *
 *  Copyright (C) 2000 Uros Bizjak <uros@kss-loka.si>
 */

#include <linux/export.h>
#include "opl3_voice.h"

static int snd_opl3_open_seq_oss(struct snd_seq_oss_arg *arg, void *closure);
static int snd_opl3_close_seq_oss(struct snd_seq_oss_arg *arg);
static int snd_opl3_ioctl_seq_oss(struct snd_seq_oss_arg *arg, unsigned int cmd, unsigned long ioarg);
static int snd_opl3_load_patch_seq_oss(struct snd_seq_oss_arg *arg, int format, const char __user *buf, int offs, int count);
static int snd_opl3_reset_seq_oss(struct snd_seq_oss_arg *arg);

/* operators */

static const struct snd_seq_oss_callback oss_callback = {
	.owner = 	THIS_MODULE,
	.open =		snd_opl3_open_seq_oss,
	.close =	snd_opl3_close_seq_oss,
	.ioctl =	snd_opl3_ioctl_seq_oss,
	.load_patch =	snd_opl3_load_patch_seq_oss,
	.reset =	snd_opl3_reset_seq_oss,
};

static int snd_opl3_oss_event_input(struct snd_seq_event *ev, int direct,
				    void *private_data, int atomic, int hop)
{
	struct snd_opl3 *opl3 = private_data;

	if (ev->type != SNDRV_SEQ_EVENT_OSS)
		snd_midi_process_event(&opl3_ops, ev, opl3->oss_chset);
	return 0;
}

/* ------------------------------ */

static void snd_opl3_oss_free_port(void *private_data)
{
	struct snd_opl3 *opl3 = private_data;

	snd_midi_channel_free_set(opl3->oss_chset);
}

static int snd_opl3_oss_create_port(struct snd_opl3 * opl3)
{
	struct snd_seq_port_callback callbacks;
	char name[32];
	int voices, opl_ver;

	voices = (opl3->hardware < OPL3_HW_OPL3) ?
		MAX_OPL2_VOICES : MAX_OPL3_VOICES;
	opl3->oss_chset = snd_midi_channel_alloc_set(voices);
	if (opl3->oss_chset == NULL)
		return -ENOMEM;
	opl3->oss_chset->private_data = opl3;

	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.owner = THIS_MODULE;
	callbacks.event_input = snd_opl3_oss_event_input;
	callbacks.private_free = snd_opl3_oss_free_port;
	callbacks.private_data = opl3;

	opl_ver = (opl3->hardware & OPL3_HW_MASK) >> 8;
	sprintf(name, "OPL%i OSS Port", opl_ver);

	opl3->oss_chset->client = opl3->seq_client;
	opl3->oss_chset->port = snd_seq_event_port_attach(opl3->seq_client, &callbacks,
							  SNDRV_SEQ_PORT_CAP_WRITE,
							  SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC |
							  SNDRV_SEQ_PORT_TYPE_MIDI_GM |
							  SNDRV_SEQ_PORT_TYPE_HARDWARE |
							  SNDRV_SEQ_PORT_TYPE_SYNTHESIZER,
							  voices, voices,
							  name);
	if (opl3->oss_chset->port < 0) {
		int port;
		port = opl3->oss_chset->port;
		snd_midi_channel_free_set(opl3->oss_chset);
		return port;
	}
	return 0;
}

/* ------------------------------ */

/* register OSS synth */
void snd_opl3_init_seq_oss(struct snd_opl3 *opl3, char *name)
{
	struct snd_seq_oss_reg *arg;
	struct snd_seq_device *dev;

	if (snd_seq_device_new(opl3->card, 0, SNDRV_SEQ_DEV_ID_OSS,
			       sizeof(struct snd_seq_oss_reg), &dev) < 0)
		return;

	opl3->oss_seq_dev = dev;
	strlcpy(dev->name, name, sizeof(dev->name));
	arg = SNDRV_SEQ_DEVICE_ARGPTR(dev);
	arg->type = SYNTH_TYPE_FM;
	if (opl3->hardware < OPL3_HW_OPL3) {
		arg->subtype = FM_TYPE_ADLIB;
		arg->nvoices = MAX_OPL2_VOICES;
	} else {
		arg->subtype = FM_TYPE_OPL3;
		arg->nvoices = MAX_OPL3_VOICES;
	}
	arg->oper = oss_callback;
	arg->private_data = opl3;

	if (snd_opl3_oss_create_port(opl3)) {
		/* register to OSS synth table */
		snd_device_register(opl3->card, dev);
	}
}

/* unregister */
void snd_opl3_free_seq_oss(struct snd_opl3 *opl3)
{
	if (opl3->oss_seq_dev) {
		/* The instance should have been released in prior */
		opl3->oss_seq_dev = NULL;
	}
}

/* ------------------------------ */

/* open OSS sequencer */
static int snd_opl3_open_seq_oss(struct snd_seq_oss_arg *arg, void *closure)
{
	struct snd_opl3 *opl3 = closure;
	int err;

	if (snd_BUG_ON(!arg))
		return -ENXIO;

	if ((err = snd_opl3_synth_setup(opl3)) < 0)
		return err;

	/* fill the argument data */
	arg->private_data = opl3;
	arg->addr.client = opl3->oss_chset->client;
	arg->addr.port = opl3->oss_chset->port;

	if ((err = snd_opl3_synth_use_inc(opl3)) < 0)
		return err;

	opl3->synth_mode = SNDRV_OPL3_MODE_SYNTH;
	return 0;
}

/* close OSS sequencer */
static int snd_opl3_close_seq_oss(struct snd_seq_oss_arg *arg)
{
	struct snd_opl3 *opl3;

	if (snd_BUG_ON(!arg))
		return -ENXIO;
	opl3 = arg->private_data;

	snd_opl3_synth_cleanup(opl3);

	snd_opl3_synth_use_dec(opl3);
	return 0;
}

/* load patch */

/* from sound_config.h */
#define SBFM_MAXINSTR	256

static int snd_opl3_load_patch_seq_oss(struct snd_seq_oss_arg *arg, int format,
				       const char __user *buf, int offs, int count)
{
	struct snd_opl3 *opl3;
	struct sbi_instrument sbi;
	char name[32];
	int err, type;

	if (snd_BUG_ON(!arg))
		return -ENXIO;
	opl3 = arg->private_data;

	if (format == FM_PATCH)
		type = FM_PATCH_OPL2;
	else if (format == OPL3_PATCH)
		type = FM_PATCH_OPL3;
	else
		return -EINVAL;

	if (count < (int)sizeof(sbi)) {
		snd_printk(KERN_ERR "FM Error: Patch record too short\n");
		return -EINVAL;
	}
	if (copy_from_user(&sbi, buf, sizeof(sbi)))
		return -EFAULT;

	if (sbi.channel < 0 || sbi.channel >= SBFM_MAXINSTR) {
		snd_printk(KERN_ERR "FM Error: Invalid instrument number %d\n",
			   sbi.channel);
		return -EINVAL;
	}

	memset(name, 0, sizeof(name));
	sprintf(name, "Chan%d", sbi.channel);

	err = snd_opl3_load_patch(opl3, sbi.channel, 127, type, name, NULL,
				  sbi.operators);
	if (err < 0)
		return err;

	return sizeof(sbi);
}

/* ioctl */
static int snd_opl3_ioctl_seq_oss(struct snd_seq_oss_arg *arg, unsigned int cmd,
				  unsigned long ioarg)
{
	if (snd_BUG_ON(!arg))
		return -ENXIO;
	switch (cmd) {
		case SNDCTL_FM_LOAD_INSTR:
			snd_printk(KERN_ERR "OPL3: "
				   "Obsolete ioctl(SNDCTL_FM_LOAD_INSTR) used. "
				   "Fix the program.\n");
			return -EINVAL;

		case SNDCTL_SYNTH_MEMAVL:
			return 0x7fffffff;

		case SNDCTL_FM_4OP_ENABLE:
			// handled automatically by OPL instrument type
			return 0;

		default:
			return -EINVAL;
	}
	return 0;
}

/* reset device */
static int snd_opl3_reset_seq_oss(struct snd_seq_oss_arg *arg)
{
	if (snd_BUG_ON(!arg))
		return -ENXIO;

	return 0;
}
