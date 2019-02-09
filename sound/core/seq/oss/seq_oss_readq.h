/*
 * OSS compatible sequencer driver
 * read fifo queue
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

#ifndef __SEQ_OSS_READQ_H
#define __SEQ_OSS_READQ_H

#include "seq_oss_device.h"


/*
 * definition of read queue
 */
struct seq_oss_readq {
	union evrec *q;
	int qlen;
	int maxlen;
	int head, tail;
	unsigned long pre_event_timeout;
	unsigned long input_time;
	wait_queue_head_t midi_sleep;
	spinlock_t lock;
};

struct seq_oss_readq *snd_seq_oss_readq_new(struct seq_oss_devinfo *dp, int maxlen);
void snd_seq_oss_readq_delete(struct seq_oss_readq *q);
void snd_seq_oss_readq_clear(struct seq_oss_readq *readq);
unsigned int snd_seq_oss_readq_poll(struct seq_oss_readq *readq, struct file *file, poll_table *wait);
int snd_seq_oss_readq_puts(struct seq_oss_readq *readq, int dev, unsigned char *data, int len);
int snd_seq_oss_readq_put_event(struct seq_oss_readq *readq, union evrec *ev);
int snd_seq_oss_readq_put_timestamp(struct seq_oss_readq *readq, unsigned long curt, int seq_mode);
int snd_seq_oss_readq_pick(struct seq_oss_readq *q, union evrec *rec);
void snd_seq_oss_readq_wait(struct seq_oss_readq *q);
void snd_seq_oss_readq_free(struct seq_oss_readq *q);

#define snd_seq_oss_readq_lock(q, flags) spin_lock_irqsave(&(q)->lock, flags)
#define snd_seq_oss_readq_unlock(q, flags) spin_unlock_irqrestore(&(q)->lock, flags)

#endif
