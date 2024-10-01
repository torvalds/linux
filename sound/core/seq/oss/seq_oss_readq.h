/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OSS compatible sequencer driver
 * read fifo queue
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
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
int snd_seq_oss_readq_sysex(struct seq_oss_readq *q, int dev,
			    struct snd_seq_event *ev);
int snd_seq_oss_readq_put_event(struct seq_oss_readq *readq, union evrec *ev);
int snd_seq_oss_readq_put_timestamp(struct seq_oss_readq *readq, unsigned long curt, int seq_mode);
int snd_seq_oss_readq_pick(struct seq_oss_readq *q, union evrec *rec);
void snd_seq_oss_readq_wait(struct seq_oss_readq *q);
void snd_seq_oss_readq_free(struct seq_oss_readq *q);

#define snd_seq_oss_readq_lock(q, flags) spin_lock_irqsave(&(q)->lock, flags)
#define snd_seq_oss_readq_unlock(q, flags) spin_unlock_irqrestore(&(q)->lock, flags)

#endif
