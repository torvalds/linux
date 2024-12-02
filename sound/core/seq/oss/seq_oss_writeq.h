/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OSS compatible sequencer driver
 * write priority queue
 *
 * Copyright (C) 1998,99 Takashi Iwai <tiwai@suse.de>
 */

#ifndef __SEQ_OSS_WRITEQ_H
#define __SEQ_OSS_WRITEQ_H

#include "seq_oss_device.h"


struct seq_oss_writeq {
	struct seq_oss_devinfo *dp;
	int maxlen;
	abstime_t sync_time;
	int sync_event_put;
	wait_queue_head_t sync_sleep;
	spinlock_t sync_lock;
};


/*
 * seq_oss_writeq.c
 */
struct seq_oss_writeq *snd_seq_oss_writeq_new(struct seq_oss_devinfo *dp, int maxlen);
void snd_seq_oss_writeq_delete(struct seq_oss_writeq *q);
void snd_seq_oss_writeq_clear(struct seq_oss_writeq *q);
int snd_seq_oss_writeq_sync(struct seq_oss_writeq *q);
void snd_seq_oss_writeq_wakeup(struct seq_oss_writeq *q, abstime_t time);
int snd_seq_oss_writeq_get_free_size(struct seq_oss_writeq *q);
void snd_seq_oss_writeq_set_output(struct seq_oss_writeq *q, int size);


#endif
