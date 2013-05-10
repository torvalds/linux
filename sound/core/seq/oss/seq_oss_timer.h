/*
 * OSS compatible sequencer driver
 * timer handling routines
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __SEQ_OSS_TIMER_H
#define __SEQ_OSS_TIMER_H

#include "seq_oss_device.h"

/*
 * timer information definition
 */
struct seq_oss_timer {
	struct seq_oss_devinfo *dp;
	reltime_t cur_tick;
	int realtime;
	int running;
	int tempo, ppq;	/* ALSA queue */
	int oss_tempo, oss_timebase;
};	


struct seq_oss_timer *snd_seq_oss_timer_new(struct seq_oss_devinfo *dp);
void snd_seq_oss_timer_delete(struct seq_oss_timer *dp);

int snd_seq_oss_timer_start(struct seq_oss_timer *timer);
int snd_seq_oss_timer_stop(struct seq_oss_timer *timer);
int snd_seq_oss_timer_continue(struct seq_oss_timer *timer);
int snd_seq_oss_timer_tempo(struct seq_oss_timer *timer, int value);
#define snd_seq_oss_timer_reset  snd_seq_oss_timer_start

int snd_seq_oss_timer_ioctl(struct seq_oss_timer *timer, unsigned int cmd, int __user *arg);

/*
 * get current processed time
 */
static inline abstime_t
snd_seq_oss_timer_cur_tick(struct seq_oss_timer *timer)
{
	return timer->cur_tick;
}


/*
 * is realtime event?
 */
static inline int
snd_seq_oss_timer_is_realtime(struct seq_oss_timer *timer)
{
	return timer->realtime;
}

#endif
