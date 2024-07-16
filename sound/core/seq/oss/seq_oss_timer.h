/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OSS compatible sequencer driver
 * timer handling routines
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
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

#endif
