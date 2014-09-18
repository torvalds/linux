/*
 *  Main header file for the ALSA sequencer
 *  Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
 *            (c) 1998-1999 by Jaroslav Kysela <perex@perex.cz>
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
#ifndef __SOUND_ASEQUENCER_H
#define __SOUND_ASEQUENCER_H

#include <linux/ioctl.h>
#include <sound/asound.h>
#include <uapi/sound/asequencer.h>

/* helper macro */
#define snd_seq_event_bounce_ext_data(ev) ((void*)((char *)(ev)->data.ext.ptr + sizeof(struct snd_seq_event_bounce)))

/*
 * type check macros
 */
/* result events: 0-4 */
#define snd_seq_ev_is_result_type(ev)	((ev)->type < 5)
/* channel specific events: 5-19 */
#define snd_seq_ev_is_channel_type(ev)	((ev)->type >= 5 && (ev)->type < 20)
/* note events: 5-9 */
#define snd_seq_ev_is_note_type(ev)	((ev)->type >= 5 && (ev)->type < 10)
/* control events: 10-19 */
#define snd_seq_ev_is_control_type(ev)	((ev)->type >= 10 && (ev)->type < 20)
/* queue control events: 30-39 */
#define snd_seq_ev_is_queue_type(ev)	((ev)->type >= 30 && (ev)->type < 40)
/* system status messages */
#define snd_seq_ev_is_message_type(ev)	((ev)->type >= 60 && (ev)->type < 69)
/* sample messages */
#define snd_seq_ev_is_sample_type(ev)	((ev)->type >= 70 && (ev)->type < 79)
/* user-defined messages */
#define snd_seq_ev_is_user_type(ev)	((ev)->type >= 90 && (ev)->type < 99)
/* fixed length events: 0-99 */
#define snd_seq_ev_is_fixed_type(ev)	((ev)->type < 100)
/* variable length events: 130-139 */
#define snd_seq_ev_is_variable_type(ev)	((ev)->type >= 130 && (ev)->type < 140)
/* reserved for kernel */
#define snd_seq_ev_is_reserved(ev)	((ev)->type >= 150)

/* direct dispatched events */
#define snd_seq_ev_is_direct(ev)	((ev)->queue == SNDRV_SEQ_QUEUE_DIRECT)

/*
 * macros to check event flags
 */
/* prior events */
#define snd_seq_ev_is_prior(ev)		(((ev)->flags & SNDRV_SEQ_PRIORITY_MASK) == SNDRV_SEQ_PRIORITY_HIGH)

/* event length type */
#define snd_seq_ev_length_type(ev)	((ev)->flags & SNDRV_SEQ_EVENT_LENGTH_MASK)
#define snd_seq_ev_is_fixed(ev)		(snd_seq_ev_length_type(ev) == SNDRV_SEQ_EVENT_LENGTH_FIXED)
#define snd_seq_ev_is_variable(ev)	(snd_seq_ev_length_type(ev) == SNDRV_SEQ_EVENT_LENGTH_VARIABLE)
#define snd_seq_ev_is_varusr(ev)	(snd_seq_ev_length_type(ev) == SNDRV_SEQ_EVENT_LENGTH_VARUSR)

/* time-stamp type */
#define snd_seq_ev_timestamp_type(ev)	((ev)->flags & SNDRV_SEQ_TIME_STAMP_MASK)
#define snd_seq_ev_is_tick(ev)		(snd_seq_ev_timestamp_type(ev) == SNDRV_SEQ_TIME_STAMP_TICK)
#define snd_seq_ev_is_real(ev)		(snd_seq_ev_timestamp_type(ev) == SNDRV_SEQ_TIME_STAMP_REAL)

/* time-mode type */
#define snd_seq_ev_timemode_type(ev)	((ev)->flags & SNDRV_SEQ_TIME_MODE_MASK)
#define snd_seq_ev_is_abstime(ev)	(snd_seq_ev_timemode_type(ev) == SNDRV_SEQ_TIME_MODE_ABS)
#define snd_seq_ev_is_reltime(ev)	(snd_seq_ev_timemode_type(ev) == SNDRV_SEQ_TIME_MODE_REL)

/* queue sync port */
#define snd_seq_queue_sync_port(q)	((q) + 16)

#endif /* __SOUND_ASEQUENCER_H */
