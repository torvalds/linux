/*
 * OSS compatible sequencer driver
 * write priority queue
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

#ifndef __SEQ_OSS_WRITEQ_H
#define __SEQ_OSS_WRITEQ_H

#include "seq_oss_device.h"


struct seq_oss_writeq_t {
	seq_oss_devinfo_t *dp;
	int maxlen;
	abstime_t sync_time;
	int sync_event_put;
	wait_queue_head_t sync_sleep;
	spinlock_t sync_lock;
};


/*
 * seq_oss_writeq.c
 */
seq_oss_writeq_t *snd_seq_oss_writeq_new(seq_oss_devinfo_t *dp, int maxlen);
void snd_seq_oss_writeq_delete(seq_oss_writeq_t *q);
void snd_seq_oss_writeq_clear(seq_oss_writeq_t *q);
int snd_seq_oss_writeq_sync(seq_oss_writeq_t *q);
void snd_seq_oss_writeq_wakeup(seq_oss_writeq_t *q, abstime_t time);
int snd_seq_oss_writeq_get_free_size(seq_oss_writeq_t *q);
void snd_seq_oss_writeq_set_output(seq_oss_writeq_t *q, int size);


#endif
