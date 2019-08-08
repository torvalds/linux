// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Interface for OSS sequencer emulation
 *
 *  Copyright (C) 1999 Takashi Iwai <tiwai@suse.de>
 *
 * Changes
 * 19990227   Steve Ratcliffe   Made separate file and merged in latest
 * 				midi emulation.
 */


#include <linux/export.h>
#include <linux/uaccess.h>
#include <sound/core.h>
#include "emux_voice.h"
#include <sound/asoundef.h>

static int snd_emux_open_seq_oss(struct snd_seq_oss_arg *arg, void *closure);
static int snd_emux_close_seq_oss(struct snd_seq_oss_arg *arg);
static int snd_emux_ioctl_seq_oss(struct snd_seq_oss_arg *arg, unsigned int cmd,
				  unsigned long ioarg);
static int snd_emux_load_patch_seq_oss(struct snd_seq_oss_arg *arg, int format,
				       const char __user *buf, int offs, int count);
static int snd_emux_reset_seq_oss(struct snd_seq_oss_arg *arg);
static int snd_emux_event_oss_input(struct snd_seq_event *ev, int direct,
				    void *private, int atomic, int hop);
static void reset_port_mode(struct snd_emux_port *port, int midi_mode);
static void emuspec_control(struct snd_emux *emu, struct snd_emux_port *port,
			    int cmd, unsigned char *event, int atomic, int hop);
static void gusspec_control(struct snd_emux *emu, struct snd_emux_port *port,
			    int cmd, unsigned char *event, int atomic, int hop);
static void fake_event(struct snd_emux *emu, struct snd_emux_port *port,
		       int ch, int param, int val, int atomic, int hop);

/* operators */
static struct snd_seq_oss_callback oss_callback = {
	.owner = THIS_MODULE,
	.open = snd_emux_open_seq_oss,
	.close = snd_emux_close_seq_oss,
	.ioctl = snd_emux_ioctl_seq_oss,
	.load_patch = snd_emux_load_patch_seq_oss,
	.reset = snd_emux_reset_seq_oss,
};


/*
 * register OSS synth
 */

void
snd_emux_init_seq_oss(struct snd_emux *emu)
{
	struct snd_seq_oss_reg *arg;
	struct snd_seq_device *dev;

	/* using device#1 here for avoiding conflicts with OPL3 */
	if (snd_seq_device_new(emu->card, 1, SNDRV_SEQ_DEV_ID_OSS,
			       sizeof(struct snd_seq_oss_reg), &dev) < 0)
		return;

	emu->oss_synth = dev;
	strcpy(dev->name, emu->name);
	arg = SNDRV_SEQ_DEVICE_ARGPTR(dev);
	arg->type = SYNTH_TYPE_SAMPLE;
	arg->subtype = SAMPLE_TYPE_AWE32;
	arg->nvoices = emu->max_voices;
	arg->oper = oss_callback;
	arg->private_data = emu;

	/* register to OSS synth table */
	snd_device_register(emu->card, dev);
}


/*
 * unregister
 */
void
snd_emux_detach_seq_oss(struct snd_emux *emu)
{
	if (emu->oss_synth) {
		snd_device_free(emu->card, emu->oss_synth);
		emu->oss_synth = NULL;
	}
}


/* use port number as a unique soundfont client number */
#define SF_CLIENT_NO(p)	((p) + 0x1000)

/*
 * open port for OSS sequencer
 */
static int
snd_emux_open_seq_oss(struct snd_seq_oss_arg *arg, void *closure)
{
	struct snd_emux *emu;
	struct snd_emux_port *p;
	struct snd_seq_port_callback callback;
	char tmpname[64];

	emu = closure;
	if (snd_BUG_ON(!arg || !emu))
		return -ENXIO;

	if (!snd_emux_inc_count(emu))
		return -EFAULT;

	memset(&callback, 0, sizeof(callback));
	callback.owner = THIS_MODULE;
	callback.event_input = snd_emux_event_oss_input;

	sprintf(tmpname, "%s OSS Port", emu->name);
	p = snd_emux_create_port(emu, tmpname, 32,
				 1, &callback);
	if (p == NULL) {
		snd_printk(KERN_ERR "can't create port\n");
		snd_emux_dec_count(emu);
		return -ENOMEM;
	}

	/* fill the argument data */
	arg->private_data = p;
	arg->addr.client = p->chset.client;
	arg->addr.port = p->chset.port;
	p->oss_arg = arg;

	reset_port_mode(p, arg->seq_mode);

	snd_emux_reset_port(p);
	return 0;
}


#define DEFAULT_DRUM_FLAGS	((1<<9) | (1<<25))

/*
 * reset port mode
 */
static void
reset_port_mode(struct snd_emux_port *port, int midi_mode)
{
	if (midi_mode) {
		port->port_mode = SNDRV_EMUX_PORT_MODE_OSS_MIDI;
		port->drum_flags = DEFAULT_DRUM_FLAGS;
		port->volume_atten = 0;
		port->oss_arg->event_passing = SNDRV_SEQ_OSS_PROCESS_KEYPRESS;
	} else {
		port->port_mode = SNDRV_EMUX_PORT_MODE_OSS_SYNTH;
		port->drum_flags = 0;
		port->volume_atten = 32;
		port->oss_arg->event_passing = SNDRV_SEQ_OSS_PROCESS_EVENTS;
	}
}


/*
 * close port
 */
static int
snd_emux_close_seq_oss(struct snd_seq_oss_arg *arg)
{
	struct snd_emux *emu;
	struct snd_emux_port *p;

	if (snd_BUG_ON(!arg))
		return -ENXIO;
	p = arg->private_data;
	if (snd_BUG_ON(!p))
		return -ENXIO;

	emu = p->emu;
	if (snd_BUG_ON(!emu))
		return -ENXIO;

	snd_emux_sounds_off_all(p);
	snd_soundfont_close_check(emu->sflist, SF_CLIENT_NO(p->chset.port));
	snd_seq_event_port_detach(p->chset.client, p->chset.port);
	snd_emux_dec_count(emu);

	return 0;
}


/*
 * load patch
 */
static int
snd_emux_load_patch_seq_oss(struct snd_seq_oss_arg *arg, int format,
			    const char __user *buf, int offs, int count)
{
	struct snd_emux *emu;
	struct snd_emux_port *p;
	int rc;

	if (snd_BUG_ON(!arg))
		return -ENXIO;
	p = arg->private_data;
	if (snd_BUG_ON(!p))
		return -ENXIO;

	emu = p->emu;
	if (snd_BUG_ON(!emu))
		return -ENXIO;

	if (format == GUS_PATCH)
		rc = snd_soundfont_load_guspatch(emu->sflist, buf, count,
						 SF_CLIENT_NO(p->chset.port));
	else if (format == SNDRV_OSS_SOUNDFONT_PATCH) {
		struct soundfont_patch_info patch;
		if (count < (int)sizeof(patch))
			return -EINVAL;
		if (copy_from_user(&patch, buf, sizeof(patch)))
			return -EFAULT;
		if (patch.type >= SNDRV_SFNT_LOAD_INFO &&
		    patch.type <= SNDRV_SFNT_PROBE_DATA)
			rc = snd_soundfont_load(emu->sflist, buf, count, SF_CLIENT_NO(p->chset.port));
		else {
			if (emu->ops.load_fx)
				rc = emu->ops.load_fx(emu, patch.type, patch.optarg, buf, count);
			else
				rc = -EINVAL;
		}
	} else
		rc = 0;
	return rc;
}


/*
 * ioctl
 */
static int
snd_emux_ioctl_seq_oss(struct snd_seq_oss_arg *arg, unsigned int cmd, unsigned long ioarg)
{
	struct snd_emux_port *p;
	struct snd_emux *emu;

	if (snd_BUG_ON(!arg))
		return -ENXIO;
	p = arg->private_data;
	if (snd_BUG_ON(!p))
		return -ENXIO;

	emu = p->emu;
	if (snd_BUG_ON(!emu))
		return -ENXIO;

	switch (cmd) {
	case SNDCTL_SEQ_RESETSAMPLES:
		snd_soundfont_remove_samples(emu->sflist);
		return 0;
			
	case SNDCTL_SYNTH_MEMAVL:
		if (emu->memhdr)
			return snd_util_mem_avail(emu->memhdr);
		return 0;
	}

	return 0;
}


/*
 * reset device
 */
static int
snd_emux_reset_seq_oss(struct snd_seq_oss_arg *arg)
{
	struct snd_emux_port *p;

	if (snd_BUG_ON(!arg))
		return -ENXIO;
	p = arg->private_data;
	if (snd_BUG_ON(!p))
		return -ENXIO;
	snd_emux_reset_port(p);
	return 0;
}


/*
 * receive raw events: only SEQ_PRIVATE is accepted.
 */
static int
snd_emux_event_oss_input(struct snd_seq_event *ev, int direct, void *private_data,
			 int atomic, int hop)
{
	struct snd_emux *emu;
	struct snd_emux_port *p;
	unsigned char cmd, *data;

	p = private_data;
	if (snd_BUG_ON(!p))
		return -EINVAL;
	emu = p->emu;
	if (snd_BUG_ON(!emu))
		return -EINVAL;
	if (ev->type != SNDRV_SEQ_EVENT_OSS)
		return snd_emux_event_input(ev, direct, private_data, atomic, hop);

	data = ev->data.raw8.d;
	/* only SEQ_PRIVATE is accepted */
	if (data[0] != 0xfe)
		return 0;
	cmd = data[2] & _EMUX_OSS_MODE_VALUE_MASK;
	if (data[2] & _EMUX_OSS_MODE_FLAG)
		emuspec_control(emu, p, cmd, data, atomic, hop);
	else
		gusspec_control(emu, p, cmd, data, atomic, hop);
	return 0;
}


/*
 * OSS/AWE driver specific h/w controls
 */
static void
emuspec_control(struct snd_emux *emu, struct snd_emux_port *port, int cmd,
		unsigned char *event, int atomic, int hop)
{
	int voice;
	unsigned short p1;
	short p2;
	int i;
	struct snd_midi_channel *chan;

	voice = event[3];
	if (voice < 0 || voice >= port->chset.max_channels)
		chan = NULL;
	else
		chan = &port->chset.channels[voice];

	p1 = *(unsigned short *) &event[4];
	p2 = *(short *) &event[6];

	switch (cmd) {
#if 0 /* don't do this atomically */
	case _EMUX_OSS_REMOVE_LAST_SAMPLES:
		snd_soundfont_remove_unlocked(emu->sflist);
		break;
#endif
	case _EMUX_OSS_SEND_EFFECT:
		if (chan)
			snd_emux_send_effect_oss(port, chan, p1, p2);
		break;
		
	case _EMUX_OSS_TERMINATE_ALL:
		snd_emux_terminate_all(emu);
		break;

	case _EMUX_OSS_TERMINATE_CHANNEL:
		/*snd_emux_mute_channel(emu, chan);*/
		break;
	case _EMUX_OSS_RESET_CHANNEL:
		/*snd_emux_channel_init(chset, chan);*/
		break;

	case _EMUX_OSS_RELEASE_ALL:
		fake_event(emu, port, voice, MIDI_CTL_ALL_NOTES_OFF, 0, atomic, hop);
		break;
	case _EMUX_OSS_NOTEOFF_ALL:
		fake_event(emu, port, voice, MIDI_CTL_ALL_SOUNDS_OFF, 0, atomic, hop);
		break;

	case _EMUX_OSS_INITIAL_VOLUME:
		if (p2) {
			port->volume_atten = (short)p1;
			snd_emux_update_port(port, SNDRV_EMUX_UPDATE_VOLUME);
		}
		break;

	case _EMUX_OSS_CHN_PRESSURE:
		if (chan) {
			chan->midi_pressure = p1;
			snd_emux_update_channel(port, chan, SNDRV_EMUX_UPDATE_FMMOD|SNDRV_EMUX_UPDATE_FM2FRQ2);
		}
		break;

	case _EMUX_OSS_CHANNEL_MODE:
		reset_port_mode(port, p1);
		snd_emux_reset_port(port);
		break;

	case _EMUX_OSS_DRUM_CHANNELS:
		port->drum_flags = *(unsigned int*)&event[4];
		for (i = 0; i < port->chset.max_channels; i++) {
			chan = &port->chset.channels[i];
			chan->drum_channel = ((port->drum_flags >> i) & 1) ? 1 : 0;
		}
		break;

	case _EMUX_OSS_MISC_MODE:
		if (p1 < EMUX_MD_END)
			port->ctrls[p1] = p2;
		break;
	case _EMUX_OSS_DEBUG_MODE:
		break;

	default:
		if (emu->ops.oss_ioctl)
			emu->ops.oss_ioctl(emu, cmd, p1, p2);
		break;
	}
}

/*
 * GUS specific h/w controls
 */

#include <linux/ultrasound.h>

static void
gusspec_control(struct snd_emux *emu, struct snd_emux_port *port, int cmd,
		unsigned char *event, int atomic, int hop)
{
	int voice;
	unsigned short p1;
	int plong;
	struct snd_midi_channel *chan;

	if (port->port_mode != SNDRV_EMUX_PORT_MODE_OSS_SYNTH)
		return;
	if (cmd == _GUS_NUMVOICES)
		return;
	voice = event[3];
	if (voice < 0 || voice >= port->chset.max_channels)
		return;

	chan = &port->chset.channels[voice];

	p1 = *(unsigned short *) &event[4];
	plong = *(int*) &event[4];

	switch (cmd) {
	case _GUS_VOICESAMPLE:
		chan->midi_program = p1;
		return;

	case _GUS_VOICEBALA:
		/* 0 to 15 --> 0 to 127 */
		chan->control[MIDI_CTL_MSB_PAN] = (int)p1 << 3;
		snd_emux_update_channel(port, chan, SNDRV_EMUX_UPDATE_PAN);
		return;

	case _GUS_VOICEVOL:
	case _GUS_VOICEVOL2:
		/* not supported yet */
		return;

	case _GUS_RAMPRANGE:
	case _GUS_RAMPRATE:
	case _GUS_RAMPMODE:
	case _GUS_RAMPON:
	case _GUS_RAMPOFF:
		/* volume ramping not supported */
		return;

	case _GUS_VOLUME_SCALE:
		return;

	case _GUS_VOICE_POS:
#ifdef SNDRV_EMUX_USE_RAW_EFFECT
		snd_emux_send_effect(port, chan, EMUX_FX_SAMPLE_START,
				     (short)(plong & 0x7fff),
				     EMUX_FX_FLAG_SET);
		snd_emux_send_effect(port, chan, EMUX_FX_COARSE_SAMPLE_START,
				     (plong >> 15) & 0xffff,
				     EMUX_FX_FLAG_SET);
#endif
		return;
	}
}


/*
 * send an event to midi emulation
 */
static void
fake_event(struct snd_emux *emu, struct snd_emux_port *port, int ch, int param, int val, int atomic, int hop)
{
	struct snd_seq_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = SNDRV_SEQ_EVENT_CONTROLLER;
	ev.data.control.channel = ch;
	ev.data.control.param = param;
	ev.data.control.value = val;
	snd_emux_event_input(&ev, 0, port, atomic, hop);
}
