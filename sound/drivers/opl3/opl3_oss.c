/*
 *  Interface for OSS sequencer emulation
 *
 *  Copyright (C) 2000 Uros Bizjak <uros@kss-loka.si>
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
 */

#include "opl3_voice.h"
#include <linux/slab.h>

static int snd_opl3_open_seq_oss(struct snd_seq_oss_arg *arg, void *closure);
static int snd_opl3_close_seq_oss(struct snd_seq_oss_arg *arg);
static int snd_opl3_ioctl_seq_oss(struct snd_seq_oss_arg *arg, unsigned int cmd, unsigned long ioarg);
static int snd_opl3_load_patch_seq_oss(struct snd_seq_oss_arg *arg, int format, const char __user *buf, int offs, int count);
static int snd_opl3_reset_seq_oss(struct snd_seq_oss_arg *arg);

/* */

static inline mm_segment_t snd_enter_user(void)
{
	mm_segment_t fs = get_fs();
	set_fs(get_ds());
	return fs;
}

static inline void snd_leave_user(mm_segment_t fs)
{
	set_fs(fs);
}

/* operators */

extern struct snd_midi_op opl3_ops;

static struct snd_seq_oss_callback oss_callback = {
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

	snd_assert(arg != NULL, return -ENXIO);

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

	snd_assert(arg != NULL, return -ENXIO);
	opl3 = arg->private_data;

	snd_opl3_synth_cleanup(opl3);

	snd_opl3_synth_use_dec(opl3);
	return 0;
}

/* load patch */

/* offsets for SBI params */
#define AM_VIB		0
#define KSL_LEVEL	2
#define ATTACK_DECAY	4
#define SUSTAIN_RELEASE	6
#define WAVE_SELECT	8

/* offset for SBI instrument */
#define CONNECTION	10
#define OFFSET_4OP	11

/* from sound_config.h */
#define SBFM_MAXINSTR	256

static int snd_opl3_load_patch_seq_oss(struct snd_seq_oss_arg *arg, int format,
				       const char __user *buf, int offs, int count)
{
	struct snd_opl3 *opl3;
	int err = -EINVAL;

	snd_assert(arg != NULL, return -ENXIO);
	opl3 = arg->private_data;

	if ((format == FM_PATCH) || (format == OPL3_PATCH)) {
		struct sbi_instrument sbi;

		size_t size;
		struct snd_seq_instr_header *put;
		struct snd_seq_instr_data *data;
		struct fm_xinstrument *xinstr;

		struct snd_seq_event ev;
		int i;

		mm_segment_t fs;

		if (count < (int)sizeof(sbi)) {
			snd_printk("FM Error: Patch record too short\n");
			return -EINVAL;
		}
		if (copy_from_user(&sbi, buf, sizeof(sbi)))
			return -EFAULT;

		if (sbi.channel < 0 || sbi.channel >= SBFM_MAXINSTR) {
			snd_printk("FM Error: Invalid instrument number %d\n", sbi.channel);
			return -EINVAL;
		}

		size = sizeof(*put) + sizeof(struct fm_xinstrument);
		put = kzalloc(size, GFP_KERNEL);
		if (put == NULL)
			return -ENOMEM;
		/* build header */
		data = &put->data;
		data->type = SNDRV_SEQ_INSTR_ATYPE_DATA;
		strcpy(data->data.format, SNDRV_SEQ_INSTR_ID_OPL2_3);
		/* build data section */
		xinstr = (struct fm_xinstrument *)(data + 1);
		xinstr->stype = FM_STRU_INSTR;
        
		for (i = 0; i < 2; i++) {
			xinstr->op[i].am_vib = sbi.operators[AM_VIB + i];
			xinstr->op[i].ksl_level = sbi.operators[KSL_LEVEL + i];
			xinstr->op[i].attack_decay = sbi.operators[ATTACK_DECAY + i];
			xinstr->op[i].sustain_release = sbi.operators[SUSTAIN_RELEASE + i];
			xinstr->op[i].wave_select = sbi.operators[WAVE_SELECT + i];
		}
		xinstr->feedback_connection[0] = sbi.operators[CONNECTION];

		if (format == OPL3_PATCH) {
			xinstr->type = FM_PATCH_OPL3;
			for (i = 0; i < 2; i++) {
				xinstr->op[i+2].am_vib = sbi.operators[OFFSET_4OP + AM_VIB + i];
				xinstr->op[i+2].ksl_level = sbi.operators[OFFSET_4OP + KSL_LEVEL + i];
				xinstr->op[i+2].attack_decay = sbi.operators[OFFSET_4OP + ATTACK_DECAY + i];
				xinstr->op[i+2].sustain_release = sbi.operators[OFFSET_4OP + SUSTAIN_RELEASE + i];
				xinstr->op[i+2].wave_select = sbi.operators[OFFSET_4OP + WAVE_SELECT + i];
			}
			xinstr->feedback_connection[1] = sbi.operators[OFFSET_4OP + CONNECTION];
		} else {
			xinstr->type = FM_PATCH_OPL2;
		}

		put->id.instr.std = SNDRV_SEQ_INSTR_TYPE2_OPL2_3;
		put->id.instr.bank = 127;
		put->id.instr.prg = sbi.channel;
		put->cmd = SNDRV_SEQ_INSTR_PUT_CMD_CREATE;

		memset (&ev, 0, sizeof(ev));
		ev.source.client = SNDRV_SEQ_CLIENT_OSS;
		ev.dest = arg->addr; 

		ev.flags = SNDRV_SEQ_EVENT_LENGTH_VARUSR;
		ev.queue = SNDRV_SEQ_QUEUE_DIRECT;

		fs = snd_enter_user();
	__again:
		ev.type = SNDRV_SEQ_EVENT_INSTR_PUT;
		ev.data.ext.len = size;
		ev.data.ext.ptr = put;

		err = snd_seq_instr_event(&opl3->fm_ops, opl3->ilist, &ev,
				    opl3->seq_client, 0, 0);
		if (err == -EBUSY) {
			struct snd_seq_instr_header remove;

			memset (&remove, 0, sizeof(remove));
			remove.cmd = SNDRV_SEQ_INSTR_FREE_CMD_SINGLE;
			remove.id.instr = put->id.instr;

			/* remove instrument */
			ev.type = SNDRV_SEQ_EVENT_INSTR_FREE;
			ev.data.ext.len = sizeof(remove);
			ev.data.ext.ptr = &remove;

			snd_seq_instr_event(&opl3->fm_ops, opl3->ilist, &ev,
					    opl3->seq_client, 0, 0);
			goto __again;
		}
		snd_leave_user(fs);

		kfree(put);
	}
	return err;
}

/* ioctl */
static int snd_opl3_ioctl_seq_oss(struct snd_seq_oss_arg *arg, unsigned int cmd,
				  unsigned long ioarg)
{
	struct snd_opl3 *opl3;

	snd_assert(arg != NULL, return -ENXIO);
	opl3 = arg->private_data;
	switch (cmd) {
		case SNDCTL_FM_LOAD_INSTR:
			snd_printk("OPL3: Obsolete ioctl(SNDCTL_FM_LOAD_INSTR) used. Fix the program.\n");
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
	struct snd_opl3 *opl3;

	snd_assert(arg != NULL, return -ENXIO);
	opl3 = arg->private_data;

	return 0;
}
