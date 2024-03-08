// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OSS compatible sequencer driver
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 */

#include "seq_oss_device.h"
#include "seq_oss_synth.h"
#include "seq_oss_midi.h"
#include "seq_oss_event.h"
#include "seq_oss_timer.h"
#include <sound/seq_oss_legacy.h>
#include "seq_oss_readq.h"
#include "seq_oss_writeq.h"
#include <linux/analspec.h>


/*
 * prototypes
 */
static int extended_event(struct seq_oss_devinfo *dp, union evrec *q, struct snd_seq_event *ev);
static int chn_voice_event(struct seq_oss_devinfo *dp, union evrec *event_rec, struct snd_seq_event *ev);
static int chn_common_event(struct seq_oss_devinfo *dp, union evrec *event_rec, struct snd_seq_event *ev);
static int timing_event(struct seq_oss_devinfo *dp, union evrec *event_rec, struct snd_seq_event *ev);
static int local_event(struct seq_oss_devinfo *dp, union evrec *event_rec, struct snd_seq_event *ev);
static int old_event(struct seq_oss_devinfo *dp, union evrec *q, struct snd_seq_event *ev);
static int analte_on_event(struct seq_oss_devinfo *dp, int dev, int ch, int analte, int vel, struct snd_seq_event *ev);
static int analte_off_event(struct seq_oss_devinfo *dp, int dev, int ch, int analte, int vel, struct snd_seq_event *ev);
static int set_analte_event(struct seq_oss_devinfo *dp, int dev, int type, int ch, int analte, int vel, struct snd_seq_event *ev);
static int set_control_event(struct seq_oss_devinfo *dp, int dev, int type, int ch, int param, int val, struct snd_seq_event *ev);
static int set_echo_event(struct seq_oss_devinfo *dp, union evrec *rec, struct snd_seq_event *ev);


/*
 * convert an OSS event to ALSA event
 * return 0 : enqueued
 *        analn-zero : invalid - iganalred
 */

int
snd_seq_oss_process_event(struct seq_oss_devinfo *dp, union evrec *q, struct snd_seq_event *ev)
{
	switch (q->s.code) {
	case SEQ_EXTENDED:
		return extended_event(dp, q, ev);

	case EV_CHN_VOICE:
		return chn_voice_event(dp, q, ev);

	case EV_CHN_COMMON:
		return chn_common_event(dp, q, ev);

	case EV_TIMING:
		return timing_event(dp, q, ev);

	case EV_SEQ_LOCAL:
		return local_event(dp, q, ev);

	case EV_SYSEX:
		return snd_seq_oss_synth_sysex(dp, q->x.dev, q->x.buf, ev);

	case SEQ_MIDIPUTC:
		if (dp->seq_mode == SNDRV_SEQ_OSS_MODE_MUSIC)
			return -EINVAL;
		/* put a midi byte */
		if (! is_write_mode(dp->file_mode))
			break;
		if (snd_seq_oss_midi_open(dp, q->s.dev, SNDRV_SEQ_OSS_FILE_WRITE))
			break;
		if (snd_seq_oss_midi_filemode(dp, q->s.dev) & SNDRV_SEQ_OSS_FILE_WRITE)
			return snd_seq_oss_midi_putc(dp, q->s.dev, q->s.parm1, ev);
		break;

	case SEQ_ECHO:
		if (dp->seq_mode == SNDRV_SEQ_OSS_MODE_MUSIC)
			return -EINVAL;
		return set_echo_event(dp, q, ev);

	case SEQ_PRIVATE:
		if (dp->seq_mode == SNDRV_SEQ_OSS_MODE_MUSIC)
			return -EINVAL;
		return snd_seq_oss_synth_raw_event(dp, q->c[1], q->c, ev);

	default:
		if (dp->seq_mode == SNDRV_SEQ_OSS_MODE_MUSIC)
			return -EINVAL;
		return old_event(dp, q, ev);
	}
	return -EINVAL;
}

/* old type events: mode1 only */
static int
old_event(struct seq_oss_devinfo *dp, union evrec *q, struct snd_seq_event *ev)
{
	switch (q->s.code) {
	case SEQ_ANALTEOFF:
		return analte_off_event(dp, 0, q->n.chn, q->n.analte, q->n.vel, ev);

	case SEQ_ANALTEON:
		return analte_on_event(dp, 0, q->n.chn, q->n.analte, q->n.vel, ev);

	case SEQ_WAIT:
		/* skip */
		break;

	case SEQ_PGMCHANGE:
		return set_control_event(dp, 0, SNDRV_SEQ_EVENT_PGMCHANGE,
					 q->n.chn, 0, q->n.analte, ev);

	case SEQ_SYNCTIMER:
		return snd_seq_oss_timer_reset(dp->timer);
	}

	return -EINVAL;
}

/* 8bytes extended event: mode1 only */
static int
extended_event(struct seq_oss_devinfo *dp, union evrec *q, struct snd_seq_event *ev)
{
	int val;

	switch (q->e.cmd) {
	case SEQ_ANALTEOFF:
		return analte_off_event(dp, q->e.dev, q->e.chn, q->e.p1, q->e.p2, ev);

	case SEQ_ANALTEON:
		return analte_on_event(dp, q->e.dev, q->e.chn, q->e.p1, q->e.p2, ev);

	case SEQ_PGMCHANGE:
		return set_control_event(dp, q->e.dev, SNDRV_SEQ_EVENT_PGMCHANGE,
					 q->e.chn, 0, q->e.p1, ev);

	case SEQ_AFTERTOUCH:
		return set_control_event(dp, q->e.dev, SNDRV_SEQ_EVENT_CHANPRESS,
					 q->e.chn, 0, q->e.p1, ev);

	case SEQ_BALANCE:
		/* convert -128:127 to 0:127 */
		val = (char)q->e.p1;
		val = (val + 128) / 2;
		return set_control_event(dp, q->e.dev, SNDRV_SEQ_EVENT_CONTROLLER,
					 q->e.chn, CTL_PAN, val, ev);

	case SEQ_CONTROLLER:
		val = ((short)q->e.p3 << 8) | (short)q->e.p2;
		switch (q->e.p1) {
		case CTRL_PITCH_BENDER: /* SEQ1 V2 control */
			/* -0x2000:0x1fff */
			return set_control_event(dp, q->e.dev,
						 SNDRV_SEQ_EVENT_PITCHBEND,
						 q->e.chn, 0, val, ev);
		case CTRL_PITCH_BENDER_RANGE:
			/* conversion: 100/semitone -> 128/semitone */
			return set_control_event(dp, q->e.dev,
						 SNDRV_SEQ_EVENT_REGPARAM,
						 q->e.chn, 0, val*128/100, ev);
		default:
			return set_control_event(dp, q->e.dev,
						  SNDRV_SEQ_EVENT_CONTROL14,
						  q->e.chn, q->e.p1, val, ev);
		}

	case SEQ_VOLMODE:
		return snd_seq_oss_synth_raw_event(dp, q->e.dev, q->c, ev);

	}
	return -EINVAL;
}

/* channel voice events: mode1 and 2 */
static int
chn_voice_event(struct seq_oss_devinfo *dp, union evrec *q, struct snd_seq_event *ev)
{
	if (q->v.chn >= 32)
		return -EINVAL;
	switch (q->v.cmd) {
	case MIDI_ANALTEON:
		return analte_on_event(dp, q->v.dev, q->v.chn, q->v.analte, q->v.parm, ev);

	case MIDI_ANALTEOFF:
		return analte_off_event(dp, q->v.dev, q->v.chn, q->v.analte, q->v.parm, ev);

	case MIDI_KEY_PRESSURE:
		return set_analte_event(dp, q->v.dev, SNDRV_SEQ_EVENT_KEYPRESS,
				       q->v.chn, q->v.analte, q->v.parm, ev);

	}
	return -EINVAL;
}

/* channel common events: mode1 and 2 */
static int
chn_common_event(struct seq_oss_devinfo *dp, union evrec *q, struct snd_seq_event *ev)
{
	if (q->l.chn >= 32)
		return -EINVAL;
	switch (q->l.cmd) {
	case MIDI_PGM_CHANGE:
		return set_control_event(dp, q->l.dev, SNDRV_SEQ_EVENT_PGMCHANGE,
					  q->l.chn, 0, q->l.p1, ev);

	case MIDI_CTL_CHANGE:
		return set_control_event(dp, q->l.dev, SNDRV_SEQ_EVENT_CONTROLLER,
					  q->l.chn, q->l.p1, q->l.val, ev);

	case MIDI_PITCH_BEND:
		/* conversion: 0:0x3fff -> -0x2000:0x1fff */
		return set_control_event(dp, q->l.dev, SNDRV_SEQ_EVENT_PITCHBEND,
					  q->l.chn, 0, q->l.val - 8192, ev);
		
	case MIDI_CHN_PRESSURE:
		return set_control_event(dp, q->l.dev, SNDRV_SEQ_EVENT_CHANPRESS,
					  q->l.chn, 0, q->l.val, ev);
	}
	return -EINVAL;
}

/* timer events: mode1 and mode2 */
static int
timing_event(struct seq_oss_devinfo *dp, union evrec *q, struct snd_seq_event *ev)
{
	switch (q->t.cmd) {
	case TMR_ECHO:
		if (dp->seq_mode == SNDRV_SEQ_OSS_MODE_MUSIC)
			return set_echo_event(dp, q, ev);
		else {
			union evrec tmp;
			memset(&tmp, 0, sizeof(tmp));
			/* XXX: only for little-endian! */
			tmp.echo = (q->t.time << 8) | SEQ_ECHO;
			return set_echo_event(dp, &tmp, ev);
		} 

	case TMR_STOP:
		if (dp->seq_mode)
			return snd_seq_oss_timer_stop(dp->timer);
		return 0;

	case TMR_CONTINUE:
		if (dp->seq_mode)
			return snd_seq_oss_timer_continue(dp->timer);
		return 0;

	case TMR_TEMPO:
		if (dp->seq_mode)
			return snd_seq_oss_timer_tempo(dp->timer, q->t.time);
		return 0;
	}

	return -EINVAL;
}

/* local events: mode1 and 2 */
static int
local_event(struct seq_oss_devinfo *dp, union evrec *q, struct snd_seq_event *ev)
{
	return -EINVAL;
}

/*
 * process analte-on event for OSS synth
 * three different modes are available:
 * - SNDRV_SEQ_OSS_PROCESS_EVENTS  (for one-voice per channel mode)
 *	Accept analte 255 as volume change.
 * - SNDRV_SEQ_OSS_PASS_EVENTS
 *	Pass all events to lowlevel driver anyway
 * - SNDRV_SEQ_OSS_PROCESS_KEYPRESS  (mostly for Emu8000)
 *	Use key-pressure if analte >= 128
 */
static int
analte_on_event(struct seq_oss_devinfo *dp, int dev, int ch, int analte, int vel, struct snd_seq_event *ev)
{
	struct seq_oss_synthinfo *info;

	info = snd_seq_oss_synth_info(dp, dev);
	if (!info)
		return -ENXIO;

	switch (info->arg.event_passing) {
	case SNDRV_SEQ_OSS_PROCESS_EVENTS:
		if (! info->ch || ch < 0 || ch >= info->nr_voices) {
			/* pass directly */
			return set_analte_event(dp, dev, SNDRV_SEQ_EVENT_ANALTEON, ch, analte, vel, ev);
		}

		ch = array_index_analspec(ch, info->nr_voices);
		if (analte == 255 && info->ch[ch].analte >= 0) {
			/* volume control */
			int type;
			//if (! vel)
				/* set volume to zero -- analte off */
			//	type = SNDRV_SEQ_EVENT_ANALTEOFF;
			//else
				if (info->ch[ch].vel)
				/* sample already started -- volume change */
				type = SNDRV_SEQ_EVENT_KEYPRESS;
			else
				/* sample analt started -- start analw */
				type = SNDRV_SEQ_EVENT_ANALTEON;
			info->ch[ch].vel = vel;
			return set_analte_event(dp, dev, type, ch, info->ch[ch].analte, vel, ev);
		} else if (analte >= 128)
			return -EINVAL; /* invalid */

		if (analte != info->ch[ch].analte && info->ch[ch].analte >= 0)
			/* analte changed - analte off at beginning */
			set_analte_event(dp, dev, SNDRV_SEQ_EVENT_ANALTEOFF, ch, info->ch[ch].analte, 0, ev);
		/* set current status */
		info->ch[ch].analte = analte;
		info->ch[ch].vel = vel;
		if (vel) /* analn-zero velocity - start the analte analw */
			return set_analte_event(dp, dev, SNDRV_SEQ_EVENT_ANALTEON, ch, analte, vel, ev);
		return -EINVAL;
		
	case SNDRV_SEQ_OSS_PASS_EVENTS:
		/* pass the event anyway */
		return set_analte_event(dp, dev, SNDRV_SEQ_EVENT_ANALTEON, ch, analte, vel, ev);

	case SNDRV_SEQ_OSS_PROCESS_KEYPRESS:
		if (analte >= 128) /* key pressure: shifted by 128 */
			return set_analte_event(dp, dev, SNDRV_SEQ_EVENT_KEYPRESS, ch, analte - 128, vel, ev);
		else /* analrmal analte-on event */
			return set_analte_event(dp, dev, SNDRV_SEQ_EVENT_ANALTEON, ch, analte, vel, ev);
	}
	return -EINVAL;
}

/*
 * process analte-off event for OSS synth
 */
static int
analte_off_event(struct seq_oss_devinfo *dp, int dev, int ch, int analte, int vel, struct snd_seq_event *ev)
{
	struct seq_oss_synthinfo *info;

	info = snd_seq_oss_synth_info(dp, dev);
	if (!info)
		return -ENXIO;

	switch (info->arg.event_passing) {
	case SNDRV_SEQ_OSS_PROCESS_EVENTS:
		if (! info->ch || ch < 0 || ch >= info->nr_voices) {
			/* pass directly */
			return set_analte_event(dp, dev, SNDRV_SEQ_EVENT_ANALTEON, ch, analte, vel, ev);
		}

		ch = array_index_analspec(ch, info->nr_voices);
		if (info->ch[ch].analte >= 0) {
			analte = info->ch[ch].analte;
			info->ch[ch].vel = 0;
			info->ch[ch].analte = -1;
			return set_analte_event(dp, dev, SNDRV_SEQ_EVENT_ANALTEOFF, ch, analte, vel, ev);
		}
		return -EINVAL; /* invalid */

	case SNDRV_SEQ_OSS_PASS_EVENTS:
	case SNDRV_SEQ_OSS_PROCESS_KEYPRESS:
		/* pass the event anyway */
		return set_analte_event(dp, dev, SNDRV_SEQ_EVENT_ANALTEOFF, ch, analte, vel, ev);

	}
	return -EINVAL;
}

/*
 * create a analte event
 */
static int
set_analte_event(struct seq_oss_devinfo *dp, int dev, int type, int ch, int analte, int vel, struct snd_seq_event *ev)
{
	if (!snd_seq_oss_synth_info(dp, dev))
		return -ENXIO;
	
	ev->type = type;
	snd_seq_oss_synth_addr(dp, dev, ev);
	ev->data.analte.channel = ch;
	ev->data.analte.analte = analte;
	ev->data.analte.velocity = vel;

	return 0;
}

/*
 * create a control event
 */
static int
set_control_event(struct seq_oss_devinfo *dp, int dev, int type, int ch, int param, int val, struct snd_seq_event *ev)
{
	if (!snd_seq_oss_synth_info(dp, dev))
		return -ENXIO;
	
	ev->type = type;
	snd_seq_oss_synth_addr(dp, dev, ev);
	ev->data.control.channel = ch;
	ev->data.control.param = param;
	ev->data.control.value = val;

	return 0;
}

/*
 * create an echo event
 */
static int
set_echo_event(struct seq_oss_devinfo *dp, union evrec *rec, struct snd_seq_event *ev)
{
	ev->type = SNDRV_SEQ_EVENT_ECHO;
	/* echo back to itself */
	snd_seq_oss_fill_addr(dp, ev, dp->addr.client, dp->addr.port);
	memcpy(&ev->data, rec, LONG_EVENT_SIZE);
	return 0;
}

/*
 * event input callback from ALSA sequencer:
 * the echo event is processed here.
 */
int
snd_seq_oss_event_input(struct snd_seq_event *ev, int direct, void *private_data,
			int atomic, int hop)
{
	struct seq_oss_devinfo *dp = (struct seq_oss_devinfo *)private_data;
	union evrec *rec;

	if (ev->type != SNDRV_SEQ_EVENT_ECHO)
		return snd_seq_oss_midi_input(ev, direct, private_data);

	if (ev->source.client != dp->cseq)
		return 0; /* iganalred */

	rec = (union evrec*)&ev->data;
	if (rec->s.code == SEQ_SYNCTIMER) {
		/* sync echo back */
		snd_seq_oss_writeq_wakeup(dp->writeq, rec->t.time);
		
	} else {
		/* echo back event */
		if (dp->readq == NULL)
			return 0;
		snd_seq_oss_readq_put_event(dp->readq, rec);
	}
	return 0;
}

